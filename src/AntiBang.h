#pragma once

#include "BanStore.h"
#include "Config.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "ll/api/mod/NativeMod.h"

class Player;
class CommandOutput;
class ConnectionRequest;

namespace antibang {

class AntiBang {
public:
    static AntiBang& getInstance();

    AntiBang() : mSelf(*ll::mod::NativeMod::current()) {}

    [[nodiscard]] ll::mod::NativeMod& getSelf() const { return mSelf; }

    bool load();
    bool enable();
    bool disable();

private:
    void registerListeners();
    void registerCommands();
    void registerCommandsNow();

    Identity makeIdentity(
        Player&                  player,
        std::string_view         ipHint  = {},
        ConnectionRequest const* request = nullptr
    ) const;
    std::optional<std::string> evaluate(
        Player&                  player,
        bool                     recordNow,
        bool                     autoBan,
        std::string_view         ipHint  = {},
        ConnectionRequest const* request = nullptr
    );
    bool                       isWhitelisted(Identity const& id) const;
    void                       record(Identity const& id, std::string const& status);
    void                       loadHistory();
    void                       alertOps(std::string const& text) const;
    std::string                buildKickMessage(std::string const& reason) const;

    std::optional<Identity> resolve(std::string const& target) const;
    Player*                 findOnline(std::string const& name) const;
    int                     applyBan(Identity const& id, std::string const& reason, std::string const& by, bool includeIp);

    void cmdBan(std::string const& target, std::string const& reason, std::string const& by, CommandOutput& out);
    void cmdUnban(std::string const& target, CommandOutput& out);
    void cmdCheck(std::string const& target, CommandOutput& out);
    void cmdList(CommandOutput& out);
    void cmdManual(
        std::string const& type,
        std::string const& value,
        std::string const& reason,
        std::string const& by,
        CommandOutput&     out
    );
    void cmdReload(CommandOutput& out);

    ll::mod::NativeMod& mSelf;

    Config mConfig;
    BanStore mStore;

    // 小写名字 -> 在线玩家指针，只用于命令定位，断开时移除
    std::unordered_map<std::string, Player*> mOnline;
    // 小写名字 / XUID -> 最近一次登录的身份信息
    std::unordered_map<std::string, Identity> mKnown;

    std::filesystem::path mHistoryFile;
    bool                  mCommandsRegistered = false;
};

} // namespace antibang
