// Copyright Epic Games, Inc. All Rights Reserved.


#include "ShooterCharacter.h"
#include "ShooterWeapon.h"
#include "Weapons/BoBFabWeapons.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EnhancedInputComponent.h"
#include "Components/InputComponent.h"
#include "Components/PawnNoiseEmitterComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Boss_CS07.h"
#include "BoBExecutioner.h"
#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "TimerManager.h"
#include "ShooterGameMode.h"
#include "ShooterGameState.h"
#include "GameFramework/GameModeBase.h"
#include "ShooterHUD.h"
#include "BODPlayerState.h"
#include "LootPickup.h"
#include "WaveManager.h"
#include "BoBShop.h"
#include "BaseCore.h"
#include "AI/ShooterNPC.h"
#include "DrawDebugHelpers.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/DamageType.h"
#include "Net/UnrealNetwork.h"

AShooterCharacter::AShooterCharacter()
{
	// create the noise emitter component
	PawnNoiseEmitter = CreateDefaultSubobject<UPawnNoiseEmitterComponent>(TEXT("Pawn Noise Emitter"));

	// configure movement
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 600.0f, 0.0f);
}

void AShooterCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AShooterCharacter, ComboCount);
	DOREPLIFETIME(AShooterCharacter, ComboMultiplier);
	DOREPLIFETIME(AShooterCharacter, bReadyToStartRep);
	DOREPLIFETIME(AShooterCharacter, Carried);
	DOREPLIFETIME(AShooterCharacter, BankedValue);
	DOREPLIFETIME(AShooterCharacter, ShardFlags);
	DOREPLIFETIME(AShooterCharacter, Gaze);
	DOREPLIFETIME(AShooterCharacter, Relics);
	DOREPLIFETIME(AShooterCharacter, ShopBought);
	DOREPLIFETIME(AShooterCharacter, GazeRateMul);
	DOREPLIFETIME(AShooterCharacter, bAttuning);
	DOREPLIFETIME(AShooterCharacter, ShopRotating);
	DOREPLIFETIME(AShooterCharacter, CinderGainMul);
	DOREPLIFETIME(AShooterCharacter, WeaponDamageMul);
	DOREPLIFETIME(AShooterCharacter, bCoolantActive);
	DOREPLIFETIME(AShooterCharacter, bRevealLoot);
	DOREPLIFETIME(AShooterCharacter, bPhaseNetActive);
	DOREPLIFETIME(AShooterCharacter, bSentryActive);
	DOREPLIFETIME(AShooterCharacter, OwnedItems);
}

void AShooterCharacter::DoToggleShop()
{
	// B 只管开关；买/返回各有专键，避免"按了数字是不是就已经买了"的歧义
	if (bShopOpen)
	{
		SetShopOpen(false);
		return;
	}
	// 只在波间(补给/潜影窗口)允许开店：战斗中不能采买
	bool bBetweenWaves = false;
	for (TActorIterator<AWaveManager> It(GetWorld()); It; ++It)
	{
		bBetweenWaves = It->GetIntervalRemaining() >= 0.0f;
		break;
	}
	if (!bBetweenWaves)
	{
		LootToastMsg = TEXT("投送端只在同化潮间隙开放");
		LootToastUntil = GetWorld()->GetTimeSeconds() + 1.8f;
		return;
	}
	SetShopOpen(true);
}

void AShooterCharacter::SetShopOpen(bool bOpen)
{
	bShopOpen = bOpen;
	ShopSelected = -1;
	ShopHoverRow = -1;
	ShopHoverBtn = -1;
	// 开店即交出鼠标：商店页鼠标要能无限制点选
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->bShowMouseCursor = bOpen;
		if (bOpen)
		{
			PC->SetIgnoreLookInput(true);
			// 只放光标不改输入模式的话，焦点会飘到 Slate，Y/U 就时灵时不灵。
			// GameAndUI + 锁在视口内：鼠标归 UI，键盘仍然派发给 PlayerController。
			FInputModeGameAndUI Mode;
			Mode.SetLockMouseToViewportBehavior(EMouseLockMode::LockAlways);
			Mode.SetHideCursorDuringCapture(false);
			PC->SetInputMode(Mode);
			// 光标落在屏幕中心，不用满屏找
			int32 VX = 0, VY = 0;
			PC->GetViewportSize(VX, VY);
			PC->SetMouseLocation(VX / 2, VY / 2);
		}
		else
		{
			// 用 Reset 而非 SetIgnoreLookInput(false)：那是计数器，重复关会把视角锁死
			PC->ResetIgnoreLookInput();
			PC->SetInputMode(FInputModeGameOnly());
		}
	}
}

void AShooterCharacter::DoShopBack()
{
	// U：详情页 → 货架页；已在货架页则无动作(关店走 B)
	if (bShopOpen && ShopSelected >= 0) { ShopSelected = -1; }
}

void AShooterCharacter::DoShopClick()
{
	if (!bShopOpen) { return; }
	if (ShopSelected >= 0)
	{
		// 详情页：点中"买"/"再看看"按钮才响应，点别处不误触
		if (ShopHoverBtn == 0) { DoShopConfirm(); }
		else if (ShopHoverBtn == 1) { ShopSelected = -1; }
		return;
	}
	if (ShopHoverRow >= 0) { ShopSelectRow(ShopHoverRow); }
}

TArray<uint8> AShooterCharacter::GetShopVisibleItems() const
{
	TArray<uint8> Out;
	for (int32 i = 0; i < BoBShopFixedCount; ++i) { Out.Add(static_cast<uint8>(i)); }
	for (uint8 R : ShopRotating) { Out.Add(R); }
	return Out;
}

void AShooterCharacter::ShopOrWeaponKey(int32 Index)
{
	// 商店开着：数字键 = 选中该商品并展开详情（再按 B 确认购买）
	if (bShopOpen)
	{
		ShopSelectRow(Index);
		return;
	}
	SelectWeaponIndex(Index);
}

void AShooterCharacter::ShopSelectRow(int32 Row)
{
	if (!bShopOpen) { return; }
	const TArray<uint8> Items = GetShopVisibleItems();
	if (!Items.IsValidIndex(Row)) { return; }
	ShopSelected = Row;
}

void AShooterCharacter::DoShopConfirm()
{
	if (!bShopOpen || ShopSelected < 0) { return; }
	const TArray<uint8> Items = GetShopVisibleItems();
	if (!Items.IsValidIndex(ShopSelected)) { return; }
	const uint8 ItemIdx = Items[ShopSelected];
	if (HasAuthority()) { Server_BuyShopItem_Implementation(ItemIdx); }
	else { Server_BuyShopItem(ItemIdx); }
}

void AShooterCharacter::ResetShopForNewWave()
{
	// 新波开打：任何一端都要把店面关掉并把鼠标还给准星
	if (bShopOpen) { SetShopOpen(false); }
	if (!HasAuthority()) { return; }
	ShopBought.Init(0, static_cast<int32>(EBoBShopItem::MAX));
	// 本波临时增益清零（永久类如 Stim/Swift/Hardening 不在此列）
	GazeRateMul = 1.0f;
	CinderGainMul = 1.0f;
	WeaponDamageMul = 1.0f;
	bCoolantActive = false;
	bRevealLoot = false;
	bPhaseNetActive = false;
	bSentryActive = false;

	// 轮换货架：从非常备商品里随机抽 BoBShopRotatingSlots 件
	TArray<uint8> Pool;
	for (int32 i = BoBShopFixedCount; i < static_cast<int32>(EBoBShopItem::MAX); ++i)
	{
		Pool.Add(static_cast<uint8>(i));
	}
	for (int32 i = 0; i < Pool.Num(); ++i)
	{
		Pool.Swap(i, FMath::RandRange(i, Pool.Num() - 1));
	}
	ShopRotating.Reset();
	for (int32 i = 0; i < FMath::Min(BoBShopRotatingSlots, Pool.Num()); ++i)
	{
		ShopRotating.Add(Pool[i]);
	}
}

