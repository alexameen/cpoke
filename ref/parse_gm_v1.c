/* -*- mode: c; -*- */

/* ========================================================================= */

#define GM_GLOBAL_STORE

#include "parse_gm.h"
#include "gm_store.h"
#include "ext/jsmn.h"
#include "ext/jsmn_iterator.h"
#include "util/bits.h"
#include "util/json_util.h"
#include "util/jsmn_iterator_stack.h"
#include "pokedex.h"
#include <stdio.h>
#include <stdlib.h>
#include "ext/uthash.h"
#include <pcre.h>


/* ------------------------------------------------------------------------- */

struct reg_pat_pair_s { pcre ** reg; const char * pat; };

  int
gm_regexes_init( gm_regexes_t * grs )
{
  assert( grs != NULL );
  int          rsl     = 0;
  const char * err_msg = NULL;
  int          err_idx = 0;

  struct reg_pat_pair_s rps[] = {
    { & grs->tmpl_mon,      tmpl_mon_pat      },
    { & grs->tmpl_shadow,   tmpl_shadow_pat   },
    { & grs->tmpl_pure,     tmpl_pure_pat     },
    { & grs->tmpl_norm,     tmpl_norm_pat     },
    { & grs->tmpl_pvp_move, tmpl_pvp_move_pat },
    { & grs->tmpl_pvp_fast, tmpl_pvp_fast_pat }
  };

  for ( uint8_t i = 0; i < array_size( rps ); i++ )
    {
      ( * rps[i].reg ) =
        pcre_compile2( rps[i].pat, 0, & rsl, & err_msg, & err_idx, NULL );
      if ( rsl != 0 )
        {
          fprintf( stderr, "ERROR: PCRE: Code %d at pos %d of '%s'\n\t%s\n",
                   rsl, err_idx, rps[i].pat, err_msg
                 );
          while ( i-- ) pcre_free( * rps[i].reg );
          return rsl;
        }
      assert( *( rps[i].reg ) != NULL );
    }

  return rsl;
}

  void
gm_regexes_free( gm_regexes_t * grs )
{
  assert( grs != NULL );
  pcre_free( grs->tmpl_mon );
  pcre_free( grs->tmpl_shadow );
  pcre_free( grs->tmpl_pure );
  pcre_free( grs->tmpl_norm );
  pcre_free( grs->tmpl_pvp_move );
  pcre_free( grs->tmpl_pvp_fast );
}


/* ------------------------------------------------------------------------- */

  void
gm_parser_release( gm_parser_t * gm_parser )
{
  jsmn_file_parser_free( gm_parser->fparser );
  free( gm_parser->fparser ); /* The file parser itself was malloced */
  gm_regexes_free( &gm_parser->regs );
  jsmnis_free( &gm_parser->iter_stack );
  gm_parser->buffer     = NULL;
  gm_parser->buffer_len = 0;
  gm_parser->tokens     = NULL; /* Tokens were already freed by fparser */
  gm_parser->tokens_cnt = 0;
  gm_parser->fparser    = NULL;
  free( gm_parser->incomplete_mon );
  free( gm_parser->incomplete_fam );
  gm_parser->incomplete_mon  = NULL;
  gm_parser->incomplete_fam  = NULL;
  gm_parser->incomplete_idx  = 0;
  gm_parser->incomplete_size = 0;
}

  void
gm_parser_free( gm_parser_t * gm_parser )
{
  gm_parser_release( gm_parser );
  /* Free the moves table */
  store_move_t * curr_move = NULL;
  store_move_t * tmp_move  = NULL;
  HASH_ITER( hh_name, gm_parser->moves_by_name, curr_move, tmp_move )
    {
      HASH_DELETE( hh_name, gm_parser->moves_by_name, curr_move );
      HASH_DELETE( hh_move_id, gm_parser->moves_by_id, curr_move );
      free( curr_move->name );
      free( curr_move );
    }
  /* Free Pokedex */
  pdex_mon_t * curr_mon = NULL;
  pdex_mon_t * tmp_mon  = NULL;
  HASH_ITER( hh_name, gm_parser->mons_by_name, curr_mon, tmp_mon )
    {
      HASH_DELETE( hh_name, gm_parser->mons_by_name, curr_mon );
      HASH_DELETE( hh_dex_num, gm_parser->mons_by_dex, curr_mon );
      pdex_mon_free( curr_mon );
    }
}

  static int
seek_templates_start( gm_parser_t * gm_parser )
{
  assert( gm_parser != NULL );
  if ( 0 < gm_parser->iter_stack.stack_size )
    {
      jsmnis_free( &gm_parser->iter_stack );
    }
  int jsmn_rsl = jsmnis_init( &( gm_parser->iter_stack ),
                              gm_parser->tokens,
                              gm_parser->tokens_cnt,
                              8
                            );
  if ( jsmn_rsl != 0 ) return jsmn_rsl;

  /* Open the top level object */
  jsmn_rsl = jsmnis_push( &( gm_parser->iter_stack ), 0 );
  if ( jsmn_rsl != 0 ) return jsmn_rsl;

  /* Open the item template list */
  jsmn_rsl = jsmnis_open_key_seq( gm_parser->buffer,
                                  &( gm_parser->iter_stack ),
                                  "itemTemplate",
                                  0
                                );
  return jsmn_rsl;
}


  size_t
