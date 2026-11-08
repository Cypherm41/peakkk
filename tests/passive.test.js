'use strict';
// Player does nothing; checks that the AI economy grows, attacks and can win.
const G = require('../js/game.js');
const diff = process.argv[2] || 'normal';
const game = new G.Game({ seed: 777, difficulty: diff });
const dt = 0.1;
let first = null;
for (let i = 0; i < 40 * 60 / dt; i++) {
  game.update(dt); game.events.length = 0;
  if (!first && game.units.some((u) => u.team === 1 && u.def.role === 'soldier' && u.x < 40)) first = game.time;
  if (game.winner !== null) break;
}
const ys = game.units.filter((u) => u.team === 1);
console.log(`[${diff}] ended at ${(game.time / 60).toFixed(1)} min, winner=${game.winner === null ? 'none' : G.TEAMS[game.winner].name}, ` +
  `yellow units=${ys.length}, first push past midline at ${first ? (first / 60).toFixed(1) + ' min' : 'never'}`);
