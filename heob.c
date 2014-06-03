
//          Copyright Hannes Domani 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dbghelp.h>
#include <dwarfstack.h>


#define PTRS 48

#define USE_STACKWALK       0
#define WRITE_DEBUG_STRINGS 0


typedef HMODULE WINAPI func_LoadLibraryW( LPCWSTR );
typedef LPVOID WINAPI func_GetProcAddress( HMODULE,LPCSTR );
typedef BOOL WINAPI func_WriteFile( HANDLE,LPCVOID,DWORD,LPDWORD,LPOVERLAPPED );
typedef HMODULE WINAPI func_GetModuleHandle( LPCSTR );
typedef SIZE_T WINAPI func_VirtualQuery(
    LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T );
typedef BOOL WINAPI func_VirtualProtect( LPVOID,SIZE_T,DWORD,PDWORD );
typedef LPVOID WINAPI func_VirtualAlloc( LPVOID,SIZE_T,DWORD,DWORD );
typedef BOOL WINAPI func_VirtualFree( LPVOID,SIZE_T,DWORD );
typedef VOID WINAPI func_Sleep( DWORD );
typedef BOOL WINAPI func_SetEvent( HANDLE );
typedef VOID WINAPI func_ExitProcess( UINT );
typedef USHORT WINAPI func_CaptureStackBackTrace( ULONG,ULONG,PVOID*,PULONG );
typedef DWORD WINAPI func_GetModuleFileNameA( HMODULE,LPSTR,DWORD );
typedef VOID WINAPI func_CriticalSection( LPCRITICAL_SECTION );
typedef LPVOID WINAPI func_HeapAlloc( HANDLE,DWORD,SIZE_T );
typedef LPVOID WINAPI func_HeapReAlloc( HANDLE,DWORD,LPVOID,SIZE_T );
typedef BOOL WINAPI func_HeapFree( HANDLE,DWORD,LPVOID );
typedef HANDLE WINAPI func_GetProcessHeap( VOID );
typedef VOID WINAPI func_ZeroMemory( PVOID,SIZE_T );
typedef VOID WINAPI func_MoveMemory( PVOID,const VOID*,SIZE_T );
typedef VOID WINAPI func_FillMemory( PVOID,SIZE_T,UCHAR );
typedef VOID WINAPI func_GetSystemInfo( LPSYSTEM_INFO );
typedef LPTOP_LEVEL_EXCEPTION_FILTER WINAPI func_SetUnhandledExceptionFilter(
    LPTOP_LEVEL_EXCEPTION_FILTER );
typedef BOOL WINAPI func_IsBadReadPtr( const VOID*,UINT_PTR );

typedef DWORD WINAPI func_SymSetOptions( DWORD );
typedef BOOL WINAPI func_SymInitialize( HANDLE,PCSTR,BOOL );
typedef BOOL WINAPI func_SymGetLineFromAddr64(
    HANDLE,DWORD64,PDWORD,PIMAGEHLP_LINE64 );
typedef BOOL WINAPI func_SymCleanup( HANDLE );
#if USE_STACKWALK
typedef BOOL WINAPI func_StackWalk64(
    DWORD,HANDLE,HANDLE,LPSTACKFRAME64,PVOID,PREAD_PROCESS_MEMORY_ROUTINE64,
    PFUNCTION_TABLE_ACCESS_ROUTINE64,PGET_MODULE_BASE_ROUTINE64,
    PTRANSLATE_ADDRESS_ROUTINE64 );
#endif

struct dbghelp;
typedef int func_dwstOfFile( const char*,uint64_t,uint64_t*,int,
    void(*)(uint64_t,const char*,int,struct dbghelp*),struct dbghelp* );

typedef int WINAPI func_strlen( LPCSTR );
typedef int WINAPI func_strcmp( LPCSTR,LPCSTR );

typedef void *func_malloc( size_t );
typedef void *func_calloc( size_t,size_t );
typedef void func_free( void* );
typedef void *func_realloc( void*,size_t );
typedef char *func_strdup( const char* );
typedef wchar_t *func_wcsdup( const wchar_t* );

typedef struct
{
  const char *funcName;
  void *origFunc;
  void *myFunc;
}
replaceData;

typedef struct
{
  void *ptr;
  size_t size;
  void *frames[PTRS];
}
allocation;

typedef struct
{
  allocation a;
  void *frames[PTRS];
}
freed;

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
}
exceptionInfo;

struct remoteData;
typedef const char *func_replaceFuncs( struct remoteData*,const char*,
    const char*,replaceData*,unsigned int );
typedef void func_trackAllocs( struct remoteData*,void*,void*,size_t );
#ifndef _WIN64
typedef int func_fixDataFuncAddr( unsigned char*,size_t,void* );
#endif
typedef void func_writeMods( struct remoteData*,allocation*,int );
typedef void *func_pm_alloc( struct remoteData*,size_t );
typedef void func_pm_free( struct remoteData*,void* );
typedef size_t func_pm_alloc_size( struct remoteData*,void* );

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
  WRITE_SLACK,
  WRITE_MAIN_ALLOC_FAIL,
};

typedef struct
{
  intptr_t protect;
  intptr_t align;
  intptr_t init;
  intptr_t slackInit;
  intptr_t protectFree;
  intptr_t handleException;
}
options;

typedef struct remoteData
{
  func_WriteFile *fWriteFile;
  func_GetModuleHandle *fGetModuleHandle;
  func_VirtualQuery *fVirtualQuery;
  func_VirtualProtect *fVirtualProtect;
  func_VirtualAlloc *fVirtualAlloc;
  func_VirtualFree *fVirtualFree;
  func_Sleep *fSleep;
  func_SetEvent *fSetEvent;
  func_CaptureStackBackTrace *fCaptureStackBackTrace;
  func_LoadLibraryW *fLoadLibrary;
  func_GetProcAddress *fGetProcAddress;
  func_GetModuleFileNameA *fGetModuleFileName;
  func_CriticalSection *fInitializeCriticalSection;
  func_CriticalSection *fEnterCriticalSection;
  func_CriticalSection *fLeaveCriticalSection;
  func_HeapAlloc *fHeapAlloc;
  func_HeapReAlloc *fHeapReAlloc;
  func_HeapFree *fHeapFree;
  func_GetProcessHeap *fGetProcessHeap;
  func_ZeroMemory *fZeroMemory;
  func_MoveMemory *fMoveMemory;
  func_FillMemory *fFillMemory;
  func_GetSystemInfo *fGetSystemInfo;
  func_SetUnhandledExceptionFilter *fSetUnhandledExceptionFilter;
  func_IsBadReadPtr *fIsBadReadPtr;

  func_strlen *fstrlen;
  func_strcmp *fstrcmp;
  func_strcmp *fstrcmpi;

  func_malloc *fmalloc;
  func_calloc *fcalloc;
  func_free *ffree;
  func_realloc *frealloc;
  func_strdup *fstrdup;
  func_wcsdup *fwcsdup;
  func_ExitProcess *fExitProcess;
  func_malloc *fop_new;
  func_free *fop_delete;
  func_malloc *fop_new_a;
  func_free *fop_delete_a;

  func_malloc *mmalloc;
  func_calloc *mcalloc;
  func_free *mfree;
  func_realloc *mrealloc;
  func_strdup *mstrdup;
  func_wcsdup *mwcsdup;
  func_ExitProcess *mExitProcess;
  func_SetUnhandledExceptionFilter *mSUEF;
  func_malloc *mop_new;
  func_free *mop_delete;
  func_malloc *mop_new_a;
  func_free *mop_delete_a;

  func_malloc *pmalloc;
  func_calloc *pcalloc;
  func_free *pfree;
  func_realloc *prealloc;
  func_strdup *pstrdup;
  func_wcsdup *pwcsdup;

  func_pm_alloc *pm_alloc;
  func_pm_free *pm_free;
  func_pm_alloc_size *pm_alloc_size;

  func_replaceFuncs *mreplaceFuncs;
  func_trackAllocs *mtrackAllocs;
#ifndef _WIN64
  func_fixDataFuncAddr *mfixDataFuncAddr;
#endif
  func_writeMods *mwriteMods;

  HANDLE master;
  HANDLE initFinished;

  allocation *alloc_a;
  int alloc_q;
  int alloc_s;

  freed *freed_a;
  int freed_q;
  int freed_s;

  union {
    wchar_t exePath[MAX_PATH];
    char exePathA[MAX_PATH];
  };

  CRITICAL_SECTION cs;
  HANDLE heap;
  DWORD pageSize;

  options opt;
}
remoteData;


typedef enum
{
  ATT_NORMAL,
  ATT_OK,
  ATT_SECTION,
  ATT_INFO,
  ATT_WARN,
  ATT_BASE,
  ATT_COUNT,
}
textColorAtt;
struct textColor;
typedef void func_TextColor( struct textColor*,textColorAtt );
typedef struct textColor
{
  func_TextColor *fTextColor;
  HANDLE out;
  int colors[ATT_COUNT];
}
textColor;


#undef RtlMoveMemory
VOID WINAPI RtlMoveMemory( PVOID,const VOID*,SIZE_T );

