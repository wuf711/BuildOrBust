// Build or Bust — 局内自检台。
//
// 为什么单独一个文件：之前的自检命令是一路加上来的，各自格式不同，
// 都得人眼读日志才知道过没过。跑一趟无头要四五分钟，人眼漏检一次就白跑。
// 这里统一成断言式——每条检查输出一行 [BoBTest] PASS/FAIL，
// 最后一行给总数，脚本 grep 一次就能判定，不需要读懂内容。
//
// 用法（PIE 控制台或 -ExecCmds）：
//     BoB.Test boss      三阶段状态机 + 倒计时账目
//     BoB.Test exec      处决者：降临 / 脱离 / 放逐
//     BoB.Test data      数据表与资产完整性（不需要跑起来）
//     BoB.Test all       全部
//
// 无头一键：tools/run_tests.ps1

#include "CoreMinimal.h"
#include "Boss_CS07.h"
#include "BoBExecutioner.h"
#include "BoBEnemyTypes.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "WaveManager.h"
#include "BODPlayerState.h"
#include "LootPickup.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "GameFramework/Controller.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING

namespace BoBTest
{
	static int32 GPass = 0;
	static int32 GFail = 0;

	static void Reset() { GPass = 0; GFail = 0; }

	/** 一条断言。失败不抛异常也不中断——一趟要把所有问题都暴露出来，
	 *  而不是修一个跑一趟 */
	static bool Check(const FString& What, bool bOk, const FString& Detail = FString())
	{
		bOk ? GPass++ : GFail++;
		UE_LOG(LogTemp, Log, TEXT("[BoBTest] %s  %s%s"),
			bOk ? TEXT("PASS") : TEXT("FAIL"), *What,
			Detail.IsEmpty() ? TEXT("") : *FString::Printf(TEXT("  (%s)"), *Detail));
		return bOk;
	}

	static void CheckNear(const FString& What, float Got, float Want, float Tol)
	{
		Check(What, FMath::Abs(Got - Want) <= Tol,
			FString::Printf(TEXT("实测 %.2f 期望 %.2f"), Got, Want));
	}

	static void Summary(const FString& Scenario)
	{
		UE_LOG(LogTemp, Log, TEXT("[BoBTest] ===== %s 结束：%d 通过 / %d 失败 ====="),
			*Scenario, GPass, GFail);
		UE_LOG(LogTemp, Log, TEXT("[BoBTest] VERDICT %s"),
			GFail == 0 ? TEXT("OK") : TEXT("BROKEN"));
	}

	// ---------- 场景：数据表（不需要世界跑起来） ----------
	static void RunData()
	{
		const TArray<FName> Ids = UBoBEnemyLib::AllEnemyIds();
		Check(TEXT("型号表可读"), Ids.Num() > 0, FString::Printf(TEXT("%d 行"), Ids.Num()));
		Check(TEXT("型号数 = 22"), Ids.Num() == 22, FString::Printf(TEXT("%d"), Ids.Num()));

		int32 NoMesh = 0, BadVariant = 0;
		for (const FName& Id : Ids)
		{
			FBoBEnemyRow Row, Resolved;
			if (!UBoBEnemyLib::GetEnemyRow(Id, Row)) { continue; }
			if (!UBoBEnemyLib::ResolveAssets(Row, Resolved)) { BadVariant++; continue; }
			if (Resolved.Mesh.IsNull()) { NoMesh++; }
		}
		Check(TEXT("所有行都能解析出资产"), BadVariant == 0,
			FString::Printf(TEXT("%d 行失败"), BadVariant));
		Check(TEXT("所有行都有骨骼网格"), NoMesh == 0,
			FString::Printf(TEXT("%d 行缺 mesh"), NoMesh));

		// 第 1 潮必须至少有一个投得起的型号，否则开局空场
		Check(TEXT("TIDE 1 有可投放型号"),
			UBoBEnemyLib::AffordableAt(1, 999).Num() > 0);
	}

