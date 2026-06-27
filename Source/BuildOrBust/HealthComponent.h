#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDeath, AActor*, DeadActor, AActor*, Killer);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnHealthChanged, float, CurrentHP, float, MaxHP, float, Delta);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class BUILDORBUST_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	UPROPERTY(BlueprintAssignable)
	FOnDeath OnDeath;

	UPROPERTY(BlueprintAssignable)
	FOnHealthChanged OnHealthChanged;

	UFUNCTION(BlueprintCallable)
	void TakeDamage(float Damage, AActor* DamageCauser);

	UFUNCTION(BlueprintCallable)
	void Heal(float Amount);

	UFUNCTION(BlueprintPure)
	float GetHP() const { return CurrentHP; }

	UFUNCTION(BlueprintPure)
	float GetMaxHP() const { return MaxHP; }

	UFUNCTION(BlueprintPure)
	bool IsDead() const { return bIsDead; }

	// 波次倍率应用入口（WaveManager调用）
	UFUNCTION(BlueprintCallable)
	void ApplyHealthMultiplier(float Multiplier);

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float MaxHP = 100.f;

private:
	UPROPERTY(ReplicatedUsing=OnRep_HP)
	float CurrentHP = 100.f;

	bool bIsDead = false;

	UFUNCTION()
	void OnRep_HP();
};