gm_parser_init( gm_parser_t * gm_parser, const char * gm_fpath )
{
  assert( gm_fpath != NULL );
  assert( gm_parser != NULL );

  size_t read_chars  = 0;
  long   read_tokens = 0;
  int    jsmn_rsl    = 0;

  gm_parser->fparser =
    (jsmn_file_parser_t *) malloc( sizeof( jsmn_file_parser_t ) );
  if ( gm_parser->fparser == NULL ) return 0;

  read_chars = jsmn_file_parser_init( gm_parser->fparser, gm_fpath );

  if ( read_chars == 0 )
    {
      jsmn_file_parser_free( gm_parser->fparser );
      free( gm_parser->fparser );
      return 0;
    }

  /* `jsmn_parse_realloc' handles allocation. */
  read_tokens = jsmn_parse_realloc( &( gm_parser->fparser->jparser ),
                                    gm_parser->fparser->buffer,
                                    gm_parser->fparser->buffer_len,
                                    &( gm_parser->fparser->tokens ),
                                    &( gm_parser->fparser->tokens_cnt )
                                  );
  if ( read_tokens == JSMN_ERROR_NOMEM )
    {
      fprintf( stderr,
              "%s: JSMN failed to allocate space for tokens for file '%s'.\n",
              __func__,
              gm_fpath
            );
      gm_parser_free( gm_parser );
      return 0;
    }

  /* Initialize tables */
  gm_parser->moves_by_name = NULL;
  gm_parser->moves_by_id   = NULL;
  gm_parser->mons_by_name  = NULL;
  gm_parser->mons_by_dex   = NULL;

  gm_parser->buffer     = gm_parser->fparser->buffer;
  gm_parser->buffer_len = read_chars;
  gm_parser->tokens     = gm_parser->fparser->tokens;
  gm_parser->tokens_cnt = read_tokens;

  gm_parser->incomplete_idx  = 0;
  gm_parser->incomplete_size = 16;
  gm_parser->incomplete_mon  =
    (pdex_mon_t **) malloc( sizeof( pdex_mon_t * ) *
                            gm_parser->incomplete_size
                          );
  gm_parser->incomplete_fam  =
    (jsmntok_t **) malloc( sizeof( jsmntok_t * ) * gm_parser->incomplete_size );

  /* use `seek_templates_start' to setup `iter_stack' */
  gm_parser->iter_stack.tokens          = NULL;
  gm_parser->iter_stack.stack           = NULL;
  gm_parser->iter_stack.is_object_flags = NULL;
  gm_parser->iter_stack.stack_size      = 0;
  gm_parser->iter_stack.stack_index     = 0;
  gm_parser->iter_stack.jsmn_len        = 0;
  gm_parser->iter_stack.hint            = 0;

  gm_regexes_init( &( gm_parser->regs ) );

  process_moves( gm_parser );
  process_pokemon( gm_parser );

  return read_tokens;
}


/* ------------------------------------------------------------------------- */

  ptype_t
parse_gm_type( const char * json, jsmntok_t * token )
{
  if ( ( json == NULL ) || ( token == NULL ) ) return PT_NONE;
  if ( toklen( token ) < 16 ) return PT_NONE; /* Ice is shortest at 16 */
  if ( strncmp( json + token->start, "POKEMON_TYPE_", 13 ) != 0 )
    {
      return PT_NONE;
    }

  for ( int i = 1; i < NUM_PTYPES; i++ )
    {
      if ( strncasecmp( json + token->start  + 13,
                        PTYPE_NAMES[i],
                        toklen( token ) - 13
                      ) == 0
         ) return (ptype_t) i;
    }

  return PT_NONE;
}


/* ------------------------------------------------------------------------- */

  bool
stris_pvp_charged_move( const char * str, gm_regexes_t * regs )
{
  assert( regs != NULL );
  if ( str == NULL ) return false;
  return ( ! pcre_exec( regs->tmpl_pvp_move, NULL, str, strlen( str ), 0, 0, NULL, 0 ) ) &&
    ( !! pcre_exec( regs->tmpl_pvp_fast, NULL, str, strlen( str ), 0, 0, NULL, 0 ) );
}


/* ------------------------------------------------------------------------- */

  uint16_t
parse_gm_dex_num( const char * json, jsmntok_t * token )
{
  if ( ( json == NULL ) || ( token == NULL ) ) return 0;
  if ( json[token->start] != 'V' ) return 0;
  return atouin( json + token->start + 1, 4 );
}


/* ------------------------------------------------------------------------- */

  stats_t
parse_gm_stats( const char * json, jsmnis_t * iter_stack )
{
  stats_t     stats     = { 0, 0, 0 };
  jsmntok_t * key       = NULL;
  jsmntok_t * val       = NULL;

  while ( jsmni_next( jsmnis_curr( iter_stack ), &key, &val, 0 ) > 0 )
    {
      if ( jsoneq_str( json, key, "baseStamina" ) )
        {
          stats.stamina = atouin( json + val->start, 4 );
        }
      else if ( jsoneq_str( json, key, "baseAttack" ) )
        {
          stats.attack = atouin( json + val->start, 4 );
        }
      else if ( jsoneq_str( json, key, "baseDefense" ) )
        {
          stats.defense = atouin( json + val->start, 4 );
        }
    }
  return stats;
}


/* ------------------------------------------------------------------------- */

  buff_t
