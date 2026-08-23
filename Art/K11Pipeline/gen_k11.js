// gen_k11.js -- build K-11's two-layer terrain per 地形总纲_K11.md and write OBJ for UE.
//
// Province model (radius from the core, cm) and the causal chain behind each one:
//   P0  <3500   投射坐标   naturally flat-ish; the Bureau SELECTED it, did not grade it
//   P1  3500..9000   生土带   basin -- baseline field holds assimilation off, ground stays raw
//   P2  9000..18000  原住城区 gentle shelf the Venice fabric sits on, fed by spring line
//   P3  18000..26000 峰丛山地 karst towers: the map's relief comes from here
//   P4  >26000       封停环   ordered plateau, hard edge where assimilation stops
//   L2  -3000..-5500 地下层   smaller than the surface, reached only through collapses
//
// The sculptor is 同化, not water: assimilation ORDERS the ground. A(x,y) rises outward,
// so bedding gets more regular the further from the core you go. That gradient IS the
// difficulty read, and it is why the raw broken ground sits where the player defends.
'use strict';
const fs = require('fs');
const OUT = __dirname.replace(/\\/g, '/');
const D = JSON.parse(fs.readFileSync(OUT + '/hybrid_data.json', 'utf8'));

// --v1 reproduces the first K-11 surface (no city mask, single tower profile) and writes
// ONLY the height snapshot. Needed once, because that version was already applied to the
// level before the snapshot mechanism existed: without it the next delta would be measured
// from the original gY2 and every actor would be lifted a second time.
// ---- snapshot discipline ----
// The baseline for the re-drape delta is k11_height_applied.json = "the surface the level
// actually has right now". It is advanced ONLY by ue_f0_apply.py, after a successful
// import. The generator never touches it.
//
// The earlier design had the generator overwrite the baseline on every run, so running it
// twice silently zeroed the delta and left every actor draped on a surface that no longer
// existed. That cost four separate rounds before it got fixed properly.
const APPLIED = OUT + '/k11_height_applied.json';

const LEGACY = process.argv.includes('--v1');
// Reproduces the pre-fix assimilation field (centred on the core instead of the rift).
const OLD_A = process.argv.includes('--oldA');
// Reproduces the pre-fix bedding amount (tied to assimilation, so towers came out smooth).
const OLD_BED = process.argv.includes('--oldBed');
// Any --old* flag means "write the baseline and stop"; it must not emit meshes.
const BASELINE_ONLY = LEGACY || OLD_A || OLD_BED;
// When true, surface() stops before the entry pit and the exit mesa. Differencing the two
// gives exactly what those two features changed, which is the only re-drape still owed:
// the rest of the terrain is untouched by this pass.
let NOPIT = false;

const X0 = D.X0, X1 = D.X1, Y0 = D.Z0, Y1 = D.Z1;
const CELL = 200;
// Rock skin under every walkable surface. A heightfield with no thickness is invisible
// from underneath -- from inside the cave you look straight up through the ground, and
// every prop above reads as a flat cut-out. Ground has to be a solid.
const SKIN = 400;      // under the surface
const UG_SKIN = 260;   // under the cave floor and above the cave roof
const NX = Math.floor((X1 - X0) / CELL) + 1;
const NY = Math.floor((Y1 - Y0) / CELL) + 1;

// ---------------- noise ----------------
function h2(i, j, s) {
  let h = Math.imul(i * 374761393 + j * 668265263 + s * 1442695041, 1274126177);
  h = (h ^ (h >>> 13)) >>> 0;
  return h / 4294967295;
}
function vnoise(x, y, sc, s) {
  const fx = x / sc, fy = y / sc, i = Math.floor(fx), j = Math.floor(fy);
  const tx = fx - i, ty = fy - j;
  const sx = tx * tx * (3 - 2 * tx), sy = ty * ty * (3 - 2 * ty);
  const a = h2(i, j, s), b = h2(i + 1, j, s), c = h2(i, j + 1, s), d = h2(i + 1, j + 1, s);
  return (a * (1 - sx) + b * sx) * (1 - sy) + (c * (1 - sx) + d * sx) * sy;
}
function fbm(x, y, sc, oct, s) {
  let v = 0, a = 0.5, c = sc;
  for (let o = 0; o < oct; o++) { v += a * vnoise(x, y, c, s + o * 7); a *= 0.5; c *= 0.5; }
  return v;
}
const sm = t => { t = Math.max(0, Math.min(1, t)); return t * t * (3 - 2 * t); };
const band = (v, a, b) => sm((v - a) / (b - a));

// ---------------- tectonics: two joint sets set the grain of everything ----------------
const JA = 34 * Math.PI / 180, JB = -58 * Math.PI / 180;
function jointStrength(x, y) {
  const wa = 1400 * (fbm(x, y, 30000, 3, 11) - 0.5);
  const wb = 1400 * (fbm(x, y, 26000, 3, 29) - 0.5);
  const ua = (x * Math.sin(JA) - y * Math.cos(JA) + wa) / 6200;
  const ub = (x * Math.sin(JB) - y * Math.cos(JB) + wb) / 8100;
  const sharp = t => { const f = Math.abs(t - Math.round(t)) * 2; return Math.pow(1 - f, 3); };
  return Math.max(sharp(ua), sharp(ub) * 0.8);
}

// ---------------- assimilation field: the sculptor ----------------
// Rises with distance from the core (the baseline field holds it off near the core) and
// runs a little hotter along the joints, where the rock was already broken.
// The gradient radiates from the RIFT, not from the core.
//
// This was backwards at first: A was written as distance-from-core, which quietly claimed
// the core produced the gradient. It cannot. The Bureau projected the core in long after
// the landscape was already what it is; it did not make the assimilation, it PICKED a spot
// where the assimilation happened to be shallow. Causality runs rift -> ground -> siting,
// and writing it the other way round would have had the map arguing for something that
// never happened.
const RIFT = { x: -9000, y: -7000 };
function assimilation(x, y) {
  if (OLD_A) {
    // the wrong version, kept only so the "before" snapshot can be reproduced once
    const r = Math.hypot(x, y);
    let A0 = band(r, 6000, 24000);
    A0 += 0.18 * jointStrength(x, y) * band(r, 9000, 20000);
    A0 += 0.12 * (fbm(x, y, 22000, 3, 71) - 0.5);
    return Math.max(0, Math.min(1, A0));
  }
  const d = Math.hypot(x - RIFT.x, y - RIFT.y);
  let A = band(d, 3000, 30000);
  A = 1.0 - A;                       // deepest at the rift, fading outward
  A += 0.18 * jointStrength(x, y) * (1.0 - band(d, 8000, 26000));
  A += 0.12 * (fbm(x, y, 22000, 3, 71) - 0.5);
  return Math.max(0, Math.min(1, A));
}

// ---------------- bedding ----------------
// Ordered ground has regular beds; raw ground barely shows them. Same operator, the
// assimilation field just turns it up.
function strata(z, x, y, A) {
  if (A <= 0.02) return z;
  const step = 260 - 90 * A;                 // ordered rock beds thinner and more evenly
  const jitter = (1 - A) * step * 0.45 * (fbm(x, y, 5200, 2, 91) - 0.5) * 2;
  const t = (z + jitter) / step;
  const k = Math.floor(t), f = t - k;
  const riser = 0.55 - 0.18 * A;             // ordered = flatter treads, crisper risers
  const bandedZ = step * (k + sm(Math.min(1, f / riser)));
  return z * (1 - A * 0.85) + (bandedZ - jitter) * (A * 0.85);
}

// ---------------- where the settlement is ----------------
// Real karst towns sit on the flat plain BETWEEN the towers (Guilin, Guizhou), never on
// them. The Venice fabric is already placed, so the towers have to yield to it: sample
// the footprints into a coarse mask and suppress tower growth there. This is also what
// makes the settlement explicable instead of arbitrary -- the city is on the flat ground
// because the flat ground is what was left after everything else dissolved.
const MASK_CELL = 800;
const MNX = Math.ceil((X1 - X0) / MASK_CELL) + 1;
const MNY = Math.ceil((Y1 - Y0) / MASK_CELL) + 1;
const CITY = new Float32Array(MNX * MNY);
(function buildCityMask() {
  for (const b of D.B) {
    const p = b.p || [];
    if (p.length < 3) continue;
    let x0 = 1e9, x1 = -1e9, y0 = 1e9, y1 = -1e9;
    for (const q of p) { x0 = Math.min(x0, q[0]); x1 = Math.max(x1, q[0]); y0 = Math.min(y0, q[1]); y1 = Math.max(y1, q[1]); }
    for (let x = x0; x <= x1; x += MASK_CELL / 2) for (let y = y0; y <= y1; y += MASK_CELL / 2) {
      const i = Math.round((x - X0) / MASK_CELL), j = Math.round((y - Y0) / MASK_CELL);
      if (i >= 0 && j >= 0 && i < MNX && j < MNY) CITY[j * MNX + i] = 1;
    }
  }
  // dilate so towers back off gradually rather than ending in a cliff at the last wall
  for (let pass = 0; pass < 3; pass++) {
    const T = Float32Array.from(CITY);
    for (let j = 1; j < MNY - 1; j++) for (let i = 1; i < MNX - 1; i++) {
      const k = j * MNX + i;
      CITY[k] = Math.max(T[k], 0.78 * Math.max(T[k - 1], T[k + 1], T[k - MNX], T[k + MNX]));
    }
  }
})();
function cityMask(x, y) {
  const fx = (x - X0) / MASK_CELL, fy = (y - Y0) / MASK_CELL;
  const i = Math.max(0, Math.min(MNX - 2, Math.floor(fx)));
  const j = Math.max(0, Math.min(MNY - 2, Math.floor(fy)));
  const tx = Math.max(0, Math.min(1, fx - i)), ty = Math.max(0, Math.min(1, fy - j));
  const at = (a, b) => CITY[b * MNX + a];
  return (at(i, j) * (1 - tx) + at(i + 1, j) * tx) * (1 - ty)
       + (at(i, j + 1) * (1 - tx) + at(i + 1, j + 1) * tx) * ty;
}

// ---------------- karst towers (P3) ----------------
// Towers are what did NOT dissolve. Seeded on the joint lattice so they line up with the
// structural grain instead of being scattered noise.
function towerField(x, y) {
  const g = 5400;
  let best = 0;
  const ci = Math.round(x / g), cj = Math.round(y / g);
  for (let di = -1; di <= 1; di++) for (let dj = -1; dj <= 1; dj++) {
    const i = ci + di, j = cj + dj;
    const ox = (h2(i, j, 3) - 0.5) * g * 0.85;
    const oy = (h2(i, j, 5) - 0.5) * g * 0.85;
    const px = i * g + ox, py = j * g + oy;
    const rad = 1100 + h2(i, j, 7) * 1500;
    const hgt = 3600 + h2(i, j, 9) * 5600;          // 36..92m -- the map's relief
    const d = Math.hypot(x - px, y - py);
    if (d > rad * 2.2) continue;
    // vary the profile per tower: uniform cones read as a spike field, not as karst
    const shp = LEGACY ? 2.1 : (1.5 + h2(i, j, 13) * 1.9);
    const prof = Math.pow(Math.max(0, 1 - d / (rad * 2.2)), shp);
    best = Math.max(best, hgt * prof);
  }
  return best;
}

// ---------------- surface ----------------
function surface(x, y) {
  const r = Math.hypot(x, y);
  const A = assimilation(x, y);

  // province staircase: basin -> shelf -> mountains -> plateau
  let z = 0;
  z += 1500 * band(r, 3500, 9000);                       // P1 basin wall
  z += 1100 * band(r, 9000, 18000);                      // P2 shelf
  z += 2600 * band(r, 17000, 26000);                     // P3 base
  z += 4200 * band(r, 25000, 34000);                     // P4 plateau

  // broad relief + drainage-ish incision, aligned to the grain
  z += 2600 * (fbm(x, y, 21000, 5, 3) - 0.45);
  z -= 900 * jointStrength(x, y) * band(r, 5000, 30000);  // joints are where it wore down

  // karst towers only in P3 and beyond, and never under the settlement
  const cm = LEGACY ? 0 : cityMask(x, y);
  const twr = towerField(x, y) * band(r, 16000, 21000)
       * (1 - 0.55 * band(r, 30000, 38000)) * (1 - Math.min(1, cm * 1.15));
  // 高度场对每个 (x,y) 只能给出一个 z —— 垂直面和倒悬在数学上就表达不了，所以
  // 这些塔体一直是锥形，那不是参数没调好。塔体改由 writeTowers() 的等值面网格
  // 提供（真的能倒悬），高度场这里只留一个 28% 的缓坡基座把它们托起来。
  // 两者是重叠埋进去的关系，不拼接，也就不会有接缝裂口。
  z += LEGACY ? twr : twr * 0.28;

  // P0: the coordinate the Bureau picked -- relatively flat, NOT engineered flat
  const pad = 1 - band(r, 1800, 4200);
  z *= (1 - pad);
  z += pad * 130 * (fbm(x, y, 2600, 2, 33) - 0.5);        // +-65cm over the pad

  // Bedding belongs to the ROCK, not to assimilation. Tying the amount to A left every
  // karst tower a smooth cone, because the tower belt sits far from the rift where A is
  // near zero. Limestone is bedded everywhere; assimilation only makes the beds more
  // regular. Floor it at 0.45 and let A push it up from there.
  z = strata(z, x, y, (OLD_BED ? A : (0.45 + 0.50 * A)) * (1 - pad));

  if (NOPIT) return z;   // used to measure exactly what the pit and the mesa changed

  // 干涸河道：三条从山坡向盆地方向延伸的浅切槽。参考图明确「少量干涸河道」，是
  // 岁月痕迹，不是主干路径 —— 宽 8m，切深 90cm，符合"下过雨但断流了"的地质感。
  // 只在盆地外围 (r > 6000) 出现，避免切穿核心广场。
  if (!NOPIT && r > 6000 && r < 22000) {
    const RIVERS = [{ a: 0.31, wob: 0.7 }, { a: 2.1, wob: -0.4 }, { a: 4.7, wob: 0.3 }];
    for (const R of RIVERS) {
      // 河道从 r=6000 顺角度 a 向外延伸，允许一点蜿蜒
      const cx = Math.cos(R.a) * r + Math.sin(R.a) * (r * 0.06 * Math.sin(r / 3200 + R.wob));
      const cy = Math.sin(R.a) * r - Math.cos(R.a) * (r * 0.06 * Math.sin(r / 3200 + R.wob));
      const dist = Math.abs((x - Math.cos(R.a) * r * 0)); // fall through: 用向量到线距离
      const nx = -Math.sin(R.a), ny = Math.cos(R.a);      // 法向
      const perp = (x * nx + y * ny) + r * 0.06 * Math.sin(r / 3200 + R.wob);
      const d2 = Math.abs(perp);
      if (d2 < 400) {
        const cut = 90 * (1 - d2 / 400);
        z -= cut;
      }
    }
  }

  // ENTRY pit: a funnel carved into the surface, wide at the rim and narrowing down to
  // the gallery. Carving it INTO the heightfield (rather than punching a hole) is what
  // lets the switchback path hug a real wall, and it is how these collapses actually
  // look: a cone of debris-graded rock, not a drilled shaft.
  {
    const d = Math.hypot(x - ENTRY.x, y - ENTRY.y);
    const wob = 1 + 0.15 * Math.sin(Math.atan2(y - ENTRY.y, x - ENTRY.x) * 3 + 0.9);
    const rt = ENTRY.rTop * wob;
    if (d < rt) {
      const t = Math.max(0, Math.min(1, (d - ENTRY.rBot) / (rt - ENTRY.rBot)));
      const cone = ENTRY.floor + (ENTRY.lip - ENTRY.floor) * Math.pow(sm(t), 0.78);
      z = Math.min(z, cone);
    }
  }

  // EXIT: 隐蔽溶洞。参考图 4「原文明抵抗派逃生通道」明确要求"隐蔽、位置偏僻、
  // 后期需搜寻"，而不是外露的台地。原来的 +42m 大 mesa 从核心一眼就能看到，是
  // 反设定。改成"地面上一个 2m 深、3.5m 宽的小塌陷坑，坑心露出向下的楼梯口"，
  // 坑边缘做一圈石檐挡住核心方向的视线 —— 只有靠近走到坑沿才能看到入口。
  {
    const d = Math.hypot(x - EXIT.x, y - EXIT.y);
    // 出口周围压成一块【平地】，不是碗。
    // 三版演进：①半径 350/深 200 的浅坑 —— 洞口比周围低 165cm，MaxStepHeight 只有 45，
    // 人卡在洞口出不来。②半径 9m/深 4.2m 的碗 —— 25° 在数学上可走，但实机测下来
    // 玩家从地道口掉进碗里跳不出去：洞口和碗底之间仍有落差，一出洞就陷在坑里。
    // ③现在：出洞即平地，外圈平滑收回自然地形，没有任何需要爬的东西。
    if (EXIT.padZ !== undefined && d < EXIT_PAD_R + EXIT_PAD_BLEND) {
      const t = Math.max(0, Math.min(1, (d - EXIT_PAD_R) / EXIT_PAD_BLEND));
      const sm = t * t * (3 - 2 * t);          // smoothstep，收边和自然地面相切
      z = EXIT.padZ + (z - EXIT.padZ) * sm;
    }
    // 平台上再顺着地道挖一条放坡的堑口：中心压到地道底之下 60cm（让玩家踩在
    // 地道自己的地面上），两侧 15m 内按 ~16 度爬回平台。这样出洞是走上一段缓坡，
    // 而不是从洞口掉进一个孔里。
    // 放坡已停用：它把地表拉到离地道地面只差 1m 的位置，两个可走面叠在一起，
    // Recast 把导航建在上面那层(ring 281 导航 1111 / 地道 999)，底下的地道被孤立，
    // 寻路断在那儿。出口的洞已由 K11_ExitApron 实体填掉，放坡没有存在必要了。
    const cut = null;
    if (cut !== null) {
      const t2 = Math.max(0, Math.min(1, cut.d / TUN_CUT_R));
      // 中心线上【正好等于】地道地面，不是压低 60cm：
      // 差几十厘米就会在两个几乎重合的可走面之间留一道坎，实测 gap=+56 那一格
      // 正是导航断点，56 > MaxStepHeight 45，玩家也迈不过去。
      const target = cut.z + 5 + TUN_CUT_SIDE * Math.pow(t2, 1.3);
      z = z + (Math.min(z, target) - z) * cut.w;
    }
    // 塌陷坑北侧一小段石檐（阻挡从核心方向看进去）：宽 2m、高 +80cm
    const dx = x - EXIT.x, dy = y - EXIT.y;
    const angle = Math.atan2(dy, dx);
    const cover = Math.abs(angle - Math.PI / 2) < 0.55 && d > 300 && d < 550;
    if (cover) { z += 80; }
  }

  // ---- 地道口的下沉堑壕 ----
  // 让地道去顶穿地表，接缝永远对不齐：碗面横在洞口上、岩体又比地道地面低，
  // 实测寻路始终停在离出口几米处。现实里的平硐口不是顶穿的，是地面顺着坡挖下去。
  // 所以反过来做：地表沿着地道最后一段【往下让】，让出拱高，地道自然露天。
  // TUN_CENTRE 在 writeAccess 里才填，而 writeAccess 会调 surface() 求 EXIT 高度 ——
  // 那时候它还是空的，这段不生效，正好避开循环依赖（writeAccess 已排在 writeSurface 前）。
  if (TUN_CENTRE.length) {
    const gx = Math.floor(x / TUN_CS), gy = Math.floor(y / TUN_CS);
    const rad = Math.ceil(1500 / TUN_CS);
    let best = null, bd = 1e9;
    for (let i = -rad; i <= rad; i++) {
      for (let j = -rad; j <= rad; j++) {
        const arr = TUN_GRID && TUN_GRID.get((gx + i) + ',' + (gy + j));
        if (!arr) { continue; }
        for (const p of arr) {
          const d = Math.hypot(x - p[0], y - p[1]);
          if (d < bd) { bd = d; best = p; }
        }
      }
    }
    // 只在地道已经快到地表的那一段开堑壕，深处不动
    if (best && bd < 1500 && best[2] + 900 > z) {
      const t = Math.max(0, Math.min(1, (bd - 500) / 1000));
      const sm = t * t * (3 - 2 * t);
      const want = best[2] + 560;              // 拱高 400 + 余量
      z = Math.min(z, want + (z - want) * sm);
    }
  }

  return z;
}

// ---------------- 第一层塔体：三维密度场 + 等值面 ----------------
// 高度场是 z = f(x,y)：一个 (x,y) 只有一个 z，所以垂直面、倒悬、天生桥全都表达不了。
// 这里换成 f(x,y,z)，>0 是实体，等值面 f=0 就是岩面 —— 倒悬天然成立。
//
// 用 Surface Nets 而不是标准 Marching Cubes：等值面质量相当，但不需要那两张 256 项
// 查找表，代码短一半，输出的四边形也更平滑。每个有符号变化的体素出一个顶点，位置取
// 该体素十二条棱上零交点的平均；每条有符号变化的网格棱，连接它周围四个体素的顶点。
//
// 范围卡死在塔带的外接盒，不做全图体素化：P0/P1/P2/P4 本来就没有倒悬，高度场够用。
const TWR_CS = 200;              // 体素边长
const TWR_PAD = 2.6;             // 塔体影响半径相对 rad 的倍数

let _twrSeeds = null;
function towerSeeds() {
  if (_twrSeeds) { return _twrSeeds; }
  const g = 5400, out = [];
  const lim = Math.ceil(38000 / g) + 1;
  for (let i = -lim; i <= lim; i++) for (let j = -lim; j <= lim; j++) {
    const ox = (h2(i, j, 3) - 0.5) * g * 0.85, oy = (h2(i, j, 5) - 0.5) * g * 0.85;
    const px = i * g + ox, py = j * g + oy;
    const rr = Math.hypot(px, py);
    const belt = band(rr, 16000, 21000) * (1 - 0.55 * band(rr, 30000, 38000));
    if (belt <= 0.02) { continue; }
    const mask = 1 - Math.min(1, cityMask(px, py) * 1.15);
    if (mask <= 0.02) { continue; }
    const hgt = (3600 + h2(i, j, 9) * 5600) * belt * mask;
    if (hgt < 900) { continue; }
    out.push({ px: px, py: py, rad: 1100 + h2(i, j, 7) * 1500, hgt: hgt,
               shp: 1.5 + h2(i, j, 13) * 1.9, ph: h2(i, j, 17) * 6.283,
               baseZ: surface(px, py) });
  }
  _twrSeeds = out;
  return out;
}

// 底切的因果：同化沿层面从基岩往里啃，越靠近裂隙啃得越狠。所以凹槽是水平的（跟着
// 层面走）、越靠塔基越深（那里贴着基岩导管）、越靠近裂隙越明显。这不是水溶蚀的
// 蘑菇岩，是被格式化的物质从底下被抽走之后，上面没被同化的岩体悬在那儿。
function towerDensity(x, y, z, near) {
  let best = -1e9;
  for (let n = 0; n < near.length; n++) {
    const t = near[n];
    const u = (z - t.baseZ) / t.hgt;
    if (u > 1.02) { continue; }
    const dx = x - t.px, dy = y - t.py;
    const d = Math.sqrt(dx * dx + dy * dy);
    const uc = u < 0 ? 0 : (u > 1 ? 1 : u);
    let R = t.rad * 2.2 * (1 - Math.pow(uc, 1 / t.shp));
    const s = Math.sin(z / 620 * 6.283 + t.ph);
    const slot = s > 0 ? s * s * s : 0;                       // 只在层面正相位开槽，槽口锐利
    R *= 1 - 0.42 * assimilation(x, y) * slot * Math.exp(-(u > 0 ? u : 0) * 2.0);
    R *= 1 + 0.30 * (fbm(x + z * 0.8, y - z * 0.6, 1700, 3, 61) - 0.5);
    const v = R - d;
    if (v > best) { best = v; }
  }
  return best;
}

