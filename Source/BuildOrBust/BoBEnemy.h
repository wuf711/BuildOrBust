// Build or Bust — 数据表驱动的敌人。21 个型号共用这一个类。

#pragma once

#include "CoreMinimal.h"
#include "Variant_Shooter/AI/ShooterNPC.h"
#include "BoBEnemyTypes.h"
#include "BoBEnemy.generated.h"

class UMaterialInstanceDynamic;

/**
 *  按 DT_BoBEnemy 的一行把自己配置出来。
 *
 *  规格书 10.2：变种不建新类，全部走数据表 + Socket/Hide Bone。
 *  所以 11 个基础型号和 11 个变种走的是同一个类、同一套代码，
 *  差别全在 ApplyEnemyRow 读到的那一行里。
 */
UCLASS()
class BUILDORBUST_API ABoBEnemy : public AShooterNPC
{
	GENERATED_BODY()

public:
	ABoBEnemy();

	/**
	 *  读表并把自己变成那个型号。变种会先经 ResolveAssets
	 *  从基础行继承 mesh 和动画。
	 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Enemy")
	bool ApplyEnemyRow(FName RowId);

	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	FName GetEnemyId() const { return EnemyId; }

	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	const FBoBEnemyRow& GetRow() const { return Row; }

	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	bool IsInteractive() const { return Row.Role == EBoBRole::Interactive; }

	/**
	 *  按命中部位和入射方向折算实际伤害。
	 *  弱点吃倍率，正面吃减伤——两者都来自数据表，不写死在类里。
	 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Enemy")
	float ResolveIncomingDamage(float RawDamage, FName HitBone,
		const FVector& HitFromDirection);

	/**
	 *  本型号的凝视钩当前是否被触发。
	 *
	 *  钩子档位来自数据表的 GazeHookBand，判定的是**场上任意一名玩家**的失谐/偏移，
	 *  不是被打的这一只自己的状态——规格书要的就是"队友主动污染自己去开壳"。
	 *  按自己算的话这个战术根本不成立。
	 */
	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	bool IsGazeHookActive() const;

	/**
	 *  孢子囊/外壳是否闭合中（闭合＝绝对防弹）。
	 *  目前只有侵蚀温床有这层壳：精英 + 失谐≥80 钩，就是它在数据表里的签名。
	 */
	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	bool IsShellClosed() const;

	/**
	 *  失谐伤害倍率：壳开之后，场上最高失谐越高，打进去越疼。
	 *
	 *  只有闸门（开/关）的话，玩家把失谐拉到 80 就没有继续往上顶的理由了，
	 *  "主动污染自己"这个战术只有一个台阶。做成连续的，越往危险里走收益越大，
	 *  跟处决者在 100 降临形成一条真正的风险收益曲线。
	 */
	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	float GetGazeDamageMul() const;

	/**
	 *  失谐到这个值才开始能打进伤害。低于它孢子囊闭合，绝对防弹。
	 *  取 50 而不是数据表里的 80：80 才开壳的话，玩家要冒着接近处决者
	 *  触发线的风险才拿得到第一点收益，窗口太窄。50 起步、100 吃满，
	 *  中间整段都是"再往上顶一点"的连续决策。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Enemy")
	float GazeDamageStart = 50.f;

	/** 失谐顶满（100）时的伤害倍率上限 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Enemy")
	float GazeDamageMax = 2.5f;

	/**
	 *  变种的常驻姿态。AnimBP 从这儿取，作为基础姿势叠加。
	 *  骨架摆位是共用的，体态差异只能从这条路走。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BoB|Enemy")
	TMap<FName, FVector> ActivePose;

	/**
	 *  最近一次受击的结算结果，给 HUD 画飘字用。
	 *  0 也要记——打在闭合的孢子囊上就是 0，那个 0 本身就是给玩家的信息，
	 *  比任何"弹着无效"的文案都干净。
	 */
	UPROPERTY(BlueprintReadOnly, Category = "BoB|Enemy")
	float LastHitDamage = -1.f;

	UPROPERTY(BlueprintReadOnly, Category = "BoB|Enemy")
	float LastHitTime = -100.f;

