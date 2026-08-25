# K‑11 地形管线

这里是 K‑11 地形的唯一规范源。公开目录只保留正式生成、重导入、审计入口及其必需数据；一次性试验脚本、中间产物和历史参考图在本地归档，不属于现行流程。

执行顺序：

1. `node Art\K11Pipeline\gen_k11.js`
2. 用 UE 5.8 加载 `/Game/Variant_Shooter/Lvl_Shooter`，执行 `ue_reimport_k11.py`
3. 对主地图运行 `WorldPartitionNavigationDataBuilder`
4. 执行 `ue_audit_k11.py`

审计必须全量运行。A6 检查全部走廊格。2026‑08‑24 当前结果为 22 / 22，负一层 2002 / 2002 格可达；S3b 另查逃生口路板底部支撑和两侧回填。

`hybrid_data.json` 是生成器输入，`k11_height_applied.json` 是当前关卡已应用地表的基线；两者必须与生成器一起版本化。OBJ、FBX、诊断图和授权纹理均为本地生成或本地依赖，不进入公共仓库。首次审计前必须先运行生成器，并按 `docs/03_本地资产依赖.md` 恢复所需纹理。

生成器输出 `k11_greybox.obj` 作为地表掩体、废墟体块和地标的灰盒，输出 `k11_soilfill.obj` 作为地表与洞顶之间的剖面显示填土，输出 `k11_navfloor.obj` 作为地下连续底板。`K11_SoilFill` 使用双面灰盒材质，只显示，不碰撞，也不参与导航。视觉 `k11_hexfield` 的墙柱负责从地下底板切出实际走廊。重导入时会删除 `K11_BLOCK_*` 旧城市模块、旧底盘、旧城灯和废弃出口补片，并重贴六个 PlayerStart。逃生口露天路段生成连续实体路基，底部压入 `K11_Surface`，两侧封边。Recast 使用 World Partition 模式和 16384 Tile Pool；完成生成与重导入后必须运行 Builder，再执行新版审计。

`ue_reimport_k11.py` 会确保 `RecastNavMesh-Default` 启用 World Partition 导航，但不会假装一次异步 `RebuildNavigation` 已经完成。持久化导航必须使用 UE 自带的 `WorldPartitionNavigationDataBuilder`；普通编辑器启动阶段受 `AsyncLoadLock` 保护，直接发重建命令会被拒绝。

三个正式入口均从脚本位置推导路径。任何从本地归档恢复的旧脚本，在进入正式流程前都必须先完成可移植性处理、明确输入输出，并补上验收项。
