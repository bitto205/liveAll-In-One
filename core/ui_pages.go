package core

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"syscall"
	"time"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	pagesInitMu         sync.Mutex
	pagesProcMu         sync.Mutex
	pagesRun            uintptr
	pagesShow           uintptr
	pagesSetStartHidden uintptr
	pagesOverlayCommand uintptr
	pagesOverlayState   uintptr
	pagesDir            string
	pagesRunning        bool
	pagesEverRan        bool
)

// OpenPages loads LiveAIOPages.dll (once) and shows the pages UI.
func OpenPages(root string) error {
	pagesInitMu.Lock()
	if pagesRun == 0 {
		if err := initPagesDLL(root); err != nil {
			pagesInitMu.Unlock()
			return err
		}
	}
	pagesInitMu.Unlock()
	return showPages(root)
}

// prepareQtEnv points Qt at plugins next to LiveAIOPages.dll.
// Required for F5/debug: the Go exe is not in the same directory as qwindows.dll.
func prepareQtEnv(dllDir, root string) {
	_ = windows.SetDllDirectory(dllDir)
	_ = os.Setenv("PATH", dllDir+";"+os.Getenv("PATH"))
	_ = os.Setenv("LIVEAIO_ROOT", root)
	_ = os.Setenv("QT_PLUGIN_PATH", dllDir)
	_ = os.Setenv("QT_QPA_PLATFORM_PLUGIN_PATH", filepath.Join(dllDir, "platforms"))
}

func initPagesDLL(root string) error {
	dllPath, err := findPagesDLL(root)
	if err != nil {
		return err
	}
	dir := filepath.Dir(dllPath)
	prepareQtEnv(dir, root)

	h, err := windows.LoadLibraryEx(dllPath, 0, windows.LOAD_WITH_ALTERED_SEARCH_PATH)
	if err != nil {
		return fmt.Errorf("LoadLibrary %s: %w", dllPath, err)
	}
	proc, err := windows.GetProcAddress(h, "LiveAIO_PagesRun")
	if err != nil {
		_ = windows.FreeLibrary(h)
		return fmt.Errorf("GetProcAddress LiveAIO_PagesRun: %w", err)
	}
	pagesRun = proc
	if show, err := windows.GetProcAddress(h, "LiveAIO_PagesShow"); err == nil {
		pagesShow = show
	}
	if proc, err := windows.GetProcAddress(h, "LiveAIO_PagesSetStartHidden"); err == nil {
		pagesSetStartHidden = proc
	}
	if proc, err := windows.GetProcAddress(h, "LiveAIO_PagesOverlayCommand"); err == nil {
		pagesOverlayCommand = proc
	}
	if proc, err := windows.GetProcAddress(h, "LiveAIO_PagesOverlayState"); err == nil {
		pagesOverlayState = proc
	}
	pagesDir = dir
	_ = h // keep module loaded for process lifetime
	return nil
}

// OverlayCommand controls a transparent tool window without raising the main UI.
func OverlayCommand(root, tool, action string) error {
	pagesInitMu.Lock()
	if pagesRun == 0 {
		if err := initPagesDLL(root); err != nil {
			pagesInitMu.Unlock()
			return err
		}
	}
	proc := pagesOverlayCommand
	pagesInitMu.Unlock()
	if proc == 0 {
		return fmt.Errorf("LiveAIO_PagesOverlayCommand export not found")
	}

	pagesProcMu.Lock()
	running := pagesRunning
	pagesProcMu.Unlock()
	startedHidden := false
	if !running {
		if pagesSetStartHidden != 0 {
			_, _, _ = syscall.SyscallN(pagesSetStartHidden, 1)
		}
		if err := showPages(root); err != nil {
			return err
		}
		startedHidden = true
	}

	toolPtr, _ := syscall.BytePtrFromString(tool)
	actionPtr, _ := syscall.BytePtrFromString(action)
	deadline := time.Now().Add(5 * time.Second)
	for {
		rc, _, _ := syscall.SyscallN(
			proc,
			uintptr(unsafe.Pointer(toolPtr)),
			uintptr(unsafe.Pointer(actionPtr)),
		)
		if rc == 0 {
			runtime.KeepAlive(toolPtr)
			runtime.KeepAlive(actionPtr)
			return nil
		}
		if !startedHidden || time.Now().After(deadline) {
			return fmt.Errorf("overlay command %s/%s failed: %d", tool, action, rc)
		}
		time.Sleep(50 * time.Millisecond)
	}
}

