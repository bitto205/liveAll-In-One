package listener

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
	"sync"
	"time"

	"github.com/chromedp/cdproto/cdp"
	"github.com/chromedp/cdproto/network"
	"github.com/chromedp/cdproto/runtime"
	"github.com/chromedp/chromedp"
)

const (
	defaultUA    = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36"
	envUseSystem = "LIVEAIO_USE_SYSTEM_BROWSER"
)

// BrowserOptions controls Chromium launch for routes 1/2 and login.
type BrowserOptions struct {
	Root          string
	ForceSystem   bool
	Headless      bool
	TrimResources bool
	UserDataDir   string
}

// BrowserSession is one chromedp context.
type BrowserSession struct {
	Ctx         context.Context
	System      bool
	cancel      context.CancelFunc
	allocCancel context.CancelFunc
}

func statePath(root string) string { return filepath.Join(root, "state.json") }

func preferSystemFromConfig(root string) bool {
	raw, err := os.ReadFile(filepath.Join(root, "config.json"))
	if err != nil {
		return false
	}
	var m map[string]any
	if json.Unmarshal(raw, &m) != nil {
		return false
	}
	v, _ := m["use_system_browser"].(bool)
	return v
}

func findBundledExe(root string) string {
	browsers := filepath.Join(root, "browsers")
	entries, err := os.ReadDir(browsers)
	if err != nil {
		return ""
	}
	var latestPath, latestName string
	for _, e := range entries {
		if !e.IsDir() || !strings.HasPrefix(e.Name(), "chromium_headless_shell-") {
			continue
		}
		for _, sub := range []string{"chrome-headless-shell-win64", "chrome-headless-shell-win32"} {
			p := filepath.Join(browsers, e.Name(), sub, "chrome-headless-shell.exe")
			if st, err := os.Stat(p); err == nil && !st.IsDir() && e.Name() >= latestName {
				latestName = e.Name()
				latestPath = p
			}
		}
	}
	return latestPath
}

func findSystemBrowser() string {
	local := os.Getenv("LOCALAPPDATA")
	pf := os.Getenv("ProgramFiles")
	pf86 := os.Getenv("ProgramFiles(x86)")
	for _, base := range []string{local, pf, pf86} {
		if base == "" {
			continue
		}
		for _, p := range []string{
			filepath.Join(base, "Google", "Chrome", "Application", "chrome.exe"),
			filepath.Join(base, "Microsoft", "Edge", "Application", "msedge.exe"),
		} {
			if st, err := os.Stat(p); err == nil && !st.IsDir() {
				return p
			}
		}
	}
	return ""
}

func launchBrowser(parent context.Context, opt BrowserOptions) (*BrowserSession, error) {
	forceSystem := opt.ForceSystem || preferSystemFromConfig(opt.Root) || os.Getenv(envUseSystem) == "1"
	exe := ""
	system := false
	if !forceSystem {
		exe = findBundledExe(opt.Root)
	}
	if exe == "" {
		exe = findSystemBrowser()
		system = true
		if exe == "" {
			return nil, fmt.Errorf("no Chromium found (browsers/ or system Chrome/Edge)")
		}
	}
	opts := append(chromedp.DefaultExecAllocatorOptions[:],
		chromedp.ExecPath(exe),
		chromedp.Flag("disable-blink-features", "AutomationControlled"),
		chromedp.Flag("no-sandbox", true),
		chromedp.UserAgent(defaultUA),
		chromedp.WindowSize(1920, 1080),
	)
	if opt.Headless {
		opts = append(opts, chromedp.Flag("headless", "new"))
	} else {
		opts = append(opts, chromedp.Flag("headless", false))
	}
	if opt.UserDataDir != "" {
		opts = append(opts, chromedp.UserDataDir(opt.UserDataDir))
	}
	if opt.TrimResources {
		opts = append(opts, chromedp.Flag("blink-settings", "imagesEnabled=false"))
	}
	allocCtx, allocCancel := chromedp.NewExecAllocator(parent, opts...)
	ctx, cancel := chromedp.NewContext(allocCtx)
	s := &BrowserSession{Ctx: ctx, System: system, cancel: cancel, allocCancel: allocCancel}
	if err := chromedp.Run(ctx); err != nil {
		s.Close()
		return nil, fmt.Errorf("chromedp start: %w", err)
	}
	_ = chromedp.Run(ctx, chromedp.ActionFunc(func(ctx context.Context) error {
		return network.Enable().Do(ctx)
	}))
	_ = applyStorageState(ctx, statePath(opt.Root))
	_ = chromedp.Run(ctx, chromedp.Evaluate(`Object.defineProperty(navigator, 'webdriver', { get: () => undefined }); window.chrome = { runtime: {} };`, nil))
	return s, nil
}

