// Copyright Epic Games, Inc. All Rights Reserved.

#include "Variant_Shooter/ShooterHUD.h"
#include "Variant_Shooter/ShooterGameState.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "WaveManager.h"
#include "Boss_CS07.h"
#include "BoBEnergyPillar.h"
#include "BoBEnemy.h"
#include "GameFramework/GameStateBase.h"
#include "BaseCore.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "Variant_Shooter/Weapons/BoBFabWeapons.h"
#include "LootPickup.h"
#include "BODPlayerState.h"
#include "BoBShop.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Texture2D.h"
#include "Engine/Font.h"
#include "Fonts/CompositeFont.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"

// 结算画面首次出现的真实时刻（渐暗用；对局进行中复位，跨局安全）
static double GBoBMatchOverSeen = -1.0;

// 类名→武器显示名（新枪种在此登记；Coil/LaserPistol 含 Rifle/Pistol 子串，须先判长名）
static FString BoBWeaponDisplayName(const FString& CN)
{
	if (CN.Contains(TEXT("Dice")))        { return TEXT("混沌比特"); }
	if (CN.Contains(TEXT("CoilRifle")))   { return TEXT("奔雷磁轨"); }
	if (CN.Contains(TEXT("LaserPistol"))) { return TEXT("曳星光铳"); }
	if (CN.Contains(TEXT("Rifle")))       { return TEXT("照明步枪"); }
	if (CN.Contains(TEXT("Grenade")))     { return TEXT("热寂单元"); }
	return TEXT("武器");
}

// 武器详情文案（用户定稿）。背包页选中武器时展示。
static FString BoBWeaponFlavor(const FString& CN)
{
	if (CN.Contains(TEXT("Dice")))
		return TEXT("抛出这个刻满相斥纹路的爆破物后，没有人能预判它在半空重组引爆的灾难形态。由于杀伤逻辑完全处于随机的混沌态，现行守则只能无奈地要求使用者，在生还后自行记录它的爆发表现。");
	if (CN.Contains(TEXT("CoilRifle")))
		return TEXT("为了换取极其狂暴的穿甲效率，工程部直接抹除了这把武器的安全延迟。巨大的枪口激波能轻易震穿耳膜，因此新兵手册上的第一课就是：开火前，必须张开你的嘴。");
	if (CN.Contains(TEXT("LaserPistol")))
		return TEXT("未知的能量被死死封存在螺旋玻璃腔内，向外溢散着流转的光晕。命中目标的瞬间会附带两秒的强光信标——与其说是战术标记，不如说是设计者试图在绝望中发出的微弱求救信号。");
	if (CN.Contains(TEXT("Rifle")))
		return TEXT("搭载大功率探照模组的制式火器。其刺眼的光束足以撕开最浓重的雾霭。但在说明书的扉页，装备部印着一行极大的免责声明：光束不具备诱敌属性，异常实体感兴趣的，是被强光暴露的持枪者。");
	if (CN.Contains(TEXT("Grenade")))
		return TEXT("扣下扳机后，武器会陷入长达半秒令人窒息的绝对静默，随后才是足以融化一切的轰鸣。无数老兵用生命为这半秒延迟留下了唯一忠告：轰鸣到来前，把你的双脚像钉子一样死死钉在原地。");
	return FString();
}

UTexture2D* AShooterHUD::GetItemIcon(const FString& Path)
{
	if (Path.IsEmpty()) { return nullptr; }
	if (const TObjectPtr<UTexture2D>* Found = IconCache.Find(Path))
	{
		return Found->Get();
	}
	// 缺资产也写入 nullptr，缓存住失败结果，不让每帧都去磁盘找一遍
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path);
	IconCache.Add(Path, Tex);
	return Tex;
}