	// ---------- 场景：Boss 三阶段 ----------
	static void RunBoss(UWorld* World)
	{
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ABoss_CS07* B = World->SpawnActor<ABoss_CS07>(
			ABoss_CS07::StaticClass(), FVector(0, 0, 500), FRotator::ZeroRotator, SP);
		Check(TEXT("CS-07 可生成"), B != nullptr);
		if (!B) { return; }

		B->Activate();
		const float T0 = B->GetExtractionRemaining();
		CheckNear(TEXT("初始倒计时 = 300"), T0, 300.f, 0.6f);
		Check(TEXT("初始阶段 = 权限剥夺"), B->GetPhase() == EBoBBossPhase::Authority);
		CheckNear(TEXT("初始抗性 = 100%"), B->GetResistance(), 1.f, 0.01f);

		// 烧到节点一
		UGameplayStatics::ApplyDamage(B, B->ResistancePool * 0.35f, nullptr, nullptr, nullptr);
		Check(TEXT("跨 66% 进入对称性破缺"), B->GetPhase() == EBoBBossPhase::Symmetry);
		Check(TEXT("阶段二折射盾已升起"), B->IsShielded());
		CheckNear(TEXT("节点一减 15 秒"), B->GetExtractionRemaining(), T0 - 15.f, 1.0f);

		// 盾下开火不该掉抗性
		const float R = B->GetResistance();
		UGameplayStatics::ApplyDamage(B, B->ResistancePool * 0.20f, nullptr, nullptr, nullptr);
		CheckNear(TEXT("盾下普通伤害被挡"), B->GetResistance(), R, 0.001f);

		// 柱子推进到节点二、节点三
		for (int32 i = 0; i < 12 && B->GetPhase() == EBoBBossPhase::Symmetry; ++i)
		{
			B->BreakPillar();
		}
		Check(TEXT("柱子可推进到静默走廊"), B->GetPhase() == EBoBBossPhase::Corridor);

		while (B->GetResistance() > 0.f)
		{
			B->BreakPillar();
		}
		CheckNear(TEXT("三节点共减 45 秒"), B->GetExtractionRemaining(), T0 - 45.f, 1.5f);
		// 走 B：抗性烧尽＝击败（结局二）。原契约 C7「打不死」已被用户推翻，
		// 这条断言跟着反过来——Boss 应当停止活动
		Check(TEXT("抗性烧尽后 CS-07 停止活动（结局二）"), !B->IsShielded());

		B->Destroy();
	}

	// ---------- 场景：处决者 ----------
	static void RunExec(UWorld* World)
	{
		AShooterCharacter* P = nullptr;
		for (TActorIterator<AShooterCharacter> It(World); It; ++It)
		{
			if (It->GetController()) { P = *It; break; }
		}
		Check(TEXT("找得到玩家"), P != nullptr);
		if (!P) { return; }

		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FVector Start = P->GetActorLocation() + FVector(2500, 0, 0);
		ABoBExecutioner* E = World->SpawnActor<ABoBExecutioner>(
			ABoBExecutioner::StaticClass(), Start, FRotator::ZeroRotator, SP);
		Check(TEXT("处决者可生成"), E != nullptr);
		if (!E) { return; }

		P->SetGaze(100.f);
		E->Hunt(P);
		Check(TEXT("已锁定目标"), E->GetTarget() == P);

		// 手动推几拍，验证它真的在朝玩家逼近
		const float D0 = FVector::Dist(E->GetActorLocation(), P->GetActorLocation());
		for (int32 i = 0; i < 10; ++i) { E->Tick(0.1f); }
		const float D1 = FVector::Dist(E->GetActorLocation(), P->GetActorLocation());
		Check(TEXT("正在直线逼近"), D1 < D0,
			FString::Printf(TEXT("%.0f -> %.0f"), D0, D1));

		// 失谐压回 70 以下，追一段后应当退场
		P->SetGaze(50.f);
		for (int32 i = 0; i < 80 && IsValid(E); ++i) { E->Tick(0.1f); }
		Check(TEXT("失谐压回后退场"), !IsValid(E) || E->IsActorBeingDestroyed());

		P->SetGaze(0.f);
	}
}

static void BoBTestCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World) { return; }
	const FString Which = Args.Num() > 0 ? Args[0].ToLower() : TEXT("all");

	BoBTest::Reset();
	if (Which == TEXT("data") || Which == TEXT("all")) { BoBTest::RunData(); }
	if (Which == TEXT("boss") || Which == TEXT("all")) { BoBTest::RunBoss(World); }
	if (Which == TEXT("exec") || Which == TEXT("all")) { BoBTest::RunExec(World); }
	BoBTest::Summary(Which);
}