void AShooterCharacter::Server_BuyShopItem_Implementation(uint8 ItemIndex)
{
	if (ItemIndex >= static_cast<uint8>(EBoBShopItem::MAX) || IsDead()) { return; }
	ABODPlayerState* BPS = GetPlayerState<ABODPlayerState>();
	if (!BPS) { return; }

	if (ShopBought.Num() != static_cast<int32>(EBoBShopItem::MAX))
	{
		ShopBought.Init(0, static_cast<int32>(EBoBShopItem::MAX));
	}

	const EBoBShopItem Item = static_cast<EBoBShopItem>(ItemIndex);
	const FBoBShopEntry& E = GetShopEntry(Item);

	// 本波限购
	if (E.StockPerWave > 0 && ShopBought[ItemIndex] >= E.StockPerWave)
	{
		LootToastMsg = TEXT("本潮已售罄");
		LootToastUntil = GetWorld()->GetTimeSeconds() + 2.0f;
		return;
	}
	// 余烬不足
	if (!BPS->TrySpendCinder(E.Cost))
	{
		LootToastMsg = FString::Printf(TEXT("配额不足（需 %d）"), E.Cost);
		LootToastUntil = GetWorld()->GetTimeSeconds() + 2.0f;
		return;
	}
	ShopBought[ItemIndex]++;

	// 待用道具：买下只是收进道具栏，什么时候用是玩家的事。
	// (流明在补给站脚下点亮、锚向索把你从序核拉回序核，都等于白买)
	if (IsCarryItem(Item))
	{
		OwnedItems.Add(ItemIndex);
		LootToastMsg = FString::Printf(TEXT("收入道具栏：%s　按 Q 使用"), *E.Name);
		LootToastUntil = GetWorld()->GetTimeSeconds() + 2.4f;
		return;
	}

	// 结算效果
	switch (Item)
	{
	// ===== 常备栏 =====
	case EBoBShopItem::Medkit:
		HealPlayer(MaxHP);
		break;
	case EBoBShopItem::AmmoCrate:
		// 抬高当前武器备弹上限一轮，并按新上限补满
		if (CurrentWeapon && CurrentWeapon->GetMaxReserveAmmo() > 0)
		{
			CurrentWeapon->RaiseReserveCap(1);
		}
		break;
	case EBoBShopItem::CoreWeld:
		for (TActorIterator<ABaseCore> It(GetWorld()); It; ++It) { It->RepairBase(250.0f); break; }
		{
			// 隐藏旗标：一旦花配额补过核心，"从未修补"就永久落下。
			// 这里不给任何提示——玩家不该知道刚才这一下动了结局
			extern void BoBSetFlag(FName Flag, bool bValue);
			BoBSetFlag(FName("NeverRepaired"), false);
		}
		break;

	// ===== 轮换栏：创意收藏品(有正面必有代价) =====
	case EBoBShopItem::Coolant:
		// 过载冷却剂：本波射速无限制，但开火持续掉血(在武器开火处按此倍率扣血)
		bCoolantActive = true;
		break;
	case EBoBShopItem::DecoyBeacon:
		// 诱饵信标：揭示全图战利品 + 本波余烬翻倍，代价是注视立刻 +30
		CinderGainMul = 2.0f;
		bRevealLoot = true;
		SetGaze(Gaze + 30.0f);
		break;
	case EBoBShopItem::HedgeContract:
		// 黑市对冲：典当 30% 最大生命换 60 余烬
		if (MaxHP > 40.0f)
		{
			const float Pawn = MaxHP * 0.3f;
			MaxHP -= Pawn;
			CurrentHP = FMath::Min(CurrentHP, MaxHP);
			OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));
			BPS->AddCinder(60);
		}
		break;
	case EBoBShopItem::ScryLens:
		bRevealLoot = true;   // 只给情报，不给战力
		break;
	case EBoBShopItem::PhaseNet:
		// 相位阻断网：锚点周围减速场(标签供 NPC 侧读取)
		bPhaseNetActive = true;
		break;
	case EBoBShopItem::Sentry:
		bSentryActive = true;   // 本波哨戒炮台
		break;
	case EBoBShopItem::Damper:
		GazeRateMul = 0.65f;
		break;
	case EBoBShopItem::TimeExt:
		for (TActorIterator<AWaveManager> It(GetWorld()); It; ++It) { It->ExtendShadowCruise(25.0f); break; }
		break;
	case EBoBShopItem::Stim:
		GrantMaxHealthBonus(15.0f);
		break;
	case EBoBShopItem::WildCore:
	{
		// 野核：七成好事、三成倒霉
		if (FMath::FRand() < 0.7f)
		{
			const int32 Roll = FMath::RandRange(0, 2);
			if (Roll == 0) { BPS->AddCinder(45); LootToastMsg = TEXT("未编目遗构：配额 +45"); }
			else if (Roll == 1) { GrantMaxHealthBonus(20.0f); LootToastMsg = TEXT("未编目遗构：生命上限 +20"); }
			else { SetGaze(0.0f); LootToastMsg = TEXT("未编目遗构：失谐归零"); }
		}
		else
		{
			SetGaze(Gaze + 35.0f);
			LootToastMsg = TEXT("未编目遗构反噬：失谐暴涨！");
		}
		LootToastUntil = GetWorld()->GetTimeSeconds() + 2.4f;
		return;
	}
	default: break;
	}

	LootToastMsg = FString::Printf(TEXT("已购入：%s（-%d 配额）"), *E.Name, E.Cost);
	LootToastUntil = GetWorld()->GetTimeSeconds() + 2.0f;
}

bool AShooterCharacter::IsCarryItem(EBoBShopItem Item)
{
	// 这三件的价值全在"什么时候用"上，买入即生效就等于没有
	switch (Item)
	{
	case EBoBShopItem::Lumen:
	case EBoBShopItem::AnchorLine:
	case EBoBShopItem::Charge:
		return true;
	default:
		return false;
	}
}

void AShooterCharacter::SetAttuning(bool bOn)
{
	if (HasAuthority()) { Server_SetAttuning_Implementation(bOn); }
	else { Server_SetAttuning(bOn); }
}

void AShooterCharacter::Server_SetAttuning_Implementation(bool bOn)
{
	bAttuning = bOn;
}

void AShooterCharacter::ApplyFieldDrain(float FractionOfMax, float FloorPct)
{
	if (!HasAuthority() || FractionOfMax <= 0.0f || MaxHP <= 0.0f) { return; }
	const float Floor = MaxHP * FMath::Clamp(FloorPct, 0.0f, 1.0f);
	if (CurrentHP <= Floor) { return; }
	CurrentHP = FMath::Max(CurrentHP - MaxHP * FractionOfMax, Floor);
}

void AShooterCharacter::DoUseItem()
{
	if (bShopOpen) { return; }
	if (OwnedItems.Num() == 0)
	{
		LootToastMsg = TEXT("道具栏是空的");
		LootToastUntil = GetWorld()->GetTimeSeconds() + 1.6f;
		return;
	}
	const int32 Slot = FMath::Clamp(ItemSlot, 0, OwnedItems.Num() - 1);
	if (HasAuthority()) { Server_UseItem_Implementation(static_cast<uint8>(Slot)); }
	else { Server_UseItem(static_cast<uint8>(Slot)); }
}