func (s *BrowserSession) Close() {
	if s == nil {
		return
	}
	if s.cancel != nil {
		s.cancel()
	}
	if s.allocCancel != nil {
		s.allocCancel()
	}
}

func enterTimeout(system bool) time.Duration {
	if system {
		return 28 * time.Second
	}
	return 15 * time.Second
}

type storageState struct {
	Cookies []struct {
		Name     string  `json:"name"`
		Value    string  `json:"value"`
		Domain   string  `json:"domain"`
		Path     string  `json:"path"`
		Expires  float64 `json:"expires"`
		HTTPOnly bool    `json:"httpOnly"`
		Secure   bool    `json:"secure"`
		SameSite string  `json:"sameSite"`
	} `json:"cookies"`
}

func applyStorageState(ctx context.Context, path string) error {
	raw, err := os.ReadFile(path)
	if err != nil {
		return err
	}
	var st storageState
	if err := json.Unmarshal(raw, &st); err != nil {
		return err
	}
	cookies := make([]*network.CookieParam, 0, len(st.Cookies))
	for _, c := range st.Cookies {
		if c.Name == "" {
			continue
		}
		p := &network.CookieParam{
			Name: c.Name, Value: c.Value, Domain: c.Domain, Path: c.Path,
			HTTPOnly: c.HTTPOnly, Secure: c.Secure,
		}
		if p.Path == "" {
			p.Path = "/"
		}
		if c.Expires > 0 {
			exp := cdp.TimeSinceEpoch(time.Unix(int64(c.Expires), 0))
			p.Expires = &exp
		}
		switch c.SameSite {
		case "Strict":
			p.SameSite = network.CookieSameSiteStrict
		case "Lax":
			p.SameSite = network.CookieSameSiteLax
		case "None":
			p.SameSite = network.CookieSameSiteNone
		}
		cookies = append(cookies, p)
	}
	if len(cookies) == 0 {
		return nil
	}
	return chromedp.Run(ctx, chromedp.ActionFunc(func(ctx context.Context) error {
		return network.SetCookies(cookies).Do(ctx)
	}))
}

func saveStorageState(ctx context.Context, path string) error {
	var cookies []*network.Cookie
	if err := chromedp.Run(ctx, chromedp.ActionFunc(func(ctx context.Context) error {
		var err error
		cookies, err = network.GetCookies().Do(ctx)
		return err
	})); err != nil {
		return err
	}
	out := storageState{}
	for _, c := range cookies {
		exp := float64(-1)
		if c.Expires > 0 {
			exp = float64(c.Expires)
		}
		out.Cookies = append(out.Cookies, struct {
			Name     string  `json:"name"`
			Value    string  `json:"value"`
			Domain   string  `json:"domain"`
			Path     string  `json:"path"`
			Expires  float64 `json:"expires"`
			HTTPOnly bool    `json:"httpOnly"`
			Secure   bool    `json:"secure"`
			SameSite string  `json:"sameSite"`
		}{
			Name: c.Name, Value: c.Value, Domain: c.Domain, Path: c.Path,
			Expires: exp, HTTPOnly: c.HTTPOnly, Secure: c.Secure, SameSite: string(c.SameSite),
		})
	}
	b, err := json.MarshalIndent(out, "", "  ")
	if err != nil {
		return err
	}
	return os.WriteFile(path, b, 0644)
}

var (
	enterPathRE    = regexp.MustCompile(`(?i)/webcast/room/(?:web/)?enter`)
	statusInHTMLRE = regexp.MustCompile(`"status"\s*:\s*([24])`)
)

