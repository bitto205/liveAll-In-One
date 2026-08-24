# liveaio-core 索引

**契约真源：** [`CONTRACT.md`](CONTRACT.md)  
**C++ 对接：** [`CPP_UI_CONTRACT.md`](CPP_UI_CONTRACT.md)  
**JSON 样例：** [`protocol_examples.md`](protocol_examples.md)  
**设计锁：** [`DESIGN_LOCKS.md`](DESIGN_LOCKS.md)  
**Plan 5 收尾：** [`PLAN5.md`](PLAN5.md)

## 计划索引

| Plan | 内容 |
|------|------|
| 1–2 | Go core / hub / 协议骨架（已落地） |
| 3 | C++ pages |
| 4 | C++ tools（overlay / skin / memo / danmu / overtime） |
| 5 | 契约收尾与接口冻结（本文档链） |

## 入口

用户双击 **`LiveAIO.exe`**（C++）：LoadLibrary Core → 托盘 / IPC；按需加载 Pages/Tools。

调试：`go build ./main` 或 F5（整进程 Go，不经 Core dll）。

## 当前实现

| 片 | 位置 |
|----|------|
| C++ 宿主 | `build/host/` → `LiveAIO.exe` |
| Core DLL 导出 | `core/dll/` |
| 会话 / 编排 | `core/hub.go`、`core/hub_ui.go`、`core/run.go` |
| UI 拉起 | `core/ui_pages.go` |
| 调试 exe | `main/main.go` → `core.Run` |

## Debug

跑起来看日志（`log/`），不要单独拆模块测试脚本。

```powershell
.\build\build_work\custom\LiveAIO.exe --no-admin
```

## Build

唯一脚本（勿再用已废弃的 `core/scripts` 路径）：

```powershell
.\build\build.ps1              # → build_work/custom/
.\build\build.ps1 -Release     # → build_work/<version>/
```

产物：`LiveAIO.exe` + `LiveAIOCore.dll` + `LiveAIOPages.dll` / `LiveAIOTools.dll`  
依赖：仓库根 `go.mod` / `go.sum`（`build/liveaio.*` 仅为镜像）  
CMake Tools：源目录 `build/`
