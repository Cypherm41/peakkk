/* Native test for the C simulation core. Includes game.c directly so it can poke at internals.
 *   make test            (or)   gcc -std=c99 -O2 -Ic tests/sim_test.c -o sim_test && ./sim_test
 *   ./sim_test [minutes] [seed]
 */
#include <stdio.h>
#include <stdlib.h>
#include "../c/game.c"

static int failures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } } while (0)

static const char *DN[3] = { "easy", "normal", "hard" };

static void census(int t, int *builders, int *soldiers, int *blds_) {
  *builders = *soldiers = *blds_ = 0;
  for (int i = 0; i < MAXU; i++) if (units[i].id && !units[i].dead && units[i].team == t) { if (units[i].type == U_BUILDER) (*builders)++; else (*soldiers)++; }
  for (int i = 0; i < MAXB; i++) if (blds[i].id && !blds[i].dead && blds[i].team == t) (*blds_)++;
}

static void test_map(void) {
  g_init(12345, 1, 0);
  int tree = 0, gold = 0, stone = 0;
  for (int i = 0; i < MAXN; i++) if (nodes[i].id && !nodes[i].dead) { if (nodes[i].type == N_TREE) tree++; else if (nodes[i].type == N_GOLD) gold++; else stone++; }
  int lakeTiles = 0; for (int i = 0; i < MAP_W * MAP_H; i++) lakeTiles += lake[i];
  printf("map: trees %d gold %d stone %d lake tiles %d\n", tree, gold, stone, lakeTiles);
  CHECK(tree > 200 && gold > 15 && stone > 8, "map has enough resources");
  CHECK(lakeTiles > 300, "central lake exists");
  int nb = 0, nu = 0;
  for (int i = 0; i < MAXB; i++) if (blds[i].id) nb++;
  for (int i = 0; i < MAXU; i++) if (units[i].id) nu++;
  CHECK(nb == 2 && nu == 12, "each team starts with a town center, 5 builders and a spearman");
  CHECK(teams[0].popUsed == 6 && teams[0].popCap == 10, "starting population 6/10");

  /* a land route must exist between the two bases */
  Goal g; g.mode = 0; g.gx = base_x[1] - 2; g.gy = base_y[1];
  int ok = findPath(base_x[0] + 2, base_y[0], 0, &g, g.gx, g.gy, 20000);
  printf("path between bases: reached=%d, %d raw waypoints\n", ok, rawn);
  CHECK(ok, "land path exists between bases");
  /* the lake is not walkable, ships can't leave it */
  CHECK(!passLand(36, 36) && passWater(36, 36), "lake tile is water only");
}

static void test_maps(void) {
  static const char *MN[5] = { "lake", "plains", "river", "ponds", "forest" };
  for (int m = 0; m < 5; m++) for (int seed = 1; seed <= 25; seed++) {
    g_set_map(m); g_init(seed * 7919, 1, 0);
    int tree = 0, gold = 0, stone = 0, water = 0, nearTrees = 0;
    for (int i = 0; i < MAXN; i++) if (nodes[i].id && !nodes[i].dead) {
      if (nodes[i].type == N_TREE) { tree++; if (d_hypot(nodes[i].x - base_x[0], nodes[i].y - base_y[0]) < 16) nearTrees++; } else if (nodes[i].type == N_GOLD) gold++; else stone++;
    }
    for (int i = 0; i < MAP_W * MAP_H; i++) water += terrain[i] == T_WATER;
    Goal g; g.mode = 0; g.gx = base_x[1] - 2; g.gy = base_y[1];
    int ok = findPath(base_x[0] + 2, base_y[0], 0, &g, g.gx, g.gy, 30000);
    char msg[160];
    snprintf(msg, sizeof msg, "map %s seed %d: land route between bases", MN[m], seed); CHECK(ok, msg);
    snprintf(msg, sizeof msg, "map %s seed %d: enough resources (t%d g%d s%d)", MN[m], seed, tree, gold, stone); CHECK(tree > 60 && gold >= 12 && stone >= 6 && nearTrees >= 25, msg);
    snprintf(msg, sizeof msg, "map %s seed %d: water amount %d", MN[m], seed, water); CHECK((m == 1 || m == 4) ? water == 0 : water > 120, msg);
    /* nothing may be spawned on water, and both starts must be free land */
    for (int i = 0; i < MAXB; i++) if (blds[i].id) CHECK(terrain[blds[i].y * MAP_W + blds[i].x] != T_WATER, "town center on land");
    if (seed == 1) printf("map %-6s: trees %d gold %d stone %d water %d\n", MN[m], tree, gold, stone, water);
  }
  g_set_map(0);
}

