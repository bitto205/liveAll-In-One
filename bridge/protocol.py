"""IPC op 常量 — 与 core/internal/protocol/ops.go、core/CONTRACT.md 同步。"""
from __future__ import annotations

DEFAULT_PIPE = r"\\.\pipe\liveaio-core"
DEFAULT_TCP = ("127.0.0.1", 19877)
CORE_VERSION = "0.1.0"

# 握手 / 生命周期
OP_READY = "ready"
OP_PING = "ping"
OP_PONG = "pong"
OP_SHUTDOWN = "shutdown"
OP_ERROR = "error"

# 会话 / 帧
OP_CONNECT = "connect"
OP_DISCONNECT = "disconnect"
OP_FRAME_PUSH = "frame.push"
OP_MESSAGE_INGEST = "message.ingest"
OP_STATUS = "status"
OP_MESSAGE = "message"

# 配置（Python 写文件；此处只推快照）
OP_CONFIG_SET = "config.set"
OP_CONFIG_GET = "config.get"

# 工具业务事件 Core → UI
OP_TICK = "tick"
OP_LEDGER = "ledger"
OP_DANMU_SHOW = "danmu.show"
OP_MEMO_ITEM = "memo.item"

# 工具命令 UI → Core
OP_TOOL_OVERTIME_SET = "tool.overtime.set"
OP_TOOL_OVERTIME_CMD = "tool.overtime.cmd"
OP_TOOL_OVERTIME_SIM = "tool.overtime.sim_gift"
OP_TOOL_DANMU_SET = "tool.danmu.set"
OP_TOOL_MEMO_SET = "tool.memo.set"
