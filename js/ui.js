'use strict';

/* ==========================================================================
   BANNERS OF PEAKKK — isometric renderer, HUD and input
   (uses the globals defined in game.js)
   ========================================================================== */

const $ = (id) => document.getElementById(id);
const canvas = $('game'), ctx = canvas.getContext('2d');
const mmCanvas = $('minimap'), mctx = mmCanvas.getContext('2d');
const TOP_H = 40, BOTTOM_H = 168;
const MM_W = 260, MM_H = 130;
const MM_K = MM_W / ((MAP_W + MAP_H) * 32);
const HOTKEYS = 'QWERASDFZXCV';
const BUILD_SLOTS = ['house', 'farm', 'storehouse', 'barracks', 'archery', 'stable', 'blacksmith', 'port', 'tower', 'towncenter'];

let game = null;
let W = 0, H = 0, DPR = 1, VX = 0, VY = 0;
const cam = { x: 0, y: 0 };
let zoom = 1;
let sel = [];
let placing = null;       // { type, x, y, valid }
let cmdMode = null;       // 'attackmove'
let paused = false, speed = 1, lastT = 0, hudTimer = 0, idleIdx = 0;
let diffChoice = 'normal';
let curCmds = new Array(12).fill(null);
let cmdSig = '';
let miniTerrain = null;
let muted = false;
const groups = {};
const pings = [];
const clickFx = [];
const keys = {};
const mouse = { x: 0, y: 0, inside: false, down: false, mid: false, sx: 0, sy: 0, drag: false, lastClick: 0, lastType: null, onMini: false };

/* ------------------------------ Coordinates ------------------------------ */

const isoX = (x, y) => (x - y) * (TILE_W / 2);
const isoY = (x, y) => (x + y) * (TILE_H / 2);
function w2s(x, y) { return { x: (isoX(x, y) - cam.x) * zoom + VX, y: (isoY(x, y) - cam.y) * zoom + VY }; }
function s2w(sx, sy) {
  const a = ((sx - VX) / zoom + cam.x) / (TILE_W / 2), b = ((sy - VY) / zoom + cam.y) / (TILE_H / 2);
  return { x: (a + b) / 2, y: (b - a) / 2 };
}
function clampCam() {
  cam.x = clamp(cam.x, -MAP_H * 32, MAP_W * 32);
  cam.y = clamp(cam.y, 0, (MAP_W + MAP_H) * 16);
}
function centerCam(x, y) { cam.x = isoX(x, y); cam.y = isoY(x, y); clampCam(); }
function resize() {
  DPR = window.devicePixelRatio || 1;
  W = window.innerWidth; H = window.innerHeight;
  canvas.width = Math.round(W * DPR); canvas.height = Math.round(H * DPR);
  VX = W / 2; VY = TOP_H + (H - TOP_H - BOTTOM_H) / 2;
}

/* --------------------------------- Sound --------------------------------- */

let actx = null;
function tone(f, d, type, vol, when) {
  const t = actx.currentTime + (when || 0);
  const o = actx.createOscillator(), g = actx.createGain();
  o.type = type || 'square'; o.frequency.value = f;
  g.gain.setValueAtTime(vol || 0.05, t); g.gain.exponentialRampToValueAtTime(0.0001, t + d);
  o.connect(g); g.connect(actx.destination); o.start(t); o.stop(t + d + 0.02);
}
function sfx(kind) {
  if (muted) return;
  try {
    if (!actx) { const AC = window.AudioContext || window.webkitAudioContext; if (!AC) return; actx = new AC(); }
    if (actx.state === 'suspended') actx.resume();
    switch (kind) {
      case 'select': tone(620, 0.06, 'triangle', 0.05); break;
      case 'order': tone(420, 0.07, 'triangle', 0.05); tone(560, 0.07, 'triangle', 0.04, 0.05); break;
      case 'err': tone(150, 0.18, 'sawtooth', 0.05); break;
      case 'build': tone(520, 0.12, 'triangle', 0.06); tone(660, 0.12, 'triangle', 0.06, 0.1); tone(780, 0.2, 'triangle', 0.06, 0.2); break;
      case 'alert': tone(330, 0.2, 'sawtooth', 0.06); tone(250, 0.3, 'sawtooth', 0.06, 0.2); break;
      case 'place': tone(240, 0.1, 'square', 0.05); break;
    }
  } catch (e) { /* audio is optional */ }
}

/* ------------------------------ Small helpers ---------------------------- */

const shadeCache = {};
function shade(hex, f) {
  const k = hex + f;
  if (shadeCache[k]) return shadeCache[k];
  const r = Math.min(255, Math.round(parseInt(hex.slice(1, 3), 16) * f));
  const g = Math.min(255, Math.round(parseInt(hex.slice(3, 5), 16) * f));
  const b = Math.min(255, Math.round(parseInt(hex.slice(5, 7), 16) * f));
  return (shadeCache[k] = 'rgb(' + r + ',' + g + ',' + b + ')');
}
function toast(msg, cls) {
  const el = document.createElement('div');
  el.className = 'toast' + (cls ? ' ' + cls : '');
  el.textContent = msg;
  const box = $('toasts');
  box.appendChild(el);
  while (box.children.length > 4) box.removeChild(box.firstChild);
  setTimeout(() => { if (el.parentNode) el.parentNode.removeChild(el); }, 3700);
}
function pointInPoly(px, py, pts) {
  let inside = false;
  for (let i = 0, j = pts.length - 1; i < pts.length; j = i++) {
    const xi = pts[i][0], yi = pts[i][1], xj = pts[j][0], yj = pts[j][1];
    if ((yi > py) !== (yj > py) && px < (xj - xi) * (py - yi) / (yj - yi) + xi) inside = !inside;
  }
  return inside;
}
const costHtml = (cost, res) => Object.keys(cost).map((k) => `<i class="${!res || res[k] >= cost[k] ? '' : 'no'}">${RES_ICON[k]}${cost[k]}</i>`).join(' ');
const mine = () => sel.filter((e) => e.kind === 'unit' && e.team === 0 && !e.dead);
const isWorker = (u) => u.def.role === 'worker';

/* ------------------------------- Terrain draw ---------------------------- */

const GRASS = ['#4f8b3b', '#4b8637', '#53913e', '#487f38', '#579541', '#4d8939'];
const SAND = ['#cdb878', '#c8b270', '#d2bd80', '#c9b474', '#d0ba7c', '#cbb676'];

function drawTerrain(vis, now) {
  const g = game;
  for (let y = vis.y0; y <= vis.y1; y++) {
    for (let x = vis.x0; x <= vis.x1; x++) {
      const i = y * MAP_W + x, t = g.terrain[i];
      const cx = isoX(x, y), cy = isoY(x, y);
      if (cx < vis.l - 40 || cx > vis.r + 40 || cy < vis.t - 20 || cy > vis.b + 20) continue;
      ctx.fillStyle = t === T_WATER ? ((x + y) & 1 ? '#2b69a1' : '#2d6fa9') : t === T_SAND ? SAND[g.shade[i]] : GRASS[g.shade[i]];
      ctx.beginPath();
      ctx.moveTo(cx, cy - 0.6); ctx.lineTo(cx + 32.8, cy + 16); ctx.lineTo(cx, cy + 32.6); ctx.lineTo(cx - 32.8, cy + 16);
      ctx.closePath(); ctx.fill();
      if (t === T_WATER && ((x * 13 + y * 7) % 5 === 0)) {
        const s = Math.sin(now * 1.6 + x * 0.9 + y * 0.7);
        if (s > 0.1) {
          ctx.strokeStyle = 'rgba(255,255,255,' + (0.18 * s).toFixed(2) + ')'; ctx.lineWidth = 1.5;
          ctx.beginPath(); ctx.moveTo(cx - 10 + s * 4, cy + 14); ctx.lineTo(cx + 6 + s * 4, cy + 14); ctx.stroke();
        }
      } else if (t === T_GRASS && ((x * 31 + y * 17) % 9 === 0)) {
        ctx.fillStyle = 'rgba(30,70,20,.35)';
        ctx.fillRect(cx - 4, cy + 14, 2, 3); ctx.fillRect(cx + 5, cy + 18, 2, 3);
      }
    }
  }
  // map edge (south-west and south-east cliffs)
  ctx.fillStyle = '#4a3a22';
  ctx.beginPath(); ctx.moveTo(isoX(0, MAP_H), isoY(0, MAP_H)); ctx.lineTo(isoX(MAP_W, MAP_H), isoY(MAP_W, MAP_H));
  ctx.lineTo(isoX(MAP_W, MAP_H), isoY(MAP_W, MAP_H) + 26); ctx.lineTo(isoX(0, MAP_H), isoY(0, MAP_H) + 26); ctx.closePath(); ctx.fill();
  ctx.fillStyle = '#392c19';
  ctx.beginPath(); ctx.moveTo(isoX(MAP_W, 0), isoY(MAP_W, 0)); ctx.lineTo(isoX(MAP_W, MAP_H), isoY(MAP_W, MAP_H));
  ctx.lineTo(isoX(MAP_W, MAP_H), isoY(MAP_W, MAP_H) + 26); ctx.lineTo(isoX(MAP_W, 0), isoY(MAP_W, 0) + 26); ctx.closePath(); ctx.fill();
}

/* --------------------------------- Nodes --------------------------------- */