static int countTeam(int team, int *ub, int *bb) {
  int u = 0, b = 0;
  for (int i = 0; i < MAXU; i++) if (units[i].id && !units[i].dead && units[i].team == team) u++;
  for (int i = 0; i < MAXB; i++) if (blds[i].id && !blds[i].dead && blds[i].team == team) b++;
  *ub = u; *bb = b; return 0;
}
static void test_fog(void) {
  int eu, eb, hidden;
  g_set_map(0); g_set_fog(1); g_init(4242, 1, 0);
  g_snapshot();
  countTeam(1, &eu, &eb);
  int seenEnemy = 0, seenOwn = 0;
  for (int i = 0; i < snapCount[0]; i++) { if ((int)snapU[i * U_STRIDE + 2] == 1) seenEnemy++; else seenOwn++; }
  for (int i = 0; i < snapCount[1]; i++) { if ((int)snapB[i * B_STRIDE + 2] == 1) seenEnemy++; else seenOwn++; }
  CHECK(seenEnemy == 0 && seenOwn >= 7, "fog: at the start only our own units and buildings are in the snapshot");
  CHECK(fog[base_y[0] * MAP_W + base_x[0]] == 2 && fog[base_y[1] * MAP_W + base_x[1]] == 0, "fog: our base is visible, the enemy base is unexplored");
  int nn = 0; for (int i = 0; i < MAXN; i++) if (nodes[i].id && !nodes[i].dead) nn++;
  CHECK(snapCount[2] > 0 && snapCount[2] < nn, "fog: unexplored resource nodes are left out of the snapshot");
  /* walk a unit into the enemy base: the enemy appears, and is hidden again (but stays explored) after it leaves */
  Unit *scout = NULL; for (int i = 0; i < MAXU; i++) if (units[i].id && units[i].team == 0) { scout = &units[i]; break; }
  double ox = scout->x, oy = scout->y;
  scout->x = base_x[1] - 4; scout->y = base_y[1];
  g_snapshot(); seenEnemy = 0;
  for (int i = 0; i < snapCount[1]; i++) if ((int)snapB[i * B_STRIDE + 2] == 1) seenEnemy++;
  for (int i = 0; i < snapCount[0]; i++) if ((int)snapU[i * U_STRIDE + 2] == 1) seenEnemy++;
  CHECK(seenEnemy >= 1, "fog: a scout next to the enemy base sees its buildings and units");
  scout->x = ox; scout->y = oy;
  g_snapshot(); int ghostB = 0, enemyU = 0;
  for (int i = 0; i < snapCount[1]; i++) if ((int)snapB[i * B_STRIDE + 2] == 1) ghostB++;
  for (int i = 0; i < snapCount[0]; i++) if ((int)snapU[i * U_STRIDE + 2] == 1) enemyU++;
  CHECK(enemyU == 0, "fog: enemy units disappear again once the scout leaves");
  CHECK(ghostB >= 1, "fog: enemy buildings we saw are remembered after the scout leaves");
  /* destroyed while out of sight: we still remember it; once the ground is seen again it is gone */
  for (int i = 0; i < MAXB; i++) if (blds[i].id && blds[i].team == 1) blds[i].dead = 1;
  g_snapshot(); int stillGhost = 0;
  for (int i = 0; i < snapCount[1]; i++) if ((int)snapB[i * B_STRIDE + 2] == 1) stillGhost++;
  CHECK(stillGhost == ghostB, "fog: a building destroyed out of sight is still remembered");
  scout->x = base_x[1] - 4; scout->y = base_y[1];
  g_snapshot(); int afterLook = 0;
  for (int i = 0; i < snapCount[1]; i++) if ((int)snapB[i * B_STRIDE + 2] == 1) afterLook++;
  CHECK(afterLook == 0, "fog: looking at the spot again clears the memory of a destroyed building");
  scout->x = ox; scout->y = oy; g_snapshot();
  CHECK(fog[base_y[1] * MAP_W + base_x[1] - 4] == 1, "fog: the area the scout passed stays explored");
  /* with fog off everything is delivered and every tile counts as visible */
  g_set_fog(0); g_init(4242, 1, 0); g_snapshot();
  countTeam(0, &hidden, &eb); countTeam(1, &eu, &eb);
  int tot = 0; for (int i = 0; i < MAXU; i++) if (units[i].id && !units[i].dead) tot++;
  int allVisible = 1; for (int i = 0; i < MAP_W * MAP_H; i++) if (fog[i] != 2) allVisible = 0;
  CHECK(snapCount[0] == tot && allVisible, "fog off: the snapshot is complete and the whole map is visible");
}

