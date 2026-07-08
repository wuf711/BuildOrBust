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
UCLASS()
class BUILDORBUST_API AShooterHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

private:
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
