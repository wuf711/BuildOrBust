#include "BoBShop.h"

// 商品表：改平衡/改文案只动这里。
// 常备栏 = 保底补给(不限次)；轮换栏 = 收藏品，每件都是一次"规则交易"。
//
// 文案两条规矩：
//   Desc   —— 机制说清楚，正负写在一起，玩家不用去别处找代价。
//   Flavor —— 用户定稿，游戏内详情页原文显示。
static const FBoBShopEntry GShopTable[] =
{
// ============ 常备货架(保底补给，不限次) ============
/* Medkit */ {
	TEXT("战术急救包"),
	TEXT("立即回满生命值"),
	TEXT("勘察局下发的标准医疗干预组件。当你的肢体因撕裂而发出过载警报时，其内置的强效镇痛剂能直接切断中枢神经的痛觉反馈。“只要你还能扣动扳机，系统就默认你处于健康状态。”"),
	25, 0, TEXT("/Game/BoB/UI/Icons/T_Item_Medkit.T_Item_Medkit") },
/* AmmoCrate */ {
	TEXT("通用弹药箱"),
	TEXT("当前武器备弹上限 +1 轮，并按新上限补满。混沌比特不受补给线管辖"),
	TEXT("投送端有一条写在明面上的死规矩：“当场签收，离柜免责”。至于这个毫无焊缝的压制箱体在离手三分钟后为何会凭空多出几百克质量，官方统一将其归咎于测重仪表的基准漂移。"),
	20, 0, TEXT("/Game/BoB/UI/Icons/T_Item_Ammo.T_Item_Ammo") },
/* CoreWeld */ {
	TEXT("基准补充剂"),
	TEXT("为核心补充 250 点耐久。核心为双方共用，谁付账都一样"),
	TEXT("极具讽刺的是，某位技术员违规私自调和的产物，在基准维持的效率上竟远超官方标准配方。调和比例破译自某块未知碎片的几何刻痕，代价是第一任破译者已经被送进了物理隔离室。"),
	40, 0, TEXT("/Game/BoB/UI/Icons/T_Item_Weld.T_Item_Weld") },
/* Lumen */ {
	TEXT("便携谐振灯"),
	TEXT("买下进道具栏，按 Q 就地展开压制场，持续降低范围内的失谐增长速率。部署后无法回收或移动"),
	TEXT("声压计的归零表盘与展开时肉眼不可见的微弱频闪，共同构成了这层脆弱的庇护所。守夜守则第四条是一项出于理智保护的强制建议：当光晕铺开时，绝对不要试图凝视光圈边缘之外的阴影。"),
	26, 0, TEXT("/Game/BoB/UI/Icons/T_Item_Lumen.T_Item_Lumen") },

// ============ 轮换货架(收藏品，每波随机上架，限购) ============
/* Coolant */ {
	TEXT("过载冷却剂"),
	TEXT("本次同化潮内解除当前武器的射速限制。持续开火期间每秒扣血，最低保留 1 点"),
	TEXT("强行突破武器理论射速阈值的代价，是阀门破封瞬间自枪管蔓延至护木的极寒。操作手册建议使用者在痛觉袭来时“适时松手”，但前线干员通常会选择用战术胶带将手掌与护木死死缠接。"),
	42, 1, TEXT("/Game/BoB/UI/Icons/T_Item_Coolant.T_Item_Coolant") },
/* DecoyBeacon */ {
	TEXT("诱饵信标"),
	TEXT("本次同化潮内标记地图上的全部采集点，配额获取翻倍。启用瞬间失谐 +30"),
	TEXT("只要向地面抛出这枚高速旋转的装置，四射的强光就能极其高效地标定出遗构的精准坐标。同时，它也向方圆数里内的所有捕食者发送了一份精确的用餐邀请。"),
	34, 1, TEXT("/Game/BoB/UI/Icons/T_Item_Decoy.T_Item_Decoy") },
/* HedgeContract */ {
	TEXT("配额透支协议"),
	TEXT("抵押 30% 生命上限，立刻到手 60 配额。上限本局内不会自行恢复"),
	TEXT("签字者将以不可逆的生命阈值损耗为抵押，换取立等可取的物资配额。这张压膜硬卡背面的清偿条款密密麻麻，却无人翻阅——在这里，死亡往往比债务更早结清。"),
	0, 1, TEXT("/Game/BoB/UI/Icons/T_Item_Hedge.T_Item_Hedge") },
/* ScryLens */ {
	TEXT("地形回波仪"),
	TEXT("标记地图上的全部采集点。不提供任何战斗增益"),
	TEXT("没人知道外壳上那些逢六进一的古怪刻度属于哪种文明。真正令人不安的是，这件从废墟里捡来的残破仪器，竟能与联合勘察局的现役终端完成零误差的数据握手。我们似乎只是在用一套更劣质的仿品，重新丈量前人的遗迹。"),
	28, 1, TEXT("/Game/BoB/UI/Icons/T_Item_Lens.T_Item_Lens") },
/* AnchorLine */ {
	TEXT("回抛索"),
	TEXT("买下进道具栏，按 Q 立刻返回核心，随身遗构一并带回。只能用一次"),
	TEXT("绳结在承受极限拉力时，会自动松解半圈以化解致命的物理冲量。后勤规范严厉警告干员必须将其“视作不可拆解的一体化装置”，因为尝试复原绳结的人都永远失去了他们的右臂。"),
	38, 1, TEXT("/Game/BoB/UI/Icons/T_Item_Anchor.T_Item_Anchor") },
/* PhaseNet */ {
	TEXT("相位阻断网"),
	TEXT("本次同化潮内在核心周围展开固定位置的减速力场。目标进入该力场时将优先沿边缘绕行"),
	TEXT("拦截测试的录像显示，陷入这片通电区域的异常实体，全部表现出整齐划一的顺时针洄游倾向。它们在这层类似高黏度流体的空间滞涩中盲目打转，宛如被卷入了某种不可见的巨型涡流。"),
	36, 1, TEXT("/Game/BoB/UI/Icons/T_Item_PhaseNet.T_Item_PhaseNet") },
/* Sentry */ {
	TEXT("自律哨戒单元"),
	TEXT("部署一台自动炮塔，自主索敌开火。本次同化潮结束时停机，不保留至下次"),
	TEXT("依赖三足液压结构固定的自动火力平台。唤醒这台双联机炮的唯一方式，是准确复述四个发音逻辑极度反人类的晦涩音节。那是上一批外来者用命试出来的最高权限。"),
	50, 1, TEXT("/Game/BoB/UI/Icons/T_Item_Sentry.T_Item_Sentry") },
/* WildCore */ {
	TEXT("未编目遗构"),
	TEXT("一场赌局。七成给你好处，三成让失谐暴涨 35"),
	TEXT("勘察局的资料库里没有为这种光滑的多面体留下任何位置。直接接触它是一项纯粹的概率学处刑：要么诱发生理机能的良性突变，要么瞬间向视觉神经灌入高强度的频闪噪音。"),
	15, 3, TEXT("/Game/BoB/UI/Icons/T_Item_WildCore.T_Item_WildCore") },
/* Damper */ {
	TEXT("谐振抑制器"),
	TEXT("本次同化潮内失谐增长速率降低 35%。随同化潮结束失效"),
	TEXT("无论宿主如何移动，这个违背了现有物理定律的黑盒都会精准追踪其失谐波动。曾有干员试图通过物理摧毁表盘来逃避它的凝视，最终只换来了处决者的提前降临。"),
	30, 1, TEXT("/Game/BoB/UI/Icons/T_Item_Damper.T_Item_Damper") },
/* TimeExt */ {
	TEXT("勘探延时许可"),
	TEXT("本次险区勘探时限 +25 秒。在外面待得越久，被找到的机会越多"),
	TEXT("由勘察局签发的权限凭证。拿着这张单据，勘察员能强行从系统里多榨取二十五秒的滞留容错。但本土主导的回收倒计时从不为个体的狂奔提供余地，迟到在这里不是时间概念，而是物理形态的终结。"),
	30, 1, TEXT("/Game/BoB/UI/Icons/T_Item_TimeExt.T_Item_TimeExt") },
/* Charge */ {
	TEXT("应急高爆装药"),
	TEXT("买下进道具栏，按 Q 在脚下引爆一次大范围高爆。只有一次"),
	TEXT("绝缘胶带将保险栓死死缠了八圈，接缝处的平整度透着一种病态的强迫症。它的生产批号早已被刮除，外壳上残留着一行用战术匕首刻下的潦草遗言：“留给第十潮，塞进它们嘴里。”"),
	45, 1, TEXT("/Game/BoB/UI/Icons/T_Item_Charge.T_Item_Charge") },
/* Stim */ {
	TEXT("机能强化剂"),
	TEXT("生命上限永久 +15。当下不回血"),
	TEXT("临床观察表明，多次注射这种琥珀色液体的干员，其表现出的痛觉缺失与机械重复行为，与初级同化体的症状呈高度重合。面对这份报告，联合勘察局给出的最终批复是：允许大批量配发。"),
	35, 3, TEXT("/Game/BoB/UI/Icons/T_Item_Stim.T_Item_Stim") },
};

static_assert(UE_ARRAY_COUNT(GShopTable) == static_cast<int32>(EBoBShopItem::MAX),
	"商品表长度必须与 EBoBShopItem 枚举一致");

const FBoBShopEntry& GetShopEntry(EBoBShopItem Item)
{
	const int32 Idx = FMath::Clamp(static_cast<int32>(Item), 0, static_cast<int32>(EBoBShopItem::MAX) - 1);
	return GShopTable[Idx];
}