static unsigned long long digest(void) {
  unsigned long long h = 1469598103934665603ull;
#define MIX(v) do { double d_ = (double)(v); unsigned char *b_ = (unsigned char *)&d_; for (int k_ = 0; k_ < 8; k_++) h = (h ^ b_[k_]) * 1099511628211ull; } while (0)
  for (int i = 0; i < MAXU; i++) if (units[i].id && !units[i].dead) { MIX(units[i].id); MIX(units[i].x); MIX(units[i].y); MIX(units[i].hp); MIX(units[i].state); }
  for (int i = 0; i < MAXB; i++) if (blds[i].id && !blds[i].dead) { MIX(blds[i].id); MIX(blds[i].hp); MIX(blds[i].progress); }
  for (int t = 0; t < 2; t++) for (int k = 0; k < 4; k++) MIX(teams[t].res[k]);
  for (int t = 0; t < 2; t++) for (int k = 0; k < STAT_N; k++) MIX(stats[t][k]);
  MIX(nextId); MIX(winner); MIX(game_time);
#undef MIX
  return h;
}
static void test_save_load(void) {
  g_set_map(2); g_set_fog(1); g_init(555, 1, 3);
  for (int i = 0; i < 20 * 150; i++) g_update(0.05);
  int len = g_save_size();
  static uint8_t keep[4 * 1024 * 1024];
  CHECK(len > 1000 && len < (int)sizeof keep, "save: the save has a sensible size");
  uint8_t *sb = g_save(); for (int i = 0; i < len; i++) keep[i] = sb[i];
  for (int i = 0; i < 20 * 60; i++) g_update(0.05);
  unsigned long long d1 = digest();
  /* load it into a completely different game and play the same minute again: must end up identical */
  g_set_map(0); g_set_fog(0); g_init(999, 2, 0);
  uint8_t *lb = g_load_buf(); for (int i = 0; i < len; i++) lb[i] = keep[i];
  CHECK(g_load(len) == 1, "save: a valid save loads");
  CHECK(fogOn == 1 && mapType == 2 && difficulty == 1, "save: map, fog and difficulty come back with the save");
  for (int i = 0; i < 20 * 60; i++) g_update(0.05);
  CHECK(digest() == d1, "save: playing on after a load gives exactly the same game as playing on without one");
  /* the stats are kept and counted */
  int trained = stats[0][ST_TRAINED] + stats[1][ST_TRAINED], gathered = stats[0][ST_FOOD] + stats[0][ST_WOOD] + stats[1][ST_WOOD];
  CHECK(trained > 5 && gathered > 100, "stats: training and gathering are counted");
  /* bad data is refused and leaves the game alone */
  unsigned long long before = digest();
  lb = g_load_buf(); for (int i = 0; i < len; i++) lb[i] = keep[i];
  lb[0] ^= 0xff; CHECK(g_load(len) == 0 && digest() == before, "save: a wrong header is refused");
  lb[0] ^= 0xff; lb[8] ^= 0x55; CHECK(g_load(len) == 0 && digest() == before, "save: a different layout is refused");
  lb[8] ^= 0x55; CHECK(g_load(len - 1) == 0 && g_load(0) == 0 && digest() == before, "save: a truncated save is refused");
  g_set_map(0); g_set_fog(0);
}

