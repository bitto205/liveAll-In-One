// Package connectdiag — connect failure diagnosis (routes 1/2/4; route 3 uses manual lifecycle).
package connectdiag

import (
	"context"
	"errors"
	"fmt"
	"net"
	"net/http"
	"os/exec"
	"strings"
	"syscall"
	"time"

	"github.com/chromedp/chromedp"
)

const (
	ProxyShellName      = "proxy_shell.exe"
	ProxyShellIPCPort   = 19098
	ProxyShellProxyPort = 19088

	BadRoomMinWait = 4 * time.Second

	MsgConnected  = "直播间已连接"
	MsgNotLiving  = "直播间未开播"
	MsgBadRoom    = "房间号错误，请检查房间号"
	MsgTimeoutNet = "连接超时，请检查您的网络连接与配置，可在github提出issue"
	MsgTimeout    = "连接超时，请检查您的网络连接"
)

type ConnectCode string

const (
	CodeConnected  ConnectCode = "connected"
	CodeNotLiving  ConnectCode = "not_living"
	CodeBadRoom    ConnectCode = "bad_room"
	CodeTimeoutNet ConnectCode = "timeout_net"
	CodeTimeout    ConnectCode = "timeout"
)

// ConnectError is a user-facing connect outcome (cases 2–5).
type ConnectError struct {
	Code   ConnectCode
	Detail string
}

func (e *ConnectError) Error() string {
	if e == nil {
		return ""
	}
	base := MsgTimeout
	switch e.Code {
	case CodeNotLiving:
		base = MsgNotLiving
	case CodeBadRoom:
		base = MsgBadRoom
	case CodeTimeoutNet:
		base = MsgTimeoutNet
	case CodeTimeout:
		base = MsgTimeout
	}
	if e.Detail == "" {
		return base
	}
	return base + "\n" + e.Detail
}

func ErrNotLiving() error  { return &ConnectError{Code: CodeNotLiving} }
func ErrBadRoom() error   { return &ConnectError{Code: CodeBadRoom} }
func ErrTimeoutNet() error { return &ConnectError{Code: CodeTimeoutNet} }
func ErrTimeout() error   { return &ConnectError{Code: CodeTimeout} }
func ErrWithDetail(code ConnectCode, detail string) error {
	return &ConnectError{Code: code, Detail: strings.TrimSpace(detail)}
}

func AsConnectError(err error) (*ConnectError, bool) {
	var ce *ConnectError
	if errors.As(err, &ce) {
		return ce, true
	}
	return nil, false
}

// PageHints from a title probe at connect failure time.
type PageHints struct {
	Title          string
	AnchorLiveRoom bool
	LandingPage    bool
}

func ReadPageTitle(ctx context.Context) (string, error) {
	var title string
	err := chromedp.Run(ctx, chromedp.Evaluate(`document.title || ''`, &title))
	return strings.TrimSpace(title), err
}

func PageHintsFromTitle(title string) PageHints {
	return PageHints{
		Title:          title,
		AnchorLiveRoom: isAnchorLiveTitle(title),
		LandingPage:    isLandingTitle(title),
	}
}

func isAnchorLiveTitle(title string) bool {
	title = strings.TrimSpace(title)
	if title == "" || isLandingTitle(title) {
		return false
	}
	return strings.Contains(title, "的抖音直播间")
}

func isLandingTitle(title string) bool {
	title = strings.TrimSpace(title)
	return strings.Contains(title, "抖音直播电脑版") ||
		strings.Contains(title, "抖音直播网页版入口")
}

// BrowserStuck reports no WSS and no enter progress — safe to run failure diagnosis.
func BrowserStuck(enterReqN int, enterSeen, wssOK bool) bool {
	return !wssOK && !enterSeen && enterReqN == 0
}

// TryBadRoom probes title once when stuck; returns ErrBadRoom or nil.
func TryBadRoom(ctx context.Context, enterReqN int, enterSeen bool, sinceNav time.Duration) error {
	if enterSeen || enterReqN > 0 || sinceNav < BadRoomMinWait {
		return nil
	}
	title, err := ReadPageTitle(ctx)
	if err != nil {
		return nil
	}
	if isLandingTitle(title) {
		return ErrBadRoom()
	}
	return nil
}

