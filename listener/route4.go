package listener

import (
	"bufio"
	"context"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"os"
	"os/exec"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

const (
	ipcPort   = 19098
	proxyPort = 19088
	shellName = "proxy_shell.exe"

	DefaultIPCAddr = "127.0.0.1:19098"
	CtrlPrefix     = "__LH_CTRL__:"
	CtrlLiveOn     = "LIVE_ON_AIR:true"
	CtrlLiveOff    = "LIVE_ON_AIR:false"
	CtrlWSOpen     = "WS_OPEN"
	CtrlWSDown     = "WS_DISCONNECTED"
)

type catalog struct {
	IndexPath   string `json:"index_path"`
	Spawn       string `json:"spawn"`
	ProxyMode   string `json:"proxy_mode"`
	ProxyInject string `json:"proxy_inject"`
}

type R4 struct{}

func (R4) ID() ID { return Route4 }

func (R4) Run(ctx context.Context, p Params) error {
	if err := PrepareR4(p.Root); err != nil {
		return err
	}
	<-ctx.Done()
	return nil
}

// PrepareR4 checks patch catalog + proxy_shell ports. Frames via Shell IPC.
func PrepareR4(root string) error {
	if err := ensurePatched(root); err != nil {
		return err
	}
	deadline := time.Now().Add(5 * time.Second)
	var last error
	for time.Now().Before(deadline) {
		last = health()
		if last == nil {
			return nil
		}
		time.Sleep(250 * time.Millisecond)
	}
	return last
}

func catalogPath() string {
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".liveaio", "index_patch_catalog.json")
}

func bundledShell(root string) string {
	return filepath.Join(root, "listener", "proxy_shell.exe")
}

func ensurePatched(root string) error {
	raw, err := os.ReadFile(catalogPath())
	if err != nil {
		return fmt.Errorf("companion not patched (no catalog): %w", err)
	}
	var cat catalog
	if err := json.Unmarshal(raw, &cat); err != nil || cat.IndexPath == "" {
		return fmt.Errorf("invalid patch catalog")
	}
	text, err := os.ReadFile(cat.IndexPath)
	if err != nil {
		return fmt.Errorf("read index.js: %w", err)
	}
	body := string(text)
	if cat.Spawn == "" || !strings.Contains(body, cat.Spawn) {
		return fmt.Errorf("index.js missing spawn injection")
	}
	switch cat.ProxyMode {
	case "inject":
		if cat.ProxyInject == "" || !strings.Contains(body, cat.ProxyInject) {
			return fmt.Errorf("index.js missing proxy inject")
		}
	case "replace":
		if !strings.Contains(body, "127.0.0.1:19088") {
			return fmt.Errorf("index.js missing proxy-server")
		}
	default:
		return fmt.Errorf("unknown proxy_mode %q", cat.ProxyMode)
	}
	deployed := filepath.Join(filepath.Dir(cat.IndexPath), shellName)
	src := bundledShell(root)
	if _, err := os.Stat(deployed); err != nil {
		return fmt.Errorf("deployed %s missing", shellName)
	}
	if _, err := os.Stat(src); err == nil {
		a, _ := os.ReadFile(src)
		b, _ := os.ReadFile(deployed)
		if len(a) > 0 && len(b) > 0 && string(a) != string(b) {
			return fmt.Errorf("proxy_shell.exe does not match bundled copy")
		}
	}
	return nil
}

func health() error {
	if !shellRunning() {
		return fmt.Errorf("proxy_shell.exe not running (start companion)")
	}
	if !tcpOpen("127.0.0.1", ipcPort) {
		return fmt.Errorf("IPC port %d not listening", ipcPort)
	}
	if !tcpOpen("127.0.0.1", proxyPort) {
		return fmt.Errorf("proxy TCP port %d not listening", proxyPort)
	}
	return nil
}

func tcpOpen(host string, port int) bool {
	c, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", host, port), 800*time.Millisecond)
	if err != nil {
		return false
	}
	_ = c.Close()
	return true
}