static void test_economy(void) {
  g_init(4242, 1, 0);
  Bld *tc = &blds[0];
  double food0 = teams[0].res[R_FOOD];
  CHECK(queueUnit(tc, U_BUILDER) == ERR_NONE, "can queue a builder");
  CHECK(teams[0].res[R_FOOD] == food0 - 50 && teams[0].popUsed == 7, "builder costs 50 food and 1 pop");
  CHECK(queueUnit(tc, U_SWORDSMAN) == ERR_CANTTRAIN, "town center cannot train swordsmen");
  teams[0].res[R_FOOD] = 0;
  CHECK(queueUnit(tc, U_BUILDER) == ERR_RES0 + R_FOOD, "not enough food is reported");
  teams[0].res[R_FOOD] = 1000;
  for (int i = 0; i < 4; i++) queueUnit(tc, U_BUILDER);
  CHECK(teams[0].popUsed == 10 && queueUnit(tc, U_BUILDER) != ERR_NONE, "population cap blocks further training");
  CHECK(tc->qlen == 4, "four builders are queued");
  double foodBefore = teams[0].res[R_FOOD];
  cancelQueue(tc, 3);
  CHECK(teams[0].popUsed == 9 && teams[0].res[R_FOOD] == foodBefore + 50, "cancelling refunds population and food");

  /* send all builders to chop trees and check wood arrives */
  g_init(4242, 1, 0);
  Node *tr = findNearestNode(R_WOOD, base_x[0], base_y[0], 40);
  CHECK(tr != 0, "tree near base");
  int n = 0;
  for (int i = 0; i < MAXU; i++) if (units[i].id && units[i].team == 0 && units[i].type == U_BUILDER) cmdIds[n++] = units[i].id;
  g_order_gather(n, tr->id);
  double wood0 = teams[0].res[R_WOOD];
  for (int i = 0; i < 1200; i++) g_update(0.1);
  printf("wood after 2 min of 5 builders chopping: %.0f -> %.0f\n", wood0, teams[0].res[R_WOOD]);
  CHECK(teams[0].res[R_WOOD] > wood0 + 100, "builders gather and deliver wood");

  /* build a farm, then farm it */
  g_init(4242, 1, 0);
  Unit *b0 = 0; for (int i = 0; i < MAXU; i++) if (units[i].id && units[i].team == 0 && units[i].type == U_BUILDER) { b0 = &units[i]; break; }
  int fx, fy; CHECK(findPlacement(B_FARM, base_x[0] + 4, base_y[0], 2, 8, 0, &fx, &fy), "found a farm spot");
  cmdIds[0] = b0->id;
  int fid = g_place(0, B_FARM, fx, fy, 1);
  CHECK(fid > 0, "farm placed");
  double food1 = teams[0].res[R_FOOD];
  for (int i = 0; i < 1800; i++) g_update(0.1);
  Bld *farm = B(fid);
  printf("farm built=%d users=%d, food %.0f -> %.0f\n", farm ? farm->done : -1, farm ? farm->nusers : -1, food1, teams[0].res[R_FOOD]);
  CHECK(farm && farm->done, "builder finishes the farm");
  CHECK(teams[0].res[R_FOOD] > food1 + 50, "builder auto-farms after building the farm");
  CHECK(g_place(0, B_PORT, 36, 36, 0) < 0, "port cannot be placed in the water");
}

