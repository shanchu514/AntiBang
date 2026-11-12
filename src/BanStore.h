#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace antibang {

// 一次登录能拿到的全部身份信息。
struct Identity {
    std::string name;         // 游戏内名字
    std::string xuid;         // Xbox Live 账号 ID，最可靠的标识
    std::string uuid;         // 玩家 UUID（每个存档实体一个）
    std::string ip;           // 已剥掉端口的 IP
    std::string deviceId;     // 客户端登录包里的 DeviceId
    std::string selfSignedId; // 客户端自签名 ID，重装/清数据会变，但和 DeviceId 互补
};

// 一条封禁记录。
// type 取值：xuid / name / device / ip（ip 的值可以带 /24 表示网段）
struct Ban {
    std::string  type;
    std::string  value;
    std::int64_t createdAt = 0;
    std::int64_t expiresAt = 0; // 0 = 永久
    std::string  by;
    std::string  reason;
};

struct MatchOptions {
    bool name   = false;
    bool device = true;
    bool ip     = true;
};

class BanStore {
public:
    void load(std::filesystem::path const& file);
    bool save() const;

    [[nodiscard]] std::vector<Ban> const& all() const { return mBans; }

    // 返回清理掉的过期条目数
    std::size_t purgeExpired();

    // 已存在同 type + value 时返回 false
    bool add(Ban ban);

    // 删除所有 value 匹配（不分大小写）的记录，返回删除数量
    std::size_t removeValue(std::string const& value);

    [[nodiscard]] bool hasValue(std::string const& type, std::string const& value) const;

    [[nodiscard]] std::optional<Ban> match(Identity const& id, MatchOptions const& options) const;

private:
    std::filesystem::path mFile;
    std::vector<Ban>      mBans;
};

} // namespace antibang
