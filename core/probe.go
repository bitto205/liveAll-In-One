package core

import (
	"net"
	"time"
)

// ProbeTCP returns true if something accepts on addr (e.g. core already running).
func ProbeTCP(addr string) bool {
	conn, err := net.DialTimeout("tcp", addr, 400*time.Millisecond)
	if err != nil {
		return false
	}
	_ = conn.Close()
	return true
}
