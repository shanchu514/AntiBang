/**
 * AntiBang.js —— BDS 防熊：记全服玩家信息 + 封 XUID / 设备码 / IP / 名字
 *
 * 运行环境：LeviLamina + LegacyScriptEngine（QuickJS 引擎）
 *   lip install github.com/LiteLDev/LegacyScriptEngine#quickjs
 *
 * 安装：把本文件放到服务端  plugins/AntiBang.js
 *       启动一次 -> 引擎会自动生成插件清单目录 -> 再启动一次生效
 * 数据：plugins/AntiBang/bans.json     封禁列表（可直接手改，改完 /abreload）
 *       plugins/AntiBang/history.tsv  登录历史：每个进服的人一条，含名字/设备码/IP
 *
 * 命令（OP 可用，控制台也能用）：
 *   /abban <玩家名或XUID> [原因]   封禁：XUID + 设备码 + IP（+ 名字）一起封，在线立刻踢
 *   /abname <名字> [原因]          直接封名字，并连带封掉他的 XUID / 设备码 / IP
 *   /abcheck <名字或XUID>         查这个人：进服次数、首次/最后时间、设备码、IP、封没封
 *   /abfind [关键字]              查谁进过服（不给关键字就列最近 20 个），按名字/XUID/IP/设备码模糊搜
 *   /abunban <XUID|设备码|IP|名字> 解封
 *   /ablist                        列出封禁记录
 *   /abip <IP或网段> [原因]        手动封 IP，支持 1.2.3.4 和 1.2.3.0/24
 *   /abdev <设备码> [原因]         手动封设备码
 *   /abreload                      重新读取 bans.json 和 history.tsv
 */

// ======== 可以改的配置 ========
const CFG = {
    checkDevice: true,      // 按设备码封禁
    checkIp: true,          // 按 IP 封禁
    checkName: true,        // 按名字封禁（/abname 用得上；名字在基岩版是账号唯一的）

    banDevice: true,        // /abban 时连设备码一起封
    banIp: true,            // /abban 时连 IP 一起封
    banName: true,          // /abban 时也记一条名字封禁
    ipBanHours: 72,         // 连带的 IP 封禁多少小时后失效，0 = 永久

    autoBanAlt: true,       // 小号用已封设备/IP/名字进来时，自动把这个账号也封掉

    kickMsg: "你已被本服封禁\n原因: {reason}\n如有疑问请联系管理员"
};
// ==============================

const DATA_DIR = "plugins/AntiBang";
const BAN_FILE = DATA_DIR + "/bans.json";
const HISTORY_FILE = DATA_DIR + "/history.tsv";

let bans = [];   // [{type, value, createdAt, expiresAt, by, reason}]
let known = {};  // 小写名字 / 小写xuid -> {name,xuid,uuid,ip,clientId,os,firstSeen,lastSeen,joins}

// ---------- 小工具 ----------

function nowSec() {
    return Math.floor(Date.now() / 1000);
}

function pad2(n) {
    return n < 10 ? "0" + n : "" + n;
}

function fmtTime(ts) {
    if (!ts) return "-";
    const d = new Date(ts * 1000);
    return d.getFullYear() + "-" + pad2(d.getMonth() + 1) + "-" + pad2(d.getDate()) + " " +
        pad2(d.getHours()) + ":" + pad2(d.getMinutes());
}

function eqi(a, b) {
    return String(a || "").toLowerCase() === String(b || "").toLowerCase();
}

function stripPort(raw) {
    let s = String(raw || "").trim();
    if (!s) return "";
    const bar = s.indexOf("|");
    if (bar >= 0) return s.substring(0, bar);
    if (s.charAt(0) === "[") {
        const end = s.indexOf("]");
        if (end > 0) return s.substring(1, end);
    }
    const colons = (s.match(/:/g) || []).length;
    if (colons === 1) return s.substring(0, s.indexOf(":"));
    return s;
}

function ipv4ToInt(s) {
    const p = String(s || "").split(".");
    if (p.length !== 4) return null;
    let v = 0;
    for (let i = 0; i < 4; i++) {
        if (!/^\d{1,3}$/.test(p[i])) return null;
        const n = parseInt(p[i], 10);
        if (n > 255) return null;
        v = (v * 256) + n;
    }
    return v;
}

