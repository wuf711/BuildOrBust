# -*- coding: utf-8 -*-
"""
生成 DT_BoBEnemy 的导入 CSV（21 个型号 + Boss）。

数值在这里定，不在 UE 里手填——21 行手填必然写错，而且改一次平衡要点 21 次。
跑一遍：
  python tools/make_enemy_csv.py
输出：
  Art/Export/DT_BoBEnemy.csv
"""
import csv
import os
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[1]
OUT = PROJECT_ROOT / "Art/Export/DT_BoBEnemy.csv"
FOES = "/Game/BoB/Foes"


def mesh(n):
    return "%s/SK_%s/SK_%s.SK_%s" % (FOES, n, n, n)


def anim(n, clip):
    return "%s/SK_%s/A_%s_%s.A_%s_%s" % (FOES, n, n, clip, n, clip)


# 基础型号：id, 名, 派系, 层级, 定位, 动词, HP, 速, 伤, 正面减伤,
#           弱点骨, cost, 解锁潮, 权重, 凝视档, 凝视参数, mesh 名, 三段动作, 质检
BASE = [
    ("Pilgrim", "朝圣晶簇", "Accept", "Drift", "Regular", "朝",
     100, 300, 12, 0.0, "head", 10, 1, 1.6, "None", 0.0,
     "Pilgrim", ("Walk_Pilgrimage", "Attack_Offer", "Death"),
     "AoE 清杂能力 + 备弹深度"),

    ("Embracer", "拥抱者", "Accept", "Symbiote", "Interactive", "拥",
     320, 260, 25, 1.0, "spine_02", 30, 4, 0.9, "Offset40", 0.20,
     "Embracer", ("Walk_Guard", "Attack_Embrace", "Death"),
     "极限机动性 + 绕侧爆发"),

    ("Sower", "侵蚀温床", "Accept", "Symbiote", "Elite", "播",
     900, 150, 18, 0.0, "spine_02", 70, 6, 0.5, "Dissonance80", 1.0,
     "Sower", ("Walk_Brood", "Attack_Seed", "Death"),
     "团队分工 + 主动拉高失谐的胆量"),

    ("Martyr", "殉道重甲", "Resist", "Drift", "Regular", "撞",
     400, 240, 30, 0.60, "spine_02", 28, 3, 1.3, "None", 0.0,
     "Martyr", ("Walk_Heavy", "Attack_Charge", "Death"),
     "破甲能力 / 高单发伤害"),

    ("Scavenger", "拾荒残躯", "Resist", "Symbiote", "Elite", "修",
     260, 280, 16, 0.0, "spine_01", 34, 4, 0.8, "Dissonance70", 1.0,
     "Scavenger", ("Walk_Limp", "Attack_Repair", "Death"),
     "爆发窗口把握（3 秒内打死）"),

    ("Bearer", "滤网背负者", "Resist", "Symbiote", "Interactive", "泄",
     300, 230, 20, 0.0, "spine_01", 26, 5, 1.0, "Offset40", 2.0,
     "Bearer", ("Walk_Bear", "Attack_Vent", "Death"),
     "精准射击 + 交战距离管理"),

    ("Runner", "跃迁斥候", "Explorer", "Drift", "Regular", "扑",
     130, 520, 22, 0.0, "head", 22, 5, 1.2, "Dissonance70", 3.0,
     "Runner", ("Run_Zigzag", "Attack_Pounce", "Death"),
     "跟枪 / 拉枪基本功"),

    ("Misstep", "失步者", "Explorer", "Symbiote", "Interactive", "走火",
     180, 250, 14, 0.0, "lowerarm_r", 30, 6, 0.9, "Offset40", 2.0,
     "Misstep", ("Walk_Misstep", "Attack_Misfire", "Death"),
     "听音辨位 + 远程反制"),

    ("Preacher", "传道者", "Explorer", "Symbiote", "Interactive", "诳",
     220, 260, 10, 0.0, "neck_01", 32, 7, 0.8, "Offset40", 5.0,
     "Preacher", ("Walk_Preach", "Attack_Proclaim", "Death"),
     "目标优先级 + 团队沟通"),

    ("Mirror", "完美镜像", "Explorer", "Ascend", "Elite", "仿",
     500, 380, 0, 0.0, "", 80, 8, 0.4, "None", 0.0,
     "Mirror", ("Walk_Mimic", "Attack_Mimic", "Death"),
     "团队信任 + 反直觉执行力"),

    ("Terminal", "CS-07 模因终端", "Terminal", "Ascend", "Boss", "校对",
     0, 0, 0, 0.0, "crust_mid", 0, 10, 0.0, "None", 0.0,
     "Terminal", ("Idle_Pulse", "Break_Mid", "Break_Base"),
     "撑住 90 秒，不是打死它"),
]

