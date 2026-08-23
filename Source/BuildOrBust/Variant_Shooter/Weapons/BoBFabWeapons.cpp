#include "BoBFabWeapons.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "UObject/ConstructorHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "TimerManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SphereComponent.h"
#include "Camera/CameraComponent.h"
#include "Engine/OverlapResult.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "BaseCore.h"

// 让弹丸直线飞（不弹跳、无重力、恒速）——能量波/脉冲不该像手雷抛物+弹跳
static void BoBStraightenFlight(AActor* Proj, float Speed)
{
	if (UProjectileMovementComponent* PM = Proj->FindComponentByClass<UProjectileMovementComponent>())
	{
		PM->bShouldBounce = false;
		PM->ProjectileGravityScale = 0.0f;
		PM->MaxSpeed = Speed;
		PM->Velocity = PM->Velocity.GetSafeNormal() * Speed;
	}
}

ABoBFabWeapon::ABoBFabWeapon()
{
	// 骨架/动画/蒙太奇沿用步枪资产（FP 骨架只当"隐形挂架"：提供 Muzzle 插槽与开火动画）
	static ConstructorHelpers::FObjectFinder<USkeletalMesh> RifleSkm(TEXT("/Game/Weapons/Rifle/Meshes/SKM_Rifle.SKM_Rifle"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> FPAnim(TEXT("/Game/Variant_Shooter/Anims/ABP_FP_Weapon"));
	static ConstructorHelpers::FClassFinder<UAnimInstance> TPAnim(TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Rifle"));
	static ConstructorHelpers::FObjectFinder<UAnimMontage> Montage(TEXT("/Game/Variant_Shooter/Anims/FP_Rifle_Shoot_Montage.FP_Rifle_Shoot_Montage"));
	if (RifleSkm.Succeeded())
	{
		GetFirstPersonMesh()->SetSkeletalMesh(RifleSkm.Object);
		GetThirdPersonMesh()->SetSkeletalMesh(RifleSkm.Object);   // 远端玩家看通用枪型
	}
	if (FPAnim.Succeeded())
	{
		FirstPersonAnimInstanceClass = FPAnim.Class;
	}
	if (TPAnim.Succeeded())
	{
		ThirdPersonAnimInstanceClass = TPAnim.Class;
	}
	if (Montage.Succeeded())
	{
		FiringMontage = Montage.Object;
	}
	// 造型网格走【世界通道】渲染(FirstPersonType=None)：第一人称管线对静态网格实测不渲染
	//（诊断证明渲染标志与可正常显示的骨架网格完全一致却不显 → FP 管线只吃骨架网格）。
	// 世界通道静态网格确定能渲染(脉冲环/战利品已证)，bOnlyOwnerSee 保证只有持有者看得到。
	// 父骨架用半透明材质隐身、只影响其自身，不连累世界通道子网格。
	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(GetFirstPersonMesh());
	VisualMesh->SetCollisionProfileName(FName("NoCollision"));
	VisualMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::None);
	VisualMesh->bOnlyOwnerSee = false;   // 诊断：唯一能渲染的静态网格(弹丸脉冲环)正是无此限制；先验证 owner 可见性是否为元凶
	VisualMesh->SetCastShadow(false);

	MuzzleSocketName = FName("Muzzle");
	// 自动定向接管朝向；VisualRot 仅作每把可选翻转微调(默认单位)，VisualOffset 作每把位置微调(默认 0)
	VisualRot = FRotator::ZeroRotator;
	VisualOffset = FVector::ZeroVector;
}

void ABoBFabWeapon::BeginPlay()
{
	Super::BeginPlay();

	// VisualMesh 保持挂在武器手骨骨架(GetFirstPersonMesh)下——它跟随 HandGrip_R 手骨，
	// 随第一人称动画(步枪/手枪握姿)一起摆动，武器才"跟手"而非浮空。

	// 手枪 + 骰子：用项目自带手枪握姿 ABP_FP_Pistol(单手)——骰子单手持握便于抛掷。
	// (近战已删；ABP_Knife 跨骨架会让手臂崩掉不渲染，已弃)
	if (IsA(ALaserPistolWeapon::StaticClass()) || IsA(ADiceWeapon::StaticClass()))
	{
		if (UClass* FP = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Variant_Shooter/Anims/ABP_FP_Pistol.ABP_FP_Pistol_C")))
		{
			FirstPersonAnimInstanceClass = FP;
		}
		if (UClass* TP = LoadClass<UAnimInstance>(nullptr, TEXT("/Game/Variant_Shooter/Anims/ABP_TP_Pistol.ABP_TP_Pistol_C")))
		{
			ThirdPersonAnimInstanceClass = TP;
		}
	}

	if (!VisualMeshPath.IsEmpty())
	{
		UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, *VisualMeshPath);
		UE_LOG(LogTemp, Warning, TEXT("BoBFabWeapon %s visual: %s"), *GetName(), M ? *M->GetName() : TEXT("LOAD FAILED"));
		if (M)
		{
			VisualMesh->SetStaticMesh(M);
			const FBoxSphereBounds B = M->GetBounds();
			const FVector Ext = B.BoxExtent;
			// 缩放：最长边归一到 VisualLen
			const float Longest = 2.0f * FMath::Max3(Ext.X, Ext.Y, Ext.Z);
			const float S = Longest > 1.0f ? VisualLen / Longest : 1.0f;
			VisualMesh->SetRelativeScale3D(FVector(S));

			// 自动定向：把模型三条主轴对齐到武器手骨系(实测 SKM_Rifle 长轴=+Y=枪管朝前 / 次轴=+Z=上)。
			// 不再逐把猜 VisualRot——扫描件枢轴/朝向乱，但"最长轴=枪管、次长轴=上"是稳定的。
			// VisualRot 仅作每把可选的翻转微调(默认 0)。
			auto AxisUnit = [](int32 i) { return i == 0 ? FVector(1, 0, 0) : (i == 1 ? FVector(0, 1, 0) : FVector(0, 0, 1)); };
			const int32 LongI = (Ext.X >= Ext.Y && Ext.X >= Ext.Z) ? 0 : ((Ext.Y >= Ext.Z) ? 1 : 2);
			const int32 ShortI = (Ext.X <= Ext.Y && Ext.X <= Ext.Z) ? 0 : ((Ext.Y <= Ext.Z) ? 1 : 2);
			const int32 MedI = 3 - LongI - ShortI;
			const FQuat Q1 = FQuat::FindBetweenNormals(AxisUnit(LongI), FVector(0, 1, 0));   // 长轴→+Y(前)
			const FQuat Q2 = FQuat::FindBetweenNormals(Q1.RotateVector(AxisUnit(MedI)), FVector(0, 0, 1)); // 次轴→+Z(上)
			const FQuat Q = VisualRot.Quaternion() * Q2 * Q1;
			VisualMesh->SetRelativeRotation(Q.Rotator());

			// 定位：算旋转+缩放后的实际包围盒；前向=+Y 让后端(枪托/刀柄)贴在手部略后，X(右)/Z(上)居中到握持高度
			FVector Mn(FLT_MAX, FLT_MAX, FLT_MAX), Mx(-FLT_MAX, -FLT_MAX, -FLT_MAX);
			for (int32 sx = -1; sx <= 1; sx += 2)
			{
				for (int32 sy = -1; sy <= 1; sy += 2)
				{
					for (int32 sz = -1; sz <= 1; sz += 2)
					{
						const FVector R = Q.RotateVector((B.Origin + FVector(sx * Ext.X, sy * Ext.Y, sz * Ext.Z)) * S);
						Mn = Mn.ComponentMin(R);
						Mx = Mx.ComponentMax(R);
					}
				}
			}
			const float RearBehind = -12.0f;   // 后端在手部略后(像枪托)
			VisualMesh->SetRelativeLocation(FVector(
				VisualOffset.X - 0.5f * (Mn.X + Mx.X),          // X(右)居中 + 微调
				RearBehind - Mn.Y + VisualOffset.Y,            // 前向=+Y：后端贴手略后
				VisualOffset.Z - 0.5f * (Mn.Z + Mx.Z) + 3.4f)); // Z(上)居中到步枪握持高度~3.4
			VisualMesh->SetVisibility(true);

			// 按武器套专属材质(不走模型 UV，纯程序化，不碎)
			const TCHAR* SkinPath = nullptr;
			if (IsA(ADiceWeapon::StaticClass()))        { SkinPath = TEXT("/Game/Wasteland/FX/M_BoBDiceToon.M_BoBDiceToon"); }       // 混沌比特：问号砖
			else if (IsA(ALaserPistolWeapon::StaticClass())) { SkinPath = TEXT("/Game/Wasteland/FX/M_BoBPistolPastel.M_BoBPistolPastel"); } // 曳星光铳：粉紫梦幻
			else if (IsA(ACoilRifleWeapon::StaticClass()))   { SkinPath = TEXT("/Game/Wasteland/FX/M_BoBRailLightning.M_BoBRailLightning"); } // 奔雷磁轨：流动电弧
			if (SkinPath)
			{
				if (UMaterialInterface* Skin = LoadObject<UMaterialInterface>(nullptr, SkinPath))
				{
					for (int32 i = 0; i < VisualMesh->GetNumMaterials(); ++i)
					{
						VisualMesh->SetMaterial(i, Skin);
					}
				}
			}

			// 武器专属染色：给造型上带颜色的金属质感材质(TintColor.R<0 = 不染色，保留原材质)
			if (TintColor.R >= 0.0f)
			{
				if (UMaterialInterface* TintBase = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wasteland/FX/M_BoBWeaponTint.M_BoBWeaponTint")))
				{
					if (UMaterialInstanceDynamic* MID = UMaterialInstanceDynamic::Create(TintBase, VisualMesh))
					{
						MID->SetVectorParameterValue(FName("TintColor"), TintColor);
						for (int32 i = 0; i < VisualMesh->GetNumMaterials(); ++i)
						{
							VisualMesh->SetMaterial(i, MID);
						}
					}
				}
			}
		}
	}
	// 骨架枪隐形化：遮罩材质+遮罩0=像素全裁剪（半透明版曾编译失败渲染成默认灰）
	if (UMaterialInterface* Inv = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wasteland/FX/M_BoBHide.M_BoBHide")))
	{
		for (int32 i = 0; i < GetFirstPersonMesh()->GetNumMaterials(); ++i)
		{
			GetFirstPersonMesh()->SetMaterial(i, Inv);
		}
	}
}

void ABoBFabWeapon::Fire()
{
	// 非近战，或觉醒窗口内：走基类弹丸射击（近战的"斩击波"就是这里发出去的）
	if (MeleeChargeNeeded <= 0 || GetWorld()->GetTimeSeconds() < EmpowerEndTime)
	{
		Super::Fire();
		return;
	}
	if (!bIsFiring)
	{
		return;
	}

	// ===== 近战挥击：镜头方向球形横扫 =====
	const FVector Target = WeaponOwner ? WeaponOwner->GetWeaponTargetLocation() : GetActorLocation();
	const FVector Start = PawnOwner ? PawnOwner->GetActorLocation() + FVector(0, 0, 50) : GetActorLocation();
	const FVector Dir = (Target - Start).GetSafeNormal();
	TArray<FHitResult> Hits;
	FCollisionQueryParams QP;
	QP.AddIgnoredActor(PawnOwner);
	QP.AddIgnoredActor(this);
	GetWorld()->SweepMultiByChannel(Hits, Start, Start + Dir * MeleeRange, FQuat::Identity,
		ECC_Pawn, FCollisionShape::MakeSphere(MeleeRadius), QP);

	TArray<AActor*> Struck;
	for (const FHitResult& H : Hits)
	{
		APawn* P = Cast<APawn>(H.GetActor());
		if (!P || P == PawnOwner || Struck.Contains(P))
		{
			continue;
		}
		Struck.Add(P);
		if (PawnOwner && PawnOwner->HasAuthority())
		{
			UGameplayStatics::ApplyDamage(P, MeleeDamage, PawnOwner->GetController(), this, UDamageType::StaticClass());
		}
		else if (AShooterCharacter* SC = Cast<AShooterCharacter>(PawnOwner))
		{
			SC->Server_ReportHit(P, MeleeDamage);
		}
	}

	// 充能：每次挥击都 +1（命中敌人额外加速），攒满进入觉醒（刀气打满）——
	// 空挥也累计，无敌人时也能测试充能条
	bool bEmpowered = false;
	MeleeHits += 1 + Struck.Num();
	if (MeleeHits >= MeleeChargeNeeded)
	{
		MeleeHits = 0;
		EmpowerEndTime = GetWorld()->GetTimeSeconds() + EmpowerDuration;
		CurrentBullets = MagazineSize;
		bEmpowered = true;
	}

	// 程序化挥砍：VisualMesh 快速划弧（不依赖动画资产也有动作感）
	if (VisualMesh)
	{
		const FRotator BaseRot = VisualRot;
		TSharedPtr<float> SwingT = MakeShared<float>(0.0f);
		TSharedPtr<FTimerHandle> SwingH = MakeShared<FTimerHandle>();
		TWeakObjectPtr<ABoBFabWeapon> WeakW(this);
		GetWorld()->GetTimerManager().SetTimer(*SwingH, FTimerDelegate::CreateLambda([WeakW, SwingT, SwingH, BaseRot]()
		{
			ABoBFabWeapon* W = WeakW.Get();
			if (!W)
			{
				return;
			}
			*SwingT += 0.016f / 0.22f;
			const float A = FMath::Min(*SwingT, 1.0f);
			const float Arc = FMath::Sin(A * PI) * 75.0f;
			W->VisualMesh->SetRelativeRotation(BaseRot + FRotator(-Arc * 0.4f, Arc, 0.0f));
			if (A >= 1.0f)
			{
				W->VisualMesh->SetRelativeRotation(BaseRot);
				W->GetWorld()->GetTimerManager().ClearTimer(*SwingH);
			}
		}), 0.016f, true);
	}
	// 近战反馈=可见的挥刀弧(上方程序化划弧) + 枪口红闪；斩击波的"刀光"在觉醒期由飞行气刃承载
	// 手臂开火蒙太奇（角色 PlayFiringMontage 是空桩，直接对手臂动画实例播）
	if (AShooterCharacter* SCh = Cast<AShooterCharacter>(PawnOwner))
	{
		if (SCh->GetFirstPersonMesh() && SCh->GetFirstPersonMesh()->GetAnimInstance() && FiringMontage)
		{
			SCh->GetFirstPersonMesh()->GetAnimInstance()->Montage_Play(FiringMontage, 1.3f);
		}
	}

	// 挥砍闪光（觉醒瞬间大爆闪）——红色调，配合刀光观感
	if (MuzzleLight)
	{
		MuzzleLight->SetLightColor(FLinearColor(1.0f, 0.28f, 0.16f));
		MuzzleLight->SetIntensity(bEmpowered ? 5200.0f : 1500.0f);
		GetWorld()->GetTimerManager().SetTimer(MuzzleFlashTimer,
			FTimerDelegate::CreateLambda([WeakThis = TWeakObjectPtr<ABoBFabWeapon>(this)]()
			{
				if (ABoBFabWeapon* W = WeakThis.Get())
				{
					if (W->MuzzleLight)
					{
						W->MuzzleLight->SetIntensity(0.0f);
					}
				}
			}), bEmpowered ? 0.22f : 0.07f, false);
	}
	if (WeaponOwner)
	{
		// 觉醒外把弹药行当充能表用：充能 X / 需求 N
		WeaponOwner->UpdateWeaponHUD(EmpowerEndTime > GetWorld()->GetTimeSeconds() ? CurrentBullets : MeleeHits,
			EmpowerEndTime > GetWorld()->GetTimeSeconds() ? MagazineSize : MeleeChargeNeeded);
		WeaponOwner->PlayFiringMontage(FiringMontage);
		WeaponOwner->AddWeaponRecoil(FiringRecoil);
	}
	MakeNoise(ShotLoudness, PawnOwner, PawnOwner ? PawnOwner->GetActorLocation() : GetActorLocation(), ShotNoiseRange, ShotNoiseTag);

	// 半自动节奏（同基类语义）
	TimeOfLastShot = GetWorld()->GetTimeSeconds();
	GetWorld()->GetTimerManager().SetTimer(RefireTimer, this, &ABoBFabWeapon::FireCooldownExpired, RefireRate, false);
}

// ===== 弹丸 =====

// 给弹丸挂发光形态（材质由布设脚本生成到 /Game/Wasteland/FX；缺了也不崩，退化为曳光弹）
void BoBAddBoltVisual(AActor* Proj, const TCHAR* MeshPath, const TCHAR* MatPath, const FVector& Scale, const FRotator& Rot)
{
	UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, MeshPath);
	if (!M || !Proj->GetRootComponent())
	{
		return;
	}
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Proj);
	C->SetupAttachment(Proj->GetRootComponent());
	C->SetStaticMesh(M);
	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, MatPath))
	{
		C->SetMaterial(0, Mat);
	}
	C->SetRelativeScale3D(Scale);
	C->SetRelativeRotation(Rot);
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	C->RegisterComponent();
}