function ipv4InCidr(ip, cidr) {
    const slash = String(cidr).indexOf("/");
    if (slash < 0) return false;
    const net = ipv4ToInt(cidr.substring(0, slash));
    const addr = ipv4ToInt(ip);
    if (net === null || addr === null) return false;
    const prefix = parseInt(cidr.substring(slash + 1), 10);
    if (isNaN(prefix) || prefix < 0 || prefix > 32) return false;
    if (prefix === 0) return true;
    const mask = prefix === 32 ? 0xFFFFFFFF : ((0xFFFFFFFF << (32 - prefix)) >>> 0);
    return ((addr & mask) >>> 0) === ((net & mask) >>> 0);
}

function safeDir() {
    try {
        if (typeof file.mkdir === "function") file.mkdir(DATA_DIR);
    } catch (e) {
        // 目录已存在时会报错，忽略
    }
}

function appendLine(path, line) {
    try {
        if (typeof file.writeLine === "function") {
            file.writeLine(path, line);
            return;
        }
    } catch (e) {
        // 退回整文件重写
    }
    let old = "";
    try {
        old = file.readFrom(path) || "";
    } catch (e) {
        old = "";
    }
    file.writeTo(path, old ? old + "\n" + line : line);
}

// ---------- 封禁数据 ----------

function loadBans() {
    bans = [];
    safeDir();
    let text = "";
    try {
        text = file.readFrom(BAN_FILE) || "";
    } catch (e) {
        text = "";
    }
    if (!text) return;
    try {
        const obj = JSON.parse(text);
        if (obj && Array.isArray(obj.bans)) {
            bans = obj.bans.filter(b => b && b.type && b.value);
        }
    } catch (e) {
        log("[AntiBang] bans.json 解析失败：" + e);
    }
}

function saveBans() {
    try {
        safeDir();
        file.writeTo(BAN_FILE, JSON.stringify({ version: 1, bans: bans }, null, 2));
    } catch (e) {
        log("[AntiBang] 写入 bans.json 失败：" + e);
    }
}

function purgeExpired() {
    const t = nowSec();
    const before = bans.length;
    bans = bans.filter(b => !(b.expiresAt && b.expiresAt <= t));
    if (bans.length !== before) saveBans();
}

function hasBan(type, value) {
    if (!value) return false;
    return bans.some(b => b.type === type && eqi(b.value, value));
}

function addBan(entry) {
    if (!entry.value) return false;
    if (hasBan(entry.type, entry.value)) return false;
    bans.push(entry);
    return true;
}

function expiresText(b) {
    if (!b.expiresAt) return "永久";
    const left = b.expiresAt - nowSec();
    if (left <= 0) return "已过期";
    return "剩余 " + Math.ceil(left / 3600) + " 小时";
}

// ---------- 玩家信息（记住每个人） ----------

function identityOf(pl) {
    const id = {
        name: String(pl.realName || pl.name || ""),
        xuid: String(pl.xuid || ""),
        uuid: String(pl.uuid || ""),
        ip: "",
        clientId: "",
        os: ""
    };
    try {
        const dv = pl.getDevice ? pl.getDevice() : null;
        if (dv) {
            id.ip = stripPort(dv.ip);
            id.clientId = String(dv.clientId || "");
            id.os = String(dv.os || "");
        }
    } catch (e) {
        log("[AntiBang] 读取设备信息失败：" + e);
    }
    return id;
}

function emptyIdentity() {
    return { name: "", xuid: "", uuid: "", ip: "", clientId: "", os: "" };
}

/**
 * 把一次记录合并进 known（名字和 XUID 两个 key 指向同一个对象）
 * ts：这次记录的时间戳；isJoin：是否算一次进服
 */
