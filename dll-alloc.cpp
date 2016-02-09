
//          Copyright Hannes Domani 2014 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <stdlib.h>
#include <windows.h>

static void *allocated = NULL;

extern "C" __declspec(dllexport) void *dll_alloc( size_t s )
{
  return( allocated=malloc(s) );
}

static void *memory = NULL;

extern "C" __declspec(dllexport) void *dll_memory( void )
{
  return( memory );
}

static int *one_int = NULL;

extern "C" __declspec(dllexport) int *dll_int( void )
{
  return( one_int );
}

static int *arr_int = NULL;

extern "C" __declspec(dllexport) int *dll_arr( void )
{
  return( arr_int );
}

extern "C" BOOL WINAPI DllMain(
    HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved )
{
  (void)hinstDLL;
  (void)lpvReserved;

  if( fdwReason==DLL_PROCESS_ATTACH )
  {
    memory = malloc( 101 );
    one_int = new int;
    arr_int = new int[50];
  }

  if( fdwReason==DLL_PROCESS_DETACH )
    free( allocated );

  return( TRUE );
}