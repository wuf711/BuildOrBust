#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "WaveManager.generated.h"

class AShooterNPC;

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
	int32 MaxWave = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	float WaveInterval = 8.0f;

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

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// 当前波次（复制给客户端，用于 HUD 显示）
	UPROPERTY(Replicated)
	int32 CurrentWave = 0;

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
};
