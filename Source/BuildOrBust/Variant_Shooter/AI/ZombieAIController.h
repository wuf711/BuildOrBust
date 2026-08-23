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

	// 距目标中心多远停止移动输入。核心水晶在啃食高度约 410 宽，丧尸嘴 = StopDistance - 胶囊半径(~34)。
	// 设 350 让嘴啃进水晶内部一点（消除"啃空气"的视觉缝隙）；核心隐形碰撞球 r290 兜底防钻太深
	UPROPERTY(EditAnywhere, Category="Zombie")
	float StopDistance = 350.0f;

	//~ 前探避障（三线扇形，只测静态几何）
	UPROPERTY(EditAnywhere, Category="Zombie|Avoidance")
	float ProbeDistance = 220.0f;   // 前探距离
	UPROPERTY(EditAnywhere, Category="Zombie|Avoidance")
	float ProbeRadius = 40.0f;      // 探测球半径
	UPROPERTY(EditAnywhere, Category="Zombie|Avoidance")
	float ProbeAngle = 35.0f;       // 左右探线与正前的夹角
	UPROPERTY(EditAnywhere, Category="Zombie|Avoidance")
	float ProbeTurnAngle = 50.0f;   // 正前受阻时的实际偏转角

public:
	/**
	 *  玩家进到这个半径内就抢走仇恨，不再一路奔核心。
	 *  1200 约等于两个身位加一点余量——够近才算"挡路"，
	 *  设太大会变成全场追人，守家的骨架就散了。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="AI")
	float PlayerAggroRadius = 1200.f;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	// 当前追击目标（核心或玩家）
	TWeakObjectPtr<AActor> TargetActor;

	FTimerHandle RetargetTimer;

	// 选目标：优先中央核心，无核心则最近玩家
	void RefreshTarget();

	// 三线扇形前探，返回避开静态障碍后的移动方向
	FVector ComputeAvoidanceDir(APawn* Self, const FVector& DesiredDir) const;

	//~ 卡墙脱困：正面顶住掩体等障碍时侧向绕行
	FVector LastStallCheckPos = FVector::ZeroVector;   // 上次推进检查时的位置
	float NextStallCheckTime = 0.0f;                   // 下次推进检查时间（世界秒）
	float DetourUntil = 0.0f;                          // 侧向绕行截止时间
	float DetourSign = 1.0f;                           // 绕行方向（±1）
};
