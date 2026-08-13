package main

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"flag"
	"log/slog"
	"os"
	"os/signal"
	"path/filepath"
	"sync"
	"syscall"

	"liveaio/core/internal/biz/danmu"
	"liveaio/core/internal/biz/memo"
	"liveaio/core/internal/biz/overtime"
	"liveaio/core/internal/ipc"
	"liveaio/core/internal/launcher"
	"liveaio/core/internal/listener/capture"
	"liveaio/core/internal/listener/parse"
	"liveaio/core/internal/listener/shellipc"
	"liveaio/core/internal/protocol"
)

type hub struct {
	mu        sync.Mutex
	conns     map[*ipc.Conn]struct{}
	root      string
	route     string
	liveID    string
	connected bool
	overtime  *overtime.Engine
	danmu     *danmu.Filter
	memo      *memo.Filter
	log       *slog.Logger
	shutdown  func()
	capture   *capture.Manager
	shell     *shellipc.Client
}

func (h *hub) addConn(c *ipc.Conn) {
	h.mu.Lock()
	if h.conns == nil {
		h.conns = map[*ipc.Conn]struct{}{}
	}
	h.conns[c] = struct{}{}
	h.mu.Unlock()
}

func (h *hub) removeConn(c *ipc.Conn) {
	h.mu.Lock()
	delete(h.conns, c)
	h.mu.Unlock()
}

func (h *hub) send(env ipc.Envelope) {
	h.mu.Lock()
	conns := make([]*ipc.Conn, 0, len(h.conns))
	for c := range h.conns {
		conns = append(conns, c)
	}
	h.mu.Unlock()
	for _, c := range conns {
		_ = c.Send(env)
	}
}

func (h *hub) ingestFrame(raw []byte) {
	ok, msgs := parse.TryParseFrame(raw)
	if !ok {
		return
	}
	if !h.connected {
		h.connected = true
		h.send(ipc.Envelope{"op": protocol.OpStatus, "connected": true, "route": h.route})
	}
	for _, m := range msgs {
		out := ipc.Envelope{"op": protocol.OpMessage}
		for k, v := range m {
			out[k] = v
		}
		h.send(out)
		h.afterMessage(m)
	}
}

func (h *hub) stopCapture() {
	if h.shell != nil {
		h.shell.Stop()
		h.shell = nil
	}
	if h.capture != nil {
		h.capture.Stop()
	}
}

func (h *hub) startRoute4() {
	h.stopCapture()
	cl := &shellipc.Client{
		OnCtrl: func(ctrl string) {
			switch ctrl {
			case shellipc.CtrlLiveOn:
				h.connected = true
				h.send(ipc.Envelope{"op": protocol.OpStatus, "connected": true, "route": "4"})
				h.log.Info("route4 live on")
			case shellipc.CtrlLiveOff, shellipc.CtrlWSDown:
				h.connected = false
				h.send(ipc.Envelope{"op": protocol.OpStatus, "connected": false, "route": "4"})
				h.log.Info("route4 live off", "ctrl", ctrl)
			}
		},
		OnFrame: func(raw []byte) {
			if !h.connected {
				return
			}
			h.ingestFrame(raw)
		},
		OnErr: func(err error) {
			h.log.Warn("shellipc", "err", err)
		},
	}
	if err := cl.Start(); err != nil {
		h.log.Error("shellipc start", "err", err)
		h.send(ipc.Envelope{"op": protocol.OpError, "code": "shellipc", "msg": err.Error()})
		return
	}
	h.shell = cl
	h.log.Info("route4 shellipc listening")
}

func (h *hub) startPlaywright(route string, forceSystem bool) {
	h.stopCapture()
	if h.capture == nil {
		return
	}
	if err := h.capture.StartPlaywrightWorker(h.liveID, route, forceSystem); err != nil {
		h.log.Error("playwright worker", "err", err)
		h.send(ipc.Envelope{"op": protocol.OpError, "code": "pw_worker", "msg": err.Error()})
		return
	}
}

