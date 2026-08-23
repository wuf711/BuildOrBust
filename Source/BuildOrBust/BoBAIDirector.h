// Build or Bust — AI Director。预算积分制，不是固定权重匀速滴漏。

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BoBEnemyTypes.h"
#include "BoBAIDirector.generated.h"

class ABoBEnemy;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDirectorSpawn,
	FName, EnemyId, ABoBEnemy*, Enemy);

/** 一次投放决策的完整记录，调试和回放都靠它。 */
USTRUCT(BlueprintType)
struct FBoBSpawnRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly) FName EnemyId;
	UPROPERTY(BlueprintReadOnly) int32 Cost = 0;
	UPROPERTY(BlueprintReadOnly) float AtProgress = 0.f;
	UPROPERTY(BlueprintReadOnly) float Intensity = 0.f;
	/** 因为场上交互体已满两种而被降级成常规靶 */
	UPROPERTY(BlueprintReadOnly) bool bDowngraded = false;
};

/**
 *  预算积分制的投放器（规格书第三部）。
 *
 *      潮次总预算   B(n) = B0 * (1 + k*n)
 *      瞬时速率     R(t) = B_remain * I(t) / T_remain
 *
 *  I(t) 是分段常数的强度包络，不用连续函数——波峰波谷要能一眼看懂、
 *  一眼调得动。其中 50~70% 那段 0.3 的喘息期是整套系统的价值所在：
 *  没有它，玩家从头到尾被匀速推着走，疲劳而没有记忆点。
 */
UCLASS()
class BUILDORBUST_API ABoBAIDirector : public AActor
{
	GENERATED_BODY()

public:
	ABoBAIDirector();

	virtual void Tick(float DeltaSeconds) override;

	/** 生成完成后广播。接住的人负责挂死亡回调之类的后续 */
	UPROPERTY(BlueprintAssignable)
	FOnDirectorSpawn OnDirectorSpawn;

	UFUNCTION(BlueprintCallable, Category = "BoB|Director")
	void BeginWave(int32 InWaveIndex);

	UFUNCTION(BlueprintCallable, Category = "BoB|Director")
	void EndWave();

	/**
	 *  临时增援。不走预算池，专供剧本事件（比如 Boss 阶段二打错柱子）。
	 *  仍然复用 FindSpawnPoint，所以"不能让玩家看见凭空出现"这条规矩照旧生效。
	 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Director")
	int32 RequestReinforcement(FName EnemyId, int32 Count);

	/** 还在投放中（预算没花完且时间没到） */
	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	bool IsWaveActive() const { return bWaveActive; }

	// —— 纯函数：不依赖世界状态，便于离线验算整条曲线 ——

	/** B(n) = B0 * (1 + k*n) */
	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	float WaveBudget(int32 InWaveIndex) const
	{
		return BaseBudget * (1.f + GrowthK * InWaveIndex);
	}

	/**
	 *  强度包络。progress 是潮次进度 0..1。
	 *  分段常数，四段：试探 / 正常 / 喘息 / 高潮。
	 */
	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	static float IntensityAt(float Progress);

	/**
	 *  压力阀。只有两条规则——L4D 那种情绪状态机的额外状态
	 *  只会让节奏难以调试，这两条已经覆盖绝大部分体感。
	 */
	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	static float PressureValve(float LowestHealthPct, float AverageHealthPct,
		int32 AliveEnemies);

	/**
	 *  一潮进度到某处，最高允许出到第几威胁档。
	 *  这是"一潮之内渐进加难"的闸门：开场只有 1 档杂兵，精英只在后段解禁。
	 */
	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	static int32 AllowedTierAt(float Progress);

	/** 常规靶 : 交互体 的投放概率，按潮次放水/加压 */
	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	static float InteractiveShare(int32 InWaveIndex);

	// —— 运行时读数 ——

	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	float GetBudgetRemain() const { return BudgetRemain; }

	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	float GetProgress() const;

	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	float GetCurrentRate() const;

