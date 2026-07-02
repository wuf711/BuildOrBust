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

	// 停止距离（接近到此距离即停，由近战伤害负责攻击）。
	// 核心加了半径~390的实体碰撞球后丧尸物理上最近只能贴到~420，判定放宽到450（仍在核心啃食区450内）
	UPROPERTY(EditAnywhere, Category="Zombie")
	float AcceptanceRadius = 450.0f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	FTimerHandle ChaseTimer;
	void ChaseTick();
};
