#include "WaveManager.h"
#include "BoBAIDirector.h"
#include "BoBEnemy.h"
#include "Boss_CS07.h"
#include "BoBExecutioner.h"
#include "Kismet/GameplayStatics.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "ExperienceComponent.h"
#include "UpgradeComponent.h"
#include "UpgradeTypes.h"
#include "BaseCore.h"
#include "Variant_Shooter/ShooterGameMode.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/GameStateBase.h"
#include "BODPlayerState.h"
#include "LootPickup.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "NavigationSystem.h"
#include "Animation/SkeletalMeshActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Components/PointLightComponent.h"
#include "Boss_CS07.h"

// 本文件下方的实现，但 BeginPlay 排在它们前面，所以要先声明。
// 这个坑在这一份文件里已经踩过两次了：C++ 只往上看。
static void BoBScheduleWildlife(UWorld* World);
static void BoBEvaluateTriggers(UWorld* World);
void BoBResetFlags();
void BoBSetFlag(FName Flag, bool bValue);
bool BoBGetFlag(FName Flag);

// 触发器巡检的计时器与累计量。文件级静态而不是成员：新增 UPROPERTY 要动 .h，
// 那就得关编辑器整编，而编辑器正开着。
static FTimerHandle GFlagTimer;
static float GVigilSeconds = 0.0f;      // 在天坑口凝视界隙的累计秒数
static float GWoundedSeconds = 0.0f;    // 低血量下的累计秒数

static void ScheduleBoBLoot(UWorld* World, int32 GateWave);

// 开局要等所有人关掉简报才放第一波。自动化测试没有人去点那一下，
// 所以留这个开关跳过就绪确认——ECVF_Cheat，正式对局里改不动
static TAutoConsoleVariable<int32> CVarBoBAutoReady(
	TEXT("BoB.AutoReady"), 0,
	TEXT("1 = 跳过开局就绪确认直接开波（自动化测试用）"),
	ECVF_Cheat);

AWaveManager::AWaveManager()
{
	bReplicates = true;
	bAlwaysRelevant = true;   // 确保 CurrentWave 复制到所有客户端，否则 P2 端一直显示第 0 波
	PrimaryActorTick.bCanEverTick = false;
}

void AWaveManager::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AWaveManager, CurrentWave);
	DOREPLIFETIME(AWaveManager, IntervalEndServerTime);
	DOREPLIFETIME(AWaveManager, bWaitingForReady);
	DOREPLIFETIME(AWaveManager, RunEnding);
}

void AWaveManager::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		// 投放节奏交给 Director。关卡里放了就用现成的，没放就自己起一个——
		// World Partition 的关卡改起来动静太大，不值得为一个纯逻辑 Actor 去动
		if (bUseDirector && !Director)
		{
			for (TActorIterator<ABoBAIDirector> It(GetWorld()); It; ++It)
			{
				Director = *It;
				break;
			}
			if (!Director)
			{
				Director = GetWorld()->SpawnActor<ABoBAIDirector>(
					ABoBAIDirector::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
			}
		}
		if (Director)
		{
			Director->OnDirectorSpawn.AddDynamic(this, &AWaveManager::HandleDirectorSpawn);
			UE_LOG(LogTemp, Log, TEXT("[BoB] Director 就位，投放类 = %s"),
				Director->EnemyClass ? *Director->EnemyClass->GetName() : TEXT("(未配置)"));
		}

		// 失谐触顶巡检，全程常驻（含波间）
		GetWorld()->GetTimerManager().SetTimer(
			ExecutionerTimer, this, &AWaveManager::CheckExecutioner, 0.5f, true);

		// 事件旗标：开局重置，然后 0.5 秒一拍跑触发器巡检。
		// 跟处决者巡检分开，是因为它必须在波间也照跑——隐藏条件大多是在
		// 险区勘探阶段满足的，只在潮次内判定等于永远判不到
		BoBResetFlags();
		{
			FTimerDelegate D;
			UWorld* Wd = GetWorld();
			D.BindLambda([Wd]() { BoBEvaluateTriggers(Wd); });
			GetWorld()->GetTimerManager().SetTimer(GFlagTimer, D, 0.5f, true, 1.0f);
		}

		// 野生同化体：延后播撒 + 定期补员。
		// 延后是因为开局那一帧导航还没建好，ProjectPointToNavigation 基本必失败；
		// 补员是因为玩家清空一片之后，那片地不该一直空着——野外的密度是环境属性，
		// 不是一次性关卡摆放。
		BoBScheduleWildlife(GetWorld());

		// 开局先按第 1 波口径布一次战利品（限额+稀有度门控），玩家出简报就是收紧后的场面
		ScheduleBoBLoot(GetWorld(), 1);

		// 等全体玩家就绪（各自关闭开局简报）后再放第一波
		bWaitingForReady = true;
		GetWorld()->GetTimerManager().SetTimer(
			ReadyPollTimer, this, &AWaveManager::CheckReadyToStart, 0.5f, true);
	}
}

void AWaveManager::CheckReadyToStart()
{
	// 就绪旗挂在玩家角色上：统计所有受玩家控制的 ShooterCharacter，全部就绪才开局。
	// AShooterNPC 与 AShooterCharacter 是兄弟类，不会被这个迭代器枚举到。
	int32 NumPlayers = 0;
	for (TActorIterator<AShooterCharacter> It(GetWorld()); It; ++It)
	{
		AController* C = It->GetController();
		if (!C || !C->IsPlayerController())
		{
			continue;   // 服务器端仅统计仍被玩家控制的角色
		}
		NumPlayers++;
		if (!It->IsReadyToStart() && CVarBoBAutoReady.GetValueOnGameThread() == 0)
		{
			return;
		}
	}
	if (NumPlayers == 0)
	{
		return;
	}
	GetWorld()->GetTimerManager().ClearTimer(ReadyPollTimer);
	bWaitingForReady = false;
	FTimerHandle StartTimer;
	GetWorld()->GetTimerManager().SetTimer(
		StartTimer, this, &AWaveManager::StartNextWave, InitialDelay, false);
}

float AWaveManager::GetIntervalRemaining() const
{
	if (IntervalEndServerTime <= 0.0f)
	{
		return -1.0f;
	}
	if (AGameStateBase* GS = GetWorld() ? GetWorld()->GetGameState() : nullptr)
	{
		return FMath::Max(0.0f, IntervalEndServerTime - GS->GetServerWorldTimeSeconds());
	}
	return -1.0f;
}

int32 AWaveManager::GetAliveEnemyCount() const
{
	int32 Count = 0;
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AShooterNPC> It(World); It; ++It)
		{
			AShooterNPC* NPC = *It;
			// BoB.Show 放的调试假人也是 AShooterNPC，不能算进本波敌人：
			// 算进去 HUD 数字虚高，而且假人不会死，这一波就永远清不掉
			if (NPC->Tags.Contains(FName("BoBDummy")))
			{
				continue;
			}
			// 野生同化体同理，而且更致命：它们常驻地图各处、不会主动送死，
			// 算进来的话每一潮都清不掉，整局卡死。
			// 同化潮只统计"这一回合进攻核心的敌人"——这是潮次的定义，不是优化。
			if (NPC->Tags.Contains(FName("BoBWild")))
			{
				continue;
			}
			if (IsValid(NPC) && !NPC->IsActorBeingDestroyed() && !NPC->IsDead())
			{
				Count++;
			}
		}
	}
	return Count;
}

