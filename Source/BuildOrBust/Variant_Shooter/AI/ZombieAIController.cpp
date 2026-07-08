#include "ZombieAIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "BaseCore.h"

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
	// 优先奔向中央核心（守家玩法：玩家负责在路上拦截）
	if (AActor* Core = UGameplayStatics::GetActorOfClass(GetWorld(), ABaseCore::StaticClass()))
	{
		TargetActor = Core;
		return;
	}

	// 没有核心则追最近的玩家
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
	TargetActor = Best;
}

void AZombieAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	APawn* Self = GetPawn();
	if (!Self || !TargetActor.IsValid())
	{
		return;
	}

	FVector To = TargetActor->GetActorLocation() - Self->GetActorLocation();
	To.Z = 0.0f;
	const float Dist = To.Size();

	if (Dist > StopDistance)
	{
		FVector Dir = To.GetSafeNormal();

		// 卡墙脱困：每 0.6 秒检查推进量。若持续输入却几乎没位移（正面顶住掩体），
		// 侧向 75° 绕行 0.8 秒；方向随机、卡住会反复触发直到绕开
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