static __attribute__((noinline)) void mprintf(
    textColor *tc,const char *format,... )
{
  va_list vl;
  va_start( vl,format );
  const char *ptr = format;
  HANDLE out = tc->out;
  DWORD written;
  while( ptr[0] )
  {
    if( ptr[0]=='%' && ptr[1] )
    {
      if( ptr>format )
        WriteFile( out,format,ptr-format,&written,NULL );
      switch( ptr[1] )
      {
        case 's':
          {
            const char *arg = va_arg( vl,const char* );
            if( arg && arg[0] )
              WriteFile( out,arg,lstrlen(arg),&written,NULL );
          }
          break;

        case 'd':
        case 'u':
          {
            uintptr_t arg;
            int minus = 0;
            if( ptr[1]=='d' )
            {
              intptr_t argi = va_arg( vl,intptr_t );
              if( argi<0 )
              {
                arg = -argi;
                minus = 1;
              }
              else
                arg = argi;
            }
            else
              arg = va_arg( vl,uintptr_t );
            char str[32];
            char *start = str + ( sizeof(str)-1 );
            start[0] = 0;
            if( !arg )
              (--start)[0] = '0';
            while( arg )
            {
              (--start)[0] = arg%10 + '0';
              arg /= 10;
            }
            if( minus )
              (--start)[0] = '-';
            WriteFile( out,start,lstrlen(start),&written,NULL );
          }
          break;

        case 'p':
        case 'x':
          {
            uintptr_t arg;
            int bytes;
            if( ptr[1]=='p' )
            {
              arg = va_arg( vl,uintptr_t );
              bytes = sizeof(void*);
            }
            else
            {
              arg = va_arg( vl,unsigned int );
              bytes = sizeof(unsigned int);
            }
            char str[20];
            char *end = str;
            int b;
            end++[0] = '0';
            end++[0] = 'x';
            for( b=bytes*2-1; b>=0; b-- )
            {
              uintptr_t bits = ( arg>>(b*4) )&0xf;
              if( bits>=10 )
                end++[0] = bits - 10 + 'A';
              else
                end++[0] = bits + '0';
            }
            end[0] = 0;
            WriteFile( out,str,lstrlen(str),&written,NULL );
          }
          break;

        case 'c':
          {
            textColorAtt arg = va_arg( vl,textColorAtt );
            if( tc->fTextColor )
              tc->fTextColor( tc,arg );
          }
          break;
      }
      ptr += 2;
      format = ptr;
      continue;
    }
    ptr++;
  }
  if( ptr>format )
    WriteFile( out,format,ptr-format,&written,NULL );
  va_end( vl );
}
#define printf(a...) mprintf(tc,a)

static __attribute__((noinline)) char *mstrchr( const char *s,char c )
{
  for( ; *s; s++ )
    if( *s==c ) return( (char*)s );
  return( NULL );
}
#define strchr mstrchr

static __attribute__((noinline)) char *mstrrchr( const char *s,char c )
{
  char *ret = NULL;
  for( ; *s; s++ )
    if( *s==c ) ret = (char*)s;
  return( ret );
}
#define strrchr mstrrchr

static __attribute__((noinline)) int matoi( const char *s )
{
  int ret = 0;
  for( ; *s>='0' && *s<='9'; s++ )
    ret = ret*10 + ( *s - '0' );
  return( ret );
}
#define atoi matoi

static int mmemcmp( const void *p1,const void *p2,size_t s )
{
  const unsigned char *b1 = p1;
  const unsigned char *b2 = p2;
  size_t i;
  for( i=0; i<s; i++ )
    if( b1[i]!=b2[i] ) return( 1 );
  return( 0 );
}
#define memcmp mmemcmp


static void TextColorConsole( textColor *tc,textColorAtt color )
{
  SetConsoleTextAttribute( tc->out,tc->colors[color] );
}
static void TextColorTerminal( textColor *tc,textColorAtt color )
{
  int c = tc->colors[color];
  char text[] = { 27,'[',(c/10000)%10+'0',(c/1000)%10+'0',(c/100)%10+'0',';',
    (c/10)%10+'0',c%10+'0','m' };
  DWORD written;
  WriteFile( tc->out,text,sizeof(text),&written,NULL );
}
static void checkOutputVariant( textColor *tc )
{
  tc->fTextColor = NULL;
  tc->out = GetStdHandle( STD_OUTPUT_HANDLE );

  DWORD flags;
  if( GetConsoleMode(tc->out,&flags) )
  {
    tc->fTextColor = &TextColorConsole;

    tc->colors[ATT_NORMAL]  = FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_RED;
    tc->colors[ATT_OK]      = FOREGROUND_GREEN|FOREGROUND_INTENSITY;
    tc->colors[ATT_SECTION] =
      FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
    tc->colors[ATT_INFO]    =
      FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
    tc->colors[ATT_WARN]    = FOREGROUND_RED|FOREGROUND_INTENSITY;
    tc->colors[ATT_BASE]    = BACKGROUND_INTENSITY;

    TextColorConsole( tc,ATT_NORMAL );
    return;
  }

  HMODULE ntdll = GetModuleHandle( "ntdll.dll" );
  if( !ntdll ) return;

  typedef enum
  {
    ObjectNameInformation=1,
  }
  OBJECT_INFORMATION_CLASS;
  typedef LONG func_NtQueryObject(
      HANDLE,OBJECT_INFORMATION_CLASS,PVOID,ULONG,PULONG );
  func_NtQueryObject *fNtQueryObject =
    (func_NtQueryObject*)GetProcAddress( ntdll,"NtQueryObject" );
  if( fNtQueryObject )
  {
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
    HANDLE heap = GetProcessHeap();
    OBJECT_NAME_INFORMATION *oni =
      HeapAlloc( heap,0,sizeof(OBJECT_NAME_INFORMATION) );
    ULONG len;
    wchar_t namedPipe[] = L"\\Device\\NamedPipe\\";
    size_t l1 = sizeof(namedPipe)/2 - 1;
    wchar_t toMaster[] = L"-to-master";
    size_t l2 = sizeof(toMaster)/2 - 1;
    if( !fNtQueryObject(tc->out,ObjectNameInformation,
          oni,sizeof(OBJECT_NAME_INFORMATION),&len) &&
        oni->Name.Length/2>l1+l2 &&
        !memcmp(oni->Name.Buffer,namedPipe,l1*2) &&
        !memcmp(oni->Name.Buffer+(oni->Name.Length/2-l2),toMaster,l2*2) )
    {
      tc->fTextColor = &TextColorTerminal;

      tc->colors[ATT_NORMAL]  =  4939;
      tc->colors[ATT_OK]      =  4932;
      tc->colors[ATT_SECTION] =  4936;
      tc->colors[ATT_INFO]    =  4935;
      tc->colors[ATT_WARN]    =  4931;
      tc->colors[ATT_BASE]    = 10030;

      TextColorTerminal( tc,ATT_NORMAL );
    }
    HeapFree( heap,0,oni );
  }
}


static HANDLE inject( HANDLE process,options *opt,char *exePath,
    textColor *tc );
#ifndef _WIN64
#define GET_REMOTEDATA( rd ) remoteData *rd = *((remoteData**)-1)
#else
typedef union
{
  HANDLE (*func)( HANDLE,options*,char*,textColor* );
  remoteData **data;
}
unalias;
#define GET_REMOTEDATA( rd ) remoteData *rd = *((unalias)&inject).data
#endif