static void test_combat(void) {
  g_init(7, 1, 0);
  Unit *a = createUnit(0, U_SWORDSMAN, 30.5, 10.5), *b = createUnit(1, U_SWORDSMAN, 31.5, 10.5);
  /* the second pair is placed far outside the first pair's sight range so the fights can't interfere */
  Unit *sp = createUnit(0, U_SPEARMAN, 40.5, 62.5), *cv = createUnit(1, U_CAVALRY, 41.5, 62.5);
  int aid = a->id, bid = b->id, spid = sp->id, cvid = cv->id;
  for (int i = 0; i < 300; i++) g_update(0.1);
  Unit *a2 = U(aid), *b2 = U(bid);
  printf("idle swordsmen fight: %s / %s\n", a2 ? "A alive" : "A dead", b2 ? "B alive" : "B dead");
  CHECK((a2 == 0) != (b2 == 0), "exactly one of two equal swordsmen survives");
  Unit *sp2 = U(spid), *cv2 = U(cvid);
  printf("spearman vs cavalry: spear %s, cav %s\n", sp2 ? "alive" : "dead", cv2 ? "alive" : "dead");
  CHECK(sp2 && !cv2, "spearman beats cavalry");
}

static void run_ai_vs_ai(double minutes, uint32_t seed, int diff) {
  g_init(seed, diff, 3);
  int maxUnits = 0;
  int steps = (int)(minutes * 600);
  for (int i = 0; i < steps; i++) {
    g_update(0.1);
    int n = 0; for (int k = 0; k < MAXU; k++) if (units[k].id && !units[k].dead) n++;
    if (n > maxUnits) maxUnits = n;
    if (i % 3000 == 0) {
      int b0, s0, d0, b1, s1, d1; census(0, &b0, &s0, &d0); census(1, &b1, &s1, &d1);
      printf("[%4.1fm] G: %2db %2ds %2dbld pop %3d/%3d f%.0f w%.0f g%.0f s%.0f | Y: %2db %2ds %2dbld pop %3d/%3d f%.0f w%.0f g%.0f s%.0f\n", game_time / 60,
             b0, s0, d0, teams[0].popUsed, teams[0].popCap, teams[0].res[0], teams[0].res[1], teams[0].res[2], teams[0].res[3],
             b1, s1, d1, teams[1].popUsed, teams[1].popCap, teams[1].res[0], teams[1].res[1], teams[1].res[2], teams[1].res[3]);
    }
    if (winner >= 0) break;
  }
  printf("AI vs AI (%s, seed %u): simulated %.1f min, winner=%s, peak units %d\n", DN[diff], (unsigned)seed, game_time / 60, winner < 0 ? "none" : winner ? "Yellow" : "Green", maxUnits);
  CHECK(maxUnits > 30, "both AIs build armies");
  for (int i = 0; i < MAXU; i++) if (units[i].id && !units[i].dead) {
    CHECK(units[i].x == units[i].x && units[i].y == units[i].y, "unit position is a number");
    CHECK(units[i].x > -1 && units[i].x < MAP_W + 1 && units[i].y > -1 && units[i].y < MAP_H + 1, "unit is on the map");
  }
  for (int t = 0; t < 2; t++) CHECK(teams[t].popUsed >= 0 && teams[t].popUsed <= MAX_POP + 5, "population in range");
}

/* place a building somewhere near the team's start (first free spot); returns its id, or a negative error */
static int tryPlace(int team, int type) {
  int r = -ERR_CANTBUILD;
  for (int dy = -14; dy <= 14; dy += 3) for (int dx = -14; dx <= 14; dx += 3) {
    r = placeBuilding(team, type, (int)base_x[team] + dx, (int)base_y[team] + dy, 0, 0);
    if (r > 0 || (BDEF[type].faction && BDEF[type].faction != teams[team].fac)) return r;
  }
  return r;
}

