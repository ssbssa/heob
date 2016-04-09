
//          Copyright Hannes Domani 2014 - 2015.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// includes {{{

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef NO_DBGHELP
#include <dbghelp.h>
#endif

// }}}
// defines {{{

#define PTRS 48

#define USE_STACKWALK       1
#define WRITE_DEBUG_STRINGS 0

#define DLLEXPORT __declspec(dllexport)

#ifndef _MSC_VER
#define NOINLINE __attribute__((noinline))
#define NORETURN __attribute__((noreturn))
#define CODE_SEG(seg) __attribute__((section(seg)))
#define UNREACHABLE __builtin_unreachable()
#else
#define NOINLINE __declspec(noinline)
#define NORETURN __declspec(noreturn)
#define CODE_SEG(seg) __declspec(code_seg(seg))
#define UNREACHABLE __assume(0)
#endif

#if defined(NO_DBGHELP) && USE_STACKWALK
#undef USE_STACKWALK
#define USE_STACKWALK 0
#endif

// }}}
// function definitions {{{

typedef BOOL WINAPI func_VirtualProtect( LPVOID,SIZE_T,DWORD,PDWORD );
typedef HANDLE WINAPI func_GetCurrentProcess( VOID );
typedef BOOL WINAPI func_FlushInstructionCache( HANDLE,LPCVOID,SIZE_T );
typedef HMODULE WINAPI func_LoadLibraryA( LPCSTR );
typedef HMODULE WINAPI func_LoadLibraryW( LPCWSTR );
typedef BOOL WINAPI func_FreeLibrary( HMODULE );
typedef LPVOID WINAPI func_GetProcAddress( HMODULE,LPCSTR );
typedef LPTOP_LEVEL_EXCEPTION_FILTER WINAPI func_SetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER );
typedef VOID WINAPI func_ExitProcess( UINT );
typedef BOOL WINAPI func_TerminateProcess( HANDLE,UINT );

typedef void *func_malloc( size_t );
typedef void *func_calloc( size_t,size_t );
typedef void func_free( void* );
typedef void *func_realloc( void*,size_t );
typedef char *func_strdup( const char* );
typedef wchar_t *func_wcsdup( const wchar_t* );
typedef char *func_getcwd( char*,int );
typedef wchar_t *func_wgetcwd( wchar_t*,int );
typedef char *func_getdcwd( int,char*,int );
typedef wchar_t *func_wgetdcwd( int,wchar_t*,int );
typedef char *func_fullpath( char*,const char*,size_t );
typedef wchar_t *func_wfullpath( wchar_t*,const wchar_t*,size_t );
typedef char *func_tempnam( char*,char* );
typedef wchar_t *func_wtempnam( wchar_t*,wchar_t* );

#ifndef NO_DBGHELP
typedef DWORD WINAPI func_SymSetOptions( DWORD );
typedef BOOL WINAPI func_SymInitialize( HANDLE,PCSTR,BOOL );
typedef BOOL WINAPI func_SymGetLineFromAddr64(
    HANDLE,DWORD64,PDWORD,PIMAGEHLP_LINE64 );
typedef BOOL WINAPI func_SymFromAddr(
    HANDLE,DWORD64,PDWORD64,PSYMBOL_INFO );
typedef BOOL WINAPI func_SymCleanup( HANDLE );
typedef DWORD WINAPI func_SymAddrIncludeInlineTrace( HANDLE,DWORD64 );
typedef BOOL WINAPI func_SymQueryInlineTrace(
    HANDLE,DWORD64,DWORD,DWORD64,DWORD64,LPDWORD,LPDWORD );
typedef BOOL WINAPI func_SymGetLineFromInlineContext(
    HANDLE,DWORD64,ULONG,DWORD64,PDWORD,PIMAGEHLP_LINE64 );
typedef BOOL WINAPI func_SymFromInlineContext(
    HANDLE,DWORD64,ULONG,PDWORD64,PSYMBOL_INFO );
typedef BOOL WINAPI func_SymGetModuleInfo64(
    HANDLE,DWORD64,PIMAGEHLP_MODULE64 );
typedef DWORD64 WINAPI func_SymLoadModule64(
    HANDLE,HANDLE,PCSTR,PCSTR,DWORD64,DWORD );
