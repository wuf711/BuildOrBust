// Build or Bust — AI Director 实现。

#include "BoBAIDirector.h"
#include "BoBEnemy.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "NavigationSystem.h"
#include "UObject/ConstructorHelpers.h"

ABoBAIDirector::ABoBAIDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;   // 投放决策不需要每帧做

	// 默认指到蓝图层。找不到就留空——SpawnEnemy 会喊一声而不是崩
	static ConstructorHelpers::FClassFinder<ABoBEnemy>
		DefaultFoe(TEXT("/Game/BoB/Blueprints/BP_BoBEnemy"));
	if (DefaultFoe.Succeeded())
	{
		EnemyClass = DefaultFoe.Class;
	}
}

float ABoBAIDirector::IntensityAt(float Progress)
{
	// 分段常数，不做连续函数：波峰波谷要能一眼看懂、一眼调得动。
	// 50~70% 那段 0.3 是喘息期——玩家在这里换弹、拉人、重新分工。
	// 去掉它，整潮就变成匀速传送带，累而且没有记忆点。
	if (Progress < 0.20f) { return 0.6f; }   // 试探期
	if (Progress < 0.50f) { return 1.0f; }   // 正常压力
	if (Progress < 0.70f) { return 0.3f; }   // 喘息期
	return 1.6f;                             // 高潮
}

float ABoBAIDirector::PressureValve(float LowestHealthPct, float AverageHealthPct,
	int32 AliveEnemies)
{
	// 只有这两条。L4D 那种情绪状态机的额外状态只会让节奏难以调试。
	float Mul = 1.f;
	if (LowestHealthPct < 0.30f)
	{
		Mul *= 0.5f;
	}
	if (AverageHealthPct > 0.80f && AliveEnemies <= 0)
	{
		Mul *= 1.3f;
	}
	return Mul;
}

int32 ABoBAIDirector::AllowedTierAt(float Progress)
{
	// 分段放开，和强度包络同向但更早一点——
	// 等到高潮段（70%）才第一次见到 3 档，玩家来不及适应就结束了
	if (Progress < 0.20f) { return 1; }
	if (Progress < 0.45f) { return 2; }
	if (Progress < 0.75f) { return 3; }
	return 4;
}

float ABoBAIDirector::InteractiveShare(int32 InWaveIndex)
{
	// 常规 : 交互 —— 1-3 潮放水建立信心，7-10 潮加压
	if (InWaveIndex <= 3) { return 0.2f; }
	if (InWaveIndex <= 6) { return 0.3f; }
	return 0.4f;
}

void ABoBAIDirector::BeginWave(int32 InWaveIndex)
{
	WaveIndex = InWaveIndex;
	BudgetTotal = WaveBudget(InWaveIndex);
	BudgetRemain = BudgetTotal;
	TimeRemain = WaveDuration;
	SpawnCredit = 0.f;
	bHasPending = false;
	bWaveActive = true;
	SpawnLog.Reset();
}

void ABoBAIDirector::EndWave()
{
	bWaveActive = false;
	// 没花完的预算直接作废，不追加到下一潮：
	// 攒预算会让某一潮突然爆量，玩家读不出原因
	BudgetRemain = 0.f;
	SpawnCredit = 0.f;
}

float ABoBAIDirector::GetProgress() const
{
	if (WaveDuration <= KINDA_SMALL_NUMBER) { return 1.f; }
	return FMath::Clamp(1.f - TimeRemain / WaveDuration, 0.f, 1.f);
}

float ABoBAIDirector::GetCurrentRate() const
{
	if (!bWaveActive || TimeRemain <= KINDA_SMALL_NUMBER || BudgetRemain <= 0.f)
	{
		return 0.f;
	}
	const float I = IntensityAt(GetProgress())
		* PressureValve(LowestHealthPct, AverageHealthPct, AliveEnemies);
	// R(t) = B_remain * I(t) / T_remain
	// I=1 时正好在剩余时间里均匀花完；I<1 会攒下来，到高潮段一起倾泻
	return BudgetRemain * I / TimeRemain;
}