// OverlayState returns bits 1=open, 2=frame shown, 4=locked without starting UI.
func OverlayState(tool string) int {
	pagesProcMu.Lock()
	running := pagesRunning
	pagesProcMu.Unlock()
	if !running || pagesOverlayState == 0 {
		return 0
	}
	toolPtr, _ := syscall.BytePtrFromString(tool)
	state, _, _ := syscall.SyscallN(pagesOverlayState, uintptr(unsafe.Pointer(toolPtr)))
	runtime.KeepAlive(toolPtr)
	return int(state)
}

func findPagesDLL(root string) (string, error) {
	candidates := []string{
		filepath.Join(root, "build", "build_work", "custom", "LiveAIOPages.dll"),
	}
	verDir := filepath.Join(root, "build", "build_work")
	if st, err := os.Stat(verDir); err == nil && st.IsDir() {
		entries, _ := os.ReadDir(verDir)
		for _, e := range entries {
			if !e.IsDir() || e.Name() == "custom" {
				continue
			}
			candidates = append(candidates, filepath.Join(verDir, e.Name(), "LiveAIOPages.dll"))
		}
	}
	for _, p := range candidates {
		if st, err := os.Stat(p); err == nil && !st.IsDir() {
			return p, nil
		}
	}
	return "", fmt.Errorf("LiveAIOPages.dll not found under build/build_work/custom or build/build_work/<version>")
}

func showPages(root string) error {
	pagesProcMu.Lock()
	defer pagesProcMu.Unlock()

	if pagesRunning {
		// 一个进程只能有一个 QApplication，界面已在跑就只让它抬窗。
		if pagesShow != 0 {
			if ok, _, _ := syscall.SyscallN(pagesShow); ok != 0 {
				return nil
			}
		}
		return nil
	}
	if pagesRun == 0 {
		return fmt.Errorf("pages module not loaded")
	}
	if pagesEverRan {
		// 界面退出即整体退出；重新起一个 QApplication 会让 Qt 崩在静态状态上。
		return fmt.Errorf("pages UI already exited")
	}
	if pagesDir != "" {
		prepareQtEnv(pagesDir, root)
	} else {
		_ = os.Setenv("LIVEAIO_ROOT", root)
	}
	pagesRunning = true
	pagesEverRan = true

	go func() {
		runtime.LockOSThread()
		defer runtime.UnlockOSThread()

		argv0, _ := syscall.BytePtrFromString(os.Args[0])
		argv := []*byte{argv0, nil}
		_, _, _ = syscall.SyscallN(pagesRun, uintptr(1), uintptr(unsafe.Pointer(&argv[0])))
		pagesProcMu.Lock()
		pagesRunning = false
		pagesProcMu.Unlock()
	}()
	return nil
}

// LoginUIState reads state.json and returns (display text, login button enabled).
// Pages query via IPC (ui.command login.query); they must not read files directly.
func LoginUIState(root string) (text string, canLogin bool) {
	path := filepath.Join(root, "state.json")
	raw, err := os.ReadFile(path)
	if err != nil {
		return "未登录", true
	}
	var state map[string]any
	if err := json.Unmarshal(raw, &state); err != nil {
		return "登录状态读取失败", true
	}
	cookies, _ := state["cookies"].([]any)
	now := time.Now().Unix()
	for _, c := range cookies {
		m, ok := c.(map[string]any)
		if !ok {
			continue
		}
		name, _ := m["name"].(string)
		if name != "sessionid" {
			continue
		}
		val, _ := m["value"].(string)
		if val == "" {
			return "未找到登录凭证", true
		}
		exp := int64(-1)
		switch v := m["expires"].(type) {
		case float64:
			exp = int64(v)
		case int64:
			exp = v
		case int:
			exp = int64(v)
		}
		if exp > 0 && exp < now {
			return "登录已过期", true
		}
		if exp <= 0 {
			return "已登录", false
		}
		days := (exp - now) / 86400
		if days < 0 {
			days = 0
		}
		return fmt.Sprintf("已登录，还剩约 %d 天", days), false
	}
	return "未找到登录凭证", true
}

// DefaultRouteEnv is a UI-safe env snapshot when listener helpers are unavailable.
func DefaultRouteEnv(route string) map[string]any {
	return map[string]any{
		"route":   route,
		"ok":      true,
		"ready":   true,
		"message": "",
		"details": map[string]any{},
	}
}
