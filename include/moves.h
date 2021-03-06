/* -*- mode: c; -*- */

#ifndef _MOVES_H
#define _MOVES_H

/* ========================================================================= */

#include "ext/uthash.h"
#include "hash.h"
#include "ptypes.h"
#include "store.h"
#include <stdbool.h>
#include <stdint.h>


/* ------------------------------------------------------------------------- */

typedef enum packed {
  bc_1000, bc_0500, bc_0300, bc_0125, bc_0100, bc_0000
} buff_chance_t; /* 3 bits used, 4 total */


/* ------------------------------------------------------------------------- */

struct stat_buff_s {
  uint8_t target  : 1; /* 0 --> self; 1 --> opponent */
  uint8_t debuffp : 1; /* 1 --> Debuff --> ( - amount ) */
  uint8_t amount  : 2;
} packed;
typedef struct stat_buff_s  stat_buff_t;


  static inline const_fn int8_t
decode_stat_buff( stat_buff_t buff )
{
  return buff.debuffp ? buff.amount : ( - buff.amount );
}


/* ------------------------------------------------------------------------- */

struct buff_s {
  buff_chance_t chance;   /* I think 3 bits */
  stat_buff_t   atk_buff;
  stat_buff_t   def_buff;
} packed; /* 11 bits used, 16 total */
typedef struct buff_s  buff_t;


static const buff_t NO_BUFF = { .chance = bc_0000,
  .atk_buff = { .target = 0, .debuffp = 0, .amount = 0 },
  .def_buff = { .target = 0, .debuffp = 0, .amount = 0 }
};



/* ------------------------------------------------------------------------- */

//static const double BUFF_MOD[] = {
//  0.5000000, 0.5714286, 0.6666667, 0.8000000,  /* Debuff */
//  1.0000000,
//  1.2500000, 1.5000000, 1.7500000, 2.0000000   /*  Buff  */
//};

static const float BUFF_MOD[] = {
  0.50000, 0.57143, 0.66667, 0.80000,  /* Debuff */
  1.00000,
  1.25000, 1.50000, 1.75000, 2.00000   /*  Buff  */
};

typedef enum packed {
  B_4_8, B_4_7, B_4_6, B_4_5,
  B_4_4,
  B_5_4, B_6_4, B_7_4, B_8_4
} buff_level_t;


/* ------------------------------------------------------------------------- */

struct buff_state_s {
  buff_level_t atk_buff_lv;
  buff_level_t def_buff_lv;
} packed;
typedef struct buff_state_s  buff_state_t;

static const buff_state_t NO_BUFF_STATE = {
  .atk_buff_lv = B_4_4, .def_buff_lv = B_4_4
};

#define get_buff_mod( buff_level )  ( BUFF_MOD[( buff_level )] )

void apply_buff( buff_state_t * buff_state, buff_t buff );


/* ------------------------------------------------------------------------- */

/**
 * As of [2020-08-22] there are 342 moves
 */
struct base_move_s {
  uint16_t move_id;
  ptype_t  type;
  bool     is_fast : 1;
  uint8_t  power;   /* NOTE: These values are not always the same for PvP/PvE */
  uint8_t  energy;  /* NOTE: These values are not always the same for PvP/PvE */
} packed;
typedef struct base_move_s  base_move_t;

static const base_move_t NO_MOVE = {
  .move_id = 0,
  .type    = PT_NONE,
  .is_fast = false,
  .power   = 0,
  .energy  = 0
};

#define as_base_move( child_move )                                            \
  ( (base_move_t) { .move_id = ( child_move ).move_id,                        \
                    .type    = ( child_move ).type,                           \
                    .is_fast = ( child_move ).is_fast,                        \
                    .power   = ( child_move ).power,                          \
                    .energy  = ( child_move ).energy,                         \
                  } )


/* ------------------------------------------------------------------------- */

struct pve_move_s {
  base_move_t;           /* Inherit */
  uint16_t    cooldown;  /* ms */
} packed;
typedef struct pve_move_s  pve_move_t;

static const pve_move_t NO_MOVE_PVE = {
  NO_MOVE,
  .cooldown = 0
};

int pve_move_from_store( store_t    * store,
                         uint16_t     move_id,
                         pve_move_t * move
                       );


/* ------------------------------------------------------------------------- */

struct pvp_charged_move_s {
  base_move_t;       /* Inherit */
  buff_t      buff;  /* A pointer could be used, but it costs more */
} packed;
typedef struct pvp_charged_move_s  pvp_charged_move_t;

static const pvp_charged_move_t NO_MOVE_PVP_CHARGED = {
  NO_MOVE,
  .buff    = NO_BUFF
};

int pvp_charged_move_from_store( store_t            * store,
                                 uint16_t             move_id,
                                 pvp_charged_move_t * move
                               );


