package capture

import (
	"os"
	"os/exec"
	"path/filepath"
	"sync"
)

// Manager owns route-specific capture (shell IPC / playwright worker).
type Manager struct {
	mu     sync.Mutex
	stopFn func()
	root   string
	logf   func(string, ...any)
}

func New(root string, logf func(string, ...any)) *Manager {
	if logf == nil {
		logf = func(string, ...any) {}
	}
	return &Manager{root: root, logf: logf}
}

func (m *Manager) Stop() {
	m.mu.Lock()
	fn := m.stopFn
	m.stopFn = nil
	m.mu.Unlock()
	if fn != nil {
		fn()
	}
}

func (m *Manager) setStop(fn func()) {
	m.Stop()
	m.mu.Lock()
	m.stopFn = fn
	m.mu.Unlock()
}

// StartPlaywrightWorker launches thin Python capture for routes 1/2.
func (m *Manager) StartPlaywrightWorker(liveID, route string, forceSystem bool) error {
	m.Stop()
	py := filepath.Join(m.root, ".venv", "Scripts", "python.exe")
	script := filepath.Join(m.root, "listener", "pw_worker.py")
	if _, err := os.Stat(script); err != nil {
		return err
	}
	args := []string{script, "--live-id", liveID, "--route", route, "--tcp", "127.0.0.1:19877"}
	if forceSystem {
		args = append(args, "--force-system")
	}
	var cmd *exec.Cmd
	if _, err := os.Stat(py); err == nil {
		cmd = exec.Command(py, args...)
	} else {
		cmd = exec.Command("python", args...)
	}
	cmd.Dir = m.root
	cmd.Env = append(os.Environ(), "LIVEAIO_ROOT="+m.root)
	if err := cmd.Start(); err != nil {
		return err
	}
	m.logf("playwright worker started", "pid", cmd.Process.Pid, "route", route)
	m.setStop(func() {
		_ = cmd.Process.Kill()
		_, _ = cmd.Process.Wait()
	})
	return nil
}
