
//          Copyright Hannes Domani 2014 - 2025.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// includes {{{

#define HEOB_INTERNAL
#include "heob.h"

#ifndef NO_DBGHELP
#include <dbghelp.h>
#endif

#ifdef _MSC_VER
#include <intrin.h>
#endif

#include <stddef.h>

// }}}
// defines {{{

#define PTRS 128

#define USE_STACKWALK       1

#define DLLEXPORT __declspec(dllexport)

#if !defined(_MSC_VER) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#define NORETURN __attribute__((noreturn))
#define CODE_SEG(seg) __attribute__((section(seg)))
#define UNREACHABLE __builtin_unreachable()
#define ASSUME_ALIGNED(v,p,a) (v=__builtin_assume_aligned(p,a))
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
#define ASSUME_ALIGNED(v,p,a) (v=(p),__assume(!(((uintptr_t)v)%(a))))
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

#if !defined(__aarch64__) && defined(_M_ARM64)
#define __aarch64__ 1
#endif

#ifndef _WIN64
#define IL_INT LONG
#define IL_INC(var) InterlockedIncrement(var)
#define READ_TEB_PTR(o) __readfsdword(o)
#define READ_TEB_DWORD(o) __readfsdword(o)
#define WRITE_TEB_DWORD(o,v) __writefsdword(o,v)
#else
#define IL_INT LONGLONG
#define IL_INC(var) InterlockedIncrement64(var)
#ifndef __aarch64__
#define READ_TEB_PTR(o) __readgsqword(o)
#define READ_TEB_DWORD(o) __readgsdword(o)
#define WRITE_TEB_DWORD(o,v) __writegsdword(o,v)
#else
#ifdef __GNUC__
register char *teb_reg18 __asm__("x18");
#else
#define teb_reg18 __getReg(18)
#endif
#define READ_TEB_PTR(o) (*(void**)(teb_reg18+o))
#define READ_TEB_DWORD(o) (*(DWORD*)(teb_reg18+o))
#define WRITE_TEB_DWORD(o,v) (*(DWORD*)(teb_reg18+o) = v)
#endif
#endif

#define GET_TEB() ((TEB*)READ_TEB_PTR(offsetof(TEB,Self)))
#define GET_PEB() ((PEB*)READ_TEB_PTR(offsetof(TEB,Peb)))
#define GET_LAST_ERROR() READ_TEB_DWORD(offsetof(TEB,LastErrorValue))
#define SET_LAST_ERROR(e) WRITE_TEB_DWORD(offsetof(TEB,LastErrorValue),e)

#ifdef __clang__
#define __writefsdword(o,v) \
  (*((volatile DWORD __attribute__((__address_space__(257)))*)(o)) = v)
#define __writegsdword(o,v) \
  (*((volatile DWORD __attribute__((__address_space__(256)))*)(o)) = v)
#elif defined(__GNUC__) && __GNUC__>=8
#define __readfsdword(o)    (*(__seg_fs unsigned int*)(o))
#define __writefsdword(o,v) (*(__seg_fs unsigned int*)(o) = v)

#define __readgsdword(o)    (*(__seg_gs unsigned int*)(o))
#define __writegsdword(o,v) (*(__seg_gs unsigned int*)(o) = v)

#define __readgsqword(o)    (*(__seg_gs unsigned long long*)(o))
#endif

#ifndef STATUS_INVALID_IMAGE_FORMAT
#define STATUS_INVALID_IMAGE_FORMAT 0xC000007B
#endif
#ifndef STATUS_THREAD_IS_TERMINATING
#define STATUS_THREAD_IS_TERMINATING 0xC000004B
#endif

#define EXCEPTION_FATAL_APP_EXIT STATUS_FATAL_APP_EXIT
#define EXCEPTION_ASSERTION_FAILURE STATUS_ASSERTION_FAILURE
#define EXCEPTION_HEAP_CORRUPTION 0xC0000374
#define EXCEPTION_STACK_BUFFER_OVERRUN STATUS_STACK_BUFFER_OVERRUN