bool ABoBAIDirector::PickEnemy(FName& OutId, FBoBEnemyRow& OutRow,
	bool& bOutDowngraded) const
{
	bOutDowngraded = false;

	// 按剩余预算筛，不按已攒额度——按额度筛就只会不停投最便宜的那种
	const int32 Affordable = FMath::FloorToInt(BudgetRemain);
	TArray<FName> Cand = UBoBEnemyLib::AffordableAt(WaveIndex, Affordable);
	if (Cand.Num() == 0)
	{
		return false;
	}

	// 分成两池：常规靶 / 战术交互体
	TArray<TPair<FName, FBoBEnemyRow>> Regular, Interactive;
	for (const FName& Id : Cand)
	{
		FBoBEnemyRow Row;
		if (!UBoBEnemyLib::GetEnemyRow(Id, Row)) { continue; }
		if (Row.PickWeight <= 0.f) { continue; }
		// 威胁档闸门：一潮之内按进度逐档放开。
		// 这是"渐进式加大难度"真正落地的地方——开场只放 1 档杂兵，
		// 越往后越高的档才解禁，精英只在后段出现
		if (Row.ThreatTier > AllowedTierAt(GetProgress())) { continue; }
		if (Row.Role == EBoBRole::Interactive)
		{
			Interactive.Emplace(Id, Row);
		}
		else
		{
			Regular.Emplace(Id, Row);
		}
	}

	const float Share = InteractiveShare(WaveIndex);
	bool bWantInteractive = FMath::FRand() < Share;

	// 硬约束：场上最多两种交互体。超了就把这次的交互体降级成常规靶——
	// 规格书写明这条不可协商，否则玩家的反应带宽会被打满
	if (bWantInteractive && AliveInteractive >= MaxInteractiveAlive)
	{
		bWantInteractive = false;
		bOutDowngraded = true;
	}

	const TArray<TPair<FName, FBoBEnemyRow>>& Pool =
		(bWantInteractive && Interactive.Num() > 0) ? Interactive : Regular;
	if (Pool.Num() == 0)
	{
		return false;
	}

	// 池内按 PickWeight 抽签。威胁档的门在上面按 Tier 筛过了，
	// 这里不再二次加权——两层都调权重会互相打架，调平衡时没人算得清
	float Total = 0.f;
	TArray<float> W;
	W.Reserve(Pool.Num());
	for (const auto& P : Pool)
	{
		W.Add(P.Value.PickWeight);
		Total += P.Value.PickWeight;
	}
	if (Total <= 0.f) { return false; }

	float Roll = FMath::FRand() * Total;
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		Roll -= W[i];
		if (Roll <= 0.f)
		{
			OutId = Pool[i].Key;
			OutRow = Pool[i].Value;
			return true;
		}
	}
	OutId = Pool.Last().Key;
	OutRow = Pool.Last().Value;
	return true;
}

bool ABoBAIDirector::IsVisibleToAnyPlayer(const FVector& Where) const
{
	UWorld* W = GetWorld();
	if (!W) { return false; }

	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator();
		It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC || !PC->PlayerCameraManager) { continue; }

		const FVector CamLoc = PC->PlayerCameraManager->GetCameraLocation();
		const FVector CamFwd = PC->PlayerCameraManager->GetCameraRotation().Vector();
		const float Fov = PC->PlayerCameraManager->GetFOVAngle();

		FVector ToTarget = Where - CamLoc;
		if (!ToTarget.Normalize()) { return true; }

		// 余量算进去：余光扫到也算看见
		const float Limit = FMath::Cos(FMath::DegreesToRadians(Fov * 0.5f + FovMargin));
		if (FVector::DotProduct(ToTarget, CamFwd) > Limit)
		{
			return true;
		}
	}
	return false;
}