function drawTree(n) {
  const cx = isoX(n.x + 0.5, n.y + 0.5), cy = isoY(n.x + 0.5, n.y + 0.5) + 5;
  const s = 0.85 + ((n.id * 7) % 5) / 16;
  ctx.fillStyle = 'rgba(0,0,0,.22)'; ctx.beginPath(); ctx.ellipse(cx + 2, cy, 11 * s, 4.5 * s, 0, 0, 6.3); ctx.fill();
  ctx.fillStyle = '#5b3b1d'; ctx.fillRect(cx - 2.5, cy - 9 * s, 5, 10 * s);
  const cols = [['#2f6b2c', '#3f8a38'], ['#2a6128', '#388031'], ['#33732f', '#459440']][n.id % 3];
  for (let i = 0; i < 3; i++) {
    const yb = cy - (6 + i * 9) * s, w = (16 - i * 4) * s, hh = 14 * s;
    ctx.fillStyle = cols[0];
    ctx.beginPath(); ctx.moveTo(cx - w, yb); ctx.lineTo(cx + w, yb); ctx.lineTo(cx, yb - hh); ctx.closePath(); ctx.fill();
    ctx.fillStyle = cols[1];
    ctx.beginPath(); ctx.moveTo(cx, yb - hh); ctx.lineTo(cx + w, yb); ctx.lineTo(cx + w * 0.1, yb); ctx.closePath(); ctx.fill();
  }
}
function drawRocks(n) {
  const cx = isoX(n.x + 0.5, n.y + 0.5), cy = isoY(n.x + 0.5, n.y + 0.5) + 6;
  const f = 0.55 + 0.45 * (n.amt / n.max);
  const gold = n.type === 'gold';
  ctx.fillStyle = 'rgba(0,0,0,.25)'; ctx.beginPath(); ctx.ellipse(cx, cy, 15 * f, 6 * f, 0, 0, 6.3); ctx.fill();
  const rocks = [[-8, -2, 9, 11, 0], [5, -1, 8, 9, 1], [-1, -8, 10, 12, 2]];
  for (const [dx, dy, w, h, k] of rocks) {
    const bx = cx + dx * f, by = cy + dy * f * 0.6, ww = w * f, hh = h * f;
    ctx.fillStyle = gold ? ['#6e6a66', '#7b7771', '#645f5b'][k] : ['#8e8e97', '#a3a3ac', '#7c7c85'][k];
    ctx.beginPath(); ctx.moveTo(bx - ww, by); ctx.lineTo(bx - ww * 0.5, by - hh); ctx.lineTo(bx + ww * 0.4, by - hh * 0.9); ctx.lineTo(bx + ww, by); ctx.closePath(); ctx.fill();
    ctx.fillStyle = gold ? 'rgba(255,255,255,.08)' : 'rgba(255,255,255,.18)';
    ctx.beginPath(); ctx.moveTo(bx - ww * 0.5, by - hh); ctx.lineTo(bx + ww * 0.4, by - hh * 0.9); ctx.lineTo(bx, by - hh * 0.4); ctx.closePath(); ctx.fill();
    if (gold) {
      ctx.fillStyle = '#ffd23a';
      ctx.beginPath(); ctx.moveTo(bx - 3, by - hh * 0.5); ctx.lineTo(bx, by - hh * 0.75); ctx.lineTo(bx + 3, by - hh * 0.45); ctx.lineTo(bx, by - hh * 0.3); ctx.closePath(); ctx.fill();
      ctx.fillStyle = '#fff3a0'; ctx.fillRect(bx - 1, by - hh * 0.7, 2, 2);
    }
  }
}

/* ------------------------------- Buildings ------------------------------- */

const P = (x, y, z) => [isoX(x, y), isoY(x, y) - (z || 0)];
function poly(pts, fill, stroke) {
  ctx.beginPath(); ctx.moveTo(pts[0][0], pts[0][1]);
  for (let i = 1; i < pts.length; i++) ctx.lineTo(pts[i][0], pts[i][1]);
  ctx.closePath();
  if (fill) { ctx.fillStyle = fill; ctx.fill(); }
  if (stroke) { ctx.strokeStyle = stroke; ctx.lineWidth = 1; ctx.stroke(); }
}
function box(x, y, w, h, z0, z1, wall, topCol) {
  poly([P(x, y + h, z0), P(x + w, y + h, z0), P(x + w, y + h, z1), P(x, y + h, z1)], shade(wall, 0.84), 'rgba(0,0,0,.25)');
  poly([P(x + w, y + h, z0), P(x + w, y, z0), P(x + w, y, z1), P(x + w, y + h, z1)], shade(wall, 0.6), 'rgba(0,0,0,.25)');
  poly([P(x, y, z1), P(x + w, y, z1), P(x + w, y + h, z1), P(x, y + h, z1)], topCol || shade(wall, 1.08), 'rgba(0,0,0,.2)');
}
function pyramid(x, y, w, h, z, hh, color) {
  const a = P(x + w / 2, y + h / 2, z + hh);
  const N = P(x, y, z), E = P(x + w, y, z), S = P(x + w, y + h, z), Wp = P(x, y + h, z);
  poly([N, E, a], shade(color, 0.7)); poly([N, Wp, a], shade(color, 0.78));
  poly([Wp, S, a], shade(color, 0.95), 'rgba(0,0,0,.25)'); poly([S, E, a], shade(color, 0.72), 'rgba(0,0,0,.25)');
  return a;
}
function flag(x, y, z, T, len) {
  const p = P(x, y, z), t = performance.now() / 300;
  ctx.strokeStyle = '#3a2a18'; ctx.lineWidth = 1.6; ctx.beginPath(); ctx.moveTo(p[0], p[1]); ctx.lineTo(p[0], p[1] - len); ctx.stroke();
  const w = 11 + Math.sin(t + x) * 1.5;
  ctx.fillStyle = T.color; ctx.strokeStyle = T.dark; ctx.lineWidth = 1;
  ctx.beginPath(); ctx.moveTo(p[0], p[1] - len); ctx.lineTo(p[0] + w, p[1] - len + 3 + Math.sin(t) * 1); ctx.lineTo(p[0], p[1] - len + 8); ctx.closePath(); ctx.fill(); ctx.stroke();
}
function door(x, y, w, h, hh) {
  const cx = x + w / 2;
  poly([P(cx - 0.35, y + h, 0), P(cx + 0.35, y + h, 0), P(cx + 0.35, y + h, hh), P(cx - 0.35, y + h, hh)], '#2a1a0c', 'rgba(0,0,0,.5)');
}
function windowAt(x, y, w, h, fx, z) {
  poly([P(fx - 0.14, y + h, z), P(fx + 0.14, y + h, z), P(fx + 0.14, y + h, z + 7), P(fx - 0.14, y + h, z + 7)], '#f3da8a', 'rgba(0,0,0,.4)');
}

