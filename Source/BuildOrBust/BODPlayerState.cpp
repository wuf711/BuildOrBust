#include "BODPlayerState.h"
#include "Net/UnrealNetwork.h"

void ABODPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABODPlayerState, Kills);
	DOREPLIFETIME(ABODPlayerState, PlayerLevel);
	DOREPLIFETIME(ABODPlayerState, SurvivalTime);
	DOREPLIFETIME(ABODPlayerState, UpgradeList);
}

void ABODPlayerState::BeginPlay()
{
	Super::BeginPlay();
	SetActorTickEnabled(true);
}

void ABODPlayerState::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority() && bAlive)
	{
		SurvivalTime += DeltaTime;
	}
}

void ABODPlayerState::AddKill()
{
	if (HasAuthority())
	{
		Kills++;
	}
}
