package core

import (
	"context"
	"os"
	"os/signal"
	"syscall"
)

// run is the shared startup chain for exe (debug) and LiveAIOCore.dll.
func Run(args []string) int {
	fs := newFlagSet(args)
	rootFlag := fs.root
	tcp := fs.tcp
	noUI := fs.noUI
	noTray := fs.noTray
	noAdmin := fs.noAdmin

	exe, _ := os.Executable()
	root := rootFlag
	if root == "" {
		root = ResolveAppRoot(exe)
	}
	_ = ChdirRoot(root)
	_ = os.Setenv("LIVEAIO_ROOT", root)
	if noTray {
		_ = os.Setenv("LIVEAIO_NO_TRAY", "1")
	}

	if !noAdmin {
		if err := EnsureAdmin(exe, BuildElevatedParams(args), root); err != nil {
			return 1
		}
	}

	log := FileLogger(root, "liveaio.log")
	openUI := !noUI

	if !TryHoldLauncher() {
		log.Info("already running")
		// 只请求持有实例显示界面；本进程不加载 Pages，避免出现第二个 UI 进程。
		if openUI {
			if err := RequestShowUI(tcp); err != nil {
				log.Error("request show UI", "err", err)
				return 1
			}
		}
		return 0
	}
	defer ReleaseLauncher()

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	h, srv := startHub(ctx, root, tcp, log, cancel)
	h.showUI = func() {
		if err := OpenPages(root); err != nil {
			log.Error("open UI", "err", err)
		}
	}
	defer h.stopCapture()
	defer h.overtime.Stop()
	defer srv.Stop()

	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-sigCh
		h.stopCapture()
		srv.Stop()
		cancel()
	}()

	if openUI {
		if err := OpenPages(root); err != nil {
			log.Error("open UI", "err", err)
		}
	}

	if noTray {
		<-ctx.Done()
		return 0
	}

	log.Info("tray starting")
	go func() {
		<-ctx.Done()
		QuitTray()
	}()
	if err := RunTray(TrayConfig{
		Root:     root,
		Tooltip:  "LiveAIO",
		OnShowUI: func() { h.requestShowUI() },
		OnOverlay: func(tool, action string) {
			if err := OverlayCommand(root, tool, action); err != nil {
				log.Error("overlay command", "tool", tool, "action", action, "err", err)
			}
		},
		OverlayState: OverlayState,
		OnQuit: func() {
			h.stopCapture()
			srv.Stop()
			cancel()
			os.Exit(0)
		},
	}); err != nil {
		log.Error("tray failed", "err", err)
		return 1
	}
	return 0
}
