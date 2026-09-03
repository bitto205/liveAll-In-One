# LiveAIO 契约：进程 / 通信 / 工具 UI / 归属

> 单一真源。协议常量与 schema 以 Go [`protocol.go`](protocol.go) / [`models.go`](models.go) 为准；本文与 [`protocol_examples.md`](protocol_examples.md) 只做说明。  
> C++ pages/tools 对接细则见 [`CPP_UI_CONTRACT.md`](CPP_UI_CONTRACT.md)。  
> **原则：** Go（Core DLL）是协议 / config / 工具业务 / 线路编排 owner；C++ 只做 pages + tools UI。

---

## 1. 进程与通信

```text
LiveAIO.exe                 C++ 薄宿主：LoadLibrary 各模块
  ├── LiveAIOCore.dll       Go：hub / IPC / 托盘 / 线路 / 工具业务
  ├── LiveAIOPages.dll      C++ Qt 页面（连 Core TCP）
  ├── LiveAIOTools.dll      C++ Qt 工具悬浮窗（连 Core TCP）
  └── resources/            静态资源（文件，非 dll）
```

调试可用：`go build ./main` 整进程 Go exe（不经 Core dll）。生产路径：C++ `LiveAIO.exe` → `LiveAIO_CoreMain`。

| 场景 | 谁在跑 |
|------|--------|
| 关界面 | **仅** Core（托盘可再打开 pages） |
| 线路 1/2 | Core 同进程 chromedp（`listener/browser.go`） |
| 线路 3 | Core 同进程：`proxy_shell` + 临时系统代理 + Shell IPC |
| 线路 4 | Core `PrepareR4` + `shellipc` 听帧（伴侣已 patch） |
| 工具 / 页面开着 | Core + Pages/Tools DLL（同进程由宿主加载） |

| 项 | 约定 |
|----|------|
| 传输 | JSONL；TCP `127.0.0.1:19877`（[`DefaultTCPAddr`](protocol.go)） |
| 信封 | 每行 JSON，必有 `op` |
| 配置写者 | **仅 Go core** 读写 `config.json` 与 schema |
| UI 退出 | 关窗 = 断 IPC，**不**杀 Core；显式退出才 `shutdown` / `ui.command` `quit.shutdown_all` |

握手（[`server.go`](server.go) + [`hub.go`](hub.go)）：

1. TCP 连接建立 → Core 发 `ready`
2. Hub `addConn` → 发 `capabilities`（含 `features`）→ 发当前 `status`

### 1.1 UI → Core（命令）

与 `hub.handle` / `handleUICommand` 对齐：

| op | 谁发 | 作用 |
|----|------|------|
| `ping` | pages/tools | 探活 → `pong` |
| `shutdown` | 显式退出 | 停 Core |
| `connect` / `disconnect` | pages | 会话（`route` / `live_id` / `force_system`） |
| `status` | pages | 拉取当前连接态（点对点回） |
| `frame.push` | 线路 2/3/4（或同进程回调等价） | `payload_b64` → Go 解析 → `message` + 工具事件 |
| `message.ingest` | 线路 1（已解析） | **只**跑工具业务，**不** echo `message` |
| `tool.overtime.set` / `cmd` / `sim_gift` | OvertimeTool | 规则 / 控制 / 模拟送礼 |
| `tool.danmu.set` | DanmuTool | 过滤开关 |
| `tool.memo.set` | MemoTool | 过滤开关 |
| `config.set` / `config.get` | pages/tools | 统一读写 config |
| `ui.command` | pages | 登录 / 线路环境 / patch / 退出类动作（见下） |

`ui.command` 的 `action`（[`hub_ui.go`](hub_ui.go)）：

| action | 回执 op |
|--------|---------|
| `login.query` / `login.start` | `login.state` |
| `route.env` | `route.env` |
| `route4.set_companion_path` / `route4.patch` / `route4.unpatch` / `route3.unpatch` | `route.env` |
| `ui.show` | 广播 `ui.focus` + 必要时加载 Pages，`status` ack |
| `quit.detach_ui` | `status` ack |
| `quit.shutdown_all` | `ready` + `bye` 后停 Core |

### 1.2 Core → UI（事件）

| op | 消费方 | 作用 |
|----|--------|------|
| `ready` / `pong` / `error` | 握手 / 日志 | 连接与错误 |
| `capabilities` | pages/tools | 协议能力 + `features` |
| `status` | pages → 工具 | 连接态 |
| `message` | pages 广播；**工具不做二次业务过滤** | 字段以 Go schema 为准 |
| `config.ok` | 发起 `config.set` 的客户端 | 写入成功回执（`key` / `value`） |
| `config.value` | 发起 `config.get` 的客户端 | 单 key 或整表 `values` |
| `login.state` / `route.env` | pages | 登录文案 / 线路环境 |
| `tick` | OvertimeTool | `remaining_seconds`, `running` |
| `ledger` | OvertimeTool | 用户台账 |
| `danmu.show` | DanmuTool | 已过滤条目 |
| `memo.item` | MemoTool | 已过滤条目 |