static void *new_malloc( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fmalloc( s );

  rd->mtrackAllocs( rd,NULL,b,s );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_malloc\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void *new_calloc( size_t n,size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fcalloc( n,s );

  rd->mtrackAllocs( rd,NULL,b,n*s );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_calloc\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void new_free( void *b )
{
  GET_REMOTEDATA( rd );
  rd->ffree( b );

  rd->mtrackAllocs( rd,b,NULL,0 );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_free\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif
}

static void *new_realloc( void *b,size_t s )
{
  GET_REMOTEDATA( rd );
  void *nb = rd->frealloc( b,s );

  rd->mtrackAllocs( rd,b,nb,s );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_realloc\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( nb );
}

static char *new_strdup( const char *s )
{
  GET_REMOTEDATA( rd );
  char *b = rd->fstrdup( s );

  rd->mtrackAllocs( rd,NULL,b,rd->fstrlen(s)+1 );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_strdup\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static wchar_t *new_wcsdup( const wchar_t *s )
{
  GET_REMOTEDATA( rd );
  wchar_t *b = rd->fwcsdup( s );

  size_t l = 0;
  while( s[l] ) l++;
  rd->mtrackAllocs( rd,NULL,b,(l+1)*sizeof(wchar_t) );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wcsdup\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void *new_op_new( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fop_new( s );

  rd->mtrackAllocs( rd,NULL,b,s );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_new\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void new_op_delete( void *b )
{
  GET_REMOTEDATA( rd );
  rd->fop_delete( b );

  rd->mtrackAllocs( rd,b,NULL,0 );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_delete\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif
}

static void *new_op_new_a( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fop_new_a( s );

  rd->mtrackAllocs( rd,NULL,b,s );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_new_a\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void new_op_delete_a( void *b )
{
  GET_REMOTEDATA( rd );
  rd->fop_delete_a( b );

  rd->mtrackAllocs( rd,b,NULL,0 );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_delete_a\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif
}

static VOID WINAPI new_ExitProcess( UINT c )
{
  GET_REMOTEDATA( rd );

  int type;
  DWORD written;
#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_ExitProcess\n";
  type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  rd->fEnterCriticalSection( &rd->cs );

  if( rd->alloc_q )
    rd->mwriteMods( rd,rd->alloc_a,rd->alloc_q );

  type = WRITE_LEAKS;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,&c,sizeof(UINT),&written,NULL );
  rd->fWriteFile( rd->master,&rd->alloc_q,sizeof(int),&written,NULL );
  if( rd->alloc_q )
    rd->fWriteFile( rd->master,rd->alloc_a,rd->alloc_q*sizeof(allocation),
        &written,NULL );

  rd->fLeaveCriticalSection( &rd->cs );

  rd->fExitProcess( c );
}

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI new_SUEF(
    LPTOP_LEVEL_EXCEPTION_FILTER plTopLevelExceptionFilter )
{
  (void)plTopLevelExceptionFilter;

  return( NULL );
}


static void *protect_alloc_m( remoteData *rd,size_t s )
{
  while( s%rd->opt.align ) s++;

  if( !s ) return( NULL );

  DWORD pageSize = rd->pageSize;
  size_t pages = ( s-1 )/pageSize + 2;

  unsigned char *b = (unsigned char*)rd->fVirtualAlloc(
      NULL,pages*pageSize,MEM_RESERVE,PAGE_NOACCESS );
  if( !b )
    return( NULL );

  size_t slackSize = ( pageSize - (s%pageSize) )%pageSize;

  if( rd->opt.protect>1 )
    b += pageSize;

  rd->fVirtualAlloc( b,(pages-1)*pageSize,MEM_COMMIT,PAGE_READWRITE );

  if( slackSize && rd->opt.slackInit )
  {
    unsigned char *slackStart = b;
    if( rd->opt.protect>1 ) slackStart += s;
    rd->fFillMemory( slackStart,slackSize,rd->opt.slackInit );
  }

  if( rd->opt.protect==1 )
    b += slackSize;

  return( b );
}

static void protect_free_m( remoteData *rd,void *b )
{
  if( !b ) return;

  size_t s = rd->pm_alloc_size( rd,b );
  if( !s ) return;

  DWORD pageSize = rd->pageSize;
  size_t pages = ( s-1 )/pageSize + 2;

  uintptr_t p = (uintptr_t)b;
  unsigned char *slackStart;
  size_t slackSize;
  if( rd->opt.protect==1 )
  {
    slackSize = p%pageSize;
    p -= slackSize;
    slackStart = (unsigned char*)p;
  }
  else
  {
    slackStart = ((unsigned char*)p) + s;
    slackSize = ( pageSize - (s%pageSize) )%pageSize;
    p -= pageSize;
  }

  if( slackSize )
  {
    size_t i;
    for( i=0; i<slackSize && slackStart[i]==rd->opt.slackInit; i++ );
    if( i<slackSize )
    {
      rd->fEnterCriticalSection( &rd->cs );

      int j;
      for( j=rd->alloc_q-1; j>=0 && rd->alloc_a[j].ptr!=b; j-- );
      if( j>=0 )
      {
        allocation aa[2];
        rd->fMoveMemory( aa,rd->alloc_a+j,sizeof(allocation) );
        void **frames = aa[1].frames;
        int ptrs = rd->fCaptureStackBackTrace( 3,PTRS,frames,NULL );
        if( ptrs<PTRS )
          rd->fZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );
        aa[1].ptr = slackStart + i;

        rd->mwriteMods( rd,aa,2 );

        int type = WRITE_SLACK;
        DWORD written;
        rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
        rd->fWriteFile( rd->master,aa,2*sizeof(allocation),&written,NULL );
      }

      rd->fLeaveCriticalSection( &rd->cs );
    }
  }

  b = (void*)p;

  rd->fVirtualFree( b,pages*pageSize,MEM_DECOMMIT );

  if( !rd->opt.protectFree )
    rd->fVirtualFree( b,0,MEM_RELEASE );
}

static size_t alloc_size( remoteData *rd,void *p )
{
  rd->fEnterCriticalSection( &rd->cs );

  int i;
  for( i=rd->alloc_q-1; i>=0 && rd->alloc_a[i].ptr!=p; i-- );
  size_t s = i>=0 ? rd->alloc_a[i].size : 0;

  rd->fLeaveCriticalSection( &rd->cs );

  return( s );
}

static void *protect_malloc( size_t s )
{
  GET_REMOTEDATA( rd );

  void *b = rd->pm_alloc( rd,s );
  if( !b ) return( NULL );

  if( rd->opt.init )
    rd->fFillMemory( b,s,rd->opt.init );

  return( b );
}

static void *protect_calloc( size_t n,size_t s )
{
  GET_REMOTEDATA( rd );

  return( rd->pm_alloc(rd,n*s) );
}

static void protect_free( void *b )
{
  GET_REMOTEDATA( rd );

  rd->pm_free( rd,b );
}

static void *protect_realloc( void *b,size_t s )
{
  GET_REMOTEDATA( rd );

  if( !s )
  {
    rd->pm_free( rd,b );
    return( NULL );
  }

  if( !b )
    return( rd->pm_alloc(rd,s) );

  size_t os = rd->pm_alloc_size( rd,b );
  if( !os ) return( NULL );

  void *nb = rd->pm_alloc( rd,s );
  if( !nb ) return( NULL );

  rd->fMoveMemory( nb,b,os<s?os:s );

  if( s>os && rd->opt.init )
    rd->fFillMemory( ((char*)nb)+os,s-os,rd->opt.init );

  rd->pm_free( rd,b );

  return( nb );
}

static char *protect_strdup( const char *s )
{
  GET_REMOTEDATA( rd );

  size_t l = rd->fstrlen( s ) + 1;

  char *b = rd->pm_alloc( rd,l );
  if( !b ) return( NULL );

  rd->fMoveMemory( b,s,l );

  return( b );
}

static wchar_t *protect_wcsdup( const wchar_t *s )
{
  GET_REMOTEDATA( rd );

  size_t l = 0;
  while( s[l] ) l++;
  l = ( l+1 )*2;

  wchar_t *b = rd->pm_alloc( rd,l );
  if( !b ) return( NULL );

  rd->fMoveMemory( b,s,l );

  return( b );
}

#ifdef _WIN64
#define csp Rsp
#define cip Rip
#define cfp Rbp
#if USE_STACKWALK
#define MACH_TYPE IMAGE_FILE_MACHINE_AMD64
#endif
#else
#define csp Esp
#define cip Eip
#define cfp Ebp
#if USE_STACKWALK
#define MACH_TYPE IMAGE_FILE_MACHINE_I386
#endif
#endif
static LONG WINAPI exceptionWalker( LPEXCEPTION_POINTERS ep )
{
  GET_REMOTEDATA( rd );

  int type;
  DWORD written;

  exceptionInfo ei;
  ei.aq = 1;

  if( ep->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
      ep->ExceptionRecord->NumberParameters==2 )
  {
    char *addr = (char*)ep->ExceptionRecord->ExceptionInformation[1];

    int i;
    for( i=rd->alloc_q-1; i>=0; i-- )
    {
      allocation *a = rd->alloc_a + i;

      char *ptr = a->ptr;
      char *noAccessStart;
      char *noAccessEnd;
      if( rd->opt.protect==1 )
      {
        noAccessStart = ptr + a->size;
        noAccessEnd = noAccessStart + rd->pageSize;
      }
      else
      {
        noAccessStart = ptr - rd->pageSize;
        noAccessEnd = ptr;
      }

      if( addr>=noAccessStart && addr<noAccessEnd )
      {
        rd->fMoveMemory( &ei.aa[1],a,sizeof(allocation) );
        ei.aq++;

        break;
      }
    }
    if( i<0 )
    {
      for( i=rd->freed_q-1; i>=0; i-- )
      {
        freed *f = rd->freed_a + i;

        char *ptr = f->a.ptr;
        size_t size = f->a.size;
        char *noAccessStart;
        char *noAccessEnd;
        if( rd->opt.protect==1 )
        {
          noAccessStart = ptr - ( ((uintptr_t)ptr)%rd->pageSize );
          noAccessEnd = ptr + f->a.size + rd->pageSize;
        }
        else
        {
          noAccessStart = ptr - rd->pageSize;
          noAccessEnd = ptr + ( (size-1)/rd->pageSize+1 )*rd->pageSize;
        }

        if( addr>=noAccessStart && addr<noAccessEnd )
        {
          rd->fMoveMemory( &ei.aa[1],&f->a,sizeof(allocation) );
          rd->fMoveMemory( &ei.aa[2].frames,&f->frames,PTRS*sizeof(void*) );
          ei.aq += 2;

          break;
        }
      }
    }
  }

  type = WRITE_EXCEPTION;

  int count = 0;
  void **frames = ei.aa[0].frames;

#if USE_STACKWALK
  wchar_t dll_dbghelp[] = { 'd','b','g','h','e','l','p','.','d','l','l',0 };
  HMODULE symMod = rd->fLoadLibrary( dll_dbghelp );
  func_StackWalk64 *fStackWalk64 = NULL;
  if( symMod )
  {
    char stackwalk64[] = "StackWalk64";
    fStackWalk64 =
      (func_StackWalk64*)rd->fGetProcAddress( symMod,stackwalk64 );
  }

  if( fStackWalk64 )
  {
    STACKFRAME64 stack;
    CONTEXT *context = ep->ContextRecord;

    rd->fZeroMemory( &stack,sizeof(STACKFRAME64) );
    stack.AddrPC.Offset = context->cip;
    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrStack.Offset = context->csp;
    stack.AddrStack.Mode = AddrModeFlat;
    stack.AddrFrame.Offset = context->cfp;
    stack.AddrFrame.Mode = AddrModeFlat;

    char dll_kernel32[] = "kernel32.dll";
    HMODULE kernel32 = rd->fGetModuleHandle( dll_kernel32 );
    char getcurrentprocess[] = "GetCurrentProcess";
    func_GetProcessHeap *fGetCurrentProcess =
      rd->fGetProcAddress( kernel32,getcurrentprocess );
    HANDLE process = fGetCurrentProcess();
    char getcurrentthread[] = "GetCurrentThread";
    func_GetProcessHeap *fGetCurrentThread =
      rd->fGetProcAddress( kernel32,getcurrentthread );
    HANDLE thread = fGetCurrentThread();

    char symfunctiontableaccess[] = "SymFunctionTableAccess64";
    PFUNCTION_TABLE_ACCESS_ROUTINE64 fSymFunctionTableAccess64 =
      rd->fGetProcAddress( symMod,symfunctiontableaccess );
    char symgetmodulebase[] = "SymGetModuleBase64";
    PGET_MODULE_BASE_ROUTINE64 fSymGetModuleBase64 =
      rd->fGetProcAddress( symMod,symgetmodulebase );

    while( count<PTRS )
    {
      if( !fStackWalk64(MACH_TYPE,process,thread,&stack,context,
            NULL,fSymFunctionTableAccess64,fSymGetModuleBase64,NULL) )
        break;

      uintptr_t frame = stack.AddrPC.Offset;
      if( !frame ) break;

      if( !count ) frame++;
      frames[count++] = (void*)frame;
    }
  }
  else
#endif
  {
    frames[count++] = (void*)( ep->ContextRecord->cip+1 );
#if 0
    ULONG_PTR csp = *(ULONG_PTR*)ep->ContextRecord->csp;
    if( csp ) ei.frames[count++] = (void*)csp;
#endif
    ULONG_PTR *sp = (ULONG_PTR*)ep->ContextRecord->cfp;
    while( count<PTRS )
    {
      if( rd->fIsBadReadPtr(sp,2*sizeof(ULONG_PTR)) || !sp[0] || !sp[1] )
        break;

      ULONG_PTR *np = (ULONG_PTR*)sp[0];
      frames[count++] = (void*)sp[1];

      sp = np;
    }
  }
  if( count<PTRS )
    rd->fZeroMemory( frames+count,(PTRS-count)*sizeof(void*) );

  rd->mwriteMods( rd,ei.aa,ei.aq );

  rd->fMoveMemory( &ei.er,ep->ExceptionRecord,sizeof(EXCEPTION_RECORD) );
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,&ei,sizeof(exceptionInfo),&written,NULL );

  rd->fExitProcess( 1 );

  return( EXCEPTION_EXECUTE_HANDLER );
}


#define REL_PTR( base,ofs ) ( ((PBYTE)base)+ofs )

static const char *replaceFuncs( remoteData *rd,const char *caller,
    const char *called,replaceData *rep,unsigned int count )
{
  HMODULE app = rd->fGetModuleHandle( caller );
  if( !app ) return( NULL );

  PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)app;
  PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );
  if( IMAGE_NT_SIGNATURE!=inh->Signature )
    return( NULL );

  PIMAGE_IMPORT_DESCRIPTOR iid =
    (PIMAGE_IMPORT_DESCRIPTOR)REL_PTR( idh,inh->OptionalHeader.
        DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress );

  PSTR repModName = NULL;
  UINT i;
  for( i=0; iid[i].Characteristics; i++ )
  {
    PSTR curModName = (PSTR)REL_PTR( idh,iid[i].Name );
    if( called && rd->fstrcmpi(curModName,called) )
      continue;
    if( !iid[i].FirstThunk || !iid[i].OriginalFirstThunk )
      break;

    PIMAGE_THUNK_DATA thunk =
      (PIMAGE_THUNK_DATA)REL_PTR( idh,iid[i].FirstThunk );
    PIMAGE_THUNK_DATA originalThunk =
      (PIMAGE_THUNK_DATA)REL_PTR( idh,iid[i].OriginalFirstThunk );

    repModName = called ? curModName : NULL;
    for( ; originalThunk->u1.Function; originalThunk++,thunk++ )
    {
      if( originalThunk->u1.Ordinal&IMAGE_ORDINAL_FLAG )
        continue;

      PIMAGE_IMPORT_BY_NAME import =
        (PIMAGE_IMPORT_BY_NAME)REL_PTR( idh,originalThunk->u1.AddressOfData );

      void **origFunc = NULL;
      void *myFunc = NULL;
      unsigned int j;
      for( j=0; j<count; j++ )
      {
        if( rd->fstrcmp((LPCSTR)import->Name,rep[j].funcName) ) continue;
        origFunc = rep[j].origFunc;
        myFunc = rep[j].myFunc;
        break;
      }
      if( !origFunc ) continue;

      repModName = curModName;

      MEMORY_BASIC_INFORMATION mbi;
      rd->fVirtualQuery( thunk,&mbi,sizeof(MEMORY_BASIC_INFORMATION) );
      if( !rd->fVirtualProtect(mbi.BaseAddress,mbi.RegionSize,
            PAGE_EXECUTE_READWRITE,&mbi.Protect) )
        break;

      if( !*origFunc )
        *origFunc = (void*)thunk->u1.Function;
      thunk->u1.Function = (DWORD_PTR)myFunc;

      if( !rd->fVirtualProtect(mbi.BaseAddress,mbi.RegionSize,
            mbi.Protect,&mbi.Protect) )
        break;
    }

    if( !called && repModName ) called = repModName;
  }

  return( repModName );
}

#ifndef _WIN64
int fixDataFuncAddr( unsigned char *pos,size_t size,void *data )
{
  unsigned char *end = pos + ( size-3 );
  while( 1 )
  {
    while( pos<end && pos[0]!=0xff ) pos++;
    if( pos>=end ) break;

    if( pos[1]!=0xff || pos[2]!=0xff || pos[3]!=0xff )
    {
      pos++;
      continue;
    }

    ((void**)pos)[0] = data;
    return( 1 );
  }

  return( 0 );
}
#endif

void trackAllocs( struct remoteData *rd,
    void *free_ptr,void *alloc_ptr,size_t alloc_size )
{
  if( free_ptr )
  {
    rd->fEnterCriticalSection( &rd->cs );

    int i;
    for( i=rd->alloc_q-1; i>=0 && rd->alloc_a[i].ptr!=free_ptr; i-- );
    if( i>=0 )
    {
      if( rd->opt.protectFree )
      {
        if( rd->freed_q>=rd->freed_s )
        {
          rd->freed_s += 65536;
          freed *freed_an;
          if( !rd->freed_a )
            freed_an = rd->fHeapAlloc(
                rd->heap,0,rd->freed_s*sizeof(freed) );
          else
            freed_an = rd->fHeapReAlloc(
                rd->heap,0,rd->freed_a,rd->freed_s*sizeof(freed) );
          if( !freed_an )
          {
            DWORD written;
            int type = WRITE_MAIN_ALLOC_FAIL;
            rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
            rd->fExitProcess( 1 );
            return;
          }
          rd->freed_a = freed_an;
        }

        freed *f = rd->freed_a + rd->freed_q;
        rd->freed_q++;
        rd->fMoveMemory( &f->a,&rd->alloc_a[i],sizeof(allocation) );

        void **frames = f->frames;
        int ptrs = rd->fCaptureStackBackTrace( 2,PTRS,frames,NULL );
        if( ptrs<PTRS )
          rd->fZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );
      }

      rd->alloc_q--;
      if( i<rd->alloc_q ) rd->alloc_a[i] = rd->alloc_a[rd->alloc_q];
    }
    else
    {
      allocation a;
      a.ptr = free_ptr;
      a.size = 0;

      void **frames = a.frames;
      int ptrs = rd->fCaptureStackBackTrace( 2,PTRS,frames,NULL );
      if( ptrs<PTRS )
        rd->fZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );

      rd->mwriteMods( rd,&a,1 );

      DWORD written;
      int type = WRITE_FREE_FAIL;
      rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
      rd->fWriteFile( rd->master,&a,sizeof(allocation),&written,NULL );
    }

    rd->fLeaveCriticalSection( &rd->cs );
  }

  if( alloc_ptr )
  {
    rd->fEnterCriticalSection( &rd->cs );

    while( alloc_size%rd->opt.align ) alloc_size++;

    if( rd->alloc_q>=rd->alloc_s )
    {
      rd->alloc_s += 65536;
      allocation *alloc_an;
      if( !rd->alloc_a )
        alloc_an = rd->fHeapAlloc(
            rd->heap,0,rd->alloc_s*sizeof(allocation) );
      else
        alloc_an = rd->fHeapReAlloc(
            rd->heap,0,rd->alloc_a,rd->alloc_s*sizeof(allocation) );
      if( !alloc_an )
      {
        DWORD written;
        int type = WRITE_MAIN_ALLOC_FAIL;
        rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
        rd->fExitProcess( 1 );
        return;
      }
      rd->alloc_a = alloc_an;
    }
    allocation *a = rd->alloc_a + rd->alloc_q;
    rd->alloc_q++;
    a->ptr = alloc_ptr;
    a->size = alloc_size;

    void **frames = a->frames;
    int ptrs = rd->fCaptureStackBackTrace( 2,PTRS,frames,NULL );
    if( ptrs<PTRS )
      rd->fZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );

    rd->fLeaveCriticalSection( &rd->cs );
  }
  else if( alloc_size )
  {
    allocation a;
    a.ptr = NULL;
    a.size = alloc_size;

    void **frames = a.frames;
    int ptrs = rd->fCaptureStackBackTrace( 2,PTRS,frames,NULL );
    if( ptrs<PTRS )
      rd->fZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );

    rd->fEnterCriticalSection( &rd->cs );

    rd->mwriteMods( rd,&a,1 );

    DWORD written;
    int type = WRITE_ALLOC_FAIL;
    rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
    rd->fWriteFile( rd->master,&a,sizeof(allocation),&written,NULL );

    rd->fLeaveCriticalSection( &rd->cs );
  }
}

