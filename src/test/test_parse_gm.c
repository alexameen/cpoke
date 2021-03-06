/* -*- mode: c; -*- */

/* ========================================================================== */

#include "ext/jsmn.h"
#include "ext/jsmn_iterator.h"
#include "ext/uthash.h"
#include "parse_gm.h"
#include "ptypes.h"
#include "util/json_util.h"
#include "util/macros.h"
#include "util/test_util.h"
#include <pcre.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* -------------------------------------------------------------------------- */

#include "../data/test/parse_gm_0.h"
#include "../data/test/parse_gm_1.h"
#include "../data/test/parse_gm_2.h"


/* -------------------------------------------------------------------------- */

#define parse_gm_json_str( STR_NAME, TOKEN_LIST_NAME, COUNT_NAME )            \
  jsmntok_t * TOKEN_LIST_NAME = NULL;                                         \
  jsmn_parser_t STR_NAME ## _PARSER;                                          \
  memset( & STR_NAME ## _PARSER, 0, sizeof( jsmn_parser_t ) );                \
  size_t COUNT_NAME = 0;                                                      \
  long STR_NAME ## _PARSER_RSL = jsmn_parse_realloc( & STR_NAME ## _PARSER,   \
                                                     ( STR_NAME ),            \
                                                     ( STR_NAME ## _LEN ),    \
                                                     &( TOKEN_LIST_NAME ),    \
                                                     &( COUNT_NAME )          \
                                                   );                         \
  COUNT_NAME = STR_NAME ## _PARSER_RSL


/* -------------------------------------------------------------------------- */

  static bool
test_parse_pvp_charged_move( void )
{
  parse_gm_json_str( JSON1, tokens, tokens_cnt );
  assert( 0 < JSON1_PARSER_RSL );
  size_t buffer_len = JSON1_LEN;

  jsmnis_t iter_stack;
  memset( &iter_stack, 0, sizeof( jsmnis_t ) );
  jsmnis_init( &iter_stack, tokens, tokens_cnt, 5 );
  jsmnis_push( &iter_stack, 0 );
  jsmntok_t * key = NULL;
  jsmntok_t * val = NULL;
  jsmni_find_key_seq( JSON1,
                      jsmnis_curr( &iter_stack ),
                      &key,
                      "templateId",
                      &val,
                      0
                    );

  pvp_charged_move_t move;
  char *             name = NULL;
  memset( &move, 0, sizeof( pvp_charged_move_t ) );

  uint16_t move_id = parse_pvp_charged_move( JSON1, &iter_stack, &name, &move );

  jsmnis_free( &iter_stack );

  expect( move_id == move.move_id );
  expect( move_id == 309 );
  expect( strcmp( name, "MIRROR_SHOT" ) == 0 );
  expect( move.type    == STEEL );
  expect( move.power   == 35 );
  expect( move.energy  == 35 );
  expect( move.is_fast == false );
  expect( move.buff.atk_buff.target  == 1 );
  expect( move.buff.atk_buff.amount  == 1 );
  expect( move.buff.atk_buff.debuffp == 1 );
  expect( move.buff.chance           == bc_0300 );

  free( name );
  free( tokens );

  return true;
}


/* -------------------------------------------------------------------------- */

  static bool
test_parse_pvp_fast_move( void )
{
  parse_gm_json_str( JSON2, tokens, tokens_cnt );
  assert( 0 < JSON2_PARSER_RSL );
  size_t buffer_len = JSON2_LEN;

  jsmnis_t iter_stack;
  memset( &iter_stack, 0, sizeof( jsmnis_t ) );
  jsmnis_init( &iter_stack, tokens, tokens_cnt, 5 );
  jsmnis_push( &iter_stack, 0 );
  jsmntok_t * key = NULL;
  jsmntok_t * val = NULL;
  jsmni_find_key_seq( JSON2,
                      jsmnis_curr( &iter_stack ),
                      &key,
                      "templateId",
                      &val,
                      0
                    );

  pvp_fast_move_t move;
  char *             name = NULL;
  memset( & move, 0, sizeof( pvp_fast_move_t ) );

  uint16_t move_id = parse_pvp_fast_move( JSON2, &iter_stack, &name, & move );

  jsmnis_free( &iter_stack );

  expect( move_id == move.move_id );
  expect( move_id == 320 );
  expect( strcmp( name, "CHARM" ) == 0 );
  expect( move.type    == FAIRY );
  expect( move.power   == 16 );
  expect( move.energy  == 6 );
  expect( move.is_fast == true );
  expect( move.turns   == 2 );

  free( name );
  free( tokens );

  return true;
}


/* -------------------------------------------------------------------------- */

  static bool
test_parse_pvp_moves( void )
{
  return true;
}


/* -------------------------------------------------------------------------- */

  static bool
test_regex_patterns( void )
{
  /* Compile Regex patterns for templateIds */
  gm_regexes_t regs;
  int          rsl = gm_regexes_init( &regs );
  assert( rsl == 0 );

  /* FIXME test pokemon */
  char test_str1[] = "COMBAT_V0013_MOVE_WRAP";
  char test_str2[] = "COMBAT_V0062_MOVE_ANCIENT_POWER";
  char test_str3[] = "COMBAT_V0200_MOVE_FURY_CUTTER_FAST";

  expect( pcre_exec( regs.tmpl_pvp_move, NULL, test_str1, strlen( test_str1 ), 0, 0, 0, 0 ) == 0 );
  expect( pcre_exec( regs.tmpl_pvp_move, NULL, test_str2, strlen( test_str2 ), 0, 0, 0, 0 ) == 0 );
  expect( pcre_exec( regs.tmpl_pvp_move, NULL, test_str3, strlen( test_str3 ), 0, 0, 0, 0 ) == 0 );

  expect( pcre_exec( regs.tmpl_pvp_fast, NULL, test_str1, strlen( test_str1 ), 0, 0, 0, 0 ) != 0 );
  expect( pcre_exec( regs.tmpl_pvp_fast, NULL, test_str2, strlen( test_str2 ), 0, 0, 0, 0 ) != 0 );
  expect( pcre_exec( regs.tmpl_pvp_fast, NULL, test_str3, strlen( test_str3 ), 0, 0, 0, 0 ) == 0 );

  expect( pcre_exec( regs.tmpl_mon, NULL, test_str1, strlen( test_str1 ), 0, 0, 0, 0 ) != 0 );
  expect( pcre_exec( regs.tmpl_mon, NULL, test_str2, strlen( test_str2 ), 0, 0, 0, 0 ) != 0 );
  expect( pcre_exec( regs.tmpl_mon, NULL, test_str3, strlen( test_str3 ), 0, 0, 0, 0 ) != 0 );

  gm_regexes_free( &regs );

  return true;
}


/* -------------------------------------------------------------------------- */

  static bool
test_parse_gm_type( void )
{
  /* Try parsing on a simple, one token JSON string */
  const char fairy_str[] = "POKEMON_TYPE_FAIRY";
  jsmntok_t  tok = {
    .type  = JSMN_STRING,
    .start = 0,
    .end   = array_size( fairy_str ),
    .size  = 0
  };
  expect( parse_gm_type( fairy_str, &tok ) == FAIRY );

  /* Try parsing on a full pokemon's GM entry */
  parse_gm_json_str( JSON0, tokens, tokens_cnt );
  assert( 0 < JSON0_PARSER_RSL );
  size_t buffer_len = JSON0_LEN;

  int type_idx = json_find( JSON0,
                            tokens,
                            jsoneq_str_p,
                            (void *) "type1",
                            tokens_cnt,
                            0
                          );
  type_idx++; /* Current index is the key, the next is the value. */

  assert( jsoneq_str( JSON0, tokens + type_idx, "POKEMON_TYPE_GRASS" ) );
  ptype_t type1 = parse_gm_type( JSON0, tokens + type_idx );
  expect( type1 == GRASS );

  type_idx += 2; /* Type 2's value is 2 tokens forward in this case. */
  assert( jsoneq_str( JSON0, tokens + type_idx, "POKEMON_TYPE_POISON" ) );
  ptype_t type2 = parse_gm_type( JSON0, tokens + type_idx );
  expect( type2 == POISON );

  free( tokens );

  return true;
}


/* -------------------------------------------------------------------------- */

  static bool
test_parse_gm_dex_num( void )
{
  const char tid_str[] = "V0001_POKEMON_BULBASAUR_NORMAL";
  jsmntok_t tid_tok = {
    .type  = JSMN_STRING,
    .start = 0,
    .end   = array_size( tid_str ),
    .size  = 0
  };
  uint16_t dex_num = parse_gm_dex_num( tid_str, &tid_tok );
  expect( dex_num == 1 );
  return true;
}


/* -------------------------------------------------------------------------- */

  static bool
test_parse_gm_stats( void )
{
  const char stats_str[] = R"RAW_JSON(
      { "baseStamina": 128, "baseAttack": 118, "baseDefense": 111 }
    )RAW_JSON";
  size_t stats_str_len = array_size( stats_str );
  jsmntok_t tokens[7];
  memset( tokens, 0, sizeof( jsmntok_t ) * 7 );
  jsmn_parser_t jparser;
  memset( &jparser, 0, sizeof( jparser ) );
  jsmn_init( &jparser );
  int tokens_cnt = jsmn_parse( &jparser, stats_str, stats_str_len, tokens, 7 );
  jsmnis_t iter_stack;
  memset( &iter_stack, 0, sizeof( jsmnis_t ) );
  jsmnis_init( &iter_stack, tokens, tokens_cnt, 3 );
  jsmnis_push( &iter_stack, 0 );

  stats_t stats = parse_gm_stats( stats_str, &iter_stack );
  expect( stats.stamina == 128 );
  expect( stats.attack == 118 );
  expect( stats.defense == 111 );

  jsmnis_free( &iter_stack );

  return true;
}