void AShooterCharacter::Server_UseItem_Implementation(uint8 SlotIndex)
{
	if (!OwnedItems.IsValidIndex(SlotIndex)) { return; }
	const EBoBShopItem Item = static_cast<EBoBShopItem>(OwnedItems[SlotIndex]);
	const FBoBShopEntry& E = GetShopEntry(Item);

	switch (Item)
	{
	case EBoBShopItem::Lumen:
	{
		// 就地点亮一盏压注视信标（复用 BoBFloodlight 标签）
		FActorSpawnParameters SP;
		SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		if (AActor* B = GetWorld()->SpawnActor<AActor>(AActor::StaticClass(), GetActorLocation(), FRotator::ZeroRotator, SP))
		{
			USceneComponent* Rt = NewObject<USceneComponent>(B);
			B->SetRootComponent(Rt);
			Rt->RegisterComponent();
			B->SetActorLocation(GetActorLocation());
			B->Tags.Add(FName("BoBFloodlight"));
			if (UStaticMesh* M = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
			{
				UStaticMeshComponent* SM = NewObject<UStaticMeshComponent>(B);
				SM->SetupAttachment(Rt);
				SM->SetStaticMesh(M);
				SM->SetRelativeScale3D(FVector(0.5f, 0.5f, 1.4f));
				SM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Wasteland/FX/M_BoBFX_Cyan.M_BoBFX_Cyan")))
				{
					SM->SetMaterial(0, Mat);
				}
				SM->RegisterComponent();
			}
		}
		break;
	}
	case EBoBShopItem::AnchorLine:
		SetActorLocation(FVector(0.0f, 0.0f, GetActorLocation().Z + 50.0f), false, nullptr, ETeleportType::TeleportPhysics);
		break;
	case EBoBShopItem::Charge:
	{
		// 脚下一发大范围高爆，敌我判定只打 NPC
		const FVector Center = GetActorLocation();
		const float Radius = 900.0f;
		for (TActorIterator<AShooterNPC> It(GetWorld()); It; ++It)
		{
			AShooterNPC* NPC = *It;
			if (!NPC || NPC->IsPendingKillPending()) { continue; }
			const float D = FVector::Dist(NPC->GetActorLocation(), Center);
			if (D > Radius) { continue; }
			// 近处满伤，边缘衰减到三成
			const float Falloff = FMath::Lerp(1.0f, 0.3f, FMath::Clamp(D / Radius, 0.0f, 1.0f));
			UGameplayStatics::ApplyDamage(NPC, 420.0f * Falloff, GetController(), this, nullptr);
		}
		if (UWorld* W = GetWorld())
		{
			DrawDebugSphere(W, Center, Radius, 16, FColor(255, 140, 40), false, 1.2f, 0, 4.0f);
		}
		break;
	}
	default:
		return;
	}

	OwnedItems.RemoveAt(SlotIndex);
	LootToastMsg = FString::Printf(TEXT("已使用：%s"), *E.Name);
	LootToastUntil = GetWorld()->GetTimeSeconds() + 2.0f;
}

void AShooterCharacter::BeginPlay()
{
	Super::BeginPlay();

	// reset HP to max
	CurrentHP = MaxHP;

	// update the HUD
	OnDamaged.Broadcast(1.0f);

	// 体力结算定时器（仅本机控制端）
	SprintMeter = SprintTime;
	if (IsLocallyControlled())
	{
		GetWorld()->GetTimerManager().SetTimer(SprintTimer, this, &AShooterCharacter::SprintFixedTick, 0.1f, true);
	}

	// 存放轮询（仅服务器）：背着宝物走近核心自动入库
	if (HasAuthority())
	{
		GetWorld()->GetTimerManager().SetTimer(DepositTimer, this, &AShooterCharacter::DepositTick, 0.5f, true);

		// ===== 测试模式：开局直接发齐所有武器，方便逐把评测（交付前必须置 false）=====
		constexpr bool bBoBWeaponTestMode = false;
		if (bBoBWeaponTestMode)
		{
			FTimerHandle GiveAllHandle;
			GetWorldTimerManager().SetTimer(GiveAllHandle, FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				static const TCHAR* All[] =
				{
					TEXT("/Script/BuildOrBust.CoilRifleWeapon"),
					TEXT("/Game/Variant_Shooter/Blueprints/Pickups/Weapons/BP_ShooterWeapon_GrenadeLauncher.BP_ShooterWeapon_GrenadeLauncher_C"),
					TEXT("/Script/BuildOrBust.LaserPistolWeapon"),
					TEXT("/Script/BuildOrBust.DiceWeapon"),
				};
				for (const TCHAR* P : All)
				{
					if (UClass* C = LoadClass<AShooterWeapon>(nullptr, P))
					{
						AddWeaponClass(TSubclassOf<AShooterWeapon>(C));
					}
				}
			}), 0.6f, false);
		}
	}
}

int32 AShooterCharacter::CarriedSlotsUsed() const
{
	int32 Used = 0;
	for (uint8 K : Carried)
	{
		Used += GetLootDef(static_cast<ELootKind>(K)).Slots;
	}
	return Used;
}

bool AShooterCharacter::TryAddLoot(uint8 Kind)
{
	if (!HasAuthority() || IsDead())
	{
		return false;
	}
	const FLootDef& Def = GetLootDef(static_cast<ELootKind>(Kind));
	if (CarriedSlotsUsed() + Def.Slots > BackpackSlots)
	{
		return false;   // 背包空间不足
	}
	Carried.Add(Kind);
	return true;
}

void AShooterCharacter::DepositTick()
{
	if (IsDead())
	{
		return;
	}

	// ===== 注视值 Gaze(0.5s 一跳，服务器权威)：离锚点(核心=世界原点)越远/揣得越多越涨；安全圈内缓降 =====
	// 招牌机制"贪婪=被看见"：苟着攒一大把不存→越来越烫→触顶招猎手；勤存/回圈才凉。
	const float Dist = GetActorLocation().Size2D();
	// 安全区大幅放宽：核心周边 2500 都算"锚点区"，不必紧贴核心才回落
	const float SafeR = 2500.0f;
	// 满值距离大幅拉远：要走到离核心 ~18000 才可能被顶满
	const float FarR = 18000.0f;
	if (bAttuning)
	{
		// 主动同调压过锚点区回落——不然站在核心边上按住没有任何反应，
		// 玩家会以为这个键坏了。想升失谐就一定升得上去，代价自负
		SetGaze(Gaze + AttuneRate);
	}
	else if (Dist <= SafeR)
	{
		SetGaze(Gaze - 4.0f);     // 锚点区内回落
	}
	else
	{
		int32 CarriedValue = 0;
		for (uint8 K : Carried) { CarriedValue += GetLootDef(static_cast<ELootKind>(K)).Value; }
		// 深入系数 0~1：距离在 SafeR~FarR 之间线性
		const float DeepT = FMath::Clamp((Dist - SafeR) / (FarR - SafeR), 0.0f, 1.0f);
		const float Rise = 0.25f                                  // 基础(慢)
			+ 0.15f * (static_cast<float>(CarriedValue) / 100.0f)  // 揣得越贵越烫
			+ 1.6f * DeepT;                                        // 走得越深越烫
		SetGaze(Gaze + Rise * GazeRateMul);   // 信号抑制器可降低本波涨速
	}

	// 应急探照装置：地图各处的回复点，站进去快速压注视
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		if (It->ActorHasTag(FName("BoBFloodlight")) &&
			FVector::Dist2D(It->GetActorLocation(), GetActorLocation()) <= 900.0f)
		{
			SetGaze(Gaze - 9.0f);
			break;
		}
	}

	// ===== 存放：背着宝物走近核心自动入库(原逻辑) =====
	if (Carried.Num() == 0 || Dist > SafeR)
	{
		return;
	}
	for (uint8 K : Carried)
	{
		const ELootKind Kind = static_cast<ELootKind>(K);
		BankedValue += GetLootDef(Kind).Value;
		if (Kind == ELootKind::ShardA) { ShardFlags |= 1; }
		if (Kind == ELootKind::ShardB) { ShardFlags |= 2; }
	}
	// 单人集齐并存放残片甲+乙 → 一次性套装奖励
	if ((ShardFlags & 3) == 3 && !(ShardFlags & 4))
	{
		ShardFlags |= 4;
		BankedValue += 400;
	}
	Carried.Empty();
	SetGaze(Gaze - 25.0f);   // 锚定(兑现)额外压注视：存了就安静
}

void AShooterCharacter::DoPickup()
{
	if (HasAuthority())
	{
		Server_TryPickupNearest_Implementation();
	}
	else
	{
		Server_TryPickupNearest();
	}
}

void AShooterCharacter::DoDropLoot()
{
	if (HasAuthority())
	{
		Server_DropLastLoot_Implementation();
	}
	else
	{
		Server_DropLastLoot();
	}
}

