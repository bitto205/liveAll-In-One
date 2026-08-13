package ipc

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"log/slog"
	"net"
	"sync"

	"liveaio/core/internal/protocol"

	"github.com/Microsoft/go-winio"
)

// Envelope is one JSONL object.
type Envelope map[string]any

func (e Envelope) Op() string {
	if e == nil {
		return ""
	}
	v, _ := e["op"].(string)
	return v
}

type Handler func(conn *Conn, env Envelope)

type Conn struct {
	raw    net.Conn
	wmu    sync.Mutex
	closed bool
}

func (c *Conn) Send(env Envelope) error {
	c.wmu.Lock()
	defer c.wmu.Unlock()
	if c.closed {
		return io.ErrClosedPipe
	}
	b, err := json.Marshal(env)
	if err != nil {
		return err
	}
	b = append(b, '\n')
	_, err = c.raw.Write(b)
	return err
}

func (c *Conn) Close() error {
	c.wmu.Lock()
	c.closed = true
	c.wmu.Unlock()
	return c.raw.Close()
}

type Server struct {
	PipeName string
	TCPAddr  string
	Handler  Handler
	Log      *slog.Logger
	OnConnect    func(*Conn)
	OnDisconnect func(*Conn)

	ln     net.Listener
	cancel context.CancelFunc
	wg     sync.WaitGroup
}

func (s *Server) log() *slog.Logger {
	if s.Log != nil {
		return s.Log
	}
	return slog.Default()
}

func (s *Server) Listen() (net.Listener, string, error) {
	// Phase 0: TCP primary (reliable without elevation). Pipe optional later.
	addr := s.TCPAddr
	if addr == "" {
		addr = protocol.DefaultTCPAddr
	}
	tcpLn, err := net.Listen("tcp", addr)
	if err == nil {
		return tcpLn, "tcp:" + addr, nil
	}

	pipe := s.PipeName
	if pipe == "" {
		pipe = protocol.DefaultPipeName
	}
	ln, err2 := winio.ListenPipe(pipe, nil)
	if err2 != nil {
		return nil, "", fmt.Errorf("tcp: %v; pipe: %w", err, err2)
	}
	return ln, "pipe:" + pipe, nil
}

func (s *Server) Serve(ctx context.Context) error {
	ln, how, err := s.Listen()
	if err != nil {
		return err
	}
	s.ln = ln
	s.log().Info("IPC listening", "addr", how)

	ctx, s.cancel = context.WithCancel(ctx)
	s.wg.Add(1)
	go func() {
		defer s.wg.Done()
		<-ctx.Done()
		_ = ln.Close()
	}()

	for {
		raw, err := ln.Accept()
		if err != nil {
			select {
			case <-ctx.Done():
				return nil
			default:
				s.log().Error("accept failed", "err", err)
				return err
			}
		}
		s.wg.Add(1)
		go func(raw net.Conn) {
			defer s.wg.Done()
			s.serveConn(ctx, raw)
		}(raw)
	}
}

func (s *Server) serveConn(ctx context.Context, raw net.Conn) {
	c := &Conn{raw: raw}
	defer c.Close()
	if s.OnConnect != nil {
		s.OnConnect(c)
	}
	defer func() {
		if s.OnDisconnect != nil {
			s.OnDisconnect(c)
		}
	}()
	_ = c.Send(Envelope{"op": protocol.OpReady, "version": protocol.Version})

	r := bufio.NewReader(raw)
	for {
		select {
		case <-ctx.Done():
			return
		default:
		}
		line, err := r.ReadBytes('\n')
		if err != nil {
			if err != io.EOF {
				s.log().Debug("conn read end", "err", err)
			}
			return
		}
		var env Envelope
		if err := json.Unmarshal(line, &env); err != nil {
			_ = c.Send(Envelope{"op": protocol.OpError, "code": "bad_json", "msg": err.Error()})
			continue
		}
		if s.Handler != nil {
			s.Handler(c, env)
		}
	}
}

func (s *Server) Stop() {
	if s.cancel != nil {
		s.cancel()
	}
	if s.ln != nil {
		_ = s.ln.Close()
	}
	s.wg.Wait()
}