ACoilProjectile::ACoilProjectile()
{
	HitDamage = 34.0f;         // 脉冲波：起手伤害中等，随飞行放大而衰减
	bExplodeOnHit = true;
	ExplosionRadius = 200.0f;  // 起手范围小，随飞行放大
	DeferredDestructionTime = 0.1f;   // 命中即清，别让成长环残留
}

void ACoilProjectile::BeginPlay()
{
	Super::BeginPlay();
	BoBStraightenFlight(this, 1200.0f);   // 脉冲波移动再放慢
	// 压暗基类那盏青白点光——它才是"看着像光球"的元凶
	if (UPointLightComponent* TL = FindComponentByClass<UPointLightComponent>())
	{
		TL->SetIntensity(120.0f);
		TL->SetLightColor(FLinearColor(0.1f, 0.9f, 1.0f));
	}
	// 青色镂空圆环：正对玩家的平面 + 径向环材质(中空)，随飞行放大——离发射点越远环越大、伤害越低
	if (UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane")))
	{
		UStaticMeshComponent* Ring = NewObject<UStaticMeshComponent>(this);
		Ring->SetupAttachment(GetRootComponent());
		Ring->SetStaticMesh(Plane);
		if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wasteland/FX/M_BoBFX_Ring.M_BoBFX_Ring")))
		{
			Ring->SetMaterial(0, Mat);
		}
		Ring->SetRelativeRotation(FRotator(-90.0f, 0.0f, 0.0f));   // 盘面法线朝 X=飞行方向，玩家正对看到整环
		Ring->SetRelativeScale3D(FVector(0.18f, 0.18f, 0.18f));   // 初始半径更小
		Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ring->SetCastShadow(false);
		Ring->RegisterComponent();
		TWeakObjectPtr<UStaticMeshComponent> WeakRing(Ring);
		FTimerHandle GrowHandle;
		GetWorldTimerManager().SetTimer(GrowHandle, FTimerDelegate::CreateWeakLambda(this, [this, WeakRing]()
		{
			if (UStaticMeshComponent* R = WeakRing.Get())
			{
				R->SetRelativeScale3D(R->GetRelativeScale3D() + FVector(0.055f, 0.055f, 0.055f));   // 扩大更慢
			}
			// 存活期内范围持续变广；伤害曲线交给 CoilPulseTick(第 3 秒达峰)
			ExplosionRadius = FMath::Min(ExplosionRadius + 26.0f, 680.0f);
		}), 0.05f, true);

		// ===== 脉冲机制：前 3 秒穿透不炸，之后碰撞即消；最长 9 秒；每 0.5s 造成一次范围伤害 =====
		SpawnTime = GetWorld()->GetTimeSeconds();
		// 前 3 秒完全无碰撞：既不阻挡也不触发命中，穿透一切继续飞。
		// 注意：碰撞球是基类的私有 CollisionComponent(球体)，不是 RootComponent，
		// 必须遍历所有 Primitive 组件关掉，否则弹丸仍会被墙挡住停在原地。
		for (UActorComponent* C : GetComponents())
		{
			if (UPrimitiveComponent* P = Cast<UPrimitiveComponent>(C))
			{
				P->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			}
		}
		if (UProjectileMovementComponent* PM2 = FindComponentByClass<UProjectileMovementComponent>())
		{
			PM2->bShouldBounce = false;
			PM2->ProjectileGravityScale = 0.0f;
			PM2->SetUpdatedComponent(GetRootComponent());   // 确保移动组件仍驱动本体
		}
		if (UProjectileMovementComponent* PM = FindComponentByClass<UProjectileMovementComponent>())
		{
			PM->bShouldBounce = false;
			PM->ProjectileGravityScale = 0.0f;
		}
		GetWorldTimerManager().SetTimer(PulseTimer, this, &ACoilProjectile::CoilPulseTick, 0.5f, true, 0.5f);
		FTimerHandle ArmHandle;
		GetWorldTimerManager().SetTimer(ArmHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
		{
			bArmed = true;   // 满 3 秒后恢复碰撞：一碰即散
			// 只恢复碰撞球(球体组件)，圆环特效保持无碰撞
			for (UActorComponent* C : GetComponents())
			{
				if (USphereComponent* S = Cast<USphereComponent>(C))
				{
					S->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				}
			}
		}), 3.0f, false);
		SetLifeSpan(9.0f);   // 至多存在 9 秒（防止不撞墙一直飞）
	}
}

