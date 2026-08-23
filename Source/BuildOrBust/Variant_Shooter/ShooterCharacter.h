// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BuildOrBustCharacter.h"
#include "ShooterWeaponHolder.h"
#include "BoBShop.h"
#include "ShooterCharacter.generated.h"

class AShooterWeapon;
class UInputAction;
class UInputComponent;
class UPawnNoiseEmitterComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBulletCountUpdatedDelegate, int32, MagazineSize, int32, Bullets);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDamagedDelegate, float, LifePercent);

/**
 *  A player controllable first person shooter character
 *  Manages a weapon inventory through the IShooterWeaponHolder interface
 *  Manages health and death
 */
UCLASS(abstract)
class BUILDORBUST_API AShooterCharacter : public ABuildOrBustCharacter, public IShooterWeaponHolder
{
	GENERATED_BODY()
	
	/** AI Noise emitter component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components", meta = (AllowPrivateAccess = "true"))
	UPawnNoiseEmitterComponent* PawnNoiseEmitter;

protected:

	/** Fire weapon input action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* FireAction;

	/** Switch weapon input action */
	UPROPERTY(EditAnywhere, Category ="Input")
	UInputAction* SwitchWeaponAction;

	/** Name of the first person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category ="Weapons")
	FName FirstPersonWeaponSocket = FName("HandGrip_R");

	/** Name of the third person mesh weapon socket */
	UPROPERTY(EditAnywhere, Category ="Weapons")
	FName ThirdPersonWeaponSocket = FName("HandGrip_R");

	/** Max distance to use for aim traces */
	UPROPERTY(EditAnywhere, Category ="Aim", meta = (ClampMin = 0, ClampMax = 100000, Units = "cm"))
	float MaxAimDistance = 10000.0f;

	/** Max HP this character can have */
	UPROPERTY(EditAnywhere, Category="Health")
	float MaxHP = 500.0f;

	/** Current HP remaining to this character */
	float CurrentHP = 0.0f;

	/** Team ID for this character*/
	UPROPERTY(EditAnywhere, Category="Team")
	uint8 TeamByte = 0;

	/** Actor tag to grant this character when it dies */
	UPROPERTY(EditAnywhere, Category="Team")
	FName DeathTag = FName("Dead");

	/** List of weapons picked up by the character */
	TArray<AShooterWeapon*> OwnedWeapons;

	/** Weapon currently equipped and ready to shoot with */
	TObjectPtr<AShooterWeapon> CurrentWeapon;

	UPROPERTY(EditAnywhere, Category ="Destruction", meta = (ClampMin = 0, ClampMax = 10, Units = "s"))
	float RespawnTime = 5.0f;

	FTimerHandle RespawnTimer;

	/** 疾跑倍率（乘法开关，与增益的 MaxWalkSpeed 乘法叠加不冲突） */
	UPROPERTY(EditAnywhere, Category="Sprint")
	float SprintMultiplier = 1.6f;

	/** 体力上限（可持续疾跑秒数） */
	UPROPERTY(EditAnywhere, Category="Sprint")
	float SprintTime = 6.0f;

	/** 体力恢复速率（秒体力/秒） */
	UPROPERTY(EditAnywhere, Category="Sprint")
	float SprintRegenRate = 0.9f;

	/** 当前体力（仅本机控制端结算，耗尽自动停跑） */
	float SprintMeter = 6.0f;

	/** 体力固定结算（0.1s 定时器，仅本机控制端启动） */
	void SprintFixedTick();

	FTimerHandle SprintTimer;

	/** 本地疾跑状态 */
	bool bSprintingLocal = false;

	/** Shift 按下/松开 */
	void DoStartSprint();
	void DoEndSprint();

	/** 实际应用/取消疾跑速度 */
	void ApplySprint(bool bOn);

	/** 客户端疾跑状态上报服务器（CharacterMovement 服务器权威） */
	UFUNCTION(Server, Reliable)
	void Server_SetSprint(bool bOn);

	/** R 键：暂停并打开完整指南 */
	void DoToggleHints();

	/** E 键：小地图放大/缩小 */
	void DoToggleMap();

