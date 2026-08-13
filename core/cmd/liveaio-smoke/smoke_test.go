package main

import (
	"bufio"
	"encoding/json"
	"net"
	"os"
	"testing"
	"time"

	"liveaio/core/internal/launcher"
	"liveaio/core/internal/protocol"
)

func TestProbeTCP_closedPort(t *testing.T) {
	if launcher.ProbeTCP("127.0.0.1:1") {
		t.Fatal("expected closed port to fail probe")
	}
}

// 迷你 JSONL 服务：模拟 core 的 ready + ping/pong，不依赖 liveaio-core 进程。
func TestSmokeCheck_againstMiniServer(t *testing.T) {
	ln, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer ln.Close()

	go func() {
		conn, err := ln.Accept()
		if err != nil {
			return
		}
		defer conn.Close()

		ready, _ := json.Marshal(map[string]any{
			"op": protocol.OpReady, "version": protocol.Version,
		})
		ready = append(ready, '\n')
		_, _ = conn.Write(ready)

		r := bufio.NewReader(conn)
		for {
			line, err := r.ReadBytes('\n')
			if err != nil {
				return
			}
			var env map[string]any
			if json.Unmarshal(line, &env) != nil {
				continue
			}
			if env["op"] == protocol.OpPing {
				pong, _ := json.Marshal(map[string]any{
					"op": protocol.OpPong, "id": env["id"],
				})
				pong = append(pong, '\n')
				_, _ = conn.Write(pong)
			}
		}
	}()

	addr := ln.Addr().String()
	if err := smokeCheck(addr, 3*time.Second); err != nil {
		t.Fatal(err)
	}
}

// 对已运行的 liveaio-core 做集成探活：LIVEAIO_INTEGRATION=1 go test ./cmd/liveaio-smoke/...
func TestSmokeCheck_integration(t *testing.T) {
	if os.Getenv("LIVEAIO_INTEGRATION") != "1" {
		t.Skip("set LIVEAIO_INTEGRATION=1 with liveaio-core running")
	}
	addr := os.Getenv("LIVEAIO_TCP")
	if addr == "" {
		addr = protocol.DefaultTCPAddr
	}
	if !launcher.ProbeTCP(addr) {
		t.Fatalf("core not listening on %s", addr)
	}
	if err := smokeCheck(addr, 5*time.Second); err != nil {
		t.Fatal(err)
	}
}
