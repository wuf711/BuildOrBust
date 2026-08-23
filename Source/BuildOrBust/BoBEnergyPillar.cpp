// Build or Bust — 能量柱实现。

#include "BoBEnergyPillar.h"
#include "Boss_CS07.h"
#include "Components/StaticMeshComponent.h"
#include "Net/UnrealNetwork.h"

ABoBEnergyPillar::ABoBEnergyPillar()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetCollisionProfileName(TEXT("BlockAllDynamic"));
	SetRootComponent(Body);
}

void ABoBEnergyPillar::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABoBEnergyPillar, Shape);
	DOREPLIFETIME(ABoBEnergyPillar, Vein);
	DOREPLIFETIME(ABoBEnergyPillar, bIsTarget);
}

const TCHAR* ABoBEnergyPillar::ShapeName(EBoBPillarShape S)
{
	static const TCHAR* N[] = { TEXT("三角"), TEXT("螺旋"), TEXT("六边"), TEXT("方") };
	return N[FMath::Clamp((int32)S, 0, 3)];
}

const TCHAR* ABoBEnergyPillar::VeinName(EBoBPillarVein V)
{
	static const TCHAR* N[] = { TEXT("开裂"), TEXT("绞合"), TEXT("断续"), TEXT("放射") };
	return N[FMath::Clamp((int32)V, 0, 3)];
}

void ABoBEnergyPillar::Setup(ABoss_CS07* InBoss, EBoBPillarShape InShape,
	EBoBPillarVein InVein, bool bInTarget)
{
	BossOwner = InBoss;
	Shape = InShape;
	Vein = InVein;
	bIsTarget = bInTarget;
}

float ABoBEnergyPillar::TakeDamage(float Damage, const FDamageEvent& DamageEvent,
	AController* EventInstigator, AActor* DamageCauser)
{
	if (!HasAuthority() || bResolved || Damage <= 0.f) { return 0.f; }
	bResolved = true;

	if (BossOwner)
	{
		// 打对了烧抗性，打错了引一波朝圣晶簇。两种结果都由 Boss 统一收口，
		// 柱子自己不生怪——生怪要走 Director 的存活统计，散在这里迟早对不上账
		BossOwner->ResolvePillarHit(bIsTarget);
	}
	UE_LOG(LogTemp, Log, TEXT("[BoBBoss] 能量柱被击中：%s%s（%s / %s）"),
		bIsTarget ? TEXT("正确") : TEXT("错误"),
		bIsTarget ? TEXT("") : TEXT("，引来增援"),
		ShapeName(Shape), VeinName(Vein));

	Destroy();
	return Damage;
}
