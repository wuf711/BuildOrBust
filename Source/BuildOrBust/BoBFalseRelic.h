// Build or Bust — 阶段一：无效遗构与残存接收端口。规格书 6.2。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoBFalseRelic.generated.h"

class UStaticMeshComponent;
class USphereComponent;
class AShooterCharacter;
class ABoss_CS07;

/**
 *  无效遗构。阶段一在场地边缘生成 3 处，闪红光。
 *
 *  刻意不走 ELootKind／背包格子那套：它不该占背包、不该折配额、
 *  也不该被结算成得分。玩家的**动作**和平时搬遗构一样（走过去捡、搬到端口），
 *  但它在经济系统里根本不存在——规格书那句"平时录入的动作其实是在给这东西
 *  debug"，靠的是动作相同，不是道具相同。
 */
UCLASS()
class BUILDORBUST_API ABoBFalseRelic : public AActor
{
	GENERATED_BODY()

public:
	ABoBFalseRelic();

	/** 当前扛着它的人，没人扛就是 nullptr */
	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	AShooterCharacter* GetHolder() const { return Holder; }

	/** 端口收下它：从持有者身上摘掉并销毁 */
	void ConsumeAtPort();

	/** 这个人身上有没有扛着无效遗构 */
	static ABoBFalseRelic* FindHeldBy(const AShooterCharacter* Who);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnPickupOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Trigger;

	UPROPERTY(ReplicatedUsing = OnRep_Holder)
	TObjectPtr<AShooterCharacter> Holder;

	UFUNCTION()
	void OnRep_Holder();

	/** 挂到持有者身上／落回地面 */
	void AttachToHolder();
};

/**
 *  Boss 侧面的残存接收端口。扛着无效遗构走进来就录入，烧 15% 抗性。
 *  规格书说端口是"残存"的——所以它不做提示音效之外的引导，找不找得到是玩家的事。
 */
UCLASS()
class BUILDORBUST_API ABoBBossPort : public AActor
{
	GENERATED_BODY()

public:
	ABoBBossPort();

	/** 录入后把结果记到哪个 Boss 上 */
	UPROPERTY(BlueprintReadWrite, Category = "BoB|Boss")
	TObjectPtr<ABoss_CS07> BossOwner;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnPortOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& Sweep);

	UPROPERTY(VisibleAnywhere) TObjectPtr<USphereComponent> Trigger;
};
