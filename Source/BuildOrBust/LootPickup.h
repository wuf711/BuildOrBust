#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "LootPickup.generated.h"

class UStaticMeshComponent;
class USphereComponent;

/** 战利品种类（名称/占格/分值见 GetLootDef） */
UENUM(BlueprintType)
enum class ELootKind : uint8
{
	Ore,           // 荧光矿簇：1格 60 分，可重复+每波重刷
	Canned,        // 战前罐头：1格 80
	CoinBag,       // 旧币袋：1格 95
	Tape,          // 加密磁带：1格 110
	Filter,        // 净水滤芯：2格 150
	MedBox,        // 医疗冷藏箱：2格 170
	Crystal,       // 谐振水晶：2格 190（唯一）
	ShardA,        // 信标残片·甲：3格 260（唯一，成对）
	ShardB,        // 信标残片·乙：3格 260（唯一，成对；单人集齐存放 +400）
	RelicSingle,   // 方舟核心样本：3格 330（全图唯一最高单件）
	WeaponMod,     // 武器改装件：不占格，随机攻击词条+补满弹药，每波重刷
};

struct FLootDef
{
	const TCHAR* Name;
	int32 Slots;
	int32 Value;
	bool bRespawns;
	/** 详情文案：拾取 toast / 背包详情展示；用户定稿写入 */
	const TCHAR* Flavor;
};

/** 静态查表 */
BUILDORBUST_API const FLootDef& GetLootDef(ELootKind Kind);

/**
 *  可拾取战利品（listen-server 复制）：
 *  走进触发球自动拾取；宝物进背包，改装件立即生效。
 *  bTaken 复制隐藏；矿簇/改装件每波由 WaveManager 重刷。
 */
UCLASS()
class BUILDORBUST_API ALootPickup : public AActor
{
	GENERATED_BODY()

public:
	ALootPickup();

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Loot")
	ELootKind Kind = ELootKind::Ore;

	UPROPERTY(VisibleAnywhere, Category="Loot")
	UStaticMeshComponent* Mesh;

	UPROPERTY(VisibleAnywhere, Category="Loot")
	USphereComponent* Trigger;

	/** 已被拾取（复制隐藏，等待重刷或永久消失） */
	UPROPERTY(ReplicatedUsing=OnRep_Taken)
	bool bTaken = false;

	/** 玩家丢弃的武器：拾取时直接给这把枪（仅 WeaponMod 类型使用） */
	UPROPERTY(Replicated)
	TSubclassOf<class AShooterWeapon> WeaponClassOverride;

	/** 服务器：设置拾取状态（隐藏/恢复） */
	void SetTaken(bool bNewTaken);

	/** 服务器：把本拾取物给予角色（武器缓存/宝物通用；含提示），成功返回 true */
	bool TryGiveTo(class AShooterCharacter* Char);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_Taken();

	UFUNCTION()
	void OnTriggerOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	void ApplyVisibility();
};
