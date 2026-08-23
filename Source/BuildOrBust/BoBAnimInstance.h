// Build or Bust — 敌人动画实例：循环播一条序列，并叠加变种的常驻姿态。

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimInstanceProxy.h"
#include "BoBAnimInstance.generated.h"

class UAnimSequence;

/**
 *  真正干活的地方。动画求值跑在工作线程上，所以数据不能直接读
 *  UAnimInstance 的成员——必须在 PreUpdate 里从游戏线程拷进来。
 */
USTRUCT()
struct FBoBAnimProxy : public FAnimInstanceProxy
{
	GENERATED_BODY()

	FBoBAnimProxy() = default;
	explicit FBoBAnimProxy(UAnimInstance* InAnimInstance)
		: FAnimInstanceProxy(InAnimInstance) {}

	virtual void PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds) override;
	virtual void Update(float DeltaSeconds) override;
	virtual bool Evaluate(FPoseContext& Output) override;

private:
	/** 从游戏线程拷过来的循环序列 */
	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Sequence = nullptr;

	/** 骨骼名 -> 局部空间旋转增量 */
	TMap<FName, FQuat> Pose;

	float CurrentTime = 0.f;
	float PlayRate = 1.f;
};

/**
 *  21 个型号共用这一个动画实例。
 *
 *  为什么不用 AnimBP 资产：变种的常驻姿态是数据表里一串
 *  "骨骼名=角度" ——每个变种影响的骨骼都不一样（少则 2 根，多则 11 根），
 *  用 AnimGraph 的 Transform(Modify)Bone 节点就得为每根骨头手摆一个节点，
 *  变种一改数据表就得回去改图。在 C++ 里按 map 遍历一次就完了。
 */
UCLASS()
class BUILDORBUST_API UBoBAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	/** 要循环播放的移动动画 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Anim")
	void SetLoopSequence(UAnimSequence* InSequence);

	/**
	 *  变种常驻姿态。FVector 是局部空间的欧拉角（度），
	 *  分量顺序 X/Y/Z 与 Blender 里 pose_bone.rotation_euler 一致——
	 *  两边共用 tools/make_enemy_csv.py 里那份 POSE 表，改一处两处都变。
	 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Anim")
	void SetPoseOverride(const TMap<FName, FVector>& InPose);

	/** 把一组欧拉角（度）按 X→Y→Z 转成四元数 */
	static FQuat EulerToQuat(const FVector& DegXYZ);

protected:
	virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;

	UPROPERTY(Transient)
	TObjectPtr<UAnimSequence> Sequence = nullptr;

	/** 存四元数而不是欧拉角：每帧求值时不用再算一遍三角函数 */
	TMap<FName, FQuat> Pose;

	friend struct FBoBAnimProxy;
};
