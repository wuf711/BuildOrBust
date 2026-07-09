// Copyright Epic Games, Inc. All Rights Reserved.


#include "Variant_Shooter/AI/ShooterNPC.h"
#include "ShooterWeapon.h"
#include "Components/SkeletalMeshComponent.h"
#include "Camera/CameraComponent.h"
#include "Kismet/KismetMathLibrary.h"
#include "Engine/World.h"
#include "ShooterGameMode.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "ShooterCharacter.h"
#include "UpgradeComponent.h"
#include "UpgradeTypes.h"
#include "ZombieAIController.h"
#include "BaseCore.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/DamageType.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"

AShooterNPC::AShooterNPC()
{
	// 使用极简丧尸AI，并确保生成时自动接管
	AIControllerClass = AZombieAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;

	bReplicates = true;   // 确保死亡状态(bIsDead)能复制到客户端
}

void AShooterNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterNPC, bIsDead);
}

void AShooterNPC::BeginPlay()
{
	Super::BeginPlay();

	// 速度/血量基准（兜底；waved 敌人随后会被 WaveManager 调 InitCombatBaseline 用乘过倍率的值覆盖）
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		BaseWalkSpeed = Move->MaxWalkSpeed;

		// 群体避让（RVO）：直线转向移动模型下防止丧尸互相顶住卡死
		Move->SetAvoidanceEnabled(true);
	}
	MaxHP = CurrentHP;

	// 丧尸是近战，不生成武器。启动近战攻击轮询。
	GetWorld()->GetTimerManager().SetTimer(
		MeleeTimer, this, &AShooterNPC::MeleeAttackTick, AttackInterval, true);

	// 丧尸动画：载入动画包并启动步态轮询（各端本地驱动，速度来自复制的移动状态）
	SetupZombieAnims();
	GetWorld()->GetTimerManager().SetTimer(
		AnimTimer, this, &AShooterNPC::UpdateLocomotionAnim, 0.12f, true);

	// 种类特殊能力：仅服务器驱动（速度/自回血/对核心伤害都需权威化）
	if (HasAuthority())
	{
		if (bSprinter)
		{
			GetWorld()->GetTimerManager().SetTimer(
				SprintTimer, this, &AShooterNPC::SprintBurstStart, SprintInterval, true);
		}
		if (bSelfHealing)
		{
			GetWorld()->GetTimerManager().SetTimer(
				SelfHealTimer, this, &AShooterNPC::SelfHealTick, 1.0f, true);
		}
		if (bRangedAttacker)
		{
			GetWorld()->GetTimerManager().SetTimer(
				RangedTimer, this, &AShooterNPC::RangedAttackTick, RangedInterval, true);
		}
	}
}

void AShooterNPC::InitCombatBaseline()
{
	// 在波次倍率应用之后调用：把当前速度作为基准、当前血量作为回血上限
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		BaseWalkSpeed = Move->MaxWalkSpeed;
	}
	MaxHP = CurrentHP;

	// 击杀得分按难度（血量）自动换算：越肉越值钱，且随波次血量倍率水涨船高。
	// 普通100血=10分、快攻50血=5分、肉盾150血=15分；第5波普通160血≈16分。
	ScoreValue = FMath::Max(5, FMath::RoundToInt(MaxHP / 10.0f));
}

void AShooterNPC::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear timers
	GetWorld()->GetTimerManager().ClearTimer(DeathTimer);
	GetWorld()->GetTimerManager().ClearTimer(MeleeTimer);
	GetWorld()->GetTimerManager().ClearTimer(SprintTimer);
	GetWorld()->GetTimerManager().ClearTimer(SprintEndTimer);
	GetWorld()->GetTimerManager().ClearTimer(SelfHealTimer);
	GetWorld()->GetTimerManager().ClearTimer(RangedTimer);
}

