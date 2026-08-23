#include "LootPickup.h"
#include "Variant_Shooter/ShooterCharacter.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "Variant_Shooter/Weapons/ShooterWeapon.h"
#include "UpgradeComponent.h"
#include "UpgradeTypes.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Net/UnrealNetwork.h"

const FLootDef& GetLootDef(ELootKind Kind)
{
	static const FLootDef Defs[] =
	{
		{ TEXT("自复矿簇"),     1,  60, true,
		  TEXT("采掘指南明确标注该晶体只需三日便能重新生长。当剥离下它冰凉的断面时，红外仪上却显示着高于环境的诡异热量——这片被同化污染的大地，展现出了比我们更高效的自愈逻辑。") },
		{ TEXT("未逾期口粮"),   1,  80, false,
		  TEXT("无论按哪种已知历法推算，罐底钢印的日期都已经是四个世纪前的往事了。但外包装的保质期永远停滞在“剩余三个月”，内部封存的炖肉至今仍散发着令人作呕的温热。") },
		{ TEXT("计量牌"),       1,  95, false,
		  TEXT("哪怕这种边缘磨亮的金属片曾象征过原住文明的某种绝对权威，它遵循的六进制在这里也毫无意义。在投送端的物资置换协议里，一把沉甸甸的历史遗物，刚好能按废铁的重量兑换半盒弹药。") },
		{ TEXT("未译数据板"),   1, 110, false,
		  TEXT("唤醒屏幕的瞬间，狂乱无序的铭文便会占据前三行不断重组。唯独第四行的七个字符，在勘察局回收的所有样本中，都保持着一种令人窒息的绝对一致。") },
		{ TEXT("同化滤芯"),     2, 150, false,
		  TEXT("带有标准通用法兰的圆柱滤筒设备。滤网上凝固的灰白残渣，是原住民妄图将同化物质从生态中物理剥离的铁证。它曾承载过庞大的清场计划，如今却只剩下基准崩塌后的死寂。") },
		{ TEXT("恒态封存舱"),   2, 170, false,
		  TEXT("没有任何接缝的合金表面，完美融于周遭环境的温度。但只要接入感应器，读数便会死死咬住零下二十度的极寒——比起储藏设备，它更像是一个拒绝与外部世界发生热力学交互的休眠者。") },
		{ TEXT("谐振晶"),       2, 190, false,
		  TEXT("靠近基准场时它的热辐射足以烫伤皮肤，一旦脱离照射范围，又会迅速陷入刺骨的冰冷。这种荒谬的物理反差，完美映射了 K-11 坐标下早已被彻底颠覆的现实逻辑。") },
		{ TEXT("双生钥·启"),    3, 260, false,
		  TEXT("当这把日轮纹路的密匙被顺时针旋转两周半时，内部沉闷的机械咬合声会穿透手腕的骨骼。设计它的人坚信，某种庞大之物正蛰伏在深渊中等待唤醒。") },
		{ TEXT("双生钥·封"),    3, 260, false,
		  TEXT("出土数量呈现压倒性优势的月轮纹密匙，被发现时总是首尾相连结成死寂的闭环。面对无可挽回的同化侵袭，这里的大多数原住民最终选择转动它，亲手锁死了自己的文明。") },
		{ TEXT("基准核心·初代残件"), 3, 330, false,
		  TEXT("尺寸规格与现役基准发生器完全一致的废弃外壳。表面被利器刻意刮平的区域，原本应该印着完整的制式编号。这件遗物默默证明了一个事实：我们并非点亮黑暗的第一人，我们只是在模仿死者的提灯姿势。") },
		{ TEXT("武器缓存"),     0,   0, true,  TEXT("") },
	};
	return Defs[static_cast<uint8>(Kind)];
}

ALootPickup::ALootPickup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	SetReplicateMovement(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);

	Trigger = CreateDefaultSubobject<USphereComponent>(TEXT("Trigger"));
	Trigger->SetupAttachment(Mesh);
	Trigger->SetSphereRadius(150.0f);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
}