/* faction units: gating by faction and the catapult's range */
static void test_faction_units(void) {
  g_set_fog(0); g_set_ai_fair(-1);
  g_set_faction(0, 1); g_set_faction(1, 2); g_init(11, 1, 0);
  Bld fake; __builtin_memset(&fake, 0, sizeof fake); fake.id = 1; fake.done = 1;
  for (int t = 0; t < 2; t++) { teams[t].res[R_FOOD] = 2000; teams[t].res[R_WOOD] = 2000; teams[t].res[R_GOLD] = 2000; teams[t].popCap = 100; }
  fake.team = 0; fake.type = B_LODGE; CHECK(queueUnit(&fake, U_WARDEN) == ERR_NONE, "Rangers train wardens at the Hunters Lodge");
  fake.team = 1; fake.qlen = 0; CHECK(queueUnit(&fake, U_WARDEN) == ERR_CANTTRAIN, "Legion cannot train wardens");
  fake.type = B_SIEGECAMP; fake.qlen = 0; CHECK(queueUnit(&fake, U_CATAPULT) == ERR_NONE, "Legion trains catapults at the Siege Camp");
  fake.team = 0; fake.qlen = 0; CHECK(queueUnit(&fake, U_CATAPULT) == ERR_CANTTRAIN, "Rangers cannot train catapults");
  g_set_faction(0, 0); g_set_faction(1, 0); g_init(11, 1, 0); teams[0].res[R_WOOD] = 2000; teams[0].res[R_GOLD] = 2000; teams[0].popCap = 100;
  fake.team = 0; fake.type = B_LODGE; fake.qlen = 0; CHECK(queueUnit(&fake, U_WARDEN) == ERR_CANTTRAIN, "classic mode has no faction units");
  /* who trains what */
  g_set_faction(0, 0); g_set_faction(1, 0); g_init(11, 1, 0);
  teams[0].res[R_FOOD] = 2000; teams[0].res[R_WOOD] = 2000; teams[0].res[R_GOLD] = 2000; teams[0].popCap = 100;
  struct { int bld, unit, ok; const char *what; } rules[] = {
    { B_BARRACKS, U_SWORDSMAN, 1, "Barracks train swordsmen" }, { B_BARRACKS, U_SPEARMAN, 0, "Barracks no longer train spearmen" },
    { B_TOWNCENTER, U_BUILDER, 1, "Town Center trains builders" }, { B_TOWNCENTER, U_SPEARMAN, 1, "Town Center trains spearmen" },
    { B_ARCHERY, U_ARCHER, 1, "Bowyer trains archers" }, { B_STABLE, U_CAVALRY, 1, "Stable trains cavalry" },
    { B_PORT, U_WARSHIP, 1, "Port trains warships" }, { B_WIZARDTOWER, U_WIZARD, 1, "Wizard Tower trains wizards" },
    { B_BLACKSMITH, U_CATAPULT, 0, "Blacksmith no longer trains catapults" }, { B_ARCHERY, U_WARDEN, 0, "Bowyer does not train wardens" },
  };
  for (unsigned i = 0; i < sizeof rules / sizeof *rules; i++) {
    fake.team = 0; fake.type = rules[i].bld; fake.qlen = 0; fake.done = 1;
    CHECK((queueUnit(&fake, rules[i].unit) == ERR_NONE) == rules[i].ok, rules[i].what);
  }
  /* faction buildings can only be placed by their faction */
  g_set_faction(0, 1); g_set_faction(1, 2); g_init(11, 1, 0);
  teams[0].res[R_WOOD] = teams[0].res[R_STONE] = teams[0].res[R_GOLD] = 5000; teams[1].res[R_WOOD] = teams[1].res[R_STONE] = teams[1].res[R_GOLD] = 5000;
  CHECK(tryPlace(0, B_LODGE) > 0, "Rangers can build a Hunters Lodge");
  CHECK(tryPlace(0, B_SIEGECAMP) < 0, "Rangers cannot build a Siege Camp");
  CHECK(tryPlace(1, B_LODGE) < 0, "Legion cannot build a Hunters Lodge");
  CHECK(tryPlace(1, B_SIEGECAMP) > 0, "Legion can build a Siege Camp");
  CHECK(tryPlace(0, B_WIZARDTOWER) > 0 && tryPlace(1, B_WIZARDTOWER) > 0, "both factions can build a Wizard Tower");
  /* fireball: area damage that ignores armor, then a cooldown */
  g_set_faction(0, 0); g_set_faction(1, 0); g_init(12, 1, 0);
  int wz = g_debug_spawn(0, U_WIZARD, 40.5, 40.5);
  int e1 = g_debug_spawn(1, U_SWORDSMAN, 46.5, 40.5), e2 = g_debug_spawn(1, U_SWORDSMAN, 47.2, 41.0), e3 = g_debug_spawn(1, U_SWORDSMAN, 52.5, 40.5);
  U(e1)->state = U(e2)->state = U(e3)->state = S_IDLE;
  double hp1 = U(e1)->hp, hp3 = U(e3)->hp;
  int wzs[1] = { wz }; orderCast(wzs, 1, 46.8, 40.7);
  for (int i = 0; i < 10; i++) g_update(0.1);
  CHECK(U(e1) && U(e1)->hp <= hp1 - 50 && U(e2) && U(e2)->hp < U(e2)->maxHp - 50, "fireball hurts everyone in the blast, through armor");
  CHECK(U(e3) && U(e3)->hp >= hp3 - 12, "fireball spares units outside the blast");
  CHECK(U(wz)->spellCd > 15, "fireball goes on cooldown");
  int before = (int)U(wz)->spellCd; orderCast(wzs, 1, 46.8, 40.7); g_update(0.1);
  CHECK(U(wz)->state != S_CAST && (int)U(wz)->spellCd <= before, "a wizard on cooldown cannot cast");
  g_set_faction(0, 1); g_set_faction(1, 2); g_init(11, 1, 0);
  /* a catapult shells a house from range 7 without being touched */
  g_set_faction(0, 2); g_set_faction(1, 1); g_init(11, 1, 0);
  Bld *h = createBuilding(1, B_HOUSE, 36, 12, 1); CHECK(h != 0, "target house");
  double hp0 = h ? h->hp : 0; int hid = h ? h->id : 0;
  int cid = g_debug_spawn(0, U_CATAPULT, 36.5 - 7, 12.5); CHECK(cid > 0, "catapult spawned");
  int one[1] = { cid }; orderAttack(one, 1, hid);
  for (int i = 0; i < 300; i++) g_update(0.1);
  Bld *h2 = B(hid);
  CHECK(h2 == 0 || h2->hp < hp0 - 20, "catapult damages buildings from a distance");
  g_set_faction(0, 0); g_set_faction(1, 0);
}