void AShooterCharacter::Server_TryPickupNearest_Implementation()
{
	const FVector MyLoc = GetActorLocation();

	// 补给站(核心旁)：靠近按 F —— 花余烬补当前武器备弹(混沌比特不可补给)
	for (TActorIterator<AActor> SIt(GetWorld()); SIt; ++SIt)
	{
		if (!SIt->ActorHasTag(FName("BoBSupplyStation"))) { continue; }
		if (FVector::Dist2D(SIt->GetActorLocation(), MyLoc) > 520.0f) { continue; }
		if (AShooterWeapon* W = CurrentWeapon)
		{
			ABODPlayerState* BPS = GetPlayerState<ABODPlayerState>();
			const int32 Missing = W->GetMaxReserveAmmo() - W->GetReserveAmmo();
			if (W->GetMaxReserveAmmo() <= 0)
			{
				LootToastMsg = TEXT("该武器无法补给");
			}
			else if (Missing <= 0)
			{
				LootToastMsg = TEXT("备弹已满");
			}
			else if (!BPS)
			{
				LootToastMsg = TEXT("补给失败");
			}
			else
			{
				// 余烬计价：每 10 发备弹 1 余烬（至少 1）；钱不够就按现有余烬尽量补
				const int32 FullCost = FMath::Max(1, FMath::CeilToInt(Missing / 10.0f));
				const int32 PayCost = FMath::Min(FullCost, BPS->GetCinder());
				if (PayCost <= 0)
				{
					LootToastMsg = FString::Printf(TEXT("配额不足（需 %d）"), FullCost);
				}
				else
				{
					BPS->TrySpendCinder(PayCost);
					const int32 Added = W->AddReserveAmmo(PayCost * 10);
					LootToastMsg = FString::Printf(TEXT("补给：备弹 +%d（花费 %d 配额）"), Added, PayCost);
				}
			}
			LootToastUntil = GetWorld()->GetTimeSeconds() + 2.2f;
		}
		return;   // 站在补给站前时，F 用于补给，不再抢拾取
	}

	// CS-07 前的第三交互项：插入封停阵列。
	// 只有揣着第九十八枚才出现——没有它时靠近 Boss 按 F 什么都不会发生，
	// 所以这个选项对没走隐藏线的玩家是完全不可见的。
	{
		extern bool BoBGetFlag(FName Flag);
		if (BoBGetFlag(FName("Seal98")))
		{
			for (TActorIterator<ABoss_CS07> BIt(GetWorld()); BIt; ++BIt)
			{
				ABoss_CS07* Boss = *BIt;
				if (!IsValid(Boss)) { continue; }
				if (FVector::Dist(Boss->GetActorLocation(), MyLoc) > 900.0f) { continue; }
				// 置全局标志而不是直接调 WaveManager：结算入口 FinishRun 是私有的，
				// 从这里调不到，而为它加一个公开方法就要动 .h → 关编辑器整编。
				// WaveManager 自己的 0.5 秒巡检会收走这面旗
				extern bool GBoBOverwriteRequested;
				GBoBOverwriteRequested = true;
				LootToastMsg = TEXT("第九十八枚插进了阵列。");
				LootToastUntil = GetWorld()->GetTimeSeconds() + 4.0f;
				return;
			}
		}
	}

	// 空白封停件（第九十八枚）：结局三的核心道具。
	// 三条硬性前置，缺一不可：三份线索齐、失谐拉满、**处决者存活且在追击中**。
	// 处决者判定是这一条的关键——它把"最危险的状态"变成刻录窗口，而不是障碍。
	{
		extern void BoBSetFlag(FName Flag, bool bValue);
		extern bool BoBGetFlag(FName Flag);
		for (TActorIterator<AActor> SIt(GetWorld()); SIt; ++SIt)
		{
			AActor* S = *SIt;
			if (!S->ActorHasTag(FName("BoBSeal98"))) { continue; }
			if (FVector::Dist(S->GetActorLocation(), MyLoc) > 460.0f) { continue; }

			const bool bClues = BoBGetFlag(FName("Relic1"))
				&& BoBGetFlag(FName("Relic2")) && BoBGetFlag(FName("Relic3"));
			bool bExecAlive = false;
			for (TActorIterator<ABoBExecutioner> EIt(GetWorld()); EIt; ++EIt)
			{
				if (IsValid(*EIt)) { bExecAlive = true; break; }
			}

			if (BoBGetFlag(FName("Seal98")))
			{
				LootToastMsg = TEXT("第九十八枚已刻录完成。");
			}
			else if (!bClues)
			{
				LootToastMsg = TEXT("石面是空的。你不知道该刻什么。");
			}
			else if (Gaze < 100.0f)
			{
				LootToastMsg = FString::Printf(
					TEXT("刻痕对不上手。失谐 %.0f，还不够。"), Gaze);
			}
			else if (!bExecAlive)
			{
				LootToastMsg = TEXT("手很稳。太稳了，刻不进去。");
			}
			else
			{
				BoBSetFlag(FName("Seal98"), true);
				LootToastMsg = TEXT("三、一、四。左、左、右。09:07。最后一道刻完了。");
				UE_LOG(LogTemp, Log, TEXT("[BoB] 第九十八枚封停件刻录完成"));
			}
			LootToastUntil = GetWorld()->GetTimeSeconds() + 4.0f;
			return;
		}
	}

	// 隐藏者：地图深处的一具无生命智慧体。它不动、不说话，只在你靠近时给出一个
	// 数字。达成之后回来，它才承认你。委托内容刻意是"清野外"而不是"守核心"——
	// 它关心的和勘察局关心的不是同一件事，这本身就是叙事。
	{
		extern int32 GBoBWildKills;
		extern void BoBSetFlag(FName Flag, bool bValue);
		extern bool BoBGetFlag(FName Flag);
		const int32 NeedKills = 8;
		for (TActorIterator<AActor> HIt(GetWorld()); HIt; ++HIt)
		{
			AActor* H = *HIt;
			if (!H->ActorHasTag(FName("BoBHermit"))) { continue; }
			if (FVector::Dist(H->GetActorLocation(), MyLoc) > 440.0f) { continue; }

			if (BoBGetFlag(FName("Hermit")))
			{
				LootToastMsg = TEXT("它不再回应。该给的已经给过了。");
			}
			else if (GBoBWildKills >= NeedKills)
			{
				BoBSetFlag(FName("Hermit"), true);
				if (ABODPlayerState* BPS = GetPlayerState<ABODPlayerState>())
				{
					BPS->AddCinder(400);
				}
				SetGaze(FMath::Max(0.0f, Gaze - 25.0f));
				LootToastMsg = TEXT("它把一枚温热的东西按进你掌心。配额 +400，失谐 −25。");
			}
			else
			{
				LootToastMsg = FString::Printf(
					TEXT("它在地上划了一个数：%d。你已划掉 %d 个。"), NeedKills, GBoBWildKills);
			}
			LootToastUntil = GetWorld()->GetTimeSeconds() + 3.6f;
			return;
		}
	}

	// 勘探物：地图上散布的可交互点。碰一下竖起一面隐藏旗标，但**只给一句现场描述**，
	// 绝不说"你解锁了什么"。玩家读到的是这个世界的一段记录，不是一条成就提示。
	{
		extern void BoBSetFlag(FName Flag, bool bValue);
		// 同一处勘探物，早去和晚去看到的不一样：地表正在被同化，记录也跟着变。
		// 这让"什么时候去"成为决策，也让重玩有理由——把真相拆成碎片散在地点上，
		// 再拆一次散在时间上。
		// Flag 为空的条目是**纯世界观碎片**：不推进任何东西。全都推进的话，
		// 玩家会把每个可交互物当成打卡清单，探索就退化成清单管理。
		struct FLore
		{
			const TCHAR* Label; const TCHAR* Flag; float GazeNeed;
			const TCHAR* Early; const TCHAR* Late;
		};
		static const FLore Lore[] = {
			{ TEXT("BoB_Lore_Glyph"), TEXT("Glyph"), 0.0f,
			  TEXT("刻纹与封停件上的那一组完全一致。它们不是记号，是同一句话。"),
			  TEXT("刻纹变深了。你上次来时它还只是划痕，现在像是自己长出来的。") },
			// 三份遗留物各给刻法的三分之一。文本本身就是线索载体：
			// 排列顺序（A）、走向（B）、读数（C），三条合起来才拼得出一道刻法
			{ TEXT("BoB_Lore_Relic1"), TEXT("Relic1"), 0.0f,
			  TEXT("制式背包。空样本架从左到右是：三、一、四，中间那格空着。"),
			  TEXT("样本架结了薄晶，但那三个数还认得出：三、一、四。") },
			{ TEXT("BoB_Lore_Relic2"), TEXT("Relic2"), 0.0f,
			  TEXT("手绘地形图。三条等高线的走向被人用炭笔加粗过：左、左、右。"),
			  TEXT("加粗的那三条线更深了。走向没变：左、左、右。") },
			{ TEXT("BoB_Lore_Relic3"), TEXT("Relic3"), 0.0f,
			  TEXT("计时器。停机读数停在 09:07，秒位一直没动。"),
			  TEXT("计时器又走了一格。读数变成 09:08——它在等谁把最后一道刻完。") },
			{ TEXT("BoB_Lore_Res"), TEXT("Resonance"), 60.0f,
			  TEXT("柱体随你的呼吸改变频率。你分不清是它在跟你，还是你在跟它。"),
			  TEXT("这次是它先起的调子。你跟上了，而且跟得很自然。") },
			// —— 以下不推进任何旗标，只是这个世界的一角 ——
			{ TEXT("BoB_Lore_Mark1"), nullptr, 0.0f,
			  TEXT("石面上刻着九十七道竖线，最后一道没刻完。"),
			  TEXT("那道没刻完的竖线，现在刻完了。") },
			{ TEXT("BoB_Lore_Mark2"), nullptr, 0.0f,
			  TEXT("一处被反复踩实的凹地。有人在这里站了很久，面朝天坑。"),
			  TEXT("凹地边上多了第二处。站的方向和第一处一模一样。") },
		};

		int32 WaveNow = 0;
		for (TActorIterator<AWaveManager> WIt(GetWorld()); WIt; ++WIt)
		{
			WaveNow = WIt->GetCurrentWave();
			break;
		}
		for (TActorIterator<AActor> LIt(GetWorld()); LIt; ++LIt)
		{
			AActor* A = *LIt;
			if (!A->ActorHasTag(FName("BoBLore"))) { continue; }
			if (FVector::Dist(A->GetActorLocation(), MyLoc) > 420.0f) { continue; }

			const FString Lbl = A->GetActorLabel();
			for (const FLore& L : Lore)
			{
				if (Lbl != L.Label) { continue; }
				if (L.GazeNeed > 0.0f && Gaze < L.GazeNeed)
				{
					LootToastMsg = TEXT("柱体没有反应。你还太清醒。");
				}
				else
				{
					if (L.Flag) { BoBSetFlag(FName(L.Flag), true); }
					// 第 5 潮起换成后期文本。同化在推进，同一处地方就不该还是老样子
					LootToastMsg = (WaveNow >= 5 && L.Late) ? L.Late : L.Early;
				}
				break;
			}
			LootToastUntil = GetWorld()->GetTimeSeconds() + 3.6f;
			return;   // 站在勘探物前时，F 归它
		}
	}

	// 封停件：靠近按 F 唤醒。结局二的解锁链，也是本作最主要的取舍装置。
	// 门槛是失谐而不是配额优先——你得先让自己被同化到读得懂它们，才谈得上花钱。
	{
		extern int32 GBoBSealsAwake;
		extern const int32 GBoBSealsNeeded;
		for (TActorIterator<AActor> SIt(GetWorld()); SIt; ++SIt)
		{
			AActor* Seal = *SIt;
			if (!Seal->ActorHasTag(FName("BoBSeal"))) { continue; }
			if (FVector::Dist(Seal->GetActorLocation(), MyLoc) > 460.0f) { continue; }

			if (Seal->ActorHasTag(FName("BoBSealOn")))
			{
				LootToastMsg = FString::Printf(TEXT("此封停件已唤醒（%d/%d）"),
					GBoBSealsAwake, GBoBSealsNeeded);
			}
			else if (Gaze < 50.0f)
			{
				// 复用已有的凝视档位：读不懂就是读不懂，没有另一条绕过去的路
				LootToastMsg = FString::Printf(TEXT("读数与实感对不上（需失谐 50，当前 %.0f）"), Gaze);
			}
			else
			{
				ABODPlayerState* BPS = GetPlayerState<ABODPlayerState>();
				// 越往后越贵：前面几枚要能试，最后几枚必须付出防线
				const int32 Cost = 60 + GBoBSealsAwake * 45;
				if (!BPS || BPS->GetCinder() < Cost)
				{
					LootToastMsg = FString::Printf(TEXT("配额不足（需 %d）"), Cost);
				}
				else
				{
					BPS->TrySpendCinder(Cost);
					Seal->Tags.Add(FName("BoBSealOn"));
					GBoBSealsAwake++;

					// 唤醒装置＝放大同化。代价落在所有人头上，不只是按 F 的那个
					for (TActorIterator<AShooterCharacter> PIt(GetWorld()); PIt; ++PIt)
					{
						PIt->GazeRateMul = FMath::Min(2.6f, PIt->GazeRateMul + 0.18f);
					}

					// 天坑口的光随进度变亮：守在核心的队友抬头就知道进行到哪了，
					// 不需要任何 UI。地图自己在报进度
					for (TActorIterator<AActor> BIt(GetWorld()); BIt; ++BIt)
					{
						if (!BIt->ActorHasTag(FName("BoBSealBeacon"))) { continue; }
						if (UPointLightComponent* L = BIt->FindComponentByClass<UPointLightComponent>())
						{
							L->SetIntensity(40000.0f + 130000.0f * GBoBSealsAwake);
							L->SetAttenuationRadius(9000.0f + 1400.0f * GBoBSealsAwake);
						}
					}

					LootToastMsg = (GBoBSealsAwake >= GBoBSealsNeeded)
						? FString::Printf(TEXT("封停阵列已足数（%d/%d）——CS-07 可被击败"),
							GBoBSealsAwake, GBoBSealsNeeded)
						: FString::Printf(TEXT("封停件唤醒 %d/%d（花费 %d 配额，失谐涨速上升）"),
							GBoBSealsAwake, GBoBSealsNeeded, Cost);
					UE_LOG(LogTemp, Log, TEXT("[BoB] 封停件 %d/%d 唤醒"),
						GBoBSealsAwake, GBoBSealsNeeded);
				}
			}
			LootToastUntil = GetWorld()->GetTimeSeconds() + 2.6f;
			return;   // 站在封停件前时，F 归它
		}
	}

	ALootPickup* Best = nullptr;
	float BestD = 450.0f;   // 拾取半径（扁平/半埋网格的实体中心可能离人较远，放宽）
	for (TActorIterator<ALootPickup> It(GetWorld()); It; ++It)
	{
		if (It->bTaken) { continue; }
		const float D = FVector::Dist2D(It->GetActorLocation(), MyLoc);
		if (D < BestD) { BestD = D; Best = *It; }
	}
	if (Best)
	{
		Best->TryGiveTo(this);
	}
}

