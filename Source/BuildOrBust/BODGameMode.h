#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BODGameMode.generated.h"

UCLASS()
class BUILDORBUST_API ABODGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABODGameMode();

	// 敌人死亡时由敌人调用
	UFUNCTION(BlueprintCallable)
	void NotifyEnemyKilled(APlayerController* Killer);

	// 玩家死亡时调用
	UFUNCTION(BlueprintCallable)
	void NotifyPlayerDied(APlayerController* DeadPlayer);

	UFUNCTION(BlueprintPure)
	int32 GetAlivePlayerCount() const { return AlivePlayerCount; }

protected:
	virtual void PostLogin(APlayerController* NewPlayer) override;

private:
	int32 AlivePlayerCount = 0;

	void CheckGameOver();
};