function drawBuilding(b, ghostAlpha) {
  const T = TEAMS[b.team], d = b.def, x = b.x, y = b.y, w = b.w, h = b.h;
  const prog = b.done ? 1 : Math.max(0.14, b.progress);
  const Hh = (d.height || 20) * prog;
  ctx.save();
  if (ghostAlpha) ctx.globalAlpha = ghostAlpha; else if (!b.done) ctx.globalAlpha = 0.6 + 0.4 * prog;
  poly([P(x - 0.1, y - 0.1, 0), P(x + w + 0.1, y - 0.1, 0), P(x + w + 0.1, y + h + 0.1, 0), P(x - 0.1, y + h + 0.1, 0)], 'rgba(40,28,12,.35)');
  const roofOn = b.done || prog > 0.7;
  switch (b.type) {
    case 'farm': {
      poly([P(x, y, 0), P(x + w, y, 0), P(x + w, y + h, 0), P(x, y + h, 0)], '#6b4a26', 'rgba(0,0,0,.3)');
      for (let i = 0; i < 5; i++) {
        const yy = y + (i + 0.5) * h / 5, a = P(x + 0.12, yy, 0), c = P(x + w - 0.12, yy, 0);
        ctx.strokeStyle = b.done ? (i % 2 ? '#d9b834' : '#7db33a') : '#5a3d1c'; ctx.lineWidth = 3.2;
        ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(c[0], c[1]); ctx.stroke();
      }
      if (b.done && b.users.size) {
        const p = P(x + w - 0.2, y + 0.2, 0); ctx.fillStyle = '#e8d6a0'; ctx.fillRect(p[0] - 1, p[1] - 12, 2, 12);
        ctx.fillRect(p[0] - 5, p[1] - 10, 10, 2);
      }
      break;
    }
    case 'towncenter': {
      const h0 = Hh * 0.6;
      box(x, y, w, h, 0, h0, '#c7b68f');
      door(x, y, w, h, h0 * 0.7);
      for (const fx of [x + 0.6, x + w - 0.6]) windowAt(x, y, w, h, fx, h0 * 0.45);
      // crenellations
      for (let i = 0; i < 3; i++) {
        const t = (i + 0.5) / 3, p = P(x + w * t, y + h, h0);
        ctx.fillStyle = '#b3a27d'; ctx.fillRect(p[0] - 3, p[1] - 5, 6, 5);
      }
      const kx = x + w / 2 - 0.85, ky = y + h / 2 - 0.85;
      box(kx, ky, 1.7, 1.7, h0, Hh, '#d3c39d');
      if (roofOn) { const a = pyramid(kx - 0.12, ky - 0.12, 1.94, 1.94, Hh, 20, T.color); flag(kx + 0.85, ky + 0.85, Hh + 20, T, 16); void a; }
      // banners
      for (const bx of [x + 0.25, x + w - 0.25]) {
        poly([P(bx - 0.12, y + h, h0 * 0.85), P(bx + 0.12, y + h, h0 * 0.85), P(bx + 0.12, y + h, h0 * 0.35), P(bx, y + h, h0 * 0.25), P(bx - 0.12, y + h, h0 * 0.35)], T.color, T.dark);
      }
      break;
    }
    case 'house': {
      box(x, y, w, h, 0, Hh, '#e9dcb6');
      door(x, y, w, h, Hh * 0.6);
      windowAt(x, y, w, h, x + 0.3, Hh * 0.45);
      if (roofOn) pyramid(x - 0.15, y - 0.15, w + 0.3, h + 0.3, Hh, 15, T.color);
      break;
    }
    case 'storehouse': {
      box(x, y, w, h, 0, Hh, '#a88b5e');
      door(x, y, w, h, Hh * 0.7);
      if (roofOn) pyramid(x - 0.12, y - 0.12, w + 0.24, h + 0.24, Hh, 11, T.dark);
      const p = P(x + w + 0.15, y + h - 0.2, 0);
      ctx.fillStyle = '#c9b27a'; ctx.beginPath(); ctx.ellipse(p[0], p[1] - 4, 4, 5, 0, 0, 6.3); ctx.fill();
      ctx.fillStyle = T.color; ctx.fillRect(p[0] - 1.5, p[1] - 9, 3, 2);
      break;
    }
    case 'barracks': case 'archery': case 'stable': case 'blacksmith': {
      const wallCol = { barracks: '#9b7650', archery: '#a98557', stable: '#8c6a45', blacksmith: '#85807a' }[b.type];
      box(x, y, w, h, 0, Hh, wallCol);
      door(x, y, w, h, Hh * 0.62);
      if (roofOn) {
        pyramid(x - 0.2, y - 0.2, w + 0.4, h + 0.4, Hh, 15, T.color);
        flag(x + w * 0.5, y + h * 0.5, Hh + 15, T, 14);
      }
      // emblem on the left face
      const ex = x + w * 0.72, ez = Hh * 0.55, p = P(ex, y + h, ez);
      ctx.save(); ctx.translate(p[0], p[1]); ctx.lineWidth = 2;
      if (b.type === 'barracks') {
        ctx.strokeStyle = '#e6edf2'; ctx.beginPath(); ctx.moveTo(-6, -6); ctx.lineTo(6, 6); ctx.moveTo(6, -6); ctx.lineTo(-6, 6); ctx.stroke();
        ctx.strokeStyle = '#b08a3a'; ctx.beginPath(); ctx.moveTo(-3, 3); ctx.lineTo(3, 3); ctx.stroke();
      } else if (b.type === 'archery') {
        for (const [r, c] of [[7, '#f2f2f2'], [5, '#c93a2a'], [3, '#f2f2f2'], [1.4, '#c93a2a']]) { ctx.fillStyle = c; ctx.beginPath(); ctx.ellipse(0, 0, r, r * 0.8, 0, 0, 6.3); ctx.fill(); }
      } else if (b.type === 'stable') {
        ctx.strokeStyle = '#d9d9d9'; ctx.lineWidth = 2.4; ctx.beginPath(); ctx.arc(0, 0, 5, 0.2, Math.PI - 0.2, true); ctx.stroke();
      } else {
        ctx.fillStyle = '#2b2b2f'; ctx.fillRect(-6, -2, 12, 3); ctx.fillRect(-3, 1, 6, 4); ctx.fillRect(-7, -3, 4, 2);
        const q = P(x + 0.6, y + 0.6, Hh + 14);
        ctx.restore(); ctx.save();
        ctx.fillStyle = '#585860'; ctx.fillRect(q[0] - 2, q[1] - 2, 4, 10);
        const t = performance.now() / 400;
        ctx.fillStyle = 'rgba(200,200,200,.35)'; ctx.beginPath(); ctx.arc(q[0] + Math.sin(t) * 3, q[1] - 8 - (t % 1) * 6, 4, 0, 6.3); ctx.fill();
        ctx.beginPath(); ctx.arc(q[0] + Math.sin(t + 2) * 4, q[1] - 16 - (t % 1) * 6, 5, 0, 6.3); ctx.fill();
      }
      ctx.restore();
      break;
    }
    case 'port': {
      box(x, y, w, h, 0, 6 * prog + 2, '#8a6a45', '#a98a5a');
      for (let i = 1; i < 6; i++) {
        const a = P(x + w * i / 6, y, 6 * prog + 2), c = P(x + w * i / 6, y + h, 6 * prog + 2);
        ctx.strokeStyle = 'rgba(0,0,0,.25)'; ctx.lineWidth = 1; ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(c[0], c[1]); ctx.stroke();
      }
      const hx = x + 0.3, hy = y + 0.3;
      box(hx, hy, 1.4, 1.4, 6, 6 + Hh * 0.6, '#b49a6c');
      if (roofOn) pyramid(hx - 0.1, hy - 0.1, 1.6, 1.6, 6 + Hh * 0.6, 10, T.color);
      const mp = P(x + w - 0.6, y + h - 0.6, 6);
      ctx.strokeStyle = '#4a3320'; ctx.lineWidth = 3; ctx.beginPath(); ctx.moveTo(mp[0], mp[1]); ctx.lineTo(mp[0], mp[1] - 34 * prog); ctx.stroke();
      if (roofOn) {
        ctx.fillStyle = T.color; ctx.strokeStyle = T.dark;
        ctx.beginPath(); ctx.moveTo(mp[0], mp[1] - 32); ctx.lineTo(mp[0] + 16, mp[1] - 20); ctx.lineTo(mp[0], mp[1] - 10); ctx.closePath(); ctx.fill(); ctx.stroke();
      }
      for (const [cx2, cy2] of [[x + w - 0.5, y + 0.5], [x + w - 0.9, y + 0.9]]) { const p = P(cx2, cy2, 8); ctx.fillStyle = '#9b7a46'; ctx.fillRect(p[0] - 4, p[1] - 6, 8, 8); ctx.strokeStyle = '#5a4020'; ctx.strokeRect(p[0] - 4, p[1] - 6, 8, 8); }
      break;
    }
    case 'tower': {
      const tx = x + 0.2, ty = y + 0.2, tw = w - 0.4, th = h - 0.4;
      box(tx, ty, tw, th, 0, Hh, '#b3a98f');
      poly([P(tx + tw * 0.5 - 0.1, ty + th, Hh * 0.55), P(tx + tw * 0.5 + 0.1, ty + th, Hh * 0.55), P(tx + tw * 0.5 + 0.1, ty + th, Hh * 0.75), P(tx + tw * 0.5 - 0.1, ty + th, Hh * 0.75)], '#1d1308');
      door(tx, ty, tw, th, Hh * 0.28);
      if (b.done) {
        box(x + 0.05, y + 0.05, w - 0.1, h - 0.1, Hh, Hh + 6, '#a89d82');
        for (let i = 0; i < 4; i++) { const p = P(x + 0.1 + (w - 0.2) * (i / 3), y + h - 0.05, Hh + 6); ctx.fillStyle = '#948a70'; ctx.fillRect(p[0] - 2, p[1] - 4, 4, 4); }
        flag(x + w / 2, y + h / 2, Hh + 6, T, 18);
      }
      break;
    }
  }
  if (!b.done) {
    ctx.strokeStyle = '#7a5a2c'; ctx.lineWidth = 1.6;
    const top = (d.height || 20) + 8;
    for (const [px, py] of [[x, y + h], [x + w, y + h], [x + w, y]]) {
      const a = P(px, py, 0), c = P(px, py, top * (0.5 + 0.5 * prog));
      ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(c[0], c[1]); ctx.stroke();
    }
    const a = P(x, y + h, top * 0.4), c = P(x + w, y + h, top * 0.4);
    ctx.beginPath(); ctx.moveTo(a[0], a[1]); ctx.lineTo(c[0], c[1]); ctx.stroke();
  }
  if (b.done && b.hp < b.maxHp * 0.5 && b.type !== 'farm') {
    const t = performance.now() / 120;
    for (let i = 0; i < 3; i++) {
      const p = P(x + w * (0.3 + 0.2 * i), y + h * (0.4 + 0.15 * i), Hh * 0.9);
      const f = 6 + Math.sin(t + i * 2) * 2;
      ctx.fillStyle = '#ff7a1a'; ctx.beginPath(); ctx.ellipse(p[0], p[1] - f * 0.4, 4, f, 0, 0, 6.3); ctx.fill();
      ctx.fillStyle = '#ffd24a'; ctx.beginPath(); ctx.ellipse(p[0], p[1] - f * 0.2, 2, f * 0.55, 0, 0, 6.3); ctx.fill();
    }
  }
  ctx.restore();
}

/* --------------------------------- Units --------------------------------- */

function legs(t, mv, col) {
  const l = mv ? Math.sin(t * 9) * 3 : 0;
  ctx.strokeStyle = col || '#3b2a1a'; ctx.lineWidth = 2.6; ctx.lineCap = 'round';
  ctx.beginPath(); ctx.moveTo(-2, -6); ctx.lineTo(-2 + l, 0); ctx.moveTo(2, -6); ctx.lineTo(2 - l, 0); ctx.stroke();
}
function head(y, helmet) {
  ctx.fillStyle = '#e9c39b'; ctx.beginPath(); ctx.arc(0, y, 3.4, 0, 6.3); ctx.fill();
  if (helmet) { ctx.fillStyle = '#9aa0a8'; ctx.beginPath(); ctx.arc(0, y - 0.4, 3.9, Math.PI, 6.283); ctx.fill(); ctx.fillRect(-3.9, y - 0.6, 7.8, 1.3); }
}
function line(x0, y0, x1, y1, col, w) {
  ctx.strokeStyle = col; ctx.lineWidth = w; ctx.beginPath(); ctx.moveTo(x0, y0); ctx.lineTo(x1, y1); ctx.stroke();
}

