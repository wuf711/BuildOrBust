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
};