static FAutoConsoleCommandWithWorldAndArgs GBoBTest(
	TEXT("BoB.Test"),
	TEXT("局内自检：BoB.Test [data|boss|exec|all]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBTestCmd));

#endif

#if !UE_BUILD_SHIPPING
// ============================================================
//  测试版作弊命令。目的只有一个：不用打完九潮就能验任何东西。
//  全部 ECVF_Cheat / 非 Shipping，打包正式版自动消失。
// ============================================================

/**
 *  命令回显。只写 UE_LOG 的话字只进"输出日志"窗口，游戏画面上什么都不显示，
 *  敲完像没生效——测试命令最要紧的就是让人当场看见它生效了。
 */
static void BoBSay(const FString& Msg)
{
	UE_LOG(LogTemp, Log, TEXT("[BoB] %s"), *Msg);
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 4.0f, FColor::Cyan, FString(TEXT("[BoB] ")) + Msg);
	}
}

static AShooterCharacter* BoBLocalPlayer(UWorld* World)
{
	for (TActorIterator<AShooterCharacter> It(World); It; ++It)
	{
		if (It->GetController() && It->GetController()->IsPlayerController())
		{
			return *It;
		}
	}
	return nullptr;
}

/** BoB.Wave <n> —— 直接跳到第 N 潮。测 Boss 就 BoB.Wave 10 */
static void BoBWaveCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() == 0) { BoBSay(TEXT("用法: BoB.Wave 10")); return; }
	const int32 N = FCString::Atoi(*Args[0]);
	for (TActorIterator<AWaveManager> It(World); It; ++It)
	{
		It->DebugJumpToWave(N);
		BoBSay(FString::Printf(TEXT("跳到 TIDE %d"), N));
		return;
	}
	BoBSay(TEXT("关卡里没有 WaveManager"));
}

/** BoB.Kill —— 清掉场上所有敌人（不含调试假人），用来立刻结束本潮 */
static void BoBKillCmd(const TArray<FString>&, UWorld* World)
{
	int32 N = 0;
	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		if (!It->Tags.Contains(FName("BoBDummy"))) { It->Destroy(); N++; }
	}
	BoBSay(FString::Printf(TEXT("清掉 %d 个同化体"), N));
}

// WaveManager.cpp 里的实现。extern 而不是抄一遍，是为了只有一处投放规则
extern int32 BoBSpawnWildAssimilates(UWorld* World, int32 Count, float MinR, float MaxR);
extern bool GBoBFreezeSpawns;   // BoBAIDirector.cpp

/**
 *  BoB.Tp <core|entry|ring|tk|exit> —— 传送到地图关键点。
 *  F0 入口离核心 174m，靠走的去验封停件太费时间；这条命令存在的唯一理由就是省这段路。
 */