// ===== 野生同化体的投放参数 =====
// 用文件级常量而不是 UPROPERTY：新增可编辑属性要动 .h，那就得关编辑器整编。
// 这批数值还在试玩调整期，留在 .cpp 里走 LiveCoding 热更改起来快得多。
static constexpr int32 WILD_POPULATION   = 14;      // 场上维持的野生数量
static constexpr float WILD_MIN_RADIUS   = 9000.f;  // 90m：同化潮战场之外才开始有
static constexpr float WILD_MAX_RADIUS   = 24000.f; // 240m：一直铺到图缘
static constexpr float WILD_SEED_DELAY   = 5.0f;    // 开局延后，等导航建好
static constexpr float WILD_RESTOCK_SEC  = 45.0f;   // 补员周期
static FTimerHandle GWildTimer;

/** 场上还活着几只野生的 */
static int32 CountWildAlive(UWorld* World)
{
	int32 N = 0;
	if (World)
	{
		for (TActorIterator<AShooterNPC> It(World); It; ++It)
		{
			AShooterNPC* NPC = *It;
			if (IsValid(NPC) && !NPC->IsActorBeingDestroyed() && !NPC->IsDead()
				&& NPC->Tags.Contains(FName("BoBWild")))
			{
				++N;
			}
		}
	}
	return N;
}

int32 BoBSpawnWildAssimilates(UWorld* World, int32 Count, float MinR, float MaxR);
int32 BoBSpawnAnchorBeasts(UWorld* World, int32 Count);

extern bool GBoBFreezeSpawns;   // BoBAIDirector.cpp：作弊开关，冻结一切投放

/**
 *  已唤醒的封停件数量。结局二的解锁进度。
 *
 *  九十七枚里只要唤醒 9 枚——设定里"封停件是启动件的九倍"，倒过来就是这个数。
 *  唤醒是**主动加难**：每一枚都吃配额（和修掩体、买炮台抢同一个池），并且永久
 *  抬高全场失谐涨速。所以越接近结局二，防线越薄、处决者来得越早。
 *  这就是这张图的联动核心：每前进一步都让守家更难，而守家失败则一切归零。
 */
int32 GBoBSealsAwake = 0;
/** 累计击杀的野生同化体数量。隐藏者的委托靠它验收 */
int32 GBoBWildKills = 0;
/**
 *  玩家在 CS-07 面前用掉了第九十八枚封停件，请求结局三。
 *  由 ShooterCharacter 置起，CheckExecutioner 那一拍收走——
 *  FinishRun 是私有的，跨类只能这么递话。
 */
bool GBoBOverwriteRequested = false;
// extern const, not plain const: at namespace scope a const has INTERNAL linkage by
// default, so the extern declaration on the other side would compile and then fail to
// link. This is the whole reason the first hot-compile failed.
extern const int32 GBoBSealsNeeded;
const int32 GBoBSealsNeeded = 9;

// ============================================================================
//  事件旗标登记表
// ----------------------------------------------------------------------------
//  联动的最小单元：触发器（看不见的条件检测器）判定玩家是否满足了某个动作或状态，
//  满足就竖起一面旗标；旗标是全局的记忆，只有竖起/落下两态。世界的样子由旗标组合决定。
//
//  这里刻意分成两类，因为它们的可见性规则完全相反：
//
//   · 进度门旗标（封停件那一套）——**应该**被玩家看见。锁与钥匙要可读，
//     否则玩家不知道自己在解什么。
//
//   · 隐藏旗标——**永远不提示**。没有 toast、没有 HUD、没有音效。玩家甚至
//     不知道自己刚做的动作会影响结局。终局一次性检查，组合成条件树。
//     所以下面这些旗标的置位处一律不许写任何面向玩家的反馈。
// ============================================================================
static TMap<FName, bool> GBoBFlags;

void BoBSetFlag(FName Flag, bool bValue)
{
	GBoBFlags.Add(Flag, bValue);
}

bool BoBGetFlag(FName Flag)
{
	const bool* P = GBoBFlags.Find(Flag);
	return P && *P;
}

/** 开局把"至今未曾…"这类旗标先竖起来，之后由玩家的行为把它们打落 */
void BoBResetFlags()
{
	GBoBFlags.Empty();
	// 早先这里挂了三面"全程不做某事"的保持型旗标（不回血/不修核心/不杀野生）。
	// 砍掉了：那种条件是熬出来的不是找出来的，玩家既发现不了，达成了也只是忍耐。
	// 隐藏条件应该落在地图里的可交互物上——去找、去碰，而不是全程憋着。
	GBoBSealsAwake = 0;
	GVigilSeconds = 0.0f;
	GWoundedSeconds = 0.0f;
}

/**
 *  触发器巡检。0.5 秒一拍，纯判定，不产生任何玩家可见的反馈。
 *
 *  这里的条件刻意做成"反直觉或极细微"：玩家不会意识到自己正在满足它们。
 *  凝视一根光柱、带伤硬撑、深潜到失谐临界——都不是游戏教过的动作。
 */
