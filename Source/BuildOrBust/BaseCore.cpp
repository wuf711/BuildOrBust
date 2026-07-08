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

	// 啃食判定圈：蓝图实例序列化的旧半径(420)会压过 C++ 默认值，而丧尸被核心碰撞球
	// 挡停在中心距 ~420-484 处，恰好整段卡在圈外啃不到。运行时强制扩到 520，
	// 覆盖任何旧序列化值（不依赖逐实例属性持久化，打包/PIE 一致生效）
	if (DamageZone)
	{
		DamageZone->SetSphereRadius(520.0f);
	}

	CurrentBaseHP = MaxBaseHP;

	// 旧的重叠检测啃食（DamageTick 定时器）已停用：
	// 核心伤害改由丧尸主动啃食驱动（ShooterNPC::MeleeAttackTick，贴近即咬），
	// 避免 DamageZone 半径被蓝图实例旧序列化值污染导致"围而不咬"。
	// DamageTick 函数保留备用，如需恢复旧模型还原此处定时器即可。
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
