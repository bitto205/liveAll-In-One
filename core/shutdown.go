package core

import (
	"bufio"
	"encoding/json"
	"fmt"
	"net"
	"time"
)

// ShutdownCore asks a running core to exit via IPC.
func ShutdownCore(addr string, timeout time.Duration) error {
	if addr == "" {
		addr = DefaultTCPAddr
	}
	conn, err := net.DialTimeout("tcp", addr, timeout)
	if err != nil {
		return fmt.Errorf("dial: %w", err)
	}
	defer conn.Close()

	_ = conn.SetDeadline(time.Now().Add(timeout))
	r := bufio.NewReader(conn)
	line, err := r.ReadBytes('\n')
	if err != nil {
		return fmt.Errorf("ready: %w", err)
	}
	var env map[string]any
	if err := json.Unmarshal(line, &env); err != nil {
		return fmt.Errorf("ready json: %w", err)
	}
	if env["op"] != OpReady {
		return fmt.Errorf("expected ready, got %v", env["op"])
	}

	payload, _ := json.Marshal(map[string]any{"op": OpShutdown})
	payload = append(payload, '\n')
	if _, err := conn.Write(payload); err != nil {
		return fmt.Errorf("shutdown: %w", err)
	}
	return nil
}