void ACoilProjectile::CoilPulseTick()
{
	if (!GetWorld() || GetLocalRole() != ROLE_Authority)
	{
		return;
	}
	// 伤害曲线：出膛渐强 -> 第 3 秒达峰 -> 之后缓慢衰减（不再是"越远越弱"）
	const float Age = GetWorld()->GetTimeSeconds() - SpawnTime;
	float Scale;
	if (Age <= 3.0f)
	{
		Scale = FMath::Lerp(0.35f, 1.0f, FMath::Clamp(Age / 3.0f, 0.0f, 1.0f));
	}
	else
	{
		Scale = FMath::Lerp(1.0f, 0.55f, FMath::Clamp((Age - 3.0f) / 6.0f, 0.0f, 1.0f));
	}

	// 范围内敌人各吃一次脉冲伤害
	TArray<FOverlapResult> Overlaps;
	FCollisionShape Shape;
	Shape.SetSphere(ExplosionRadius);
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	QueryParams.AddIgnoredActor(GetInstigator());
	GetWorld()->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity, ObjParams, Shape, QueryParams);

	TArray<AActor*> Done;
	for (const FOverlapResult& R : Overlaps)
	{
		AActor* A = R.GetActor();
		if (!A || Done.Contains(A)) { continue; }
		Done.Add(A);
		if (Cast<AShooterNPC>(A))
		{
			UGameplayStatics::ApplyDamage(A, HitDamage * Scale,
				GetInstigator() ? GetInstigator()->GetController() : nullptr, this, UDamageType::StaticClass());
		}
	}
}

void ACoilProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	// 前 3 秒穿透一切（不消失）；满 3 秒后一碰即散
	// 双保险：除 bArmed 外再用存活时长兜底，避免任何early-hit 路径提前销毁
	if (!bArmed || (GetWorld() && GetWorld()->GetTimeSeconds() - SpawnTime < 3.0f))
	{
		return;
	}
	GetWorldTimerManager().ClearTimer(PulseTimer);
	Super::NotifyHit(MyComp, Other, OtherComp, bSelfMoved, HitLocation, HitNormal, NormalImpulse, Hit);
}

ALaserBoltProjectile::ALaserBoltProjectile()
{
	HitDamage = 42.0f;   // 射速大幅提高后单发伤害相应降低
}

void ALaserBoltProjectile::BeginPlay()
{
	Super::BeginPlay();
	BoBStraightenFlight(this, 5200.0f);
	// 紫罗兰色曳光弹（不再白）
	BoBAddBoltVisual(this, TEXT("/Engine/BasicShapes/Sphere.Sphere"),
		TEXT("/Game/Wasteland/FX/M_BoBFX_Violet.M_BoBFX_Violet"), FVector(0.55f, 0.1f, 0.1f), FRotator::ZeroRotator);
	if (UPointLightComponent* TL = FindComponentByClass<UPointLightComponent>())
	{
		TL->SetLightColor(FLinearColor(0.55f, 0.15f, 1.0f));
	}
}

// ===================== 爆炸/掷骰特效 =====================
// 分层做法：核心闪光、火球、冲击环、碎屑各有各的生命周期与缓动，
// 膨胀走 ease-out(起手最快、迅速失势)——匀速膨胀是最容易露怯的地方。
namespace BoBFX
{
	struct FBit
	{
		TWeakObjectPtr<UStaticMeshComponent> Comp;
		TWeakObjectPtr<UMaterialInstanceDynamic> Mid;
		FVector Vel = FVector::ZeroVector;    // cm/s
		FVector Spin = FVector::ZeroVector;   // deg/s
		FVector Axis = FVector::OneVector;    // 非等比缩放权重
		float Scale0 = 1.0f;
		float Scale1 = 1.0f;                  // 生命终点的缩放
		float Life = 0.5f;                    // 本层存活秒数
		float Delay = 0.0f;
		bool bEase = true;                    // true=ease-out 膨胀，false=匀速(碎屑用)
	};