/* a fair AI must still find and destroy a building that is far from the Green base and out of its sight */
static void run_hidden(int diff) {
  g_set_fog(1); g_set_ai_fair(1);
  g_init(777, diff, 2);
  int hx = 30, hy = 6, ok = 0;
  for (int t = 0; t < 40 && !ok; t++) { Bld *h = createBuilding(0, B_HOUSE, hx, hy + t, 1); if (h) { ok = 1; hy += t; } }
  CHECK(ok, "hidden house placed");
  int knew0 = 0; for (int i = 0; i < MAXB; i++) if (blds[i].id && blds[i].team == 0 && aiKnownB[1][i] == blds[i].id) knew0++;
  for (int i = 0; i < 45 * 600 && winner < 0; i++) g_update(0.1);
  printf("fair AI vs passive player with a hidden house: ended %.1f min, winner=%s (known at start: %d)\n", game_time / 60, winner < 0 ? "none" : winner ? "Yellow" : "Green", knew0);
  CHECK(winner == 1, "the fair AI scouts out and destroys every building");
  g_set_ai_fair(-1); g_set_fog(0);
}

static void run_passive(int diff) {
  g_init(777, diff, 2);                       /* only Yellow is AI-controlled; Green does nothing */
  double firstPush = -1;
  for (int i = 0; i < 40 * 600 && winner < 0; i++) {
    g_update(0.1);
    if (firstPush < 0) for (int k = 0; k < MAXU; k++) if (units[k].id && units[k].team == 1 && UDEF[units[k].type].soldier && units[k].x < 40) { firstPush = game_time; break; }
  }
  printf("passive player vs %s AI: ended %.1f min, winner=%s, first push past midline %.1f min\n", DN[diff], game_time / 60, winner < 0 ? "none" : winner ? "Yellow" : "Green", firstPush / 60);
  CHECK(winner == 1, "AI destroys a passive player");
}