const (
	enterLiving = 2
	enterEnded  = 4
)

// EnterInfo is room enter status from web/enter or RENDER_DATA.
type EnterInfo struct {
	Status     int
	RoomStatus int
	Title      string
	IDStr      string
}

func isRoomEnterURL(u string) bool {
	return u != "" && enterPathRE.MatchString(u)
}

func isLiving(status int) bool { return status == enterLiving }

func asInt(v any, def int) int {
	switch t := v.(type) {
	case float64:
		return int(t)
	case int:
		return t
	case int64:
		return int(t)
	case string:
		n, err := strconv.Atoi(t)
		if err != nil {
			return def
		}
		return n
	case json.Number:
		n, err := t.Int64()
		if err != nil {
			return def
		}
		return int(n)
	default:
		return def
	}
}

func parseRoomEnterPayload(payload any) *EnterInfo {
	var obj map[string]any
	switch t := payload.(type) {
	case nil:
		return nil
	case []byte:
		if json.Unmarshal(t, &obj) != nil {
			return nil
		}
	case string:
		if json.Unmarshal([]byte(strings.TrimSpace(t)), &obj) != nil {
			return nil
		}
	case map[string]any:
		obj = t
	default:
		return nil
	}
	data, _ := obj["data"].(map[string]any)
	if data == nil {
		return nil
	}
	roomStatus := asInt(data["room_status"], 0)
	var room map[string]any
	switch rooms := data["data"].(type) {
	case []any:
		if len(rooms) > 0 {
			room, _ = rooms[0].(map[string]any)
		}
	case map[string]any:
		room = rooms
	}
	if room == nil {
		return &EnterInfo{Status: enterEnded, RoomStatus: roomStatus}
	}
	st := asInt(room["status"], asInt(room["status_str"], 0))
	return &EnterInfo{
		Status: st, RoomStatus: roomStatus,
		Title: strField(room["title"]), IDStr: strField(room["id_str"], room["id"]),
	}
}

func strField(vs ...any) string {
	for _, v := range vs {
		if s, ok := v.(string); ok && s != "" {
			return s
		}
		if n, ok := v.(float64); ok {
			return strconv.FormatInt(int64(n), 10)
		}
	}
	return ""
}

func walkRoomStatus(obj any, depth int) *EnterInfo {
	if depth > 8 || obj == nil {
		return nil
	}
	switch t := obj.(type) {
	case map[string]any:
		if _, ok := t["status"]; ok {
			if _, ok2 := t["status_str"]; ok2 || t["id_str"] != nil || t["title"] != nil {
				st := asInt(t["status"], asInt(t["status_str"], 0))
				if st == enterLiving || st == enterEnded {
					return &EnterInfo{
						Status: st, RoomStatus: asInt(t["room_status"], 0),
						Title: strField(t["title"]), IDStr: strField(t["id_str"], t["id"]),
					}
				}
			}
		}
		for _, v := range t {
			if found := walkRoomStatus(v, depth+1); found != nil {
				return found
			}
		}
	case []any:
		for i, v := range t {
			if i >= 20 {
				break
			}
			if found := walkRoomStatus(v, depth+1); found != nil {
				return found
			}
		}
	}
	return nil
}

func parseRenderDataText(raw string) *EnterInfo {
	if raw == "" {
		return nil
	}
	text := strings.TrimSpace(raw)
	for _, candidate := range []string{text, mustUnescape(text)} {
		var data any
		if json.Unmarshal([]byte(candidate), &data) == nil {
			if found := walkRoomStatus(data, 0); found != nil {
				return found
			}
		}
		if m := statusInHTMLRE.FindStringSubmatch(candidate); len(m) > 1 {
			st, _ := strconv.Atoi(m[1])
			return &EnterInfo{Status: st}
		}
	}
	if m := statusInHTMLRE.FindStringSubmatch(text); len(m) > 1 {
		st, _ := strconv.Atoi(m[1])
		return &EnterInfo{Status: st}
	}
	return nil
}

func mustUnescape(s string) string {
	u, err := url.QueryUnescape(s)
	if err != nil {
		return s
	}
	return u
}

