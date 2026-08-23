#include "SniperWeapon.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Components/PointLightComponent.h"

void BoBAddBoltVisual(AActor* Proj, const TCHAR* MeshPath, const TCHAR* MatPath, const FVector& Scale, const FRotator& Rot);

ASniperProjectile::ASniperProjectile()
{
	HitDamage = 300.0f;      // 一发入魂：命中目标本体重伤
	bExplodeOnHit = true;    // 谐振弹头：命中点大范围溅射，一枪犁一片
	ExplosionRadius = 560.0f;
	DeferredDestructionTime = 0.15f;   // 命中后 0.15s 让金色爆闪可见即销毁（不残留光污染）
}

void ASniperProjectile::BeginPlay()
{
	Super::BeginPlay();
	// 高速平直弹道：告别"榴弹式"抛物+弹跳，做成近乎瞬达的实弹
	if (UProjectileMovementComponent* PM = FindComponentByClass<UProjectileMovementComponent>())
	{
		PM->bShouldBounce = false;
		PM->ProjectileGravityScale = 0.0f;
		PM->MaxSpeed = 28000.0f;
		PM->Velocity = PM->Velocity.GetSafeNormal() * 28000.0f;   // 构造期已按目标定向，这里只提速
	}
	// 金黄色弹丸
	BoBAddBoltVisual(this, TEXT("/Engine/BasicShapes/Sphere.Sphere"),
		TEXT("/Game/Wasteland/FX/M_BoBFX_Gold.M_BoBFX_Gold"), FVector(0.95f, 0.18f, 0.18f), FRotator::ZeroRotator);
	// 曳光灯改金色：命中瞬间基类会把它爆闪成"金色扩散光"
	if (UPointLightComponent* TL = FindComponentByClass<UPointLightComponent>())
	{
		TL->SetLightColor(FLinearColor(1.0f, 0.78f, 0.28f));
		TL->SetIntensity(1200.0f);
		TL->SetAttenuationRadius(420.0f);
	}
}

ASniperWeapon::ASniperWeapon()
{
	VisualMeshPath = TEXT("/Game/FabAssets/Sniper/gun2.gun2");
	VisualLen = 135.0f;
	ProjectileClass = ASniperProjectile::StaticClass();
	MagazineSize = 12;
	RefireRate = 0.55f;      // 射速大大提高
	bFullAuto = false;
	AimVariance = 0.0f;      // 指哪打哪
	FiringRecoil = 1.0f;
	ShotLoudness = 2.0f;
	ShotNoiseRange = 8000.0f;
}
