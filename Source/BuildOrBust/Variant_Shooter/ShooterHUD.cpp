// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/ShooterHUD.h"
#include "Variant_Shooter/ShooterGameState.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "WaveManager.h"
#include "BaseCore.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

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
		if (TActorIterator<AWaveManager> It(World); It)
		{
			Wave = FMath::Min(It->GetCurrentWave(), It->MaxWave);
			MaxW = It->MaxWave;
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
	else
	{
		// 简报隐藏后保留其占位，避免波次/核心血量行上移贴住顶部血条
		LY += 2.0f * LH;
	}

	if (TActorIterator<AWaveManager> It(World); It)
	{
		const int32 CurWave = It->GetCurrentWave();
		// 新一波开始 → 屏幕中央弹 3 秒开场横幅（CurrentWave 已复制，客户端同样生效）
		if (CurWave > LastSeenWave)
		{
			if (CurWave >= 1)
			{
				WaveBannerUntil = World->GetTimeSeconds() + 3.0f;
			}
			LastSeenWave = CurWave;
		}

		DrawLeft(FString::Printf(TEXT("第 %d / %d 波      剩余敌人：%d"),
			CurWave, It->MaxWave, It->GetAliveEnemyCount()),
			FLinearColor(1.0f, 0.6f, 0.1f));
	}

	if (TActorIterator<ABaseCore> It(World); It)
	{
		const float CoreHP = It->GetBaseHP();
		// 核心掉血 → 中央弹 1.5 秒受击警示（CurrentBaseHP 已复制，客户端同样生效）
		if (LastSeenCoreHP >= 0.0f && CoreHP < LastSeenCoreHP - 0.1f)
		{
			CoreAlertUntil = World->GetTimeSeconds() + 1.5f;
		}
		LastSeenCoreHP = CoreHP;

		DrawLeft(FString::Printf(TEXT("核心血量：%.0f / %.0f"), CoreHP, It->MaxBaseHP),
			FLinearColor(1.0f, 0.3f, 0.3f));
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
		// 单机（手机/独立进程）不可能有人加入，不显示等待提示，减少界面噪音
		if (Players.Num() < 2 && World->GetNetMode() != NM_Standalone)
		{
			DrawRight(TEXT("（等待玩家加入…）"), FLinearColor(0.6f, 0.6f, 0.6f));
		}
	}

	// ===== 中央战斗反馈：波次开场横幅 / 核心受击警示 =====
	// 文案只用离线字体已烘字符（"来袭/受到/警告"等字未烘，重烘字体后可换）
	const float Now = World->GetTimeSeconds();
	auto DrawCentered = [&](const FString& Text, const FLinearColor& Color, float Y, float Scale)
	{
		float XL = 0.0f, YL = 0.0f;
		Canvas->TextSize(Font, Text, XL, YL, Scale, Scale);
		const float X = (ScreenW - XL) * 0.5f;
		DrawText(Text, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), X + 2.5f, Y + 2.5f, Font, Scale);
		DrawText(Text, Color, X, Y, Font, Scale);
	};

	if (Now < WaveBannerUntil && LastSeenWave >= 1)
	{
		DrawCentered(FString::Printf(TEXT("—— 第 %d 波来袭 ——"), LastSeenWave),
			FLinearColor(1.0f, 0.75f, 0.1f), 170.0f, 1.5f);
	}
	if (Now < CoreAlertUntil)
	{
		DrawCentered(TEXT("！核心受到攻击！"), FLinearColor(1.0f, 0.25f, 0.2f), 235.0f, 1.2f);
	}

	// 连击提示：连续击杀时在画面下方显示（ComboCount/Multiplier 已复制，双端生效）
	if (const AShooterCharacter* MyChar = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr)
	{
		const int32 Combo = MyChar->GetComboCount();
		if (Combo >= 2)
		{
			const int32 BonusPct = FMath::RoundToInt((MyChar->GetComboMultiplier() - 1.0f) * 100.0f);
			DrawCentered(FString::Printf(TEXT("连击 ×%d（伤害 +%d%%）"), Combo, BonusPct),
				FLinearColor(1.0f, 0.55f, 0.1f), Canvas->SizeY * 0.76f, 1.1f);
		}
	}

	// 击杀飘分：本机得分增长瞬间，准星下方上浮显示「+N 分」（分数走 PlayerState 复制，双端生效）
	if (Me)
	{
		const float MyScore = Me->GetScore();
		if (LastSeenMyScore >= 0.0f && MyScore > LastSeenMyScore + 0.1f)
		{
			ScorePopupAmount = FMath::RoundToInt(MyScore - LastSeenMyScore);
			ScorePopupUntil = Now + 1.0f;

			// 击杀确认音：复用模板枪声，调高音调压低音量作"确认滴答"（仅本机 2D 播放）
			static USoundBase* KillSound = LoadObject<USoundBase>(nullptr,
				TEXT("/Game/Weapons/GrenadeLauncher/Audio/FirstPersonTemplateWeaponFire02.FirstPersonTemplateWeaponFire02"));
			if (KillSound)
			{
				UGameplayStatics::PlaySound2D(World, KillSound, 0.5f, 1.8f);
			}
		}
		LastSeenMyScore = MyScore;
	}
	if (Now < ScorePopupUntil && ScorePopupAmount > 0)
	{
		// 剩余时间越少位置越高 → 轻微上浮动画
		const float T = FMath::Clamp(ScorePopupUntil - Now, 0.0f, 1.0f);
		const float Y = Canvas->SizeY * 0.5f + 70.0f - (1.0f - T) * 40.0f;
		DrawCentered(FString::Printf(TEXT("+%d 分"), ScorePopupAmount),
			FLinearColor(0.4f, 1.0f, 0.45f), Y, 1.0f);
	}

	// 击杀确认标记：击杀后 0.25 秒内，准星四周闪出 X 形短线（复用飘分计时，不加新状态）
	if (Now < ScorePopupUntil - 0.75f)
	{
		const float CX = ScreenW * 0.5f;
		const float CY = Canvas->SizeY * 0.5f;
		const FLinearColor MarkColor(1.0f, 0.9f, 0.3f);
		for (int32 SX = -1; SX <= 1; SX += 2)
		{
			for (int32 SY = -1; SY <= 1; SY += 2)
			{
				DrawLine(CX + SX * 8.0f, CY + SY * 8.0f, CX + SX * 18.0f, CY + SY * 18.0f, MarkColor, 2.0f);
			}
		}
	}
}