function touchKnown(id, ts, isJoin) {
    const nameKey = id.name ? id.name.toLowerCase() : "";
    const xuidKey = id.xuid ? id.xuid.toLowerCase() : "";
    if (!nameKey && !xuidKey) return null;

    let rec = (nameKey && known[nameKey]) || (xuidKey && known[xuidKey]) || null;
    if (!rec) {
        rec = {
            name: id.name || "",
            xuid: id.xuid || "",
            uuid: id.uuid || "",
            ip: id.ip || "",
            clientId: id.clientId || "",
            os: id.os || "",
            firstSeen: ts || 0,
            lastSeen: ts || 0,
            joins: 0
        };
    } else {
        if (id.name) rec.name = id.name;
        if (id.xuid) rec.xuid = id.xuid;
        if (id.uuid) rec.uuid = id.uuid;
        // 只更新有值的字段，避免把旧数据覆盖成空
        if (id.ip) rec.ip = id.ip;
        if (id.clientId) rec.clientId = id.clientId;
        if (id.os) rec.os = id.os;
        if (ts) {
            if (!rec.firstSeen || ts < rec.firstSeen) rec.firstSeen = ts;
            if (!rec.lastSeen || ts > rec.lastSeen) rec.lastSeen = ts;
        }
    }
    if (isJoin) rec.joins = (rec.joins || 0) + 1;

    if (nameKey) known[nameKey] = rec;
    if (xuidKey) known[xuidKey] = rec;
    return rec;
}

function remember(id, status) {
    if (!id.name && !id.xuid) return;
    const ts = nowSec();
    touchKnown(id, ts, status === "JOIN");
    const line = [ts, status, id.name, id.xuid, id.ip, id.clientId, id.os, id.uuid].join("\t");
    try {
        appendLine(HISTORY_FILE, line);
    } catch (e) {
        // 写日志失败不影响拦截
    }
}

function loadHistory() {
    known = {};
    let text = "";
    try {
        text = file.readFrom(HISTORY_FILE) || "";
    } catch (e) {
        return;
    }
    if (!text) return;
    const lines = text.split("\n");
    for (let i = 0; i < lines.length; i++) {
        const f = lines[i].replace(/\r$/, "").split("\t");
        if (f.length < 5) continue;
        const ts = parseInt(f[0], 10) || 0;
        const status = f[1] || "";
        const id = {
            name: f[2] || "",
            xuid: f[3] || "",
            ip: f[4] || "",
            clientId: f[5] || "",
            os: f[6] || "",
            uuid: f[7] || ""
        };
        touchKnown(id, ts, status === "JOIN");
    }
}

// ---------- 匹配 ----------

function matchBan(id) {
    for (let i = 0; i < bans.length; i++) {
        const b = bans[i];
        if (b.expiresAt && b.expiresAt <= nowSec()) continue;

        if (b.type === "xuid" && id.xuid && eqi(b.value, id.xuid)) return b;

        if (b.type === "device" && CFG.checkDevice && id.clientId && eqi(b.value, id.clientId)) return b;

        if (b.type === "ip" && CFG.checkIp && id.ip) {
            if (String(b.value).indexOf("/") >= 0) {
                if (ipv4InCidr(id.ip, b.value)) return b;
            } else if (eqi(b.value, id.ip)) {
                return b;
            }
        }

        if (b.type === "name" && CFG.checkName && id.name && eqi(b.value, id.name)) return b;
    }
    return null;
}

function isBannedQuick(r) {
    return !!matchBan({ name: r.name, xuid: r.xuid, ip: r.ip, clientId: r.clientId });
}

function fmtKick(reason) {
    return String(CFG.kickMsg).replace("{reason}", reason || "未填写原因");
}

// ---------- 封禁动作 ----------

function addXuidBan(id, reason, by) {
    return addBan({ type: "xuid", value: id.xuid, createdAt: nowSec(), expiresAt: 0, by: by, reason: reason });
}

/**
 * opts = { name: 是否记名字封禁, ip: 是否连带封 IP }
 */
function applyBan(id, reason, by, opts) {
    opts = opts || {};
    const t = nowSec();
    let added = 0;
    const push = (type, value, expiresAt) => {
        if (!value) return;
        if (addBan({ type: type, value: value, createdAt: t, expiresAt: expiresAt || 0, by: by, reason: reason })) {
            added++;
        }
    };

    push("xuid", id.xuid, 0);
    if (opts.name && id.name) push("name", id.name, 0);
    if (CFG.banDevice) push("device", id.clientId, 0);
    if (opts.ip && CFG.banIp && id.ip) {
        push("ip", id.ip, CFG.ipBanHours > 0 ? t + CFG.ipBanHours * 3600 : 0);
    }
    saveBans();
    return added;
}

