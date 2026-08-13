// liveaio-smoke — 独立 IPC 探活/冒烟工具（不是 liveaio-core 本体）。
//
// 用法:
//
//	go run ./cmd/liveaio-smoke probe
//	go run ./cmd/liveaio-smoke ping
//	go run ./cmd/liveaio-smoke run
package main

import (
	"flag"
	"fmt"
	"os"
	"time"

	"liveaio/core/internal/launcher"
	"liveaio/core/internal/protocol"
)

func main() {
	addr := flag.String("addr", protocol.DefaultTCPAddr, "core TCP addr")
	timeout := flag.Duration("timeout", 5*time.Second, "per-step timeout")
	flag.Parse()

	cmd := "run"
	if flag.NArg() > 0 {
		cmd = flag.Arg(0)
	}

	switch cmd {
	case "probe":
		if launcher.ProbeTCP(*addr) {
			fmt.Println("ok: core listening on", *addr)
			return
		}
		fmt.Fprintln(os.Stderr, "fail: nothing listening on", *addr)
		os.Exit(1)

	case "ping", "run":
		if err := smokeCheck(*addr, *timeout); err != nil {
			fmt.Fprintln(os.Stderr, "fail:", err)
			os.Exit(1)
		}
		fmt.Println("ok: ready + ping/pong on", *addr)

	default:
		fmt.Fprintf(os.Stderr, "unknown command %q (probe|ping|run)\n", cmd)
		os.Exit(2)
	}
}
