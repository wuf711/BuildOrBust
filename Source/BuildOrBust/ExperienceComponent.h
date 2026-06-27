#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ExperienceComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnLevelUp, int32, NewLevel);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BUILDORBUST_API UExperienceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UExperienceComponent();

	UPROPERTY(BlueprintAssignable)
	FOnLevelUp OnLevelUp;

	UFUNCTION(BlueprintCallable)
	void AddExperience(int32 Amount);

	UFUNCTION(BlueprintPure)
	int32 GetLevel() const { return CurrentLevel; }

	UFUNCTION(BlueprintPure)
	float GetExpPercent() const;

	// 波次结束时由 WaveManager（服务器）调用：仅在拥有该角色的客户端本地弹出三选一增益。
	// 复用 OnLevelUp 信号（BP 已绑定 OnLevelUp → 弹 WB_UpgradePicker），无需改蓝图连线。
	UFUNCTION(Client, Reliable)
	void Client_OfferUpgrade();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UPROPERTY(Replicated)
	int32 CurrentExp = 0;

	UPROPERTY(Replicated)
	int32 CurrentLevel = 1;

	// 每级所需经验 = BaseExp * Level^ExpScale
	UPROPERTY(EditDefaultsOnly)
	int32 BaseExp = 10;

	UPROPERTY(EditDefaultsOnly)
	float ExpScale = 1.4f;

	int32 GetExpToNextLevel() const;

	UFUNCTION()
	void OnRep_Level();
};