static void BoBEvaluateTriggers(UWorld* World)
{
	if (!World) { return; }
	const float DT = 0.5f;

	for (TActorIterator<AShooterCharacter> It(World); It; ++It)
	{
		AShooterCharacter* P = *It;
		if (!IsValid(P)) { continue; }

		const FVector Loc = P->GetActorLocation();

		// 深潜：进到第二层且失谐已顶到 90 以上。这是玩家最不该做的事，
		// 所以它才配当隐藏条件——正常人会先退出去把失谐降下来
		if (Loc.Z < -1500.0f && P->GetGaze() >= 90.0f)
		{
			BoBSetFlag(FName("DeepDescent"), true);
		}

		// 守夜：在天坑口正对界隙光柱不动。没有任何提示说明这件事有意义
		for (TActorIterator<AActor> BIt(World); BIt; ++BIt)
		{
			if (!BIt->ActorHasTag(FName("BoBSealBeacon"))) { continue; }
			const FVector To = BIt->GetActorLocation() - Loc;
			if (To.SizeSquared() > FMath::Square(5200.0f)) { break; }
			const FVector Look = P->GetControlRotation().Vector();
			if (FVector::DotProduct(Look, To.GetSafeNormal()) > 0.965f)
			{
				GVigilSeconds += DT;
				if (GVigilSeconds >= 12.0f) { BoBSetFlag(FName("Vigil"), true); }
			}
			break;
		}

		// 抗命：主动把失谐顶到 100 把处决者引出来，然后活着熬到它被放逐。
		//
		// 这一面旗刻意不靠新增收集物，而是**用既有规则做一件反常的事**——
		// 游戏教你的是"失谐要压住、处决者要躲"，这里反过来。调研里几个真正
		// 站得住的隐藏结局都是这个形状（Hades 要你第四次故意送死），
		// 而不是再摆一排可交互物。
		{
			static bool bSawExecutioner = false;
			bool bAnyNow = false;
			for (TActorIterator<ABoBExecutioner> EIt(World); EIt; ++EIt)
			{
				if (IsValid(*EIt)) { bAnyNow = true; break; }
			}
			if (bAnyNow)
			{
				bSawExecutioner = true;
			}
			else if (bSawExecutioner && !P->IsDead())
			{
				BoBSetFlag(FName("Defiance"), true);
				bSawExecutioner = false;
			}
		}

		// 三处遗留集齐才算一面旗。分开放在三个不同的地貌省份，
		// 逼玩家真的把图走一遍，而不是在核心边上蹲着
		int32 Found = 0;
		for (int32 i = 1; i <= 3; ++i)
		{
			if (BoBGetFlag(FName(*FString::Printf(TEXT("Relic%d"), i)))) { Found++; }
		}
		if (Found >= 3) { BoBSetFlag(FName("Testimony"), true); }

		// 天坑光束：亮度报封停件进度（结局二），色相报线索集齐度（结局三）。
		// 两条进度共用一根光柱，因为它是唯一同时能被 F0 里和地表上的人看到的东西。
		for (TActorIterator<AActor> BIt(World); BIt; ++BIt)
		{
			if (!BIt->ActorHasTag(FName("BoBSealBeacon"))) { continue; }
			UPointLightComponent* L = BIt->FindComponentByClass<UPointLightComponent>();
			if (!L) { break; }
			// 0/3 暖白 → 1/3 微青 → 2/3 青 → 3/3 冷白偏蓝
			static const FColor Hue[4] = {
				FColor(255, 228, 186, 255), FColor(214, 232, 214, 255),
				FColor(168, 232, 236, 255), FColor(196, 226, 255, 255)
			};
			const FColor Want = Hue[FMath::Clamp(Found, 0, 3)];
			if (L->LightColor != Want)
			{
				L->SetLightColor(FLinearColor(Want));
				UE_LOG(LogTemp, Log, TEXT("[BoB] 天坑光束色相切换：线索 %d/3"), Found);
			}
			break;
		}

		// 隐藏者：玩家把它移出视锥之后换一次姿态。
		// 判定用相机朝向的点积而不是引擎的 WasRecentlyRendered——后者受遮挡和
		// 距离剔除影响，玩家明明背过身它却还算"可见"，姿态就永远不变。
		for (TActorIterator<AActor> HIt(World); HIt; ++HIt)
		{
			if (!HIt->ActorHasTag(FName("BoBHermit"))) { continue; }
			AActor* H = *HIt;
			const FVector To = H->GetActorLocation() - Loc;
			if (To.SizeSquared() > FMath::Square(9000.0f)) { break; }
			const bool bInView =
				FVector::DotProduct(P->GetControlRotation().Vector(), To.GetSafeNormal()) > 0.55f;
			const bool bWasSeen = H->ActorHasTag(FName("HermitSeen"));
			if (bInView && !bWasSeen)
			{
				H->Tags.Add(FName("HermitSeen"));
			}
			else if (!bInView && bWasSeen)
			{
				H->Tags.Remove(FName("HermitSeen"));
				// 转过去一个角度。没有任何文本提这件事
				FRotator R = H->GetActorRotation();
				R.Yaw = FMath::UnwindDegrees(R.Yaw + 57.0f);
				H->SetActorRotation(R);
			}
			break;
		}
	}
}

/** 补到目标密度。清掉一片之后那片地不该一直空着 */
static void BoBRestockWildlife(UWorld* World)
{
	// 冻结要连野生补员一起停，否则"暂停出怪"之后地图还在慢慢长怪，
	// 那就不叫暂停了
	if (GBoBFreezeSpawns) { return; }
	const int32 Alive = CountWildAlive(World);
	const int32 Need = WILD_POPULATION - Alive;
	if (Need > 0)
	{
		BoBSpawnWildAssimilates(World, Need, WILD_MIN_RADIUS, WILD_MAX_RADIUS);
	}

	// 锚点体常驻两只。补员而不是一次性投放：打掉之后隔一阵还会有，
	// 否则第一波清掉就再也没有了，"要不要去打"这个决策只出现一次
	int32 Anchors = 0;
	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		AShooterNPC* N = *It;
		if (IsValid(N) && !N->IsDead() && N->Tags.Contains(FName("BoBAnchor"))) { Anchors++; }
	}
	if (Anchors < 2)
	{
		BoBSpawnAnchorBeasts(World, 2 - Anchors);
	}
}

/** 开局延后播撒，随后周期补员 */
static void BoBScheduleWildlife(UWorld* World)
{
	if (!World) { return; }
	FTimerDelegate Tick;
	Tick.BindLambda([World]() { BoBRestockWildlife(World); });
	World->GetTimerManager().SetTimer(GWildTimer, Tick, WILD_RESTOCK_SEC, true, WILD_SEED_DELAY);
}

/**
 *  野生同化体：散布在远处各类地形上的环境敌人，不属于任何一次同化潮。
 *
 *  它们不啃核心、不进潮次计数（见 GetAliveEnemyCount 的 BoBWild 跳过）、不被逼近就不动手。
 *  存在的意义是让"险区勘探"真的有险：出门不再只是跑图捡东西，而是要判断哪一只惹得起。
 *  同时这也是地图多样性唯一能直接换成玩法的地方——不同地形放不同型号，
 *  玩家看地貌就知道那边有什么。
 *
 *  非 static：BoBTest.cpp 的 BoB.Wild 指令要 extern 它，省得把同一段抄两遍。
 */
