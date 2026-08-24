package core

// Protocol constants. Go is the single source of truth for UI/core IPC.

const (
	ProtocolVersion = "0.1.0"
	Version         = ProtocolVersion
)

const (
	OpReady         = "ready"
	OpPing          = "ping"
	OpPong          = "pong"
	OpShutdown      = "shutdown"
	OpError         = "error"
	OpConnect       = "connect"
	OpDisconnect    = "disconnect"
	OpFramePush     = "frame.push"
	OpMessageIngest = "message.ingest"
	OpStatus        = "status"
	OpMessage       = "message"
	OpConfigSet     = "config.set"
	OpConfigGet     = "config.get"
	OpConfigValue   = "config.value" // response to config.get
	OpConfigOk      = "config.ok"    // ack after successful config.set (hub)
	OpCapabilities  = "capabilities"
	OpFocusUI       = "ui.focus" // hub → Pages: raise the existing window
	OpUICommand     = "ui.command"
	OpLoginState    = "login.state"
	OpRouteEnv      = "route.env"
	OpTick          = "tick"
	OpLedger        = "ledger"
	OpDanmuShow     = "danmu.show"
	OpMemoItem      = "memo.item"

	OpToolOvertimeSet = "tool.overtime.set"
	OpToolOvertimeCmd = "tool.overtime.cmd"
	OpToolOvertimeSim = "tool.overtime.sim_gift"
	OpToolDanmuSet    = "tool.danmu.set"
	OpToolMemoSet     = "tool.memo.set"
)

const (
	DefaultPipeName = `\\.\pipe\liveaio-core`
	DefaultTCPAddr  = "127.0.0.1:19877"
)