void AShooterCharacter::Server_DropLastLoot_Implementation()
{
	if (Carried.Num() == 0 || IsDead())
	{
		return;
	}
	const uint8 Kind = Carried.Last();
	Carried.Pop();
	// 在脚前生成掉落物（网格由 LootPickup 兜底为木箱）
	const FVector DropLoc = GetActorLocation() + GetActorForwardVector() * 140.0f - FVector(0, 0, 60.0f);
	if (ALootPickup* P = GetWorld()->SpawnActor<ALootPickup>(ALootPickup::StaticClass(), DropLoc, FRotator::ZeroRotator))
	{
		P->Kind = static_cast<ELootKind>(Kind);
		P->Tags.Add(FName("BoBLootGem"));
	}
	LootToastMsg = FString::Printf(TEXT("已丢弃:%s"), GetLootDef(static_cast<ELootKind>(Kind)).Name);
	LootToastUntil = GetWorld()->GetTimeSeconds() + 2.2f;
	if (!IsLocallyControlled())
	{
		Client_NotifyLoot(true, Kind, false);   // 客户端提示由 Client RPC 覆盖文本（简化：重发拾取样式）
	}
}

void AShooterCharacter::DoDropWeapon()
{
	if (HasAuthority())
	{
		Server_DropWeapon_Implementation();
	}
	else
	{
		Server_DropWeapon();
	}
}

void AShooterCharacter::Server_DropWeapon_Implementation()
{
	// 至少保留一把武器
	if (OwnedWeapons.Num() <= 1 || !CurrentWeapon || IsDead())
	{
		return;
	}
	AShooterWeapon* Dropped = CurrentWeapon;
	const TSubclassOf<AShooterWeapon> DropClass = Dropped->GetClass();

	// 切到另一把
	int32 NextIdx = OwnedWeapons.Find(Dropped) == 0 ? 1 : 0;
	Dropped->DeactivateWeapon();
	OwnedWeapons.Remove(Dropped);
	CurrentWeapon = OwnedWeapons[FMath::Clamp(NextIdx, 0, OwnedWeapons.Num() - 1)];
	CurrentWeapon->ActivateWeapon();
	Dropped->Destroy();

	// 脚前生成武器拾取物（木箱外观，拾取直接归还这把枪）
	const FVector DropLoc = GetActorLocation() + GetActorForwardVector() * 150.0f - FVector(0, 0, 60.0f);
	if (ALootPickup* P = GetWorld()->SpawnActor<ALootPickup>(ALootPickup::StaticClass(), DropLoc, FRotator::ZeroRotator))
	{
		P->Kind = ELootKind::WeaponMod;
		P->WeaponClassOverride = DropClass;
		P->Tags.Add(FName("BoBLootWpn"));
	}
	LootToastMsg = TEXT("已丢弃武器");
	LootToastUntil = GetWorld()->GetTimeSeconds() + 2.2f;
	if (!IsLocallyControlled())
	{
		Client_NotifyLoot(true, static_cast<uint8>(ELootKind::WeaponMod), false);
	}
}