function writeTowers(path) {
  const seeds = towerSeeds();
  if (!seeds.length) { fs.writeFileSync(path, ''); return { towers: 0, verts: 0, quads: 0 }; }

  let bx0 = 1e9, bx1 = -1e9, by0 = 1e9, by1 = -1e9, zLo = 1e9, zHi = -1e9;
  for (const t of seeds) {
    const R = t.rad * TWR_PAD;
    bx0 = Math.min(bx0, t.px - R); bx1 = Math.max(bx1, t.px + R);
    by0 = Math.min(by0, t.py - R); by1 = Math.max(by1, t.py + R);
    zLo = Math.min(zLo, t.baseZ - 1800); zHi = Math.max(zHi, t.baseZ + t.hgt + TWR_CS * 2);
  }
  const gx = Math.floor(bx0 / TWR_CS) * TWR_CS, gy = Math.floor(by0 / TWR_CS) * TWR_CS;
  const gz = Math.floor(zLo / TWR_CS) * TWR_CS;
  const NIx = Math.ceil((bx1 - gx) / TWR_CS) + 2;
  const NIy = Math.ceil((by1 - gy) / TWR_CS) + 2;
  const NIz = Math.ceil((zHi - gz) / TWR_CS) + 2;

  // 逐列预筛：塔带是个环，外接盒里大半是空的。先算出每列附近有哪些塔、z 该从哪到哪，
  // 空列直接不进循环 —— 不做这一步就是几百万次白算。
  const colSeeds = new Array(NIx * NIy);
  const colLo = new Int32Array(NIx * NIy), colHi = new Int32Array(NIx * NIy);
  let active = 0;
  for (let i = 0; i < NIx; i++) for (let j = 0; j < NIy; j++) {
    const x = gx + i * TWR_CS, y = gy + j * TWR_CS;
    let near = null, lo = 1e9, hi = -1e9;
    for (const t of seeds) {
      const R = t.rad * TWR_PAD;
      if (Math.abs(x - t.px) > R || Math.abs(y - t.py) > R) { continue; }
      if (Math.hypot(x - t.px, y - t.py) > R) { continue; }
      (near || (near = [])).push(t);
      lo = Math.min(lo, t.baseZ - 1800); hi = Math.max(hi, t.baseZ + t.hgt + TWR_CS);
    }
    const k = i * NIy + j;
    colSeeds[k] = near;
    if (near) {
      active++;
      colLo[k] = Math.max(0, Math.floor((lo - gz) / TWR_CS));
      colHi[k] = Math.min(NIz - 1, Math.ceil((hi - gz) / TWR_CS));
    }
  }

  const P = new Float32Array(NIx * NIy);       // 当前 z 平面的密度
  const Q = new Float32Array(NIx * NIy);       // 上一个 z 平面
  const EMPTY = -1e9;
  P.fill(EMPTY); Q.fill(EMPTY);
  let vCur = new Int32Array((NIx - 1) * (NIy - 1)).fill(-1);
  let vPrev = new Int32Array((NIx - 1) * (NIy - 1)).fill(-1);

  const V = [], F = [];
  const cross = new Int32Array(NIx * NIy);
  const at = (i, j) => i * NIy + j;
  const cell = (i, j) => i * (NIy - 1) + j;

  for (let k = 0; k < NIz; k++) {
    const z = gz + k * TWR_CS;
    Q.set(P);                                                     // 上一个 z 平面滚下来
    for (let i = 0; i < NIx; i++) for (let j = 0; j < NIy; j++) {
      const c = at(i, j), s = colSeeds[c];
      P[c] = (s && k >= colLo[c] && k <= colHi[c]) ? towerDensity(gx + i * TWR_CS, gy + j * TWR_CS, z, s) : EMPTY;
    }
    // 每列沿 z 数一遍符号翻转。高度场对每个 (x,y) 只能给一个 z，所以它的每一列
    // 恒为 1 次翻转。>1 次就意味着这一列上"实体-空-实体"，也就是真的有倒悬 ——
    // 这是"等值面确实做到了高度场做不到的事"的证据，不是靠看图说的。
    if (k > 0) {
      for (let c = 0; c < NIx * NIy; c++) {
        if (colSeeds[c] && (Q[c] > 0) !== (P[c] > 0)) { cross[c]++; }
      }
    }
    const swap = vPrev; vPrev = vCur; vCur = swap; vCur.fill(-1);
    if (k === 0) { continue; }

    // 体素顶点：十二条棱上零交点的平均
    for (let i = 0; i < NIx - 1; i++) for (let j = 0; j < NIy - 1; j++) {
      const d = [Q[at(i, j)], Q[at(i + 1, j)], Q[at(i + 1, j + 1)], Q[at(i, j + 1)],
                 P[at(i, j)], P[at(i + 1, j)], P[at(i + 1, j + 1)], P[at(i, j + 1)]];
      let pos = 0;
      for (let m = 0; m < 8; m++) { if (d[m] > 0) pos++; }
      if (pos === 0 || pos === 8) { continue; }
      const CO = [[0,0,0],[1,0,0],[1,1,0],[0,1,0],[0,0,1],[1,0,1],[1,1,1],[0,1,1]];
      const ED = [[0,1],[1,2],[2,3],[3,0],[4,5],[5,6],[6,7],[7,4],[0,4],[1,5],[2,6],[3,7]];
      let sx = 0, sy = 0, sz = 0, n = 0;
      for (const [a, b] of ED) {
        const da = d[a], db = d[b];
        if ((da > 0) === (db > 0)) { continue; }
        const t = da / (da - db);
        sx += CO[a][0] + (CO[b][0] - CO[a][0]) * t;
        sy += CO[a][1] + (CO[b][1] - CO[a][1]) * t;
        sz += CO[a][2] + (CO[b][2] - CO[a][2]) * t;
        n++;
      }
      if (!n) { continue; }
      const wx = gx + (i + sx / n) * TWR_CS;
      const wy = gy + (j + sy / n) * TWR_CS;
      const wz = gz + (k - 1 + sz / n) * TWR_CS;
      V.push('v ' + wx.toFixed(1) + ' ' + (-wy).toFixed(1) + ' ' + wz.toFixed(1));
      vCur[cell(i, j)] = V.length;
    }

    // 每条有符号变化的网格棱，连接它周围四个体素的顶点成一个四边形。
    // OBJ 这边 Y 已取负（一次镜像），所以绕序整体反一次才朝外。
    const quad = (a, b, c2, dd, flip) => {
      if (a < 0 || b < 0 || c2 < 0 || dd < 0) { return; }
      if (flip) { F.push('f ' + a + ' ' + dd + ' ' + c2 + ' ' + b); }
      else { F.push('f ' + a + ' ' + b + ' ' + c2 + ' ' + dd); }
    };
    for (let i = 1; i < NIx - 1; i++) for (let j = 1; j < NIy - 1; j++) {
      // z 向棱：Q(i,j) -> P(i,j)，四个体素同在本层
      if ((Q[at(i, j)] > 0) !== (P[at(i, j)] > 0)) {
        quad(vCur[cell(i - 1, j - 1)], vCur[cell(i, j - 1)], vCur[cell(i, j)], vCur[cell(i - 1, j)],
             Q[at(i, j)] > 0);
      }
      // x/y 向棱必须取 Q 平面（本体素层的下底面），不能取 P。取 P 的话这条棱周围
      // 的四个体素里有两个属于还没算出来的上一层，四边形会连到 -1 上直接丢掉。
      // vPrev = 体素层 k-2（z 下侧），vCur = 体素层 k-1（z 上侧），两层都贴着 Q 平面。
      if ((Q[at(i, j)] > 0) !== (Q[at(i + 1, j)] > 0)) {
        quad(vPrev[cell(i, j - 1)], vPrev[cell(i, j)], vCur[cell(i, j)], vCur[cell(i, j - 1)],
             Q[at(i, j)] > 0);
      }
      if ((Q[at(i, j)] > 0) !== (Q[at(i, j + 1)] > 0)) {
        quad(vPrev[cell(i - 1, j)], vCur[cell(i - 1, j)], vCur[cell(i, j)], vPrev[cell(i, j)],
             Q[at(i, j)] > 0);
      }
    }
  }
  fs.writeFileSync(path, V.concat(F).join('\n'));
  let over = 0, solid = 0, maxCross = 0;
  for (let c = 0; c < NIx * NIy; c++) {
    if (!colSeeds[c] || !cross[c]) { continue; }
    solid++;
    if (cross[c] > maxCross) { maxCross = cross[c]; }
    if (cross[c] > 2) { over++; }        // >2 = 实体-空-实体，高度场表达不出来的那种
  }
  return { towers: seeds.length, verts: V.length, quads: F.length,
           grid: NIx + 'x' + NIy + 'x' + NIz, cols: active, colTot: NIx * NIy,
           zLo: gz, zHi: gz + NIz * TWR_CS,
           solidCols: solid, overhangCols: over, maxCross: maxCross };
}

// ---------------- layer two ----------------
// Smaller than the surface, SW of the core, its own relief, its own bedding.
const UG = { cx: -9000, cy: -7000, rx: 8600, ry: 7000, floor: -3600 };
// Three vertical connections, three different jobs:
//   ENTRY   a walkable collapse pit 174m from the core, switchback path down its wall.
//           This is the way IN. Falling off the path is the fall-damage design.
//   TK      the big tiankeng: light shaft and landmark, 46m straight down = a way to die,
//           not a way in.
//   EXIT    stairs up to a mesa top. One way only: you leave the mesa by a drop you
//           cannot climb back up, so it can never be used as a second entrance.
// floor 从 -3150 改到 -3700：LZ 竞技场的地面锁在 -3680，坑底停在 -3150 意味着
// 盘旋下来的栈道在离地 5.3m 的半空中就断了，下段周围什么都没有 —— 那不是"没做墙"，
// 是这条路根本没走到地面。坑底必须落到玩家真正会站上去的那层。
// rTop 从 3400 收到 2200：34m 半径的坑配 1.65m 的踏板，比值 1:20.6，台阶细得像根线。
// 参考图③ 的巨坑是紧凑的漏斗，螺旋阶梯占明显宽度。收窄后比值降到 1:7 左右。
const ENTRY = { x: -9966, y: -14315, rTop: 2200, rBot: 1050, lip: 2590, floor: -3700 };
const EXIT = { x: -4200, y: -10200, r: 900, mesa: 4200 };
const EXIT_PAD_R = 1800;      // 出口平台半径 18m：够站人、够转身、够放引导物
const EXIT_PAD_BLEND = 1600;  // 平台外圈用 16m 平滑收回自然地形，不留台坎
const TUN_CUT_R = 1500;       // 出口堑口的侧向半宽 15m
const TUN_CUT_SIDE = 430;     // 15m 外抬 4.3m => 侧坡约 16 度，随便哪个方向都能走上去
const TUN_CUT_RANGE = 2600;   // 只在出口 26m 内放坡（洞口无拱段 r<900，够用了）
const TUN_CUT_FADE  = 1100;   // 外侧 11m 平滑淡出，避免在边界上立一道断崖
// ---- 出口搬到地图的另外半边，并且落在自然低地上 ----
// 原来出口在 (-4200,-10200)，离 ENTRY(-9966,-14315) 只有 7.2km/100 = 72m —— 两个
// 通道几乎挨在一起，"从另一头钻出来"的意义完全没有。
// 而且逃生地道要"坡度极小"，坡度 = 爬升 / 跑长：跑长受地图尺寸限制，所以真正能压
// 坡度的手段是【少爬一点】——把出口放在自然地形的低洼处，而不是随便一个点。
// 这里在远离 ENTRY 的北半区扫一遍自然地表(NOPIT 模式，排除 pit/塌陷坑本身的影响)，
// 取最低点。EXIT 必须在 surface() 第一次被调用前定下来，因为 surface() 会在 EXIT
// 处刻那个隐蔽塌陷坑 —— 所以这里用 NOPIT 提前退出，避开循环依赖。
{
  const wasNoPit = NOPIT;
  NOPIT = true;
  let bz = Infinity, bx = EXIT.x, by = EXIT.y;
  for (let x = X0 + 7000; x <= X1 - 7000; x += 500) {
    for (let y = Y0 + 7000; y <= Y1 - 7000; y += 500) {
      if (Math.hypot(x - ENTRY.x, y - ENTRY.y) < 26000) { continue; }  // 必须在另外半边
      if (Math.hypot(x, y) < 11000) { continue; }                      // 别贴着核心广场
      const z = surface(x, y);
      if (z < bz) { bz = z; bx = x; by = y; }
    }
  }
  NOPIT = wasNoPit;
  EXIT.x = bx; EXIT.y = by; EXIT.naturalZ = bz;
  // 平台高度：比自然地面略低一点，读起来像被清理平整过的一小片场地
  EXIT.padZ = bz - 120;
}
// 自然地表 z（不含 pit/mesa 修改），楼梯顶要落在这里下方一点，坑底才露口
let EXIT_NATURAL_Z = 0;    // 生成器一进入就填，见下
// 斜向上通道(逃生步道)的起点。迷宫外墙必须在这里留缺口，否则玩家出不去。
// 值在 writeAccess() 里算出来后回填，但迷宫在它之后才建，所以能读到。
let EXIT_RAMP_X = 0, EXIT_RAMP_Y = 0;
// 逃生地道的中心线（writeAccess 里填），writeHexField 要照它把迷宫让开：
// 地道刚出洞口的十几环还在迷宫墙的高度带里，迷宫墙会直接把地道堵死。
let TUN_PATH = [];
const LOBES = [
  { cx: UG.cx, cy: UG.cy, rx: UG.rx, ry: UG.ry },        // the reveal chamber
  { cx: -9600, cy: -11000, rx: 3000, ry: 4600 },          // gallery down to the entry
  { cx: ENTRY.x, cy: ENTRY.y, rx: 3600, ry: 3600 },       // the entry pit floor
  { cx: EXIT.x, cy: EXIT.y, rx: 1500, ry: 1500 },         // foot of the exit stair
  { cx: -6600, cy: -9200, rx: 2600, ry: 2600 },           // gallery to the exit
];
function ugField(x, y) {
  let best = -1;
  for (const L of LOBES) {
    const dx = (x - L.cx) / L.rx, dy = (y - L.cy) / L.ry;
    const a = Math.atan2(dy, dx);
    const wob = 1 + 0.22 * Math.sin(a * 3 + L.cx * 0.0004) + 0.12 * Math.sin(a * 5 - L.cy * 0.0006);
    best = Math.max(best, 1 - Math.hypot(dx, dy) / wob);
  }
  return best;
}
function ugFloor(x, y) {
  const f = Math.max(0, Math.min(1, ugField(x, y)));
  let z = UG.floor + 1500 * Math.pow(1 - f, 1.3);
  z += 620 * (fbm(x, y, 9000, 4, 41) - 0.5);
  z += 240 * (fbm(x, y, 3100, 3, 43) - 0.5);
  const dTk = Math.hypot(x - TK.x, y - TK.y);
  z += 900 * Math.exp(-Math.pow(dTk / (TK.r * 0.9), 2));   // debris cone under the collapse
  return strata(z, x, y, 0.55);
}
// the collapse: where the chamber is tallest the roof failed -- the tiankeng places itself
const TK = { x: -9600, y: -6200, r: 2400 };
// 参考图规定：第一层和第二层之间只有山顶巨坑(ENTRY)和隐蔽溶洞出口(EXIT)两条通道，
// 没有其他露天洞。这两个 swallow(塌陷天窗)会在洞顶捅出通天窟窿，是多余的第三、第四
// 个入口，清空。地下层由此成为真正封闭的空间 —— 这也是迷宫作为关卡成立的前提：
// 玩家不能从天而降跳过迷宫直达中心。
const SWALLOWS = [];
function ugCeil(x, y) {
  // 基础挑高从 1900 压到 1400、起伏从 700 压到 400：原值让洞顶在地形低洼处顶穿地表，
  // 造成大量非法露天洞(11% 开天)。参考图只允许两个通道，洞顶必须整体在地表之下。
  let h = 1400 + 400 * Math.sin(x / 7400 - 0.4) * Math.sin(y / 6100 + 1.2);
  const dTk = Math.hypot(x - TK.x, y - TK.y);
  // 天坑原本把洞顶抬高 42m，直接顶穿地表形成第三个露天洞。参考图规定第一层和
  // 第二层之间只有山顶巨坑(ENTRY)和隐蔽溶洞(EXIT)两条通道，所以这里只保留一个
  // 温和的挑高(12m)让主厅有体量，但绝不允许触到地表。
  h += 1200 * Math.exp(-Math.pow(dTk / TK.r, 4));
  // 3800 not 2600: at 2600 the second swallow's roof stopped ~5m short of daylight,
  // so it was a bulge in the ceiling rather than a door. A hole that does not open is
  // worse than no hole -- it reads as geometry error, not as terrain.
  // 5600, raised twice now: every time the lobes or the surface move, a fixed spike
  // stops reaching daylight and the hole silently seals. Size it off the ground above.
  for (const s of SWALLOWS) {
    const need = Math.max(3800, surface(s.x, s.y) - ugFloor(s.x, s.y) - 1900 + 900);
    h += need * Math.exp(-Math.pow(Math.hypot(x - s.x, y - s.y) / s.r, 3.4));
  }
  // 洞顶高度保持自然形态，不做任何限高。
  // 2026-08-12 我一度把迷宫头顶压到地面上方 8.2m、非迷宫区压到 10m —— 用户明确否决并
  // 要求回退。"填实一二层之间"指的是【洞顶以上到地表之间】要是实心岩土(crustBottom
  // 负责)，不是把第二层压矮。第二层的净空是设计，不是多余体积。
  return ugFloor(x, y) + h;
}
// 开洞判据必须是白名单，不能是"洞顶碰到地表就开"。
// 后者是涌现式的：洞体一旦延伸到地表低洼处就自动开天窗，参考图规定的"只有两个通道"
// 根本守不住(实测仍有 10% 开天、地表出现两个非法大洞)。
// 改成显式白名单：只有山顶巨坑(ENTRY)和隐蔽溶洞(EXIT)两处开口，其余一律封死岩层。

// 地道破土处：地表要沿着地道开一条槽，不能只在出口开一个圆孔。
// UE 实测：只开半径 320 的孔时，第 296/297 环脚下踩的是 K11_Surface(碗面)，
// 比地道地面高 181cm —— 地道从底下爬上来，一头撞在碗面上，寻路到此为止。
// 判据要带深度条件：只有地道快顶到地表了的那几环才开槽，否则 310m 全程
// 都会被开成一条露天沟。
// 出口放坡：返回附近地道中心线上最近的一点 {d, z}，只在出口那一带找。
// 地道口不能靠"在地表上开个孔"来露出来 —— 孔是垂直壁的，孔底是岩体顶面
// (地表 − SKIN)，实测洞口 1495 / 孔底 944 / 平台 1345：人出洞先掉 5.5m
// 进一个四壁垂直的 4m 深坑，爬不出来。
// 正确做法是把地表顺着地道【放坡】降下去，像修路开的堑口一样。
function tunnelCutNear(x, y) {
  if (!TUN_GRID) { return null; }
  // 作用范围必须【平滑淡出】，不能到边界一刀切。
  // 硬开关那版实测：ring 266 地表 1658、267 掉到 944、268 掉到 519 —— 一格之内
  // 铲掉 7 米，边界上立了一道垂直断崖，导航正好断在那儿（ring 268）。
  const dEx = Math.hypot(x - EXIT.x, y - EXIT.y);
  if (dEx > TUN_CUT_RANGE) { return null; }
  const wRange = Math.max(0, Math.min(1, (TUN_CUT_RANGE - dEx) / TUN_CUT_FADE));
  const gx = Math.floor(x / TUN_CS), gy = Math.floor(y / TUN_CS);
  const rad = Math.ceil(TUN_CUT_R / TUN_CS);
  let best = null;
  for (let i = -rad; i <= rad; i++) {
    for (let j = -rad; j <= rad; j++) {
      const arr = TUN_GRID.get((gx + i) + ',' + (gy + j));
      if (!arr) { continue; }
      for (const p of arr) {
        const d = Math.hypot(x - p[0], y - p[1]);
        if (d > TUN_CUT_R) { continue; }
        if (best === null || d < best.d) { best = { d: d, z: p[2], w: wRange }; }
      }
    }
  }
  return best;
}

function tunnelDaylight(x, y) {
  if (!TUN_GRID) { return false; }
  // 出口那一带一律不开孔：孔是垂直壁的，孔底是岩体顶面(地表-SKIN)，
  // 实测洞口 1495 / 孔底 944 / 平台 1345 —— 出洞掉 5.5m 进一个爬不出来的坑。
  // 那一段交给 surface() 里的放坡堑口(tunnelCutNear)去接。
  // 放坡区内：地表已经被降到地道地面高度，这时开孔露出管子是无缝的（两边同高）。
  // 只有当地表还明显高于地道地面时才不开 —— 那说明地道还埋着，开孔会露出
  // 岩体顶面(地表-SKIN)形成深坑。
  const gx = Math.floor(x / TUN_CS), gy = Math.floor(y / TUN_CS);
  for (let i = -1; i <= 1; i++) {
    for (let j = -1; j <= 1; j++) {
      const arr = TUN_GRID.get((gx + i) + ',' + (gy + j));
      if (!arr) { continue; }
      for (const p of arr) {
        if (Math.hypot(x - p[0], y - p[1]) > 440) { continue; }
        if (p[2] + 520 > surface(x, y) - SKIN) { return true; }
      }
    }
  }
  return false;
}
const ugOpen = (x, y) =>
  // 山顶巨坑：漏斗形塌陷，玩家沿六边形螺旋阶梯下到负一层
  (Math.hypot(x - ENTRY.x, y - ENTRY.y) < ENTRY.rBot * 1.05)
  // 隐蔽溶洞：单向出口，从负一层返回地表
  // 出口不再开孔：孔是垂直壁的，孔底是岩体顶面，人出洞就掉进 4m 深坑爬不出来。
  // 改成 surface() 里沿地道放坡的堑口（tunnelCutNear），地表自己降下去接上洞口。
  || tunnelDaylight(x, y);

// ---------------- the three ways between the layers ----------------
// Inverse of the entry cone: given a height, how wide is the pit there. Needed so the
// path can hug the wall instead of hanging in the middle of the hole.
function entryRadiusAt(z) {
  const s = Math.max(0, Math.min(1, (z - ENTRY.floor) / (ENTRY.lip - ENTRY.floor)));
  const raw = Math.pow(s, 1 / 0.78);
  // invert smoothstep
  const t = 0.5 - Math.sin(Math.asin(Math.max(-1, Math.min(1, 1 - 2 * raw))) / 3);
  return ENTRY.rBot + Math.max(0, Math.min(1, t)) * (ENTRY.rTop - ENTRY.rBot);
}

