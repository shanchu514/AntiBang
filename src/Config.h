#pragma once

// LeviLamina 会用反射把 config.json 读进这个结构体，所以：
// 1) 必须是聚合类型（不能写构造函数）；
// 2) 必须有一个 int 类型的 version 成员（ll::config 靠它做版本迁移）。

#include <string>
#include <vector>

namespace antibang {

struct Config {
    int version = 1;

    // 总开关。关掉后只记录、不拦截。
    bool enabled = true;

    // true  = 在登录握手阶段直接拦死（最稳，客户端只会看到"连接超时/无法连接"）
    // false = 先放进来一个 tick 再踢，客户端能看到下面 kickMessage 里的原因
    bool hardBlock = true;

    // 三类封禁条件的开关（XUID 永远检查，不提供开关）。
    bool checkDevice = true;
    bool checkIp     = true;
    bool checkName   = false; // 按名字封禁容易误伤同名新号，默认关

    // /abban 时是否连带封设备码 / IP。
    bool banDevice = true;
    bool banIp     = true;

    // 连带封 IP 的有效期（小时）。<= 0 表示永久。
    // 手机热点/运营商 NAT 下大量玩家共用一个出口 IP，建议保留一个到期时间。
    int  ipBanTtlHours  = 72;
    // true = 封 IP 时按 /24 段封（更狠，也更容易误封同网吧/同小区玩家）
    bool ipBanUseSlash24 = false;

    // 新账号用了被封设备码/被封 IP 时，自动把这个新账号的 XUID 也封掉（封小号的核心）。
    bool autoBanAltXuid = true;

    // 管理员（OP）不受封禁影响，避免把自己锁在门外。
    bool exemptOps = true;

    // 是否往 history.tsv 追加登录记录（关掉就不做设备/IP 关联查询了）。
    bool logHistory = true;

    // 踢出消息，{reason} 会被替换成封禁原因。
    std::string kickMessage = "你已被本服封禁\n原因: {reason}\n如有疑问请联系管理员";

    // 白名单：这些 XUID / 名字永不被封禁（填名字时不分大小写）。
    std::vector<std::string> whitelistXuids;
    std::vector<std::string> whitelistNames;
};

} // namespace antibang
