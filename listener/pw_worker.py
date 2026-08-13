"""
listener/pw_worker.py — 无 UI 的 Playwright 抓帧进程，由 liveaio-core 拉起。

只负责浏览器 + 原始帧 → core frame.push；proto 解码在 Go。
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import socket
import sys
import time

# repo root
_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if _ROOT not in sys.path:
    sys.path.insert(0, _ROOT)
os.chdir(_ROOT)


def _connect_core(tcp: str, timeout: float = 8.0) -> socket.socket:
    host, _, port_s = tcp.partition(":")
    port = int(port_s or "19877")
    deadline = time.time() + timeout
    last = None
    while time.time() < deadline:
        try:
            s = socket.create_connection((host, port), timeout=1.0)
            s.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            # drain ready line
            buf = b""
            while b"\n" not in buf:
                chunk = s.recv(4096)
                if not chunk:
                    break
                buf += chunk
            return s
        except OSError as e:
            last = e
            time.sleep(0.2)
    raise SystemExit(f"cannot connect core at {tcp}: {last}")


def _send(sock: socket.socket, env: dict) -> None:
    sock.sendall((json.dumps(env, ensure_ascii=False) + "\n").encode("utf-8"))


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--live-id", required=True)
    ap.add_argument("--route", default="2", choices=("1", "2"))
    ap.add_argument("--tcp", default="127.0.0.1:19877")
    ap.add_argument("--force-system", action="store_true")
    args = ap.parse_args()

    sock = _connect_core(args.tcp)
    _send(sock, {"op": "ping", "id": "pw"})

    def push_frame(raw: bytes) -> None:
        _send(sock, {
            "op": "frame.push",
            "payload_b64": base64.b64encode(raw).decode("ascii"),
        })

    def on_status(ok: bool) -> None:
        _send(sock, {"op": "status", "connected": bool(ok), "route": args.route})

    # Worker is capture-only: reuse listener2 for route 2; route 1 still hooks→ingest later
    if args.route == "1":
        from listener import listener1 as L
        # listener1 yields messages not frames — ingest via message.ingest after build
        def on_msg(msg):
            from dataclasses import asdict, is_dataclass
            from util.models import (
                ChatMessage, ControlMessage, EnterMessage, FansclubMessage,
                FollowMessage, GiftMessage, LikeMessage,
            )
            if isinstance(msg, ChatMessage):
                payload = {"type": "chat", "user": msg.user, "user_id": msg.user_id, "content": msg.content}
            elif isinstance(msg, GiftMessage):
                payload = {
                    "type": "gift", "user": msg.user, "user_id": msg.user_id,
                    "gift": msg.gift, "gift_id": msg.gift_id, "count": msg.count,
                    "repeat_end": msg.repeat_end,
                }
            elif isinstance(msg, LikeMessage):
                payload = {"type": "like", "user": msg.user, "user_id": msg.user_id, "count": msg.count}
            elif isinstance(msg, FollowMessage):
                payload = {"type": "follow", "user": msg.user, "user_id": msg.user_id}
            elif isinstance(msg, FansclubMessage):
                payload = {"type": "fansclub", "user": msg.user, "user_id": msg.user_id, "content": msg.content}
            elif isinstance(msg, ControlMessage):
                payload = {"type": "control", "status": msg.status}
            elif isinstance(msg, EnterMessage):
                payload = {"type": "enter", "user": msg.user, "user_id": msg.user_id}
            else:
                return
            _send(sock, {"op": "message.ingest", **payload})

        L.start_listener(
            args.live_id, on_msg, on_status=on_status, force_system=args.force_system,
        )
        return 0

    # route 2: bind core_feed then run listener2 (no local parse)
    from listener.core_feed import bind
    bind(push_frame)

    from listener.listener2 import start_listener

    def _noop_msg(_msg):
        return

    start_listener(
        args.live_id,
        _noop_msg,
        on_status=on_status,
        force_system=args.force_system,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