/* ------------------------------------------------------------------------- */

struct pvp_fast_move_s {
  base_move_t;            /* Inherit */
  uint8_t     turns : 2;  /* 1-4 */
} packed;
typedef struct pvp_fast_move_s  pvp_fast_move_t;

static const pvp_fast_move_t NO_MOVE_PVP_FAST = { NO_MOVE, .turns = 0 };

int pvp_fast_move_from_store( store_t         * store,
                              uint16_t          move_id,
                              pvp_fast_move_t * move
                            );


/* ------------------------------------------------------------------------- */

/**
 * Meant to be held in data storage for reference.
 * Should be able to construct both pvp/pve moves from this data.
 */
struct store_move_s {
  char *         name;
  ptype_t        type;
  bool           is_fast : 1;
  uint16_t       move_id;
  uint16_t       cooldown;     /* PvE seconds, PvP turns ( Fast Move ) */
  uint8_t        pve_power;
  uint8_t        pvp_power;
  uint8_t        pve_energy;
  uint8_t        pvp_energy;
  buff_t         buff;
  UT_hash_handle hh_name;
  UT_hash_handle hh_move_id;
};
typedef struct store_move_s  store_move_t;

static const store_move_t NO_MOVE_STORE = {
  .name       = NULL,
  .type       = PT_NONE,
  .is_fast    = false,
  .move_id    = 0,
  .cooldown   = 1,
  .pve_power  = 1,
  .pvp_power  = 1,
  .pve_energy = 1,
  .pvp_energy = 1,
  .buff       = NO_BUFF,
  .hh_name    = HH_NULL,
  .hh_move_id = HH_NULL
};


/* ------------------------------------------------------------------------- */

  static inline store_key_t
move_store_key( store_move_t * move )
{
  return (store_key_t) {
    .key_type = STORE_NUM,
    .val_type = STORE_MOVE,
    .data_h0  = move->move_id,
    .data_h1  = 0
  };
}

  static inline store_key_t
move_id_store_key( uint16_t move_id )
{
  return (store_key_t) {
    .key_type = STORE_NUM,
    .val_type = STORE_MOVE,
    .data_h0  = max( move_id, - move_id ),  /* Catch negatives from legacy */
    .data_h1  = 0
  };
}


/* ------------------------------------------------------------------------- */

  static inline pve_move_t
pve_move_from_store_move( store_move_t * stored )
{
  assert( stored != NULL );
  pve_move_t move = {
    .move_id  = stored->move_id,
    .type     = stored->type,
    .is_fast  = stored->is_fast,
    .power    = stored->pve_power,
    .energy   = stored->pve_energy,
    .cooldown = stored->cooldown
  };
  return move;
}

  static inline pvp_charged_move_t
pvp_charged_move_from_store_move( store_move_t * stored )
{
  assert( stored != NULL );
  pvp_charged_move_t move = {
    .move_id = stored->move_id,
    .type    = stored->type,
    .is_fast = stored->is_fast,
    .power   = stored->pvp_power,
    .energy  = stored->pvp_energy,
    .buff    = stored->buff
  };
  return move;
}

  static inline pvp_fast_move_t
pvp_fast_move_from_store_move( store_move_t * stored )
{
  assert( stored != NULL );
  pvp_fast_move_t move = {
    .move_id = stored->move_id,
    .type    = stored->type,
    .is_fast = stored->is_fast,
    .power   = stored->pvp_power,
    .energy  = stored->pvp_energy,
    .turns   = stored->cooldown
  };
  return move;
}


/* ------------------------------------------------------------------------- */

int fprint_buff( FILE * stream, const buff_t * buff );
#define print_buff( BUFF )  fprint_buff( stdout, ( BUFF ) )

int fprint_buff_json( FILE * stream, const buff_t * buff );
#define print_buff_json( BUFF )  fprint_buff_json( stdout, ( BUFF ) )

int fprint_buff_c( FILE * stream, const buff_t * buff );
#define print_buff_c( BUFF )  fprint_buff_c( stdout, ( BUFF ) )


/* ------------------------------------------------------------------------- */

int fprint_store_move( FILE * stream, const store_move_t * move );
#define print_store_move( MOVE )  fprint_store_move( stdout, ( MOVE ) )

int fprint_store_move_json( FILE * stream, const store_move_t * move );
#define print_store_move_json( MOVE )  fprint_store_move_json( stdout, ( MOVE ) )

int fprint_store_move_c( FILE * stream, const store_move_t * move );
#define print_store_move_c( MOVE )  fprint_store_move_c( stdout, ( MOVE ) )


/* ------------------------------------------------------------------------- */



/* ========================================================================= */

#endif /* moves.h */

/* vim: set filetype=c : */