# 变种：id, 名, 基础型号, HP 倍, 速 倍, 伤 倍, 正面减伤覆盖(None=继承),
#       cost, 解锁潮, 权重, 缩放, 隐藏骨骼, 材质标量, 质检
VARIANT = [
    ("Pilgrim_Twin", "双冠朝圣者", "Pilgrim",
     1.40, 1.0, 1.0, None, 14, 5, 0.7, 1.10, "",  # 全显示：多一层花冠
     "CrustAmount=1.35", "单体穿透伤害"),
    ("Pilgrim_Broken", "断冠者", "Pilgrim",
     0.80, 1.25, 1.0, None, 12, 7, 0.6, 0.95, "crown_r,crown_2nd",
     "CrustAmount=0.55", "跟枪能力"),

    ("Embracer_Dual", "双环拥抱者", "Embracer",
     1.0, 1.0, 1.0, None, 38, 8, 0.5, 1.05, "",  # 全显示：两个环
     "CrustAmount=1.25", "持续输出与弹药管理"),

    ("Martyr_Cracked", "破甲殉道者", "Martyr",
     1.0, 1.15, 1.0, 0.35, 26, 6, 0.9, 1.0, "plate_hi,plate_lo,helm",
     "CrustAmount=0.70", "目标优先级判断"),
    ("Martyr_Warden", "全甲督战者", "Martyr",
     1.0, 0.80, 1.0, 0.75, 40, 8, 0.3, 1.08, "",
     "CrustAmount=1.30", "爆头精度"),

    ("Scavenger_Bare", "空手拾荒者", "Scavenger",
     0.70, 1.0, 1.0, None, 18, 5, 1.0, 0.95, "batt,claw",  # 减法
     "CrustAmount=0.60", "无修补机制，纯常规压力"),

    ("Bearer_Twin", "双筒背负者", "Bearer",
     1.0, 1.0, 1.0, None, 34, 8, 0.5, 1.06, "",
     "CrustAmount=1.20", "精准射击加强"),

    ("Runner_Maimed", "断肢斥候", "Runner",
     1.0, 1.20, 0.70, None, 24, 7, 0.8, 0.96, "lowerarm_l,hand_l,blades",
     "CrustAmount=0.75", "极限跟枪"),
    ("Runner_Dual", "双持斥候", "Runner",
     1.0, 1.0, 1.0, None, 32, 9, 0.3, 1.0, "",
     "CrustAmount=0.90", "近身应对"),

    ("Misstep_Dual", "双臂失步者", "Misstep",
     1.0, 1.0, 1.0, None, 40, 9, 0.3, 1.0, "",
     "CrustAmount=1.15", "听音辨位加强"),

    ("Preacher_Silent", "静默传道者", "Preacher",
     1.0, 1.0, 1.0, None, 38, 8, 0.35, 1.0, "",
     "CrustAmount=0.40,EmissiveMul=0.0", "HUD 异常察觉能力"),
]

# 基础型号默认隐藏的预埋件。变体把对应的项从这里去掉就"长出来"了。
# 这一栏就是"加法变种"的实现：件先建好，谁不隐藏谁就有。
BASE_HIDE = {
    "Pilgrim": "crown_2nd",       # 第二层十二瓣  -> 双冠朝圣者
    "Embracer": "ring_b",         # 第二个环      -> 双环拥抱者
    "Martyr": "helm",             # 督战者头盔    -> 全甲督战者
    "Bearer": "filter_b",         # 第二个滤芯    -> 双筒背负者
    "Runner": "blades",           # 前臂双刃      -> 双持斥候
    "Misstep": "arm_b",           # 第二条融合臂  -> 双臂失步者
}

