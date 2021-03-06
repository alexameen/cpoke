/* -*- mode: c; -*- */

#ifndef _FILES_H
#define _FILES_H

/* ========================================================================= */

#include <stdbool.h>
#include <stdlib.h>


/* ------------------------------------------------------------------------- */

bool   file_exists( const char * fpath );
long   file_size( const char * fpath );
size_t fread_malloc( const char * fpath, char ** buffer );


/* ------------------------------------------------------------------------- */



/* ========================================================================= */

#endif /* files.h */

/* vim: set filetype=c : */