	/** 开局就绪：关闭简报即视为就绪（只上报一次） */
	bool bSentReady = false;
	void NotifyGuideClosed();

	UFUNCTION(Server, Reliable)
	void Server_SetReadyToStart();

	/** 开局就绪旗（复制；Shooter 模式用默认 PlayerState，故挂在角色上） */
	UPROPERTY(Replicated)
	bool bReadyToStartRep = false;

public:

	bool IsReadyToStart() const { return bReadyToStartRep; }

protected:

	/** Canvas HUD 数据缓存（UpdateWeaponHUD 写入） */
	int32 HUDAmmo = 0;
	int32 HUDMag = 0;

public:

	/** 供 Canvas HUD 读取的状态 */
	void GetAmmoCounts(int32& OutAmmo, int32& OutMag) const { OutAmmo = HUDAmmo; OutMag = HUDMag; }
	float GetHealthPercent() const { return MaxHP > 0.0f ? CurrentHP / MaxHP : 0.0f; }

	/**
	 *  失谐力场抽血（Boss 阶段三 · 静默走廊）。
	 *  按最大 HP 的比例扣，扣到 FloorPct 就停手。
	 *
	 *  不走 TakeDamage 是刻意的：力场不该触发受击反馈、不该记击杀者，
	 *  更不该杀死玩家。规格书 6.4 明确"环境只施压，不杀人"，
	 *  唯一致命源是朝圣者接触。
	 */
	UFUNCTION(BlueprintCallable, Category="BoB")
	void ApplyFieldDrain(float FractionOfMax, float FloorPct);

	/**
	 *  主动同调（按住不放）：自己把失谐往上顶。
	 *
	 *  没有这个开关，"污染自己是主动战术"就只是句设定——失谐只会随距离和
	 *  身上遗构被动地涨，玩家没有任何手段在需要的时候把它拉到温床的开壳线。
	 *  做成按住而不是开关：松手就停，涨到哪一档由玩家每一刻自己决定，
	 *  而不是设一次然后忘掉。处决者在 100 降临，所以顶多高永远是个现场判断。
	 */
	UFUNCTION(BlueprintCallable, Category="BoB")
	void SetAttuning(bool bOn);

	UFUNCTION(BlueprintPure, Category="BoB")
	bool IsAttuning() const { return bAttuning; }

	/** 主动同调每次结算涨多少失谐（结算频率同失谐 tick） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="BoB")
	float AttuneRate = 3.2f;

	UPROPERTY(Replicated)
	bool bAttuning = false;

	UFUNCTION(Server, Reliable)
	void Server_SetAttuning(bool bOn);

	/** 调试无敌（BoB.God）。只在非 Shipping 下会被命令打开 */
	UPROPERTY(BlueprintReadWrite, Category="BoB|Debug")
	bool bDebugInvulnerable = false;
	float GetSprintPercent() const { return SprintTime > 0.0f ? SprintMeter / SprintTime : 0.0f; }
	bool IsSprinting() const { return bSprintingLocal; }

	//~ 背包 / 存放（切片二：宝物拾取-背回-核心存放-撤离结算）

	/** 背包容量（格） */
	static constexpr int32 BackpackSlots = 5;

	/** 背包中的宝物（ELootKind 值列表，复制供 HUD/结算） */
	UPROPERTY(Replicated)
	TArray<uint8> Carried;

	/** 已存入核心的价值（含残片套装奖励，撤离时折算进得分） */
	UPROPERTY(Replicated)
	int32 BankedValue = 0;

	/** 残片存放标记：bit0=甲 bit1=乙 bit2=套装奖励已发 */
	UPROPERTY(Replicated)
	uint8 ShardFlags = 0;

	// ===== 肉鸽 run 状态(阶段0 骨架；涨降/阈值逻辑在阶段1 填充) =====

	/** 注视值 0~100：离锚点搜刮时上涨，锚定/待安全圈下降；触顶招来猎手(招牌机制)。 */
	UPROPERTY(Replicated)
	float Gaze = 0.0f;

	/** 已获得的异变(肉鸽词条 Key 列表；阶段3 用，先留空复制)。 */
	UPROPERTY(Replicated)
	TArray<FName> Relics;

