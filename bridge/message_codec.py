"""Convert core IPC message envelopes ↔ util.models.LiveMessage."""
from __future__ import annotations

from typing import Any

from util.models import (
    ChatMessage,
    ControlMessage,
    EmojiChatMessage,
    EnterMessage,
    FansclubMessage,
    FollowMessage,
    GiftMessage,
    LikeMessage,
    LiveMessage,
    OnlineMessage,
    RoomEnterStatusMessage,
    RoomRankMessage,
    RoomStatsMessage,
)


def envelope_to_message(env: dict[str, Any]) -> LiveMessage | None:
    """Parse op=message (or bare type dict) into LiveMessage."""
    data = dict(env)
    data.pop("op", None)
    t = data.get("type")
    if not t:
        return None
    try:
        if t == "chat":
            return ChatMessage(user=str(data.get("user", "")), user_id=str(data.get("user_id", "")), content=str(data.get("content", "")))
        if t == "gift":
            return GiftMessage(
                user=str(data.get("user", "")),
                user_id=str(data.get("user_id", "")),
                gift=str(data.get("gift", "")),
                gift_id=int(data.get("gift_id") or 0),
                count=int(data.get("count") or 1),
                repeat_end=int(data.get("repeat_end") if data.get("repeat_end") is not None else -1),
            )
        if t == "like":
            return LikeMessage(user=str(data.get("user", "")), user_id=str(data.get("user_id", "")), count=int(data.get("count") or 1))
        if t == "enter":
            return EnterMessage(user=str(data.get("user", "")), user_id=str(data.get("user_id", "")))
        if t == "follow":
            return FollowMessage(
                user=str(data.get("user", "")),
                user_id=str(data.get("user_id", "")),
                action=int(data.get("action") or 0),
                share_type=int(data.get("share_type") or 0),
                share_target=str(data.get("share_target", "")),
                follow_count=int(data.get("follow_count") or 0),
            )
        if t == "online":
            return OnlineMessage(current=int(data.get("current") or 0), total=int(data.get("total") or 0))
        if t == "fansclub":
            return FansclubMessage(user=str(data.get("user", "")), user_id=str(data.get("user_id", "")), content=str(data.get("content", "")))
        if t == "emoji":
            return EmojiChatMessage(
                user=str(data.get("user", "")),
                user_id=str(data.get("user_id", "")),
                emoji_id=str(data.get("emoji_id", "")),
                default_content=str(data.get("default_content", "")),
            )
        if t == "room_stats":
            return RoomStatsMessage(
                display_long=str(data.get("display_long", "")),
                display_short=str(data.get("display_short", "")),
                display_middle=str(data.get("display_middle", "")),
                display_value=int(data.get("display_value") or 0),
                total=int(data.get("total") or 0),
                display_type=int(data.get("display_type") or 0),
            )
        if t == "rank":
            return RoomRankMessage(ranks=list(data.get("ranks") or []))
        if t == "control":
            return ControlMessage(status=int(data.get("status") or 0))
        if t == "room_enter":
            return RoomEnterStatusMessage(
                status=int(data.get("status") or 0),
                room_status=int(data.get("room_status") or 0),
                title=str(data.get("title", "")),
                id_str=str(data.get("id_str", "")),
            )
    except Exception:
        return None
    return None
