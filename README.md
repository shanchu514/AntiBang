# AntiBang

基岩版（Bedrock）服务端防熊插件：按 XUID、设备码、IP、名字封禁玩家，并记录每个进服玩家的完整身份信息，方便事后追查小号。

- 原生 C++ 版与脚本（JavaScript）版两种，数据文件通用
- 四维封禁，命中即在登录阶段拦掉
- 新账号撞上已封的设备码 / IP / 名字时，自动连坐封掉该账号

## 功能

**封禁**

- `/abban` 一次写入 XUID、设备码、IP、名字四条记录，在线的话立刻踢下线
- `/abname` 按名字封，历史里有记录时带出他的 XUID、设备码、IP 一起封
- `/abip` 支持 `1.2.3.4` 与 `1.2.3.0/24` 网段；`/abdev` 单独封设备码
- IP 封禁可设有效期，默认 72 小时到期，避免误伤共享出口的玩家

**记录**

- 每次登录追加一行到 `history.tsv`：时间、状态、名字、XUID、IP、设备码、系统、UUID
- `/abcheck` 查某人进过几次、第一次与最后一次时间、当时的设备码和 IP
- `/abfind` 按名字、XUID、IP、设备码模糊搜索；不给关键字则列最近 20 个进服的人

**实施**

- C++ 版挂在 `PlayerConnectEvent`，在握手阶段直接取消，客户端只看到「无法连接」
- JS 版挂在 `onJoin`，进来一个 tick 后被踢，能看到自定义的封禁原因

## 安装

### 脚本版（免编译）

需要 [LegacyScriptEngine](https://github.com/LiteLDev/LegacyScriptEngine) 的 QuickJS 引擎。

1. 安装引擎：

```bash
lip install github.com/LiteLDev/LegacyScriptEngine#quickjs
```

2. 把 `js/AntiBang.js` 复制到 `<服务端>/plugins/AntiBang.js`。
3. 重启服务端两次：第一次引擎把脚本迁移成 `plugins/AntiBang/` 插件目录，第二次生效。
4. 控制台出现 `[AntiBang] 已启用` 即成功。

### 原生版（C++）

BDS 1.26.20 可直接用预编译版本：[Releases](https://github.com/shanchu514/AntiBang/releases)；其他版本按下文自行编译。

需要 Visual Studio 2022（桌面 C++ 工作负载）与 [xmake](https://xmake.io)：

```powershell
# BDS 1.21.93（LeviLamina 1.4.x）
.\build.ps1 -LLVersion 1.4.4

# BDS 1.26.20（LeviLamina 26.20.x）
.\build.ps1 -LLVersion "26.20.*"
```

也可以 push 到 GitHub，由 [.github/workflows/build.yml](.github/workflows/build.yml) 自动为两个 LL 版本各出一份产物。

产物 `AntiBang.dll` 与 `manifest.json` 放进 `<服务端>/plugins/AntiBang/`。

## 指令

需要 OP 权限；控制台执行时不带斜杠。

| 指令 | 说明 |
| --- | --- |
| `/abban <玩家\|XUID> [原因]` | 封 XUID + 设备码 + IP + 名字，在线立即踢 |
| `/abname <名字> [原因]` | 按名字封，并连带封掉他的 XUID / 设备码 / IP |
| `/abcheck <名字\|XUID>` | 查进服记录与封禁状态 |
| `/abfind [关键字]` | 查谁进过服 |
| `/abunban <值>` | 解封，支持 XUID / 设备码 / IP / 名字 |
| `/ablist` | 列出封禁记录 |
| `/abip <IP\|网段> [原因]` | 手动封 IP 或网段 |
| `/abdev <设备码> [原因]` | 手动封设备码 |
| `/abreload` | 重新读取数据文件 |

日常用法：

```
/abcheck 熊孩子名字            # 先看他的设备码和 IP
/abban 熊孩子名字 破坏出生点     # 人还在线，立刻踢
/abname 熊孩子名字 破坏出生点    # 人已经跑了，靠历史记录连坐
```

完整说明见 [docs/commands.md](docs/commands.md)。

## 配置

脚本版改文件顶部的 `CFG`；原生版改 `plugins/AntiBang/config/config.json`。

| 项 | 默认 | 说明 |
| --- | --- | --- |
| `checkDevice` / `checkIp` / `checkName` | `true` | 三类条件是否参与匹配 |
| `banDevice` / `banIp` / `banName` | `true` | `/abban` 时连带封哪些 |
| `ipBanHours` | `72` | IP 封禁有效期，`0` 表示永久 |
| `autoBanAlt` | `true` | 小号撞上已封设备/IP/名字时封掉其 XUID |
| `kickMsg` | 见文件 | 踢出提示，`{reason}` 会被替换 |

## 数据文件

都在 `plugins/AntiBang/`：

- `bans.json` — 封禁表，JSON，可直接手改后 `/abreload`
- `history.tsv` — 进服记录，Tab 分隔，Excel 或记事本都能打开

## 构建

```powershell
.\build.cmd              # 默认 26.20.*
.\build.ps1 -LLVersion 1.4.4
```

版本对应：BDS 1.21.93 → LeviLamina 1.4.x；BDS 1.26.20 → LeviLamina 26.20.x。版本不对会编译失败或启动报错。

## 实现

- 原生版通过 `PlayerConnectEvent` 拿到 `NetworkIdentifier`（IP）与 `ConnectionRequest`（`DeviceId`、`SelfSignedId`），取消事件即拒绝登录
- 封禁表是内存中的 `std::vector`，登录时线性比对，几千条记录对登录耗时可忽略
- `IpUtil.h` 实现 IPv4 解析与 CIDR 匹配；IPv6 按完整字符串精确匹配
- 脚本版用 `pl.getDevice()` 取 `ip` 与 `clientId`，逻辑与原生版一致

## 已知限制

- 设备码是客户端自行生成、存在 App 私有目录的随机串，服务端无法验证真伪；重装、清数据、应用分身、改包客户端都会让封禁失效
- IP 封禁对手机流量、校园网、网吧这类大 NAT 环境误伤率高，所以默认带 72 小时到期、不默认封 `/24`
- 名字封禁依赖基岩版名字与账号绑定；改名后旧名字的封禁失效，但首次撞名时会自动补封 XUID
- 脚本版在 `onJoin` 才踢人，被拦的人会看到短暂的进入过程

## 许可证

[MIT](LICENSE)。