	float GetGaze() const { return Gaze; }
	/** 服务器：设置注视值(钳到 0~100)。 */
	void SetGaze(float In) { Gaze = FMath::Clamp(In, 0.0f, 100.0f); }
	/** 注视四档：0平静(0-40) 1警觉(40-70) 2锁定(70-99) 3触顶(>=100)；供 HUD/眨眼/刷怪/猎手读。 */
	int32 GetGazeBand() const
	{
		if (Gaze >= 100.0f) { return 3; }
		if (Gaze >= 70.0f)  { return 2; }
		if (Gaze >= 40.0f)  { return 1; }
		return 0;
	}
	const TArray<FName>& GetRelics() const { return Relics; }

	/** 本机：把注视值喂给后处理(暗角+去饱和)并驱动全屏眨眼；由 SprintFixedTick(0.1s) 调用。 */
	void UpdateGazePostProcess(float DeltaSeconds);

	// ===== 补给商店(波间花余烬采买) =====

	/** 本机：商店是否打开(HUD 读取绘制) */
	bool bShopOpen = false;

	/** 本波各商品已购次数(服务器权威，随波清空) */
	UPROPERTY(Replicated)
	TArray<uint8> ShopBought;

	/** 本波轮换货架抽中的商品索引(服务器每波重抽，复制给客户端画 UI) */
	UPROPERTY(Replicated)
	TArray<uint8> ShopRotating;

	/** 本波临时增益 */
	UPROPERTY(Replicated)
	float GazeRateMul = 1.0f;        // 信号抑制器

	UPROPERTY(Replicated)
	float CinderGainMul = 1.0f;      // 拾荒者协议

	UPROPERTY(Replicated)
	float WeaponDamageMul = 1.0f;    // 枪械超频

	// 创意收藏品的本波状态位
	UPROPERTY(Replicated)
	bool bCoolantActive = false;     // 过载冷却剂

	UPROPERTY(Replicated)
	bool bRevealLoot = false;        // 观测图/诱饵信标：全图揭示战利品

	UPROPERTY(Replicated)
	bool bPhaseNetActive = false;    // 相位阻断网

	UPROPERTY(Replicated)
	bool bSentryActive = false;      // 哨戒炮台

	/** 道具栏：买下后不立即生效、要玩家自己挑时机用掉的东西(流明/锚向索/高爆雷) */
	UPROPERTY(Replicated)
	TArray<uint8> OwnedItems;

	const TArray<uint8>& GetOwnedItems() const { return OwnedItems; }

	/** 该商品是"待用道具"(进道具栏)还是"买了就生效"？ */
	static bool IsCarryItem(EBoBShopItem Item);

	/** Q 键：用掉道具栏里当前选中的那件 */
	void DoUseItem();

	UFUNCTION(Server, Reliable)
	void Server_UseItem(uint8 SlotIndex);

	/** 本机：道具栏里选中的位置(背包页点击可改) */
	int32 ItemSlot = 0;

	int32 GetShopBought(int32 Index) const { return ShopBought.IsValidIndex(Index) ? ShopBought[Index] : 0; }
	float GetCinderGainMul() const { return CinderGainMul; }
	float GetWeaponDamageMul() const { return WeaponDamageMul; }

	/** 商店当前展示的商品索引列表(常备 + 本波轮换) */
	TArray<uint8> GetShopVisibleItems() const;

	/** 波间开始时清空本波购买记录与临时增益 */
	void ResetShopForNewWave();

	UFUNCTION(Server, Reliable)
	void Server_BuyShopItem(uint8 ItemIndex);

	/** B 键：只负责开/关商店 */
	void DoToggleShop();
	/** 开/关商店并同步鼠标光标、清空选中态 */
	void SetShopOpen(bool bOpen);
	/** 数字键分流入口：商店开着走"选中查看详情"，否则走武器切换 */
	void ShopOrWeaponKey(int32 Index);
	/** Y 键：买下当前详情页的商品 */
	void DoShopConfirm();
	/** U 键：详情页退回货架页 */
	void DoShopBack();
	/** 商店开启时的鼠标左键：按悬停目标分流(选商品 / 买 / 再看看) */
	void DoShopClick();
	/** 鼠标点击选中(HUD 传入格索引) */
	void ShopSelectRow(int32 Row);

