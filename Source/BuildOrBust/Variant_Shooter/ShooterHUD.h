// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "ShooterHUD.generated.h"

/**
 *  每个玩家各自的抬头显示。
 *  左上：目标 / 波次 / 核心血量；右上：本玩家 vs 对手 的实时得分对比。
 *  因为是 per-player 绘制（各画各的视角），多人单进程下也不会互相抢位置/跳动。
 */
class UTexture2D;
class UFont;

UCLASS()
class BUILDORBUST_API AShooterHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	/** R 键：暂停并切换完整指南面板 */
	void ToggleGuide();

	/** 点击关闭指南（开局弹窗/暂停面板通用） */
	void CloseGuide();

	/** 指南是否可见 */
	bool IsGuideVisible() const { return bShowGuide; }

	/** E 键：小地图放大/缩小 */
	void ToggleMapSize() { bMapBig = !bMapBig; }

	/** Tab：背包页开关 */
	void ToggleBackpack() { bShowBackpack = !bShowBackpack; }

private:

	/** 指南面板可见性（开局默认弹出） */
	bool bShowGuide = true;

	/** 背包页可见性（Tab） */
	bool bShowBackpack = false;

	/** 指南页码：0=行动简报 1=按键操作 */
	int32 GuidePage = 0;

	/** 指南是否处于 R 键暂停状态（开局弹窗不暂停） */
	bool bGuidePaused = false;

	/** 小地图贴图（/Game/Wasteland/T_BoBMinimap，懒加载） */
	UPROPERTY()
	TObjectPtr<UTexture2D> MinimapTex = nullptr;

	bool bTriedLoadMinimap = false;

	/** 商品 2D 意象贴图缓存（key = IconPath；加载失败也记 nullptr，避免每帧重试） */
	UPROPERTY()
	TMap<FString, TObjectPtr<UTexture2D>> IconCache;

	/** 取商品图标；路径为空或资产缺失返回 nullptr（调用方退回占位框） */
	UTexture2D* GetItemIcon(const FString& Path);

	/** 小地图放大态（E 键切换 340↔640） */
	bool bMapBig = false;

	/** 运行时组装的全字库粗体字体（雅黑 Bold FontFace → UFont），防 GC */
	UPROPERTY()
	TObjectPtr<UFont> RuntimeFont = nullptr;

	bool bTriedBuildFont = false;
	//~ 战斗反馈状态（每个 HUD 实例各自维护；不能用 static，单进程双窗口会互串）

	/** 上次看到的波次，用于检测新波开始 */
	int32 LastSeenWave = -1;

	/** 波次开场横幅显示截止时间（世界秒） */
	float WaveBannerUntil = 0.0f;

	/** 上次看到的核心血量，用于检测核心受击 */
	float LastSeenCoreHP = -1.0f;

	/** 核心受击警示显示截止时间（世界秒） */
	float CoreAlertUntil = 0.0f;

	/** 上次看到的本机玩家得分，用于检测得分增长（击杀飘分） */
	float LastSeenMyScore = -1.0f;

	/** 飘分显示截止时间（世界秒）与本次增量 */
	float ScorePopupUntil = 0.0f;
	int32 ScorePopupAmount = 0;
};