parse_gm_buff( const char * json, jsmni_t * iter )
{
  buff_t      buff      = NO_BUFF;
  jsmntok_t * key       = NULL;
  jsmntok_t * val       = NULL;
  char        amount    = 0;

  while ( jsmni_next( iter, &key, &val, 0 ) > 0 )
    {
      if ( jsoneq_str( json, key, "targetAttackStatStageChange" ) )
        {
          amount = atocn( json + val->start, toklen( val ) );
          buff.atk_buff.target  = 1;
          buff.atk_buff.amount  = ( 0 < amount ) ? amount : ( - amount );
          buff.atk_buff.debuffp = !!( amount < 0 );
        }
      else if ( jsoneq_str( json, key, "targetDefenseStatStageChange" ) )
        {
          amount = atocn( json + val->start, toklen( val ) );
          buff.def_buff.target  = 1;
          buff.def_buff.amount  = ( 0 < amount ) ? amount : ( - amount );
          buff.def_buff.debuffp = !!( amount < 0 );
        }
      else if ( jsoneq_str( json, key, "attackerAttackStatStageChange" ) )
        {
          amount = atocn( json + val->start, toklen( val ) );
          buff.atk_buff.target  = 0;
          buff.atk_buff.amount  = ( 0 < amount ) ? amount : ( - amount );
          buff.atk_buff.debuffp = !!( amount < 0 );
        }
      else if ( jsoneq_str( json, key, "attackerDefenseStatStageChange" ) )
        {
          amount = atocn( json + val->start, toklen( val ) );
          buff.def_buff.target  = 0;
          buff.def_buff.amount  = ( 0 < amount ) ? amount : ( - amount );
          buff.def_buff.debuffp = !!( amount < 0 );
        }
      else if ( jsoneq_str( json, key, "buffActivationChance" ) )
        {
          if      ( jsoneq( json, val, "1.0" ) )   buff.chance = bc_1000;
          else if ( jsoneq( json, val, "0.5" ) )   buff.chance = bc_0500;
          else if ( jsoneq( json, val, "0.3" ) )   buff.chance = bc_0300;
          else if ( jsoneq( json, val, "0.125" ) ) buff.chance = bc_0125;
          else if ( jsoneq( json, val, "0.1" ) )   buff.chance = bc_0100;
          else                                     buff.chance = bc_0000;
        }
    }

  return buff;
}


/* ------------------------------------------------------------------------- */

  uint16_t
