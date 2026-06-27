#include "BoBJumpPad.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/Character.h"

ABoBJumpPad::ABoBJumpPad()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	// 触发盒作为根，略高于地面，玩家走入即弹
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetBoxExtent(FVector(90.0f, 90.0f, 60.0f));
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);

	// 视觉网格：扁平台，碰撞关闭（只靠触发盒），网格/材质在生成时指定
	PadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PadMesh"));
	PadMesh->SetupAttachment(Trigger);
	PadMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ABoBJumpPad::BeginPlay()
{
	Super::BeginPlay();

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ABoBJumpPad::OnTriggerBeginOverlap);
}

void ABoBJumpPad::OnTriggerBeginOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& Sweep)
{
	ACharacter* Char = Cast<ACharacter>(OtherActor);
	if (!Char)
	{
		return;
	}

	// 只弹玩家：丧尸由 AI 控制器操控，IsPlayerControlled() 为假 → 不受弹跳台影响
	if (!Char->IsPlayerControlled())
	{
		return;
	}

	// 由服务器权威发起弹射，LaunchCharacter 的速度会复制到客户端（监听服务器双人都生效）
	if (!HasAuthority())
	{
		return;
	}

	FVector LaunchVel(0.0f, 0.0f, LaunchZSpeed);
	const bool bHorizontal = !FMath::IsNearlyZero(LaunchForwardSpeed);
	if (bHorizontal)
	{
		LaunchVel += GetActorForwardVector() * LaunchForwardSpeed;
	}

	// 垂直弹射保留水平动量(XYOverride=false)；含前冲分量时覆盖水平速度
	Char->LaunchCharacter(LaunchVel, bHorizontal, true);
}
