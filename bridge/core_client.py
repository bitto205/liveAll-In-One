"""
bridge/core_client.py — attach liveaio-core, JSONL over TCP → Qt signals.

Core 由 Go 启动链拉起；本客户端只附着 IPC。UI 退出 detach；显式「退出」发 shutdown。
"""
from __future__ import annotations

import json
import socket
import threading
import time
from pathlib import Path
from typing import Any, Callable

from PySide6.QtCore import QObject, Signal

from bridge.protocol import (
    DEFAULT_TCP,
    OP_PING,
    OP_SHUTDOWN,
)

Envelope = dict[str, Any]
OnEvent = Callable[[Envelope], None]


def probe_core(timeout: float = 0.4) -> bool:
    """True if something already accepts the core TCP port."""
    try:
        s = socket.create_connection(DEFAULT_TCP, timeout=timeout)
        s.close()
        return True
    except OSError:
        return False


class CoreClient(QObject):
    """IPC client — attach only (core is started by Go)."""

    event_received = Signal(object)  # Envelope
    ready = Signal()
    failed = Signal(str)

    def __init__(self, parent=None, app_root: Path | None = None):
        super().__init__(parent)
        self._app_root = app_root or Path(__file__).resolve().parent.parent
        self._sock: socket.socket | None = None
        self._reader: threading.Thread | None = None
        self._stop = threading.Event()
        self._ready = threading.Event()
        self._lock = threading.Lock()

    @property
    def is_ready(self) -> bool:
        return self._ready.is_set() and self._sock is not None

    def start(self, *, timeout: float = 8.0) -> bool:
        """Connect to an already-running core."""
        self._stop.clear()
        self._ready.clear()

        deadline = time.time() + timeout
        last_err = ""
        while time.time() < deadline:
            try:
                self._connect_once()
                break
            except OSError as e:
                last_err = str(e)
                time.sleep(0.15)
        else:
            self.failed.emit(last_err or "connect timeout")
            return False

        self._reader = threading.Thread(target=self._read_loop, name="core-ipc", daemon=True)
        self._reader.start()
        if not self._ready.wait(timeout):
            self.failed.emit("ready timeout")
            self.detach()
            return False
        return True

    def _connect_once(self) -> None:
        s = socket.create_connection(DEFAULT_TCP, timeout=1.0)
        s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock = s

    def _read_loop(self) -> None:
        assert self._sock is not None
        buf = b""
        try:
            while not self._stop.is_set():
                chunk = self._sock.recv(65536)
                if not chunk:
                    break
                buf += chunk
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    line = line.strip()
                    if not line:
                        continue
                    try:
                        env = json.loads(line.decode("utf-8"))
                    except json.JSONDecodeError:
                        continue
                    if env.get("op") == "ready":
                        self._ready.set()
                        self.ready.emit()
                    self.event_received.emit(env)
        except OSError:
            pass

    def send(self, env: Envelope) -> None:
        data = (json.dumps(env, ensure_ascii=False) + "\n").encode("utf-8")
        with self._lock:
            if self._sock is None:
                return
            self._sock.sendall(data)

    def ping(self, req_id: str = "1") -> None:
        self.send({"op": OP_PING, "id": req_id})

    def detach(self) -> None:
        """断开 IPC，不停止 core（关 UI 时用）。"""
        self._stop.set()
        with self._lock:
            if self._sock is not None:
                try:
                    self._sock.close()
                except Exception:
                    pass
                self._sock = None
        self._ready.clear()

    def shutdown_core(self) -> None:
        """通知 core 结束（托盘「退出」等）。"""
        try:
            self.send({"op": OP_SHUTDOWN})
        except Exception:
            pass
        self.detach()

    def stop(self) -> None:
        """兼容旧名：等同 detach。"""
        self.detach()