# 变种的常驻姿态：骨骼 -> 欧拉角（度）。和 tools/blender/preview_all.py 里的
# POSE 表是同一份数据——预览和运行时必须看同一个来源，否则图对不上游戏。
POSE = {
    "Martyr_Warden": "thigh_l=26,0,0;thigh_r=26,0,0;calf_l=-34,0,0;calf_r=-34,0,0;"
                     "spine_01=7,0,0;spine_02=5,0,0;clavicle_l=0,0,-16;"
                     "clavicle_r=0,0,16;upperarm_l=0,0,-20;upperarm_r=0,0,20;head=-9,0,0",
    "Martyr_Cracked": "spine_01=-6,0,0;spine_02=-5,0,0;thigh_l=-8,0,0;"
                      "thigh_r=-6,0,0;head=6,0,0",
    "Bearer_Twin": "spine_01=13,0,0;spine_02=10,0,0;neck_01=14,0,0;"
                   "thigh_l=-12,10,0;thigh_r=-12,-10,0;"
                   "upperarm_l=10,-8,0;upperarm_r=10,8,0",
    "Runner_Dual": "spine_01=12,0,0;spine_03=8,0,0;upperarm_l=-34,0,-12;"
                   "upperarm_r=-34,0,12;lowerarm_l=-52,0,0;lowerarm_r=-52,0,0;"
                   "thigh_l=-14,0,0;thigh_r=10,0,0;head=-8,0,0",
    "Runner_Maimed": "clavicle_l=0,0,14;spine_02=0,0,9;spine_03=0,0,7;"
                     "head=0,0,-6;upperarm_r=-12,0,8",
    "Misstep_Dual": "clavicle_l=0,0,12;clavicle_r=0,0,-12;spine_02=8,0,0;"
                    "spine_03=6,0,0;neck_01=10,0,0;head=3,0,0;"
                    "upperarm_l=6,0,4;upperarm_r=6,0,-4",
    "Pilgrim_Twin": "spine_02=-4,0,0;spine_03=-5,0,0;neck_01=-4,0,0",
    "Pilgrim_Broken": "spine_02=0,0,-7;spine_03=0,0,-9;neck_01=0,0,-6;head=0,0,-5",
    "Embracer_Dual": "spine_02=9,0,0;spine_03=7,0,0;"
                     "upperarm_l=-10,0,0;upperarm_r=-10,0,0",
    "Scavenger_Bare": "spine_01=0,0,5;spine_02=0,0,4",
    "Preacher_Silent": "spine_02=6,0,0;neck_01=8,0,0;head=7,0,0;"
                       "upperarm_l=4,0,-6;upperarm_r=4,0,6",
}

HEADER = [
    "---", "DisplayName", "Faction", "Tier", "Role", "Verb",
    "HP", "MoveSpeed", "Damage", "FrontalDR", "WeakpointBone", "WeakpointMul",
    "SpawnCost", "WaveUnlock", "ThreatTier", "PickWeight",
    "GazeHookBand", "GazeHookParam",
    "VariantOf", "MeshScale", "HiddenBones", "MaterialScalars", "PoseOverride",
    "Mesh", "AnimMove", "AnimAttack", "AnimDeath",
    "WarnSFX", "LoopSFX", "DeathSFX", "BuildCheckNote",
]


def bones(s):
    """UE 的 CSV 导入里，数组写成 (A,B) 这种带括号的形式。"""
    if not s:
        return "()"
    return "(%s)" % ",".join('"%s"' % b.strip() for b in s.split(",") if b.strip())


def scalars(s):
    """TMap 写成 (("Key", Value)) 形式。"""
    if not s:
        return "()"
    items = []
    for kv in s.split(","):
        k, _, v = kv.partition("=")
        items.append('("%s", %s)' % (k.strip(), v.strip()))
    return "(%s)" % ",".join(items)


def pose(s):
    """TMap<FName,FVector>：(("bone",(X=..,Y=..,Z=..)), ...)"""
    if not s:
        return "()"
    items = []
    for entry in s.split(";"):
        entry = entry.strip()
        if not entry:
            continue
        k, _, v = entry.partition("=")
        xs = [t.strip() for t in v.split(",")]
        while len(xs) < 3:
            xs.append("0")
        items.append('("%s",(X=%s,Y=%s,Z=%s))' % (k.strip(), xs[0], xs[1], xs[2]))
    return "(%s)" % ",".join(items)


