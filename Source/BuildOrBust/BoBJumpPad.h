#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoBJumpPad.generated.h"

class UBoxComponent;
class UStaticMeshComponent;

/**
 *  弹跳台：玩家踩上去被向上弹射，可借此跳上掩体顶、构造更立体好玩的地形。
 *  纯 C++ 逻辑，放进关卡即可（视觉网格/材质在生成时指定）。
 */
UCLASS()
class BUILDORBUST_API ABoBJumpPad : public AActor
{
	GENERATED_BODY()

public:
	ABoBJumpPad();

	/** 向上弹射初速度(cm/s)。约 700 跳上 170 高掩体，1100 更夸张可玩 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JumpPad")
	float LaunchZSpeed = 1100.0f;

	/** 沿 Pad 正前方(+X)的水平弹射分量，0 = 纯垂直 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="JumpPad")
	float LaunchForwardSpeed = 0.0f;

protected:
	/** 触发盒：检测玩家踩上 */
	UPROPERTY(VisibleAnywhere, Category="JumpPad")
	UBoxComponent* Trigger;

	/** 弹跳台视觉网格（生成时指定网格/材质） */
	UPROPERTY(VisibleAnywhere, Category="JumpPad")
	UStaticMeshComponent* PadMesh;

	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);
};
