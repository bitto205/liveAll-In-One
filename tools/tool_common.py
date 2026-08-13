"""工具模块公共：单例、礼物图标、退出、按 iface 派发 core 事件。"""
from __future__ import annotations

import os
from typing import Callable

from PySide6.QtCore import Qt
from PySide6.QtGui import QPixmap

from tools.iface import CORE_TOOL_EVENT_OPS
from util.log_util import get_tagged_logger
from util.paths import gift_dir

logger = get_tagged_logger("工具", "tools.common")

_GIFT_ICON_DIR = str(gift_dir() / "icon")
_GIFT_NAMES_CACHE: list[str] | None = None
_GIFT_SRC_CACHE: dict[str, QPixmap] = {}
_APP_SHUTTING_DOWN = False
_TOOLS_PAGE = None


class ToolSingleton:
    """工具窗单例 mixin：配合 QMainWindow，避免重复 __new__/__init__ 样板。"""

    _instance = None

    def __new__(cls, parent=None):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
        return cls._instance

    @staticmethod
    def guard_init(obj) -> bool:
        if getattr(obj, "_initialized", False):
            return False
        obj._initialized = True
        return True


def bind_tools_page(page) -> None:
    global _TOOLS_PAGE
    _TOOLS_PAGE = page


def unregister_tool_from_page(tool_name: str) -> None:
    page = _TOOLS_PAGE
    if page is not None and hasattr(page, "unregister_tool"):
        page.unregister_tool(tool_name)


def release_tool_singleton(
    tool_cls: type,
    *,
    cleanup: Callable | None = None,
) -> None:
    inst = getattr(tool_cls, "_instance", None)
    if inst is None:
        return
    if cleanup is not None:
        cleanup(inst)
    tool_cls._instance = None
    if hasattr(inst, "_initialized"):
        inst._initialized = False


def mark_app_shutting_down() -> None:
    global _APP_SHUTTING_DOWN
    _APP_SHUTTING_DOWN = True


def is_app_shutting_down() -> bool:
    return _APP_SHUTTING_DOWN


def shutdown_all_tools() -> None:
    """退出主程序时关闭所有工具窗及其悬浮子窗。"""
    mark_app_shutting_down()
    closed = _force_close_all_tool_windows()
    if closed:
        logger.info("已关闭 %d 个工具窗口", closed)


def _iter_loaded_tool_instances():
    """只遍历已 import 且已实例化的工具。"""
    import sys
    from tools import get_tools

    for meta in get_tools():
        mod = sys.modules.get(meta.module_name)
        if mod is None:
            continue
        cls = getattr(mod, meta.attr_name, None)
        if cls is None:
            continue
        inst = getattr(cls, "_instance", None)
        if inst is not None:
            yield meta, cls, inst


def any_tool_ui_visible() -> bool:
    """是否仍有 Qt 工具窗/悬浮窗需要 UI 进程（UI 需求例外）。"""
    for _meta, _cls, inst in _iter_loaded_tool_instances():
        if getattr(inst, "isVisible", lambda: False)():
            return True
        for attr in ("_danmu_win", "_overtime_win", "_user_time_win"):
            sub = getattr(inst, attr, None)
            if sub is not None and getattr(sub, "isVisible", lambda: False)():
                return True
    return False


def dispatch_status_to_open_tools(connected: bool) -> int:
    n = 0
    for _meta, _cls, inst in _iter_loaded_tool_instances():
        fn = getattr(inst, "on_status_change", None)
        if not callable(fn):
            continue
        try:
            fn(connected)
            n += 1
        except Exception as e:
            logger.error("工具 %s 状态处理异常: %s", type(inst).__name__, e, exc_info=True)
    return n


def dispatch_core_tick(env: dict) -> int:
    """Core op=tick → ToolUI.on_core_tick。"""
    n = 0
    for _meta, _cls, inst in _iter_loaded_tool_instances():
        fn = getattr(inst, "on_core_tick", None)
        if not callable(fn):
            continue
        try:
            fn(env)
            n += 1
        except Exception as e:
            logger.error("工具 %s tick 异常: %s", type(inst).__name__, e, exc_info=True)
    return n


def dispatch_core_tool_event(env: dict) -> int:
    """Core op ∈ {ledger, danmu.show, memo.item} → ToolUI.on_core_event。"""
    if not isinstance(env, dict) or env.get("op") not in CORE_TOOL_EVENT_OPS:
        return 0
    n = 0
    for _meta, _cls, inst in _iter_loaded_tool_instances():
        fn = getattr(inst, "on_core_event", None)
        if not callable(fn):
            continue
        try:
            fn(env)
            n += 1
        except Exception as e:
            logger.error("工具 %s core 事件异常: %s", type(inst).__name__, e, exc_info=True)
    return n


def _force_close_all_tool_windows() -> int:
    closed = 0
    for meta, cls, inst in list(_iter_loaded_tool_instances()):
        cleanup = getattr(inst, "_cleanup_for_release", None)
        for attr in ("_danmu_win", "_overtime_win", "_user_time_win"):
            sub = getattr(inst, attr, None)
            if sub is not None:
                sub.close()
        inst.close()
        release_tool_singleton(cls, cleanup=cleanup)
        unregister_tool_from_page(meta.name)
        closed += 1
    return closed


def gift_names_cached() -> list[str]:
    global _GIFT_NAMES_CACHE
    if _GIFT_NAMES_CACHE is None:
        from resources.gift.gift_info import all_gifts
        _GIFT_NAMES_CACHE = sorted(all_gifts().keys())
    return _GIFT_NAMES_CACHE


def _screen_dpr() -> float:
    from PySide6.QtWidgets import QApplication
    scr = QApplication.primaryScreen()
    return float(scr.devicePixelRatio()) if scr else 1.0


def scale_pixmap_dpr(px: QPixmap, side: int) -> QPixmap:
    """缩到逻辑边长×DPR 的物理像素，再标记 devicePixelRatio，避免高分屏发糊。"""
    dpr = _screen_dpr()
    phys = max(1, round(max(1, int(side)) * dpr))
    out = px.scaled(phys, phys, Qt.KeepAspectRatio, Qt.SmoothTransformation)
    out.setDevicePixelRatio(dpr)
    return out


def load_gift_pixmap(gift_name: str, side: int, *, tool_id: str = "overtime") -> QPixmap | None:
    from resources.gift.gift_info import get_gift_id, get_icon_path

    path = get_icon_path(gift_name)
    if not path:
        gid = get_gift_id(gift_name)
        if gid:
            for ext in (".webp", ".png", ".jpg"):
                p = os.path.join(_GIFT_ICON_DIR, f"{gid}{ext}")
                if os.path.exists(p):
                    path = p
                    break
    if not path:
        return None

    src = _GIFT_SRC_CACHE.get(path)
    if src is None or src.isNull():
        src = QPixmap(path)
        if src.isNull():
            from resources.skin.media import load_still
            still = load_still(path, max(side, 168), dpr=1.0, scale="smooth")
            if still.pixmap.isNull():
                return None
            src = still.pixmap
            src.setDevicePixelRatio(1.0)
        _GIFT_SRC_CACHE[path] = src

    return scale_pixmap_dpr(src, side)