`ui.focus`（`OpFocusUI`）：托盘「打开界面」或第二次启动时由 hub 广播；已运行的 Pages 抬起既有窗口。
第二个进程只发 `ui.command` `ui.show` 然后退出，绝不自己加载 Pages。

样例 JSON 见 [`protocol_examples.md`](protocol_examples.md)。

### 1.3 线路数据路径（无双解析）

| 线路 | 抓帧/会话 | 解析 | 工具业务 |
|------|-----------|------|----------|
| 2 / 3 / 4 | Go listener → `OnFrame` / Shell | **仅 Go** `TryParseFrame` | Go |
| 1 | Go chromedp JS hook → `OnMessage` | 线路侧已映射 | Go（`message.ingest` 语义，不 echo） |

禁止：同一帧在 UI 或第二套解析器再 parse 一次进工具。

### 1.4 C++ UI 懒加载与 overlay

Pages/Tools 的懒加载矩阵、同 Qt 实例内的独立透明 overlay 双窗、Async-by-default 规则见 [`CPP_UI_CONTRACT.md`](CPP_UI_CONTRACT.md)（§懒加载矩阵、§透明 overlay、§Async-by-default）。

---

## 2. 工具：业务 vs UI

### 2.1 归属

| | Go core（业务） | C++ `tools/*`（UI） |
|--|----------------|---------------------|
| 加班机 | 规则换算、tick、ledger、sim | 设置表单、悬浮窗皮肤/几何、显示剩余秒 |
| 弹幕机 | 类型开关、钻石/点赞阈值、累计 | 气泡窗、皮肤、后缀展示 |
| 备忘录 | 过滤、堆叠键 | 列表、清空、自定义行、皮肤 |

**禁止**在 C++ 再实现礼物/弹幕业务过滤。工具只消费 `tick` / `ledger` / `danmu.show` / `memo.item`，只发 `tool.*.set|cmd|sim_gift`。

设置变更：

```text
Qt 控件 → config.set →（可选）tool.*.set → core 业务生效
```

模拟送礼：`tool.overtime.sim_gift`（不走本地假业务路径）。

### 2.2 设置字段（推给 core 的 JSON）

与 [`Normalize*Settings`](models.go) 对齐。

**danmu**（`tool.danmu.set` → `settings`）

- `danmu_chat_on`, `danmu_gift_on`, `danmu_gift_min_diamonds`
- `danmu_follow_on`, `danmu_like_on`, `danmu_like_threshold`, `danmu_like_accumulate`

**memo**（`tool.memo.set` → `settings`）

- `memo.gift.enabled`, `memo.gift.stack`, `memo.gift.min_diamonds`
- `memo.follow.enabled`, `memo.like.enabled`, `memo.like.stack`

**overtime**（`tool.overtime.set` → `settings`）

- `hours`, `minutes`, `seconds`
- `rules[]`: `{gift, mode: add|sub|random, unit: s|m|h, value, min, max}`  
  （中文「加/减/随机」「秒/分/时」由 Go 归一化）

---

## 3. Ownership

| 层 | 负责 |
|----|------|
| Go `core` | 协议、config、工具业务、托盘、UAC、hub、启动链 `Run`、按需 LoadLibrary Pages；`core/dll` 导出 Core DLL |
| Go `listener` | 线路输入边界与 proto 解码；不写 UI |
| C++ `pages` | 连接页 / 设置 / 登录与线路环境展示 |
| C++ `tools` | 三个悬浮工具窗与皮肤绘制 |
| C++ `build/host` | 仅启动与 LoadLibrary（与已删除的 Go `host/` 包无关） |

---

## 4. 目录布局（平面内聚）

```text
main/                 调试用 Go exe 入口
core/                 协议 / hub / config / 工具业务 / 托盘 / UAC / Pages DLL 拉起
core/dll/             LiveAIOCore.dll 导出入口（c-shared）
listener/             线路采集
tools/                C++ Qt 工具源码
pages/                C++ Qt 页面源码
build/                唯一构建：build.ps1 + CMake；产物 build_work/
resources/
  image/              应用图标
  gift/               图鉴 JSON + icon/
  skin/               皮肤 JSON
  cpp/                C++ 读接口（gift.cpp / skin.cpp；勿与 .go 同目录）
```

| 路径 | 归属 |
|------|------|
| `resources/gift/` | 图鉴；Go `resources/gift.go` + C++ `resources/cpp/gift.cpp` |
| `resources/skin/` | 皮肤；Go `skin.go` 路径辅助 + C++ `resources/cpp/skin.cpp` |
| `resources/image/` | 托盘/窗口图标 |

---

## 5. 验收清单

- [ ] 新业务状态只加在 Go，不在 UI 侧加倒计时/过滤
- [ ] 新 UI 事件有 `op` 名，先写入 `protocol.go`，再同步本文 + `protocol_examples.md` + `CPP_UI_CONTRACT.md`
- [ ] UI 只通过 IPC 收 core 事件；无第二套 op 名、无直写 `config.json`
- [ ] 静态资源只放 `resources/`