#if USE_STACKWALK
typedef BOOL WINAPI func_StackWalk64(
    DWORD,HANDLE,HANDLE,LPSTACKFRAME64,PVOID,PREAD_PROCESS_MEMORY_ROUTINE64,
    PFUNCTION_TABLE_ACCESS_ROUTINE64,PGET_MODULE_BASE_ROUTINE64,
    PTRANSLATE_ADDRESS_ROUTINE64 );
#endif
#endif

// }}}
// disable memmove/memset {{{

#undef RtlMoveMemory
VOID WINAPI RtlMoveMemory( PVOID,const VOID*,SIZE_T );
#undef RtlZeroMemory
VOID WINAPI RtlZeroMemory( PVOID,SIZE_T );
#undef RtlFillMemory
VOID WINAPI RtlFillMemory( PVOID,SIZE_T,UCHAR );

// }}}
// remote data declarations {{{

typedef enum
{
  AT_MALLOC,
  AT_NEW,
  AT_NEW_ARR,
  AT_EXIT,
}
allocType;

typedef enum
{
  LT_LOST,
  LT_JOINTLY_LOST,
  LT_INDIRECTLY_LOST,
  LT_REACHABLE,
  LT_INDIRECTLY_REACHABLE,
  LT_COUNT,
}
leakType;

typedef enum
{
  FT_MALLOC,
  FT_CALLOC,
  FT_FREE,
  FT_REALLOC,
  FT_STRDUP,
  FT_WCSDUP,
  FT_OP_NEW,
  FT_OP_DELETE,
  FT_OP_NEW_A,
  FT_OP_DELETE_A,
  FT_GETCWD,
  FT_WGETCWD,
  FT_GETDCWD,
  FT_WGETDCWD,
  FT_FULLPATH,
  FT_WFULLPATH,
  FT_TEMPNAM,
  FT_WTEMPNAM,
  FT_COUNT,
}
funcType;

typedef struct
{
  union {
    void *ptr;
    int count;
  };
  size_t size;
  void *frames[PTRS];
  int id;
  allocType at : 8;
  leakType lt : 8;
  funcType ft : 8;
  funcType ftFreed : 8;
}
allocation;

typedef struct
{
  int protect;
  int align;
  int init;
  int slackInit;
  int protectFree;
  int handleException;
  int newConsole;
  int fullPath;
  int allocMethod;
  int leakDetails;
  int useSp;
  int dlls;
  int pid;
  int exitTrace;
  int sourceCode;
  int raiseException;
  int minProtectSize;
  int findNearest;
  int leakContents;
  int mergeLeaks;
}
options;

typedef struct remoteData
{
  HMODULE kernel32;
  func_VirtualProtect *fVirtualProtect;
  func_GetCurrentProcess *fGetCurrentProcess;
  func_FlushInstructionCache *fFlushInstructionCache;
  func_LoadLibraryA *fLoadLibraryA;
  func_LoadLibraryW *fLoadLibraryW;
  func_FreeLibrary *fFreeLibrary;
  func_GetProcAddress *fGetProcAddress;
  func_SetUnhandledExceptionFilter *fSetUnhandledExceptionFilter;
  func_ExitProcess *fExitProcess;

  HANDLE master;
  HANDLE initFinished;

  union {
    wchar_t exePath[MAX_PATH];
    char exePathA[MAX_PATH];
  };

  size_t injOffset;

  options opt;

  int raise_alloc_q;
  int raise_alloc_a[1];
}
remoteData;

void inj( remoteData *rd,HMODULE app );

// }}}
// extra communication declarations {{{

enum
{
#if WRITE_DEBUG_STRINGS
  WRITE_STRING,
#endif
  WRITE_LEAKS,
  WRITE_MODS,
  WRITE_EXCEPTION,
  WRITE_ALLOC_FAIL,
  WRITE_FREE_FAIL,
  WRITE_DOUBLE_FREE,
  WRITE_SLACK,
  WRITE_MAIN_ALLOC_FAIL,
  WRITE_WRONG_DEALLOC,
  WRITE_LEAK_CONTENTS,
  WRITE_RAISE_ALLOCATION,
};

typedef struct
{
  size_t base;
  size_t size;
  char path[MAX_PATH];
}
modInfo;

typedef struct
{
  EXCEPTION_RECORD er;
  allocation aa[3];
  int aq;
  int nearest;
}
exceptionInfo;

// }}}

// vim:fdm=marker
