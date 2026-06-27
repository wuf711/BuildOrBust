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

AShooterGameState::AShooterGameState()
{
}

void AShooterGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterGameState, bMatchOver);
	DOREPLIFETIME(AShooterGameState, WinnerName);
}

void AShooterGameState::BeginPlay()
{
	Super::BeginPlay();
	// 计分板/状态显示已交给 AShooterHUD（per-player 绘制），此处不再用全局屏幕消息，避免多人时互相抢位置
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
		TEXT("【目标】守住中央核心，击杀来袭的丧尸，撑过全部波次即胜利！"));

	// 波次 + 剩余敌人
	for (TActorIterator<AWaveManager> It(World); It; ++It)
	{
		AWaveManager* WM = *It;
		GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::Orange,
			FString::Printf(TEXT("第 %d / %d 波      剩余敌人：%d"),
				WM->GetCurrentWave(), WM->MaxWave, WM->GetAliveEnemyCount()));
		break;
	}

	// 核心血量
	for (TActorIterator<ABaseCore> It(World); It; ++It)
	{
		ABaseCore* BC = *It;
		GEngine->AddOnScreenDebugMessage(Key++, 0.6f, FColor::Red,
			FString::Printf(TEXT("核心血量：%.0f / %.0f"), BC->GetBaseHP(), BC->MaxBaseHP));
		break;
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
			FString::Printf(TEXT(">>>  胜者: %s  <<<"), *WinnerName));
	}
}