void AShooterCharacter::Client_NotifyLoot_Implementation(bool bOk, uint8 Kind, bool bWeapon)
{
	const FLootDef& Def = GetLootDef(static_cast<ELootKind>(Kind));
	if (!bOk && bWeapon)
	{
		LootToastMsg = TEXT("已持有该武器");
	}
	else if (!bOk)
	{
		LootToastMsg = TEXT("背包容量不足！先回核心存放");
	}
	else if (bWeapon)
	{
		LootToastMsg = TEXT("获得新武器！按 B 查看");
	}
	else if (static_cast<ELootKind>(Kind) == ELootKind::WeaponMod)
	{
		LootToastMsg = TEXT("武器已改装，弹药补满");
	}
	else
	{
		LootToastMsg = FString::Printf(TEXT("拾取：%s（%d分）"), Def.Name, Def.Value);
	}
	LootToastUntil = GetWorld()->GetTimeSeconds() + 2.2f;
}

void AShooterCharacter::SelectWeaponIndex(int32 Index)
{
	if (IsDead() || !OwnedWeapons.IsValidIndex(Index) || OwnedWeapons[Index] == CurrentWeapon)
	{
		return;
	}
	if (CurrentWeapon)
	{
		CurrentWeapon->DeactivateWeapon();
	}
	CurrentWeapon = OwnedWeapons[Index];
	CurrentWeapon->ActivateWeapon();
}

void AShooterCharacter::EndPlay(EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	// clear the respawn timer
	GetWorld()->GetTimerManager().ClearTimer(RespawnTimer);
}

void AShooterCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// base class handles move, aim and jump inputs
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Firing
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Started, this, &AShooterCharacter::DoStartFiring);
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &AShooterCharacter::DoStopFiring);

		// Switch weapon 的 EnhancedInput 绑定与 Shift 疾跑冲突（模板把它映射在 Shift 上），
		// 改用下方 CapsLock / 滚轮 传统绑定，这里不再绑 SwitchWeaponAction
	}

	// 疾跑与 HUD 提示：传统按键绑定（与 Enhanced Input 并存，无需新增输入资产）
	if (PlayerInputComponent)
	{
		PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AShooterCharacter::DoStartSprint);
		PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AShooterCharacter::DoEndSprint);
		FInputKeyBinding& RKey = PlayerInputComponent->BindKey(EKeys::R, IE_Pressed, this, &AShooterCharacter::DoToggleHints);
		RKey.bExecuteWhenPaused = true;   // 暂停中也要能按 R 恢复
		// 暂停时 EnhancedInput 不派发 FireAction，左键翻简报页需要这条暂停豁免的传统绑定兜底
		FInputKeyBinding& LMB = PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &AShooterCharacter::DoStartFiring);
		LMB.bExecuteWhenPaused = true;
		PlayerInputComponent->BindKey(EKeys::E, IE_Pressed, this, &AShooterCharacter::DoToggleMap);
		// 补给商店：B 开关 / 数字选商品 / Y 买 / U 再看看（Y、U 同排相邻，手不用挪）
		PlayerInputComponent->BindKey(EKeys::B, IE_Pressed, this, &AShooterCharacter::DoToggleShop);   // CS 式：B 开商店
		FInputKeyBinding& YKey = PlayerInputComponent->BindKey(EKeys::Y, IE_Pressed, this, &AShooterCharacter::DoShopConfirm);
		YKey.bExecuteWhenPaused = true;
		FInputKeyBinding& UKey = PlayerInputComponent->BindKey(EKeys::U, IE_Pressed, this, &AShooterCharacter::DoShopBack);
		UKey.bExecuteWhenPaused = true;
		PlayerInputComponent->BindKey(EKeys::One, IE_Pressed, this, &AShooterCharacter::SelectWeapon1);
		PlayerInputComponent->BindKey(EKeys::Two, IE_Pressed, this, &AShooterCharacter::SelectWeapon2);
		PlayerInputComponent->BindKey(EKeys::Three, IE_Pressed, this, &AShooterCharacter::SelectWeapon3);
		PlayerInputComponent->BindKey(EKeys::Four, IE_Pressed, this, &AShooterCharacter::SelectWeapon4);
		PlayerInputComponent->BindKey(EKeys::Five, IE_Pressed, this, &AShooterCharacter::SelectWeapon5);
		PlayerInputComponent->BindKey(EKeys::Six, IE_Pressed, this, &AShooterCharacter::SelectWeapon6);
		PlayerInputComponent->BindKey(EKeys::Seven, IE_Pressed, this, &AShooterCharacter::SelectWeapon7);
		PlayerInputComponent->BindKey(EKeys::Eight, IE_Pressed, this, &AShooterCharacter::SelectWeapon8);
		PlayerInputComponent->BindKey(EKeys::N, IE_Pressed, this, &AShooterCharacter::DoToggleBackpack);   // 背包挪到 N
		PlayerInputComponent->BindKey(EKeys::F, IE_Pressed, this, &AShooterCharacter::DoPickup);
		PlayerInputComponent->BindKey(EKeys::CapsLock, IE_Pressed, this, &AShooterCharacter::DoSwitchWeapon);
		PlayerInputComponent->BindKey(EKeys::MouseScrollUp, IE_Pressed, this, &AShooterCharacter::DoSwitchWeapon);
		PlayerInputComponent->BindKey(EKeys::MouseScrollDown, IE_Pressed, this, &AShooterCharacter::DoSwitchWeapon);
		PlayerInputComponent->BindKey(EKeys::Q, IE_Pressed, this, &AShooterCharacter::DoUseItem);      // Q 用道具
		PlayerInputComponent->BindKey(EKeys::G, IE_Pressed, this, &AShooterCharacter::DoDropWeapon);   // 枪战经典：G 丢枪
		PlayerInputComponent->BindKey(EKeys::H, IE_Pressed, this, &AShooterCharacter::DoDropLoot);
	}
}

void AShooterCharacter::DoStartSprint()
{
	// 体力太低（<15%）不允许起跑，避免抖动
	if (SprintMeter < SprintTime * 0.15f)
	{
		return;
	}
	ApplySprint(true);
	if (!HasAuthority())
	{
		Server_SetSprint(true);
	}
}

void AShooterCharacter::SprintFixedTick()
{
	if (bSprintingLocal && GetVelocity().SizeSquared() > 100.0f)
	{
		SprintMeter = FMath::Max(SprintMeter - 0.1f, 0.0f);
		if (SprintMeter <= 0.0f)
		{
			// 体力耗尽：强制停跑（本地 + 上报服务器）
			DoEndSprint();
		}
	}
	else
	{
		SprintMeter = FMath::Min(SprintMeter + SprintRegenRate * 0.1f, SprintTime);
	}

	// 搭同一个 0.1s 本机定时器驱动注视表现，不额外开 Tick
	UpdateGazePostProcess(0.1f);
}

