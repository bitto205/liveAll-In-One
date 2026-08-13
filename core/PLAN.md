# liveaio-core 索引

**契约真源：** [`CONTRACT.md`](CONTRACT.md)（进程 / IPC / 工具 UI / util 归属）  
**JSON 样例：** [`protocol_examples.md`](protocol_examples.md)  
**设计锁：** [`DESIGN_LOCKS.md`](DESIGN_LOCKS.md)

## 入口（Go）

用户双击 **`LiveAIO.exe`**（即 `liveaio.exe`）：
UAC → 单实例 → spawn **liveaio-core.exe** → 原生托盘。

Python 只负责界面：`python ui_shell.py`（或 `liveaio --ui`）。

## 当前实现

| 片 | 位置 |
|----|------|
| 外部启动链 | `core/cmd/liveaio` |
| 常驻服务 | `core/cmd/liveaio-core` |
| Qt UI | `ui_shell.py` |

## Debug

```powershell
.\core\scripts\build.ps1
.\LiveAIO.exe                    # 启动链
.\core\dist\liveaio-core.exe     # 只跑服务（无 UAC/托盘）
python ui_shell.py               # 只调界面
```

## Build

```powershell
.\core\scripts\build.ps1
```

→ `core/dist/liveaio-core.exe` + 项目根 `LiveAIO.exe`
