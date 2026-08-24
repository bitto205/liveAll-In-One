# resources/ — 静态资源 + 读写接口

## 数据
- `gift/gift_info.json` + `gift/icon/*` — 礼物图鉴与图标
- `skin/<tool>/<id>/skin.json` — 工具皮肤
- `image/` — 应用图标等

## 接口归属
| 能力 | Go | C++ |
|------|----|-----|
| 图鉴 price / gift_id（业务过滤） | `resources/gift.go`（`core` 薄别名） | — |
| 图标路径（UI 绘制） | `IconPath`（可选） | `resources/cpp/gift.cpp` |
| 皮肤 JSON / fit / 阴影 / 动图 | 路径辅助 `skin.go` | `resources/cpp/skin.cpp`（UI 解释） |

约定：业务钻石数走 Go core；皮肤与图标像素加载走 C++ tools。
