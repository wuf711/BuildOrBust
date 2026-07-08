#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "ZombieAIController.generated.h"

/**
 *  丧尸 AI（重构版）：直线转向移动，不依赖导航网格。
 *  每帧朝目标（中央核心，无核心则最近玩家）AddMovementInput，
 *  物理碰撞自然滑墙绕障，RVO 群体避让防互卡（在 ShooterNPC 开启）。
 *  攻击由 ShooterNPC::MeleeAttackTick 主动执行（贴近核心即啃食），
 *  与移动解耦——彻底绕开导航网格数据在 WP 关卡上的持久化/重建/孤岛问题。
 */
UCLASS()
class BUILDORBUST_API AZombieAIController : public AAIController
{
	GENERATED_BODY()

public:
	AZombieAIController();

	virtual void Tick(float DeltaSeconds) override;

	// 目标刷新间隔（核心不动，兜底追玩家时需要刷新最近者）
	UPROPERTY(EditAnywhere, Category="Zombie")
	float RetargetInterval = 1.0f;

	// 距目标中心多远停止移动输入（核心碰撞球半径~420；贴到 440 让丧尸紧挨水晶啃食）
	UPROPERTY(EditAnywhere, Category="Zombie")
	float StopDistance = 440.0f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	// 当前追击目标（核心或玩家）
	TWeakObjectPtr<AActor> TargetActor;

	FTimerHandle RetargetTimer;

	// 选目标：优先中央核心，无核心则最近玩家
	void RefreshTarget();

	//~ 卡墙脱困：正面顶住掩体等障碍时侧向绕行
	FVector LastStallCheckPos = FVector::ZeroVector;   // 上次推进检查时的位置
	float NextStallCheckTime = 0.0f;                   // 下次推进检查时间（世界秒）
	float DetourUntil = 0.0f;                          // 侧向绕行截止时间
	float DetourSign = 1.0f;                           // 绕行方向（±1）
};
