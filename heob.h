
//          Copyright Hannes Domani 2014 - 2017.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// includes {{{

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef NO_DBGHELP
#include <dbghelp.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#endif

// }}}
// defines {{{

#define PTRS 128

#define USE_STACKWALK       1

#define DLLEXPORT __declspec(dllexport)

#ifndef _MSC_VER
#define NOINLINE __attribute__((noinline))
#define NORETURN __attribute__((noreturn))
#define CODE_SEG(seg) __attribute__((section(seg)))
#define UNREACHABLE __builtin_unreachable()
#define ASSUME_ALIGNED(p,a) __builtin_assume_aligned(p,a)
#define ASSUME(c) ({ if( !(c) ) __builtin_unreachable(); })
#define LIKELY(c) __builtin_expect(!!(c),1)
#define UNLIKELY(c) __builtin_expect(!!(c),0)
#define RETURN_ADDRESS() __builtin_return_address(0)
#define FRAME_ADDRESS() __builtin_frame_address(0)
#else
#define NOINLINE __declspec(noinline)
#define NORETURN __declspec(noreturn)
#define CODE_SEG(seg) __declspec(code_seg(seg))
#define UNREACHABLE __assume(0)
#define ASSUME_ALIGNED(p,a) (p)
#define ASSUME(c) __assume(c)
#define LIKELY(c) (c)
#define UNLIKELY(c) (c)
#define RETURN_ADDRESS() _ReturnAddress()
#define FRAME_ADDRESS() _AddressOfReturnAddress()
#endif

#if defined(NO_DBGHELP) && USE_STACKWALK
#undef USE_STACKWALK
#define USE_STACKWALK 0
#endif

// }}}
// function definitions {{{

typedef DWORD WINAPI func_QueueUserAPC( PAPCFUNC,HANDLE,ULONG_PTR );
typedef HANDLE WINAPI func_GetCurrentThread( VOID );
typedef BOOL WINAPI func_VirtualProtect( LPVOID,SIZE_T,DWORD,PDWORD );
typedef HANDLE WINAPI func_GetCurrentProcess( VOID );
typedef BOOL WINAPI func_FlushInstructionCache( HANDLE,LPCVOID,SIZE_T );
typedef HMODULE WINAPI func_LoadLibraryA( LPCSTR );
typedef HMODULE WINAPI func_LoadLibraryW( LPCWSTR );
typedef HMODULE WINAPI func_LoadLibraryExA( LPCSTR,HANDLE,DWORD );
typedef HMODULE WINAPI func_LoadLibraryExW( LPCWSTR,HANDLE,DWORD );
typedef BOOL WINAPI func_FreeLibrary( HMODULE );
typedef LPVOID WINAPI func_GetProcAddress( HMODULE,LPCSTR );
typedef LPTOP_LEVEL_EXCEPTION_FILTER WINAPI func_SetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER );
typedef VOID WINAPI func_ExitProcess( UINT );
typedef BOOL WINAPI func_TerminateProcess( HANDLE,UINT );
typedef VOID WINAPI func_RaiseException( DWORD,DWORD,DWORD,const ULONG_PTR* );
typedef VOID WINAPI func_FreeLibraryAndExitThread( HMODULE,DWORD );
typedef BOOL WINAPI func_CreateProcessA(
    LPCSTR,LPSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,
    LPVOID,LPCSTR,LPSTARTUPINFO,LPPROCESS_INFORMATION );
typedef BOOL WINAPI func_CreateProcessW(
    LPCWSTR,LPWSTR,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL,DWORD,
    LPVOID,LPCWSTR,LPSTARTUPINFOW,LPPROCESS_INFORMATION );

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
typedef void func_free_dbg( void*,int );
typedef void *func_recalloc( void*,size_t,size_t );

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
typedef DWORD WINAPI func_UnDecorateSymbolName( PCSTR,PSTR,DWORD,DWORD );
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
  FT_FREE_DBG,
  FT_RECALLOC,
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
  size_t id;
  union {
    struct {
      allocType at : 4;
      int recording : 4;
      leakType lt : 8;
      funcType ft : 8;
      funcType ftFreed : 8;
    };
    struct {
      unsigned char spacing[3];
      unsigned char frameCount;
    };
  };
#ifndef NO_THREADNAMES
  int threadNameIdx;
#endif
}
allocation;

typedef struct
{
  int protect;
  int align;
  UINT64 init;
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
  int groupLeaks;
  size_t minLeakSize;
  int leakRecording;
  int attached;
  int children;
  int leakErrorExitCode;
  int exceptionDetails;
}
options;

typedef struct
{
  wchar_t commandLine[32768];
  wchar_t currentDirectory[MAX_PATH];
  wchar_t stdinName[32768];
  wchar_t stdoutName[32768];
  wchar_t stderrName[32768];
}
attachedProcessInfo;

