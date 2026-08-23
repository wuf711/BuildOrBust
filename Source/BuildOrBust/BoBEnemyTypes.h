// Build or Bust — 敌人型号表。21 个型号全部走这一张表，变种不建新类。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "UObject/StrongObjectPtr.h"
#include "BoBEnemyTypes.generated.h"

class USkeletalMesh;
class UAnimSequence;
class USoundBase;

/** 派系。决定晶体生长母题、音效母题、出场潮次。 */
UENUM(BlueprintType)
enum class EBoBFaction : uint8
{
	Accept		UMETA(DisplayName = "接受派"),
	Resist		UMETA(DisplayName = "抵抗派"),
	Explorer	UMETA(DisplayName = "外来者"),
	Terminal	UMETA(DisplayName = "终端"),
};

/** 同化程度。 */
UENUM(BlueprintType)
enum class EBoBTier : uint8
{
	Drift		UMETA(DisplayName = "游离"),
	Symbiote	UMETA(DisplayName = "共生"),
	Ascend		UMETA(DisplayName = "升华"),
};

/** 战术定位。Director 按这个配比控制刷怪。 */
UENUM(BlueprintType)
enum class EBoBRole : uint8
{
	Regular		UMETA(DisplayName = "常规"),
	Interactive	UMETA(DisplayName = "交互"),
	Elite		UMETA(DisplayName = "精英"),
	Boss		UMETA(DisplayName = "首领"),
};

/** 凝视钩触发档位：玩家的偏移/失谐越过这档，本型号的行为参数才改变。 */
UENUM(BlueprintType)
enum class EBoBGazeBand : uint8
{
	None		UMETA(DisplayName = "无"),
	Offset40	UMETA(DisplayName = "偏移≥40"),
	Dissonance70 UMETA(DisplayName = "失谐≥70"),
	Dissonance80 UMETA(DisplayName = "失谐≥80"),
};

/**
 *  一个敌人型号。
 *
 *  变种（双冠朝圣者、破甲殉道者那类）不新建类也不新建 mesh：
 *  VariantOf 指回基础型号，靠 MeshScale ／ HiddenBones ／ 材质参数区分。
 *  规格书 10.2：变种全部走数据表 + Socket/Hide Bone。
 */
USTRUCT(BlueprintType)
struct FBoBEnemyRow : public FTableRowBase
{
	GENERATED_BODY()

