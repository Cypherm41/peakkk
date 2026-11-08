'use strict';

/* ==========================================================================
   BANNERS OF PEAKKK — game logic
   Pure simulation: map generation, units, buildings, economy, combat, pathfinding
   and the enemy AI. This file never touches the DOM, so it can be run headless
   (see tests/sim.test.js).
   ========================================================================== */

const MAP_W = 72, MAP_H = 72;
const TILE_W = 64, TILE_H = 32;
const MAX_POP = 120;
const T_GRASS = 0, T_WATER = 1, T_SAND = 2;
const FARM_RATE = 0.7;       // food per second per builder on a farm
const FARM_SLOTS = 3;        // builders per farm

const TEAMS = [
  { id: 0, name: 'Green',  color: '#3fae49', dark: '#1f6b2a', light: '#9af0a6' },
  { id: 1, name: 'Yellow', color: '#ebc83c', dark: '#9a7410', light: '#fff2a8' },
];

const RES_ICON = { food: '🌾', wood: '🪵', gold: '🪙', stone: '🪨' };
const RES_LIST = ['food', 'wood', 'gold', 'stone'];

/* ------------------------------ Definitions ------------------------------ */

const NODE_DEFS = {
  tree:  { name: 'Tree',         res: 'wood',  amt: 100, rate: 1.0 },
  gold:  { name: 'Gold Mine',    res: 'gold',  amt: 500, rate: 0.8 },
  stone: { name: 'Stone Quarry', res: 'stone', amt: 400, rate: 0.8 },
};

const UNIT_DEFS = {
  builder: {
    name: 'Builder', icon: '👷', cls: 'worker', role: 'worker', domain: 'land',
    hp: 40, speed: 2.1, atk: 3, atkType: 'melee', range: 0.9, cd: 1.5,
    armor: { melee: 0, pierce: 1, siege: 0 }, cost: { food: 50 }, time: 12, pop: 1, sight: 7, carry: 12,
    desc: 'Gathers wood, food, gold and stone, builds and repairs. Weak in a fight.',
  },
  swordsman: {
    name: 'Swordsman', icon: '🗡️', cls: 'infantry', role: 'soldier', domain: 'land',
    hp: 75, speed: 1.9, atk: 9, atkType: 'melee', range: 0.9, cd: 1.1,
    armor: { melee: 2, pierce: 1, siege: 0 }, cost: { food: 60, gold: 25 }, time: 16, pop: 1, sight: 6,
    desc: 'Sturdy melee infantry. Strong against spearmen and buildings, weak to archers.',
  },
  spearman: {
    name: 'Spearman', icon: '🔱', cls: 'spear', role: 'soldier', domain: 'land',
    hp: 60, speed: 1.9, atk: 5, atkType: 'melee', range: 1.3, cd: 1.2, bonus: { cavalry: 14 },
    armor: { melee: 1, pierce: 3, siege: 0 }, cost: { food: 50, wood: 25 }, time: 14, pop: 1, sight: 6,
    desc: 'Cheap infantry with a long reach. Devastating against cavalry; loses to swordsmen.',
  },
  archer: {
    name: 'Archer', icon: '🏹', cls: 'archer', role: 'soldier', domain: 'land',
    hp: 35, speed: 1.9, atk: 6, atkType: 'pierce', range: 5.5, cd: 1.4,
    armor: { melee: 0, pierce: 0, siege: 0 }, cost: { wood: 40, gold: 30 }, time: 17, pop: 1, sight: 7,
    desc: 'Ranged attacker that shreds infantry from a distance. Fragile and weak to cavalry.',
  },
  cavalry: {
    name: 'Cavalry', icon: '🐎', cls: 'cavalry', role: 'soldier', domain: 'land',
    hp: 110, speed: 3.4, atk: 10, atkType: 'melee', range: 1.0, cd: 1.3, bonus: { archer: 8, worker: 5 },
    armor: { melee: 2, pierce: 3, siege: 0 }, cost: { food: 80, gold: 60 }, time: 24, pop: 2, sight: 7, big: true,
    desc: 'Fast mounted knight. Runs down archers and builders; spearmen are its nightmare.',
  },
  warship: {
    name: 'Warship', icon: '⛵', cls: 'ship', role: 'soldier', domain: 'water',
    hp: 160, speed: 2.4, atk: 14, atkType: 'siege', range: 7.5, cd: 2.2,
    armor: { melee: 3, pierce: 6, siege: 0 }, cost: { wood: 120, gold: 60 }, time: 30, pop: 3, sight: 8, big: true,
    desc: 'Lake warship with a heavy catapult. Bombards shorelines and enemy ships.',
  },
};

const BUILDING_DEFS = {
  towncenter: {
    name: 'Town Center', icon: '🏰', w: 3, h: 3, hp: 1600, cost: { wood: 300, stone: 200 }, time: 90, pop: 10,
    drop: true, trains: ['builder'], atk: 8, atkType: 'pierce', range: 6.5, cd: 1.5, height: 44,
    desc: 'Heart of your realm. Trains builders, stores resources, +10 population, shoots arrows.',
  },
  house: {
    name: 'House', icon: '🏠', w: 2, h: 2, hp: 350, cost: { wood: 40 }, time: 20, pop: 5, height: 20,
    desc: 'Adds +5 population capacity.',
  },
  farm: {
    name: 'Farm', icon: '🌾', w: 2, h: 2, hp: 200, cost: { wood: 60 }, time: 15, farm: true, height: 2,
    desc: 'Endless food for up to 3 builders. Build it close to a Town Center or Storehouse.',
  },
  storehouse: {
    name: 'Storehouse', icon: '📦', w: 2, h: 2, hp: 400, cost: { wood: 80 }, time: 20, drop: true, height: 18,
    desc: 'Drop-off point for all resources. Build it next to forests, mines and farms.',
  },
  barracks: {
    name: 'Barracks', icon: '⚔️', w: 3, h: 3, hp: 900, cost: { wood: 150, stone: 50 }, time: 40,
    trains: ['swordsman', 'spearman'], height: 28,
    desc: 'Trains Swordsmen and Spearmen.',
  },
  archery: {
    name: 'Archery Range', icon: '🎯', w: 3, h: 3, hp: 800, cost: { wood: 175 }, time: 40,
    trains: ['archer'], height: 26,
    desc: 'Trains Archers.',
  },
  stable: {
    name: 'Stable', icon: '🐴', w: 3, h: 3, hp: 900, cost: { wood: 175, stone: 50 }, time: 45,
    trains: ['cavalry'], height: 26,
    desc: 'Trains Cavalry.',
  },
  blacksmith: {
    name: 'Blacksmith', icon: '🔨', w: 3, h: 3, hp: 800, cost: { wood: 150, gold: 50 }, time: 40,
    research: ['weapons', 'armor'], height: 26,
    desc: 'Researches weapon and armor upgrades for all your soldiers.',
  },
  port: {
    name: 'Port', icon: '⚓', w: 3, h: 3, hp: 1000, cost: { wood: 200, stone: 75 }, time: 50,
    trains: ['warship'], shore: true, height: 24,
    desc: 'Must be built on the lake shore. Trains Warships.',
  },
  tower: {
    name: 'Watch Tower', icon: '🗼', w: 2, h: 2, hp: 700, cost: { wood: 80, stone: 120 }, time: 40,
    atk: 11, atkType: 'pierce', range: 7.5, cd: 1.6, height: 54,
    desc: 'Defensive tower that shoots arrows at nearby enemies.',
  },
};
// armor for every building
for (const k in BUILDING_DEFS) BUILDING_DEFS[k].armor = { melee: 4, pierce: 12, siege: 0 };
for (const k in UNIT_DEFS) UNIT_DEFS[k].id = k;
for (const k in BUILDING_DEFS) BUILDING_DEFS[k].id = k;

const TECHS = {
  weapons: {
    name: 'Forged Weapons', icon: '⚔️', max: 2,
    desc: '+2 attack for all soldiers (per level).',
    cost: (lvl) => ({ food: 150 + 100 * lvl, gold: 100 + 75 * lvl }),
    time: (lvl) => 40 + 15 * lvl,
  },
  armor: {
    name: 'Plate Armor', icon: '🛡️', max: 2,
    desc: '+1 melee and pierce armor for all soldiers (per level).',
    cost: (lvl) => ({ food: 150 + 100 * lvl, gold: 100 + 75 * lvl }),
    time: (lvl) => 40 + 15 * lvl,
  },
};

const DIFFS = {
  easy:   { gather: 0.8,  maxB: 16, wave: 10, grow: 3, pace: 1.4, bonus: 0,   think: 1.0 },
  normal: { gather: 1.0,  maxB: 24, wave: 9,  grow: 4, pace: 1.0, bonus: 100, think: 0.7 },
  hard:   { gather: 1.25, maxB: 32, wave: 8,  grow: 5, pace: 0.75, bonus: 300, think: 0.5 },
};

