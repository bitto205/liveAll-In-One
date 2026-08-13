//go:build !windows

package launcher

func EnsureAdmin(exePath, args, workDir string) error {
	return nil
}

func BuildElevatedParams(argv []string) string {
	return ""
}
