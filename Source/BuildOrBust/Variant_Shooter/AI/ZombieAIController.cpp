#include "ZombieAIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "BaseCore.h"

// 野生同化体：散布在地图各处地形上的环境敌人，不属于任何一次同化潮。
// 挑衅半径远小于同化潮的 PlayerAggroRadius——野生的要等你真的贴上去才理你；
// 一旦被打过（BoBWildAngry）追击半径放大，否则打一枪就走太廉价。
// 全部走标签而不是新属性，是为了留在 .cpp 里热更（新 .h 要关编辑器整编）。
static const FName TAG_Wild(TEXT("BoBWild"));
static const FName TAG_WildAngry(TEXT("BoBWildAngry"));
static constexpr float WildProvokeRadius = 650.0f;
static constexpr float WildChaseRadius   = 2600.0f;
static constexpr float WildRoamSpeed     = 0.28f;   // 漫游时的移动输入强度

AZombieAIController::AZombieAIController()
{
	// 每帧直线转向移动（不依赖导航网格/寻路）
	PrimaryActorTick.bCanEverTick = true;
}

void AZombieAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	RefreshTarget();
	GetWorld()->GetTimerManager().SetTimer(
		RetargetTimer, this, &AZombieAIController::RefreshTarget, RetargetInterval, true);
}

void AZombieAIController::OnUnPossess()
{
	GetWorld()->GetTimerManager().ClearTimer(RetargetTimer);
	Super::OnUnPossess();
}

void AZombieAIController::RefreshTarget()
{
	// 核心仍是默认目标（守家玩法的骨架），但**不再无脑只认核心**：
	// 挡在路上的玩家会被就近盯上。原来的写法是只要核心存在就直接 return，
	// 结果同化体从玩家身边贴脸走过也不理，玩家没有任何"被追"的压力，
	// 打起来就像在打靶场的移动靶。
	AActor* Core = UGameplayStatics::GetActorOfClass(GetWorld(), ABaseCore::StaticClass());

	// 先找最近的玩家
	AActor* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	APawn* Self = GetPawn();
	if (!Self)
	{
		return;
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* P = PC->GetPawn())
			{
				const float DSq = FVector::DistSquared(Self->GetActorLocation(), P->GetActorLocation());
				if (DSq < BestDistSq)
				{
					BestDistSq = DSq;
					Best = P;
				}
			}
		}
	}
	// 野生同化体在这里分叉：它不认核心，也不主动挑事。
	// TargetActor 是这套 AI 唯一的行为开关——置空它就不再朝任何东西移动，
	// 于是"不攻击核心"和"不被靠近就不打人"两条一起成立，不必另写一套待机状态机。
	if (Self->ActorHasTag(TAG_Wild))
	{
		const float Reach = Self->ActorHasTag(TAG_WildAngry) ? WildChaseRadius : WildProvokeRadius;
		TargetActor = (Best && FMath::Sqrt(BestDistSq) <= Reach) ? Best : nullptr;
		return;
	}

	// 玩家进了仇恨半径就改追玩家；否则继续奔核心。
	// 用绝对半径而不是"比核心更近"：后者会让站在核心旁边的玩家永远抢不到仇恨，
	// 而那恰恰是最需要被追着打的位置
	if (Best && Self && FMath::Sqrt(BestDistSq) <= PlayerAggroRadius)
	{
		TargetActor = Best;
	}
	else
	{
		TargetActor = Core ? Core : Best;
	}
}

void AZombieAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* Self = GetPawn();
	if (!Self)
	{
		return;
	}

	// 野生的没目标时不要杵成雕像——那比追着你打还出戏。给一个缓慢改向的漫游。
	// 方向完全由时间和实例 ID 算出来，不存任何状态，所以不用新增成员变量
	// （新 .h 就要关编辑器整编，这条改动想留在热更范围内）。
	if (!TargetActor.IsValid())
	{
		if (Self->ActorHasTag(TAG_Wild))
		{
			const float Seed = static_cast<float>(Self->GetUniqueID() % 997);
			const float T = GetWorld()->GetTimeSeconds() * 0.09f + Seed;
			const float Yaw = (FMath::Sin(T) + 0.5f * FMath::Sin(T * 2.37f + Seed)) * 180.0f;
			const FVector Dir = FRotator(0.0f, Yaw, 0.0f).Vector();
			Self->AddMovementInput(ComputeAvoidanceDir(Self, Dir), WildRoamSpeed);
			SetControlRotation(Dir.Rotation());
		}
		return;
	}

	FVector To = TargetActor->GetActorLocation() - Self->GetActorLocation();
	To.Z = 0.0f;
	const float Dist = To.Size();

	if (Dist > StopDistance)
	{
		const FVector DesiredDir = To.GetSafeNormal();

		// ① 前探避障：三线扇形（正前 / 左偏 / 右偏）探静态几何，选空侧绕行
		FVector Dir = ComputeAvoidanceDir(Self, DesiredDir);

		// ② 卡墙脱困兜底：避障仍卡在凹形死角时，超时后随机侧移强脱
		const float Now = GetWorld()->GetTimeSeconds();
		if (Now >= NextStallCheckTime)
		{
			if (NextStallCheckTime > 0.0f && Now >= DetourUntil &&
				FVector::Dist2D(Self->GetActorLocation(), LastStallCheckPos) < 30.0f)
			{
				DetourSign = FMath::RandBool() ? 1.0f : -1.0f;
				DetourUntil = Now + 0.8f;
			}
			LastStallCheckPos = Self->GetActorLocation();
			NextStallCheckTime = Now + 0.6f;
		}
		if (Now < DetourUntil)
		{
			Dir = Dir.RotateAngleAxis(75.0f * DetourSign, FVector::UpVector);
		}

		Self->AddMovementInput(Dir);
		// 面向移动方向（用控制器朝向驱动，Character 默认跟随控制器 Yaw）
		SetControlRotation(Dir.Rotation());
	}
	else if (Dist > 1.0f)
	{
		// 已到攻击距离：站定但保持面向目标（啃食朝向）
		SetControlRotation(To.Rotation());
	}
}

FVector AZombieAIController::ComputeAvoidanceDir(APawn* Self, const FVector& DesiredDir) const
{
	// 三线扇形前探：正前一条 + 左右各偏 ProbeAngle。只测静态世界几何（掩体/墙/核心碰撞球），
	// 不测其他丧尸（互相避让交给 RVO，避免两套系统打架）
	const FVector Origin = Self->GetActorLocation();

	FCollisionQueryParams Params(SCENE_QUERY_STAT(ZombieAvoid), false, Self);
	FCollisionObjectQueryParams ObjParams(ECC_WorldStatic);
	const FCollisionShape Probe = FCollisionShape::MakeSphere(ProbeRadius);

	auto Blocked = [&](const FVector& D) -> bool
	{
		FHitResult Hit;
		return GetWorld()->SweepSingleByObjectType(
			Hit, Origin, Origin + D * ProbeDistance, FQuat::Identity, ObjParams, Probe, Params);
	};

	// 正前方通畅 → 直接走，零开销偏转
	if (!Blocked(DesiredDir))
	{
		return DesiredDir;
	}

	// 正前受阻 → 比较左右两侧哪条更空，朝更空的一侧偏转 ProbeTurnAngle
	const FVector Left  = DesiredDir.RotateAngleAxis(-ProbeAngle, FVector::UpVector);
	const FVector Right = DesiredDir.RotateAngleAxis( ProbeAngle, FVector::UpVector);
	const bool bLeftBlocked  = Blocked(Left);
	const bool bRightBlocked = Blocked(Right);

	if (bLeftBlocked && !bRightBlocked)
	{
		return DesiredDir.RotateAngleAxis( ProbeTurnAngle, FVector::UpVector);
	}
	if (bRightBlocked && !bLeftBlocked)
	{
		return DesiredDir.RotateAngleAxis(-ProbeTurnAngle, FVector::UpVector);
	}

	// 两侧都空或都堵：沿用当前脱困方向做一致偏转（避免左右横跳），交给兜底逻辑收拾死角
	return DesiredDir.RotateAngleAxis(ProbeTurnAngle * DetourSign, FVector::UpVector);
}