bool ABoBAIDirector::FindSpawnPoint(FVector& OutLocation) const
{
	UWorld* W = GetWorld();
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(W);
	if (!W || !Nav) { return false; }

	// 收集玩家位置
	TArray<FVector> Players;
	for (FConstPlayerControllerIterator It = W->GetPlayerControllerIterator();
		It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* P = PC->GetPawn())
			{
				Players.Add(P->GetActorLocation());
			}
		}
	}
	if (Players.Num() == 0) { return false; }

	// 以随机一名玩家为中心撒点，逐个过筛
	const int32 Tries = 24;
	for (int32 i = 0; i < Tries; ++i)
	{
		const FVector Around = Players[FMath::RandHelper(Players.Num())];
		FNavLocation Hit;
		if (!Nav->GetRandomReachablePointInRadius(Around, SpawnDistMax, Hit))
		{
			continue;
		}

		// 距最近玩家要落在区间里：太近会当着面冒出来，太远则半天走不到
		float NearestSq = TNumericLimits<float>::Max();
		for (const FVector& P : Players)
		{
			NearestSq = FMath::Min(NearestSq,
				FVector::DistSquared(Hit.Location, P));
		}
		const float Nearest = FMath::Sqrt(NearestSq);
		if (Nearest < SpawnDistMin || Nearest > SpawnDistMax)
		{
			continue;
		}

		if (IsVisibleToAnyPlayer(Hit.Location))
		{
			continue;
		}

		OutLocation = Hit.Location;
		return true;
	}

	// 一个都不合格就返回 false，由调用方延迟。
	// 规格书：宁可空 2 秒，也不能让玩家看见凭空出现——这里不降低标准
	return false;
}

