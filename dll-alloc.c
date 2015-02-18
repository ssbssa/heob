
#include <stdlib.h>
#include <windows.h>

static void *allocated = NULL;

__declspec(dllexport) void *dll_alloc( size_t s )
{
  return( allocated=malloc(s) );
}

BOOL WINAPI DllMain( HINSTANCE hinstDLL,DWORD fdwReason,LPVOID lpvReserved )
{
  (void)hinstDLL;
  (void)lpvReserved;

  if( fdwReason==DLL_PROCESS_DETACH )
    free( allocated );

  return( TRUE );
}