function writeAccess(path) {
  const V = [], VP = [], F = [];
  const push = (x, y, z) => {
    // OBJ 只保留一位小数，面积判断必须使用同一组量化后的坐标；否则原始点略有
    // 差异、落盘后却重合时，仍会写出 UE 导入器必然丢弃的零面积面。
    const p = [+x.toFixed(1), +(-y).toFixed(1), +z.toFixed(1)];
    VP.push(p);
    V.push('v ' + p[0].toFixed(1) + ' ' + p[1].toFixed(1) + ' ' + p[2].toFixed(1));
    return V.length;
  };
  const tri = (a, b, c) => {
    const A = VP[a - 1], B = VP[b - 1], C = VP[c - 1];
    const ux = B[0] - A[0], uy = B[1] - A[1], uz = B[2] - A[2];
    const vx = C[0] - A[0], vy = C[1] - A[1], vz = C[2] - A[2];
    const cx = uy * vz - uz * vy, cy = uz * vx - ux * vz, cz = ux * vy - uy * vx;
    if (cx * cx + cy * cy + cz * cz > 1e-8) { F.push('f ' + a + ' ' + b + ' ' + c); }
  };
  // 栈道和楼梯的四边形是路面片段，不是高度场晶格，对角线不参与六边形化
  const quad = (a, b, c, d) => { tri(a, c, b); tri(a, d, c); };

  // ---- switchback path down the entry pit ----
  // Real tiankeng access looks exactly like this (Xiaozhai's stair path). Landings every
  // half turn give somewhere to stand and fight; the drop off the outer edge is the
  // fall-damage the brief asked for.
  // 参考图②③：山顶巨坑内部的螺旋阶梯由六边形板相邻拼接而成，沿山体内壁蜿蜒，
  // 不可穿越、不悬空。所以不再用连续的斜坡带(那读起来是一条工业栈道)，改成一块块
  // 独立的六边形踏板：每块板绕坑心转一小步、降一级，外沿嵌进岩壁，相邻板边缘相接。
  const TURNS = 4.2, STEPS = 280, THICK = 46;   // 坑收窄后周长变短，加圈数把坡度压回 8度左右
  // 踏板半径必须随坑半径收缩：坑是上宽下窄的漏斗，固定 190 的板在下段会啃进
  // 对面的坑壁(穿模)，在上段又显得太小(比例失衡)。改成按当前坑半径的比例取。
  const PLATE_R_MAX = 320, PLATE_R_MIN = 220;   // 踏板对边宽 3.8..5.5m，是能站人打枪的平台
  let pathCells = 0, maxDrop = 0;
  for (let k = 0; k < STEPS; k++) {
    const t = k / (STEPS - 1);
    const z = ENTRY.lip + (ENTRY.floor - ENTRY.lip) * t;
    const th = -Math.PI * 0.35 + t * Math.PI * 2 * TURNS;
    maxDrop = Math.max(maxDrop, z - ENTRY.floor);
    // 踏板中心贴着坑壁：半径 = 该高度的坑半径 - 板半径，外沿正好啃进壁里
    const pitR = entryRadiusAt(z);
    // 板宽 = 坑半径的 22%，钳在 105..165 之间：上段宽敞、下段收窄，比例跟着坑走
    const PLATE_R = Math.max(PLATE_R_MIN, Math.min(PLATE_R_MAX, pitR * 0.32));
    const rr = pitR - PLATE_R * 1.15;
    const cx = ENTRY.x + Math.cos(th) * rr;
    const cy = ENTRY.y + Math.sin(th) * rr;
    // 六边形踏板：顶面 + 底面 + 六个侧面。板与板高度差约 33cm，是可走的台阶。
    const T = [], B = [];
    for (let i = 0; i < 6; i++) {
      const a = Math.PI / 3 * i + th;    // 板随螺旋方向转，边缘才能相接
      const px = cx + Math.cos(a) * PLATE_R, py = cy + Math.sin(a) * PLATE_R;
      T.push(push(px, py, z));
      B.push(push(px, py, z - THICK));
    }
    for (let i = 1; i < 5; i++) { F.push('f ' + T[0] + ' ' + T[i + 1] + ' ' + T[i]); }
    for (let i = 1; i < 5; i++) { F.push('f ' + B[0] + ' ' + B[i] + ' ' + B[i + 1]); }
    for (let i = 0; i < 6; i++) {
      const j = (i + 1) % 6;
      F.push('f ' + T[i] + ' ' + B[j] + ' ' + B[i]);
      F.push('f ' + T[i] + ' ' + T[j] + ' ' + B[j]);
    }
    pathCells++;
  }
  const grade = Math.atan((ENTRY.lip - ENTRY.floor) /
    (2 * Math.PI * ((ENTRY.rTop + ENTRY.rBot) / 2) * TURNS)) * 180 / Math.PI;

  // ---- 斜向上通道（出口）: 在实心地壳里挖出来的一条粗糙歪斜地道 ----
  // 上一版是"踏面+踢面+两片侧壁"拼出来的一条【纸片带】：没有顶、没有体积，
  // 而且 UE 实测那 2008 个面【一点碰撞都没有】—— 沿设计中心线全程射线打空，
  // 从迷宫寻路到地表出口直接无解。玩家根本出不去。
  //
  // 这一版按"在土里挖出来的洞"来做：沿路径扫一个不规则的拱形断面 —— 底下是一条
  // 平的可走地面，两侧和顶是半径带噪声的粗糙岩壁，首尾各自敞口。它是一个闭合的
  // 管子，有厚度、有顶、有碰撞。
  //
  // 坡度：用户要"较长、坡度极小"。坡度 = 爬升/跑长，跑长受地图尺寸限制，所以
  // 一靠把出口放在自然低地(见 EXIT 选点)少爬一点，二靠把路径做成外凸的二次贝塞尔
  // 把跑长撑长。下面按目标坡度反解需要的跑长，再二分那个外凸量。
  const TUN_GRADE_DEG = 7.5;                   // 目标坡度
  // 地面半宽 2.6m（宽 5.2m）。原来 1.9m 在几何上完全够走(AgentRadius 才 35)，但
  // 逐环扫下来总有一两处相邻环连不上 —— 几何、净空、坡度全都正常，是 Recast 分块
  // 生成时窄长斜走廊卡在 tile 边界上没连起来。而地道是一条链，断一环后面全失联。
  // 把断面加宽给 Recast 留足余量，比去调全局 navmesh 分辨率代价小得多。
  const TUN_FLOOR_HW = 260;
  const TUN_ARCH = 400;                        // 拱顶净高（AgentHeight 144）
  const TUN_K = 9;                             // 断面上拱的分段数
    // 必须用平台高度，不能用 EXIT_NATURAL_Z：后者是在 NOPIT=true 时算的
  // "自然地面"(1465)，而出口周围已经被压成平台(1345)。用自然地面会让洞口
  // 比平台高 150cm，出洞就是一个迈不过去的下坎（MaxStepHeight 只有 45）。
  const sTop = (EXIT.padZ !== undefined ? EXIT.padZ : EXIT_NATURAL_Z) + 20;
  // 起点必须和迷宫地面【严格】同高，不能用 ENTRY.floor 这个近似。
  // 迷宫地面是 MAZE_FLOOR = AZ.LZ = HEXQ(ugFloor(ENTRY.x, ENTRY.y))，而 writeAccess
  // 在 writeHexField 之前跑、拿不到 AZ，所以这里用同一个公式重算一遍。
  // 差几十厘米就是一道跨不过去的坎 —— UE 实测通道地板悬在迷宫地面上方 3m，
  // 玩家从走廊走到洞口够不着，S4「出口可导航」永远过不了。
  const sBot = HEXQ(ugFloor(ENTRY.x, ENTRY.y));
  const riseTot = sTop - sBot;
  // 洞口开在迷宫【背对】出口的那一侧，不是朝着出口的那一侧。
  //
  // 朝着出口开的后果实测过：EXIT 离迷宫中心才 6193cm，洞口再往那边推 25 环(7350cm)，
  // 洞口和 EXIT 之间只剩 1168cm 弦长，却要在这段里爬 49.7m —— 二分外凸量为了凑够
  // 310m 弧长把控制点推到 26000cm 上限，贝塞尔直接甩到地图另一头，
  // UE 实测通道终点落在 (9234,6085)，离 EXIT 211 米。
  //
  // 背对着开，弦长 = mouthR + |中心->EXIT|，足够摊平坡度，通道也自然地绕迷宫半圈，
  // 读起来就是"抵抗派从迷宫深处往外凿了很长一段"。
  // 朝向 EXIT 开口。EXIT 不是常量 —— 它在 502 行定义后，会被"扫描离 ENTRY 26km 外的
  // 自然最低点"那段(514-524)改写，实测落到 (9234,6085)，离迷宫中心 2 万多 cm。
  // 所以弦长天然就够，不需要背对着开。
  // （之前拿 (-4200,-10200) 这个作废的初始值判断，误以为洞口离 EXIT 只有 1168cm。）
  const _vx = EXIT.x - MAZE.cx, _vy = EXIT.y - MAZE.cy;
  const _vl = Math.hypot(_vx, _vy) || 1;
  // 洞口半径必须落进真正的走廊区，不能贴最外圈：
  // ringsHex..ringsHex+3 是实心外墙带，第 29 环紧贴墙根，洞口会开在墙上。
  const mouthR = (MAZE.ringsHex - 5) * HEX_DR;
  const P0 = [MAZE.cx + _vx / _vl * mouthR, MAZE.cy + _vy / _vl * mouthR];
  const P2 = [EXIT.x, EXIT.y];
  const chord = Math.hypot(P2[0] - P0[0], P2[1] - P0[1]);
  const wantRun = riseTot / Math.tan(TUN_GRADE_DEG * Math.PI / 180);
  const bezLen = (h) => {
    const mx = (P0[0] + P2[0]) / 2, my = (P0[1] + P2[1]) / 2;
    const ux = -(P2[1] - P0[1]) / chord, uy = (P2[0] - P0[0]) / chord;
    const P1 = [mx + ux * h, my + uy * h];
    let L = 0, px = P0[0], py = P0[1];
    for (let i = 1; i <= 64; i++) {
      const t = i / 64, u = 1 - t;
      const x = u * u * P0[0] + 2 * u * t * P1[0] + t * t * P2[0];
      const y = u * u * P0[1] + 2 * u * t * P1[1] + t * t * P2[1];
      L += Math.hypot(x - px, y - py); px = x; py = y;
    }
    return { L: L, P1: P1 };
  };
  // 二分外凸量，让弧长逼近 wantRun（够不到就取地图能给的最大弯度）
  let lo = 0, hi = 26000;
  for (let it = 0; it < 40; it++) {
    const mid = (lo + hi) / 2;
    if (bezLen(mid).L < wantRun) { lo = mid; } else { hi = mid; }
  }
  const bow = bezLen((lo + hi) / 2);
  const P1 = bow.P1, runLen = bow.L;
  let grade2 = Math.atan(riseTot / runLen) * 180 / Math.PI;
  EXIT_RAMP_X = P0[0]; EXIT_RAMP_Y = P0[1];   // 共享给迷宫外墙开缺口用

  const bezAt = (t) => {
    const u = 1 - t;
    return [u * u * P0[0] + 2 * u * t * P1[0] + t * t * P2[0],
            u * u * P0[1] + 2 * u * t * P1[1] + t * t * P2[1]];
  };
  // 环距必须由【每环高差 ≤ 40cm】反解，不能拍个 2.6m 了事。
  // 实测 260cm 环距配 11.4° 坡度 = 每环爬 52~91cm，全程 119 环没有一环合规；
  // 玩家和 AI 都得靠斜面"滑"上去，导航在陡段直接断链，S4 永远过不了。
  //
  // 但不能用平均坡度反解：贝塞尔弧长分布不均，末段比平均陡得多 ——
  // 按平均值算的环距在那里照样超（实测 148 环仍有 55 处 >40cm）。
  // 改成【按实测最大高差迭代加密】：先试一个环数，量一遍全程最大每环高差，
  // 不达标就加密，直到达标或撞上限。
  let SEG = Math.max(40, Math.ceil(runLen / 200));
  for (let tries = 0; tries < 14; tries++) {
    let acc0 = 0, prev0 = bezAt(0);
    const arc0 = [0];
    for (let s = 1; s <= SEG; s++) {
      const p = bezAt(s / SEG);
      acc0 += Math.hypot(p[0] - prev0[0], p[1] - prev0[1]);
      arc0.push(acc0); prev0 = p;
    }
    let worst = 0;
    for (let s = 0; s < SEG; s++) {
      worst = Math.max(worst, riseTot * (arc0[s + 1] - arc0[s]) / acc0);
    }
    if (worst <= 40 || SEG > 1400) { break; }
    SEG = Math.ceil(SEG * Math.max(1.25, worst / 30));   // 收敛要快：worst/38 太温和，228 环时还剩 48cm
  }
  const rings = [];
  let acc = 0, prevXY = bezAt(0);
  const arc = [0];
  for (let s = 1; s <= SEG; s++) {
    const p = bezAt(s / SEG);
    acc += Math.hypot(p[0] - prevXY[0], p[1] - prevXY[1]);
    arc.push(acc); prevXY = p;
  }
  // ---- 出了洞腔才开始爬 ----
  // 地道一出迷宫洞口就按恒定坡度往上爬的话，头 20 环左右的高度落在
  // 【迷宫墙顶(-3160) 以上、洞顶(-2280) 以下】——那一段它既不在岩层里，也不在迷宫里，
  // 而是一根悬在洞腔里的管子。既违反"在填充土壤里挖出来"的设定，导航上也断
  // (实测第 10~13、16 环全断在这一段)。
  // 所以：还在洞腔里的时候保持迷宫地面高度平走，钻进实心岩层之后再开始爬。
  let sRock = 0;
  for (let s = 0; s <= SEG; s++) {
    const p = bezAt(s / SEG);
    if (ugField(p[0], p[1]) <= 0.03) { sRock = s; break; }
  }
  const arcRock = arc[sRock];
  for (let s = 0; s <= SEG; s++) {
    const t = s / SEG;
    const c = bezAt(t);
    const cz = arc[s] <= arcRock ? sBot
      : sBot + riseTot * (arc[s] - arcRock) / Math.max(1, acc - arcRock);
    const nxt = bezAt(Math.min(1, t + 1 / SEG)), prv = bezAt(Math.max(0, t - 1 / SEG));
    const tx = nxt[0] - prv[0], ty = nxt[1] - prv[1];
    const tl = Math.hypot(tx, ty) || 1;
    const nx = -ty / tl, ny = tx / tl;                  // 断面法向（水平）
    // 断面整体也左右/上下歪一点，读起来才像手挖的歪洞而不是挤出来的管
    const swayN = 55 * (fbm(c[0], c[1], 2600, 3, 71) - 0.5) * 2;
    const hwS = TUN_FLOOR_HW * (1 + 0.30 * (hexHash(s, 3, 17) - 0.5));
    const ring = [];
    for (let k = 0; k <= TUN_K; k++) {
      const phi = Math.PI * k / TUN_K;
      // 极端粗糙：每个点沿自己的径向再抖一次。
      // 但【地面那条带不能跟着抖】：地面两角(k=0/K)和紧挨着它们的第一段拱壁如果各抖各的，
      // 洞壁就会探到地面上方去，把可走宽度掐窄。UE 实测 119 段里断了 5 段(第10~13、16、117 环)，
      // 全是这么来的 —— 而地道和迷宫一样是一条链，断一环后面全失联。
      // 所以：拱顶照抖，越靠近地面抖得越轻；地面两角只留很小的宽度变化。
      const base = Math.sin(phi);                        // 0 在地面，1 在拱顶
      const rough = 1 + 0.28 * (hexHash(s, k, 29) - 0.5) * 2 * base;
      const rw = hwS * (1 + 0.10 * (hexHash(s, k, 29) - 0.5) * 2 * base);
      const rh = TUN_ARCH * rough;
      const off = swayN * base;   // 歪斜也从地面处收敛到 0，地面永远是平整连续的一条带
      ring.push(push(c[0] + nx * (rw * Math.cos(phi) + off),
                     c[1] + ny * (rw * Math.cos(phi) + off),
                     cz + rh * Math.sin(phi)));
    }
    rings.push({ v: ring, c: c, z: cz, n: [nx, ny], hw: hwS });
  }
  // 绕序：地道是【从里面走】的 —— 法线必须朝内，地面必须朝上。
  // 第一版地面写成 quad(A[K], A[0], B[0], B[K])，在 XY 平面里是顺时针 => 法线朝下。
  // 从上往下的射线打在背面被剔除，UE 实测 118 环里 0 环有地板，寻路直接无解。
  // 第二版改成"两个绕序都发"，碰撞是好了(118/118)，但【导航反而更糟】：Recast 体素化
  // 时同一处出现一对朝向相反的面，可走 span 被判掉，120 环全被 navmesh 覆盖却一环都
  // 连不上 —— 从洞口走两步就到头。
  // 正解是单面 + 正确绕序，不是双面。
  const quadIn = (a, b, c, d) => quad(d, c, b, a);
  // 平走段（还在洞腔里、高度就是迷宫地面）【不出管子】：那一段的地面由迷宫开槽提供，
  // 管子再铺一层就和迷宫地面完全共面 —— 两个重合的可走面会把 Recast 的 span 判乱。
  // UE 实测：从洞口能走到第 40 环，第 41 环(z=-3567，正好是管底刚抬离迷宫地面的地方)
  // 断掉，后面 247 环全部失联。所以管子从钻进岩层那一环才开始出。
  // 只在【迷宫开槽真的给了地面】的那些环上跳过管子。迷宫足迹之外没有开槽，
  // 那里必须由管子自己出地面，否则脚下就是比隧道高 8m 的自然洞底 —— 一断到底。
  const inMazeFoot = (px0, py0) => {
    const px = px0 - MAZE.cx, py = py0 - MAZE.cy;
    const fq = px / HEX_DQ, fr = (py - fq * HEX_DR * 0.5) / HEX_DR;
    let c0 = fq, c2 = fr, c1 = -c0 - c2;
    let r0 = Math.round(c0), r1 = Math.round(c1), r2 = Math.round(c2);
    const d0 = Math.abs(r0 - c0), d1 = Math.abs(r1 - c1), d2 = Math.abs(r2 - c2);
    if (d0 > d1 && d0 > d2) { r0 = -r1 - r2; } else if (d1 > d2) { r1 = -r0 - r2; } else { r2 = -r0 - r1; }
    return (Math.abs(r0) + Math.abs(r1) + Math.abs(r2)) / 2 <= MAZE.ringsHex + 3;
  };
  const carved = rings.map(r => inMazeFoot(r.c[0], r.c[1]) && r.z < sBot + 5);
  // 出口那一段【不出拱顶】，只留地面 —— 否则地道自己的拱把洞口封死了。
  // UE 实测：在出口 (9234,6085) 从天上往下打，最先打到的是 F0Access 的拱顶 z=1634，
  // 地表那个洞(ugOpen 已经开好了)底下扣着一个完整的管子，人根本钻不出去，
  // 寻路也就停在离洞口 479cm 的地方。
  // 去掉拱顶之后这一段变成一条露天的堑壕，接进地表的塌陷坑，才算真正破土。
  const mouth = rings.map(r => Math.hypot(r.c[0] - EXIT.x, r.c[1] - EXIT.y) < EXIT.r);
  for (let s = 0; s < SEG; s++) {
    if (carved[s] && carved[s + 1]) { continue; }
    const A = rings[s].v, B = rings[s + 1].v;
    if (!(mouth[s] && mouth[s + 1])) {
      for (let k = 0; k < TUN_K; k++) { quadIn(A[k], A[k + 1], B[k + 1], B[k]); } // 拱壁(朝内)
    }
    quadIn(A[TUN_K], A[0], B[0], B[TUN_K]);                                       // 地面(朝上)
  }
  const tunSteps = SEG;
  // 平走段(还在洞腔里那一段)全部交给迷宫去开槽 —— 那一段没有管壁，走的就是被凿开的
  // 迷宫地面；再往后地道才钻进实心岩层，由管子自己提供地面和洞壁。
  TUN_PATH = rings.filter(r => r.z < sBot + 620).map(r => [r.c[0], r.c[1], r.z]);
  // 全线中心线交给 crustBottom 做包络，让地壳把隧道裹进去（见 crustBottom）
  TUN_CENTRE = rings.map(r => [r.c[0], r.c[1], r.z]);
  buildTunGrid();
  grade2 = Math.atan(riseTot / Math.max(1, acc - arcRock)) * 180 / Math.PI;
  // 把地道中心线落盘，UE 侧的断言直接照着它采样，不用在 Python 里重算贝塞尔
  fs.writeFileSync(OUT + '/k11_tunnel.json', JSON.stringify({
    grade: grade2, run: runLen, chord: chord, rise: riseTot,
    floorHalfWidth: TUN_FLOOR_HW, arch: TUN_ARCH,
    padZ: EXIT.padZ, padR: EXIT_PAD_R, exit: [EXIT.x, EXIT.y],
    centre: rings.map(r => [Math.round(r.c[0]), Math.round(r.c[1]), Math.round(r.z)]),
  }));


  // ---- 竖井底部：六棱鼓形门墙（六个六边形门洞）----
  // 用户要求：竖井外壳最下方修剪成几个六边形门口，而不是现在的三角锯齿。
  //
  // 锯齿的成因：ugOpen 的 ENTRY 判据是【圆】，在 200cm 三角晶格上栅格化；而漏斗壁
  // 在 rBot 那个半径上近乎垂直 —— 横向差一格，竖向就差两三米。栅格化的圆改不出干净
  // 边，所以这里用显式几何盖一层六棱鼓，把锯齿关在它后面，门洞形状由这层几何决定。
  //
  // 门位不是随便排的：UE 实测扫了 60 个相位，每个相位取六个方位、检查【壳外三个
  // 半径上有没有 navmesh 落脚点】，选"最差的那扇门连通性最高"的相位 = 4 度。
  // 门必须开在能走的方位上，否则推开门是一堵迷宫外墙。
  // 光有门框还不够：124/184/244 三个方位的漏斗壁本来是实心的，所以 ugOpen 那边
  // 同步开了六条径向门缝把壁凿穿（见 entryOpen）。
  {
    const DRUM_AP = 1150;                       // 内切半径 = UE 实测外壳半径 1038..1247
    const DRUM_T = 130;                         // 墙厚
    const DRUM_H = 620;                         // 墙高，盖住 -3680..-3060 那段锯齿
    const DRUM_R = DRUM_AP / 0.8660254;         // 正六边形：边长 = 外接圆半径
    const PHASE = 4 * Math.PI / 180;            // 实测最优相位
    const DOOR_R = 230;                         // 门洞外接圆：宽 460、高 398
    const HW = DRUM_R * 0.5;                    // 每面半宽
    const DOOR_VC = DOOR_R * 0.8660254;         // 门心高度 199，使下边正好落在地面
    const z0 = sBot;

    // flat-top 正六边形在【面内】摆放 -> 下边平(门槛)、上边平(门楣)、左右出尖。
    // 门槛必须落在地面上，否则玩家跨不进去（MaxStepHeight 45cm）。
    const hexR = (a) => {
      let m = a % (Math.PI / 3);
      if (m < 0) m += Math.PI / 3;
      return DOOR_R * 0.8660254 / Math.cos(m - Math.PI / 6);
    };
    // 面板矩形边界：u ∈ [-HW,HW]，v ∈ [0,DRUM_H]，从门心射线求交
    const rectR = (a) => {
      const dx = Math.cos(a), dy = Math.sin(a);
      let t = Infinity;
      if (dx > 1e-9) { t = Math.min(t, HW / dx); }
      if (dx < -1e-9) { t = Math.min(t, -HW / dx); }
      if (dy > 1e-9) { t = Math.min(t, (DRUM_H - DOOR_VC) / dy); }
      if (dy < -1e-9) { t = Math.min(t, -DOOR_VC / dy); }
      return t;
    };

    // 环带角度表：六个门洞顶点 + 四个矩形角，两条环线在同一组角度上一一对应，
    // 这样"矩形挖六边形洞"就退化成一圈四边形，不用做通用多边形三角化。
    const angs = [];
    for (let i = 0; i < 6; i++) { angs.push(Math.PI / 3 * i); }
    for (const [cu, cv] of [[HW, DRUM_H - DOOR_VC], [-HW, DRUM_H - DOOR_VC],
                            [-HW, -DOOR_VC], [HW, -DOOR_VC]]) {
      let a = Math.atan2(cv, cu);
      if (a < 0) { a += Math.PI * 2; }
      angs.push(a);
    }
    angs.sort((p, q) => p - q);

    // 面内 (u,v) + 离轴距离 r -> 世界顶点
    const FP = (th, u, v, r) => {
      const nx = Math.cos(th), ny = Math.sin(th);
      return push(ENTRY.x + nx * r - ny * u, ENTRY.y + ny * r + nx * u, z0 + v);
    };
    // 竖直墙面【双面发】：法线朝向在这套 Y 取负的 OBJ 里极易搞反，而竖直面
    // 本来就不产生可走 span，双面不会像地面那样被 Recast 判掉（那个坑踩过三次）。
    const wall2 = (a, b, c, d) => {
      tri(a, b, c); tri(a, c, d);
      tri(a, c, b); tri(a, d, c);
    };

    const rIn = DRUM_AP - DRUM_T * 0.5, rOut = DRUM_AP + DRUM_T * 0.5;
    let drumFaces = 0, drumDoors = 0;
    for (let k = 0; k < 6; k++) {
      const th = PHASE + Math.PI / 3 * k;
      const hv = [], rv = [];                    // 洞口环 / 矩形环，两侧各一份
      for (const a of angs) {
        const hr = hexR(a), rr = rectR(a);
        const hu = Math.cos(a) * hr, hvv = DOOR_VC + Math.sin(a) * hr;
        const ru = Math.cos(a) * rr, rvv = DOOR_VC + Math.sin(a) * rr;
        hv.push([FP(th, hu, hvv, rIn), FP(th, hu, hvv, rOut), hu, hvv]);
        rv.push([FP(th, ru, rvv, rIn), FP(th, ru, rvv, rOut), ru, rvv]);
      }
      for (let i = 0; i < angs.length; i++) {
        const j = (i + 1) % angs.length;
        // 洞口和矩形在这一段上重合（门槛贴地那一段）就没有面板，跳过
        const degen = Math.abs(hv[i][3] - rv[i][3]) < 1.0 && Math.abs(hv[j][3] - rv[j][3]) < 1.0
                   && Math.abs(hv[i][2] - rv[i][2]) < 1.0 && Math.abs(hv[j][2] - rv[j][2]) < 1.0;
        if (degen) { continue; }
        wall2(hv[i][0], hv[j][0], rv[j][0], rv[i][0]);      // 内面
        wall2(hv[i][1], hv[j][1], rv[j][1], rv[i][1]);      // 外面
        drumFaces += 2;
      }
      // 门洞侧壁（门套）：把内外两圈洞口边缘连起来，让门有厚度
      for (let i = 0; i < angs.length; i++) {
        const j = (i + 1) % angs.length;
        wall2(hv[i][0], hv[j][0], hv[j][1], hv[i][1]);
        drumFaces++;
      }
      // 顶盖 + 两侧竖直接缝：鼓是实体，不是一张纸
      const tL = FP(th, -HW, DRUM_H, rIn), tR = FP(th, HW, DRUM_H, rIn);
      const tLo = FP(th, -HW, DRUM_H, rOut), tRo = FP(th, HW, DRUM_H, rOut);
      wall2(tL, tR, tRo, tLo);
      drumDoors++;
    }
    console.log('  shaft drum: 6 faces @ apothem ' + DRUM_AP + 'cm, wall ' + DRUM_T
      + 'cm, height ' + DRUM_H + 'cm; ' + drumDoors + ' hexagonal doorways '
      + (DOOR_R * 2) + 'x' + Math.round(DOOR_R * 1.7320508) + 'cm (sill on floor); '
      + drumFaces + ' quads');
  }
  fs.writeFileSync(path, ['s 1'].concat(V, F).join('\n'));
  return { pathSegs: pathCells, grade: grade, drop: maxDrop, steps: tunSteps,
           stairRise: sTop - sBot, grade2: grade2, verts: V.length,
           tunRun: runLen, tunChord: chord, exitXY: [EXIT.x, EXIT.y],
           mouthXY: [EXIT_RAMP_X, EXIT_RAMP_Y] };
}