#define EXCEPTION_VC_CPP_EXCEPTION 0xe06d7363
#ifndef _WIN64
#define THROW_ARGS 3
#define CALC_THROW_ARG(mod,ofs) (ofs)
#else
#define THROW_ARGS 4
#define CALC_THROW_ARG(mod,ofs) ((size_t)(mod)+(ofs))
#endif

#define REL_PTR( base,ofs ) ( (void*)(((PBYTE)base)+ofs) )

#ifndef _WIN64
#define csp Esp
#define cip Eip
#define cfp Ebp
#define MACH_TYPE IMAGE_FILE_MACHINE_I386
#define PROC_ARCH PROCESSOR_ARCHITECTURE_INTEL
#else
#ifndef __aarch64__
#define csp Rsp
#define cip Rip
#define cfp Rbp
#define MACH_TYPE IMAGE_FILE_MACHINE_AMD64
#define PROC_ARCH PROCESSOR_ARCHITECTURE_AMD64
#else
#define csp Sp
#define cip Pc
#define cfp Fp
#define MACH_TYPE IMAGE_FILE_MACHINE_ARM64
#define PROC_ARCH PROCESSOR_ARCHITECTURE_ARM64
#endif
#endif

#ifndef IMAGE_FILE_MACHINE_ARM64
#define IMAGE_FILE_MACHINE_ARM64 0xaa64
#endif
#ifndef PROCESSOR_ARCHITECTURE_ARM64
#define PROCESSOR_ARCHITECTURE_ARM64 12
#endif

#define SHARED_USER_DATA 0x7ffe0000

// }}}
// kernel32.dll function definitions {{{

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
typedef BOOL WINAPI func_InitializeCriticalSectionEx(
    LPCRITICAL_SECTION,DWORD,DWORD );
typedef HRESULT WINAPI func_GetThreadDescription( HANDLE,PWSTR* );
typedef BOOL WINAPI func_IsWow64Process2( HANDLE,USHORT*,USHORT* );
typedef BOOL WINAPI func_GetProcessInformation(
    HANDLE,PROCESS_INFORMATION_CLASS,LPVOID,DWORD );

// }}}
// CRT function definitions {{{

typedef void *func_signal( int,void(*)(int) );
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
typedef int *func_errno( void );

// }}}
// dbghelp.dll function definitions {{{

#ifndef NO_DBGHELP
typedef DWORD WINAPI func_SymSetOptions( DWORD );
typedef BOOL WINAPI func_SymInitialize( HANDLE,PCSTR,BOOL );
typedef BOOL WINAPI func_SymInitializeW( HANDLE,PCWSTR,BOOL );
typedef BOOL WINAPI func_SymGetLineFromAddr64(
    HANDLE,DWORD64,PDWORD,PIMAGEHLP_LINE64 );
typedef BOOL WINAPI func_SymGetLineFromAddrW64(
    HANDLE,DWORD64,PDWORD,PIMAGEHLP_LINEW64 );
typedef BOOL WINAPI func_SymFromAddr(
    HANDLE,DWORD64,PDWORD64,PSYMBOL_INFO );
typedef BOOL WINAPI func_SymCleanup( HANDLE );
typedef DWORD WINAPI func_SymAddrIncludeInlineTrace( HANDLE,DWORD64 );
typedef BOOL WINAPI func_SymQueryInlineTrace(
    HANDLE,DWORD64,DWORD,DWORD64,DWORD64,LPDWORD,LPDWORD );
typedef BOOL WINAPI func_SymGetLineFromInlineContextW(
    HANDLE,DWORD64,ULONG,DWORD64,PDWORD,PIMAGEHLP_LINEW64 );
typedef BOOL WINAPI func_SymFromInlineContext(
    HANDLE,DWORD64,ULONG,PDWORD64,PSYMBOL_INFO );
typedef BOOL WINAPI func_SymGetModuleInfo64(
    HANDLE,DWORD64,PIMAGEHLP_MODULE64 );
typedef DWORD64 WINAPI func_SymLoadModule64(
    HANDLE,HANDLE,PCSTR,PCSTR,DWORD64,DWORD );
