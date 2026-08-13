# LiveAIO Core IPC Protocol v0.1

> 总契约（工具 UI / util 归属 / 进程）：[`CONTRACT.md`](CONTRACT.md)  
> 本文只放 **JSON 样例** 与字段对照表。

## Process topology

```
liveaio.exe / LiveAIO.exe (启动链 — UAC、托盘)
    └── spawns liveaio-core.exe (IPC / 听帧 / 工具)
    └── tray「打开界面」→ python ui_shell.py

ui_shell (Qt UI)  ←→  bridge.CoreClient  ←→  liveaio-core (Go)
```

- UI and core are **separate processes**.
- **Go** is the only app entry; Python UI attach-only.
- Core never creates windows.

## Transport

| Mode | Address | When |
|------|---------|------|
| Named Pipe (optional) | `\\.\pipe\liveaio-core` | When elevated / available |
| TCP (Phase 0 primary) | `127.0.0.1:19877` | Default listen; bridge connects here |

**Framing:** UTF-8 **JSON Lines** (one JSON object per line, `\n` terminated).

**Handshake:** Core accepts a client, then immediately writes a `ready` event. Client may send `ping`; core replies `pong`.

## Envelope

Every line is a JSON object with at least:

```json
{"op": "<command_or_event_name>", ...}
```

- **Commands** (UI → Core): use `op`.
- **Events** (Core → UI): also use `op` (same field name for simplicity in v0.1).

Optional common fields: `id` (string, correlate request/response), `ts` (unix ms).

---

## Commands v0.1 (UI → Core)

### ping

```json
{"op":"ping","id":"1"}
```

### shutdown

```json
{"op":"shutdown"}
```

Core exits after flushing.

### connect *(P1+)*

```json
{"op":"connect","live_id":"123","route":"4","force_system":false}
```

### disconnect *(P1+)*

```json
{"op":"disconnect"}
```

### frame.push *(routes 2/4)*
Raw WS / IPC frame → Go parse → `message` + tool events.

```json
{"op":"frame.push","payload_b64":"<base64 of raw WS frame bytes>"}
```

### message.ingest *(routes 1/3)*
Python already parsed; core runs tool filters only (no `message` echo).

```json
{"op":"message.ingest","type":"gift","user":"u","user_id":"1","gift":"小心心","count":1}
```

### config.set / config.get *(P2+)*

```json
{"op":"config.set","key":"overtime.settings","value":{}}
{"op":"config.get","key":"overtime.settings","id":"2"}
```

### tool.* *(P3+)*

```json
{"op":"tool.overtime.set","settings":{}}
{"op":"tool.overtime.cmd","cmd":"reset"}
{"op":"tool.overtime.sim_gift","gift":"小心心","count":1}
{"op":"tool.danmu.set","settings":{}}
{"op":"tool.memo.set","settings":{}}
```

---

## Events v0.1 (Core → UI)

### ready

```json
{"op":"ready","version":"0.1.0"}
```

### pong

```json
{"op":"pong","id":"1"}
```

### error

```json
{"op":"error","code":"unsupported","msg":"route 1 not in core yet"}
```

### status *(P1+)*

```json
{"op":"status","connected":true,"route":"4"}
```

### message *(P1+)* — aligns with `util/models.py`

```json
{"op":"message","type":"chat","user":"Alice","user_id":"1","content":"hi"}
```

```json
{"op":"message","type":"gift","user":"Bob","user_id":"2","gift":"小心心","gift_id":5655,"count":1,"repeat_end":1}
```

```json
{"op":"message","type":"like","user":"C","user_id":"3","count":5}
```

```json
{"op":"message","type":"follow","user":"D","user_id":"4","action":1,"share_type":0,"share_target":"","follow_count":0}
```

```json
{"op":"message","type":"control","status":3}
```

### tick / ledger / danmu.show / memo.item *(P3+)*

```json
{"op":"tick","remaining_seconds":120,"running":true}
{"op":"ledger","entries":[{"user":"Bob","user_id":"2","seconds":30}]}
{"op":"danmu.show","kind":"chat","user":"A","text":"hi"}
{"op":"memo.item","kind":"gift","user":"B","text":"送出小心心 x1","stack_key":"gift:B:小心心"}
```

---

## Core message types ↔ Python models

| `type` | Python class | Core fields (v0.1 required) |
|--------|--------------|-----------------------------|
| chat | ChatMessage | user, user_id, content |
| gift | GiftMessage | user, user_id, gift, gift_id, count, repeat_end |
| like | LikeMessage | user, user_id, count |
| enter | EnterMessage | user, user_id |
| follow | FollowMessage | user, user_id, action, share_type, share_target, follow_count |
| fansclub | FansclubMessage | user, user_id, content |
| online | OnlineMessage | current, total |
| control | ControlMessage | status |
| room_enter | RoomEnterStatusMessage | status, room_status, title, id_str |
| emoji | EmojiChatMessage | user, user_id, emoji_id, default_content |
| room_stats | RoomStatsMessage | display_* , total, display_type |
| rank | RoomRankMessage | ranks |

Constants: living enter status `2`, ended `4`, control finish `3`.

---

## Example session (≥5 lines)

```json
{"op":"ready","version":"0.1.0"}
{"op":"ping","id":"1"}
{"op":"pong","id":"1"}
{"op":"frame.push","payload_b64":"AAAA"}
{"op":"error","code":"parse","msg":"invalid frame"}
{"op":"shutdown"}
```

## Binary location

Dev build: `core/dist/liveaio-core.exe`  
Also searched: app root `liveaio-core.exe`.