function kickOnline(key, reason) {
    let pl = null;
    try {
        pl = mc.getPlayer(key);
    } catch (e) {
        pl = null;
    }
    if (pl) pl.kick(fmtKick(reason));
    return pl;
}

// ---------- 查找 ----------

function resolveTarget(target) {
    const t = String(target || "").trim();
    if (!t) return null;

    let online = null;
    try {
        online = mc.getPlayer(t);
    } catch (e) {
        online = null;
    }
    if (online) {
        const id = identityOf(online);
        const rec = known[String(id.name).toLowerCase()] || known[String(id.xuid).toLowerCase()];
        if (rec) {
            id.firstSeen = rec.firstSeen;
            id.lastSeen = rec.lastSeen;
            id.joins = rec.joins;
        }
        return id;
    }

    const k = known[t.toLowerCase()];
    if (k) return k;

    if (/^\d+$/.test(t)) {
        const id = emptyIdentity();
        id.xuid = t;
        return id;
    }
    return null;
}

function allRecords() {
    const seen = {};
    const arr = [];
    for (const key in known) {
        const r = known[key];
        const k = String(r.xuid || r.name || "").toLowerCase();
        if (!k || seen[k]) continue;
        seen[k] = true;
        arr.push(r);
    }
    return arr;
}

// ---------- 事件 ----------

mc.listen("onJoin", pl => {
    const id = identityOf(pl);
    const hit = matchBan(id);
    remember(id, hit ? ("BLOCKED:" + hit.type) : "JOIN");

    if (!hit) return;

    // 小号：设备码 / IP / 名字命中，但账号本身没被封 → 把这个账号也封掉（下次改名也跑不掉）
    if (CFG.autoBanAlt && hit.type !== "xuid" && id.xuid && !hasBan("xuid", id.xuid)) {
        addXuidBan(id, "关联封禁: 与已封" +
            (hit.type === "device" ? "设备码" : (hit.type === "ip" ? "IP" : "名字")) +
            "相同（" + (hit.reason || "") + "）", "AntiBang");
        saveBans();
        log("[AntiBang] 小号 " + id.name + "（XUID " + id.xuid + "）命中已封" + hit.type + "，已连带封禁该账号");
    }

    log("[AntiBang] 拒绝 " + id.name + "（" + id.ip + "，设备 " + id.clientId + "）登录：命中 " +
        hit.type + "=" + hit.value);
    pl.kick(fmtKick(hit.reason));
});