function drawUnit(u, selected) {
  const x = isoX(u.x, u.y), y = isoY(u.x, u.y);
  const T = TEAMS[u.team], big = !!u.def.big, t = u.anim, mv = u.moving;
  const bob = mv ? Math.abs(Math.sin(t * 9)) * 2 : 0;
  const sw = u.atkAnim > 0 ? Math.sin((1 - u.atkAnim / 0.28) * Math.PI) : 0;
  ctx.save(); ctx.translate(x, y);
  if (u.type === 'warship') ctx.translate(0, Math.sin(t * 2) * 1.5);
  ctx.fillStyle = u.def.domain === 'water' ? 'rgba(0,30,60,.3)' : 'rgba(0,0,0,.3)';
  ctx.beginPath(); ctx.ellipse(0, 1, big ? 13 : 8, big ? 5.5 : 4, 0, 0, 6.3); ctx.fill();
  if (selected) { ctx.strokeStyle = T.light; ctx.lineWidth = 2; ctx.beginPath(); ctx.ellipse(0, 1, big ? 17 : 11, big ? 8 : 5.5, 0, 0, 6.3); ctx.stroke(); }
  ctx.scale(u.face, 1);
  switch (u.type) {
    case 'builder': {
      legs(t, mv);
      ctx.fillStyle = T.color; ctx.fillRect(-4.5, -15 - bob, 9, 10);
      ctx.fillStyle = 'rgba(0,0,0,.3)'; ctx.fillRect(-4.5, -9 - bob, 9, 1.3);
      head(-18.5 - bob, false);
      ctx.fillStyle = '#9b7a3e'; ctx.beginPath(); ctx.ellipse(0, -21 - bob, 6, 1.7, 0, 0, 6.3); ctx.fill();
      ctx.beginPath(); ctx.arc(0, -21.3 - bob, 3.2, Math.PI, 6.283); ctx.fill();
      if (u.carry >= 1) {
        ctx.fillStyle = { food: '#d9c24a', wood: '#7a4f25', gold: '#ffd23a', stone: '#9a9aa2' }[u.carryType] || '#aaa';
        ctx.fillRect(-9, -15 - bob, 5, 6); ctx.strokeStyle = 'rgba(0,0,0,.4)'; ctx.strokeRect(-9, -15 - bob, 5, 6);
      }
      ctx.save(); ctx.translate(4, -11 - bob);
      ctx.rotate(u.working ? Math.sin(t * 10) * 0.9 - 0.6 : -0.3 + (mv ? Math.sin(t * 9) * 0.2 : 0));
      line(0, 0, 0, -11, '#7a5a30', 1.8);
      const tool = u.state === 'build' ? 'hammer' : u.gatherKind === 'wood' ? 'axe' : (u.gatherKind === 'gold' || u.gatherKind === 'stone') ? 'pick' : u.gatherKind === 'food' ? 'hoe' : 'hammer';
      ctx.fillStyle = '#b8bec4';
      if (tool === 'axe') ctx.fillRect(0, -12, 5, 5);
      else if (tool === 'pick') { ctx.fillRect(-5, -12, 10, 2); }
      else if (tool === 'hoe') { ctx.fillRect(0, -12, 4, 2); ctx.fillRect(3, -12, 2, 4); }
      else { ctx.fillRect(-3, -13, 7, 4); }
      ctx.restore();
      break;
    }
    case 'swordsman': case 'spearman': {
      legs(t, mv);
      ctx.fillStyle = T.color; ctx.fillRect(-4.8, -15 - bob, 9.6, 10);
      ctx.fillStyle = 'rgba(255,255,255,.2)'; ctx.fillRect(-4.8, -15 - bob, 9.6, 2);
      head(-18.5 - bob, true);
      ctx.fillStyle = T.color; ctx.strokeStyle = '#e8e0c0'; ctx.lineWidth = 1.3;
      ctx.beginPath(); ctx.ellipse(-5.5, -10 - bob, 3.2, 5, 0, 0, 6.3); ctx.fill(); ctx.stroke();
      if (u.type === 'swordsman') {
        ctx.save(); ctx.translate(5, -10 - bob); ctx.rotate(-0.8 + sw * 1.9);
        line(0, 0, 0, -12, '#e4eaee', 2); line(-2.5, -2, 2.5, -2, '#b08a3a', 1.6); ctx.restore();
      } else {
        const th = sw * 7;
        line(4 + th * 0.3, -3, 8 + th * 0.4, -27 + th, '#8a6a3a', 1.8);
        ctx.fillStyle = '#e4eaee'; ctx.beginPath(); ctx.moveTo(8 + th * 0.4, -27 + th); ctx.lineTo(10 + th * 0.4, -31 + th); ctx.lineTo(6 + th * 0.4, -31 + th); ctx.closePath();
        ctx.moveTo(7.2 + th * 0.4, -29 + th); ctx.fill();
      }
      break;
    }
    case 'archer': {
      legs(t, mv, '#2f3a22');
      ctx.fillStyle = T.color; ctx.fillRect(-4.2, -15 - bob, 8.4, 10);
      ctx.fillStyle = '#6b4a2a'; ctx.fillRect(-7, -16 - bob, 3.5, 9);
      head(-18.5 - bob, false);
      ctx.fillStyle = T.dark; ctx.beginPath(); ctx.moveTo(-4.4, -17 - bob); ctx.lineTo(0, -25 - bob); ctx.lineTo(4.4, -17 - bob); ctx.closePath(); ctx.fill();
      ctx.strokeStyle = '#6b4a2a'; ctx.lineWidth = 2; ctx.beginPath(); ctx.arc(5, -11 - bob, 7, -1.25, 1.25); ctx.stroke();
      const pull = sw > 0 ? 2.5 : 0;
      line(5 + Math.cos(-1.25) * 7, -11 - bob + Math.sin(-1.25) * 7, 5 - pull, -11 - bob, '#e8e0c0', 0.8);
      line(5 + Math.cos(1.25) * 7, -11 - bob + Math.sin(1.25) * 7, 5 - pull, -11 - bob, '#e8e0c0', 0.8);
      if (u.cd > 0.8 || sw > 0) line(5 - pull, -11 - bob, 13, -11 - bob, '#cbb27a', 1.2);
      break;
    }
    case 'cavalry': {
      const gal = mv ? Math.sin(t * 10) : 0;
      ctx.strokeStyle = '#3b2412'; ctx.lineWidth = 2.6; ctx.lineCap = 'round';
      for (const [lx, ph] of [[-8, 0], [-5, 1.6], [6, 0.8], [9, 2.4]]) {
        const sv = mv ? Math.sin(t * 10 + ph) * 4 : 0;
        ctx.beginPath(); ctx.moveTo(lx, -8); ctx.lineTo(lx + sv, 0); ctx.stroke();
      }
      ctx.fillStyle = '#7a5230'; ctx.beginPath(); ctx.ellipse(0, -11 - Math.abs(gal), 12, 5.5, 0, 0, 6.3); ctx.fill();
      ctx.fillStyle = T.color; ctx.beginPath(); ctx.ellipse(-1, -13 - Math.abs(gal), 8, 4, 0, 0, 6.3); ctx.fill();
      ctx.fillStyle = '#7a5230'; ctx.beginPath(); ctx.moveTo(8, -14); ctx.lineTo(13, -23); ctx.lineTo(17, -21); ctx.lineTo(13, -9); ctx.closePath(); ctx.fill();
      ctx.beginPath(); ctx.ellipse(16, -21, 4.5, 2.6, 0.5, 0, 6.3); ctx.fill();
      ctx.strokeStyle = '#2c1a0b'; ctx.lineWidth = 2; ctx.beginPath(); ctx.moveTo(-11, -13); ctx.quadraticCurveTo(-16, -11, -14, -5); ctx.stroke();
      const ry = -22 - Math.abs(gal);
      ctx.fillStyle = T.color; ctx.fillRect(-3.5, ry - 1, 7, 9);
      ctx.save(); ctx.translate(0, ry + 1); head(-5, true); ctx.restore();
      const th = sw * 8;
      line(3, ry + 4, 22 + th, ry - 2 + th * 0.1, '#8a6a3a', 1.9);
      ctx.fillStyle = '#e4eaee'; ctx.beginPath(); ctx.moveTo(22 + th, ry - 2 + th * 0.1); ctx.lineTo(27 + th, ry - 1.5 + th * 0.1); ctx.lineTo(22 + th, ry - 4 + th * 0.1); ctx.fill();
      break;
    }
    case 'warship': {
      if (mv) { ctx.fillStyle = 'rgba(255,255,255,.35)'; ctx.beginPath(); ctx.ellipse(-20, 4, 9, 2.5, 0, 0, 6.3); ctx.fill(); ctx.beginPath(); ctx.ellipse(-30, 5, 6, 1.8, 0, 0, 6.3); ctx.fill(); }
      if (mv) { ctx.strokeStyle = '#4a3320'; ctx.lineWidth = 1.5; const o = Math.sin(t * 6) * 4; for (const ox of [-8, 0, 8]) { ctx.beginPath(); ctx.moveTo(ox, -1); ctx.lineTo(ox + o, 8); ctx.stroke(); } }
      ctx.fillStyle = '#6b4526'; ctx.strokeStyle = '#2f1d0e'; ctx.lineWidth = 1.2;
      ctx.beginPath(); ctx.moveTo(-19, -4); ctx.lineTo(21, -4); ctx.lineTo(14, 6); ctx.lineTo(-13, 6); ctx.closePath(); ctx.fill(); ctx.stroke();
      ctx.fillStyle = T.color; ctx.fillRect(-18, -4, 38, 2.4);
      ctx.fillStyle = '#8a5e34'; ctx.fillRect(-14, -7, 28, 3);
      line(0, -6, 0, -36, '#4a3320', 2.6);
      ctx.fillStyle = T.color; ctx.strokeStyle = T.dark; ctx.lineWidth = 1;
      ctx.beginPath(); ctx.moveTo(1.5, -34); ctx.lineTo(1.5, -10); ctx.quadraticCurveTo(16, -14, 15, -22); ctx.quadraticCurveTo(12, -29, 1.5, -34); ctx.fill(); ctx.stroke();
      ctx.fillStyle = T.light; ctx.beginPath(); ctx.moveTo(0, -36); ctx.lineTo(9, -34); ctx.lineTo(0, -31); ctx.fill();
      if (sw > 0) { ctx.fillStyle = '#ffb02a'; ctx.beginPath(); ctx.arc(23, -9, 5 * sw + 1, 0, 6.3); ctx.fill(); }
      break;
    }
  }
  ctx.restore();
}

/* ------------------------ Overlays: bars, projectiles -------------------- */

function drawBar(cx, cy, w, frac, kind) {
  frac = clamp(frac, 0, 1);
  ctx.fillStyle = 'rgba(0,0,0,.75)'; ctx.fillRect(cx - w / 2 - 1, cy - 1, w + 2, 5);
  ctx.fillStyle = kind === 'prog' ? '#4aa0e0' : frac > 0.6 ? '#5fcf6b' : frac > 0.3 ? '#e2c23a' : '#d8493b';
  ctx.fillRect(cx - w / 2, cy, w * frac, 3);
}

function drawProjectile(p) {
  const c = centerOf(p.target), t = p.t / p.dur;
  const ex = c.x, ey = c.y;
  const at = (tt) => {
    const x = p.sx + (ex - p.sx) * tt, y = p.sy + (ey - p.sy) * tt;
    const dd = Math.hypot(ex - p.sx, ey - p.sy);
    const z = 10 + Math.sin(tt * Math.PI) * dd * (p.kind === 'stone' ? 7 : 3.5);
    return [isoX(x, y), isoY(x, y) - z];
  };
  const a = at(t), b = at(Math.max(0, t - 0.07));
  if (p.kind === 'stone') {
    ctx.fillStyle = '#2b2b2b'; ctx.beginPath(); ctx.arc(a[0], a[1], 3.6, 0, 6.3); ctx.fill();
    ctx.fillStyle = 'rgba(255,255,255,.25)'; ctx.beginPath(); ctx.arc(a[0] - 1, a[1] - 1, 1.2, 0, 6.3); ctx.fill();
  } else {
    line(b[0], b[1], a[0], a[1], '#e8d9a8', 1.6);
    ctx.fillStyle = '#ccc'; ctx.beginPath(); ctx.arc(a[0], a[1], 1.3, 0, 6.3); ctx.fill();
  }
}

/* ------------------------------- Main render ----------------------------- */

function entityVisible(ix, iy, vis, pad) { return ix > vis.l - pad && ix < vis.r + pad && iy > vis.t - pad - 60 && iy < vis.b + pad; }

