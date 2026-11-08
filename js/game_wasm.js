'use strict';

/* ==========================================================================
   Bridge between the C simulation (c/game.c, compiled to WebAssembly and
   embedded in wasm_blob.js) and the renderer / HUD in ui.js.

   The C side owns all game state. After each batch of updates, sync() copies
   a flat snapshot out of wasm memory into persistent JS objects (one per
   unit / building / resource node, keyed by id) so the UI can keep selecting
   and drawing them like ordinary objects. Orders go the other way through
   small exported functions.

   Needs the globals from defs.js and GAME_WASM_BASE64 from wasm_blob.js.
   ========================================================================== */

let _X = null;            // wasm exports
let _mem = null;          // wasm linear memory (never grows: the core uses static storage only)

const gameReady = (async () => {
  const bin = atob(GAME_WASM_BASE64);
  const bytes = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i);
  const { instance } = await WebAssembly.instantiate(bytes, {});
  _X = instance.exports; _mem = _X.memory;
  // Make sure JS and C agree on the snapshot layout.
  const expect = [20, 34, 6, 8, 9, 6, 8];
  expect.forEach((v, i) => { if (_X.g_layout(i) !== v) throw new Error('wasm snapshot layout mismatch at ' + i); });
})();

const U_STRIDE = 20, B_STRIDE = 34, N_STRIDE = 6, P_STRIDE = 8, E_STRIDE = 9, EV_STRIDE = 6, T_STRIDE = 8;
const STATE_NAMES = ['idle', 'move', 'attackmove', 'attack', 'gather', 'build'];
const SUB_NAMES = [null, 'toRes', 'work', 'toDrop'];
const DUST_COLORS = ['#6b4a2b', '#8a7a5a', '#7a6a52'];
const ERROR_TEXT = {
  1: 'Not ready', 2: 'Cannot train that here', 3: 'Queue is full', 4: 'Need more houses', 5: 'Population limit reached',
  6: 'Not enough food', 7: 'Not enough wood', 8: 'Not enough gold', 9: 'Not enough stone',
  10: 'Already fully researched', 11: 'Cannot build here', 12: 'Ports must be built on the lake shore',
};

class Game {
  constructor(opts = {}) {
    if (!_X) throw new Error('Game core is not loaded yet (await gameReady first)');
    const diffIdx = Math.max(0, DIFF_KEYS.indexOf(opts.difficulty || 'normal'));
    const seed = opts.seed != null ? opts.seed : (Math.random() * 4294967296) >>> 0;
    const aiTeams = opts.aiTeams || [1];
    let mask = 0; for (const t of aiTeams) mask |= 1 << t;
    this.seed = seed; this.difficulty = DIFF_KEYS[diffIdx];
    _X.g_init(seed >>> 0, diffIdx, mask);

    this.units = []; this.buildings = []; this.nodes = []; this.projectiles = []; this.effects = []; this.events = [];
    this.time = 0; this.winner = null;
    this.teams = TEAMS.map((t) => ({ ...t, res: { food: 0, wood: 0, gold: 0, stone: 0 }, popUsed: 0, popCap: 0, up: { weapons: 0, armor: 0 } }));
    this.terrain = new Uint8Array(_mem.buffer, _X.g_terrain(), MAP_W * MAP_H);
    this.lake = new Uint8Array(_mem.buffer, _X.g_lake(), MAP_W * MAP_H);
    this.shade = new Uint8Array(_mem.buffer, _X.g_shade(), MAP_W * MAP_H);
    this.occ = new Array(MAP_W * MAP_H).fill(null);
    this._u = new Map(); this._b = new Map(); this._n = new Map();
    this.sync();
  }

  update(dt) { _X.g_update(dt); }

