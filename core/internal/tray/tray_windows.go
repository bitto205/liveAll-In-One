//go:build windows

package tray

import (
	"os"
	"os/exec"
	"path/filepath"
	"runtime"
	"sync"
	"syscall"
	"unsafe"

	"golang.org/x/sys/windows"
)

var (
	shell32           = windows.NewLazySystemDLL("shell32.dll")
	user32            = windows.NewLazySystemDLL("user32.dll")
	procShellNotify   = shell32.NewProc("Shell_NotifyIconW")
	procLoadImage     = user32.NewProc("LoadImageW")
	procCreatePopup   = user32.NewProc("CreatePopupMenu")
	procAppendMenu    = user32.NewProc("AppendMenuW")
	procTrackPopup    = user32.NewProc("TrackPopupMenu")
	procDestroyMenu   = user32.NewProc("DestroyMenu")
	procDefWindowProc = user32.NewProc("DefWindowProcW")
	procRegisterClass = user32.NewProc("RegisterClassExW")
	procCreateWindow  = user32.NewProc("CreateWindowExW")
	procGetMessage    = user32.NewProc("GetMessageW")
	procTranslate     = user32.NewProc("TranslateMessage")
	procDispatch      = user32.NewProc("DispatchMessageW")
	procPostQuit      = user32.NewProc("PostQuitMessage")
	procSetForeground = user32.NewProc("SetForegroundWindow")
	procGetCursorPos  = user32.NewProc("GetCursorPos")
)

const (
	nimAdd    = 0x00000000
	nimModify = 0x00000001
	nimDelete = 0x00000002
	nifMessage = 0x00000001
	nifIcon    = 0x00000002
	nifTip     = 0x00000004

	wmApp        = 0x8000
	wmTray       = wmApp + 1
	wmLButtonDbl = 0x0203
	wmRButtonUp  = 0x0205
	wmCommand    = 0x0111
	wmDestroy    = 0x0002

	idShowUI = 1001
	idQuit   = 1002

	tpmRightButton = 0x0002
)

type notifyIconData struct {
	CbSize           uint32
	HWnd             windows.Handle
	UID              uint32
	UFlags           uint32
	UCallbackMessage uint32
	HIcon            windows.Handle
	SzTip            [128]uint16
}

type wndClassEx struct {
	CbSize        uint32
	Style         uint32
	LpfnWndProc   uintptr
	CbClsExtra    int32
	CbWndExtra    int32
	HInstance     windows.Handle
	HIcon         windows.Handle
	HCursor       windows.Handle
	HbrBackground windows.Handle
	LpszMenuName  *uint16
	LpszClassName *uint16
	HIconSm       windows.Handle
}

type point struct{ X, Y int32 }

type msg struct {
	HWnd    windows.Handle
	Message uint32
	WParam  uintptr
	LParam  uintptr
	Time    uint32
	Pt      point
}

// Config for native tray.
type Config struct {
	Root     string
	Tooltip  string
	OnShowUI func()
	OnQuit   func()
}

var (
	cfgMu sync.Mutex
	cfg   Config
	hwnd  windows.Handle
)

func utf16Tip(s string) (out [128]uint16) {
	u, _ := windows.UTF16FromString(s)
	n := len(u)
	if n > 127 {
		n = 127
	}
	copy(out[:], u[:n])
	return
}

func loadAppIcon(root string) windows.Handle {
	candidates := []string{
		filepath.Join(root, "icon.ico"),
		filepath.Join(root, "resources", "icon.ico"),
		filepath.Join(root, "app.ico"),
	}
	for _, p := range candidates {
		if _, err := os.Stat(p); err != nil {
			continue
		}
		pathPtr, _ := windows.UTF16PtrFromString(p)
		r, _, _ := procLoadImage.Call(
			0,
			uintptr(unsafe.Pointer(pathPtr)),
			1, // IMAGE_ICON
			0, 0,
			0x00000010, // LR_LOADFROMFILE
		)
		if r != 0 {
			return windows.Handle(r)
		}
	}
	// IDI_APPLICATION
	r, _, _ := user32.NewProc("LoadIconW").Call(0, 32512)
	return windows.Handle(r)
}

func wndProc(h windows.Handle, msgU uint32, w, l uintptr) uintptr {
	switch msgU {
	case wmTray:
		switch l {
		case wmLButtonDbl:
			cfgMu.Lock()
			fn := cfg.OnShowUI
			cfgMu.Unlock()
			if fn != nil {
				go fn()
			}
		case wmRButtonUp:
			showMenu(h)
		}
		return 0
	case wmCommand:
		switch w & 0xffff {
		case idShowUI:
			cfgMu.Lock()
			fn := cfg.OnShowUI
			cfgMu.Unlock()
			if fn != nil {
				go fn()
			}
		case idQuit:
			cfgMu.Lock()
			fn := cfg.OnQuit
			cfgMu.Unlock()
			removeIcon(h)
			if fn != nil {
				go fn()
			}
			procPostQuit.Call(0)
		}
		return 0
	case wmDestroy:
		removeIcon(h)
		procPostQuit.Call(0)
		return 0
	}
	r, _, _ := procDefWindowProc.Call(uintptr(h), uintptr(msgU), w, l)
	return r
}