void AShooterHUD::DrawHUD()
{
	Super::DrawHUD();

	UWorld* World = GetWorld();
	if (!Canvas || !World)
	{
		return;
	}

	// 字体：运行时组装雅黑 Bold（FontFace → UFont，全字库动态渲染，永不缺字且加粗）
	if (!bTriedBuildFont)
	{
		bTriedBuildFont = true;
		if (UObject* Face = LoadObject<UObject>(nullptr, TEXT("/Game/Variant_Shooter/UI/Font_HUD.Font_HUD")))
		{
			UFont* F = NewObject<UFont>(this);
			F->FontCacheType = EFontCacheType::Runtime;
			FTypefaceEntry& Entry = F->GetMutableInternalCompositeFont().DefaultTypeface.Fonts.AddDefaulted_GetRef();
			Entry.Name = TEXT("Default");
			Entry.Font = FFontData(Face);
			F->LegacyFontSize = 26;
			RuntimeFont = F;
		}
	}
	UFont* Font = RuntimeFont;
	float FS = 1.1f;     // 运行时字体基准缩放
	float LH = 42.0f;    // 行距
	if (!Font)
	{
		// 兜底：旧离线字体（字库不全）→ 引擎中字体
		static UFont* CuteFont = LoadObject<UFont>(nullptr, TEXT("/Game/Variant_Shooter/UI/Font_Cute.Font_Cute"));
		Font = CuteFont ? CuteFont : GEngine->GetMediumFont();
		FS = CuteFont ? 0.85f : 1.7f;
		LH = 44.0f;
	}
	const float ScreenW = Canvas->SizeX;

	// ===== 玩家 / 对局状态 =====
	APlayerController* PC = GetOwningPlayerController();
	APlayerState* Me = PC ? PC->PlayerState : nullptr;
	AShooterGameState* GS = World->GetGameState<AShooterGameState>();

	// 左上文字绘制器：先黑影后彩字，任何背景都清晰（左上让位给小地图，文字排在其右侧）
	float LX = 396.0f;
	float LY = 36.0f;
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

		// ===== 撤离结算：整屏渐暗 + 左侧战果面板（队友口径：不判胜负，逐人清点表现）=====
		if (GBoBMatchOverSeen < 0.0)
		{
			GBoBMatchOverSeen = FPlatformTime::Seconds();
		}
		const float Fade = FMath::Clamp((float)(FPlatformTime::Seconds() - GBoBMatchOverSeen) / 1.4f, 0.0f, 1.0f);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f * Fade), 0.0f, 0.0f, ScreenW, Canvas->SizeY);
		if (Fade < 0.12f)
		{
			return;
		}

		bool bCoreAlive = true;
		if (TActorIterator<ABaseCore> CoreIt(World); CoreIt)
		{
			bCoreAlive = CoreIt->GetBaseHP() > 0.0f;
		}
		auto BankedOf = [&](APlayerState* PS) -> int32
		{
			for (TActorIterator<AShooterCharacter> CIt(World); CIt; ++CIt)
			{
				if (CIt->GetPlayerState() == PS)
				{
					return CIt->GetBankedValue();
				}
			}
			return 0;
		};

		const float PW = 620.0f;
		const float PH = 238.0f + Players.Num() * (LH * 0.85f + 6.0f);
		const float PX = 64.0f, PY = (Canvas->SizeY - PH) * 0.5f;
		DrawRect(FLinearColor(0.05f, 0.07f, 0.10f, 0.92f * Fade), PX, PY, PW, PH);
		const FLinearColor Frame(0.35f, 0.75f, 0.85f, 0.9f * Fade);
		DrawLine(PX, PY, PX + PW, PY, Frame, 2.0f);
		DrawLine(PX, PY + PH, PX + PW, PY + PH, Frame, 2.0f);
		DrawLine(PX, PY, PX, PY + PH, Frame, 2.0f);
		DrawLine(PX + PW, PY, PX + PW, PY + PH, Frame, 2.0f);

		float RowY = PY + 24.0f;
		auto PRow = [&](const FString& T, const FLinearColor& C, float Scale)
		{
			float TW = 0.0f, TH = 0.0f;
			GetTextSize(T, TW, TH, Font, FS * Scale);
			const float TX = PX + (PW - TW) * 0.5f;
			DrawText(T, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f * Fade), TX + 2.0f, RowY + 2.0f, Font, FS * Scale);
			FLinearColor CC = C;
			CC.A *= Fade;
			DrawText(T, CC, TX, RowY, Font, FS * Scale);
			RowY += LH * Scale + 6.0f;
		};
		PRow(TEXT("——  行 动 结 束  ——"), FLinearColor(1.0f, 0.85f, 0.3f), 0.95f);
		PRow(bCoreAlive
			? FString::Printf(TEXT("坚守至 TIDE %d · 回收通道已开启"), Wave)
			: FString::Printf(TEXT("基准核心在 TIDE %d 陷落 · 回收中断"), Wave),
			FLinearColor(0.92f, 0.92f, 0.88f), 0.72f);
		RowY += 8.0f;
		for (int32 i = 0; i < Players.Num(); i++)
		{
			const bool bSelf = (Players[i] == Me);
			const int32 Total = FMath::RoundToInt(Players[i]->GetScore());
			const int32 Bank = BankedOf(Players[i]);
			PRow(FString::Printf(TEXT("P%d%s   %d 分（击杀 %d ＋ 物资 %d）"), i + 1,
				bSelf ? TEXT("（你）") : TEXT("　　　"), Total, FMath::Max(0, Total - Bank), Bank),
				bSelf ? FLinearColor(0.3f, 1.0f, 0.4f) : FLinearColor(0.9f, 0.9f, 0.9f), 0.78f);
		}
		RowY += 6.0f;
		PRow(TEXT("每名幸存者的表现已单独清点存档"), FLinearColor(0.55f, 0.85f, 0.95f), 0.62f);
		RowY += 2.0f;
		PRow(World->GetAuthGameMode() ? TEXT("[R] 重新开始") : TEXT("等待主机重新开始…"),
			FLinearColor(1.0f, 0.95f, 0.6f), 0.68f);
		return;   // 结束后不再画实时 HUD
	}

	// ===== 对局进行中：实时 HUD =====
	// （旧 12 秒开局简报已由开局指南弹窗取代）
	GBoBMatchOverSeen = -1.0;   // 对局进行中持续复位，重开一局后结算渐暗重新生效

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

		// 波次类型：3/6/9 精英战、末波 Boss 战（用已烘字，勿改字）
		{
			const int32 Kind = It->GetWaveKind(CurWave);
			// 潮次分档用同化体三档命名：表层 / 渐进 / 渗透。
			// 普通潮不标（就是表层），3/6/9 渐进，第 10 潮渗透。
			// 字库已按源码用字重烘（tools/font_add_chars.py），换字后跑一遍那个脚本再 Reimport，
			// 否则新字在 HUD 上是空白
			const TCHAR* KindTxt = (Kind == 2) ? TEXT("   【 渗 透 同 化 】") : ((Kind == 1) ? TEXT("   【 渐 进 同 化 】") : TEXT(""));
			const FLinearColor WaveCol = (Kind == 2) ? FLinearColor(1.0f, 0.25f, 0.2f)
									   : ((Kind == 1) ? FLinearColor(1.0f, 0.42f, 0.12f)
													  : FLinearColor(1.0f, 0.6f, 0.1f));
			DrawLeft(FString::Printf(TEXT("TIDE %d      残余同化体：%d%s"),
				CurWave, It->GetAliveEnemyCount(), KindTxt), WaveCol);
		}

		// 波间搜刮窗口（顶部居中，服务器时间同步）
		const float Remain = It->GetIntervalRemaining();
		if (Remain >= 0.0f)
		{
			// 补给时段（险区勘探之前的 45s）：明确告诉玩家现在能采买，别让人干等
			const float SupplyLeft = It->GetSupplyRemaining();
			if (SupplyLeft >= 0.0f)
			{
				const FString SHead = TEXT("补 给 时 段");
				const FString SSub = FString::Printf(TEXT("[ B ] 调取投送清单      [ F ] 在投送端补备弹      %d 秒后进入勘探"),
					FMath::CeilToInt(SupplyLeft));
				float SW = 0.0f, SH = 0.0f, SbW = 0.0f, SbH = 0.0f;
				GetTextSize(SHead, SW, SH, Font, FS * 1.1f);
				GetTextSize(SSub, SbW, SbH, Font, FS * 0.6f);
				const float SHX = (ScreenW - SW) * 0.5f;
				DrawText(SHead, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), SHX + 2.0f, 94.0f, Font, FS * 1.1f);
				DrawText(SHead, FLinearColor(1.0f, 0.80f, 0.32f), SHX, 92.0f, Font, FS * 1.1f);
				const float SSX = (ScreenW - SbW) * 0.5f;
				DrawText(SSub, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f), SSX + 1.5f, 92.0f + SH + 7.5f, Font, FS * 0.6f);
				DrawText(SSub, FLinearColor(0.88f, 0.86f, 0.72f), SSX, 92.0f + SH + 6.0f, Font, FS * 0.6f);

				// 补给倒计时条：走完就进险区勘探
				const float SB_W = FMath::Min(ScreenW * 0.34f, 460.0f), SB_H = 7.0f;
				const float SB_X = (ScreenW - SB_W) * 0.5f, SB_Y = 92.0f + SH + SbH + 14.0f;
				DrawRect(FLinearColor(0.03f, 0.03f, 0.02f, 0.85f), SB_X - 2.0f, SB_Y - 2.0f, SB_W + 4.0f, SB_H + 4.0f);
				const FLinearColor SGold(0.72f, 0.58f, 0.24f, 0.85f);
				DrawRect(SGold, SB_X - 2.0f, SB_Y - 2.0f, SB_W + 4.0f, 1.0f);
				DrawRect(SGold, SB_X - 2.0f, SB_Y + SB_H + 1.0f, SB_W + 4.0f, 1.0f);
				const float SFrac = FMath::Clamp(SupplyLeft / FMath::Max(1.0f, It->SupplyPhaseDuration), 0.0f, 1.0f);
				DrawRect(SupplyLeft < 10.0f ? FLinearColor(1.0f, 0.45f, 0.25f) : FLinearColor(1.0f, 0.78f, 0.28f),
					SB_X, SB_Y, SB_W * SFrac, SB_H);
			}

			// 险区勘探入场：不用体育赛事式 3-2-1，改为诡奇末世口吻的静默开场
			// 险区勘探时限随波次成长，Elapsed 须按本波实际时限算，否则后期开场提示会错位
			// 总时长 = 入场提示 + 探索时限；Elapsed 用于判断入场阶段
			const float Elapsed = (It->GetShadowCruiseDuration() + It->ShadowCruiseIntro) - Remain;
			if (Elapsed < 7.0f)
			{
				// 0~4.6s：主标题淡入淡出；4.6~7.0s：副标题
				const bool bPhase1 = Elapsed < 4.6f;
				const FString Line = bPhase1 ? TEXT("此 刻 它 还 没 有 抬 眼")
											 : TEXT("潜 影 开 始");
				const float LineFS = bPhase1 ? FS * 1.35f : FS * 1.7f;
				// 淡入淡出 alpha
				const float LocalT = bPhase1 ? (Elapsed / 4.6f) : ((Elapsed - 4.6f) / 2.4f);
				const float A = FMath::Clamp(FMath::Sin(LocalT * PI) * 1.6f, 0.0f, 1.0f);
				float IntroW = 0.0f, IntroH = 0.0f;
				GetTextSize(Line, IntroW, IntroH, Font, LineFS);
				const float IntroX = (ScreenW - IntroW) * 0.5f, IntroY = Canvas->SizeY * 0.23f;
				DrawText(Line, FLinearColor(0.0f, 0.0f, 0.0f, 0.85f * A), IntroX + 3.0f, IntroY + 3.0f, Font, LineFS);
				DrawText(Line, bPhase1 ? FLinearColor(0.62f, 0.80f, 0.92f, A)
									   : FLinearColor(1.0f, 0.80f, 0.32f, A), IntroX, IntroY, Font, LineFS);
			}
			// 入场提示期间隐藏倒计时，避免两处文字打架
			if (Elapsed >= 4.6f)
			{
				// 提示不占探索时间：倒计时钳到探索时限，从满时长开始走
				const float ShowRemain = FMath::Min(Remain, It->GetShadowCruiseDuration());
				const FString CD = FString::Printf(TEXT("勘探 %d    %02d:%02d"),
					CurWave, FMath::FloorToInt(ShowRemain / 60.0f), FMath::FloorToInt(ShowRemain) % 60);
				float CDW = 0.0f, CDH = 0.0f;
				GetTextSize(CD, CDW, CDH, Font, FS * 0.95f);
				const float CDX = (ScreenW - CDW) * 0.5f;
				DrawText(CD, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), CDX + 2.0f, 98.0f, Font, FS * 0.95f);
				DrawText(CD, Remain < 15.0f ? FLinearColor(1.0f, 0.45f, 0.3f) : FLinearColor(0.5f, 0.95f, 1.0f), CDX, 96.0f, Font, FS * 0.95f);

				// 险区勘探进度条：时限随波成长，条长固定、填充随剩余时间缩短
				const float CruiseTotal = FMath::Max(1.0f, It->GetShadowCruiseDuration());
				const float PB_W = FMath::Min(ScreenW * 0.34f, 460.0f), PB_H = 7.0f;
				const float PB_X = (ScreenW - PB_W) * 0.5f, PB_Y = 96.0f + CDH + 6.0f;
				DrawRect(FLinearColor(0.03f, 0.03f, 0.02f, 0.85f), PB_X - 2.0f, PB_Y - 2.0f, PB_W + 4.0f, PB_H + 4.0f);
				const FLinearColor PGold(0.72f, 0.58f, 0.24f, 0.85f);
				DrawRect(PGold, PB_X - 2.0f, PB_Y - 2.0f, PB_W + 4.0f, 1.0f);
				DrawRect(PGold, PB_X - 2.0f, PB_Y + PB_H + 1.0f, PB_W + 4.0f, 1.0f);
				const float PFrac = FMath::Clamp(ShowRemain / CruiseTotal, 0.0f, 1.0f);
				DrawRect(Remain < 15.0f ? FLinearColor(1.0f, 0.35f, 0.22f) : FLinearColor(0.45f, 0.88f, 1.0f),
					PB_X, PB_Y, PB_W * PFrac, PB_H);
			}
		}

		// 开局等待全体就绪提示（简报已关但对局未开），显示就绪进度 n/m
		if (It->IsWaitingForReady() && !bShowGuide)
		{
			// AShooterCharacter 迭代器只枚举玩家角色；AShooterNPC 是它的兄弟类。
			int32 NumReady = 0, NumTotal = 0;
			for (TActorIterator<AShooterCharacter> ChIt(World); ChIt; ++ChIt)
			{
				NumTotal++;
				if (ChIt->IsReadyToStart()) { NumReady++; }
			}
			const FString WT = FString::Printf(TEXT("已就绪 (%d/%d)，等待其余幸存者查看简报…"), NumReady, FMath::Max(NumTotal, 1));
			float WTW = 0.0f, WTH = 0.0f;
			GetTextSize(WT, WTW, WTH, Font, FS * 0.85f);
			DrawText(WT, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), (ScreenW - WTW) * 0.5f + 2.0f, Canvas->SizeY * 0.35f + 2.0f, Font, FS * 0.85f);
			DrawText(WT, FLinearColor(1.0f, 0.95f, 0.6f), (ScreenW - WTW) * 0.5f, Canvas->SizeY * 0.35f, Font, FS * 0.85f);
		}
	}

	float CoreHPNow = -1.0f, CoreHPMax = 0.0f;
	if (TActorIterator<ABaseCore> It(World); It)
	{
		const float CoreHP = It->GetBaseHP();
		// 核心掉血 → 中央弹 1.5 秒受击警示（CurrentBaseHP 已复制，客户端同样生效）
		if (LastSeenCoreHP >= 0.0f && CoreHP < LastSeenCoreHP - 0.1f)
		{
			CoreAlertUntil = World->GetTimeSeconds() + 1.5f;
		}
		LastSeenCoreHP = CoreHP;

		// 数值交给左侧竖直圆筒表（本函数尾部绘制），此处不再画文本行
		CoreHPNow = CoreHP;
		CoreHPMax = It->MaxBaseHP;
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
		DrawCentered(FString::Printf(TEXT("TIDE %d"), LastSeenWave),
			FLinearColor(1.0f, 0.75f, 0.1f), 170.0f, 1.5f);
	}
	if (Now < CoreAlertUntil)
	{
		DrawCentered(TEXT("！核心受到攻击！"), FLinearColor(1.0f, 0.25f, 0.2f), 235.0f, 1.2f);
	}

	// ===== 失谐值（招牌）：右上竖条 + 档位文字。贪婪=被看见，越高越危险 =====
	if (const AShooterCharacter* GChar = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr)
	{
		const float G = FMath::Clamp(GChar->GetGaze() / 100.0f, 0.0f, 1.0f);
		const int32 Band = GChar->GetGazeBand();
		// 黑金档位配色：平静=暗金 / 警觉=亮金 / 锁定=橙金 / 暴露=血红
		static const FLinearColor BandCol[4] = {
			FLinearColor(0.55f, 0.44f, 0.18f),
			FLinearColor(0.91f, 0.76f, 0.34f),
			FLinearColor(1.00f, 0.55f, 0.16f),
			FLinearColor(0.95f, 0.16f, 0.12f)
		};
		static const TCHAR* BandTxt[4] = { TEXT("平静"), TEXT("警觉"), TEXT("锁定"), TEXT("暴露") };

		// 黑金风：屏幕正上方横条(长条，四段临界色)
		const float BarW = FMath::Min(ScreenW * 0.42f, 620.0f), BarH = 11.0f;
		const float BarX = (ScreenW - BarW) * 0.5f, BarY = 18.0f;
		// 触顶时条体呼吸闪烁
		float Pulse = 1.0f;
		if (Band >= 2)
		{
			Pulse = 0.65f + 0.35f * FMath::Sin(Now * (Band == 3 ? 9.0f : 4.5f));
		}
		// 黑底 + 细金边框(手稿风)
		DrawRect(FLinearColor(0.03f, 0.03f, 0.02f, 0.88f), BarX - 3.0f, BarY - 3.0f, BarW + 6.0f, BarH + 6.0f);
		const FLinearColor Gold(0.72f, 0.58f, 0.24f, 0.9f);
		DrawRect(Gold, BarX - 3.0f, BarY - 3.0f, BarW + 6.0f, 1.0f);
		DrawRect(Gold, BarX - 3.0f, BarY + BarH + 2.0f, BarW + 6.0f, 1.0f);
		DrawRect(Gold, BarX - 3.0f, BarY - 3.0f, 1.0f, BarH + 6.0f);
		DrawRect(Gold, BarX + BarW + 2.0f, BarY - 3.0f, 1.0f, BarH + 6.0f);

		// 四段临界分区底色(极暗)：0-40 / 40-70 / 70-100 / 满
		static const float SegEnd[3] = { 0.40f, 0.70f, 1.00f };
		float SegStart = 0.0f;
		for (int32 s = 0; s < 3; ++s)
		{
			DrawRect(BandCol[s] * 0.16f, BarX + BarW * SegStart, BarY, BarW * (SegEnd[s] - SegStart), BarH);
			SegStart = SegEnd[s];
		}
		// 实际失谐填充：按当前档位取色，横向从左往右
		const FLinearColor Fill = BandCol[FMath::Clamp(Band, 0, 3)] * Pulse;
		DrawRect(Fill, BarX, BarY, BarW * G, BarH);
		// 临界刻度(40 / 70)：金色细竖线
		DrawRect(Gold, BarX + BarW * 0.40f, BarY - 2.0f, 1.0f, BarH + 4.0f);
		DrawRect(Gold, BarX + BarW * 0.70f, BarY - 2.0f, 1.0f, BarH + 4.0f);

		const FString GT = FString::Printf(TEXT("失谐 · %s"), BandTxt[FMath::Clamp(Band, 0, 3)]);
		float GTw = 0.0f, GTh = 0.0f;
		GetTextSize(GT, GTw, GTh, Font, FS * 0.62f);
		DrawText(GT, BandCol[FMath::Clamp(Band, 0, 3)] * Pulse,
			BarX + BarW * 0.5f - GTw * 0.5f, BarY + BarH + 5.0f, Font, FS * 0.62f);
	}

	// ===== CS-07（TIDE 10）：伪终端标题 + 抗性槽 + 回收倒计时 =====
	// 主角是倒计时不是抗性槽。打它不掉血、只烧抗性，而抗性只换倒计时——
	// 所以倒计时做得最大最亮，抗性槽退一档，玩家一眼就知道该盯哪个数
	if (const ABoss_CS07* Boss = Cast<ABoss_CS07>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ABoss_CS07::StaticClass())))
	{
		const float BW = FMath::Min(ScreenW * 0.46f, 700.0f), BH = 13.0f;
		// 122 而不是 76：76 会和左上角那行 TIDE/残余同化体/阶段标签横向撞上
		const float BX = (ScreenW - BW) * 0.5f, BY = 122.0f;
		const FLinearColor Bronze(0.80f, 0.54f, 0.22f, 1.0f);

		// 伪终端抬头。每隔一小段抽两个字符替成乱码——"生态数据不完整"这句话
		// 光写出来没用，得让它自己在屏幕上坏给玩家看。乱码字符只用 ASCII，
		// 烘录字体一定有，不会变成空白
		FString Title = TEXT("CS-07 · 未知实体 · 生态数据不完整");
		{
			static const TCHAR Junk[] = { '#', '?', '/', '*', '=', '<', '>', '+' };
			FRandomStream R(FMath::FloorToInt(Now * 5.0f));
			for (int32 k = 0; k < 2 && Title.Len() > 0; ++k)
			{
				const int32 Idx = R.RandRange(0, Title.Len() - 1);
				if (Title[Idx] != TEXT(' '))
				{
					Title[Idx] = Junk[R.RandRange(0, UE_ARRAY_COUNT(Junk) - 1)];
				}
			}
		}
		float TW = 0.0f, TH = 0.0f;
		GetTextSize(Title, TW, TH, Font, FS * 0.60f);
		DrawText(Title, Bronze * 0.85f, BX + BW * 0.5f - TW * 0.5f, BY - TH - 4.0f,
			Font, FS * 0.60f);

		// 抗性槽：底槽 + 剩余量，33/66 两处刻度就是三个崩塌节点的位置
		DrawRect(FLinearColor(0.03f, 0.02f, 0.02f, 0.90f), BX - 3.0f, BY - 3.0f, BW + 6.0f, BH + 6.0f);
		DrawRect(Bronze * 0.16f, BX, BY, BW, BH);
		DrawRect(Bronze, BX, BY, BW * FMath::Clamp(Boss->GetResistance(), 0.0f, 1.0f), BH);
		DrawRect(Bronze * 0.9f, BX + BW * 0.33f, BY - 2.0f, 1.0f, BH + 4.0f);
		DrawRect(Bronze * 0.9f, BX + BW * 0.66f, BY - 2.0f, 1.0f, BH + 4.0f);

		// 阶段名靠左，抗性百分比靠右
		static const TCHAR* PhaseTxt[3] = { TEXT("权限剥夺"), TEXT("对称性破缺"), TEXT("静默走廊") };
		const int32 Pi = FMath::Clamp((int32)Boss->GetPhase(), 0, 2);
		DrawText(PhaseTxt[Pi], Bronze * 0.8f, BX, BY + BH + 4.0f, Font, FS * 0.55f);
		const FString Pct = FString::Printf(TEXT("抗性 %d%%"),
			FMath::RoundToInt(Boss->GetResistance() * 100.0f));
		float PW = 0.0f, PH = 0.0f;
		GetTextSize(Pct, PW, PH, Font, FS * 0.55f);
		DrawText(Pct, Bronze * 0.8f, BX + BW - PW, BY + BH + 4.0f, Font, FS * 0.55f);

		// 回收倒计时：真正的过关条件，做成整块 HUD 上最大的数字
		const float Left = Boss->GetExtractionRemaining();
		const bool bUrgent = Left <= 15.0f;
		const float Beat = bUrgent ? (0.62f + 0.38f * FMath::Sin(Now * 8.0f)) : 1.0f;
		const FLinearColor CtCol = bUrgent ? FLinearColor(1.0f, 0.30f, 0.22f) * Beat
										   : FLinearColor(0.62f, 0.92f, 1.0f);
		const FString CT = FString::Printf(TEXT("回收通道  %d:%02d"),
			FMath::FloorToInt(Left / 60.0f), FMath::FloorToInt(Left) % 60);
		float CW = 0.0f, CH = 0.0f;
		GetTextSize(CT, CW, CH, Font, FS * 1.05f);
		DrawText(CT, CtCol, BX + BW * 0.5f - CW * 0.5f, BY + BH + 22.0f, Font, FS * 1.05f);

		// —— 阶段二：每人只看得见一半信息 ——
		// A 看外壳形状，B 看晶体纹路。单人把两半合并（规格书：单人合并显示，
		// 但仍要在弹幕下自行配对）。这里是玩家唯一能感知到"我只有一半"的地方，
		// 少了它，非对称就只存在于代码里
		if (Boss->GetPhase() == EBoBBossPhase::Symmetry)
		{
			int32 HalfRole = 0;
			bool bSolo = true;
			if (const AGameStateBase* PGS = GetWorld()->GetGameState())
			{
				bSolo = PGS->PlayerArray.Num() <= 1;
				if (PC && PC->PlayerState)
				{
					const int32 Idx = PGS->PlayerArray.IndexOfByKey(PC->PlayerState);
					HalfRole = (Idx == INDEX_NONE) ? 0 : (Idx % 2);
				}
			}
			const FLinearColor Cyan(0.62f, 0.92f, 1.0f);

			FString Clue;
			if (bSolo)
			{
				Clue = FString::Printf(TEXT("弱点：%s 外壳 · %s 纹路"),
					ABoBEnergyPillar::ShapeName(Boss->GetTargetShape()),
					ABoBEnergyPillar::VeinName(Boss->GetTargetVein()));
			}
			else if (HalfRole == 0)
			{
				Clue = FString::Printf(TEXT("弱点外壳：%s　（纹路在队友那边）"),
					ABoBEnergyPillar::ShapeName(Boss->GetTargetShape()));
			}
			else
			{
				Clue = FString::Printf(TEXT("弱点纹路：%s　（外壳在队友那边）"),
					ABoBEnergyPillar::VeinName(Boss->GetTargetVein()));
			}
			float ClW = 0.0f, ClH = 0.0f;
			GetTextSize(Clue, ClW, ClH, Font, FS * 0.68f);
			DrawText(Clue, Cyan, BX + BW * 0.5f - ClW * 0.5f, BY + BH + 22.0f + CH + 4.0f,
				Font, FS * 0.68f);

			// 柱子头顶标注：同样只标自己看得见的那一半
			for (TActorIterator<ABoBEnergyPillar> PIt(GetWorld()); PIt; ++PIt)
			{
				const FVector Head = PIt->GetActorLocation() + FVector(0.f, 0.f, 320.f);
				const FVector Scr = Project(Head);
				if (Scr.Z <= 0.0f) { continue; }   // 在身后，别画到屏幕上

				const FString Tag = bSolo
					? FString::Printf(TEXT("%s / %s"),
						ABoBEnergyPillar::ShapeName(PIt->GetShape()),
						ABoBEnergyPillar::VeinName(PIt->GetVein()))
					: (HalfRole == 0 ? ABoBEnergyPillar::ShapeName(PIt->GetShape())
								 : ABoBEnergyPillar::VeinName(PIt->GetVein()));
				float TgW = 0.0f, TgH = 0.0f;
				GetTextSize(Tag, TgW, TgH, Font, FS * 0.62f);
				DrawRect(FLinearColor(0.02f, 0.03f, 0.04f, 0.72f),
					Scr.X - TgW * 0.5f - 6.0f, Scr.Y - TgH * 0.5f - 3.0f, TgW + 12.0f, TgH + 6.0f);
				DrawText(Tag, Cyan, Scr.X - TgW * 0.5f, Scr.Y - TgH * 0.5f, Font, FS * 0.62f);
			}
		}
	}

	// ===== 伤害飘字 =====
	// 每次命中在敌人头顶飘一个数字，0 也照飘——打在闭合的孢子囊上就是 0，
	// 那个 0 本身就把"现在打不动"说清楚了，不需要再写一行文案去解释机制
	{
		const float Life = 0.9f;
		for (TActorIterator<ABoBEnemy> EIt(GetWorld()); EIt; ++EIt)
		{
			const ABoBEnemy* E = *EIt;
			if (!IsValid(E) || E->LastHitDamage < 0.f) { continue; }
			const float Age = Now - E->LastHitTime;
			if (Age < 0.f || Age > Life) { continue; }

			// 边飘边淡出，避免连射时糊成一团
			const float A = 1.0f - Age / Life;
			// 从命中处小幅上飘 30cm 就够，飘太远就脱离了命中点的位置信息
			const FVector Scr = Project(E->LastHitLoc + FVector(0.f, 0.f, Age * 30.f));
			if (Scr.Z <= 0.f) { continue; }

			const bool bNil = E->LastHitDamage < 0.5f;
			const FString Txt = FString::Printf(TEXT("%d"), FMath::RoundToInt(E->LastHitDamage));
			// 0 用冷灰，正常伤害用暖白——不靠文字，靠颜色区分"没打动"和"打动了"
			const FLinearColor Col = bNil ? FLinearColor(0.55f, 0.62f, 0.68f, A)
										  : FLinearColor(1.0f, 0.93f, 0.72f, A);
			float DW = 0.f, DH = 0.f;
			GetTextSize(Txt, DW, DH, Font, FS * 0.8f);
			DrawText(Txt, Col, Scr.X - DW * 0.5f, Scr.Y - DH * 0.5f, Font, FS * 0.8f);
		}
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

	// ===== 左上小地图（程序绘制风格贴图 + 实时标记）=====
	if (!bTriedLoadMinimap)
	{
		bTriedLoadMinimap = true;
		MinimapTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Wasteland/T_BoBMinimap.T_BoBMinimap"));
	}
	const float MapS = bMapBig ? FMath::Min(Canvas->SizeY * 0.82f, 900.0f) : 340.0f;   // E 键放大/缩小
	// 放大时水平居中、顶部让开波次/得分信息栏；小图贴左上角
	const float MX0 = bMapBig ? (ScreenW - MapS) * 0.5f : 24.0f;
	const float MY0 = bMapBig ? 100.0f : 24.0f;
	if (MinimapTex)
	{
		DrawRect(FLinearColor(0.05f, 0.07f, 0.10f, 0.85f), MX0 - 4.0f, MY0 - 4.0f, MapS + 8.0f, MapS + 8.0f);
		DrawTexture(MinimapTex, MX0, MY0, MapS, MapS, 0.0f, 0.0f, 1.0f, 1.0f);
		// 贴图为数据直绘：u=(X-X0)/宽, v=(Y-Z0)/高（地图世界界: X[-28766,20574] Y[-20915,24978]）
		const float WX0 = -28766.0f, WX1 = 20574.0f, WZ0 = -20915.0f, WZ1 = 24978.0f;
		auto WorldToMap = [&](const FVector& P, float& OutX, float& OutY)
		{
			OutX = MX0 + (P.X - WX0) / (WX1 - WX0) * MapS;
			OutY = MY0 + (P.Y - WZ0) / (WZ1 - WZ0) * MapS;
		};
		// 战利品标记：HUD 动态描绘（按运行时 Tag 找 Actor，尺寸与贴图分辨率无关）
		//
		// 只画走近了的。开局就在图上标满战利品，探索就退化成"照着圆点跑一遍"——
		// Signalis 那套是反过来的：扣住信息，逼玩家自己去记地形；Hunt: Showdown
		// 干脆准备把战利品和撤离点从地图上整个拿掉。我们保留标记但加距离门，
		// 让小地图回到"我刚才路过的那边有东西"，而不是一张答案卡。
		// 例外：买了诱饵信标的那一波仍然全图揭示——那是花钱买来的信息优势。
		const AShooterCharacter* LootMe = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
		const bool bRevealAll = LootMe && LootMe->bRevealLoot;
		const FVector LootMyLoc = (PC && PC->GetPawn()) ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		{
			// 战利品/武器点缩小，让核心/补给/流明装置更醒目
			const float GR = bMapBig ? 5.0f : 3.2f;
			const float WR = bMapBig ? 4.0f : 2.6f;
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				// 已被拾取的战利品不再显示图标
				if (const ALootPickup* LP = Cast<ALootPickup>(*It))
				{
					if (LP->bTaken) { continue; }
				}
				// 距离门：没走近就不标。诱饵信标那一波例外——那是花钱买来的信息优势
				if (!bRevealAll && FVector::Dist(LootMyLoc, It->GetActorLocation()) > 5600.0f)
				{
					continue;
				}
				float Px, Py;
				if (It->ActorHasTag(FName("BoBLootGem")))
				{
					// 宝物 = 圆形（白圈+蓝心）
					WorldToMap(It->GetActorLocation(), Px, Py);
					Canvas->K2_DrawPolygon(nullptr, FVector2D(Px, Py), FVector2D(GR + 1.5f, GR + 1.5f), 24, FLinearColor(1.0f, 1.0f, 1.0f, 0.9f));
					Canvas->K2_DrawPolygon(nullptr, FVector2D(Px, Py), FVector2D(GR, GR), 24, FLinearColor(1.0f, 0.78f, 0.1f));
				}
				else if (It->ActorHasTag(FName("BoBLootWpn")))
				{
					// 武器 = 方块
					WorldToMap(It->GetActorLocation(), Px, Py);
					DrawRect(FLinearColor(0.85f, 1.0f, 0.85f, 0.9f), Px - WR - 1.5f, Py - WR - 1.5f, (WR + 1.5f) * 2.0f, (WR + 1.5f) * 2.0f);
					DrawRect(FLinearColor(0.22f, 0.82f, 0.22f), Px - WR, Py - WR, WR * 2.0f, WR * 2.0f);
				}
			}
		}
		// 敌人红点：威胁档越高点越大。玩家不用去数怪，扫一眼就知道
		// "东边那团里有个大的"——这是小地图在守家游戏里最该提供的信息
		const FVector MapMyLoc = (PC && PC->GetPawn()) ? PC->GetPawn()->GetActorLocation() : FVector::ZeroVector;
		for (TActorIterator<ABoBEnemy> It(World); It; ++It)
		{
			const ABoBEnemy* E = *It;
			if (!IsValid(E) || E->IsDead()) { continue; }
			if (E->Tags.Contains(FName("BoBDummy"))) { continue; }
			// 野生同化体要走近才现形。全图铺红点的话，小地图就从"核心受多大压力"
			// 变成一张野怪分布图，潮次那条最该被一眼读到的信息反而被淹掉；
			// 而且探索的乐趣本来就在于你不知道那边蹲着什么
			if (E->Tags.Contains(FName("BoBWild"))
				&& FVector::Dist(MapMyLoc, E->GetActorLocation()) > 3200.0f)
			{
				continue;
			}

			float Px, Py;
			WorldToMap(E->GetActorLocation(), Px, Py);
			// 1 档 2.2 起步，每档 +1.1；放大地图时整体再放大
			const int32 Tier = FMath::Clamp(E->GetRow().ThreatTier, 1, 4);
			const float R = (2.2f + 1.1f * (Tier - 1)) * (bMapBig ? 1.6f : 1.0f);
			// 高档偏橙，低档偏红——大小之外再给一层颜色区分
			const FLinearColor Col = FLinearColor(1.0f, 0.12f + 0.16f * (Tier - 1), 0.12f, 0.95f);
			Canvas->K2_DrawPolygon(nullptr, FVector2D(Px, Py), FVector2D(R, R), 12, Col);
		}

		// 勘探线索：靠近到一定距离才在小地图上亮一个感叹号。
		// 这是引导而不是标注——先让玩家自己走到附近，再告诉他"这里有东西"，
		// 而不是开局就在地图上标好答案。F0 入口用的就是这个。
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->ActorHasTag(FName("BoBDiscovery"))) { continue; }
			const float Dist = FVector::Dist(MapMyLoc, It->GetActorLocation());
			if (Dist > 6500.0f) { continue; }

			float Ex, Ey;
			WorldToMap(It->GetActorLocation(), Ex, Ey);
			// 进入范围后由淡转亮，越近越实：给玩家一个"我正在靠近"的读数
			const float A = FMath::Clamp(1.0f - (Dist - 2200.0f) / 4300.0f, 0.35f, 1.0f);
			// 慢脉冲，免得和敌人红点混成一片静态色块
			const float Pulse = 0.82f + 0.18f * FMath::Sin(World->GetTimeSeconds() * 3.4f);
			const float S = (bMapBig ? 1.7f : 1.0f) * Pulse;
			const FLinearColor Gold(1.0f, 0.86f, 0.28f, A);
			const FLinearColor Dark(0.05f, 0.04f, 0.0f, A * 0.9f);
			// 感叹号：一竖一点，各带一圈深色描边，压在底图上才看得清
			DrawRect(Dark, Ex - 2.6f * S, Ey - 9.5f * S, 5.2f * S, 11.0f * S);
			DrawRect(Gold, Ex - 1.5f * S, Ey - 8.5f * S, 3.0f * S, 9.0f * S);
			DrawRect(Dark, Ex - 2.6f * S, Ey + 2.6f * S, 5.2f * S, 5.2f * S);
			DrawRect(Gold, Ex - 1.5f * S, Ey + 3.4f * S, 3.0f * S, 3.6f * S);
		}

		// 应急流明装置：青白六边形（放大，突出关键点位）
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->ActorHasTag(FName("BoBFloodlight"))) { continue; }
			float Fx, Fy;
			WorldToMap(It->GetActorLocation(), Fx, Fy);
			const float FR = bMapBig ? 11.0f : 7.0f;
			Canvas->K2_DrawPolygon(nullptr, FVector2D(Fx, Fy), FVector2D(FR + 2.5f, FR + 2.5f), 6, FLinearColor(0.30f, 0.85f, 1.0f, 0.55f));
			Canvas->K2_DrawPolygon(nullptr, FVector2D(Fx, Fy), FVector2D(FR, FR), 6, FLinearColor(0.85f, 0.98f, 1.0f, 0.95f));
		}
		// 投送端：金色方块（核心旁，按 F 补备弹）
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (!It->ActorHasTag(FName("BoBSupplyStation"))) { continue; }
			float Sx, Sy;
			WorldToMap(It->GetActorLocation(), Sx, Sy);
			const float SR = bMapBig ? 8.0f : 5.5f;
			// 与核心图标靠得太近时，沿远离核心方向推开，避免两个图标黏在一起
			float Cx, Cy;
			WorldToMap(FVector::ZeroVector, Cx, Cy);
			const float MinGap = (bMapBig ? 30.0f : 20.0f);
			FVector2D Dir(Sx - Cx, Sy - Cy);
			const float DLen = Dir.Size();
			if (DLen > 1.0f && DLen < MinGap)
			{
				Dir /= DLen;
				Sx = Cx + Dir.X * MinGap;
				Sy = Cy + Dir.Y * MinGap;
			}
			DrawRect(FLinearColor(0.10f, 0.08f, 0.02f, 0.95f), Sx - SR - 2.0f, Sy - SR - 2.0f, (SR + 2.0f) * 2.0f, (SR + 2.0f) * 2.0f);
			DrawRect(FLinearColor(1.0f, 0.78f, 0.28f), Sx - SR, Sy - SR, SR * 2.0f, SR * 2.0f);
		}
		// 核心标记 = 正八边形（白圈+青心，HUD 动态描绘）
		{
			float CoreX, CoreY;
			WorldToMap(FVector::ZeroVector, CoreX, CoreY);
			const float CR = bMapBig ? 18.0f : 12.5f;
			Canvas->K2_DrawPolygon(nullptr, FVector2D(CoreX, CoreY), FVector2D(CR + 2.5f, CR + 2.5f), 8, FLinearColor(1.0f, 1.0f, 1.0f, 0.95f));
			Canvas->K2_DrawPolygon(nullptr, FVector2D(CoreX, CoreY), FVector2D(CR, CR), 8, FLinearColor(0.18f, 0.82f, 0.88f));
		}
		// 玩家标记：导航箭头（纸飞机形，随朝向旋转；本机=白，队友=黄）
		for (TActorIterator<AShooterCharacter> It(World); It; ++It)
		{
			if (It->IsDead()) { continue; }
			float Px, Py;
			WorldToMap(It->GetActorLocation(), Px, Py);
			const bool bMe = (PC && It->GetController() == PC);
			const FLinearColor MarkC = bMe ? FLinearColor::White : FLinearColor(1.0f, 0.4f, 0.85f);   // 队友=品红，避开宝物金黄
			const float AS = bMapBig ? 13.0f : 9.0f;                    // 箭头尺寸
			const float Yaw = FMath::DegreesToRadians(It->GetActorRotation().Yaw);
			const FVector2D Fw(FMath::Cos(Yaw), FMath::Sin(Yaw));       // 前向
			const FVector2D Pp(-Fw.Y, Fw.X);                             // 侧向
			const FVector2D Tip(Px + Fw.X * AS, Py + Fw.Y * AS);
			const FVector2D L(Px - Fw.X * AS * 0.8f + Pp.X * AS * 0.62f, Py - Fw.Y * AS * 0.8f + Pp.Y * AS * 0.62f);
			const FVector2D R(Px - Fw.X * AS * 0.8f - Pp.X * AS * 0.62f, Py - Fw.Y * AS * 0.8f - Pp.Y * AS * 0.62f);
			const FVector2D Notch(Px - Fw.X * AS * 0.34f, Py - Fw.Y * AS * 0.34f);
			const float Th = bMapBig ? 3.5f : 2.6f;
			// 黑色描边（偏移一像素）提升对比
			DrawLine(Tip.X + 1, Tip.Y + 1, L.X + 1, L.Y + 1, FLinearColor(0, 0, 0, 0.8f), Th + 1.5f);
			DrawLine(L.X + 1, L.Y + 1, Notch.X + 1, Notch.Y + 1, FLinearColor(0, 0, 0, 0.8f), Th + 1.5f);
			DrawLine(Notch.X + 1, Notch.Y + 1, R.X + 1, R.Y + 1, FLinearColor(0, 0, 0, 0.8f), Th + 1.5f);
			DrawLine(R.X + 1, R.Y + 1, Tip.X + 1, Tip.Y + 1, FLinearColor(0, 0, 0, 0.8f), Th + 1.5f);
			// 箭头本体
			DrawLine(Tip.X, Tip.Y, L.X, L.Y, MarkC, Th);
			DrawLine(L.X, L.Y, Notch.X, Notch.Y, MarkC, Th);
			DrawLine(Notch.X, Notch.Y, R.X, R.Y, MarkC, Th);
			DrawLine(R.X, R.Y, Tip.X, Tip.Y, MarkC, Th);
			DrawLine(Notch.X, Notch.Y, Tip.X, Tip.Y, MarkC, Th * 0.8f);
		}

		// ===== 放大地图时：右下角图例（小字，图标+说明对齐） =====
		if (bMapBig)
		{
			// 固定像素尺寸（用 FS 相对值会被压扁成一行）
			const float LgFS = 15.0f;
			const float RowH = 26.0f;
			const int32 Rows = 5;
			const float LgW = 292.0f, LgH = RowH * Rows + 18.0f;
			const float LgX = FMath::Clamp(MX0 + MapS - LgW - 12.0f, 8.0f, ScreenW - LgW - 8.0f);
			const float LgY = FMath::Clamp(MY0 + MapS - LgH - 12.0f, 8.0f, Canvas->SizeY - LgH - 8.0f);
			// 黑金底板
			DrawRect(FLinearColor(0.03f, 0.03f, 0.02f, 0.88f), LgX, LgY, LgW, LgH);
			const FLinearColor LgGold(0.72f, 0.58f, 0.24f, 0.9f);
			DrawRect(LgGold, LgX, LgY, LgW, 1.0f);
			DrawRect(LgGold, LgX, LgY + LgH - 1.0f, LgW, 1.0f);
			DrawRect(LgGold, LgX, LgY, 1.0f, LgH);
			DrawRect(LgGold, LgX + LgW - 1.0f, LgY, 1.0f, LgH);

			const float IconX = LgX + 18.0f;      // 图标中心
			const float TextX = LgX + 36.0f;      // 文字起点
			float RowY = LgY + 9.0f;
			// 文字基线：让文字在行内垂直居中
			auto TextY = [&]() { return RowY + (RowH - LgFS) * 0.5f - 1.0f; };
			auto IconY = [&]() { return RowY + RowH * 0.5f; };

			// 走 RuntimeFont（雅黑全字库），中文可直接写
			Canvas->K2_DrawPolygon(nullptr, FVector2D(IconX, IconY()), FVector2D(8.0f, 8.0f), 8, FLinearColor(0.18f, 0.82f, 0.88f));
			DrawText(TEXT("核心 · 守住它"), FLinearColor(0.80f, 0.92f, 0.95f), TextX, TextY(), Font, LgFS);
			RowY += RowH;

			DrawRect(FLinearColor(1.0f, 0.78f, 0.28f), IconX - 7.0f, IconY() - 7.0f, 14.0f, 14.0f);
			DrawText(TEXT("投送端 · F 补弹 / B 开店"), FLinearColor(1.0f, 0.86f, 0.55f), TextX, TextY(), Font, LgFS);
			RowY += RowH;

			Canvas->K2_DrawPolygon(nullptr, FVector2D(IconX, IconY()), FVector2D(8.0f, 8.0f), 6, FLinearColor(0.85f, 0.98f, 1.0f));
			DrawText(TEXT("谐振灯 · 压制失谐"), FLinearColor(0.72f, 0.92f, 1.0f), TextX, TextY(), Font, LgFS);
			RowY += RowH;

			Canvas->K2_DrawPolygon(nullptr, FVector2D(IconX, IconY()), FVector2D(5.0f, 5.0f), 24, FLinearColor(1.0f, 0.78f, 0.1f));
			DrawText(TEXT("遗构 · 带回核心录入"), FLinearColor(1.0f, 0.90f, 0.60f), TextX, TextY(), Font, LgFS);
			RowY += RowH;

			DrawRect(FLinearColor(0.22f, 0.82f, 0.22f), IconX - 5.0f, IconY() - 5.0f, 10.0f, 10.0f);
			DrawText(TEXT("武器 · 可拾取"), FLinearColor(0.70f, 0.95f, 0.70f), TextX, TextY(), Font, LgFS);
		}
	}

	// ===== 小地图下方：核心血量 竖直粗圆筒表（放大地图时固定在小图位置下方） =====
	if (CoreHPNow >= 0.0f && CoreHPMax > 0.0f)
	{
		const float TubeX = 44.0f, TubeY = 24.0f + 340.0f + 26.0f, TubeW = 46.0f, TubeH = 220.0f;
		const float Pct = FMath::Clamp(CoreHPNow / CoreHPMax, 0.0f, 1.0f);
		// 外壳（圆筒感：左右高光条 + 深底）
		DrawRect(FLinearColor(0.04f, 0.05f, 0.08f, 0.92f), TubeX - 5.0f, TubeY - 5.0f, TubeW + 10.0f, TubeH + 10.0f);
		DrawRect(FLinearColor(0.10f, 0.12f, 0.16f), TubeX, TubeY, TubeW, TubeH);
		// 填充：自底向上，血量低变红
		const FLinearColor FillC = Pct > 0.5f ? FLinearColor(0.16f, 0.78f, 0.85f)
			: (Pct > 0.25f ? FLinearColor(0.95f, 0.72f, 0.2f) : FLinearColor(0.92f, 0.25f, 0.2f));
		const float FillH = TubeH * Pct;
		DrawRect(FillC, TubeX, TubeY + TubeH - FillH, TubeW, FillH);
		// 圆筒高光/暗边
		DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), TubeX + 3.0f, TubeY, 7.0f, TubeH);
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.25f), TubeX + TubeW - 9.0f, TubeY, 9.0f, TubeH);
		// 刻度线
		for (int32 i = 1; i < 4; i++)
		{
			DrawLine(TubeX, TubeY + TubeH * i / 4.0f, TubeX + TubeW, TubeY + TubeH * i / 4.0f, FLinearColor(0.0f, 0.0f, 0.0f, 0.35f), 1.5f);
		}
		// 右侧数值
		DrawText(TEXT("核心"), FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), TubeX + TubeW + 14.0f + 2.0f, TubeY + 2.0f, Font, FS * 0.8f);
		DrawText(TEXT("核心"), FLinearColor(0.5f, 0.9f, 0.95f), TubeX + TubeW + 14.0f, TubeY, Font, FS * 0.8f);
		const FString CoreNum = FString::Printf(TEXT("%.0f / %.0f"), CoreHPNow, CoreHPMax);
		DrawText(CoreNum, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), TubeX + TubeW + 14.0f + 2.0f, TubeY + LH * 0.8f + 2.0f, Font, FS * 0.85f);
		DrawText(CoreNum, FillC, TubeX + TubeW + 14.0f, TubeY + LH * 0.8f, Font, FS * 0.85f);
	}

	// ===== 左下：玩家血条（含百分比）+ 弹药文本 =====
	AShooterCharacter* MyChar = PC ? Cast<AShooterCharacter>(PC->GetPawn()) : nullptr;
	if (MyChar)
	{
		const float BarW = 280.0f, BarH = 26.0f;
		const float BarX = 30.0f, BarY = Canvas->SizeY - 128.0f;
		const float HPct = FMath::Clamp(MyChar->GetHealthPercent(), 0.0f, 1.0f);
		DrawRect(FLinearColor(0.04f, 0.05f, 0.08f, 0.9f), BarX - 3.0f, BarY - 3.0f, BarW + 6.0f, BarH + 6.0f);
		DrawRect(FLinearColor(0.12f, 0.14f, 0.18f), BarX, BarY, BarW, BarH);
		const FLinearColor HPColor = HPct > 0.5f ? FLinearColor(0.35f, 0.85f, 0.35f)
			: (HPct > 0.25f ? FLinearColor(0.95f, 0.72f, 0.2f) : FLinearColor(0.92f, 0.25f, 0.2f));
		DrawRect(HPColor, BarX, BarY, BarW * HPct, BarH);
		const FString HPTxt = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(HPct * 100.0f));
		float HPTw = 0.0f, HPTh = 0.0f;
		GetTextSize(HPTxt, HPTw, HPTh, Font, FS * 0.7f);
		const float HPTx = BarX + (BarW - HPTw) * 0.5f;
		const float HPTy = BarY + (BarH - HPTh) * 0.5f;
		DrawText(HPTxt, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), HPTx + 1.5f, HPTy + 1.5f, Font, FS * 0.7f);
		DrawText(HPTxt, FLinearColor::White, HPTx, HPTy, Font, FS * 0.7f);

		// 弹药 / 近战充能：按当前武器类型显示；近战额外给机制提示行
		int32 Ammo = 0, Mag = 0;
		MyChar->GetAmmoCounts(Ammo, Mag);
		FString AmmoTxt = FString::Printf(TEXT("弹药：%d / %d"), Ammo, Mag);
		FString HintTxt;
		if (AShooterWeapon* CurW = MyChar->GetCurrentWeapon())
		{
			const FString Cn = CurW->GetClass()->GetName();
			// 备弹显示：备弹>0 显示「弹匣 | 备弹」；混沌比特(不可补给)标注「不可补给」
			if (CurW->GetMaxReserveAmmo() > 0)
			{
				AmmoTxt = FString::Printf(TEXT("弹药：%d / %d    备弹 %d"), Ammo, Mag, CurW->GetReserveAmmo());
			}
			if (CurW->IsOutOfAmmo())
			{
				AmmoTxt = FString::Printf(TEXT("弹药：0 / %d    弹尽"), Mag);
			}
			if (Cn.Contains(TEXT("Dice")))
			{
				HintTxt = TEXT("左键抛出 · 落地/撞击即爆");
			}
		}
		const float AmmoY = BarY + BarH + 12.0f;
		DrawText(AmmoTxt, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), BarX + 2.5f, AmmoY + 2.5f, Font, FS);
		DrawText(AmmoTxt, FLinearColor(1.0f, 0.85f, 0.4f), BarX, AmmoY, Font, FS);
		if (!HintTxt.IsEmpty())
		{
			// 提示放在弹药文字右侧同一行，避免与底部「[R] 暂停 / 指南」重叠
			float AmmoW = 0.0f, AmmoH = 0.0f;
			GetTextSize(AmmoTxt, AmmoW, AmmoH, Font, FS);
			const float HintX = BarX + AmmoW + 22.0f;
			const float HintY = AmmoY + (FS - FS * 0.58f) * 0.5f;
			DrawText(HintTxt, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), HintX + 1.5f, HintY + 1.5f, Font, FS * 0.58f);
			DrawText(HintTxt, FLinearColor(0.65f, 0.9f, 1.0f), HintX, HintY, Font, FS * 0.58f);
		}

		// 骰子蓄力条：长按左键时准星下方显示金色进度(越满扔得越远)
		if (ADiceWeapon* DiceW = Cast<ADiceWeapon>(MyChar->GetCurrentWeapon()))
		{
			const float Ca = DiceW->GetChargeAlpha();
			if (Ca >= 0.0f)
			{
				// 加长、下移到中央文字/拾取提示之下，避免遮挡；不带文字
				const float CBW = 360.0f, CBH = 11.0f;
				const float CBX = (ScreenW - CBW) * 0.5f, CBY = Canvas->SizeY * 0.72f;
				DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), CBX - 2.0f, CBY - 2.0f, CBW + 4.0f, CBH + 4.0f);
				DrawRect(FLinearColor(0.10f, 0.10f, 0.12f, 0.85f), CBX, CBY, CBW, CBH);
				const FLinearColor Fill = FMath::Lerp(FLinearColor(1.0f, 0.7f, 0.15f), FLinearColor(1.0f, 0.95f, 0.4f), Ca);
				DrawRect(Fill, CBX, CBY, CBW * Ca, CBH);
			}
		}

		// 体力条：屏幕正下方，疾跑中或未回满时显示
		const float SPct = FMath::Clamp(MyChar->GetSprintPercent(), 0.0f, 1.0f);
		if (MyChar->IsSprinting() || SPct < 0.995f)
		{
			const float StW = 360.0f, StH = 14.0f;
			const float StX = (ScreenW - StW) * 0.5f;
			const float StY = Canvas->SizeY - 64.0f;
			DrawRect(FLinearColor(0.04f, 0.05f, 0.08f, 0.85f), StX - 3.0f, StY - 3.0f, StW + 6.0f, StH + 6.0f);
			DrawRect(FLinearColor(0.12f, 0.14f, 0.18f), StX, StY, StW, StH);
			const FLinearColor StC = SPct > 0.3f ? FLinearColor(0.45f, 0.8f, 1.0f) : FLinearColor(0.95f, 0.55f, 0.2f);
			DrawRect(StC, StX, StY, StW * SPct, StH);
			DrawText(TEXT("体力"), FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), StX - 62.0f + 1.5f, StY - 8.0f + 1.5f, Font, FS * 0.6f);
			DrawText(TEXT("体力"), FLinearColor(0.8f, 0.92f, 1.0f), StX - 62.0f, StY - 8.0f, Font, FS * 0.6f);
		}

		// ===== 近距可拾取提示：按 F 拾取 =====
		{
			const ALootPickup* Near = nullptr;
			float NearD = 450.0f;   // 与 ShooterCharacter 拾取半径一致
			const FVector MyLoc = MyChar->GetActorLocation();
			for (TActorIterator<ALootPickup> LIt(World); LIt; ++LIt)
			{
				if (LIt->bTaken) { continue; }
				const float D = FVector::Dist2D(LIt->GetActorLocation(), MyLoc);
				if (D < NearD) { NearD = D; Near = *LIt; }
			}
			if (Near)
			{
				// 武器点按类别显名：特殊箱(骰子) / 主武器箱(其余)
				FString PickName = GetLootDef(Near->Kind).Name;
				if (Near->Kind == ELootKind::WeaponMod && Near->WeaponClassOverride)
				{
					const FString Cn = Near->WeaponClassOverride->GetName();
					PickName = Cn.Contains(TEXT("Dice")) ? TEXT("特殊武器箱") : TEXT("主武器箱");
				}
				const FString PickTxt = FString::Printf(TEXT("[F] 拾取：%s"), *PickName);
				float PW = 0.0f, PH = 0.0f;
				GetTextSize(PickTxt, PW, PH, Font, FS * 0.8f);
				DrawText(PickTxt, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), (ScreenW - PW) * 0.5f + 2.0f, Canvas->SizeY * 0.56f + 2.0f, Font, FS * 0.8f);
				DrawText(PickTxt, FLinearColor(0.6f, 1.0f, 0.7f), (ScreenW - PW) * 0.5f, Canvas->SizeY * 0.56f, Font, FS * 0.8f);
			}
		}

		// 拾取提示的基准高度；投送端面板出现时会把它往下推，两者不叠字
		float ToastY = Canvas->SizeY * 0.62f;

		// ===== 站在投送端旁：交互面板（商店没开时才提示，免得和店面叠一起）=====
		if (!MyChar->bShopOpen && !bShowGuide && !bShowBackpack)
		{
			bool bNearDepot = false;
			const FVector MyLoc = MyChar->GetActorLocation();
			for (TActorIterator<AActor> It(World); It; ++It)
			{
				if (!It->ActorHasTag(FName("BoBSupplyStation"))) { continue; }
				if (FVector::Dist2D(It->GetActorLocation(), MyLoc) <= 520.0f) { bNearDepot = true; break; }
			}
			if (bNearDepot)
			{
				bool bCanShop = false;
				for (TActorIterator<AWaveManager> WIt(World); WIt; ++WIt)
				{
					bCanShop = WIt->GetIntervalRemaining() >= 0.0f;
					break;
				}
				const FString L1 = TEXT("投 送 端");
				// 按 F 到底能补几轮、要花多少配额，写在提示里，不让玩家盲按
				FString L2 = TEXT("[ F ]  该武器无法补给");
				if (const AShooterWeapon* CW = MyChar->GetCurrentWeapon())
				{
					const int32 DepotMag = FMath::Max(1, CW->GetMagazineSize());
					const int32 Missing = CW->GetMaxReserveAmmo() - CW->GetReserveAmmo();
					if (CW->GetMaxReserveAmmo() <= 0)
					{
						L2 = TEXT("[ F ]  该武器无法补给");
					}
					else if (Missing <= 0)
					{
						L2 = FString::Printf(TEXT("[ F ]  备弹已满（%d 轮）"), CW->GetReserveMags());
					}
					else
					{
						const int32 Cost = FMath::Max(1, FMath::CeilToInt(Missing / 10.0f));
						L2 = FString::Printf(TEXT("[ F ]  补满备弹  +%.1f 轮 / %d 配额"),
							static_cast<float>(Missing) / static_cast<float>(DepotMag), Cost);
					}
				}
				const FString L3 = bCanShop ? TEXT("[ B ]  调取投送清单") : TEXT("投送端只在间隙开放");
				float W1 = 0.0f, H1 = 0.0f, W2 = 0.0f, H2 = 0.0f, W3 = 0.0f, H3 = 0.0f;
				GetTextSize(L1, W1, H1, Font, FS * 0.68f);
				GetTextSize(L2, W2, H2, Font, FS * 0.56f);
				GetTextSize(L3, W3, H3, Font, FS * 0.56f);
				const float BoxW = FMath::Max3(W1, W2, W3) + 44.0f;
				const float BoxH = H1 + H2 + H3 + 34.0f;
				const float BoxX = (ScreenW - BoxW) * 0.5f, BoxY = Canvas->SizeY * 0.575f;
				ToastY = BoxY + BoxH + 16.0f;   // 把拾取/失败提示挤到面板下方
				DrawRect(FLinearColor(0.03f, 0.03f, 0.02f, 0.82f), BoxX, BoxY, BoxW, BoxH);
				const FLinearColor DGold(0.72f, 0.58f, 0.24f, 0.9f);
				DrawRect(DGold, BoxX, BoxY, BoxW, 1.0f);
				DrawRect(DGold, BoxX, BoxY + BoxH - 1.0f, BoxW, 1.0f);
				DrawRect(DGold, BoxX, BoxY, 1.0f, BoxH);
				DrawRect(DGold, BoxX + BoxW - 1.0f, BoxY, 1.0f, BoxH);
				float DepotY = BoxY + 8.0f;
				DrawText(L1, FLinearColor(0.96f, 0.88f, 0.60f), BoxX + (BoxW - W1) * 0.5f, DepotY, Font, FS * 0.68f);
				DepotY += H1 + 8.0f;
				DrawText(L2, FLinearColor(0.88f, 0.90f, 0.84f), BoxX + (BoxW - W2) * 0.5f, DepotY, Font, FS * 0.56f);
				DepotY += H2 + 4.0f;
				DrawText(L3, bCanShop ? FLinearColor(1.0f, 0.80f, 0.34f) : FLinearColor(0.60f, 0.52f, 0.46f),
					BoxX + (BoxW - W3) * 0.5f, DepotY, Font, FS * 0.56f);
			}
		}

		// ===== 拾取提示（屏幕中下，2 秒）=====
		if (World->GetTimeSeconds() < MyChar->LootToastUntil && !MyChar->LootToastMsg.IsEmpty())
		{
			float TW = 0.0f, TH = 0.0f;
			GetTextSize(MyChar->LootToastMsg, TW, TH, Font, FS * 0.85f);
			DrawText(MyChar->LootToastMsg, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), (ScreenW - TW) * 0.5f + 2.0f, ToastY + 2.0f, Font, FS * 0.85f);
			DrawText(MyChar->LootToastMsg, FLinearColor(1.0f, 0.9f, 0.5f), (ScreenW - TW) * 0.5f, ToastY, Font, FS * 0.85f);
		}

		// ===== 道具栏：常驻右下、压在武器栏上方；不显示则玩家不知道买的东西还在身上 =====
		{
			const TArray<uint8>& CarryItems = MyChar->GetOwnedItems();
			if (CarryItems.Num() > 0)
			{
				const int32 Slot = FMath::Clamp(MyChar->ItemSlot, 0, CarryItems.Num() - 1);
				const FBoBShopEntry& CE = GetShopEntry(static_cast<EBoBShopItem>(CarryItems[Slot]));
				const float IW = 200.0f, IH = 46.0f;
				const float IX = ScreenW - IW - 24.0f;
				const float IY = Canvas->SizeY - 40.0f - 36.0f * MyChar->GetOwnedWeapons().Num() - IH - 8.0f;
				DrawRect(FLinearColor(0.055f, 0.052f, 0.035f, 0.88f), IX, IY, IW, IH);
				const FLinearColor IGold(0.72f, 0.58f, 0.24f, 0.9f);
				DrawRect(IGold, IX, IY, IW, 1.0f);
				DrawRect(IGold, IX, IY + IH - 1.0f, IW, 1.0f);
				DrawRect(IGold, IX, IY, 1.0f, IH);
				DrawRect(IGold, IX + IW - 1.0f, IY, 1.0f, IH);
				const float TS = IH - 10.0f;
				if (UTexture2D* CIcon = GetItemIcon(CE.IconPath))
				{
					DrawTexture(CIcon, IX + 5.0f, IY + 5.0f, TS, TS, 0.0f, 0.0f, 1.0f, 1.0f);
				}
				DrawText(CE.Name, FLinearColor(0.95f, 0.90f, 0.72f), IX + TS + 12.0f, IY + 5.0f, Font, FS * 0.56f);
				const FString UseHint = CarryItems.Num() > 1
					? FString::Printf(TEXT("[Q] 使用   共 %d 件"), CarryItems.Num())
					: TEXT("[Q] 使用");
				DrawText(UseHint, FLinearColor(0.78f, 0.70f, 0.45f), IX + TS + 12.0f, IY + IH - 20.0f, Font, FS * 0.46f);
			}
		}

		// ===== CS 式武器栏：常驻右下，数字键 1/2/3 直切 =====
		const TArray<AShooterWeapon*>& Weps = MyChar->GetOwnedWeapons();
		if (Weps.Num() > 0)
		{
			const float WW = 200.0f, WH = 36.0f;
			const float WX = ScreenW - WW - 24.0f;
			float WY = Canvas->SizeY - 40.0f - WH * Weps.Num();
			for (int32 i = 0; i < Weps.Num(); i++)
			{
				if (!Weps[i]) { continue; }
				const bool bCur = (Weps[i] == MyChar->GetCurrentWeapon());
				const FString Disp = BoBWeaponDisplayName(Weps[i]->GetClass()->GetName());
				DrawRect(bCur ? FLinearColor(0.14f, 0.34f, 0.48f, 0.88f) : FLinearColor(0.05f, 0.07f, 0.10f, 0.72f), WX, WY, WW, WH - 5.0f);
				const FString Line = FString::Printf(TEXT("%d  %s%s"), i + 1, *Disp, bCur ? TEXT("  ◄") : TEXT(""));
				DrawText(Line, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), WX + 10.0f + 1.5f, WY + 2.0f + 1.5f, Font, FS * 0.62f);
				DrawText(Line, bCur ? FLinearColor(0.7f, 0.95f, 1.0f) : FLinearColor(0.75f, 0.75f, 0.75f), WX + 10.0f, WY + 2.0f, Font, FS * 0.62f);
				WY += WH;
			}
		}

		// ===== 背包最小化指示（HP 条上方一行小字+迷你格点，B 打开弹窗）=====
		{
			TArray<FLinearColor> SlotCols;
			for (uint8 K : MyChar->GetCarried())
			{
				const FLootDef& D = GetLootDef(static_cast<ELootKind>(K));
				const FLinearColor C = D.Slots >= 3 ? FLinearColor(1.0f, 0.78f, 0.2f)
					: (D.Slots == 2 ? FLinearColor(0.75f, 0.42f, 1.0f) : FLinearColor(0.5f, 0.75f, 0.95f));
				for (int32 s = 0; s < D.Slots; s++) { SlotCols.Add(C); }
			}
			const float SS = 13.0f, SX0 = 30.0f, SY0 = Canvas->SizeY - 156.0f;
			for (int32 i = 0; i < AShooterCharacter::BackpackSlots; i++)
			{
				DrawRect(i < SlotCols.Num() ? SlotCols[i] : FLinearColor(0.14f, 0.16f, 0.2f, 0.85f), SX0 + i * (SS + 4.0f), SY0, SS, SS);
			}
			const FString BankTxt = FString::Printf(TEXT("已存 %d   [N] 背包"), MyChar->GetBankedValue());
			DrawText(BankTxt, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), SX0 + 5 * (SS + 4.0f) + 12.0f + 1.5f, SY0 - 5.0f + 1.5f, Font, FS * 0.55f);
			DrawText(BankTxt, FLinearColor(0.55f, 0.95f, 1.0f), SX0 + 5 * (SS + 4.0f) + 12.0f, SY0 - 5.0f, Font, FS * 0.55f);

			// 配额（花销货币）：背包行上方，琥珀色
			if (const ABODPlayerState* BPS = PC ? PC->GetPlayerState<ABODPlayerState>() : nullptr)
			{
				const FString CinderTxt = FString::Printf(TEXT("配额  %d"), BPS->GetCinder());
				// 与下方背包格居中对齐、再上移一点，避免压住"已存/[B]背包"那行
				float CW = 0.0f, CH = 0.0f;
				GetTextSize(CinderTxt, CW, CH, Font, FS * 0.62f);
				const float RowW = AShooterCharacter::BackpackSlots * (SS + 4.0f) - 4.0f;
				const float CX = SX0 + (RowW - CW) * 0.5f;
				const float CY = SY0 - 38.0f;
				DrawText(CinderTxt, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), CX + 1.5f, CY + 1.5f, Font, FS * 0.62f);
				DrawText(CinderTxt, FLinearColor(1.0f, 0.78f, 0.28f), CX, CY, Font, FS * 0.62f);
			}
		}

		// ===== B 键投送端：货架页(左常备 2x2 / 右本波刷新 2x2) → 详情页(Y 买 / U 再看看) =====
		// 配色走深空蓝：图标全是青色边光，压在黄褐底上会被补色抵消成灰白，看着像抠图失败
		if (MyChar->bShopOpen && !bShowGuide && !bShowBackpack)
		{
			const ABODPlayerState* SPS = PC ? PC->GetPlayerState<ABODPlayerState>() : nullptr;
			const int32 MyCinder = SPS ? SPS->GetCinder() : 0;
			const TArray<uint8> Items = MyChar->GetShopVisibleItems();
			const int32 N = Items.Num();

			// 面板：屏幕的三分之二强，宽高严格 2:1；再按屏高兜一次底，防超高屏溢出
			const float PW = FMath::Min(ScreenW * 0.74f, Canvas->SizeY * 1.68f);
			const float PH = PW * 0.5f;
			const float PX = (ScreenW - PW) * 0.5f, PY = (Canvas->SizeY - PH) * 0.5f;

			const FLinearColor Cyan(0.35f, 0.78f, 0.95f, 0.95f);
			const FLinearColor CyanDim(0.16f, 0.38f, 0.52f, 0.85f);
			const FLinearColor Paper(0.88f, 0.95f, 1.0f);
			const FLinearColor Amber(1.0f, 0.78f, 0.28f);
			const FLinearColor Muted(0.46f, 0.58f, 0.68f);

			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.68f), 0.0f, 0.0f, ScreenW, Canvas->SizeY);
			DrawRect(FLinearColor(0.010f, 0.018f, 0.030f, 0.97f), PX, PY, PW, PH);
			DrawRect(Cyan, PX, PY, PW, 2.0f);
			DrawRect(Cyan, PX, PY + PH - 2.0f, PW, 2.0f);
			DrawRect(Cyan, PX, PY, 2.0f, PH);
			DrawRect(Cyan, PX + PW - 2.0f, PY, 2.0f, PH);

			// 三段：标题栏 / 主体 / 按键栏
			const float HeadH = PH * 0.10f;
			const float FootH = PH * 0.085f;
			const float BodyY = PY + HeadH;
			const float BodyH = PH - HeadH - FootH;
			const float SideMargin = PW * 0.03f;

			// ---- 标题栏 ----
			{
				DrawText(TEXT("投 送 端"), Paper, PX + SideMargin, PY + HeadH * 0.24f, Font, FS * 0.92f);
				const FString CinTxt = FString::Printf(TEXT("配额  %d"), MyCinder);
				float CW0 = 0.0f, CH0 = 0.0f;
				GetTextSize(CinTxt, CW0, CH0, Font, FS * 0.8f);
				DrawText(CinTxt, Amber, PX + PW - CW0 - SideMargin, PY + HeadH * 0.30f, Font, FS * 0.8f);
				DrawRect(CyanDim, PX + SideMargin, BodyY - 1.0f, PW - SideMargin * 2.0f, 1.0f);
			}

			float MX = -1.0f, MY = -1.0f;
			if (PC) { PC->GetMousePosition(MX, MY); }
			int32 HoverRow = -1, HoverBtn = -1;

			// 中文按等宽近似断行：整串测一次取平均字宽，比逐字测量快得多且对全角很准
			auto WrapLines = [&](const FString& S, float MaxW, float Sc) -> TArray<FString>
			{
				TArray<FString> Out;
				if (S.IsEmpty()) { return Out; }
				// 先吃掉文案里写死的换行（条款/守则那几条靠它分行），再按宽度折
				TArray<FString> Paras;
				S.ParseIntoArray(Paras, TEXT("\n"), false);
				for (const FString& Para : Paras)
				{
					if (Para.IsEmpty()) { Out.Add(FString()); continue; }
					float TW = 0.0f, TH = 0.0f;
					GetTextSize(Para, TW, TH, Font, FS * Sc);
					const float AvgW = FMath::Max(1.0f, TW / FMath::Max(1, Para.Len()));
					const int32 PerLine = FMath::Max(4, FMath::FloorToInt(MaxW * 0.98f / AvgW));
					for (int32 Pos = 0; Pos < Para.Len(); Pos += PerLine)
					{
						Out.Add(Para.Mid(Pos, PerLine));
					}
				}
				return Out;
			};

			if (MyChar->ShopSelected < 0 || !Items.IsValidIndex(MyChar->ShopSelected))
			{
				// ================= 货架页 =================
				const float LabelH = BodyH * 0.09f;
				const float GridH = BodyH - LabelH - BodyH * 0.04f;
				const float CellGap = PW * 0.011f;
				const float CellS = (GridH - CellGap) * 0.5f;          // 正方形格
				const float SideW = CellS * 2.0f + CellGap;
				const float HalfW = PW * 0.5f;
				const float LeftX = PX + (HalfW - SideW) * 0.5f;
				const float RightX = PX + HalfW + (HalfW - SideW) * 0.5f;
				const float GridY = BodyY + LabelH;

				DrawRect(CyanDim, PX + HalfW - 1.0f, BodyY + BodyH * 0.04f, 1.0f, BodyH * 0.92f);

				DrawText(TEXT("常 备 货 架"), Muted, LeftX + 2.0f, BodyY + LabelH * 0.18f, Font, FS * 0.55f);
				DrawText(TEXT("本 轮 投 送"), Muted, RightX + 2.0f, BodyY + LabelH * 0.18f, Font, FS * 0.55f);

				for (int32 i = 0; i < N; ++i)
				{
					const bool bRight = (i >= BoBShopFixedCount);
					const int32 k = bRight ? (i - BoBShopFixedCount) : i;
					if (k >= 4) { continue; }
					const float CX0 = (bRight ? RightX : LeftX) + (k % 2) * (CellS + CellGap);
					const float CY0 = GridY + (k / 2) * (CellS + CellGap);

					const int32 ItemIdx = Items[i];
					const FBoBShopEntry& E = GetShopEntry(static_cast<EBoBShopItem>(ItemIdx));
					const int32 Bought = MyChar->GetShopBought(ItemIdx);
					const bool bSoldOut = (E.StockPerWave > 0 && Bought >= E.StockPerWave);
					const bool bAfford = (MyCinder >= E.Cost) && !bSoldOut;

					const bool bHover = (MX >= CX0 && MX <= CX0 + CellS && MY >= CY0 && MY <= CY0 + CellS);
					if (bHover) { HoverRow = i; }

					DrawRect(bHover ? FLinearColor(0.050f, 0.110f, 0.170f, 0.95f) : FLinearColor(0.020f, 0.038f, 0.058f, 0.92f),
						CX0, CY0, CellS, CellS);
					const FLinearColor Edge = bHover ? Cyan : CyanDim;
					DrawRect(Edge, CX0, CY0, CellS, 1.0f);
					DrawRect(Edge, CX0, CY0 + CellS - 1.0f, CellS, 1.0f);
					DrawRect(Edge, CX0, CY0, 1.0f, CellS);
					DrawRect(Edge, CX0 + CellS - 1.0f, CY0, 1.0f, CellS);

					// 左上角键号章
					const float KS = CellS * 0.13f;
					DrawRect(FLinearColor(0.030f, 0.060f, 0.090f, 0.95f), CX0 + 1.0f, CY0 + 1.0f, KS, KS);
					DrawRect(CyanDim, CX0 + 1.0f, CY0 + KS + 1.0f, KS, 1.0f);
					DrawRect(CyanDim, CX0 + KS + 1.0f, CY0 + 1.0f, 1.0f, KS);
					{
						const FString KeyTxt = FString::Printf(TEXT("%d"), i + 1);
						float KW = 0.0f, KH = 0.0f;
						GetTextSize(KeyTxt, KW, KH, Font, FS * 0.52f);
						DrawText(KeyTxt, bAfford ? Cyan : FLinearColor(0.35f, 0.42f, 0.48f),
							CX0 + 1.0f + (KS - KW) * 0.5f, CY0 + 1.0f + (KS - KH) * 0.5f, Font, FS * 0.52f);
					}

					// 意象图：占格子上部，正方形
					const float IS = CellS * 0.60f;
					const float IX = CX0 + (CellS - IS) * 0.5f;
					const float IY = CY0 + CellS * 0.05f;
					if (UTexture2D* IconTex = GetItemIcon(E.IconPath))
					{
						DrawTexture(IconTex, IX, IY, IS, IS, 0.0f, 0.0f, 1.0f, 1.0f,
							bSoldOut ? FLinearColor(0.42f, 0.48f, 0.52f, 0.65f) : FLinearColor::White);
					}
					else
					{
						const float BS = IS * 0.56f, BX = CX0 + (CellS - BS) * 0.5f, BY = IY + (IS - BS) * 0.5f;
						DrawRect(FLinearColor(0.030f, 0.055f, 0.080f, 0.9f), BX, BY, BS, BS);
						DrawRect(CyanDim, BX, BY, BS, 1.0f);
						DrawRect(CyanDim, BX, BY + BS - 1.0f, BS, 1.0f);
						DrawRect(CyanDim, BX, BY, 1.0f, BS);
						DrawRect(CyanDim, BX + BS - 1.0f, BY, 1.0f, BS);
					}

					// 名称（居中，超宽自动缩一档）
					{
						float NW = 0.0f, NH = 0.0f;
						float NSc = 0.62f;
						GetTextSize(E.Name, NW, NH, Font, FS * NSc);
						if (NW > CellS - 16.0f)
						{
							NSc *= (CellS - 16.0f) / FMath::Max(1.0f, NW);
							GetTextSize(E.Name, NW, NH, Font, FS * NSc);
						}
						DrawText(E.Name, bSoldOut ? FLinearColor(0.42f, 0.48f, 0.52f) : Paper,
							CX0 + (CellS - NW) * 0.5f, CY0 + CellS * 0.665f, Font, FS * NSc);
					}

					// 价格 / 库存
					{
						const FString PriceTxt = bSoldOut ? TEXT("已无存量")
							: (E.Cost > 0 ? FString::Printf(TEXT("%d 配额"), E.Cost) : TEXT("无需付费"));
						float PWd = 0.0f, PHt = 0.0f;
						GetTextSize(PriceTxt, PWd, PHt, Font, FS * 0.56f);
						DrawText(PriceTxt,
							bSoldOut ? FLinearColor(0.62f, 0.42f, 0.36f) : (bAfford ? Amber : FLinearColor(0.72f, 0.42f, 0.36f)),
							CX0 + (CellS - PWd) * 0.5f, CY0 + CellS * 0.79f, Font, FS * 0.56f);

						const FString StockTxt = (E.StockPerWave > 0)
							? FString::Printf(TEXT("限领 %d／已领 %d"), E.StockPerWave, Bought)
							: TEXT("不限次");
						float SWd = 0.0f, SHt = 0.0f;
						GetTextSize(StockTxt, SWd, SHt, Font, FS * 0.44f);
						DrawText(StockTxt, (E.StockPerWave > 0) ? Muted : FLinearColor(0.42f, 0.62f, 0.56f),
							CX0 + (CellS - SWd) * 0.5f, CY0 + CellS * 0.895f, Font, FS * 0.44f);
					}
				}
			}
			else
			{
				// ================= 详情页 =================
				const int32 SelIdx = Items[MyChar->ShopSelected];
				const FBoBShopEntry& SE = GetShopEntry(static_cast<EBoBShopItem>(SelIdx));
				const int32 Bought = MyChar->GetShopBought(SelIdx);
				const bool bSoldOut = (SE.StockPerWave > 0 && Bought >= SE.StockPerWave);
				const bool bAfford = (MyCinder >= SE.Cost) && !bSoldOut;

				const float BtnRowH = BodyH * 0.17f;
				const float ContY = BodyY + BodyH * 0.045f;
				const float ContH = BodyH - BtnRowH - BodyH * 0.045f;

				// ---- 左：意象大图，正方形吃满内容区高度 ----
				const float ArtS = ContH;
				const float AX = PX + SideMargin;
				const float AY = ContY;
				DrawRect(FLinearColor(0.020f, 0.038f, 0.058f, 0.9f), AX, AY, ArtS, ArtS);
				{
					const float CkL = ArtS * 0.09f;
					const float AR = AX + ArtS, AB = AY + ArtS;
					DrawRect(Cyan, AX, AY, CkL, 1.0f);              DrawRect(Cyan, AX, AY, 1.0f, CkL);
					DrawRect(Cyan, AR - CkL, AY, CkL, 1.0f);        DrawRect(Cyan, AR - 1.0f, AY, 1.0f, CkL);
					DrawRect(Cyan, AX, AB - 1.0f, CkL, 1.0f);       DrawRect(Cyan, AX, AB - CkL, 1.0f, CkL);
					DrawRect(Cyan, AR - CkL, AB - 1.0f, CkL, 1.0f); DrawRect(Cyan, AR - 1.0f, AB - CkL, 1.0f, CkL);
				}
				if (UTexture2D* IconTex = GetItemIcon(SE.IconPath))
				{
					const float IS = ArtS * 0.94f;
					DrawTexture(IconTex, AX + (ArtS - IS) * 0.5f, AY + (ArtS - IS) * 0.5f, IS, IS, 0.0f, 0.0f, 1.0f, 1.0f);
				}

				// ---- 右：文案。上半贴顶排，风味块贴底与大图底边齐 ----
				const float TX = AX + ArtS + PW * 0.032f;
				const float TW2 = PX + PW - SideMargin - TX;
				float TY = ContY;

				DrawText(SE.Name, Paper, TX, TY, Font, FS * 1.12f);
				{
					float NW = 0.0f, NH = 0.0f;
					GetTextSize(SE.Name, NW, NH, Font, FS * 1.12f);
					TY += NH + 10.0f;
					// 名称下一道短下划线，给标题一个落点
					DrawRect(Cyan, TX, TY - 4.0f, FMath::Min(NW, TW2), 2.0f);
					TY += 12.0f;
				}
				// 价格做成实心标签，右边跟限购
				{
					const FString CostTxt = SE.Cost > 0 ? FString::Printf(TEXT("%d 配额"), SE.Cost) : TEXT("无需付费");
					float CW1 = 0.0f, CH1 = 0.0f;
					GetTextSize(CostTxt, CW1, CH1, Font, FS * 0.68f);
					const float TagW = CW1 + 28.0f, TagH = CH1 + 12.0f;
					DrawRect(bAfford ? FLinearColor(0.16f, 0.12f, 0.02f, 0.9f) : FLinearColor(0.14f, 0.06f, 0.05f, 0.9f),
						TX, TY, TagW, TagH);
					const FLinearColor TagEdge = bAfford ? Amber : FLinearColor(0.66f, 0.34f, 0.28f);
					DrawRect(TagEdge, TX, TY, TagW, 1.0f);
					DrawRect(TagEdge, TX, TY + TagH - 1.0f, TagW, 1.0f);
					DrawRect(TagEdge, TX, TY, 1.0f, TagH);
					DrawRect(TagEdge, TX + TagW - 1.0f, TY, 1.0f, TagH);
					DrawText(CostTxt, bAfford ? Amber : FLinearColor(0.80f, 0.46f, 0.38f),
						TX + 14.0f, TY + 6.0f, Font, FS * 0.68f);

					const FString StockTxt = (SE.StockPerWave > 0)
						? FString::Printf(TEXT("限领 %d　已领 %d"), SE.StockPerWave, Bought)
						: TEXT("不限次供应");
					DrawText(StockTxt, (SE.StockPerWave > 0) ? Muted : FLinearColor(0.42f, 0.62f, 0.56f),
						TX + TagW + 20.0f, TY + TagH * 0.5f - CH1 * 0.35f, Font, FS * 0.52f);
					TY += TagH + 22.0f;
				}
				// 效果段
				{
					DrawText(TEXT("效  果"), FLinearColor(0.45f, 0.85f, 0.90f), TX, TY, Font, FS * 0.54f);
					float HW = 0.0f, HH = 0.0f;
					GetTextSize(TEXT("效  果"), HW, HH, Font, FS * 0.54f);
					TY += HH + 8.0f;
					const TArray<FString> DLines = WrapLines(SE.Desc, TW2, 0.62f);
					for (const FString& L : DLines)
					{
						float LW = 0.0f, LHt = 0.0f;
						GetTextSize(L, LW, LHt, Font, FS * 0.62f);
						DrawText(L, FLinearColor(0.92f, 0.96f, 1.0f), TX, TY, Font, FS * 0.62f);
						TY += LHt + 5.0f;
					}
				}

				// 风味块：底边与大图对齐，行数反算起点，中间的空白留给呼吸
				{
					const float FSc = 0.54f;
					const TArray<FString> FLines = WrapLines(SE.Flavor, TW2 - 18.0f, FSc);
					float ProbeW = 0.0f, ProbeH = 0.0f;
					GetTextSize(TEXT("字"), ProbeW, ProbeH, Font, FS * FSc);
					const float LineStep = ProbeH + 5.0f;
					float BlockH = 0.0f;
					for (const FString& L : FLines) { BlockH += L.IsEmpty() ? LineStep * 0.5f : LineStep; }

					float FY = ContY + ContH - BlockH;
					FY = FMath::Max(FY, TY + 18.0f);   // 效果段太长时不许压上去
					// 左侧一条竖线，把它标成"引文"而不是正文
					DrawRect(CyanDim, TX, FY - 2.0f, 2.0f, BlockH + 4.0f);
					for (const FString& L : FLines)
					{
						if (L.IsEmpty()) { FY += LineStep * 0.5f; continue; }
						DrawText(L, FLinearColor(0.52f, 0.62f, 0.70f), TX + 16.0f, FY, Font, FS * FSc);
						FY += LineStep;
					}
				}

				// ---- 底部两枚按钮：右对齐到面板右缘，和大图左缘形成对角 ----
				{
					const float BW = PW * 0.175f, BH = BtnRowH * 0.62f, BGap = PW * 0.018f;
					const float BY = BodyY + BodyH - BtnRowH + (BtnRowH - BH) * 0.35f;
					const float BX2 = PX + PW - SideMargin - BW;
					const float BX1 = BX2 - BW - BGap;

					const bool bHov1 = (MX >= BX1 && MX <= BX1 + BW && MY >= BY && MY <= BY + BH);
					const bool bHov2 = (MX >= BX2 && MX <= BX2 + BW && MY >= BY && MY <= BY + BH);
					if (bHov1) { HoverBtn = 0; }
					else if (bHov2) { HoverBtn = 1; }

					// 买下：可买时是实心青，买不起转暗红描边
					const FLinearColor BuyFill = !bAfford ? FLinearColor(0.10f, 0.05f, 0.05f, 0.9f)
						: (bHov1 ? FLinearColor(0.32f, 0.72f, 0.88f, 0.95f) : FLinearColor(0.10f, 0.30f, 0.42f, 0.92f));
					DrawRect(BuyFill, BX1, BY, BW, BH);
					const FLinearColor BuyEdge = bAfford ? Cyan : FLinearColor(0.52f, 0.28f, 0.24f, 0.85f);
					DrawRect(BuyEdge, BX1, BY, BW, 1.0f);
					DrawRect(BuyEdge, BX1, BY + BH - 1.0f, BW, 1.0f);
					DrawRect(BuyEdge, BX1, BY, 1.0f, BH);
					DrawRect(BuyEdge, BX1 + BW - 1.0f, BY, 1.0f, BH);
					{
						const FString T1 = bSoldOut ? TEXT("已 售 罄") : (bAfford ? TEXT("买  下") : TEXT("余 烬 不 足"));
						float W1 = 0.0f, H1 = 0.0f;
						GetTextSize(T1, W1, H1, Font, FS * 0.7f);
						const FLinearColor T1C = !bAfford ? FLinearColor(0.72f, 0.52f, 0.46f)
							: (bHov1 ? FLinearColor(0.02f, 0.08f, 0.12f) : Paper);
						DrawText(T1, T1C, BX1 + (BW - W1) * 0.5f, BY + BH * 0.24f - H1 * 0.5f + BH * 0.08f, Font, FS * 0.7f);
						const FString K1 = TEXT("[ Y ]");
						float KW1 = 0.0f, KH1 = 0.0f;
						GetTextSize(K1, KW1, KH1, Font, FS * 0.46f);
						const FLinearColor K1C = !bAfford ? FLinearColor(0.50f, 0.38f, 0.34f)
							: (bHov1 ? FLinearColor(0.04f, 0.14f, 0.20f) : FLinearColor(0.55f, 0.80f, 0.92f));
						DrawText(K1, K1C, BX1 + (BW - KW1) * 0.5f, BY + BH - KH1 - BH * 0.12f, Font, FS * 0.46f);
					}

					// 再看看：始终描边款，视觉权重低于"买下"
					DrawRect(bHov2 ? FLinearColor(0.10f, 0.15f, 0.19f, 0.95f) : FLinearColor(0.030f, 0.055f, 0.075f, 0.9f), BX2, BY, BW, BH);
					const FLinearColor BackEdge = bHov2 ? FLinearColor(0.62f, 0.74f, 0.82f, 0.9f) : FLinearColor(0.28f, 0.40f, 0.48f, 0.85f);
					DrawRect(BackEdge, BX2, BY, BW, 1.0f);
					DrawRect(BackEdge, BX2, BY + BH - 1.0f, BW, 1.0f);
					DrawRect(BackEdge, BX2, BY, 1.0f, BH);
					DrawRect(BackEdge, BX2 + BW - 1.0f, BY, 1.0f, BH);
					{
						const FString T2 = TEXT("再 看 看");
						float W2 = 0.0f, H2 = 0.0f;
						GetTextSize(T2, W2, H2, Font, FS * 0.7f);
						DrawText(T2, FLinearColor(0.82f, 0.88f, 0.92f), BX2 + (BW - W2) * 0.5f, BY + BH * 0.24f - H2 * 0.5f + BH * 0.08f, Font, FS * 0.7f);
						const FString K2 = TEXT("[ U ]");
						float KW2 = 0.0f, KH2 = 0.0f;
						GetTextSize(K2, KW2, KH2, Font, FS * 0.46f);
						DrawText(K2, FLinearColor(0.52f, 0.62f, 0.70f), BX2 + (BW - KW2) * 0.5f, BY + BH - KH2 - BH * 0.12f, Font, FS * 0.46f);
					}
				}
			}

			MyChar->ShopHoverRow = HoverRow;
			MyChar->ShopHoverBtn = HoverBtn;

			// ---- 底部按键栏 ----
			{
				const float HintY = PY + PH - FootH + FootH * 0.28f;
				DrawRect(CyanDim, PX + SideMargin, PY + PH - FootH, PW - SideMargin * 2.0f, 1.0f);
				const FString Keys = (MyChar->ShopSelected >= 0)
					? TEXT("[ Y ] 支取        [ U ] 再看看        [ B ] 离开投送端")
					: TEXT("[ 1 - 8 ] 查看详情        [ B ] 离开投送端");
				float KW = 0.0f, KH = 0.0f;
				GetTextSize(Keys, KW, KH, Font, FS * 0.54f);
				DrawText(Keys, FLinearColor(0.60f, 0.74f, 0.84f), PX + (PW - KW) * 0.5f, HintY, Font, FS * 0.54f);
			}
		}

		// ===== B 键背包弹窗：左=武器分类槽（主×2/副×1/爆炸物×1），右=宝物格 =====
		if (bShowBackpack && !bShowGuide)
		{
			const float BPW = FMath::Min(ScreenW * 0.78f, 1180.0f), BPH = FMath::Min(Canvas->SizeY * 0.82f, 880.0f);
			const float BX0 = (ScreenW - BPW) * 0.5f, BY0 = (Canvas->SizeY - BPH) * 0.5f;
			DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.62f), 0.0f, 0.0f, ScreenW, Canvas->SizeY);
			DrawRect(FLinearColor(0.030f, 0.030f, 0.024f, 0.97f), BX0, BY0, BPW, BPH);
			// 与投送端同一套黑金，两个面板不再像两个游戏
			const FLinearColor Frame(0.76f, 0.62f, 0.28f, 0.95f);
			const FLinearColor FrameDim(0.42f, 0.34f, 0.16f, 0.8f);
			DrawRect(Frame, BX0, BY0, BPW, 2.0f);
			DrawRect(Frame, BX0, BY0 + BPH - 2.0f, BPW, 2.0f);
			DrawRect(Frame, BX0, BY0, 2.0f, BPH);
			DrawRect(Frame, BX0 + BPW - 2.0f, BY0, 2.0f, BPH);
			// 标题栏：左标题 / 右配额，下方一条通栏细线
			{
				DrawText(TEXT("随 身 装 备"), FLinearColor(0.96f, 0.90f, 0.68f), BX0 + 34.0f, BY0 + 16.0f, Font, FS * 0.92f);
				if (const ABODPlayerState* BkPS = PC ? PC->GetPlayerState<ABODPlayerState>() : nullptr)
				{
					const FString CTxt = FString::Printf(TEXT("配额  %d"), BkPS->GetCinder());
					float CW2 = 0.0f, CH2 = 0.0f;
					GetTextSize(CTxt, CW2, CH2, Font, FS * 0.8f);
					DrawText(CTxt, FLinearColor(1.0f, 0.78f, 0.28f), BX0 + BPW - CW2 - 34.0f, BY0 + 20.0f, Font, FS * 0.8f);
				}
				DrawRect(FrameDim, BX0 + 30.0f, BY0 + 58.0f, BPW - 60.0f, 1.0f);
			}
			// 中缝分隔线
			DrawRect(FrameDim, BX0 + BPW * 0.5f, BY0 + 70.0f, 1.0f, BPH - 130.0f);
			auto ShadowText = [&](const FString& T, const FLinearColor& C, float X, float Y, float Scale)
			{
				DrawText(T, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), X + 1.8f, Y + 1.8f, Font, FS * Scale);
				DrawText(T, C, X, Y, Font, FS * Scale);
			};
			// —— 左列：武器分类槽 ——
			const float LX0 = BX0 + 34.0f;
			const float ColW = BPW * 0.5f - 68.0f;   // 卡片跟着面板走，不再写死 320
			float LY0 = BY0 + 74.0f;
			ShadowText(TEXT("武 器 · 数字键直切"), FLinearColor(0.60f, 0.52f, 0.30f), LX0, LY0, 0.62f);
			LY0 += LH * 0.62f + 8.0f;
			// 删掉刀斧手枪后只剩 5 把，槽位重划为主武器 ×3 + 特殊 ×1（特殊 = 混沌比特）
			AShooterWeapon* Primary1 = nullptr; AShooterWeapon* Primary2 = nullptr;
			AShooterWeapon* Primary3 = nullptr; AShooterWeapon* Special = nullptr;
			for (AShooterWeapon* W : Weps)
			{
				if (!W) { continue; }
				const FString CN = W->GetClass()->GetName();
				if (CN.Contains(TEXT("Dice"))) { if (!Special) { Special = W; } }
				else if (!Primary1) { Primary1 = W; }
				else if (!Primary2) { Primary2 = W; }
				else if (!Primary3) { Primary3 = W; }
			}
			struct FSlotRow { const TCHAR* Label; AShooterWeapon* W; };
			const FSlotRow Rows[] = {
				{ TEXT("主武器 1"), Primary1 }, { TEXT("主武器 2"), Primary2 },
				{ TEXT("主武器 3"), Primary3 }, { TEXT("特殊"),     Special },
			};
			for (const FSlotRow& R : Rows)
			{
				const bool bCur = (R.W && R.W == MyChar->GetCurrentWeapon());
				DrawRect(bCur ? FLinearColor(0.20f, 0.16f, 0.06f, 0.92f) : FLinearColor(0.072f, 0.070f, 0.050f, 0.9f), LX0, LY0, ColW, 68.0f);
				const FLinearColor CardEdge = bCur ? Frame : FrameDim;
				DrawRect(CardEdge, LX0, LY0, ColW, 1.0f);
				DrawRect(CardEdge, LX0, LY0 + 67.0f, ColW, 1.0f);
				DrawRect(CardEdge, LX0, LY0, 1.0f, 68.0f);
				DrawRect(CardEdge, LX0 + ColW - 1.0f, LY0, 1.0f, 68.0f);
				// 当前武器左侧竖条，扫一眼就知道拿的是哪把
				if (bCur) { DrawRect(FLinearColor(1.0f, 0.78f, 0.28f), LX0, LY0, 3.0f, 68.0f); }
				ShadowText(R.Label, FLinearColor(0.58f, 0.52f, 0.38f), LX0 + 14.0f, LY0 + 5.0f, 0.52f);
				FString Disp = TEXT("— 空 —");
				if (R.W)
				{
					const FString CN = R.W->GetClass()->GetName();
					const int32 Idx = Weps.Find(R.W);
					Disp = FString::Printf(TEXT("[%d]  %s%s"), Idx + 1,
						*BoBWeaponDisplayName(CN),
						bCur ? TEXT("   ◄ 当前") : TEXT(""));
				}
				ShadowText(Disp, R.W ? (bCur ? FLinearColor(1.0f, 0.90f, 0.62f) : FLinearColor(0.90f, 0.88f, 0.80f)) : FLinearColor(0.42f, 0.40f, 0.36f), LX0 + 14.0f, LY0 + 30.0f, 0.72f);
				LY0 += 78.0f;
			}
			// —— 右列：宝物格 ——
			const float RX0 = BX0 + BPW * 0.5f + 34.0f;
			float RY0 = BY0 + 74.0f;
			ShadowText(FString::Printf(TEXT("元 质 · 容量 %d / %d"), MyChar->CarriedSlotsUsed(), AShooterCharacter::BackpackSlots), FLinearColor(0.60f, 0.52f, 0.30f), RX0, RY0, 0.62f);
			RY0 += LH * 0.62f + 8.0f;
			TArray<FLinearColor> SlotCols2;
			for (uint8 K : MyChar->GetCarried())
			{
				const FLootDef& D = GetLootDef(static_cast<ELootKind>(K));
				const FLinearColor C = D.Slots >= 3 ? FLinearColor(1.0f, 0.78f, 0.2f)
					: (D.Slots == 2 ? FLinearColor(0.75f, 0.42f, 1.0f) : FLinearColor(0.5f, 0.75f, 0.95f));
				for (int32 s = 0; s < D.Slots; s++) { SlotCols2.Add(C); }
			}
			const float BS = 52.0f;
			for (int32 i = 0; i < AShooterCharacter::BackpackSlots; i++)
			{
				DrawRect(FLinearColor(0.04f, 0.05f, 0.08f, 0.9f), RX0 + i * (BS + 8.0f) - 2.0f, RY0 - 2.0f, BS + 4.0f, BS + 4.0f);
				DrawRect(i < SlotCols2.Num() ? SlotCols2[i] : FLinearColor(0.14f, 0.16f, 0.2f), RX0 + i * (BS + 8.0f), RY0, BS, BS);
			}
			RY0 += BS + 16.0f;
			if (MyChar->GetCarried().Num() == 0)
			{
				const TCHAR* Hints[] = {
					TEXT("黄色光点处可搜刮遗构。"),
					TEXT("离核心越远，遗构价值越高，"),
					TEXT("但所需的背包容量也越大。"),
					TEXT("将遗构带回核心录入，"),
					TEXT("回收后计入评定。"),
				};
				for (const TCHAR* H : Hints)
				{
					ShadowText(H, FLinearColor(0.72f, 0.74f, 0.7f), RX0, RY0, 0.56f);
					RY0 += LH * 0.56f + 2.0f;
				}
				RY0 += 4.0f;
			}
			for (uint8 K : MyChar->GetCarried())
			{
				const FLootDef& D = GetLootDef(static_cast<ELootKind>(K));
				const bool b3 = D.Slots >= 3, b2 = D.Slots == 2;
				const FString Tag = b3 ? TEXT("【传世】") : b2 ? TEXT("【史诗】") : TEXT("【精良】");
				const FLinearColor TagC = b3 ? FLinearColor(1.0f, 0.78f, 0.2f)
					: b2 ? FLinearColor(0.75f, 0.42f, 1.0f) : FLinearColor(0.5f, 0.75f, 0.95f);
				float TagW = 0.0f, TagH = 0.0f;
				GetTextSize(Tag, TagW, TagH, Font, FS * 0.6f);
				ShadowText(Tag, TagC, RX0, RY0, 0.6f);
				ShadowText(FString::Printf(TEXT("%s   %d分"), D.Name, D.Value), FLinearColor(0.92f, 0.92f, 0.88f), RX0 + TagW, RY0, 0.6f);
				RY0 += LH * 0.6f + 4.0f;
			}
			RY0 += 8.0f;
			ShadowText(FString::Printf(TEXT("已存入核心：%d 分"), MyChar->GetBankedValue()), FLinearColor(0.55f, 0.95f, 1.0f), RX0, RY0, 0.66f);
			RY0 += LH * 0.66f + 10.0f;

			// ===== 道具区：投送端买到的可放置装备 / 收藏品(与武器、遗构同页) =====
			{
				DrawRect(FrameDim, RX0, RY0, BX0 + BPW - 34.0f - RX0, 1.0f);
				RY0 += 10.0f;
				ShadowText(TEXT("道 具 · 投送端支取"), FLinearColor(0.60f, 0.52f, 0.30f), RX0, RY0, 0.62f);
				RY0 += LH * 0.62f + 6.0f;

				// 汇总同类道具数量
				const TArray<uint8>& Owned = MyChar->GetOwnedItems();
				TMap<uint8, int32> Counts;
				for (uint8 Id : Owned) { Counts.FindOrAdd(Id)++; }

				if (Counts.Num() == 0)
				{
					ShadowText(TEXT("（道具栏是空的 · 间隙到投送端支取）"), FLinearColor(0.5f, 0.55f, 0.55f), RX0, RY0, 0.55f);
					RY0 += LH * 0.55f + 4.0f;
				}
				else
				{
					ShadowText(TEXT("点击选中，战场上按 Q 使用"), FLinearColor(0.52f, 0.50f, 0.44f), RX0, RY0, 0.48f);
					RY0 += LH * 0.48f + 8.0f;

					float ItemMX = -1.0f, ItemMY = -1.0f;
					if (PC) { PC->GetMousePosition(ItemMX, ItemMY); }
					const int32 SelKind = Owned.IsValidIndex(MyChar->ItemSlot) ? static_cast<int32>(Owned[MyChar->ItemSlot]) : -1;

					for (const TPair<uint8, int32>& P : Counts)
					{
						const FBoBShopEntry& IE = GetShopEntry(static_cast<EBoBShopItem>(P.Key));
						const float ThumbS = 38.0f;
						const float TY0 = RY0 - 4.0f;
						const float RowW = BX0 + BPW - 34.0f - RX0;
						const bool bSelected = (static_cast<int32>(P.Key) == SelKind);
						const bool bHovered = (ItemMX >= RX0 && ItemMX <= RX0 + RowW && ItemMY >= TY0 && ItemMY <= TY0 + ThumbS);
						// 点中就把道具栏切到这一类的第一件
						if (bHovered && PC && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
						{
							const int32 Found = Owned.IndexOfByKey(P.Key);
							if (Found != INDEX_NONE) { MyChar->ItemSlot = Found; }
						}
						if (bSelected || bHovered)
						{
							DrawRect(bSelected ? FLinearColor(0.20f, 0.16f, 0.06f, 0.85f) : FLinearColor(0.13f, 0.12f, 0.07f, 0.7f),
								RX0 - 6.0f, TY0 - 3.0f, RowW + 6.0f, ThumbS + 6.0f);
						}
						if (bSelected) { DrawRect(FLinearColor(1.0f, 0.78f, 0.28f), RX0 - 6.0f, TY0 - 3.0f, 3.0f, ThumbS + 6.0f); }

						DrawRect(FLinearColor(0.085f, 0.082f, 0.055f, 0.92f), RX0, TY0, ThumbS, ThumbS);
						if (UTexture2D* IconTex = GetItemIcon(IE.IconPath))
						{
							DrawTexture(IconTex, RX0, TY0, ThumbS, ThumbS, 0.0f, 0.0f, 1.0f, 1.0f);
						}
						const FLinearColor ThumbEdge = bSelected ? Frame : FrameDim;
						DrawRect(ThumbEdge, RX0, TY0, ThumbS, 1.0f);
						DrawRect(ThumbEdge, RX0, TY0 + ThumbS - 1.0f, ThumbS, 1.0f);
						DrawRect(ThumbEdge, RX0, TY0, 1.0f, ThumbS);
						DrawRect(ThumbEdge, RX0 + ThumbS - 1.0f, TY0, 1.0f, ThumbS);

						ShadowText(IE.Name, bSelected ? FLinearColor(1.0f, 0.92f, 0.70f) : FLinearColor(0.92f, 0.88f, 0.72f),
							RX0 + ThumbS + 12.0f, RY0 - 1.0f, 0.60f);
						{
							const FString CntTxt = FString::Printf(TEXT("x%d"), P.Value);
							float CnW = 0.0f, CnH = 0.0f;
							GetTextSize(CntTxt, CnW, CnH, Font, FS * 0.58f);
							ShadowText(CntTxt, FLinearColor(1.0f, 0.78f, 0.28f), BX0 + BPW - 34.0f - CnW, RY0 - 1.0f, 0.58f);
						}
						RY0 += FMath::Max(LH * 0.58f + 8.0f, ThumbS + 8.0f);
					}
				}
			}
			// 底部按键栏
			{
				DrawRect(FrameDim, BX0 + 30.0f, BY0 + BPH - 54.0f, BPW - 60.0f, 1.0f);
				const FString T = TEXT("[ N ] 关闭背包        [ 1 - 8 ] 切换武器        [ H ] 丢弃遗构");
				float TW = 0.0f, TH = 0.0f;
				GetTextSize(T, TW, TH, Font, FS * 0.56f);
				ShadowText(T, FLinearColor(0.80f, 0.74f, 0.55f), BX0 + (BPW - TW) * 0.5f, BY0 + BPH - 40.0f, 0.56f);
			}
		}
	}

	// ===== 撤离直升机·客户端侧补装 =====
	// 复制只带位置不带网格/缩放/动画：客户端按 BoBHeli 标签本地补齐（1Hz 轮询，幂等；主机侧全为无操作）
	{
		static double NextHeliPoll = 0.0;
		const double NowS = FPlatformTime::Seconds();
		if (NowS > NextHeliPoll)
		{
			NextHeliPoll = NowS + 1.0;
			for (TActorIterator<ASkeletalMeshActor> HIt(World); HIt; ++HIt)
			{
				if (!HIt->ActorHasTag(FName("BoBHeli")))
				{
					continue;
				}
				USkeletalMeshComponent* HC = HIt->GetSkeletalMeshComponent();
				if (!HC)
				{
					continue;
				}
				if (!HC->GetSkeletalMeshAsset())
				{
					if (USkeletalMesh* HM = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/FabAssets/MH6/Helicopter.Helicopter")))
					{
						HC->SetSkeletalMesh(HM);
						HC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
						const float MaxXY = FMath::Max(HM->GetBounds().BoxExtent.X, HM->GetBounds().BoxExtent.Y) * 2.0f;
						if (MaxXY > 1.0f)
						{
							HIt->SetActorScale3D(FVector(900.0f / MaxXY));
						}
					}
				}
				if (HC->GetSkeletalMeshAsset() && !HC->IsPlaying())
				{
					if (UAnimSequence* HA = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/FabAssets/MH6/Helicopter_Anim.Helicopter_Anim")))
					{
						HC->PlayAnimation(HA, true);
					}
				}
			}
		}
	}

	// ===== 灯光闪烁（客户端视觉）=====
	// 打了 BoBFlicker 标签的点光每帧按双层噪声抖动强度（基准值首见时缓存）
	{
		static TMap<TWeakObjectPtr<APointLight>, float> FlickerBase;
		const float TT = (float)FPlatformTime::Seconds();
		for (TActorIterator<APointLight> LIt(World); LIt; ++LIt)
		{
			if (!LIt->ActorHasTag(FName("BoBFlicker")))
			{
				continue;
			}
			UPointLightComponent* PLC = LIt->PointLightComponent;
			if (!PLC)
			{
				continue;
			}
			float& Base = FlickerBase.FindOrAdd(*LIt, PLC->Intensity);
			const float Seed = (float)(GetTypeHash(LIt->GetFName()) % 997);
			const float Slow = FMath::PerlinNoise1D(TT * 5.5f + Seed);
			const float Fast = FMath::PerlinNoise1D(TT * 21.0f + Seed * 1.7f);
			PLC->SetIntensity(Base * FMath::Clamp(0.7f + 0.4f * Slow + 0.2f * Fast, 0.2f, 1.4f));
		}
	}

	// ===== 指南面板（开局弹窗 / R 键暂停均用它）=====
	// 开局不真暂停：波次由"全员确认简报"就绪门控（WaveManager）拦住。
	// 暂停会吞掉 EnhancedInput 的鼠标点击（简报翻不了页），且主机暂停会复制冻结客户端。
	if (bShowGuide)
	{
		// 面板尺寸随内容自适应：测量遍先累计最宽行与总高，再定框，避免大片留白
		float PanW = 0.0f, PX0 = 0.0f;
		float GY = 0.0f, MaxRowW = 0.0f;
		bool bMeasureRows = true;
		auto GLine = [&](const FString& T, const FLinearColor& C, float Scale, float /*Indent*/)
		{
			float TW = 0.0f, TH = 0.0f;
			GetTextSize(T, TW, TH, Font, FS * Scale);
			if (bMeasureRows)
			{
				MaxRowW = FMath::Max(MaxRowW, TW);
			}
			else
			{
				const float TX = PX0 + (PanW - TW) * 0.5f;
				DrawText(T, FLinearColor(0.0f, 0.0f, 0.0f, 0.9f), TX + 2.0f, GY + 2.0f, Font, FS * Scale);
				DrawText(T, C, TX, GY, Font, FS * Scale);
			}
			GY += LH * Scale + 2.0f;
		};
		auto DrawGuideRows = [&]()
		{
		const FLinearColor CHead(1.0f, 0.85f, 0.3f);
		const FLinearColor CBody(0.92f, 0.92f, 0.88f);
		const FLinearColor CKey(0.55f, 0.85f, 0.95f);
		if (GuidePage == 0)
		{

		GLine(TEXT("K - 11 · 勘察员简报"), FLinearColor(0.4f, 0.9f, 1.0f), 1.12f, 270.0f);
		GY += 14.0f;
		GLine(TEXT("【任务】"), CHead, 0.84f,44.0f);
		GLine(TEXT("你是联合勘察局派往 K-11 的勘察员。界隙正在吞掉地球，"), CBody, 0.76f,64.0f);
		GLine(TEXT("本土需要能停住它的办法，而这颗星球上有人做到过。"), CBody, 0.76f,64.0f);
		GLine(TEXT("尽量多带遗构回去，守住核心，撑过 10 次同化潮等回收通道。"), CBody, 0.76f,64.0f);
		GY += 11.0f;
		GLine(TEXT("【核心】"), CHead, 0.84f,44.0f);
		GLine(TEXT("坐标内唯一的锚点。它周围维持一小片现实基准，"), CBody, 0.76f,64.0f);
		GLine(TEXT("场内你的感知与环境一致。核心耐久归零，任务失败。"), CBody, 0.76f,64.0f);
		GY += 11.0f;
		GLine(TEXT("【同化潮】"), CHead, 0.84f,44.0f);
		GLine(TEXT("同化体成批冲向核心，共 10 次。TIDE 3 / 6 / 9 为渐进同化批，"), CBody, 0.76f,64.0f);
		GLine(TEXT("行动更整齐；TIDE 10 为渗透同化。"), CBody, 0.76f,64.0f);
		GY += 11.0f;
		GLine(TEXT("【补给时段】"), CHead, 0.84f,44.0f);
		GLine(TEXT("每次同化潮之间先有 45 秒补给时段，投送端此时开放。"), CBody, 0.76f,64.0f);
		GLine(TEXT("B 调取投送清单，F 补充当前武器备弹。"), CBody, 0.76f,64.0f);
		GLine(TEXT("投送清单分常备与本轮投送两栏，以配额支取。"), CBody, 0.76f,64.0f);
		GLine(TEXT("配额由击杀掉落，回收后清零，带不回本土。"), CBody, 0.76f,64.0f);
		GY += 11.0f;
		GLine(TEXT("【险区勘探】"), CHead, 0.84f,44.0f);
		GLine(TEXT("补给时段结束进入险区勘探，此时才可离开基准场。"), CBody, 0.76f,64.0f);
		GLine(TEXT("黄色光点是遗构，越深入价值越高。绿色光点是武器缓存。"), CBody, 0.76f,64.0f);
		GLine(TEXT("遗构需带回核心录入。未录入的，阵亡时全部损失。"), CBody, 0.76f,64.0f);
		GY += 11.0f;
		GLine(TEXT("【失谐】"), CHead, 0.84f,44.0f);
		GLine(TEXT("离开基准场后，你的画面会与实景逐渐脱节，出现撕裂与噪点。"), CBody, 0.76f,64.0f);
		GLine(TEXT("勘察局把这套读数叫失谐，也叫认知污染。原理类似辐射值。"), CBody, 0.76f,64.0f);
		GLine(TEXT("走得越远、身上未录入的遗构越多，失谐涨得越快。"), CBody, 0.76f,64.0f);
		GLine(TEXT("回到基准场、进入谐振灯范围、或把遗构录入，都能压制失谐。"), CBody, 0.76f,64.0f);
		GLine(TEXT("失谐触顶后，处决者沿你的方向直线接近。"), CBody, 0.76f,64.0f);
		GY += 16.0f;
		GLine(TEXT("——  单击鼠标左键，查看按键操作  ——"), FLinearColor(1.0f, 0.95f, 0.6f), 0.82f, 300.0f);
		}
		else
		{
		GLine(TEXT("【按键操作】"), CHead, 0.90f,44.0f);
		GY += 10.0f;
		GLine(TEXT("W / A / S / D  移动                    鼠标  视角"), CKey, 0.78f,64.0f);
		GLine(TEXT("Shift  疾跑（消耗体力）                空格  跳跃"), CKey, 0.78f,64.0f);
		GLine(TEXT("鼠标左键  开火                         滚轮 / CapsLock  切换武器"), CKey, 0.78f,64.0f);
		GLine(TEXT("1 - 8  快速切枪                        N  背包"), CKey, 0.78f,64.0f);
		GLine(TEXT("F  拾取 / 在投送端补备弹               Q  使用道具"), CKey, 0.78f,64.0f);
		GLine(TEXT("G  丢弃当前武器"), CKey, 0.78f,64.0f);
		GLine(TEXT("H  丢弃遗构                            E  小地图放大 / 缩小"), CKey, 0.78f,64.0f);
		GLine(TEXT("便携谐振灯 / 回抛索 / 高爆装药支取后进道具栏，按 Q 自选时机使用。"), CBody, 0.72f,64.0f);
		GLine(TEXT("R  暂停并打开本手册"), CKey, 0.78f,64.0f);
		GY += 10.0f;
		GLine(TEXT("【投送端】站到投送端旁，同化潮间隙可用"), CHead, 0.84f,44.0f);
		GLine(TEXT("B  调取 / 收起投送清单                 1 - 8  查看该项详情"), CKey, 0.78f,64.0f);
		GLine(TEXT("Y  支取                                U  再看看（退回清单）"), CKey, 0.78f,64.0f);
		GLine(TEXT("清单页鼠标可直接点选，按数字只看详情，不扣配额。"), CBody, 0.72f,64.0f);
		GY += 16.0f;
		GLine(TEXT("——  单击左键 / 按 R 键，进入战场  ——"), FLinearColor(1.0f, 0.95f, 0.6f), 0.82f, 300.0f);
		}
		};
		DrawGuideRows();                        // 第一遍：量最宽行与总高
		const float RowsH = GY;
		PanW = FMath::Clamp(MaxRowW + 110.0f, 720.0f, ScreenW - 40.0f);
		const float PanH = FMath::Min(RowsH + 96.0f, Canvas->SizeY - 40.0f);
		PX0 = (ScreenW - PanW) * 0.5f;
		const float PY0 = (Canvas->SizeY - PanH) * 0.5f;
		DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 0.0f, 0.0f, ScreenW, Canvas->SizeY);
		DrawRect(FLinearColor(0.055f, 0.075f, 0.105f, 0.95f), PX0, PY0, PanW, PanH);
		// 外框 + 内衬细线
		const FLinearColor Frame(0.35f, 0.75f, 0.85f, 0.9f);
		DrawLine(PX0, PY0, PX0 + PanW, PY0, Frame, 2.0f);
		DrawLine(PX0, PY0 + PanH, PX0 + PanW, PY0 + PanH, Frame, 2.0f);
		DrawLine(PX0, PY0, PX0, PY0 + PanH, Frame, 2.0f);
		DrawLine(PX0 + PanW, PY0, PX0 + PanW, PY0 + PanH, Frame, 2.0f);
		const FLinearColor Frame2(0.35f, 0.75f, 0.85f, 0.3f);
		DrawLine(PX0 + 7.0f, PY0 + 7.0f, PX0 + PanW - 7.0f, PY0 + 7.0f, Frame2, 1.0f);
		DrawLine(PX0 + 7.0f, PY0 + PanH - 7.0f, PX0 + PanW - 7.0f, PY0 + PanH - 7.0f, Frame2, 1.0f);
		DrawLine(PX0 + 7.0f, PY0 + 7.0f, PX0 + 7.0f, PY0 + PanH - 7.0f, Frame2, 1.0f);
		DrawLine(PX0 + PanW - 7.0f, PY0 + 7.0f, PX0 + PanW - 7.0f, PY0 + PanH - 7.0f, Frame2, 1.0f);
		bMeasureRows = false;
		GY = PY0 + FMath::Max(14.0f, (PanH - RowsH) * 0.5f);
		DrawGuideRows();                        // 第二遍：居中绘制
	}
	else
	{
		DrawText(TEXT("[R] 暂停 / 指南"), FLinearColor(0.85f, 0.85f, 0.85f, 0.7f), 30.0f, Canvas->SizeY - 44.0f, Font, FS * 0.65f);
	}
}

void AShooterHUD::ToggleGuide()
{
	if (bShowGuide)
	{
		CloseGuide();
		return;
	}
	bShowGuide = true;
	GuidePage = 0;
	// R 打开 → 尝试暂停（单机/listen 主机生效；纯客户端仅显示面板）
	bGuidePaused = UGameplayStatics::SetGamePaused(GetWorld(), true);
}

void AShooterHUD::CloseGuide()
{
	// 第一页（简报）关闭 → 翻到第二页（按键操作）；第二页关闭才真正退出
	if (GuidePage == 0)
	{
		GuidePage = 1;
		return;
	}
	bShowGuide = false;
	GuidePage = 0;
	if (bGuidePaused)
	{
		UGameplayStatics::SetGamePaused(GetWorld(), false);
		bGuidePaused = false;
	}
}
