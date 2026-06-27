#include "UpgradeComponent.h"
#include "Net/UnrealNetwork.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "BaseCore.h"
#include "EngineUtils.h"

UUpgradeComponent::UUpgradeComponent()
{
	SetIsReplicatedByDefault(true);
}

void UUpgradeComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UUpgradeComponent, OwnedUpgrades);
}

void UUpgradeComponent::Server_ApplyUpgrade_Implementation(EUpgradeType Type)
{
	OwnedUpgrades.Add(Type);
	ApplyUpgradeEffect(Type);
	OnUpgradeApplied.Broadcast();
}

void UUpgradeComponent::ApplyUpgradeEffect(EUpgradeType Type)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	switch (Type)
	{
	case EUpgradeType::SpeedBoost:
		if (ACharacter* Char = Cast<ACharacter>(OwnerActor))
		{
			if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
			{
				Move->MaxWalkSpeed *= 1.20f;
			}
		}
		break;

	case EUpgradeType::MaxHealth:
		if (AShooterCharacter* SC = Cast<AShooterCharacter>(OwnerActor))
		{
			SC->GrantMaxHealthBonus(120.0f);
		}
		break;

	case EUpgradeType::ArmorLayer:
		if (AShooterCharacter* SC = Cast<AShooterCharacter>(OwnerActor))
		{
			SC->AddDamageReduction(0.18f);
		}
		break;

	case EUpgradeType::VampireBullet:
		// 击杀回血是事件型，存为标记，由 WaveManager 击杀时查询
		break;

	case EUpgradeType::CoreReinforce:
	{
		// 守家：提升中央核心血量上限并立即修复
		for (TActorIterator<ABaseCore> It(GetWorld()); It; ++It)
		{
			(*It)->AddMaxHPAndHeal(400.0f);
			break;
		}
		break;
	}

	case EUpgradeType::CoreShield:
	{
		// 守家：核心减伤 +20%（累加）
		for (TActorIterator<ABaseCore> It(GetWorld()); It; ++It)
		{
			(*It)->AddBaseDamageReduction(0.20f);
			break;
		}
		break;
	}

	default:
		// 其它增益（弹道类、核心回能等事件型）暂存标记，由各自系统查询
		break;
	}
}

TArray<EUpgradeType> UUpgradeComponent::RollChoices(int32 NumChoices)
{
	// 增益池：丧尸只扑基地、不打玩家，故生存类（嗜血/护甲/强体）对得分毫无意义，已全部移除。
	// 只放「能更快更多地击杀」和「直接放大击杀得分」的增益——决定同场竞技的 PVP 胜负。
	TArray<EUpgradeType> Pool = {
		EUpgradeType::DamageUp,     // 杀得更快
		EUpgradeType::InstaKill,    // 概率秒杀
		EUpgradeType::SpeedBoost,   // 跑得更快、更早拦截
		EUpgradeType::FrostBullet,  // 减速，争取更多击杀时间
		EUpgradeType::PoisonBullet, // 毒杀，刷分
		EUpgradeType::ScoreBoost,   // 直接加分
		EUpgradeType::CritScore     // 概率双倍分
	};

	TArray<EUpgradeType> Result;
	NumChoices = FMath::Min(NumChoices, Pool.Num());

	for (int32 i = 0; i < NumChoices; i++)
	{
		const int32 Idx = FMath::RandRange(0, Pool.Num() - 1);
		Result.Add(Pool[Idx]);
		Pool.RemoveAt(Idx);
	}

	return Result;
}

void UUpgradeComponent::GetUpgradeDisplay(EUpgradeType Type, FText& OutName, FText& OutDescription)
{
	switch (Type)
	{
	case EUpgradeType::DamageUp:
		OutName = FText::FromString(TEXT("强化弹药"));
		OutDescription = FText::FromString(TEXT("子弹伤害提升 30%。可叠加，杀得更快即得分更多。"));
		break;
	case EUpgradeType::ScoreBoost:
		OutName = FText::FromString(TEXT("赏金猎人"));
		OutDescription = FText::FromString(TEXT("每次击杀获得的得分提升 30%。可叠加，PVP 滚雪球利器。"));
		break;
	case EUpgradeType::CritScore:
		OutName = FText::FromString(TEXT("致命彩头"));
		OutDescription = FText::FromString(TEXT("每次击杀有 25% 概率获得双倍得分。可叠加。"));
		break;
	case EUpgradeType::CoreRestore:
		OutName = FText::FromString(TEXT("核心回能"));
		OutDescription = FText::FromString(TEXT("每击杀一名敌人，为中央核心恢复 40 点血量。可叠加。"));
		break;
	case EUpgradeType::CoreReinforce:
		OutName = FText::FromString(TEXT("核心增幅"));
		OutDescription = FText::FromString(TEXT("中央核心血量上限提升 400，并立即修复 400 点。可叠加。"));
		break;
	case EUpgradeType::CoreShield:
		OutName = FText::FromString(TEXT("核心护盾"));
		OutDescription = FText::FromString(TEXT("中央核心受到的伤害降低 20%。可叠加，上限 80%。"));
		break;
	case EUpgradeType::SpeedBoost:
		OutName = FText::FromString(TEXT("迅捷步伐"));
		OutDescription = FText::FromString(TEXT("移动速度提升 20%。可叠加。"));
		break;
	case EUpgradeType::MaxHealth:
		OutName = FText::FromString(TEXT("强健体魄"));
		OutDescription = FText::FromString(TEXT("生命上限提升 120 点，并立即治疗至满。可叠加。"));
		break;
	case EUpgradeType::ArmorLayer:
		OutName = FText::FromString(TEXT("护甲强化"));
		OutDescription = FText::FromString(TEXT("受到的所有伤害降低 18%。可叠加，上限 80%。"));
		break;
	case EUpgradeType::VampireBullet:
		OutName = FText::FromString(TEXT("嗜血"));
		OutDescription = FText::FromString(TEXT("每击杀一名敌人，恢复 25 点生命。可叠加。"));
		break;
	case EUpgradeType::FrostBullet:
		OutName = FText::FromString(TEXT("霜冻弹"));
		OutDescription = FText::FromString(TEXT("命中使敌人移动速度降低 55%，持续 3.5 秒。"));
		break;
	case EUpgradeType::PoisonBullet:
		OutName = FText::FromString(TEXT("剧毒弹"));
		OutDescription = FText::FromString(TEXT("命中使敌人中毒，每秒流失生命，持续 5 秒。"));
		break;
	case EUpgradeType::InstaKill:
		OutName = FText::FromString(TEXT("处决"));
		OutDescription = FText::FromString(TEXT("每次命中有 10% 概率直接处决敌人。可叠加。"));
		break;
	default:
		OutName = FText::FromString(TEXT("未知增益"));
		OutDescription = FText::GetEmpty();
		break;
	}
}

bool UUpgradeComponent::HasUpgrade(EUpgradeType Type) const
{
	return OwnedUpgrades.Contains(Type);
}

int32 UUpgradeComponent::GetUpgradeCount(EUpgradeType Type) const
{
	int32 Count = 0;
	for (EUpgradeType T : OwnedUpgrades)
	{
		if (T == Type) Count++;
	}
	return Count;
}