static void BoBTpCmd(const TArray<FString>& Args, UWorld* World)
{
    AShooterCharacter* P = BoBLocalPlayer(World);
    if (!P) { return; }
    const FString Where = Args.Num() > 0 ? Args[0].ToLower() : TEXT("entry");

    FVector At;
    if (Where == TEXT("core"))       { At = FVector(0.f, 0.f, 300.f); }
    else if (Where == TEXT("entry")) { At = FVector(-9966.f, -14315.f, 2590.f); }   // 入口坑沿（地面）
    else if (Where == TEXT("lz"))    { At = FVector(-9966.f, -14315.f, -3680.f); }  // F0 落地区
    else if (Where == TEXT("mid"))   { At = FVector(-9600.f, -11000.f, -3760.f); }  // F0 廊道中继
    else if (Where == TEXT("main") || Where == TEXT("tk"))
                                     { At = FVector(-9600.f,  -6200.f, -3560.f); }  // F0 主场（天坑下）
    else if (Where == TEXT("gate"))  { At = FVector(-6600.f,  -9200.f, -3760.f); }  // F0 闸口
    // 逃生地道。入口在迷宫外沿，出口搬到了地图另外半边 (9234,6085)，
    // 旧的 (-4200,-10200) 是上一版出口，已经不是通道口了，留着会传到实心岩层里。
    else if (Where == TEXT("tunnel") || Where == TEXT("tun"))
                                     { At = FVector(-2779.f,  -2025.f, -3680.f); }  // 地道入口（迷宫侧）
    else if (Where == TEXT("exit"))  { At = FVector( 9234.f,   6085.f,  1525.f); }  // 地道出口（地表隐蔽塌陷坑）
    else if (Where == TEXT("ring"))  { At = FVector(-9000.f,  -9600.f, -3200.f); }  // 封停环边上
    else
    {
        BoBSay(TEXT("用法: BoB.Tp core|entry|lz|mid|main|gate|tunnel|exit|ring"));
        return;
    }

    // 地面吸附。上面那些 Z 只是"大概在哪一层"的声明，绝不能当落点用：地形一重建，
    // 写死的高度就落进地壳内部，玩家生在地表网格和洞顶之间的夹层里，看到的是地面
    // 的背面 —— 那不是穿模，是传送本身把人塞进了实心岩石。所以高度一律现场问地形。
    //
    // 多重射线而不是单发：入口是个坑，主场头顶 40m 处还盖着洞顶，从高处单发打下去
    // 会先命中顶板并把它当成"地面"。取离目标高度最近的那个命中点才是这一层的地面。
    {
        TArray<FHitResult> Hits;
        FCollisionQueryParams Q(SCENE_QUERY_STAT(BoBTpGround), true, P);
        const FVector Top(At.X, At.Y, At.Z + 3000.f);
        const FVector Bot(At.X, At.Y, At.Z - 8000.f);
        if (World->LineTraceMultiByChannel(Hits, Top, Bot, ECC_WorldStatic, Q))
        {
            float Best = 0.f; bool bFound = false;
            for (const FHitResult& H : Hits)
            {
                const float Z = H.ImpactPoint.Z;
                if (Z > At.Z + 250.f) { continue; }              // 头顶的顶板，不是地面
                if (!bFound || FMath::Abs(Z - At.Z) < FMath::Abs(Best - At.Z))
                {
                    Best = Z; bFound = true;
                }
            }
            if (bFound) { At.Z = Best + 110.f; }
        }
    }
    P->SetActorLocation(At, false, nullptr, ETeleportType::TeleportPhysics);
    BoBSay(FString::Printf(TEXT("传送到 %s (%.0f, %.0f, %.0f)"), *Where, At.X, At.Y, At.Z));
}

/**
 *  BoB.Freeze [0|1] —— 暂停出怪。不带参数就切换。
 *  同化潮和野生补员一起冻。冻结期间本潮不会结算（场上清空也不算清潮），
 *  这是有意的：要的是"世界停下来让我看东西"，不是"跳过这一潮"。
 */
static void BoBFreezeCmd(const TArray<FString>& Args, UWorld*)
{
	// 同 BoB.God：不带参数一律开，要关显式写 0。切换型开关会让人以为自己开着
	GBoBFreezeSpawns = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : true;
	BoBSay(FString::Printf(TEXT("出怪 = %s（同化潮 + 野生补员）"),
		GBoBFreezeSpawns ? TEXT("已冻结") : TEXT("恢复")));
}

/**
 *  BoB.Speed [倍数] —— 移速。不带参数给 4 倍，给 0 或 1 恢复。
 *  勘探跑图和验地形时最费时间的就是走路，尤其现在图有 495m。
 */
static void BoBSpeedCmd(const TArray<FString>& Args, UWorld* World)
{
	AShooterCharacter* P = BoBLocalPlayer(World);
	if (!P) { return; }
	UCharacterMovementComponent* Move = P->GetCharacterMovement();
	if (!Move) { return; }

	// 记住第一次改之前的原值，否则反复调用会在已经放大的基础上继续放大
	static float BaseSpeed = 0.0f;
	if (BaseSpeed <= 0.0f) { BaseSpeed = Move->MaxWalkSpeed; }

	const float Mul = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 4.0f;
	Move->MaxWalkSpeed = BaseSpeed * FMath::Max(0.1f, Mul);
	BoBSay(FString::Printf(TEXT("移速 ×%.1f（%.0f → %.0f）"),
		Mul, BaseSpeed, Move->MaxWalkSpeed));
}