static void writeMods( struct remoteData *rd,allocation *alloc_a,int alloc_q )
{
  int mi_q = 0;
  modInfo *mi_a = NULL;
  int i,j,k;
  for( i=0; i<alloc_q; i++ )
  {
    allocation *a = alloc_a + i;
    for( j=0; j<PTRS; j++ )
    {
      size_t ptr = (size_t)a->frames[j];
      if( !ptr ) break;

      for( k=0; k<mi_q; k++ )
      {
        if( ptr>=mi_a[k].base && ptr<mi_a[k].base+mi_a[k].size )
          break;
      }
      if( k<mi_q ) continue;

      MEMORY_BASIC_INFORMATION mbi;
      rd->fVirtualQuery( (void*)ptr,&mbi,sizeof(MEMORY_BASIC_INFORMATION) );
      size_t base = (size_t)mbi.AllocationBase;
      size_t size = mbi.RegionSize;
      if( base+size<ptr ) size = ptr - base;

      for( k=0; k<mi_q && mi_a[k].base!=base; k++ );
      if( k<mi_q )
      {
        mi_a[k].size = size;
        continue;
      }

      mi_q++;
      if( !mi_a )
        mi_a = rd->fHeapAlloc( rd->heap,0,mi_q*sizeof(modInfo) );
      else
        mi_a = rd->fHeapReAlloc(
            rd->heap,0,mi_a,mi_q*sizeof(modInfo) );
      if( !mi_a )
      {
        DWORD written;
        int type = WRITE_MAIN_ALLOC_FAIL;
        rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
        rd->fExitProcess( 1 );
      }
      mi_a[mi_q-1].base = base;
      mi_a[mi_q-1].size = size;
      if( !rd->fGetModuleFileName((void*)base,mi_a[mi_q-1].path,MAX_PATH) )
        mi_q--;
    }
  }
  int type = WRITE_MODS;
  DWORD written;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,&mi_q,sizeof(int),&written,NULL );
  if( mi_q )
    rd->fWriteFile( rd->master,mi_a,mi_q*sizeof(modInfo),&written,NULL );
  rd->fHeapFree( rd->heap,0,mi_a );
}


