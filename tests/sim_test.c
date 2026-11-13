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

int main(int argc, char **argv) {
  double minutes = argc > 1 ? atof(argv[1]) : 25;
  uint32_t seed = argc > 2 ? (uint32_t)atoi(argv[2]) : 12345;
  test_map();
  test_maps();
  test_economy();
  test_combat();
  run_ai_vs_ai(minutes, seed, 1);
  run_ai_vs_ai(minutes, seed + 1, 2);
  for (int d = 0; d < 3; d++) run_passive(d);
  printf(failures ? "\n%d FAILURE(S)\n" : "\nALL OK\n", failures);
  return failures ? 1 : 0;
}