int32 BoBSpawnWildAssimilates(UWorld* World, int32 Count, float MinR, float MaxR)
{
	if (!World || Count <= 0)
	{
		return 0;
	}

	const TArray<FName> Ids = UBoBEnemyLib::AllEnemyIds();
	if (Ids.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BoB] 野生投放失败：型号表是空的"));
		return 0;
	}

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	FVector CoreAt = FVector::ZeroVector;
	if (AActor* Core = UGameplayStatics::GetActorOfClass(World, ABaseCore::StaticClass()))
	{
		CoreAt = Core->GetActorLocation();
	}

	// 计分/配额链路挂到 WaveManager 上。用名字绑定而不是 AddDynamic：
	// AddDynamic 是宏，展开后要直接取 &AWaveManager::HandleEnemyDeath，
	// 而那是私有成员，自由函数取不到。BindUFunction 走反射按名字查，
	// 既拿得到又不必为此改 .h 触发整编
	AWaveManager* WM = Cast<AWaveManager>(
		UGameplayStatics::GetActorOfClass(World, AWaveManager::StaticClass()));

	int32 Made = 0;
	// 给足重试次数：投放点要同时满足"在可走面上"和"不贴玩家"，
	// 命中率本来就不高，宁可多试也不要降低标准把敌人塞进石头里
	for (int32 Try = 0; Try < Count * 30 && Made < Count; ++Try)
	{
		const float Ang = FMath::FRandRange(0.f, 360.f);
		const float Rad = FMath::FRandRange(MinR, MaxR);
		FVector Want = CoreAt + FRotator(0.f, Ang, 0.f).Vector() * Rad;

		FNavLocation Projected;
		if (!Nav || !Nav->ProjectPointToNavigation(Want, Projected, FVector(1500.f, 1500.f, 3000.f)))
		{
			continue;
		}
		Want = Projected.Location;

		// 别在玩家脸上生成——野生的卖点是"你先看见它"，不是突然出现
		bool bTooClose = false;
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				if (APawn* P = PC->GetPawn())
				{
					if (FVector::Dist2D(P->GetActorLocation(), Want) < 2500.f)
					{
						bTooClose = true;
						break;
					}
				}
			}
		}
		if (bTooClose)
		{
			continue;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		// 抬到胶囊半高，贴着导航面生成会卡进地里（跟 Director 同一个坑）
		ABoBEnemy* E = World->SpawnActor<ABoBEnemy>(ABoBEnemy::StaticClass(),
			Want + FVector(0.f, 0.f, 95.f),
			FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params);
		if (!E)
		{
			continue;
		}

		// 各种各样：21 个型号里直接随机抽，不按潮次预算筛。
		// 野外遇到什么本来就不该受"这是第几潮"约束——那是同化潮的规则，不是这片地的规则
		if (!E->ApplyEnemyRow(Ids[FMath::RandRange(0, Ids.Num() - 1)]))
		{
			E->Destroy();
			continue;
		}
		E->InitCombatBaseline();

		E->Tags.Add(FName("BoBWild"));

		// 击杀回报走现成的结算链路：配额是按得分折算的，抬基础分就一并变厚，
		// 不必新开一条奖励通道。野生是可选风险，回报必须明显高于潮次杂兵，
		// 否则没人有理由去招惹它们，这套系统就白做了。
		E->ScoreValue = FMath::RoundToInt(E->ScoreValue * 2.5f) + 15;

		// 不挂这条，击杀野生既不给分也不给配额——奖励规则写了也不会生效
		if (WM)
		{
			FScriptDelegate Score;
			Score.BindUFunction(WM, FName("HandleEnemyDeath"));
			E->OnPawnDeathWithKiller.Add(Score);
		}

		++Made;
	}

	UE_LOG(LogTemp, Log, TEXT("[BoB] 野生同化体投放 %d/%d（半径 %.0f-%.0f）"),
		Made, Count, MinR, MaxR);
	return Made;
}


/**
 *  锚点体：野生里的一档"奖励敌人"。
 *
 *  外形上把同化锚点拉满（体型 ×1.7、晶体覆盖与自发光顶格），所以隔老远就认得出——
 *  玩家自己判断打不打得过，这个判断本身就是玩法。它同样不碰核心、不进潮次计数，
 *  但比普通野生硬得多，击败掉**结局三的凭证**之一。
 *
 *  放两只而不是一只：一只的话运气成分太重，两只才谈得上"挑一只打"。
 */
int32 BoBSpawnAnchorBeasts(UWorld* World, int32 Count)
{
	if (!World || Count <= 0) { return 0; }
	const TArray<FName> Ids = UBoBEnemyLib::AllEnemyIds();
	if (Ids.Num() == 0) { return 0; }

	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	AWaveManager* WM = Cast<AWaveManager>(
		UGameplayStatics::GetActorOfClass(World, AWaveManager::StaticClass()));

	int32 Made = 0;
	for (int32 Try = 0; Try < Count * 40 && Made < Count; ++Try)
	{
		const float Ang = FMath::FRandRange(0.f, 360.f);
		const float Rad = FMath::FRandRange(13000.f, 23000.f);   // 远，但不到图缘
		FVector Want = FRotator(0.f, Ang, 0.f).Vector() * Rad;
		FNavLocation Proj;
		if (!Nav || !Nav->ProjectPointToNavigation(Want, Proj, FVector(1800.f, 1800.f, 3500.f)))
		{
			continue;
		}
		Want = Proj.Location;

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
		ABoBEnemy* E = World->SpawnActor<ABoBEnemy>(ABoBEnemy::StaticClass(),
			Want + FVector(0.f, 0.f, 140.f),
			FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), Params);
		if (!E) { continue; }
		if (!E->ApplyEnemyRow(Ids[FMath::RandRange(0, Ids.Num() - 1)]))
		{
			E->Destroy();
			continue;
		}
		E->InitCombatBaseline();

		E->Tags.Add(FName("BoBWild"));
		E->Tags.Add(FName("BoBAnchor"));
		E->Tags.Add(FName("BoBDiscovery"));   // 靠近才在小地图亮感叹号，不开局标答案

		// 一眼认得出：体型、血量、分值全部拉开
		E->SetActorScale3D(E->GetActorScale3D() * 1.7f);
		E->MaxHP *= 4.5f;
		E->CurrentHP = E->MaxHP;
		E->ScoreValue = E->ScoreValue * 6 + 120;

		if (WM)
		{
			FScriptDelegate Score;
			Score.BindUFunction(WM, FName("HandleEnemyDeath"));
			E->OnPawnDeathWithKiller.Add(Score);
		}
		++Made;
	}
	UE_LOG(LogTemp, Log, TEXT("[BoB] 锚点体投放 %d/%d"), Made, Count);
	return Made;
}