func (h *hub) handle(c *ipc.Conn, env ipc.Envelope) {
	switch env.Op() {
	case protocol.OpPing:
		out := ipc.Envelope{"op": protocol.OpPong}
		if id, ok := env["id"]; ok {
			out["id"] = id
		}
		_ = c.Send(out)

	case protocol.OpShutdown:
		_ = c.Send(ipc.Envelope{"op": protocol.OpReady, "bye": true})
		h.stopCapture()
		if h.shutdown != nil {
			h.shutdown()
		}

	case protocol.OpConnect:
		liveID, _ := env["live_id"].(string)
		route, _ := env["route"].(string)
		forceSystem := false
		if v, ok := env["force_system"].(bool); ok {
			forceSystem = v
		}
		h.liveID = liveID
		h.route = route
		h.connected = false
		h.log.Info("connect", "live_id", liveID, "route", route)

		switch route {
		case "4":
			// Python may still patch companion; core owns IPC listen + parse.
			h.startRoute4()
		case "1", "2":
			h.startPlaywright(route, forceSystem)
		case "3":
			// mitm stays in Python helper; frames arrive via frame.push
			h.stopCapture()
			h.log.Info("route3: expect frame.push from mitm helper")
		default:
			h.stopCapture()
		}

	case protocol.OpDisconnect:
		h.stopCapture()
		h.connected = false
		h.send(ipc.Envelope{"op": protocol.OpStatus, "connected": false, "route": h.route})

	case protocol.OpStatus:
		// Playwright/mitm helper may report live gate.
		conn := false
		if v, ok := env["connected"].(bool); ok {
			conn = v
		}
		h.connected = conn
		out := ipc.Envelope{"op": protocol.OpStatus, "connected": conn, "route": h.route}
		if r, ok := env["route"].(string); ok && r != "" {
			out["route"] = r
		}
		h.send(out)

	case protocol.OpFramePush:
		b64, _ := env["payload_b64"].(string)
		raw, err := base64.StdEncoding.DecodeString(b64)
		if err != nil {
			_ = c.Send(ipc.Envelope{"op": protocol.OpError, "code": "bad_b64", "msg": err.Error()})
			return
		}
		h.ingestFrame(raw)

	case protocol.OpMessageIngest:
		m := parse.Msg{}
		for k, v := range env {
			if k == "op" {
				continue
			}
			m[k] = v
		}
		if _, ok := m["type"].(string); !ok {
			_ = c.Send(ipc.Envelope{"op": protocol.OpError, "code": "bad_ingest", "msg": "missing type"})
			return
		}
		h.afterMessage(m)

	case protocol.OpToolOvertimeSet:
		var s overtime.Settings
		if raw, ok := env["settings"]; ok {
			b, _ := json.Marshal(raw)
			_ = json.Unmarshal(b, &s)
		}
		h.overtime.SetSettings(s)

	case protocol.OpToolOvertimeCmd:
		cmd, _ := env["cmd"].(string)
		h.overtime.Cmd(cmd)

	case protocol.OpToolOvertimeSim:
		gift, _ := env["gift"].(string)
		count := 1
		switch v := env["count"].(type) {
		case float64:
			count = int(v)
		case int:
			count = v
		}
		user, _ := env["user"].(string)
		if user == "" {
			user = "sim"
		}
		h.overtime.HandleGift(user, user, gift, count)

	case protocol.OpToolDanmuSet:
		var s danmu.Settings
		if raw, ok := env["settings"]; ok {
			b, _ := json.Marshal(raw)
			_ = json.Unmarshal(b, &s)
		}
		h.danmu.Set(s)

	case protocol.OpToolMemoSet:
		var s memo.Settings
		if raw, ok := env["settings"]; ok {
			b, _ := json.Marshal(raw)
			_ = json.Unmarshal(b, &s)
		}
		h.memo.Set(s)

	case protocol.OpConfigSet:
		_ = c.Send(ipc.Envelope{"op": "config.ok", "key": env["key"]})

	default:
		_ = c.Send(ipc.Envelope{
			"op": protocol.OpError, "code": "unsupported",
			"msg": "unknown op: " + env.Op(),
		})
	}
}

func (h *hub) afterMessage(m parse.Msg) {
	t, _ := m["type"].(string)
	if t == "gift" {
		gift, _ := m["gift"].(string)
		user, _ := m["user"].(string)
		uid, _ := m["user_id"].(string)
		count := 1
		switch v := m["count"].(type) {
		case float64:
			count = int(v)
		case int:
			count = v
		case int64:
			count = int(v)
		}
		h.overtime.HandleGift(user, uid, gift, count)
	}
	if show := h.danmu.Accept(m, 0); show != nil {
		h.send(ipc.Envelope(show))
	}
	if item := h.memo.Accept(m, 0); item != nil {
		h.send(ipc.Envelope(item))
	}
}

func main() {
	rootFlag := flag.String("root", "", "LIVEAIO app root")
	pipe := flag.String("pipe", protocol.DefaultPipeName, "named pipe path")
	tcp := flag.String("tcp", protocol.DefaultTCPAddr, "tcp listen addr")
	flag.Parse()

	exe, _ := os.Executable()
	root := *rootFlag
	if root == "" {
		root = launcher.ResolveAppRoot(exe)
	}
	_ = launcher.ChdirRoot(root)
	os.Setenv("LIVEAIO_ROOT", root)

	log := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelInfo}))

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	h := &hub{
		root:     root,
		log:      log,
		shutdown: cancel,
		conns:    map[*ipc.Conn]struct{}{},
		capture: capture.New(root, func(msg string, kv ...any) {
			log.Info(msg, kv...)
		}),
	}
	h.overtime = overtime.New(
		func(rem int, running bool) {
			h.send(ipc.Envelope{"op": protocol.OpTick, "remaining_seconds": rem, "running": running})
		},
		func(entries []overtime.LedgerEntry) {
			h.send(ipc.Envelope{"op": protocol.OpLedger, "entries": entries})
		},
	)
	h.danmu = danmu.New()
	h.memo = memo.New()
	h.overtime.StartTicker()
	defer h.overtime.Stop()
	defer h.stopCapture()

	srv := &ipc.Server{
		PipeName: *pipe,
		TCPAddr:  *tcp,
		Handler:  h.handle,
		Log:      log,
		OnConnect: func(c *ipc.Conn) {
			h.addConn(c)
		},
		OnDisconnect: func(c *ipc.Conn) {
			h.removeConn(c)
		},
	}

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigCh
		h.stopCapture()
		cancel()
		srv.Stop()
	}()

	go func() {
		log.Info("liveaio-core starting", "root", root, "version", protocol.Version, "exe", filepath.Base(exe))
		if err := srv.Serve(ctx); err != nil {
			log.Error("serve ended", "err", err)
			cancel()
		}
	}()

	<-ctx.Done()
}
