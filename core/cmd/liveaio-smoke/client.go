package main

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"time"

	"liveaio/core/internal/protocol"
)

type ipcClient struct {
	addr string
	conn net.Conn
}

func dialIPC(addr string, timeout time.Duration) (*ipcClient, error) {
	if addr == "" {
		addr = protocol.DefaultTCPAddr
	}
	conn, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return nil, err
	}
	_ = conn.SetDeadline(time.Now().Add(timeout))
	return &ipcClient{addr: addr, conn: conn}, nil
}

func (c *ipcClient) close() error {
	if c == nil || c.conn == nil {
		return nil
	}
	return c.conn.Close()
}

func (c *ipcClient) send(env map[string]any) error {
	b, err := json.Marshal(env)
	if err != nil {
		return err
	}
	b = append(b, '\n')
	_, err = c.conn.Write(b)
	return err
}

func (c *ipcClient) readLine(timeout time.Duration) (map[string]any, error) {
	if timeout > 0 {
		_ = c.conn.SetDeadline(time.Now().Add(timeout))
	}
	line, err := bufio.NewReader(c.conn).ReadBytes('\n')
	if err != nil {
		return nil, err
	}
	var env map[string]any
	if err := json.Unmarshal(line, &env); err != nil {
		return nil, err
	}
	return env, nil
}

func waitReady(c *ipcClient, timeout time.Duration) (map[string]any, error) {
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		env, err := c.readLine(time.Until(deadline))
		if err != nil {
			return nil, err
		}
		if env["op"] == protocol.OpReady {
			return env, nil
		}
	}
	return nil, fmt.Errorf("ready timeout")
}

func pingPong(c *ipcClient, timeout time.Duration) error {
	if err := c.send(map[string]any{"op": protocol.OpPing, "id": "smoke"}); err != nil {
		return err
	}
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		env, err := c.readLine(time.Until(deadline))
		if err != nil {
			return err
		}
		if env["op"] == protocol.OpPong {
			return nil
		}
	}
	return fmt.Errorf("pong timeout")
}

func smokeCheck(addr string, timeout time.Duration) error {
	c, err := dialIPC(addr, timeout)
	if err != nil {
		return fmt.Errorf("dial: %w", err)
	}
	defer c.close()

	if _, err := waitReady(c, timeout); err != nil {
		return fmt.Errorf("ready: %w", err)
	}
	if err := pingPong(c, timeout); err != nil {
		return fmt.Errorf("ping: %w", err)
	}
	return nil
}
