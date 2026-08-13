"""
pages/__init__.py — 页面与设置面板注册入口（懒加载）

页面类在首次访问 meta.cls / SETTINGS_PAGE 时才 import。
"""
from __future__ import annotations

import importlib
import sys
import os

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from PySide6.QtWidgets import QWidget


class _PageMeta:
    def __init__(
        self,
        module: str,
        attr: str,
        icon: str,
        name: str,
        order: int,
        section: str = "main",
    ):
        self._module = module
        self._attr = attr
        self.icon = icon
        self.name = name
        self.order = order
        self.section = section
        self._cls: type | None = None

    @property
    def cls(self) -> type:
        if self._cls is None:
            mod = importlib.import_module(self._module)
            self._cls = getattr(mod, self._attr)
        return self._cls

    @property
    def module_name(self) -> str:
        return self._module


# 导航页目录：不顶层 import home/tools 等重模块
_PAGE_CATALOG: list[tuple[str, str, str, str, int, str]] = [
    ("pages.home_page", "HomePage", "🏠", "主页", 0, "main"),
    ("pages.tools_page", "ToolsPage", "⚒", "工具", 1, "main"),
]

_SETTING_SECTION_CATALOG: list[tuple[str, str]] = [
    ("pages.settings_page", "SystemSettings"),
    ("pages.tools_page", "ToolsSettings"),
]


_REGISTRY: list[_PageMeta] | None = None


def register(icon: str, name: str, order: int = 99,
             section: str = "main"):
    """装饰器兼容：真实目录在 _PAGE_CATALOG。"""
    def decorator(cls):
        cls._page_icon = icon
        cls._page_name = name
        cls._page_order = order
        cls._page_section = section
        return cls
    return decorator


def get_pages() -> list[_PageMeta]:
    global _REGISTRY
    if _REGISTRY is None:
        _REGISTRY = [_PageMeta(*row) for row in _PAGE_CATALOG]
    main = sorted([p for p in _REGISTRY if p.section == "main"],
                  key=lambda p: p.order)
    bottom = sorted([p for p in _REGISTRY if p.section == "bottom"],
                    key=lambda p: p.order)
    return main + bottom


class BasePage(QWidget):
    def on_message(self, msg):
        pass

    def on_status_change(self, connected: bool):
        pass


class BaseSetting(QWidget):
    name: str = ""
    order: int = 99

    def build_section(self, title: str):
        from PySide6.QtWidgets import QVBoxLayout, QLabel
        card = QWidget()
        card.setObjectName("SettingCard")
        lay = QVBoxLayout(card)
        lay.setContentsMargins(20, 16, 20, 16)
        lay.setSpacing(12)
        lbl = QLabel(title)
        lbl.setObjectName("SettingCardTitle")
        lay.addWidget(lbl)
        return card, lay


class _LazyType:
    """延迟解析的类型代理，首次调用时才 import。"""

    def __init__(self, module: str, attr: str):
        self._module = module
        self._attr = attr
        self._resolved: type | None = None

    def _resolve(self) -> type:
        if self._resolved is None:
            mod = importlib.import_module(self._module)
            self._resolved = getattr(mod, self._attr)
        return self._resolved

    def __call__(self, *args, **kwargs):
        return self._resolve()(*args, **kwargs)

    def __instancecheck__(self, instance):
        return isinstance(instance, self._resolve())

    def __subclasscheck__(self, subclass):
        return issubclass(subclass, self._resolve())

    def __repr__(self):
        return f"<LazyType {self._module}.{self._attr}>"


SETTINGS_PAGE = _LazyType("pages.settings_page", "SettingsPage")


class _SettingsList:
    """访问时才加载各设置分段类。"""

    def __iter__(self):
        return iter(self._all())

    def __len__(self):
        return len(self._all())

    def __getitem__(self, idx):
        return self._all()[idx]

    def _all(self) -> list[type]:
        classes: list[type] = []
        for module, attr in _SETTING_SECTION_CATALOG:
            mod = importlib.import_module(module)
            classes.append(getattr(mod, attr))
        return sorted(classes, key=lambda cls: getattr(cls, "order", 99))


SETTINGS = _SettingsList()

__all__ = [
    "register", "get_pages",
    "BasePage", "BaseSetting",
    "SETTINGS_PAGE", "SETTINGS",
]