	struct FState
	{
		TArray<FBit> Bits;
		float Elapsed = 0.0f;
		float Duration = 1.0f;
		float Gravity = 0.0f;                 // cm/s^2，负值=上浮
	};
}

// Effect: 0爆裂 1治疗 2迟滞 3榴弹爆炸(ShooterProjectile.cpp 里前置声明后调用)
void BoBSpawnBurstFX(UWorld* World, const FVector& Loc, int32 Effect, const FLinearColor& Color, float Radius)
{
	if (!World) { return; }
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* CylMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	UMaterialInterface* BurstMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wasteland/FX/M_BoBFX_Burst.M_BoBFX_Burst"));
	UMaterialInterface* RingMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wasteland/FX/M_BoBFX_BurstRing.M_BoBFX_BurstRing"));
	if (!SphereMesh || !PlaneMesh || !BurstMat || !RingMat)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BoB] BurstFX 资源缺失，特效未生成"));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AActor* Host = World->SpawnActor<AActor>(AActor::StaticClass(), Loc, FRotator::ZeroRotator, SpawnParams);
	if (!Host) { return; }
	USceneComponent* HostRoot = NewObject<USceneComponent>(Host);
	Host->SetRootComponent(HostRoot);
	HostRoot->RegisterComponent();
	// 裸 AActor 生成时没有根组件，spawn 的位置不会落到后补的根上（否则整套特效播在世界原点）。
	// 抬高一点，免得半个火球埋进地面。
	Host->SetActorLocation(Loc + FVector(0.0f, 0.0f, 25.0f));

	TSharedRef<BoBFX::FState> St = MakeShared<BoBFX::FState>();
	const float Sph = 1.0f / 50.0f;    // 基础球/面网格半边长 50cm：cm -> 缩放
	const float Cub = 1.0f / 100.0f;   // 基础方块 100cm

	// Cm0/Cm1 用世界厘米给尺寸，读代码时一眼知道特效多大
	auto Add = [&](UStaticMesh* Mesh, UMaterialInterface* Mat, const FVector& RelLoc, const FRotator& Rot, const FLinearColor& C,
		float Cm0, float Cm1, float Unit, float Life, const FVector& Axis, const FVector& Vel, const FVector& Spin, float Delay, bool bEase)
	{
		if (!Mesh) { return; }
		UStaticMeshComponent* Comp = NewObject<UStaticMeshComponent>(Host);
		Comp->SetupAttachment(HostRoot);
		Comp->SetStaticMesh(Mesh);
		Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Comp->SetCastShadow(false);
		Comp->SetRelativeLocation(RelLoc);
		Comp->SetRelativeRotation(Rot);
		Comp->SetRelativeScale3D(Axis * (Cm0 * Unit));
		Comp->RegisterComponent();
		Comp->SetMaterial(0, Mat);   // 必须先赋 FX 材质，否则 MID 是从默认灰材质生成的、参数全无效
		UMaterialInstanceDynamic* Mid = Comp->CreateAndSetMaterialInstanceDynamic(0);
		if (Mid) { Mid->SetVectorParameterValue(TEXT("Color"), C); }
		BoBFX::FBit Bit;
		Bit.Comp = Comp; Bit.Mid = Mid; Bit.Vel = Vel; Bit.Spin = Spin; Bit.Axis = Axis;
		Bit.Scale0 = Cm0 * Unit; Bit.Scale1 = Cm1 * Unit; Bit.Life = Life; Bit.Delay = Delay; Bit.bEase = bEase;
		St->Bits.Add(Bit);
	};

	const FLinearColor Flash(3.0f, 2.6f, 2.0f);          // 起爆核心的白热闪光，各效果通用
	const FLinearColor Core = Color * 2.2f;              // 主体色加亮
	const FVector NoSpin = FVector::ZeroVector;

	if (Effect == 0)          // ===== 爆裂：白热闪光 -> 火球 -> 地面冲击环 -> 火星 =====
	{
		St->Duration = 0.95f;
		St->Gravity = 950.0f;
		Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Flash, 30.0f, 150.0f, Sph, 0.16f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Core, 55.0f, Radius * 0.42f, Sph, 0.50f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(PlaneMesh, RingMat, FVector(0.0f, 0.0f, 12.0f), FRotator::ZeroRotator, Color, 60.0f, Radius * 1.05f, Sph, 0.62f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.0f, true);
		for (int32 i = 0; i < 18; ++i)
		{
			const FVector Dir = FRotator(FMath::FRandRange(10.0f, 70.0f), FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector();
			Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Core, 9.0f, 2.0f, Sph, FMath::FRandRange(0.55f, 0.9f),
				FVector::OneVector, Dir * FMath::FRandRange(420.0f, 900.0f), NoSpin, 0.0f, false);
		}
	}
	else if (Effect == 1)     // ===== 治疗：光柱升起 + 双层脉冲地环 + 上飘光点 =====
	{
		St->Duration = 1.3f;
		St->Gravity = -180.0f;
		Add(CylMesh, BurstMat, FVector(0.0f, 0.0f, 130.0f), FRotator::ZeroRotator, Core, 70.0f, 46.0f, Sph, 1.05f, FVector(1.0f, 1.0f, 3.0f), FVector::ZeroVector, FVector(0.0f, 70.0f, 0.0f), 0.0f, true);
		Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Color * 1.2f, 40.0f, Radius * 0.42f, Sph, 0.75f, FVector(1.0f, 1.0f, 0.7f), FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(PlaneMesh, RingMat, FVector(0.0f, 0.0f, 8.0f), FRotator::ZeroRotator, Color, 50.0f, Radius * 0.95f, Sph, 0.8f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(PlaneMesh, RingMat, FVector(0.0f, 0.0f, 14.0f), FRotator::ZeroRotator, Core, 40.0f, Radius * 0.7f, Sph, 0.75f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.32f, true);
		for (int32 i = 0; i < 20; ++i)
		{
			const FVector Flat = FRotator(0.0f, FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector() * FMath::FRandRange(0.25f, 1.0f) * Radius * 0.5f;
			Add(SphereMesh, BurstMat, Flat, FRotator::ZeroRotator, Core, 7.0f, 3.0f, Sph, FMath::FRandRange(0.8f, 1.2f),
				FVector::OneVector, FVector(Flat.X * -0.2f, Flat.Y * -0.2f, FMath::FRandRange(110.0f, 260.0f)), NoSpin, 0.0f, false);
		}
	}
	else if (Effect == 3)     // ===== 榴弹爆炸：闪光 -> 火球 -> 双冲击环 -> 大量余烬 =====
	{
		St->Duration = 1.25f;
		St->Gravity = 820.0f;
		Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Flash, 40.0f, 190.0f, Sph, 0.14f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Core, 70.0f, Radius * 0.85f, Sph, 0.62f, FVector(1.0f, 1.0f, 0.9f), FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(PlaneMesh, RingMat, FVector(0.0f, 0.0f, 12.0f), FRotator::ZeroRotator, Color, 70.0f, Radius * 2.0f, Sph, 0.70f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(PlaneMesh, RingMat, FVector(0.0f, 0.0f, 60.0f), FRotator::ZeroRotator, Color * 0.6f, 50.0f, Radius * 1.3f, Sph, 0.65f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.20f, true);
		for (int32 i = 0; i < 24; ++i)
		{
			const FVector Dir = FRotator(FMath::FRandRange(5.0f, 75.0f), FMath::FRandRange(0.0f, 360.0f), 0.0f).Vector();
			Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Core * 0.9f, 10.0f, 2.0f, Sph, FMath::FRandRange(0.7f, 1.2f),
				FVector::OneVector, Dir * FMath::FRandRange(350.0f, 950.0f), NoSpin, 0.0f, false);
		}
	}
	else                      // ===== 迟滞：霜罩缓张 + 地环 + 缓升冰棱 =====
	{
		St->Duration = 1.5f;
		St->Gravity = 120.0f;
		Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Flash, 30.0f, 120.0f, Sph, 0.18f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(SphereMesh, BurstMat, FVector::ZeroVector, FRotator::ZeroRotator, Color * 1.6f, 60.0f, Radius * 0.55f, Sph, 1.35f, FVector(1.0f, 1.0f, 0.6f), FVector::ZeroVector, NoSpin, 0.0f, true);
		Add(PlaneMesh, RingMat, FVector(0.0f, 0.0f, 8.0f), FRotator::ZeroRotator, Core, 60.0f, Radius * 1.0f, Sph, 0.85f, FVector::OneVector, FVector::ZeroVector, NoSpin, 0.0f, true);
		for (int32 i = 0; i < 16; ++i)
		{
			const float Yaw = FMath::FRandRange(0.0f, 360.0f);
			const FVector Flat = FRotator(0.0f, Yaw, 0.0f).Vector() * FMath::FRandRange(0.25f, 1.0f) * Radius * 0.6f;
			Add(CubeMesh, BurstMat, Flat, FRotator(0.0f, Yaw, FMath::FRandRange(-20.0f, 20.0f)), Core, 26.0f, 34.0f, Cub, FMath::FRandRange(1.0f, 1.4f),
				FVector(0.22f, 0.22f, 1.8f), FVector(0.0f, 0.0f, FMath::FRandRange(70.0f, 150.0f)), FVector(0.0f, FMath::FRandRange(-50.0f, 50.0f), 0.0f), 0.0f, true);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[BoB] BurstFX effect=%d bits=%d 半径=%.0f 位置=%s"), Effect, St->Bits.Num(), Radius, *Loc.ToCompactString());

	FTimerHandle FXTimer;
	World->GetTimerManager().SetTimer(FXTimer, FTimerDelegate::CreateWeakLambda(Host, [St]()
	{
		const float Dt = 1.0f / 60.0f;
		St->Elapsed += Dt;
		for (BoBFX::FBit& Bit : St->Bits)
		{
			UStaticMeshComponent* Comp = Bit.Comp.Get();
			if (!Comp) { continue; }
			if (St->Elapsed < Bit.Delay) { Comp->SetVisibility(false); continue; }
			const float Age = St->Elapsed - Bit.Delay;
			if (Age > Bit.Life) { Comp->SetVisibility(false); continue; }
			Comp->SetVisibility(true);
			const float T = FMath::Clamp(Age / Bit.Life, 0.0f, 1.0f);
			// ease-out：起手最快、迅速失势；碎屑走匀速靠速度和重力表现
			const float Curve = Bit.bEase ? (1.0f - FMath::Pow(1.0f - T, 3.0f)) : T;
			Bit.Vel.Z -= St->Gravity * Dt;
			Comp->AddRelativeLocation(Bit.Vel * Dt);
			if (!Bit.Spin.IsNearlyZero())
			{
				Comp->AddRelativeRotation(FRotator(Bit.Spin.X * Dt, Bit.Spin.Y * Dt, Bit.Spin.Z * Dt));
			}
			Comp->SetRelativeScale3D(Bit.Axis * FMath::Max(0.01f, FMath::Lerp(Bit.Scale0, Bit.Scale1, Curve)));
			if (UMaterialInstanceDynamic* Mid = Bit.Mid.Get())
			{
				// 亮度前段稳住、末段快掉，避免整团一起匀速变暗
				Mid->SetScalarParameterValue(TEXT("Fade"), FMath::Pow(1.0f - T, 1.8f));
			}
		}
	}), 1.0f / 60.0f, true);
	Host->SetLifeSpan(St->Duration + 0.05f);
}
float ADiceProjectile::PendingCharge = 0.5f;

ADiceProjectile::ADiceProjectile()
{
	bExplodeOnHit = false;   // 命中效果自定义(爆裂/治疗/迟滞)，不走基类爆炸
	DeferredDestructionTime = 0.1f;
}

void ADiceProjectile::BeginPlay()
{
	Super::BeginPlay();
	// 掷骰随机三效果：0爆裂(伤敌) / 1治疗(回玩家血) / 2迟滞(减速敌人)
	DiceEffect = FMath::RandRange(0, 2);
	switch (DiceEffect)
	{
	// 不可补给 → 每颗都金贵，效果全面加强
	case 0:  DiceColor = FLinearColor(1.00f, 0.40f, 0.05f); DicePower = 520.0f; DiceRadius = 780.0f; break; // 爆裂·橙
	case 1:  DiceColor = FLinearColor(0.20f, 1.00f, 0.45f); DicePower = 280.0f; DiceRadius = 700.0f; break; // 治疗·绿
	default: DiceColor = FLinearColor(0.25f, 0.60f, 1.00f); DicePower = 0.0f;   DiceRadius = 900.0f; break; // 迟滞·蓝
	}
	// 投掷手感：蓄力越久扔得越快越远(PendingCharge 0~1)；保留重力/弹跳成弧
	// 手雷式抛投：初速放慢、抬手角度加大、重力减轻——出手到落地看得清整条弧线
	const float ChargeSpeed = FMath::Lerp(620.0f, 1500.0f, PendingCharge);
	const float ChargeUp = FMath::Lerp(260.0f, 520.0f, PendingCharge);
	if (UProjectileMovementComponent* PM = FindComponentByClass<UProjectileMovementComponent>())
	{
		PM->MaxSpeed = FMath::Max(ChargeSpeed, 1100.0f);
		PM->Velocity = PM->Velocity.GetSafeNormal() * ChargeSpeed + FVector(0.0f, 0.0f, ChargeUp);
		PM->ProjectileGravityScale = 0.85f;
		PM->bShouldBounce = true;
	}
	PendingCharge = 0.5f;   // 用后重置默认
	// 曳光灯染成该效果颜色(飞行时该色光晕、命中时爆闪成该色)
	if (UPointLightComponent* TL = FindComponentByClass<UPointLightComponent>())
	{
		TL->SetLightColor(DiceColor);
		TL->SetIntensity(2000.0f);
		TL->SetAttenuationRadius(360.0f);
	}
	// 旋转飞出的骰子实体
	if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/FabAssets/Dice/Dice.Dice")))
	{
		UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
		C->SetupAttachment(GetRootComponent());
		C->SetStaticMesh(M);
		const FBoxSphereBounds B = M->GetBounds();
		const float Longest = 2.0f * FMath::Max3(B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z);
		C->SetRelativeScale3D(FVector(Longest > 1.0f ? 16.0f / Longest : 1.0f));
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		C->SetCastShadow(false);
		C->RegisterComponent();
		if (UMaterialInterface* Toon = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wasteland/FX/M_BoBDiceToon.M_BoBDiceToon")))
		{
			for (int32 i = 0; i < C->GetNumMaterials(); ++i) { C->SetMaterial(i, Toon); }
		}
	}
}

void ADiceProjectile::NotifyHit(UPrimitiveComponent* MyComp, AActor* Other, UPrimitiveComponent* OtherComp, bool bSelfMoved, FVector HitLocation, FVector HitNormal, FVector NormalImpulse, const FHitResult& Hit)
{
	if (bHit) { return; }
	bHit = true;
	if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(GetRootComponent()))
	{
		Root->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UProjectileMovementComponent* PM = FindComponentByClass<UProjectileMovementComponent>())
	{
		PM->StopMovementImmediately();
	}
	// 范围内按掷骰效果处理
	TArray<FOverlapResult> Overlaps;
	FCollisionShape Shape;
	Shape.SetSphere(DiceRadius);
	FCollisionObjectQueryParams ObjParams;
	ObjParams.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);
	GetWorld()->OverlapMultiByObjectType(Overlaps, GetActorLocation(), FQuat::Identity, ObjParams, Shape, QueryParams);
	TArray<AActor*> Done;
	for (const FOverlapResult& R : Overlaps)
	{
		AActor* A = R.GetActor();
		if (!A || Done.Contains(A)) { continue; }
		Done.Add(A);
		AShooterNPC* NPC = Cast<AShooterNPC>(A);
		if (DiceEffect == 0)          // 爆裂：伤敌
		{
			if (NPC) { UGameplayStatics::ApplyDamage(A, DicePower, GetInstigator() ? GetInstigator()->GetController() : nullptr, this, UDamageType::StaticClass()); }
		}
		else if (DiceEffect == 1)     // 治疗：给玩家(非 NPC 的角色)回血
		{
			if (!NPC) { if (AShooterCharacter* PC = Cast<AShooterCharacter>(A)) { PC->HealPlayer(DicePower); } }
		}
		else                          // 迟滞：减速敌人 5 秒
		{
			if (NPC) { NPC->ApplySlow(0.6f, 5.0f); }
		}
	}
	// 治疗骰：范围内的核心也回血(核心是主目标，扔核心边上回核心)
	if (DiceEffect == 1)
	{
		for (TActorIterator<ABaseCore> It(GetWorld()); It; ++It)
		{
			if (FVector::Dist(It->GetActorLocation(), GetActorLocation()) <= DiceRadius)
			{
				It->RepairBase(DicePower * 2.0f);
			}
		}
	}
	// 落点特效：三种效果三套构型
	BoBSpawnBurstFX(GetWorld(), GetActorLocation(), DiceEffect, DiceColor, DiceRadius);
	// 该色爆闪
	if (UPointLightComponent* TL = FindComponentByClass<UPointLightComponent>())
	{
		TL->SetIntensity(4500.0f);
		TL->SetAttenuationRadius(DiceRadius * 0.9f);
	}
	MakeNoise(2.0f, GetInstigator(), GetActorLocation(), 3000.0f, FName("Explosion"));
	GetWorld()->GetTimerManager().SetTimer(DestructionTimer, FTimerDelegate::CreateWeakLambda(this, [this]() { Destroy(); }), 0.13f, false);
}

// ===== 武器 =====

ACoilRifleWeapon::ACoilRifleWeapon()
{
	VisualMeshPath = TEXT("/Game/FabAssets/CoilRifle/experimental_rifle_coilgun.experimental_rifle_coilgun");
	VisualLen = 82.0f;          // 步枪型：归一到 82cm，用步枪握姿(基类默认 ABP_FP_Weapon)
	VisualOffset = FVector(0.0f, 0.0f, -2.0f);   // -6 压太低，回抬到 -2
	ProjectileClass = ACoilProjectile::StaticClass();
	MagazineSize = 30;
	MaxReserveAmmo = 210; ReserveAmmo = 90;    // 统一初始 3 轮，上限靠弹药箱抬
	RefireRate = 0.34f;     // 射速降下来
	bFullAuto = true;
	AimVariance = 4.0f;
	FiringRecoil = 0.3f;
	ShotLoudness = 1.2f;
}

ALaserPistolWeapon::ALaserPistolWeapon()
{
	VisualMeshPath = TEXT("/Game/FabAssets/SciPistol/Scifi_Pistol1.Scifi_Pistol1");
	VisualLen = 36.0f;          // 手枪型：建模增大一点（30->36cm）
	VisualOffset = FVector(1.3f, -7.5f, 0.0f);   // 向镜头外 7.5cm、往左共 1.3cm
	// 粉紫梦幻皮肤在 BeginPlay 按类型套(M_BoBPistolPastel)
	ProjectileClass = ALaserBoltProjectile::StaticClass();
	MagazineSize = 60;          // 泼水射速配大弹匣(40->60)
	MaxReserveAmmo = 360; ReserveAmmo = 180;   // 统一初始 3 轮
	RefireRate = 0.06f;         // 射速大幅提高(0.16->0.06，近乎泼水)
	bFullAuto = true;           // 高射速配全自动
	AimVariance = 1.1f;         // 精度提高，手感更稳
	FiringRecoil = 0.16f;       // 后坐减小，连射更跟手
}

ADiceWeapon::ADiceWeapon()
{
	VisualMeshPath = TEXT("/Game/FabAssets/Dice/Dice.Dice");
	VisualLen = 9.0f;           // 再缩小：越靠近镜头越显大，13 仍占屏
	// 局部 +X=左、+Y=前(枪口)、+Z=上：往左 5、往回(贴近镜头)6，让右手看着是攥住方块
	// PIE 实测：局部 +X = 屏幕左、+Y = 前、+Z = 上（此前骰子在镜头右 9.9cm 处）。
	// 往左 6cm、向镜头外拉 3cm，骰子右上角落到拇指正左方。
	// Y 拉太近会被 70° FOV 的透视扭成不对称的歪方块，且和拇指穿模，退回 -6.5
	VisualOffset = FVector(7.0f, -8.3f, -1.8f);   // 再向镜头外 0.5cm
	// 绕上轴(俯视)转 36°
	VisualRot = FRotator(0.0f, 36.0f, 0.0f);
	// 骰子不染色：保留原骰子贴图质感(金色染色把它糊成扁平金盘)
	ProjectileClass = ADiceProjectile::StaticClass();
	// 混沌比特：拾取即得 7 颗，且【不可补给】——用完就没有，每颗都金贵
	MagazineSize = 7;
	MaxReserveAmmo = 0; ReserveAmmo = 0;
	RefireRate = 0.9f;
	bFullAuto = false;
	AimVariance = 1.5f;
	FiringRecoil = 0.5f;
	ShotLoudness = 0.6f;
	ShotNoiseRange = 2500.0f;
}

void ADiceWeapon::Fire()
{
	Super::Fire();   // 抛出骰子(基类 FireProjectile)
	if (!VisualMesh) { return; }
	// 手里的骰子藏起来，等"扔出去那颗落地后 0.5 秒"再摸出下一颗
	VisualMesh->SetVisibility(false);

	// 抓刚扔出的那颗(此刻在飞、由本 pawn 发射、创建时间最短的一颗)
	ADiceProjectile* Thrown = nullptr;
	float Newest = BIG_NUMBER;
	for (TActorIterator<ADiceProjectile> It(GetWorld()); It; ++It)
	{
		if (It->GetInstigator() == GetInstigator())
		{
			const float Age = It->GetGameTimeSinceCreation();
			if (Age < Newest) { Newest = Age; Thrown = *It; }
		}
	}

	TWeakObjectPtr<UStaticMeshComponent> WeakVis(VisualMesh);
	TWeakObjectPtr<ADiceProjectile> WeakThrown(Thrown);
	TWeakObjectPtr<ADiceWeapon> WeakThis(this);
	// 轮询：骰子落地即 StopMovementImmediately(速度归零)或已销毁 → 判定落地
	TSharedPtr<FTimerHandle> Poll = MakeShared<FTimerHandle>();
	GetWorldTimerManager().SetTimer(*Poll, FTimerDelegate::CreateWeakLambda(this, [WeakVis, WeakThrown, WeakThis, Poll]()
	{
		ADiceWeapon* Self = WeakThis.Get();
		if (!Self) { return; }
		bool bLanded = false;
		ADiceProjectile* P = WeakThrown.Get();
		if (!P)
		{
			bLanded = true;   // 已销毁=已落地
		}
		else if (UProjectileMovementComponent* PM = P->FindComponentByClass<UProjectileMovementComponent>())
		{
			if (PM->Velocity.SizeSquared() < 25.0f) { bLanded = true; }   // <5cm/s 视为停下
		}
		if (bLanded)
		{
			Self->GetWorldTimerManager().ClearTimer(*Poll);
			FTimerHandle Show;
			Self->GetWorldTimerManager().SetTimer(Show, FTimerDelegate::CreateWeakLambda(Self, [WeakVis]()
			{
				if (UStaticMeshComponent* V = WeakVis.Get()) { V->SetVisibility(true); }
			}), 0.5f, false);
		}
	}), 0.05f, true);
	// 兜底：万一没抓到那颗(Thrown 为空)，按旧的固定时长摸出，别让骰子永远消失
	if (!Thrown)
	{
		GetWorldTimerManager().ClearTimer(*Poll);
		FTimerHandle Fallback;
		GetWorldTimerManager().SetTimer(Fallback, FTimerDelegate::CreateWeakLambda(this, [WeakVis]()
		{
			if (UStaticMeshComponent* V = WeakVis.Get()) { V->SetVisibility(true); }
		}), 1.2f, false);
	}
}

void ADiceWeapon::StartFiring()
{
	// 长按=蓄力：不立即扔，手里骰子保持(未甩出的动作)。摸下一颗期间(骰子隐藏)不允许蓄力
	if (VisualMesh && !VisualMesh->IsVisible())
	{
		return;
	}
	ChargeStartTime = GetWorld()->GetTimeSeconds();
	bIsFiring = true;
}

void ADiceWeapon::StopFiring()
{
	// 松开=按蓄力值扔出(长按越久越快越远)
	if (ChargeStartTime >= 0.0f && CurrentBullets > 0 && VisualMesh && VisualMesh->IsVisible())
	{
		ADiceProjectile::PendingCharge = FMath::Clamp((GetWorld()->GetTimeSeconds() - ChargeStartTime) / 1.2f, 0.15f, 1.0f);
		bIsFiring = true;   // 确保基类 Fire 内 bIsFiring 检查通过
		Fire();
	}
	ChargeStartTime = -1.0f;
	bIsFiring = false;
	GetWorldTimerManager().ClearTimer(RefireTimer);
}

float ADiceWeapon::GetChargeAlpha() const
{
	if (ChargeStartTime < 0.0f || !GetWorld())
	{
		return -1.0f;
	}
	return FMath::Clamp((GetWorld()->GetTimeSeconds() - ChargeStartTime) / 1.2f, 0.0f, 1.0f);
}