// --old*: write the APPLIED baseline and stop. Emits no meshes, changes nothing else.
if (BASELINE_ONLY) {
  const cur = [];
  for (let j = 0; j <= NY; j++) for (let i = 0; i <= NX; i++) {
    cur.push(Math.round(surface(X0 + i * CELL, Y0 + j * CELL)));
  }
  fs.writeFileSync(APPLIED, JSON.stringify({ nx: NX, ny: NY, cell: CELL, z: cur }));
  console.log('applied-baseline written (' + cur.length + ' samples); no meshes emitted');
  process.exit(0);
}

// ---------------- build ----------------
// OBJ convention copied from gen_terrain3.py: Y is pre-negated and the winding is
// a,c,b / a,d,c, because the legacy FBX path flips it back on import. Get this wrong and
// the whole map imports mirrored -- that cost a round once already.
// ---- 三角晶格 ----
// 矩形网格的每个顶点只有 4 个邻居，所以任何边界、阶地、崖面的折线都只能沿 XY 两个
// 轴走 —— 那就是整张地图"方方正正"的根源，跟高度函数无关，是采样拓扑决定的。
// 三角晶格：偶数行不动，奇数行右移半格，行距压成 √3/2。每个内部顶点正好 6 个邻居，
// 扇成一个六边形，折线沿六边形的三个方向走，和柱阵是同一套几何语言。
//
// 只改网格的采样点，不改 surface() 本身 —— 所以 writeDelta 的重铺基线仍按矩形格
// 采样，格式和历史快照都不受影响。
const HEXROW = Math.sqrt(3) / 2;
const latX = (x0, i, j, c) => x0 + i * c + ((j & 1) ? c * 0.5 : 0);
const latY = (y0, j, c) => y0 + j * c * HEXROW;

// ---- 地壳底面：地表以下必须是实心岩土，不是一层壳 ----
// 原来地表底面固定取 surface(x,y) - SKIN(400cm)，洞腔顶也只有 UG_SKIN(260cm) 的壳。
// 两层之间因此是【空的】：站在负一层往上看能看穿两层之间的虚空，螺旋梯和逃生口
// 周围也只是一圈薄壳套着，不是山体。
// 改成：有洞腔的地方，地壳一直填到洞顶；没洞腔的地方，一路填到 CRUST_DEEP。
// 这样巨坑就是在实心地壳里凿出来的竖井，逃生地道是在土里挖出来的洞，
// 而不是为了摆楼梯临时套的圆筒。
// 顶点数一个没多（还是每列上下两个），变的只是底面高度和封边的高度。
// 没有洞腔的地方，地壳底面【贴着地表走】，不要拉到一个固定深度。
// 我一开始拉到 -6800，本意是"填厚"，效果恰好相反：地表是上下两张面片围出来的
// 闭合壳，把底面拉深 = 把这个壳撑成一个 40m 高的死腔，逃生地道正好悬在里面 ——
// 用户看到的"地道是实心的、周围反而是空气"就是这么来的。
// 闭合壳本身是业界标准做法（玩家进不去、也看不进去），但壳必须【贴着真实空腔走】，
// 不能自己造一个巨大的空心出来。
// ---- 隧道包络：地壳底面必须跟着隧道往下走 ----
// UE 截图实测：隧道中段在 (-4026,12818,z=-1801)，而那里地壳底面 = surface-400 还在
// 二十多米【之上】—— 隧道整段吊在地壳底下的虚空里，四周什么都没有。
// 这就是"地道是实心的、周围反而是空气"。
// 地壳是"地表面 + 底面"围出的闭合体，隧道要算"从地壳里掏出来的"，前提是隧道必须
// 落在这个闭合体【内部】。所以底面在隧道走廊上要压到隧道底之下。
let TUN_CENTRE = [];
let TUN_GRID = null;
const TUN_ARCH_REF = 400;   // 和 writeAccess 里的 TUN_ARCH 一致，用于包裹检查
const TUN_ENVR = 1200;      // 包络半径：管子半宽 260 + 岩壁厚度，留足
const TUN_CLEAR = 800;      // 地壳底面要比隧道底再低这么多
const TUN_CS = 1200;        // 空间哈希格
function buildTunGrid() {
  TUN_GRID = new Map();
  for (const p of TUN_CENTRE) {
    const k = Math.floor(p[0] / TUN_CS) + ',' + Math.floor(p[1] / TUN_CS);
    if (!TUN_GRID.has(k)) { TUN_GRID.set(k, []); }
    TUN_GRID.get(k).push(p);
  }
}
// 返回该 xy 附近隧道底的最低高度；不在隧道附近返回 null
function tunnelFloorNear(x, y) {
  if (!TUN_GRID) { return null; }
  const gx = Math.floor(x / TUN_CS), gy = Math.floor(y / TUN_CS);
  const rad = Math.ceil(TUN_ENVR / TUN_CS);
  let best = null;
  const r2 = TUN_ENVR * TUN_ENVR;
  for (let i = -rad; i <= rad; i++) {
    for (let j = -rad; j <= rad; j++) {
      const arr = TUN_GRID.get((gx + i) + ',' + (gy + j));
      if (!arr) { continue; }
      for (const p of arr) {
        const dx = x - p[0], dy = y - p[1];
        if (dx * dx + dy * dy > r2) { continue; }
        if (best === null || p[2] < best) { best = p[2]; }
      }
    }
  }
  return best;
}

function crustBottom(x, y) {
  const top = surface(x, y) - SKIN;
  const f = ugField(x, y);
  // 洞腔内：填到洞顶 skin 的上沿。
  // 必须是 ugCeil + UG_SKIN，不能是 ugCeil：压到 ugCeil 会让地壳底面和洞顶完全共面，
  // 两个重合的面互相吃掉，洞顶整个消失（从迷宫抬头直接看到地表的城和信标灯）。
  let b = f > 0 ? Math.min(top, ugCeil(x, y) + UG_SKIN) : top;
  // 出口那一圈不做包络：那里隧道要【破土而出】，地壳底面再往下压会把洞口挖成一口
  // 十几米深的竖井。包络只管把隧道埋在岩层里的那一段裹住。
  const tz = tunnelFloorNear(x, y);
  if (tz !== null && Math.hypot(x - EXIT.x, y - EXIT.y) > EXIT.r) {
    b = Math.min(b, tz - TUN_CLEAR);
  }
  return b;
}

function writeSurface(path) {
  const L = [];
  // 行距压了 √3/2，行数要按比例加回来，否则地图在 Y 方向短一截
  const NYH = Math.ceil(NY / HEXROW);
  for (let j = 0; j <= NYH; j++) for (let i = 0; i <= NX; i++) {
    const x = latX(X0, i, j, CELL), y = latY(Y0, j, CELL);
    L.push('v ' + x.toFixed(1) + ' ' + (-y).toFixed(1) + ' ' + surface(x, y).toFixed(1));
    L.push('vt ' + (x / 800).toFixed(4) + ' ' + (y / 800).toFixed(4));
  }
  // Underside vertices. The ground is a SOLID with a skin, not a sheet: a zero-thickness
  // heightfield is invisible from below, so standing in the cave you look up through the
  // world. That is what read as "large areas of missing terrain".
  const topCount = (NX + 1) * (NYH + 1);
  for (let j = 0; j <= NYH; j++) for (let i = 0; i <= NX; i++) {
    const x = latX(X0, i, j, CELL), y = latY(Y0, j, CELL);
    L.push('v ' + x.toFixed(1) + ' ' + (-y).toFixed(1) + ' ' + crustBottom(x, y).toFixed(1));
    L.push('vt ' + (x / 800).toFixed(4) + ' ' + (y / 800).toFixed(4));
  }

  const vid = (i, j) => j * (NX + 1) + i + 1;
  const bid = (i, j) => topCount + j * (NX + 1) + i + 1;
  L.push('s 1');

  // Cut the collapses OUT of the surface, and remember which cells were cut so the
  // exposed edges can be capped afterwards.
  const cut = [];
  let holes = 0;
  for (let j = 0; j < NYH; j++) {
    cut.push(new Array(NX).fill(false));
    for (let i = 0; i < NX; i++) {
      const cx = (latX(X0, i, j, CELL) + latX(X0, i + 1, j + 1, CELL)) * 0.5;
      const cy = (latY(Y0, j, CELL) + latY(Y0, j + 1, CELL)) * 0.5;
      if (ugField(cx, cy) > 0 && ugOpen(cx, cy)) { cut[j][i] = true; holes++; }
    }
  }

  for (let j = 0; j < NYH; j++) for (let i = 0; i < NX; i++) {
    if (cut[j][i]) { continue; }
    const a = vid(i, j), b = vid(i + 1, j), c = vid(i + 1, j + 1), d = vid(i, j + 1);
    const e = bid(i, j), f = bid(i + 1, j), g = bid(i + 1, j + 1), h = bid(i, j + 1);
    // 对角线随行奇偶翻转。不翻的话所有三角形朝同一个方向倒，晶格退化成一排斜条纹，
    // 六边形对称性就没了 —— 折线又会重新沿一个固定方向走。
    if (j & 1) {
      L.push('f ' + a + '/' + a + ' ' + c + '/' + c + ' ' + b + '/' + b);
      L.push('f ' + a + '/' + a + ' ' + d + '/' + d + ' ' + c + '/' + c);
      L.push('f ' + e + '/' + e + ' ' + f + '/' + f + ' ' + g + '/' + g);
      L.push('f ' + e + '/' + e + ' ' + g + '/' + g + ' ' + h + '/' + h);
    } else {
      L.push('f ' + a + '/' + a + ' ' + d + '/' + d + ' ' + b + '/' + b);
      L.push('f ' + b + '/' + b + ' ' + d + '/' + d + ' ' + c + '/' + c);
      L.push('f ' + e + '/' + e + ' ' + f + '/' + f + ' ' + h + '/' + h);
      L.push('f ' + f + '/' + f + ' ' + g + '/' + g + ' ' + h + '/' + h);
    }
  }

  // Cap every exposed edge: around each hole, and around the whole map rim. Without the
  // caps the skin is just a second sheet and the cut edges show as paper-thin slots.
  const solid = (i, j) => (i >= 0 && j >= 0 && i < NX && j < NYH && !cut[j][i]);
  let caps = 0;
  const wall = (t0, t1, b0, b1) => {
    L.push('f ' + t0 + '/' + t0 + ' ' + t1 + '/' + t1 + ' ' + b1 + '/' + b1);
    L.push('f ' + t0 + '/' + t0 + ' ' + b1 + '/' + b1 + ' ' + b0 + '/' + b0);
    caps++;
  };
  for (let j = 0; j < NYH; j++) for (let i = 0; i < NX; i++) {
    if (!solid(i, j)) { continue; }
    if (!solid(i + 1, j)) { wall(vid(i + 1, j), vid(i + 1, j + 1), bid(i + 1, j), bid(i + 1, j + 1)); }
    if (!solid(i - 1, j)) { wall(vid(i, j + 1), vid(i, j), bid(i, j + 1), bid(i, j)); }
    if (!solid(i, j + 1)) { wall(vid(i + 1, j + 1), vid(i, j + 1), bid(i + 1, j + 1), bid(i, j + 1)); }
    if (!solid(i, j - 1)) { wall(vid(i, j), vid(i + 1, j), bid(i, j), bid(i + 1, j)); }
  }

  fs.writeFileSync(path, L.join('\n'));
  return { verts: topCount * 2, holes: holes, caps: caps, rows: NYH };
}

// ---------------- 底岩同化层：六边形硅化柱阵 ----------------
// 同化不是侵蚀，是把混沌物质强制格式化成绝对对称的结构。所以地下不该是溶洞，
// 而该是被切削重组过的几何空间。
//
// 上一版把这条美术设定直接怼到地形上，结果是灾难。柱底取 ugFloor() —— 一个连续
// 噪声高度场，里面有 1500*pow(1-f,1.3) + 620*fbm + 240*fbm + 900 的碎石锥 + strata
// 分层 —— 相邻两根柱的柱底就能差好几米，我却又在上面叠了 0/95/190/330/520 的离散
// 掩体分级。于是地面根本不是地面，是一床钉板：一根数据上是 tier 0(平地)的柱子，
// 实际可能比旁边高两米，玩家走两步就被"地面"顶住。掩体分级从来没成立过。
//
// 这一版反过来：先有平地，再长掩体。核心是一条可断言的硬不变量——
//
//   走廊网络（竞技场 + 连接它们的主干道）内，任意相邻两格的地面高差 ∈ {0, STEP}，
//   且 STEP < UE CharacterMovement 的 MaxStepHeight(45cm)。
//
// 网络外的地形只做 STEP 量化，是背景断崖，不承诺可走，也不长顶板柱。
// 掩体只从这条平地上长出来：它是障碍物，不是地面。生成器末尾会把这条不变量
// 实测一遍并打印，违反就是 FAIL —— 不靠眼睛看截图判断地形能不能走。
const HEX_R = 170;                        // 中心到顶点；对边宽约 294cm，人体尺度
const HEX_DQ = HEX_R * 1.5;               // 列距 255cm
const HEX_DR = HEX_R * Math.sqrt(3);      // 行距 294cm
const STEP = 40;                          // 台阶高，必须 < MaxStepHeight 45cm
const HEX_TIERS = [95, 190, 330];         // 蹲/站/全高，只做掩体，绝不参与地面
const HEX_CLEAR = 420;                    // 走廊网络内要求的最小净空

// 竞技场：手工指定的绝对平整交战区。这不是程序生成能替代的，是关卡设计决策。
//   r    完全平整的半径
//   rOut 过渡回自然地形的外沿
//   cover 掩体密度
// 半径要留出真正的走廊。上一版 MID(r=1800) 和 MAIN(r=2600) 中心只隔 4800，走廊只剩
// 400cm，却要在里面消化 680cm 落差 —— 边界上直接立起一堵 3.7m 的墙，主场从落地点
// 走不到。竞技场之间必须留至少十几米的过渡段，落差才有地方摊。
const ARENAS = [
  { name: 'LZ',   x: ENTRY.x, y: ENTRY.y, r: 1150, rOut: 2200, cover: 0.10 },  // 入口坑底：落地区
  { name: 'MID',  x: -9600,   y: -11000,  r: 1300, rOut: 2400, cover: 0.14 },  // 廊道中继
  { name: 'MAIN', x: TK.x,    y: TK.y,    r: 2100, rOut: 3400, cover: 0.16 },  // 天坑下的主场：天光 + 挑高
  { name: 'GATE', x: -6600,   y: -9200,   r: 1050, rOut: 2000, cover: 0.13 },  // 通往出口的闸口
  { name: 'EXIT', x: EXIT.x,  y: EXIT.y,  r: 1000, rOut: 1800, cover: 0.10 },  // 出口楼梯脚：最后一战
];
// 主干道：铺装过的行走走廊。hw = 半宽。
const LANES = [
  { a: 'LZ',   b: 'MID',  hw: 760 },
  { a: 'MID',  b: 'MAIN', hw: 720 },
  { a: 'MID',  b: 'GATE', hw: 700 },
  { a: 'GATE', b: 'EXIT', hw: 700 },
];

// ---------------- 负一层迷宫（按模式图第 2 组） ----------------
// 模式图规范：同心环迷宫，入口=螺旋阶梯最下端，中心=祭坛/污染源(必藏物)，
// 死胡同=藏物点(隐藏结局解锁)，斜向上通道=出口(通隐蔽洞窟)。
//
// 结构：以 LZ(螺旋梯落点)为圆心的 N 个同心环。环与环之间是柱墙(不可通行)，
// 每环开 2~3 个错位的通口(gate)。玩家必须沿环绕行找到下一个通口，逐环向内。
// 相邻两环的通口角度故意错开，形成"绕大半圈"的迷宫感 —— 这是同心环迷宫的
// 核心机制，不需要随机迷宫算法，秩序感反而更强(符合"同化=强制秩序"设定)。
const MAZE = {
  // 中心必须落在柱阵格心上。ENTRY 原坐标在格坐标里是 q=41.741 r=7.251 —— 偏移
  // 0.74/0.25 格，worldToAxial 取整时会落到相邻格，同一条走廊被切成断续的碎片
  // (自检里外圈可达率只有 30~60% 就是这么来的)。吸附后偏差仅 99cm，不影响布局。
  cx: 0, cy: 0,                  // 下方 snapMazeCenter() 填入，= 离 ENTRY 最近的格心
  rings: 7,                      // 环数
  // ---- 全部改用"六边形环"计数，不再用欧氏半径 ----
  // 环 = 距中心 N 步的那一圈六边形格。墙由格子逐个拼接，天然是正六边形轮廓。
  // 用半径切圆再转格子，走廊会被格心采样吃光(首版 dr=620 时走廊净宽 280cm < 294cm
  // 格距，一个格心都落不进去，自检报"入口走不到中心"抓的就是这个)。
  altarRings: 3,                 // 中心祭坛室：0..3 圈(半径约 3 格 ≈ 9m)
  corridorRings: 3,              // 每条环形走廊占 2 圈(约 6m 宽，人能跑能绕)
  ringsHex: 30,                  // 迷宫总圈数 -> 半径约 26*294 = 76m
  gatesPerRing: 2,               // 每道墙的通口数
  gateCells: 7,                  // 每个通口占几格宽(六边形环上的格数,不是弧长)
  deadEnds: 14,                  // 死胡同/藏物点数量
};
// 吸附迷宫中心到柱阵格心。柱阵起点和 writeHexField 里一致。
{
  // 迷宫中心必须偏离楼梯落点：中心和落点重合的话，玩家下楼梯就直接站在祭坛上，
  // 整座迷宫形同虚设(实测落点算出来是第 0 环 = 祭坛室内)。
  // 参考图③ 的入口在最外圈边缘 —— 所以把中心朝洞腹(UG 中心)方向推 (ringsHex-2) 环，
  // 楼梯落点就落到迷宫外沿，玩家必须从外向内穿越。
  const _push = (MAZE.ringsHex - 2) * HEX_DR;
  const _vx = UG.cx - ENTRY.x, _vy = UG.cy - ENTRY.y;
  const _vl = Math.hypot(_vx, _vy) || 1;
  const _tx = ENTRY.x + _vx / _vl * _push;
  const _ty = ENTRY.y + _vy / _vl * _push;
  // 再吸附到柱阵格心，否则 worldToAxial 取整会让走廊断成碎片
  const _x0 = UG.cx - UG.rx * 1.35, _y0 = UG.cy - UG.ry * 1.35;
  const _fq = (_tx - _x0) / HEX_DQ;
  const _fr = (_ty - _y0) / HEX_DR;
  const _sq = Math.round(_fq), _sr = Math.round(_fr);
  MAZE.cx = _x0 + _sq * HEX_DQ;
  MAZE.cy = _y0 + _sr * HEX_DR + (_sq % 2 ? HEX_DR * 0.5 : 0);
  MAZE.gq = _sq; MAZE.gr = _sr;   // 迷宫中心在【网格下标】里的位置，见 gridToAxial
}

// ---- 网格下标 -> 轴向坐标（这是迷宫能不能连通的关键）----
// writeHexField 把格子摆在 odd-q 偏移网格上：y = y0 + r*DR + (q 为奇数 ? 0.5*DR : 0)，
// 奇数列整体上移半行、且【只】上移半行。
// 而迷宫是在轴向坐标上生成的：y = cy + ar*DR + aq*0.5*DR，偏移随列号线性累加。
// 两者是不同的点阵。原来的 worldToAxial 拿格心的世界坐标去反算轴向再取整，等于把
// 一个线性错切的坐标系硬套到周期性错开的网格上 —— 列号越大偏得越多，多个物理格
// 会round到同一个轴向格，另一些轴向格一个物理格都分不到。
// 后果：递归回溯生成的完美迷宫（拓扑上 100% 连通）画到网格上就被剪断。
// UE 实测：走廊被切成 37 个互不连通的连通块，最大的一块只有 315 格，
// 入口所在那块只有 10 格 —— 从入口只走得到 1% 的迷宫。
// 正解是从网格下标直接算轴向，这是双射：
//   aq = q - gq
//   ar = (r - gr) + ((q&1) - q - ((gq&1) - gq)) / 2
// ((q&1) - q) 恒为偶数（q 偶时 = -q，q 奇时 = 1-q），所以 ar 一定是整数。
function gridToAxial(q, r) {
  const aq = q - MAZE.gq;
  const ar = (r - MAZE.gr) + (((q & 1) - q) - ((MAZE.gq & 1) - MAZE.gq)) / 2;
  return [aq, ar];
}

const MAZE_R_MAX = MAZE.ringsHex * HEX_DR;   // ≈ 7650cm ≈ 76m

// ---- 迷宫必须有自己的洞腔 ----
// 地下层的可用范围 = LOBES 的并集(hexCovers)，而迷宫是以 MAZE.cx/cy 为心、
// 半径 MAZE_R_MAX 的正六边形 —— 两者原本毫无关系，迷宫直接长在洞腔外面就没有地面。
// UE 实测(ue_maze_probe.py，按格心逐格打射线)：环 0..19 完好，环 20 起开始缺格，
// 到环 30 有一半的格【根本没有几何】——2791 格里 530 格是空的(19%)。导航因此碎成
// 孤岛，从入口只走得到 30% 的走廊。最大溢出在北侧 2689cm，比洞腔北缘高出 9 环。
//
// 缩小迷宫解决不了：中心是从 ENTRY 朝洞心推 (ringsHex-2) 环得来的，环数一小中心
// 就跟着南移，南缘立刻又捅出去。而 ENTRY 到洞心 7378cm 本身就大于 UG.ry 7000 ——
// 任何"够得到 ENTRY 的迷宫"都装不进原洞腔。
// 正解是补一个迷宫自己的腔体，这也符合设定：迷宫是被同化体开凿的空间，它就是
// 地下层的一个厅。写在这里而不是 LOBES 字面量里，因为要先算出 MAZE.cx/cy；
// ugField 的首次调用发生在文件末尾的 write* 里，改得到。
// 半径给 1.30 而不是刚好包住六边形的 1.06：hexCovers 判的是 ugField > 0.03，而
// ugField 是各 lobe 的平滑混合，值在到达 lobe 名义半径之前就跌破阈值了。1.06 那版
// UE 实测还剩 180 个格【没有几何】。
// 这 180 个洞不是小瑕疵 —— 递归回溯生成的完美迷宫是一棵【树】，树上删掉任意一个
// 节点就断成两截。实测这 180 个洞把 1299 个可走格切成 37 个连通块，最大的一块
// 只有 315 格，入口所在那块只有 10 格：从入口只走得到 1% 的迷宫。
// 迷宫要连通，它的地面就必须一格不缺。
LOBES.push({ cx: MAZE.cx, cy: MAZE.cy, rx: MAZE_R_MAX * 1.30, ry: MAZE_R_MAX * 1.30 });

// 每环通口的角度：用确定性哈希，且强制与上一环错开 >= 100°，保证必须绕行
function mazeGateAngles(ring) {
  const out = [];
  const base = hexHash(ring, 7, 31) * Math.PI * 2;
  for (let g = 0; g < MAZE.gatesPerRing; g++) {
    // 同环内的多个通口均分，再加环相关的整体旋转
    let a = base + g * (Math.PI * 2 / MAZE.gatesPerRing);
    // 相邻环整体再转 130°：内外通口错开，玩家进环后要绕大半圈才找到下一个口
    a += ring * 2.269;    // 130° in rad
    out.push(a % (Math.PI * 2));
  }
  return out;
}

// ---- 六边形环距：迷宫的"环"是六边形，不是圆 ----
// 用轴向坐标的六边形距离(cube distance)，环 N = 距中心恰好 N 步的那一圈六边形格。
// 这样每一环天然是个正六边形，墙由格子逐个拼接而成，而不是把圆形墙硬切成格子。
// 这才是"同化=强制格式化成六边形秩序"的正确表达。
function hexRingIndex(x, y) {
  // 世界坐标 -> 相对迷宫中心的轴向坐标
  const px = (x - MAZE.cx), py = (y - MAZE.cy);
  const q = (2 / 3) * px / HEX_DQ * 1.5 / 1.5;    // 归一到列步
  const qa = (2 / 3) * px / (HEX_R * 1.5) * 1.5;
  // 直接用 gen 的晶格换算：q = px / HEX_DQ, r 由 py 与 q 推出
  const fq = px / HEX_DQ;
  const fr = (py - (fq * HEX_DR * 0.5)) / HEX_DR;
  // cube round
  let cx3 = fq, cz3 = fr, cy3 = -cx3 - cz3;
  let rx = Math.round(cx3), ry = Math.round(cy3), rz = Math.round(cz3);
  const dx3 = Math.abs(rx - cx3), dy3 = Math.abs(ry - cy3), dz3 = Math.abs(rz - cz3);
  if (dx3 > dy3 && dx3 > dz3) rx = -ry - rz;
  else if (dy3 > dz3) ry = -rx - rz;
  else rz = -rx - ry;
  // 六边形距离 = cube 距离
  return (Math.abs(rx) + Math.abs(ry) + Math.abs(rz)) / 2;
}