// 战利品调度（开局与每次清波开窗时执行，服务器权威）：
// ① 稀有度分波解锁：珍品(2格)第3波起、遗物(3格)第5波起
// ② 每个搜刮窗口只随机激活池中一部分点位（宝物/武器分开限额），其余隐藏——供给收紧、位置每波都换
// ③ 被玩家拿走且不可重刷的点位永久退场；GateHidden 标签=被调度隐藏（区别于被拿走）
static void ScheduleBoBLoot(UWorld* World, int32 GateWave)
{
	static const FName GateTag(TEXT("GateHidden"));

	// ===== 测试模式：全点位常刷、无视稀有度/限额（交付前必须改回 false）=====
	constexpr bool bBoBLootTestMode = false;
	if (bBoBLootTestMode)
	{
		for (TActorIterator<ALootPickup> TIt(World); TIt; ++TIt)
		{
			if (TIt->Tags.Contains(GateTag))
			{
				TIt->Tags.Remove(GateTag);
				TIt->SetTaken(false);
			}
			else if (TIt->bTaken && GetLootDef(TIt->Kind).bRespawns)
			{
				TIt->SetTaken(false);
			}
		}
		return;
	}

	constexpr int32 MaxActiveGems = 5;      // 每窗在场宝物点上限
	constexpr int32 MaxActiveWeapons = 4;   // 每窗在场武器点上限

	TArray<ALootPickup*> Gems, Weapons;
	for (TActorIterator<ALootPickup> LIt(World); LIt; ++LIt)
	{
		const FLootDef& Def = GetLootDef(LIt->Kind);
		const bool bSchedulerHidden = LIt->Tags.Contains(GateTag);
		if (LIt->bTaken && !bSchedulerHidden && !Def.bRespawns)
		{
			continue;   // 已被拿走且不可重刷 → 永久退场
		}
		// 狙击枪箱=传世级武器，与 3 格宝物同为第 5 波解锁
		const int32 MinWave = LIt->Tags.Contains(FName("BoBWpnS")) ? 5
			: (Def.Slots >= 3 ? 5 : (Def.Slots == 2 ? 3 : 1));
		if (GateWave < MinWave)
		{
			LIt->Tags.AddUnique(GateTag);
			LIt->SetTaken(true);
			continue;
		}
		(LIt->Kind == ELootKind::WeaponMod ? Weapons : Gems).Add(*LIt);
	}

	auto Activate = [](TArray<ALootPickup*>& Pool, int32 MaxActive)
	{
		for (ALootPickup* P : Pool)
		{
			P->Tags.AddUnique(GateTag);
			P->SetTaken(true);
		}
		for (int32 i = 0; i < Pool.Num(); ++i)
		{
			Pool.Swap(i, FMath::RandRange(i, Pool.Num() - 1));
		}
		// 同类每窗最多 2 个（防"满地都是矿石"），名额不满再放开补齐
		TMap<uint8, int32> KindCount;
		TArray<ALootPickup*> Chosen;
		for (ALootPickup* P : Pool)
		{
			if (Chosen.Num() >= MaxActive)
			{
				break;
			}
			int32& C = KindCount.FindOrAdd(static_cast<uint8>(P->Kind));
			if (C >= 2)
			{
				continue;
			}
			C++;
			Chosen.Add(P);
		}
		for (ALootPickup* P : Pool)
		{
			if (Chosen.Num() >= MaxActive)
			{
				break;
			}
			Chosen.AddUnique(P);
		}
		for (ALootPickup* P : Chosen)
		{
			P->Tags.Remove(GateTag);
			P->SetTaken(false);
		}
	};
	Activate(Gems, MaxActiveGems);
	Activate(Weapons, MaxActiveWeapons);
}

void AWaveManager::StartNextWave()
{
	IntervalEndServerTime = -1.0f;   // 波间结束
	CurrentWave++;

	// 超过最大波次 → 胜利
	if (CurrentWave > MaxWave)
	{
		OnAllWavesComplete.Broadcast();
		return;
	}

	FWaveConfig Config = BuildWaveConfig(CurrentWave);

	OnWaveStart.Broadcast(CurrentWave);

	// 新一波开始：清空本波商店购买记录与临时增益(信号抑制器只保一波)，并关掉玩家的商店面板
	for (TActorIterator<AShooterCharacter> ChIt(GetWorld()); ChIt; ++ChIt)
	{
		ChIt->ResetShopForNewWave();
		ChIt->bShopOpen = false;
	}

	if (IsDirectorDriven())
	{
		// 预算积分制：开波时一个都不放，投放沿整潮铺开
		Director->BeginWave(CurrentWave);
		UE_LOG(LogTemp, Log, TEXT("[BoB] TIDE %d 开始，预算 %.0f"),
			CurrentWave, Director->WaveBudget(CurrentWave));
	}
	else
	{
		const int32 Spawned = SpawnEnemies(Config);
		UE_LOG(LogTemp, Log, TEXT("TIDE %d 开始，生成 %d 个敌人"), CurrentWave, Spawned);
	}

	// TIDE 10：CS-07 上线。它不走 Director 的预算池（Role=Boss 在 AffordableAt 里被排除），
	// 由这里直接放，且**不计入存活敌人数**——契约 C7 说玩家打不死它，
	// 算进去这一潮就永远清不掉，撤离条件是倒计时不是击杀
	if (IsBossWave(CurrentWave))
	{
		SpawnBoss();
	}

	BeginWaveStat();
	bWaveActive = true;

	// 启动循环监控
	GetWorld()->GetTimerManager().SetTimer(
		MonitorTimer, this, &AWaveManager::MonitorWave, MonitorInterval, true);
}

void AWaveManager::MonitorWave()
{
	const int32 Alive = GetAliveEnemyCount();

	// Director 模式下开波瞬间场上是空的，光看存活数会立刻误判清波，
	// 所以要等它把预算花完（或时间到）才允许结算
	const bool bStillSpawning = IsDirectorDriven() && Director->IsWaveActive();

	if (bWaveActive && Alive <= 0 && !bStillSpawning)
	{
		bWaveActive = false;
		if (Director)
		{
			Director->EndWave();
		}
		GetWorld()->GetTimerManager().ClearTimer(MonitorTimer);
		OnWaveCleared();
	}
}

void AWaveManager::SpawnBoss()
{
	UWorld* World = GetWorld();
	if (!World || Boss) { return; }

	// 摆在核心对面：玩家守核心时抬头就能看见那座塔
	FVector At(0.f, 0.f, 0.f);
	if (TActorIterator<ABaseCore> It(World); It)
	{
		At = It->GetActorLocation() + FVector(-1800.f, 0.f, 0.f);
	}

	// 核心是浮空的，直接沿用它的 Z 会让塔碑悬在半空或半截埋进地里。
	// 往下打一条射线找地面，再抬起胶囊半高（500）把它稳稳放上去
	{
		FHitResult Hit;
		FCollisionQueryParams Q(SCENE_QUERY_STAT(BoBBossDrop), false);
		if (World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 3000.f),
			At - FVector(0.f, 0.f, 5000.f), ECC_WorldStatic, Q))
		{
			At = Hit.ImpactPoint + FVector(0.f, 0.f, 500.f);
		}
	}

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Boss = World->SpawnActor<ABoss_CS07>(ABoss_CS07::StaticClass(), At, FRotator::ZeroRotator, SP);
	if (!Boss)
	{
		UE_LOG(LogTemp, Error, TEXT("[BoB] CS-07 生成失败，TIDE 10 会没有 Boss"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[BoB] CS-07 就位于 %s"), *At.ToCompactString());
	Boss->OnExtractionReady.AddDynamic(this, &AWaveManager::HandleBossExtractionReady);
	Boss->OnBossDefeated.AddDynamic(this, &AWaveManager::HandleBossDefeated);
	Boss->OnWrongPillar.AddDynamic(this, &AWaveManager::HandleWrongPillar);
	Boss->Activate();
}

void AWaveManager::DebugJumpToWave(int32 N)
{
	if (!HasAuthority()) { return; }
	UWorld* World = GetWorld();

	// 把当前潮的残留清干净，否则新一潮的清潮判定会被上一潮的怪卡住。
	// 野生同化体留着：它们本来就不进清潮判定，而且属于地图的环境层，
	// 不该被一次调试跳潮抹掉——抹掉了还会立刻补员，白折腾一轮
	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		if (It->Tags.Contains(FName("BoBDummy")) || It->Tags.Contains(FName("BoBWild")))
		{
			continue;
		}
		It->Destroy();
	}
	World->GetTimerManager().ClearTimer(MonitorTimer);
	bWaveActive = false;
	bWaitingForReady = false;
	World->GetTimerManager().ClearTimer(ReadyPollTimer);

	// StartNextWave 会自增，所以这里减一
	CurrentWave = FMath::Max(0, N - 1);
	StartNextWave();
}

static float BoBCoreHP(UWorld* W)
{
	for (TActorIterator<ABaseCore> It(W); It; ++It) { return It->GetBaseHP(); }
	return 0.f;
}

void AWaveManager::BeginWaveStat()
{
	FWaveStat S;
	S.Wave = CurrentWave;
	S.CoreAtStart = BoBCoreHP(GetWorld());
	WaveStats.Add(S);
	WaveStartTime = GetWorld()->GetTimeSeconds();
}

void AWaveManager::EndWaveStat()
{
	if (WaveStats.Num() == 0) { return; }
	FWaveStat& S = WaveStats.Last();
	S.CoreAtEnd = BoBCoreHP(GetWorld());
	S.Seconds = GetWorld()->GetTimeSeconds() - WaveStartTime;
	if (Director) { S.Spawned = Director->GetLog().Num(); }

	UE_LOG(LogTemp, Log, TEXT("[BoB战报] ===== 整局核心曲线 ====="));
	UE_LOG(LogTemp, Log, TEXT("[BoB战报] 潮次  投放  用时   核心(始->终)  本潮掉血"));
	for (const FWaveStat& X : WaveStats)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[BoB战报] %4d  %4d  %4.0fs  %5.0f -> %-5.0f  %.0f"),
			X.Wave, X.Spawned, X.Seconds, X.CoreAtStart, X.CoreAtEnd,
			X.CoreAtStart - X.CoreAtEnd);
	}
}