function render() {
  ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
  ctx.fillStyle = '#0b100c'; ctx.fillRect(0, 0, W, H);
  if (!game) return;
  ctx.setTransform(DPR * zoom, 0, 0, DPR * zoom, DPR * (VX - cam.x * zoom), DPR * (VY - cam.y * zoom));
  const now = performance.now() / 1000;
  const vis = { l: cam.x - VX / zoom, r: cam.x + (W - VX) / zoom, t: cam.y - VY / zoom, b: cam.y + (H - VY) / zoom };
  const cs = [[vis.l, vis.t], [vis.r, vis.t], [vis.l, vis.b + 40], [vis.r, vis.b + 40]].map(([ix, iy]) => { const a = ix / 32, b = iy / 16; return [(a + b) / 2, (b - a) / 2]; });
  vis.x0 = Math.max(0, Math.floor(Math.min(...cs.map((c) => c[0]))) - 1); vis.x1 = Math.min(MAP_W - 1, Math.ceil(Math.max(...cs.map((c) => c[0]))) + 1);
  vis.y0 = Math.max(0, Math.floor(Math.min(...cs.map((c) => c[1]))) - 1); vis.y1 = Math.min(MAP_H - 1, Math.ceil(Math.max(...cs.map((c) => c[1]))) + 1);

  drawTerrain(vis, now);

  // selection footprints & rally lines (on the ground)
  for (const e of sel) {
    if (e.kind === 'building' && !e.dead) {
      poly([P(e.x - 0.05, e.y - 0.05), P(e.x + e.w + 0.05, e.y - 0.05), P(e.x + e.w + 0.05, e.y + e.h + 0.05), P(e.x - 0.05, e.y + e.h + 0.05)], 'rgba(255,255,255,.1)');
      ctx.strokeStyle = e.team === 0 ? '#9af0a6' : '#ff8e7e'; ctx.lineWidth = 2.5; ctx.stroke();
    } else if (e.kind === 'node' && !e.dead) {
      poly([P(e.x, e.y), P(e.x + 1, e.y), P(e.x + 1, e.y + 1), P(e.x, e.y + 1)], 'rgba(255,255,255,.12)');
      ctx.strokeStyle = '#fff'; ctx.lineWidth = 2; ctx.stroke();
    }
  }
  if (sel.length === 1 && sel[0].kind === 'building' && sel[0].rally && sel[0].team === 0) {
    const b = sel[0], c = centerOf(b), r = b.rally;
    ctx.setLineDash([6, 5]); line(isoX(c.x, c.y), isoY(c.x, c.y), isoX(r.x, r.y), isoY(r.x, r.y), 'rgba(255,255,255,.6)', 1.5); ctx.setLineDash([]);
    const p = P(r.x, r.y, 0); line(p[0], p[1], p[0], p[1] - 20, '#fff', 1.5);
    ctx.fillStyle = TEAMS[0].color; ctx.beginPath(); ctx.moveTo(p[0], p[1] - 20); ctx.lineTo(p[0] + 10, p[1] - 16); ctx.lineTo(p[0], p[1] - 12); ctx.fill();
  }
  // order markers
  for (const f of clickFx) {
    const k = f.t / 0.7, r = 6 + k * 14;
    ctx.strokeStyle = f.color; ctx.globalAlpha = 1 - k; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.ellipse(isoX(f.x, f.y), isoY(f.x, f.y), r * 1.6, r * 0.8, 0, 0, 6.3); ctx.stroke(); ctx.globalAlpha = 1;
  }

  // placement footprint
  if (placing) {
    const d = BUILDING_DEFS[placing.type];
    for (let j = 0; j < d.h; j++) for (let i = 0; i < d.w; i++) {
      const tx = placing.x + i, ty = placing.y + j;
      const ok = tx >= 0 && ty >= 0 && tx < MAP_W && ty < MAP_H && game.terrain[ty * MAP_W + tx] !== T_WATER && !game.occ[ty * MAP_W + tx];
      poly([P(tx, ty), P(tx + 1, ty), P(tx + 1, ty + 1), P(tx, ty + 1)], ok && placing.valid ? 'rgba(80,255,120,.38)' : 'rgba(255,70,60,.45)');
    }
  }

  // collect + depth sort
  const list = [];
  for (const n of game.nodes) {
    const ix = isoX(n.x + 0.5, n.y + 0.5), iy = isoY(n.x + 0.5, n.y + 0.5);
    if (!entityVisible(ix, iy, vis, 50)) continue;
    n._k = n.x + n.y + 1; list.push(n);
  }
  for (const b of game.buildings) {
    const ix = isoX(b.x + b.w / 2, b.y + b.h / 2), iy = isoY(b.x + b.w / 2, b.y + b.h / 2);
    if (!entityVisible(ix, iy, vis, 160)) continue;
    b._k = b.x + b.y + (b.w + b.h) / 2 - (b.type === 'farm' ? 100 : 0); list.push(b);
  }
  for (const u of game.units) {
    if (!entityVisible(isoX(u.x, u.y), isoY(u.x, u.y), vis, 60)) continue;
    u._k = u.x + u.y; list.push(u);
  }
  list.sort((a, b) => a._k - b._k);
  for (const e of list) {
    if (e.kind === 'node') { if (e.type === 'tree') drawTree(e); else drawRocks(e); }
    else if (e.kind === 'building') drawBuilding(e, 0);
    else drawUnit(e, sel.includes(e));
  }

  // projectiles & effects
  for (const p of game.projectiles) drawProjectile(p);
  for (const e of game.effects) {
    const x = isoX(e.x, e.y), y = isoY(e.x, e.y) - e.z, a = clamp(e.life / e.max, 0, 1);
    if (e.type === 'dust') { ctx.globalAlpha = a * 0.8; ctx.fillStyle = e.color; ctx.beginPath(); ctx.arc(x, y, 2 + (1 - a) * 3, 0, 6.3); ctx.fill(); ctx.globalAlpha = 1; }
    else { ctx.globalAlpha = Math.min(1, a * 2); ctx.font = 'bold 13px Georgia'; ctx.textAlign = 'center'; ctx.lineWidth = 3; ctx.strokeStyle = 'rgba(0,0,0,.7)'; ctx.strokeText(e.text, x, y); ctx.fillStyle = e.color; ctx.fillText(e.text, x, y); ctx.globalAlpha = 1; }
  }

  // health bars
  for (const e of list) {
    if (e.kind === 'node') continue;
    const isSel = sel.includes(e);
    if (e.kind === 'unit') {
      if (!isSel && e.hp >= e.maxHp) continue;
      drawBar(isoX(e.x, e.y), isoY(e.x, e.y) - (e.def.big ? 46 : 31), e.def.big ? 26 : 20, e.hp / e.maxHp);
    } else {
      const showB = isSel || e.hp < e.maxHp || !e.done;
      if (!showB) continue;
      const c = centerOf(e), top = (e.def.height || 20) * (e.done ? 1 : Math.max(0.14, e.progress)) + (e.type === 'tower' || e.type === 'towncenter' ? 30 : 22);
      const bx = isoX(c.x, c.y), by = isoY(c.x, c.y) - top;
      if (!e.done) { drawBar(bx, by, 26 + e.w * 10, e.progress, 'prog'); drawBar(bx, by + 6, 26 + e.w * 10, e.hp / e.maxHp); }
      else drawBar(bx, by, 26 + e.w * 10, e.hp / e.maxHp);
    }
  }

  // building ghost
  if (placing) {
    const d = BUILDING_DEFS[placing.type];
    drawBuilding({ type: placing.type, def: d, team: 0, x: placing.x, y: placing.y, w: d.w, h: d.h, done: true, progress: 1, hp: d.hp, maxHp: d.hp, users: new Set() }, placing.valid ? 0.62 : 0.35);
  }

  // screen-space overlays
  ctx.setTransform(DPR, 0, 0, DPR, 0, 0);
  if (mouse.down && mouse.drag && !placing) {
    const x = Math.min(mouse.sx, mouse.x), y = Math.min(mouse.sy, mouse.y), w = Math.abs(mouse.x - mouse.sx), h = Math.abs(mouse.y - mouse.sy);
    ctx.fillStyle = 'rgba(100,255,130,.12)'; ctx.fillRect(x, y, w, h);
    ctx.strokeStyle = '#7dff91'; ctx.lineWidth = 1.5; ctx.strokeRect(x, y, w, h);
  }
}

/* --------------------------------- Minimap ------------------------------- */

const mini = (x, y) => [((x - y) * 32 + MAP_H * 32) * MM_K, (x + y) * 16 * MM_K];
function buildMiniTerrain() {
  miniTerrain = document.createElement('canvas');
  miniTerrain.width = MM_W; miniTerrain.height = MM_H;
  const c = miniTerrain.getContext('2d');
  c.fillStyle = '#0b0f0a'; c.fillRect(0, 0, MM_W, MM_H);
  for (let y = 0; y < MAP_H; y++) for (let x = 0; x < MAP_W; x++) {
    const t = game.terrain[y * MAP_W + x];
    c.fillStyle = t === T_WATER ? '#2b69a1' : t === T_SAND ? '#b9a566' : '#3d6e2d';
    const a = mini(x, y), b = mini(x + 1, y), d = mini(x + 1, y + 1), e = mini(x, y + 1);
    c.beginPath(); c.moveTo(a[0], a[1] - 0.3); c.lineTo(b[0] + 0.4, b[1]); c.lineTo(d[0], d[1] + 0.4); c.lineTo(e[0] - 0.4, e[1]); c.closePath(); c.fill();
  }
}
function drawMinimap() {
  if (!game) return;
  mctx.clearRect(0, 0, MM_W, MM_H);
  if (miniTerrain) mctx.drawImage(miniTerrain, 0, 0);
  for (const n of game.nodes) {
    const p = mini(n.x + 0.5, n.y + 0.5);
    mctx.fillStyle = n.type === 'tree' ? '#1f4a1c' : n.type === 'gold' ? '#ffd23a' : '#b5b5bd';
    mctx.fillRect(p[0] - 0.8, p[1] - 0.8, n.type === 'tree' ? 1.6 : 2.4, n.type === 'tree' ? 1.6 : 2.4);
  }
  for (const b of game.buildings) {
    const c = centerOf(b), p = mini(c.x, c.y), s = b.w + 1.5;
    mctx.fillStyle = TEAMS[b.team].color; mctx.fillRect(p[0] - s / 2, p[1] - s / 2, s, s);
    mctx.strokeStyle = '#000'; mctx.lineWidth = 0.6; mctx.strokeRect(p[0] - s / 2, p[1] - s / 2, s, s);
  }
  for (const u of game.units) {
    const p = mini(u.x, u.y);
    mctx.fillStyle = TEAMS[u.team].color; mctx.fillRect(p[0] - 1, p[1] - 1, 2.2, 2.2);
  }
  for (const pg of pings) {
    const p = mini(pg.x, pg.y), r = 3 + pg.t * 10;
    mctx.strokeStyle = 'rgba(255,80,60,' + (1 - pg.t / 1.5).toFixed(2) + ')'; mctx.lineWidth = 2; mctx.beginPath(); mctx.arc(p[0], p[1], r, 0, 6.3); mctx.stroke();
  }
  const cs = [[0, TOP_H], [W, TOP_H], [W, H - BOTTOM_H], [0, H - BOTTOM_H]].map(([sx, sy]) => { const w = s2w(sx, sy); return mini(w.x, w.y); });
  mctx.strokeStyle = '#fff'; mctx.lineWidth = 1.2; mctx.beginPath();
  cs.forEach((c, i) => (i ? mctx.lineTo(c[0], c[1]) : mctx.moveTo(c[0], c[1]))); mctx.closePath(); mctx.stroke();
}
function miniToWorld(mx, my) {
  const ix = mx / MM_K - MAP_H * 32, iy = my / MM_K;
  const a = ix / 32, b = iy / 16;
  return { x: clamp((a + b) / 2, 0, MAP_W - 1), y: clamp((b - a) / 2, 0, MAP_H - 1) };
}

/* --------------------------- Picking & selection ------------------------- */