parse_pdex_mon( const char   *  json,
                jsmnis_t     *  iter_stack,
                store_move_t *  moves_by_name,
                pdex_mon_t   *  mons_by_name,
                jsmntok_t    ** incomplete_fam_tok,
                pdex_mon_t   *  mon
              )
{
  assert( json != NULL );
  assert( iter_stack != NULL );
  assert( mon != NULL );

  const unsigned short stack_idx = iter_stack->stack_index;
  jsmntok_t *          key       = NULL;
  jsmntok_t *          val       = NULL;
  int                  idx       = jsmnis_pos( iter_stack );
  int                  rsl       = 0;

  /* `templateId' value should already be targeted by `iter_stack' */
  assert( 0 < idx );
  /* Clear struct */
  memset( mon, 0, sizeof( pdex_mon_t ) );
  mon->next_form = NULL;
  mon->tags = TAG_NONE_M;
  mon->fast_moves_cnt = 0;
  mon->charged_moves_cnt = 0;

  /* Parse Dex # from `templateId' value */
  mon->dex_number = parse_gm_dex_num( json, iter_stack->tokens + idx );
  assert( mon->dex_number != 0 );

  /* Mark starters */
  if ( is_starter( mon->dex_number ) ) mon->tags |= TAG_STARTER_M;
  if ( is_regional( mon->dex_number ) ) mon->tags |= TAG_REGIONAL_M;

  /* Create iterator on `pokemon' value */
  rsl = jsmnis_open_key_seq( json, iter_stack, "pokemon", 0 );
  assert( 0 == rsl );

  while ( jsmni_next( jsmnis_curr( iter_stack ), &key, &val, iter_stack->hint )
            > 0
        )
    {
      if ( jsoneq_str( json, key, "uniqueId" ) )
        {
          mon->name = strndup( json + val->start, toklen( val ) );
          assert( mon->name != NULL );
        }
      else if ( jsoneq_str( json, key, "type1" ) ||
                jsoneq_str( json, key, "type2" )
              )
        {
          mon->types |= get_ptype_mask( parse_gm_type( json, val ) );
        }
      else if ( jsoneq_str( json, key, "stats" ) )
        {
          jsmnis_push_curr( iter_stack );
          mon->base_stats = parse_gm_stats( json, iter_stack );
          jsmnis_pop( iter_stack );
        }
      else if ( jsoneq_str( json, key, "quickMoves" ) ||
                jsoneq_str( json, key, "eliteQuickMove" )
              )
        {
          idx = mon->fast_moves_cnt;
          mon->fast_moves_cnt += val->size;
          if ( mon->fast_move_ids == NULL )
            {
              mon->fast_move_ids  = malloc( sizeof( uint16_t ) *
                                              mon->fast_moves_cnt
                                          );
            }
          else
            {
              mon->fast_move_ids  = realloc( mon->fast_move_ids,
                                             sizeof( uint16_t ) *
                                               mon->fast_moves_cnt
                                           );
            }
          assert( mon->fast_move_ids != NULL );
          rsl = json[key->start] == 'e' ? -1 : 1;
          jsmnis_push_curr( iter_stack );
          while ( jsmni_next( jsmnis_curr( iter_stack ), NULL, &val, 0 ) > 0 )
            {
              mon->fast_move_ids[idx++] = lookup_move_idn( moves_by_name,
                                                           json + val->start,
                                                           toklen( val ) - 5
                                                         ) * rsl;
              assert( 0 != mon->fast_move_ids[idx - 1] );
            }
          jsmnis_pop( iter_stack );
          assert( idx == mon->fast_moves_cnt );
        }
      else if ( jsoneq_str( json, key, "cinematicMoves" ) ||
                jsoneq_str( json, key, "eliteCinematicMove" )
              )
        {
          idx = mon->charged_moves_cnt;
          mon->charged_moves_cnt += val->size;
          if ( mon->charged_move_ids == NULL )
            {
              mon->charged_move_ids  = malloc( sizeof( uint16_t ) *
                                                 mon->charged_moves_cnt
                                             );
            }
          else
            {
              mon->charged_move_ids  = realloc( mon->charged_move_ids,
                                                sizeof( uint16_t ) *
                                                  mon->charged_moves_cnt
                                              );
            }
          assert( mon->charged_move_ids != NULL );
          rsl = json[key->start] == 'e' ? -1 : 1;
          jsmnis_push_curr( iter_stack );
          while ( jsmni_next( jsmnis_curr( iter_stack ), NULL, &val, 0 ) > 0 )
            {
              mon->charged_move_ids[idx++] = lookup_move_idn( moves_by_name,
                                                              json + val->start,
                                                              toklen( val )
                                                            ) * rsl;
              assert( 0 != mon->charged_move_ids[idx - 1] );
            }
          jsmnis_pop( iter_stack );
          assert( idx == mon->charged_moves_cnt );
        }
      else if ( jsoneq_str( json, key, "form" ) )
        {
          if ( strncmp( mon->name, "NIDORAN_", 8 ) != 0 )
            {
              mon->form_name = strndup( json + val->start +
                                          strlen( mon->name ) + 1,
                                        toklen( val ) - strlen( mon->name ) - 1
                                      );

            }
          else
            {
              mon->form_name = strndup( json + val->start + 8,
                                        toklen( val ) - 8
                                      );

            }
          assert( mon->form_name != NULL );
          if ( ( strcmp( mon->form_name, "SHADOW" )   == 0 ) ||
               ( strcmp( mon->form_name, "PURIFIED" ) == 0 )
             ) mon->tags |= TAG_SHADOW_ELIGABLE_M;
        }
      else if ( jsoneq_str( json, key, "familyId" ) )
        {
          if ( strncmp( json + val->start + 7, mon->name, toklen( val ) - 7)
               == 0
             )
            {
              mon->family = mon->dex_number;
            }
          else
            {
              mon->family = lookup_dexn( mons_by_name,
                                         json + val->start + 7,
                                         toklen( val ) - 7
                                       );
              if ( mon->family == 0 ) *incomplete_fam_tok = val;
            }
        }
      else if ( jsoneq_str( json, key, "pokemonClass" ) )
        {
          if ( strncmp( json + val->start,
                        "POKEMON_CLASS_LEGENDARY",
                        toklen( val )
                      ) == 0
             )
            {
              mon->tags |= TAG_LEGENDARY;
            }
          else if ( strncmp( json + val->start,
                             "POKEMON_CLASS_MYTHIC",
                             toklen( val )
                           ) == 0
                  )
            {
              mon->tags |= TAG_MYTHIC;
            }
        }
    }
  jsmnis_pop( iter_stack );

  if ( mon->form_name == NULL )
    {
      mon->form_name = strdup( "BASE" );
      assert( mon->form_name != NULL );
    }

  assert( mon->name != NULL );
  assert( iter_stack->stack_index == stack_idx );
  assert( mon->types != PT_NONE_M );
  assert( mon->base_stats.stamina != 0 );
  assert( mon->base_stats.attack != 0 );
  assert( mon->base_stats.defense != 0 );

  return mon->dex_number;
}


/* ------------------------------------------------------------------------- */

/**
 * Pokedex data is stored as a hash table indexed by Dex Numbers/Names,
 * holding a linked list of "forms".
 * <p>
 * Unlike the data store, the parser's simpler table cannot directly jump to a
 * specific form. This is fine, because the GM Parser is not meant to be used
 * to store data; it is a temporary construct to extract data for later
 * organization and reduction. Still we try to save space where we can by
 * using move ids and dex numbers for families when we can. For this reason
 * moves must be parsed before pokemon, and in a handful of cases families
 * will need to be filled in after the fact ( baby forms ).
 * <p>
 * A lot of these forms are completely redundant. For example, shadow/purified
 * are always identical to their normal forms, with the exception of the
 * extra charged moves "FRUSTRATION", and "RETURN"; these skipped by the parser.
 * Similarly Unown and Spinda have a ton of identical forms that are skipped.
 * Other pokemon likely have redundant forms, but for the sake of simplicity we
 * parse them anyways, and "merge" them later when building the data store.
 */
  bool
