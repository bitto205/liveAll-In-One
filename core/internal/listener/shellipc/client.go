package shellipc

import (
	"bufio"
	"encoding/binary"
	"fmt"
	"io"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"
)

const (
	DefaultIPCAddr = "127.0.0.1:19098"
	CtrlPrefix     = "__LH_CTRL__:"
	CtrlLiveOn     = "LIVE_ON_AIR:true"
	CtrlLiveOff    = "LIVE_ON_AIR:false"
	CtrlWSOpen     = "WS_OPEN"
	CtrlWSDown     = "WS_DISCONNECTED"
)

// Client reads length-prefixed packets from proxy_shell IPC.
type Client struct {
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

func (c *Client) addr() string {
	if c.Addr != "" {
		return c.Addr
	}
	return DefaultIPCAddr
}

// Start dials, auths, and loops until Stop.
func (c *Client) Start() error {
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

func (c *Client) Stop() {
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

func (c *Client) readLoop(conn net.Conn) {
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
