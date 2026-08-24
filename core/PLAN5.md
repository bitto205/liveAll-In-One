# Plan 5: 收尾与接口约定记录

> **临时收尾计划**：对齐文档与代码现实，冻结 C++ pages/tools 对接约定。不实现 UI、不重写 listener。

## 交付物

| 文件 | 作用 |
|------|------|
| [`CONTRACT.md`](CONTRACT.md) | 进程 / IPC / 工具归属 / 目录（无 Python UI、bridge、iface） |
| [`CPP_UI_CONTRACT.md`](CPP_UI_CONTRACT.md) | C++ 必遵守的握手、命令、事件、生命周期、资源路径 |
| [`protocol_examples.md`](protocol_examples.md) | JSON 样例（含 `capabilities` / `config.ok` / `config.value`） |
| [`DESIGN_LOCKS.md`](DESIGN_LOCKS.md) | 现状锁 |
| [`PLAN.md`](PLAN.md) | 四计划 + Plan5 索引；Build → `build/build.ps1` |
| [`protocol.go`](protocol.go) | `OpConfigOk = "config.ok"` 等常量 |

## 构建与验证

```powershell
.\build\build.ps1
.\build\build_work\custom\LiveAIO.exe --no-admin
go test ./...
```

正式 UI：宿主加载 `LiveAIOPages.dll` / `LiveAIOTools.dll`。业务无 Python / 无 `LiveAIOUI.exe`。

## 不做

- 不推进 Plan 3/4 的 UI 功能编码
- 不把目标改回「现在必须能跑完整 UI」
- 不保留过渡 adapter 层设计