void ALootPickup::BeginPlay()
{
	Super::BeginPlay();
	// 布设脚本用标签标注箱型（WeaponClassOverride 无 EditAnywhere，编辑器脚本设不了属性）：
	// BoBWpnR=主武器箱(步枪) BoBWpnP=副武器箱(手枪) BoBWpnG=爆炸物箱(榴弹)
	if (HasAuthority() && Kind == ELootKind::WeaponMod && !WeaponClassOverride)
	{
		const TCHAR* Path =
			Tags.Contains(FName("BoBWpnR")) ? TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Rifle.BP_ShooterWeapon_Rifle_C") :
			Tags.Contains(FName("BoBWpnP")) ? TEXT("/Script/BuildOrBust.LaserPistolWeapon") :
			Tags.Contains(FName("BoBWpnG")) ? TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher_C") :
			Tags.Contains(FName("BoBWpnS")) ? TEXT("/Script/BuildOrBust.SniperWeapon") :
			// 赤霄刃/断潮战斧已移除。旧标签重定向到现存武器，免得地图上留下捡不到东西的空点位
			Tags.Contains(FName("BoBWpnK")) ? TEXT("/Script/BuildOrBust.CoilRifleWeapon") :
			Tags.Contains(FName("BoBWpnA")) ? TEXT("/Script/BuildOrBust.LaserPistolWeapon") : nullptr;
		if (Path)
		{
			WeaponClassOverride = LoadClass<AShooterWeapon>(nullptr, Path);
		}
	}
	// 改为 F 键手动拾取（不再绑定自动碰撞拾取）
	// 运行时生成的掉落物没有网格 → 兜底用木箱
	if (!Mesh->GetStaticMesh())
	{
		if (UStaticMesh* Crate = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/Wasteland/HexApoc/SM_Wooden_Crate.SM_Wooden_Crate")))
		{
			Mesh->SetStaticMesh(Crate);
		}
	}
	ApplyVisibility();
}

void ALootPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ALootPickup, Kind);
	DOREPLIFETIME(ALootPickup, bTaken);
	DOREPLIFETIME(ALootPickup, WeaponClassOverride);
}

void ALootPickup::OnRep_Taken()
{
	ApplyVisibility();
}

void ALootPickup::ApplyVisibility()
{
	SetActorHiddenInGame(bTaken);
	Trigger->SetCollisionEnabled(bTaken ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
	// 场景里的光柱/点光在编辑器中附着为子 actor，随本体一起隐显（隐藏的点位不能亮灯）
	TArray<AActor*> Attached;
	GetAttachedActors(Attached);
	for (AActor* A : Attached)
	{
		A->SetActorHiddenInGame(bTaken);
	}
}

void ALootPickup::SetTaken(bool bNewTaken)
{
	bTaken = bNewTaken;
	ApplyVisibility();
}

void ALootPickup::OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult)
{
	// 已弃用（改为 F 键手动拾取）
}

