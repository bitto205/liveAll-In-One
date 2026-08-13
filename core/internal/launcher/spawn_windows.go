//go:build windows

package launcher

import (
	"fmt"
	"os"
	"os/exec"
	"syscall"
)

// SpawnCore starts liveaio-core detached from the current process tree.
func SpawnCore(coreExe, root, tcp string) error {
	cmd := exec.Command(coreExe, "--root", root, "--tcp", tcp)
	cmd.Dir = root
	cmd.Env = append(os.Environ(), "LIVEAIO_ROOT="+root)
	cmd.SysProcAttr = &syscall.SysProcAttr{
		CreationFlags: syscall.CREATE_NEW_PROCESS_GROUP,
	}
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("spawn core: %w", err)
	}
	return nil
}