  /* Copy the C state into JS objects. Call after update() calls and after any command. */
  sync() {
    _X.g_snapshot();
    const buf = _mem.buffer;
    const cnt = new Int32Array(buf, _X.g_snap_counts(), 6);
    const nu = cnt[0], nb = cnt[1], nn = cnt[2], np = cnt[3], ne = cnt[4], nev = cnt[5];
    const U = new Float32Array(buf, _X.g_snap_units(), nu * U_STRIDE);
    const B = new Float32Array(buf, _X.g_snap_blds(), nb * B_STRIDE);
    const N = new Float32Array(buf, _X.g_snap_nodes(), nn * N_STRIDE);
    const P = new Float32Array(buf, _X.g_snap_projs(), np * P_STRIDE);
    const E = new Float32Array(buf, _X.g_snap_effs(), ne * E_STRIDE);
    const EV = new Float32Array(buf, _X.g_snap_events(), nev * EV_STRIDE);
    const T = new Float32Array(buf, _X.g_snap_teams(), 2 * T_STRIDE + 2);

    // teams / clock
    for (let t = 0; t < 2; t++) {
      const o = t * T_STRIDE, tm = this.teams[t];
      tm.res.food = T[o]; tm.res.wood = T[o + 1]; tm.res.gold = T[o + 2]; tm.res.stone = T[o + 3];
      tm.popUsed = T[o + 4]; tm.popCap = T[o + 5]; tm.up.weapons = T[o + 6]; tm.up.armor = T[o + 7];
    }
    this.time = T[2 * T_STRIDE];
    const w = T[2 * T_STRIDE + 1]; this.winner = w < 0 ? null : w;

    // units
    const seenU = new Set(), units = [];
    for (let i = 0; i < nu; i++) {
      const o = i * U_STRIDE, id = U[o];
      let e = this._u.get(id);
      if (!e) {
        const type = UNIT_KEYS[U[o + 1]];
        e = { kind: 'unit', id, type, def: UNIT_DEFS[type], team: U[o + 2], dead: false, lastKind: null };
        this._u.set(id, e);
      }
      e.x = U[o + 3]; e.y = U[o + 4]; e.hp = U[o + 5]; e.maxHp = U[o + 6];
      e.state = STATE_NAMES[U[o + 7]]; e.face = U[o + 8]; e.anim = U[o + 9]; e.moving = !!U[o + 10]; e.working = !!U[o + 11];
      e.carry = U[o + 12]; e.carryType = U[o + 13] >= 0 ? RES_LIST[U[o + 13]] : null; e.gatherKind = U[o + 14] >= 0 ? RES_LIST[U[o + 14]] : null;
      e.sub = SUB_NAMES[U[o + 15]]; e.atkAnim = U[o + 16]; e.cd = U[o + 17]; e.targetDone = !!U[o + 18]; e.targetId = U[o + 19];
      seenU.add(id); units.push(e);
    }
    for (const [id, e] of this._u) if (!seenU.has(id)) { e.dead = true; this._u.delete(id); }
    this.units = units;

    // buildings
    const seenB = new Set(), blds = [];
    for (let i = 0; i < nb; i++) {
      const o = i * B_STRIDE, id = B[o];
      let e = this._b.get(id);
      if (!e) {
        const type = BUILDING_KEYS[B[o + 1]];
        e = { kind: 'building', id, type, def: BUILDING_DEFS[type], team: B[o + 2], x: B[o + 3], y: B[o + 4], w: B[o + 5], h: B[o + 6], dead: false, queue: [], users: { size: 0 }, rally: null };
        this._b.set(id, e);
      }
      e.hp = B[o + 7]; e.maxHp = B[o + 8]; e.done = !!B[o + 9]; e.progress = B[o + 10]; e.qt = B[o + 11];
      const ql = B[o + 12];
      e.queue.length = 0;
      for (let q = 0; q < ql; q++) {
        const k = B[o + 18 + q * 3], idx = B[o + 19 + q * 3], time = B[o + 20 + q * 3];
        e.queue.push(k === 0 ? { kind: 'unit', type: UNIT_KEYS[idx], time } : { kind: 'tech', tech: TECH_KEYS[idx], time });
      }
      e.rally = B[o + 13] ? { x: B[o + 14], y: B[o + 15], target: null } : null;
      e.users.size = B[o + 17];
      seenB.add(id); blds.push(e);
    }
    for (const [id, e] of this._b) if (!seenB.has(id)) { e.dead = true; this._b.delete(id); }
    this.buildings = blds;

    // resource nodes
    const seenN = new Set(), nodes = [];
    for (let i = 0; i < nn; i++) {
      const o = i * N_STRIDE, id = N[o];
      let e = this._n.get(id);
      if (!e) { e = { kind: 'node', id, type: NODE_KEYS[N[o + 1]], x: N[o + 2], y: N[o + 3], w: 1, h: 1, max: N[o + 5], dead: false }; this._n.set(id, e); }
      e.amt = N[o + 4];
      seenN.add(id); nodes.push(e);
    }
    for (const [id, e] of this._n) if (!seenN.has(id)) { e.dead = true; this._n.delete(id); }
    this.nodes = nodes;

    // tile occupancy, used by the UI for picking and placement previews
    this.occ.fill(null);
    for (const n of nodes) this.occ[n.y * MAP_W + n.x] = n;
    for (const b of blds) for (let j = 0; j < b.h; j++) for (let i = 0; i < b.w; i++) this.occ[(b.y + j) * MAP_W + b.x + i] = b;

    // projectiles, effects, events
    const proj = [];
    for (let i = 0; i < np; i++) {
      const o = i * P_STRIDE;
      proj.push({ sx: P[o], sy: P[o + 1], t: P[o + 2], dur: P[o + 3], kind: P[o + 4] ? 'stone' : 'arrow', target: { kind: 'unit', x: P[o + 5], y: P[o + 6] } });
    }
    this.projectiles = proj;
    const eff = [];
    for (let i = 0; i < ne; i++) {
      const o = i * E_STRIDE;
      if (E[o] === 0) eff.push({ type: 'dust', x: E[o + 1], y: E[o + 2], z: E[o + 3], life: E[o + 4], max: E[o + 5], color: DUST_COLORS[E[o + 6]] || '#8a7a5a' });
      else eff.push({ type: 'text', x: E[o + 1], y: E[o + 2], z: E[o + 3], life: E[o + 4], max: E[o + 5], text: '+' + E[o + 7] + ' ' + RES_ICON[RES_LIST[E[o + 8]]], color: '#fff' });
    }
    this.effects = eff;
    for (let i = 0; i < nev; i++) {
      const o = i * EV_STRIDE, type = EV[o], team = EV[o + 1], a = EV[o + 4];
      if (type === 1) this.events.push({ type: 'alert', team, x: EV[o + 2], y: EV[o + 3] });
      else if (type === 2) this.events.push({ type: 'built', team, name: BUILDING_DEFS[BUILDING_KEYS[a]].name, x: EV[o + 2], y: EV[o + 3] });
      else if (type === 3) this.events.push({ type: 'destroyed', team, name: BUILDING_DEFS[BUILDING_KEYS[a]].name, x: EV[o + 2], y: EV[o + 3] });
      else if (type === 4) this.events.push({ type: 'msg', team, text: TECHS[TECH_KEYS[a]].name + ' researched' });
      else if (type === 5) this.events.push({ type: 'msg', team: 0, text: 'The Yellow army is on the march!', alertEnemy: true });
    }
  }