func shellRunning() bool {
	cmd := exec.Command("tasklist", "/FI", "IMAGENAME eq "+shellName, "/NH")
	hideCmd(cmd)
	out, err := cmd.Output()
	if err != nil {
		return false
	}
	return strings.Contains(strings.ToLower(string(out)), strings.ToLower(shellName))
}

// Shell reads length-prefixed packets from proxy_shell IPC (routes 3/4).
type Shell struct {
	Addr    string
	OnCtrl  func(ctrl string)
	OnFrame func(raw []byte)
	OnErr   func(error)

	mu     sync.Mutex
	conn   net.Conn
	stopCh chan struct{}
}

func tokenPath() string {
	home, _ := os.UserHomeDir()
	for _, dir := range []string{
		filepath.Join(home, ".liveaio"),
		filepath.Join(home, ".livehelper"),
	} {
		p := filepath.Join(dir, "ipc_token")
		if _, err := os.Stat(p); err == nil {
			return p
		}
	}
	return filepath.Join(home, ".liveaio", "ipc_token")
}

func ReadToken() (string, error) {
	b, err := os.ReadFile(tokenPath())
	if err != nil {
		return "", err
	}
	return strings.TrimSpace(string(b)), nil
}

func (c *Shell) addr() string {
	if c.Addr != "" {
		return c.Addr
	}
	return DefaultIPCAddr
}

func (c *Shell) Start() error {
	c.mu.Lock()
	if c.stopCh != nil {
		c.mu.Unlock()
		return fmt.Errorf("already started")
	}
	c.stopCh = make(chan struct{})
	c.mu.Unlock()

	token, err := ReadToken()
	if err != nil {
		return fmt.Errorf("ipc token: %w", err)
	}

	var conn net.Conn
	deadline := time.Now().Add(15 * time.Second)
	for {
		conn, err = net.DialTimeout("tcp", c.addr(), 2*time.Second)
		if err == nil {
			break
		}
		if time.Now().After(deadline) {
			return fmt.Errorf("dial proxy_shell IPC: %w", err)
		}
		select {
		case <-c.stopCh:
			return io.ErrClosedPipe
		case <-time.After(300 * time.Millisecond):
		}
	}

	c.mu.Lock()
	c.conn = conn
	c.mu.Unlock()

	if _, err := conn.Write([]byte(token + "\n")); err != nil {
		_ = conn.Close()
		return err
	}

	go c.readLoop(conn)
	return nil
}

func (c *Shell) Stop() {
	c.mu.Lock()
	if c.stopCh != nil {
		select {
		case <-c.stopCh:
		default:
			close(c.stopCh)
		}
	}
	if c.conn != nil {
		_ = c.conn.Close()
		c.conn = nil
	}
	c.mu.Unlock()
}

func (c *Shell) readLoop(conn net.Conn) {
	defer conn.Close()
	r := bufio.NewReader(conn)
	for {
		select {
		case <-c.stopCh:
			return
		default:
		}
		var hdr [4]byte
		if _, err := io.ReadFull(r, hdr[:]); err != nil {
			if c.OnErr != nil && err != io.EOF {
				c.OnErr(err)
			}
			return
		}
		n := binary.BigEndian.Uint32(hdr[:])
		if n == 0 || n > 32<<20 {
			if c.OnErr != nil {
				c.OnErr(fmt.Errorf("bad packet len %d", n))
			}
			return
		}
		buf := make([]byte, n)
		if _, err := io.ReadFull(r, buf); err != nil {
			if c.OnErr != nil {
				c.OnErr(err)
			}
			return
		}
		s := string(buf)
		if strings.HasPrefix(s, CtrlPrefix) {
			ctrl := strings.TrimSpace(strings.TrimPrefix(s, CtrlPrefix))
			if c.OnCtrl != nil {
				c.OnCtrl(ctrl)
			}
			continue
		}
		if c.OnFrame != nil {
			c.OnFrame(buf)
		}
	}
}
