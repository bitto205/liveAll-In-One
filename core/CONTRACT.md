# LiveAIO 契约：进程 / 通信 / 工具 UI / util 归属

> 单一真源。实现以本文件 + `core/protocol_examples.md` 样例 JSON 为准。  
> **原则：** 按语言隔离。**Go** = 启动链 + 常驻 core。**Python** = 仅 Qt UI（`ui_shell.py`）。

---

## 1. 进程与通信

```
liveaio.exe / LiveAIO.exe        外部启动链（UAC、单实例、托盘、spawn core）
liveaio-core.exe                 纯服务（IPC、听帧、解析、工具）
ui_shell.py                      Python/Qt UI（attach core）
pw_worker.py                     无窗 Playwright（1/2，core 子进程）
```

| 场景 | 谁在跑 |
|------|--------|
| 关界面 | **仅 core**（原生托盘可再「打开界面」） |
| 线路 1/2 | core + `pw_worker`（无 Qt） |
| 线路 4 | core `shellipc` + 可选 Python patch 挂起 |
| 线路 3 | core 解析 + Python mitm 助手 `frame.push` |
| 工具悬浮窗开着 | core + Python UI（渲染需求） |

| 项 | 约定 |
|----|------|
| 传输 | JSONL；TCP `127.0.0.1:19877` |
| 信封 | 每行 JSON，必有 `op` |
| 配置写者 | UI 在时 **仅 Python** 写文件，再 `tool.*.set` |
| UI 退出 | `detach` 不断 core；仅显式退出才 `shutdown` |

### 1.1 UI → Core（命令）

| op | 谁发 | 作用 |
|----|------|------|
| `ping` | bridge | 探活 |
| `shutdown` | main/app 退出 | 停 core |
| `connect` / `disconnect` | App 连麦时 | 会话标记（route/live_id） |
| `frame.push` | 线路 2/4 抓帧 | `payload_b64` → Go 解析 → `message` + 工具事件 |
| `message.ingest` | 线路 1/3 本地已解析 | **只**跑工具业务，**不** echo `message` |
| `tool.overtime.set` / `cmd` / `sim_gift` | 加班机 UI | 规则 / 控制 / 模拟送礼 |
| `tool.danmu.set` | 弹幕机 UI | 过滤开关 |
| `tool.memo.set` | 备忘录 UI | 过滤开关 |
| `config.set` / `get` | 预留 | 键值快照；Python 仍是唯一文件写者 |

### 1.2 Core → UI（事件）

| op | UI 消费方 | 作用 |
|----|-----------|------|
| `ready` / `pong` / `error` | bridge / 日志 | 握手与错误 |
| `status` | App → 页面/工具 | 连接态 |
| `message` | App → 广播页面；**工具不走此路径做过滤** | 与 `util/models.py` 同形 |
| `tick` | OvertimeTool | `remaining_seconds`, `running` |
| `ledger` | OvertimeTool（可选） | 用户台账 |
| `danmu.show` | DanmuTool | 已过滤条目：`kind`/`user`/`text`/… |
| `memo.item` | MemoTool | 已过滤条目：`kind`/`user`/`text`/`stack_key`/`stack` |

样例 JSON 见 `protocol_examples.md`。

### 1.3 线路数据路径（减法，无双解析）

| 线路 | 抓帧/会话 | 解析 | 工具业务 |
|------|-----------|------|----------|
| 2 / 4 | Python | **仅 Go**（`frame.push`） | Go |
| 1 / 3 | Python | Python（暂留） | Go（`message.ingest`） |

禁止：同一帧本地再 parse 一次进工具。

---

## 2. 工具：业务 vs UI

### 2.1 归属

| | Go core（业务） | Python `tools/*`（UI） |
|--|----------------|------------------------|
| 加班机 | 规则换算、tick、ledger、sim | 设置表单、悬浮窗皮肤/几何、显示剩余秒 |
| 弹幕机 | 类型开关、钻石/点赞阈值、累计 | DanmuWindow、皮肤、后缀展示 |
| 备忘录 | 过滤、堆叠键 | 列表、清空、自定义行、皮肤 |

