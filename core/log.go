package core

import (
	"io"
	"log/slog"
	"os"
	"path/filepath"
)

func FileLogger(root, name string) *slog.Logger {
	dir := filepath.Join(root, "log")
	_ = os.MkdirAll(dir, 0755)
	path := filepath.Join(dir, name)
	f, err := os.OpenFile(path, os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0644)
	if err != nil {
		return slog.New(slog.NewTextHandler(os.Stderr, nil))
	}
	w := io.MultiWriter(os.Stderr, f)
	return slog.New(slog.NewTextHandler(w, &slog.HandlerOptions{Level: slog.LevelInfo}))
}
