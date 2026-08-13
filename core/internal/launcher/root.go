package launcher

import (
	"os"
	"path/filepath"
)

// ResolveAppRoot finds the LiveAIO repo root from the running executable.
func ResolveAppRoot(exePath string) string {
	if env := os.Getenv("LIVEAIO_ROOT"); env != "" {
		return env
	}
	if exePath == "" {
		var err error
		exePath, err = os.Executable()
		if err != nil {
			return "."
		}
	}
	dir := filepath.Dir(exePath)
	base := filepath.Base(dir)
	// .../core/dist/liveaio-core.exe → repo root
	if base == "dist" {
		if filepath.Base(filepath.Dir(dir)) == "core" {
			return filepath.Dir(filepath.Dir(dir))
		}
	}
	// .../core/liveaio-core.exe
	if base == "core" {
		return filepath.Dir(dir)
	}
	return dir
}

func ChdirRoot(root string) error {
	if root == "" || root == "." {
		return nil
	}
	return os.Chdir(root)
}