void AWaveManager::CheckExecutioner()
{
	UWorld* World = GetWorld();
	if (!World) { return; }

	// 结局三的收口。挂在这个已有的 0.5 秒巡检上，而不是新开一个定时器
	if (GBoBOverwriteRequested && RunEnding == 0)
	{
		GBoBOverwriteRequested = false;
		FinishRun(false);
		return;
	}

	// 场上已经有一个就不再派。规格书是"降临"不是"成群"，
	// 两个处决者同时穿墙直冲，玩家没有任何可读性可言
	for (TActorIterator<ABoBExecutioner> It(World); It; ++It)
	{
		if (IsValid(*It)) { return; }
	}

	for (TActorIterator<AShooterCharacter> It(World); It; ++It)
	{
		AShooterCharacter* C = *It;
		if (!C->GetController() || C->IsDead()) { continue; }
		if (C->GetGaze() < 100.0f) { continue; }

		// 从玩家背后一段距离外出现，别糊在脸上
		const FVector Behind = C->GetActorLocation()
			- C->GetActorForwardVector() * 1600.0f + FVector(0.0f, 0.0f, 60.0f);
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (ABoBExecutioner* E = World->SpawnActor<ABoBExecutioner>(
			ABoBExecutioner::StaticClass(), Behind, FRotator::ZeroRotator, SP))
		{
			E->Hunt(C);
		}
		return;
	}
}

void AWaveManager::HandleWrongPillar()
{
	// 罚则走 Director 的正规投放口，才会被存活统计和压力阀看见。
	// 直接在柱子里 SpawnActor 的话，这几只是"账外"的，清潮判定会漏算
	if (Director)
	{
		Director->RequestReinforcement(FName("Pilgrim"), WrongPillarPenalty);
	}
}

void AWaveManager::HandleBossExtractionReady()
{
	FinishRun(false);   // 结局一 · 坚守成功
}

void AWaveManager::HandleBossDefeated()
{
	FinishRun(true);    // 结局二 · 击败 CS-07
}

void AWaveManager::FinishRun(bool bBossDefeated)
{
	if (RunEnding != 0) { return; }   // 两条路都可能触发，只认第一个
	RunEnding = bBossDefeated ? 2 : 1;
	UWorld* World = GetWorld();

	// ---- 结局三 · 协议覆写：拿第九十八枚插进阵列，是显式选择，不走旗标统计 ----
	if (BoBGetFlag(FName("Seal98")))
	{
		RunEnding = 3;
		UE_LOG(LogTemp, Log, TEXT("[BoB] 结局三 · 协议覆写（第九十八枚已插入阵列）"));
	}

	// ---- 隐藏分支：终局一次性检查隐藏旗标，组合成条件树 ----
	// 全程没有任何提示告诉玩家这些条件存在。到这一刻才结算，玩家回头才会
	// 意识到"原来那次我盯着光柱发呆是有意义的"。
	{
		// 通关时身上还带着没存放的宝物：你没把东西交出去
		bool bUnbanked = false;
		for (TActorIterator<AShooterCharacter> It(World); It; ++It)
		{
			if (It->GetCarried().Num() > 0) { bUnbanked = true; break; }
		}
		BoBSetFlag(FName("Unbanked"), bUnbanked);

		// 三面来自主动行为，三面来自地图上的可交互物。都不提示，终局才一次性算
		static const FName Secret[] = {
			FName("DeepDescent"),   // 在第二层里把失谐顶到 90 以上
			FName("Vigil"),         // 在天坑口对着界隙站满 12 秒
			FName("Unbanked"),      // 通关时身上还留着没交的宝物
			FName("Glyph"),         // 读过原住刻纹壁
			FName("Testimony"),     // 集齐三处前批外来者的遗留
			FName("Resonance"),     // 在共鸣柱上做过一次谐振
			FName("Anchor"),        // 击败过锚点体
			FName("Hermit"),        // 完成过隐藏者的委托
			FName("Defiance")       // 引出处决者并活着熬到它被放逐
		};
		int32 Met = 0;
		FString Which;
		for (const FName& F : Secret)
		{
			if (BoBGetFlag(F)) { Met++; Which += F.ToString() + TEXT(" "); }
		}
		// 旗标统计只在没走覆写线时才有资格改结局：结局三应该由玩家的显式选择决定，
		// 不该被"凑够四面"顶掉，否则那个选项就白做了
		if (Met >= 4 && RunEnding != 3)
		{
			RunEnding = 3;
		}
		UE_LOG(LogTemp, Log, TEXT("[BoB] 隐藏旗标 %d/7 [%s] -> 结局 %d"),
			Met, *Which, RunEnding);
	}

	UE_LOG(LogTemp, Log, TEXT("[BoB] 本局结束：%s"),
		RunEnding == 3 ? TEXT("隐藏结局") :
		(bBossDefeated ? TEXT("结局二 · 击败 CS-07") : TEXT("结局一 · 坚守成功")));

	// 强制清场。不清的话结算画面背后还有同化体在啃核心，
	// 玩家看着结算文字听着核心告警，观感很怪
	int32 Swept = 0;
	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		if (!It->Tags.Contains(FName("BoBDummy"))) { It->Destroy(); Swept++; }
	}
	for (TActorIterator<ABoBExecutioner> It(World); It; ++It) { It->Destroy(); }
	UE_LOG(LogTemp, Log, TEXT("[BoB] 收尾清场 %d 个同化体"), Swept);

	bWaveActive = false;
	World->GetTimerManager().ClearTimer(MonitorTimer);
	World->GetTimerManager().ClearTimer(ExecutionerTimer);
	if (Director) { Director->EndWave(); }
	EndWaveStat();
	OnAllWavesComplete.Broadcast();

	// 留 2.5 秒让结算横幅出来再定格，立刻暂停的话玩家什么都没看清
	FTimerHandle Freeze;
	World->GetTimerManager().SetTimer(Freeze, FTimerDelegate::CreateLambda([World]()
	{
		UGameplayStatics::SetGamePaused(World, true);
	}), 2.5f, false);
}