/* factions: classic is neutral; Rangers/Legion change unit stats and prices, deterministically */
static void test_factions(void) {
  /* classic is neutral: stats and prices equal the unit table */
  g_set_faction(0, 0); g_set_faction(1, 0); g_init(5, 1, 0);
  CHECK(g_get_faction(0) == 0 && g_get_faction(1) == 0, "default factions are classic");
  Unit *c = createUnit(0, U_SWORDSMAN, 30.5, 10.5);
  CHECK(c->maxHp == UDEF[U_SWORDSMAN].hp, "classic swordsman has base health");
  /* Rangers (1) vs Legion (2) */
  g_set_faction(0, 1); g_set_faction(1, 2); g_init(5, 1, 0);
  CHECK(g_get_faction(0) == 1 && g_get_faction(1) == 2, "factions are stored per team");
  Unit *sw0 = createUnit(0, U_SWORDSMAN, 30.5, 10.5), *sw1 = createUnit(1, U_SWORDSMAN, 31.5, 10.5);
  CHECK(sw1->maxHp > sw0->maxHp && sw1->maxHp == UDEF[U_SWORDSMAN].hp * 1.15, "Legion swordsmen are tougher than Ranger ones");
  teams[0].res[R_WOOD] = 1000; teams[0].res[R_GOLD] = 1000; teams[0].popCap = 100;
  teams[1].res[R_WOOD] = 1000; teams[1].res[R_GOLD] = 1000; teams[1].popCap = 100;
  double w = teams[0].res[R_WOOD];
  Bld fake; __builtin_memset(&fake, 0, sizeof fake); fake.id = 1; fake.team = 0; fake.type = B_ARCHERY; fake.done = 1;
  CHECK(queueUnit(&fake, U_ARCHER) == ERR_NONE, "Rangers can queue an archer");
  CHECK(w - teams[0].res[R_WOOD] == 34, "Ranger archer costs 15% less wood (40 -> 34)");
  w = teams[1].res[R_WOOD]; fake.team = 1; fake.qlen = 0;
  CHECK(queueUnit(&fake, U_ARCHER) == ERR_NONE && w - teams[1].res[R_WOOD] == 40, "Legion archer keeps the base price");
  /* determinism: same seed and factions give the same game */
  g_set_faction(0, 2); g_set_faction(1, 1); g_init(9, 1, 3);
  for (int i = 0; i < 600; i++) g_update(0.1);
  unsigned long long d1 = digest();
  g_set_faction(0, 2); g_set_faction(1, 1); g_init(9, 1, 3);
  for (int i = 0; i < 600; i++) g_update(0.1);
  CHECK(d1 == digest(), "faction games are deterministic");
  /* save/load keeps factions */
  g_set_faction(0, 1); g_set_faction(1, 2); g_init(3, 1, 2);
  int n = g_save_size(); g_save();
  g_set_faction(0, 0); g_set_faction(1, 0); g_init(4, 1, 2);
  CHECK(g_load(n) && g_get_faction(0) == 1 && g_get_faction(1) == 2, "save/load keeps the factions");
  g_set_faction(0, 0); g_set_faction(1, 0);
}

int main(int argc, char **argv) {
  double minutes = argc > 1 ? atof(argv[1]) : 25;
  uint32_t seed = argc > 2 ? (uint32_t)atoi(argv[2]) : 12345;
  test_map();
  test_maps();
  test_fog();
  test_save_load();
  test_factions();
  test_economy();
  test_combat();
  run_ai_vs_ai(minutes, seed, 1);
  run_ai_vs_ai(minutes, seed + 1, 2);
  for (int d = 0; d < 3; d++) run_passive(d);
  test_faction_units();
  run_hidden(2);
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL OK\n", failures);
  return failures ? 1 : 0;
}
