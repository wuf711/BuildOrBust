# -*- coding: utf-8 -*-
"""
Director 曲线离线验算。

和 BoBAIDirector.cpp 里的方程保持一致，用来回答两个问题：
  1 喘息期在投放曲线上真的看得出来吗（不然那 0.3 就白设了）
  2 一潮的预算实际花掉多少、剩多少

跑：
  <UE>/Engine/Binaries/ThirdParty/Python3/Win64/python.exe tools/sim_director.py
"""

BASE_BUDGET = 90.0
GROWTH_K = 0.34
WAVE_DURATION = 100.0
DT = 0.1

# 型号 cost（取自 DT_BoBEnemy），按解锁潮筛
COSTS = [
    ("Pilgrim", 10, 1, 1.6, "regular"),
    ("Martyr", 28, 3, 1.3, "regular"),
    ("Embracer", 30, 4, 0.9, "interactive"),
    ("Scavenger", 34, 4, 0.8, "elite"),
    ("Bearer", 26, 5, 1.0, "interactive"),
    ("Runner", 22, 5, 1.2, "regular"),
    ("Pilgrim_Twin", 14, 5, 0.7, "regular"),
    ("Scavenger_Bare", 18, 5, 1.0, "elite"),
    ("Sower", 70, 6, 0.5, "elite"),
    ("Misstep", 30, 6, 0.9, "interactive"),
    ("Martyr_Cracked", 26, 6, 0.9, "regular"),
    ("Preacher", 32, 7, 0.8, "interactive"),
    ("Pilgrim_Broken", 12, 7, 0.6, "regular"),
    ("Runner_Maimed", 24, 7, 0.8, "regular"),
    ("Mirror", 80, 8, 0.4, "elite"),
    ("Martyr_Warden", 40, 8, 0.3, "regular"),
    ("Embracer_Dual", 38, 8, 0.5, "interactive"),
    ("Bearer_Twin", 34, 8, 0.5, "interactive"),
    ("Preacher_Silent", 38, 8, 0.35, "interactive"),
    ("Runner_Dual", 32, 9, 0.3, "regular"),
    ("Misstep_Dual", 40, 9, 0.3, "interactive"),
]


def intensity(p):
    if p < 0.20:
        return 0.6
    if p < 0.50:
        return 1.0
    if p < 0.70:
        return 0.3
    return 1.6


def wave_budget(n):
    return BASE_BUDGET * (1.0 + GROWTH_K * n)


def cheapest_at(wave):
    c = [x[1] for x in COSTS if x[2] <= wave]
    return min(c) if c else 999


def simulate(wave):
    """返回 (每 10% 区段的投放 cost, 花掉, 剩余)"""
    total = wave_budget(wave)
    remain = total
    t_remain = WAVE_DURATION
    credit = 0.0
    buckets = [0.0] * 10
    spent = 0.0
    floor_cost = cheapest_at(wave)

    t = 0.0
    while t_remain > 0:
        p = 1.0 - t_remain / WAVE_DURATION
        rate = remain * intensity(p) / t_remain if remain > 0 else 0.0
        credit += rate * DT
        # 攒够最便宜的型号就投；这里用平均 cost 近似，看的是节奏不是具体谁
        while credit >= floor_cost and remain >= floor_cost:
            cost = floor_cost
            credit -= cost
            remain -= cost
            spent += cost
            buckets[min(int(p * 10), 9)] += cost
        t_remain -= DT
        t += DT
    return buckets, spent, remain


print("=" * 74)
print("潮次  预算   花掉   剩余   ", end="")
print("  ".join("%d0%%" % i for i in range(10)))
print("-" * 74)
for wave in range(1, 11):
    b, spent, remain = simulate(wave)
    bars = "  ".join("%3.0f" % x for x in b)
    print("%2d   %5.0f  %5.0f  %5.0f    %s" % (wave, wave_budget(wave),
                                               spent, remain, bars))
print("=" * 74)
print("强度包络:  0-20%%=0.6   20-50%%=1.0   50-70%%=0.3(喘息)   70-100%%=1.6")
print()
# 喘息期是否真的看得出来：拿 50-70 两段和前后比
b, _s, _r = simulate(5)
calm = b[5] + b[6]
before = b[3] + b[4]
after = b[7] + b[8]
print("第 5 潮  喘息段(50-70%%) = %.0f   之前(30-50%%) = %.0f   之后(70-90%%) = %.0f"
      % (calm, before, after))
if calm < before * 0.6 and calm < after * 0.6:
    print("=> 喘息期在曲线上成立：投放量掉到前后的一半以下")
else:
    print("=> 喘息期不明显，包络需要重调")
