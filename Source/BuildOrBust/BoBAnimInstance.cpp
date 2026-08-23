// Build or Bust — 敌人动画实例实现。

#include "BoBAnimInstance.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "BonePose.h"
#include "BoneContainer.h"

FQuat UBoBAnimInstance::EulerToQuat(const FVector& DegXYZ)
{
	// 逐轴组合而不是塞进 FRotator：FRotator 的分量是 Pitch/Yaw/Roll，
	// 和这里的 X/Y/Z 不是一回事，直接塞会静默转错轴
	const FQuat QX(FVector::XAxisVector, FMath::DegreesToRadians(DegXYZ.X));
	const FQuat QY(FVector::YAxisVector, FMath::DegreesToRadians(DegXYZ.Y));
	const FQuat QZ(FVector::ZAxisVector, FMath::DegreesToRadians(DegXYZ.Z));
	return QZ * QY * QX;
}

void UBoBAnimInstance::SetLoopSequence(UAnimSequence* InSequence)
{
	Sequence = InSequence;
}

void UBoBAnimInstance::SetPoseOverride(const TMap<FName, FVector>& InPose)
{
	Pose.Reset();
	for (const TPair<FName, FVector>& P : InPose)
	{
		if (P.Key.IsNone() || P.Value.IsNearlyZero())
		{
			continue;
		}
		Pose.Add(P.Key, EulerToQuat(P.Value));
	}
}

FAnimInstanceProxy* UBoBAnimInstance::CreateAnimInstanceProxy()
{
	return new FBoBAnimProxy(this);
}

void FBoBAnimProxy::PreUpdate(UAnimInstance* InAnimInstance, float DeltaSeconds)
{
	FAnimInstanceProxy::PreUpdate(InAnimInstance, DeltaSeconds);

	// 唯一允许从游戏线程往工作线程搬数据的地方
	if (const UBoBAnimInstance* Owner = Cast<UBoBAnimInstance>(InAnimInstance))
	{
		Sequence = Owner->Sequence;
		Pose = Owner->Pose;
	}
}

void FBoBAnimProxy::Update(float DeltaSeconds)
{
	FAnimInstanceProxy::Update(DeltaSeconds);

	if (Sequence)
	{
		const float Len = Sequence->GetPlayLength();
		if (Len > KINDA_SMALL_NUMBER)
		{
			CurrentTime = FMath::Fmod(CurrentTime + DeltaSeconds * PlayRate, Len);
			if (CurrentTime < 0.f)
			{
				CurrentTime += Len;
			}
		}
	}
}

bool FBoBAnimProxy::Evaluate(FPoseContext& Output)
{
	// 先铺参考姿势，再把动画盖上去。这一步不能只在"没有动画"时做——
	// FPoseContext 刚拿到手时内容是未初始化的内存，而导出的片段只给
	// 真正会动的骨骼打了关键帧，没被片段覆盖到的骨头就会留着一堆随机值。
	// 症状是模型在游戏里整个塌掉（看不见，但胶囊还能打中），
	// 以及同一段姿态每次量出来的角度都不一样——量的其实是随机内存。
	Output.ResetToRefPose();

	if (Sequence)
	{
		FAnimationPoseData PoseData(Output);
		FAnimExtractContext Ctx(static_cast<double>(CurrentTime), false);
		Sequence->GetAnimationPose(PoseData, Ctx);
	}

	if (Pose.Num() > 0)
	{
		const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
		for (const TPair<FName, FQuat>& P : Pose)
		{
			// 用 FBoneReference 做名字到 compact 索引的解析。
			// 别自己拿 GetPoseBoneIndexForBoneName 的结果去构造 FMeshPoseBoneIndex：
			// 那是两套不同的索引空间，混用不会报错，只会去转别的骨头——
			// 症状是同样的输入每次跑出来的角度都不一样，有的骨头压根没动。
			FBoneReference Ref(P.Key);
			Ref.Initialize(Bones);
			if (!Ref.IsValidToEvaluate(Bones))
			{
				continue;
			}
			// 后乘 = 在骨骼自身的局部坐标系里再转一下，
			// 这样叠在行走循环之上而不是把它顶掉
			FTransform& T = Output.Pose[Ref.GetCompactPoseIndex(Bones)];
			T.SetRotation(T.GetRotation() * P.Value);
			T.NormalizeRotation();
		}
	}
	return true;
}
