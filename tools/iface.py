"""工具 UI 接口规范（业务在 Go core，见 core/CONTRACT.md）。"""
from __future__ import annotations

from typing import Protocol, runtime_checkable


# Core → UI 业务事件（App 派进 ToolUI）
CORE_TICK_OP = "tick"
CORE_TOOL_EVENT_OPS = frozenset({
    "ledger",
    "danmu.show",
    "memo.item",
})
CORE_TOOL_OPS = CORE_TOOL_EVENT_OPS | {CORE_TICK_OP}


@runtime_checkable
class ToolUI(Protocol):
    """已打开工具单例须满足的 UI 侧接口。

    - 不在此处理帧解析 / 规则过滤 / 倒计时加减（均在 core）。
    - 设置变更：写 config 后调用 bridge.tool_sync.push_*。
    """

    def on_core_event(self, env: dict) -> None:
        """处理 ledger / danmu.show / memo.item；无关 op 直接 return。"""

    def on_core_tick(self, env: dict) -> None:
        """加班机：remaining_seconds / running。其它工具可空实现。"""

    def on_status_change(self, connected: bool) -> None:
        """监听连接态（可选逻辑）。"""
