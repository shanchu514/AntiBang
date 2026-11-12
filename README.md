# AntiBang

> 服务器被人熊了，怕他注册小号回来 —— 所以写了这个插件。

[![platform](https://img.shields.io/badge/platform-BDS%20%2B%20LeviLamina-blueviolet)](https://levimc.org/)
![bds](https://img.shields.io/badge/BDS-1.21.93%20%7C%201.26.20-success)
![lang](https://img.shields.io/badge/lang-C%2B%2B20%20%7C%20JavaScript-orange)
[![license](https://img.shields.io/badge/license-MIT-blue)](LICENSE)

BDS 服务端防熊插件：**封禁 XUID / 设备码 / IP / 名字**，并记录每个进服玩家的完整身份信息，
方便事后追查小号。

提供两个版本：**JS 版**（免编译，丢进 `plugins/` 就能跑）和 **C++ 版**（原生插件，在登录握手阶段直接掐断）。

---

## 它能做什么

| 能力 | 说明 |
| --- | --- |
| 四维封禁 | XUID（账号）、设备码（客户端）、IP（可到 `/24` 网段）、游戏名 |
| 登录即拦 | 命中封禁表立即拒绝，客户端看不到服务器 |
| 小号连带 | 新账号用了已封的设备码 / IP / 名字 → 自动把这个账号的 XUID 也封掉 |
| 全量记录 | 每个进服的人记一条：时间、名字、XUID、IP、设备码、系统、UUID |
| 进服查询 | `/abcheck` 看某人进没进过、进过几次、用的什么设备 |
| 限时封禁 | IP 封禁可设有效期，避免误伤共享出口的玩家 |

---

## 两个版本

| | JS 版 | C++ 版 |
| --- | --- | --- |
| 文件 | [`js/AntiBang.js`](js/AntiBang.js) | [`src/`](src/) |
| 安装 | 丢进 `plugins/`，重启两次 | 编译出 `AntiBang.dll`，放进 `plugins/AntiBang/` |
| 依赖 | LeviLamina + LegacyScriptEngine(quickjs) | LeviLamina + VS2022 + xmake |
| 拦截时机 | `onJoin`（人已连上，下一 tick 踢掉） | 登录握手阶段（根本连不上） |
| 额外能力 | `/abfind` 查历史、进服次数统计 | — |

**只想快点用起来 → 选 JS 版；想要最硬的拦截 → 选 C++ 版。** 两者的数据文件格式一致。

---

## 快速开始（JS 版）

### 1. 装脚本引擎

```bash
lip install github.com/LiteLDev/LegacyScriptEngine#quickjs
```

没有 `lip` 的，去 [LegacyScriptEngine releases](https://github.com/LiteLDev/LegacyScriptEngine/releases)
下 `LegacyScriptEngine-server-quickjs-windows-x64.zip`，解压到 `plugins/`。

> ⚠️ 引擎版本必须和你的 LeviLamina 版本对应，装错会加载失败。

### 2. 放插件

把 `AntiBang.js` 复制到 `<服务端根目录>/plugins/AntiBang.js`。

### 3. 重启两次

| 第几次 | 会发生什么 |
| --- | --- |
| 第一次 | 引擎把 `AntiBang.js` 迁移成 `plugins/AntiBang/` 插件目录，此时还不生效 |
| 第二次 | 生效，控制台出现 `[AntiBang] 已启用` |

### 4. 验证

进游戏执行 `/abfind`，能列出玩家就说明通了。

---

## 指令

> 需要 OP 权限。控制台执行时**不要加 `/`**。

| 指令 | 说明 |
| --- | --- |
| `/abban <玩家\|XUID> [原因]` | 封禁：XUID + 设备码 + IP + 名字一起写，在线立刻踢 |
| `/abname <名字> [原因]` | 按名字封；有历史记录时连带封掉他的 XUID / 设备码 / IP |
| `/abcheck <名字\|XUID>` | 查进服次数、首次/末次时间、设备码、系统、IP、封禁状态 |
| `/abfind [关键字]` | 查谁进过服（名字/XUID/IP/设备码模糊搜），不填则列最近 20 人 |
| `/abunban <值>` | 解封，支持 XUID / 设备码 / IP / 名字 |
| `/ablist` | 列出封禁记录 |
| `/abip <IP\|网段> [原因]` | 手动封 IP，支持 `1.2.3.4` 和 `1.2.3.0/24` |
| `/abdev <设备码> [原因]` | 手动封设备码 |
| `/abreload` | 重新读取 `bans.json` 和 `history.tsv` |

完整说明见 [docs/commands.md](docs/commands.md)。

### 典型流程

```bash
/abcheck 熊孩子名字          # 先看他的设备码和 IP
/abban 熊孩子名字 破坏出生点   # 人还在线 → 立刻踢 + 四维封禁
/abfind 关键字               # 人跑了 → 查他进服记录
/abname 熊孩子名字 破坏出生点  # 有记录 → 名字 + XUID + 设备码 + IP 全封
```

---

## 配置

JS 版改文件顶部的 `CFG`；C++ 版改 `plugins/AntiBang/config/config.json`。

| 配置 | 默认 | 说明 |
| --- | --- | --- |
| `checkDevice` | `true` | 是否按设备码封禁 |
| `checkIp` | `true` | 是否按 IP 封禁 |
| `checkName` | `true` | 是否按名字封禁 |
| `banDevice` / `banIp` / `banName` | `true` | `/abban` 时连带封哪些 |
| `ipBanHours` | `72` | IP 封禁有效期，`0` = 永久 |
| `autoBanAlt` | `true` | 小号命中已封设备/IP/名字时自动封其 XUID |
| `kickMsg` | 见文件 | 踢出提示，`{reason}` 会被替换 |

---

## 数据文件

| 文件 | 内容 |
| --- | --- |
| `plugins/AntiBang/bans.json` | 封禁表，JSON，可直接手改后 `/abreload` |
| `plugins/AntiBang/history.tsv` | 每个进服的人一行，Tab 分隔 |

```
时间戳 | 状态 | 名字 | XUID | IP | 设备码 | 系统 | UUID
```

`history.tsv` 用 Excel 或记事本都能打开，直接搜名字 / IP / 设备码就能把一个人挖出来。

---

## 编译 C++ 版

需要 Visual Studio 2022（勾选「使用 C++ 的桌面开发」）+ [xmake](https://xmake.io)。

```powershell
# 一键编译（Windows）
.\build.cmd

# 指定 LeviLamina 版本
.\build.ps1 -LLVersion 1.4.4     # BDS 1.21.93
.\build.ps1 -LLVersion 26.20.*   # BDS 1.26.20
```

或者用 GitHub Actions（无需本地环境）：push 后 [`.github/workflows/build.yml`](.github/workflows/build.yml)
会自动为两个 LL 版本各出一个 artifact。

版本对应关系：

| 服务端版本 | LeviLamina | 编译参数 |
| --- | --- | --- |
| BDS 1.21.93.x | 1.4.x | `--ll_version=1.4.*` |
| BDS 1.26.20.x | 26.20.x | `--ll_version=26.20.*` |

---

## 说点实话

这类插件的效果有天然上限，写在这里免得有人误会：

| 手段 | 能拦 | 拦不住 |
| --- | --- | --- |
| **XUID** | 同一微软账号，改名/换设备/换网都进不来 | 对方注册新微软账号 |
| **设备码** | 同一台设备、未重装的客户端 | 卸载重装 / 清数据 / 应用分身 / 改包客户端 |
| **IP** | 同一出口 IP 下的小号 | 挂加速器 / 换网络；手机流量、校园网是大 NAT，容易误伤 |
| **名字** | 用同一个名字回来 | 改名（但首次撞名会自动补封 XUID） |

**设备码不是硬件码**：它是客户端自己生成、存在 App 私有目录里的一个随机串，服务端无法验证真伪。
它的价值是提高开小号成本，不是识别设备。

所以结论是：**白名单 > XUID 封禁 > 设备码 > IP**。本插件默认值也是按「少误伤」定的
（IP 封禁 72 小时到期、不默认封 `/24` 段）。

真正防小号还得靠白名单 + 邀请制 + 手机号验证这类有成本的入口 —— 那是管理流程，不是插件能解决的。

---

## 常见问题

**Q：封了人他还能进？**
① `/abcheck` 看设备码是否为空（有些客户端会清空）；② 他可能重装过客户端；③ 他挂了加速器换了 IP；
④ 设备码只能拦同一台未重装的设备。

**Q：误封了怎么解？**
`/abunban <名字>`，会把这个人的 XUID / 设备码 / IP 一起解掉。
或直接改 `bans.json` 后 `/abreload`。

**Q：客户端只显示「无法连接」，没有封禁原因？**
C++ 版握手阶段掐断的正常表现。想让对方看到原因，把 `hardBlock` 改成 `false`。

**Q：会不会拖慢服务器？**
只在玩家登录时做一次线性比对，几千条记录对登录耗时没有可感知影响。

---

## 目录结构

```
AntiBang/
├─ js/AntiBang.js              # JS 版（免编译）
├─ src/                        # C++ 版源码
│  ├─ AntiBang.cpp/.h          # 主体：事件、命令、封禁逻辑
│  ├─ BanStore.cpp/.h          # bans.json 读写
│  ├─ Config.h                 # config.json 结构
│  └─ IpUtil.h                 # IP 解析 / 网段匹配
├─ docs/commands.md            # 完整指令表
├─ xmake.lua                   # 构建脚本
├─ build.ps1 / build.cmd       # 一键编译
└─ .github/workflows/build.yml # 云编译
```

---

## 致谢

- [LeviLamina](https://github.com/LiteLDev/LeviLamina) —— BDS 插件加载器
- [LegacyScriptEngine](https://github.com/LiteLDev/LegacyScriptEngine) —— 脚本引擎
- [levilamina-mod-template](https://github.com/LiteLDev/levilamina-mod-template) —— 工程模板

## License

[MIT](LICENSE)
