#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UpgradeTypes.h"
#include "UpgradeComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnUpgradeApplied);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BUILDORBUST_API UUpgradeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UUpgradeComponent();

	UPROPERTY(BlueprintAssignable)
	FOnUpgradeApplied OnUpgradeApplied;

	// Server调用：应用选中的增益
	UFUNCTION(BlueprintCallable, Server, Reliable)
	void Server_ApplyUpgrade(EUpgradeType Type);

	// 升级时随机抽取 NumChoices 个不重复增益供玩家选择
	UFUNCTION(BlueprintCallable)
	TArray<EUpgradeType> RollChoices(int32 NumChoices);

	// 获取某增益的显示名称和描述（供卡片UI使用）
	UFUNCTION(BlueprintPure)
	static void GetUpgradeDisplay(EUpgradeType Type, FText& OutName, FText& OutDescription);

	UFUNCTION(BlueprintPure)
	bool HasUpgrade(EUpgradeType Type) const;

	UFUNCTION(BlueprintPure)
	int32 GetUpgradeCount(EUpgradeType Type) const;

	// 返回当前玩家持有的所有增益（用于UI展示）
	UFUNCTION(BlueprintPure)
	TArray<EUpgradeType> GetAllUpgrades() const { return OwnedUpgrades; }

protected:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 应用单个增益的实际效果到拥有者
	void ApplyUpgradeEffect(EUpgradeType Type);

private:
	UPROPERTY(Replicated)
	TArray<EUpgradeType> OwnedUpgrades;
};