mc.listen("onServerStarted", () => {
    safeDir();
    loadBans();
    purgeExpired();
    loadHistory();
    log("[AntiBang] 已启用：封禁 " + bans.length + " 条，历史玩家 " + allRecords().length + " 人");

    // ---------- /abban <target> [reason] ----------
    const ban = mc.newCommand("abban", "封禁玩家：XUID + 设备码 + IP + 名字一起封", PermType.GameMasters);
    ban.mandatory("target", ParamType.String);
    ban.optional("reason", ParamType.RawText);
    ban.overload(["target"]);
    ban.overload(["target", "reason"]);
    ban.setCallback((_cmd, ori, out, res) => {
        const id = resolveTarget(res.target);
        if (!id) {
            out.error("找不到 " + res.target + "：他还没进过服。要按名字硬封请用 /abname");
            return;
        }
        const reason = String(res.reason || "未填写原因");
        const by = String(ori.name || "Console");
        const added = applyBan(id, reason, by, { name: CFG.banName, ip: true });
        kickOnline(id.xuid || id.name, reason);

        out.success("已封禁 " + (id.name || id.xuid) + "，新增 " + added + " 条记录");
        log("[AntiBang] " + by + " 封禁了 " + id.name + "（XUID " + id.xuid + "，IP " + id.ip +
            "，设备 " + id.clientId + "），原因：" + reason);
    });
    ban.setup();

    // ---------- /abname <name> [reason] ----------
    const nameCmd = mc.newCommand("abname", "按名字封禁，并连带封掉他的 XUID / 设备码 / IP", PermType.GameMasters);
    nameCmd.mandatory("name", ParamType.String);
    nameCmd.optional("reason", ParamType.RawText);
    nameCmd.overload(["name"]);
    nameCmd.overload(["name", "reason"]);
    nameCmd.setCallback((_cmd, ori, out, res) => {
        const name = String(res.name || "").trim();
        if (!name) {
            out.error("名字不能为空");
            return;
        }
        const reason = String(res.reason || "按名字封禁");
        const by = String(ori.name || "Console");

        // 优先用历史里的记录（这样能连带封掉设备码/IP）；查不到就只封名字
        const rec = known[name.toLowerCase()] || null;
        const id = rec || (function () {
            const e = emptyIdentity();
            e.name = name;
            return e;
        })();

        let added = 0;
        // 名字必封
        if (addBan({ type: "name", value: name, createdAt: nowSec(), expiresAt: 0, by: by, reason: reason })) added++;
        if (id.xuid && addXuidBan(id, reason, by)) added++;
        if (CFG.banDevice && id.clientId &&
            addBan({ type: "device", value: id.clientId, createdAt: nowSec(), expiresAt: 0, by: by, reason: reason })) added++;
        if (CFG.banIp && id.ip &&
            addBan({
                type: "ip", value: id.ip, createdAt: nowSec(),
                expiresAt: CFG.ipBanHours > 0 ? nowSec() + CFG.ipBanHours * 3600 : 0,
                by: by, reason: reason
            })) added++;
        saveBans();

        kickOnline(rec ? rec.xuid || rec.name : name, reason);

        if (rec) {
            out.success("已封禁 " + name + "，并连带封了 XUID / 设备码 / IP，共 " + added + " 条");
            log("[AntiBang] " + by + " 按名字封禁 " + name + "（XUID " + rec.xuid + "，IP " + rec.ip +
                "，设备 " + rec.clientId + "），原因：" + reason);
        } else {
            out.success("已封禁名字 " + name + "（暂无历史记录，只封了名字；他第一次进服会被直接踢掉）");
            log("[AntiBang] " + by + " 按名字封禁 " + name + "（无历史记录），原因：" + reason);
        }
    });
    nameCmd.setup();

    // ---------- /abcheck <target> ----------
    const check = mc.newCommand("abcheck", "查这个人：进服次数 / 时间 / 设备码 / IP / 封禁状态", PermType.GameMasters);
    check.mandatory("target", ParamType.String);
    check.overload(["target"]);
    check.setCallback((_cmd, _ori, out, res) => {
        const id = resolveTarget(res.target);
        if (!id) {
            out.error("没有 " + res.target + " 的任何记录：他没进过这个服，或者名字记错了");
            return;
        }
        out.success("名字: " + (id.name || "(空)"));
        out.success("XUID: " + (id.xuid || "(空)"));
        out.success("设备码: " + (id.clientId || "(空)"));
        out.success("系统: " + (id.os || "(空)"));
        out.success("IP: " + (id.ip || "(空)"));
        out.success("进服次数: " + (id.joins || 0));
        out.success("第一次: " + fmtTime(id.firstSeen));
        out.success("最后一次: " + fmtTime(id.lastSeen));

        const hit = matchBan(id);
        out.success(hit
            ? "状态: 已封禁 [" + hit.type + "] " + hit.value + " / " + expiresText(hit) + " / " + hit.reason
            : "状态: 正常");
    });
    check.setup();

    // ---------- /abfind [keyword] ----------
    const find = mc.newCommand("abfind", "查谁进过服：按名字/XUID/IP/设备码模糊搜，不给关键字列最近 20 人",
        PermType.GameMasters);
    find.optional("keyword", ParamType.String);
    find.overload([]);
    find.overload(["keyword"]);
    find.setCallback((_cmd, _ori, out, res) => {
        const kw = String(res.keyword || "").trim().toLowerCase();
        let list = allRecords();

        if (kw) {
            list = list.filter(r =>
                String(r.name || "").toLowerCase().indexOf(kw) >= 0 ||
                String(r.xuid || "").toLowerCase().indexOf(kw) >= 0 ||
                String(r.ip || "").toLowerCase().indexOf(kw) >= 0 ||
                String(r.clientId || "").toLowerCase().indexOf(kw) >= 0
            );
        }
        list.sort((a, b) => (b.lastSeen || 0) - (a.lastSeen || 0));

        out.success(kw ? ("匹配 \"" + kw + "\" 的玩家: " + list.length + " 人") : ("最近进服的玩家: " + list.length + " 人"));
        const max = Math.min(list.length, 20);
        for (let i = 0; i < max; i++) {
            const r = list[i];
            out.success((isBannedQuick(r) ? "§c[已封] §f" : "") + r.name +
                " | 最后 " + fmtTime(r.lastSeen) + " | " + (r.joins || 0) + " 次 | " + (r.ip || "-"));
        }
        if (list.length > max) out.success("... 只显示最近 20 人");
    });
    find.setup();

    // ---------- /abunban <value> ----------
    const unban = mc.newCommand("abunban", "解除封禁：XUID / 设备码 / IP / 名字", PermType.GameMasters);
    unban.mandatory("value", ParamType.String);
    unban.overload(["value"]);
    unban.setCallback((_cmd, _ori, out, res) => {
        const needle = String(res.value || "").trim();
        const id = resolveTarget(needle);
        const keys = [needle];
        if (id) keys.push(id.xuid, id.clientId, id.ip, id.name);

        const before = bans.length;
        bans = bans.filter(b => {
            for (let i = 0; i < keys.length; i++) {
                if (keys[i] && eqi(b.value, keys[i])) return false;
            }
            return true;
        });
        const removed = before - bans.length;
        if (removed === 0) {
            out.error("没有找到匹配 " + needle + " 的封禁记录");
            return;
        }
        saveBans();
        out.success("已解除 " + removed + " 条封禁记录");
    });
    unban.setup();

    // ---------- /ablist ----------
    const list = mc.newCommand("ablist", "列出封禁记录", PermType.GameMasters);
    list.overload([]);
    list.setCallback((_cmd, _ori, out) => {
        out.success("共 " + bans.length + " 条封禁记录");
        const max = Math.min(bans.length, 20);
        for (let i = 0; i < max; i++) {
            const b = bans[i];
            out.success("[" + b.type + "] " + b.value + " | " + expiresText(b) + " | " + (b.reason || ""));
        }
        if (bans.length > max) out.success("... 只显示前 20 条，完整内容见 plugins/AntiBang/bans.json");
    });
    list.setup();

    // ---------- /abip <value> [reason] ----------
    const ip = mc.newCommand("abip", "手动封 IP，支持 1.2.3.4 或 1.2.3.0/24", PermType.GameMasters);
    ip.mandatory("value", ParamType.String);
    ip.optional("reason", ParamType.RawText);
    ip.overload(["value"]);
    ip.overload(["value", "reason"]);
    ip.setCallback((_cmd, ori, out, res) => {
        const value = String(res.value || "").trim();
        const exp = CFG.ipBanHours > 0 ? nowSec() + CFG.ipBanHours * 3600 : 0;
        const added = addBan({
            type: "ip", value: value, createdAt: nowSec(), expiresAt: exp,
            by: String(ori.name || "Console"), reason: String(res.reason || "手动封禁 IP")
        });
        saveBans();
        out.success(added ? ("已封禁 IP: " + value + "（" + expiresText({ expiresAt: exp }) + "）")
                          : ("该记录已存在: " + value));
    });
    ip.setup();

    // ---------- /abdev <value> [reason] ----------
    const dev = mc.newCommand("abdev", "手动封设备码", PermType.GameMasters);
    dev.mandatory("value", ParamType.String);
    dev.optional("reason", ParamType.RawText);
    dev.overload(["value"]);
    dev.overload(["value", "reason"]);
    dev.setCallback((_cmd, ori, out, res) => {
        const value = String(res.value || "").trim();
        const added = addBan({
            type: "device", value: value, createdAt: nowSec(), expiresAt: 0,
            by: String(ori.name || "Console"), reason: String(res.reason || "手动封禁设备")
        });
        saveBans();
        out.success(added ? ("已封禁设备码: " + value) : ("该记录已存在: " + value));
    });
    dev.setup();

    // ---------- /abreload ----------
    const reload = mc.newCommand("abreload", "重新读取封禁列表和登录历史", PermType.GameMasters);
    reload.overload([]);
    reload.setCallback((_cmd, _ori, out) => {
        loadBans();
        purgeExpired();
        loadHistory();
        out.success("已重新加载：封禁 " + bans.length + " 条，历史玩家 " + allRecords().length + " 人");
    });
    reload.setup();

    log("[AntiBang] 命令已注册");
});
