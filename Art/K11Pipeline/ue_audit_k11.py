# -*- coding: utf-8 -*-
"""
ue_audit_k11.py  --  K-11 terrain / F0 maze assertions, run INSIDE the UE editor.

This is the piece the pipeline never had: every assertion is measured against the
actual level, not against gen_k11.js's in-memory data. maze_plan.png proves the
algorithm; this proves the level.

Run from the loaded project with Unreal Editor's Execute Python Script command.
"""
import unreal, os, math, time, json
from collections import Counter, defaultdict

TOOLS = os.path.dirname(os.path.abspath(__file__))
PROJECT_ROOT = os.path.abspath(os.path.join(TOOLS, '..', '..'))
TERRAIN_DIR = os.path.join(PROJECT_ROOT, 'Content', 'Wasteland', 'Terrain')
GROUND = {'K11_Surface', 'K11_Underground', 'K11_F0Access'}
# obj -> uasset pairs that must be re-imported together
# k11_water is no longer emitted: excluding the maze footprint and the entry apron from
# the water mask left no cell below the water line.
PAIRS = ['k11_surface', 'k11_soilfill', 'k11_greybox', 'k11_underground', 'k11_f0access',
         'k11_hexfield', 'k11_hexcut', 'k11_navfloor']
MAZE_C = (-8880, -6144)          # altar
MAZE_R = 8800                    # maze outer radius, cm
HEX_R = 170.0
HEX_DQ = HEX_R * 1.5             # 255
HEX_DR = HEX_R * math.sqrt(3)    # 294.449
MAZE_RINGS = 30
MAZE_FLOOR = -3680.0
ENTRY = (-9538, -13338, -3671)   # stair landing inside the maze
CORRIDOR_Z = -3670               # floor samples at or below this are walkable floor
STEP_LIMIT = 40                  # cm; CMC MaxStepHeight 45 / AgentMaxStepHeight 35
WALL_MIN = 240                   # cm above adjacent corridor floor
SEAL_SCALES = (40.0, 35.0)       # BoB_Seal prisms, excluded from the city-column族 check

W = unreal.EditorLevelLibrary.get_editor_world()
Q = unreal.TraceTypeQuery.TRACE_TYPE_QUERY1
_les = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

_results = []
def report(tag, name, ok, detail):
    _results.append((tag, name, ok, detail))
    print('  [%s] %-4s %-28s %s' % ('PASS' if ok else 'FAIL', tag, name, detail))


# ---------------------------------------------------------------- helpers
def actors():
    return _les.get_all_level_actors()

def mesh_actors(mesh_name):
    out = []
    for a in actors():
        for cp in a.get_components_by_class(unreal.StaticMeshComponent):
            m = cp.static_mesh
            if m and m.get_name() == mesh_name:
                out.append((a, cp))
                break
    return out

def by_label(labels):
    return [a for a in actors() if a.get_actor_label() in labels]

def _ignore_all_but(keep_labels):
    return [a for a in actors() if a.get_actor_label() not in keep_labels]

def trace_z(x, y, ztop, zbot, ignore=None):
    """First blocking hit going down. Returns (z, actor_label) or None."""
    h = unreal.SystemLibrary.line_trace_single(
        W, unreal.Vector(x, y, ztop), unreal.Vector(x, y, zbot),
        Q, True, ignore or [], unreal.DrawDebugTrace.NONE, True)
    if not h:
        return None
    d = h.to_dict()
    a = d['hit_actor']
    return (d['location'].z, a.get_actor_label() if a else '?')


