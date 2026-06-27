#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ZombieAIController.generated.h"

/**
 *  极简丧尸 AI：不依赖 StateTree/感知，持续奔向最近的玩家。
 */
UCLASS()
class BUILDORBUST_API AZombieAIController : public AAIController
{
	GENERATED_BODY()

public:
	AZombieAIController();

	// 重新寻路间隔
	UPROPERTY(EditAnywhere, Category="Zombie")
	float ChaseInterval = 0.5f;

	// 停止距离（接近到此距离即停，由近战伤害负责攻击）。设大一些让丧尸停在基地表面外，不钻进基地模型
	UPROPERTY(EditAnywhere, Category="Zombie")
	float AcceptanceRadius = 300.0f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	FTimerHandle ChaseTimer;
	void ChaseTick();
};