static DWORD WINAPI remoteCall( remoteData *rd )
{
  HMODULE app = rd->fLoadLibrary( rd->exePath );
  char inj_name[] = "inj";
  DWORD (*func_inj)( remoteData*,unsigned char* );
  func_inj = rd->fGetProcAddress( app,inj_name );
  func_inj( rd,(unsigned char*)func_inj );

  rd->fSetEvent( rd->initFinished );
  while( 1 ) rd->fSleep( INFINITE );

  return( 0 );
}


static HANDLE inject( HANDLE process,options *opt,char *exePath,textColor *tc )
{
  size_t funcSize = (size_t)&inject - (size_t)&remoteCall;
  size_t fullSize = funcSize + sizeof(remoteData);

  unsigned char *fullDataRemote =
    VirtualAllocEx( process,NULL,fullSize,MEM_COMMIT,PAGE_EXECUTE_READWRITE );

  HANDLE heap = GetProcessHeap();
  unsigned char *fullData = HeapAlloc( heap,0,fullSize );
  RtlMoveMemory( fullData,&remoteCall,funcSize );
  remoteData *data = (remoteData*)( fullData+funcSize );
  ZeroMemory( data,sizeof(remoteData) );

  LPTHREAD_START_ROUTINE remoteFuncStart =
    (LPTHREAD_START_ROUTINE)( fullDataRemote );

  HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );
  data->fWriteFile =
    (func_WriteFile*)GetProcAddress( kernel32,"WriteFile" );
  data->fGetModuleHandle =
    (func_GetModuleHandle*)GetProcAddress( kernel32,"GetModuleHandleA" );
  data->fVirtualQuery =
    (func_VirtualQuery*)GetProcAddress( kernel32,"VirtualQuery" );
  data->fVirtualProtect =
    (func_VirtualProtect*)GetProcAddress( kernel32,"VirtualProtect" );
  data->fVirtualAlloc =
    (func_VirtualAlloc*)GetProcAddress( kernel32,"VirtualAlloc" );
  data->fVirtualFree =
    (func_VirtualFree*)GetProcAddress( kernel32,"VirtualFree" );
  data->fSleep =
    (func_Sleep*)GetProcAddress( kernel32,"Sleep" );
  data->fSetEvent =
    (func_SetEvent*)GetProcAddress( kernel32,"SetEvent" );
  data->fCaptureStackBackTrace =
    (func_CaptureStackBackTrace*)GetProcAddress(
        kernel32,"RtlCaptureStackBackTrace" );
  data->fLoadLibrary =
    (func_LoadLibraryW*)GetProcAddress( kernel32,"LoadLibraryW" );
  data->fGetProcAddress =
    (func_GetProcAddress*)GetProcAddress( kernel32,"GetProcAddress" );
  data->fGetModuleFileName =
    (func_GetModuleFileNameA*)GetProcAddress( kernel32,"GetModuleFileNameA" );
  data->fInitializeCriticalSection =
    (func_CriticalSection*)GetProcAddress(
        kernel32,"InitializeCriticalSection" );
  data->fEnterCriticalSection =
    (func_CriticalSection*)GetProcAddress(
        kernel32,"EnterCriticalSection" );
  data->fLeaveCriticalSection =
    (func_CriticalSection*)GetProcAddress(
        kernel32,"LeaveCriticalSection" );
  data->fHeapAlloc =
    (func_HeapAlloc*)GetProcAddress( kernel32,"HeapAlloc" );
  data->fHeapReAlloc =
    (func_HeapReAlloc*)GetProcAddress( kernel32,"HeapReAlloc" );
  data->fHeapFree =
    (func_HeapFree*)GetProcAddress( kernel32,"HeapFree" );
  data->fGetProcessHeap =
    (func_GetProcessHeap*)GetProcAddress( kernel32,"GetProcessHeap" );
  data->fZeroMemory =
    (func_ZeroMemory*)GetProcAddress( kernel32,"RtlZeroMemory" );
  data->fMoveMemory =
    (func_MoveMemory*)GetProcAddress( kernel32,"RtlMoveMemory" );
  data->fFillMemory =
    (func_FillMemory*)GetProcAddress( kernel32,"RtlFillMemory" );
  data->fGetSystemInfo =
    (func_GetSystemInfo*)GetProcAddress( kernel32,"GetSystemInfo" );
  data->fSetUnhandledExceptionFilter =
    (func_SetUnhandledExceptionFilter*)GetProcAddress(
        kernel32,"SetUnhandledExceptionFilter" );
  data->fIsBadReadPtr =
    (func_IsBadReadPtr*)GetProcAddress( kernel32,"IsBadReadPtr" );
  data->fExitProcess =
    (func_ExitProcess*)GetProcAddress( kernel32,"ExitProcess" );

  data->fstrlen =
    (func_strlen*)GetProcAddress( kernel32,"lstrlen" );
  data->fstrcmp =
    (func_strcmp*)GetProcAddress( kernel32,"lstrcmp" );
  data->fstrcmpi =
    (func_strcmp*)GetProcAddress( kernel32,"lstrcmpi" );

  GetModuleFileNameW( NULL,data->exePath,MAX_PATH );

  HANDLE readPipe,writePipe;
  CreatePipe( &readPipe,&writePipe,NULL,0 );
  DuplicateHandle( GetCurrentProcess(),writePipe,
      process,&data->master,0,FALSE,
      DUPLICATE_CLOSE_SOURCE|DUPLICATE_SAME_ACCESS );

  HANDLE initFinished = CreateEvent( NULL,FALSE,FALSE,NULL );
  DuplicateHandle( GetCurrentProcess(),initFinished,
      process,&data->initFinished,0,FALSE,
      DUPLICATE_SAME_ACCESS );

  RtlMoveMemory( &data->opt,opt,sizeof(options) );

  WriteProcessMemory( process,fullDataRemote,fullData,fullSize,NULL );

  HANDLE thread = CreateRemoteThread( process,NULL,0,
      remoteFuncStart,
      fullDataRemote+funcSize,0,NULL );
  HANDLE h[2] = { thread,initFinished };
  if( WaitForMultipleObjects(2,h,FALSE,INFINITE)==WAIT_OBJECT_0 )
  {
    CloseHandle( initFinished );
    CloseHandle( thread );
    CloseHandle( readPipe );
    HeapFree( heap,0,fullData );
    printf( "%cprocess failed to initialize\n",ATT_WARN );
    return( NULL );
  }
  CloseHandle( initFinished );
  CloseHandle( thread );

  ReadProcessMemory( process,fullDataRemote+funcSize,data,
      sizeof(remoteData),NULL );
  if( !data->master )
  {
    CloseHandle( readPipe );
    readPipe = NULL;
    printf( "%conly works with dynamically linked CRT\n",ATT_WARN );
  }
  else
    lstrcpy( exePath,data->exePathA );
  HeapFree( heap,0,fullData );

  return( readPipe );
}

