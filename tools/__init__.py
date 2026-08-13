"""
tools/__init__.py — 工具注册表（懒加载）

契约：core/CONTRACT.md；UI 接口：tools/iface.ToolUI。
业务过滤/倒计时在 Go；本包只注册 Qt 工具窗。

新增工具：
  1. 实现 ToolUI（on_core_event / on_core_tick / on_status_change）
  2. 在 tools/ 下新建 xxx_tool.py，用 @register_tool
  3. 在下方 _CATALOG 加一行（不要在本文件顶层 import 工具模块）
  4. 若有新 core 事件，同步 CONTRACT + ops.go + bridge/protocol
"""
from __future__ import annotations

import importlib
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

# module, attr, name, desc, icon, order — 打开时才 import module
_CATALOG: list[tuple[str, str, str, str, str, int]] = [
    ("tools.memo_tool", "MemoTool", "备忘录",
     "将礼物、关注、点赞记录为可消除的列表条目", "📋", 0),
    ("tools.danmu_tool", "DanmuTool", "弹幕机",
     "透明悬浮弹幕显示窗口", "💬", 1),
    ("tools.overtime_tool", "OvertimeTool", "加班机",
     "透明悬浮加班显示窗口", "⏱", 2),
]


class _ToolMeta:
    def __init__(
        self,
        module: str,
        attr: str,
        name: str,
        desc: str,
        icon: str,
        order: int,
    ):
        self._module = module
        self._attr = attr
        self.name = name
        self.desc = desc
        self.icon = icon
        self.order = order
        self._cls: type | None = None

    @property
    def cls(self) -> type:
        """首次访问时才 import 工具模块。"""
        if self._cls is None:
            mod = importlib.import_module(self._module)
            self._cls = getattr(mod, self._attr)
        return self._cls

    @property
    def module_name(self) -> str:
        return self._module

    @property
    def attr_name(self) -> str:
        return self._attr


_METAS: list[_ToolMeta] | None = None


def register_tool(name: str, desc: str = "",
                  icon: str = "🔧", order: int = 99):
    """装饰器：保留兼容；真实目录在 _CATALOG，避免顶层 import。"""
    def decorator(cls):
        cls._tool_name = name
        cls._tool_desc = desc
        cls._tool_icon = icon
        cls._tool_order = order
        return cls
    return decorator


def get_tools() -> list[_ToolMeta]:
    """按 order 返回工具元数据（不触发工具模块 import）。"""
    global _METAS
    if _METAS is None:
        _METAS = [_ToolMeta(*row) for row in _CATALOG]
    return sorted(_METAS, key=lambda t: t.order)


def shutdown_all_tools() -> None:
    from tools.tool_common import shutdown_all_tools as _shutdown
    _shutdown()


__all__ = [
    "get_tools", "register_tool", "shutdown_all_tools",
]
