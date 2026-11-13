'use strict';

/* ==========================================================================
   Shared game data: unit / building / tech definitions and small helpers.
   This file is the single source of truth. The simulation lives in C
   (c/game.c); `node tools/gen_defs.js` turns these tables into c/defs.h so
   the C code and the UI always agree on stats.
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
const ATK_TYPES = ['melee', 'pierce', 'siege'];
const CLASSES = ['worker', 'infantry', 'spear', 'archer', 'cavalry', 'ship'];

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
for (const k in BUILDING_DEFS) BUILDING_DEFS[k].armor = { melee: 4, pierce: 12, siege: 0 };
for (const k in UNIT_DEFS) UNIT_DEFS[k].id = k;
for (const k in BUILDING_DEFS) BUILDING_DEFS[k].id = k;

// Upgrades. cost/time grow linearly with the level being researched.
function techDef(o) {
  o.cost = (lvl) => ({ food: o.base.food + o.step.food * lvl, gold: o.base.gold + o.step.gold * lvl });
  o.time = (lvl) => o.tbase + o.tstep * lvl;
  return o;
}
const TECHS = {
  weapons: techDef({ name: 'Forged Weapons', icon: '⚔️', max: 2, desc: '+2 attack for all soldiers (per level).',
    base: { food: 150, gold: 100 }, step: { food: 100, gold: 75 }, tbase: 40, tstep: 15 }),
  armor: techDef({ name: 'Plate Armor', icon: '🛡️', max: 2, desc: '+1 melee and pierce armor for all soldiers (per level).',
    base: { food: 150, gold: 100 }, step: { food: 100, gold: 75 }, tbase: 40, tstep: 15 }),
};

const DIFFS = {
  easy:   { gather: 0.8,  maxB: 16, wave: 10, grow: 3, pace: 1.4,  bonus: 0,   think: 1.0 },
  normal: { gather: 1.0,  maxB: 24, wave: 9,  grow: 4, pace: 1.0,  bonus: 100, think: 0.7 },
  hard:   { gather: 1.05, maxB: 28, wave: 8,  grow: 5, pace: 0.85, bonus: 150, think: 0.55 },
};

// Index orders shared with the C core (the order of the keys above).
const UNIT_KEYS = Object.keys(UNIT_DEFS);
const BUILDING_KEYS = Object.keys(BUILDING_DEFS);
const NODE_KEYS = Object.keys(NODE_DEFS);
const TECH_KEYS = Object.keys(TECHS);
const DIFF_KEYS = Object.keys(DIFFS);
/* Selectable maps. The order is the index passed to g_set_map in the C core. */
const MAPS = {
  lake:   { name: 'Central Lake',  desc: 'Classic. A big lake splits the middle; build Ports and fight on the water.' },
  plains: { name: 'Open Plains',   desc: 'No water, wide open ground and sparse groves. Wood is scarce, armies move fast.' },
  river:  { name: 'Twin Fords',    desc: 'A winding river with three fords. Hold the crossings or sail the river.' },
  ponds:  { name: 'Pond Country',  desc: 'Scattered ponds break up the land. Lots of room to expand, lots of detours.' },
  forest: { name: 'Deep Forest',   desc: 'Thick woods fill the map. Lumber is plentiful; only a narrow road links the bases.' },
};
const MAP_KEYS = Object.keys(MAPS);

/* -------------------------------- Helpers -------------------------------- */

const clamp = (v, a, b) => (v < a ? a : v > b ? b : v);
const dist = (ax, ay, bx, by) => Math.hypot(ax - bx, ay - by);
function canAfford(res, cost) {
  for (const k in cost) if ((res[k] || 0) < cost[k]) return false;
  return true;
}
function missingRes(res, cost) {
  for (const k in cost) if ((res[k] || 0) < cost[k]) return k;
  return null;
}
const centerOf = (e) => (e.kind === 'unit' ? { x: e.x, y: e.y } : { x: e.x + e.w / 2, y: e.y + e.h / 2 });

if (typeof module !== 'undefined' && module.exports) {
  module.exports = {
    MAP_W, MAP_H, TILE_W, TILE_H, MAX_POP, T_GRASS, T_WATER, T_SAND, FARM_RATE, FARM_SLOTS, TEAMS, RES_ICON, RES_LIST,
    ATK_TYPES, CLASSES, NODE_DEFS, UNIT_DEFS, BUILDING_DEFS, TECHS, DIFFS, MAPS, MAP_KEYS, UNIT_KEYS, BUILDING_KEYS, NODE_KEYS, TECH_KEYS, DIFF_KEYS,
    clamp, dist, canAfford, missingRes, centerOf,
  };
}
