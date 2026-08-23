// Build or Bust — 阶段二：双人非对称能量柱。规格书 6.2。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoBEnergyPillar.generated.h"

class UStaticMeshComponent;
class ABoss_CS07;

/** 几何外壳形状。玩家 A 看得见这个 */
UENUM(BlueprintType)
enum class EBoBPillarShape : uint8
{
	Triangle UMETA(DisplayName = "三角"),
	Spiral   UMETA(DisplayName = "螺旋"),
	Hexagon  UMETA(DisplayName = "六边"),
	Square   UMETA(DisplayName = "方"),
};

/** 晶体纹路。玩家 B 看得见这个 */
UENUM(BlueprintType)
enum class EBoBPillarVein : uint8
{
	Split    UMETA(DisplayName = "开裂"),
	Braid    UMETA(DisplayName = "绞合"),
	Dotted   UMETA(DisplayName = "断续"),
	Radial   UMETA(DisplayName = "放射"),
};

/**
 *  能量柱。
 *
 *  非对称的实现要点：**四根柱子的属性必须互相遮蔽**。
 *  弱点柱之外还得有一根同形状的、一根同纹路的，两人才都锁不定唯一解，
 *  非得对一次话不可。四根全不同形状不同纹路的话，A 一个人看形状就能猜中，
 *  这个机制就白设了。
 */
UCLASS()
class BUILDORBUST_API ABoBEnergyPillar : public AActor
{
	GENERATED_BODY()

public:
	ABoBEnergyPillar();

	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	EBoBPillarShape GetShape() const { return Shape; }

	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	EBoBPillarVein GetVein() const { return Vein; }

	void Setup(ABoss_CS07* InBoss, EBoBPillarShape InShape, EBoBPillarVein InVein, bool bInTarget);

	static const TCHAR* ShapeName(EBoBPillarShape S);
	static const TCHAR* VeinName(EBoBPillarVein V);

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(Replicated) EBoBPillarShape Shape = EBoBPillarShape::Triangle;
	UPROPERTY(Replicated) EBoBPillarVein Vein = EBoBPillarVein::Split;
	UPROPERTY(Replicated) bool bIsTarget = false;

	UPROPERTY() TObjectPtr<ABoss_CS07> BossOwner;

	bool bResolved = false;
};