typedef struct remoteData
{
  HMODULE heobMod;
  HMODULE kernel32;
  func_QueueUserAPC *fQueueUserAPC;
  func_GetCurrentThread *fGetCurrentThread;
  func_VirtualProtect *fVirtualProtect;
  func_GetCurrentProcess *fGetCurrentProcess;
  func_FlushInstructionCache *fFlushInstructionCache;
  func_LoadLibraryW *fLoadLibraryW;
  func_GetProcAddress *fGetProcAddress;

  HANDLE master;
  HANDLE controlPipe;
  HANDLE initFinished;
  HANDLE startMain;
#ifndef NO_DBGENG
  HANDLE exceptionWait;
#endif

  wchar_t exePath[MAX_PATH];

  size_t injOffset;

  options opt;
  options globalopt;
  char *specificOptions;

  int recording;

  attachedProcessInfo *api;

  char subOutName[MAX_PATH];
  char subXmlName[MAX_PATH];
  char subCurDir[MAX_PATH];

  int noCRT;

  int raise_alloc_q;
  int raise_alloc_a[1];
}
remoteData;

VOID CALLBACK heob( ULONG_PTR arg );

// }}}
// extra communication declarations {{{

enum
{
  WRITE_LEAKS,
  WRITE_MODS,
  WRITE_EXCEPTION,
  WRITE_ALLOC_FAIL,
  WRITE_FREE_FAIL,
  WRITE_DOUBLE_FREE,
  WRITE_SLACK,
  WRITE_MAIN_ALLOC_FAIL,
  WRITE_WRONG_DEALLOC,
  WRITE_RAISE_ALLOCATION,
#ifndef NO_THREADNAMES
  WRITE_THREAD_NAMES,
#endif
  WRITE_EXIT,
  WRITE_RECORDING,
};

enum
{
  LEAK_RECORDING_STOP,
  LEAK_RECORDING_START,
  LEAK_RECORDING_CLEAR,
  LEAK_RECORDING_SHOW,
  LEAK_RECORDING_STATE=-1,
  LEAK_COUNT=-2,
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
  CONTEXT c;
  allocation aa[3];
  int aq;
  int nearest;
}
exceptionInfo;

#ifndef NO_THREADNAMES
typedef struct
{
  char name[64];
}
threadNameInfo;
#endif

// }}}
// ntdll.dll function definitions {{{

typedef enum
{
  ObjectNameInformation=1,
}
OBJECT_INFORMATION_CLASS;

typedef enum
{
  ProcessImageFileName=27,
}
PROCESSINFOCLASS;

typedef enum
{
  ThreadBasicInformation,
}
THREADINFOCLASS;

typedef struct
{
  USHORT Length;
  USHORT MaximumLength;
  PWSTR Buffer;
}
UNICODE_STRING;

typedef struct
{
  UNICODE_STRING Name;
  WCHAR NameBuffer[0xffff];
}
OBJECT_NAME_INFORMATION;

typedef struct _TEB
{
  PVOID ExceptionList;
  PVOID StackBase;
  PVOID StackLimit;
  BYTE Reserved1[1952];
  PVOID Reserved2[409];
  PVOID TlsSlots[64];
  BYTE Reserved3[8];
  PVOID Reserved4[26];
  PVOID ReservedForOle;
  PVOID Reserved5[4];
  PVOID *TlsExpansionSlots;
}
TEB, *PTEB;

typedef struct _CLIENT_ID
{
  PVOID UniqueProcess;
  PVOID UniqueThread;
}
CLIENT_ID, *PCLIENT_ID;

typedef DWORD KPRIORITY;

typedef struct _THREAD_BASIC_INFORMATION
{
  LONG ExitStatus;
  PTEB TebBaseAddress;
  CLIENT_ID ClientId;
  KAFFINITY AffinityMask;
  KPRIORITY Priority;
  KPRIORITY BasePriority;
}
THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

typedef LONG NTAPI func_NtQueryObject(
    HANDLE,OBJECT_INFORMATION_CLASS,PVOID,ULONG,PULONG );
typedef LONG NTAPI func_NtQueryInformationProcess(
    HANDLE,PROCESSINFOCLASS,PVOID,ULONG,PULONG );
typedef LONG NTAPI func_NtQueryInformationThread(
    HANDLE,THREADINFOCLASS,PVOID,ULONG,PULONG );

// }}}
// common functions {{{

char *num2hexstr( char *str,UINT64 arg,int count );
char *num2str( char *start,uintptr_t arg,int minus );
char *mstrrchr( const char *s,char c );
int strstart( const char *str,const char *start );
int isWrongArch( HANDLE process );
int heobSubProcess(
    DWORD creationFlags,LPPROCESS_INFORMATION processInformation,
    HMODULE heobMod,HANDLE heap,options *opt,
    func_CreateProcessA *fCreateProcessA,
    const char *subOutName,const char *subXmlName,const char *subCurDir,
    int raise_alloc_q,size_t *raise_alloc_a,const char *specificOptions );

// }}}

// vim:fdm=marker