	UFUNCTION(BlueprintPure, Category = "BoB|Director")
	const TArray<FBoBSpawnRecord>& GetLog() const { return SpawnLog; }

	// —— 配置 ——

	/**
	 *  实际生成的类。要在关卡里指到 ABoBEnemy 的蓝图子类上——
	 *  AI 控制器和 StateTree 挂在蓝图那一层，直接用 C++ 类会刷出没有 AI 的空壳。
	 *  没配的话 Director 不投放也不扣预算，只在日志里叫一次。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	TSubclassOf<ABoBEnemy> EnemyClass;

	/** 自己扫世界填存活数和玩家血量；关掉就得由外部每帧喂 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	bool bAutoTrackWorldState = true;

	/** B0 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	float BaseBudget = 90.f;

	/** k */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	float GrowthK = 0.34f;

	/** 一潮的计划时长（秒）。预算按它摊开 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	float WaveDuration = 100.f;

	/**
	 *  场上同时存在的战术交互体上限。
	 *  规格书：这条不可协商，超了玩家的反应带宽会被打满。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	int32 MaxInteractiveAlive = 2;

	/** 生成点距最近玩家的距离区间（厘米） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	float SpawnDistMin = 1500.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	float SpawnDistMax = 4500.f;

	/** 视锥判定的额外余量（度）。玩家余光扫到也算看见 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Director")
	float FovMargin = 10.f;

	/** 当前场上交互体数量。由外部在敌人生成/死亡时维护 */
	UPROPERTY(BlueprintReadWrite, Category = "BoB|Director")
	int32 AliveInteractive = 0;

	UPROPERTY(BlueprintReadWrite, Category = "BoB|Director")
	int32 AliveEnemies = 0;

	UPROPERTY(BlueprintReadWrite, Category = "BoB|Director")
	float LowestHealthPct = 1.f;

	UPROPERTY(BlueprintReadWrite, Category = "BoB|Director")
	float AverageHealthPct = 1.f;

protected:
	/** 从可负担的型号里按权重抽一个；返回是否成功 */
	bool PickEnemy(FName& OutId, FBoBEnemyRow& OutRow, bool& bOutDowngraded) const;

	/**
	 *  找一个玩家看不见的生成点。
	 *  全部不满足时返回 false —— 规格书要求宁可空 2 秒延迟生成，
	 *  也不能让玩家看见凭空出现，所以这里不做降级妥协。
	 */
	bool FindSpawnPoint(FVector& OutLocation) const;

	bool IsVisibleToAnyPlayer(const FVector& Where) const;

	/** 真正把敌人放出来并按型号配置好；失败返回 nullptr（调用方不扣预算） */
	ABoBEnemy* SpawnEnemy(FName Id, const FVector& Where);

	/** 扫一遍世界，刷新压力阀要用的四个读数 */
	void RefreshWorldState();

private:
	UPROPERTY() int32 WaveIndex = 0;
	UPROPERTY() float BudgetTotal = 0.f;
	UPROPERTY() float BudgetRemain = 0.f;
	UPROPERTY() float TimeRemain = 0.f;
	UPROPERTY() bool bWaveActive = false;

	/** 累积的"应投放预算"，攒够一个型号的 cost 才真的放 */
	float SpawnCredit = 0.f;

	/** 上一次没投放成功的原因，和下面的定时状态行一起打出来 */
	FString LastStall;
	float StatusTimer = 0.f;
	int32 NoPointStreak = 0;

	/**
	 *  已经选定、正在攒钱的下一个型号。
	 *
	 *  没有这一层的话，选型池是按"当前攒到多少"筛的——额度一跨过最便宜那档
	 *  就立刻花掉，永远攒不到贵的，结果整潮全是同一种杂兵。
	 *  先定目标再攒钱，贵型号才有出场机会。
	 */
	FName PendingId;
	FBoBEnemyRow PendingRow;
	bool bHasPending = false;

	UPROPERTY() TArray<FBoBSpawnRecord> SpawnLog;
};