// ---- 世界坐标 -> 轴向格坐标 (q,r)，迷宫用格坐标直接生成，不用半径/角度 ----
// 之前用同心圆环 + 角度开口，反复 FAIL：六边形环上的格心到中心的欧氏距离在
// N*HEX_DR*0.866(边心) 到 N*HEX_DR(角点) 之间变动，任何依赖半径或弧长的命中判据
// 都会大量落空(实测环 27 只采到 12/162 格、通口 0 个)。
// 参考图③ 给的是正确结构：墙沿六边形的六个格方向走直线、拐直角，形成回廊。
// 在格坐标上生成，天然对齐格心，不存在采样落空。
// 世界坐标 -> 以迷宫中心为原点的 axial 六边形坐标。
//
// 这里踩过一个很贵的坑：物理柱阵用的是 **odd-q 偏移布局**
//     cY = y0 + row*HEX_DR + (col%2 ? HEX_DR*0.5 : 0)
// 偏移量在 0 和 +147 之间【交替】。而原来的实现按 **axial 布局** 反解
//     fr = (py - fq*HEX_DR*0.5) / HEX_DR
// 那个偏移量随 q【线性增长】(0, +147, +294, +441...)。两套布局根本不是一回事，
// |q|>1 之后映射就开始错位，迷宫的走廊和墙被打散到错误的物理格上，
// 走廊断成互不连通的孤岛 —— UE 审计 A6 长期卡在 15~35% 可达就是这么来的。
//
// 正确做法：先反解成 offset(col,row)，再用标准 odd-q -> axial 公式，最后减去中心。
function offsetToAxial(col, row) {
  return [col, row - ((col - (col & 1)) >> 1)];
}
function worldToAxial(x, y) {
  const x0 = UG.cx - UG.rx * 1.35, y0 = UG.cy - UG.ry * 1.35;
  const col = Math.round((x - x0) / HEX_DQ);
  const row = Math.round((y - y0 - ((col & 1) ? HEX_DR * 0.5 : 0)) / HEX_DR);
  const [aq, ar] = offsetToAxial(col, row);
  const [cq, cr] = offsetToAxial(MAZE.gq, MAZE.gr);
  return [aq - cq, ar - cr];
}

// (x,y) 是否落在某道六边形环墙上（= 不可通行的柱墙）  [旧实现，保留供对照]
// 返回 -1 表示不在墙上；否则返回它属于第几环
function mazeWallRing(x, y) {
  const ringIdx = hexRingIndex(x, y);
  if (ringIdx > MAZE.ringsHex) { return -1; }
  // 环的排布：内圈祭坛(0..altarR)，之后每 corridorW+1 圈里，最外一圈是墙
  const period = MAZE.corridorRings + 1;   // 走廊圈数 + 1 圈墙
  if (ringIdx <= MAZE.altarRings) { return -1; }   // 祭坛室，无墙
  const local = (ringIdx - MAZE.altarRings - 1) % period;
  if (local !== MAZE.corridorRings) { return -1; }  // 不是墙圈
  const wallNo = Math.floor((ringIdx - MAZE.altarRings - 1) / period) + 1;
  // 检查是不是通口：按角度
  const dx = x - MAZE.cx, dy = y - MAZE.cy;
  const th = Math.atan2(dy, dx);
  const gates = mazeGateAngles(wallNo);
  // 通口按"环上的归一化位置 t"开，不按弧长也不按半径。
  // 六边形环不是圆：环 N 上的格到中心的欧氏距离在 N*HEX_DR*0.866(边心) 到
  // N*HEX_DR(角点) 之间变动，任何依赖半径的命中判据都会大量落空 ——
  // 实测环 27 按角度+半径只采到 12/162 格且一个通口都没开，
  // 自检"入口走不到中心"就是这么来的。
  //
  // t = 方位角 / 2π，环上的格沿 t 均匀分布，所以"占 K 格"等价于"占 K/6N 的 t"。
  const t = ((th % (Math.PI * 2)) + Math.PI * 2) % (Math.PI * 2) / (Math.PI * 2);
  const cellsInRing = Math.max(6, 6 * ringIdx);
  const gateFrac = MAZE.gateCells / cellsInRing;
  for (const ga of gates) {
    const gt = ((ga % (Math.PI * 2)) + Math.PI * 2) % (Math.PI * 2) / (Math.PI * 2);
    let d = Math.abs(t - gt);
    if (d > 0.5) { d = 1 - d; }                 // 环上最短距离(跨 0 点)
    if (d < gateFrac) { return -1; }            // 是通口，可通行
  }
  return wallNo;
}

// 死胡同藏物点：从某道环墙向外/内伸出的短盲道。返回该点是否是死胡同内部
function mazeDeadEnd(x, y) {
  const ringIdx = hexRingIndex(x, y);
  if (ringIdx > MAZE.ringsHex || ringIdx <= MAZE.altarRings) { return false; }
  // 死胡同 = 从走廊向墙里凿进去一格的盲龛。挑墙圈的相邻内侧那一圈，
  // 在特定角度上把墙格改成走廊格 -> 形成一个只有一个入口的凹槽。
  const period = MAZE.corridorRings + 1;
  const local = (ringIdx - MAZE.altarRings - 1) % period;
  if (local !== MAZE.corridorRings) { return false; }   // 只在墙圈上开龛
  const wallNo = Math.floor((ringIdx - MAZE.altarRings - 1) / period) + 1;
  const th = Math.atan2(y - MAZE.cy, x - MAZE.cx);
  const rw = Math.max(1, ringIdx) * HEX_DR;
  for (let i = 0; i < MAZE.deadEnds; i++) {
    if (Math.floor(hexHash(i, 11, 3) * 8) + 1 !== wallNo) { continue; }
    const ang = hexHash(i, 13, 5) * Math.PI * 2;
    const halfArc = 260 / rw;
    const diff = Math.abs(((th - ang + Math.PI * 3) % (Math.PI * 2)) - Math.PI);
    if (diff < halfArc) { return true; }
  }
  return false;
}

const HEXQ = v => Math.round(v / STEP) * STEP;
const hexCovers = (x, y) => ugField(x, y) > 0.03;

function hexHash(a, b, c) {
  let h = Math.imul(a | 0, 73856093) ^ Math.imul(b | 0, 19349663) ^ Math.imul(c | 0, 83492791);
  h = Math.imul(h ^ (h >>> 13), 1274126177);
  return ((h ^ (h >>> 16)) >>> 0) / 4294967295;
}

// OBJ 写出器。两件上一版没做、直接导致"质感粗糙"的事：
//  1. 写显式面法线。原来一个 vn 都没有，只丢了个 's 1'，UE 只能自己算平滑法线，
//     硬边全被抹圆再炸成刺 —— 六棱柱看起来像融化的蜡，不像被切削的硅。
//  2. 绕序。OBJ 这边 Y 已经取负，那是一次镜像(det=-1)，几何绕序会整体翻面。
//     所以世界空间算好朝外的绕序之后，写出时必须再反一次。
// 三个材质槽。顶点绘制在这里不是"另一种做法"，是做不到：柱顶盖的六个顶点同时是
// 六个侧面的上沿顶点，顶点色逐顶点存、在三角形内插值，同一个顶点不可能既是粗糙
// 顶面又是纯黑镜面截面 —— 想要绝对锐利的材质分界线，顶点色只能给一片渐变涂抹。
// 而且重导入会作废顶点色（它绑死顶点数和顺序），usemtl 分组每次导入都白送。
const MAT_TOP = 0;    // 顶面：能站能踩，粗糙岩石
const MAT_SIDE = 1;   // 侧面：柱体外壁
const MAT_CUT = 2;    // 切口截面：被高维手术刀切断的面，纯黑镜面 + 顶沿青色边缘光
function HexMesh() {
  this.V = []; this.N = []; this.T = []; this.F = [[], [], []]; this.nk = new Map();
}
// vt 的 V 分量记录"沿这根柱子从底到顶的归一化高度"。切口材质靠它在 V≈1 处画出
// 那道极细的青线 —— 边缘光由材质算，不加任何额外几何。
HexMesh.prototype.v = function (x, y, z, t) {
  this.V.push('v ' + x.toFixed(1) + ' ' + (-y).toFixed(1) + ' ' + z.toFixed(1));
  this.T.push('vt 0.0000 ' + (t === undefined ? 1 : t).toFixed(4));
  return this.V.length;
};
HexMesh.prototype.n = function (nx, ny, nz) {
  const k = nx.toFixed(3) + '_' + ny.toFixed(3) + '_' + nz.toFixed(3);
  let i = this.nk.get(k);
  if (i === undefined) {
    this.N.push('vn ' + nx.toFixed(4) + ' ' + (-ny).toFixed(4) + ' ' + nz.toFixed(4));
    i = this.N.length; this.nk.set(k, i);
  }
  return i;
};
// a,b,c 按世界空间朝外绕序传入；这里负责翻面（OBJ 已把 Y 取负，是一次镜像）
HexMesh.prototype.f = function (a, b, c, n, slot) {
  const w = i => i + '/' + i + '/' + n;
  this.F[slot === undefined ? MAT_SIDE : slot].push('f ' + w(a) + ' ' + w(c) + ' ' + w(b));
};
// UE 的旧 OBJ 导入器会把 usemtl 分组合并成单个 section，三个材质槽根本保不住
// （import_materials 开关也救不回来）。所以切口面单独出一个网格文件：三个材质槽
// 本来就是三次绘制调用，拆成两个网格开销完全一样，但导入器不会合并。
// 顶点表整份复制到两个文件里 —— 三万个顶点的冗余，换一个确定能用的结果。
HexMesh.prototype.write = function (pathMain, pathCut) {
  const head = this.V.concat(this.T, this.N);
  fs.writeFileSync(pathMain, head.concat(this.F[MAT_TOP], this.F[MAT_SIDE]).join('\n'));
  if (pathCut) { fs.writeFileSync(pathCut, head.concat(this.F[MAT_CUT]).join('\n')); }
  return this.V.length;
};

// 闭合六棱柱：顶盖 + 底盖 + 六个绝对垂直的侧面。有厚度，不是纸片。
// cutMask 是六位位掩码，第 k 位为 1 表示第 k 个侧面是"被切断的截面"（临着坑洞或
// 竖井），走 MAT_CUT。这就是"绝对几何的断崖"：切口不是散落的碎块，是柱体侧壁本身，
// 天然就是直线和标准 120° 折角 —— 六边形镶嵌只能给出这两种边界。
function hexPrism(m, cx, cy, zTop, zBot, R, rot, cutMask) {
  const T = [], B = [];
  const H = Math.max(1, zTop - zBot);
  for (let k = 0; k < 6; k++) {
    const a = Math.PI / 3 * k + rot;
    const px = cx + Math.cos(a) * R, py = cy + Math.sin(a) * R;
    T.push(m.v(px, py, zTop, 1));
    B.push(m.v(px, py, zBot, 0));
  }
  const nT = m.n(0, 0, 1), nB = m.n(0, 0, -1);
  for (let k = 1; k < 5; k++) { m.f(T[0], T[k], T[k + 1], nT, MAT_TOP); }
  for (let k = 1; k < 5; k++) { m.f(B[0], B[k + 1], B[k], nB, MAT_SIDE); }
  for (let k = 0; k < 6; k++) {
    const j = (k + 1) % 6;
    const a = Math.PI / 3 * (k + 0.5) + rot;
    const ns = m.n(Math.cos(a), Math.sin(a), 0);
    const slot = (cutMask && (cutMask & (1 << k))) ? MAT_CUT : MAT_SIDE;
    m.f(T[k], B[k], B[j], ns, slot);
    m.f(T[k], B[j], T[j], ns, slot);
  }
}