void AShooterCharacter::UpdateGazePostProcess(float DeltaSeconds)
{
	if (!IsLocallyControlled())
	{
		return;
	}
	UCameraComponent* Cam = GetFirstPersonCameraComponent();
	if (!Cam)
	{
		return;
	}

	// 后处理材质实例(只建一次，挂在本机相机的后处理设置上)
	if (!GazePPMID)
	{
		UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/BoB/FX/M_BoBGazePP.M_BoBGazePP"));
		if (!Base)
		{
			return;
		}
		GazePPMID = UMaterialInstanceDynamic::Create(Base, this);
		Cam->PostProcessSettings.WeightedBlendables.Array.Empty();
		Cam->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(1.0f, GazePPMID));
		Cam->PostProcessBlendWeight = 1.0f;
	}

	const float G = FMath::Clamp(Gaze / 100.0f, 0.0f, 1.0f);

	// ===== 屏幕四周红光呼吸(渐显渐隐，非突兀眨眼) =====
	// 场景整体压暗由材质按 Gaze 处理(有下限 0.42，不会全黑)；这里只驱动边缘光的呼吸相位。
	// 注视越高，呼吸越快越强：警觉慢而淡，暴露急而浓。
	// 边缘特效只在注视越过阈值后才出现；越过后强度与呼吸速度随注视继续攀升
	// 前期几乎无感、后期猛烈：第一临界(40%)前完全不出特效，之后用二次曲线急速拉升
	constexpr float EdgeThreshold = 0.40f;
	float EdgeAmt = 0.0f;
	float Pulse = 0.0f;
	float GazeVisual = 0.0f;   // 喂给材质的注视强度(前期压到近 0)
	if (G > EdgeThreshold)
	{
		// T：阈值之上的归一化推进度 0~1
		// 曲线 = 0.05*T³ + 0.5*T² + 0.45*T：前段弱、中段抬起、后段猛
		const float T = FMath::Clamp((G - EdgeThreshold) / (1.0f - EdgeThreshold), 0.0f, 1.0f);
		const float TC = 0.05f * T * T * T + 0.5f * T * T + 0.45f * T;
		GazeVisual = TC;
		EdgeAmt = FMath::Lerp(0.10f, 2.6f, TC);                // 起始极弱，满值更强
		const float Speed = FMath::Lerp(1.2f, 12.0f, TC);      // 呼吸由极慢到极快
		BlinkPhase += DeltaSeconds * Speed;
		if (BlinkPhase > 2.0f * PI) { BlinkPhase -= 2.0f * PI; }
		Pulse = 0.5f + 0.5f * FMath::Sin(BlinkPhase);
	}
	else
	{
		BlinkPhase = 0.0f;
	}

	GazePPMID->SetScalarParameterValue(TEXT("Gaze"), GazeVisual);
	GazePPMID->SetScalarParameterValue(TEXT("Blink"), Pulse);
	GazePPMID->SetScalarParameterValue(TEXT("Edge"), EdgeAmt);
}

void AShooterCharacter::DoEndSprint()
{
	ApplySprint(false);
	if (!HasAuthority())
	{
		Server_SetSprint(false);
	}
}

void AShooterCharacter::Server_SetSprint_Implementation(bool bOn)
{
	ApplySprint(bOn);
}

void AShooterCharacter::ApplySprint(bool bOn)
{
	if (bSprintingLocal == bOn)
	{
		return;
	}
	bSprintingLocal = bOn;
	if (UCharacterMovementComponent* Move = GetCharacterMovement())
	{
		Move->MaxWalkSpeed = bOn ? Move->MaxWalkSpeed * SprintMultiplier : Move->MaxWalkSpeed / SprintMultiplier;
	}
}

void AShooterCharacter::DoToggleHints()
{
	// 结算画面挂出后：R = 重开一局（仅主机能发起，ServerTravel 会带上客户端）
	if (AShooterGameState* EndGS = GetWorld()->GetGameState<AShooterGameState>(); EndGS && EndGS->IsMatchOver())
	{
		if (AGameModeBase* GM = GetWorld()->GetAuthGameMode())
		{
			GM->ProcessServerTravel(TEXT("?restart"), false);
		}
		return;
	}

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AShooterHUD* HUD = Cast<AShooterHUD>(PC->GetHUD()))
		{
			HUD->ToggleGuide();
			if (!HUD->IsGuideVisible())
			{
				NotifyGuideClosed();   // 完整看完两页并关闭 = 开局就绪确认
			}
		}
	}
}

void AShooterCharacter::NotifyGuideClosed()
{
	if (bSentReady)
	{
		return;
	}
	bSentReady = true;
	if (HasAuthority())
	{
		bReadyToStartRep = true;
	}
	else
	{
		Server_SetReadyToStart();
	}
}

void AShooterCharacter::Server_SetReadyToStart_Implementation()
{
	bReadyToStartRep = true;
}

void AShooterCharacter::DoToggleMap()
{
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AShooterHUD* HUD = Cast<AShooterHUD>(PC->GetHUD()))
		{
			HUD->ToggleMapSize();
		}
	}
}

void AShooterCharacter::DoToggleBackpack()
{
	// 商店开着时不抢 N：商店的买/返回各有专键
	if (bShopOpen) { return; }
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		if (AShooterHUD* HUD = Cast<AShooterHUD>(PC->GetHUD()))
		{
			HUD->ToggleBackpack();
		}
	}
}

float AShooterCharacter::TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// ignore if already dead
	if (CurrentHP <= 0.0f)
	{
		return 0.0f;
	}

	// 调试无敌：放在最前面，护甲/减伤那一串都不用走
	if (bDebugInvulnerable)
	{
		return 0.0f;
	}

	// 应用护甲减伤
	Damage *= (1.0f - DamageReductionPct);

	// Reduce HP
	CurrentHP -= Damage;

	// Have we depleted HP?
	if (CurrentHP <= 0.0f)
	{
		Die();
	}

	// update the HUD
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));

	return Damage;
}

void AShooterCharacter::GrantMaxHealthBonus(float BonusMax)
{
	MaxHP += BonusMax;
	CurrentHP = MaxHP;
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));
}

void AShooterCharacter::HealPlayer(float Amount)
{
	if (CurrentHP <= 0.0f)
	{
		return;
	}
	// 隐藏旗标：治过一次就永久落下。判定放在实际回血这一步而不是道具使用处，
	// 这样不管从哪条路回的血都算数——旗标要盯的是"你到底有没有回过血"这件事本身
	if (Amount > 0.0f && CurrentHP < MaxHP)
	{
		extern void BoBSetFlag(FName Flag, bool bValue);
		BoBSetFlag(FName("NeverHealed"), false);
	}
	CurrentHP = FMath::Min(MaxHP, CurrentHP + Amount);
	OnDamaged.Broadcast(FMath::Max(0.0f, CurrentHP / MaxHP));
}

void AShooterCharacter::AddDamageReduction(float Pct)
{
	DamageReductionPct = FMath::Clamp(DamageReductionPct + Pct, 0.0f, 0.8f);
}

void AShooterCharacter::AddCombo()
{
	ComboCount++;
	// 倍率 1.0 → 3.0，每击杀 +0.1
	ComboMultiplier = FMath::Min(1.0f + ComboCount * 0.1f, 3.0f);

	// 4 秒内无击杀则重置
	GetWorld()->GetTimerManager().SetTimer(
		ComboResetTimer, this, &AShooterCharacter::ResetCombo, 4.0f, false);
}

void AShooterCharacter::ResetCombo()
{
	ComboCount = 0;
	ComboMultiplier = 1.0f;
}

void AShooterCharacter::DoAim(float Yaw, float Pitch)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoAim(Yaw, Pitch);
	}
}

void AShooterCharacter::DoMove(float Right, float Forward)
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoMove(Right, Forward);
	}
}

void AShooterCharacter::DoJumpStart()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpStart();
	}
}

void AShooterCharacter::DoJumpEnd()
{
	// only route inputs if the character is not dead
	if (!IsDead())
	{
		Super::DoJumpEnd();
	}
}

void AShooterCharacter::DoStartFiring()
{
	// 同一次点击会同时走 EnhancedInput(FireAction) 和 LeftMouseButton 传统绑定（暂停豁免）两条路：
	// 每角色 30ms 去抖，既防简报连翻两页，也防全自动武器同一击双发（同帧两路间隔仅微秒级）
	static TMap<TWeakObjectPtr<const AShooterCharacter>, double> LastFirePressMap;
	const double Now = FPlatformTime::Seconds();
	double& LastPress = LastFirePressMap.FindOrAdd(this, -10.0);
	if (Now - LastPress < 0.03)
	{
		return;
	}
	LastPress = Now;

	// 指南面板打开时：本次点击只用于关闭面板，不开火
	if (APlayerController* GuidePC = Cast<APlayerController>(GetController()))
	{
		if (AShooterHUD* HUD = Cast<AShooterHUD>(GuidePC->GetHUD()))
		{
			if (HUD->IsGuideVisible())
			{
				HUD->CloseGuide();   // 第一页→翻页；第二页→真正关闭
				if (!HUD->IsGuideVisible())
				{
					NotifyGuideClosed();
				}
				return;
			}
		}
	}

	// 商店打开时：本次点击交给商店，绝不走火
	if (bShopOpen)
	{
		DoShopClick();
		return;
	}

	// 保险：任何情况下第一次开火也视为开局就绪（防状态错位卡在等待）
	NotifyGuideClosed();

	// 只在本地开火：客户端子弹命中后通过 Server_ReportHit 上报服务器结算（见 ShooterProjectile::ProcessHit）
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StartFiring();
	}
}