func extractEnterFromPage(ctx context.Context) (*EnterInfo, error) {
	var raw string
	err := chromedp.Run(ctx, chromedp.Evaluate(`(() => {
		const el = document.getElementById('RENDER_DATA');
		if (el && el.textContent) return el.textContent;
		return '';
	})()`, &raw))
	if err != nil {
		return nil, err
	}
	return parseRenderDataText(raw), nil
}

const loginURL = "https://www.douyin.com/"

const loginConfirmJS = `(() => {
    const inject = () => {
        if (document.getElementById('__dy_login_btn__')) return;
        const btn = document.createElement('div');
        btn.id = '__dy_login_btn__';
        btn.style.cssText = 'position:fixed;bottom:30px;right:30px;z-index:999999;background:#fe2c55;color:#fff;font-size:16px;font-weight:bold;padding:14px 28px;border-radius:8px;cursor:pointer;box-shadow:0 4px 12px rgba(0,0,0,0.3);user-select:none;';
        btn.innerText = '\u2705  \u6211\u5df2\u5b8c\u6210\u767b\u5f55';
        btn.onclick = () => { window.__LOGIN_DONE__ = true; btn.innerText = '\u23f3 \u4fdd\u5b58\u4e2d...'; btn.style.background = '#888'; };
        document.body.appendChild(btn);
    };
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', inject);
    else inject();
})();`

// DoLogin opens a headed system browser for Douyin login and saves state.json.
func DoLogin(root string) bool {
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()
	sess, err := launchBrowser(ctx, BrowserOptions{
		Root: root, ForceSystem: true, Headless: false, TrimResources: false,
	})
	if err != nil {
		return false
	}
	defer sess.Close()

	path := statePath(root)
	if err := chromedp.Run(sess.Ctx,
		chromedp.Evaluate(loginConfirmJS, nil),
		chromedp.Navigate(loginURL),
		chromedp.Evaluate(loginConfirmJS, nil),
	); err != nil {
		return false
	}

	for {
		var done bool
		err := chromedp.Run(sess.Ctx, chromedp.Evaluate(`window.__LOGIN_DONE__ === true`, &done))
		if err != nil {
			return false
		}
		if done {
			time.Sleep(time.Second)
			if err := saveStorageState(sess.Ctx, path); err != nil {
				return false
			}
			if sessionOK(path) {
				return true
			}
			_ = chromedp.Run(sess.Ctx, chromedp.Evaluate(`(() => {
				window.__LOGIN_DONE__ = false;
				const btn = document.getElementById('__dy_login_btn__');
				if (btn) { btn.innerText = '\u2705  \u6211\u5df2\u5b8c\u6210\u767b\u5f55'; btn.style.background = '#fe2c55'; }
			})()`, nil))
			continue
		}
		select {
		case <-sess.Ctx.Done():
			return false
		case <-time.After(500 * time.Millisecond):
		}
	}
}

func sessionOK(path string) bool {
	raw, err := os.ReadFile(path)
	if err != nil {
		return false
	}
	var st struct {
		Cookies []struct {
			Name    string  `json:"name"`
			Value   string  `json:"value"`
			Expires float64 `json:"expires"`
		} `json:"cookies"`
	}
	if json.Unmarshal(raw, &st) != nil {
		return false
	}
	now := float64(time.Now().Unix())
	for _, c := range st.Cookies {
		if c.Name != "sessionid" || c.Value == "" {
			continue
		}
		if c.Expires > 0 && c.Expires < now {
			return false
		}
		return true
	}
	return false
}

// Route2Driver: chromedp WSS /push/v2/ → OnFrame.
type Route2Driver struct{}

func (Route2Driver) ID() ID { return Route2 }

func (d Route2Driver) Run(ctx context.Context, p Params) error {
	return runBrowserCapture(ctx, p, false)
}

// Route1Driver: JS hook → OnMessage (message.ingest semantics).
type Route1Driver struct{}

func (Route1Driver) ID() ID { return Route1 }

func (d Route1Driver) Run(ctx context.Context, p Params) error {
	return runBrowserCapture(ctx, p, true)
}

