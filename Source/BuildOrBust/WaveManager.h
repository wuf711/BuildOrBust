#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaveManager.generated.h"

class AShooterNPC;
class ABoBEnemy;
class ABoBAIDirector;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveStart, int32, WaveNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnWaveComplete, int32, WaveNumber);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAllWavesComplete);

USTRUCT(BlueprintType)
struct FWaveConfig
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 EnemyCount = 5;

	UPROPERTY(BlueprintReadOnly)
	float SpeedMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly)
	float HealthMultiplier = 1.0f;

	UPROPERTY(BlueprintReadOnly)
	int32 ExpPerKill = 5;
};

UCLASS()
class BUILDORBUST_API AWaveManager : public AActor
{
	GENERATED_BODY()

public:
	AWaveManager();

	UPROPERTY(BlueprintAssignable)
	FOnWaveStart OnWaveStart;

	UPROPERTY(BlueprintAssignable)
	FOnWaveComplete OnWaveComplete;

	UPROPERTY(BlueprintAssignable)
	FOnAllWavesComplete OnAllWavesComplete;

	UFUNCTION(BlueprintCallable)
	void StartNextWave();

	UFUNCTION(BlueprintPure)
	int32 GetCurrentWave() const { return CurrentWave; }

	// 直接统计全世界存活敌人（真实数量）
	UFUNCTION(BlueprintPure)
	int32 GetAliveEnemyCount() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	int32 MaxWave = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	float WaveInterval = 120.0f;   // 潜影窗口基础时长（第 1 波后）

	// ===== 潜影(Shadow Cruise)：波间探索阶段。行动力随波次成长，越后期越敢深入 =====

	/** 每满 3 波，潜影时限增加的秒数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave|潜影")
	float ShadowCruiseGrowth = 30.0f;

	/** 潜影时限上限 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave|潜影")
	float ShadowCruiseMax = 260.0f;

	/** 入场提示时长(不占探索时间；HUD 据此把倒计时从完整时限开始显示) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave|潜影")
	float ShadowCruiseIntro = 7.0f;

	/** 补给阶段时长：清波后先给一段安全采买时间，再进潜影 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave|补给")
	float SupplyPhaseDuration = 45.0f;

	/** 波间当前处于哪个子阶段：0=补给 1=潜影入场提示 2=潜影探索 (-1=不在波间) */
	UFUNCTION(BlueprintPure)
	int32 GetIntervalPhase() const
	{
		const float R = GetIntervalRemaining();
		if (R < 0.0f) { return -1; }
		const float Cruise = GetShadowCruiseDuration();
		if (R > Cruise + ShadowCruiseIntro) { return 0; }
		if (R > Cruise) { return 1; }
		return 2;
	}

	/** 补给阶段剩余秒数(不在补给阶段返回 -1) */
	UFUNCTION(BlueprintPure)
	float GetSupplyRemaining() const
	{
		const float R = GetIntervalRemaining();
		const float Cruise = GetShadowCruiseDuration();
		return (R > Cruise + ShadowCruiseIntro) ? (R - Cruise - ShadowCruiseIntro) : -1.0f;
	}

	/** 商店「行动时限延长」：给本轮潜影加时(服务器) */
	void ExtendShadowCruise(float Seconds);

	/** 本波潜影时限 = 基础 120s + 每三波 +30s，钳到上限 */
	UFUNCTION(BlueprintPure)
	float GetShadowCruiseDuration() const
	{
		const int32 Tiers = FMath::Max(0, CurrentWave) / 3;   // 每满 3 波涨一档
		return FMath::Min(WaveInterval + ShadowCruiseGrowth * Tiers, ShadowCruiseMax);
	}

	// ===== 波次类型：3/6/9 精英战，第 10 波(末波) Boss 战 =====

	/** 该波是否为精英战 */
	UFUNCTION(BlueprintPure)
	static bool IsEliteWave(int32 Wave) { return Wave > 0 && Wave % 3 == 0 && Wave < 10; }

	/** 该波是否为 Boss 战(末波) */
	UFUNCTION(BlueprintPure)
	bool IsBossWave(int32 Wave) const { return Wave >= MaxWave; }

	/** 当前波类型：0=普通 1=精英 2=Boss */
	UFUNCTION(BlueprintPure)
	int32 GetWaveKind(int32 Wave) const
	{
		if (IsBossWave(Wave)) { return 2; }
		if (IsEliteWave(Wave)) { return 1; }
		return 0;
	}

	/** 波间倒计时剩余秒数；<0 = 当前不在波间 */
	UFUNCTION(BlueprintPure)
	float GetIntervalRemaining() const;