/* -------------------------------------------------------------------------- */

  static bool
test_parse_gm_buff( void )
{
  const char buff_str[] = R"RAW_JSON(
      { "targetAttackStatStageChange": -1, "buffActivationChance": 0.3 }
    )RAW_JSON";

  size_t buff_str_len = array_size( buff_str );
  jsmntok_t tokens[5];
  memset( tokens, 0, sizeof( jsmntok_t ) * 5 );
  jsmn_parser_t jparser;
  memset( &jparser, 0, sizeof( jsmn_parser_t ) );
  jsmn_init( &jparser );
  int tokens_cnt = jsmn_parse( &jparser, buff_str, buff_str_len, tokens, 5 );
  assert( tokens_cnt == 5 );
  jsmni_t iter;
  memset( &iter, 0, sizeof( jsmni_t ) );
  jsmni_init( &iter, tokens, tokens_cnt, 0 );

  buff_t buff = parse_gm_buff( buff_str, &iter );
  expect( buff.atk_buff.target  == 1 );
  expect( buff.atk_buff.amount  == 1 );
  expect( buff.atk_buff.debuffp == 1 );
  expect( buff.def_buff.target  == 0 );
  expect( buff.def_buff.amount  == 0 );
  expect( buff.def_buff.debuffp == 0 );
  expect( buff.chance == bc_0300 );


  const char buff_str2[] = R"RAW_JSON(
      { "attackerAttackStatStageChange": -1,
        "attackerDefenseStatStageChange": -1,
        "buffActivationChance": 1.0
      }
    )RAW_JSON";

  size_t buff_str2_len = array_size( buff_str2 );
  jsmntok_t tokens2[7];
  memset( tokens2, 0, sizeof( jsmntok_t ) * 7 );
  jsmn_parser_t jparser2;
  memset( &jparser2, 0, sizeof( jsmn_parser_t ) );
  jsmn_init( &jparser2 );

  tokens_cnt = jsmn_parse( &jparser2, buff_str2, buff_str2_len, tokens2, 7 );
  assert( tokens_cnt == 7 );

  jsmni_t iter2;
  memset( &iter2, 0, sizeof( jsmni_t ) );
  jsmni_init( &iter2, tokens2, tokens_cnt, 0 );

  buff = parse_gm_buff( buff_str2, &iter2 );
  expect( buff.atk_buff.target  == 0 );
  expect( buff.atk_buff.amount  == 1 );
  expect( buff.atk_buff.debuffp == 1 );
  expect( buff.def_buff.target  == 0 );
  expect( buff.def_buff.amount  == 1 );
  expect( buff.def_buff.debuffp == 1 );
  expect( buff.chance == bc_1000 );

  return true;
}


