// Build or Bust — 无效遗构与接收端口实现。

#include "BoBFalseRelic.h"
#include "Boss_CS07.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

// ===================== 无效遗构 =====================

ABoBFalseRelic::ABoBFalseRelic()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(true);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(110.f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SetRootComponent(Trigger);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Trigger);
	Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void ABoBFalseRelic::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABoBFalseRelic, Holder);
}

void ABoBFalseRelic::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ABoBFalseRelic::OnPickupOverlap);
	}
}

ABoBFalseRelic* ABoBFalseRelic::FindHeldBy(const AShooterCharacter* Who)
{
	if (!Who) { return nullptr; }
	for (TActorIterator<ABoBFalseRelic> It(Who->GetWorld()); It; ++It)
	{
		if (It->Holder == Who) { return *It; }
	}
	return nullptr;
}

void ABoBFalseRelic::OnPickupOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (!HasAuthority() || Holder) { return; }
	AShooterCharacter* Char = Cast<AShooterCharacter>(OtherActor);
	if (!Char || Char->IsDead()) { return; }

	// 一次只能扛一件。规格书里 3 件要分头搬，扛不了两件才有分工可言
	if (FindHeldBy(Char)) { return; }

	Holder = Char;
	AttachToHolder();
}

void ABoBFalseRelic::OnRep_Holder()
{
	AttachToHolder();
}

void ABoBFalseRelic::AttachToHolder()
{
	if (Holder)
	{
		AttachToComponent(Holder->GetRootComponent(),
			FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		SetActorRelativeLocation(FVector(0.f, 0.f, 130.f));
		Trigger->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	else
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}

void ABoBFalseRelic::ConsumeAtPort()
{
	if (HasAuthority())
	{
		Holder = nullptr;
		Destroy();
	}
}

// ===================== 接收端口 =====================

ABoBBossPort::ABoBBossPort()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->InitSphereRadius(160.f);
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	SetRootComponent(Trigger);
}

void ABoBBossPort::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
		// 忘了绑这一句，端口就是个纯装饰——玩家搬到跟前什么都不会发生
		Trigger->OnComponentBeginOverlap.AddDynamic(this, &ABoBBossPort::OnPortOverlap);
	}
}

void ABoBBossPort::OnPortOverlap(UPrimitiveComponent*, AActor* OtherActor,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	if (!HasAuthority()) { return; }
	AShooterCharacter* Char = Cast<AShooterCharacter>(OtherActor);
	if (!Char) { return; }

	ABoBFalseRelic* Relic = ABoBFalseRelic::FindHeldBy(Char);
	if (!Relic) { return; }

	Relic->ConsumeAtPort();
	if (BossOwner)
	{
		BossOwner->RegisterFalseRelic();
	}
	UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 无效遗构送达接收端口"));
}