__declspec(dllexport) DWORD inj( remoteData *rd,unsigned char *func_addr )
{
  rd->fInitializeCriticalSection( &rd->cs );
  rd->heap = rd->fGetProcessHeap();

  SYSTEM_INFO si;
  rd->fGetSystemInfo( &si );
  rd->pageSize = si.dwPageSize;

  unsigned int i;
  remoteData **dataPtr;
  struct {
    void *addr;
    void *ptr;
  } funcs[] = {
    { &replaceFuncs   ,&rd->mreplaceFuncs    },
    { &trackAllocs    ,&rd->mtrackAllocs     },
#ifndef _WIN64
    { &fixDataFuncAddr,&rd->mfixDataFuncAddr },
#endif
    { &writeMods      ,&rd->mwriteMods       },
    { &inject         ,&dataPtr              },
    { &protect_alloc_m,&rd->pm_alloc         },
    { &protect_free_m ,&rd->pm_free          },
    { &alloc_size     ,&rd->pm_alloc_size    },
    { &new_SUEF       ,&rd->mSUEF            },
  };
  for( i=0; i<sizeof(funcs)/sizeof(funcs[0]); i++ )
  {
#ifndef _WIN64
    size_t ofs = (size_t)funcs[i].addr - (size_t)&inj;
    void *fp = func_addr + ofs;
    *(void**)funcs[i].ptr = fp;
#else
    *(void**)funcs[i].ptr = funcs[i].addr;
    (void)func_addr;
#endif
  }

  {
    MEMORY_BASIC_INFORMATION mbi;
    rd->fVirtualQuery( dataPtr,&mbi,sizeof(MEMORY_BASIC_INFORMATION) );
    rd->fVirtualProtect( mbi.BaseAddress,mbi.RegionSize,
        PAGE_EXECUTE_READWRITE,&mbi.Protect );
    *dataPtr = rd;
    rd->fVirtualProtect( mbi.BaseAddress,mbi.RegionSize,
        mbi.Protect,&mbi.Protect );
  }

  LONG WINAPI (*exceptionWalkerV)( LPEXCEPTION_POINTERS );
  struct {
    void *addr;
    void *ptr;
  } fix_funcs[] = {
    { &new_malloc     ,&rd->mmalloc      },
    { &new_calloc     ,&rd->mcalloc      },
    { &new_free       ,&rd->mfree        },
    { &new_realloc    ,&rd->mrealloc     },
    { &new_strdup     ,&rd->mstrdup      },
    { &new_wcsdup     ,&rd->mwcsdup      },
    { &new_op_new     ,&rd->mop_new      },
    { &new_op_delete  ,&rd->mop_delete   },
    { &new_op_new_a   ,&rd->mop_new_a    },
    { &new_op_delete_a,&rd->mop_delete_a },
    { &new_ExitProcess,&rd->mExitProcess },
    { &protect_malloc ,&rd->pmalloc      },
    { &protect_calloc ,&rd->pcalloc      },
    { &protect_free   ,&rd->pfree        },
    { &protect_realloc,&rd->prealloc     },
    { &protect_strdup ,&rd->pstrdup      },
    { &protect_wcsdup ,&rd->pwcsdup      },
    { &exceptionWalker,&exceptionWalkerV },
  };

  for( i=0; i<sizeof(fix_funcs)/sizeof(fix_funcs[0]); i++ )
  {
#ifndef _WIN64
    size_t ofs = (size_t)fix_funcs[i].addr - (size_t)&inj;
    void *fp = func_addr + ofs;
    *(void**)fix_funcs[i].ptr = fp;

    MEMORY_BASIC_INFORMATION mbi;
    rd->fVirtualQuery( fp,&mbi,sizeof(MEMORY_BASIC_INFORMATION) );
    rd->fVirtualProtect( mbi.BaseAddress,mbi.RegionSize,
        PAGE_EXECUTE_READWRITE,&mbi.Protect );
    rd->mfixDataFuncAddr( fp,
        (char*)(mbi.BaseAddress+mbi.RegionSize)-(char*)fp,dataPtr );
    rd->fVirtualProtect( mbi.BaseAddress,mbi.RegionSize,
        mbi.Protect,&mbi.Protect );
#else
    *(void**)fix_funcs[i].ptr = fix_funcs[i].addr;
#endif
  }

  char fname_malloc[] = "malloc";
  char fname_calloc[] = "calloc";
  char fname_free[] = "free";
  char fname_realloc[] = "realloc";
  char fname_strdup[] = "_strdup";
  char fname_wcsdup[] = "_wcsdup";
#ifndef _WIN64
  char fname_op_new[] = "??2@YAPAXI@Z";
  char fname_op_delete[] = "??3@YAXPAX@Z";
  char fname_op_new_a[] = "??_U@YAPAXI@Z";
  char fname_op_delete_a[] = "??_V@YAXPAX@Z";
#else
  char fname_op_new[] = "??2@YAPEAX_K@Z";
  char fname_op_delete[] = "??3@YAXPEAX@Z";
  char fname_op_new_a[] = "??_U@YAPEAX_K@Z";
  char fname_op_delete_a[] = "??_V@YAXPEAX@Z";
#endif
  replaceData rep[] = {
    { fname_malloc     ,&rd->fmalloc     ,rd->mmalloc      },
    { fname_calloc     ,&rd->fcalloc     ,rd->mcalloc      },
    { fname_free       ,&rd->ffree       ,rd->mfree        },
    { fname_realloc    ,&rd->frealloc    ,rd->mrealloc     },
    { fname_strdup     ,&rd->fstrdup     ,rd->mstrdup      },
    { fname_wcsdup     ,&rd->fwcsdup     ,rd->mwcsdup      },
    { fname_op_new     ,&rd->fop_new     ,rd->mop_new      },
    { fname_op_delete  ,&rd->fop_delete  ,rd->mop_delete   },
    { fname_op_new_a   ,&rd->fop_new_a   ,rd->mop_new_a    },
    { fname_op_delete_a,&rd->fop_delete_a,rd->mop_delete_a },
  };
  const char *dll_msvcrt =
    rd->mreplaceFuncs( rd,NULL,NULL,rep,sizeof(rep)/sizeof(replaceData) );
  if( !dll_msvcrt )
  {
    rd->master = NULL;
    return( 0 );
  }

  if( rd->opt.protect )
  {
    rd->fmalloc = rd->pmalloc;
    rd->fcalloc = rd->pcalloc;
    rd->ffree = rd->pfree;
    rd->frealloc = rd->prealloc;
    rd->fstrdup = rd->pstrdup;
    rd->fwcsdup = rd->pwcsdup;
    rd->fop_new = rd->pmalloc;
    rd->fop_delete = rd->pfree;
    rd->fop_new_a = rd->pmalloc;
    rd->fop_delete_a = rd->pfree;
  }

  if( rd->opt.handleException )
    rd->fSetUnhandledExceptionFilter( exceptionWalkerV );

  char dll_kernel32[] = "kernel32.dll";
  char fname_ExitProcess[] = "ExitProcess";
  char fname_SUEF[] = "SetUnhandledExceptionFilter";
  replaceData rep2[] = {
    { fname_ExitProcess,&rd->fExitProcess                ,rd->mExitProcess },
    { fname_SUEF       ,&rd->fSetUnhandledExceptionFilter,rd->mSUEF        },
  };
  unsigned int rep2count = sizeof(rep2)/sizeof(replaceData);
  if( !rd->opt.handleException )
    rep2count--;
  rd->mreplaceFuncs( rd,NULL,dll_kernel32,rep2,rep2count );
  for( i=0; i<rep2count; i++ )
    rd->mreplaceFuncs( rd,dll_msvcrt,NULL,rep2+i,1 );

  rd->fGetModuleFileName( NULL,rd->exePathA,MAX_PATH );

  return( 0 );
}


