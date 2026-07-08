#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoBJumpPad.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

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
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JumpPad")
	float LaunchZSpeed = 1100.0f;

	/** 沿 Pad 正前方(+X)的水平弹射分量，0 = 纯垂直 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "JumpPad")
	float LaunchForwardSpeed = 0.0f;

protected:
	/** 触发盒：检测玩家踩上 */
	UPROPERTY(VisibleAnywhere, Category = "JumpPad")
	UBoxComponent* Trigger;

	/** 弹跳台视觉网格（生成时指定网格/材质） */
	UPROPERTY(VisibleAnywhere, Category = "JumpPad")
	UStaticMeshComponent* PadMesh;

	/** 在编辑器指定的静态“色块/拼色”基础材质或材质实例（用于创建动态实例） */
	UPROPERTY(EditAnywhere, Category = "JumpPad")
	UMaterialInterface* ColorBlockMaterial;

	/** 是否在运行时覆盖颜色（若材质有参数则生效） */
	UPROPERTY(EditAnywhere, Category = "JumpPad")
	bool bOverrideColor = true;

	/** 主色（用于覆盖材质参数） */
	UPROPERTY(EditAnywhere, Category = "JumpPad", meta = (HideAlphaChannel))
	FLinearColor PrimaryColor = FLinearColor(0.20f, 0.80f, 1.00f);

	/** 次色（双色样式使用） */
	UPROPERTY(EditAnywhere, Category = "JumpPad", meta = (HideAlphaChannel))
	FLinearColor SecondaryColor = FLinearColor(0.08f, 0.08f, 0.08f);

	/** 简单风格：0 = 纯色, 1 = 双色, 2 = 条纹（需材质支持对应参数） */
	UPROPERTY(EditAnywhere, Category = "JumpPad")
	int32 StyleIndex = 0;

	/** 运行时创建的动态材质实例（每个插槽一个） */
	UPROPERTY(Transient)
	TArray<UMaterialInstanceDynamic*> DynamicMaterials;

	virtual void BeginPlay() override;

	UFUNCTION()
	void OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep);
};