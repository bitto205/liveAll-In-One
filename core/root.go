package core

import (
	"os"
	"path/filepath"
)

// ResolveAppRoot finds the LiveAIO app root from the running executable.
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
	// 发布目录（exe 旁有 Core DLL + resources/browsers）优先，避免包放在仓库
	// build/ 下时误走到仓库根目录。
	if isPackagedRoot(dir) {
		return dir
	}
	for d := dir; ; d = filepath.Dir(d) {
		if isAppRoot(d) {
			return d
		}
		parent := filepath.Dir(d)
		if parent == d {
			break
		}
	}
	return dir
}

func isPackagedRoot(root string) bool {
	if _, err := os.Stat(filepath.Join(root, "LiveAIOCore.dll")); err != nil {
		return false
	}
	if _, err := os.Stat(filepath.Join(root, "resources")); err == nil {
		return true
	}
	_, err := os.Stat(filepath.Join(root, "browsers"))
	return err == nil
}

func isAppRoot(root string) bool {
	if _, err := os.Stat(filepath.Join(root, "go.mod")); err == nil {
		if _, err := os.Stat(filepath.Join(root, "main")); err == nil {
			if _, err := os.Stat(filepath.Join(root, "resources")); err == nil {
				return true
			}
		}
	}
	for _, probe := range []string{
		filepath.Join(root, "build", "build_work", "custom", "LiveAIOCore.dll"),
		filepath.Join(root, "build", "build_work", "custom", "LiveAIO.exe"),
		filepath.Join(root, "build", "liveaio.mod"),
	} {
		if _, err := os.Stat(probe); err == nil {
			return true
		}
	}
	return false
}

func ChdirRoot(root string) error {
	if root == "" || root == "." {
		return nil
	}
	return os.Chdir(root)
}
