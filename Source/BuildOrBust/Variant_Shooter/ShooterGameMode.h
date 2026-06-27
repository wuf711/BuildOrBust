// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterGameMode.generated.h"

class UShooterUI;

/**
 *  Simple GameMode for a first person shooter game
 *  Manages game UI
 *  Keeps track of team scores
 */
UCLASS(abstract)
class BUILDORBUST_API AShooterGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:

	AShooterGameMode();

protected:

	/** Type of UI widget to spawn */
	UPROPERTY(EditAnywhere, Category="Shooter")
	TSubclassOf<UShooterUI> ShooterUIClass;

	/** Pointer to the UI widget */
	TObjectPtr<UShooterUI> ShooterUI;

	/** Map of scores by team ID */
	TMap<uint8, int32> TeamScores;

	/** 玩家累计得分（含连击倍率） */
	int32 PlayerScore = 0;

	/** 游戏是否已结束 */
	bool bGameOver = false;

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** 比赛结束时统计分数最高者，写入 GameState 供所有端显示 */
	void RecordMatchWinner();

public:

	/** Increases the score for the given team */
	void IncrementTeamScore(uint8 TeamByte);

	/** 加分（击杀时按连击倍率调用） */
	UFUNCTION(BlueprintCallable, Category="Score")
	void AddPlayerScore(int32 Points);

	UFUNCTION(BlueprintPure, Category="Score")
	int32 GetPlayerScore() const { return PlayerScore; }

	/** 触发胜利（打满波次） */
	UFUNCTION(BlueprintCallable, Category="Game")
	void TriggerWin();

	/** 触发失败（玩家死亡） */
	UFUNCTION(BlueprintCallable, Category="Game")
	void TriggerLose();

	UFUNCTION(BlueprintPure, Category="Game")
	bool IsGameOver() const { return bGameOver; }

	/** 结算回调：蓝图里弹出结算界面（bWin=是否胜利, FinalScore=最终得分） */
	UFUNCTION(BlueprintImplementableEvent, Category="Game", meta=(DisplayName="On Game Over"))
	void BP_OnGameOver(bool bWin, int32 FinalScore);
};