function buildingHit(b, sx, sy) {
  const hh = ((b.def.height || 20) + 6) * zoom;
  const p = (x, y) => w2s(x, y);
  const N = p(b.x, b.y), E = p(b.x + b.w, b.y), S = p(b.x + b.w, b.y + b.h), Wp = p(b.x, b.y + b.h);
  return pointInPoly(sx, sy, [[Wp.x, Wp.y - hh], [N.x, N.y - hh], [E.x, E.y - hh], [E.x, E.y], [S.x, S.y], [Wp.x, Wp.y]]);
}
function pickAt(sx, sy) {
  let best = null, bd = 1e9;
  for (const u of game.units) {
    const p = w2s(u.x, u.y), r = (u.def.big ? 17 : 12) * zoom;
    const d = Math.hypot(p.x - sx, p.y - 11 * zoom - sy);
    if (d < r && d < bd) { bd = d; best = u; }
  }
  if (best) return best;
  let bb = null, bk = -1e9;
  for (const b of game.buildings) {
    if (b.type === 'farm') continue;
    if (buildingHit(b, sx, sy)) { const k = b.x + b.y + (b.w + b.h) / 2; if (k > bk) { bk = k; bb = b; } }
  }
  if (bb) return bb;
  const w = s2w(sx, sy), tx = Math.floor(w.x), ty = Math.floor(w.y);
  for (let dy = 0; dy <= 1; dy++) for (let dx = 0; dx <= 1; dx++) {
    // trees draw above their tile, so also test the tile behind
    const x = tx + dx, y = ty + dy;
    if (x < 0 || y < 0 || x >= MAP_W || y >= MAP_H) continue;
    const o = game.occ[y * MAP_W + x];
    if (o && o.kind === 'node' && (dx === 0 && dy === 0)) return o;
    if (o && o.kind === 'node' && o.type === 'tree' && Math.hypot(w2s(o.x + 0.5, o.y + 0.5).x - sx, w2s(o.x + 0.5, o.y + 0.5).y - 14 * zoom - sy) < 16 * zoom) return o;
  }
  const ox = game.occ[ty * MAP_W + tx];
  if (ox && ox.kind === 'building') return ox;
  return null;
}

function setSel(list) {
  sel = list; cmdSig = '';
  if (game) { renderInfo(); renderCmds(); }   // refresh now, so hotkeys right after a selection act on the new command card
}
function selectUnitsInBox(x0, y0, x1, y1, add) {
  const xa = Math.min(x0, x1), xb = Math.max(x0, x1), ya = Math.min(y0, y1), yb = Math.max(y0, y1);
  const found = game.units.filter((u) => {
    if (u.team !== 0) return false;
    const p = w2s(u.x, u.y);
    return p.x >= xa && p.x <= xb && p.y - 10 >= ya - 8 && p.y - 10 <= yb + 8;
  });
  if (!found.length) { if (!add) setSel([]); return; }
  setSel(add ? Array.from(new Set(sel.filter((e) => e.kind === 'unit' && e.team === 0).concat(found))) : found);
  sfx('select');
}
function leftClick(sx, sy, shift) {
  if (cmdMode === 'attackmove') {
    const w = s2w(sx, sy), us = mine();
    const soldiers = us.filter((u) => !isWorker(u)), others = us.filter(isWorker);
    if (soldiers.length) game.orderAttackMove(soldiers, w.x, w.y);
    if (others.length) game.orderMove(others, w.x, w.y);
    addFx(w.x, w.y, '#ff8e7e'); sfx('order'); setMode(null);
    return;
  }
  const e = pickAt(sx, sy), now = performance.now();
  if (!e) { if (!shift) setSel([]); return; }
  if (e.kind === 'unit' && e.team === 0) {
    if (now - mouse.lastClick < 380 && mouse.lastType === e.type) {
      const same = game.units.filter((u) => u.team === 0 && u.type === e.type && (() => { const p = w2s(u.x, u.y); return p.x >= 0 && p.x <= W && p.y >= TOP_H && p.y <= H - BOTTOM_H; })());
      setSel(same);
    } else if (shift) setSel(sel.includes(e) ? sel.filter((s) => s !== e) : sel.filter((s) => s.kind === 'unit' && s.team === 0).concat([e]));
    else setSel([e]);
  } else setSel([e]);
  mouse.lastClick = now; mouse.lastType = e.type;
  sfx('select');
}
function addFx(x, y, color) { clickFx.push({ x, y, color, t: 0 }); }

function rightClick(sx, sy) {
  if (cmdMode) { setMode(null); return; }
  if (placing) { placing = null; cmdSig = ''; return; }
  const w = s2w(sx, sy), tgt = pickAt(sx, sy), us = mine();
  if (us.length) {
    const workers = us.filter(isWorker), others = us.filter((u) => !isWorker(u));
    if (tgt && tgt.team === 1) { game.orderAttack(us, tgt); addFx(w.x, w.y, '#ff6b5a'); }
    else if (tgt && tgt.kind === 'node') {
      if (workers.length) game.orderGather(workers, tgt);
      if (others.length) game.orderMove(others, w.x, w.y);
      addFx(w.x, w.y, '#fff3a0');
    } else if (tgt && tgt.kind === 'building' && tgt.team === 0 && (tgt.type === 'farm' || !tgt.done || tgt.hp < tgt.maxHp) && workers.length) {
      if (tgt.type === 'farm' && tgt.done) game.orderGather(workers, tgt); else game.orderBuild(workers, tgt);
      if (others.length) game.orderMove(others, w.x, w.y);
      addFx(w.x, w.y, '#9af0a6');
    } else { game.orderMove(us, w.x, w.y); addFx(w.x, w.y, '#9af0a6'); }
    sfx('order');
    return;
  }
  if (sel.length === 1 && sel[0].kind === 'building' && sel[0].team === 0 && sel[0].done && sel[0].def.trains) {
    const b = sel[0];
    game.setRally(b, w.x, w.y, tgt && (tgt.kind === 'node' || tgt.type === 'farm') ? tgt : null);
    addFx(w.x, w.y, '#9af0a6'); toast('Rally point set'); sfx('order');
  }
}

function setMode(m) {
  cmdMode = m;
  const hint = $('modeHint');
  if (m === 'attackmove') { hint.style.display = 'block'; hint.textContent = 'Attack-move: click a destination (right-click or Esc to cancel)'; canvas.style.cursor = 'crosshair'; }
  else if (placing) { hint.style.display = 'block'; hint.textContent = 'Click to build (Shift = keep building, right-click / Esc = cancel)'; canvas.style.cursor = 'crosshair'; }
  else { hint.style.display = 'none'; canvas.style.cursor = 'default'; }
  cmdSig = '';
}

/* --------------------------------- Commands ------------------------------ */

function startPlacing(type) {
  const d = BUILDING_DEFS[type], res = game.teams[0].res;
  const miss = missingRes(res, d.cost);
  if (miss) { toast('Not enough ' + miss, 'bad'); sfx('err'); return; }
  cmdMode = null; placing = { type, x: 0, y: 0, valid: false };
  updatePlacing(); setMode(null);
}
function updatePlacing() {
  if (!placing) return;
  const d = BUILDING_DEFS[placing.type], w = s2w(mouse.x, mouse.y);
  placing.x = Math.floor(w.x) - Math.floor(d.w / 2); placing.y = Math.floor(w.y) - Math.floor(d.h / 2);
  placing.valid = game.canPlace(placing.type, placing.x, placing.y, 0);
}
function tryPlace(shift) {
  if (!placing) return;
  const workers = mine().filter(isWorker);
  if (!workers.length) { placing = null; setMode(null); toast('Select a builder first', 'bad'); return; }
  const r = game.placeBuilding(0, placing.type, placing.x, placing.y, workers);
  if (r.err) { toast(r.err, 'bad'); sfx('err'); return; }
  sfx('place');
  const type = placing.type;
  if (shift && missingRes(game.teams[0].res, BUILDING_DEFS[type].cost) === null) { updatePlacing(); } else { placing = null; setMode(null); }
  cmdSig = '';
}
function stopCmd() { const us = mine(); if (us.length) game.orderStop(us); setMode(null); }

function getCommands() {
  const cmds = new Array(12).fill(null);
  if (!game || game.winner !== null) return cmds;
  const team = game.teams[0], res = team.res;
  const ms = sel.filter((e) => e.team === 0 && !e.dead);
  if (!ms.length) return cmds;
  if (ms[0].kind === 'building') {
    const b = ms[0];
    if (!b.done) return cmds;
    let i = 0;
    for (const type of b.def.trains || []) {
      const d = UNIT_DEFS[type];
      const popOk = team.popUsed + d.pop <= team.popCap;
      const bonus = d.bonus ? ' · Bonus vs ' + Object.keys(d.bonus).join(', ') : '';
      cmds[i++] = {
        icon: d.icon, label: d.name, cost: d.cost, enabled: !missingRes(res, d.cost) && popOk,
        tip: { title: d.name, cost: d.cost, desc: d.desc, stats: `HP ${d.hp} · ATK ${d.atk} · Armor ${d.armor.melee}/${d.armor.pierce} · ${d.range > 2 ? 'Range ' + d.range : 'Melee'} · Pop ${d.pop}${bonus}`, warn: !popOk ? 'Need more houses' : null, extra: 'Shift: queue 5' },
        run: (shift) => { for (let n = 0; n < (shift ? 5 : 1); n++) { const err = game.queueUnit(b, type); if (err) { toast(err, 'bad'); sfx('err'); break; } if (n === 0) sfx('select'); } },
      };
    }
    for (const tk of b.def.research || []) {
      const T = TECHS[tk], lvl = team.up[tk] + game.techLevelPending(0, tk), maxed = lvl >= T.max;
      const cost = T.cost(Math.min(lvl, T.max - 1));
      cmds[i++] = {
        icon: T.icon, label: T.name + (maxed ? ' (max)' : ' ' + ['I', 'II', 'III'][lvl]), cost: maxed ? {} : cost, enabled: !maxed && !missingRes(res, cost),
        tip: { title: T.name + ' — level ' + (lvl + 1), cost: maxed ? {} : cost, desc: T.desc, warn: maxed ? 'Fully researched' : null },
        run: () => { const err = game.queueTech(b, tk); if (err) { toast(err, 'bad'); sfx('err'); } else sfx('select'); },
      };
    }
    return cmds;
  }
  const us = ms.filter((e) => e.kind === 'unit');
  if (!us.length) return cmds;
  const workers = us.filter(isWorker), soldiers = us.filter((u) => !isWorker(u));
  if (workers.length) {
    BUILD_SLOTS.forEach((type, i) => {
      const d = BUILDING_DEFS[type];
      cmds[i] = {
        icon: d.icon, label: d.name, cost: d.cost, enabled: !missingRes(res, d.cost), active: placing && placing.type === type,
        tip: { title: d.name, cost: d.cost, desc: d.desc, stats: `HP ${d.hp} · Build time ${d.time}s${d.pop ? ' · +' + d.pop + ' pop' : ''}` },
        run: () => startPlacing(type),
      };
    });
  }
  if (soldiers.length) {
    cmds[workers.length ? 10 : 4] = { icon: '⚔️', label: 'Attack', cost: {}, enabled: true, active: cmdMode === 'attackmove', tip: { title: 'Attack-move', desc: 'Move to a point, attacking any enemy met on the way.' }, run: () => setMode(cmdMode ? null : 'attackmove') };
  }
  cmds[workers.length ? 11 : 5] = { icon: '⏹️', label: 'Stop', cost: {}, enabled: true, tip: { title: 'Stop', desc: 'Cancel current orders.' }, run: stopCmd };
  return cmds;
}

/* ----------------------------------- HUD --------------------------------- */