	UPROPERTY(BlueprintReadOnly, Category = "BoB|Enemy")
	FVector LastHitLoc = FVector::ZeroVector;

	/**
	 *  完美镜像：精英 + 无弱点骨。另两个精英都有弱点骨，这个组合唯一。
	 */
	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	bool IsMirror() const;

	/** 它复制的是谁。只有宿主看它是空白脸，宿主对它伤害为 0 */
	UPROPERTY(BlueprintReadOnly, Category = "BoB|Enemy")
	TObjectPtr<class AShooterCharacter> MirrorHost;

	/** 破防了没。破防后宿主才打得动它 */
	UPROPERTY(BlueprintReadOnly, Category = "BoB|Enemy")
	bool bMirrorBroken = false;

	/**
	 *  破防。规格书给了两条路：宿主打空当前弹匣，或宿主站进谐振灯光圈。
	 *  灯圈这条已经在 Tick 里判；打空弹匣那条要由开火/换弹链路调这个函数。
	 */
	UFUNCTION(BlueprintCallable, Category = "BoB|Enemy")
	void BreakMirror(const FString& Why);

	/** 谐振灯光圈半径，和阶段三保持一致 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Enemy")
	float MirrorLampRadius = 400.f;

	/** 是否是拾荒残躯那一档：精英 + 失谐≥70 钩，就是它在数据表里的签名 */
	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	bool IsScavenger() const;

	/** 正在修补（停步读条）中 */
	UFUNCTION(BlueprintPure, Category = "BoB|Enemy")
	bool IsRepairing() const { return RepairUntil > 0.f; }

	// —— 拾荒残躯，数值来自规格书 ——

	/** 修补读条时长。规格书给玩家的爆发窗口就是这 3 秒 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Enemy|拾荒")
	float RepairTime = 3.f;

	/** 吸收成功回多少最大 HP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Enemy|拾荒")
	float RepairHealFrac = 0.30f;

	/** 走到多近才停下来修补 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Enemy|拾荒")
	float RepairReach = 260.f;

	/** 失谐越档后改成远程吸收，够得着的距离 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Enemy|拾荒")
	float AbsorbReach = 1400.f;

public:
	virtual float TakeDamage(float Damage, const FDamageEvent& DamageEvent,
		AController* EventInstigator, AActor* DamageCauser) override;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** 镜像每拍：查灯圈破防条件 */
	void TickMirror();

	/** 拾荒残躯每拍：找遗构、停步读条、吸收 */
	void TickScavenge(float DeltaSeconds);

	/** 读条结束前被打断就白费——这正是"3 秒内打死"的窗口 */
	void CancelRepair();

	float RepairUntil = 0.f;
	TWeakObjectPtr<class ALootPickup> RepairTarget;
	float CachedWalkSpeed = 0.f;

	/**
	 *  改走自己的 AnimInstance，而不是基类的单节点播放。
	 *  基类那条路会把网格切成单节点模式并销毁 UBoBAnimInstance，
	 *  变种的常驻姿态也就跟着没了——实测生成半秒内就被冲掉。
	 */
	virtual void PlayLocoAnim(UAnimSequence* Anim, bool bLoop) override;

	/** 把 HiddenBones 里的骨骼藏掉，加法变种就是靠基础型号藏预埋件实现的 */
	void ApplyHiddenBones();

	/** 材质标量覆盖：晶体覆盖率、自发光强度 */
	void ApplyMaterialScalars();

	UPROPERTY(BlueprintReadOnly, Category = "BoB|Enemy")
	FName EnemyId;

	UPROPERTY(BlueprintReadOnly, Category = "BoB|Enemy")
	FBoBEnemyRow Row;

	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynMaterials;

	/** 场上所有玩家里最高的失谐值 */
	float HighestPlayerGaze() const;

	/** 本行凝视钩的档位阈值；无钩返回 0 */
	float GazeBandThreshold() const;

	/** 正面减伤的判定角度（度）：入射方向和朝向夹角小于它才算正面 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BoB|Enemy")
	float FrontalArc = 70.f;
};