	// —— 身份 ——

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|身份")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|身份")
	EBoBFaction Faction = EBoBFaction::Accept;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|身份")
	EBoBTier Tier = EBoBTier::Drift;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|身份")
	EBoBRole Role = EBoBRole::Regular;

	/** 动词。规格书给每个型号一个字：朝／拥／播／撞／修／泄／扑／走火／诳／仿 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|身份")
	FText Verb;

	// —— 属性 ——

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|属性")
	float HP = 100.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|属性")
	float MoveSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|属性")
	float Damage = 12.f;

	/** 正面减伤。重甲类专用，0 表示无 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|属性")
	float FrontalDR = 0.f;

	/** 弱点骨骼。命中它按倍率结算；空表示无弱点（完美镜像） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|属性")
	FName WeakpointBone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|属性")
	float WeakpointMul = 2.5f;

	// —— Director ——

	/** 预算消耗。Director 的 B(n) 就是拿这个数往外扣 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|Director")
	int32 SpawnCost = 10;

	/** 第几潮起可能出现 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|Director")
	int32 WaveUnlock = 1;

	/**
	 *  威胁档 1-4。控制它在一潮**之内**什么时候才允许出场。
	 *
	 *  为什么不能拿 SpawnCost 当威胁度：cost 是投放成本，不是难度。
	 *  朝圣晶簇 cost 10、滤网背负者 cost 26，但背负者未必比一群朝圣者难打。
	 *  拿 cost 排序的结果是开场就冒出背负者、末段反而回落到朝圣者（实测过）。
	 *  L4D 的做法也是把"投什么"和"投多少"拆成两条独立的轴。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ThreatTier = 1;

	/** 抽取权重。稀有变种给低值 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|Director")
	float PickWeight = 1.f;

	// —— 凝视钩（第九部）——

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|凝视钩")
	EBoBGazeBand GazeHookBand = EBoBGazeBand::None;

	/** 越档后的参数变化量，含义由各型号自己解释（倍率／米／秒） */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|凝视钩")
	float GazeHookParam = 0.f;

	// —— 变种 ——

	/** 指回基础型号；为空表示自己就是基础型号 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|变种")
	FName VariantOf;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|变种")
	float MeshScale = 1.f;

	/** 要隐藏的骨骼。断冠者靠它砍掉半边花冠，空手拾荒者靠它藏掉配件 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|变种")
	TArray<FName> HiddenBones;

	/** 材质参数覆盖：晶体覆盖率、发光强度这类。键名对应材质里的标量参数 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|变种")
	TMap<FName, float> MaterialScalars;

	/**
	 *  常驻姿态覆盖：骨骼名 -> 欧拉角（度，XYZ）。AnimBP 把它作为基础姿势叠加。
	 *
	 *  变种共用一个 mesh，骨架摆位没法改，只能靠姿态拉开体态差异——
	 *  督战者蹲得更低、背负者被压得更弯、双持斥候前倾。
	 *  光靠增删部件，同一具骨架出来的几个变种站姿是一样的。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|变种")
	TMap<FName, FVector> PoseOverride;

	// —— 资产 ——

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|资产")
	TSoftObjectPtr<USkeletalMesh> Mesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|资产")
	TSoftObjectPtr<UAnimSequence> AnimMove;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|资产")
	TSoftObjectPtr<UAnimSequence> AnimAttack;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|资产")
	TSoftObjectPtr<UAnimSequence> AnimDeath;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|音效")
	TSoftObjectPtr<USoundBase> WarnSFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|音效")
	TSoftObjectPtr<USoundBase> LoopSFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|音效")
	TSoftObjectPtr<USoundBase> DeathSFX;

	// —— 策划备注 ——

	/**
	 *  它在质检玩家的什么能力。不进游戏，只给设计侧看。
	 *  反过来指导投送端的商品设计：每件商品都该补某项质检的短板。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BoB|备注")
	FString BuildCheckNote;
};

/**
 *  型号表查询。表在 /Game/BoB/Data/DT_BoBEnemy，懒加载。
 */
UCLASS()
class BUILDORBUST_API UBoBEnemyLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 取一行；查不到返回 false */
	UFUNCTION(BlueprintCallable, Category = "BoB|Enemy")
	static bool GetEnemyRow(FName RowName, FBoBEnemyRow& OutRow);

	/** 全部型号 ID */
	UFUNCTION(BlueprintCallable, Category = "BoB|Enemy")
	static TArray<FName> AllEnemyIds();

	/**
	 *  按潮次和预算筛出可投放的型号。
	 *  Director 拿这个结果再按 PickWeight 抽签。
	 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Enemy")
	static TArray<FName> AffordableAt(int32 WaveIndex, int32 BudgetLeft);

	/** 变种取基础型号的资产：Mesh 和动画都从 VariantOf 那行拿 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Enemy")
	static bool ResolveAssets(const FBoBEnemyRow& Row, FBoBEnemyRow& OutResolved);

	/** 供测试/工具注入，绕过懒加载 */
	static void SetEnemyTable(UDataTable* InTable);

private:
	static const FBoBEnemyRow* Find(FName RowName);
	static UDataTable* Table();

	/**
	 *  必须是强引用。没有任何 UObject 持有这张表，用弱指针的话
	 *  GC 会在开局约一分钟后把它回收掉，之后 Table() 一路返回空，
	 *  Director 选不出任何型号——整套敌人系统静默死亡且不报错。
	 */
	static TStrongObjectPtr<UDataTable> EnemyTable;
};
