# K‑11 地形管线

这里是 K‑11 地形的唯一规范源。公开目录只保留正式生成、重导入、审计入口及其必需数据；一次性试验脚本、中间产物和历史参考图在本地归档，不属于现行流程。

执行顺序：

1. `node Art\K11Pipeline\gen_k11.js`
2. 用 UE 5.8 加载 `/Game/Variant_Shooter/Lvl_Shooter`，执行 `ue_reimport_k11.py`
3. 对主地图运行 `WorldPartitionNavigationDataBuilder`
4. 执行 `ue_audit_k11.py`

审计必须全量运行。A6 已从随机 60 格升级为全部走廊格；2026‑08‑23 当前基线为 1994 / 1994（100%），完整审计 16 / 16。

`hybrid_data.json` 是生成器输入，`k11_height_applied.json` 是当前关卡已应用地表的基线；两者必须与生成器一起版本化。OBJ、FBX、诊断图和授权纹理均为本地生成或本地依赖，不进入公共仓库。首次审计前必须先运行生成器，并按 `docs/03_本地资产依赖.md` 恢复所需纹理。

生成器输出 `k11_navfloor.obj` 作为连续底板，视觉 `k11_hexfield` 的墙柱负责从底板切出实际走廊。地表底面与地道岩体包络已分离，西北边缘的岩体侵入也在 Rock 布尔层局部清除。Recast 使用 World Partition 模式和 16384 Tile Pool；完成生成与重导入后必须运行 Builder，再以 16 / 16 审计作为封口条件。

`ue_reimport_k11.py` 会确保 `RecastNavMesh-Default` 启用 World Partition 导航，但不会假装一次异步 `RebuildNavigation` 已经完成。持久化导航必须使用 UE 自带的 `WorldPartitionNavigationDataBuilder`；普通编辑器启动阶段受 `AsyncLoadLock` 保护，直接发重建命令会被拒绝。

三个正式入口均从脚本位置推导路径。任何从本地归档恢复的旧脚本，在进入正式流程前都必须先完成可移植性处理、明确输入输出，并补上验收项。