	/** 本机：当前查看详情的商品格(-1 = 停在货架页) */
	int32 ShopSelected = -1;

	/** 本机：鼠标悬停的货架格(HUD 每帧写入，点击时读取；-1 = 没悬停) */
	mutable int32 ShopHoverRow = -1;

	/** 本机：鼠标悬停的详情页按钮(0=买 1=再看看 -1=无) */
	mutable int32 ShopHoverBtn = -1;

private:
	/** 注视后处理的动态材质实例(仅本机创建一次) */
	UPROPERTY(Transient)
	class UMaterialInstanceDynamic* GazePPMID = nullptr;

	/** 眨眼节奏：距下次眨眼的秒数 / 本次眨眼剩余时长 */
	float BlinkTimer = 0.0f;
	float BlinkPhase = 0.0f;

public:

	/** 当前占用格数 */
	int32 CarriedSlotsUsed() const;

	/** 服务器：尝试放入背包（满则失败） */
	bool TryAddLoot(uint8 Kind);

	int32 GetBankedValue() const { return BankedValue; }
	uint8 GetShardFlags() const { return ShardFlags; }
	const TArray<uint8>& GetCarried() const { return Carried; }

	/** HUD：武器栏访问 */
	const TArray<AShooterWeapon*>& GetOwnedWeapons() const { return OwnedWeapons; }
	AShooterWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

	/** 拾取结果提示（仅发给本机玩家；bWeapon=获得新武器） */
	UFUNCTION(Client, Reliable)
	void Client_NotifyLoot(bool bOk, uint8 Kind, bool bWeapon);

	/** HUD 读取拾取提示 */
	FString LootToastMsg;
	float LootToastUntil = 0.0f;

protected:

	/** 服务器 0.5s 轮询：背着宝物走近核心 → 自动存放 */
	void DepositTick();

	FTimerHandle DepositTimer;

	/** 数字键 1/2/3 直切武器 */
	// 数字键：商店开着时买第 N 件，否则切第 N 把武器（同键不冲突）
	void SelectWeapon1() { ShopOrWeaponKey(0); }
	void SelectWeapon2() { ShopOrWeaponKey(1); }
	void SelectWeapon3() { ShopOrWeaponKey(2); }
	void SelectWeapon4() { ShopOrWeaponKey(3); }
	void SelectWeapon5() { ShopOrWeaponKey(4); }
	void SelectWeapon6() { ShopOrWeaponKey(5); }
	void SelectWeapon7() { ShopOrWeaponKey(6); }
	void SelectWeapon8() { ShopOrWeaponKey(7); }
	void SelectWeaponIndex(int32 Index);

	/** Tab：背包页开关 */
	void DoToggleBackpack();

	/** F：拾取最近的战利品；G：丢弃背包最后一件 */
	void DoPickup();
	void DoDropLoot();

	UFUNCTION(Server, Reliable)
	void Server_TryPickupNearest();

	UFUNCTION(Server, Reliable)
	void Server_DropLastLoot();

	/** H：丢弃当前武器（至少保留一把） */
	void DoDropWeapon();

	UFUNCTION(Server, Reliable)
	void Server_DropWeapon();

public:

protected:

public:

	/** Bullet count updated delegate */
	FBulletCountUpdatedDelegate OnBulletCountUpdated;

	/** Damaged delegate */
	FDamagedDelegate OnDamaged;

public:

	/** Constructor */
	AShooterCharacter();

protected:

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Gameplay cleanup */
	virtual void EndPlay(EEndPlayReason::Type EndPlayReason) override;

	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	/** 复制属性（连击状态） */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

public:

