// liveaio — 外部启动链：UAC、单实例、拉起 core、原生托盘、按需开 UI。
// liveaio-core 只跑 IPC/听帧/工具，不含启动链。
package main

import (
	"flag"
	"log/slog"
	"os"
	"time"

	"liveaio/core/internal/launcher"
	"liveaio/core/internal/protocol"
	"liveaio/core/internal/tray"
)

func main() {
	rootFlag := flag.String("root", "", "LIVEAIO app root")
	tcp := flag.String("tcp", protocol.DefaultTCPAddr, "core TCP addr")
	withUI := flag.Bool("ui", false, "also launch Python/Qt UI")
	noTray := flag.Bool("no-tray", false, "spawn core only, no tray (debug)")
	flag.Parse()

	exe, _ := os.Executable()
	root := *rootFlag
	if root == "" {
		root = launcher.ResolveAppRoot(exe)
	}
	_ = launcher.ChdirRoot(root)
	os.Setenv("LIVEAIO_ROOT", root)

	if err := launcher.EnsureAdmin(exe, launcher.BuildElevatedParams(os.Args), root); err != nil {
		slog.Error("admin elevation failed", "err", err)
		os.Exit(1)
	}

	log := slog.New(slog.NewTextHandler(os.Stderr, &slog.HandlerOptions{Level: slog.LevelInfo}))

	// 单实例：core 已在跑 → 可选开 UI 后退出
	if launcher.ProbeTCP(*tcp) {
		log.Info("liveaio-core already running", "tcp", *tcp)
		if *withUI {
			_ = tray.LaunchUI(root)
		}
		return
	}

	coreExe, err := launcher.FindCoreExe(root, exe)
	if err != nil {
		log.Error("find core", "err", err)
		os.Exit(1)
	}
	if err := launcher.SpawnCore(coreExe, root, *tcp); err != nil {
		log.Error("spawn core", "err", err)
		os.Exit(1)
	}
	if err := launcher.WaitCore(*tcp, 15*time.Second); err != nil {
		log.Error("wait core", "err", err)
		os.Exit(1)
	}
	log.Info("liveaio-core started", "exe", coreExe)

	if *withUI {
		if err := tray.LaunchUI(root); err != nil {
			log.Error("launch UI", "err", err)
		}
	}

	if *noTray {
		return
	}

	log.Info("native tray starting")
	if err := tray.Run(tray.Config{
		Root:    root,
		Tooltip: "LiveAIO",
		OnShowUI: func() {
			if err := tray.LaunchUI(root); err != nil {
				log.Error("launch UI", "err", err)
			}
		},
		OnQuit: func() {
			if err := launcher.ShutdownCore(*tcp, 3*time.Second); err != nil {
				log.Error("shutdown core", "err", err)
			}
			os.Exit(0)
		},
	}); err != nil {
		log.Error("tray failed", "err", err)
		os.Exit(1)
	}
}
