
#include <stdlib.h>

__declspec(dllexport) void *dll_alloc( size_t s )
{
  return( malloc(s) );
}
