#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BaseCore.generated.h"

class USphereComponent;
class UStaticMeshComponent;

/**
 *  据点核心：中心可被摧毁的基地。
 *  敌人进入伤害范围会持续削减基地血量，归零则游戏失败。
 */
UCLASS()
class BUILDORBUST_API ABaseCore : public AActor
{
	GENERATED_BODY()

public:
	ABaseCore();

	// 基地最大血量
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category="Base")
	float MaxBaseHP = 1000.0f;

	// 每个敌人每秒造成的伤害
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Base")
	float DamagePerEnemyPerSecond = 10.0f;

	// 伤害结算间隔
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Base")
	float DamageInterval = 1.0f;

	// 每波结束基地血量上限的自动增长（自动升级，不再依赖玩家增益）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Base")
	float WaveHPGrowth = 200.0f;

	UFUNCTION(BlueprintPure, Category="Base")
	float GetBaseHPPercent() const;

	UFUNCTION(BlueprintPure, Category="Base")
	float GetBaseHP() const { return CurrentBaseHP; }

	//~ 守家增益接口（均在服务器调用）
	/** 为核心回血（击杀触发） */
	void RepairBase(float Amount);
	/** 提升核心血量上限并立即修复同等数值 */
	void AddMaxHPAndHeal(float Amount);
	/** 累加核心减伤（0~0.8） */
	void AddBaseDamageReduction(float Pct);
	/** 每波结束自动调用：提升核心血量上限并回满（守家自动化） */
	void AutoUpgradeAndHeal();
	/** 对核心造成伤害（含减伤、归零判负）。近战啃食与远程轰击共用此入口。 */
	void DamageCore(float RawAmount);

	// 蓝图表现：基地受损（传剩余百分比）
	UFUNCTION(BlueprintImplementableEvent, Category="Base", meta=(DisplayName="On Base Damaged"))
	void BP_OnBaseDamaged(float HPPercent);

	// 蓝图表现：基地被摧毁
	UFUNCTION(BlueprintImplementableEvent, Category="Base", meta=(DisplayName="On Base Destroyed"))
	void BP_OnBaseDestroyed();

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	USceneComponent* SceneRoot;

	// 基地外观（蓝图里拖入网格）
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	UStaticMeshComponent* BaseMesh;

	// 敌人进入即啃血的范围
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Components")
	USphereComponent* DamageZone;

private:
	// 当前血量（复制给客户端用于 HUD 显示）
	UPROPERTY(Replicated)
	float CurrentBaseHP = 0.0f;

	// 核心减伤百分比（守家增益累加）
	float DamageReductionPct = 0.0f;

	bool bDestroyed = false;
	FTimerHandle DamageTimer;

	void DamageTick();
};
