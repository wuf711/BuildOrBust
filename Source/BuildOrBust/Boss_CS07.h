// Build or Bust — CS-07 模因终端（TIDE 10）。规格书第六部。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoBEnergyPillar.h"
#include "Boss_CS07.generated.h"

class USkeletalMeshComponent;
class UAnimSequence;
class UCapsuleComponent;

/** 三阶段。抗性 100→66 权限剥夺，66→33 对称性破缺，33→0 静默走廊 */
UENUM(BlueprintType)
enum class EBoBBossPhase : uint8
{
	Authority   UMETA(DisplayName = "权限剥夺"),
	Symmetry    UMETA(DisplayName = "对称性破缺"),
	Corridor    UMETA(DisplayName = "静默走廊"),
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBossPhaseChanged, EBoBBossPhase, NewPhase);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossExtractionReady);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWrongPillar);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBossDefeated);

/**
 *  CS-07 模因终端。
 *
 *  契约 C7：**玩家永远打不死它**。所以这里没有 HP、没有死亡、没有击杀奖励。
 *  打它只做一件事——烧抗性槽，而抗性槽只用来提前撤离倒计时。
 *  倒计时归零就开回收通道，那一刻 Boss 仍在场、仍在活动。
 *
 *      全程不打 → 90 秒，能过关但灰头土脸
 *      全程打对 → 三节点烧完各减 15 秒，提前 45 秒
 *
 *  已实现：抗性槽 / 三阶段切换 / 三次结构崩塌 / 阶段二折射盾 / 撤离倒计时。
 *  未实现（需要关卡内容，各自是独立交付物）：阶段一弹幕与无效遗构搬运、
 *  阶段二双人非对称能量柱、阶段三静默走廊。这些在下面留了明确的接入点，
 *  没有假装已经生效。
 */
UCLASS()
class BUILDORBUST_API ABoss_CS07 : public AActor
{
	GENERATED_BODY()

public:
	ABoss_CS07();

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

	/** 开打。由 WaveManager 在 TIDE 10 调用 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Boss")
	void Activate();

	/** 抗性槽 0..1。1 = 完好 */
	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	float GetResistance() const { return Resistance; }

	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	EBoBBossPhase GetPhase() const { return Phase; }

	/** 回收倒计时剩余秒数。归零即开通道 */
	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	float GetExtractionRemaining() const { return ExtractionRemain; }

	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	bool IsShielded() const { return Phase == EBoBBossPhase::Symmetry && bShieldUp; }

	/**
	 *  阶段一：把一件无效遗构搬到侧面端口录入，烧掉 15% 抗性。
	 *  规格书的设计意图是让玩家意识到——平时录入的动作是在给这东西 debug。
	 *  搬运与端口交互属于关卡内容，这里只提供结算入口。
	 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Boss")
	void RegisterFalseRelic();

	/** 阶段二：打对一根能量柱，烧抗性 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Boss")
	void BreakPillar();

	/** 柱子被击中后统一收口：对了烧抗性并开下一轮，错了广播增援请求 */
	void ResolvePillarHit(bool bCorrect);

	/** 本轮弱点的外壳形状（玩家 A 的那一半信息） */
	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	EBoBPillarShape GetTargetShape() const { return TargetShape; }

	/** 本轮弱点的晶体纹路（玩家 B 的那一半信息） */
	UFUNCTION(BlueprintPure, Category = "BoB|Boss")
	EBoBPillarVein GetTargetVein() const { return TargetVein; }

	/** 打错柱子：由 WaveManager 接住去引一波朝圣晶簇 */
	UPROPERTY(BlueprintAssignable, Category = "BoB|Boss")
	FOnWrongPillar OnWrongPillar;

	UPROPERTY(BlueprintAssignable, Category = "BoB|Boss")
	FOnBossPhaseChanged OnPhaseChanged;

	/**
	 *  抗性烧光＝击败（结局二）。
	 *
	 *  这条推翻了原规格书的契约 C7「玩家永远打不死 CS-07」——用户 2026-08-10 定的，
	 *  后续在勘探阶段加隐藏关键道具来解锁这个结局。6.3 那段
	 *  「破坏是真的，击败是假的」的叙事要跟着一起重写。
	 */
	UPROPERTY(BlueprintAssignable, Category = "BoB|Boss")
	FOnBossDefeated OnBossDefeated;

	/** 倒计时归零：开回收通道 */
	UPROPERTY(BlueprintAssignable, Category = "BoB|Boss")
	FOnBossExtractionReady OnExtractionReady;

	// —— 配置，数值全部来自规格书第六部 ——

