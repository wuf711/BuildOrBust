#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "UpgradeTypes.h"
#include "BODPlayerState.generated.h"

UCLASS()
class BUILDORBUST_API ABODPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable)
	void AddKill();

	UFUNCTION(BlueprintPure)
	int32 GetKills() const { return Kills; }

	UFUNCTION(BlueprintPure)
	int32 GetPlayerLevel() const { return PlayerLevel; }

	UFUNCTION(BlueprintCallable)
	void SetPlayerLevel(int32 NewLevel) { PlayerLevel = NewLevel; }

	// 存活时长（用于排名）
	UPROPERTY(Replicated, BlueprintReadOnly)
	float SurvivalTime = 0.f;

	// ===== 余烬 Cinder：击杀掉落的花销货币，用于补给站补弹 / 波间商店采买 =====
	UPROPERTY(Replicated, BlueprintReadOnly)
	int32 Cinder = 0;

	UFUNCTION(BlueprintPure)
	int32 GetCinder() const { return Cinder; }

	/** 服务器：增加余烬 */
	void AddCinder(int32 Amount) { Cinder = FMath::Max(0, Cinder + Amount); }

	/** 服务器：尝试花费，成功返回 true */
	bool TrySpendCinder(int32 Cost)
	{
		if (Cost <= 0 || Cinder < Cost) { return false; }
		Cinder -= Cost;
		return true;
	}

	// 开局就绪（双人各自关闭简报确认后，WaveManager 才放第一波）
	UPROPERTY(Replicated, BlueprintReadOnly)
	bool bReadyToStart = false;

	bool IsReadyToStart() const { return bReadyToStart; }
	void SetReadyToStart() { bReadyToStart = true; }

	// 已选增益列表（用于其他玩家UI显示）
	UPROPERTY(Replicated, BlueprintReadOnly)
	TArray<EUpgradeType> UpgradeList;

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

private:
	UPROPERTY(Replicated)
	int32 Kills = 0;

	UPROPERTY(Replicated)
	int32 PlayerLevel = 1;

	bool bAlive = true;
};
