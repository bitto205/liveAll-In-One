package overtime

import (
	"math/rand"
	"strings"
	"sync"
	"time"
)

type Rule struct {
	Gift   string `json:"gift"`
	Mode   string `json:"mode"` // add | sub | random
	Value  int    `json:"value"`
	Unit   string `json:"unit"` // s | m | h
	MinVal int    `json:"min"`
	MaxVal int    `json:"max"`
}

type Settings struct {
	Hours   int    `json:"hours"`
	Minutes int    `json:"minutes"`
	Seconds int    `json:"seconds"`
	Rules   []Rule `json:"rules"`
}

type LedgerEntry struct {
	User    string `json:"user"`
	UserID  string `json:"user_id"`
	Seconds int    `json:"seconds"`
}

type Engine struct {
	mu        sync.Mutex
	remaining int
	running   bool
	settings  Settings
	ledger    map[string]int // user_id -> seconds
	stopTick  chan struct{}
	onTick    func(remaining int, running bool)
	onLedger  func(entries []LedgerEntry)
}

func New(onTick func(int, bool), onLedger func([]LedgerEntry)) *Engine {
	return &Engine{
		ledger:   map[string]int{},
		onTick:   onTick,
		onLedger: onLedger,
		stopTick: make(chan struct{}),
	}
}

func unitToSeconds(v int, unit string) int {
	switch strings.ToLower(unit) {
	case "h", "hour", "hours":
		return v * 3600
	case "m", "min", "minute", "minutes":
		return v * 60
	default:
		return v
	}
}

func ruleToSeconds(r Rule, count int) int {
	if count < 1 {
		count = 1
	}
	switch strings.ToLower(r.Mode) {
	case "sub", "subtract", "-":
		return -unitToSeconds(r.Value, r.Unit) * count
	case "random", "rand":
		lo, hi := r.MinVal, r.MaxVal
		if hi < lo {
			lo, hi = hi, lo
		}
		if hi == lo {
			return unitToSeconds(lo, r.Unit) * count
		}
		n := lo + rand.Intn(hi-lo+1)
		return unitToSeconds(n, r.Unit) * count
	default: // add
		return unitToSeconds(r.Value, r.Unit) * count
	}
}

func (e *Engine) SetSettings(s Settings) {
	e.mu.Lock()
	defer e.mu.Unlock()
	e.settings = s
	total := s.Hours*3600 + s.Minutes*60 + s.Seconds
	if total > 0 {
		e.remaining = total
	}
	e.emitTickLocked()
}

func (e *Engine) Cmd(cmd string) {
	e.mu.Lock()
	defer e.mu.Unlock()
	switch cmd {
	case "pause":
		e.running = false
	case "resume", "start":
		e.running = true
	case "reset":
		s := e.settings
		e.remaining = s.Hours*3600 + s.Minutes*60 + s.Seconds
		e.running = true
	case "clear_ledger":
		e.ledger = map[string]int{}
		e.emitLedgerLocked()
	}
	e.emitTickLocked()
}

func (e *Engine) StartTicker() {
	go func() {
		t := time.NewTicker(time.Second)
		defer t.Stop()
		for {
			select {
			case <-e.stopTick:
				return
			case <-t.C:
				e.mu.Lock()
				if e.running && e.remaining > 0 {
					e.remaining--
					e.emitTickLocked()
				}
				e.mu.Unlock()
			}
		}
	}()
}

func (e *Engine) Stop() {
	select {
	case <-e.stopTick:
	default:
		close(e.stopTick)
	}
}

func (e *Engine) HandleGift(user, userID, gift string, count int) {
	e.mu.Lock()
	defer e.mu.Unlock()
	var hit *Rule
	for i := range e.settings.Rules {
		r := &e.settings.Rules[i]
		if r.Gift != "" && r.Gift == gift {
			hit = r
			break
		}
	}
	if hit == nil {
		return
	}
	delta := ruleToSeconds(*hit, count)
	e.remaining += delta
	if e.remaining < 0 {
		e.remaining = 0
	}
	if userID == "" {
		userID = user
	}
	e.ledger[userID] += delta
	e.running = true
	e.emitTickLocked()
	e.emitLedgerLocked()
}

func (e *Engine) emitTickLocked() {
	if e.onTick != nil {
		e.onTick(e.remaining, e.running)
	}
}

func (e *Engine) emitLedgerLocked() {
	if e.onLedger == nil {
		return
	}
	out := make([]LedgerEntry, 0, len(e.ledger))
	for uid, sec := range e.ledger {
		out = append(out, LedgerEntry{UserID: uid, Seconds: sec})
	}
	e.onLedger(out)
}