/**
 *  BoB.NoGaze [0|1] —— 无视失谐。不带参数就切换。
 *  走的是 GazeRateMul：把涨速乘数压到 0，顺手把当前值清零。
 *  没有另开一个"免疫"分支——失谐的每一处消费端都读 Gaze，
 *  多一个旁路就多一处会忘记同步的地方。
 */
static void BoBNoGazeCmd(const TArray<FString>& Args, UWorld* World)
{
	AShooterCharacter* P = BoBLocalPlayer(World);
	if (!P) { return; }
	const bool bOn = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : true;
	P->GazeRateMul = bOn ? 0.0f : 1.0f;
	if (bOn) { P->SetGaze(0.0f); }
	BoBSay(FString::Printf(TEXT("失谐 = %s"),
		bOn ? TEXT("免疫（涨速 0，已清零）") : TEXT("恢复正常")));
}

/**
 *  BoB.Wild [n] [minR_m] [maxR_m] —— 立刻投放 n 只野生同化体。
 *  半径按米给，默认 90–240m。用来验三件事：不啃核心、不进潮次计数、不逼近不动手。
 */
static void BoBWildCmd(const TArray<FString>& Args, UWorld* World)
{
	const int32 N = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 12;
	const float MinR = (Args.Num() > 1 ? FCString::Atof(*Args[1]) : 90.f) * 100.f;
	const float MaxR = (Args.Num() > 2 ? FCString::Atof(*Args[2]) : 240.f) * 100.f;
	const int32 Made = BoBSpawnWildAssimilates(World, N, MinR, MaxR);

	int32 Wild = 0, Tide = 0;
	for (TActorIterator<AShooterNPC> It(World); It; ++It)
	{
		if (!IsValid(*It) || It->IsDead()) { continue; }
		if (It->Tags.Contains(FName("BoBDummy"))) { continue; }
		It->Tags.Contains(FName("BoBWild")) ? ++Wild : ++Tide;
	}
	BoBSay(FString::Printf(
		TEXT("投放 %d/%d 只野生；场上 野生 %d / 潮次 %d（潮次计数只应看后者）"),
		Made, N, Wild, Tide));
}

/**
 *  BoB.God [0|1] —— 无敌。不带参数一律**开**，要关必须显式写 0。
 *
 *  原来不带参数是切换，结果按第二次就把无敌悄悄关掉了，人还以为自己开着。
 *  日志里真的这么发生过：无敌开 → 处决者降临 → 无敌关 → 三秒后死。
 *  调试开关的默认行为应该是"确保处于我想要的状态"，不是"翻转"。
 */
static void BoBGodCmd(const TArray<FString>& Args, UWorld* World)
{
	AShooterCharacter* P = BoBLocalPlayer(World);
	if (!P) { return; }
	P->bDebugInvulnerable = Args.Num() > 0 ? (FCString::Atoi(*Args[0]) != 0) : true;
	BoBSay(FString::Printf(TEXT("无敌 = %s%s"),
		P->bDebugInvulnerable ? TEXT("开") : TEXT("关"),
		Args.Num() > 0 ? TEXT("") : TEXT("（要关请打：1 0）")));
}

/** BoB.Gaze <0-100> —— 设失谐。设到 100 就能立刻验处决者 */
static void BoBGazeCmd(const TArray<FString>& Args, UWorld* World)
{
	AShooterCharacter* P = BoBLocalPlayer(World);
	if (!P || Args.Num() == 0) { BoBSay(TEXT("用法: BoB.Gaze 100")); return; }
	P->SetGaze(FCString::Atof(*Args[0]));
	BoBSay(FString::Printf(TEXT("失谐 = %.0f"), P->GetGaze()));
}

/** BoB.Cinder <n> —— 发配额，用来测投送端 */
static void BoBCinderCmd(const TArray<FString>& Args, UWorld* World)
{
	AShooterCharacter* P = BoBLocalPlayer(World);
	if (!P) { return; }
	const int32 N = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 5000;
	if (ABODPlayerState* PS = Cast<ABODPlayerState>(P->GetPlayerState()))
	{
		PS->AddCinder(N);
		BoBSay(FString::Printf(TEXT("配额 +%d，现有 %d"), N, PS->GetCinder()));
	}
}

