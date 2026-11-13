/* Public API of the C simulation core (c/game.c), as seen from the SDL2 front end.
 *
 * game.c is linked as a separate object and only these functions are visible, which is the same boundary the
 * WebAssembly build exposes to js/game_wasm.js. The front end never reads simulation internals: it calls
 * g_snapshot() once per frame and reads the flat float arrays below.
 */
#ifndef SIM_H
#define SIM_H

#include <stdint.h>

void g_init(uint32_t seed, int diff, int aiMask);   /* diff 0..2, aiMask bit i = team i is AI controlled */
void g_set_map(int m);   /* 0 lake, 1 plains, 2 river, 3 ponds, 4 forest - applies to the next g_init */
void g_update(double dt);

/* orders operate on the unit ids previously written into g_idbuf() */
int *g_idbuf(void);
int g_idbuf_len(void);
void g_order_move(int n, double x, double y, int attackMove);
void g_order_stop(int n);
void g_order_attack(int n, int target);
void g_order_build(int n, int bid);
void g_order_gather(int n, int target);

int g_place(int team, int type, int x, int y, int n);      /* new building id, or a negative error code */
int g_can_place(int type, int x, int y, int margin);
int g_queue_unit(int bid, int type);                         /* 0 or an error code (see ERR_* below) */
int g_queue_tech(int bid, int tech);
void g_cancel_queue(int bid, int i);
int g_tech_pending(int team, int tech);
void g_set_rally(int bid, double x, double y, int target);
int g_winner(void);

void g_snapshot(void);
int *g_snap_counts(void);      /* units, buildings, nodes, projectiles, effects, events */
float *g_snap_units(void);
float *g_snap_blds(void);
float *g_snap_nodes(void);
float *g_snap_projs(void);
float *g_snap_effs(void);
float *g_snap_events(void);
float *g_snap_teams(void);
uint8_t *g_terrain(void);
uint8_t *g_lake(void);
uint8_t *g_shade(void);
int g_layout(int which);

/* snapshot layout; checked against g_layout() at start-up */
#define U_STRIDE 20
#define B_STRIDE 34
#define N_STRIDE 6
#define P_STRIDE 8
#define E_STRIDE 9
#define EV_STRIDE 6
#define T_STRIDE 8

/* capacity limits of the core (c/game.c) */
#define SIM_MAXU 640
#define SIM_MAXB 256
#define SIM_MAXN 1300
#define SIM_MAXP 256
#define SIM_MAXE 640
#define SIM_MAXEV 96
#define SIM_MAXID 65536

enum { ST_IDLE = 0, ST_MOVE, ST_ATTACKMOVE, ST_ATTACK, ST_GATHER, ST_BUILD };
enum { SUBST_NONE = 0, SUBST_TORES, SUBST_WORK, SUBST_TODROP };
enum { TILE_GRASS = 0, TILE_WATER = 1, TILE_SAND = 2 };
enum { RES_FOOD = 0, RES_WOOD, RES_GOLD, RES_STONE };
enum { EVT_ALERT = 1, EVT_BUILT, EVT_DESTROYED, EVT_TECH, EVT_MARCH };

#endif