void AShooterCharacter::DoStopFiring()
{
	if (CurrentWeapon && !IsDead())
	{
		CurrentWeapon->StopFiring();
	}
}

void AShooterCharacter::Server_ReportHit_Implementation(AActor* HitActor, float Damage)
{
	// 客户端(P2)的子弹在本地判定命中，把命中目标上报服务器权威结算伤害。
	// EventInstigator = 本玩家控制器 → 丧尸死亡时击杀归属本玩家、正确计分。
	if (HitActor)
	{
		UGameplayStatics::ApplyDamage(HitActor, Damage, GetController(), this, UDamageType::StaticClass());
	}
}

void AShooterCharacter::DoSwitchWeapon()
{
	// ensure we have at least two weapons two switch between
	if (OwnedWeapons.Num() > 1 && !IsDead())
	{
		// deactivate the old weapon
		CurrentWeapon->DeactivateWeapon();

		// find the index of the current weapon in the owned list
		int32 WeaponIndex = OwnedWeapons.Find(CurrentWeapon);

		// is this the last weapon?
		if (WeaponIndex == OwnedWeapons.Num() - 1)
		{
			// loop back to the beginning of the array
			WeaponIndex = 0;
		}
		else {
			// select the next weapon index
			++WeaponIndex;
		}

		// set the new weapon as current
		CurrentWeapon = OwnedWeapons[WeaponIndex];

		// activate the new weapon
		CurrentWeapon->ActivateWeapon();
	}
}

void AShooterCharacter::AttachWeaponMeshes(AShooterWeapon* Weapon)
{
	const FAttachmentTransformRules AttachmentRule(EAttachmentRule::SnapToTarget, false);

	// attach the weapon actor
	Weapon->AttachToActor(this, AttachmentRule);

	// attach the weapon meshes
	Weapon->GetFirstPersonMesh()->AttachToComponent(GetFirstPersonMesh(), AttachmentRule, FirstPersonWeaponSocket);
	Weapon->GetThirdPersonMesh()->AttachToComponent(GetMesh(), AttachmentRule, FirstPersonWeaponSocket);
	
}

void AShooterCharacter::PlayFiringMontage(UAnimMontage* Montage)
{
	// stub
}

void AShooterCharacter::AddWeaponRecoil(float Recoil)
{
	// 只在本地控制端施加后坐力：服务器为客户端(P2)权威开火时不再重复施加，
	// 否则客户端会被"本地+服务器"双重后坐力来回拉扯，导致镜头/持枪剧烈抖动、打不准
	if (IsLocallyControlled())
	{
		AddControllerPitchInput(Recoil);
	}
}

void AShooterCharacter::UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize)
{
	HUDAmmo = CurrentAmmo;
	HUDMag = MagazineSize;
	OnBulletCountUpdated.Broadcast(MagazineSize, CurrentAmmo);
}

FVector AShooterCharacter::GetWeaponTargetLocation()
{
	// trace ahead from the camera viewpoint
	FHitResult OutHit;

	const FVector Start = GetFirstPersonCameraComponent()->GetComponentLocation();
	const FVector End = Start + (GetFirstPersonCameraComponent()->GetForwardVector() * MaxAimDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	GetWorld()->LineTraceSingleByChannel(OutHit, Start, End, ECC_Visibility, QueryParams);

	// return either the impact point or the trace end
	return OutHit.bBlockingHit ? OutHit.ImpactPoint : OutHit.TraceEnd;
}

void AShooterCharacter::AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass)
{
	// do we already own this weapon?
	AShooterWeapon* OwnedWeapon = FindWeaponOfType(WeaponClass);

	if (!OwnedWeapon)
	{
		// spawn the new weapon
		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.Instigator = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.TransformScaleMethod = ESpawnActorScaleMethod::MultiplyWithRoot;

		AShooterWeapon* AddedWeapon = GetWorld()->SpawnActor<AShooterWeapon>(WeaponClass, GetActorTransform(), SpawnParams);

		if (AddedWeapon)
		{
			// add the weapon to the owned list
			OwnedWeapons.Add(AddedWeapon);

			// if we have an existing weapon, deactivate it
			if (CurrentWeapon)
			{
				CurrentWeapon->DeactivateWeapon();
			}

			// switch to the new weapon
			CurrentWeapon = AddedWeapon;
			CurrentWeapon->ActivateWeapon();
		}
	}
}

void AShooterCharacter::OnWeaponActivated(AShooterWeapon* Weapon)
{
	// update the bullet counter
	OnBulletCountUpdated.Broadcast(Weapon->GetMagazineSize(), Weapon->GetBulletCount());

	// set the character mesh AnimInstances
	GetFirstPersonMesh()->SetAnimInstanceClass(Weapon->GetFirstPersonAnimInstanceClass());
	GetMesh()->SetAnimInstanceClass(Weapon->GetThirdPersonAnimInstanceClass());

	// 切武器立即刷新 Canvas 弹药显示(否则显示上一把的数，如切到骰子还显示手枪的 14)
	UpdateWeaponHUD(Weapon->GetBulletCount(), Weapon->GetMagazineSize());
	// (纯色/渐变染色被用户否掉——太土，恢复各武器原 PBR 贴图；要彩色走卡通渲染另做)

	// 远端代理没有 Controller，FP 手臂动画蓝图会每帧刷 GetController 报错——
	// 非本机控制的角色手臂只在被渲染时评估姿势（bOnlyOwnerSee 下等于不评估）
	GetFirstPersonMesh()->VisibilityBasedAnimTickOption = IsLocallyControlled()
		? EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones
		: EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
}

void AShooterCharacter::OnWeaponDeactivated(AShooterWeapon* Weapon)
{
	// unused
}

void AShooterCharacter::OnSemiWeaponRefire()
{
	// unused
}

AShooterWeapon* AShooterCharacter::FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const
{
	// check each owned weapon
	for (AShooterWeapon* Weapon : OwnedWeapons)
	{
		if (Weapon->IsA(WeaponClass))
		{
			return Weapon;
		}
	}

	// weapon not found
	return nullptr;

}

void AShooterCharacter::Die()
{
	// 调试无敌必须在这里也拦一道。原来只在 TakeDamage 里挡，于是**摔死和掉出世界
	// 照样能杀掉开着无敌的玩家**——地图现在有 46m 的天坑和 57m 的入口坑，
	// 这条路径比挨打常见得多。而且人一死，CheckExecutioner 会跳过死亡玩家，
	// 处决者也就永远不来了：两个现象是同一个根因。
	if (bDebugInvulnerable)
	{
		CurrentHP = FMath::Max(CurrentHP, MaxHP * 0.5f);
		// 掉到世界底下就捞回地面，否则会一直往下掉、一直触发
		if (GetActorLocation().Z < -12000.0f)
		{
			FHitResult Hit;
			const FVector Probe(GetActorLocation().X, GetActorLocation().Y, 30000.0f);
			if (GetWorld()->LineTraceSingleByChannel(Hit, Probe,
					Probe - FVector(0.0f, 0.0f, 45000.0f), ECC_WorldStatic))
			{
				SetActorLocation(Hit.ImpactPoint + FVector(0.0f, 0.0f, 150.0f),
					false, nullptr, ETeleportType::TeleportPhysics);
			}
			else
			{
				SetActorLocation(FVector(0.0f, 0.0f, 800.0f), false, nullptr,
					ETeleportType::TeleportPhysics);
			}
		}
		return;
	}

	// deactivate the weapon
	if (IsValid(CurrentWeapon))
	{
		CurrentWeapon->DeactivateWeapon();
	}

	// increment the team score
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->IncrementTeamScore(TeamByte);
	}

	// grant the death tag to the character
	Tags.Add(DeathTag);
		
	// stop character movement
	GetCharacterMovement()->StopMovementImmediately();

	// disable controls
	DisableInput(nullptr);

	// reset the bullet counter UI
	OnBulletCountUpdated.Broadcast(0, 0);

	// call the BP handler
	BP_OnDeath();

	// 玩家死亡 = 游戏失败（肉鸽不复活）
	if (AShooterGameMode* GM = Cast<AShooterGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->TriggerLose();
	}
}

void AShooterCharacter::OnRespawn()
{
	// destroy the character to force the PC to respawn
	Destroy();
}

bool AShooterCharacter::IsDead() const
{
	// the character is dead if their current HP drops to zero
	return CurrentHP <= 0.0f;
}