// Hook pushes into __DY_MSG_Q for Go drain (avoids fragile console parsing).
const hookJS = `(() => {
    if (window.__DY_HOOK__) return;
    window.__DY_HOOK__ = true;
    window.__DY_MSG_Q = window.__DY_MSG_Q || [];
    const oldPush = Array.prototype.push;
    Array.prototype.push = function (...args) {
        try {
            for (const msg of args) {
                if (!msg || typeof msg !== "object") continue;
                const method = msg.method;
                if (!method) continue;
                const payload = msg.payload || {};
                const user    = payload.user?.desensitized_nickname || payload.user?.nickname || "";
                const user_id = String(payload.user?.id || payload.user?.id_str || "");
                let data = null;
                if (method === "WebcastChatMessage") {
                    const content = payload.content || "";
                    if (user && content) data = { type: "chat", user, user_id, content };
                } else if (method === "WebcastGiftMessage") {
                    const gift = payload?.gift?.name || "";
                    const gift_id = payload?.gift?.id ?? 0;
                    const repeat_end = payload?.repeat_end;
                    const count = payload?.combo_count ? Number(payload.combo_count) : 1;
                    if (user && gift && String(repeat_end).trim() === "1")
                        data = { type: "gift", user, user_id, gift, gift_id, count, repeat_end: 1 };
                } else if (method === "WebcastLikeMessage") {
                    const count = Number(payload?.count || 1);
                    if (user) data = { type: "like", user, user_id, count };
                } else if (method === "WebcastMemberMessage") {
                    if (user) data = { type: "enter", user, user_id };
                } else if (method === "WebcastSocialMessage") {
                    if (user) data = { type: "follow", user, user_id };
                } else if (method === "WebcastRoomUserSeqMessage") {
                    data = { type: "online", current: Number(payload?.total || 0), total: Number(payload?.total_pv_for_anchor || 0) };
                } else if (method === "WebcastFansclubMessage") {
                    data = { type: "fansclub", user, user_id, content: payload?.content || "" };
                } else if (method === "WebcastEmojiChatMessage") {
                    data = { type: "emoji", user, user_id, emoji_id: String(payload?.emoji_id || ""), default_content: payload?.default_content || "" };
                } else if (method === "WebcastRoomStatsMessage") {
                    const display_long = payload?.display_long || "";
                    if (display_long) data = { type: "room_stats", display_long };
                } else if (method === "WebcastRoomRankMessage") {
                    const ranks = (payload?.ranks_list || []).map(r => ({
                        user_id: String(r?.user?.id || ""), nickname: r?.user?.nickname || r?.user?.nick_name || "", rank: Number(r?.rank || 0)
                    }));
                    data = { type: "rank", ranks };
                } else if (method === "WebcastControlMessage") {
                    data = { type: "control", status: Number(payload?.status || 0) };
                }
                if (data) { try { window.__DY_MSG_Q.push(data); } catch(e) {} }
            }
        } catch(e) {}
        return oldPush.apply(this, args);
    };
})();`

