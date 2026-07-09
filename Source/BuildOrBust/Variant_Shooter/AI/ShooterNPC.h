// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BuildOrBustCharacter.h"
#include "ShooterWeaponHolder.h"
#include "ShooterNPC.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FPawnDeathDelegate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FPawnDeathWithKillerDelegate, AController*, Killer, int32, ScoreValue);

class AShooterWeapon;
class UAnimSequence;

/**
 *  A simple AI-controlled shooter game NPC
 *  Executes its behavior through a StateTree managed by its AI Controller
 *  Holds and manages a weapon
 */
UCLASS(abstract)
class BUILDORBUST_API AShooterNPC : public ABuildOrBustCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()

public:

	/** 构造函数：指定丧尸AI控制器 */
	AShooterNPC();

	/** Current HP for this character. It dies if it reaches zero through damage */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Damage")
	float CurrentHP = 100.0f;

	/** 击杀本丧尸给玩家的基础得分（不同种类可不同；越难越值钱）。会再乘连击/得分增益。 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Score")
	int32 ScoreValue = 10;

	//~ 近战丧尸属性（不同种类用不同数值）

	/** 近战伤害 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee")
	float MeleeDamage = 15.0f;

	/** 近战触发距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee")
	float MeleeRange = 180.0f;

	/** 攻击间隔（越小攻速越快） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee")
	float AttackInterval = 1.2f;

	/** 啃食核心的触发距离（到核心中心的水平距离；核心水晶表面~420，贴上才啃） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee")
	float CoreAttackRange = 500.0f;

	/** 啃食核心的每秒伤害（每只丧尸；主动攻击模型，替代旧的核心侧重叠检测） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Melee")
	float CoreGnawDPS = 5.0f;

	//~ 种类特殊能力（在各 BP 变体的 Class Defaults 里勾选开启）

	/** 疾跑：周期性短时提速冲刺（适合快攻型） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special")
	bool bSprinter = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special", meta=(EditCondition="bSprinter"))
	float SprintSpeedMult = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special", meta=(EditCondition="bSprinter"))
	float SprintInterval = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special", meta=(EditCondition="bSprinter"))
	float SprintDuration = 1.2f;

	/** 自回血：周期性回复自身生命（适合肉盾型，更难秒、更值钱） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special")
	bool bSelfHealing = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special", meta=(EditCondition="bSelfHealing"))
	float SelfHealPerSec = 8.0f;

	/** 远程攻击：在射程内持续轰击中央核心（玩家须优先点掉） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special")
	bool bRangedAttacker = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special", meta=(EditCondition="bRangedAttacker"))
	float RangedDamage = 25.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special", meta=(EditCondition="bRangedAttacker"))
	float RangedRange = 1800.0f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Special", meta=(EditCondition="bRangedAttacker"))
	float RangedInterval = 2.0f;

	/** 自回血上限（生成时由 WaveManager 设为满血；运行时基准） */
	float MaxHP = 100.0f;

