// Build or Bust — 处决者实现。

#include "BoBExecutioner.h"
#include "BaseCore.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

ABoBExecutioner::ABoBExecutioner()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickInterval = 0.1f;
	bReplicates = true;
	SetReplicateMovement(true);
	bAlwaysRelevant = true;

	// 细长三米。穿墙是它的核心机制，所以整体不参与世界碰撞——
	// 用导航或物理去做"穿墙"都会在某个拐角卡住，直接不碰撞最可靠
	Hull = CreateDefaultSubobject<UCapsuleComponent>(TEXT("Hull"));
	Hull->InitCapsuleSize(46.f, 150.f);
	Hull->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetRootComponent(Hull);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Hull);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 正式模型尚未交付。直接引用一个不存在的 /Game 资产会让每次启动都报 CDO 错误，
	// 因此基类只提供稳定可见的引擎占位体；正式视觉应由资产交付后在派生蓝图中覆盖 Body。
	static ConstructorHelpers::FObjectFinder<UStaticMesh> FallbackMesh(
		TEXT("/Engine/BasicShapes/Cylinder"));
	if (FallbackMesh.Succeeded())
	{
		// 引擎圆柱是 100cm 见方且以中心为原点：压扁到 0.46 直径、拉到 3m 高，
		// 再把原点从胶囊中心挪到脚底，对上 Hull 的 46/150。
		Body->SetStaticMesh(FallbackMesh.Object);
		Body->SetRelativeScale3D(FVector(0.92f, 0.92f, 3.0f));
	}
}

void ABoBExecutioner::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABoBExecutioner, Target);
}

void ABoBExecutioner::Hunt(AShooterCharacter* InTarget)
{
	Target = InTarget;
	DisengageTimer = 0.f;
	UE_LOG(LogTemp, Log, TEXT("[BoBExec] 处决者降临，目标 %s"),
		InTarget ? *InTarget->GetName() : TEXT("(空)"));
}

void ABoBExecutioner::Banish(const FString& Why)
{
	UE_LOG(LogTemp, Log, TEXT("[BoBExec] 处决者退场：%s"), *Why);
	Destroy();
}

void ABoBExecutioner::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority()) { return; }

	AShooterCharacter* T = Target;
	if (!T || T->IsDead())
	{
		Banish(FString(TEXT("目标已不在")));
		return;
	}

	// —— 放逐判定：它正待在某盏灯的范围里，而那盏灯这一拍没了 ——
	// 规格书的击杀条件是"引进灯的范围，再打爆那盏灯"。
	// 用"上一拍在谁的范围里、这一拍那个谁还在不在"来判，
	// 就不用去改谐振灯那边的实现，也不用给灯加回调
	AActor* NowInside = nullptr;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->Tags.Contains(FName("BoBFloodlight")) &&
			FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= LampRadius)
		{
			NowInside = *It;
			break;
		}
	}
	if (LampInside.IsValid() && !NowInside && LampInside->IsActorBeingDestroyed())
	{
		Banish(FString(TEXT("谐振场过载")));
		return;
	}
	if (LampInside.IsValid() && !LampInside.Get())
	{
		Banish(FString(TEXT("谐振场过载")));
		return;
	}
	LampInside = NowInside;

	// —— 脱离判定：失谐压回 70 以下，追一段就走 ——
	if (T->GetGaze() < DisengageGaze)
	{
		DisengageTimer += DeltaSeconds;
		if (DisengageTimer >= DisengageDelay)
		{
			Banish(FString(TEXT("失谐已压回")));
			return;
		}
	}
	else
	{
		DisengageTimer = 0.f;
	}

	// —— 直线接近。穿墙，但不穿核心 ——
	const FVector Here = GetActorLocation();
	const FVector Goal = T->GetActorLocation();
	FVector Step = (Goal - Here).GetSafeNormal() * ApproachSpeed * DeltaSeconds;
	FVector Next = Here + Step;

	if (const ABaseCore* Core = Cast<ABaseCore>(
		UGameplayStatics::GetActorOfClass(GetWorld(), ABaseCore::StaticClass())))
	{
		// 核心是唯一挡得住它的东西——躲进基准场是规格书给的脱离手段之一。
		// 挡法是把落点推回球面外，而不是停下：停下的话玩家贴着核心边缘绕圈就能永久卡住它
		const FVector C = Core->GetActorLocation();
		const FVector Flat(Next.X - C.X, Next.Y - C.Y, 0.f);
		if (Flat.SizeSquared() < CoreBlockRadius * CoreBlockRadius)
		{
			const FVector Out = Flat.GetSafeNormal() * CoreBlockRadius;
			Next.X = C.X + Out.X;
			Next.Y = C.Y + Out.Y;
		}
	}
	SetActorLocation(Next);
	SetActorRotation((Goal - Next).GetSafeNormal().Rotation());

	// —— 接触伤害 ——
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextTouchTime && FVector::Dist(Next, Goal) <= TouchRange)
	{
		NextTouchTime = Now + TouchCooldown;
		UGameplayStatics::ApplyDamage(T, TouchDamage, nullptr, this, nullptr);
	}
}
