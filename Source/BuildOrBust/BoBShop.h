// Build or Bust — 补给阶段商店
// 两个货架：常备(固定 4 件，不限次) + 每波轮换(随机抽 4 件，限购)
// 每件商品带 Flavor(氛围文案) 与 Downside(代价)，选中后在详情页展示再购买。

#pragma once

#include "CoreMinimal.h"
#include "BoBShop.generated.h"

UENUM(BlueprintType)
enum class EBoBShopItem : uint8
{
	// ---- 常备货架(固定，每波都有，不限次) ----
	Medkit          UMETA(DisplayName = "战术急救包"),
	AmmoCrate       UMETA(DisplayName = "通用弹药箱"),
	CoreWeld        UMETA(DisplayName = "基准补充剂"),
	Lumen           UMETA(DisplayName = "便携谐振灯"),

	// ---- 轮换货架(创意收藏品，每波随机上架，限购)；顺序须与 BoBShop.cpp 表一致 ----
	Coolant         UMETA(DisplayName = "过载冷却剂"),
	DecoyBeacon     UMETA(DisplayName = "诱饵信标"),
	HedgeContract   UMETA(DisplayName = "配额透支协议"),
	ScryLens        UMETA(DisplayName = "地形回波仪"),
	AnchorLine      UMETA(DisplayName = "回抛索"),
	PhaseNet        UMETA(DisplayName = "相位阻断网"),
	Sentry          UMETA(DisplayName = "自律哨戒单元"),
	WildCore        UMETA(DisplayName = "未编目遗构"),
	Damper          UMETA(DisplayName = "谐振抑制器"),
	TimeExt         UMETA(DisplayName = "勘探延时许可"),
	Charge          UMETA(DisplayName = "应急高爆装药"),
	Stim            UMETA(DisplayName = "机能强化剂"),

	MAX             UMETA(Hidden)
};

/** 常备货架商品数(枚举前 N 个，不限购) */
static constexpr int32 BoBShopFixedCount = 4;
/** 每波轮换货架展示数量 */
static constexpr int32 BoBShopRotatingSlots = 4;

USTRUCT(BlueprintType)
struct FBoBShopEntry
{
	GENERATED_BODY()

	/** 显示名 */
	FString Name;
	/** 效果说明：正负一起写清楚，不再单列"代价"段 */
	FString Desc;
	/** 详情页灰字：一段场景切片，不解释机制 */
	FString Flavor;
	/** 余烬售价 */
	int32 Cost = 0;
	/** 本波可购次数上限(0 = 不限) */
	int32 StockPerWave = 0;
	/** 图标资产路径(2D 意象；留空则画占位符) */
	FString IconPath;
};

const FBoBShopEntry& GetShopEntry(EBoBShopItem Item);
