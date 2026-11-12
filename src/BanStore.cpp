#include "BanStore.h"

#include "IpUtil.h"

#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace antibang {

namespace {

bool isValidType(std::string const& type) {
    return type == "xuid" || type == "name" || type == "device" || type == "ip";
}

std::string jsonString(nlohmann::json const& node, char const* key) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_string()) {
        return {};
    }
    return it->get<std::string>();
}

std::int64_t jsonInt(nlohmann::json const& node, char const* key) {
    auto it = node.find(key);
    if (it == node.end() || !it->is_number_integer()) {
        return 0;
    }
    return it->get<std::int64_t>();
}

} // namespace

void BanStore::load(std::filesystem::path const& file) {
    mFile = file;
    mBans.clear();

    std::ifstream in(mFile, std::ios::binary);
    if (!in) {
        return;
    }
    std::string content{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (content.empty()) {
        return;
    }

    auto root = nlohmann::json::parse(content, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return;
    }
    auto it = root.find("bans");
    if (it == root.end() || !it->is_array()) {
        return;
    }

    for (auto const& node : *it) {
        if (!node.is_object()) {
            continue;
        }
        Ban ban;
        ban.type      = jsonString(node, "type");
        ban.value     = jsonString(node, "value");
        ban.createdAt = jsonInt(node, "created_at");
        ban.expiresAt = jsonInt(node, "expires_at");
        ban.by        = jsonString(node, "by");
        ban.reason    = jsonString(node, "reason");
        if (ban.value.empty() || !isValidType(ban.type)) {
            continue;
        }
        mBans.push_back(std::move(ban));
    }
}

bool BanStore::save() const {
    if (mFile.empty()) {
        return false;
    }

    nlohmann::ordered_json root;
    root["version"] = 1;
    auto arr        = nlohmann::ordered_json::array();
    for (auto const& ban : mBans) {
        nlohmann::ordered_json node;
        node["type"]         = ban.type;
        node["value"]        = ban.value;
        node["created_at"]   = ban.createdAt;
        node["expires_at"]   = ban.expiresAt;
        node["by"]           = ban.by;
        node["reason"]       = ban.reason;
        arr.push_back(std::move(node));
    }
    root["bans"] = std::move(arr);

    std::ofstream out(mFile, std::ios::binary | std::ios::trunc);
    if (!out) {
        return false;
    }
    out << root.dump(2) << '\n';
    return static_cast<bool>(out);
}

std::size_t BanStore::purgeExpired() {
    auto const  now     = nowSeconds();
    std::size_t removed = 0;
    for (auto it = mBans.begin(); it != mBans.end();) {
        if (it->expiresAt != 0 && it->expiresAt <= now) {
            it = mBans.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

bool BanStore::add(Ban ban) {
    if (ban.value.empty() || !isValidType(ban.type)) {
        return false;
    }
    for (auto const& existing : mBans) {
        if (existing.type == ban.type && iequals(existing.value, ban.value)) {
            return false;
        }
    }
    mBans.push_back(std::move(ban));
    return true;
}

std::size_t BanStore::removeValue(std::string const& value) {
    std::string needle{trim(value)};
    if (needle.empty()) {
        return 0;
    }
    std::string stripped = stripPort(needle);

    std::size_t removed = 0;
    for (auto it = mBans.begin(); it != mBans.end();) {
        if (iequals(it->value, needle) || (!stripped.empty() && iequals(it->value, stripped))) {
            it = mBans.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

bool BanStore::hasValue(std::string const& type, std::string const& value) const {
    if (value.empty()) {
        return false;
    }
    for (auto const& ban : mBans) {
        if (ban.type == type && iequals(ban.value, value)) {
            return true;
        }
    }
    return false;
}

std::optional<Ban> BanStore::match(Identity const& id, MatchOptions const& options) const {
    auto const now = nowSeconds();

    for (auto const& ban : mBans) {
        if (ban.expiresAt != 0 && ban.expiresAt <= now) {
            continue;
        }

        if (ban.type == "xuid") {
            if (!id.xuid.empty() && ban.value == id.xuid) {
                return ban;
            }
            continue;
        }

        if (ban.type == "name") {
            if (options.name && iequals(ban.value, id.name)) {
                return ban;
            }
            continue;
        }

        if (ban.type == "device") {
            if (options.device && (iequals(ban.value, id.deviceId) || iequals(ban.value, id.selfSignedId))) {
                return ban;
            }
            continue;
        }

        if (ban.type == "ip") {
            if (!options.ip || id.ip.empty()) {
                continue;
            }
            if (ban.value.find('/') != std::string::npos) {
                if (ipv4InCidr(id.ip, ban.value)) {
                    return ban;
                }
            } else if (iequals(ban.value, id.ip)) {
                return ban;
            }
        }
    }
    return std::nullopt;
}

} // namespace antibang
