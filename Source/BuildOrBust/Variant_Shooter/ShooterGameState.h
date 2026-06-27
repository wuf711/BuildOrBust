// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "ShooterGameState.generated.h"

/**
 *  多人「同场竞技」用的 GameState。
 *  在所有客户端（含服务器）周期性显示实时计分板，并在比赛结束时显示胜者。
 *  分数本身存在各玩家内置的 APlayerState::Score 上（自带复制）。
 */
UCLASS()
class BUILDORBUST_API AShooterGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AShooterGameState();

	/** 服务器在比赛结束时调用，记录胜者名（复制到所有端用于显示） */
	void SetMatchResult(const FString& InWinnerName);

	/** 比赛是否结束（供 HUD 查询） */
	bool IsMatchOver() const { return bMatchOver; }

	/** 胜者名（供 HUD 查询） */
	const FString& GetWinnerName() const { return WinnerName; }

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	bool bMatchOver = false;

	UPROPERTY(Replicated)
	FString WinnerName;

private:
	FTimerHandle ScoreboardTimer;

	/** 定时刷新屏幕计分板（在每个端各自运行） */
	void UpdateScoreboard();
};