void AShooterNPC::MeleeAttackTick()
{
	if (bIsDead)
	{
		return;
	}

	// 啃食核心：贴近即咬（动画由步态轮询的循环撕咬状态负责；这里只做服务器权威伤害结算）
	// （不依赖核心侧 DamageZone 重叠检测——其半径被蓝图实例旧序列化值污染是历史病根）
	if (HasAuthority())
	{
		if (ABaseCore* Core = Cast<ABaseCore>(UGameplayStatics::GetActorOfClass(GetWorld(), ABaseCore::StaticClass())))
		{
			if (FVector::Dist2D(GetActorLocation(), Core->GetActorLocation()) <= CoreAttackRange)
			{
				Core->DamageCore(CoreGnawDPS * AttackInterval);
			}
		}
	}

	// 找最近的玩家
	APawn* Target = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (APawn* P = PC->GetPawn())
			{
				const float DSq = FVector::DistSquared(GetActorLocation(), P->GetActorLocation());
				if (DSq < BestDistSq)
				{
					BestDistSq = DSq;
					Target = P;
				}
			}
		}
	}

	if (!Target)
	{
		return;
	}

	// 在近战范围内 → 挥爪 + 造成伤害（正在循环撕咬核心时不打断循环）
	if (FMath::Sqrt(BestDistSq) <= MeleeRange)
	{
		if (LocoState != 4)
		{
			PlayAttackAnim();
		}
		UGameplayStatics::ApplyDamage(Target, MeleeDamage, GetController(), this, UDamageType::StaticClass());
	}
}

void AShooterNPC::SetupZombieAnims()
{
	const FString Base = TEXT("/Game/ZombieAnimationPack/Animations/Mannequin_UE5/");
	auto LoadAnim = [&Base](const TCHAR* Name) -> UAnimSequence*
	{
		const FString Path = Base + Name + TEXT(".") + Name;
		return LoadObject<UAnimSequence>(nullptr, *Path);
	};

	IdleAnim = LoadAnim(TEXT("anim_Idle_A"));
	RunAnim  = LoadAnim(TEXT("anim_Run_A"));

	// 走路三选一：按实例 ID 固定挑选，让尸潮步态自带差异
	static const TCHAR* Walks[3] = { TEXT("anim_Walk_A"), TEXT("anim_Walk_B"), TEXT("anim_Walk_C") };
	WalkAnim = LoadAnim(Walks[GetUniqueID() % 3]);

	AttackAnims.Reset();
	// 只用站立攻击 A/B——C/D 是对地面受害者的俯身攻击（包主题是医院/病床），站着咬核心会像摔倒
	static const TCHAR* Attacks[2] = { TEXT("anim_Attack_A"), TEXT("anim_Attack_B") };
	for (const TCHAR* Name : Attacks)
	{
		if (UAnimSequence* A = LoadAnim(Name))
		{
			// 强制锁根骨：攻击段的"前扑位移"烘在根骨上，单节点播放会导致
			// 网格扑出胶囊（穿模）→ 循环回卷瞬移回来。锁根后变为原地上身前扑
			A->bForceRootLock = true;
			AttackAnims.Add(A);
		}
	}

	// 骨架适配：动画包用自带的 SKEL_Mannequin 副本，与工程 SK_Mannequin 不是同一资产。
	// 骨架不合时把网格换成包内同款 UE5 人偶（视觉一致），并原样保留彩色材质（种类辨识度）
	USkeletalMeshComponent* M = GetMesh();
	USkeletalMesh* Cur = M ? M->GetSkeletalMeshAsset() : nullptr;
	if (WalkAnim && Cur && WalkAnim->GetSkeleton() != Cur->GetSkeleton())
	{
		if (USkeletalMesh* PackMesh = LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/ZombieAnimationPack/Demo/EpicContent/Mannequin_UE5/Meshes/SK_Manny_Simple.SK_Manny_Simple")))
		{
			TArray<UMaterialInterface*> OldMats;
			for (int32 i = 0; i < M->GetNumMaterials(); ++i)
			{
				OldMats.Add(M->GetMaterial(i));
			}

			M->SetSkeletalMesh(PackMesh);

			for (int32 i = 0; i < M->GetNumMaterials(); ++i)
			{
				UMaterialInterface* Mat = OldMats.IsValidIndex(i) ? OldMats[i] : (OldMats.Num() > 0 ? OldMats[0] : nullptr);
				if (Mat)
				{
					M->SetMaterial(i, Mat);
				}
			}
		}
	}
}

void AShooterNPC::UpdateLocomotionAnim()
{
	if (bIsDead || !GetMesh())
	{
		return;
	}

	// 攻击动画独占期内不切步态
	if (GetWorld()->GetTimeSeconds() < AttackAnimUntil)
	{
		return;
	}

	// 贴身啃核心：循环播放固定攻击动画（LocoState=4）。循环内无重启 → 撕咬平滑不卡帧；
	// 每只按实例 ID 固定选一段，尸群动作仍有差异
	if (ABaseCore* Core = Cast<ABaseCore>(UGameplayStatics::GetActorOfClass(GetWorld(), ABaseCore::StaticClass())))
	{
		if (FVector::Dist2D(GetActorLocation(), Core->GetActorLocation()) <= CoreAttackRange)
		{
			if (LocoState != 4 && AttackAnims.Num() > 0)
			{
				if (UAnimSequence* Anim = AttackAnims[GetUniqueID() % AttackAnims.Num()])
				{
					GetMesh()->PlayAnimation(Anim, true);
					LocoState = 4;
				}
			}
			return;
		}
	}

	const float Speed = GetVelocity().Size2D();
	const uint8 NewState = Speed > 260.0f ? 3 : (Speed > 20.0f ? 2 : 1);
	if (NewState == LocoState)
	{
		return;
	}
	LocoState = NewState;

	UAnimSequence* Anim = (NewState == 3) ? RunAnim : (NewState == 2 ? WalkAnim : IdleAnim);
	if (Anim)
	{
		GetMesh()->PlayAnimation(Anim, true);
	}
}

void AShooterNPC::PlayAttackAnim()
{
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < AttackAnimUntil || AttackAnims.Num() == 0 || bIsDead || !GetMesh())
	{
		return;
	}

	UAnimSequence* Anim = AttackAnims[FMath::RandRange(0, AttackAnims.Num() - 1)];
	if (!Anim)
	{
		return;
	}

	GetMesh()->PlayAnimation(Anim, false);
	AttackAnimUntil = Now + FMath::Min(Anim->GetPlayLength(), 1.4f);
	LocoState = 0;   // 攻击结束后强制重评步态
}