typedef DWORD64 WINAPI func_SymLoadModuleExW(
    HANDLE,HANDLE,PCWSTR,PCWSTR,DWORD64,DWORD,PMODLOAD_DATA,DWORD );
typedef DWORD WINAPI func_UnDecorateSymbolName( PCSTR,PSTR,DWORD,DWORD );
#if USE_STACKWALK
typedef BOOL WINAPI func_StackWalk64(
    DWORD,HANDLE,HANDLE,LPSTACKFRAME64,PVOID,PREAD_PROCESS_MEMORY_ROUTINE64,
    PFUNCTION_TABLE_ACCESS_ROUTINE64,PGET_MODULE_BASE_ROUTINE64,
    PTRANSLATE_ADDRESS_ROUTINE64 );
typedef BOOL WINAPI func_SymRegisterCallback64(
    HANDLE,PSYMBOL_REGISTERED_CALLBACK64,ULONG64 );
typedef BOOL WINAPI func_SymFindFileInPathW(
    HANDLE,PCWSTR,PCWSTR,PVOID,DWORD,DWORD,DWORD,PWSTR,
    PFINDFILEINPATHCALLBACKW,PVOID );
#endif
typedef BOOL WINAPI func_MiniDumpWriteDump(
    HANDLE,DWORD,HANDLE,MINIDUMP_TYPE,PMINIDUMP_EXCEPTION_INFORMATION,
    PMINIDUMP_USER_STREAM_INFORMATION,PMINIDUMP_CALLBACK_INFORMATION );
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
  ProcessBasicInformation=0,
  ProcessImageFileName=27,
}
PROCESSINFOCLASS;

typedef enum
{
  ThreadBasicInformation,
  ThreadQuerySetWin32StartAddress=9,
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
  PVOID SubSystemTib;
  PVOID FiberData;
  PVOID ArbitraryUserPointer;
  struct _TEB *Self;
  PVOID Reserved1[5];
  struct _PEB *Peb;
  ULONG LastErrorValue;
  ULONG CountOfOwnedCriticalSections;
  PVOID Reserved2[399];
  BYTE Reserved3[1944];
  PVOID TlsSlots[64];
  BYTE Reserved4[8];
  PVOID Reserved5[26];
  PVOID ReservedForOle;
  PVOID Reserved6[4];
  PVOID *TlsExpansionSlots;
}
TEB, *PTEB;

typedef struct _PEB_LDR_DATA
{
  BYTE Reserved1[8];
  PVOID Reserved2[1];
  LIST_ENTRY InLoadOrderModuleList;
  LIST_ENTRY InMemoryOrderModuleList;
  LIST_ENTRY InInitializationOrderModuleList;
}
PEB_LDR_DATA, *PPEB_LDR_DATA;

enum
{
  IMAGE_DLL            =0x00000004,
  ENTRY_PROCESSED      =0x00004000,
  PROCESS_ATTACH_CALLED=0x00080000,
};

typedef struct _LDR_DATA_TABLE_ENTRY
{
  LIST_ENTRY InLoadOrderModuleList;
  LIST_ENTRY InMemoryOrderModuleList;
  LIST_ENTRY InInitializationOrderModuleList;
  PVOID DllBase;
  PVOID EntryPoint;
  ULONG SizeOfImage;
  UNICODE_STRING FullDllName;
  UNICODE_STRING BaseDllName;
  ULONG Flags;
  USHORT LoadCount;
  USHORT TlsIndex;
  LIST_ENTRY HashLinks;
  ULONG TimeDateStamp;
}
LDR_DATA_TABLE_ENTRY, *PLDR_DATA_TABLE_ENTRY;

typedef struct
{
  USHORT Flags;
  USHORT Length;
  ULONG TimeStamp;
  UNICODE_STRING DosPath;
}
RTL_DRIVE_LETTER_CURDIR;

