"""Route 2/4：原始帧只送给 Go core 解析（Python 不再本地 try_parse_frame）。"""
from __future__ import annotations

from typing import Callable

_PUSH: Callable[[bytes], None] | None = None


def bind(push: Callable[[bytes], None] | None) -> None:
    global _PUSH
    _PUSH = push


def push_frame(raw: bytes) -> None:
    if _PUSH is None:
        return
    _PUSH(raw)
