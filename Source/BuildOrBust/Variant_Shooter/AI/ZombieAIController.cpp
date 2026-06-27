#include "ZombieAIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "BaseCore.h"

AZombieAIController::AZombieAIController()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AZombieAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	GetWorld()->GetTimerManager().SetTimer(
		ChaseTimer, this, &AZombieAIController::ChaseTick, ChaseInterval, true);
}

void AZombieAIController::OnUnPossess()
{
	GetWorld()->GetTimerManager().ClearTimer(ChaseTimer);
	Super::OnUnPossess();
}

void AZombieAIController::ChaseTick()
{
	APawn* Self = GetPawn();
	if (!Self)
	{
		return;
	}

	// 优先奔向基地（守要塞核心）；玩家负责在路上拦截
	AActor* TargetActor = UGameplayStatics::GetActorOfClass(GetWorld(), ABaseCore::StaticClass());

	// 没有基地则退回追最近玩家
	if (!TargetActor)
	{
		float BestDistSq = TNumericLimits<float>::Max();
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
						TargetActor = P;
					}
				}
			}
		}
	}

	if (TargetActor)
	{
		MoveToActor(TargetActor, AcceptanceRadius);
	}
}