/* -------------------------------------------------------------------------- */

  static bool
test_lookup_move_id( void )
{
  store_move_t mirror_shot = {
    .name       = "MIRROR_SHOT",
    .type       = STEEL,
    .is_fast    = false,
    .move_id    = 309,
    .cooldown   = 0,
    .pve_power  = 0,
    .pve_energy = 0,
    .pvp_power  = 35,
    .pvp_energy = 35,
    .buff       = {
      .atk_buff = { .target = 1, .debuffp = 1, .amount = 1 },
      .def_buff = { 0, 0, 0 },
      .chance   = bc_0300
    },
    .hh_name    = 0,
    .hh_move_id = 0
  };

  store_move_t charm = {
    .name       = "CHARM",
    .type       = FAIRY,
    .is_fast    = true,
    .move_id    = 320,
    .cooldown   = 2,
    .pve_power  = 0,
    .pve_energy = 0,
    .pvp_power  = 16,
    .pvp_energy = 6,
    .buff       = NO_BUFF,
    .hh_name    = 0,
    .hh_move_id = 0
  };

  store_move_t * moves_by_name = NULL;
  HASH_ADD_KEYPTR( hh_name,
                   moves_by_name,
                   mirror_shot.name,
                   strlen( mirror_shot.name ),
                   & mirror_shot
                 );
  HASH_ADD_KEYPTR( hh_name,
                   moves_by_name,
                   charm.name,
                   strlen( charm.name ),
                   & charm
                 );

  expect( lookup_move_id( moves_by_name, mirror_shot.name )
          == mirror_shot.move_id
        );

  /* Confirm that HASH_FIND checked the actual string, not the address */
  char * mirror_shot_str = strdup( "MIRROR_SHOT" );
  expect( lookup_move_id( moves_by_name, mirror_shot_str )
          == mirror_shot.move_id
          );
  free( mirror_shot_str );
  mirror_shot_str = NULL;

  expect( lookup_move_id( moves_by_name, charm.name )
          == charm.move_id
        );
  /* Cleanup */
  HASH_CLEAR( hh_name, moves_by_name );
  return true;
}


/* -------------------------------------------------------------------------- */

  bool
test_parse_gm( void )
{
  bool rsl = true;
  rsl &= do_test( regex_patterns );
  rsl &= do_test( parse_gm_type );
  rsl &= do_test( parse_gm_dex_num );
  rsl &= do_test( parse_gm_stats );
  rsl &= do_test( parse_gm_buff );
  rsl &= do_test( parse_pvp_charged_move );
  rsl &= do_test( parse_pvp_fast_move );
  rsl &= do_test( lookup_move_id );
  return rsl;
}


/* -------------------------------------------------------------------------- */

#ifdef MK_TEST_BINARY
int
main( int argc, char * argv[], char ** envp )
{
  return test_parse_gm() ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif


/* -------------------------------------------------------------------------- */



/* ========================================================================== */

/* vim: set filetype=c : */
