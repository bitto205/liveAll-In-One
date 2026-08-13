//go:build !windows

package launcher

import (
	"fmt"
	"os"
	"os/exec"
)

func SpawnCore(coreExe, root, tcp string) error {
	cmd := exec.Command(coreExe, "--root", root, "--tcp", tcp)
	cmd.Dir = root
	cmd.Env = append(os.Environ(), "LIVEAIO_ROOT="+root)
	if err := cmd.Start(); err != nil {
		return fmt.Errorf("spawn core: %w", err)
	}
	return nil
}