typedef struct
{
  ULONG MaximumLength;
  ULONG Length;
  DWORD Reserved1[3];
  PVOID Reserved2[4];
  UNICODE_STRING CurrentDirectory;
  PVOID CurrentDirectoryHandle;
  UNICODE_STRING DllPath;
  UNICODE_STRING ImagePathName;
  UNICODE_STRING CommandLine;
  PVOID Reserved3;
  ULONG Reserved4[9];
  UNICODE_STRING Reserved5[4];
  RTL_DRIVE_LETTER_CURDIR CurrentDirectories[32];
  UINT_PTR Reserved6[2];
  PVOID Reserved7;
  ULONG Reserved8;
  ULONG LoaderThreads;
}
RTL_USER_PROCESS_PARAMETERS;

typedef struct _PEB
{
  PVOID Reserved1[3];
  PPEB_LDR_DATA Ldr;
  RTL_USER_PROCESS_PARAMETERS *ProcessParameters;
}
PEB, *PPEB;

typedef struct _PROCESS_BASIC_INFORMATION
{
  PVOID Reserved1;
  PPEB PebBaseAddress;
  PVOID Reserved2[2];
  ULONG_PTR UniqueProcessId;
  PVOID Reserved3;
}
PROCESS_BASIC_INFORMATION;

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

typedef VOID (NTAPI *PKNORMAL_ROUTINE)( PVOID,PVOID,PVOID );
typedef LONG NTAPI func_NtQueueApcThread(
    HANDLE,PKNORMAL_ROUTINE,PVOID,PVOID,PVOID );
typedef LONG NTAPI func_LdrGetDllHandle(
    PWSTR,PULONG,UNICODE_STRING*,HMODULE* );
typedef LONG NTAPI func_NtSetEvent( HANDLE,PLONG );
typedef LONG NTAPI func_NtDelayExecution( BOOL,PLARGE_INTEGER );
typedef LONG NTAPI func_NtGetNextThread(
    HANDLE,HANDLE,ACCESS_MASK,ULONG,ULONG,HANDLE* );

typedef LONG NTAPI func_RtlGetVersion( PRTL_OSVERSIONINFOW );
typedef LARGE_INTEGER NTAPI func_RtlExtendedLargeIntegerDivide(
    LARGE_INTEGER,ULONG,PULONG );

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

  FT_BLOCKED, // sampled thread is blocked
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
      unsigned recording : 1;
      unsigned raiseFree : 1;
      unsigned unusedBits : 2;
      leakType lt : 8;
      funcType ft : 8;
      funcType ftFreed : 8;
    };
    struct {
      unsigned char spacing[3];
      unsigned char frameCount;
    };
  };
#ifndef NO_THREADS
  int threadNum;
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
#if USE_STACKWALK
  int samplingInterval;
#endif
  int forwardStartupInfo;
  int disableParallelLoading;
}
options;

typedef struct
{
  int type;
  int cyg_argc;
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

  func_LdrGetDllHandle *fLdrGetDllHandle;
  func_NtSetEvent *fNtSetEvent;
  func_NtDelayExecution *fNtDelayExecution;

  HANDLE master;
  HANDLE controlPipe;
  HANDLE initFinished;
  HANDLE startMain;
  HANDLE exceptionWait;
#ifndef NO_DBGHELP
  HANDLE miniDumpWait;
#endif
#if USE_STACKWALK
  HANDLE heobProcess;
  HANDLE samplingStop;
  int *noStackWalkRemote;
#endif
#ifndef NO_THREADS
  DWORD threadNumTlsRemote;
#endif

  wchar_t exePath[MAX_PATH];
  size_t injOffset;

  UNICODE_STRING kernelName;
  WCHAR kernelNameBuffer[13];

  options opt;
  options globalopt;
  wchar_t *specificOptions;
  DWORD appCounterID;

  int recording;
  int *recordingRemote;

  attachedProcessInfo *api;

  wchar_t subOutName[MAX_PATH];
  wchar_t subXmlName[MAX_PATH];
  wchar_t subSvgName[MAX_PATH];
  wchar_t subCurDir[MAX_PATH];
  wchar_t subSymPath[16384];

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
  WRITE_FREE_WHILE_REALLOC,
  WRITE_WRONG_DEALLOC,
  WRITE_RAISE_ALLOCATION,
#ifndef NO_THREADS
  WRITE_THREAD_NAME,
  WRITE_THREAD_ID,
#endif
  WRITE_EXIT_TRACE,
  WRITE_EXIT,
  WRITE_RECORDING,
#if USE_STACKWALK
  WRITE_SAMPLING,
  WRITE_ADD_SAMPLING_THREAD,
  WRITE_REMOVE_SAMPLING_THREAD,
#endif
#ifndef NO_DBGHELP
  WRITE_CRASHDUMP,
#endif
  WRITE_REFERENCE,
};