add_mon_data( gm_parser_t * gm_parser, pdex_mon_t * mon )
{
  assert( gm_parser != NULL );
  assert( mon != NULL );

  pdex_mon_t * stored = NULL;
  HASH_FIND( hh_dex_num,
             gm_parser->mons_by_dex,
             & mon->dex_number,
             sizeof( uint16_t ),
             stored
           );

  /* Add new pdex_mon_t if none exists yet */
  if ( stored == NULL )
    {
      mon->form_idx = 0;
      HASH_ADD_KEYPTR( hh_name,
                       gm_parser->mons_by_name,
                       mon->name,
                       strlen( mon->name ),
                       mon
                     );
      HASH_ADD( hh_dex_num,
                gm_parser->mons_by_dex,
                dex_number,
                sizeof( uint16_t ),
                mon
              );
    }
  else
    {
      /* For shadow/purified and the duplicate "normal" forms, mark shadow
       * eligable on the base form, but don't add a new one. */
      if ( ( strcmp( mon->form_name, "SHADOW" )   == 0 ) ||
           ( strcmp( mon->form_name, "PURIFIED" ) == 0 ) ||
           ( ( strcmp( mon->form_name, "NORMAL" )   == 0 ) &&
             ( mon->dex_number != 649 ) /* Genensect is an exception */
           )
         )
        {
          stored->tags |= TAG_SHADOW_ELIGABLE_M;
          pdex_mon_free( mon );
          return true; /* This was freed so we cannot return `false' */
        }

      /* Check if this form already exists.
       * If it does, update the family name, but leave everything else alone. */
      if ( strcmp( stored->form_name, mon->form_name ) == 0 )
        {
          stored->family = mon->family;
          pdex_mon_free( mon );
          return true; /* This was freed so we cannot return `false' */
        }

      while ( stored->next_form != NULL )
        {
          stored = stored->next_form;
          /* Check for same form */
          if ( strcmp( stored->form_name, mon->form_name ) == 0 )
            {
              stored->family = mon->family;
              pdex_mon_free( mon );
              return true; /* This was freed so we cannot return `false' */
            }
        }
      stored->next_form = mon;
      mon->form_idx = stored->form_idx + 1;
    }

  return ( 0 < mon->family );
}


/* ------------------------------------------------------------------------- */

  uint16_t
parse_pvp_charged_move( const char         *  json,
                        jsmnis_t           *  iter_stack,
                        char               ** name,
                        pvp_charged_move_t *  move
                      )
{
  assert( json != NULL );
  assert( iter_stack != NULL );
  assert( name != NULL );
  assert( move != NULL );

  const unsigned short stack_idx = iter_stack->stack_index;
  jsmntok_t *          key       = NULL;
  jsmntok_t *          val       = NULL;
  int                  idx       = jsmnis_pos( iter_stack );
  int                  rsl       = 0;

  /* `templateId' value should already be targeted by `iter_stack' */

  /* Clear struct */
  memset( move, 0, sizeof( pvp_charged_move_t ) );

  move->is_fast = false;

  /* Parse Move ID */
  move->move_id = atouin( json + iter_stack->tokens[idx].start + 8, 4 );

  rsl = jsmnis_open_key_seq( json, iter_stack, "combatMove", 0 );
  while ( jsmni_next( jsmnis_curr( iter_stack ),
                      &key, &val, iter_stack->hint
                    ) > 0
        )
    {
      if ( jsoneq_str( json, key, "uniqueId" ) )
        {
          *name = strndup( json + val->start, toklen( val ) );
        }
      else if ( jsoneq_str( json, key, "type" ) )
        {
          move->type = parse_gm_type( json, val );
        }
      else if ( jsoneq_str( json, key, "power" ) )
        {
          move->power = atoi( json + val->start );
        }
      else if ( jsoneq_str( json, key, "energyDelta" ) )
        {
          rsl = atoi( json + val->start );
          move->energy = ( 0 < rsl ) ? rsl : -rsl;
        }
      else if ( jsoneq_str( json, key, "buffs" ) )
        {
          jsmnis_push_curr( iter_stack );
          move->buff = parse_gm_buff( json, current_iterator( iter_stack ) );
          jsmnis_pop( iter_stack );
        }
    }
  jsmnis_pop( iter_stack );

  assert( iter_stack->stack_index == stack_idx );
  assert( *name != NULL );
  assert( move->is_fast == false );
  assert( move->move_id != 0 );
  assert( move->type != PT_NONE );
  assert( 0 < move->power );
  assert( 0 < move->energy );

  return move->move_id;
}


/* ------------------------------------------------------------------------- */

  uint16_t