static int isWrongArch( HANDLE process )
{
  BOOL remoteWow64,meWow64;
  IsWow64Process( process,&remoteWow64 );
  IsWow64Process( GetCurrentProcess(),&meWow64 );
  return( remoteWow64!=meWow64 );
}


typedef struct dbghelp
{
  HANDLE process;
  func_SymGetLineFromAddr64 *fSymGetLineFromAddr64;
  func_dwstOfFile *fdwstOfFile;
  textColor *tc;
}
dbghelp;

static void locFunc(
    uint64_t addr,const char *filename,int lineno,dbghelp *dh )
{
  textColor *tc = dh->tc;

  IMAGEHLP_LINE64 il;
  if( lineno==DWST_NO_DBG_SYM && dh->fSymGetLineFromAddr64 )
  {
    memset( &il,0,sizeof(IMAGEHLP_LINE64) );
    il.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD dis;
    if( dh->fSymGetLineFromAddr64(dh->process,addr,&dis,&il) )
    {
      filename = il.FileName;
      lineno = il.LineNumber;
    }
  }

  const char *sep1 = strrchr( filename,'/' );
  const char *sep2 = strrchr( filename,'\\' );
  if( sep2>sep1 ) sep1 = sep2;
  if( sep1 ) filename = sep1 + 1;

  switch( lineno )
  {
    case DWST_BASE_ADDR:
      printf( "    %c%p%c %c%s%c\n",
          ATT_BASE,(uintptr_t)addr,ATT_NORMAL,
          ATT_BASE,filename,ATT_NORMAL );
      break;

    case DWST_NO_DBG_SYM:
    case DWST_NO_SRC_FILE:
    case DWST_NOT_FOUND:
      printf( "%c    %p\n",ATT_NORMAL,(uintptr_t)addr );
      break;

    default:
      printf( "%c    %p %c%s%c:%d\n",
          ATT_NORMAL,(uintptr_t)addr,
          ATT_OK,filename,ATT_NORMAL,(intptr_t)lineno );
      break;
  }
}

static void printStack( void **framesV,modInfo *mi_a,int mi_q,dbghelp *dh )
{
  uint64_t frames[PTRS];
  int j;
  for( j=0; j<PTRS; j++ )
  {
    if( !framesV[j] ) break;
    frames[j] = ((uintptr_t)framesV[j]) - 1;
  }
  int fc = j;
  for( j=0; j<fc; j++ )
  {
    int k;
    uint64_t frame = frames[j];
    for( k=0; k<mi_q && (frame<mi_a[k].base ||
          frame>=mi_a[k].base+mi_a[k].size); k++ );
    if( k>=mi_q )
    {
      locFunc( frame,"?",DWST_BASE_ADDR,NULL );
      continue;
    }
    modInfo *mi = mi_a + k;

    int l;
    for( l=j+1; l<fc && frames[l]>=mi->base &&
        frames[l]<mi->base+mi->size; l++ );

    if( dh->fdwstOfFile )
      dh->fdwstOfFile( mi->path,mi->base,frames+j,l-j,locFunc,dh );
    else
    {
      locFunc( mi->base,mi->path,DWST_BASE_ADDR,dh );
      int i;
      for( i=j; i<l; i++ )
        locFunc( frames[i],mi->path,DWST_NO_DBG_SYM,dh );
    }

    j = l - 1;
  }
}


