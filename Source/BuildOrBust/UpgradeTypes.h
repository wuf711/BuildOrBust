#pragma once

#include "CoreMinimal.h"
#include "UpgradeTypes.generated.h"

UENUM(BlueprintType)
enum class EUpgradeType : uint8
{
	// 攻击类
	PiercingBullet,		// 穿透弹
	ExplosiveHeadshot,	// 爆裂头
	RicochetBullet,		// 弹射
	DoubleFire,			// 双发
	FrostBullet,		// 冰冻弹

	// 生存类
	VampireBullet,		// 击杀回血
	DashAbility,		// 冲刺
	ArmorLayer,			// 护甲
	ExpMagnet,			// 经验磁铁

	// 变态类（10级后解锁）
	BulletTime,			// 子弹时间
	MirrorClone,		// 镜像分身
	ChainExplosion,		// 死亡爆炸

	// 属性类（已实现生效，进入抽卡池）
	SpeedBoost,			// 疾跑：移速+15%
	MaxHealth,			// 强体：最大生命+75

	// 控制/爆发类（已实现生效）
	PoisonBullet,		// 剧毒：命中持续掉血
	InstaKill,			// 斩杀：概率秒杀

	// 核心强化（肉鸽推关爽点：可叠加的伤害倍率）
	DamageUp,			// 强化弹药：子弹伤害提升，可叠层

	// 得分类（同场竞技核心：直接放大击杀收益，决定 PVP 胜负）
	ScoreBoost,			// 赏金：每次击杀得分提升，可叠加
	CritScore,			// 致命彩头：击杀有概率获得双倍得分，可叠加

	// 守家类（增益作用于中央核心，契合据点防守玩法）
	CoreRestore,		// 核心回能：每次击杀为核心回血
	CoreReinforce,		// 核心增幅：核心血量上限提升并立即修复
	CoreShield,			// 核心护盾：核心受到的伤害降低
};

USTRUCT(BlueprintType)
struct FUpgradeData
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EUpgradeType Type;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText Name;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	FText Description;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	UTexture2D* Icon = nullptr;

	// 只有等级>=UnlockLevel才会进入抽取池
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	int32 UnlockLevel = 1;
};
