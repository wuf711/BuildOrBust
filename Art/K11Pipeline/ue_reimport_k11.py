# -*- coding: utf-8 -*-
"""
ue_reimport_k11.py  --  bring stale terrain assets back in sync with the generator.

Targeted on purpose. ue_f0_apply.py re-imports four meshes, re-drapes 5000 actors off
k11_delta.json, and slams M_K11_Ground onto everything -- running it wholesale would
undo the material split and move actors for nothing. Measured first:

  * k11_surface / k11_underground / k11_hexfield : tri count, vert count and bounds are
    identical to their .obj, and k11_delta.json is 0.0cm across all 57288 samples.
    Nothing to do -- only the mtime moved.
  * k11_f0access : 6572 tris in UE vs 7608 in the obj. This is the crater stair
    (writeAccess(), 280 hexagonal treads) plus the round-20 rewrite of the exit
    passage into a straight hand-cut ascent. Never imported.
  * k11_water : 1830 vs 2724.
  * k11_hexcut : same face count but Z extent 610 vs 700.

Materials follow the split in the pipeline notes: Basalt on terrain, MazeSplit only on
HexField, and M_K11_Ground is not used at all.

Run from the loaded project with Unreal Editor's Execute Python Script command.
"""
import unreal, os, time, math

TOOLS = os.path.dirname(os.path.abspath(__file__))
DEST = '/Game/Wasteland/Terrain'

# (obj, asset name, actor label, fallback material path)
# The fallback is only used when the actor does not exist yet. For an actor that is
# already in the level we KEEP whatever material it currently has -- material work is
# happening in parallel and re-import must not stomp it.
JOBS = [
    ('k11_surface.obj',     'k11_surface',     'K11_Surface',     '/Game/Wasteland/Materials/M_K11_Greybox'),
    ('k11_soilfill.obj',    'k11_soilfill',    'K11_SoilFill',    '/Game/Wasteland/Materials/M_K11_SoilFillGreybox'),
    ('k11_greybox.obj',     'k11_greybox',     'K11_Greybox',     '/Game/Wasteland/Materials/M_K11_Greybox'),
    ('k11_underground.obj', 'k11_underground', 'K11_Underground', '/Game/Wasteland/Materials/M_K11_Greybox'),
    ('k11_f0access.obj',    'k11_f0access',    'K11_F0Access',    '/Game/Wasteland/Materials/M_K11_Greybox'),
    ('k11_hexfield.obj',    'k11_hexfield',    'K11_HexField',    '/Game/Wasteland/Scan/M_K11_MazeSplit'),
    ('k11_navfloor.obj',    'k11_navfloor',    'K11_NavFloor',    '/Game/Wasteland/FX/M_BoBInvisible'),
    ('k11_hexcut.obj',      'k11_hexcut',      'K11_HexCut',      '/Game/Wasteland/Materials/M_K11_Cut'),
    # 远景地形：只挡视线，不挡人，也不参与导航
    ('k11_farfield.obj',    'k11_farfield',    'K11_FarField',    '/Game/Wasteland/Scan/M_K11_Scan'),
]
# k11_water 早已不再生成（迷宫足迹+入口排掉之后没有低于水线的格），旧的水面平面
# 曾经泡住 56 个采样点的迷宫走廊，必须删掉而不是留着指向陈旧网格。
# k11_crustfill 和各版独立 rock actor 都已被 K11_Surface 统一闭合土体取代。
DELETE_LABELS = ['K11_Water', 'K11_CrustFill', 'K11_Rock', 'K11_RockUpper', 'K11_RockLower']
DELETE_PREFIXES = ('K11_Col_', 'BoB_City_', 'BoB_Fill_W',
                   'BoB_Ruin_', 'BoB_Deco_', 'BoB_Elem_', 'BoB_Fill_',
                   'BoB_CliffRock', 'SM_house')
REDRAPE_PREFIXES = ('BoB_Loot2_', 'BoB_Floodlight_', 'BoB_Lore_', 'BoB_Prop_',
                    'BoB_Beacon_', 'BoB_Hermit', 'BoB_SupplyStation')
# Water is a look-only plane: it must never block a capsule or generate navmesh.
# It used to be BlockAll + affects-navigation, sitting 7m above the maze floor.
NO_COLLIDE = {'K11_Water', 'K11_FarField', 'K11_SoilFill'}
GREYBOX_MATERIAL_LABELS = {'K11_Surface', 'K11_SoilFill', 'K11_Greybox',
                           'K11_Underground', 'K11_F0Access'}

_les = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def ensure_soil_fill_material():
    """Keep the section-fill material visually identical to the terrain greybox.

    SoilFill is deliberately two-sided: an editor camera can sit inside any closed
    fill cell, where a normal one-sided material would cull every surrounding face.
    The shared terrain material stays one-sided.
    """
    src = '/Game/Wasteland/Materials/M_K11_Greybox'
    dst = '/Game/Wasteland/Materials/M_K11_SoilFillGreybox'
    mat = unreal.EditorAssetLibrary.load_asset(dst)
    if not mat:
        if not unreal.EditorAssetLibrary.duplicate_asset(src, dst):
            print('    !! could not create M_K11_SoilFillGreybox')
            return None
        mat = unreal.EditorAssetLibrary.load_asset(dst)
    mat.set_editor_property('two_sided', True)
    unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
    return mat