function unitStatus(u) {
  switch (u.state) {
    case 'idle': return 'Idle';
    case 'move': return 'Moving';
    case 'attackmove': return 'Attack-moving';
    case 'attack': return 'Attacking';
    case 'build': return u.targetDone ? 'Repairing' : 'Building';
    case 'gather': return ({ wood: 'Chopping wood', food: 'Farming', gold: 'Mining gold', stone: 'Quarrying stone' }[u.gatherKind] || 'Gathering') + (u.sub === 'toDrop' ? ' (returning)' : '');
  }
  return '';
}
const hpClass = (f) => (f > 0.6 ? '' : f > 0.3 ? 'mid' : 'low');
const tagFor = (team) => (team === 0 ? '<span class="tag g">Green</span>' : team === 1 ? '<span class="tag y">Yellow</span>' : '<span class="tag n">Neutral</span>');

function renderInfo() {
  const el = $('info');
  sel = sel.filter((e) => !e.dead);
  if (!sel.length) {
    el.innerHTML = '<div class="ititle">Nothing selected</div><div class="idesc">Left-click or drag to select. Right-click to give orders. Select your Town Center to train builders, then send them to chop trees, build farms and mine gold. Press F1 for all controls.</div>';
    return;
  }
  if (sel.length === 1) {
    const e = sel[0];
    if (e.kind === 'node') {
      const d = NODE_DEFS[e.type], f = e.amt / e.max;
      el.innerHTML = `<div class="ititle"><span class="big">${RES_ICON[d.res]}</span>${d.name}</div><div class="bar ${hpClass(f)}"><div style="width:${f * 100}%"></div></div><div class="istat">Remaining: <b>${Math.ceil(e.amt)}</b> ${d.res}</div><div class="idesc">Right-click with builders selected to gather. Build a Storehouse nearby to shorten the walk.</div>`;
      return;
    }
    const f = e.hp / e.maxHp;
    if (e.kind === 'unit') {
      const d = e.def;
      let extra = '';
      if (e.team === 0) extra = `<div class="istat">${unitStatus(e)}${e.carry >= 1 ? ` · Carrying <b>${Math.floor(e.carry)}</b> ${RES_ICON[e.carryType]}` : ''}</div>`;
      const atk = d.atk + (e.team !== undefined && d.role === 'soldier' && d.atkType !== 'siege' && game ? 2 * game.teams[e.team].up.weapons : 0);
      const arm = (k) => d.armor[k] + (d.role === 'soldier' && k !== 'siege' ? game.teams[e.team].up.armor : 0);
      el.innerHTML = `<div class="ititle"><span class="big">${d.icon}</span>${d.name} ${tagFor(e.team)}</div><div class="bar ${hpClass(f)}"><div style="width:${f * 100}%"></div></div>
        <div class="istat">HP <b>${Math.ceil(e.hp)}/${e.maxHp}</b> · ATK <b>${atk}</b> · Armor <b>${arm('melee')}/${arm('pierce')}</b> · ${d.range > 2 ? 'Range <b>' + d.range + '</b>' : 'Melee'}</div>${extra}<div class="idesc">${d.desc}</div>`;
      return;
    }
    const d = e.def;
    let html = `<div class="ititle"><span class="big">${d.icon}</span>${d.name} ${tagFor(e.team)}</div><div class="bar ${hpClass(f)}"><div style="width:${f * 100}%"></div></div><div class="istat">HP <b>${Math.ceil(e.hp)}/${e.maxHp}</b>${d.atk ? ` · ATK <b>${d.atk}</b> · Range <b>${d.range}</b>` : ''}${d.pop && e.done ? ` · +<b>${d.pop}</b> pop` : ''}</div>`;
    if (!e.done) html += `<div class="istat">Under construction — <b>${Math.floor(e.progress * 100)}%</b></div><div class="bar prog"><div style="width:${e.progress * 100}%"></div></div>`;
    else if (e.team === 0) {
      if (e.type === 'farm') html += `<div class="istat">Farmers: <b>${e.users.size}/${FARM_SLOTS}</b></div>`;
      if (e.queue.length) {
        html += '<div class="queue">' + e.queue.map((q, i) => {
          const ic = q.kind === 'unit' ? UNIT_DEFS[q.type].icon : TECHS[q.tech].icon;
          const p = i === 0 ? Math.min(100, e.qt / q.time * 100) : 0;
          return `<div class="qslot" data-q="${i}" title="Click to cancel">${ic}<div class="qp" style="width:${p}%"></div></div>`;
        }).join('') + '</div>';
      } else if (d.trains || d.research) html += `<div class="idesc">${d.desc} Right-click the ground to set a rally point.</div>`;
      else html += `<div class="idesc">${d.desc}</div>`;
    } else html += `<div class="idesc">${d.desc}</div>`;
    el.innerHTML = html;
    return;
  }
  const counts = {};
  for (const e of sel) counts[e.def ? e.def.icon : '?'] = (counts[e.def ? e.def.icon : '?'] || 0) + 1;
  const head = Object.keys(counts).map((k) => `${k}×${counts[k]}`).join('  ');
  const grid = sel.slice(0, 48).map((e) => `<div class="sunit${e.carry >= 1 ? ' carry' : ''}" data-id="${e.id}" title="${e.def.name}">${e.def.icon}<div class="sh" style="width:${clamp(e.hp / e.maxHp, 0, 1) * 30}px;background:${e.hp / e.maxHp > 0.5 ? '#5fcf6b' : e.hp / e.maxHp > 0.25 ? '#e2c23a' : '#d8493b'}"></div></div>`).join('');
  el.innerHTML = `<div class="ititle">${sel.length} selected <span style="font-size:15px;font-weight:normal">${head}</span></div><div class="sgrid">${grid}</div>${sel.length > 48 ? `<div class="istat">+${sel.length - 48} more</div>` : ''}`;
}

function renderCmds() {
  curCmds = getCommands();
  const sig = curCmds.map((c) => (c ? c.icon + (c.enabled ? 1 : 0) + (c.active ? 'a' : '') + (c.label || '') : '-')).join('|') + '#' + sel.map((e) => e.id).join(',').slice(0, 40);
  if (sig === cmdSig) return;
  cmdSig = sig;
  $('cmds').innerHTML = curCmds.map((c, i) => {
    if (!c) return '<button class="cmd empty"></button>';
    const co = Object.keys(c.cost || {}).length ? costHtml(c.cost, game.teams[0].res) : '<span style="opacity:.8">' + (c.label || '') + '</span>';
    return `<button class="cmd${c.enabled ? '' : ' off'}${c.active ? ' active' : ''}" data-i="${i}"><span class="hk">${HOTKEYS[i]}</span><span class="ic">${c.icon}</span><span class="co">${co}</span></button>`;
  }).join('');
}

function updateHud() {
  if (!game) return;
  const t = game.teams[0], r = t.res;
  for (const k of RES_LIST) { const el = $('res-' + k); el.lastElementChild.textContent = Math.floor(r[k]); el.classList.toggle('low', r[k] < 30); }
  const pop = $('res-pop'); pop.lastElementChild.textContent = t.popUsed + '/' + t.popCap; pop.classList.toggle('low', t.popUsed >= t.popCap);
  const s = Math.floor(game.time);
  $('clock').textContent = Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
  $('idle-count').textContent = idleBuilders().length;
  renderInfo(); renderCmds();
}
const idleBuilders = () => game.units.filter((u) => u.team === 0 && u.type === 'builder' && u.state === 'idle');

function showTip(i) {
  const c = curCmds[i], tip = $('tooltip');
  if (!c || !c.tip) { tip.style.display = 'none'; return; }
  const T = c.tip, res = game.teams[0].res;
  tip.innerHTML = `<h4>${T.title} <span style="opacity:.7;font-size:12px">[${HOTKEYS[i]}]</span></h4>` +
    (T.cost && Object.keys(T.cost).length ? `<div class="tc">${costHtml(T.cost, res)}</div>` : '') +
    (T.stats ? `<div>${T.stats}</div>` : '') + `<div class="d">${T.desc || ''}</div>` +
    (T.warn ? `<div class="no">${T.warn}</div>` : '') + (T.extra ? `<div style="opacity:.7;font-size:12px">${T.extra}</div>` : '');
  tip.style.display = 'block';
  const el = $('cmds').querySelector(`[data-i="${i}"]`);
  const rc = el ? el.getBoundingClientRect() : { left: W - 300, top: H - 200 };
  tip.style.left = Math.max(8, Math.min(W - 300, rc.left - 100)) + 'px';
  tip.style.top = Math.max(8, rc.top - tip.offsetHeight - 10) + 'px';
}

function processEvents() {
  for (const ev of game.events) {
    if (ev.team !== 0) continue;
    if (ev.type === 'alert') { toast('We are under attack!', 'alert'); sfx('alert'); pings.push({ x: ev.x, y: ev.y, t: 0 }); }
    else if (ev.type === 'built') { toast(ev.name + ' complete'); sfx('build'); }
    else if (ev.type === 'msg') { toast(ev.text, ev.alertEnemy ? 'alert' : ''); if (ev.alertEnemy) sfx('alert'); }
    else if (ev.type === 'destroyed') { toast('Our ' + ev.name + ' was destroyed!', 'alert'); sfx('alert'); }
  }
  game.events.length = 0;
}

/* ---------------------------------- Input -------------------------------- */

function overlayOpen() { return !$('overlay').classList.contains('gone'); }
function showPanel(id) {
  $('overlay').classList.remove('gone');
  for (const p of ['menu', 'endpanel', 'helppanel']) $(p).classList.toggle('hidden', p !== id);
  $('overlay').classList.toggle('clear', id !== 'menu');
}
function hideOverlay() { $('overlay').classList.add('gone'); }

canvas.addEventListener('mousedown', (e) => {
  if (!game || overlayOpen()) return;
  mouse.x = e.clientX; mouse.y = e.clientY;
  if (e.button === 0) {
    if (placing) { tryPlace(e.shiftKey); return; }
    mouse.down = true; mouse.drag = false; mouse.sx = e.clientX; mouse.sy = e.clientY;
  } else if (e.button === 1) { mouse.mid = true; e.preventDefault(); }
  else if (e.button === 2) rightClick(e.clientX, e.clientY);
});
window.addEventListener('mousemove', (e) => {
  mouse.x = e.clientX; mouse.y = e.clientY;
  if (!game) return;
  if (mouse.down && !mouse.drag && Math.hypot(mouse.x - mouse.sx, mouse.y - mouse.sy) > 6) mouse.drag = true;
  if (mouse.mid) { cam.x -= e.movementX / zoom; cam.y -= e.movementY / zoom; clampCam(); }
  if (mouse.onMini && (e.buttons & 1)) minimapPointer(e);
  if (placing) updatePlacing();
});
canvas.addEventListener('mouseenter', () => { mouse.inside = true; });
canvas.addEventListener('mouseleave', () => { mouse.inside = false; });
window.addEventListener('mouseup', (e) => {
  if (!game) return;
  if (e.button === 0) {
    mouse.onMini = false;
    if (mouse.down) {
      if (mouse.drag && !placing) selectUnitsInBox(mouse.sx, mouse.sy, e.clientX, e.clientY, e.shiftKey);
      else if (!placing && e.target === canvas) leftClick(e.clientX, e.clientY, e.shiftKey);
    }
    mouse.down = false; mouse.drag = false;
  } else if (e.button === 1) mouse.mid = false;
});
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  if (!game) return;
  const before = s2w(e.clientX, e.clientY);
  zoom = clamp(zoom * (e.deltaY < 0 ? 1.1 : 1 / 1.1), 0.55, 1.6);
  const after = s2w(e.clientX, e.clientY);
  cam.x += (isoX(before.x, before.y) - isoX(after.x, after.y));
  cam.y += (isoY(before.x, before.y) - isoY(after.x, after.y));
  clampCam();
}, { passive: false });
document.addEventListener('contextmenu', (e) => e.preventDefault());