float AShooterNPC::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// ignore if already dead
	if (bIsDead)
	{
		return 0.0f;
	}

	// 忽略友军伤害：来自其他敌人(NPC)的伤害不生效，防止敌人互相误伤
	if (EventInstigator)
	{
		if (APawn* InstPawn = EventInstigator->GetPawn())
		{
			if (InstPawn->IsA(AShooterNPC::StaticClass()))
			{
				return 0.0f;
			}
		}
	}

	// 记录击杀者
	LastInstigator = EventInstigator;

	// 读取攻击者的增益与连击
	UUpgradeComponent* Up = nullptr;
	AShooterCharacter* Attacker = nullptr;
	if (EventInstigator)
	{
		if (APawn* InstPawn = EventInstigator->GetPawn())
		{
			Up = InstPawn->FindComponentByClass<UUpgradeComponent>();
			Attacker = Cast<AShooterCharacter>(InstPawn);
		}
	}

	// 连击伤害倍率
	if (Attacker)
	{
		Damage *= Attacker->GetComboMultiplier();
	}

	// 强化弹药：子弹伤害 +30%/层（可叠加，肉鸽推关核心）
	if (Up)
	{
		Damage *= (1.0f + 0.30f * Up->GetUpgradeCount(EUpgradeType::DamageUp));
	}

	// 处决：每层 10% 概率直接秒杀
	if (Up && Up->HasUpgrade(EUpgradeType::InstaKill))
	{
		if (FMath::FRand() < 0.10f * Up->GetUpgradeCount(EUpgradeType::InstaKill))
		{
			Damage = CurrentHP + 1.0f;
		}
	}

	// Reduce HP
	CurrentHP -= Damage;

	// 命中控制效果（未死才挂）
	if (CurrentHP > 0.0f && Up)
	{
		if (Up->HasUpgrade(EUpgradeType::FrostBullet))
		{
			ApplySlow(0.55f, 3.5f);
		}
		if (Up->HasUpgrade(EUpgradeType::PoisonBullet))
		{
			ApplyPoison(12.0f, 5, EventInstigator);
		}
	}

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die();
	}

	return Damage;
}

