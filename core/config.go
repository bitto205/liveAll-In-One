package core

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
)

type ConfigStore struct {
	path string
	mu   sync.Mutex
}

func NewConfigStore(root string) *ConfigStore {
	if root == "" {
		root = "."
	}
	return &ConfigStore{path: filepath.Join(root, "config.json")}
}

func (s *ConfigStore) Path() string { return s.path }

func (s *ConfigStore) ReadAll() (map[string]any, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.readAllLocked()
}

func (s *ConfigStore) Get(key string) (any, bool, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	cfg, err := s.readAllLocked()
	if err != nil {
		return nil, false, err
	}
	v, ok := cfg[key]
	return v, ok, nil
}

func (s *ConfigStore) Set(key string, value any) error {
	s.mu.Lock()
	defer s.mu.Unlock()
	cfg, err := s.readAllLocked()
	if err != nil {
		return err
	}
	cfg[key] = value
	return s.writeAllLocked(cfg)
}

func (s *ConfigStore) readAllLocked() (map[string]any, error) {
	raw, err := os.ReadFile(s.path)
	if err != nil {
		if os.IsNotExist(err) {
			return map[string]any{}, nil
		}
		return nil, fmt.Errorf("read config: %w", err)
	}
	if len(raw) == 0 {
		return map[string]any{}, nil
	}
	var cfg map[string]any
	if err := json.Unmarshal(raw, &cfg); err != nil {
		return nil, fmt.Errorf("parse config: %w", err)
	}
	if cfg == nil {
		cfg = map[string]any{}
	}
	return cfg, nil
}

func (s *ConfigStore) writeAllLocked(cfg map[string]any) error {
	if cfg == nil {
		cfg = map[string]any{}
	}
	if err := os.MkdirAll(filepath.Dir(s.path), 0755); err != nil {
		return fmt.Errorf("mkdir config dir: %w", err)
	}
	raw, err := json.MarshalIndent(cfg, "", "  ")
	if err != nil {
		return fmt.Errorf("marshal config: %w", err)
	}
	raw = append(raw, '\n')
	if err := os.WriteFile(s.path, raw, 0644); err != nil {
		return fmt.Errorf("write config: %w", err)
	}
	return nil
}
