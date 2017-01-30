
//          Copyright Hannes Domani 2014 - 2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <direct.h>
#include <conio.h>


#if !defined(_MSC_VER) || _MSC_VER<1900
__declspec(dllimport) void *operator new[]( size_t );
__declspec(dllimport) void operator delete[]( void* );
#endif

extern "C" __declspec(dllimport) void *dll_alloc( size_t );
extern "C" __declspec(dllimport) void *dll_memory( void );
extern "C" __declspec(dllimport) int *dll_int( void );
extern "C" __declspec(dllimport) int *dll_arr( void );
extern "C" __declspec(dllimport) DWORD dll_thread_id( void );
extern "C" __declspec(dllimport) void do_nothing( void* );


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


static DWORD WINAPI workerThread( LPVOID arg )
{
  int alloc_count = (UINT_PTR)arg;
  DWORD sum = 0;
  unsigned char **allocs = (unsigned char**)malloc(
      alloc_count*sizeof(unsigned char*) );
  for( int i=0; i<alloc_count; i++ )
    allocs[i] = (unsigned char*)malloc( 16 );
  for( int i=0; i<alloc_count; i++ )
    sum += allocs[i][0];
  for( int i=0; i<alloc_count; i++ )
    free( allocs[i] );
  free( allocs );
  return( sum );
}


const DWORD EXCEPTION_THREAD_NAME = 0x406D1388;

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
  DWORD dwType;
  LPCSTR szName;
  DWORD dwThreadID;
  DWORD dwFlags;
} THREADNAME_INFO;
#pragma pack(pop)

static LONG CALLBACK ignoreNameException( PEXCEPTION_POINTERS ExceptionInfo )
{
  if( ExceptionInfo->ExceptionRecord->ExceptionCode==EXCEPTION_THREAD_NAME )
    return EXCEPTION_CONTINUE_EXECUTION;
  return EXCEPTION_CONTINUE_SEARCH;
}

static void SetThreadName( DWORD dwThreadID,const char *threadName )
{
  THREADNAME_INFO info;
  info.dwType = 0x1000;
  info.szName = threadName;
  info.dwThreadID = dwThreadID;
  info.dwFlags = 0;

  PVOID veh = AddVectoredExceptionHandler( 1,ignoreNameException );

  RaiseException( EXCEPTION_THREAD_NAME,0,
      sizeof(info)/sizeof(ULONG_PTR),(ULONG_PTR*)&info );

  RemoveVectoredExceptionHandler( veh );
}

static DWORD WINAPI namedThread( LPVOID arg )
{
  if( arg )
    SetThreadName( -1,"self named thread" );

  char *leak = (char*)malloc( 10+(arg?0:16) );
  return leak[0];
}