// ClassifyBrowserFault maps a final timeout (no WSS / hung enter) to case 3/4/5.
// Runs InternetReachable only here — not during normal capture.
func ClassifyBrowserFault(ctx context.Context, enterReqN int, enterSeen bool, hints *PageHints) error {
	h := PageHints{}
	if hints != nil {
		h = *hints
	}
	if h.Title == "" && !h.LandingPage && !h.AnchorLiveRoom {
		if title, err := ReadPageTitle(ctx); err == nil {
			h = PageHintsFromTitle(title)
		}
	}
	if !enterSeen && enterReqN == 0 && h.LandingPage {
		return ErrBadRoom()
	}
	if h.AnchorLiveRoom || enterReqN > 0 {
		if InternetReachable() {
			return ErrTimeoutNet()
		}
		return ErrTimeout()
	}
	if InternetReachable() {
		return ErrTimeoutNet()
	}
	return ErrTimeout()
}

// InternetReachable reports whether outbound HTTP to common hosts works.
func InternetReachable() bool {
	client := &http.Client{Timeout: 4 * time.Second}
	for _, url := range []string{
		"https://www.douyin.com/",
		"https://www.baidu.com/",
	} {
		resp, err := client.Head(url)
		if err != nil {
			continue
		}
		_ = resp.Body.Close()
		if resp.StatusCode > 0 && resp.StatusCode < 500 {
			return true
		}
	}
	return false
}

// ProxyShellStatus is a point-in-time proxy_shell / IPC health snapshot.
type ProxyShellStatus struct {
	ProcessRunning bool
	IPCListening   bool
	ProxyListening bool
	IPCPort        int
	ProxyPort      int
}

// ProxyShellPortsOpen is a lightweight poll (TCP only, no process scan).
func ProxyShellPortsOpen() bool {
	return TCPOpen("127.0.0.1", ProxyShellIPCPort) &&
		TCPOpen("127.0.0.1", ProxyShellProxyPort)
}

// InspectProxyShell checks process + TCP ports (call on connect failure only).
func InspectProxyShell() ProxyShellStatus {
	return ProxyShellStatus{
		ProcessRunning: proxyShellRunning(),
		IPCListening:   TCPOpen("127.0.0.1", ProxyShellIPCPort),
		ProxyListening: TCPOpen("127.0.0.1", ProxyShellProxyPort),
		IPCPort:        ProxyShellIPCPort,
		ProxyPort:      ProxyShellProxyPort,
	}
}

func (s ProxyShellStatus) Diagnostic() string {
	proc := "未运行"
	if s.ProcessRunning {
		proc = "运行中"
	}
	ipc := "未监听"
	if s.IPCListening {
		ipc = "正常"
	}
	proxy := "未监听"
	if s.ProxyListening {
		proxy = "正常"
	}
	return strings.Join([]string{
		fmt.Sprintf("proxy_shell.exe: %s", proc),
		fmt.Sprintf("IPC %d: %s", s.IPCPort, ipc),
		fmt.Sprintf("代理 %d: %s", s.ProxyPort, proxy),
	}, "\n")
}

func (s ProxyShellStatus) OK() bool {
	return s.ProcessRunning && s.IPCListening && s.ProxyListening
}

// ErrProxyShellConnect wraps proxy_shell IPC / health failures with full diagnostics.
func ErrProxyShellConnect(cause error) error {
	st := InspectProxyShell()
	detail := st.Diagnostic()
	if cause != nil && strings.TrimSpace(cause.Error()) != "" {
		detail += "\n" + cause.Error()
	}
	code := CodeTimeout
	if InternetReachable() {
		code = CodeTimeoutNet
	}
	return ErrWithDetail(code, detail)
}

func TCPOpen(host string, port int) bool {
	c, err := net.DialTimeout("tcp", fmt.Sprintf("%s:%d", host, port), 800*time.Millisecond)
	if err != nil {
		return false
	}
	_ = c.Close()
	return true
}

func proxyShellRunning() bool {
	cmd := exec.Command("tasklist", "/FI", "IMAGENAME eq "+ProxyShellName, "/NH")
	cmd.SysProcAttr = &syscall.SysProcAttr{HideWindow: true, CreationFlags: 0x08000000}
	out, err := cmd.Output()
	if err != nil {
		return false
	}
	return strings.Contains(strings.ToLower(string(out)), strings.ToLower(ProxyShellName))
}