let MAZE_FLOOR_G = 0;   // 迷宫地面高度：建高度场时填，出网格时用来统一柱底
function writeHexField(pathSolid, pathDeco) {
  // ---- 1. 铺格 ----
  const x0 = UG.cx - UG.rx * 1.35, x1 = UG.cx + UG.rx * 1.35;
  const y0 = UG.cy - UG.ry * 1.35, y1 = UG.cy + UG.ry * 1.35;
  const NQ = Math.ceil((x1 - x0) / HEX_DQ);
  const NR = Math.ceil((y1 - y0) / HEX_DR) + 1;
  const N = NQ * NR;
  const idx = (q, r) => q * NR + r;
  const cX = new Float64Array(N), cY = new Float64Array(N);
  const live = new Uint8Array(N);
  for (let q = 0; q < NQ; q++) for (let r = 0; r < NR; r++) {
    const x = x0 + q * HEX_DQ;
    const y = y0 + r * HEX_DR + (q % 2 ? HEX_DR * 0.5 : 0);
    const k = idx(q, r);
    cX[k] = x; cY[k] = y;
    // 迷宫足迹【无条件】置活，不能交给 hexCovers 的阈值去碰运气。
    // hexCovers 判的是 ugField > 0.03，ugField 是各 lobe 的平滑混合；把 lobe 半径从
    // 1.06 加到 1.30 之后"完全没有几何"的格子从 180 降到 29，但连通块数只从 37 掉到
    // 32 —— 因为落在阈值外的格子并不会变成洞，而是被后面的背景/梯田层接管，铺成
    // 别的高度。它在俯视图上看着是"墙"，实际是迷宫少了一格。
    // 而递归回溯出来的完美迷宫是一棵树：少一格就断一刀。所以这里必须是确定性的
    // 几何判据，不是场阈值。+3 环是给外圈墙留的厚度。
    const [_aq, _ar] = gridToAxial(q, r);
    const _md = (Math.abs(_aq) + Math.abs(_ar) + Math.abs(-_aq - _ar)) / 2;
    live[k] = (hexCovers(x, y) || _md <= MAZE.ringsHex + 3) ? 1 : 0;
  }
  // 平顶六边形 odd-q 偏移布局的邻居表（奇数列整体下移半行）
  const NB_EVEN = [[0, -1], [0, 1], [1, -1], [1, 0], [-1, -1], [-1, 0]];
  const NB_ODD = [[0, -1], [0, 1], [1, 0], [1, 1], [-1, 0], [-1, 1]];
  // 同样六个邻居，但按 hexPrism 侧面的顺序排：第 s 个侧面的外法线指向 30+60s 度，
  // 所以第 s 位对应的就是这张表的第 s 项。顺序错了，切口会开在错误的那一面上。
  const FACE_EVEN = [[1, 0], [0, 1], [-1, 0], [-1, -1], [0, -1], [1, -1]];
  const FACE_ODD = [[1, 1], [0, 1], [-1, 1], [-1, 0], [0, -1], [1, 0]];
  function nbrs(q, r) {
    const t = (q % 2 ? NB_ODD : NB_EVEN), out = [];
    for (let i = 0; i < 6; i++) {
      const nq = q + t[i][0], nr = r + t[i][1];
      if (nq >= 0 && nq < NQ && nr >= 0 && nr < NR && live[idx(nq, nr)]) out.push(idx(nq, nr));
    }
    return out;
  }

  // ---- 2. 竞技场高度：沿主干道传播，把每条道的坡度锁进预算内 ----
  // 预算 = 道长/列距 * STEP，即"这条道最多能爬多高，还能保证每级都 ≤ STEP"。
  // 直接取地形高度是不行的：MAIN 坐在天坑碎石锥上，比 MID 高约 9m，而两者只隔 48m
  // —— 那是 18.7°，用 294cm 踏面的六边形去铺就是 1m 一级的台阶，人上不去。
  // 六边形柱顶是离散台阶，能不能走取决于级高(riser)，不是坡度。
  const byName = {};
  for (const A of ARENAS) { byName[A.name] = A; }
  const AZ = { LZ: HEXQ(ugFloor(byName.LZ.x, byName.LZ.y)) };
  const laneInfo = [];
  for (let pass = 0, moved = true; moved && pass < 8; pass++) {
    moved = false;
    for (const L of LANES) {
      for (const dir of [[L.a, L.b], [L.b, L.a]]) {
        const from = dir[0], to = dir[1];
        if (AZ[from] === undefined || AZ[to] !== undefined) continue;
        const A = byName[from], B = byName[to];
        const len = Math.hypot(B.x - A.x, B.y - A.y);
        // 预算只能按走廊长度算，不是中心距：竞技场内部是锁平的，一厘米落差都摊不了。
        const corr = Math.max(HEX_DQ, len - A.r - B.r);
        const budget = (corr / HEX_DQ) * STEP * 0.9;   // 留 10% 余量给量化
        const want = HEXQ(ugFloor(B.x, B.y));
        AZ[to] = HEXQ(Math.max(AZ[from] - budget, Math.min(AZ[from] + budget, want)));
        moved = true;
      }
    }
  }

  // ---- 3. 高度场：竞技场锁平，主干道沿线插值，其余量化后当背景 ----
  const z = new Float64Array(N), walk = new Uint8Array(N), fixed = new Uint8Array(N);
  for (let k = 0; k < N; k++) { z[k] = ugFloor(cX[k], cY[k]); }
  function arenaAt(x, y) {
    let best = null, bd = 1e9;
    for (const A of ARENAS) {
      const d = Math.hypot(x - A.x, y - A.y);
      if (d <= A.r && d < bd) { bd = d; best = A; }
    }
    return best;
  }
  for (const A of ARENAS) {
    for (let k = 0; k < N; k++) {
      if (!live[k]) continue;
      const d = Math.hypot(cX[k] - A.x, cY[k] - A.y);
      if (d <= A.r) { z[k] = AZ[A.name]; fixed[k] = 1; walk[k] = 1; }
      // !fixed 是必须的：竞技场之间的 rOut 过渡圈会互相重叠，少了这个守卫，后一个
      // 竞技场的过渡圈会把前一个锁平的地面重新掰弯。
      else if (d <= A.rOut && !fixed[k]) {
        const t = (d - A.r) / (A.rOut - A.r);
        z[k] = AZ[A.name] * (1 - t) + z[k] * t;
      }
    }
  }
  for (const L of LANES) {
    const A = byName[L.a], B = byName[L.b];
    const vx = B.x - A.x, vy = B.y - A.y, L2 = vx * vx + vy * vy;
    const len = Math.sqrt(L2);
    const corr = Math.max(1, len - A.r - B.r);
    laneInfo.push({ n: L.a + '->' + L.b, len: len, corr: corr, dz: AZ[L.b] - AZ[L.a],
                    riser: Math.abs(AZ[L.b] - AZ[L.a]) * HEX_DQ / corr });
    for (let k = 0; k < N; k++) {
      if (!live[k] || fixed[k]) continue;
      let t = L2 > 0 ? ((cX[k] - A.x) * vx + (cY[k] - A.y) * vy) / L2 : 0;
      t = Math.max(0, Math.min(1, t));
      const d = Math.hypot(cX[k] - (A.x + vx * t), cY[k] - (A.y + vy * t));
      if (d > L.hw * 1.6) continue;
      // 沿线插值按"边沿到边沿"重映射：t*len 是离 A 圆心的距离，落差只能摊在
      // A.r .. len-B.r 这段真正的走廊上，否则接缝处会立起一堵墙。
      const u = Math.max(0, Math.min(1, (t * len - A.r) / corr));
      const target = AZ[L.a] * (1 - u) + AZ[L.b] * u;
      if (d <= L.hw) { z[k] = target; walk[k] = 1; }
      else if (!walk[k]) {
        const s = (d - L.hw) / (L.hw * 0.6);
        z[k] = target * (1 - s) + z[k] * s;
      }
    }
  }
  // 量化：走廊内 |Δz| ≤ STEP 的连续场，量化后仍 ≤ STEP（round 单调，差值不超过一格）
  for (let k = 0; k < N; k++) { z[k] = HEXQ(z[k]); }

  // ---- 3a. 迷宫层：照抄参考图③ 的六边形折线迷宫 ----
  // 在轴向格坐标上跑标准递归回溯(recursive backtracker)：
  //   格子分"房间格"和"墙格"，房间格按 2 格间隔布点，打通相邻房间即拆掉中间的墙格。
  // 递归回溯生成的是完美迷宫(perfect maze)：任意两点之间恰好一条路径，
  // 100% 连通、无环、天然有大量死胡同 —— 正好就是图③要的"死胡同=藏物点"。
  // 这比同心环+角度开口靠谱得多：全程在格坐标上操作，不存在采样落空。
  {
    const R = MAZE.ringsHex;                       // 迷宫半径(格)
    const key = (q, r) => (q + 200) * 1000 + (r + 200);
    const hexDist = (q, r) => (Math.abs(q) + Math.abs(r) + Math.abs(-q - r)) / 2;
    // 迷宫只占祭坛之外的环带：内边界 = altarRings+1，外边界 = R
    const inMaze = (q, r) => { const d = hexDist(q, r); return d > MAZE.altarRings && d <= R; };
    // 房间格：q,r 均为偶数。相邻房间相距 2 格，中间那格是墙。
    const isRoom = (q, r) => (q % 2 === 0) && (r % 2 === 0);
    const visited = new Set();
    const opened = new Set();                       // 被打通的墙格 key
    const DIRS = [[2, 0], [-2, 0], [0, 2], [0, -2], [2, -2], [-2, 2]];
    // 从中心(祭坛)开始回溯，保证中心一定连通到外圈
    // 起点必须在迷宫区内(祭坛之外)，否则回溯打通的墙格会被祭坛判据覆盖，
    // 整座迷宫和中心断开 —— 自检入口走不到中心抓的就是这个。
    const startQ = (MAZE.altarRings + 1) * 2, startR = 0;
    const stack = [[startQ, startR]];
    visited.add(key(startQ, startR));
    let guard = 0;
    while (stack.length && guard++ < 200000) {
      const [cq, cr] = stack[stack.length - 1];
      // 收集未访问的邻居房间
      const cand = [];
      for (let d = 0; d < DIRS.length; d++) {
        const nq = cq + DIRS[d][0], nr = cr + DIRS[d][1];
        if (!inMaze(nq, nr) || visited.has(key(nq, nr))) continue;
        cand.push(d);
      }
      if (!cand.length) { stack.pop(); continue; }
      // 确定性随机选一个方向
      const pick = cand[Math.floor(hexHash(cq, cr, stack.length) * cand.length) % cand.length];
      const nq = cq + DIRS[pick][0], nr = cr + DIRS[pick][1];
      // 打通中间那格墙
      opened.add(key(cq + DIRS[pick][0] / 2, cr + DIRS[pick][1] / 2));
      visited.add(key(nq, nr));
      stack.push([nq, nr]);
    }
    // ---- 编织(braiding)：给迷宫加环，别让它是一棵树 ----
    // 递归回溯出来的是【完美迷宫】：任意两点之间恰好一条路径，零冗余。
    // 好看，但在引擎里极其脆弱 —— 树上任何一条边断掉，它后面整棵子树就全部失联。
    // UE 实测：相邻格之间 201 次寻路只失败 5 次(2.5%)，可就这 5 处把 1412 格走廊
    // 切成 33 个连通块，从楼梯落点只走得到 17%，可达区是个环带(离中心 3663..9537cm)，
    // 内圈 12 环整个够不着。
    // 也就是说：只要迷宫是树，导航上任何一点点毛刺都会被放大成"整片区域进不去"。
    // 所以主动开一批额外的墙，让迷宫变成有环的(braided)。冗余路径一多，
    // 个别链接断了还有别的路绕过去。
    // 死胡同不会消失 —— 只打通约 18% 的候选墙，藏物点还有的是，
    // 而且有环的迷宫本来就更耐走，不会逼玩家一条道走到黑再原路退回。
    {
      let braided = 0;
      for (const rk of visited) {
        const rq = Math.floor(rk / 1000) - 200, rr = (rk % 1000) - 200;
        for (const [dq, dr] of DIRS) {
          const nq = rq + dq, nr = rr + dr;
          if (!inMaze(nq, nr) || !visited.has(key(nq, nr))) continue;
          const mid = key(rq + dq / 2, rr + dr / 2);
          if (opened.has(mid)) continue;
          // 确定性：同一对格子无论从哪一头看都得到同一个哈希，否则会开两次
          const a = Math.min(rk, key(nq, nr)), b = Math.max(rk, key(nq, nr));
          if (hexHash(a, b, 91) < 0.18) { opened.add(mid); braided++; }
        }
      }
      MAZE._braided = braided;
    }
    MAZE._rooms = visited.size; MAZE._openedN = opened.size;
    // 存到闭包外供下面建高度场用
    MAZE._visited = visited; MAZE._opened = opened;
    MAZE._key = key; MAZE._inMaze = inMaze; MAZE._isRoom = isRoom;
  }

  // ---- 3a-old. 迷宫层：按模式图第 2 组，同心环迷宫覆盖整个负一层核心区 ----
  // 迷宫走廊压平到统一高度(可走)，环墙抬成 320cm 高柱(翻不过去、看得见结构)。
  // 中心祭坛室单独抬起 40cm 做台座，视觉上是"污染源"的基座。
  const MAZE_FLOOR = AZ.LZ;                    // 迷宫地面 = 螺旋梯落点高度
  MAZE_FLOOR_G = MAZE_FLOOR;                   // 供下面出网格时统一柱底用
  const mazeCell = new Int8Array(N);           // 0=非迷宫 1=走廊 2=墙 3=死胡同 4=祭坛
  {
    const { _visited: visited, _opened: opened, _key: key, _inMaze: inMaze,
            _isRoom: isRoom } = MAZE;
    for (let k = 0; k < N; k++) {
      if (!live[k]) continue;
      const [q, r] = gridToAxial((k / NR) | 0, k - (((k / NR) | 0) * NR));
      const hexD = (Math.abs(q) + Math.abs(r) + Math.abs(-q - r)) / 2;
      // 迷宫外圈 3 环做成实心外墙，把迷宫和外面的梯田彻底隔断。
      // 不隔断的后果实测过：外围梯田(-3760)比迷宫地面(-3680)低 80cm，80 > MaxStepHeight 45，
      // 导航在边界上断成孤岛，UE 审计 A6 只有 30% 可达。迷宫必须是一个封闭的可走连通域，
      // 唯一的进出口是入口和斜向上通道。
      if (hexD > MAZE.ringsHex && hexD <= MAZE.ringsHex + 3) {
        // 入口(螺旋梯落点)和出口(斜向上通道起点)处要留缺口，否则外墙把玩家关在外面/里面
        const dEnt = Math.hypot(cX[k] - ENTRY.x, cY[k] - ENTRY.y);
        const dExt = Math.hypot(cX[k] - EXIT_RAMP_X, cY[k] - EXIT_RAMP_Y);
        if (dEnt < 1600 || dExt < 1400) {
          mazeCell[k] = 1; z[k] = MAZE_FLOOR; walk[k] = 1; fixed[k] = 1;
        } else {
          mazeCell[k] = 3; z[k] = MAZE_FLOOR + 560; fixed[k] = 1; walk[k] = 0;
        }
        continue;
      }
      // 迷宫之外一律实心岩。竞技场系统是迷宫之前的旧设计，两者在同一块地上打架：
      // 实测 MAIN 锁在 -3560、MID 锁在 -3760，差 200cm，正是 A8 报的最坏台阶，
      // 也是 A6 断链的根源之一。负一层现在只有迷宫，外面不该留任何可走面。
      if (hexD > MAZE.ringsHex + 3) {
        mazeCell[k] = 3; z[k] = MAZE_FLOOR + 900; fixed[k] = 1; walk[k] = 0;
        continue;
      }
      if (hexD <= MAZE.altarRings) {
        // 中心祭坛室：藏物点 + 污染源
        mazeCell[k] = 4; z[k] = MAZE_FLOOR; walk[k] = 1; fixed[k] = 1;
        continue;
      }
      const isRoomCell = visited.has(key(q, r));
      const isOpenWall = opened.has(key(q, r));
      if (isRoomCell || isOpenWall) {
        // 地面必须绝对平。原来给走廊压低 60cm 做"视觉分层"，但
        // AgentMaxStepHeight=35 / MaxStepHeight=45，60cm 是一堵看不见的墙，
        // UE 实测把迷宫切成导航孤岛，可达率 18%。走廊/龛的区分交给材质和灯光。
        mazeCell[k] = 1; z[k] = MAZE_FLOOR; walk[k] = 1; fixed[k] = 1;
      } else {
        // 墙由高度不同的六棱柱相邻拼接而成。逐柱哈希取高度 —— 全部一样高读起来
        // 像挤出来的一堵墙，高低错落才像"柱子拼成的墙"(用户明确要求)。
        // 下限 260cm 保证翻不过去(玩家跳高约 100cm)，上限 520cm 保持通透感。
        // 墙高下限 320cm：实测 260 那版在关卡里有 2347 个样本低于 240、最低 80cm
        // （柱底被地形起伏吃掉了）。迷宫的可玩性建立在"看不见下一个路口"上，
        // 参差不齐 = 遮挡失效。抖动只往上加，保证下限。
        // 墙高用【平滑场】而不是逐格随机哈希。两个理由：
        //  1. 逐格随机让相邻墙顶差最多 200cm，UE 审计 A8 分不出"墙顶高差"和"地面台阶"，
        //     一律报违规；平滑场把相邻差压到一格 STEP 以内。
        //  2. 随机读起来是噪声，平滑起伏读起来是"被同一股力量格式化出来的结构"，
        //     后者才是同化该有的样子。
        // 尺度 2600cm，相邻格 294cm 只走 11% 的行程，量化后基本是 0 或 40。
        mazeCell[k] = 2;
        const wh = fbm(cX[k], cY[k], 2600, 2, 91);
        z[k] = MAZE_FLOOR + 320 + Math.round(wh * 200 / STEP) * STEP;
        fixed[k] = 1; walk[k] = 0;
      }
    }
    // 迷宫入口：楼梯落点(ENTRY)那一带强制凿成走廊。回溯迷宫是在格坐标上生成的，
    // 落点那格是走廊还是墙完全看运气 —— 实测就是一堵墙，玩家下楼梯直接撞墙。
    // 参考图③ 的入口是明确开在最外圈上的口子，必须显式保证。
    for (let k = 0; k < N; k++) {
      if (!mazeCell[k]) continue;
      const dEnt = Math.hypot(cX[k] - ENTRY.x, cY[k] - ENTRY.y);
      if (dEnt > 1400) continue;            // 落点周围 14m 范围
      mazeCell[k] = 1; z[k] = MAZE_FLOOR; walk[k] = 1; fixed[k] = 1;
    }

    // 逃生地道出洞口的那一段必须给它让路。地道以 9 度往上爬，出洞口十几环还在
    // 迷宫墙的高度带里(墙顶到 MAZE_FLOOR+520)，迷宫墙会横在地道中间。
    // UE 实测：相邻环之间 17 次寻路只断了 1 处，就在第 7->8 环 —— 恰好是原来那个
    // 1400cm 出口缺口的边界之外。而地道也是一条链：断一环，后面 110 环全部失联，
    // 玩家从迷宫走不到地表。
    // 所以不是把缺口半径调大，而是沿着地道中心线逐点开槽。
    for (const [tx, ty, tz] of TUN_PATH) {
      for (let k = 0; k < N; k++) {
        if (!mazeCell[k]) continue;
        if (Math.hypot(cX[k] - tx, cY[k] - ty) > 480) continue;
        mazeCell[k] = 1; z[k] = MAZE_FLOOR; walk[k] = 1; fixed[k] = 1;
      }
    }

    // 出口通路：从祭坛一路凿到斜向上通道的洞口。
    //
    // 只在洞口周围凿个 14m 的圆是不够的 —— UE 实测祭坛到洞口无路径(误差 72m)：
    // 洞口是通了，但它和迷宫主体之间隔着回溯生成的墙。玩家在迷宫里找到出口也进不去。
    // 出口是"隐藏结局"链条的最后一环，不能靠运气连通，必须显式保证。
    //
    // 凿成一条 5m 半宽的直带：既是确定的通路，也读得出"抵抗派挖出来的撤离线"。
    {
      const ax = MAZE.cx, ay = MAZE.cy;
      const bx = EXIT_RAMP_X, by = EXIT_RAMP_Y;
      const vx = bx - ax, vy = by - ay;
      const L2 = vx * vx + vy * vy || 1;
      for (let k = 0; k < N; k++) {
        if (!mazeCell[k]) continue;
        let t = ((cX[k] - ax) * vx + (cY[k] - ay) * vy) / L2;
        t = Math.max(0, Math.min(1, t));
        const dx = cX[k] - (ax + vx * t), dy = cY[k] - (ay + vy * t);
        if (Math.hypot(dx, dy) > 500) continue;      // 半宽 5m
        mazeCell[k] = 1; z[k] = MAZE_FLOOR; walk[k] = 1; fixed[k] = 1;
      }
      // 洞口本身再多凿一圈，让通道口有个可站的前室
      for (let k = 0; k < N; k++) {
        if (!mazeCell[k]) continue;
        if (Math.hypot(cX[k] - bx, cY[k] - by) > 1400) continue;
        mazeCell[k] = 1; z[k] = MAZE_FLOOR; walk[k] = 1; fixed[k] = 1;
      }
    }

    // 祭坛门：回溯起点位于祭坛外，祭坛与它之间那几格默认是墙，会把中心封死。
    // 沿 +q 方向从祭坛边缘凿到回溯起点，形成唯一的祭坛入口(符合中心必为藏物点
    // 的设计 —— 中心是终点，只有一条路进去)。
    for (let k = 0; k < N; k++) {
      if (mazeCell[k] !== 2) continue;
      const [q, r] = gridToAxial((k / NR) | 0, k - (((k / NR) | 0) * NR));
      const d = (Math.abs(q) + Math.abs(r) + Math.abs(-q - r)) / 2;
      // 门必须一路凿到回溯起点所在的环，中间留一格墙就把祭坛彻底封死。
      // 回溯起点 sq0 = (altarRings+1)*2 = 8，所以门要开到 altarRings+5 = 8，
      // 正好接上第一间房 (8,0)。原来写 +3（只到 d=6）在第 7 环留了一格墙，
      // 实测祭坛 0/37 可达 —— 中心藏物点是隐藏结局的解锁点，断在这里等于整条链废掉。
      // 用 |r|<=1 的带宽而不是 r===0：worldToAxial 是取整的，那条射线上未必有格子
      // 恰好落在 r=0，写死会让门只开出零星几格甚至一格都开不出来。
      if (Math.abs(r) <= 1 && q > 0 && d > MAZE.altarRings && d <= MAZE.altarRings + 5) {
        mazeCell[k] = 1; z[k] = MAZE_FLOOR; walk[k] = 1;
      }
    }

    // 死胡同标记：房间格且只有 1 个已打通的邻居 -> 藏物点
    const DIRS = [[2, 0], [-2, 0], [0, 2], [0, -2], [2, -2], [-2, 2]];
    for (let k = 0; k < N; k++) {
      if (mazeCell[k] !== 1) continue;
      const [q, r] = gridToAxial((k / NR) | 0, k - (((k / NR) | 0) * NR));
      if (!visited.has(key(q, r))) continue;
      let deg = 0;
      for (const [dq, dr] of DIRS) {
        if (opened.has(key(q + dq / 2, r + dr / 2))) deg++;
      }
      if (deg === 1) { mazeCell[k] = 3; }        // 死胡同 = 藏物点
    }
  }

  // ---- 3b. 梯田层：走廊外做离散大平台，参考图剖面里那种"越深越同化"的分层结构 ----
  // 首版做成 200cm 一阶，视觉上像剖面图但玩家跳不上去(UE 跳高约 100cm)。改成
  // "多阶叠加"：走廊外按离裂隙的距离分成同心环，环与环之间的高差用**多级 40cm**
  // 台阶实现 —— 视觉上仍然是可辨识的分层地貌，但每一级都是走廊不变量允许的
  // MaxStepHeight 以内，玩家可以自由攀走。
  // 越靠裂隙越低(反映"深处更被同化")但可达性 100% 保住。
  const RIFT_X = -9000, RIFT_Y = -7000;
  for (let k = 0; k < N; k++) {
    if (!live[k] || walk[k] || fixed[k]) continue;
    const dRift = Math.hypot(cX[k] - RIFT_X, cY[k] - RIFT_Y);
    // 每 5000cm 一环 = 一阶 40cm，最深处相对 MAIN 低 4 阶(160cm)
    const rings = Math.min(6, Math.max(0, Math.floor((dRift - 2000) / 5000)));
    z[k] = HEXQ(AZ.MAIN - 4 * STEP + rings * STEP);
  }

  // !mazeCell 是必须的：这个循环在迷宫建完之后跑，没有守卫的话会把迷宫的地面和墙
  // 直接推平成竞技场高度。UE 实测过后果 —— AZ.MID=-3760 覆盖出 590 个比迷宫地面低
  // 80cm 的格，AZ.MAIN=-3560 覆盖出 948 个高 120cm 的格，两者相差 200cm，
  // 正是 A8 报的最坏台阶，也是 A6 可达率上不去的主因。
  // （梯田循环有 fixed 守卫所以躲过了，这个循环一个守卫都没有。）
  for (const A of ARENAS) for (let k = 0; k < N; k++) {
    if (live[k] && !mazeCell[k] && Math.hypot(cX[k] - A.x, cY[k] - A.y) <= A.r) {
      z[k] = AZ[A.name];
    }
  }

  // ---- 4. 掩体：成簇布置，中心留焦点、边沿留绕后路，绝不参与地面 ----
  const tier = new Int16Array(N);
  const P = 4;                                    // 簇间距 4 格 ≈ 11.8m
  for (let bq = 0; bq * P < NQ; bq++) for (let br = 0; br * P < NR; br++) {
    const u = hexHash(bq, br, 1);
    const aq = bq * P + Math.floor(hexHash(bq, br, 2) * P);
    const ar = br * P + Math.floor(hexHash(bq, br, 3) * P);
    if (aq >= NQ || ar >= NR) continue;
    const ak = idx(aq, ar);
    if (!live[ak] || !walk[ak]) continue;
    // 迷宫格绝对不能撒掩体柱。掩体是 95/190/330cm 的实心柱，撒进走廊就是 190~330cm
    // 的台阶 —— UE 实测 A8 有 1756 处相邻高差超 40cm(最坏 200cm)，A6 可达率被打到 15%。
    // 迷宫的遮挡由环墙提供，走廊里必须一马平川。
    if (mazeCell[ak]) continue;
    const A = arenaAt(cX[ak], cY[ak]);
    if (A) {
      const d = Math.hypot(cX[ak] - A.x, cY[ak] - A.y);
      if (d < A.r * 0.22 || d > A.r * 0.88) continue;   // 中心是交战焦点，边沿是flank路线
    }
    if (u > (A ? A.cover : 0.09) * 6.5) continue;
    const t0 = HEX_TIERS[Math.floor(hexHash(bq, br, 4) * HEX_TIERS.length) % HEX_TIERS.length];
    tier[ak] = t0;
    // 拉 1~2 个邻格成簇：掩体要有厚度，一根杆子挡不住人
    const nb = nbrs(aq, ar);
    const extra = 1 + Math.floor(hexHash(bq, br, 5) * 2);
    for (let e = 0; e < extra && nb.length; e++) {
      const nk = nb[Math.floor(hexHash(bq, br, 6 + e) * nb.length) % nb.length];
      if (walk[nk] && !tier[nk] && !fixed[nk] === !fixed[ak]) {
        tier[nk] = e === 0 ? t0 : Math.max(95, t0 - 95);
      }
    }
  }

  // ---- 4b. 迷宫地面复核（出网格前最后一道，谁也别想再动它）----
  // 递归回溯生成的是【完美迷宫】= 一棵树：任意删掉一格就断成两截。
  // 单独拿出来验过：714 个房间 + 713 条打通的墙 = 1427 格，连通块数 1。
  // 但画到高度场上之后 UE 实测只剩 1412 格可走、33 个连通块，从入口只走得到 1%。
  // 前面竞技场/梯田/掩体/背景各层都已经加了 mazeCell 守卫，可那些守卫说的是
  // "别碰迷宫"，不是"迷宫一定在"—— 少十几格就足够把一棵树打成几十块。
  // 所以这里不再追是谁偷走的，直接在出网格前把不变量重申一遍：
  // 理想走廊集合(visited ∪ opened) + 祭坛室，全部压到 MAZE_FLOOR、可走、无掩体。
  {
    const { _visited: vs, _opened: op, _key: kk } = MAZE;
    let repaired = 0;
    for (let k = 0; k < N; k++) {
      const gq = (k / NR) | 0, gr = k - gq * NR;
      const [q, r] = gridToAxial(gq, gr);
      const d = (Math.abs(q) + Math.abs(r) + Math.abs(-q - r)) / 2;
      if (d > MAZE.ringsHex) continue;
      if (!(d <= MAZE.altarRings || vs.has(kk(q, r)) || op.has(kk(q, r)))) continue;
      if (!live[k] || z[k] !== MAZE_FLOOR || !walk[k] || tier[k]) repaired++;
      live[k] = 1; z[k] = MAZE_FLOOR; walk[k] = 1; fixed[k] = 1; tier[k] = 0;
      if (mazeCell[k] !== 3) mazeCell[k] = mazeCell[k] === 4 ? 4 : 1;
    }
    MAZE._repaired = repaired;
  }

  // ---- 5. 出网格 ----
  const solid = new HexMesh(), deco = new HexMesh();
  let cols = 0, ceilCols = 0, coverCols = 0, cutCols = 0, minClear = 1e9;
  for (let q = 0; q < NQ; q++) for (let r = 0; r < NR; r++) {
    const k = idx(q, r);
    if (!live[k]) continue;
    const cx = cX[k], cy = cY[k];
    const top = z[k] + tier[k];
    // 柱底一律扎到自然岩面以下，永远不会是悬空平台
    // 迷宫区柱底必须统一：ugFloor 是连续噪声场，直接拿来当柱底会让相邻柱的底面
    // 参差不齐、地板读起来是波浪。迷宫地面是人造铺装，必须绝对平。
    const bot = mazeCell[k]
      ? MAZE_FLOOR_G - 900                       // 迷宫：统一深埋，全部落地
      : Math.min(z[k], ugFloor(cx, cy)) - UG_SKIN;
    // 临空的侧面 = 被切断的截面。六边形镶嵌的边界只可能是直线或标准 120° 折角，
    // 所以"绝对几何的断崖"不需要额外约束，它就是这个铺法的性质。
    const FT = (q % 2 ? FACE_ODD : FACE_EVEN);
    let cut = 0;
    for (let s = 0; s < 6; s++) {
      const nq = q + FT[s][0], nr = r + FT[s][1];
      if (nq < 0 || nq >= NQ || nr < 0 || nr >= NR || !live[idx(nq, nr)]) { cut |= (1 << s); }
    }
    if (cut) { cutCols++; }
    hexPrism(solid, cx, cy, top, bot, HEX_R, 0, cut);
    cols++;
    if (tier[k]) coverCols++;

    // 顶板下垂柱：只做背景奇观，一律不落在走廊网络上方，也不进碰撞网格。
    // 上一版铺满了整个顶板，既是视觉噪音，也把可走空间的净空啃掉了。
    if (walk[k] || ugOpen(cx, cy)) {
      if (walk[k]) { minClear = Math.min(minClear, ugCeil(cx, cy) - top); }
      continue;
    }
    const h = hexHash(q, r, 9);
    if (h > 0.72) {
      const cz = ugCeil(cx, cy);
      hexPrism(deco, cx, cy, cz, cz - (200 + h * 900), HEX_R * 0.82, 0.26);
      ceilCols++;
    }
  }
  const vs = solid.write(pathSolid, pathSolid.replace('hexfield', 'hexcut'));
  const vd = deco.write(pathDeco);

  // ---- 6. 自检：不变量实测，不靠看截图 ----
  let maxRiser = 0, badPairs = 0, walkCells = 0;
  for (let q = 0; q < NQ; q++) for (let r = 0; r < NR; r++) {
    const k = idx(q, r);
    if (!live[k] || !walk[k] || tier[k]) continue;
    if (mazeCell[k]) continue;   // 迷宫区单独验，不混进主走廊网络统计
    walkCells++;
    const nb = nbrs(q, r);
    for (let i = 0; i < nb.length; i++) {
      const nk = nb[i];
      if (!walk[nk] || tier[nk] || mazeCell[nk]) continue;
      const d = Math.abs(z[k] - z[nk]);
      if (d > maxRiser) maxRiser = d;
      if (d > STEP) badPairs++;
    }
  }
  // 连通性：从 LZ 出发只走 ≤STEP 的相邻可走格，看能不能到齐所有竞技场
  const q0 = [], seenC = new Uint8Array(N);
  for (let k = 0; k < N; k++) {
    if (live[k] && walk[k] && !tier[k]
        && Math.hypot(cX[k] - byName.LZ.x, cY[k] - byName.LZ.y) < byName.LZ.r) {
      q0.push(k); seenC[k] = 1;
    }
  }
  for (let head = 0; head < q0.length; head++) {
    const k = q0[head], q = (k / NR) | 0, r = k % NR;
    const nb = nbrs(q, r);
    for (let i = 0; i < nb.length; i++) {
      const nk = nb[i];
      if (seenC[nk] || !walk[nk] || tier[nk]) continue;
      if (Math.abs(z[k] - z[nk]) > STEP) continue;
      seenC[nk] = 1; q0.push(nk);
    }
  }
  // 递归回溯生成的是完美迷宫(perfect maze)：任意两点恰好一条路径，100% 连通。
  // 所以不需要事后凿墙修复 —— 连通性由算法保证，不是靠验证后打补丁。

  // ---- 迷宫自检 ----
  // BFS 必须用 axial 邻居：mazeCell 是按 axial 坐标判定的，而 nbrs() 走的是柱阵的
  // odd-q offset 邻居表。两套坐标系的相邻不是一回事，用 nbrs() 会让 BFS 在迷宫
  // 里随机漏格 —— 之前外圈可达率只有 30~60%、怎么调参都不满就是这个原因。
  const axIdx = new Map();
  for (let k = 0; k < N; k++) {
    if (!live[k] || !mazeCell[k]) continue;
    const [q, r] = gridToAxial((k / NR) | 0, k - (((k / NR) | 0) * NR));
    axIdx.set(q * 1000 + r, k);
  }
  const AXNB = [[1,0],[1,-1],[0,-1],[-1,0],[-1,1],[0,1]];
  function axNbrs(k) {
    const [q, r] = gridToAxial((k / NR) | 0, k - (((k / NR) | 0) * NR));
    const out = [];
    for (const [dq, dr] of AXNB) {
      const nk = axIdx.get((q + dq) * 1000 + (r + dr));
      if (nk !== undefined) out.push(nk);
    }
    return out;
  }

  // ---- 迷宫自检：从入口(圆心外围最外环)必须能走到中心祭坛 ----
  let mazeStat = { corridor: 0, wall: 0, dead: 0, altar: 0, reachAltar: 0, reachDead: 0,
                   rooms: MAZE._rooms, openedWalls: MAZE._openedN };
  let ringSeenOuter = {}, ringTotalOuter = {};
  {
    for (let k = 0; k < N; k++) {
      if (mazeCell[k] === 1) mazeStat.corridor++;
      else if (mazeCell[k] === 2) mazeStat.wall++;
      else if (mazeCell[k] === 3) mazeStat.dead++;
      else if (mazeCell[k] === 4) mazeStat.altar++;
    }
    // BFS 只从真实入口出发。旧版把所有外圈走廊都当种子，会把彼此隔绝的外圈孤岛
    // 一并标成“可达”，正好漏掉 UE A6 抓到的北侧断路。
    const seenM = new Uint8Array(N); const qM = [];
    let entrySeed = -1, entrySeedD = Infinity;
    for (let k = 0; k < N; k++) {
      if (!live[k] || mazeCell[k] !== 1) continue;
      const d = Math.hypot(cX[k] - ENTRY.x, cY[k] - ENTRY.y);
      if (d < entrySeedD) { entrySeedD = d; entrySeed = k; }
    }
    if (entrySeed >= 0) { seenM[entrySeed] = 1; qM.push(entrySeed); }
    mazeStat.bfsSeeds = qM.length;
    const ringSeen = {}, ringTotal = {};
    for (let k = 0; k < N; k++) {
      if (!live[k] || mazeCell[k] === 0) continue;
      const ri = hexRingIndex(cX[k], cY[k]);
      ringTotal[ri] = (ringTotal[ri]||0)+1;
    }
    for (let head = 0; head < qM.length; head++) {
      const k = qM[head];
      for (const nk of axNbrs(k)) {
        if (seenM[nk] || mazeCell[nk] === 2 || mazeCell[nk] === 0) continue;
        if (Math.abs(z[k] - z[nk]) > STEP) continue;
        seenM[nk] = 1; qM.push(nk);
      }
    }
    ringTotalOuter = ringTotal;
    ringSeenOuter = ringSeen;
    for (let k = 0; k < N; k++) {
      if (!seenM[k]) continue;
      const ri2 = hexRingIndex(cX[k], cY[k]);
      ringSeen[ri2] = (ringSeen[ri2]||0)+1;
      if (mazeCell[k] === 4) mazeStat.reachAltar++;
      if (mazeCell[k] === 3) mazeStat.reachDead++;
    }
  }

  mazeStat.ringDiag = [];
  for (let ri = 0; ri <= MAZE.ringsHex; ri++) {
    mazeStat.ringDiag.push(ri + ':' + (ringSeenOuter[ri]||0) + '/' + (ringTotalOuter[ri]||0));
  }
  const arenaStat = ARENAS.map(A => {
    let cells = 0, reach = 0, cov = 0;
    for (let k = 0; k < N; k++) {
      if (!live[k] || Math.hypot(cX[k] - A.x, cY[k] - A.y) > A.r) continue;
      cells++;
      if (tier[k]) cov++;
      if (seenC[k]) reach++;
    }
    const hexArea = 1.5 * Math.sqrt(3) * HEX_R * HEX_R / 1e4;   // m2/格
    return { name: A.name, z: AZ[A.name], cells: cells,
             area: cells * hexArea, side: Math.sqrt(cells * hexArea),
             cover: cells ? cov / cells : 0, reach: cells ? reach / cells : 0,
             clear: ugCeil(A.x, A.y) - AZ[A.name] };
  });
  return { maze: mazeStat,
           cols: cols, ceilCols: ceilCols, coverCols: coverCols, cutCols: cutCols,
           cutFaces: solid.F[MAT_CUT].length, topFaces: solid.F[MAT_TOP].length,
           sideFaces: solid.F[MAT_SIDE].length, verts: vs, decoVerts: vd,
           walkCells: walkCells, maxRiser: maxRiser, badPairs: badPairs,
           reached: q0.length, minClear: minClear, arenas: arenaStat, lanes: laneInfo };
}