/* -------------------------------- Helpers -------------------------------- */

function mulberry32(a) {
  return function () {
    a |= 0; a = a + 0x6D2B79F5 | 0;
    let t = Math.imul(a ^ a >>> 15, 1 | a);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}
const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const dist = (ax, ay, bx, by) => Math.hypot(ax - bx, ay - by);
function distPointRect(px, py, r) {
  const dx = Math.max(r.x - px, 0, px - (r.x + r.w));
  const dy = Math.max(r.y - py, 0, py - (r.y + r.h));
  return Math.hypot(dx, dy);
}
function canAfford(res, cost) {
  for (const k in cost) if ((res[k] || 0) < cost[k]) return false;
  return true;
}
function missingRes(res, cost) {
  for (const k in cost) if ((res[k] || 0) < cost[k]) return k;
  return null;
}
function payCost(res, cost) { for (const k in cost) res[k] -= cost[k]; }
function refundCost(res, cost) { for (const k in cost) res[k] += cost[k]; }
const centerOf = (e) => (e.kind === 'unit' ? { x: e.x, y: e.y } : { x: e.x + e.w / 2, y: e.y + e.h / 2 });

/* ================================== Game ================================== */

class Game {
  constructor(opts = {}) {
    this.seed = opts.seed != null ? opts.seed : (Math.random() * 1e9) | 0;
    this.rng = mulberry32(this.seed);
    this.difficulty = opts.difficulty || 'normal';
    this.time = 0;
    this.nextId = 1;
    this.units = []; this.buildings = []; this.nodes = [];
    this.projectiles = []; this.effects = []; this.events = [];
    this.winner = null;
    this.winCheck = 0;
    this.pathBudget = 0;

    this.teams = TEAMS.map((t) => ({
      ...t,
      res: { food: 300, wood: 300, gold: 150, stone: 100 },
      popUsed: 0, popCap: 0, up: { weapons: 0, armor: 0 }, gatherMult: 1, lastAlert: -99,
    }));

    this.terrain = new Uint8Array(MAP_W * MAP_H);
    this.lake = new Uint8Array(MAP_W * MAP_H);
    this.shade = new Uint8Array(MAP_W * MAP_H);
    this.occ = new Array(MAP_W * MAP_H).fill(null);

    // pathfinding scratch
    const N = MAP_W * MAP_H;
    this.pg = new Float32Array(N); this.pp = new Int32Array(N);
    this.ps = new Int32Array(N); this.pc = new Int32Array(N);
    this.pstamp = 0; this.hk = []; this.hv = [];

    this.bases = [{ x: 11, y: 36 }, { x: 60, y: 36 }];
    this.generateMap();
    this.setupTeams();

    const aiTeams = opts.aiTeams || [1];
    this.ais = aiTeams.map((id) => new AI(this, id, this.difficulty));
  }

  /* ---------------------------- Map generation --------------------------- */

  generateMap() {
    const rng = this.rng, W = MAP_W, H = MAP_H;
    const NS = 12;
    const lat = [];
    for (let i = 0; i < (NS + 2) * (NS + 2); i++) lat.push(rng());
    const sm = (t) => t * t * (3 - 2 * t);
    const noise = (u, v) => {
      const gx = u * NS, gy = v * NS;
      const ix = Math.floor(gx), iy = Math.floor(gy);
      const fx = sm(gx - ix), fy = sm(gy - iy);
      const a = lat[iy * (NS + 2) + ix], b = lat[iy * (NS + 2) + ix + 1];
      const c = lat[(iy + 1) * (NS + 2) + ix], d = lat[(iy + 1) * (NS + 2) + ix + 1];
      return (a * (1 - fx) + b * fx) * (1 - fy) + (c * (1 - fx) + d * fx) * fy;
    };

    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      const wob = (noise(x / (W + 1), y / (H + 1)) - 0.5) * 0.4;
      const d = Math.hypot((x - 36) / 12, (y - 36) / 20);
      this.terrain[y * W + x] = d < 1 + wob ? T_WATER : T_GRASS;
      this.shade[y * W + x] = (rng() * 6) | 0;
    }
    // sand shores
    for (let y = 0; y < H; y++) for (let x = 0; x < W; x++) {
      if (this.terrain[y * W + x] === T_WATER) continue;
      let near = false;
      for (let dy = -1; dy <= 1 && !near; dy++) for (let dx = -1; dx <= 1; dx++) {
        const nx = x + dx, ny = y + dy;
        if (nx >= 0 && ny >= 0 && nx < W && ny < H && this.terrain[ny * W + nx] === T_WATER) { near = true; break; }
      }
      if (near) this.terrain[y * W + x] = T_SAND;
    }
    // flood fill the central lake
    const stack = [];
    if (this.terrain[36 * W + 36] === T_WATER) stack.push(36 * W + 36);
    while (stack.length) {
      const i = stack.pop();
      if (this.lake[i]) continue;
      this.lake[i] = 1;
      const x = i % W, y = (i / W) | 0;
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1]]) {
        const nx = x + dx, ny = y + dy;
        if (nx >= 0 && ny >= 0 && nx < W && ny < H && this.terrain[ny * W + nx] === T_WATER && !this.lake[ny * W + nx]) stack.push(ny * W + nx);
      }
    }

    // resources: per-base layout (mirrored for the second team)
    this.bases.forEach((b, i) => {
      const s = i === 0 ? 1 : -1;
      const p = (dx, dy) => [b.x + dx * s, b.y + dy];
      this.cluster('tree', ...p(-1, -9), 24, 3.6);
      this.cluster('tree', ...p(-2, 10), 26, 3.6);
      this.cluster('tree', ...p(6, -4), 10, 2.4);
      this.cluster('gold', ...p(3, -8), 5, 1.8);
      this.cluster('stone', ...p(3, 8), 4, 1.7);
    });
    // neutral resources
    this.cluster('gold', 36, 8, 6, 2.2); this.cluster('gold', 36, 64, 6, 2.2);
    this.cluster('stone', 30, 12, 4, 1.8); this.cluster('stone', 42, 60, 4, 1.8);
    this.cluster('gold', 24, 10, 4, 1.8); this.cluster('gold', 48, 62, 4, 1.8);
    // forests
    for (let n = 0, tries = 0; n < 30 && tries < 400; tries++) {
      const cx = 2 + rng() * (W - 4), cy = 2 + rng() * (H - 4);
      if (!this.isFreeLand(Math.floor(cx), Math.floor(cy))) continue;
      if (this.bases.some((b) => dist(cx, cy, b.x, b.y) < 11)) continue;
      this.cluster('tree', cx, cy, 10 + ((rng() * 30) | 0), 2.5 + rng() * 2.5);
      n++;
    }
    for (let n = 0, tries = 0; n < 70 && tries < 600; tries++) {
      const x = (rng() * W) | 0, y = (rng() * H) | 0;
      if (!this.isFreeLand(x, y) || this.nearBase(x, y, 6)) continue;
      this.addNode('tree', x, y); n++;
    }
  }

  nearBase(x, y, r) { return this.bases.some((b) => dist(x + 0.5, y + 0.5, b.x + 0.5, b.y + 0.5) < r); }
  isFreeLand(x, y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return false;
    const i = y * MAP_W + x;
    return this.terrain[i] === T_GRASS && !this.occ[i];
  }
  cluster(type, cx, cy, count, radius) {
    const rng = this.rng;
    let placed = 0;
    for (let t = 0; t < count * 12 && placed < count; t++) {
      const a = rng() * Math.PI * 2, r = Math.sqrt(rng()) * radius;
      const x = Math.floor(cx + Math.cos(a) * r), y = Math.floor(cy + Math.sin(a) * r);
      if (!this.isFreeLand(x, y) || this.nearBase(x, y, 5.5)) continue;
      this.addNode(type, x, y); placed++;
    }
  }
  addNode(type, x, y) {
    const d = NODE_DEFS[type];
    const n = { id: this.nextId++, kind: 'node', type, x, y, w: 1, h: 1, amt: d.amt, max: d.amt, dead: false };
    this.nodes.push(n);
    this.occ[y * MAP_W + x] = n;
    return n;
  }

  setupTeams() {
    this.teams.forEach((t, i) => {
      const b = this.bases[i];
      this.createBuilding(i, 'towncenter', b.x - 1, b.y - 1, true);
      const tcb = this.buildings[this.buildings.length - 1];
      for (let k = 0; k < 5; k++) this.spawnUnitAt(tcb, 'builder');
      this.spawnUnitAt(tcb, 'spearman');
    });
  }
  spawnUnitAt(b, type) {
    const pos = this.findSpawn(b, UNIT_DEFS[type].domain, null);
    if (!pos) return null;
    const u = this.createUnit(b.team, type, pos.x, pos.y);
    this.teams[b.team].popUsed += u.def.pop;
    return u;
  }

  /* ------------------------------ Terrain / grid ------------------------- */

  passLand(x, y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return false;
    const i = y * MAP_W + x;
    if (this.terrain[i] === T_WATER) return false;
    const o = this.occ[i];
    return !o || o.type === 'farm';
  }
  passWater(x, y) {
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) return false;
    return this.terrain[y * MAP_W + x] === T_WATER;
  }
  passFor(domain, x, y) { return domain === 'water' ? this.passWater(x, y) : this.passLand(x, y); }

  /* ------------------------------ Pathfinding ---------------------------- */

  heapPush(k, v) {
    const hk = this.hk, hv = this.hv;
    let i = hk.length; hk.push(k); hv.push(v);
    while (i > 0) {
      const p = (i - 1) >> 1;
      if (hk[p] <= k) break;
      hk[i] = hk[p]; hv[i] = hv[p]; i = p;
    }
    hk[i] = k; hv[i] = v;
  }
  heapPop() {
    const hk = this.hk, hv = this.hv;
    const top = hv[0];
    const lk = hk.pop(), lv = hv.pop();
    const n = hk.length;
    if (n > 0) {
      let i = 0;
      for (;;) {
        let c = 2 * i + 1;
        if (c >= n) break;
        if (c + 1 < n && hk[c + 1] < hk[c]) c++;
        if (hk[c] >= lk) break;
        hk[i] = hk[c]; hv[i] = hv[c]; i = c;
      }
      hk[i] = lk; hv[i] = lv;
    }
    return top;
  }

  // A* from integer tile (sx,sy). goal(x,y) -> bool; (tx,ty) is the heuristic target.
  findPath(sx, sy, domain, goal, tx, ty, maxNodes = 4500) {
    const W = MAP_W, H = MAP_H;
    const pass = domain === 'water' ? (x, y) => this.passWater(x, y) : (x, y) => this.passLand(x, y);
    sx = clamp(sx, 0, W - 1); sy = clamp(sy, 0, H - 1);
    const st = ++this.pstamp;
    const G = this.pg, P = this.pp, S = this.ps, C = this.pc;
    this.hk.length = 0; this.hv.length = 0;
    const heur = (x, y) => {
      const dx = Math.abs(x - tx), dy = Math.abs(y - ty);
      return dx + dy - 0.5858 * Math.min(dx, dy);
    };
    const start = sy * W + sx;
    G[start] = 0; P[start] = -1; S[start] = st;
    this.heapPush(heur(sx, sy), start);
    let best = start, bestH = heur(sx, sy), expanded = 0, found = -1;
    const DIRS = [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [1, -1], [-1, 1], [-1, -1]];
    while (this.hk.length) {
      const cur = this.heapPop();
      if (C[cur] === st) continue;
      C[cur] = st;
      const cx = cur % W, cy = (cur / W) | 0;
      if (goal(cx, cy)) { found = cur; break; }
      if (++expanded > maxNodes) break;
      for (let d = 0; d < 8; d++) {
        const nx = cx + DIRS[d][0], ny = cy + DIRS[d][1];
        if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
        if (!pass(nx, ny)) continue;
        const diag = d >= 4;
        if (diag && (!pass(cx + DIRS[d][0], cy) || !pass(cx, cy + DIRS[d][1]))) continue;
        const ni = ny * W + nx;
        if (C[ni] === st) continue;
        const ng = G[cur] + (diag ? 1.4142 : 1);
        if (S[ni] === st && ng >= G[ni]) continue;
        S[ni] = st; G[ni] = ng; P[ni] = cur;
        const h = heur(nx, ny);
        this.heapPush(ng + h, ni);
        if (h < bestH) { bestH = h; best = ni; }
      }
    }
    const partial = found < 0;
    let n = partial ? best : found;
    const pts = [];
    while (n !== start && n >= 0) { pts.push({ x: (n % W) + 0.5, y: ((n / W) | 0) + 0.5 }); n = P[n]; }
    pts.reverse();
    return { pts, partial };
  }

  los(x0, y0, x1, y1, domain) {
    const d = Math.hypot(x1 - x0, y1 - y0);
    const steps = Math.ceil(d / 0.3);
    const nx = d > 0 ? -(y1 - y0) / d : 0, ny = d > 0 ? (x1 - x0) / d : 0;
    for (let i = 1; i <= steps; i++) {
      const t = i / steps, px = x0 + (x1 - x0) * t, py = y0 + (y1 - y0) * t;
      if (!this.passFor(domain, Math.floor(px), Math.floor(py))) return false;
      if (!this.passFor(domain, Math.floor(px + nx * 0.22), Math.floor(py + ny * 0.22))) return false;
      if (!this.passFor(domain, Math.floor(px - nx * 0.22), Math.floor(py - ny * 0.22))) return false;
    }
    return true;
  }
  smoothPath(x0, y0, pts, domain) {
    const out = [];
    let cx = x0, cy = y0, i = 0;
    while (i < pts.length) {
      let j = pts.length - 1;
      while (j > i && !this.los(cx, cy, pts[j].x, pts[j].y, domain)) j--;
      out.push(pts[j]); cx = pts[j].x; cy = pts[j].y; i = j + 1;
    }
    return out;
  }

  computePath(u, goal, tx, ty) {
    if (this.pathBudget <= 0) return false;
    this.pathBudget--;
    const r = this.findPath(Math.floor(u.x), Math.floor(u.y), u.def.domain, goal, tx, ty);
    u.path = this.smoothPath(u.x, u.y, r.pts, u.def.domain);
    u.pi = 0; u.partial = r.partial;
    return true;
  }
  computePathPoint(u) {
    const gx = clamp(Math.floor(u.tx), 0, MAP_W - 1), gy = clamp(Math.floor(u.ty), 0, MAP_H - 1);
    return this.computePath(u, (x, y) => x === gx && y === gy, gx, gy);
  }
  computePathTo(u, t, reach) {
    const rect = t.kind === 'unit' ? { x: Math.floor(t.x), y: Math.floor(t.y), w: 1, h: 1 } : t;
    const c = centerOf(t);
    const lim = reach - 0.05;
    u.ptx = Math.floor(t.x); u.pty = Math.floor(t.y);
    return this.computePath(u, (x, y) => distPointRect(x + 0.5, y + 0.5, rect) <= lim, c.x - 0.5, c.y - 0.5);
  }

  followPath(u, dt) {
    if (!u.path) { u.stk = 0; return false; }
    // give up if we have not made progress for a full second (jammed against other units)
    u.stk = (u.stk || 0) + dt;
    if (u.stk >= 1) {
      const moved = Math.hypot(u.x - (u.sx == null ? -9 : u.sx), u.y - (u.sy == null ? -9 : u.sy));
      u.stk = 0; u.sx = u.x; u.sy = u.y;
      if (moved < 0.25) { u.path = null; return false; }
    }
    let step = u.def.speed * dt;
    while (step > 0 && u.pi < u.path.length) {
      const wp = u.path[u.pi];
      const dx = wp.x - u.x, dy = wp.y - u.y, d = Math.hypot(dx, dy);
      if (u.pi < u.path.length - 1 && d < 0.4) { u.pi++; continue; } // intermediate waypoints need no precision
      if (d <= step) { u.x = wp.x; u.y = wp.y; step -= d; u.pi++; if (d > 0.001) u.face = (dx - dy) >= 0 ? 1 : -1; }
      else { u.x += dx / d * step; u.y += dy / d * step; u.face = (dx - dy) >= 0 ? 1 : -1; step = 0; }
      u.moving = true;
    }
    if (u.pi >= u.path.length) { u.path = null; return false; }
    return true;
  }

  distTo(u, t) {
    if (t.kind === 'unit') return Math.hypot(u.x - t.x, u.y - t.y);
    return distPointRect(u.x, u.y, t);
  }
  reachOf(u, t) { return u.def.range + (t.kind === 'unit' ? 0.35 : 0.15); }
  faceTo(u, t) {
    const c = centerOf(t);
    const sx = (c.x - c.y) - (u.x - u.y);
    if (Math.abs(sx) > 0.05) u.face = sx >= 0 ? 1 : -1;
  }

  // Move u until within `reach` of target t. Returns true (arrived), false (moving), 'fail' (unreachable).
  approach(u, t, reach, dt) {
    if (this.distTo(u, t) <= reach) { u.path = null; return true; }
    u.repath -= dt;
    const moved = t.kind === 'unit' && (u.ptx !== Math.floor(t.x) || u.pty !== Math.floor(t.y));
    if ((!u.path || moved) && u.repath <= 0) {
      if (this.computePathTo(u, t, reach)) u.repath = 0.5;
    }
    if (!this.followPath(u, dt)) {
      if (u.partial && !u.path && u.repath > 0.25 && this.distTo(u, t) > reach) return 'fail';
      if (this.distTo(u, t) <= 3) {
        const c = centerOf(t);
        const dx = c.x - u.x, dy = c.y - u.y, d = Math.hypot(dx, dy) || 1, st = Math.min(d, u.def.speed * dt);
        const nx = u.x + dx / d * st, ny = u.y + dy / d * st;
        if (this.passFor(u.def.domain, Math.floor(nx), Math.floor(ny))) { u.x = nx; u.y = ny; u.moving = true; u.face = (dx - dy) >= 0 ? 1 : -1; }
      }
    }
    return false;
  }

  /* ------------------------------- Entities ------------------------------ */

  createUnit(teamId, type, x, y) {
    const d = UNIT_DEFS[type];
    const u = {
      id: this.nextId++, kind: 'unit', type, def: d, team: teamId, x, y, hp: d.hp, maxHp: d.hp,
      state: 'idle', path: null, pi: 0, target: null, tx: x, ty: y, cd: 0, atkAnim: 0, dead: false,
      face: teamId === 0 ? 1 : -1, anim: this.rng() * 10, moving: false, working: false,
      carryType: null, carry: 0, sub: null, scan: this.rng() * 0.5, repath: 0, partial: false,
      ignore: new Map(), gatherKind: null, lastKind: null, farm: null, drop: null, auto: false, home: null,
      ptx: -1, pty: -1, lastHit: -99,
    };
    this.units.push(u);
    return u;
  }

  createBuilding(teamId, type, x, y, done) {
    const d = BUILDING_DEFS[type];
    const b = {
      id: this.nextId++, kind: 'building', type, def: d, team: teamId, x, y, w: d.w, h: d.h,
      hp: done ? d.hp : Math.max(10, d.hp * 0.1), maxHp: d.hp, done: !!done, progress: done ? 1 : 0,
      queue: [], qt: 0, rally: null, cd: 0, scan: 0, dead: false, users: new Set(), lastHit: -99, target: null,
    };
    this.buildings.push(b);
    for (let j = 0; j < b.h; j++) for (let i = 0; i < b.w; i++) this.occ[(y + j) * MAP_W + x + i] = b;
    if (done) this.recalcPop(teamId);
    return b;
  }

  recalcPop(teamId) {
    let cap = 0;
    for (const b of this.buildings) if (b.team === teamId && b.done && !b.dead && b.def.pop) cap += b.def.pop;
    this.teams[teamId].popCap = Math.min(MAX_POP, cap);
  }

  canPlace(type, x, y, margin = 0) {
    const d = BUILDING_DEFS[type];
    for (let j = -margin; j < d.h + margin; j++) for (let i = -margin; i < d.w + margin; i++) {
      const tx = x + i, ty = y + j;
      const inside = i >= 0 && j >= 0 && i < d.w && j < d.h;
      if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) { if (inside) return false; continue; }
      const idx = ty * MAP_W + tx;
      if (inside) {
        if (this.terrain[idx] === T_WATER || this.occ[idx]) return false;
      } else {
        const o = this.occ[idx];
        if (o && o.kind === 'building' && o.type !== 'farm') return false;
      }
    }
    if (d.shore && !this.shoreOk(x, y, d.w, d.h)) return false;
    return true;
  }
  shoreOk(x, y, w, h) {
    for (let ty = y - 2; ty < y + h + 2; ty++) for (let tx = x - 2; tx < x + w + 2; tx++) {
      if (tx < 0 || ty < 0 || tx >= MAP_W || ty >= MAP_H) continue;
      if (this.lake[ty * MAP_W + tx]) return true;
    }
    return false;
  }

  findPlacement(type, cx, cy, rmin, rmax, margin) {
    const d = BUILDING_DEFS[type];
    const ox = Math.floor(d.w / 2), oy = Math.floor(d.h / 2);
    for (let r = rmin; r <= rmax; r++) {
      const cands = [];
      for (let dy = -r; dy <= r; dy++) for (let dx = -r; dx <= r; dx++) {
        if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) continue;
        cands.push([Math.round(cx) + dx - ox, Math.round(cy) + dy - oy]);
      }
      for (let i = cands.length - 1; i > 0; i--) {
        const j = (this.rng() * (i + 1)) | 0;
        [cands[i], cands[j]] = [cands[j], cands[i]];
      }
      for (const [x, y] of cands) if (this.canPlace(type, x, y, margin)) return { x, y };
    }
    return null;
  }

  placeBuilding(teamId, type, x, y, builders) {
    const d = BUILDING_DEFS[type], team = this.teams[teamId];
    if (!this.canPlace(type, x, y, 0)) return { err: d.shore ? 'Ports must be built on the lake shore' : 'Cannot build here' };
    const miss = missingRes(team.res, d.cost);
    if (miss) return { err: 'Not enough ' + miss };
    payCost(team.res, d.cost);
    const b = this.createBuilding(teamId, type, x, y, false);
    this.ejectUnits(b);
    if (builders && builders.length) this.orderBuild(builders, b);
    return { b };
  }

  ejectUnits(b) {
    for (const u of this.units) {
      if (u.dead || u.def.domain !== 'land') continue;
      const tx = Math.floor(u.x), ty = Math.floor(u.y);
      if (tx >= b.x && tx < b.x + b.w && ty >= b.y && ty < b.y + b.h) {
        const pos = this.nearestFree(u.x, u.y, 'land');
        if (pos) { u.x = pos.x; u.y = pos.y; u.path = null; }
      }
    }
  }
  nearestFree(x, y, domain) {
    const sx = Math.floor(x), sy = Math.floor(y);
    for (let r = 0; r < 8; r++) {
      let best = null, bd = 1e9;
      for (let dy = -r; dy <= r; dy++) for (let dx = -r; dx <= r; dx++) {
        if (Math.max(Math.abs(dx), Math.abs(dy)) !== r) continue;
        if (!this.passFor(domain, sx + dx, sy + dy)) continue;
        const d = dx * dx + dy * dy;
        if (d < bd) { bd = d; best = { x: sx + dx + 0.5, y: sy + dy + 0.5 }; }
      }
      if (best) return best;
    }
    return null;
  }

  findSpawn(b, domain, toward) {
    const tw = toward || { x: MAP_W / 2, y: MAP_H / 2 };
    const maxR = domain === 'water' ? 5 : 3;
    for (let r = 1; r <= maxR; r++) {
      let best = null, bd = 1e9;
      for (let y = b.y - r; y <= b.y + b.h - 1 + r; y++) for (let x = b.x - r; x <= b.x + b.w - 1 + r; x++) {
        if (!(x === b.x - r || x === b.x + b.w - 1 + r || y === b.y - r || y === b.y + b.h - 1 + r)) continue;
        if (!this.passFor(domain, x, y)) continue;
        let busy = false;
        for (const u of this.units) {
          if (u.dead) continue;
          if (Math.abs(u.x - x - 0.5) < 0.45 && Math.abs(u.y - y - 0.5) < 0.45) { busy = true; break; }
        }
        if (busy) continue;
        const d = dist(x + 0.5, y + 0.5, tw.x, tw.y);
        if (d < bd) { bd = d; best = { x: x + 0.5, y: y + 0.5 }; }
      }
      if (best) return best;
    }
    return null;
  }

  /* ------------------------------- Production ---------------------------- */

  queueUnit(b, type) {
    const d = UNIT_DEFS[type], team = this.teams[b.team];
    if (!b.done || b.dead) return 'Not ready';
    if (!b.def.trains || !b.def.trains.includes(type)) return 'Cannot train that here';
    if (b.queue.length >= 5) return 'Queue is full';
    if (team.popUsed + d.pop > team.popCap) return team.popCap >= MAX_POP ? 'Population limit reached' : 'Need more houses';
    const miss = missingRes(team.res, d.cost);
    if (miss) return 'Not enough ' + miss;
    payCost(team.res, d.cost);
    team.popUsed += d.pop;
    b.queue.push({ kind: 'unit', type, time: d.time, cost: d.cost });
    return null;
  }
  techLevelPending(teamId, tech) {
    let n = 0;
    for (const b of this.buildings) if (b.team === teamId) for (const q of b.queue) if (q.kind === 'tech' && q.tech === tech) n++;
    return n;
  }
  queueTech(b, tech) {
    const T = TECHS[tech], team = this.teams[b.team];
    if (!b.done || b.dead) return 'Not ready';
    if (b.queue.length >= 5) return 'Queue is full';
    const lvl = team.up[tech] + this.techLevelPending(b.team, tech);
    if (lvl >= T.max) return 'Already fully researched';
    const cost = T.cost(lvl);
    const miss = missingRes(team.res, cost);
    if (miss) return 'Not enough ' + miss;
    payCost(team.res, cost);
    b.queue.push({ kind: 'tech', tech, time: T.time(lvl), cost });
    return null;
  }
  cancelQueue(b, i) {
    const q = b.queue[i];
    if (!q) return;
    const team = this.teams[b.team];
    refundCost(team.res, q.cost);
    if (q.kind === 'unit') team.popUsed -= UNIT_DEFS[q.type].pop;
    b.queue.splice(i, 1);
    if (i === 0) b.qt = 0;
  }

  updateBuilding(b, dt) {
    if (b.dead || !b.done) return;
    const d = b.def;
    if (d.atk) {
      b.cd = Math.max(0, b.cd - dt);
      b.scan -= dt;
      if (b.scan <= 0) {
        b.scan = 0.4;
        let best = null, bd = d.range + 1;
        for (const u of this.units) {
          if (u.dead || u.team === b.team) continue;
          const dd = distPointRect(u.x, u.y, b);
          if (dd < bd && dd <= d.range) { bd = dd; best = u; }
        }
        b.target = best;
      }
      if (b.target && !b.target.dead && b.cd <= 0) {
        if (distPointRect(b.target.x, b.target.y, b) <= d.range + 0.5) {
          b.cd = d.cd;
          const c = centerOf(b);
          this.fire(b, b.target, c.x, c.y, 'arrow');
        } else b.target = null;
      }
    }
    if (b.queue.length) {
      const q = b.queue[0];
      b.qt += dt;
      if (b.qt >= q.time) {
        if (this.finishQueueItem(b, q)) { b.queue.shift(); b.qt = 0; } else b.qt = q.time;
      }
    }
  }

  finishQueueItem(b, q) {
    const team = this.teams[b.team];
    if (q.kind === 'tech') {
      team.up[q.tech]++;
      this.events.push({ type: 'msg', team: b.team, text: TECHS[q.tech].name + ' researched' });
      return true;
    }
    const d = UNIT_DEFS[q.type];
    const toward = b.rally ? { x: b.rally.x, y: b.rally.y } : null;
    const pos = this.findSpawn(b, d.domain, toward);
    if (!pos) return false;
    const u = this.createUnit(b.team, q.type, pos.x, pos.y);
    const r = b.rally;
    if (r) {
      if (r.target && !r.target.dead && u.def.role === 'worker' && (r.target.kind === 'node' || r.target.type === 'farm')) this.orderGather([u], r.target);
      else this.orderMove([u], r.x, r.y);
    }
    return true;
  }

  completeBuilding(b) {
    b.done = true; b.progress = 1; b.hp = b.maxHp;
    this.recalcPop(b.team);
    this.events.push({ type: 'built', team: b.team, name: b.def.name, x: b.x, y: b.y });
  }

  /* --------------------------------- Orders ------------------------------ */

  release(u) {
    if (u.farm) { u.farm.users.delete(u.id); u.farm = null; }
  }
  setOrder(u, state, target, tx, ty) {
    this.release(u);
    u.state = state; u.target = target || null;
    if (tx != null) { u.tx = tx; u.ty = ty; }
    u.path = null; u.pi = 0; u.sub = null; u.repath = 0; u.auto = false; u.partial = false;
  }

  formationSpots(n, x, y, domain) {
    const sx = clamp(Math.floor(x), 0, MAP_W - 1), sy = clamp(Math.floor(y), 0, MAP_H - 1);
    const spots = [];
    const seen = new Set();
    const q = [[sx, sy]]; seen.add(sy * MAP_W + sx);
    let qi = 0;
    while (qi < q.length && spots.length < n) {
      const [cx, cy] = q[qi++];
      if (this.passFor(domain, cx, cy)) spots.push({ x: cx + 0.5, y: cy + 0.5 });
      for (const [dx, dy] of [[1, 0], [-1, 0], [0, 1], [0, -1], [1, 1], [-1, -1], [1, -1], [-1, 1]]) {
        const nx = cx + dx, ny = cy + dy, k = ny * MAP_W + nx;
        if (nx < 0 || ny < 0 || nx >= MAP_W || ny >= MAP_H || seen.has(k)) continue;
        if (Math.abs(nx - sx) > 9 || Math.abs(ny - sy) > 9) continue;
        seen.add(k); q.push([nx, ny]);
      }
    }
    return spots;
  }

  orderMove(units, x, y, mode) {
    const state = mode || 'move';
    for (const domain of ['land', 'water']) {
      const group = units.filter((u) => !u.dead && u.def.domain === domain);
      if (!group.length) continue;
      const spots = group.length === 1 ? [{ x, y }] : this.formationSpots(group.length, x, y, domain);
      const free = spots.slice();
      const order = group.slice().sort((a, b) => dist(a.x, a.y, x, y) - dist(b.x, b.y, x, y));
      for (const u of order) {
        let bi = 0, bd = 1e9;
        for (let i = 0; i < free.length; i++) { const d = dist(u.x, u.y, free[i].x, free[i].y); if (d < bd) { bd = d; bi = i; } }
        const s = free.length ? free.splice(bi, 1)[0] : { x, y };
        this.setOrder(u, state, null, s.x, s.y);
        if (state === 'attackmove' && u.def.role !== 'soldier') u.state = 'move';
      }
    }
  }
  orderAttackMove(units, x, y) { this.orderMove(units, x, y, 'attackmove'); }
  orderStop(units) { for (const u of units) if (!u.dead) { this.setOrder(u, 'idle'); } }

  canAttack(u, t) {
    if (!t || t.dead || t.team === u.team) return false;
    if (t.kind === 'unit' && t.def.domain === 'water' && u.def.domain === 'land' && u.def.range < 2) return false;
    if (t.kind === 'building' && u.def.domain === 'water' && u.def.role !== 'soldier') return false;
    return true;
  }
  orderAttack(units, t) {
    for (const u of units) {
      if (u.dead) continue;
      if (this.canAttack(u, t)) this.setOrder(u, 'attack', t);
      else { const c = centerOf(t); this.setOrder(u, 'move', null, c.x, c.y); }
    }
  }
  orderBuild(units, b) {
    for (const u of units) {
      if (u.dead || u.def.role !== 'worker') continue;
      if (u.state === 'gather' && u.gatherKind) u.lastKind = u.gatherKind;
      this.setOrder(u, 'build', b);
    }
  }
  orderGather(units, t) {
    const kind = t.kind === 'node' ? NODE_DEFS[t.type].res : 'food';
    for (const u of units) {
      if (u.dead || u.def.role !== 'worker') continue;
      let tt = t;
      if (t.type === 'farm') {
        if (!t.done) { this.orderBuild([u], t); continue; }
        if (t.users.size >= FARM_SLOTS && !t.users.has(u.id)) {
          tt = this.nearestFarm(u.x, u.y, u.team, 14);
          if (!tt) continue;
        }
      }
      this.setOrder(u, 'gather', tt);
      u.gatherKind = kind; u.sub = 'toRes';
      if (u.carryType !== kind) { u.carry = 0; u.carryType = kind; }
      if (tt.type === 'farm') { tt.users.add(u.id); u.farm = tt; }
    }
  }
  nearestFarm(x, y, teamId, maxd) {
    let best = null, bd = maxd;
    for (const b of this.buildings) {
      if (b.dead || b.team !== teamId || b.type !== 'farm' || !b.done || b.users.size >= FARM_SLOTS) continue;
      const d = distPointRect(x, y, b);
      if (d < bd) { bd = d; best = b; }
    }
    return best;
  }
  findNearestNode(kind, x, y, maxd) {
    let best = null, bd = maxd;
    for (const n of this.nodes) {
      if (n.dead || n.amt <= 0 || NODE_DEFS[n.type].res !== kind) continue;
      const d = Math.hypot(n.x + 0.5 - x, n.y + 0.5 - y);
      if (d < bd) { bd = d; best = n; }
    }
    return best;
  }
  findDropoff(u) {
    let best = null, bd = 1e9;
    for (const b of this.buildings) {
      if (b.dead || b.team !== u.team || !b.done || !b.def.drop) continue;
      const d = distPointRect(u.x, u.y, b);
      if (d < bd) { bd = d; best = b; }
    }
    return best;
  }
  retarget(u) {
    let t = null;
    if (u.gatherKind === 'food') t = this.nearestFarm(u.x, u.y, u.team, 16);
    else t = this.findNearestNode(u.gatherKind, u.x, u.y, 28);
    if (!t) return false;
    u.target = t; u.sub = 'toRes'; u.path = null; u.repath = 0;
    if (t.type === 'farm') { this.release(u); t.users.add(u.id); u.farm = t; }
    return true;
  }

  /* ---------------------------------- Units ------------------------------ */

  updateUnit(u, dt) {
    if (u.dead) return;
    u.cd = Math.max(0, u.cd - dt);
    u.atkAnim = Math.max(0, u.atkAnim - dt);
    u.moving = false; u.working = false; u.anim += dt;
    switch (u.state) {
      case 'idle': this.updateIdle(u, dt); break;
      case 'move':
        if (!u.path && !this.computePathPoint(u)) break;
        if (!this.followPath(u, dt)) this.setOrder(u, 'idle');
        break;
      case 'attackmove': this.updateAttackMove(u, dt); break;
      case 'attack': this.updateAttack(u, dt); break;
      case 'gather': this.updateGather(u, dt); break;
      case 'build': this.updateBuild(u, dt); break;
    }
  }

  updateIdle(u, dt) {
    if (u.def.role !== 'soldier') return;
    u.scan -= dt;
    if (u.scan > 0) return;
    u.scan = 0.4 + this.rng() * 0.2;
    const t = this.findTarget(u, u.def.sight + (u.def.range > 2 ? 1 : 0), 5);
    if (t) {
      const home = { x: u.x, y: u.y };
      this.setOrder(u, 'attack', t);
      u.auto = true; u.home = home;
    }
  }

  findTarget(u, radius, bRadius) {
    let best = null, bs = 1e9;
    for (const e of this.units) {
      if (e.dead || e.team === u.team) continue;
      if (Math.abs(e.x - u.x) > radius || Math.abs(e.y - u.y) > radius) continue;
      const d = Math.hypot(e.x - u.x, e.y - u.y);
      if (d > radius || !this.canAttack(u, e)) continue;
      const ig = u.ignore.get(e.id);
      if (ig && ig > this.time) continue;
      const s = d - (e.def.role === 'soldier' ? 2 : 0) + (e.def.role === 'worker' ? 1.5 : 0);
      if (s < bs) { bs = s; best = e; }
    }
    if (best) return best;
    for (const e of this.buildings) {
      if (e.dead || e.team === u.team) continue;
      const d = distPointRect(u.x, u.y, e);
      if (d > bRadius || !this.canAttack(u, e)) continue;
      const ig = u.ignore.get(e.id);
      if (ig && ig > this.time) continue;
      const s = d + 3 - (e.def.atk ? 4 : 0);
      if (s < bs) { bs = s; best = e; }
    }
    return best;
  }

  // Shared chase-and-hit logic. Returns 'done' if the target should be dropped.
  engage(u, t, dt) {
    const r = this.approach(u, t, this.reachOf(u, t), dt);
    if (r === 'fail') { u.ignore.set(t.id, this.time + 10); return 'done'; }
    if (r === true) {
      this.faceTo(u, t);
      if (u.cd <= 0) this.doAttack(u, t);
    }
    return null;
  }

  updateAttack(u, dt) {
    const t = u.target;
    if (!t || !this.canAttack(u, t)) { this.endAttack(u); return; }
    if (u.auto && u.home && dist(u.x, u.y, u.home.x, u.home.y) > 14) { u.target = null; this.endAttack(u); return; }
    if (this.engage(u, t, dt) === 'done') this.endAttack(u);
  }
  endAttack(u) {
    const auto = u.auto, home = u.home;
    u.target = null;
    if (auto && home) {
      const near = dist(u.x, u.y, home.x, home.y) < 14;
      const t = near ? this.findTarget(u, u.def.sight + 1, 5) : null;
      if (t) { u.target = t; u.path = null; u.repath = 0; return; }
      if (dist(u.x, u.y, home.x, home.y) > 3) {
        this.setOrder(u, 'move', null, home.x, home.y);
        return;
      }
    }
    this.setOrder(u, 'idle');
  }

  updateAttackMove(u, dt) {
    if (u.target && !this.canAttack(u, u.target)) { u.target = null; u.path = null; }
    if (!u.target) {
      u.scan -= dt;
      if (u.scan <= 0) {
        u.scan = 0.35;
        const f = this.findTarget(u, u.def.sight + 1.5, u.def.sight + 1.5);
        if (f) { u.target = f; u.path = null; u.repath = 0; }
      }
    }
    if (u.target) {
      if (this.engage(u, u.target, dt) === 'done') { u.target = null; u.path = null; }
      return;
    }
    if (!u.path && !this.computePathPoint(u)) return;
    if (!this.followPath(u, dt)) this.setOrder(u, 'idle');
  }

  doAttack(u, t) {
    u.cd = u.def.cd; u.atkAnim = 0.28;
    if (u.def.range > 2) {
      this.fire(u, t, u.x, u.y, u.def.atkType === 'siege' ? 'stone' : 'arrow');
    } else {
      this.damage(u, t);
    }
  }

  fire(att, t, fx, fy, kind) {
    const c = centerOf(t);
    const d = Math.hypot(c.x - fx, c.y - fy);
    const speed = kind === 'stone' ? 9 : 14;
    this.projectiles.push({ att, target: t, sx: fx, sy: fy, t: 0, dur: Math.max(0.15, d / speed), kind });
  }

  damage(att, t) {
    if (t.dead) return;
    const ad = att.def;
    const team = this.teams[att.team];
    let dmg = ad.atk;
    if (att.kind === 'unit' && ad.role === 'soldier' && ad.atkType !== 'siege') dmg += 2 * team.up.weapons;
    if (ad.bonus && t.def.cls && ad.bonus[t.def.cls]) dmg += ad.bonus[t.def.cls];
    let armor = t.def.armor[ad.atkType] || 0;
    if (t.kind === 'unit' && t.def.role === 'soldier' && ad.atkType !== 'siege') armor += this.teams[t.team].up.armor;
    dmg = Math.max(1, dmg - armor);
    t.hp -= dmg; t.lastHit = this.time;
    const tt = this.teams[t.team];
    if (t.team === 0 && this.time - tt.lastAlert > 8) {
      tt.lastAlert = this.time;
      const c = centerOf(t);
      this.events.push({ type: 'alert', team: 0, x: c.x, y: c.y });
    }
    // idle defenders strike back
    if (t.kind === 'unit' && t.state === 'idle' && t.def.role === 'soldier' && att.kind === 'unit' && this.canAttack(t, att)) {
      const home = { x: t.x, y: t.y };
      this.setOrder(t, 'attack', att); t.auto = true; t.home = home;
    }
    if (t.hp <= 0) this.kill(t);
  }

  kill(e) {
    if (e.dead) return;
    e.dead = true;
    const c = centerOf(e);
    if (e.kind === 'unit') {
      this.teams[e.team].popUsed = Math.max(0, this.teams[e.team].popUsed - e.def.pop);
      this.release(e);
      this.burst(c.x, c.y, e.type === 'warship' ? 14 : 5, e.type === 'warship' ? '#6b4a2b' : '#8a7a5a', 0.6);
    } else if (e.kind === 'building') {
      for (let j = 0; j < e.h; j++) for (let i = 0; i < e.w; i++) {
        const k = (e.y + j) * MAP_W + e.x + i;
        if (this.occ[k] === e) this.occ[k] = null;
      }
      const team = this.teams[e.team];
      for (const q of e.queue) if (q.kind === 'unit') team.popUsed = Math.max(0, team.popUsed - UNIT_DEFS[q.type].pop);
      e.queue.length = 0;
      this.recalcPop(e.team);
      this.burst(c.x, c.y, 10 + e.w * 6, '#7a6a52', 1.4);
      this.events.push({ type: 'destroyed', team: e.team, name: e.def.name, x: c.x, y: c.y });
    }
  }

  burst(x, y, n, color, spread) {
    for (let i = 0; i < n; i++) {
      const a = this.rng() * 6.283, s = (0.3 + this.rng()) * spread;
      this.effects.push({ type: 'dust', x, y, vx: Math.cos(a) * s, vy: Math.sin(a) * s, z: 4 + this.rng() * 8, vz: 20 + this.rng() * 30, life: 0.7 + this.rng() * 0.5, max: 1.2, color });
    }
  }
  floatText(x, y, text, color) {
    this.effects.push({ type: 'text', x, y, z: 24, text, color, life: 1.1, max: 1.1 });
  }

  /* -------------------------------- Gathering ---------------------------- */

  updateGather(u, dt) {
    const team = this.teams[u.team];
    const kind = u.gatherKind;
    if (u.sub === 'work') {
      const t = u.target;
      if (!t || t.dead || (t.kind === 'node' && t.amt <= 0)) {
        if (u.carry >= 1) { u.sub = 'toDrop'; u.path = null; u.repath = 0; }
        else if (!this.retarget(u)) this.setOrder(u, 'idle');
        return;
      }
      if (this.distTo(u, t) > 1.35) { u.sub = 'toRes'; u.path = null; u.repath = 0; return; }
      u.working = true; this.faceTo(u, t);
      let amt;
      if (t.kind === 'node') {
        amt = Math.min(NODE_DEFS[t.type].rate * team.gatherMult * dt, t.amt);
        t.amt -= amt;
        if (t.amt <= 0.001) this.depleteNode(t);
      } else amt = FARM_RATE * team.gatherMult * dt;
      u.carry += amt; u.carryType = kind;
      if (u.carry >= u.def.carry) { u.sub = 'toDrop'; u.path = null; u.repath = 0; u.drop = null; }
      return;
    }
    if (u.sub === 'toRes') {
      const t = u.target;
      if (!t || t.dead || (t.kind === 'node' && t.amt <= 0)) {
        if (!this.retarget(u)) {
          if (u.carry >= 1) { u.sub = 'toDrop'; u.path = null; u.repath = 0; } else this.setOrder(u, 'idle');
        }
        return;
      }
      const r = this.approach(u, t, 0.95, dt);
      if (r === true) { u.sub = 'work'; u.path = null; if (u.carryType !== kind) { u.carry = 0; u.carryType = kind; } }
      else if (r === 'fail') { if (!this.retarget(u) || u.target === t) this.setOrder(u, 'idle'); }
      return;
    }
    if (u.sub === 'toDrop') {
      let d = u.drop;
      if (!d || d.dead || !d.done) d = u.drop = this.findDropoff(u);
      if (!d) { this.setOrder(u, 'idle'); return; }
      const r = this.approach(u, d, 1.0, dt);
      if (r === true) {
        const n = Math.floor(u.carry);
        if (n > 0) {
          team.res[u.carryType] += n;
          if (u.team === 0) this.floatText(u.x, u.y, '+' + n + ' ' + RES_ICON[u.carryType], '#fff');
        }
        u.carry -= n;
        u.sub = 'toRes'; u.path = null; u.repath = 0; u.drop = null;
      } else if (r === 'fail') { u.drop = null; u.path = null; u.repath = 0.6; }
    }
  }
  depleteNode(n) {
    n.dead = true;
    const k = n.y * MAP_W + n.x;
    if (this.occ[k] === n) this.occ[k] = null;
  }

  /* ---------------------------------- Building --------------------------- */

  updateBuild(u, dt) {
    const b = u.target;
    if (!b || b.dead) { this.afterBuild(u, null); return; }
    if (b.done && b.hp >= b.maxHp) { this.afterBuild(u, b); return; }
    const r = this.approach(u, b, 0.95, dt);
    if (r === 'fail') { this.setOrder(u, 'idle'); return; }
    if (r !== true) return;
    u.working = true; this.faceTo(u, b);
    if (!b.done) {
      b.progress += dt / b.def.time;
      b.hp = Math.min(b.maxHp, b.hp + b.maxHp * 0.9 * dt / b.def.time);
      if (b.progress >= 1) this.completeBuilding(b);
    } else {
      b.hp = Math.min(b.maxHp, b.hp + b.maxHp * dt / (b.def.time * 1.5));
    }
  }
  afterBuild(u, b) {
    if (b && b.type === 'farm' && b.users.size < FARM_SLOTS) { this.orderGather([u], b); return; }
    if (u.lastKind) {
      const kind = u.lastKind;
      const n = kind === 'food' ? this.nearestFarm(u.x, u.y, u.team, 16) : this.findNearestNode(kind, u.x, u.y, 20);
      if (n) { this.orderGather([u], n); return; }
    }
    this.setOrder(u, 'idle');
  }

  /* ------------------------- Projectiles / effects ----------------------- */

  updateProjectiles(dt) {
    for (const p of this.projectiles) {
      p.t += dt;
      if (p.t >= p.dur) {
        p.done = true;
        if (p.target && !p.target.dead) this.damage(p.att, p.target);
      }
    }
    this.projectiles = this.projectiles.filter((p) => !p.done);
  }
  updateEffects(dt) {
    for (const e of this.effects) {
      e.life -= dt;
      if (e.type === 'dust') { e.x += e.vx * dt; e.y += e.vy * dt; e.z += e.vz * dt; e.vz -= 60 * dt; if (e.z < 0) { e.z = 0; e.vz = 0; } }
      else if (e.type === 'text') e.z += 14 * dt;
    }
    this.effects = this.effects.filter((e) => e.life > 0);
  }

  separate(dt) {
    const us = this.units, n = us.length;
    const k = Math.min(1, dt * 10);
    for (let i = 0; i < n; i++) {
      const a = us[i];
      if (a.dead) continue;
      for (let j = i + 1; j < n; j++) {
        const b = us[j];
        if (b.dead || a.def.domain !== b.def.domain) continue;
        const dx = b.x - a.x, dy = b.y - a.y;
        if (dx > 0.55 || dx < -0.55 || dy > 0.55 || dy < -0.55) continue;
        let d = Math.hypot(dx, dy);
        if (d >= 0.55) continue;
        let nx, ny;
        if (d < 0.001) { nx = this.rng() - 0.5; ny = this.rng() - 0.5; const l = Math.hypot(nx, ny) || 1; nx /= l; ny /= l; d = 0; }
        else { nx = dx / d; ny = dy / d; }
        // units that are walking barge past units that are standing still
        const wa = a.moving ? 0.2 : 1, wb = b.moving ? 0.2 : 1;
        const push = (0.55 - d) * k, sa = push * wa / (wa + wb), sb = push * wb / (wa + wb);
        this.shift(a, -nx * sa, -ny * sa);
        this.shift(b, nx * sb, ny * sb);
      }
    }
  }
  shift(u, dx, dy) {
    const nx = u.x + dx, ny = u.y + dy;
    if (this.passFor(u.def.domain, Math.floor(nx), Math.floor(ny))) { u.x = nx; u.y = ny; }
  }

  /* --------------------------------- Update ------------------------------ */

  update(dt) {
    if (this.winner !== null) { this.updateEffects(dt); return; }
    this.time += dt;
    this.pathBudget = 14;
    for (const ai of this.ais) ai.update(dt);
    for (const b of this.buildings) this.updateBuilding(b, dt);
    for (const u of this.units) this.updateUnit(u, dt);
    this.separate(dt);
    this.updateProjectiles(dt);
    this.updateEffects(dt);
    this.units = this.units.filter((u) => !u.dead);
    this.buildings = this.buildings.filter((b) => !b.dead);
    if (this.nodes.length && this.nodes.some((n) => n.dead)) this.nodes = this.nodes.filter((n) => !n.dead);
    this.winCheck -= dt;
    if (this.winCheck <= 0) {
      this.winCheck = 1;
      const alive = [0, 1].map((t) => this.buildings.some((b) => b.team === t));
      if (!alive[0]) this.winner = 1;
      else if (!alive[1]) this.winner = 0;
    }
  }
}