# 体型 = 强度的第一读数。玩家隔着半个场地看不清血条，但一眼能看出个头。
#
# 下面第二列是导入 UE 后实测的原始高度（厘米，tools/test_scale.py 量的），
# 第三列是想要的成品高度，缩放值由两者相除得出。玩家胶囊约 192。
# 要调体型改"目标"那一列就行，别直接改缩放——原始高度会随模型重做变动。
#
#                     原始    目标   HP
_SIZE = {
    "Pilgrim":   (224.2, 225),   # 100  最常见的杂兵，作为尺度基准
    "Runner":    (197.4, 215),   # 130  精瘦，靠快不靠大
    "Misstep":   (180.0, 225),   # 180
    "Preacher":  (191.0, 235),   # 220
    "Scavenger": (119.0, 185),   # 260  三足矮壮，刻意压低
    "Bearer":    (179.1, 245),   # 300
    "Embracer":  (155.3, 230),   # 320
    "Martyr":    (225.6, 285),   # 400  重甲，明显压场
    "Mirror":    (181.5, 270),   # 500  精英
    "Sower":     (230.1, 355),   # 900  巨型，远远就该认出来
    "Terminal":  (1003.1, 1003), # Boss 本来就有十米，不动
}
SCALE = {k: round(t / raw, 3) for k, (raw, t) in _SIZE.items()}


# 威胁档 1-4：控制型号在一潮**之内**何时才允许出场（WaveUnlock 管的是第几潮解锁，两回事）。
# 按"打起来有多难缠"分，不按 cost——cost 是投放成本，跟难度不是一回事。
THREAT = {
    "Pilgrim": 1, "Pilgrim_Twin": 1, "Pilgrim_Broken": 1,
    "Runner": 1, "Runner_Maimed": 1,
    "Martyr": 2, "Martyr_Cracked": 2, "Misstep": 2, "Misstep_Dual": 2,
    "Bearer": 2, "Bearer_Twin": 2,
    "Embracer": 3, "Embracer_Dual": 3, "Preacher": 3, "Preacher_Silent": 3,
    "Scavenger": 3, "Scavenger_Bare": 3, "Runner_Dual": 3, "Martyr_Warden": 3,
    "Sower": 4, "Mirror": 4,
    "Terminal": 4,
}

rows = []
lookup = {}

for (rid, name, fac, tier, role, verb, hp, spd, dmg, dr, wp,
     cost, wave, weight, band, param, mname, clips, note) in BASE:
    lookup[rid] = (hp, spd, dmg, dr, fac, tier, role, verb, band, param)
    # 基础型号要把预埋的变体件藏起来。
    # "多挂一个"那类变体（双冠、双环、双筒、双持、双臂）Hide Bone 本来做不到，
    # 把件先建好、让基础型号隐藏，加法就变成了减法。
    rows.append([
        rid, name, fac, tier, role, verb,
        hp, spd, dmg, dr, wp, 2.5,
        cost, wave, THREAT.get(rid, 1), weight, band, param,
        "", SCALE.get(rid, 1.0), bones(BASE_HIDE.get(rid, "")), "()", "()",
        mesh(mname), anim(mname, clips[0]), anim(mname, clips[1]),
        anim(mname, clips[2]),
        "", "", "", note,
    ])

for (rid, name, base, hpm, spdm, dmgm, drov, cost, wave, weight,
     scale, hide, mats, note) in VARIANT:
    bhp, bspd, bdmg, bdr, bfac, btier, brole, bverb, bband, bparam = lookup[base]
    # 派系/层级/定位/动词/凝视钩全部照抄基础行：留空的话枚举会解析失败，
    # 而且 Director 按 Role 做配比控制时会把变种漏掉
    rows.append([
        rid, name, bfac, btier, brole, bverb,
        round(bhp * hpm, 1), round(bspd * spdm, 1), round(bdmg * dmgm, 1),
        bdr if drov is None else drov,
        "", 2.5,
        cost, wave, THREAT.get(rid, 1), weight, bband, bparam,
        base, round(SCALE.get(base, 1.0) * scale, 3),
        bones(hide), scalars(mats), pose(POSE.get(rid, "")),
        "", "", "", "",          # 资产留空，运行时从 VariantOf 那行取
        "", "", "", note,
    ])

os.makedirs(OUT.parent, exist_ok=True)
with open(OUT, "w", newline="", encoding="utf-8-sig") as f:
    w = csv.writer(f)
    w.writerow(HEADER)
    for r in rows:
        w.writerow(r)

print("写出 %d 行 -> %s" % (len(rows), OUT))
print("  基础型号 %d   变种 %d" % (len(BASE), len(VARIANT)))
# 变种继承的派系/定位要在导入后由 UE 侧补，这里留空是故意的：
# CSV 里填了反而会和基础行不同步
