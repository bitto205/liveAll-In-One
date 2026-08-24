package listener

import (
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"time"
)

// Route3Driver: bundled proxy_shell MITM + temporary system proxy (replaces mitmproxy local).
type Route3Driver struct{}

func (Route3Driver) ID() ID { return Route3 }

func (d Route3Driver) Run(ctx context.Context, p Params) error {
	shell := bundledShell(p.Root)
	if _, err := os.Stat(shell); err != nil {
		return fmt.Errorf("proxy_shell.exe missing: %w", err)
	}
	prev, err := getSystemProxy()
	if err != nil {
		return err
	}
	if err := setSystemProxy(fmt.Sprintf("127.0.0.1:%d", proxyPort), true); err != nil {
		return fmt.Errorf("set system proxy: %w", err)
	}
	defer func() { _ = restoreSystemProxy(prev) }()

	cmd := exec.CommandContext(ctx, shell)
	cmd.Dir = filepath.Dir(shell)
	hideCmd(cmd)
	if err := cmd.Start(); err != nil {
		return err
	}
	defer func() {
		if cmd.Process != nil {
			_ = cmd.Process.Kill()
		}
	}()

	// Wait for IPC/proxy ports.
	deadline := time.Now().Add(8 * time.Second)
	for time.Now().Before(deadline) {
		if health() == nil {
			break
		}
		time.Sleep(200 * time.Millisecond)
	}

	cl := &Shell{
		OnCtrl: func(ctrl string) {
			switch ctrl {
			case CtrlLiveOn:
				if p.OnStatus != nil {
					p.OnStatus(true)
				}
			case CtrlLiveOff, CtrlWSDown:
				if p.OnStatus != nil {
					p.OnStatus(false)
				}
			}
		},
		OnFrame: func(raw []byte) {
			if p.OnFrame != nil {
				p.OnFrame(raw)
			}
		},
	}
	if err := cl.Start(); err != nil {
		return err
	}
	defer cl.Stop()

	<-ctx.Done()
	if p.OnStatus != nil {
		p.OnStatus(false)
	}
	return ctx.Err()
}
