// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/ShooterGameMode.h"
#include "Variant_Shooter/ShooterGameState.h"
#include "BODPlayerState.h"
#include "Variant_Shooter/ShooterHUD.h"
#include "ShooterUI.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

AShooterGameMode::AShooterGameMode()
{
	// 多人同场竞技：使用自定义 GameState 做计分板
	GameStateClass = AShooterGameState::StaticClass();
	// 余烬(Cinder)等玩家级数据挂在自定义 PlayerState 上，必须显式指定，
	// 否则引擎用默认 APlayerState，Cast<ABODPlayerState> 全部失败（表现：余烬不显示、补给失败）
	PlayerStateClass = ABODPlayerState::StaticClass();
	// 每个玩家各自的抬头显示（左上状态 + 右上得分对比）
	HUDClass = AShooterHUD::StaticClass();
}

void AShooterGameMode::BeginPlay()
{
	Super::BeginPlay();

	// 不再创建顶部中间的模板团队分（与右上 P1/P2 计分板重复且含义不明）。
	// 下方所有使用处均有 if (ShooterUI) 判空，留空安全；如需恢复，还原这两行即可：
	// ShooterUI = CreateWidget<UShooterUI>(UGameplayStatics::GetPlayerController(GetWorld(), 0), ShooterUIClass);
	// ShooterUI->AddToViewport(0);
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
	// 蓝图结算 UMG 已弃用：结算画面由 ShooterHUD 的 C++ 面板绘制（渐暗+逐人战果+R重开）
}

void AShooterGameMode::TriggerLose()
{
	if (bGameOver)
	{
		return;
	}
	bGameOver = true;
	RecordMatchWinner();
	// 蓝图结算 UMG 已弃用：败局画面同样走 ShooterHUD 的 C++ 面板
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