parse_pvp_fast_move( const char      *  json,
                     jsmnis_t        *  iter_stack,
                     char            ** name,
                     pvp_fast_move_t *  move
                   )
{
  assert( json != NULL );
  assert( iter_stack != NULL );
  assert( name != NULL );
  assert( move != NULL );

  const unsigned short stack_idx = iter_stack->stack_index;
  jsmntok_t *          key       = NULL;
  jsmntok_t *          val       = NULL;
  int                  idx       = jsmnis_pos( iter_stack );
  int                  rsl       = 0;

  /* `templateId' value should already be targeted by `iter_stack' */
  assert( 0 < idx );
  /* Clear struct */
  memset( move, 0, sizeof( pvp_fast_move_t ) );

  move->is_fast = true;
  move->turns   = 1;     /* Some moves' turns are not explicitly defined */

  /* Parse Move ID */
  move->move_id = atouin( json + iter_stack->tokens[idx].start + 8, 4 );

  rsl = jsmnis_open_key_seq( json, iter_stack, "combatMove", 0 );

  while ( jsmni_next( jsmnis_curr( iter_stack ),
                      &key, &val, iter_stack->hint
                    ) > 0
        )
    {
      if ( jsoneq_str( json, key, "uniqueId" ) )
        {
          *name = strndup( json + val->start, toklen( val ) - 5 );
        }
      else if ( jsoneq_str( json, key, "type" ) )
        {
          move->type = parse_gm_type( json, val );
        }
      else if ( jsoneq_str( json, key, "power" ) )
        {
          move->power = atoi( json + val->start );
        }
      else if ( jsoneq_str( json, key, "energyDelta" ) )
        {
          rsl = atoi( json + val->start );
          move->energy = ( 0 < rsl ) ? rsl : -rsl;
        }
      else if ( jsoneq_str( json, key, "durationTurns" ) )
        {
          move->turns = atoi( json + val->start );
        }
    }
  jsmnis_pop( iter_stack );

  assert( iter_stack->stack_index == stack_idx );
  assert( *name != NULL );
  assert( move->is_fast == true );
  assert( move->move_id != 0 );
  assert( move->type != PT_NONE );
  /* Splash has 0 power ... assert( 0 < move->power ); */
  /* Transform has 0 energy ... assert( 0 < move->energy ); */
  assert( 0 < move->turns );

  return move->move_id;
}


/* ------------------------------------------------------------------------- */

  void
add_pvp_charged_move_data( gm_parser_t        *  gm_parser,
                           char               *  move_name,
                           pvp_charged_move_t *  move
                         )
{
  assert( gm_parser != NULL );
  assert( move != NULL );
  assert( move_name != NULL );

  store_move_t * stored = NULL;
  HASH_FIND( hh_move_id,
             gm_parser->moves_by_id,
             & move->move_id,
             sizeof( uint16_t ),
             stored
           );

  /* Add new store_move if none exists yet */
  if ( stored == NULL )
    {
      stored = (store_move_t *) malloc( sizeof( store_move_t ) );
      memset( stored, 0, sizeof( store_move_t ) );
      assert( stored != NULL );

      stored->name    = move_name;
      stored->move_id = move->move_id;
      stored->type    = move->type;
      stored->is_fast = false;

      HASH_ADD_KEYPTR( hh_name,
                       gm_parser->moves_by_name,
                       move_name,
                       strlen( move_name ),
                       stored
                     );
      HASH_ADD( hh_move_id,
                gm_parser->moves_by_id,
                move_id,
                sizeof( uint16_t ),
                stored
              );
    }

  stored->pvp_power  = move->power;
  stored->pvp_energy = move->energy;
  stored->buff       = move->buff;
}


/* ------------------------------------------------------------------------- */

  void
add_pvp_fast_move_data( gm_parser_t     *  gm_parser,
                        char            *  move_name,
                        pvp_fast_move_t *  move
                      )
{
  assert( gm_parser != NULL );
  assert( move != NULL );
  assert( move_name != NULL );

  store_move_t * stored = NULL;
  HASH_FIND( hh_move_id,
             gm_parser->moves_by_id,
             & move->move_id,
             sizeof( uint16_t ),
             stored
           );

  /* Add new store_move if none exists yet */
  if ( stored == NULL )
    {
      stored = (store_move_t *) malloc( sizeof( store_move_t ) );
      memset( stored, 0, sizeof( store_move_t ) );
      assert( stored != NULL );

      stored->name = move_name;
      stored->move_id = move->move_id;
      stored->type    = move->type;
      stored->is_fast = true;

      HASH_ADD_KEYPTR( hh_name,
                       gm_parser->moves_by_name,
                       move_name,
                       strlen( move_name ),
                       stored
                     );
      HASH_ADD( hh_move_id,
                gm_parser->moves_by_id,
                move_id,
                sizeof( uint16_t ),
                stored
              );
    }

  stored->pvp_power  = move->power;
  stored->pvp_energy = move->energy;
  stored->cooldown   = move->turns;
  stored->buff       = NO_BUFF;
}


/* ------------------------------------------------------------------------- */

  uint16_t
lookup_move_id( store_move_t * moves, const char * name )
{
  assert( moves != NULL );
  assert( name != NULL );
  store_move_t * move = NULL;
  HASH_FIND( hh_name, moves, name, strlen( name ), move );
  return ( move == NULL ) ? 0 : move->move_id;
}

  uint16_t
lookup_move_idn( store_move_t * moves, const char * name, int16_t n )
{
  assert( moves != NULL );
  assert( name != NULL );
  assert( 0 != n );
  store_move_t * move = NULL;
  /**
   * Take absolute value of move index incase the caller forgot to clear the
   * highest bit ( indicating legacy moves in a `pdex_mon_t' `move_id' list )
   */
  HASH_FIND( hh_name, moves, name, max( n, -n ), move );
  return ( move == NULL ) ? 0 : move->move_id;
}


/* ------------------------------------------------------------------------- */

  uint16_t
lookup_dex( pdex_mon_t * mons, const char * name )
{
  assert( mons != NULL );
  assert( name != NULL );
  pdex_mon_t * mon = NULL;
  HASH_FIND( hh_name, mons, name, strlen( name ), mon );
  return ( mon == NULL ) ? 0 : mon->dex_number;
}

  uint16_t
