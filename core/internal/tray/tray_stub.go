//go:build !windows

package tray

import "fmt"

type Config struct {
	Root     string
	Tooltip  string
	OnShowUI func()
	OnQuit   func()
}

func Run(c Config) error {
	return fmt.Errorf("native tray only on windows")
}

func LaunchUI(root string) error {
	return fmt.Errorf("LaunchUI only on windows")
}
