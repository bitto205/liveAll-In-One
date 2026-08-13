package launcher

import (
	"fmt"
	"os"
	"path/filepath"
)

// FindCoreExe locates liveaio-core next to the launcher or under core/dist/.
func FindCoreExe(root, launcherPath string) (string, error) {
	if root == "" {
		root = "."
	}
	candidates := []string{
		filepath.Join(root, "core", "dist", "liveaio-core.exe"),
		filepath.Join(root, "core", "liveaio-core.exe"),
	}
	if launcherPath != "" {
		candidates = append(candidates, filepath.Join(filepath.Dir(launcherPath), "liveaio-core.exe"))
	}
	for _, p := range candidates {
		if st, err := os.Stat(p); err == nil && !st.IsDir() {
			return p, nil
		}
	}
	return "", fmt.Errorf("liveaio-core.exe not found (root=%s)", root)
}