void AWaveManager::HandleDirectorSpawn(FName EnemyId, ABoBEnemy* Enemy)
{
	if (!Enemy) { return; }
	// 型号属性 Director 已经按表配好，这里只补上计分链路
	Enemy->OnPawnDeathWithKiller.AddDynamic(this, &AWaveManager::HandleEnemyDeath);
}

void AWaveManager::OnWaveCleared()
{
	EndWaveStat();
	OnWaveComplete.Broadcast(CurrentWave);


	if (CurrentWave >= MaxWave)
	{
		// ===== 撤离演出：救援直升机穿过穹顶天窗降到核心旁，落地稍候后统一结算 =====
		UWorld* World = GetWorld();
		ASkeletalMeshActor* Heli = nullptr;
		if (USkeletalMesh* HeliMesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/FabAssets/MH6/Helicopter.Helicopter")))
		{
			FActorSpawnParameters SP;
			SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Heli = World->SpawnActor<ASkeletalMeshActor>(ASkeletalMeshActor::StaticClass(), FVector(180.0f, 0.0f, 2950.0f), FRotator::ZeroRotator, SP);
			if (Heli)
			{
				Heli->SetReplicates(true);
				Heli->SetReplicateMovement(true);
				Heli->Tags.Add(FName("BoBHeli"));
				if (USkeletalMeshComponent* SMC = Heli->GetSkeletalMeshComponent())
				{
					SMC->SetSkeletalMesh(HeliMesh);
					SMC->SetCollisionEnabled(ECollisionEnabled::NoCollision);
					// Sketchfab 模型尺度不可控：按包围盒归一成 ~9m 机身
					const float MaxXY = FMath::Max(HeliMesh->GetBounds().BoxExtent.X, HeliMesh->GetBounds().BoxExtent.Y) * 2.0f;
					if (MaxXY > 1.0f)
					{
						Heli->SetActorScale3D(FVector(900.0f / MaxXY));
					}
					if (UAnimSequence* Anim = LoadObject<UAnimSequence>(nullptr, TEXT("/Game/FabAssets/MH6/Helicopter_Anim.Helicopter_Anim")))
					{
						SMC->PlayAnimation(Anim, true);   // 旋翼动画（客户端由 HUD 侧按 BoBHeli 标签补装补播）
					}
				}
			}
		}

		// 降落插值（先快后慢），落地 2.5s 后结算；模型缺失时短延时直接结算兜底
		const float Dur = Heli ? 12.0f : 0.5f;
		TSharedPtr<float> Alpha = MakeShared<float>(0.0f);
		TSharedPtr<FTimerHandle> Tick = MakeShared<FTimerHandle>();
		TWeakObjectPtr<AWaveManager> WeakThis(this);
		TWeakObjectPtr<ASkeletalMeshActor> WeakHeli(Heli);
		World->GetTimerManager().SetTimer(*Tick, FTimerDelegate::CreateLambda([WeakThis, WeakHeli, Alpha, Tick, Dur]()
		{
			AWaveManager* Self = WeakThis.Get();
			if (!Self)
			{
				return;
			}
			*Alpha = FMath::Min(*Alpha + 0.033f / Dur, 1.0f);
			if (ASkeletalMeshActor* H = WeakHeli.Get())
			{
				const float Ease = 1.0f - FMath::Pow(1.0f - *Alpha, 2.2f);
				H->SetActorLocation(FMath::Lerp(FVector(180.0f, 0.0f, 2950.0f), FVector(700.0f, 0.0f, -2.0f), Ease));
			}
			if (*Alpha >= 1.0f)
			{
				UWorld* W = Self->GetWorld();
				W->GetTimerManager().ClearTimer(*Tick);
				FTimerHandle SettleTimer;
				W->GetTimerManager().SetTimer(SettleTimer, FTimerDelegate::CreateLambda([WeakThis]()
				{
					AWaveManager* S = WeakThis.Get();
					if (!S)
					{
						return;
					}
					UWorld* W2 = S->GetWorld();
					// 登机：把每人存入核心的物资折算进个人得分，再触发胜利结算
					for (TActorIterator<AShooterCharacter> ChIt(W2); ChIt; ++ChIt)
					{
						if (APlayerState* PS = ChIt->GetPlayerState())
						{
							PS->SetScore(PS->GetScore() + ChIt->GetBankedValue());
						}
					}
					S->OnAllWavesComplete.Broadcast();
					if (AShooterGameMode* GM = Cast<AShooterGameMode>(W2->GetAuthGameMode()))
					{
						GM->TriggerWin();
					}
				}), 2.5f, false);
			}
		}), 0.033f, true);

		return;
	}

	// ① 中央核心自动升级并回满（守家自动化：基地不再是玩家增益的投入对象，玩家增益专注战斗/PVP）
	if (TActorIterator<ABaseCore> It(GetWorld()); It)
	{
		It->AutoUpgradeAndHeal();
	}

	// ② 波间空档（无敌人）给全体玩家同时发一次三选一增益，避免战斗中选卡卡顿
	OfferUpgradesToAllPlayers();

	// ③ 搜刮窗口开启：重抽本窗激活的战利品点位（按下一波的解锁口径）
	ScheduleBoBLoot(GetWorld(), CurrentWave + 1);

	// 波间总时长 = 补给阶段(45s) + 潜影入场提示(7s) + 潜影探索时限(随波成长)
	// 提示与补给都不占探索时间，HUD 分段显示各自的倒计时
	const float CruiseDur = SupplyPhaseDuration + ShadowCruiseIntro + GetShadowCruiseDuration();

	// 波间倒计时（服务器世界秒，复制给 HUD 显示）
	if (AGameStateBase* GS = GetWorld()->GetGameState())
	{
		IntervalEndServerTime = GS->GetServerWorldTimeSeconds() + CruiseDur;
	}

	UE_LOG(LogTemp, Log, TEXT("[BoB] 险区勘探开启：TIDE %d 之后，时限 %.0f 秒"), CurrentWave, CruiseDur);

	FTimerHandle TimerHandle;
	GetWorld()->GetTimerManager().SetTimer(
		TimerHandle, this, &AWaveManager::OnWaveIntervalEnd, CruiseDur, false);
}

void AWaveManager::ExtendShadowCruise(float Seconds)
{
	if (!HasAuthority() || IntervalEndServerTime <= 0.0f) { return; }
	IntervalEndServerTime += Seconds;
	// 同步顺延波次开始定时器
	if (UWorld* W = GetWorld())
	{
		const float Remain = GetIntervalRemaining();
		FTimerHandle NewTimer;
		W->GetTimerManager().SetTimer(NewTimer, this, &AWaveManager::OnWaveIntervalEnd, FMath::Max(0.1f, Remain), false);
	}
}

void AWaveManager::OfferUpgradesToAllPlayers()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}
		APawn* Pawn = PC->GetPawn();
		if (!Pawn)
		{
			continue;
		}
		if (UExperienceComponent* Exp = Pawn->FindComponentByClass<UExperienceComponent>())
		{
			// Client RPC：只在拥有该角色的客户端弹卡（host 本地立即执行）
			Exp->Client_OfferUpgrade();
		}
	}
}

