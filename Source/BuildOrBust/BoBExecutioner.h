// Build or Bust — 处决者。规格书第七部（触发型）。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoBExecutioner.generated.h"

class UCapsuleComponent;
class UStaticMeshComponent;
class AShooterCharacter;

/**
 *  处决者：失谐触顶（100）即刻降临的抹杀机制。
 *
 *  它不是敌人，是罚则。所以刻意**不继承 AShooterNPC**——
 *  不该被算进存活敌人数（算进去这一潮永远清不掉）、不该掉配额、
 *  不该被 Director 的压力阀看见。
 *
 *  行为：沿目标方向直线接近，穿墙，不穿核心，伤害极高。
 *  脱离：失谐压回 70 以下，追一段后消失。
 *  击杀：引进一盏便携谐振灯范围，打爆那盏灯，过载谐振场把它放逐。
 *
 *  规格书特意写明是**一盏不是三盏**：失谐触顶、无敌怪直冲脸的瞬间玩家本能是跑，
 *  要求他狂奔中规划穿过三个光圈的路线，是在恐慌状态下考验空间规划。
 *  一盏之后决策变成当场就能做的取舍——要不要为甩掉它烧掉一件保命道具。
 */
UCLASS()
class BUILDORBUST_API ABoBExecutioner : public AActor
{
	GENERATED_BODY()

public:
	ABoBExecutioner();

	virtual void Tick(float DeltaSeconds) override;

	/** 指派追杀目标 */
	void Hunt(AShooterCharacter* InTarget);

	UFUNCTION(BlueprintPure, Category = "BoB|Executioner")
	AShooterCharacter* GetTarget() const { return Target; }

	/** 谐振场过载：放逐它 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Executioner")
	void Banish(const FString& Why);

	// —— 配置，数值来自规格书第七部 ——

	/** 直线接近速度（厘米/秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Executioner")
	float ApproachSpeed = 420.f;

	/** 贴到多近算接触 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Executioner")
	float TouchRange = 150.f;

	/** 接触伤害。规格书：伤害极高 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Executioner")
	float TouchDamage = 240.f;

	/** 两次接触之间的间隔，避免一秒内连扣好几次 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Executioner")
	float TouchCooldown = 1.5f;

	/** 目标失谐掉到这个值以下就开始脱离计时 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Executioner")
	float DisengageGaze = 70.f;

	/** 脱离条件满足后再追多久才消失 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Executioner")
	float DisengageDelay = 6.f;

	/** 谐振灯的有效半径，和阶段三保持一致 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Executioner")
	float LampRadius = 400.f;

	/** 不穿核心：离核心中心这么近就被挡住 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Executioner")
	float CoreBlockRadius = 900.f;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BoB|Executioner|Visual")
	TObjectPtr<UCapsuleComponent> Hull;

	/** 基类使用引擎圆柱占位；正式资产在派生蓝图中覆盖此组件。 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BoB|Executioner|Visual")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Replicated) TObjectPtr<AShooterCharacter> Target;

private:
	float NextTouchTime = 0.f;
	float DisengageTimer = 0.f;

	/** 上一拍它待在哪盏灯的范围里。那盏灯这一拍没了 = 被打爆了 = 放逐 */
	TWeakObjectPtr<AActor> LampInside;
};