func showMenu(h windows.Handle) {
	menu, _, _ := procCreatePopup.Call()
	if menu == 0 {
		return
	}
	defer procDestroyMenu.Call(menu)
	show, _ := windows.UTF16PtrFromString("打开界面")
	quit, _ := windows.UTF16PtrFromString("退出")
	procAppendMenu.Call(menu, 0, idShowUI, uintptr(unsafe.Pointer(show)))
	procAppendMenu.Call(menu, 0, idQuit, uintptr(unsafe.Pointer(quit)))

	var pt point
	procGetCursorPos.Call(uintptr(unsafe.Pointer(&pt)))
	procSetForeground.Call(uintptr(h))
	procTrackPopup.Call(menu, tpmRightButton, uintptr(pt.X), uintptr(pt.Y), 0, uintptr(h), 0)
}

func addIcon(h windows.Handle, tip string, icon windows.Handle) {
	var nid notifyIconData
	nid.CbSize = uint32(unsafe.Sizeof(nid))
	nid.HWnd = h
	nid.UID = 1
	nid.UFlags = nifMessage | nifIcon | nifTip
	nid.UCallbackMessage = wmTray
	nid.HIcon = icon
	nid.SzTip = utf16Tip(tip)
	procShellNotify.Call(nimAdd, uintptr(unsafe.Pointer(&nid)))
}

func removeIcon(h windows.Handle) {
	var nid notifyIconData
	nid.CbSize = uint32(unsafe.Sizeof(nid))
	nid.HWnd = h
	nid.UID = 1
	procShellNotify.Call(nimDelete, uintptr(unsafe.Pointer(&nid)))
}

// Run blocks on the Win32 message loop (call from main / dedicated OS thread).
func Run(c Config) error {
	runtime.LockOSThread()
	cfgMu.Lock()
	cfg = c
	cfgMu.Unlock()

	hInst := windows.Handle(0)
	r0, _, _ := windows.NewLazySystemDLL("kernel32.dll").NewProc("GetModuleHandleW").Call(0)
	if r0 != 0 {
		hInst = windows.Handle(r0)
	}

	className, _ := windows.UTF16PtrFromString("LiveAIOCoreTrayWnd")
	var wcx wndClassEx
	wcx.CbSize = uint32(unsafe.Sizeof(wcx))
	wcx.LpfnWndProc = syscall.NewCallback(wndProc)
	wcx.HInstance = hInst
	wcx.LpszClassName = className
	atom, _, regErr := procRegisterClass.Call(uintptr(unsafe.Pointer(&wcx)))
	if atom == 0 {
		// already registered is ok
		if errno, ok := regErr.(syscall.Errno); !ok || errno != 1410 { // ERROR_CLASS_ALREADY_EXISTS
			if regErr != windows.ERROR_CLASS_ALREADY_EXISTS && regErr != syscall.Errno(1410) {
				// still try create; some hosts return confusing errs
			}
		}
	}

	title, _ := windows.UTF16PtrFromString("LiveAIOCore")
	const hwndMessage = ^windows.Handle(2) // HWND_MESSAGE == -3
	h, _, createErr := procCreateWindow.Call(
		0,
		uintptr(unsafe.Pointer(className)),
		uintptr(unsafe.Pointer(title)),
		0, 0, 0, 0, 0,
		uintptr(hwndMessage),
		0,
		uintptr(hInst),
		0,
	)
	if h == 0 {
		if createErr != nil {
			return createErr
		}
		return syscall.EINVAL
	}
	hwnd = windows.Handle(h)
	icon := loadAppIcon(c.Root)
	tip := c.Tooltip
	if tip == "" {
		tip = "LiveAIO"
	}
	addIcon(hwnd, tip, icon)

	var m msg
	for {
		r, _, _ := procGetMessage.Call(uintptr(unsafe.Pointer(&m)), 0, 0, 0)
		if int32(r) <= 0 {
			break
		}
		procTranslate.Call(uintptr(unsafe.Pointer(&m)))
		procDispatch.Call(uintptr(unsafe.Pointer(&m)))
	}
	return nil
}

// LaunchUI starts the Python/Qt UI (attach-only; core must already run).
func LaunchUI(root string) error {
	py := filepath.Join(root, ".venv", "Scripts", "python.exe")
	uiPy := filepath.Join(root, "ui_shell.py")
	var cmd *exec.Cmd
	if _, err := os.Stat(py); err == nil {
		cmd = exec.Command(py, uiPy)
	} else {
		cmd = exec.Command("python", uiPy)
	}
	cmd.Dir = root
	cmd.Env = append(os.Environ(), "LIVEAIO_ROOT="+root)
	creation := uintptr(0x08000000) // CREATE_NO_WINDOW for helper only
	cmd.SysProcAttr = &syscall.SysProcAttr{CreationFlags: uint32(creation)}
	return cmd.Start()
}
