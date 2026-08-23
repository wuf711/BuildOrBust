#pragma once

#include "CoreMinimal.h"
#include "ShooterWeapon.h"
#include "ShooterProjectile.h"
#include "BoBFabWeapons.generated.h"

/**
 *  Fab 静态网格武器基类：
 *  FirstPersonMesh 仍用步枪骨架（提供 Muzzle 插槽/动画蓝图），但设为不可见；
 *  实际造型 = 挂在其下的静态网格 VisualMesh（BeginPlay 按包围盒归一到 VisualLen）。
 */
UCLASS(abstract)
class BUILDORBUST_API ABoBFabWeapon : public AShooterWeapon
{
	GENERATED_BODY()

public:
	ABoBFabWeapon();

protected:
	virtual void BeginPlay() override;

	/** 近战充能：>0 时本武器为近战——挥击命中积攒充能，攒满进入限时"觉醒"，期间左键发射斩击波 */
	virtual void Fire() override;

	/** 第一人称造型（静态网格） */
	UPROPERTY(VisibleAnywhere, Category="Visual")
	UStaticMeshComponent* VisualMesh;

	/** 造型资产路径（子类构造里指定） */
	FString VisualMeshPath;

	/** 归一后的最长边长度 cm */
	float VisualLen = 110.0f;

	/** 相对第一人称骨架的摆位 */
	FVector VisualOffset = FVector(24.0f, 0.0f, -4.0f);
	FRotator VisualRot = FRotator(0.0f, -90.0f, 0.0f);

	/** 武器专属染色(BeginPlay 给 VisualMesh 上带颜色的金属质感材质；<0 分量=不染色保留原材质) */
	FLinearColor TintColor = FLinearColor(-1.0f, 0.0f, 0.0f, 1.0f);

	// ===== 近战充能参数（0 = 非近战，走基类射击） =====
	int32 MeleeChargeNeeded = 0;   // 攒满多少次命中触发觉醒
	float MeleeDamage = 60.0f;     // 每次挥击伤害
	float MeleeRange = 250.0f;     // 挥击距离
	float MeleeRadius = 110.0f;    // 挥击判定半径
	float EmpowerDuration = 8.0f;  // 觉醒持续秒数

	int32 MeleeHits = 0;           // 当前充能
	float EmpowerEndTime = -1.0f;  // 觉醒截止（世界秒）
};

// ===== 弹丸 =====

/** 线圈弹 */
UCLASS()
class BUILDORBUST_API ACoilProjectile : public AShooterProjectile
{
	GENERATED_BODY()
public:
	ACoilProjectile();
protected:
	virtual void BeginPlay() override;
	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;

	/** 每 0.5s 一次范围伤害；第 3 秒达伤害峰值 */
	void CoilPulseTick();

	float SpawnTime = 0.0f;
	bool bArmed = false;        // 满 3 秒后碰撞即消失（之前穿透）
	FTimerHandle PulseTimer;
};

/** 光铳弹 */
UCLASS()
class BUILDORBUST_API ALaserBoltProjectile : public AShooterProjectile
{
	GENERATED_BODY()
public:
	ALaserBoltProjectile();
protected:
	virtual void BeginPlay() override;
};

/** 爆震骰：抛物线掷出，命中/落地触发随机三效果之一(爆裂/治疗/迟滞) */
UCLASS()
class BUILDORBUST_API ADiceProjectile : public AShooterProjectile
{
	GENERATED_BODY()
public:
	ADiceProjectile();
protected:
	virtual void BeginPlay() override;
	virtual void NotifyHit(class UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit) override;
	int32 DiceEffect = 0;              // 0爆裂 1治疗 2迟滞
	FLinearColor DiceColor = FLinearColor::White;
	float DicePower = 260.0f;          // 伤害或治疗量
	float DiceRadius = 520.0f;
public:
	static float PendingCharge;        // 蓄力系数 0~1(投掷前由武器写入，BeginPlay 读取后重置)
};

// ===== 武器 =====

/** 奔雷线圈枪：全自动电磁步枪 */
UCLASS()
class BUILDORBUST_API ACoilRifleWeapon : public ABoBFabWeapon
{
	GENERATED_BODY()
public:
	ACoilRifleWeapon();
};

/** 曳星光铳：高伤半自动手枪 */
UCLASS()
class BUILDORBUST_API ALaserPistolWeapon : public ABoBFabWeapon
{
	GENERATED_BODY()
public:
	ALaserPistolWeapon();
};

/** 爆震骰：手中骰子，左键抛出触发随机效果(爆炸物槽) */
UCLASS()
class BUILDORBUST_API ADiceWeapon : public ABoBFabWeapon
{
	GENERATED_BODY()
public:
	ADiceWeapon();
	/** 蓄力进度 0~1；未蓄力返回 -1(供 HUD 画蓄力条) */
	float GetChargeAlpha() const;
protected:
	virtual void Fire() override;         // 抛出后把手里的骰子藏起来、稍后摸出下一颗
	virtual void StartFiring() override;  // 长按=蓄力(不立即扔，保持手里骰子)
	virtual void StopFiring() override;   // 松开=按蓄力值扔出
	float ChargeStartTime = -1.0f;
};