typedef struct
{
  size_t base;
  size_t size;
  wchar_t path[MAX_PATH];
  DWORD versionMS;
  DWORD versionLS;
  DWORD timestamp;
}
modInfo;

typedef struct
{
  EXCEPTION_RECORD er;
  CONTEXT c;
#if USE_STACKWALK
  HANDLE thread;
#endif
  allocation aa[3];
  int aq;
  int nearest;
  char throwName[1024];
}
exceptionInfo;

#if USE_STACKWALK
typedef struct
{
  HANDLE thread;
#ifndef NO_THREADS
  int threadNum;
#endif
  DWORD threadId;
  ULONG64 cycleTime;
}
threadSamplingType;
#endif

// }}}
// common functions {{{

char *num2hexstr( char *str,UINT64 arg,int count );
wchar_t *num2hexstrW( wchar_t *str,UINT64 arg,int count );
wchar_t *num2strW( wchar_t *start,uintptr_t arg,int minus );
wchar_t *mstrrchrW( const wchar_t *s,wchar_t c );
int strstart( const char *str,const char *start );
int isWrongArch( HANDLE process,USHORT *machProcOut );
int heobSubProcess(
    DWORD creationFlags,LPPROCESS_INFORMATION processInformation,
    HMODULE heobMod,HANDLE heap,options *opt,DWORD appCounterID,
    func_CreateProcessW *fCreateProcessW,
    const wchar_t *subOutName,const wchar_t *subXmlName,
    const wchar_t *subSvgName,const wchar_t *subCurDir,
    const wchar_t *subSymPath,
    int raise_alloc_q,size_t *raise_alloc_a,const wchar_t *specificOptions );
int convertDeviceName( const wchar_t *in,wchar_t *out,int outlen );
const char *thunkedFunctionNameByAddress(
    HMODULE mod,uintptr_t base,uintptr_t addr,const char *funcname );

static inline int mul_overflow( size_t n,size_t s,size_t *res )
{
#ifndef _MSC_VER
#if defined(__GNUC__) && __GNUC__>=5
  if( UNLIKELY(__builtin_mul_overflow(n,s,res)) )
    return( 1 );
#else
  if( UNLIKELY(s && n>(size_t)-1/s) )
    return( 1 );
  *res = n*s;
#endif
#else
#ifndef _WIN64
  unsigned __int64 res64 = __emulu( n,s );
  if( UNLIKELY(res64>(size_t)res64) )
    return( 1 );
  *res = (size_t)res64;
#else
#ifndef __aarch64__
  size_t resHigh = 0;
  *res = _umul128( n,s,&resHigh );
  if( UNLIKELY(resHigh) )
    return( 1 );
#else
  if( UNLIKELY(__umulh(n,s)) )
    return( 1 );
  *res = n*s;
#endif
#endif
#endif

  return( 0 );
}

// }}}
// KUSER_SHARED_DATA {{{

typedef struct
{
  ULONG LowPart;
  ULONG High1Part;
  ULONG High2Part;
}
KSYSTEM_TIME;

typedef struct
{
  ULONG TickCountLow;
  ULONG TickCountMultiplier;
  KSYSTEM_TIME InterruptTime;
  KSYSTEM_TIME SystemTime;
  KSYSTEM_TIME TimeZoneBias;
  USHORT ImageNumberLow;
  USHORT ImageNumberHigh;
}
KUSER_SHARED_DATA;

// }}}

// vim:fdm=marker