	/** 是否仍在等待所有玩家就绪（开局 R 确认） */
	UFUNCTION(BlueprintPure)
	bool IsWaitingForReady() const { return bWaitingForReady; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	float InitialDelay = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	float MonitorInterval = 1.0f;

	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Wave")
	TArray<AActor*> SpawnPoints;

	// 单一敌人类（留作兜底）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	TSubclassOf<AShooterNPC> EnemyClass;

	// 敌人种类池：每次生成随机抽一个。重复添加同一种可提高其出现频率
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	TArray<TSubclassOf<AShooterNPC>> EnemyTypes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	int32 BaseExpPerKill = 5;

	// ===== AI Director：接管投放节奏 =====

	/**
	 *  关卡里的 Director。留空则 BeginPlay 时自己找一个。
	 *  找得到就走预算积分制投放，找不到就退回开波一次性铺满的老路——
	 *  没有 Director 的关卡不该因此开不了波。
	 */
	UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Wave|Director")
	TObjectPtr<ABoBAIDirector> Director;

	/** 关掉就强制走老的一次性铺满，用于对照测试 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave|Director")
	bool bUseDirector = true;

	UFUNCTION(BlueprintPure, Category="Wave|Director")
	bool IsDirectorDriven() const { return bUseDirector && Director != nullptr; }

	/** 调试：直接跳到第 N 潮（BoB.Wave）。不走波间流程，立刻开打 */
	void DebugJumpToWave(int32 N);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// 当前波次（复制给客户端，用于 HUD 显示）
	UPROPERTY(Replicated)
	int32 CurrentWave = 0;

	// 波间倒计时结束时刻（服务器世界秒，复制；-1=非波间）
	UPROPERTY(Replicated)
	float IntervalEndServerTime = -1.0f;

	// 等待全体玩家就绪（开局），复制供 HUD 提示
	UPROPERTY(Replicated)
	bool bWaitingForReady = true;

	// 就绪轮询定时器
	FTimerHandle ReadyPollTimer;

	// 轮询：全体玩家就绪后开局
	void CheckReadyToStart();

	// 本波是否进行中（避免波间空档误判清空）
	bool bWaveActive = false;

	// 存活监控定时器（循环）
	FTimerHandle MonitorTimer;

	FWaveConfig BuildWaveConfig(int32 WaveNumber) const;

	// 生成敌人，返回实际成功生成数
	int32 SpawnEnemies(const FWaveConfig& Config);

	// 间隔结束刷下一波
	void OnWaveIntervalEnd();

	// 定时轮询：数全世界存活敌人，判断本波是否清空
	void MonitorWave();

	// 本波清空处理
	void OnWaveCleared();

	// 波次清空后给所有在场玩家各发一次三选一增益（仅在各自客户端弹卡）
	void OfferUpgradesToAllPlayers();

	// 给击杀者发经验+连击+得分（死亡委托绑定，ScoreValue=该丧尸基础分值）
	UFUNCTION()
	void HandleEnemyDeath(AController* Killer, int32 ScoreValue);

	// Director 投放出一个敌人：把计分回调挂上去，其余由 Director 配好
	UFUNCTION()
	void HandleDirectorSpawn(FName EnemyId, ABoBEnemy* Enemy);

	// TIDE 10 放 CS-07，并把它的回收倒计时接到通关流程上
	void SpawnBoss();

	// 倒计时归零：开回收通道（结局一 · 坚守成功）
	UFUNCTION()
	void HandleBossExtractionReady();

	// 抗性烧尽：击败 CS-07（结局二）
	UFUNCTION()
	void HandleBossDefeated();

	/**
	 *  收尾统一走这里：强制清场 + 记结局 + 暂停。
	 *  两个结局的差别只在文案和评价，流程完全一样，分开写迟早对不上。
	 */
	void FinishRun(bool bBossDefeated);

	/** 本局结局：0=未结束 1=坚守成功 2=击败 CS-07 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Wave", meta=(AllowPrivateAccess="true"))
	int32 RunEnding = 0;

public:
	UFUNCTION(BlueprintPure) int32 GetRunEnding() const { return RunEnding; }
private:

	// Boss 阶段二打错柱子：引一波朝圣晶簇
	UFUNCTION()
	void HandleWrongPillar();

	/**
	 *  每潮的战斗记录。用来回答"难度到底高在哪"——
	 *  之前只能凭"感觉核心掉得快"去调数值，调完还是凭感觉，来回摆。
	 *  记下来之后至少能看出是某一潮突然陡increase、还是全程线性偏高。
	 */
	struct FWaveStat
	{
		int32 Wave = 0;
		float CoreAtStart = 0.f;
		float CoreAtEnd = 0.f;
		int32 Spawned = 0;
		float Seconds = 0.f;
	};
	TArray<FWaveStat> WaveStats;
	float WaveStartTime = 0.f;

	/** 开潮记一次基线 */
	void BeginWaveStat();

	/** 清潮结算并打一行；同时把整局的曲线重打一遍 */
	void EndWaveStat();

	// 失谐触顶巡检：谁到 100 就给谁派一个处决者。
	// 放在 WaveManager 而不是 Director，是因为失谐更常在险区勘探（波间）触顶，
	// 而 Director 只在潮次进行中 tick
	void CheckExecutioner();

	FTimerHandle ExecutionerTimer;

	/** 打错一根柱子引几只朝圣晶簇 */
	UPROPERTY(EditAnywhere, Category="Wave|Boss")
	int32 WrongPillarPenalty = 4;

	UPROPERTY()
	TObjectPtr<class ABoss_CS07> Boss;
};
