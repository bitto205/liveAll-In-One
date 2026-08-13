"""Push tool settings from Python config (sole writer) into liveaio-core."""
from __future__ import annotations

from typing import Any

_UNIT = {"时": "h", "分": "m", "秒": "s"}
_MODE = {"加": "add", "减": "sub", "随机": "random"}


def _client():
    try:
        from ui_shell import _RUNNING_APP
        app = _RUNNING_APP
        if app is None:
            return None
        core = getattr(app, "_core", None)
        if core is None or not getattr(core, "is_ready", False):
            return None
        return core
    except Exception:
        return None


def _send(op: str, **fields: Any) -> None:
    c = _client()
    if c is None:
        return
    try:
        payload = {"op": op, **fields}
        c.send(payload)
    except Exception:
        pass


def overtime_settings_payload(data: dict | None = None) -> dict:
    from tools.overtime_tool import load_settings

    raw = data if isinstance(data, dict) else load_settings()
    rules_out = []
    for r in raw.get("rules") or []:
        if not isinstance(r, dict):
            continue
        mode = _MODE.get(str(r.get("mode", "加")), "add")
        gift = str(r.get("gift") or "")
        if mode == "random":
            unit = _UNIT.get(str(r.get("random_unit", "分")), "m")
            rules_out.append({
                "gift": gift,
                "mode": "random",
                "unit": unit,
                "min": -int(r.get("random_neg") or 0),
                "max": int(r.get("random_pos") or 0),
                "value": 0,
            })
        else:
            unit = _UNIT.get(str(r.get("unit", "分")), "m")
            rules_out.append({
                "gift": gift,
                "mode": mode,
                "unit": unit,
                "value": int(r.get("value") or 0),
                "min": 0,
                "max": 0,
            })
    return {
        "hours": int(raw.get("hours") or 0),
        "minutes": int(raw.get("minutes") or 0),
        "seconds": int(raw.get("seconds") or 0),
        "rules": rules_out,
    }


def push_overtime_settings(data: dict | None = None) -> None:
    _send("tool.overtime.set", settings=overtime_settings_payload(data))


def push_danmu_settings() -> None:
    import config as _cfg
    _send("tool.danmu.set", settings={
        "danmu_chat_on": bool(_cfg.get("danmu_chat_on", True)),
        "danmu_gift_on": bool(_cfg.get("danmu_gift_on", True)),
        "danmu_gift_min_diamonds": int(_cfg.get("danmu_gift_min_diamonds", 0) or 0),
        "danmu_follow_on": bool(_cfg.get("danmu_follow_on", True)),
        "danmu_like_on": bool(_cfg.get("danmu_like_on", True)),
        "danmu_like_threshold": max(1, int(_cfg.get("danmu_like_threshold", 1) or 1)),
        "danmu_like_accumulate": bool(_cfg.get("danmu_like_accumulate", False)),
    })


def push_memo_settings() -> None:
    import config as _cfg
    _send("tool.memo.set", settings={
        "memo.gift.enabled": bool(_cfg.get("memo.gift.enabled", True)),
        "memo.gift.stack": bool(_cfg.get("memo.gift.stack", True)),
        "memo.gift.min_diamonds": int(_cfg.get("memo.gift.min_diamonds", 0) or 0),
        "memo.follow.enabled": bool(_cfg.get("memo.follow.enabled", True)),
        "memo.like.enabled": bool(_cfg.get("memo.like.enabled", True)),
        "memo.like.stack": bool(_cfg.get("memo.like.stack", True)),
    })


def push_all_tool_settings() -> None:
    push_overtime_settings()
    push_danmu_settings()
    push_memo_settings()


def sim_overtime_gift(gift: str, count: int = 1, user: str = "sim") -> None:
    _send("tool.overtime.sim_gift", gift=gift, count=int(count or 1), user=user)


def ingest_message(msg) -> None:
    """Route 1/3 本地解析后的消息喂给 core 做工具业务（不回 echo message）。"""
    c = _client()
    if c is None:
        return
    try:
        from util.models import (
            ChatMessage,
            ControlMessage,
            EnterMessage,
            FansclubMessage,
            FollowMessage,
            GiftMessage,
            LikeMessage,
        )
        if isinstance(msg, ChatMessage):
            payload = {
                "type": "chat", "user": msg.user, "user_id": msg.user_id,
                "content": msg.content,
            }
        elif isinstance(msg, GiftMessage):
            payload = {
                "type": "gift", "user": msg.user, "user_id": msg.user_id,
                "gift": msg.gift, "gift_id": msg.gift_id, "count": msg.count,
                "repeat_end": msg.repeat_end,
            }
        elif isinstance(msg, LikeMessage):
            payload = {
                "type": "like", "user": msg.user, "user_id": msg.user_id,
                "count": msg.count,
            }
        elif isinstance(msg, FollowMessage):
            payload = {"type": "follow", "user": msg.user, "user_id": msg.user_id}
        elif isinstance(msg, FansclubMessage):
            payload = {
                "type": "fansclub", "user": msg.user, "user_id": msg.user_id,
                "content": msg.content,
            }
        elif isinstance(msg, ControlMessage):
            payload = {"type": "control", "status": msg.status}
        elif isinstance(msg, EnterMessage):
            payload = {"type": "enter", "user": msg.user, "user_id": msg.user_id}
        else:
            return
        c.send({"op": "message.ingest", **payload})
    except Exception:
        pass