/** BoB.Heal —— 回满血。跟 BoB.God 的区别是它不改受伤规则，只补一次 */
static void BoBHealCmd(const TArray<FString>&, UWorld* World)
{
	if (AShooterCharacter* P = BoBLocalPlayer(World))
	{
		P->HealPlayer(99999.f);
		BoBSay(TEXT("已回满"));
	}
}


/**
 *  BoB.Loot —— 全图资源点复活。
 *
 *  正式规则里点位受两道门管着：稀有度分潮解锁 + 每窗随机限额（GateHidden 标签）。
 *  测试时这两道门只会让人找不到东西，所以这里两道一起拆：清标签 + 取消已拿走。
 */
static void BoBLootCmd(const TArray<FString>&, UWorld* World)
{
	static const FName GateTag(TEXT("GateHidden"));
	int32 Revived = 0, Weapons = 0;
	for (TActorIterator<ALootPickup> It(World); It; ++It)
	{
		It->Tags.Remove(GateTag);
		It->SetTaken(false);
		Revived++;
		if (It->WeaponClassOverride) { Weapons++; }
	}
	BoBSay(FString::Printf(TEXT("资源点复活 %d 处（其中武器箱 %d）"), Revived, Weapons));
}

/**
 *  BoB.Gun —— 把全图武器箱里的枪一次性发给自己。
 *
 *  工程里没有全局武器表，枪只存在于地图上武器箱的 WeaponClassOverride 里，
 *  所以只能扫地图。扫不到就说明这张图上根本没放武器箱，不是命令坏了——
 *  这种情况会明说，免得以为是 bug。
 */
static void BoBGunCmd(const TArray<FString>&, UWorld* World)
{
	AShooterCharacter* P = BoBLocalPlayer(World);
	if (!P) { BoBSay(TEXT("找不到玩家")); return; }

	TSet<UClass*> Seen;
	for (TActorIterator<ALootPickup> It(World); It; ++It)
	{
		UClass* C = It->WeaponClassOverride.Get();
		if (!C || Seen.Contains(C)) { continue; }
		// 过滤没配好的箱子：抽象类、或者类名就叫 ShooterWeapon 的基类。
		// 之前 8 号键会发出一把名字就叫"武器"的东西，就是扫到了这种
		if (C->HasAnyClassFlags(CLASS_Abstract)) { continue; }
		const FString N = C->GetName();
		if (N.StartsWith(TEXT("ShooterWeapon")) || N == TEXT("BP_ShooterWeapon_C")) { continue; }
		Seen.Add(C);
		P->AddWeaponClass(It->WeaponClassOverride);
	}
	BoBSay(Seen.Num() > 0
		? FString::Printf(TEXT("已发放 %d 把武器"), Seen.Num())
		: FString(TEXT("这张图上没有武器箱，无枪可发")));
}