function writeUnderground(path) {
  const cell = 200;
  const x0 = UG.cx - UG.rx * 1.4, x1 = UG.cx + UG.rx * 1.4;
  const y0 = UG.cy - UG.ry * 1.4, y1 = UG.cy + UG.ry * 1.4;
  const nx = Math.floor((x1 - x0) / cell), ny = Math.ceil((y1 - y0) / (cell * HEXROW));
  const V = [], F = [];
  const push = (x, y, z) => { V.push('v ' + x.toFixed(1) + ' ' + (-y).toFixed(1) + ' ' + z.toFixed(1)); return V.length; };
  let QPAR = 0;   // 由外层循环按行号设置
  const quad = (a, b, c, d) => {
    // 对角线随行奇偶翻转，和 writeSurface 一致。不翻的话所有三角形朝同一个方向倒，
    // 晶格退化成一排斜条纹，六边形对称性就没了，折线又会沿一个固定方向走。
    if (QPAR) { F.push('f ' + a + ' ' + c + ' ' + b); F.push('f ' + a + ' ' + d + ' ' + c); }
    else      { F.push('f ' + a + ' ' + d + ' ' + b); F.push('f ' + b + ' ' + d + ' ' + c); }
  };
  // 同样换成三角晶格：地下层的洞顶、崖面和边界墙原本全是轴对齐的方块阶地，
  // 和柱阵的六边形语言完全对不上。
  const XY = (i, j) => [latX(x0, i, j, cell), latY(y0, j, cell)];
  const inside = (i, j) => { const [x, y] = XY(i, j); return ugField(x, y) > 0; };
  const cellIn = (i, j) => inside(i, j) && inside(i + 1, j) && inside(i + 1, j + 1) && inside(i, j + 1);
  const cellOpen = (i, j) => { for (const [a, b] of [[i,j],[i+1,j],[i+1,j+1],[i,j+1]]) { const [x, y] = XY(a, b); if (ugOpen(x, y)) return true; } return false; };

  const fIdx = new Map(), cIdx = new Map();
  const fv = (i, j) => { const k = i + ',' + j; if (!fIdx.has(k)) { const [x, y] = XY(i, j); fIdx.set(k, push(x, y, ugFloor(x, y))); } return fIdx.get(k); };
  const cv = (i, j) => { const k = i + ',' + j; if (!cIdx.has(k)) { const [x, y] = XY(i, j); cIdx.set(k, push(x, y, ugCeil(x, y))); } return cIdx.get(k); };
  // 皮层：洞底往下、洞顶往上各挤一层，让它们是实体而不是纸
  const fbIdx = new Map(), ctIdx = new Map();
  const fb = (i, j) => { const k = i + ',' + j; if (!fbIdx.has(k)) { const [x, y] = XY(i, j); fbIdx.set(k, push(x, y, ugFloor(x, y) - UG_SKIN)); } return fbIdx.get(k); };
  const ct = (i, j) => { const k = i + ',' + j; if (!ctIdx.has(k)) { const [x, y] = XY(i, j); ctIdx.set(k, push(x, y, ugCeil(x, y) + UG_SKIN)); } return ctIdx.get(k); };

  // 柱阵覆盖到的地方不能再铺这张地板：两套地面同时存在会互相穿插、Z-fighting，
  // 而且量化过的柱顶和连续的 ugFloor 高度对不上，玩家会踩进一张斜插进柱子的斜面。
  // 柱阵就是地面，这张片只留在柱阵铺不到的边缘。
  const hexCell = (i, j) => {
    for (const [a, b] of [[i, j], [i + 1, j], [i + 1, j + 1], [i, j + 1]]) {
      const [x, y] = XY(a, b);
      if (!hexCovers(x, y)) return false;
    }
    return true;
  };

  let open = 0, tot = 0, floored = 0;
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
    QPAR = j & 1;
    if (!cellIn(i, j)) continue;
    tot++;
    const op = cellOpen(i, j);
    if (op) open++;
    if (!hexCell(i, j)) {
      floored++;
      quad(fv(i, j), fv(i + 1, j), fv(i + 1, j + 1), fv(i, j + 1));
      // 洞底的背面：不加的话从更低处或穿模时会直接看穿
      quad(fb(i, j), fb(i, j + 1), fb(i + 1, j + 1), fb(i + 1, j));
    }
    if (!op) {
      quad(cv(i, j), cv(i, j + 1), cv(i + 1, j + 1), cv(i + 1, j));
      // 洞顶的上表面：洞顶同样是片，从上面看下来会穿
      quad(ct(i, j), ct(i + 1, j), ct(i + 1, j + 1), ct(i, j + 1));
    }
    // walls where the chamber ends, or where the surviving roof meets the collapse
    for (const [di, dj, e] of [[1,0,[[i+1,j],[i+1,j+1]]],[-1,0,[[i,j+1],[i,j]]],[0,1,[[i+1,j+1],[i,j+1]]],[0,-1,[[i,j],[i+1,j]]]]) {
      const [[a0,b0],[a1,b1]] = e;
      if (!cellIn(i + di, j + dj)) {
        // 边界墙从洞底一路砌到 SURFACE —— 完整的地壳剖面，不是砌到洞顶为止。
        // 原来停在洞顶，导致洞顶到地面之间那 15–25m 的岩层完全没有几何体，
        // 从洞里往侧面看就是一片空气。第一层的地壳必须在剖面上真的存在。
        const [xA, yA] = XY(a0, b0), [xB, yB] = XY(a1, b1);
        quad(push(xA, yA, ugFloor(xA, yA) - UG_SKIN), push(xB, yB, ugFloor(xB, yB) - UG_SKIN),
             push(xB, yB, surface(xB, yB)), push(xA, yA, surface(xA, yA)));
      } else if (!op && cellOpen(i + di, j + dj)) {
        const [xA, yA] = XY(a0, b0), [xB, yB] = XY(a1, b1);
        quad(cv(a0, b0), cv(a1, b1), push(xB, yB, surface(xB, yB)), push(xA, yA, surface(xA, yA)));
      }
    }
  }
  fs.writeFileSync(path, ['s 1'].concat(V, F).join('\n'));
  return { tot, open, floored, verts: V.length };
}

// ---------------- water ----------------
// 地下水汇聚区 from the karst section: the water that vanished into the swallow holes
// collects at the low point of layer two. Emitted as a MESH covering only the cells that
// are actually below the water line -- a single big plane would cut through the walls
// and read as a bug rather than a pool.
const WATER_Z = UG.floor + 620;
function writeWater(path) {
  const cell = 200;
  const x0 = UG.cx - UG.rx * 1.4, x1 = UG.cx + UG.rx * 1.4;
  const y0 = UG.cy - UG.ry * 1.4, y1 = UG.cy + UG.ry * 1.4;
  const nx = Math.floor((x1 - x0) / cell), ny = Math.floor((y1 - y0) / cell);
  const V = [], F = [];
  const idx = new Map();
  const vert = (i, j) => {
    const k = i + ',' + j;
    if (!idx.has(k)) {
      const x = x0 + i * cell, y = y0 + j * cell;
      V.push('v ' + x.toFixed(1) + ' ' + (-y).toFixed(1) + ' ' + WATER_Z.toFixed(1));
      idx.set(k, V.length);
    }
    return idx.get(k);
  };
  // 迷宫footprint 必须排掉。水面在 WATER_Z = UG.floor+620 = -2980，而迷宫地面在
  // -3680、墙顶最高才 -3180 —— 水面比整座迷宫还高，等于把负一层整个泡在 7 米水下。
  // 判据里用的是 ugFloor(连续噪声高度场)，它不知道迷宫已经把那片地面压到 MAZE_FLOOR，
  // 所以整片迷宫都被判成"低于水线"。UE 实测：坑底螺旋梯最后 14 级泡在水里，
  // 迷宫 238 个采样点的"地面"是水面。参考图里的迷宫是干的。
  const inMaze = (x, y) => {
    const px = x - MAZE.cx, py = y - MAZE.cy;
    const fq = px / HEX_DQ;
    const fr = (py - fq * HEX_DR * 0.5) / HEX_DR;
    let c0 = fq, c2 = fr, c1 = -c0 - c2;
    let r0 = Math.round(c0), r1 = Math.round(c1), r2 = Math.round(c2);
    const d0 = Math.abs(r0 - c0), d1 = Math.abs(r1 - c1), d2 = Math.abs(r2 - c2);
    if (d0 > d1 && d0 > d2) r0 = -r1 - r2;
    else if (d1 > d2) r1 = -r0 - r2;
    else r2 = -r0 - r1;
    return (Math.abs(r0) + Math.abs(r1) + Math.abs(r2)) / 2 <= MAZE.ringsHex + 8;
  };
  const wet = (i, j) => {
    const x = x0 + i * cell, y = y0 + j * cell;
    // 迷宫足迹 + 入口坑底那一圈都不能有水。+8 环而不是 +3：入口落点周围 14m 是被
    // 显式凿成走廊的，那片地面在标准迷宫轮廓之外，第一版 +3 没盖住 ——
    // UE 实测水面(-2980)底下压着 56 个采样点的迷宫走廊地面(-3680)，
    // 也就是下楼梯进迷宫的头一段整个泡在 7 米水下。
    if (inMaze(x, y)) return false;
    if (Math.hypot(x - ENTRY.x, y - ENTRY.y) < 3200) return false;
    return ugField(x, y) > 0.02 && ugFloor(x, y) < WATER_Z;
  };
  let n = 0;
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
    if (!(wet(i, j) && wet(i + 1, j) && wet(i + 1, j + 1) && wet(i, j + 1))) continue;
    const a = vert(i, j), b = vert(i + 1, j), c = vert(i + 1, j + 1), d = vert(i, j + 1);
    F.push('f ' + a + ' ' + c + ' ' + b);
    F.push('f ' + a + ' ' + d + ' ' + c);
    n++;
  }
  if (n === 0) return { cells: 0 };
  fs.writeFileSync(path, ['s 1'].concat(V, F).join('\n'));
  return { cells: n, area: n * cell * cell / 10000, z: WATER_Z };
}
const water = writeWater(OUT + '/k11_water.obj');

// ---------------- 远景地形：把地图边界那堵灰板墙换掉 ----------------
// 第一层的边界现在是地表网格的裙边——一堵齐刷刷的竖直灰板，一眼就看出"地图到此为止"。
// 正确做法是两件事分开：**挡住玩家**用一个看不见的空气墙(UE 侧 BlockingVolume)，
// **挡住视线**用一圈没有碰撞的远景地形，一直铺到雾里，让人望不到边。
// 这里出的就是后者：只生成 play 区【之外】的格子，边缘高度直接取 play 区边界上
// 最近点的 surface()，所以接缝天然对齐；越往外越抬高，收成一圈远山把天际线封住。
const FAR_CELL = 1600;
const FAR_OUT = 105000;          // 从 play 区边界再往外 1.05km
function farHeight(x, y) {
  const cx = Math.max(X0, Math.min(X1, x)), cy = Math.max(Y0, Math.min(Y1, y));
  const d = Math.hypot(x - cx, y - cy);
  const edgeZ = surface(cx, cy);
  const w = Math.min(1, d / 9000);                       // 9m 内平滑接上 play 区
  const ridge = 5200 * fbm(x, y, 26000, 4, 17) + 2600 * fbm(x, y, 9000, 3, 29);
  const rise = Math.min(1, d / 45000) * 11000;           // 越远越高，收住天际线
  return edgeZ * (1 - w) + (edgeZ * 0.3 + ridge + rise) * w;
}
function writeFarField(path) {
  // 网格必须【对齐到 play 区边界】：起点往外取整数格，于是 X0/X1/Y0/Y1 上正好落格线。
  // 不对齐的话，最里面那圈格子会跨在边界上 —— 要么和真地形 z-fighting，
  // 要么留一条对不上的缝。
  const NOUT = Math.ceil(FAR_OUT / FAR_CELL);
  const x0 = X0 - NOUT * FAR_CELL, x1 = X1 + NOUT * FAR_CELL;
  const y0 = Y0 - NOUT * FAR_CELL, y1 = Y1 + NOUT * FAR_CELL;
  const nx = Math.ceil((x1 - x0) / FAR_CELL), ny = Math.ceil((y1 - y0) / FAR_CELL);
  const V = [], F = [];
  const id = new Int32Array((nx + 1) * (ny + 1)).fill(0);
  const vat = (i, j) => {
    const k = j * (nx + 1) + i;
    if (id[k]) { return id[k]; }
    const x = x0 + i * FAR_CELL, y = y0 + j * FAR_CELL;
    V.push('v ' + x.toFixed(1) + ' ' + (-y).toFixed(1) + ' ' + farHeight(x, y).toFixed(1));
    id[k] = V.length;
    return id[k];
  };
  // play 区里一格都不出，免得和真地形打架(z-fighting)。
  // 判据要用【格子边界】和 play 区比，不能用格心：原来写的是"格心落在 play 区
  // 再往外一整格之内就跳过"，等于在可玩地形和远景之间空出 16~32m 的一圈缝 ——
  // 站在边界往外看就是一道对不上的接缝，用户实测到的就是这个。
  // 现在格线和 X0/X1/Y0/Y1 对齐了，所以"完全落在 play 区内"的格子才跳过，
  // 远景的第一圈格子正好贴着 play 区边界起。
  // 最里面【一圈】远景格子也不出：那一圈留给下面的过渡缝合带。
  // 只对齐格线还不够 —— 可玩地形是 200cm 一格、远景是 1600cm 一格，两者每 1600cm
  // 才共用一个点，中间 7 个点远景是一条直线切过去，地形却是起伏的，
  // 实测最大错开 281cm。分辨率不匹配靠对齐解决不了，得补一条过渡带。
  const inSkip = (i, j) => {
    const xa = x0 + i * FAR_CELL, xb = xa + FAR_CELL;
    const ya = y0 + j * FAR_CELL, yb = ya + FAR_CELL;
    return xa >= X0 - FAR_CELL - 1 && xb <= X1 + FAR_CELL + 1
        && ya >= Y0 - FAR_CELL - 1 && yb <= Y1 + FAR_CELL + 1;
  };
  let cells = 0;
  for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
    if (inSkip(i, j)) { continue; }
    const a = vat(i, j), b = vat(i + 1, j), c = vat(i + 1, j + 1), d = vat(i, j + 1);
    F.push('f ' + a + ' ' + c + ' ' + b);
    F.push('f ' + a + ' ' + d + ' ' + c);
    cells++;
  }

  // ---- 过渡缝合带：内环跟着地形按 CELL 走，外环按 FAR_CELL 接远景 ----
  // 两条闭合折线按【归一化周长参数】齐步走，谁落后推谁，逐段发三角形。
  // 这样拐角不用单独处理，也不会出现 T 型接点(裂缝)。
  const ring = (ax0, ax1, ay0, ay1, step, hf) => {
    const pts = [];
    const push = (x, y) => pts.push([x, y, hf(x, y)]);
    for (let x = ax0; x < ax1 - 1e-6; x += step) { push(x, ay0); }
    for (let y = ay0; y < ay1 - 1e-6; y += step) { push(ax1, y); }
    for (let x = ax1; x > ax0 + 1e-6; x -= step) { push(x, ay1); }
    for (let y = ay1; y > ay0 + 1e-6; y -= step) { push(ax0, y); }
    return pts;
  };
  // 内环必须【照抄地表网格自己的边界顶点】，不能画一条理想直线。
  // 地表用的是三角晶格(latX 奇数行右移半格)，它的边界折线在 x=X0 和 x=X0+100 之间
  // 锯齿状来回 —— 画直线的话横向就差了半格，陡坡处 Z 差两米多。
  // 顺序按逆时针：下边 -> 右边 -> 上边 -> 左边。
  const NYH2 = Math.ceil(NY / HEXROW);
  const sv = (i, j) => {
    const x = latX(X0, i, j, CELL), y = latY(Y0, j, CELL);
    return [x, y, surface(x, y)];
  };
  const inner = [];
  for (let i = 0; i < NX; i++) { inner.push(sv(i, 0)); }
  for (let j = 0; j < NYH2; j++) { inner.push(sv(NX, j)); }
  for (let i = NX; i > 0; i--) { inner.push(sv(i, NYH2)); }
  for (let j = NYH2; j > 0; j--) { inner.push(sv(0, j)); }
  const outer = ring(X0 - FAR_CELL, X1 + FAR_CELL, Y0 - FAR_CELL, Y1 + FAR_CELL,
                     FAR_CELL, farHeight);
  const vpush = (p) => {
    V.push('v ' + p[0].toFixed(1) + ' ' + (-p[1]).toFixed(1) + ' ' + p[2].toFixed(1));
    return V.length;
  };
  const iv = inner.map(vpush), ov = outer.map(vpush);
  let a = 0, b = 0, stitch = 0;
  while (a < inner.length || b < outer.length) {
    const ta = (a + 1) / inner.length, tb = (b + 1) / outer.length;
    const i0 = iv[a % inner.length], o0 = ov[b % outer.length];
    if (ta <= tb && a < inner.length) {
      F.push('f ' + i0 + ' ' + iv[(a + 1) % inner.length] + ' ' + o0);
      a++;
    } else {
      F.push('f ' + i0 + ' ' + ov[(b + 1) % outer.length] + ' ' + o0);
      b++;
    }
    stitch++;
  }

  fs.writeFileSync(path, ['s 1'].concat(V, F).join('\n'));
  return { cells: cells, verts: V.length, tris: F.length, stitch: stitch,
           span: [x1 - x0, y1 - y0] };
}
const farf = writeFarField(OUT + '/k11_farfield.obj');

// ---------------- 地壳填充：把一二层之间那段真的塞满 ----------------
// 地表网格是"上表面 + 下表面 + 封边"围成的闭合壳，中间【是空的】。洞顶到地表之间
// 中位 32.8m 的岩层，量出来厚度是对的，但那是两张皮之间的空气 —— 编辑器里把相机
// 飞进去就是一个大空隙，用户反复指的就是这里。
// 引擎里没有"实心体"，只有面；要让它剖开也是实心，就得把这段体积真的用几何填上。
// 按真实地壳的样子做成【层理】：若干层沉积岩板叠起来，每层都是闭合的板体，
// 层面带噪声起伏，剖面上看是一条条岩层线，而不是一个空腔。
const FILL_LAYERS = 6;
const FILL_CELL = 500;
function writeCrustFill(path) {
  const V = [], F = [];
  const push = (x, y, z) => { V.push('v ' + x.toFixed(1) + ' ' + (-y).toFixed(1) + ' ' + z.toFixed(1)); return V.length; };
  const quad = (a, b, c, d) => { F.push('f ' + a + ' ' + c + ' ' + b); F.push('f ' + a + ' ' + d + ' ' + c); };
  // 只在"洞顶明显低于地表"的地方填 —— 那才是有空隙的区域
  const gap = (x, y) => {
    if (ugField(x, y) <= 0) { return null; }
    const top = surface(x, y) - SKIN;
    const bot = ugCeil(x, y) + UG_SKIN;
    return (top - bot > 600) ? [bot, top] : null;
  };
  const x0 = UG.cx - UG.rx * 1.5, x1 = UG.cx + UG.rx * 1.5;
  const y0 = UG.cy - UG.ry * 1.5, y1 = UG.cy + UG.ry * 1.5;
  const nx = Math.ceil((x1 - x0) / FILL_CELL), ny = Math.ceil((y1 - y0) / FILL_CELL);
  // 每层的分界面：在 bot..top 之间按比例插值，再加噪声让层面起伏
  const iface = (x, y, L) => {
    const g = gap(x, y);
    if (!g) { return null; }
    const t = L / FILL_LAYERS;
    const wob = 1 + 0.10 * (fbm(x, y, 5200 + L * 900, 3, 60 + L) - 0.5) * 2;
    const z = g[0] + (g[1] - g[0]) * Math.min(1, Math.max(0, t * wob));
    return Math.min(g[1], Math.max(g[0], z));
  };
  let cells = 0;
  for (let L = 0; L < FILL_LAYERS; L++) {
    const id = new Map();
    const vat = (i, j, lev) => {
      const k = i + ',' + j + ',' + lev;
      if (id.has(k)) { return id.get(k); }
      const x = x0 + i * FILL_CELL, y = y0 + j * FILL_CELL;
      const z = iface(x, y, lev);
      if (z === null) { return null; }
      const v = push(x, y, z);
      id.set(k, v);
      return v;
    };
    for (let j = 0; j < ny; j++) for (let i = 0; i < nx; i++) {
      const a0 = vat(i, j, L), b0 = vat(i + 1, j, L), c0 = vat(i + 1, j + 1, L), d0 = vat(i, j + 1, L);
      const a1 = vat(i, j, L + 1), b1 = vat(i + 1, j, L + 1), c1 = vat(i + 1, j + 1, L + 1), d1 = vat(i, j + 1, L + 1);
      if (!a0 || !b0 || !c0 || !d0 || !a1 || !b1 || !c1 || !d1) { continue; }
      quad(a1, b1, c1, d1);                 // 层顶
      quad(d0, c0, b0, a0);                 // 层底
      quad(a0, b0, b1, a1);                 // 四周封边，每层都是闭合板体
      quad(b0, c0, c1, b1);
      quad(c0, d0, d1, c1);
      quad(d0, a0, a1, d1);
      cells++;
    }
  }
  fs.writeFileSync(path, ['s 1'].concat(V, F).join('\n'));
  return { cells: cells, verts: V.length, tris: F.length, layers: FILL_LAYERS };
}
// writeCrustFill 已废弃：那版是 6 层叠板，仍然是壳，被分区等值面取代

// ---------------- 分区实体岩层：等值面 ----------------
// 用户定的结构：不要一个函数包打天下，按区做，每区都是"一段实体 − 若干空腔"。
//   第一区  地表 → 第二层天花板，挖掉：山顶巨坑漏斗、逃生地道
//   第二区  第二层天花板 → 第二层地板，挖掉：迷宫体积、巨坑、地道（下半）
// 之所以能这么做是因为 writeTowers 里那套 surface nets 是通用的：给它一个
// 密度函数(>0 实体)，它吐一个闭合流形。高度场做不到"中间掏个洞"，等值面天生就行。
//
// 这里先出第一区。密度用有符号距离而不是 ±1，等值面才不会是台阶状。
const ROCK_CS = 200;

// 逃生地道的三维距离（负=在管内）。用已经建好的空间哈希。
function tunnelDist3(x, y, z, bore) {
  if (!TUN_GRID) { return 1e9; }
  const gx = Math.floor(x / TUN_CS), gy = Math.floor(y / TUN_CS);
  const rad = Math.ceil((bore + TUN_CS) / TUN_CS);
  let best = 1e9;
  for (let i = -rad; i <= rad; i++) {
    for (let j = -rad; j <= rad; j++) {
      const arr = TUN_GRID.get((gx + i) + ',' + (gy + j));
      if (!arr) { continue; }
      for (const p of arr) {
        const d = Math.hypot(x - p[0], y - p[1], z - p[2]);
        if (d < best) { best = d; }
      }
    }
  }
  return best - bore;
}

const TUN_BORE = 550;      // 比管子外廓大一圈，保证岩层不会啃进管内

// ---------------- 第二层洞腔：留腔，不填 ----------------
// 用户定的：第二层【不能全填】，要给它留一个腔，迷宫摆在腔里。
// 所以洞腔是从实体里【减掉】的一块体积，不是"洞顶+洞底两张皮"围出来的壳。
// 腔底压到迷宫地面之下 80cm —— 迷宫自己的地板(k11_hexfield)盖在上面，不共面就不打架。
// 返回 >0 表示"在腔内"；调用处取 min(D, -chamberInside) 完成减法。
const CHAMBER_FLOOR_DROP = 80;
function chamberInside(x, y, z) {
  const f = ugField(x, y);
  if (f <= 0) { return -1e9; }
  const bot = Math.min(ugFloor(x, y), ENTRY.floor + 20) - CHAMBER_FLOOR_DROP;
  // 横向：ugField 从洞腔中心的 1 衰减到边界的 0，乘一个尺度当近似距离用。
  // 只是给等值面一个平滑侧壁，精度不敏感。
  // 腔顶比 ugCeil 再高 120：K11_Underground 那层同化岩壁壳(M_K11_AssimWall，
  // 玩家真正看到的洞顶)就完整落在腔内，不会和岩体的腔顶共面 z-fighting。
  return Math.min(ugCeil(x, y) + 120 - z, z - bot, f * 6000);
}

