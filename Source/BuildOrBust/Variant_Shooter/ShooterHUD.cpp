// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/ShooterHUD.h"
#include "Variant_Shooter/ShooterGameState.h"
#include "WaveManager.h"
#include "BaseCore.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"

void AShooterHUD::DrawHUD()
{
	Super::DrawHUD();

	UWorld* World = GetWorld();
	if (!Canvas || !World)
	{
		return;
	}

	// 字体：优先用可爱字体资产（之后用桥创建 /Game/Variant_Shooter/UI/Font_Cute），未创建则退回引擎大字体（已比中字体大）
	static UFont* CuteFont = nullptr;
	static bool bTriedLoadFont = false;
	if (!bTriedLoadFont)
	{
		bTriedLoadFont = true;
		CuteFont = LoadObject<UFont>(nullptr, TEXT("/Game/Variant_Shooter/UI/Font_Cute.Font_Cute"));
	}
	// 有离线可爱字体（本身已 40px，矢量烘焙不糊）就用它、缩放 1.0；否则退回中字体并放大 1.7
	UFont* Font = CuteFont ? CuteFont : GEngine->GetMediumFont();
	const float FS = CuteFont ? 0.85f : 1.7f;    // 字体缩放（稍微调小一点）
	const float LH = CuteFont ? 44.0f : 44.0f;   // 行距
	const float ScreenW = Canvas->SizeX;

	// ===== 玩家 / 对局状态 =====
	APlayerController* PC = GetOwningPlayerController();
	APlayerState* Me = PC ? PC->PlayerState : nullptr;
	AShooterGameState* GS = World->GetGameState<AShooterGameState>();

	// 左上文字绘制器：先黑影后彩字，任何背景都清晰
	float LX = 30.0f;
	float LY = 100.0f;
	auto DrawLeft = [&](const FString& Text, const FLinearColor& Color)
	{
		DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), LX + 2.5f, LY + 2.5f, Font, FS);
		DrawText(Text, Color, LX, LY, Font, FS);
		LY += LH;
	};

	// ===== 比赛结束：左上画一块干净的结算摘要，并隐藏实时 HUD（不与结算弹窗重叠）=====
	if (GS && GS->IsMatchOver())
	{
		int32 Wave = 0, MaxW = 10;
		for (TActorIterator<AWaveManager> It(World); It; ++It)
		{
			Wave = FMath::Min(It->GetCurrentWave(), It->MaxWave);
			MaxW = It->MaxWave;
			break;
		}

		// 按 PlayerId 稳定排序 → P1/P2 在两端屏幕指向同一人
		TArray<APlayerState*> Players;
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (PS) { Players.Add(PS); }
		}
		Players.Sort([](const APlayerState& A, const APlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });

		// 最高分（PVP 胜者）与平局判断
		int32 BestScore = -1;
		for (APlayerState* PS : Players)
		{
			BestScore = FMath::Max(BestScore, FMath::RoundToInt(PS->GetScore()));
		}
		int32 BestIdx = INDEX_NONE, BestCount = 0;
		for (int32 i = 0; i < Players.Num(); i++)
		{
			if (FMath::RoundToInt(Players[i]->GetScore()) == BestScore)
			{
				BestCount++;
				if (BestIdx == INDEX_NONE) { BestIdx = i; }
			}
		}

		LY = 90.0f;
		DrawLeft(TEXT("===== 对战结束 ====="), FLinearColor(1.0f, 0.85f, 0.2f));
		DrawLeft(FString::Printf(TEXT("撑过 %d / %d 波"), Wave, MaxW), FLinearColor(1.0f, 0.6f, 0.1f));

		// 各玩家得分：P1 / P2，本机玩家标「你」并高亮
		for (int32 i = 0; i < Players.Num(); i++)
		{
			const bool bSelf = (Players[i] == Me);
			DrawLeft(FString::Printf(TEXT("P%d%s   %d 分"), i + 1, bSelf ? TEXT("（你）") : TEXT("　　　"),
				FMath::RoundToInt(Players[i]->GetScore())),
				bSelf ? FLinearColor(0.3f, 1.0f, 0.4f) : FLinearColor(1.0f, 0.9f, 0.9f));
		}

		// 胜负判定（需有 2 名玩家）
		if (Players.Num() >= 2 && BestIdx != INDEX_NONE)
		{
			if (BestCount > 1)
			{
				DrawLeft(TEXT(">> 平局！ <<"), FLinearColor(1.0f, 0.9f, 0.4f));
			}
			else
			{
				const bool bMeWon = (Players[BestIdx] == Me);
				DrawLeft(FString::Printf(TEXT(">> 胜者：P%d <<"), BestIdx + 1),
					bMeWon ? FLinearColor(0.3f, 1.0f, 0.4f) : FLinearColor(1.0f, 0.55f, 0.3f));
			}
		}
		return;   // 结束后不再画实时 HUD，避免与结算弹窗重叠
	}

	// ===== 对局进行中：实时 HUD =====
	// 开局简报：前 12 秒显示目标，之后自动隐藏（重开关卡会重置计时）
	if (World->GetTimeSeconds() < 12.0f)
	{
		DrawLeft(TEXT("【目标】双人同场竞技：击杀丧尸得分"), FLinearColor::White);
		DrawLeft(TEXT("守住中央核心，撑满十波后比分高者获胜！"), FLinearColor(1.0f, 0.9f, 0.5f));
	}

	for (TActorIterator<AWaveManager> It(World); It; ++It)
	{
		DrawLeft(FString::Printf(TEXT("第 %d / %d 波      剩余敌人：%d"),
			It->GetCurrentWave(), It->MaxWave, It->GetAliveEnemyCount()),
			FLinearColor(1.0f, 0.6f, 0.1f));
		break;
	}

	for (TActorIterator<ABaseCore> It(World); It; ++It)
	{
		DrawLeft(FString::Printf(TEXT("核心血量：%.0f / %.0f"), It->GetBaseHP(), It->MaxBaseHP),
			FLinearColor(1.0f, 0.3f, 0.3f));
		break;
	}

	// ===== 右上：本玩家 vs 同场 实时得分 =====
	float RX = ScreenW - 470.0f;
	float RY = 45.0f;
	auto DrawRight = [&](const FString& Text, const FLinearColor& Color)
	{
		DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), RX + 2.5f, RY + 2.5f, Font, FS);
		DrawText(Text, Color, RX, RY, Font, FS);
		RY += LH;
	};

	DrawRight(TEXT("【 得分对比 】"), FLinearColor::Yellow);

	if (GS)
	{
		// 按 PlayerId 稳定排序 → 两端屏幕 P1/P2 指向同一人
		TArray<APlayerState*> Players;
		for (APlayerState* PS : GS->PlayerArray)
		{
			if (PS) { Players.Add(PS); }
		}
		Players.Sort([](const APlayerState& A, const APlayerState& B) { return A.GetPlayerId() < B.GetPlayerId(); });

		for (int32 i = 0; i < Players.Num(); i++)
		{
			const bool bSelf = (Players[i] == Me);
			DrawRight(FString::Printf(TEXT("P%d%s   %d 分"), i + 1, bSelf ? TEXT("（你）") : TEXT("　　　"),
				FMath::RoundToInt(Players[i]->GetScore())),
				bSelf ? FLinearColor(0.3f, 1.0f, 0.4f) : FLinearColor(1.0f, 0.85f, 0.85f));
		}
		if (Players.Num() < 2)
		{
			DrawRight(TEXT("（等待玩家加入…）"), FLinearColor(0.6f, 0.6f, 0.6f));
		}
	}
}