	/** 不打也能过关的基础倒计时 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	float ExtractionSeconds = 300.f;   // 5 分钟。90 秒太短，玩家还没进入状态就结束了

	/** 每烧完一个节点减多少秒。三个节点共 45 秒 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	float NodeTimeCut = 15.f;

	/** 烧空整条抗性槽所需的总伤害。调它就是调"打 Boss 值不值" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	float ResistancePool = 24000.f;

	/** 一件无效遗构烧掉的抗性比例 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	float RelicBurn = 0.15f;

	/** 阶段一撒几件无效遗构 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	int32 FalseRelicCount = 3;

	// —— 阶段三 静默走廊（规格书 6.4）——

	/** 基础移速倍率。武器锁死＝卸掉负重 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss|走廊")
	float CorridorSpeedMul = 1.25f;

	/** 裸奔时每秒扣最大 HP 的比例 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss|走廊")
	float FieldDpsBare = 0.02f;

	/** 在谐振灯范围内每秒扣最大 HP 的比例 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss|走廊")
	float FieldDpsLit = 0.01f;

	/** 力场扣到这个血量比例就停手。隐藏机制，不要在 UI 上告诉玩家 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss|走廊")
	float CorridorHpFloor = 0.15f;

	/** 谐振灯的有效半径 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss|走廊")
	float LampRadius = 400.f;

	/** 能量柱离塔碑多远 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	float PillarRingRadius = 1500.f;

	/** 无效遗构离塔碑多远 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	float RelicRingRadius = 2200.f;

	/** 一根能量柱烧掉的抗性比例 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	float PillarBurn = 0.11f;

	/**
	 *  阶段二折射盾把普通伤害反弹回开火者的比例。
	 *  规格书写的是 100% 反弹，但满额反弹足以秒掉玩家，
	 *  正式调平衡时大概率要往下压——所以留成可调项而不是写死。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Boss")
	float ShieldReflectRatio = 1.0f;

protected:
	virtual void BeginPlay() override;

	/** 抗性跨过阈值时推进阶段，并播对应的结构崩塌 */
	void BurnResistance(float Delta);
	void EnterPhase(EBoBBossPhase NewPhase);

	/** 阶段一布场：3 件无效遗构 + 侧面接收端口 */
	void SpawnPhaseOneProps();

	/** 阶段二：起一轮四根能量柱 */
	void StartPillarRound();

	/** 阶段三：开/关走廊移速加成，关的时候要把原速还回去 */
	void SetCorridorMovement(bool bOn);

	/** 阶段三每拍：失谐力场扣血，带 15% 血量下限保护 */
	void TickCorridor(float DeltaSeconds);
	void PlayClip(UAnimSequence* Clip, bool bLoop);

	UPROPERTY(VisibleAnywhere, Category = "BoB|Boss")
	TObjectPtr<USkeletalMeshComponent> Tower;

	UPROPERTY(VisibleAnywhere, Category = "BoB|Boss")
	TObjectPtr<UCapsuleComponent> HitVolume;

	// —— 五段动画。Port_Retract 当前没导入，状态机也用不到 ——
	UPROPERTY(EditAnywhere, Category = "BoB|Boss|Anim")
	TObjectPtr<UAnimSequence> IdlePulse;

	UPROPERTY(EditAnywhere, Category = "BoB|Boss|Anim")
	TObjectPtr<UAnimSequence> BreakMid;    // 100 → 66：中段崩裂，露青铜刻纹

	UPROPERTY(EditAnywhere, Category = "BoB|Boss|Anim")
	TObjectPtr<UAnimSequence> BreakTop;    // 66 → 33：上段崩落，接缝喷火花

	UPROPERTY(EditAnywhere, Category = "BoB|Boss|Anim")
	TObjectPtr<UAnimSequence> BreakBase;   // 33 → 0：基座崩尽，塔碑下沉

	UPROPERTY(EditAnywhere, Category = "BoB|Boss|Anim")
	TObjectPtr<UAnimSequence> ShieldOpen;  // 阶段二升盾

private:
	UPROPERTY(Replicated) float Resistance = 1.f;
	UPROPERTY(Replicated) float ExtractionRemain = 90.f;
	UPROPERTY(Replicated) EBoBBossPhase Phase = EBoBBossPhase::Authority;
	UPROPERTY(Replicated) bool bActive = false;
	UPROPERTY(Replicated) bool bShieldUp = false;

	/** 已经兑现过减时的节点数，防止同一个阈值反复扣时间 */
	int32 NodesCleared = 0;
	bool bExtractionFired = false;
	bool bPropsPlaced = false;

	/** 进走廊前各玩家的原始移速，退出时还回去 */
	TMap<TObjectPtr<class AShooterCharacter>, float> CorridorBaseSpeed;

	UPROPERTY(Replicated) EBoBPillarShape TargetShape = EBoBPillarShape::Triangle;
	UPROPERTY(Replicated) EBoBPillarVein TargetVein = EBoBPillarVein::Split;

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};