// ---------------- 统一岩体：一块实心，减三个空腔 ----------------
// 剖面图上的那三刀。岩壁不是造出来的，是减完剩下的边界。
// 顶面保持在 surface - SKIN：地表那张皮(k11_surface)继续负责可见地貌和城柱落地，
// 这块岩体只管把它下面到洞腔之间填成实的 —— 那 32m 的假空腔就是这么来的。
const ROCK_BASE = -5180;    // 迷宫地面 -3680 再往下 15m
function densRockSolid(x, y, z) {
  let d = Math.min(surface(x, y) - SKIN - z, z - ROCK_BASE);
  if (d <= -ROCK_CS * 2) { return d; }                 // 早退，省掉后面的距离计算
  d = Math.min(d, -chamberInside(x, y, z));                                        // 减第二层洞腔
  d = Math.min(d, Math.hypot(x - ENTRY.x, y - ENTRY.y) - entryRadiusAt(z) * 1.02);  // 减山顶巨坑
  d = Math.min(d, tunnelDist3(x, y, z, TUN_BORE));                                  // 减逃生地道
  return d;
}

// 通用等值面（surface nets），密度 >0 为实体
function isoZone(path, dens, x0, y0, z0, NIx, NIy, NIz, CS) {
  const V = [], F = [];
  const at = (i, j) => i * NIy + j;
  const cellId = (i, j) => i * (NIy - 1) + j;
  const P = new Float64Array(NIx * NIy), Q = new Float64Array(NIx * NIy);
  let vCur = new Int32Array((NIx - 1) * (NIy - 1)).fill(-1);
  let vPrev = new Int32Array((NIx - 1) * (NIy - 1)).fill(-1);
  const CO = [[0,0,0],[1,0,0],[1,1,0],[0,1,0],[0,0,1],[1,0,1],[1,1,1],[0,1,1]];
  const ED = [[0,1],[1,2],[2,3],[3,0],[4,5],[5,6],[6,7],[7,4],[0,4],[1,5],[2,6],[3,7]];
  const quad = (a, b, c, d, flip) => {
    if (a < 0 || b < 0 || c < 0 || d < 0) { return; }
    if (flip) { F.push('f ' + a + ' ' + d + ' ' + c + ' ' + b); }
    else { F.push('f ' + a + ' ' + b + ' ' + c + ' ' + d); }
  };
  for (let k = 0; k < NIz; k++) {
    const z = z0 + k * CS;
    Q.set(P);
    for (let i = 0; i < NIx; i++) for (let j = 0; j < NIy; j++) {
      P[at(i, j)] = dens(x0 + i * CS, y0 + j * CS, z);
    }
    const sw = vPrev; vPrev = vCur; vCur = sw; vCur.fill(-1);
    if (k === 0) { continue; }
    for (let i = 0; i < NIx - 1; i++) for (let j = 0; j < NIy - 1; j++) {
      const d = [Q[at(i,j)], Q[at(i+1,j)], Q[at(i+1,j+1)], Q[at(i,j+1)],
                 P[at(i,j)], P[at(i+1,j)], P[at(i+1,j+1)], P[at(i,j+1)]];
      let pos = 0;
      for (let m = 0; m < 8; m++) { if (d[m] > 0) { pos++; } }
      if (pos === 0 || pos === 8) { continue; }
      let sx = 0, sy = 0, sz = 0, n = 0;
      for (const [a, b] of ED) {
        if ((d[a] > 0) === (d[b] > 0)) { continue; }
        const t = d[a] / (d[a] - d[b]);
        sx += CO[a][0] + (CO[b][0] - CO[a][0]) * t;
        sy += CO[a][1] + (CO[b][1] - CO[a][1]) * t;
        sz += CO[a][2] + (CO[b][2] - CO[a][2]) * t;
        n++;
      }
      if (!n) { continue; }
      V.push('v ' + (x0 + (i + sx / n) * CS).toFixed(1) + ' '
             + (-(y0 + (j + sy / n) * CS)).toFixed(1) + ' '
             + (z0 + (k - 1 + sz / n) * CS).toFixed(1));
      vCur[cellId(i, j)] = V.length;
    }
    for (let i = 1; i < NIx - 1; i++) for (let j = 1; j < NIy - 1; j++) {
      if ((Q[at(i,j)] > 0) !== (P[at(i,j)] > 0)) {
        quad(vCur[cellId(i-1,j-1)], vCur[cellId(i,j-1)], vCur[cellId(i,j)], vCur[cellId(i-1,j)], Q[at(i,j)] > 0);
      }
      if ((Q[at(i,j)] > 0) !== (Q[at(i+1,j)] > 0)) {
        quad(vPrev[cellId(i,j-1)], vPrev[cellId(i,j)], vCur[cellId(i,j)], vCur[cellId(i,j-1)], Q[at(i,j)] > 0);
      }
      if ((Q[at(i,j)] > 0) !== (Q[at(i,j+1)] > 0)) {
        quad(vPrev[cellId(i-1,j)], vCur[cellId(i-1,j)], vCur[cellId(i,j)], vPrev[cellId(i,j)], Q[at(i,j)] > 0);
      }
    }
  }
  fs.writeFileSync(path, V.concat(F).join('\n'));
  return { verts: V.length, quads: F.length, grid: NIx + 'x' + NIy + 'x' + NIz };
}

// 岩体必须在【所有 write* 跑完之后】才能生成：densRockSolid 要用 TUN_CENTRE
// （writeAccess 里才填）。第一版排在 writeAccess 前面，TUN_GRID 是 null，
// tunnelDist3 一律返回 1e9 —— 隧道根本没被减掉，岩层是实的、隧道埋在里面。
// z 范围 -25.8m..50.5m 就是证据（完全没受隧道影响）。
//
// 范围取"洞腔外接盒 ∪ 地道走廊"：这两处才有需要填的假空腔，地图别处地表皮下
// 本来就贴着地面，没有腔。
function buildRockSolid(path) {
  let bx0 = UG.cx - UG.rx * 1.45, bx1 = UG.cx + UG.rx * 1.45;
  let by0 = UG.cy - UG.ry * 1.45, by1 = UG.cy + UG.ry * 1.45;
  for (const p of TUN_CENTRE) {
    bx0 = Math.min(bx0, p[0] - TUN_BORE * 3); bx1 = Math.max(bx1, p[0] + TUN_BORE * 3);
    by0 = Math.min(by0, p[1] - TUN_BORE * 3); by1 = Math.max(by1, p[1] + TUN_BORE * 3);
  }
  let zh = -1e9;
  for (let x = bx0; x <= bx1; x += 600) for (let y = by0; y <= by1; y += 600) {
    zh = Math.max(zh, surface(x, y) - SKIN);
  }
  const zl = ROCK_BASE - ROCK_CS * 2;
  zh += ROCK_CS * 2;
  const NIx = Math.ceil((bx1 - bx0) / ROCK_CS) + 1;
  const NIy = Math.ceil((by1 - by0) / ROCK_CS) + 1;
  const NIz = Math.ceil((zh - zl) / ROCK_CS) + 1;
  const r = isoZone(path, densRockSolid, bx0, by0, zl, NIx, NIy, NIz, ROCK_CS);
  r.zLo = zl; r.zHi = zh;
  return r;
}

// ---------------- delta grid for re-draping the 5312 existing actors ----------------
// The old surface (gen_terrain3.py's gY2) is what every placed actor was draped onto.
// Export new-minus-old on a grid so the UE side can shift each actor by a sampled delta
// instead of re-deriving two height functions in a second language.
function writeDelta(path) {
  const b1 = x => -6500 + 1400 * Math.sin(x / 5200) + 700 * Math.sin(x / 2100 + 2);
  const b2 = x => 7500 + 1200 * Math.sin(x / 4600 + 1);
  const cross = bf => {
    const xs = [];
    for (const s of D.SPW) {
      let prev = s[1] - bf(s[0]);
      for (let i = 1; i <= 100; i++) {
        const t = i / 100, x = s[0] * (1 - t), y = s[1] * (1 - t);
        const cur = y - bf(x);
        if (prev * cur < 0) { xs.push(x); break; }
        prev = cur;
      }
    }
    return xs;
  };
  const XS1 = cross(b1), XS2 = cross(b2).concat([300]);
  const Wv = (x, xs, ph) => {
    let w = 150 + 1450 * Math.pow(Math.max(0, Math.sin(x / 3300 + ph)), 3);
    for (const cx of xs) w = Math.max(w, 1700 * Math.exp(-Math.pow((x - cx) / 1000, 2)));
    return w;
  };
  const und = (x, y) => (45 * Math.sin(x / 2100 + .7) * Math.sin(y / 1700 + 1.9)
    + 30 * Math.sin(x / 950 + 3.1) * Math.sin(y / 1150 + .4)
    + 16 * Math.sin((x + y) / 640)) * sm((Math.hypot(x, y) - 1500) / 1200);
  const oldZ = (x, y) => 400 - 400 * sm((y - b1(x)) / (2 * Wv(x, XS1, .9)))
    - 400 * sm((y - b2(x)) / (2 * Wv(x, XS2, 2.1))) + und(x, y);

  // If a previous K-11 surface was already applied to the level, the delta must be
  // measured against THAT, not against the original gY2 -- otherwise a second run shifts
  // every actor a second time and the city ends up 160m in the sky.
  const prevPath = APPLIED;
  let prev = null;
  if (fs.existsSync(prevPath)) {
    const p = JSON.parse(fs.readFileSync(prevPath, 'utf8'));
    if (p.nx === NX && p.ny === NY && p.cell === CELL) prev = p.z;
  }

  const g = [], cur = [];
  let mn = 1e9, mx = -1e9, touched = 0;
  for (let j = 0; j <= NY; j++) for (let i = 0; i <= NX; i++) {
    const x = X0 + i * CELL, y = Y0 + j * CELL;
    const z = surface(x, y);
    cur.push(Math.round(z));
    // Back to snapshot differencing. The pit-only shortcut was a one-off for the pass that
    // added the entry pit; it cannot see changes that move the whole surface, and the
    // assimilation-source fix does exactly that (bedding is driven by A, so re-centring A
    // shifts heights map-wide). The snapshot holds whatever is currently in the level, so
    // new-minus-snapshot is the honest delta.
    const base = prev ? prev[j * (NX + 1) + i] : oldZ(x, y);
    const d = z - base;
    if (Math.abs(d) >= 1) touched++;
    g.push(Math.round(d));
    mn = Math.min(mn, d); mx = Math.max(mx, d);
  }
  console.log('  delta cells non-zero: ' + touched + ' / ' + g.length);
  fs.writeFileSync(path, JSON.stringify({ X0: X0, Y0: Y0, cell: CELL, nx: NX, ny: NY, d: g }));
  fs.writeFileSync(OUT + '/k11_height_current.json',
    JSON.stringify({ nx: NX, ny: NY, cell: CELL, z: cur }));
  return { mn: mn, mx: mx, rel: prev ? 'previous K-11 surface' : 'original gY2' };
}
// EXIT_NATURAL_Z 必须在 writeSurface / writeAccess 之前算好：这两者都会读它
NOPIT = true;
EXIT_NATURAL_Z = surface(EXIT.x, EXIT.y);
NOPIT = false;

const dlt = writeDelta(OUT + '/k11_delta.json');

// ---------------- report ----------------
// writeAccess 必须排在 writeSurface 【前面】：地壳底面(crustBottom)要把逃生地道包进去，
// 而地道中心线是在 writeAccess 里算出来的。顺序反了地壳就不知道地道在哪，
// 地道会吊在地壳底下的虚空里（UE 截图实测过）。
// writeAccess 不依赖 writeSurface 的任何输出，换序是安全的。
const acc = writeAccess(OUT + '/k11_f0access.obj');
const sres = writeSurface(OUT + '/k11_surface.obj');
const nv = sres.verts;
const ug = writeUnderground(OUT + '/k11_underground.obj');
const hex = writeHexField(OUT + '/k11_hexfield.obj', OUT + '/k11_hexdeco.obj');
const twr = writeTowers(OUT + '/k11_towers.obj');
// 分区实体岩层必须排在这里：要等 writeAccess 把 TUN_CENTRE 填好
const rock = buildRockSolid(OUT + '/k11_rock.obj');

let lo = 1e9, hi = -1e9;
const prov = { P0: [1e9, -1e9], P1: [1e9, -1e9], P2: [1e9, -1e9], P3: [1e9, -1e9], P4: [1e9, -1e9] };
for (let j = 0; j <= NY; j += 2) for (let i = 0; i <= NX; i += 2) {
  const x = X0 + i * CELL, y = Y0 + j * CELL, z = surface(x, y), r = Math.hypot(x, y);
  lo = Math.min(lo, z); hi = Math.max(hi, z);
  const k = r < 3500 ? 'P0' : r < 9000 ? 'P1' : r < 18000 ? 'P2' : r < 26000 ? 'P3' : 'P4';
  prov[k][0] = Math.min(prov[k][0], z); prov[k][1] = Math.max(prov[k][1], z);
}
const R = v => (v / 100).toFixed(1);
console.log('surface grid ' + NX + 'x' + NY + ' @' + CELL + 'cm -> ' + nv + ' verts, '
  + sres.holes + ' cells cut open over the collapses ('
  + Math.round(sres.holes * CELL * CELL / 10000) + ' m2)');
console.log('surface relief ' + R(lo) + 'm .. ' + R(hi) + 'm   (total ' + R(hi - lo) + 'm)');
for (const k of ['P0','P1','P2','P3','P4'])
  console.log('  ' + k + '  ' + R(prov[k][0]) + ' .. ' + R(prov[k][1]) + ' m');
console.log('layer two: ' + ug.tot + ' cells, ' + Math.round(100 * ug.open / ug.tot) + '% open to sky, ' + ug.verts + ' verts');
console.log('  tiankeng: surface ' + R(surface(TK.x, TK.y)) + 'm -> floor ' + R(ugFloor(TK.x, TK.y))
  + 'm = ' + R(surface(TK.x, TK.y) - ugFloor(TK.x, TK.y)) + 'm drop, ~' + R(TK.r * 2) + 'm across');
SWALLOWS.forEach((s, i) => console.log('  swallow ' + (i + 1) + ': ' + R(surface(s.x, s.y) - ugFloor(s.x, s.y))
  + 'm drop, ~' + R(s.r * 2) + 'm across, roof ' + (ugOpen(s.x, s.y) ? 'OPEN' : 'CLOSED -- not a door')));
console.log('--- 第一层塔体 等值面 ---');
console.log('  ' + twr.towers + ' 座塔, 体素 ' + twr.grid + ' @' + TWR_CS + 'cm, 活跃列 '
  + twr.cols + '/' + twr.colTot + ' (' + Math.round(100 * twr.cols / twr.colTot) + '%), z '
  + R(twr.zLo) + 'm..' + R(twr.zHi) + 'm');
console.log('  输出 ' + twr.verts + ' verts, ' + twr.quads + ' quads -> k11_towers.obj');
console.log('  [倒悬] ' + twr.overhangCols + ' / ' + twr.solidCols
  + ' 列沿 z 有 >2 次符号翻转（实体-空-实体），单列最多 ' + twr.maxCross + ' 次  ->  '
  + (twr.overhangCols > 0 ? '等值面确实做出了高度场做不到的倒悬' : 'FAIL: 一处倒悬都没有，等于白做'));
console.log('--- 底岩同化层 灰盒自检 ---');
console.log('  柱阵 ' + hex.cols + ' 根地面柱(' + hex.coverCols + ' 根是掩体), '
  + hex.verts + ' verts  |  背景下垂柱 ' + hex.ceilCols + ' 根, ' + hex.decoVerts + ' verts (独立网格, 无碰撞)');
console.log('  地下地板片只剩 ' + ug.floored + ' / ' + ug.tot + ' 格(柱阵铺不到的边缘), 其余交给柱阵');
const inv = hex.badPairs === 0 && hex.maxRiser <= STEP;
console.log('  [不变量] 走廊网络内相邻地面最大级高 = ' + hex.maxRiser + 'cm  (阈值 ' + STEP
  + 'cm < MaxStepHeight 45cm)   越界配对 ' + hex.badPairs + ' 处  ->  ' + (inv ? 'PASS' : 'FAIL'));
console.log('  [连通] 从 LZ 只走 ≤' + STEP + 'cm 台阶可达 ' + hex.reached + ' / ' + hex.walkCells + ' 个可走格');
console.log('  [净空] 走廊网络内最低净空 ' + R(hex.minClear) + 'm  (要求 ≥ ' + R(HEX_CLEAR) + 'm)  -> '
  + (hex.minClear >= HEX_CLEAR ? 'PASS' : 'FAIL'));
console.log('  --- 负一层迷宫 (模式图第2组) ---');
console.log('    走廊 ' + hex.maze.corridor + ' 格 | 环墙 ' + hex.maze.wall + ' 格 | 死胡同藏物点 '
  + hex.maze.dead + ' 格 | 祭坛室 ' + hex.maze.altar + ' 格');
console.log('    [可达] 中心祭坛 ' + hex.maze.reachAltar + '/' + hex.maze.altar
  + ' 格可达  ->  ' + (hex.maze.reachAltar > 0 ? 'PASS' : 'FAIL 入口走不到中心'));
console.log('    [可达] 死胡同藏物点 ' + hex.maze.reachDead + '/' + hex.maze.dead + ' 格可达');
console.log('    BFS起点(最外圈走廊) ' + hex.maze.bfsSeeds + ' 格');
console.log('    环诊断(可达/总): ' + hex.maze.ringDiag.join(' '));
console.log('    环数 ' + MAZE.rings + ', 半径 ' + (MAZE_R_MAX/100).toFixed(0) + 'm, 每环 '
  + MAZE.gatesPerRing + ' 个通口(相邻环错开130度)');
console.log('  竞技场            锁定高度   格数    面积m2   等效边长m  掩体%   可达%   净空m');
for (const a of hex.arenas) {
  console.log('    ' + a.name.padEnd(6) + R(a.z).padStart(9) + 'm'
    + String(a.cells).padStart(7) + Math.round(a.area).toString().padStart(9)
    + a.side.toFixed(1).padStart(11) + (a.cover * 100).toFixed(1).padStart(8)
    + (a.reach * 100).toFixed(1).padStart(8) + R(a.clear).padStart(8));
}
console.log('  主干道         中心距m  走廊m   落差m   每级高cm   ' + (hex.lanes.every(l => l.riser <= STEP) ? 'PASS' : 'FAIL'));
for (const l of hex.lanes) {
  console.log('    ' + l.n.padEnd(14) + R(l.len).padStart(6) + R(l.corr).padStart(8)
    + R(l.dz).padStart(8) + l.riser.toFixed(1).padStart(10)
    + (l.riser <= STEP ? '   ok' : '   TOO STEEP'));
}
console.log('total two-layer relief ' + R(hi - ugFloor(TK.x, TK.y)) + 'm');
console.log('re-drape delta ' + R(dlt.mn) + 'm .. ' + R(dlt.mx) + 'm  -> k11_delta.json');
console.log(water.cells
  ? 'underground water: ' + Math.round(water.area) + ' m2 at z=' + R(water.z) + 'm'
  : 'underground water: none (floor never drops below the water line)');
console.log('--- F0 access ---');
console.log('  ENTRY pit at (' + ENTRY.x + ',' + ENTRY.y + ')  '
  + R(Math.hypot(ENTRY.x, ENTRY.y)) + 'm from core, rim +' + R(ENTRY.lip)
  + 'm -> floor ' + R(ENTRY.floor) + 'm  (' + R(ENTRY.lip - ENTRY.floor) + 'm descent)');
console.log('  switchback path: ' + acc.pathSegs + ' segments, grade '
  + acc.grade.toFixed(1) + ' deg, max fall off the edge ' + R(acc.drop) + 'm');
console.log('  斜向上通道(手凿逃生步道): ' + acc.steps + ' 级 x 22cm, 直线跑长 110m, 爬升 '
  + R(acc.stairRise) + 'm, 坡度 ' + acc.grade2.toFixed(1) + ' 度, 出口 z=' + R(EXIT_NATURAL_Z) + 'm 隐蔽塌陷坑内');
console.log('  surface open at entry bottom: ' + ugOpen(ENTRY.x, ENTRY.y)
  + ' | at exit shaft: ' + ugOpen(EXIT.x, EXIT.y));

// ---------------- 一二层之间的实心度自检 ----------------
// "填满一二层之间的空洞"不是靠眼睛看截图判断的。这里逐格量：迷宫地面到洞顶的净空
// (= 真正该有的空腔)，以及洞顶到地表的岩层厚度(= 该是实心的部分)。
{
  const MF = ENTRY.floor + 20;             // 迷宫地面
  const head = [], rock = [];
  for (let q = -MAZE.ringsHex; q <= MAZE.ringsHex; q += 2) {
    for (let r = -MAZE.ringsHex; r <= MAZE.ringsHex; r += 2) {
      if ((Math.abs(q) + Math.abs(r) + Math.abs(-q - r)) / 2 > MAZE.ringsHex) { continue; }
      const x = MAZE.cx + q * HEX_DQ, y = MAZE.cy + r * HEX_DR + q * HEX_DR * 0.5;
      const ce = ugCeil(x, y);
      head.push(ce - MF);
      // 巨坑和隐蔽出口是【故意】从上面捅穿的，那两处岩层厚度当然是负的。
      // 排掉它们，剩下的才是"本来就该实心"的部分。
      if (Math.hypot(x - ENTRY.x, y - ENTRY.y) < ENTRY.rTop * 1.6) { continue; }
      if (Math.hypot(x - EXIT.x, y - EXIT.y) < EXIT.r * 2.0) { continue; }
      rock.push(surface(x, y) - SKIN - ce);
    }
  }
  const st = (a) => { a.sort((p, n) => p - n); return [a[0], a[(a.length / 2) | 0], a[a.length - 1]]; };
  const h = st(head.slice()), k = st(rock.slice());
  console.log('--- 一二层实心度 (' + head.length + ' 个迷宫采样点) ---');
  console.log('  迷宫净空(地面->洞顶)  min ' + R(h[0]) + 'm  中位 ' + R(h[1]) + 'm  max ' + R(h[2]) + 'm');
  console.log('  岩层厚度(洞顶->地表)  min ' + R(k[0]) + 'm  中位 ' + R(k[1]) + 'm  max ' + R(k[2]) + 'm'
    + (k[0] < 200 ? '   <-- FAIL 有地方薄得像层壳' : '   -> PASS'));
}

// ---------------- 逃生地道是否真的被包在地壳里 ----------------
// 地道必须落在"地表面 ~ 地壳底面"这个闭合体【内部】，否则它就是一根悬在虚空里的管子
// （UE 截图实测过：中段 z=-1801，而那里地壳底面还在二十多米之上）。
{
  let outAbove = 0, outBelow = 0, worst = 0, inCav = 0;
  for (const [x, y, z] of TUN_CENTRE) {
    if (ugField(x, y) > 0) { inCav++; continue; }   // 洞腔段由洞腔自己负责
    const top = surface(x, y);
    const bot = crustBottom(x, y);
    if (z + TUN_ARCH_REF > top) { outAbove++; }
    if (z < bot) { outBelow++; worst = Math.max(worst, bot - z); }
  }
  const bad = outAbove + outBelow;
  console.log('--- 逃生地道包裹检查 (' + TUN_CENTRE.length + ' 环，其中 ' + inCav + ' 环在洞腔内) ---');
  console.log('  捅出地表 ' + outAbove + ' 环 | 掉出地壳底面 ' + outBelow + ' 环(最深 ' + R(worst) + 'm)'
    + (bad === 0 ? '   -> PASS 全程包在实心地壳里' : '   <-- FAIL 有悬空段'));
}
console.log('--- 远景地形 (无碰撞，只挡视线) ---');
console.log('  ' + farf.cells + ' 格 @' + FAR_CELL + 'cm, ' + farf.tris + ' 三角, '
  + '总跨度 ' + R(farf.span[0]) + 'm x ' + R(farf.span[1]) + 'm  -> k11_farfield.obj');

console.log('--- 统一岩体（实心 − 洞腔 − 巨坑 − 地道）---');
console.log('  体素 ' + rock.grid + ' @' + ROCK_CS + 'cm, z ' + R(rock.zLo) + 'm..' + R(rock.zHi) + 'm');
console.log('  ' + rock.verts + ' verts, ' + rock.quads + ' quads  -> k11_rock.obj');