  /* ------------------------------- Commands ------------------------------ */

  _ids(list) {
    const buf = new Int32Array(_mem.buffer, _X.g_idbuf(), _X.g_idbuf_len());
    let n = 0;
    for (const e of list) { if (n >= buf.length) break; if (!e.dead) buf[n++] = e.id; }
    return n;
  }
  orderMove(units, x, y) { _X.g_order_move(this._ids(units), x, y, 0); this.sync(); }
  orderAttackMove(units, x, y) { _X.g_order_move(this._ids(units), x, y, 1); this.sync(); }
  orderStop(units) { _X.g_order_stop(this._ids(units)); this.sync(); }
  orderAttack(units, target) { _X.g_order_attack(this._ids(units), target.id); this.sync(); }
  orderBuild(units, building) { _X.g_order_build(this._ids(units), building.id); this.sync(); }
  orderGather(units, target) { _X.g_order_gather(this._ids(units), target.id); this.sync(); }

  canPlace(type, x, y, margin = 0) { return !!_X.g_can_place(BUILDING_KEYS.indexOf(type), x, y, margin); }
  placeBuilding(teamId, type, x, y, builders) {
    const n = this._ids(builders || []);
    const r = _X.g_place(teamId, BUILDING_KEYS.indexOf(type), x, y, n);
    if (r < 0) return { err: ERROR_TEXT[-r] || 'Cannot build here' };
    this.sync();
    return { b: this._b.get(r) };
  }
  queueUnit(b, type) { const c = _X.g_queue_unit(b.id, UNIT_KEYS.indexOf(type)); this.sync(); return c ? ERROR_TEXT[c] : null; }
  queueTech(b, tech) { const c = _X.g_queue_tech(b.id, TECH_KEYS.indexOf(tech)); this.sync(); return c ? ERROR_TEXT[c] : null; }
  cancelQueue(b, i) { _X.g_cancel_queue(b.id, i); this.sync(); }
  techLevelPending(teamId, tech) { return _X.g_tech_pending(teamId, TECH_KEYS.indexOf(tech)); }
  setRally(b, x, y, target) { _X.g_set_rally(b.id, x, y, target ? target.id : 0); this.sync(); }
}

if (typeof module !== 'undefined' && module.exports) module.exports = { Game, gameReady };