func runBrowserCapture(ctx context.Context, p Params, jsHook bool) error {
	if p.LiveID == "" {
		return fmt.Errorf("live_id required")
	}
	sess, err := launchBrowser(ctx, BrowserOptions{
		Root: p.Root, ForceSystem: p.ForceSystem, Headless: true, TrimResources: true,
	})
	if err != nil {
		return err
	}
	defer sess.Close()

	var (
		mu          sync.Mutex
		liveOK      bool
		enterSeen   bool
		stopped     bool
		pushSockets = map[network.RequestID]bool{}
		enterReqs   = map[network.RequestID]bool{}
	)

	emitStatus := func(v bool) {
		if p.OnStatus != nil {
			p.OnStatus(v)
		}
	}
	fail := func() {
		mu.Lock()
		defer mu.Unlock()
		if stopped || liveOK {
			return
		}
		stopped = true
		emitStatus(false)
	}
	confirm := func() {
		mu.Lock()
		defer mu.Unlock()
		if liveOK || stopped {
			return
		}
		liveOK = true
		emitStatus(true)
	}
	applyEnter := func(enter *EnterInfo) {
		if enter == nil {
			return
		}
		mu.Lock()
		if enterSeen || stopped {
			mu.Unlock()
			return
		}
		enterSeen = true
		mu.Unlock()
		if isLiving(enter.Status) {
			confirm()
		} else {
			fail()
		}
	}

	chromedp.ListenTarget(sess.Ctx, func(ev any) {
		switch e := ev.(type) {
		case *network.EventResponseReceived:
			if isRoomEnterURL(e.Response.URL) {
				mu.Lock()
				enterReqs[e.RequestID] = true
				mu.Unlock()
			}
		case *network.EventLoadingFinished:
			mu.Lock()
			want := enterReqs[e.RequestID]
			mu.Unlock()
			if !want {
				return
			}
			var body []byte
			var errB error
			_ = chromedp.Run(sess.Ctx, chromedp.ActionFunc(func(c context.Context) error {
				b, er := network.GetResponseBody(e.RequestID).Do(c)
				body, errB = b, er
				return nil
			}))
			if errB != nil || len(body) == 0 {
				return
			}
			applyEnter(parseRoomEnterPayload(body))
		case *network.EventWebSocketCreated:
			if strings.Contains(e.URL, "/push/v2/") {
				mu.Lock()
				pushSockets[e.RequestID] = true
				mu.Unlock()
			}
		case *network.EventWebSocketFrameReceived:
			mu.Lock()
			okSock := pushSockets[e.RequestID]
			okLive := liveOK && !stopped
			mu.Unlock()
			if !okSock || !okLive || jsHook {
				return
			}
			if e.Response.Opcode != 2 {
				return
			}
			raw, err := base64.StdEncoding.DecodeString(e.Response.PayloadData)
			if err != nil || len(raw) == 0 {
				raw = []byte(e.Response.PayloadData)
			}
			if p.OnFrame != nil && len(raw) > 0 {
				p.OnFrame(raw)
			}
		case *network.EventWebSocketClosed:
			mu.Lock()
			was := pushSockets[e.RequestID] && liveOK && !stopped
			if was {
				stopped = true
			}
			mu.Unlock()
			if was {
				emitStatus(false)
			}
		case *runtime.EventConsoleAPICalled:
			_ = e // queue drain path preferred
		}
	})

	if jsHook {
		_ = chromedp.Run(sess.Ctx, chromedp.Evaluate(hookJS, nil))
	}

	url := fmt.Sprintf("https://live.douyin.com/%s", p.LiveID)
	if err := chromedp.Run(sess.Ctx, chromedp.Navigate(url)); err != nil {
		return err
	}
	if jsHook {
		_ = chromedp.Run(sess.Ctx, chromedp.Evaluate(hookJS, nil))
	}

	deadline := time.Now().Add(enterTimeout(sess.System))
	ticker := time.NewTicker(400 * time.Millisecond)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			emitStatus(false)
			return ctx.Err()
		case <-ticker.C:
			mu.Lock()
			doneEnter := enterSeen || liveOK || stopped
			alive := liveOK && !stopped
			failed := stopped && !liveOK
			mu.Unlock()

			if !doneEnter && time.Now().After(deadline) {
				fb, _ := extractEnterFromPage(sess.Ctx)
				if fb != nil {
					applyEnter(fb)
				} else {
					fail()
					return fmt.Errorf("enter status timeout")
				}
			}
			if failed {
				return fmt.Errorf("not living")
			}
			if jsHook && alive {
				drainHookQueue(sess.Ctx, p)
			}
			mu.Lock()
			s := stopped && liveOK
			mu.Unlock()
			if s {
				return nil
			}
		}
	}
}

func drainHookQueue(ctx context.Context, p Params) {
	var rawJSON string
	err := chromedp.Run(ctx, chromedp.Evaluate(`(() => {
		const q = window.__DY_MSG_Q;
		if (!q || !q.length) return '';
		return JSON.stringify(q.splice(0, q.length));
	})()`, &rawJSON))
	if err != nil || rawJSON == "" || rawJSON == "null" {
		return
	}
	var items []map[string]any
	if json.Unmarshal([]byte(rawJSON), &items) != nil {
		return
	}
	for _, it := range items {
		if p.OnMessage == nil {
			continue
		}
		m := Msg{}
		for k, v := range it {
			m[k] = v
		}
		p.OnMessage(m)
	}
}
