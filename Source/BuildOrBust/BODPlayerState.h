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
