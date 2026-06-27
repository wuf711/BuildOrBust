#include "BaseCore.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/ShooterGameMode.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Net/UnrealNetwork.h"

void ABaseCore::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABaseCore, CurrentBaseHP);
	DOREPLIFETIME(ABaseCore, MaxBaseHP);
}

void ABaseCore::RepairBase(float Amount)
{
	if (!HasAuthority() || bDestroyed)
	{
		return;
	}
	CurrentBaseHP = FMath::Min(MaxBaseHP, CurrentBaseHP + Amount);
}

void ABaseCore::AddMaxHPAndHeal(float Amount)
{
	if (!HasAuthority())
	{
		return;
	}
	MaxBaseHP += Amount;
	CurrentBaseHP = FMath::Min(MaxBaseHP, CurrentBaseHP + Amount);
}

void ABaseCore::AddBaseDamageReduction(float Pct)
{
	if (!HasAuthority())
	{
		return;
	}
	DamageReductionPct = FMath::Clamp(DamageReductionPct + Pct, 0.0f, 0.8f);
}

void ABaseCore::AutoUpgradeAndHeal()
{
	if (!HasAuthority() || bDestroyed)
	{
		return;
	}
	MaxBaseHP += WaveHPGrowth;     // 自动升级：血量上限增长
	CurrentBaseHP = MaxBaseHP;     // 回满状态
}

ABaseCore::ABaseCore()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	BaseMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BaseMesh"));
	BaseMesh->SetupAttachment(SceneRoot);

	DamageZone = CreateDefaultSubobject<USphereComponent>(TEXT("DamageZone"));
	DamageZone->SetupAttachment(SceneRoot);
	DamageZone->SetSphereRadius(450.0f);
	DamageZone->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	DamageZone->SetGenerateOverlapEvents(true);
}

void ABaseCore::BeginPlay()
{
	Super::BeginPlay();

	CurrentBaseHP = MaxBaseHP;

	if (HasAuthority())
	{
		GetWorld()->GetTimerManager().SetTimer(
			DamageTimer, this, &ABaseCore::DamageTick, DamageInterval, true);
	}
}

void ABaseCore::DamageTick()
{
	if (bDestroyed)
	{
		return;
	}

	// 统计范围内存活的敌人
	TArray<AActor*> Overlapping;
	DamageZone->GetOverlappingActors(Overlapping, AShooterNPC::StaticClass());

	int32 EnemyCount = 0;
	for (AActor* A : Overlapping)
	{
		AShooterNPC* NPC = Cast<AShooterNPC>(A);
		if (NPC && !NPC->IsDead())
		{
			EnemyCount++;
		}
	}

	if (EnemyCount <= 0)
	{
		return;
	}

	// 近战啃食：范围内每个敌人按 DPS 结算
	DamageCore(DamagePerEnemyPerSecond * DamageInterval * EnemyCount);
}

void ABaseCore::DamageCore(float RawAmount)
{
	if (!HasAuthority() || bDestroyed || RawAmount <= 0.0f)
	{
		return;
	}

	const float Dmg = RawAmount * (1.0f - DamageReductionPct);   // 核心护盾减伤
	CurrentBaseHP = FMath::Max(0.0f, CurrentBaseHP - Dmg);

	BP_OnBaseDamaged(GetBaseHPPercent());

	if (CurrentBaseHP <= 0.0f)
	{
		bDestroyed = true;
		GetWorld()->GetTimerManager().ClearTimer(DamageTimer);

		BP_OnBaseDestroyed();

		// 基地被摧毁 = 游戏失败
		if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
		{
			GM->TriggerLose();
		}
	}
}

float ABaseCore::GetBaseHPPercent() const
{
	return MaxBaseHP > 0.0f ? CurrentBaseHP / MaxBaseHP : 0.0f;
}
