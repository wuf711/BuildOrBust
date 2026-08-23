# -*- coding: utf-8 -*-
"""
重建 Font_Cute 的烘录字符表 = 现有表 ∪ HUD 源码里真正用到的字 ∪ 新增字。

为什么不能只往 chars 里加两个字就 Reimport：
这个字库现有 414 个字形，而 ImportOptions.Chars 只记了 257 字——
当初是拿更大的字符集烘的，chars 字段没同步。直接按 chars 重烘会把
多出来的字形全丢掉，HUD 里"货架只在波次间隙开放"这类文案会变成空白。
所以这里把源码里实际用到的汉字全捞出来并进去，重烘后只多不少。
"""
from pathlib import Path

import unreal

PROJECT_ROOT = Path(__file__).resolve().parents[1]

FONT = "/Game/Variant_Shooter/UI/Font_Cute"
# 所有可能上屏的文字都要覆盖到。漏一个文件，那个文件里的生僻字就是空白
SRC = [
    PROJECT_ROOT / "Source/BuildOrBust/Variant_Shooter/ShooterHUD.cpp",
    PROJECT_ROOT / "Source/BuildOrBust/Variant_Shooter/ShooterGameState.cpp",
    PROJECT_ROOT / "Source/BuildOrBust/Variant_Shooter/ShooterCharacter.cpp",
    PROJECT_ROOT / "Source/BuildOrBust/BoBShop.cpp",
    PROJECT_ROOT / "Source/BuildOrBust/LootPickup.cpp",
    PROJECT_ROOT / "Source/BuildOrBust/BoBEnergyPillar.cpp",
    PROJECT_ROOT / "Source/BuildOrBust/Boss_CS07.cpp",
    PROJECT_ROOT / "Source/BuildOrBust/BoBFalseRelic.cpp",
    PROJECT_ROOT / "Source/BuildOrBust/WaveManager.cpp",
]
ADD = "化潮"
LOG = PROJECT_ROOT / "Art/Export/_ue_font.log"

lines = []


def say(m):
    lines.append(str(m))
    unreal.log("[BoB] %s" % m)


def is_cjk(c):
    o = ord(c)
    return (0x4E00 <= o <= 0x9FFF        # 汉字
            or 0x3000 <= o <= 0x303F     # 中文标点
            or 0xFF00 <= o <= 0xFFEF)    # 全角


def from_source():
    """只取 TEXT("...") 里的字。注释里的汉字不能算——那会把字库撑爆"""
    got = set()
    for path in SRC:
        try:
            txt = open(path, encoding="utf-8").read()
        except Exception as e:
            say("读不了 %s: %s" % (path, e))
            continue
        i, n, hits = 0, len(txt), 0
        key = 'TEXT("'
        while True:
            i = txt.find(key, i)
            if i < 0:
                break
            i += len(key)
            j = txt.find('")', i)
            if j < 0:
                break
            hits += 1
            for c in txt[i:j]:
                if is_cjk(c):
                    got.add(c)
            i = j
        say("%s：扫到 %d 条 TEXT() 字面量" % (path.name, hits))
    return got


def main():
    f = unreal.EditorAssetLibrary.load_asset(FONT)
    opts = f.get_editor_property("import_options")
    cur = str(opts.get_editor_property("chars"))
    say("现状: chars 表 %d 字，实际字形 %d 个" % (
        len(cur), len(f.get_editor_property("characters"))))

    used = from_source()
    say("源码里用到的汉字/全角符号 %d 个" % len(used))
    newly = sorted(c for c in used if c not in cur)
    say("不在 chars 表里的 %d 个: %s" % (len(newly), "".join(newly)))

    extra = [c for c in ADD if c not in cur and c not in newly]
    merged = cur + "".join(newly) + "".join(extra)
    say("合并后 %d 字（新增 %d）" % (len(merged), len(merged) - len(cur)))

    opts.set_editor_property("chars", merged)
    f.set_editor_property("import_options", opts)
    ok = unreal.EditorAssetLibrary.save_asset(FONT)

    back = str(unreal.EditorAssetLibrary.load_asset(FONT)
               .get_editor_property("import_options").get_editor_property("chars"))
    say("存盘 %s，复读 %d 字" % (ok, len(back)))
    for c in "潮化谐补给站货架间隙配额背包弹药":
        say("   '%s' 在表里=%s" % (c, c in back))


try:
    main()
finally:
    LOG.parent.mkdir(parents=True, exist_ok=True)
    open(LOG, "w", encoding="utf-8").write("\n".join(lines))
