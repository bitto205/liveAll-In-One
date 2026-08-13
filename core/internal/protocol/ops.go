package protocol

// Shared op names (keep in sync with bridge/protocol.py and protocol_examples.md).

const (
	OpReady     = "ready"
	OpPing      = "ping"
	OpPong      = "pong"
	OpShutdown  = "shutdown"
	OpError     = "error"
	OpConnect   = "connect"
	OpDisconnect = "disconnect"
	OpFramePush     = "frame.push"
	OpMessageIngest = "message.ingest"
	OpStatus        = "status"
	OpMessage       = "message"
	OpConfigSet = "config.set"
	OpConfigGet = "config.get"
	OpTick      = "tick"
	OpLedger    = "ledger"
	OpDanmuShow = "danmu.show"
	OpMemoItem  = "memo.item"

	OpToolOvertimeSet = "tool.overtime.set"
	OpToolOvertimeCmd = "tool.overtime.cmd"
	OpToolOvertimeSim = "tool.overtime.sim_gift"
	OpToolDanmuSet    = "tool.danmu.set"
	OpToolMemoSet     = "tool.memo.set"
)

const Version = "0.1.0"

const (
	DefaultPipeName = `\\.\pipe\liveaio-core`
	DefaultTCPAddr  = "127.0.0.1:19877"
)
