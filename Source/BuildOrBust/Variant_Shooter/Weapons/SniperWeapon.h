#pragma once

#include "CoreMinimal.h"
#include "BoBFabWeapons.h"
#include "SniperWeapon.generated.h"

/** 狙击弹：一击重伤（曳光/命中爆闪沿用基类 C++ 特效） */
UCLASS()
class BUILDORBUST_API ASniperProjectile : public AShooterProjectile
{
	GENERATED_BODY()

public:
	ASniperProjectile();

protected:
	virtual void BeginPlay() override;
};

/** 蚀日狙击枪：高伤栓动，第 5 波起从"狙击枪箱"获取（传世级武器） */
UCLASS()
class BUILDORBUST_API ASniperWeapon : public ABoBFabWeapon
{
	GENERATED_BODY()

public:
	ASniperWeapon();
};
