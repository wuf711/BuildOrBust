// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "Variant_Shooter/ShooterGameState.h"
#include "Variant_Shooter/ShooterHUD.h"
#include "ShooterUI.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AShooterGameMode::AShooterGameMode()
{
	// 多人同场竞技：使用自定义 GameState 做计分板
	GameStateClass = AShooterGameState::StaticClass();
	// 每个玩家各自的抬头显示（左上状态 + 右上得分对比）
	HUDClass = AShooterHUD::StaticClass();
}

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();

	// create the UI
	ShooterUI = CreateWidget<UShooterUI>(UGameplayStatics::GetPlayerController(GetWorld(), 0), ShooterUIClass);
	ShooterUI->AddToViewport(0);
}

void AShooterGameMode::IncrementTeamScore(uint8 TeamByte)
{
	// retrieve the team score if any
	int32 Score = 0;
	if (int32* FoundScore = TeamScores.Find(TeamByte))
	{
		Score = *FoundScore;
	}

	// increment the score for the given team
	++Score;
	TeamScores.Add(TeamByte, Score);

	// update the UI
	if (ShooterUI)
	{
		ShooterUI->BP_UpdateScore(TeamByte, Score);
	}
}

void AShooterGameMode::AddPlayerScore(int32 Points)
{
	if (bGameOver)
	{
		return;
	}

	PlayerScore += Points;

	// 复用 UI 的 0 号槽位显示玩家总分
	if (ShooterUI)
	{
		ShooterUI->BP_UpdateScore(0, PlayerScore);
	}
}

void AShooterGameMode::TriggerWin()
{
	if (bGameOver)
	{
		return;
	}
	bGameOver = true;
	RecordMatchWinner();
	BP_OnGameOver(true, PlayerScore);
}

void AShooterGameMode::TriggerLose()
{
	if (bGameOver)
	{
		return;
	}
	bGameOver = true;
	RecordMatchWinner();
	BP_OnGameOver(false, PlayerScore);
}

void AShooterGameMode::RecordMatchWinner()
{
	// 多人同场竞技：分数最高的玩家为胜者，写入 GameState 复制给所有端显示
	AShooterGameState* GS = GetGameState<AShooterGameState>();
	if (!GS)
	{
		return;
	}

	APlayerState* Best = nullptr;
	for (APlayerState* PS : GS->PlayerArray)
	{
		if (PS && (!Best || PS->GetScore() > Best->GetScore()))
		{
			Best = PS;
		}
	}
	GS->SetMatchResult(Best ? Best->GetPlayerName() : FString(TEXT("平局")));
}
