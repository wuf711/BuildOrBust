#include "ExperienceComponent.h"
#include "Net/UnrealNetwork.h"

UExperienceComponent::UExperienceComponent()
{
	SetIsReplicatedByDefault(true);
}

void UExperienceComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UExperienceComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UExperienceComponent, CurrentExp);
	DOREPLIFETIME(UExperienceComponent, CurrentLevel);
}

void UExperienceComponent::AddExperience(int32 Amount)
{
	if (!GetOwner()->HasAuthority()) return;

	CurrentExp += Amount;

	// 仅累计经验/等级（供经验条等表现使用）。升级不再直接弹三选一——
	// 现在改为「每波结束统一发卡」，由 WaveManager 调 Client_OfferUpgrade 触发，避免战斗中卡顿。
	while (CurrentExp >= GetExpToNextLevel())
	{
		CurrentExp -= GetExpToNextLevel();
		CurrentLevel++;
	}
}

void UExperienceComponent::Client_OfferUpgrade_Implementation()
{
	// 在拥有该角色的客户端本地广播：BP 绑定 OnLevelUp → 创建并弹出 WB_UpgradePicker
	OnLevelUp.Broadcast(CurrentLevel);
}

float UExperienceComponent::GetExpPercent() const
{
	return (float)CurrentExp / (float)GetExpToNextLevel();
}

int32 UExperienceComponent::GetExpToNextLevel() const
{
	return FMath::RoundToInt(BaseExp * FMath::Pow((float)CurrentLevel, ExpScale));
}

void UExperienceComponent::OnRep_Level()
{
	OnLevelUp.Broadcast(CurrentLevel);
}