/* =================================== AI =================================== */

class AI {
  constructor(game, id, diffName) {
    this.g = game; this.id = id;
    this.diff = DIFFS[diffName] || DIFFS.normal;
    this.t = 0; this.next = 0.5;
    this.waveSize = this.diff.wave; this.waveId = 0; this.waveStart = 0; this.attacking = false;
    this.portTry = 0; this.rally = null;
    const team = game.teams[id];
    team.gatherMult = this.diff.gather;
    for (const k of RES_LIST) team.res[k] += this.diff.bonus;
  }

  update(dt) {
    this.t += dt;
    if (this.t < this.next) return;
    this.next = this.t + this.diff.think;
    this.think();
  }

  pickBuilder(builders, x, y) {
    let best = null, bd = 1e9;
    for (const u of builders) {
      if (u.state === 'build') continue;
      const d = dist(u.x, u.y, x, y) + (u.state === 'idle' ? -8 : 0) + (u.carry > 6 ? 6 : 0);
      if (d < bd) { bd = d; best = u; }
    }
    return best;
  }

  tryBuild(type, cx, cy, rmin, rmax, margin, builders) {
    const g = this.g, d = BUILDING_DEFS[type], team = g.teams[this.id];
    if (!canAfford(team.res, d.cost)) return false;
    const pos = g.findPlacement(type, cx, cy, rmin, rmax, margin);
    if (!pos) return false;
    const bu = this.pickBuilder(builders, pos.x, pos.y);
    if (!bu) return false;
    if (bu.state === 'gather' && bu.gatherKind) bu.lastKind = bu.gatherKind;
    const r = g.placeBuilding(this.id, type, pos.x, pos.y, [bu]);
    return !!r.b;
  }