void AWaveManager::OnWaveIntervalEnd()
{
	StartNextWave();
}

void AWaveManager::HandleEnemyDeath(AController* Killer, int32 ScoreValue)
{
	if (!Killer)
	{
		return;
	}

	APawn* KillerPawn = Killer->GetPawn();
	if (!KillerPawn)
	{
		return;
	}

	// 加经验
	if (UExperienceComponent* Exp = KillerPawn->FindComponentByClass<UExperienceComponent>())
	{
		Exp->AddExperience(BaseExpPerKill);
	}

	// 连击倍率
	float ComboMult = 1.0f;
	if (AShooterCharacter* SC = Cast<AShooterCharacter>(KillerPawn))
	{
		SC->AddCombo();
		ComboMult = SC->GetComboMultiplier();
	}

	// 得分 = 该丧尸基础分值 × 连击倍率 × 得分增益
	float ScoreF = ScoreValue * ComboMult;
	bool bCritScore = false;
	if (UUpgradeComponent* Up = KillerPawn->FindComponentByClass<UUpgradeComponent>())
	{
		// 赏金猎人：得分 +30%/层
		ScoreF *= (1.0f + 0.30f * Up->GetUpgradeCount(EUpgradeType::ScoreBoost));
		// 致命彩头：每层 25% 概率本次击杀得分翻倍
		bCritScore = Up->HasUpgrade(EUpgradeType::CritScore) &&
			FMath::FRand() < FMath::Min(0.25f * Up->GetUpgradeCount(EUpgradeType::CritScore), 1.0f);
	}
	int32 GainedScore = FMath::Max(1, FMath::RoundToInt(ScoreF));
	if (bCritScore)
	{
		GainedScore *= 2;
	}

	// 加分：单人 HUD 用的累计分
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->AddPlayerScore(GainedScore);
	}

	// 多人同场竞技：把分数计入击杀者自己的 PlayerState（内置复制，供计分板各自比高低）
	if (APlayerState* KillerPS = Killer->PlayerState)
	{
		KillerPS->SetScore(KillerPS->GetScore() + GainedScore);

		// 余烬掉落：击杀获得花销货币（按得分折算，精英/Boss 波更肥）
		if (ABODPlayerState* BPS = Cast<ABODPlayerState>(KillerPS))
		{
			const int32 KindMul = (GetWaveKind(CurrentWave) == 2) ? 3 : ((GetWaveKind(CurrentWave) == 1) ? 2 : 1);
			// 拾荒者协议：本波余烬收益倍率
			float GainMul = 1.0f;
			if (const AShooterCharacter* KC = Cast<AShooterCharacter>(Killer->GetPawn()))
			{
				GainMul = KC->GetCinderGainMul();
			}
			const int32 Gain = FMath::Max(1, FMath::RoundToInt(GainedScore * 0.6f * GainMul)) * KindMul;
			BPS->AddCinder(Gain);
		}
	}
}

FWaveConfig AWaveManager::BuildWaveConfig(int32 WaveNumber) const
{
	FWaveConfig Config;
	Config.EnemyCount       = 8 + (WaveNumber - 1) * 4;
	Config.SpeedMultiplier  = FMath::Min(1.0f + (WaveNumber - 1) * 0.05f, 1.5f);
	Config.HealthMultiplier = 1.0f + (WaveNumber - 1) * 0.15f;
	Config.ExpPerKill       = BaseExpPerKill + WaveNumber * 2;
	return Config;
}

int32 AWaveManager::SpawnEnemies(const FWaveConfig& Config)
{
	int32 SpawnedCount = 0;

	// 至少要有一种敌人和一个刷新点
	if ((EnemyTypes.Num() == 0 && !EnemyClass) || SpawnPoints.Num() == 0)
	{
		return SpawnedCount;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return SpawnedCount;
	}

	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);

	for (int32 i = 0; i < Config.EnemyCount; i++)
	{
		AActor* SpawnPoint = SpawnPoints[i % SpawnPoints.Num()];
		if (!SpawnPoint)
		{
			continue;
		}

		// 刷新点附近随机水平偏移
		FVector Base = SpawnPoint->GetActorLocation();
		Base.X += FMath::RandRange(-150.f, 150.f);
		Base.Y += FMath::RandRange(-150.f, 150.f);

		// 投影到导航网格，确保落在可行走地面。投影失败则跳过，绝不刷出会掉出世界的敌人
		FVector Location;
		bool bValidLocation = false;
		if (NavSys)
		{
			FNavLocation NavLoc;
			if (NavSys->ProjectPointToNavigation(Base, NavLoc, FVector(800.f, 800.f, 800.f)))
			{
				Location = NavLoc.Location + FVector(0.f, 0.f, 95.f);
				bValidLocation = true;
			}
			else if (NavSys->ProjectPointToNavigation(SpawnPoint->GetActorLocation(), NavLoc, FVector(800.f, 800.f, 800.f)))
			{
				Location = NavLoc.Location + FVector(0.f, 0.f, 95.f);
				bValidLocation = true;
			}
		}

		if (!bValidLocation)
		{
			// 这个刷新点附近没有可行走地面，跳过，避免敌人掉出世界
			continue;
		}

		// 随机选一种敌人（种类池优先，否则用兜底单一类）
		TSubclassOf<AShooterNPC> ChosenType = EnemyClass;
		if (EnemyTypes.Num() > 0)
		{
			ChosenType = EnemyTypes[FMath::RandRange(0, EnemyTypes.Num() - 1)];
		}
		if (!ChosenType)
		{
			continue;
		}

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AShooterNPC* Enemy = World->SpawnActor<AShooterNPC>(
			ChosenType, Location, SpawnPoint->GetActorRotation(), Params);

		if (Enemy)
		{
			// 波次倍率
			Enemy->CurrentHP *= Config.HealthMultiplier;

			if (UCharacterMovementComponent* Move = Enemy->GetCharacterMovement())
			{
				// 大地图下保证丧尸基速下限（首波约 40~60 秒抵达核心），再叠加波次加速
				Move->MaxWalkSpeed = FMath::Max(Move->MaxWalkSpeed, 380.0f) * Config.SpeedMultiplier;
			}

			// 以乘过倍率的速度/血量作为基准与回血上限（疾跑/减速/自回血都基于它）
			Enemy->InitCombatBaseline();

			// 死亡发经验/连击/得分
			Enemy->OnPawnDeathWithKiller.AddDynamic(this, &AWaveManager::HandleEnemyDeath);

			SpawnedCount++;
		}
	}

	return SpawnedCount;
}