void choose( int arg )
{
  printf( "allocer: main()\n" );
  fflush( stdout );

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
#define BIGNUM ((size_t)1 << (sizeof(void*)*8-1))
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
      free( (void*)(size_t)0x80000000 );
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

#ifndef _WIN64
#define BITS "32"
#else
#define BITS "64"
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
      // exit-call
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
        char *indirectly_lost2 = (char*)malloc( 0 );
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

    case 18:
      // merge identical memory leaks
      {
        char *free_me = NULL;
        for( int i=0; i<6; i++ )
        {
          char *copy = strdup( "memory leak X" );
          mem[i+1] = copy[0];
          copy[12] = '0' + i;
          if( !i ) free_me = copy;
          if( i==2 ) free( free_me );
        }
      }
      break;

    case 19:
      // realloc() of memory allocated in dll initialization
      {
        void *memory = dll_memory();
        char *new_memory = (char*)realloc( memory,501 );
        new_memory[0] = 'a';
      }
      break;

    case 20:
      // calloc() multiplication overflow
      {
#ifndef _WIN64
#define HALF_OVERFLOW 0x80000005
#else
#define HALF_OVERFLOW 0x8000000000000005
#endif
        char *m = (char*)calloc( HALF_OVERFLOW,2 );
        if( m ) mem[1] = m[0];
      }
      break;

    case 21:
      // delete memory allocated in dll initialization
      {
        int *one_int = dll_int();
        delete one_int;

        int *arr_int = dll_arr();
        delete[] arr_int;
      }
      break;

    case 22:
      // self-termination
      TerminateProcess( GetCurrentProcess(),arg );
      break;

    case 23:
      // initial value
      {
        char *emptyness = (char*)malloc( 30 );
        mem[1] = emptyness[0];
      }
      break;

#define THREAD_COUNT 8
#define ALLOC_COUNT (256*1024)
    case 24:
      // benchmark: single thread
      workerThread( (LPVOID)(THREAD_COUNT*ALLOC_COUNT) );
      break;

    case 25:
      // benchmark: multi-threading
      {
        HANDLE threads[THREAD_COUNT];
        for( int i=0; i<THREAD_COUNT; i++ )
          threads[i] = CreateThread( NULL,0,
              &workerThread,(LPVOID)ALLOC_COUNT,0,NULL );
        WaitForMultipleObjects( THREAD_COUNT,threads,TRUE,INFINITE );
        for( int i=0; i<THREAD_COUNT; i++ )
          CloseHandle( threads[i] );
      }
      break;

    case 26:
      // thread names
      {
        SetThreadName( -1,"main thread" );
        char *mainLeak = (char*)malloc( 11 );
        mem[1] = mainLeak[0];

        HANDLE thread = CreateThread(
            NULL,0,&namedThread,(void*)1,0,NULL );
        WaitForSingleObject( thread,INFINITE );
        CloseHandle( thread );

        DWORD threadId;
        thread = CreateThread(
            NULL,0,&namedThread,NULL,CREATE_SUSPENDED,&threadId );
        SetThreadName( threadId,"remotely named thread" );
        ResumeThread( thread );
        WaitForSingleObject( thread,INFINITE );
        CloseHandle( thread );
      }
      break;

    case 27:
      // benchmark: leak type detection
      {
        unsigned char v = 0;
        for( int i=0; i<4000; i++ )
        {
          unsigned char *b = (unsigned char*)malloc( 10000 );
          v += b[0];
        }
        mem[1] = v;
      }
      break;

    case 28:
      // leak until escape
      while( 1 )
      {
        int c = _getch();
        if( c==27 ) break;

        if( c=='f' )
        {
          free( (void*)0x1 );
          continue;
        }

        unsigned char *b = (unsigned char*)malloc( 15 );
        b[0] = c;
        mem[1] += b[0];
      }
      break;

    case 29:
      // compare thread id with dll
      printf( "thread id: %s\n",
          GetCurrentThreadId()==dll_thread_id()?"same":"different" );
      break;

    case 30:
      // suspended sub-process
      {
        STARTUPINFO si;
        PROCESS_INFORMATION pi;
        RtlZeroMemory( &si,sizeof(STARTUPINFO) );
        RtlZeroMemory( &pi,sizeof(PROCESS_INFORMATION) );
        si.cb = sizeof(STARTUPINFO);
        char commandLine[20];
        sprintf( commandLine,"allocer%d 28",8*(int)sizeof(void*) );
        BOOL result = CreateProcess( NULL,commandLine,NULL,NULL,FALSE,
            CREATE_SUSPENDED,NULL,NULL,&si,&pi );
        if( !result ) break;

        printf( "processId: %lu\n",pi.dwProcessId );
        printf( "threadId: %lu\n",pi.dwThreadId );
        fflush( stdout );
        WaitForSingleObject( pi.hProcess,INFINITE );

        CloseHandle( pi.hThread );
        CloseHandle( pi.hProcess );
      }
      break;

    case 31:
      // free of invalid pointer
      {
        int d = 8/sizeof(void*);
        unsigned char **ref = (unsigned char**)malloc(
            d*4*sizeof(void*) );
        ref[0*d] = (unsigned char*)malloc( 10 ) + 1;
        ref[1*d] = (unsigned char*)malloc( 20 ) - 2;
        unsigned char *noref = (unsigned char*)malloc( 30 );
        ref[2*d] = (unsigned char*)(size_t)0xabcdef01;
        ref[3*d] = (unsigned char*)&ref;
        do_nothing( ref );
        mem[1] = ref[3*d]!=NULL;
        mem[2] = noref[0];
        free( ref[1*d]+2 );
        free( ref[0*d] );
        free( ref[1*d] );
        free( noref-1 );
        free( ref[2*d] );
        free( ref[3*d] );
        free( ref );
      }
      break;

    case 32:
      // free allocation of different CRT
      {
        HMODULE msvcr100 = LoadLibrary( "msvcr100.dll" );
        if( msvcr100 )
        {
          typedef void *func_malloc( size_t );
          func_malloc *f_malloc =
            (func_malloc*)GetProcAddress( msvcr100,"malloc" );
          if( f_malloc )
          {
            void *ptr = f_malloc( 128 );
            free( ptr );
          }
          FreeLibrary( msvcr100 );
        }
      }
      break;

    case 33:
      // failed re-allocation
      {
        char *small = (char*)malloc( 16 );
        mem[1] = small[0];
        char *big = (char*)realloc( small,BIGNUM );
        if( big ) small = big;
        mem[2] = small[0];
      }
      break;

    case 34:
      // 0-sized allocation
      {
        char *m0 = (char*)malloc( 0 );
        printf( "m0: 0x%p\n",m0 );
        free( m0 );
        char *m16 = (char*)malloc( 16 );
        char *r0 = (char*)realloc( m16,0 );
        printf( "r0: 0x%p\n",r0 );
        free( r0 );
      }
      break;
  }

  mem = (char*)realloc( mem,30 );
  if( mem ) printf( "%s",mem );
  free( mem );
}


#ifndef __MINGW32__
int main( int argc,char **argv )
{
  int arg = argc>1 ? atoi( argv[1] ) : 0;
  choose( arg );

  return( arg );
}
#else
extern "C" void mainCRTStartup( void )
{
  const char *cmdLine = GetCommandLineA();
  if( cmdLine[0]=='"' )
  {
    cmdLine = strchr( cmdLine+1,'"' );
    if( cmdLine ) cmdLine++;
  }
  else
    cmdLine = strchr( cmdLine,' ' );
  if( cmdLine ) while( *cmdLine==' ' ) cmdLine++;

  int ret = -1;
  while( cmdLine && *cmdLine )
  {
    int arg = atoi( cmdLine );
    while( *cmdLine && *cmdLine!=' ' ) cmdLine++;
    while( *cmdLine==' ' ) cmdLine++;
    choose( arg );
    if( ret<0 ) ret = arg;
  }

  ExitProcess( ret );
}
#endif
