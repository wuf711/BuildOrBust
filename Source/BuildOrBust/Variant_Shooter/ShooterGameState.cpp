// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/ShooterGameState.h"
#include "GameFramework/PlayerState.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "WaveManager.h"
#include "BaseCore.h"
#include "BoBTerms.h"

AShooterGameState::AShooterGameState()
{
}

void AShooterGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterGameState, bMatchOver);
	DOREPLIFETIME(AShooterGameState, WinnerName);
	DOREPLIFETIME(AShooterGameState, RunSeed);
}

void AShooterGameState::BeginPlay()
{
	Super::BeginPlay();
	// 计分板/状态显示已交给 AShooterHUD（per-player 绘制），此处不再用全局屏幕消息，避免多人时互相抢位置

	// 开局显式绑定术语表(所有端各自绑定；绕开懒加载的静态缓存脆弱性，是标准姿势)
	if (UDataTable* T = LoadObject<UDataTable>(nullptr, TEXT("/Game/BoB/Data/DT_BoBTerms.DT_BoBTerms")))
	{
		UBoBLoc::SetTermTable(T);
	}

	// 服务器开局生成本局种子(非 0)，复制给所有端；后续节点/刷新/每日模式据此可复现
	if (HasAuthority() && RunSeed == 0)
	{
		RunSeed = FMath::RandRange(1, MAX_int32);
	}
}

void AShooterGameState::SetMatchResult(const FString& InWinnerName)
{
	bMatchOver = true;
	WinnerName = InWinnerName;
}

void AShooterGameState::UpdateScoreboard()
{
	if (!GEngine)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 用固定 Key 原地刷新（不刷屏），所有端各自显示，构成一块干净 HUD
	int32 Key = 1000;

	// 目标提示（让新玩家一眼看懂玩法）
	GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::White,
		TEXT("【目标】守住基准核心，清除来袭同化体，撑过十次同化潮。"));

	// 波次 + 剩余敌人
	if (TActorIterator<AWaveManager> It(World); It)
	{
		AWaveManager* WM = *It;
		GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::Orange,
			FString::Printf(TEXT("TIDE %d      残余同化体：%d"),
				WM->GetCurrentWave(), WM->GetAliveEnemyCount()));
	}

	// 核心血量
	if (TActorIterator<ABaseCore> It(World); It)
	{
		ABaseCore* BC = *It;
		GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::Red,
			FString::Printf(TEXT("核心血量：%.0f / %.0f"), BC->GetBaseHP(), BC->MaxBaseHP));
	}

	// 计分板（按分数从高到低）
	TArray<APlayerState*> Players = PlayerArray;
	Players.Sort([](const APlayerState& A, const APlayerState& B)
	{
		return A.GetScore() > B.GetScore();
	});
	GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::Yellow, TEXT("──────  计分板  ──────"));
	for (APlayerState* PS : Players)
	{
		if (!PS)
		{
			continue;
		}
		GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::Cyan,
			FString::Printf(TEXT("   %s    %d 分"), *PS->GetPlayerName(), FMath::RoundToInt(PS->GetScore())));
	}

	if (bMatchOver)
	{
		GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::Green,
			TEXT(">>>  行动结束，评定已回传  <<<"));
	}
}