void AShooterNPC::RecomputeSpeed()
{
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		const float SprintMult = bSprinting ? SprintSpeedMult : 1.0f;
		Move->MaxWalkSpeed = BaseWalkSpeed * SprintMult * SlowFactor;
	}
}

void AShooterNPC::ApplySlow(float SlowPct, float Duration)
{
	if (bIsDead)
	{
		return;
	}

	SlowFactor = FMath::Clamp(1.0f - SlowPct, 0.05f, 1.0f);
	RecomputeSpeed();

	// 到时恢复
	GetWorld()->GetTimerManager().SetTimer(
		SlowTimer, this, &AShooterNPC::RestoreSpeed, Duration, false);
}

void AShooterNPC::RestoreSpeed()
{
	SlowFactor = 1.0f;
	RecomputeSpeed();
}

void AShooterNPC::SprintBurstStart()
{
	if (bIsDead)
	{
		return;
	}
	bSprinting = true;
	RecomputeSpeed();
	GetWorld()->GetTimerManager().SetTimer(
		SprintEndTimer, this, &AShooterNPC::SprintBurstEnd, SprintDuration, false);
}

void AShooterNPC::SprintBurstEnd()
{
	bSprinting = false;
	RecomputeSpeed();
}

void AShooterNPC::SelfHealTick()
{
	if (bIsDead)
	{
		return;
	}
	CurrentHP = FMath::Min(MaxHP, CurrentHP + SelfHealPerSec);
}

void AShooterNPC::RangedAttackTick()
{
	if (bIsDead)
	{
		return;
	}

	// 在射程内则持续轰击中央核心（远程施压，玩家须优先点掉）
	if (ABaseCore* Core = Cast<ABaseCore>(UGameplayStatics::GetActorOfClass(GetWorld(), ABaseCore::StaticClass())))
	{
		const float Dist = FVector::Dist(GetActorLocation(), Core->GetActorLocation());
		if (Dist <= RangedRange)
		{
			Core->DamageCore(RangedDamage);
		}
	}
}

void AShooterNPC::ApplyPoison(float DamagePerTick, int32 Ticks, AController* Inst)
{
	if (bIsDead)
	{
		return;
	}

	PoisonDamage = DamagePerTick;
	PoisonTicksLeft = Ticks;
	PoisonInstigator = Inst;

	// 每秒一跳
	GetWorld()->GetTimerManager().SetTimer(
		PoisonTimer, this, &AShooterNPC::PoisonTick, 1.0f, true);
}

void AShooterNPC::PoisonTick()
{
	if (bIsDead)
	{
		GetWorld()->GetTimerManager().ClearTimer(PoisonTimer);
		return;
	}

	CurrentHP -= PoisonDamage;
	LastInstigator = PoisonInstigator;
	PoisonTicksLeft--;

	if (PoisonTicksLeft <= 0)
	{
		GetWorld()->GetTimerManager().ClearTimer(PoisonTimer);
	}

	if (CurrentHP <= 0.0f)
	{
		GetWorld()->GetTimerManager().ClearTimer(PoisonTimer);
		Die();
	}
}

void AShooterNPC::AttachWeaponMeshes(AShooterWeapon* WeaponToAttach)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	WeaponToAttach->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	WeaponToAttach->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	WeaponToAttach->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, ThirdPersonWeaponSocket);
}

void AShooterNPC::PlayFiringMontage(UAnimMontage* Montage)
{
	// unused
}

void AShooterNPC::AddWeaponRecoil(float Recoil)
{
	// unused
}

void AShooterNPC::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	// unused
}