	/** Handle incoming damage */
	virtual float TakeDamage(float Damage, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

public:

	/** Handles aim inputs from either controls or UI interfaces */
	virtual void DoAim(float Yaw, float Pitch) override;

	/** Handles move inputs from either controls or UI interfaces */
	virtual void DoMove(float Right, float Forward)  override;

	/** Handles jump start inputs from either controls or UI interfaces */
	virtual void DoJumpStart()  override;

	/** Handles jump end inputs from either controls or UI interfaces */
	virtual void DoJumpEnd()  override;

	/** Handles start firing input */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoStartFiring();

	/** Handles stop firing input */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoStopFiring();

	/** 客户端权威命中上报：客户端子弹在本地判定命中后，请求服务器对该目标结算伤害（击杀归属本玩家、正确计分） */
	UFUNCTION(Server, Reliable)
	void Server_ReportHit(AActor* HitActor, float Damage);

	/** Handles switch weapon input */
	UFUNCTION(BlueprintCallable, Category="Input")
	void DoSwitchWeapon();

public:

	//~Begin IShooterWeaponHolder interface

	/** Attaches a weapon's meshes to the owner */
	virtual void AttachWeaponMeshes(AShooterWeapon* Weapon) override;

	/** Plays the firing montage for the weapon */
	virtual void PlayFiringMontage(UAnimMontage* Montage) override;

	/** Applies weapon recoil to the owner */
	virtual void AddWeaponRecoil(float Recoil) override;

	/** Updates the weapon's HUD with the current ammo count */
	virtual void UpdateWeaponHUD(int32 CurrentAmmo, int32 MagazineSize) override;

	/** Calculates and returns the aim location for the weapon */
	virtual FVector GetWeaponTargetLocation() override;

	/** Gives a weapon of this class to the owner */
	UFUNCTION(BlueprintCallable, Category="Weapons")
	virtual void AddWeaponClass(const TSubclassOf<AShooterWeapon>& WeaponClass) override;

	/** Activates the passed weapon */
	virtual void OnWeaponActivated(AShooterWeapon* Weapon) override;

	/** Deactivates the passed weapon */
	virtual void OnWeaponDeactivated(AShooterWeapon* Weapon) override;

	/** Notifies the owner that the weapon cooldown has expired and it's ready to shoot again */
	virtual void OnSemiWeaponRefire() override;

	//~End IShooterWeaponHolder interface

protected:

	/** Returns true if the character already owns a weapon of the given class */
	AShooterWeapon* FindWeaponOfType(TSubclassOf<AShooterWeapon> WeaponClass) const;

	/** Called when this character's HP is depleted */
	void Die();

	/** Called to allow Blueprint code to react to this character's death */
	UFUNCTION(BlueprintImplementableEvent, Category="Shooter", meta = (DisplayName = "On Death"))
	void BP_OnDeath();

	/** Called from the respawn timer to destroy this character and force the PC to respawn */
	void OnRespawn();

public:

	/** Returns true if the character is dead */
	bool IsDead() const;

	//~ 增益效果接口（供 UpgradeComponent 调用）

	/** 提升最大生命并立即回满 */
	UFUNCTION(BlueprintCallable, Category="BOD|Upgrade")
	void GrantMaxHealthBonus(float BonusMax);

	/** 回复生命（用于吸血等） */
	UFUNCTION(BlueprintCallable, Category="BOD|Upgrade")
	void HealPlayer(float Amount);

	/** 增加伤害减免百分比（0~0.8） */
	UFUNCTION(BlueprintCallable, Category="BOD|Upgrade")
	void AddDamageReduction(float Pct);

	//~ 连击系统

	/** 击杀时累加连击 */
	UFUNCTION(BlueprintCallable, Category="BOD|Combo")
	void AddCombo();

	/** 当前连击伤害倍率（1.0~3.0） */
	UFUNCTION(BlueprintPure, Category="BOD|Combo")
	float GetComboMultiplier() const { return ComboMultiplier; }

	/** 当前连击数 */
	UFUNCTION(BlueprintPure, Category="BOD|Combo")
	int32 GetComboCount() const { return ComboCount; }

protected:

	/** 累计伤害减免百分比 */
	float DamageReductionPct = 0.0f;

	/** 连击数与倍率（服务器权威更新，复制到客户端供 HUD 显示连击状态） */
	UPROPERTY(Replicated)
	int32 ComboCount = 0;

	UPROPERTY(Replicated)
	float ComboMultiplier = 1.0f;

	FTimerHandle ComboResetTimer;

	/** 断击重置连击 */
	void ResetCombo();
};