  think() {
    const g = this.g, id = this.id, team = g.teams[id], R = team.res;
    const myB = g.buildings.filter((b) => b.team === id);
    const myU = g.units.filter((u) => u.team === id);
    if (!myB.length) return;
    const builders = myU.filter((u) => u.type === 'builder');
    const army = myU.filter((u) => u.def.role === 'soldier' && u.def.domain === 'land');
    const ships = myU.filter((u) => u.type === 'warship');
    const done = (t) => myB.filter((b) => b.type === t && b.done);
    const count = (t) => myB.filter((b) => b.type === t).length;
    const tc = done('towncenter')[0];
    const baseB = tc || myB[0];
    const base = centerOf(baseB);
    const enemyBase = g.bases[1 - id];
    const bt = g.time / this.diff.pace;
    const dirX = Math.sign(enemyBase.x - base.x) || 1;

    if (!this.rally) {
      const pos = g.nearestFree(base.x + dirX * 8, base.y, 'land');
      this.rally = pos || { x: base.x, y: base.y };
    }

    /* ---- economy: builders ---- */
    const maxB = Math.min(this.diff.maxB, 6 + Math.floor(bt / 25));
    const queuedB = myB.reduce((n, b) => n + b.queue.filter((q) => q.type === 'builder').length, 0);
    if (tc && builders.length + queuedB < maxB && tc.queue.length < 2) g.queueUnit(tc, 'builder');

    /* ---- houses ---- */
    const room = team.popCap - team.popUsed;
    const housesBuilding = myB.filter((b) => b.type === 'house' && !b.done).length;
    const prod = myB.filter((b) => b.def.trains && b.type !== 'towncenter').length;
    if (room <= 3 + prod * 2 && housesBuilding === 0 && team.popCap < MAX_POP) {
      this.tryBuild('house', base.x, base.y, 5, 14, 1, builders);
    }

    /* ---- builder jobs ---- */
    const cnt = { food: 0, wood: 0, gold: 0, stone: 0 };
    for (const u of builders) if (u.state === 'gather' && u.gatherKind) cnt[u.gatherKind]++;
    const w = builders.length < 10 ? { food: 0.38, wood: 0.52, gold: 0.1, stone: 0 } : { food: 0.3, wood: 0.38, gold: 0.17, stone: 0.15 };
    if (builders.length >= 6 && R.stone < 120 && count('barracks') < 3) w.stone += 0.1;
    if (R.gold < 150 && builders.length >= 8) w.gold += 0.25;
    if (R.wood < 100) w.wood += 0.15;
    if (R.food < 100) w.food += 0.15;
    const pile = { food: 900, wood: 900, gold: 900, stone: 350 };
    for (const k of RES_LIST) if (R[k] > pile[k]) w[k] *= 0.2;
    const total = Math.max(1, builders.length);
    // free up builders when a resource is piling up
    for (const k of RES_LIST) {
      if (R[k] > pile[k] + 150 && cnt[k] > 2) {
        const b = builders.find((u) => u.state === 'gather' && u.gatherKind === k);
        if (b) { g.setOrder(b, 'idle'); cnt[k]--; }
      }
    }
    let farmsTotal = myB.filter((b) => b.type === 'farm').length;
    for (const u of builders) {
      if (u.state !== 'idle') continue;
      let kind = null, bestDef = -1e9;
      for (const k of RES_LIST) {
        const def = w[k] * total - cnt[k];
        if (def > bestDef) { bestDef = def; kind = k; }
      }
      if (kind === 'food') {
        const farm = g.nearestFarm(u.x, u.y, id, 30);
        if (farm) { g.orderGather([u], farm); cnt.food++; continue; }
        if (farmsTotal < 9 && farmsTotal * FARM_SLOTS < cnt.food + 3 && R.wood >= 60 && this.tryBuild('farm', base.x, base.y, 3, 9, 0, [u])) { farmsTotal++; u.lastKind = 'food'; cnt.food++; continue; }
        kind = 'wood';
      }
      const node = g.findNearestNode(kind, u.x, u.y, 80) || g.findNearestNode(kind, base.x, base.y, 120);
      if (!node) continue;
      // storehouse near far-away resources
      let dropNear = false;
      for (const b of myB) if (b.def.drop && distPointRect(node.x + 0.5, node.y + 0.5, b) < 8) { dropNear = true; break; }
      if (!dropNear && R.wood >= 80) {
        u.lastKind = kind;
        if (this.tryBuild('storehouse', node.x, node.y, 2, 6, 0, [u])) { cnt[kind]++; continue; }
      }
      g.orderGather([u], node); cnt[kind]++;
    }

    /* ---- rebuild a lost town center ---- */
    if (!count('towncenter') && R.wood >= 300 && R.stone >= 200) this.tryBuild('towncenter', base.x, base.y, 5, 14, 1, builders);

    /* ---- build order ---- */
    const want = [
      ['barracks', builders.length >= 6 && count('barracks') < 1],
      ['archery', done('barracks').length > 0 && count('archery') < 1],
      ['stable', bt > 140 && count('stable') < 1],
      ['blacksmith', bt > 220 && count('blacksmith') < 1],
      ['barracks', bt > 280 && builders.length >= 14 && count('barracks') < 2],
      ['archery', bt > 380 && count('archery') < 2],
      ['tower', bt > 330 && count('tower') < Math.min(5, 2 + Math.floor((bt - 330) / 240))],
      ['stable', bt > 450 && count('stable') < 2],
    ];
    const buildingNow = myB.filter((b) => !b.done && b.type !== 'house' && b.type !== 'farm' && b.type !== 'storehouse').length;
    if (buildingNow < 2) {
      for (const [type, cond] of want) {
        if (!cond) continue;
        let ok;
        if (type === 'tower') ok = this.tryBuild(type, base.x + dirX * 7, base.y, 0, 6, 1, builders);
        else ok = this.tryBuild(type, base.x, base.y, 6, 15, 1, builders);
        if (ok) break;
      }
    }
    if (bt > 450 && !count('port') && this.t > this.portTry && builders.length >= 8) {
      this.portTry = this.t + 20;
      if (canAfford(R, BUILDING_DEFS.port.cost)) this.tryBuild('port', base.x, base.y, 4, 34, 1, builders);
    }

    /* ---- training ---- */
    for (const b of myB) {
      if (!b.done || !b.def.trains || b.type === 'towncenter' || b.queue.length >= 2) continue;
      let type = null;
      if (b.type === 'barracks') {
        const sw = army.filter((u) => u.type === 'swordsman').length, sp = army.filter((u) => u.type === 'spearman').length;
        type = sw <= sp ? 'swordsman' : 'spearman';
      } else if (b.type === 'archery') type = 'archer';
      else if (b.type === 'stable') type = 'cavalry';
      else if (b.type === 'port') { if (ships.length + b.queue.length < (id === 1 ? 3 : 2)) type = 'warship'; }
      if (type) g.queueUnit(b, type);
      b.rally = { x: this.rally.x, y: this.rally.y, target: null };
    }
    if (tc && !tc.rally) tc.rally = null;
    for (const b of done('blacksmith')) {
      if (b.queue.length) continue;
      const order = team.up.weapons <= team.up.armor ? ['weapons', 'armor'] : ['armor', 'weapons'];
      for (const t of order) {
        const lvl = team.up[t] + g.techLevelPending(id, t);
        if (lvl >= TECHS[t].max) continue;
        const c = TECHS[t].cost(lvl);
        if (canAfford(R, { food: c.food + 150, gold: c.gold + 100 })) { g.queueTech(b, t); break; }
      }
    }

    /* ---- defense ---- */
    let threat = null, td = 1e9;
    for (const e of g.units) {
      if (e.dead || e.team === id) continue;
      for (const b of myB) {
        const d = distPointRect(e.x, e.y, b);
        if (d < 12 && d < td) { td = d; threat = e; }
      }
    }
    if (threat && !this.attacking) {
      for (const u of army) if (u.state === 'idle' || (u.state === 'move' && !u.target)) g.orderAttackMove([u], threat.x, threat.y);
      for (const u of ships) if (u.state === 'idle' && threat.def.domain === 'water') g.orderAttackMove([u], threat.x, threat.y);
    } else if (!this.attacking) {
      for (const u of army) if (u.state === 'idle' && dist(u.x, u.y, this.rally.x, this.rally.y) > 8) g.orderMove([u], this.rally.x, this.rally.y);
    }

    /* ---- offense ---- */
    const target = this.pickTarget(base.x, base.y);
    if (!this.attacking && army.length >= this.waveSize && target) {
      this.attacking = true; this.waveId++; this.waveStart = army.length;
      for (const u of army) u.wave = this.waveId;
      g.orderAttackMove(army, target.x, target.y);
      g.events.push({ type: 'msg', team: 0, text: 'The Yellow army is on the march!', alertEnemy: true });
    } else if (this.attacking) {
      const mine = army.filter((u) => u.wave === this.waveId);
      if (mine.length < Math.max(2, this.waveStart * 0.25) || !target) {
        this.attacking = false; this.waveSize += this.diff.grow;
        g.orderMove(mine, this.rally.x, this.rally.y);
      } else {
        for (const u of mine) if (u.state === 'idle') g.orderAttackMove([u], target.x, target.y);
      }
    }
    if (ships.length >= 2 && target) {
      for (const u of ships) if (u.state === 'idle') g.orderAttackMove([u], target.x, target.y);
    }
  }

  pickTarget(fx, fy) {
    const g = this.g;
    let best = null, bd = 1e9;
    for (const b of g.buildings) {
      if (b.team === this.id) continue;
      const c = centerOf(b);
      const d = dist(fx, fy, c.x, c.y) - (b.type === 'towncenter' ? 6 : 0);
      if (d < bd) { bd = d; best = c; }
    }
    if (best) return best;
    for (const u of g.units) {
      if (u.team === this.id) continue;
      const d = dist(fx, fy, u.x, u.y);
      if (d < bd) { bd = d; best = { x: u.x, y: u.y }; }
    }
    return best;
  }
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    Game, AI, MAP_W, MAP_H, TILE_W, TILE_H, TEAMS, UNIT_DEFS, BUILDING_DEFS, NODE_DEFS, TECHS, DIFFS,
    T_GRASS, T_WATER, T_SAND, MAX_POP, canAfford, centerOf,
  };
}
