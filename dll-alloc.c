
//          Copyright Hannes Domani 2014 - 2015.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <stdlib.h>
#include <windows.h>

static void *allocated = NULL;

__declspec(dllexport) void *dll_alloc( size_t s )
{
  return( allocated=malloc(s) );
}

static void *memory = NULL;

__declspec(dllexport) void *dll_memory( void )
{
  return( memory );
}

BOOL WINAPI DllMain( HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved )
{
  (void)hinstDLL;
  (void)lpvReserved;

  if( fdwReason==DLL_PROCESS_ATTACH )
    memory = malloc( 101 );

  if( fdwReason==DLL_PROCESS_DETACH )
    free( allocated );

  return( TRUE );
}
