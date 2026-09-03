package connectdiag

import (
	"context"
	"strings"
	"testing"
	"time"
)

func TestConnectErrorMessages(t *testing.T) {
	cases := []struct {
		code ConnectCode
		want string
	}{
		{CodeNotLiving, MsgNotLiving},
		{CodeBadRoom, MsgBadRoom},
		{CodeTimeoutNet, MsgTimeoutNet},
		{CodeTimeout, MsgTimeout},
	}
	for _, c := range cases {
		if got := (&ConnectError{Code: c.code}).Error(); got != c.want {
			t.Fatalf("code %s: got %q want %q", c.code, got, c.want)
		}
	}
}

func TestConnectErrorDetail(t *testing.T) {
	err := ErrWithDetail(CodeTimeout, "proxy_shell.exe: 未运行")
	got := err.Error()
	if !strings.Contains(got, MsgTimeout) || !strings.Contains(got, "proxy_shell.exe") {
		t.Fatalf("detail: %q", got)
	}
}

func TestAsConnectError(t *testing.T) {
	err := ErrBadRoom()
	ce, ok := AsConnectError(err)
	if !ok || ce.Code != CodeBadRoom {
		t.Fatalf("AsConnectError failed: %v %v", ce, ok)
	}
}

func TestIsLandingTitle(t *testing.T) {
	if !isLandingTitle("抖音直播电脑版_抖音直播网页版入口_抖音直播") {
		t.Fatal("expected landing")
	}
	if isLandingTitle("YM.雨宮奏的抖音直播间 - 抖音直播") {
		t.Fatal("anchor should not be landing")
	}
}

func TestBrowserStuck(t *testing.T) {
	if !BrowserStuck(0, false, false) {
		t.Fatal("want stuck")
	}
	if BrowserStuck(1, false, false) {
		t.Fatal("enter req means not stuck for bad room")
	}
	if BrowserStuck(0, false, true) {
		t.Fatal("wss means not stuck")
	}
}

func TestClassifyBrowserFault_BadRoom(t *testing.T) {
	h := PageHints{LandingPage: true}
	err := ClassifyBrowserFault(context.TODO(), 0, false, &h)
	if err == nil {
		t.Fatal("want error")
	}
	ce, ok := AsConnectError(err)
	if !ok || ce.Code != CodeBadRoom {
		t.Fatalf("want bad_room got %v", err)
	}
}

func TestTryBadRoom_TooEarly(t *testing.T) {
	if err := TryBadRoom(context.TODO(), 0, false, 2*time.Second); err != nil {
		t.Fatal("too early")
	}
}
