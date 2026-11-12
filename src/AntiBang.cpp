#include "AntiBang.h"

#include "IpUtil.h"

#include "ll/api/Config.h"
#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"
#include "ll/api/event/EventBus.h"
#include "ll/api/event/player/PlayerConnectEvent.h"
#include "ll/api/event/player/PlayerDisconnectEvent.h"
#include "ll/api/event/player/PlayerJoinEvent.h"
#include "ll/api/io/Logger.h"
#include "ll/api/mod/RegisterHelper.h"

#include "mc/network/ConnectionRequest.h"
#include "mc/network/NetworkIdentifier.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/world/actor/player/Player.h"

// LL 26.x 把服务端事件搬到了 src-server 目录，但 include 路径不变；
// 老版本（如 1.4.x，对应 BDS 1.21.93）不一定有这个事件，用 __has_include 兼容。
#if __has_include("ll/api/event/command/ServerCommandRegisterEvent.h")
#include "ll/api/event/command/ServerCommandRegisterEvent.h"
#define ANTIBANG_HAS_COMMAND_REGISTER_EVENT 1
#endif

#include <fstream>
#include <iterator>
#include <system_error>

namespace antibang {

// 命令参数结构体：成员名必须和 required()/optional() 里的名字完全一致。
// 注意：不能放进匿名 namespace —— LL 的 pfr 反射要求类型有外部链接，
// 否则会报 "fake_object<T> ... does not have linkage"。
struct TargetParam {
    std::string target;
};

struct TargetReasonParam {
    std::string target;
    std::string reason;
};

struct ValueParam {
    std::string value;
};

struct ValueReasonParam {
    std::string value;
    std::string reason;
};

namespace {

// 防止玩家名/原因里的制表符、换行把 history.tsv 撑坏
std::string sanitize(std::string_view value) {
    std::string out(value);
    for (auto& c : out) {
        if (c == '\t' || c == '\r' || c == '\n') {
            c = ' ';
        }
    }
    return out;
}

bool isDigits(std::string_view s) {
    if (s.empty()) {
        return false;
    }
    for (char c : s) {
        if (c < '0' || c > '9') {
            return false;
        }
    }
    return true;
}

std::string expiresText(Ban const& ban) {
    if (ban.expiresAt == 0) {
        return "永久";
    }
    auto left = ban.expiresAt - nowSeconds();
    if (left <= 0) {
        return "已过期";
    }
    return "剩余 " + std::to_string(left / 3600) + " 小时";
}

// 老版本 LL 的 CommandRegistrar::getInstance() 不带参数，新版本要求传 bool（是否客户端）。
// 用 requires 在编译期选一个能编过的写法，省得为两个版本维护两份代码。
template <class T>
T& commandRegistrar() {
    if constexpr (requires { T::getInstance(true); }) {
        return T::getInstance(false);
    } else {
        return T::getInstance();
    }
}

} // namespace

AntiBang& AntiBang::getInstance() {
    static AntiBang instance;
    return instance;
}

bool AntiBang::load() {
    auto& logger = getSelf().getLogger();

    std::error_code ec;
    std::filesystem::create_directories(getSelf().getConfigDir(), ec);
    std::filesystem::create_directories(getSelf().getDataDir(), ec);

    auto configPath = getSelf().getConfigDir() / "config.json";
    if (!ll::config::loadConfig(mConfig, configPath)) {
        logger.warn("[AntiBang] 配置文件已新建或升级: {}", configPath.string());
    }

    mStore.load(getSelf().getDataDir() / "bans.json");
    if (auto const purged = mStore.purgeExpired(); purged > 0) {
        logger.info("[AntiBang] 已清理 {} 条过期封禁", purged);
        mStore.save();
    }

    mHistoryFile = getSelf().getDataDir() / "history.tsv";
    loadHistory();

    logger.info("[AntiBang] 加载完成，当前 {} 条封禁记录", mStore.all().size());
    return true;
}

bool AntiBang::enable() {
    registerListeners();
    registerCommands();
    getSelf().getLogger().info("[AntiBang] 已启用");
    return true;
}

bool AntiBang::disable() {
    mOnline.clear();
    getSelf().getLogger().info("[AntiBang] 已停用");
    return true;
}

void AntiBang::registerListeners() {
    auto& bus = ll::event::EventBus::getInstance();

    // 登录握手阶段：拿得到 IP、DeviceId、XUID，可以在这里直接拒绝登录。
    // 这里刻意不调用 disconnect（此时玩家还没真正进服，调踢人接口有风险），
    // 只 cancel：客户端会看到"无法连接"，但最稳。
    bus.emplaceListener<ll::event::player::PlayerConnectEvent>([this](ll::event::player::PlayerConnectEvent& event) {
        auto& player = event.self();
        auto  ip     = event.networkIdentifier().getAddress();
        if (evaluate(player, true, true, ip, &event.connectionRequest()).has_value() && mConfig.hardBlock) {
            event.cancel();
        }
    });

    // 兜底：万一某些版本取消登录不生效，人已经进来了，就在入服瞬间踢掉
    bus.emplaceListener<ll::event::player::PlayerJoinEvent>([this](ll::event::player::PlayerJoinEvent& event) {
        auto& player = event.self();
        auto  ip     = player.getNetworkIdentifier().getAddress();
        if (auto reason = evaluate(player, false, false, ip)) {
            // ServerPlayer 在 26.x 里自带一个无参 disconnect()，会隐藏 Player::disconnect(string_view)，
            // 这里显式按 Player 调用带原因的重载。
            static_cast<Player&>(player).disconnect(buildKickMessage(*reason));
            return;
        }
        mOnline[toLower(player.getRealName())] = &player;
    });

    bus.emplaceListener<ll::event::player::PlayerDisconnectEvent>(
        [this](ll::event::player::PlayerDisconnectEvent& event) {
            auto& player = event.self();
            auto  key    = toLower(player.getRealName());
            auto  it     = mOnline.find(key);
            if (it != mOnline.end() && it->second == &player) {
                mOnline.erase(it);
            }
        }
    );
}

void AntiBang::registerCommands() {
#ifdef ANTIBANG_HAS_COMMAND_REGISTER_EVENT
    // 新版本要求等服务器把原版命令注册完再注册自定义命令
    ll::event::EventBus::getInstance().emplaceListener<ll::event::command::ServerCommandRegisterEvent>(
        [this](ll::event::command::ServerCommandRegisterEvent&) { registerCommandsNow(); }
    );
#else
    // 老版本在 enable() 里注册即可
    registerCommandsNow();
#endif
}

void AntiBang::registerCommandsNow() {
    if (mCommandsRegistered) {
        return;
    }
    mCommandsRegistered = true;

    auto& registrar = commandRegistrar<ll::command::CommandRegistrar>();

    auto& ban = registrar.getOrCreateCommand(
        "abban",
        "封禁玩家：XUID + 设备码 + IP 一起封",
        CommandPermissionLevel::GameDirectors
    );
    ban.overload<TargetParam>().required("target").execute(
        [this](CommandOrigin const& origin, CommandOutput& out, TargetParam const& param) {
            cmdBan(param.target, "", origin.getName(), out);
        }
    );
    ban.overload<TargetReasonParam>().required("target").required("reason").execute(
        [this](CommandOrigin const& origin, CommandOutput& out, TargetReasonParam const& param) {
            cmdBan(param.target, param.reason, origin.getName(), out);
        }
    );

    auto& unban = registrar.getOrCreateCommand(
        "abunban",
        "解除封禁：XUID / 设备码 / IP / 玩家名都可以",
        CommandPermissionLevel::GameDirectors
    );
    unban.overload<ValueParam>().required("value").execute(
        [this](CommandOrigin const&, CommandOutput& out, ValueParam const& param) { cmdUnban(param.value, out); }
    );

    auto& check = registrar.getOrCreateCommand(
        "abcheck",
        "查看某玩家的设备码 / IP / 封禁状态",
        CommandPermissionLevel::GameDirectors
    );
    check.overload<TargetParam>().required("target").execute(
        [this](CommandOrigin const&, CommandOutput& out, TargetParam const& param) { cmdCheck(param.target, out); }
    );

    auto& list = registrar.getOrCreateCommand(
        "ablist",
        "列出封禁记录",
        CommandPermissionLevel::GameDirectors
    );
    list.overload().execute([this](CommandOrigin const&, CommandOutput& out) { cmdList(out); });

    auto& ip = registrar.getOrCreateCommand(
        "abip",
        "手动封 IP，支持 1.2.3.4 或 1.2.3.0/24",
        CommandPermissionLevel::GameDirectors
    );
    ip.overload<ValueParam>().required("value").execute(
        [this](CommandOrigin const& origin, CommandOutput& out, ValueParam const& param) {
            cmdManual("ip", param.value, "手动封禁 IP", origin.getName(), out);
        }
    );
    ip.overload<ValueReasonParam>().required("value").required("reason").execute(
        [this](CommandOrigin const& origin, CommandOutput& out, ValueReasonParam const& param) {
            cmdManual("ip", param.value, param.reason, origin.getName(), out);
        }
    );

    auto& dev = registrar.getOrCreateCommand(
        "abdev",
        "手动封设备码",
        CommandPermissionLevel::GameDirectors
    );
    dev.overload<ValueParam>().required("value").execute(
        [this](CommandOrigin const& origin, CommandOutput& out, ValueParam const& param) {
            cmdManual("device", param.value, "手动封禁设备", origin.getName(), out);
        }
    );
    dev.overload<ValueReasonParam>().required("value").required("reason").execute(
        [this](CommandOrigin const& origin, CommandOutput& out, ValueReasonParam const& param) {
            cmdManual("device", param.value, param.reason, origin.getName(), out);
        }
    );

    auto& name = registrar.getOrCreateCommand(
        "abname",
        "按玩家名封禁（容易误伤，优先用 /abban）",
        CommandPermissionLevel::GameDirectors
    );
    name.overload<ValueParam>().required("value").execute(
        [this](CommandOrigin const& origin, CommandOutput& out, ValueParam const& param) {
            cmdManual("name", param.value, "手动封禁名字", origin.getName(), out);
        }
    );
    name.overload<ValueReasonParam>().required("value").required("reason").execute(
        [this](CommandOrigin const& origin, CommandOutput& out, ValueReasonParam const& param) {
            cmdManual("name", param.value, param.reason, origin.getName(), out);
        }
    );

    auto& reload = registrar.getOrCreateCommand(
        "abreload",
        "重新加载配置和封禁列表",
        CommandPermissionLevel::GameDirectors
    );
    reload.overload().execute([this](CommandOrigin const&, CommandOutput& out) { cmdReload(out); });

    getSelf().getLogger().info("[AntiBang] 命令已注册");
}

Identity AntiBang::makeIdentity(Player& player, std::string_view ipHint, ConnectionRequest const* request) const {
    Identity id;
    id.name = player.getRealName();
    id.xuid = player.getXuid();
    id.uuid = player.getUuid().asString();

    // 登录握手阶段直接从 NetworkIdentifier 取地址最准；取不到再退回玩家对象上的接口
    id.ip = stripPort(ipHint);
    if (id.ip.empty()) {
        id.ip = stripPort(player.getIPAndPort());
    }

    if (request == nullptr) {
        request = player.getConnectionRequest().as_ptr();
    }
    if (request != nullptr) {
        id.deviceId     = request->getDeviceId();
        id.selfSignedId = request->getSelfSignedId();
    }
    return id;
}

std::optional<std::string> AntiBang::evaluate(
    Player&                  player,
    bool                     recordNow,
    bool                     autoBan,
    std::string_view         ipHint,
    ConnectionRequest const* request
) {
    if (!mConfig.enabled) {
        return std::nullopt;
    }

    Identity id = makeIdentity(player, ipHint, request);

    if (isWhitelisted(id)) {
        if (recordNow) {
            record(id, "WHITELIST");
        }
        return std::nullopt;
    }
    if (mConfig.exemptOps && player.isOperator()) {
        if (recordNow) {
            record(id, "OP");
        }
        return std::nullopt;
    }

    auto hit = mStore.match(id, MatchOptions{mConfig.checkName, mConfig.checkDevice, mConfig.checkIp});
    if (!hit) {
        if (recordNow) {
            record(id, "OK");
        }
        return std::nullopt;
    }

    if (recordNow) {
        record(id, "BLOCKED:" + hit->type);
    }

    // 设备码/IP 命中但 XUID 没封 → 说明是新开的小号，顺手把这个账号也封掉
    if (autoBan && mConfig.autoBanAltXuid && (hit->type == "device" || hit->type == "ip") && !id.xuid.empty()
        && !mStore.hasValue("xuid", id.xuid)) {
        mStore.add(
            Ban{"xuid", id.xuid, nowSeconds(), 0, "AntiBang", "关联封禁: 与已封设备/IP 相同（" + hit->reason + "）"}
        );
        mStore.save();
        getSelf().getLogger().warn("[AntiBang] 小号 {} (XUID {}) 命中已封{}，已自动封禁该账号",
                                   id.name,
                                   id.xuid,
                                   hit->type == "device" ? "设备码" : "IP");
    }

    alertOps("拦截 " + id.name + " (" + id.ip + ")，原因: " + hit->reason);
    getSelf().getLogger().info(
        "[AntiBang] 拒绝 {} 登录：命中 {}={}，原因 {}",
        id.name,
        hit->type,
        hit->value,
        hit->reason
    );
    return hit->reason;
}

bool AntiBang::isWhitelisted(Identity const& id) const {
    for (auto const& xuid : mConfig.whitelistXuids) {
        if (!xuid.empty() && xuid == id.xuid) {
            return true;
        }
    }
    for (auto const& name : mConfig.whitelistNames) {
        if (!name.empty() && iequals(name, id.name)) {
            return true;
        }
    }
    return false;
}

void AntiBang::record(Identity const& id, std::string const& status) {
    mKnown[toLower(id.name)] = id;
    if (!id.xuid.empty()) {
        mKnown[toLower(id.xuid)] = id;
    }

    if (!mConfig.logHistory || mHistoryFile.empty()) {
        return;
    }

    std::ofstream out(mHistoryFile, std::ios::binary | std::ios::app);
    if (!out) {
        return;
    }
    out << nowSeconds() << '\t' << status << '\t' << sanitize(id.name) << '\t' << id.xuid << '\t' << id.ip << '\t'
        << id.deviceId << '\t' << id.selfSignedId << '\t' << id.uuid << '\n';
}

void AntiBang::loadHistory() {
    mKnown.clear();
    if (mHistoryFile.empty()) {
        return;
    }

    std::ifstream in(mHistoryFile, std::ios::binary);
    if (!in) {
        return;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        auto fields = splitTabs(line);
        if (fields.size() < 5) {
            continue;
        }

        // ts / status / name / xuid / ip / deviceId / selfSignedId / uuid
        Identity id;
        id.name = std::string(fields[2]);
        id.xuid = std::string(fields[3]);
        id.ip   = std::string(fields[4]);
        if (fields.size() > 5) {
            id.deviceId = std::string(fields[5]);
        }
        if (fields.size() > 6) {
            id.selfSignedId = std::string(fields[6]);
        }
        if (fields.size() > 7) {
            id.uuid = std::string(fields[7]);
        }
        if (id.name.empty()) {
            continue;
        }
        mKnown[toLower(id.name)] = id;
        if (!id.xuid.empty()) {
            mKnown[toLower(id.xuid)] = id;
        }
    }
}

void AntiBang::alertOps(std::string const& text) const {
    for (auto const& entry : mOnline) {
        auto* player = entry.second;
        if (player != nullptr && player->isOperator()) {
            player->sendMessage("§c[AntiBang] §f" + text);
        }
    }
}

std::string AntiBang::buildKickMessage(std::string const& reason) const {
    std::string message = mConfig.kickMessage;
    auto        pos     = message.find("{reason}");
    if (pos != std::string::npos) {
        message.replace(pos, 8, reason);
    }
    return message;
}

std::optional<Identity> AntiBang::resolve(std::string const& target) const {
    std::string key = toLower(trim(target));
    if (key.empty()) {
        return std::nullopt;
    }

    if (auto* player = findOnline(key); player != nullptr) {
        return makeIdentity(*player);
    }
    if (auto it = mKnown.find(key); it != mKnown.end()) {
        return it->second;
    }
    if (isDigits(key)) {
        Identity id;
        id.xuid = key;
        return id;
    }
    return std::nullopt;
}

Player* AntiBang::findOnline(std::string const& name) const {
    auto it = mOnline.find(toLower(name));
    return it == mOnline.end() ? nullptr : it->second;
}

int AntiBang::applyBan(Identity const& id, std::string const& reason, std::string const& by, bool includeIp) {
    auto const now = nowSeconds();
    int        added = 0;

    auto addOne = [&](std::string type, std::string value, std::int64_t expiresAt) {
        if (value.empty()) {
            return;
        }
        if (mStore.add(Ban{std::move(type), std::move(value), now, expiresAt, by, reason})) {
            ++added;
        }
    };

    addOne("xuid", id.xuid, 0);
    if (mConfig.banDevice) {
        addOne("device", id.deviceId, 0);
        addOne("device", id.selfSignedId, 0);
    }
    if (!id.name.empty()) {
        // 名字只作为辅助线索记录，不参与默认匹配（checkName 默认 false）
        addOne("name", id.name, 0);
    }
    if (includeIp && mConfig.banIp && !id.ip.empty()) {
        std::int64_t expiresAt = 0;
        if (mConfig.ipBanTtlHours > 0) {
            expiresAt = now + static_cast<std::int64_t>(mConfig.ipBanTtlHours) * 3600;
        }
        addOne("ip", mConfig.ipBanUseSlash24 ? toSlash24(id.ip) : id.ip, expiresAt);
    }

    mStore.save();
    return added;
}

void AntiBang::cmdBan(
    std::string const& target,
    std::string const& reason,
    std::string const& by,
    CommandOutput&     out
) {
    auto id = resolve(target);
    if (!id) {
        out.error("找不到玩家 " + target + "：请先让该玩家进一次服，或用 /abip、/abdev 手动封禁");
        return;
    }

    auto why   = sanitize(reason.empty() ? std::string("未填写原因") : reason);
    auto staff = sanitize(by);
    int  added = applyBan(*id, why, staff, true);

    if (auto* player = findOnline(id->name); player != nullptr) {
        player->disconnect(buildKickMessage(why));
    }

    out.success("已封禁 " + id->name + "，新增 " + std::to_string(added) + " 条记录");
    getSelf().getLogger().info(
        "[AntiBang] {} 封禁了 {}（XUID {}，IP {}，设备 {}），原因: {}",
        staff,
        id->name,
        id->xuid,
        id->ip,
        id->deviceId,
        why
    );
}

void AntiBang::cmdUnban(std::string const& target, CommandOutput& out) {
    std::size_t removed = mStore.removeValue(target);
    if (auto id = resolve(target)) {
        removed += mStore.removeValue(id->xuid);
        removed += mStore.removeValue(id->deviceId);
        removed += mStore.removeValue(id->selfSignedId);
        removed += mStore.removeValue(id->ip);
    }

    if (removed == 0) {
        out.error("没有找到匹配 " + target + " 的封禁记录");
        return;
    }
    mStore.save();
    out.success("已解除 " + std::to_string(removed) + " 条封禁记录");
}

void AntiBang::cmdCheck(std::string const& target, CommandOutput& out) {
    auto id = resolve(target);
    if (!id) {
        out.error("没有 " + target + " 的记录：先让该玩家进一次服，或者用 /abip 直接查 IP");
        return;
    }

    out.success("名字: " + id->name);
    out.success("XUID: " + (id->xuid.empty() ? "(空)" : id->xuid));
    out.success("IP: " + (id->ip.empty() ? "(空)" : id->ip));
    out.success("设备码: " + (id->deviceId.empty() ? "(空)" : id->deviceId));
    out.success("自签名ID: " + (id->selfSignedId.empty() ? "(空)" : id->selfSignedId));

    if (auto hit = mStore.match(*id, MatchOptions{mConfig.checkName, mConfig.checkDevice, mConfig.checkIp})) {
        out.success("状态: 已封禁 [" + hit->type + "] " + hit->value + " / " + expiresText(*hit) + " / 原因: " + hit->reason);
    } else {
        out.success("状态: 正常");
    }
}

void AntiBang::cmdList(CommandOutput& out) {
    auto const& bans = mStore.all();
    out.success("共 " + std::to_string(bans.size()) + " 条封禁记录");

    std::size_t shown = 0;
    for (auto const& ban : bans) {
        if (shown >= 20) {
            out.success("... 只显示前 20 条，完整内容见 data/bans.json");
            break;
        }
        ++shown;
        out.success("[" + ban.type + "] " + ban.value + " | " + expiresText(ban) + " | " + ban.reason);
    }
}

void AntiBang::cmdManual(
    std::string const& type,
    std::string const& value,
    std::string const& reason,
    std::string const& by,
    CommandOutput&     out
) {
    auto item = sanitize(trim(value));
    if (item.empty()) {
        out.error("值不能为空");
        return;
    }

    std::int64_t expiresAt = 0;
    if (type == "ip" && mConfig.ipBanTtlHours > 0) {
        expiresAt = nowSeconds() + static_cast<std::int64_t>(mConfig.ipBanTtlHours) * 3600;
    }

    bool added = mStore.add(Ban{type, item, nowSeconds(), expiresAt, sanitize(by), sanitize(reason)});
    mStore.save();

    if (added) {
        out.success("已添加 [" + type + "] 封禁: " + item);
    } else {
        out.success("该记录已存在: " + item);
    }
}

void AntiBang::cmdReload(CommandOutput& out) {
    auto configPath = getSelf().getConfigDir() / "config.json";
    ll::config::loadConfig(mConfig, configPath);
    mStore.load(getSelf().getDataDir() / "bans.json");
    mStore.purgeExpired();
    loadHistory();
    out.success("已重新加载，当前 " + std::to_string(mStore.all().size()) + " 条封禁记录");
}

} // namespace antibang

LL_REGISTER_MOD(antibang::AntiBang, antibang::AntiBang::getInstance());