protected:

	/** Name of the collision profile to use during ragdoll death */
	UPROPERTY(EditAnywhere, Category="Damage")
	FName RagdollCollisionProfile = FName("Ragdoll");

	/** Time to wait after death before destroying this actor */
	UPROPERTY(EditAnywhere, Category="Damage")
	float DeferredDestructionTime = 5.0f;

	/** Team byte for this character */
	UPROPERTY(EditAnywhere, Category="Team")
	uint8 TeamByte = 1;

	/** Actor tag to grant this character when it dies */
	UPROPERTY(EditAnywhere, Category="Team")
	FName DeathTag = FName("Dead");

	/** Pointer to the equipped weapon */
	TObjectPtr<AShooterWeapon> Weapon;

	/** Type of weapon to spawn for this character */
	UPROPERTY(EditAnywhere, Category="Weapon")
	TSubclassOf<AShooterWeapon> WeaponClass;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category ="Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max range for aiming calculations */
	UPROPERTY(EditAnywhere, Category="Aim")
	float AimRange = 10000.0f;

	/** Cone variance to apply while aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float AimVarianceHalfAngle = 10.0f;

	/** Minimum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float MinAimOffsetZ = -35.0f;

	/** Maximum vertical offset from the target center to apply when aiming */
	UPROPERTY(EditAnywhere, Category="Aim")
	float MaxAimOffsetZ = -60.0f;

	/** Actor currently being targeted */
	TObjectPtr<AActor> CurrentAimTarget;

	/** If true, this character is currently shooting its weapon */
	bool bIsShooting = false;

	/** If true, this character has already died（复制到客户端，让 P2 也能看到丧尸倒下、失去碰撞、正确计数） */
	UPROPERTY(ReplicatedUsing = OnRep_IsDead)
	bool bIsDead = false;

	/** The controller that landed the killing blow */
	TObjectPtr<AController> LastInstigator = nullptr;

	/** Deferred destruction on death timer */
	FTimerHandle DeathTimer;

	//~ 移动速度状态（基准速度 × 疾跑倍率 × 减速系数，统一计算避免冲突）
	float BaseWalkSpeed = 0.0f;     // 生成时基准速度（含波次速度倍率）
	bool bSprinting = false;        // 是否处于疾跑冲刺
	float SlowFactor = 1.0f;        // 冰冻减速系数（1=不减速）
	FTimerHandle SlowTimer;
	void RecomputeSpeed();          // 按当前状态重算 MaxWalkSpeed

	float PoisonDamage = 0.0f;
	int32 PoisonTicksLeft = 0;
	TObjectPtr<AController> PoisonInstigator = nullptr;
	FTimerHandle PoisonTimer;

	void RestoreSpeed();
	void PoisonTick();

	//~ 近战攻击
	FTimerHandle MeleeTimer;
	void MeleeAttackTick();

	//~ 丧尸动画驱动（C++ 单节点播放 ZombieAnimationPack 动画，不依赖动画蓝图）
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> IdleAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> WalkAnim;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> RunAnim;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UAnimSequence>> AttackAnims;

	FTimerHandle AnimTimer;        // 步态轮询定时器
	float AttackAnimUntil = 0.0f;  // 攻击动画独占截止（期间不切步态）
	uint8 LocoState = 0;           // 0=未定 1=待机 2=走 3=跑

	/** 载入动画包；若骨架与当前网格不合，换成包内同款 UE5 人偶并保留配色 */
	void SetupZombieAnims();

	/** 按速度切换 待机/走/跑 循环动画 */
	void UpdateLocomotionAnim();

	/** 播放一次随机攻击动画（各端本地播放） */
	void PlayAttackAnim();

	//~ 种类特殊能力的定时器与处理
	FTimerHandle SprintTimer;       // 周期触发疾跑
	FTimerHandle SprintEndTimer;    // 疾跑结束
	FTimerHandle SelfHealTimer;     // 自回血
	FTimerHandle RangedTimer;       // 远程轰击核心
	void SprintBurstStart();
	void SprintBurstEnd();
	void SelfHealTick();
	void RangedAttackTick();

public:
	/** 生成后由 WaveManager 调用：以当前（已乘波次倍率的）速度/血量作为基准与上限 */
	void InitCombatBaseline();

public:

	/** 冰冻：减速 SlowPct(0~1)，持续 Duration 秒 */
	void ApplySlow(float SlowPct, float Duration);

	/** 剧毒：每秒 DamagePerTick 持续 Ticks 次 */
	void ApplyPoison(float DamagePerTick, int32 Ticks, AController* Inst);

public:

	/** Delegate called when this NPC dies */
	FPawnDeathDelegate OnPawnDeath;

	/** Delegate called when this NPC dies, includes the killer controller */
	UPROPERTY(BlueprintAssignable, Category="Damage")
	FPawnDeathWithKillerDelegate OnPawnDeathWithKiller;

	/** 是否已死亡（供 WaveManager 轮询存活判断） */
	UFUNCTION(BlueprintPure, Category="Damage")
	bool IsDead() const { return bIsDead; }

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** 复制属性（bIsDead） */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

public:

	//~Begin IShooterWeaponHolder interface

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) override;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() override;

	/** Gives a weapon of this class to the owner */
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	//~End IShooterWeaponHolder interface

protected:

	/** Called when HP is depleted and the character should die */
	void Die();

	/** 死亡视觉表现（布娃娃 + 关碰撞 + 死亡标签），服务器与客户端都执行 */
	void PlayDeathEffects();

	/** bIsDead 复制到客户端时触发：在客户端也播放死亡表现，让丧尸在 P2 端倒下 */
	UFUNCTION()
	void OnRep_IsDead();

	/** Called after death to destroy the actor */
	void DeferredDestruction();

public:

	/** Signals this character to start shooting at the passed actor */
	void StartShooting(AActor* ActorToShoot);

	/** Signals this character to stop shooting */
	void StopShooting();
};