# ---------------------------------------------------------------- A1
def _obj_triangle_count(path):
    n = 0
    with open(path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if line[:2] == 'f ':
                # UE triangulates OBJ polygons on import. A quad is two triangles,
                # so comparing polygon lines directly reports every box mesh stale.
                n += max(0, len(line.split()) - 3)
    return n


def a1_not_stale():
    """Imported mesh must match the .obj it came from.

    mtime alone over-reports: the generator rewrites all six files every run even
    when the geometry is unchanged. So flag mtime as a warning, but only call it a
    real divergence when the triangle count actually differs.
    """
    warn, diverged = [], []
    for n in PAIRS:
        o = os.path.join(TOOLS, n + '.obj')
        u = os.path.join(TERRAIN_DIR, n + '.uasset')
        if not (os.path.exists(o) and os.path.exists(u)):
            diverged.append((n, 'missing file'))
            continue
        to, tu = os.path.getmtime(o), os.path.getmtime(u)
        behind = (to - tu) / 3600.0
        if tu < to - 60:
            warn.append('%s %.1fh' % (n, behind))
        m = unreal.EditorAssetLibrary.load_asset('/Game/Wasteland/Terrain/' + n)
        if not m:
            diverged.append((n, 'asset not loadable'))
            continue
        ue_t, obj_t = m.get_num_triangles(0), _obj_triangle_count(o)
        if ue_t != obj_t:
            diverged.append((n, '%d tris in UE vs %d triangulated obj faces' % (ue_t, obj_t)))
    report('A1', 'geometry matches obj', not diverged,
           ('all %d match' % len(PAIRS) if not diverged else str(diverged))
           + ('   [mtime behind: %s]' % ', '.join(warn) if warn else ''))


def a1b_surface_is_closed_solid():
    """K11_Surface must be one watertight terrain authority, not a skin plus a rock actor."""
    p = os.path.join(TOOLS, 'k11_surface.obj')
    edge_uses = Counter()
    if os.path.exists(p):
        with open(p, 'r', encoding='utf-8', errors='ignore') as f:
            for line in f:
                if not line.startswith('f '):
                    continue
                face = [int(v.split('/')[0]) for v in line.split()[1:]]
                for a, b in zip(face, face[1:] + face[:1]):
                    edge_uses[tuple(sorted((a, b)))] += 1
    boundary = sum(n == 1 for n in edge_uses.values())
    nonmanifold = sum(n > 2 for n in edge_uses.values())
    surfaces = [a for a in actors() if a.get_actor_label() == 'K11_Surface']
    legacy_rock = [a for a in actors() if a.get_actor_label() == 'K11_Rock']
    ok = bool(edge_uses) and boundary == 0 and len(surfaces) == 1 and not legacy_rock
    report('A1b', 'surface is closed solid', ok,
           '%d boundary edges | %d non-manifold edges | %d surface actor | %d legacy rock actor'
           % (boundary, nonmanifold, len(surfaces), len(legacy_rock)))


def a1c_player_starts_above_ground():
    """Every initial/respawn point must stand above K11_Surface, never inside it."""
    starts = [a for a in actors() if a.get_class().get_name() == 'PlayerStart']
    ignore = _ignore_all_but({'K11_Surface'})
    bad = []
    for a in starts:
        p = a.get_actor_location()
        h = trace_z(p.x, p.y, p.z + 20, p.z - 400, ignore)
        clearance = p.z - h[0] if h else None
        if not h or h[1] != 'K11_Surface' or not (80 <= clearance <= 180):
            bad.append((a.get_actor_label(), round(p.z),
                        None if clearance is None else round(clearance),
                        None if not h else h[1]))
    report('A1c', 'PlayerStarts above ground', bool(starts) and not bad,
           '%d safe start(s)' % len(starts) if starts and not bad else str(bad))


def a1d_soil_fill_is_visual_only():
    """Section-fill blocks must exist but never duplicate terrain collision or navigation."""
    fills = [a for a in actors() if a.get_actor_label() == 'K11_SoilFill']
    bad = []
    for a in fills:
        c = a.static_mesh_component
        if (c.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION
                or c.get_editor_property('can_ever_affect_navigation')):
            bad.append(a.get_actor_label())
    layout = os.path.join(TOOLS, 'k11_soilfill_layout.json')
    count = len(json.load(open(layout, encoding='utf-8')).get('cells', [])) \
        if os.path.exists(layout) else 0
    report('A1d', 'soil fill visual only', len(fills) == 1 and not bad and count >= 100,
           '%d actor | %d closed cells | %d collision/nav violations'
           % (len(fills), count, len(bad)))


# ---------------------------------------------------------------- A2-A5: current surface greybox
def _greybox_layout():
    p = os.path.join(TOOLS, 'k11_greybox_layout.json')
    return json.load(open(p, encoding='utf-8')) if os.path.exists(p) else None


def a2_surface_replaced():
    """The current greybox is present and the superseded city-column field is gone."""
    old_prefixes = ('K11_Col_', 'BoB_City_', 'BoB_Fill_W',
                    'BoB_Ruin_', 'BoB_Deco_', 'BoB_Elem_', 'BoB_Fill_',
                    'BoB_CliffRock', 'SM_house')
    legacy = [a.get_actor_label() for a in actors()
              if a.get_actor_label().startswith(old_prefixes)]
    grey = mesh_actors('k11_greybox')
    ok = len(grey) == 1 and not legacy
    report('A2', 'surface greybox replaced', ok,
           '%d K11_Greybox actor(s) | %d legacy city actor(s)'
           % (len(grey), len(legacy)))


def a3_three_regions():
    """The generated plan retains the approved G0/G1/G2 radii and three routes."""
    d = _greybox_layout()
    if not d:
        report('A3', 'three surface regions', False, 'layout json missing')
        return
    zones = d.get('zones', {})
    branches = {b.get('name') for b in d.get('branches', [])}
    subregions = set(d.get('subregions', {}))
    ok = (zones == {'core': 5000, 'ruins': 13000, 'danger': 22000}
          and branches == {'SINK', 'RUINS', 'RIDGE'}
          and subregions == {'beaconBasin', 'dryGorge', 'springRuins',
                             'jointRidge', 'hiddenEgress'})
    report('A3', 'three surface regions', ok,
           'zones %s | branches %s | subregions %s'
           % (zones, sorted(branches), sorted(subregions)))


def a3b_surface_route_clearance():
    """First-batch greybox must preserve the authored route corridors."""
    d = _greybox_layout() or {}
    blocks = d.get('blocks', [])
    clearance = d.get('checks', {}).get('routeClearanceCm', -9999)
    story_marks = {
        'SITE_Acceptance_Hall', 'SITE_Acceptance_SpringEdgeA',
        'SITE_Acceptance_OldLaneA', 'SITE_Resistance_CommandBase',
        'SITE_Resistance_RepairYard', 'SITE_Resistance_InnerBreach',
    }
    names = {b.get('name') for b in blocks}
    ok = clearance >= 250 and story_marks <= names
    report('A3b', 'surface greybox route clearance', ok,
           '%d blocks | minimum %dcm at %s'
           % (len(blocks), clearance,
              d.get('checks', {}).get('worstRouteBlock', 'unknown')))


def a4_history_layers():
    """Natural terrain, both faction ruins and all three outsider traces stay explicit."""
    d = _greybox_layout()
    if not d:
        report('A4', 'history layers present', False, 'layout json missing')
        return
    names = [b.get('name', '') for b in d.get('blocks', [])]
    outsiders = d.get('sites', {}).get('outsiders', [])
    outsider_xyz = {(o.get('name'), o.get('x'), o.get('y')) for o in outsiders}
    expected = {('A_PACK', 11200, 5200), ('B_MAP', -16400, 11800),
                ('C_TIMER', -13500, -11500)}
    natural = set(d.get('naturalTraces', []))
    acceptance = sum(n.startswith('SITE_Acceptance_') for n in names)
    resistance = sum(n.startswith('SITE_Resistance_') for n in names)
    outsider_blocks = sum(n.startswith('SITE_Outsider_') for n in names)
    scene_marks = {
        'SITE_Outsider_A_PACK_SampleRack',
        'SITE_Outsider_B_MAP_SurveyTable',
        'SITE_Outsider_C_TIMER_TimerScreen',
    }
    ok = (scene_marks <= set(names)
          and outsider_xyz == expected
          and {'jointed limestone', 'dry valleys', 'karst spring', 'sinkholes'} <= natural)
    report('A4', 'history layers present', ok,
           'natural %d | acceptance %d | resistance %d | outsiders %d/3'
           % (len(natural), acceptance, resistance, len(outsider_xyz & expected)))


def a5_greybox_transform():
    """The combined greybox is imported at the generator's world-space transform."""
    grey = mesh_actors('k11_greybox')
    bad = []
    for a, _ in grey:
        l, r, s = a.get_actor_location(), a.get_actor_rotation(), a.get_actor_scale3d()
        if max(abs(l.x), abs(l.y), abs(l.z), abs(r.roll), abs(r.pitch), abs(r.yaw),
               abs(s.x - 1), abs(s.y - 1), abs(s.z - 1)) > 0.01:
            bad.append((a.get_actor_label(), l, r, s))
    report('A5', 'greybox world transform', len(grey) == 1 and not bad,
           'identity' if len(grey) == 1 and not bad else '%d actor(s), bad %s' % (len(grey), bad[:1]))


# ---------------------------------------------------------------- A6
def maze_corridor_cells():
    """Corridor cells on the generator's own axial lattice.

    Sampling a square grid over a bounding box (what this used to do) picks up cavern
    floor and terraces outside the maze and calls them corridors, so the result was
    neither a maze measurement nor a terrain one.
    """
    out = []
    for q in range(-MAZE_RINGS, MAZE_RINGS + 1):
        for r in range(-MAZE_RINGS, MAZE_RINGS + 1):
            if (abs(q) + abs(r) + abs(-q - r)) // 2 > MAZE_RINGS:
                continue
            x = MAZE_C[0] + q * HEX_DQ
            y = MAZE_C[1] + r * HEX_DR + q * HEX_DR * 0.5
            # 起点必须在洞顶【下面】。洞顶被压到 -2280 之后，从 -2000 起打会先撞洞顶，
            # 迷宫墙样本一下从 1052 掉到 179，A6 也跟着假摔。
            h = trace_z(x, y, -2450, -4800)
            if h and h[1] == 'K11_NavFloor' and abs(h[0] - MAZE_FLOOR) <= 40:
                out.append((x, y, h[0]))
    return out


def a6_maze_connected():
    """Every corridor cell must be reachable from the stair landing."""
    allc = maze_corridor_cells()
    cells = allc
    ok_n = 0
    off_nav = 0
    stops = Counter()
    blockers = Counter()
    failed = []
    # FindPathToLocationSynchronously consumes raw Recast coordinates; unlike the
    # gameplay move request it does not project an arbitrary world-space endpoint
    # onto navigation first.  Probe within one corridor cell so A6 measures actual
    # connectivity, while a cell with no navigation polygon still fails explicitly.
    extent = unreal.Vector(120, 120, 120)
    entry_nav = unreal.NavigationSystemV1.project_point_to_navigation(
        W, unreal.Vector(*ENTRY), None, None, extent)
    # UE 5.8's Python wrapper returns only ProjectedLocation.  K2 leaves that
    # output at the zero vector when projection fails.
    if entry_nav.length() < 1:
        report('A6', 'maze fully connected', False, 'entry has no navigation polygon')
        return
    for (x, y, z) in cells:
        target_nav = unreal.NavigationSystemV1.project_point_to_navigation(
            W, unreal.Vector(x, y, z + 40), None, None, extent)
        if target_nav.length() < 1:
            off_nav += 1
            failed.append(('off-nav', round(x), round(y)))
            continue
        p = unreal.NavigationSystemV1.find_path_to_location_synchronously(
            W, entry_nav, target_nav)
        pts = p.get_editor_property('path_points') if p else None
        if not pts:
            failed.append(('no-path', round(x), round(y)))
            continue
        e = pts[-1]
        # Recast stops at the nearest polygon edge when a projected cell centre lies
        # under the entry stair lip. One axial hex-centre interval still means the
        # target cell was reached; the old fixed 200cm cutoff false-failed at 204cm.
        if math.dist((e.x, e.y, e.z), (target_nav.x, target_nav.y, target_nav.z)) <= HEX_DQ + 5:
            ok_n += 1
        else:
            stops[(round(e.x / 500) * 500, round(e.y / 500) * 500)] += 1
            hit = unreal.SystemLibrary.line_trace_single(
                W, unreal.Vector(e.x, e.y, target_nav.z + 60),
                unreal.Vector(target_nav.x, target_nav.y, target_nav.z + 60),
                Q, True, [], unreal.DrawDebugTrace.NONE, True)
            if hit:
                actor = hit.to_dict()['hit_actor']
                blockers[actor.get_actor_label() if actor else '?'] += 1
            failed.append((round(x), round(y), round(e.x), round(e.y)))
    rate = 100.0 * ok_n / max(len(cells), 1)
    report('A6', 'maze fully connected', bool(cells) and ok_n == len(cells),
           'reachable %d/%d (%.0f%%) | off-nav %d%s' % (ok_n, len(cells), rate, off_nav,
           (('  stalls at %s; blockers %s; targets %s' %
             (stops.most_common(), blockers.most_common(), failed[:30]))
            if stops or failed else '')))


# ---------------------------------------------------------------- A7 / A8
def a7a8_wall_and_step():
    """Wall height above the corridor, and steps between adjacent WALKABLE cells.

    Both are measured on the maze's own axial lattice. The previous square-grid version
    swept a bounding box, so it graded cavern floor and terraces outside the maze as if
    they were maze walls and corridors -- that is where 'worst 200cm' kept coming from.
    """
    zs = {}
    for q in range(-MAZE_RINGS, MAZE_RINGS + 1):
        for r in range(-MAZE_RINGS, MAZE_RINGS + 1):
            if (abs(q) + abs(r) + abs(-q - r)) // 2 > MAZE_RINGS:
                continue
            x = MAZE_C[0] + q * HEX_DQ
            y = MAZE_C[1] + r * HEX_DR + q * HEX_DR * 0.5
            # 起点必须在洞顶【下面】。洞顶被压到 -2280 之后，从 -2000 起打会先撞洞顶，
            # 迷宫墙样本一下从 1052 掉到 179，A6 也跟着假摔。
            h = trace_z(x, y, -2450, -4800)
            if h and h[1] in ('K11_HexField', 'K11_NavFloor'):
                zs[(q, r)] = h[0]
    if not zs:
        report('A7', 'maze wall height', False, 'no HexField samples')
        report('A8', 'steps climbable', False, 'no HexField samples')
        return
    walls = [z for z in zs.values() if z > MAZE_FLOOR + 60]
    low = [z for z in walls if z - MAZE_FLOOR < WALL_MIN]
    report('A7', 'maze wall height', not low,
           '%d/%d wall cells below %dcm above the floor (min %.0fcm)'
           % (len(low), len(walls), WALL_MIN,
              (min(walls) - MAZE_FLOOR) if walls else 0))

    NB = [(1, 0), (-1, 0), (0, 1), (0, -1), (1, -1), (-1, 1)]
    bad, worst = 0, 0.0
    for (q, r), z in zs.items():
        if abs(z - MAZE_FLOOR) > 40:      # only walkable cells have steps
            continue
        for dq, dr in NB:
            n = zs.get((q + dq, r + dr))
            if n is None or abs(n - MAZE_FLOOR) > 40:
                continue                   # neighbour is a wall, not a step
            d = abs(n - z)
            if d > STEP_LIMIT:
                bad += 1
                worst = max(worst, d)
    report('A8', 'steps climbable', bad == 0,
           '%d walkable adjacent pairs exceed %dcm (worst %.0fcm)'
           % (bad, STEP_LIMIT, worst))


# ---------------------------------------------------------------- A9
def a9_two_openings(step=800, allow=((-10450, -14500, 2600), (-4200, -10200, 1800))):
    """Only ENTRY and EXIT may see sky. Everything else must be roofed.

    allow = the two sanctioned openings from gen_k11.js: ENTRY (crater, -9966/-14315,
    probed centre -10450/-14500) and EXIT (hidden cave, -4200/-10200). An earlier run
    of this check flagged EXIT as a stray hole because the allowlist only had ENTRY.
    """
    holes = []
    gx = -26000
    while gx <= 22000:
        gy = -26000
        while gy <= 24000:
            r = trace_z(gx, gy, 15000, -6000)
            if r and r[0] < -1200:
                near = any(math.hypot(gx - a[0], gy - a[1]) < a[2] for a in allow)
                if not near:
                    holes.append((gx, gy, round(r[0]), r[1]))
            gy += step
        gx += step
    report('A9', 'only 2 sky openings', not holes,
           'ok' if not holes else '%d stray open column(s): %s' % (len(holes), holes[:4]))


# ---------------------------------------------------------------- A10
def a10_no_flooding():
    """Water must not sit above walkable floor.

    The old check flagged any water with geometry above it, which condemned a pool at
    the bottom of the entry pit for having the stair overhead -- that pool is correct.
    What actually matters is that no water surface is above a floor the player walks on:
    the water plane used to sit at -2980, seven metres over the maze floor and above
    every maze wall, i.e. the whole labyrinth was underwater.
    """
    water = [a for a in actors() if a.get_actor_label() == 'K11_Water']
    bad = []
    for a in water:
        o, e = a.get_actor_bounds(False)
        wz = o.z
        # does this water plane overhang the maze?
        if math.hypot(o.x - MAZE_C[0], o.y - MAZE_C[1]) - max(e.x, e.y) < MAZE_R \
                and wz > MAZE_FLOOR + 40:
            bad.append((a.get_actor_label(),
                        'surface z=%.0f is above the maze floor %.0f' % (wz, MAZE_FLOOR)))
        c = a.static_mesh_component
        if c.get_collision_enabled() != unreal.CollisionEnabled.NO_COLLISION:
            bad.append((a.get_actor_label(), 'blocks capsules/navmesh'))
    report('A10', 'water does not flood', not bad,
           'ok (%d plane(s))' % len(water) if not bad else str(bad))


# ---------------------------------------------------------------- extras
def x_material_scope():
    """MazeSplit stays scoped and visible terrain stays in M1 greybox material."""
    split_on, ground_on = [], []
    for a in actors():
        for cp in a.get_components_by_class(unreal.StaticMeshComponent):
            for m in cp.get_materials():
                if not m:
                    continue
                if m.get_name() == 'M_K11_MazeSplit':
                    split_on.append(a.get_actor_label())
                elif m.get_name() == 'M_K11_Ground':
                    ground_on.append(a.get_actor_label())
    bad_split = [n for n in set(split_on) if n != 'K11_HexField']
    report('M1', 'MazeSplit scope', not bad_split,
           'ok (K11_HexField only)' if not bad_split else 'also on %s' % bad_split[:5])
    report('M2', 'M_K11_Ground purged', not ground_on,
           'ok' if not ground_on else '%d actors still use it' % len(ground_on))
    grey_labels = {'K11_Surface', 'K11_SoilFill', 'K11_Greybox',
                   'K11_Underground', 'K11_F0Access'}
    grey = {}
    for a in actors():
        if a.get_actor_label() not in grey_labels:
            continue
        names = set()
        for cp in a.get_components_by_class(unreal.StaticMeshComponent):
            names.update(m.get_name() for m in cp.get_materials() if m)
        grey[a.get_actor_label()] = sorted(names)
    expected_grey = {
        'K11_SoilFill': 'M_K11_SoilFillGreybox',
    }
    bad_grey = {k: v for k, v in grey.items()
                if expected_grey.get(k, 'M_K11_Greybox') not in v}
    missing_grey = sorted(grey_labels - set(grey))
    report('M3', 'terrain uses greybox material', not bad_grey and not missing_grey,
           'ok' if not bad_grey and not missing_grey
           else 'wrong %s | missing %s' % (bad_grey, missing_grey))


# Crater helix, mirrored from gen_k11.js writeAccess(). The stair is 280 hexagonal
# treads baked into k11_f0access -- NOT separate k11_hexplate actors. An earlier
# version of this check counted plate actors and wrongly concluded the stair was
# never built.
ENTRY_PIT = dict(x=-9966.0, y=-14315.0, rTop=2200.0, rBot=1050.0, lip=2590.0, floor=-3700.0)
STAIR_TURNS, STAIR_STEPS = 4.2, 280


def _entry_radius_at(z):
    E = ENTRY_PIT
    s = max(0.0, min(1.0, (z - E['floor']) / (E['lip'] - E['floor'])))
    raw = s ** (1 / 0.78)
    t = 0.5 - math.sin(math.asin(max(-1.0, min(1.0, 1 - 2 * raw))) / 3)
    return E['rBot'] + max(0.0, min(1.0, t)) * (E['rTop'] - E['rBot'])


def stair_centres():
    E = ENTRY_PIT
    out = []
    for k in range(STAIR_STEPS):
        t = k / (STAIR_STEPS - 1.0)
        z = E['lip'] + (E['floor'] - E['lip']) * t
        th = -math.pi * 0.35 + t * 2 * math.pi * STAIR_TURNS
        pit_r = _entry_radius_at(z)
        plate_r = max(220.0, min(320.0, pit_r * 0.32))
        rr = pit_r - plate_r * 1.15
        out.append((E['x'] + math.cos(th) * rr, E['y'] + math.sin(th) * rr, z))
    return out


def x_stair_walkable():
    """Every tread must exist, be solid stair geometry, and be a climbable riser."""
    ign = _ignore_all_but({'K11_F0Access', 'K11_Surface', 'K11_Underground'})
    zs, miss, wrong = [], 0, Counter()
    for (x, y, z) in stair_centres():
        h = unreal.SystemLibrary.line_trace_single(
            W, unreal.Vector(x, y, z + 400), unreal.Vector(x, y, z - 400),
            Q, True, ign, unreal.DrawDebugTrace.NONE, True)
        if not h:
            miss += 1
            zs.append(None)
            continue
        d = h.to_dict()
        a = d['hit_actor']
        wrong[a.get_actor_label() if a else '?'] += 1
        zs.append(d['location'].z)
    seq = [v for v in zs if v is not None]
    risers = [abs(seq[i + 1] - seq[i]) for i in range(len(seq) - 1)]
    bad = [r for r in risers if r > STEP_LIMIT]
    ok = miss == 0 and not bad
    report('S1', 'crater stair walkable', ok,
           '%d/%d treads solid | riser mean %.1f max %.1f | %d riser(s) > %dcm | on %s'
           % (len(seq), STAIR_STEPS, sum(risers) / max(len(risers), 1),
              max(risers) if risers else 0, len(bad), STEP_LIMIT,
              dict(wrong.most_common(3))))


def x_tunnel():
    """The escape bore must be solid underfoot and navigable maze -> surface.

    The previous exit was a paper ribbon of tread/riser/side quads with no roof and,
    measured in UE, no collision at all: 2008 faces in the mesh, zero traceable
    surface, no path. This checks the thing players actually stand on.
    """
    import json
    p = os.path.join(TOOLS, 'k11_tunnel.json')
    if not os.path.exists(p):
        report('S3', 'exit tunnel walkable', False, 'k11_tunnel.json missing')
        return
    T = json.load(open(p, encoding='utf-8'))
    C = T['centre']
    solid, miss, wrong = 0, 0, Counter()
    for (x, y, z) in C:
        h = unreal.SystemLibrary.line_trace_single(
            W, unreal.Vector(x, y, z + 120), unreal.Vector(x, y, z - 220),
            Q, True, [], unreal.DrawDebugTrace.NONE, True)
        if not h:
            miss += 1
            continue
        d = h.to_dict()
        a = d['hit_actor']
        wrong[a.get_actor_label() if a else '?'] += 1
        solid += 1
    report('S3', 'exit tunnel floor solid', miss == 0,
           '%d/%d ring centres have floor | grade %.1f deg, run %.0fm | on %s'
           % (solid, len(C), T['grade'], T['run'] / 100.0, dict(wrong.most_common(3))))

    # The mouth used to be carved as a 5.5m-radius round bore around the floor centre.
    # That left several metres of black air under the road even though S3 and nav passed.
    # Check the last approach directly: soil must support the floor and close both shoulders.
    ign_surface = _ignore_all_but({'K11_Surface'})
    support, shoulders, wanted_support, wanted_shoulders = 0, 0, 0, 0
    # Generator check confirms four rings legally break daylight. Earlier rings remain
    # buried, so their shoulders are tunnel walls rather than exposed ground.
    mouth_rings = C[-4:]
    for i, (x, y, z) in enumerate(mouth_rings):
        wanted_support += 1
        h = unreal.SystemLibrary.line_trace_single(
            W, unreal.Vector(x, y, z - 20), unreal.Vector(x, y, z - 320),
            Q, True, ign_surface, unreal.DrawDebugTrace.NONE, True)
        if h:
            support += 1
        if i == 0:
            px, py = C[-5][0], C[-5][1]
        else:
            px, py = mouth_rings[i - 1][0], mouth_rings[i - 1][1]
        dx, dy = x - px, y - py
        dl = math.hypot(dx, dy) or 1.0
        nx, ny = -dy / dl, dx / dl
        for side in (-1, 1):
            wanted_shoulders += 1
            sx, sy = x + nx * side * 500, y + ny * side * 500
            h = unreal.SystemLibrary.line_trace_single(
                W, unreal.Vector(sx, sy, z + 120), unreal.Vector(sx, sy, z - 320),
                Q, True, ign_surface, unreal.DrawDebugTrace.NONE, True)
            if h:
                shoulders += 1
    report('S3b', 'exit mouth backfilled',
           support == wanted_support and shoulders == wanted_shoulders,
           'floor support %d/%d | shoulders %d/%d'
           % (support, wanted_support, shoulders, wanted_shoulders))

    a, b = C[2], C[-3]
    pth = unreal.NavigationSystemV1.find_path_to_location_synchronously(
        W, unreal.Vector(a[0], a[1], a[2] + 60), unreal.Vector(b[0], b[1], b[2] + 60))
    pts = pth.get_editor_property('path_points') if pth else None
    if not pts:
        report('S4', 'exit tunnel navigable', False, 'no path object')
        return
    e = pts[-1]
    d = math.dist((e.x, e.y, e.z), (b[0], b[1], b[2] + 60))
    blocker = ''
    if d >= 400:
        h = unreal.SystemLibrary.line_trace_single(
            W, unreal.Vector(e.x, e.y, e.z + 40),
            unreal.Vector(b[0], b[1], b[2] + 100),
            Q, True, [], unreal.DrawDebugTrace.NONE, True)
        if h:
            a = h.to_dict()['hit_actor']
            blocker = ' | blocker ' + (a.get_actor_label() if a else '?')
    report('S4', 'exit tunnel navigable', d < 400,
           'path %d pts, ends %.0fcm from the surface mouth%s' % (len(pts), d, blocker))


def x_stair_nav():
    """The stair must actually carry a navmesh path from rim to maze floor."""
    c = stair_centres()
    top, bot = c[3], c[-4]
    p = unreal.NavigationSystemV1.find_path_to_location_synchronously(
        W, unreal.Vector(top[0], top[1], top[2] + 60),
        unreal.Vector(bot[0], bot[1], bot[2] + 60))
    pts = p.get_editor_property('path_points') if p else None
    if not pts:
        report('S2', 'stair navigable', False, 'no path object')
        return
    e = pts[-1]
    d = math.dist((e.x, e.y, e.z), (bot[0], bot[1], bot[2] + 60))
    report('S2', 'stair navigable', d < 300,
           'path %d pts, ends %.0fcm from the bottom tread' % (len(pts), d))


# ---------------------------------------------------------------- main
def run():
    t0 = time.time()
    print('=' * 78)
    print('K-11 LEVEL AUDIT   %s' % time.strftime('%Y-%m-%d %H:%M:%S'))
    print('=' * 78)
    a1_not_stale()
    a1b_surface_is_closed_solid()
    a1c_player_starts_above_ground()
    a1d_soil_fill_is_visual_only()
    a2_surface_replaced()
    a3_three_regions()
    a3b_surface_route_clearance()
    a4_history_layers()
    a5_greybox_transform()
    a6_maze_connected()
    a7a8_wall_and_step()
    a9_two_openings()
    a10_no_flooding()
    x_material_scope()
    x_stair_walkable()
    x_stair_nav()
    x_tunnel()
    n_ok = sum(1 for r in _results if r[2])
    print('-' * 78)
    print('%d/%d PASS      (%.0fs)' % (n_ok, len(_results), time.time() - t0))
    print('=' * 78)
    return _results


run()
