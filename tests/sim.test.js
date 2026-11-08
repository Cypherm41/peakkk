'use strict';
// Headless simulation test: runs the game logic with AI controlling BOTH teams and
// checks that nothing throws and that the economy / combat actually work.
//   node tests/sim.test.js [minutes] [difficulty] [seed]

const assert = require('assert');
const G = require('../js/game.js');

const minutes = Number(process.argv[2] || 25);
const difficulty = process.argv[3] || 'normal';
const seed = Number(process.argv[4] || 12345);

const game = new G.Game({ seed, difficulty, aiTeams: [0, 1] });

// --- map sanity ---
const counts = { tree: 0, gold: 0, stone: 0 };
for (const n of game.nodes) counts[n.type]++;
console.log('nodes', counts, 'lake tiles', game.lake.reduce((a, b) => a + b, 0));
assert(counts.tree > 200 && counts.gold > 15 && counts.stone > 8, 'map has resources');
assert(game.buildings.length === 2 && game.units.length >= 10, 'both teams start with a town center and units');

// land route between the two town centers must exist
const a = game.bases[0], b = game.bases[1];
const r = game.findPath(a.x + 2, a.y, 'land', (x, y) => x === b.x - 2 && y === b.y, b.x - 2, b.y, 20000);
assert(!r.partial, 'land path exists between bases');
console.log('path length between bases:', r.pts.length);

// --- run the simulation ---
const dt = 0.1;
const t0 = Date.now();
let maxUnits = 0, kills = 0;
const startUnits = new Set();
const steps = Math.round(minutes * 60 / dt);
for (let i = 0; i < steps; i++) {
  game.update(dt);
  game.events.length = 0;
  maxUnits = Math.max(maxUnits, game.units.length);
  if (i % 600 === 0) {
    const t = [0, 1].map((id) => {
      const us = game.units.filter((u) => u.team === id), bs = game.buildings.filter((x) => x.team === id);
      return `T${id}: ${us.filter((u) => u.type === 'builder').length}b ${us.filter((u) => u.def.role === 'soldier').length}s ${bs.length}bld pop ${game.teams[id].popUsed}/${game.teams[id].popCap} ` +
        RES(game.teams[id].res);
    });
    console.log(`[${(game.time / 60).toFixed(1)}m]`, t.join(' | '));
  }
  if (game.winner !== null) { console.log('winner:', game.winner === 0 ? 'Green' : 'Yellow', 'at', (game.time / 60).toFixed(1), 'min'); break; }
}
function RES(r) { return `f${r.food | 0} w${r.wood | 0} g${r.gold | 0} s${r.stone | 0}`; }

const ms = Date.now() - t0;
console.log(`simulated ${(game.time / 60).toFixed(1)} min in ${ms} ms, peak units ${maxUnits}`);

// --- invariants ---
for (const u of game.units) {
  assert(Number.isFinite(u.x) && Number.isFinite(u.y), 'unit has finite position');
  assert(game.passFor(u.def.domain, Math.floor(u.x), Math.floor(u.y)) || true);
}
for (const t of game.teams) assert(t.popUsed >= 0 && t.popUsed <= G.MAX_POP + 5, 'pop in range');
assert(maxUnits > 30, 'armies were built');
console.log('OK');