lookup_dexn( pdex_mon_t * mons, const char * name, size_t n )
{
  assert( mons != NULL );
  assert( name != NULL );
  assert( 0 < n );
  pdex_mon_t * mon = NULL;
  HASH_FIND( hh_name, mons, name, n, mon );
  return ( mon == NULL ) ? 0 : mon->dex_number;
}


/* ------------------------------------------------------------------------- */

  void
process_moves( gm_parser_t * gm_parser )
{
  assert( gm_parser != NULL );

  int                  jsmn_rsl     = seek_templates_start( gm_parser );
  jsmntok_t          * key          = NULL;
  jsmntok_t          * val          = NULL;
  jsmntok_t          * item         = NULL;
  pvp_fast_move_t    * fast_move    = NULL;
  pvp_charged_move_t * charged_move = NULL;
  char               * name         = NULL;

  assert( jsmn_rsl == 0 );

  /* Iterate over items */
  while ( jsmni_next( jsmnis_curr( &( gm_parser->iter_stack ) ),
                      NULL, &item, gm_parser->iter_stack.hint
                    ) > 0
        )
    {
      jsmnis_push_curr( &( gm_parser->iter_stack ) );
      key = NULL;
      val = NULL;
      jsmn_rsl = jsmni_find_next( gm_parser->buffer,
                                  jsmnis_curr( &( gm_parser->iter_stack ) ),
                                  &key,
                                  jsoneq_str_p,
                                  (void *) "templateId",
                                  &val,
                                  jsonmatch_str_pcre_p,
                                  (void *) gm_parser->regs.tmpl_pvp_move,
                                  0
                                );
      if ( jsmn_rsl <= 0 )
        {
          jsmnis_pop( &( gm_parser->iter_stack ) );
          continue;
        }

      name = NULL;

      /* Fast Move */
      if ( jsonmatch_str_pcre( gm_parser->buffer,
                          gm_parser->tokens +
                            jsmnis_pos( &( gm_parser->iter_stack ) ),
                          gm_parser->regs.tmpl_pvp_fast
                        )
         )
        {
          fast_move = (pvp_fast_move_t *) malloc( sizeof( pvp_fast_move_t ) );
          assert( fast_move != NULL );
          jsmn_rsl = parse_pvp_fast_move( gm_parser->buffer,
                                          &( gm_parser->iter_stack ),
                                          & name,
                                          fast_move
                                        );
          assert( name != NULL );
          assert( 0 < jsmn_rsl );
          add_pvp_fast_move_data( gm_parser, name, fast_move );
          free( fast_move );
        }
      else /* Charged Move */
        {
          charged_move =
            (pvp_charged_move_t *) malloc( sizeof( pvp_charged_move_t ) );
          assert( charged_move != NULL );
          jsmn_rsl = parse_pvp_charged_move( gm_parser->buffer,
                                             &( gm_parser->iter_stack ),
                                             & name,
                                             charged_move
                                           );
          assert( name != NULL );
          assert( 0 < jsmn_rsl );
          add_pvp_charged_move_data( gm_parser, name, charged_move );
          free( charged_move );
        }

      jsmnis_pop( &( gm_parser->iter_stack ) );
    }
}


/* ------------------------------------------------------------------------- */

  bool
should_parse_mon( const char      * json,
                  const jsmntok_t * token,
                  gm_regexes_t    * regs
                )
{
  assert( json != NULL );
  assert( token != NULL );
  assert( regs != NULL );
  if ( ! jsonmatch_str_pcre( json, token, regs->tmpl_mon ) )  return false;
  if ( jsonmatch_str_pcre( json, token, regs->tmpl_shadow ) ) return false;
  if ( jsonmatch_str_pcre( json, token, regs->tmpl_pure ) )   return false;
  /* We still have to parse "NORMAL" forms because of Genesect */
  //if ( jsonmatch_str( json, token, &( regs->tmpl_norm ) ) )   return false;
  return true;
}


/* ------------------------------------------------------------------------- */

  void