bool ALootPickup::TryGiveTo(AShooterCharacter* Char)
{
	if (bTaken || !HasAuthority() || !Char || Char->IsDead())
	{
		return false;
	}

	if (Kind == ELootKind::WeaponMod)
	{
		// 类别武器箱：给该类别里"玩家还没有的一把随机枪"；该类别集齐才给随机词条+补满弹
		//   主武器箱 = 奔雷磁轨/热寂单元；副武器箱 = 曳星光铳；爆炸物箱 = 混沌比特(7颗)
		if (WeaponClassOverride)
		{
			static const TCHAR* PrimaryPool[] =
			{
				TEXT("/Script/BuildOrBust.CoilRifleWeapon"),
				TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher_C"),
			};
			static const TCHAR* SidearmPool[] =
			{
				TEXT("/Script/BuildOrBust.LaserPistolWeapon"),
			};
			// 蚀日狙/近战已删；原近战箱(Katana/Axe 标签)改发爆炸物骰子
			static const TCHAR* ExplosivePool[] =
			{
				TEXT("/Script/BuildOrBust.DiceWeapon"),
			};
			const FString Cn = WeaponClassOverride->GetName();
			const TCHAR* const* Pool = PrimaryPool;
			int32 PoolNum = UE_ARRAY_COUNT(PrimaryPool);
			if (Cn.Contains(TEXT("Katana")) || Cn.Contains(TEXT("Axe")) || Cn.Contains(TEXT("Dice")))
			{
				Pool = ExplosivePool; PoolNum = UE_ARRAY_COUNT(ExplosivePool);
			}
			else if (Cn.Contains(TEXT("Pistol")))   // LaserPistol 亦含 Pistol → 归副武器
			{
				Pool = SidearmPool; PoolNum = UE_ARRAY_COUNT(SidearmPool);
			}
			// 收集该类别里玩家还没有的枪
			TArray<UClass*> Missing;
			for (int32 i = 0; i < PoolNum; ++i)
			{
				UClass* Cls = LoadClass<AShooterWeapon>(nullptr, Pool[i]);
				if (!Cls)
				{
					continue;
				}
				bool bHas = false;
				for (AShooterWeapon* W : Char->GetOwnedWeapons())
				{
					if (W && W->GetClass() == Cls)
					{
						bHas = true;
						break;
					}
				}
				if (!bHas)
				{
					Missing.Add(Cls);
				}
			}
			if (Missing.Num() > 0)
			{
				Char->AddWeaponClass(TSubclassOf<AShooterWeapon>(Missing[FMath::RandRange(0, Missing.Num() - 1)]));
				Char->Client_NotifyLoot(true, static_cast<uint8>(Kind), true);
			}
			else
			{
				static const EUpgradeType OwnedMods[] =
				{
					EUpgradeType::DamageUp, EUpgradeType::DoubleFire, EUpgradeType::PiercingBullet,
					EUpgradeType::FrostBullet, EUpgradeType::PoisonBullet, EUpgradeType::RicochetBullet,
				};
				if (UUpgradeComponent* Up = Char->FindComponentByClass<UUpgradeComponent>())
				{
					Up->Server_ApplyUpgrade(OwnedMods[FMath::RandRange(0, UE_ARRAY_COUNT(OwnedMods) - 1)]);
				}
				if (AShooterWeapon* W = Char->GetCurrentWeapon())
				{
					W->RefillAmmo();
				}
				Char->Client_NotifyLoot(true, static_cast<uint8>(Kind), false);
			}
			SetTaken(true);
			return true;
		}
		// 武器缓存：优先补一把没有的枪；枪齐了则给随机攻击词条 + 补满弹匣
		static const TCHAR* WeaponBPs[] =
		{
			TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_Pistol.BP_ShooterWeapon_Pistol_C"),
			TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher_C"),
		};
		UClass* GrantClass = nullptr;
		for (const TCHAR* Path : WeaponBPs)
		{
			UClass* Cls = LoadClass<AShooterWeapon>(nullptr, Path);
			if (!Cls) { continue; }
			bool bOwned = false;
			for (AShooterWeapon* W : Char->GetOwnedWeapons())
			{
				if (W && W->GetClass() == Cls) { bOwned = true; break; }
			}
			if (!bOwned) { GrantClass = Cls; break; }
		}
		if (GrantClass)
		{
			Char->AddWeaponClass(TSubclassOf<AShooterWeapon>(GrantClass));
			Char->Client_NotifyLoot(true, static_cast<uint8>(Kind), true);
		}
		else
		{
			static const EUpgradeType Mods[] =
			{
				EUpgradeType::DamageUp, EUpgradeType::DoubleFire, EUpgradeType::PiercingBullet,
				EUpgradeType::FrostBullet, EUpgradeType::PoisonBullet, EUpgradeType::RicochetBullet,
			};
			if (UUpgradeComponent* Up = Char->FindComponentByClass<UUpgradeComponent>())
			{
				Up->Server_ApplyUpgrade(Mods[FMath::RandRange(0, UE_ARRAY_COUNT(Mods) - 1)]);
			}
			if (AShooterWeapon* W = Char->GetCurrentWeapon())
			{
				W->RefillAmmo();
			}
			Char->Client_NotifyLoot(true, static_cast<uint8>(Kind), false);
		}
		SetTaken(true);
		return true;
	}

	// 宝物：尝试装入背包（占格制），成功/容量不足均给本机提示
	if (Char->TryAddLoot(static_cast<uint8>(Kind)))
	{
		Char->Client_NotifyLoot(true, static_cast<uint8>(Kind), false);
		SetTaken(true);
		return true;
	}
	Char->Client_NotifyLoot(false, static_cast<uint8>(Kind), false);
	return false;
}
