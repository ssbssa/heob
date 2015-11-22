
//          Copyright Hannes Domani 2014 - 2015.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <direct.h>


#ifdef __GNUC__
__declspec(dllimport) void *operator new( size_t );
__declspec(dllimport) void operator delete( void* );
#endif
__declspec(dllimport) void *operator new[]( size_t );
__declspec(dllimport) void operator delete[]( void* );

extern "C" __declspec(dllimport) void *dll_alloc( size_t );


static LONG WINAPI exceptionWalker( LPEXCEPTION_POINTERS ep )
{
  printf( "handled exception code: %08lX\n",
      ep->ExceptionRecord->ExceptionCode );
  fflush( stdout );

  static int wasHere = 0;
  if( wasHere ||
      ep->ExceptionRecord->ExceptionCode!=STATUS_ACCESS_VIOLATION )
    ExitProcess( -1 );
  wasHere++;

#ifndef _WIN64
  ep->ContextRecord->Eip += 10;
#else
  ep->ContextRecord->Rip += 10;
#endif

  return( EXCEPTION_CONTINUE_EXECUTION );
}


void choose( int arg )
{
  char *mem = (char*)malloc( 15 );
  mem[0] = 0;

  switch( arg )
  {
    case 1:
      {
        // memory leaks
        char *copy = strdup( "abcd" );
        char *zeroes = (char*)calloc( 2,500 );
        wchar_t *wcopy = wcsdup( L"efgh" );
        mem[1] = copy[0];
        mem[2] = zeroes[0];
        mem[3] = wcopy[0];
        char *newChars = new char[50];
        mem[4] = newChars[0];

        chdir( "\\" );
        char *cwd = _getcwd( NULL,0 );
        mem[5] = cwd[0];
        wchar_t *wcwd = _wgetcwd( NULL,0 );
        mem[6] = wcwd[0];
        cwd = _getdcwd( 0,NULL,0 );
        mem[7] = cwd[0];
        wcwd = _wgetdcwd( 0,NULL,0 );
        mem[8] = wcwd[0];
        char *fp = _fullpath( NULL,".",0 );
        mem[9] = fp[0];
        wchar_t *wfp = _wfullpath( NULL,L".",0 );
        mem[10] = wfp[0];
      }
      break;

    case 2:
      // access after allowed area
      mem[1] = mem[20];
      mem[25] = 5;
      break;

    case 3:
      // access before allowed area
      mem[1] = mem[-10];
      mem[-5] = 3;
      break;

    case 4:
      // allocation size alignment
      {
        int sum = 0;
        char t[100];
        int i;
        t[0] = 0;
        for( i=0; i<64; i++ )
        {
          char *tc = strdup( t );
          sum += strlen( tc );
          strcat( t,"x" );
        }
        mem[1] = sum;
      }
      break;

    case 5:
      // failed allocation
      {
#ifndef _WIN64
#define BIGNUM 2000000000
#else
#define BIGNUM 0x1000000000000000
#endif
        char *big = (char*)malloc( BIGNUM );
        mem[1] = big[0];
      }
      break;

    case 6:
      // reference pointer after being freed
      {
        char *tmp = (char*)malloc( 15 );
        char *tmp2;
        mem[1] = tmp[0];
        free( tmp );
        tmp2 = (char*)malloc( 15 );
        printf( "ptr1=0x%p; ptr2=0x%p -> %s\n",
            tmp,tmp2,tmp==tmp2?"same":"different" );
        fflush( stdout );
        mem[2] = tmp[1];
        mem[3] = tmp2[0];
        free( tmp2 );
      }
      break;

    case 7:
      // multiple free / free of invalid pointer
      free( mem );
      free( (void*)0x80000000 );
      break;

    case 8:
      // mismatch of allocation/release method
      printf( "%s",mem );
      delete mem;
      mem = new char;
      mem[0] = 0;

      printf( "%s",mem );
      delete[] mem;
      mem = new char[100];
      mem[0] = 0;
      break;

    case 9:
      // missing return address of strcmp()
      {
        mem[15] = 'a';
        char *s = strdup( "abc" );
        if( !strcmp(mem+15,s) )
          strcat( mem,"a" );
        free( s );
      }
      break;

    case 10:
      // leak in dll
      {
        char *leak = (char*)dll_alloc( 10 );
        mem[1] = leak[0];

#ifndef _MSC_VER
#ifndef _WIN64
#define BITS "32"
#else
#define BITS "64"
#endif
#else
#define BITS
#endif
        HMODULE mod = LoadLibrary( "dll-alloc-shared" BITS ".dll" );
        if( mod )
        {
          typedef void *dll_alloc_func( size_t );
          dll_alloc_func *func =
            (dll_alloc_func*)GetProcAddress( mod,"dll_alloc" );
          if( func )
          {
            leak = (char*)func( 20 );
            mem[2] = leak[0];
          }
          FreeLibrary( mod );
        }
      }
      break;

    case 11:
      exit( arg );

    case 12:
      // exception handler
      {
        SetUnhandledExceptionFilter( &exceptionWalker );
        void *ptr = (void*)&choose;
        *(int*)ptr = 5;
      }
      break;

    case 13:
      // multiple free
      free( mem );
      break;

    case 14:
      // different page protection size
      {
        struct BigStruct
        {
          char c[5000];
        };
        BigStruct *bs = (BigStruct*)malloc( sizeof(BigStruct) );
        mem[1] = bs[1].c[4500];
      }
      break;

    case 15:
      // leak types
      {
        char *indirectly_reachable = (char*)malloc( 16 );
        static char **reachable;
        reachable = (char**)malloc( sizeof(char*) );
        *reachable = indirectly_reachable;
        mem[1] = reachable[0][0];
        *(char****)indirectly_reachable = &reachable;

        char *indirectly_lost = (char*)malloc( 32 );
        char *indirectly_lost2 = (char*)malloc( 32 );
        char **lost = (char**)malloc( sizeof(char*) );
        *(char**)indirectly_lost = indirectly_lost2;
        *lost = indirectly_lost;
        mem[2] = lost[0][0];

        char *indirectly_kinda_reachable = (char*)malloc( 64 );
        static char **kinda_reachable;
        kinda_reachable = (char**)malloc( 16 );
        *kinda_reachable = indirectly_kinda_reachable + 5;
        mem[3] = kinda_reachable[0][0];
        kinda_reachable++;
        *(char****)indirectly_kinda_reachable = &kinda_reachable;

        char **jointly_lost1 = (char**)malloc( 48 );
        char **jointly_lost2 = (char**)malloc( 48 );
        char *jointly_kinda_lost = (char*)malloc( 48 );
        *jointly_lost1 = (char*)jointly_lost2;
        *jointly_lost2 = (char*)jointly_lost1;
        jointly_lost1[1] = jointly_kinda_lost + 10;
        mem[4] = jointly_lost1[0][0];

        char **self_reference = (char**)malloc( 80 );
        *self_reference = (char*)self_reference + 5;
        mem[5] = **self_reference;
      }
      break;

    case 16:
      // access near freed block
      {
        struct BigStruct
        {
          char c[5000];
        };
        BigStruct *bs = (BigStruct*)malloc( sizeof(BigStruct) );
        mem[1] = bs[0].c[0];
        free( bs );
        mem[1] = bs[1].c[4500];
      }
      break;

    case 17:
      // memory leak contents
      {
        char *copy = strdup( "this is a memory leak" );
        wchar_t *wcopy = wcsdup( L"this is a bigger memory leak" );
        char *emptyness = (char*)malloc( 33 );
        emptyness[3] = '.';
        mem[1] = copy[0];
        mem[2] = wcopy[0];
        mem[3] = emptyness[0];
      }
      break;
  }

  mem = (char*)realloc( mem,30 );
  if( mem ) printf( "%s",mem );
  free( mem );
}


int main( int argc,char **argv )
{
  int arg;
  printf( "allocer: main()\n" );
  fflush( stdout );

  arg = argc>1 ? atoi( argv[1] ) : 0;
  choose( arg );

  return( arg );
}