#ifdef _WIN64
#define smain _smain
#define BITS "64"
#else
#define BITS "32"
#endif
void smain( void )
{
  textColor tc_o;
  textColor *tc = &tc_o;
  checkOutputVariant( tc );

  char *cmdLine = GetCommandLineA();
  char *args;
  if( cmdLine[0]=='"' && (args=strchr(cmdLine+1,'"')) )
    args++;
  else
    args = strchr( cmdLine,' ' );
  options defopt = {
    1,
    4,
    0xff,
    0xcc,
    0,
    1,
  };
  options opt = defopt;
  while( args )
  {
    while( args[0]==' ' ) args++;
    if( args[0]!='-' ) break;
    switch( args[1] )
    {
      case 'p':
        opt.protect = atoi( args+2 );
        if( opt.protect<0 ) opt.protect = 0;
        break;

      case 'a':
        opt.align = atoi( args+2 );
        if( opt.align<1 ) opt.align = 1;
        break;

      case 'i':
        opt.init = atoi( args+2 );
        break;

      case 's':
        opt.slackInit = atoi( args+2 );
        break;

      case 'f':
        opt.protectFree = atoi( args+2 );
        break;

      case 'h':
        opt.handleException = atoi( args+2 );
    }
    while( args[0] && args[0]!=' ' ) args++;
  }
  if( opt.protect<1 ) opt.protectFree = 0;
  if( !args || !args[0] )
  {
    char exePath[MAX_PATH];
    GetModuleFileName( NULL,exePath,MAX_PATH );
    char *delim = strrchr( exePath,'\\' );
    if( delim ) delim++;
    else delim = exePath;
    char *point = strrchr( delim,'.' );
    if( point ) point[0] = 0;

    printf( "Usage: %c%s %c[OPTION]... %cAPP [APP-OPTION]...%c\n",
        ATT_OK,delim,ATT_INFO,ATT_SECTION,ATT_NORMAL );
    printf( "    %c-p%cX%c    page protection [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.protect,ATT_NORMAL );
    printf( "             %c0%c = off\n",ATT_INFO,ATT_NORMAL );
    printf( "             %c1%c = after\n",ATT_INFO,ATT_NORMAL );
    printf( "             %c2%c = before\n",ATT_INFO,ATT_NORMAL );
    printf( "    %c-a%cX%c    alignment [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.align,ATT_NORMAL );
    printf( "    %c-i%cX%c    initial value [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.init,ATT_NORMAL );
    printf( "    %c-s%cX%c    initial value for slack [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.slackInit,ATT_NORMAL );
    printf( "    %c-f%cX%c    freed memory protection [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.protectFree,ATT_NORMAL );
    printf( "    %c-h%cX%c    handle exceptions [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,defopt.handleException,ATT_NORMAL );
    ExitProcess( 1 );
  }

  STARTUPINFO si = {0};
  PROCESS_INFORMATION pi = {0};
  si.cb = sizeof(STARTUPINFO);
  BOOL result = CreateProcess( NULL,args,NULL,NULL,FALSE,
      CREATE_SUSPENDED,NULL,NULL,&si,&pi );
  if( !result )
  {
    printf( "%ccan't create process for '%s'\n%c",ATT_WARN,args,ATT_NORMAL );
    ExitProcess( 1 );
  }

  HANDLE readPipe = NULL;
  char exePath[MAX_PATH];
  if( isWrongArch(pi.hProcess) )
    printf( "%conly " BITS "bit applications possible\n",ATT_WARN );
  else
    readPipe = inject( pi.hProcess,&opt,exePath,tc );
  if( !readPipe )
    TerminateProcess( pi.hProcess,1 );

  if( readPipe )
  {
    HMODULE symMod = LoadLibrary( "dbghelp.dll" );
    HMODULE dwstMod = LoadLibrary( "dwarfstack.dll" );
    if( !dwstMod )
      dwstMod = LoadLibrary( "dwarfstack" BITS ".dll" );
    func_SymSetOptions *fSymSetOptions = NULL;
    func_SymInitialize *fSymInitialize = NULL;
    func_SymCleanup *fSymCleanup = NULL;
    dbghelp dh;
    dh.process = pi.hProcess;
    dh.fSymGetLineFromAddr64 = NULL;
    dh.fdwstOfFile = NULL;
    dh.tc = tc;
    if( symMod )
    {
      fSymSetOptions =
        (func_SymSetOptions*)GetProcAddress( symMod,"SymSetOptions" );
      fSymInitialize =
        (func_SymInitialize*)GetProcAddress( symMod,"SymInitialize" );
      fSymCleanup =
        (func_SymCleanup*)GetProcAddress( symMod,"SymCleanup" );
      dh.fSymGetLineFromAddr64 =
        (func_SymGetLineFromAddr64*)GetProcAddress(
            symMod,"SymGetLineFromAddr64" );
    }
    if( dwstMod )
    {
      dh.fdwstOfFile =
        (func_dwstOfFile*)GetProcAddress( dwstMod,"dwstOfFile" );
    }
    if( fSymSetOptions )
      fSymSetOptions( SYMOPT_LOAD_LINES );
    if( fSymInitialize )
    {
      char *delim = strrchr( exePath,'\\' );
      if( delim ) delim[0] = 0;
      fSymInitialize( pi.hProcess,exePath,TRUE );
    }

    ResumeThread( pi.hThread );

    DWORD didread;
    int type;
    modInfo *mi_a = NULL;
    int mi_q = 0;
    allocation *alloc_a = NULL;
    int alloc_q = -2;
    HANDLE heap = GetProcessHeap();
    UINT exitCode = -1;
    while( ReadFile(readPipe,&type,sizeof(int),&didread,NULL) )
    {
      switch( type )
      {
#if WRITE_DEBUG_STRINGS
        case WRITE_STRING:
          {
            char buf[1024];
            char *bufpos = buf;
            while( ReadFile(readPipe,bufpos,1,&didread,NULL) )
            {
              if( bufpos[0]!='\n' )
              {
                bufpos++;
                continue;
              }
              bufpos[0] = 0;
              printf( "child: '%s'\n",buf );
              bufpos = buf;
              break;
            }
          }
          break;
#endif

        case WRITE_LEAKS:
          if( !ReadFile(readPipe,&exitCode,sizeof(UINT),&didread,NULL) )
          {
            alloc_q = -2;
            break;
          }
          if( !ReadFile(readPipe,&alloc_q,sizeof(int),&didread,NULL) )
          {
            alloc_q = -2;
            break;
          }
          if( !alloc_q ) break;
          HeapFree( heap,0,alloc_a );
          alloc_a = HeapAlloc( heap,0,alloc_q*sizeof(allocation) );
          if( !ReadFile(readPipe,alloc_a,alloc_q*sizeof(allocation),
                &didread,NULL) )
          {
            alloc_q = -2;
            break;
          }
          break;

        case WRITE_MODS:
          if( !ReadFile(readPipe,&mi_q,sizeof(int),&didread,NULL) )
            mi_q = 0;
          if( !mi_q ) break;
          HeapFree( heap,0,mi_a );
          mi_a = HeapAlloc( heap,0,mi_q*sizeof(modInfo) );
          if( !ReadFile(readPipe,mi_a,mi_q*sizeof(modInfo),&didread,NULL) )
          {
            mi_q = 0;
            break;
          }
          break;

        case WRITE_EXCEPTION:
          {
            exceptionInfo ei;
            if( !ReadFile(readPipe,&ei,sizeof(exceptionInfo),&didread,NULL) )
              break;

            const char *desc = NULL;
            switch( ei.er.ExceptionCode )
            {
#define EX_DESC( name ) \
              case EXCEPTION_##name: desc = " (" #name ")"; \
                                     break

              EX_DESC( ACCESS_VIOLATION );
              EX_DESC( ARRAY_BOUNDS_EXCEEDED );
              EX_DESC( BREAKPOINT );
              EX_DESC( DATATYPE_MISALIGNMENT );
              EX_DESC( FLT_DENORMAL_OPERAND );
              EX_DESC( FLT_DIVIDE_BY_ZERO );
              EX_DESC( FLT_INEXACT_RESULT );
              EX_DESC( FLT_INVALID_OPERATION );
              EX_DESC( FLT_OVERFLOW );
              EX_DESC( FLT_STACK_CHECK );
              EX_DESC( FLT_UNDERFLOW );
              EX_DESC( ILLEGAL_INSTRUCTION );
              EX_DESC( IN_PAGE_ERROR );
              EX_DESC( INT_DIVIDE_BY_ZERO );
              EX_DESC( INT_OVERFLOW );
              EX_DESC( INVALID_DISPOSITION );
              EX_DESC( NONCONTINUABLE_EXCEPTION );
              EX_DESC( PRIV_INSTRUCTION );
              EX_DESC( SINGLE_STEP );
              EX_DESC( STACK_OVERFLOW );
            }
            printf( "%c\nunhandled exception code: %x%s\n",
                ATT_WARN,ei.er.ExceptionCode,desc );

            printf( "%c  exception on:\n",ATT_SECTION );
            printStack( ei.aa[0].frames,mi_a,mi_q,&dh );

            if( ei.er.ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
                ei.er.NumberParameters==2 )
            {
              ULONG_PTR flag = ei.er.ExceptionInformation[0];
              char *addr = (char*)ei.er.ExceptionInformation[1];
              printf( "%c  %s violation at %p\n",
                  ATT_WARN,flag==8?"data execution prevention":
                  (flag?"write access":"read access"),addr );

              if( ei.aq>1 )
              {
                char *ptr = (char*)ei.aa[1].ptr;
                size_t size = ei.aa[1].size;
                printf( "%c  %s %p (size %u, offset %s%d)\n",
                    ATT_INFO,ei.aq>2?"freed block":"protected area of",
                    ptr,size,addr>ptr?"+":"",addr-ptr );
                printf( "%c  allocated on:\n",ATT_SECTION );
                printStack( ei.aa[1].frames,mi_a,mi_q,&dh );

                if( ei.aq>2 )
                {
                  printf( "%c  freed on:\n",ATT_SECTION );
                  printStack( ei.aa[2].frames,mi_a,mi_q,&dh );
                }
              }
            }

            alloc_q = -1;
          }
          break;

        case WRITE_ALLOC_FAIL:
          {
            allocation a;
            if( !ReadFile(readPipe,&a,sizeof(allocation),&didread,NULL) )
              break;

            printf( "%c\nallocation failed of %u bytes\n",
                ATT_WARN,a.size );
            printf( "%c  called on:\n",ATT_SECTION );
            printStack( a.frames,mi_a,mi_q,&dh );
          }
          break;

        case WRITE_FREE_FAIL:
          {
            allocation a;
            if( !ReadFile(readPipe,&a,sizeof(allocation),&didread,NULL) )
              break;

            printf( "%c\ndeallocation of invalid pointer %p\n",
                ATT_WARN,a.ptr );
            printf( "%c  called on:\n",ATT_SECTION );
            printStack( a.frames,mi_a,mi_q,&dh );
          }
          break;

        case WRITE_SLACK:
          {
            allocation aa[2];
            if( !ReadFile(readPipe,aa,2*sizeof(allocation),&didread,NULL) )
              break;

            printf( "%c\nwrite access violation at %p\n",
                ATT_WARN,aa[1].ptr );
            printf( "%c  slack area of %p (size %u, offset %s%d)\n",
                ATT_INFO,aa[0].ptr,aa[0].size,
                aa[1].ptr>aa[0].ptr?"+":"",(char*)aa[1].ptr-(char*)aa[0].ptr );
            printf( "%c  allocated on:\n",ATT_SECTION );
            printStack( aa[0].frames,mi_a,mi_q,&dh );
            printf( "%c  freed on:\n",ATT_SECTION );
            printStack( aa[1].frames,mi_a,mi_q,&dh );
          }
          break;

        case WRITE_MAIN_ALLOC_FAIL:
          printf( "%c\nnot enough memory to keep track of allocations\n",
              ATT_WARN );
          alloc_q = -1;
          break;
      }
    }

    if( !alloc_q )
    {
      printf( "%c\nno leaks found\n",ATT_OK );
      printf( "%cexit code: %u (%x)\n",
          ATT_SECTION,(uintptr_t)exitCode,exitCode );
    }
    else if( alloc_q>0 )
    {
      printf( "%c\nleaks:\n",ATT_SECTION );
      int i;
      size_t sumSize = 0;
      for( i=0; i<alloc_q; i++ )
      {
        allocation a;
        a = alloc_a[i];

        if( !a.ptr ) continue;

        size_t size = a.size;
        int nmb = 1;
        int j;
        for( j=i+1; j<alloc_q; j++ )
        {
          if( !alloc_a[j].ptr ||
              memcmp(a.frames,alloc_a[j].frames,PTRS*sizeof(void*)) )
            continue;

          size += alloc_a[j].size;
          alloc_a[j].ptr = NULL;
          nmb++;
        }
        sumSize += size;

        printf( "%c  %u B / %d\n",ATT_WARN,size,(intptr_t)nmb );

        printStack( a.frames,mi_a,mi_q,&dh );
      }
      printf( "%c  sum: %u B / %d\n",ATT_WARN,sumSize,(intptr_t)alloc_q );
      printf( "%cexit code: %u (%x)\n",
          ATT_SECTION,(uintptr_t)exitCode,exitCode );
    }
    else if( alloc_q<-1 )
    {
      printf( "%c\nunexpected end of application\n",ATT_WARN );
    }

    if( fSymCleanup ) fSymCleanup( pi.hProcess );
    if( symMod ) FreeLibrary( symMod );
    if( dwstMod ) FreeLibrary( dwstMod );
    HeapFree( heap,0,alloc_a );
    HeapFree( heap,0,mi_a );
    CloseHandle( readPipe );
  }
  CloseHandle( pi.hThread );
  CloseHandle( pi.hProcess );

  printf( "%c",ATT_NORMAL );

  ExitProcess( 0 );
}