process_pokemon( gm_parser_t * gm_parser )
{
  assert( gm_parser != NULL );
  /* Moves MUST be processed first! */
  assert( gm_parser->moves_by_name != NULL );

  size_t       first_idx = 0;
  int          jsmn_rsl  = seek_templates_start( gm_parser );
  jsmntok_t  * key       = NULL;
  jsmntok_t  * val       = NULL;
  jsmntok_t  * item      = NULL;
  pdex_mon_t * mon       = NULL;

  assert( jsmn_rsl == 0 );

  /* Iterate over items */
  while ( jsmni_next( jsmnis_curr( &( gm_parser->iter_stack ) ),
                      NULL, &item, gm_parser->iter_stack.hint
                    ) > 0
        )
    {
      jsmnis_push_curr( &( gm_parser->iter_stack ) );
      key = NULL;
      val = NULL;
      jsmn_rsl = jsmni_find_next( gm_parser->buffer,
                                  jsmnis_curr( &( gm_parser->iter_stack ) ),
                                  &key,
                                  jsoneq_str_p,
                                  (void *) "templateId",
                                  &val,
                                  jsonmatch_str_pcre_p,
                                  (void *) gm_parser->regs.tmpl_mon,
                                  0
                                );
      if ( jsmn_rsl <= 0 )
        {
          jsmnis_pop( &( gm_parser->iter_stack ) );
          continue;
        }

      mon = (pdex_mon_t *) malloc( sizeof( pdex_mon_t ) );
      assert( mon != NULL );
      jsmn_rsl = parse_pdex_mon( gm_parser->buffer,
                                 &( gm_parser->iter_stack ),
                                 gm_parser->moves_by_name,
                                 gm_parser->mons_by_name,
                                 &item,
                                 mon
                               );
      assert( 0 < jsmn_rsl );

      jsmn_rsl = add_mon_data( gm_parser, mon );
      /* Family not found. Push stack */
      if ( jsmn_rsl == 0 )
        {
          if ( gm_parser->incomplete_size <= gm_parser->incomplete_idx )
            {
              gm_parser->incomplete_size <<= 1;
              gm_parser->incomplete_mon =
                (pdex_mon_t **) realloc( gm_parser->incomplete_mon,
                                         gm_parser->incomplete_size *
                                           sizeof( pdex_mon_t * )
                                       );
              gm_parser->incomplete_fam =
                (jsmntok_t **) realloc( gm_parser->incomplete_mon,
                                        gm_parser->incomplete_size *
                                          sizeof( jsmntok_t * )
                                      );
            }
          gm_parser->incomplete_mon[gm_parser->incomplete_idx]   = mon;
          gm_parser->incomplete_fam[gm_parser->incomplete_idx++] = item;
        }

      jsmnis_pop( &( gm_parser->iter_stack ) );
    }

  for ( uint8_t i = 0; i < gm_parser->incomplete_idx; i++ )
    {
      mon = gm_parser->incomplete_mon[i];
      mon->family = lookup_dexn( gm_parser->mons_by_name,
                                 gm_parser->buffer +
                                   gm_parser->incomplete_fam[i]->start + 7,
                                 toklen( gm_parser->incomplete_fam[i] ) - 7
                               );
      assert( mon->family != 0 );
    }
}


/* ------------------------------------------------------------------------- */

#ifdef MK_PARSE_GM_BINARY

#include <unistd.h>
#include <ctype.h>
#include <string.h>

static const char USAGE_STR[] = R"RAW_STRING(
Usage: parse_gm [OPTION]... [FILE]
Parse a GAME_MASTER.json file and convert it to another format.
Example: parse_gm -e c -f ./my_gm.json

Options:
  -e FORMAT    Encode to FORMAT. One of: C, JSON, SQL.  \( Case Insensitive \)
  -f FILE      Use FILE as GAME_MASTER.json file.

Default export format is C, default FILE is ./data/GAME_MASTER.json
)RAW_STRING";

  int
main( int argc, char * argv[], char ** envp )
{
  gm_parser_t    gm_parser;
  size_t         tokens_cnt = 0;
  char         * gm_path    = NULL;
  store_sink_t   export_fmt = SS_C;
  char           opt        = '\0';

  while ( optind < argc )
    {
      opt = getopt( argc, argv, "he:f:" );
      switch( opt )
        {
        case 'e':
          if ( strcasecmp( optarg, "json" ) == 0 )     export_fmt = SS_JSON;
          else if ( strcasecmp( optarg, "c" ) == 0 )   export_fmt = SS_C;
          else if ( strcasecmp( optarg, "sql" ) == 0 ) export_fmt = SS_SQL;
          break;

        case 'f':
          gm_path = optarg;
          break;

        case 'h':
          fprintf( stdout, USAGE_STR );
          return EXIT_SUCCESS;
          break;

        case '?':
          if ( ( optopt == 'e' ) || ( optopt == 'f' ) )
            {
              fprintf( stderr, "Option `-%c' requires an argument.\n", optopt );
            }
          else if ( isprint( optopt ) )
            {
              fprintf( stderr, "Unknown option `-%c'.\n", optopt );
            }
          else
            {
              fprintf( stderr, "Unknown option character `\\x%x'.\n", optopt );
            }
          fprintf( stderr, USAGE_STR );
          return EXIT_FAILURE;
          break;

        default:
          if ( gm_path == NULL )
            {
              gm_path = argv[optind++];
            }
          else
            {
              fprintf( stderr,
                       "Only one unflagged argument is permitted to be used"
                       "as the file path to `GAME_MASTER.json'.\n"
                       "However multiple unflagged arguments were encountered:"
                       " `%s' and `%s'.\nUse `-h' for Usage/Help info.\n",
                       gm_path,
                       argv[optind]
                     );
              return EXIT_FAILURE;
            }
          break;
        }

    }

  if ( gm_path == NULL ) gm_path = "./data/GAME_MASTER.json";

  /* Parse file */
  tokens_cnt = gm_parser_init( & gm_parser, gm_path );
  assert( tokens_cnt != 0 );

  /* Cleanup */
  GM_init( & gm_parser );
  gm_parser_release( & gm_parser );

  /* Prints store as a Static C Store */
  GM_export( export_fmt, stdout );
  pdex_mon_t * mon = NULL;

  /* Cleanup */
  GM_STORE.free( & GM_STORE );

  return EXIT_SUCCESS;
}
#endif /* MK_PARSE_GM_BINARY */


/* ------------------------------------------------------------------------- */



/* ========================================================================= */

/* vim: set filetype=c : */