function minimapPointer(e) {
  const r = mmCanvas.getBoundingClientRect();
  const w = miniToWorld((e.clientX - r.left) * MM_W / r.width, (e.clientY - r.top) * MM_H / r.height);
  centerCam(w.x, w.y);
}
mmCanvas.addEventListener('mousedown', (e) => {
  if (!game) return;
  const r = mmCanvas.getBoundingClientRect();
  const w = miniToWorld((e.clientX - r.left) * MM_W / r.width, (e.clientY - r.top) * MM_H / r.height);
  if (e.button === 0) {
    if (cmdMode === 'attackmove') { const us = mine(); const s = us.filter((u) => !isWorker(u)); if (s.length) game.orderAttackMove(s, w.x, w.y); setMode(null); return; }
    mouse.onMini = true; centerCam(w.x, w.y);
  } else if (e.button === 2) {
    const us = mine();
    if (us.length) { game.orderMove(us, w.x, w.y); sfx('order'); }
  }
});

$('cmds').addEventListener('click', (e) => {
  const b = e.target.closest('.cmd');
  if (!b || b.dataset.i == null || !game) return;
  const c = curCmds[+b.dataset.i];
  if (c) { c.run(e.shiftKey); cmdSig = ''; }
});
$('cmds').addEventListener('mouseover', (e) => { const b = e.target.closest('.cmd'); if (b && b.dataset.i != null) showTip(+b.dataset.i); else $('tooltip').style.display = 'none'; });
$('cmds').addEventListener('mouseleave', () => { $('tooltip').style.display = 'none'; });
$('info').addEventListener('click', (e) => {
  if (!game) return;
  const q = e.target.closest('[data-q]');
  if (q && sel.length === 1 && sel[0].kind === 'building') { game.cancelQueue(sel[0], +q.dataset.q); sfx('err'); return; }
  const u = e.target.closest('[data-id]');
  if (u) { const id = +u.dataset.id; const ent = game.units.find((x) => x.id === id); if (ent) setSel([ent]); }
});

function cycleIdle() {
  const list = idleBuilders();
  if (!list.length) { toast('No idle builders'); return; }
  idleIdx = (idleIdx + 1) % list.length;
  const u = list[idleIdx]; setSel([u]); centerCam(u.x, u.y); sfx('select');
}
function centerSelection() {
  if (sel.length) { const c = centerOf(sel[0]); centerCam(c.x, c.y); return; }
  const tc = game.buildings.find((b) => b.team === 0 && b.type === 'towncenter');
  if (tc) { const c = centerOf(tc); centerCam(c.x, c.y); }
}
function togglePause() {
  if (!game || game.winner !== null) return;
  paused = !paused; $('btn-pause').textContent = paused ? '▶' : '⏸';
  if (paused) toast('Paused (P to resume)');
}

window.addEventListener('keydown', (e) => {
  keys[e.code] = true;
  if (e.code === 'F1') { e.preventDefault(); if (!$('helppanel').classList.contains('hidden') && overlayOpen()) { if (game) hideOverlay(); else showPanel('menu'); } else if (game) showPanel('helppanel'); return; }
  if (!game || overlayOpen()) { if (e.code === 'Escape' && !$('helppanel').classList.contains('hidden')) { if (game) hideOverlay(); else showPanel('menu'); } return; }
  if (e.ctrlKey && /^Digit[1-9]$/.test(e.code)) e.preventDefault();
  if (/^Digit[1-9]$/.test(e.code)) {
    const n = e.code.slice(5);
    if (e.ctrlKey || e.shiftKey || e.metaKey) { groups[n] = sel.filter((s) => s.kind === 'unit' || s.kind === 'building').filter((s) => s.team === 0).map((s) => s.id); toast('Group ' + n + ' set (' + groups[n].length + ')'); }
    else if (groups[n]) {
      const ids = new Set(groups[n]);
      const found = game.units.filter((u) => ids.has(u.id)).concat(game.buildings.filter((b) => ids.has(b.id)));
      if (found.length) { if (e.timeStamp - (groups.last || 0) < 400 && groups.lastN === n) centerCam(found[0].x, found[0].y); setSel(found); groups.last = e.timeStamp; groups.lastN = n; sfx('select'); }
    }
    return;
  }
  switch (e.code) {
    case 'Escape': if (placing) { placing = null; setMode(null); } else if (cmdMode) setMode(null); else setSel([]); return;
    case 'KeyP': togglePause(); return;
    case 'Space': e.preventDefault(); centerSelection(); return;
    case 'Period': cycleIdle(); return;
    case 'KeyM': muted = !muted; toast(muted ? 'Sound off' : 'Sound on'); return;
  }
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  const idx = HOTKEYS.indexOf(e.code.replace('Key', ''));
  if (e.code.startsWith('Key') && idx >= 0 && curCmds[idx]) { e.preventDefault(); curCmds[idx].run(e.shiftKey); cmdSig = ''; }
});
window.addEventListener('keyup', (e) => { keys[e.code] = false; });
window.addEventListener('blur', () => { for (const k in keys) keys[k] = false; mouse.down = false; mouse.mid = false; });
window.addEventListener('resize', resize);

$('btn-idle').addEventListener('click', () => { if (game) cycleIdle(); });
$('btn-pause').addEventListener('click', togglePause);
$('btn-speed').addEventListener('click', () => { speed = speed === 1 ? 2 : speed === 2 ? 3 : 1; $('btn-speed').textContent = '⏩ ' + speed + 'x'; });
$('btn-help').addEventListener('click', () => { if (game) showPanel('helppanel'); });
$('btn-closehelp').addEventListener('click', () => { if (game) hideOverlay(); else showPanel('menu'); });
for (const b of document.querySelectorAll('.diff')) b.addEventListener('click', () => {
  diffChoice = b.dataset.d;
  for (const o of document.querySelectorAll('.diff')) o.classList.toggle('sel', o === b);
});
$('btn-start').addEventListener('click', () => startGame(diffChoice));
$('btn-again').addEventListener('click', () => showPanel('menu'));

function cameraTick(dt) {
  if (!game || overlayOpen()) return;
  let dx = 0, dy = 0;
  if (keys.ArrowLeft) dx -= 1; if (keys.ArrowRight) dx += 1; if (keys.ArrowUp) dy -= 1; if (keys.ArrowDown) dy += 1;
  if (mouse.inside && !mouse.down) {
    if (mouse.x < 6) dx -= 1; if (mouse.x > W - 6) dx += 1; if (mouse.y < TOP_H + 6) dy -= 1;
  }
  if (dx || dy) { const s = 900 / zoom * dt; cam.x += dx * s; cam.y += dy * s * 0.6; clampCam(); }
}

/* --------------------------------- Main loop ----------------------------- */

function startGame(diff) {
  game = new Game({ difficulty: diff });
  sel = []; placing = null; cmdMode = null; paused = false; speed = 1; zoom = 1; cmdSig = '';
  pings.length = 0; clickFx.length = 0; for (const k in groups) delete groups[k];
  $('btn-pause').textContent = '⏸'; $('btn-speed').textContent = '⏩ 1x';
  buildMiniTerrain();
  const tc = game.buildings.find((b) => b.team === 0 && b.type === 'towncenter');
  centerCam(tc.x + 1.5, tc.y + 1.5);
  setSel([tc]);
  setMode(null);
  hideOverlay();
  toast('Train builders at your Town Center, then put them to work!');
  const dn = { easy: 'Easy', normal: 'Normal', hard: 'Hard' }[diff];
  setTimeout(() => { if (game) toast('Enemy difficulty: ' + dn); }, 1200);
}

function frame(ts) {
  const dtReal = Math.min(0.1, (ts - lastT) / 1000 || 0);
  lastT = ts;
  if (game) {
    if (!paused && !overlayOpen() || (game.winner !== null)) {
      let rem = dtReal * speed;
      while (rem > 0) { const s = Math.min(0.05, rem); game.update(s); rem -= s; }
    }
    game.sync();   // pull the C simulation's state into JS objects once per frame
    cameraTick(dtReal);
    processEvents();
    for (let i = clickFx.length - 1; i >= 0; i--) { clickFx[i].t += dtReal; if (clickFx[i].t > 0.7) clickFx.splice(i, 1); }
    for (let i = pings.length - 1; i >= 0; i--) { pings[i].t += dtReal; if (pings[i].t > 1.5) pings.splice(i, 1); }
    if (placing) updatePlacing();
    hudTimer -= dtReal;
    if (hudTimer <= 0) { hudTimer = 0.2; updateHud(); }
    if (game.winner !== null && $('endpanel').classList.contains('hidden')) {
      const won = game.winner === 0;
      $('endtitle').textContent = won ? 'Victory!' : 'Defeat';
      $('endtitle').style.color = won ? '#9af0a6' : '#ff8e7e';
      const s = Math.floor(game.time);
      $('endsub').textContent = won ? `The Yellow realm has fallen. Your Green banners fly victorious after ${Math.floor(s / 60)}m ${s % 60}s.` : `Your last building has fallen after ${Math.floor(s / 60)}m ${s % 60}s. The Yellow realm prevails.`;
      showPanel('endpanel');
    }
    render();
    drawMinimap();
  } else {
    ctx.setTransform(DPR, 0, 0, DPR, 0, 0); ctx.fillStyle = '#0b100c'; ctx.fillRect(0, 0, W, H);
  }
  requestAnimationFrame(frame);
}

resize();
showPanel('menu');
// The simulation is a WebAssembly module compiled from C; wait for it before allowing a game to start.
$('btn-start').disabled = true; $('btn-start').textContent = 'Loading…';
gameReady.then(() => { $('btn-start').disabled = false; $('btn-start').textContent = 'Start Game'; })
  .catch((err) => { $('btn-start').textContent = 'Failed to load game core'; console.error(err); });
requestAnimationFrame(frame);