ABoBEnemy* ABoBAIDirector::SpawnEnemy(FName Id, const FVector& Where)
{
	UWorld* W = GetWorld();
	if (!W) { return nullptr; }

	if (!EnemyClass)
	{
		// 每潮只叫一次，不然 Tick 会把日志刷爆
		static int32 LastComplainedWave = -1;
		if (LastComplainedWave != WaveIndex)
		{
			LastComplainedWave = WaveIndex;
			UE_LOG(LogTemp, Warning,
				TEXT("[BoB] Director 没配 EnemyClass，本潮不会投放任何东西"));
		}
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	// 抬到胶囊半高，直接贴着导航面生成会卡进地里
	const FVector At = Where + FVector(0.f, 0.f, 95.f);
	ABoBEnemy* Enemy = W->SpawnActor<ABoBEnemy>(EnemyClass, At,
		FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params);
	if (!Enemy) { return nullptr; }

	if (!Enemy->ApplyEnemyRow(Id))
	{
		// 配不出来的敌人不能留在场上：它会顶着蓝图默认值乱跑，
		// 还会被存活统计算进去，把潮次卡住不结束
		Enemy->Destroy();
		return nullptr;
	}

	// 血量/速度都已由数据表写好，这里定基准线，疾跑和自回血才有参照
	Enemy->InitCombatBaseline();
	return Enemy;
}

int32 ABoBAIDirector::RequestReinforcement(FName EnemyId, int32 Count)
{
	if (!HasAuthority()) { return 0; }
	int32 Made = 0;
	for (int32 i = 0; i < Count; ++i)
	{
		FVector Where;
		if (!FindSpawnPoint(Where)) { break; }
		if (ABoBEnemy* E = SpawnEnemy(EnemyId, Where))
		{
			if (E->IsInteractive()) { AliveInteractive++; }
			OnDirectorSpawn.Broadcast(EnemyId, E);
			Made++;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[BoBDirector] 增援 %s x%d，实际放出 %d"),
		*EnemyId.ToString(), Count, Made);
	return Made;
}

void ABoBAIDirector::RefreshWorldState()
{
	UWorld* W = GetWorld();
	if (!W) { return; }

	int32 Alive = 0, Inter = 0;
	for (TActorIterator<AShooterNPC> It(W); It; ++It)
	{
		AShooterNPC* NPC = *It;
		if (!IsValid(NPC) || NPC->IsActorBeingDestroyed() || NPC->IsDead())
		{
			continue;
		}
		// 调试假人不参与压力阀计算，否则摆几只在场上会让 Director 误判形势
		if (NPC->Tags.Contains(FName("BoBDummy")))
		{
			continue;
		}
		// 野生同化体同理，而且更隐蔽：它们常驻十几只且不进攻核心，
		// 算进压力阀的话 Director 会一直以为场上很挤，从而压低投放速率——
		// 波次节奏被悄悄改坏，日志上还看不出毛病。压力阀衡量的是"核心承受的压力"，
		// 野外那些不构成压力
		if (NPC->Tags.Contains(FName("BoBWild")))
		{
			continue;
		}
		Alive++;
		if (const ABoBEnemy* Foe = Cast<ABoBEnemy>(NPC))
		{
			if (Foe->IsInteractive()) { Inter++; }
		}
	}
	AliveEnemies = Alive;
	AliveInteractive = Inter;

	float Sum = 0.f, Lowest = 1.f;
	int32 Num = 0;
	for (TActorIterator<AShooterCharacter> It(W); It; ++It)
	{
		AController* C = It->GetController();
		if (!C || !C->IsPlayerController()) { continue; }
		const float Pct = It->GetHealthPercent();
		Sum += Pct;
		Lowest = FMath::Min(Lowest, Pct);
		Num++;
	}
	if (Num > 0)
	{
		AverageHealthPct = Sum / Num;
		LowestHealthPct = Lowest;
	}
}

/**
 *  作弊开关：冻结投放。
 *
 *  定义在这里而不是 BoBTest.cpp，是因为闸门必须落在真正决定投放的那一拍上；
 *  放在指令那侧只能停住"下一次调用"，停不住已经在跑的预算循环。
 *  非 static：BoBTest.cpp 与 WaveManager.cpp 都要 extern 它。
 *  注意：冻结期间 IsWaveActive() 仍为真，所以本潮不会因为场上清空而结算——
 *  这正是"冻结"该有的样子，不是 bug。
 */
bool GBoBFreezeSpawns = false;

void ABoBAIDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// 生成只能发生在服务器：客户端各自刷一份就成了幽灵敌人
	if (!HasAuthority() || !bWaveActive) { return; }
	// 冻结：连世界状态刷新一起停，否则压力阀会在冻结期间持续攒额度，
	// 一解冻就把攒了半天的量一次性倾泻出来
	if (GBoBFreezeSpawns) { return; }

	if (bAutoTrackWorldState)
	{
		RefreshWorldState();
	}

	TimeRemain = FMath::Max(TimeRemain - DeltaSeconds, 0.f);
	if (TimeRemain <= 0.f)
	{
		EndWave();
		return;
	}

	SpawnCredit += GetCurrentRate() * DeltaSeconds;

	// 每 5 秒一行状态。投放停了的时候，光看"没有投放日志"分不清是
	// 速率算成 0、额度没攒够、还是点位选不出来，所以把四个数一起打出来
	StatusTimer += DeltaSeconds;
	if (StatusTimer >= 5.f)
	{
		StatusTimer = 0.f;
		UE_LOG(LogTemp, Log,
			TEXT("[BoBDirector] %3.0f%% 速率%.2f/s 额度%.1f 预算%.0f 存活%d(交互%d) 血低%.2f均%.2f 卡在: %s"),
			GetProgress() * 100.f, GetCurrentRate(), SpawnCredit, BudgetRemain,
			AliveEnemies, AliveInteractive, LowestHealthPct, AverageHealthPct,
			LastStall.IsEmpty() ? TEXT("-") : *LastStall);
	}

	// 攒够一个型号的 cost 就投；一次 Tick 可能连投几个（高潮段）
	int32 Guard = 8;
	while (Guard-- > 0 && BudgetRemain > 0.f && SpawnCredit > 0.f)
	{
		// 还没定目标就先选一个，选完就锁住攒钱，不再改主意
		bool bDowngraded = false;
		if (!bHasPending)
		{
			bHasPending = PickEnemy(PendingId, PendingRow, bDowngraded);
		}
		FName Id = PendingId;
		FBoBEnemyRow Row = PendingRow;
		if (!bHasPending)
		{
			// 绝大多数时候这只是"额度还没攒够最便宜的型号"，属于正常节奏；
			// 真正的故障是本潮压根没有可投放型号。两者分开写，
			// 免得看日志的人把攒额度当成卡死（我就当成过一次）
			const bool bAnyUnlocked =
				UBoBEnemyLib::AffordableAt(WaveIndex,
					FMath::FloorToInt(BudgetRemain)).Num() > 0;
			LastStall = bAnyUnlocked
				? FString::Printf(TEXT("攒额度中(%.1f)"), SpawnCredit)
				: TEXT("本潮没有可投放型号");
			break;
		}
		// 潮末收尾：速率是 余额×强度/剩余时间，余额只剩最后一两只时速率极低，
		// 那几只要攒很久才出来，而清潮又必须等它们死——玩家就干站着等。
		// 余额已经买不起第二只了就直接投，不再摊
		if (BudgetRemain <= Row.SpawnCost * 1.5f)
		{
			SpawnCredit = FMath::Max(SpawnCredit, (float)Row.SpawnCost);
		}
		if (Row.SpawnCost > SpawnCredit)
		{
			// 攒钱中。目标不变，等下一拍——这正是贵型号能出场的原因
			LastStall = FString::Printf(TEXT("为 %s 攒额度(要 %d，已有 %.1f)"),
				*Id.ToString(), Row.SpawnCost, SpawnCredit);
			break;
		}

		FVector Where;
		if (!FindSpawnPoint(Where))
		{
			// 找不到合格的点就把额度留着，下个 Tick 再试
			LastStall = FString::Printf(TEXT("连续 %d 次找不到生成点"), ++NoPointStreak);
			break;
		}
		NoPointStreak = 0;

		ABoBEnemy* Enemy = SpawnEnemy(Id, Where);
		if (!Enemy)
		{
			// 没放出来就不能扣预算，否则这一潮会静悄悄地空掉
			LastStall = FString::Printf(TEXT("%s 生成失败"), *Id.ToString());
			break;
		}
		bHasPending = false;   // 这一个投出去了，下一拍重新选
		LastStall.Reset();

		SpawnCredit -= Row.SpawnCost;
		BudgetRemain -= Row.SpawnCost;

		FBoBSpawnRecord& Rec = SpawnLog.AddDefaulted_GetRef();
		Rec.EnemyId = Id;
		Rec.Cost = Row.SpawnCost;
		Rec.AtProgress = GetProgress();
		Rec.Intensity = IntensityAt(Rec.AtProgress);
		Rec.bDowngraded = bDowngraded;

		// 交互体上限是按场上实际数量卡的，这里先自增，
		// 免得同一个 Tick 里连投两个交互体（下次 Refresh 才会纠正）
		if (Enemy->IsInteractive()) { AliveInteractive++; }

		UE_LOG(LogTemp, Log,
			TEXT("[BoBDirector] %.0f%% 投放 %s (cost %d, I=%.1f%s)  剩余预算 %.0f"),
			Rec.AtProgress * 100.f, *Id.ToString(), Rec.Cost, Rec.Intensity,
			bDowngraded ? TEXT(", 交互体已满降级") : TEXT(""), BudgetRemain);

		OnDirectorSpawn.Broadcast(Id, Enemy);
	}
}
