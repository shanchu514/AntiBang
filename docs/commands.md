# AntiBang 指令表

适用于 **JS 版**（`js/AntiBang.js`）。C++ 版的差异见文末。

- 权限：**OP**（`PermType.GameMasters`），控制台也能执行
- 控制台执行时**不要加 `/`**，游戏内才加
- 原因参数是 `RawText`，**带空格不用加引号**，直接写到最后即可

---

## 一、总表

| 指令 | 参数 | 作用 |
| --- | --- | --- |
| `/abban` | `<玩家名或XUID> [原因]` | 封禁一个人：XUID + 设备码 + IP + 名字一起写进封禁表，他若在线当场踢下线 |
| `/abname` | `<名字> [原因]` | 按名字封禁；历史里查得到这个人，就连带封掉他的 XUID / 设备码 / IP |
| `/abcheck` | `<名字或XUID>` | 查这个人：进服次数、第一次/最后一次时间、设备码、系统、IP、是否已封 |
| `/abfind` | `[关键字]` | 查谁进过服。关键字可匹配名字 / XUID / IP / 设备码；不填则列最近 20 个进服的人 |
| `/abunban` | `<XUID\|设备码\|IP\|名字>` | 解封，会把这个人的关联记录一起解掉 |
| `/ablist` | 无 | 列出封禁记录（只显示前 20 条，完整见 `bans.json`） |
| `/abip` | `<IP或网段> [原因]` | 手动封 IP，支持 `1.2.3.4` 和 `1.2.3.0/24`；有效期由 `ipBanHours` 决定 |
| `/abdev` | `<设备码> [原因]` | 手动封设备码（永久） |
| `/abreload` | 无 | 重新读取 `bans.json` 和 `history.tsv`（手改文件后用） |

---

## 二、几个典型用法

**有人正在破坏，直接封**

```
/abban Steve 破坏出生点
```
→ 在线的话立刻踢下线；XUID、设备码、IP、名字全进封禁表。

**人已经跑了，只记得名字**

```
/abcheck Steve
```
→ 能看到他的设备码和 IP。要封：
```
/abname Steve 破坏出生点
```
→ 名字必封；如果历史里有记录，设备码和 IP 一起封。

**查某个名字到底进没进过服**

```
/abfind steve
```
或直接列最近进服的人：
```
/abfind
```

**封一个 IP 段（同一网吧/同一片流量）**

```
/abip 112.65.1.0/24 连续捣乱
```

**误封了要解**

```
/abunban Steve
/abunban 112.65.1.4
/abunban 8f3a...c21
```
XUID、设备码、IP、名字，填哪个都行。

---

## 三、封禁类型说明

| type | 来源 | 特点 |
| --- | --- | --- |
| `xuid` | Xbox Live 账号 ID | 最可靠，改名换设备都跑不掉 |
| `name` | 游戏内名字 | 名字跟账号绑定；改名后旧名字封禁失效 |
| `device` | `pl.getDevice().clientId` | 客户端自报，清数据/重装会变 |
| `ip` | `pl.getDevice().ip` | 带到期时间（默认 72 小时），可写 `/24` 段 |

自动连带：当有人用**已封的设备码 / IP / 名字**进服，但账号本身还没被封时，
插件会把那个账号的 XUID 也加进封禁表（`CFG.autoBanAlt`），所以他下次改名也进不来。

---

## 四、配置项（`js/AntiBang.js` 顶部的 `CFG`）

| 配置 | 默认 | 说明 |
| --- | --- | --- |
| `checkDevice` | `true` | 是否按设备码封禁 |
| `checkIp` | `true` | 是否按 IP 封禁 |
| `checkName` | `true` | 是否按名字封禁 |
| `banDevice` | `true` | `/abban` 时连设备码一起封 |
| `banIp` | `true` | `/abban` 时连 IP 一起封 |
| `banName` | `true` | `/abban` 时也记一条名字封禁 |
| `ipBanHours` | `72` | 连带 IP 封禁的有效期，`0` = 永久 |
| `autoBanAlt` | `true` | 小号命中已封设备/IP/名字时自动封其 XUID |
| `kickMsg` | 见文件 | 踢出提示，`{reason}` 会替换成封禁原因 |

改完执行 `/abreload` 或重启。

---

## 五、数据文件

| 文件 | 内容 |
| --- | --- |
| `plugins/AntiBang/bans.json` | 封禁表，可直接手改，改完 `/abreload` |
| `plugins/AntiBang/history.tsv` | 每个进服的人一行：时间、状态、名字、XUID、IP、设备码、系统、UUID |

`history.tsv` 用 Excel / Notepad++ 都能打开，也可以直接搜名字或 IP。

---

## 六、C++ 版（`src/`）的差异

| 指令 | JS 版 | C++ 版 |
| --- | --- | --- |
| `/abban` | ✅ | ✅ |
| `/abname` | ✅ 连带封 XUID/设备码/IP | ✅ 但只记名字，不连带 |
| `/abcheck` | ✅ 含进服次数、时间 | ✅ 但只有 XUID/IP/设备码/状态 |
| `/abfind` | ✅ | ❌ |
| `/abunban`、`/ablist`、`/abip`、`/abdev`、`/abreload` | ✅ | ✅ |

C++ 版是在**登录握手阶段**直接掐断（更硬），JS 版是在 `onJoin` 阶段踢人（客户端会先看到进服，随后被踢）。