### 2.2 Python 工具 UI 接口（必须实现）

模块：`tools/iface.py` 的 `ToolUI`。

```text
on_core_event(env: dict) -> None
    # 处理 op ∈ {danmu.show, memo.item, ledger}；忽略无关 op
on_core_tick(env: dict) -> None   # 仅加班机需要；其它可空实现
on_status_change(connected: bool) -> None  # 可选
```

**禁止**再实现「本地过滤礼物/弹幕」的 `process_message` 业务路径。  
App 对工具的业务入口只有：`dispatch_core_tick` / `dispatch_core_tool_event`（见 `tools/tool_common.py`）。

设置变更流程：

```text
Qt 控件 → config.set(…) → bridge.tool_sync.push_* → tool.*.set → core
```

模拟送礼：`sim_overtime_gift` → `tool.overtime.sim_gift`（不走本地 `handle_gift`）。

### 2.3 设置字段（推给 core 的 JSON）

**danmu**（`tool.danmu.set.settings`）

- `danmu_chat_on`, `danmu_gift_on`, `danmu_gift_min_diamonds`
- `danmu_follow_on`, `danmu_like_on`, `danmu_like_threshold`, `danmu_like_accumulate`

**memo**（`tool.memo.set.settings`）

- `memo.gift.enabled`, `memo.gift.stack`, `memo.gift.min_diamonds`
- `memo.follow.enabled`, `memo.like.enabled`, `memo.like.stack`

**overtime**（`tool.overtime.set.settings`）

- `hours`, `minutes`, `seconds`
- `rules[]`: `{gift, mode: add|sub|random, unit: s|m|h, value, min, max}`  
  （UI 侧中文「加/减/随机」「秒/分/时」由 `bridge/tool_sync` 转换）

---

## 3. util/ 归属（迁 / 留 / 契约）

| 模块 | 归属 | 说明 |
|------|------|------|
| `models.py` | **契约** | FE/Go 消息字段表；Go 镜像同字段；改字段必须双边 + 本文档 |
| `room_enter.py` | 随解析迁 Go（未完） | 进房状态；Python 线路 1/2 仍用 |
| `paths.py` | 壳 | `app_root`；core 用 `--root` |
| `log_util.py` | 壳 | UI 日志；core 另文件 |
| `features.py` | UI | 能力开关读配置 |
| `status_cache.py` | UI | home 环境检查 |
| `playwright_bootstrap.py` / `browser_trim.py` | UI 链非核心 | 不进 Go |
| `theme.py` / `widgets.py` / `overlay_*` | **永留 UI** | |
| `config.py`（根） | **唯一写者** | 与 core 禁止双写文件 |

`util/__init__.py` 只做归属索引，不堆业务。

---

## 4. bridge 职责（薄）

| 模块 | 职责 |
|------|------|
| `protocol.py` | op 常量（与 Go `internal/protocol/ops.go` 同步） |
| `core_client.py` | spawn / 连接 / 读线程 / `send` / shutdown |
| `message_codec.py` | `message` 信封 ↔ `util.models` |
| `tool_sync.py` | 读 config → `tool.*.set`；`message.ingest`；sim gift |

bridge **不含** Qt 控件、**不含**过滤规则。

---

## 5. 验收清单（改工具/util 时勾）

- [ ] 新业务状态只加在 Go，不在 `*_tool.py` 里加过滤器/倒计时
- [ ] 新 UI 事件有 `op` 名，写入本文件 + `protocol_examples.md` + `ops.go` + `bridge/protocol.py`
- [ ] 工具只通过 `ToolUI` 收 core 事件
- [ ] 改 `models` 字段则 Go parse / codec / 本文档同改
- [ ] 不引入 adapter / `use_go_core` 双路径