static FAutoConsoleCommandWithWorldAndArgs GBoBLoot(TEXT("BoB.Loot"),
	TEXT("全图资源点复活（拆稀有度门与每窗限额）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBLootCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBGun(TEXT("BoB.Gun"),
	TEXT("发放全图武器箱里的所有武器"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBGunCmd));


static void BoBGazeMaxCmd(const TArray<FString>& Args, UWorld* World)
{
	TArray<FString> A = Args;
	if (A.Num() == 0) { A.Add(TEXT("100")); }
	BoBGazeCmd(A, World);
}

// ============================================================
//  纯数字指令。BoB.xxx 那套敲起来太长，测试时按 ~ 打一个数字就够。
//  0 是总表，忘了就敲 0。
// ============================================================

/** 9：瞬移到 CS-07 跟前。十米高的塔要是看不见，多半是它在你背后 */
static void BoBGotoBossCmd(const TArray<FString>&, UWorld* World)
{
	AShooterCharacter* P = BoBLocalPlayer(World);
	if (!P) { return; }
	for (TActorIterator<ABoss_CS07> It(World); It; ++It)
	{
		const FVector B = It->GetActorLocation();
		P->SetActorLocation(B + FVector(1400.f, 0.f, -400.f));
		if (AController* C = P->GetController())
		{
			C->SetControlRotation((B - P->GetActorLocation()).Rotation());
		}
		BoBSay(FString::Printf(TEXT("已瞬移到 CS-07（%s）"), *B.ToCompactString()));
		return;
	}
	BoBSay(TEXT("场上没有 CS-07，先敲 2 跳到 TIDE 10"));
}

static void BoBNumHelp(const TArray<FString>&, UWorld*)
{
	BoBSay(TEXT("== 数字指令 =="));
	BoBSay(TEXT("0 本表   1 无敌   2 跳TIDE10   3 引处决者"));
	BoBSay(TEXT("4 发配额  5 清场   6 回满血     7 资源点复活"));
	BoBSay(TEXT("8 发全部武器       9 瞬移到Boss"));
	BoBSay(TEXT("11 暂停出怪  12 移速x4  13 无视失谐  14 投放野生"));
	BoBSay(TEXT("15 去F0入口  16 去封停环  17 去天坑  18 回核心"));
	BoBSay(TEXT("19 去地道入口(迷宫侧)   20 去地道出口(地表)"));
	BoBSay(TEXT("（2 可带参数：2 5 = 跳到 TIDE 5；12 可带倍数：12 8）"));
	BoBSay(TEXT("（1/11/13 不带参数一律=开，要关打 1 0 / 11 0 / 13 0）"));
}

// 数字快捷键的传送包装：0-9 已经占满，新功能从 11 起
static void BoBTpEntry(const TArray<FString>&, UWorld* W)
{ TArray<FString> A; A.Add(TEXT("entry")); BoBTpCmd(A, W); }
static void BoBTpRing(const TArray<FString>&, UWorld* W)
{ TArray<FString> A; A.Add(TEXT("ring")); BoBTpCmd(A, W); }
static void BoBTpTk(const TArray<FString>&, UWorld* W)
{ TArray<FString> A; A.Add(TEXT("tk")); BoBTpCmd(A, W); }
static void BoBTpCore(const TArray<FString>&, UWorld* W)
{ TArray<FString> A; A.Add(TEXT("core")); BoBTpCmd(A, W); }

static void BoBNumWave(const TArray<FString>& Args, UWorld* World)
{
	TArray<FString> A = Args;
	if (A.Num() == 0) { A.Add(TEXT("10")); }   // 不带参数默认直奔 Boss
	BoBWaveCmd(A, World);
}

static FAutoConsoleCommandWithWorldAndArgs GN0(TEXT("0"), TEXT("测试指令总表"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBNumHelp));
static FAutoConsoleCommandWithWorldAndArgs GN1(TEXT("1"), TEXT("无敌"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBGodCmd));
static FAutoConsoleCommandWithWorldAndArgs GN2(TEXT("2"), TEXT("跳潮，默认 TIDE 10"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBNumWave));
static FAutoConsoleCommandWithWorldAndArgs GN3(TEXT("3"), TEXT("失谐拉满，引处决者"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBGazeMaxCmd));
static FAutoConsoleCommandWithWorldAndArgs GN4(TEXT("4"), TEXT("发配额"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBCinderCmd));
static FAutoConsoleCommandWithWorldAndArgs GN5(TEXT("5"), TEXT("清场"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBKillCmd));
static FAutoConsoleCommandWithWorldAndArgs GN6(TEXT("6"), TEXT("回满血"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBHealCmd));
static FAutoConsoleCommandWithWorldAndArgs GN7(TEXT("7"), TEXT("资源点复活"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBLootCmd));
static FAutoConsoleCommandWithWorldAndArgs GN8(TEXT("8"), TEXT("发全部武器"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBGunCmd));
static FAutoConsoleCommandWithWorldAndArgs GN9(TEXT("9"), TEXT("瞬移到 Boss"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBGotoBossCmd));

/** 19 —— 传送到逃生地道入口（迷宫侧洞口）。验地道就它 */
static void BoBNumTunnel(const TArray<FString>&, UWorld* World)
{
	TArray<FString> A;
	A.Add(TEXT("tunnel"));
	BoBTpCmd(A, World);
}
/** 20 —— 传送到逃生地道出口（地表隐蔽塌陷坑）。从上往下验通道就它 */
static void BoBNumExit(const TArray<FString>&, UWorld* World)
{
	TArray<FString> A;
	A.Add(TEXT("exit"));
	BoBTpCmd(A, World);
}
static FAutoConsoleCommandWithWorldAndArgs GN19(TEXT("19"), TEXT("瞬移到地道入口（迷宫侧）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBNumTunnel));
static FAutoConsoleCommandWithWorldAndArgs GN20(TEXT("20"), TEXT("瞬移到地道出口（地表）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBNumExit));

static void BoBHelpCmd(const TArray<FString>&, UWorld*)
{
	BoBSay(TEXT("== 测试命令 =="));
	BoBSay(TEXT("BoB.Wave 10   跳潮      BoB.God      无敌"));
	BoBSay(TEXT("BoB.Gaze 100  引处决者  BoB.Cinder N 发配额"));
	BoBSay(TEXT("BoB.Kill      清场      BoB.Heal     回满"));
	BoBSay(TEXT("BoB.Loot      资源点复活  BoB.Gun      发全部武器"));
	BoBSay(TEXT("BoB.Show <型号>  摆模型  BoB.Test all 自检"));
	BoBSay(TEXT("BoB.Wild [n] [近m] [远m]  投放野生同化体"));
	BoBSay(TEXT("BoB.Freeze  暂停出怪   BoB.Speed 4  移速×4"));
	BoBSay(TEXT("BoB.NoGaze  无视失谐"));
	BoBSay(TEXT("BoB.Tp core|entry|lz|mid|main|gate|tunnel|exit|ring"));
}

static FAutoConsoleCommandWithWorldAndArgs GBoBHelp(TEXT("BoB.Help"),
	TEXT("列出全部测试命令"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBHelpCmd));

static FAutoConsoleCommandWithWorldAndArgs GBoBWave(TEXT("BoB.Wave"),
	TEXT("跳到第 N 潮：BoB.Wave 10"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBWaveCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBKill(TEXT("BoB.Kill"),
	TEXT("清掉场上所有同化体"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBKillCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBGod(TEXT("BoB.God"),
	TEXT("无敌开关：BoB.God [0|1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBGodCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBGaze(TEXT("BoB.Gaze"),
	TEXT("设失谐：BoB.Gaze 100（触顶引处决者）"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBGazeCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBCinder(TEXT("BoB.Cinder"),
	TEXT("发配额：BoB.Cinder 5000"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBCinderCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBHeal(TEXT("BoB.Heal"),
	TEXT("回满血"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBHealCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBWild(TEXT("BoB.Wild"),
	TEXT("投放野生同化体：BoB.Wild [n] [近m] [远m]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBWildCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBFreeze(TEXT("BoB.Freeze"),
	TEXT("暂停出怪：BoB.Freeze [0|1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBFreezeCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBSpeed(TEXT("BoB.Speed"),
	TEXT("移速倍数：BoB.Speed [4]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBSpeedCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBNoGaze(TEXT("BoB.NoGaze"),
	TEXT("无视失谐：BoB.NoGaze [0|1]"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBNoGazeCmd));
static FAutoConsoleCommandWithWorldAndArgs GBoBTp(TEXT("BoB.Tp"),
	TEXT("传送：BoB.Tp core|entry|ring|tk|exit"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBTpCmd));
static FAutoConsoleCommandWithWorldAndArgs GN11(TEXT("11"), TEXT("暂停出怪"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBFreezeCmd));
static FAutoConsoleCommandWithWorldAndArgs GN12(TEXT("12"), TEXT("移速倍数，默认 x4"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBSpeedCmd));
static FAutoConsoleCommandWithWorldAndArgs GN13(TEXT("13"), TEXT("无视失谐"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBNoGazeCmd));
static FAutoConsoleCommandWithWorldAndArgs GN14(TEXT("14"), TEXT("投放野生同化体"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBWildCmd));
static FAutoConsoleCommandWithWorldAndArgs GN15(TEXT("15"), TEXT("去 F0 入口"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBTpEntry));
static FAutoConsoleCommandWithWorldAndArgs GN16(TEXT("16"), TEXT("去封停环"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBTpRing));
static FAutoConsoleCommandWithWorldAndArgs GN17(TEXT("17"), TEXT("去天坑"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBTpTk));
static FAutoConsoleCommandWithWorldAndArgs GN18(TEXT("18"), TEXT("回核心"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(BoBTpCore));
#endif