def import_obj(fn, name):
    t = unreal.AssetImportTask()
    t.filename = os.path.join(TOOLS, fn)
    t.destination_path = DEST
    t.destination_name = name
    t.automated = True
    t.save = True
    t.replace_existing = True
    ui = unreal.FbxImportUI()
    ui.import_mesh = True
    ui.import_textures = False
    ui.import_materials = False
    ui.import_animations = False
    ui.automated_import_should_detect_type = False
    ui.mesh_type_to_import = unreal.FBXImportType.FBXIT_STATIC_MESH
    ui.static_mesh_import_data.set_editor_property('combine_meshes', True)
    ui.static_mesh_import_data.set_editor_property('auto_generate_collision', False)
    t.options = ui
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([t])
    for p in t.imported_object_paths:
        a = unreal.load_asset(str(p))
        if isinstance(a, unreal.StaticMesh):
            bs = a.get_editor_property('body_setup')
            bs.set_editor_property('collision_trace_flag',
                                   unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            unreal.EditorAssetLibrary.save_loaded_asset(a)
            return a
    return None


def repoint(label, mesh, mat_path):
    """Point the existing actor at the re-imported mesh. Keeps the actor identity so
    nothing else in the level loses its reference."""
    acts = [a for a in _les.get_all_level_actors() if a.get_actor_label() == label]
    fresh = False
    if not acts:
        a = _les.spawn_actor_from_class(unreal.StaticMeshActor,
                                        unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
        a.set_actor_label(label)
        acts = [a]
        fresh = True
    for a in acts:
        c = a.static_mesh_component
        keep = [m for m in c.get_materials() if m is not None]
        c.set_editor_property('static_mesh', mesh)
        c.set_editor_property('mobility', unreal.ComponentMobility.STATIC)
        collide = label not in NO_COLLIDE
        affects_nav = collide
        c.set_editor_property('can_ever_affect_navigation', affects_nav)
        if not collide:
            c.set_collision_profile_name('NoCollision')
            c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        if keep and not fresh and label not in GREYBOX_MATERIAL_LABELS:
            for i in range(c.get_num_materials()):
                c.set_material(i, keep[min(i, len(keep) - 1)])
            shown = keep[0].get_name() + ' (kept)'
        else:
            mat = unreal.EditorAssetLibrary.load_asset(mat_path)
            if mat:
                a.modify()
                c.modify()
                mesh.modify()
                slot_count = max(1, c.get_num_materials(),
                                 len(mesh.get_editor_property('static_materials')))
                for i in range(slot_count):
                    mesh.set_material(i, mat)
                    c.set_material(i, mat)
                unreal.EditorAssetLibrary.save_loaded_asset(mesh, False)
            shown = (mat.get_name() if mat else 'NONE') + ' (fallback)'
        if label == 'K11_NavFloor':
            # 专用碰撞/导航面不参与画面，只给角色和 Recast 提供一张连续表面。
            c.set_editor_property('cast_shadow', False)
            c.set_editor_property('render_in_main_pass', False)
        o, e = a.get_actor_bounds(False)
        print('    %-16s tris=%-7d centre=(%.0f,%.0f,%.0f) extent=(%.0f,%.0f,%.0f) mat=%s%s'
              % (label, mesh.get_num_triangles(0), o.x, o.y, o.z, e.x, e.y, e.z,
                 shown, '' if collide else '  [NoCollision/no-nav]'))


def redrape_surface_gameplay():
    """Keep authored X/Y, but put retained gameplay and story actors on the new surface."""
    all_actors = _les.get_all_level_actors()
    surface = [a for a in all_actors if a.get_actor_label() == 'K11_Surface']
    if len(surface) != 1:
        print('    !! retained surface actors not draped: expected one K11_Surface')
        return
    world = unreal.EditorLevelLibrary.get_editor_world()
    ignore = [a for a in all_actors if a != surface[0]]
    moved = missed = 0
    for a in all_actors:
        label = a.get_actor_label()
        loc = a.get_actor_location()
        if not label.startswith(REDRAPE_PREFIXES):
            continue
        if math.hypot(loc.x, loc.y) > 30000 or loc.z < -1000:
            continue
        hit = unreal.SystemLibrary.line_trace_single(
            world, unreal.Vector(loc.x, loc.y, 12000),
            unreal.Vector(loc.x, loc.y, -5000), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
            True, ignore, unreal.DrawDebugTrace.NONE, True)
        if not hit:
            missed += 1
            continue
        ground_z = hit.to_dict()['location'].z
        origin, extent = a.get_actor_bounds(False)
        a.set_actor_location(unreal.Vector(loc.x, loc.y,
                                           loc.z + ground_z - (origin.z - extent.z) + 2),
                             False, True)
        moved += 1
    print('    redraped %d retained surface gameplay/story actors | missed %d'
          % (moved, missed))


def redrape_player_starts():
    """Put every spawn capsule above the current terrain.

    PlayerStarts are not retained props and were omitted from the first redrape pass.
    After the terrain rose around the core, all six starts remained inside K11_Surface;
    a pawn spawned inside one-sided complex collision and fell through the map.
    """
    all_actors = _les.get_all_level_actors()
    surface = [a for a in all_actors if a.get_actor_label() == 'K11_Surface']
    starts = [a for a in all_actors if a.get_class().get_name() == 'PlayerStart']
    if len(surface) != 1 or not starts:
        print('    !! PlayerStarts not draped: surface=%d starts=%d'
              % (len(surface), len(starts)))
        return
    world = unreal.EditorLevelLibrary.get_editor_world()
    ignore = [a for a in all_actors if a != surface[0]]
    moved = missed = 0
    for a in starts:
        loc = a.get_actor_location()
        hit = unreal.SystemLibrary.line_trace_single(
            world, unreal.Vector(loc.x, loc.y, 12000),
            unreal.Vector(loc.x, loc.y, -5000), unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,
            True, ignore, unreal.DrawDebugTrace.NONE, True)
        if not hit:
            missed += 1
            continue
        ground_z = hit.to_dict()['location'].z
        capsules = a.get_components_by_class(unreal.CapsuleComponent)
        half_height = max((c.get_scaled_capsule_half_height() for c in capsules), default=92.0)
        a.modify()
        a.set_actor_location(unreal.Vector(loc.x, loc.y, ground_z + half_height + 10),
                             False, True)
        moved += 1
    print('    redraped %d PlayerStarts | missed %d' % (moved, missed))


def run():
    # ---- 前置闸：编辑器世界没就绪就不许动 ----
    # 在 PIE 里、或地图没加载时，get_all_level_actors() 返回空。这种状态下脚本会把
    # 删除跑成空操作、把重摆跑成 spawn 返回 None，最后还照样调 save —— 有把
    # 关卡存成空的风险。实测踩到过两次（都没造成损失，但是运气）。
    _w = unreal.EditorLevelLibrary.get_editor_world()
    _n = len(_les.get_all_level_actors())
    if _w is None or _n < 100:
        print('  !! 中止：编辑器世界没就绪 (world=%s, actors=%d)。' % (_w, _n))
        print('     退出 PIE、确认打开的是 Lvl_Shooter，再跑一次。')
        return
    t0 = time.time()
    print('=' * 74)
    print('K-11 TARGETED RE-IMPORT')
    print('=' * 74)
    ensure_soil_fill_material()
    unreal.SystemLibrary.execute_console_command(None, 'Interchange.FeatureFlags.Import.OBJ false')
    for lbl in DELETE_LABELS:
        for a in [x for x in _les.get_all_level_actors() if x.get_actor_label() == lbl]:
            _les.destroy_actor(a)
            print('    removed %s (generator no longer emits it)' % lbl)
    old_surface = [a for a in _les.get_all_level_actors()
                   if a.get_actor_label().startswith(DELETE_PREFIXES)]
    for a in old_surface:
        _les.destroy_actor(a)
    print('    removed %d superseded surface actors for the greybox' % len(old_surface))
    for fn, nm, lbl, mat in JOBS:
        m = import_obj(fn, nm)
        if not m:
            print('    !! import failed: %s' % fn)
            continue
        repoint(lbl, m, mat)
    redrape_surface_gameplay()
    redrape_player_starts()
    # Lvl_Shooter 是 World Partition 地图；Recast 仍保持模板默认的非分区模式时，
    # Navigation Data Builder 虽会写出 chunk actors，正常编辑器却继续读取旧整图 NavMesh。
    # 把导航数据本身切到分区模式，之后由官方 WorldPartitionNavigationDataBuilder
    # 生成并持久化 chunks，地图和导航权威才是一致的。
    recast = [a for a in _les.get_all_level_actors()
              if a.get_class().get_name() == 'RecastNavMesh']
    if not recast:
        print('    !! no RecastNavMesh actor found; partitioned nav cannot be enabled')
    for nav in recast:
        if not nav.get_editor_property('is_world_partitioned'):
            nav.set_editor_property('is_world_partitioned', True)
            print('    enabled World Partition navigation on %s' % nav.get_actor_label())
        # The level spans roughly 2.6 km and stacks surface/cavern/maze navigation.
        # The template pool is sized for a small arena; once full it drops whole
        # vertical tile bands from the underground mesh.  Reserve enough streaming
        # tiles for the authored bounds instead of changing level geometry to hide it.
        nav.set_editor_property('fixed_tile_pool_size', True)
        if nav.get_editor_property('tile_pool_size') < 16384:
            nav.set_editor_property('tile_pool_size', 16384)
            print('    navigation tile pool: 16384')
    unreal.SystemLibrary.execute_console_command(
        unreal.EditorLevelLibrary.get_editor_world(), 'RebuildNavigation')
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
    print('  saved + nav rebuild requested   (%.0fs)' % (time.time() - t0))
    print('=' * 74)


if globals().get('K11_AUTORUN', True):
    run()
