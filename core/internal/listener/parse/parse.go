package parse

import (
	"bytes"
	"compress/gzip"
	"io"
	"strconv"

	"liveaio/core/internal/listener/livepb"

	"google.golang.org/protobuf/proto"
)

// Msg is a JSON-ready live message matching util/models.py field names.
type Msg map[string]any

func userName(u *livepb.User) string {
	if u == nil {
		return ""
	}
	return u.GetNickname()
}

func userID(u *livepb.User) string {
	if u == nil {
		return ""
	}
	if u.GetId() != 0 {
		return strconv.FormatInt(u.GetId(), 10)
	}
	return u.GetIdStr()
}

func ParseItem(method string, payload []byte) Msg {
	switch method {
	case "WebcastChatMessage":
		var pb livepb.ChatMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		u, c := userName(pb.GetUser()), pb.GetContent()
		if u == "" || c == "" {
			return nil
		}
		return Msg{"type": "chat", "user": u, "user_id": userID(pb.GetUser()), "content": c}

	case "WebcastGiftMessage":
		var pb livepb.GiftMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		if pb.GetRepeatEnd() != 1 {
			return nil
		}
		u := userName(pb.GetUser())
		gname := ""
		var gid int64
		if pb.GetGift() != nil {
			gname = pb.GetGift().GetName()
			gid = pb.GetGift().GetId()
		}
		count := pb.GetComboCount()
		if count == 0 {
			count = 1
		}
		if u == "" || gname == "" {
			return nil
		}
		return Msg{
			"type": "gift", "user": u, "user_id": userID(pb.GetUser()),
			"gift": gname, "gift_id": gid, "count": count, "repeat_end": 1,
		}

	case "WebcastLikeMessage":
		var pb livepb.LikeMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		u := userName(pb.GetUser())
		if u == "" {
			return nil
		}
		c := pb.GetCount()
		if c == 0 {
			c = 1
		}
		return Msg{"type": "like", "user": u, "user_id": userID(pb.GetUser()), "count": c}

	case "WebcastMemberMessage":
		var pb livepb.MemberMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		u := userName(pb.GetUser())
		if u == "" {
			return nil
		}
		return Msg{"type": "enter", "user": u, "user_id": userID(pb.GetUser())}

	case "WebcastSocialMessage":
		var pb livepb.SocialMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		u := userName(pb.GetUser())
		if u == "" {
			return nil
		}
		return Msg{
			"type": "follow", "user": u, "user_id": userID(pb.GetUser()),
			"action": pb.GetAction(), "share_type": pb.GetShareType(),
			"share_target": pb.GetShareTarget(), "follow_count": pb.GetFollowCount(),
		}

	case "WebcastRoomUserSeqMessage":
		var pb livepb.RoomUserSeqMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		totalPv, _ := strconv.ParseInt(pb.GetTotalPvForAnchor(), 10, 64)
		return Msg{"type": "online", "current": pb.GetTotal(), "total": totalPv}

	case "WebcastFansclubMessage":
		var pb livepb.FansclubMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		return Msg{
			"type": "fansclub", "user": userName(pb.GetUser()),
			"user_id": userID(pb.GetUser()), "content": pb.GetContent(),
		}

	case "WebcastEmojiChatMessage":
		var pb livepb.EmojiChatMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		return Msg{
			"type": "emoji", "user": userName(pb.GetUser()), "user_id": userID(pb.GetUser()),
			"emoji_id": strconv.FormatInt(pb.GetEmojiId(), 10), "default_content": pb.GetDefaultContent(),
		}

	case "WebcastRoomStatsMessage":
		var pb livepb.RoomStatsMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		return Msg{
			"type": "room_stats",
			"display_long": pb.GetDisplayLong(), "display_short": pb.GetDisplayShort(),
			"display_middle": pb.GetDisplayMiddle(), "display_value": pb.GetDisplayValue(),
			"total": pb.GetTotal(), "display_type": pb.GetDisplayType(),
		}

	case "WebcastRoomRankMessage":
		var pb livepb.RoomRankMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		ranks := make([]any, 0, len(pb.GetRanksList()))
		for _, r := range pb.GetRanksList() {
			ranks = append(ranks, map[string]any{
				"user": userName(r.GetUser()), "user_id": userID(r.GetUser()), "score": r.GetScoreStr(),
			})
		}
		return Msg{"type": "rank", "ranks": ranks}

	case "WebcastControlMessage":
		var pb livepb.ControlMessage
		if err := proto.Unmarshal(payload, &pb); err != nil {
			return nil
		}
		return Msg{"type": "control", "status": pb.GetStatus()}
	}
	return nil
}

func gunzip(b []byte) []byte {
	r, err := gzip.NewReader(bytes.NewReader(b))
	if err != nil {
		return b
	}
	defer r.Close()
	out, err := io.ReadAll(r)
	if err != nil {
		return b
	}
	return out
}

// TryParseFrame mirrors listener.LiveProtobuf.try_parse_frame.
func TryParseFrame(payload []byte) (ok bool, msgs []Msg) {
	var frame livepb.PushFrame
	if err := proto.Unmarshal(payload, &frame); err != nil {
		return false, nil
	}
	if len(frame.GetPayload()) == 0 {
		return false, nil
	}
	body := gunzip(frame.GetPayload())
	var resp livepb.LiveResponse
	if err := proto.Unmarshal(body, &resp); err != nil {
		return false, nil
	}
	for _, item := range resp.GetMessagesList() {
		if m := ParseItem(item.GetMethod(), item.GetPayload()); m != nil {
			msgs = append(msgs, m)
		}
	}
	return true, msgs
}
