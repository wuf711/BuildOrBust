#include "HealthComponent.h"
#include "Net/UnrealNetwork.h"

UHealthComponent::UHealthComponent()
{
	SetIsReplicatedByDefault(true);
}

void UHealthComponent::BeginPlay()
{
	Super::BeginPlay();
	CurrentHP = MaxHP;
}

void UHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UHealthComponent, CurrentHP);
}

void UHealthComponent::TakeDamage(float Damage, AActor* DamageCauser)
{
	if (!GetOwner()->HasAuthority() || bIsDead) return;

	float OldHP = CurrentHP;
	CurrentHP = FMath::Clamp(CurrentHP - Damage, 0.f, MaxHP);
	OnHealthChanged.Broadcast(CurrentHP, MaxHP, CurrentHP - OldHP);

	if (CurrentHP <= 0.f)
	{
		bIsDead = true;
		OnDeath.Broadcast(GetOwner(), DamageCauser);
	}
}

void UHealthComponent::Heal(float Amount)
{
	if (!GetOwner()->HasAuthority() || bIsDead) return;

	float OldHP = CurrentHP;
	CurrentHP = FMath::Clamp(CurrentHP + Amount, 0.f, MaxHP);
	OnHealthChanged.Broadcast(CurrentHP, MaxHP, CurrentHP - OldHP);
}

void UHealthComponent::ApplyHealthMultiplier(float Multiplier)
{
	MaxHP *= Multiplier;
	CurrentHP = MaxHP;
}

void UHealthComponent::OnRep_HP()
{
	OnHealthChanged.Broadcast(CurrentHP, MaxHP, 0.f);
}
