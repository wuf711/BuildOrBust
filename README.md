# Build or Bust — K‑11

UE 5.8 单机完整作品 / 作品集项目。当前方向不是旧课程版的“双人十波丧尸防守”，而是围绕 K‑11 地下设施展开的单人战斗探索：守潮、危险搜刮、资源结算与商店、失谐/凝视、处刑者追猎、CS‑07 Boss，以及多结局解锁。

## 当前状态

- C++ Development Editor 构建通过。
- 自动玩法测试：21 PASS / 0 FAIL。
- K‑11 地形审计：15 / 16；楼梯、逃生地道、几何同步、墙高、净空和材质范围通过。
- 唯一地形 P0：迷宫导航全量检查为 1650 / 1752 格可达（94%），102 个格分散在小型导航孤岛中。主入口、祭坛、楼梯和出口链路可达，但在修复碰撞表达并完成真人走测前，地形不能宣称封板。
- 工作区包含大量尚未提交的 8 月开发成果；不要用 7 月的 `v1.2` 提交判断当前进度。

实际进度、框架与落地顺序见 [项目现状](docs/00_项目现状.md) 和 [落地路线图](docs/01_落地路线图.md)。

## 运行与验证

1. 安装 Unreal Engine 5.8、Visual Studio 2022，并拉取 Git LFS 资源。
2. 打开 `BuildOrBust.uproject`；默认地图为 `/Game/Variant_Shooter/Lvl_Shooter`。
3. 自动测试：`powershell -ExecutionPolicy Bypass -File tools/run_tests.ps1 -Scenario all -TimeoutSec 420`。
4. 地形源生成、重导入和审计按 [K‑11 管线说明](Art/K11Pipeline/README.md) 执行。

目标平台暂定 Windows 单机。旧多人、Android 触屏和课程结算 UI 仅作为历史原型保留，不是当前验收面。

## 目录

```text
Art/K11Pipeline/   K‑11 可再生地形的源、生成器、UE 导入与审计脚本
Art/Source/        美术源文件和外部资产源
Config/            UE 项目配置
Content/           游戏资产、地图、蓝图、数据表
docs/              当前设计、审计、研究、历史与对话纪要
Source/            游戏 C++ 代码
tools/             项目级自动测试入口
```

旧课程说明和交接稿已退出当前事实入口。
