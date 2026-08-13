package launcher

import (
	"fmt"
	"time"
)

// WaitCore polls until TCP accepts or timeout.
func WaitCore(addr string, timeout time.Duration) error {
	if addr == "" {
		addr = "127.0.0.1:19877"
	}
	deadline := time.Now().Add(timeout)
	for time.Now().Before(deadline) {
		if ProbeTCP(addr) {
			return nil
		}
		time.Sleep(100 * time.Millisecond)
	}
	return fmt.Errorf("core not ready on %s", addr)
}