FVector AShooterNPC::GetWeaponTargetLocation()
{
	// start aiming from the camera location
	const FVector AimSource = GetFirstPersonCameraComponent()->GetComponentLocation();

	FVector AimDir, AimTarget = FVector::ZeroVector;

	// do we have an aim target?
	if (CurrentAimTarget)
	{
		// target the actor location
		AimTarget = CurrentAimTarget->GetActorLocation();

		// apply a vertical offset to target head/feet
		AimTarget.Z += FMath::RandRange(MinAimOffsetZ, MaxAimOffsetZ);

		// get the aim direction and apply randomness in a cone
		AimDir = (AimTarget - AimSource).GetSafeNormal();
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(AimDir, AimVarianceHalfAngle);

		
	} else {

		// no aim target, so just use the camera facing
		AimDir = UKismetMathLibrary::RandomUnitVectorInConeInDegrees(GetFirstPersonCameraComponent()->GetForwardVector(), AimVarianceHalfAngle);

	}

	// calculate the unobstructed aim target location
	AimTarget = AimSource + (AimDir * AimRange);

	// run a visibility trace to see if there's obstructions
	FHitResult OutHit;

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, AimSource, AimTarget, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterNPC::AddWeaponClass(const TSubclassOf<AShooterWeapon>& InWeaponClass)
{
	// unused
}

void AShooterNPC::OnWeaponActivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnWeaponDeactivated(AShooterWeapon* InWeapon)
{
	// unused
}

void AShooterNPC::OnSemiWeaponRefire()
{
	// are we still shooting?
	if (bIsShooting && Weapon)
	{
		// fire the weapon
		Weapon->StartFiring();
	}
}

void AShooterNPC::Die()
{
	// ignore if already dead
	if (bIsDead)
	{
		return;
	}

	// 置死亡标记：复制到客户端 → 各端 OnRep_IsDead 播放死亡表现（P2 也能看到丧尸倒下）
	bIsDead = true;

	// 死亡视觉（服务器端立即执行；客户端由 OnRep_IsDead 执行）
	PlayDeathEffects();

	// ↓↓↓ 以下为服务器权威逻辑，只在服务器执行 ↓↓↓

	// call the delegate
	OnPawnDeath.Broadcast();

	// 广播带击杀者信息+本丧尸得分值的委托（WaveManager 据此结算经验/连击/得分）
	OnPawnDeathWithKiller.Broadcast(LastInstigator, ScoreValue);

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// schedule actor destruction
	GetWorld()->GetTimerManager().SetTimer(DeathTimer, this, &AShooterNPC::DeferredDestruction, DeferredDestructionTime, false);
}

void AShooterNPC::OnRep_IsDead()
{
	// 客户端收到死亡复制：播放死亡表现（布娃娃 + 关碰撞），让 P2 端也看到丧尸真正死掉
	if (bIsDead)
	{
		PlayDeathEffects();
	}
}

void AShooterNPC::PlayDeathEffects()
{
	// grant the death tag to the character
	Tags.Add(DeathTag);

	// disable capsule collision（关掉碰撞后 P2 的子弹不会再打到尸体，避免"打不死"错觉）
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// stop movement
	if (GetCharacterMovement())
	{
		GetCharacterMovement()->StopMovementImmediately();
		GetCharacterMovement()->DisableMovement();
	}

	// enable ragdoll physics on the third person mesh
	GetMesh()->SetCollisionProfileName(RagdollCollisionProfile);
	GetMesh()->SetSimulatePhysics(true);
	GetMesh()->SetPhysicsBlendWeight(1.0f);
}

void AShooterNPC::DeferredDestruction()
{
	Destroy();
}

void AShooterNPC::StartShooting(AActor* ActorToShoot)
{
	// save the aim target
	CurrentAimTarget = ActorToShoot;

	// raise the flag
	bIsShooting = true;

	// signal the weapon
	if (Weapon)
	{
		Weapon->StartFiring();
	}
}

void AShooterNPC::StopShooting()
{
	// lower the flag
	bIsShooting = false;

	// signal the weapon
	if (Weapon)
	{
		Weapon->StopFiring();
	}
}
