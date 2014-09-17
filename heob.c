
//          Copyright Hannes Domani 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#ifndef NO_DBGHELP
#include <dbghelp.h>
#endif

#ifndef NO_DWARFSTACK
#include <dwarfstack.h>
#else
#include <stdint.h>
#define DWST_BASE_ADDR   0
#define DWST_NO_DBG_SYM -1
#endif


#define PTRS 48

#define USE_STACKWALK       0
#define WRITE_DEBUG_STRINGS 0


typedef HMODULE WINAPI func_LoadLibraryA( LPCSTR );
typedef HMODULE WINAPI func_LoadLibraryW( LPCWSTR );
typedef BOOL WINAPI func_FreeLibrary( HMODULE );
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
typedef HANDLE WINAPI func_GetStdHandle( DWORD );
typedef BOOL WINAPI func_ReadConsoleInput(
    HANDLE,PINPUT_RECORD,DWORD,LPDWORD );

#ifndef NO_DBGHELP
typedef DWORD WINAPI func_SymSetOptions( DWORD );
typedef BOOL WINAPI func_SymInitialize( HANDLE,PCSTR,BOOL );
typedef BOOL WINAPI func_SymGetLineFromAddr64(
    HANDLE,DWORD64,PDWORD,PIMAGEHLP_LINE64 );
typedef BOOL WINAPI func_SymFromAddr(
    HANDLE,DWORD64,PDWORD64,PSYMBOL_INFO );
typedef BOOL WINAPI func_SymCleanup( HANDLE );
#if USE_STACKWALK
typedef BOOL WINAPI func_StackWalk64(
    DWORD,HANDLE,HANDLE,LPSTACKFRAME64,PVOID,PREAD_PROCESS_MEMORY_ROUTINE64,
    PFUNCTION_TABLE_ACCESS_ROUTINE64,PGET_MODULE_BASE_ROUTINE64,
    PTRANSLATE_ADDRESS_ROUTINE64 );
#endif
#endif

struct dbghelp;
#ifndef NO_DWARFSTACK
typedef int func_dwstOfFile( const char*,uint64_t,uint64_t*,int,
    void(*)(uint64_t,const char*,int,const char*,struct dbghelp*),
    struct dbghelp* );
#endif

typedef int WINAPI func_strlen( LPCSTR );
typedef int WINAPI func_strlenW( LPCWSTR );
typedef int WINAPI func_strcmp( LPCSTR,LPCSTR );

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

typedef struct
{
  const char *funcName;
  void *origFunc;
  void *myFunc;
}
replaceData;

typedef enum
{
  AT_MALLOC,
  AT_NEW,
  AT_NEW_ARR,
}
allocType;

typedef struct
{
  union {
    void *ptr;
    int count;
  };
  size_t size;
  void *frames[PTRS];
  allocType at;
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
typedef HMODULE func_replaceFuncs( struct remoteData*,HMODULE,
    const char*,replaceData*,unsigned int );
typedef void func_addModule( struct remoteData*,HMODULE );
typedef void func_trackAllocs( struct remoteData*,void*,void*,size_t,
    allocType );
#ifndef _WIN64
typedef int func_fixDataFuncAddr( unsigned char*,size_t,void* );
#endif
typedef void func_writeMods( struct remoteData*,allocation*,int );
typedef void func_exitWait( struct remoteData*,UINT );
typedef void func_replaceModFuncs( struct remoteData* );
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
  WRITE_WRONG_DEALLOC,
};

typedef struct
{
  intptr_t protect;
  intptr_t align;
  intptr_t init;
  intptr_t slackInit;
  intptr_t protectFree;
  intptr_t handleException;
  intptr_t newConsole;
  intptr_t fullPath;
  intptr_t allocMethod;
  intptr_t leakDetails;
  intptr_t useSp;
  intptr_t dlls;
  intptr_t pid;
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
  func_LoadLibraryA *fLoadLibraryA;
  func_LoadLibraryW *fLoadLibraryW;
  func_FreeLibrary *fFreeLibrary;
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
  func_SetEvent *fCloseHandle;
  func_GetStdHandle *fGetStdHandle;
  func_SetEvent *fFlushConsoleInputBuffer;
  func_ReadConsoleInput *fReadConsoleInput;

  func_strlen *fstrlen;
  func_strlenW *fstrlenW;
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
  func_getcwd *fgetcwd;
  func_wgetcwd *fwgetcwd;
  func_getdcwd *fgetdcwd;
  func_wgetdcwd *fwgetdcwd;
  func_fullpath *ffullpath;
  func_wfullpath *fwfullpath;
  func_tempnam *ftempnam;
  func_wtempnam *fwtempnam;

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
  func_getcwd *mgetcwd;
  func_wgetcwd *mwgetcwd;
  func_getdcwd *mgetdcwd;
  func_wgetdcwd *mwgetdcwd;
  func_fullpath *mfullpath;
  func_wfullpath *mwfullpath;
  func_tempnam *mtempnam;
  func_wtempnam *mwtempnam;
  func_LoadLibraryA *mLoadLibraryA;
  func_LoadLibraryW *mLoadLibraryW;
  func_FreeLibrary *mFreeLibrary;

  func_malloc *pmalloc;
  func_calloc *pcalloc;
  func_free *pfree;
  func_realloc *prealloc;
  func_strdup *pstrdup;
  func_wcsdup *pwcsdup;
  func_getcwd *pgetcwd;
  func_wgetcwd *pwgetcwd;
  func_getdcwd *pgetdcwd;
  func_wgetdcwd *pwgetdcwd;
  func_fullpath *pfullpath;
  func_wfullpath *pwfullpath;
  func_tempnam *ptempnam;
  func_wtempnam *pwtempnam;

  func_free *ofree;
  func_getcwd *ogetcwd;
  func_wgetcwd *owgetcwd;
  func_getdcwd *ogetdcwd;
  func_wgetdcwd *owgetdcwd;
  func_fullpath *ofullpath;
  func_wfullpath *owfullpath;
  func_tempnam *otempnam;
  func_wtempnam *owtempnam;

  func_pm_alloc *pm_alloc;
  func_pm_free *pm_free;
  func_pm_alloc_size *pm_alloc_size;

  func_replaceFuncs *mreplaceFuncs;
  func_addModule *maddModule;
  func_replaceModFuncs *mreplaceModFuncs;
  func_trackAllocs *mtrackAllocs;
#ifndef _WIN64
  func_fixDataFuncAddr *mfixDataFuncAddr;
#endif
  func_writeMods *mwriteMods;
  func_exitWait *mexitWait;

  HANDLE master;
  HANDLE initFinished;

  allocation *alloc_a;
  int alloc_q;
  int alloc_s;

  freed *freed_a;
  int freed_q;
  int freed_s;

  HMODULE *mod_a;
  int mod_q;
  int mod_s;
  int mod_d;

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
  union {
    int colors[ATT_COUNT];
    const char *styles[ATT_COUNT];
  };
  textColorAtt color;
}
textColor;


#undef RtlMoveMemory
VOID WINAPI RtlMoveMemory( PVOID,const VOID*,SIZE_T );
#undef RtlZeroMemory
VOID WINAPI RtlZeroMemory( PVOID,SIZE_T );

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
            {
              size_t l = 0;
              while( arg[l] ) l++;
              WriteFile( out,arg,l,&written,NULL );
            }
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
            char *end = str + sizeof(str);
            char *start = end;
            if( !arg )
              (--start)[0] = '0';
            while( arg )
            {
              (--start)[0] = arg%10 + '0';
              arg /= 10;
            }
            if( minus )
              (--start)[0] = '-';
            WriteFile( out,start,end-start,&written,NULL );
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
            WriteFile( out,str,end-str,&written,NULL );
          }
          break;

        case 'c':
          {
            textColorAtt arg = va_arg( vl,textColorAtt );
            if( tc->fTextColor && tc->color!=arg )
            {
              tc->fTextColor( tc,arg );
              tc->color = arg;
            }
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
    if( b1[i]!=b2[i] ) return( (int)b2[i]-(int)b1[i] );
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
static void TextColorHtml( textColor *tc,textColorAtt color )
{
  DWORD written;
  if( tc->color )
  {
    char spanEnd[] = "</span>";
    WriteFile( tc->out,spanEnd,sizeof(spanEnd)-1,&written,NULL );
  }
  if( color )
  {
    char span1[] = "<span class=\"";
    const char *style = tc->styles[color];
    char span2[] = "\">";
    WriteFile( tc->out,span1,sizeof(span1)-1,&written,NULL );
    size_t len;
    for( len=0; style[len]; len++ );
    WriteFile( tc->out,style,len,&written,NULL );
    WriteFile( tc->out,span2,sizeof(span2)-1,&written,NULL );
  }
}
static void checkOutputVariant( textColor *tc )
{
  tc->fTextColor = NULL;
  tc->out = GetStdHandle( STD_OUTPUT_HANDLE );
  tc->color = ATT_NORMAL;

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
    if( !fNtQueryObject(tc->out,ObjectNameInformation,
          oni,sizeof(OBJECT_NAME_INFORMATION),&len) )
    {
      wchar_t namedPipe[] = L"\\Device\\NamedPipe\\";
      size_t l1 = sizeof(namedPipe)/2 - 1;
      wchar_t toMaster[] = L"-to-master";
      size_t l2 = sizeof(toMaster)/2 - 1;
      wchar_t html[] = L".html";
      size_t hl = sizeof(html)/2 - 1;
      if( oni->Name.Length/2>l1+l2 &&
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
      else if( GetFileType(tc->out)==FILE_TYPE_DISK &&
          oni->Name.Length/2>hl &&
          !memcmp(oni->Name.Buffer+(oni->Name.Length/2-hl),html,hl*2) )
      {
        char styleInit[] =
          "<style type=\"text/css\">\n"
          "body { color:lightgrey; background-color:black; }\n"
          ".ok { color:lime; }\n"
          ".section { color:turquoise; }\n"
          ".info { color:violet; }\n"
          ".warn { color:red; }\n"
          ".base { color:black; background-color:grey; }\n"
          "</style>\n"
          "<body><pre>\n";
        DWORD written;
        WriteFile( tc->out,styleInit,sizeof(styleInit)-1,&written,NULL );

        tc->fTextColor = &TextColorHtml;

        tc->styles[ATT_NORMAL]  = NULL;
        tc->styles[ATT_OK]      = "ok";
        tc->styles[ATT_SECTION] = "section";
        tc->styles[ATT_INFO]    = "info";
        tc->styles[ATT_WARN]    = "warn";
        tc->styles[ATT_BASE]    = "base";
      }
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

  rd->mtrackAllocs( rd,NULL,b,s,AT_MALLOC );

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

  rd->mtrackAllocs( rd,NULL,b,n*s,AT_MALLOC );

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

  rd->mtrackAllocs( rd,b,NULL,-1,AT_MALLOC );

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

  rd->mtrackAllocs( rd,b,nb,s,AT_MALLOC );

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

  rd->mtrackAllocs( rd,NULL,b,rd->fstrlen(s)+1,AT_MALLOC );

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

  rd->mtrackAllocs( rd,NULL,b,(rd->fstrlenW(s)+1)*2,AT_MALLOC );

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

  rd->mtrackAllocs( rd,NULL,b,s,AT_NEW );

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

  rd->mtrackAllocs( rd,b,NULL,-1,AT_NEW );

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

  rd->mtrackAllocs( rd,NULL,b,s,AT_NEW_ARR );

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

  rd->mtrackAllocs( rd,b,NULL,-1,AT_NEW_ARR );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_delete_a\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif
}

static char *new_getcwd( char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->fgetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = rd->fstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  rd->mtrackAllocs( rd,NULL,cwd,l,AT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_getcwd\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( cwd );
}

static wchar_t *new_wgetcwd( wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->fwgetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = rd->fstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  rd->mtrackAllocs( rd,NULL,cwd,l*2,AT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wgetcwd\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( cwd );
}

static char *new_getdcwd( int drive,char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->fgetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = rd->fstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  rd->mtrackAllocs( rd,NULL,cwd,l,AT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_getdcwd\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( cwd );
}

static wchar_t *new_wgetdcwd( int drive,wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->fwgetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = rd->fstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  rd->mtrackAllocs( rd,NULL,cwd,l*2,AT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wgetdcwd\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( cwd );
}

static char *new_fullpath( char *absPath,const char *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  char *fp = rd->ffullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  rd->mtrackAllocs( rd,NULL,fp,MAX_PATH,AT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_fullpath\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( fp );
}

static wchar_t *new_wfullpath( wchar_t *absPath,const wchar_t *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  wchar_t *fp = rd->fwfullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  rd->mtrackAllocs( rd,NULL,fp,MAX_PATH*2,AT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wfullpath\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( fp );
}

static char *new_tempnam( char *dir,char *prefix )
{
  GET_REMOTEDATA( rd );
  char *tn = rd->ftempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = rd->fstrlen( tn ) + 1;
  rd->mtrackAllocs( rd,NULL,tn,l,AT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_tempnam\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( tn );
}

static wchar_t *new_wtempnam( wchar_t *dir,wchar_t *prefix )
{
  GET_REMOTEDATA( rd );
  wchar_t *tn = rd->fwtempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = rd->fstrlenW( tn ) + 1;
  rd->mtrackAllocs( rd,NULL,tn,l*2,AT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wtempnam\n";
  DWORD written;
  int type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( tn );
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

  rd->mexitWait( rd,c );
}

static LPTOP_LEVEL_EXCEPTION_FILTER WINAPI new_SUEF(
    LPTOP_LEVEL_EXCEPTION_FILTER plTopLevelExceptionFilter )
{
  (void)plTopLevelExceptionFilter;

  return( NULL );
}

static HMODULE WINAPI new_LoadLibraryA( LPCSTR name )
{
  GET_REMOTEDATA( rd );

#if WRITE_DEBUG_STRINGS
  int type;
  DWORD written;
  char t[] = "called: new_LoadLibraryA\n";
  type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  HMODULE mod = rd->fLoadLibraryA( name );

  rd->fEnterCriticalSection( &rd->cs );
  rd->maddModule( rd,mod );
  rd->mreplaceModFuncs( rd );
  rd->fLeaveCriticalSection( &rd->cs );

  return( mod );
}

static HMODULE WINAPI new_LoadLibraryW( LPCWSTR name )
{
  GET_REMOTEDATA( rd );

#if WRITE_DEBUG_STRINGS
  int type;
  DWORD written;
  char t[] = "called: new_LoadLibraryW\n";
  type = WRITE_STRING;
  rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
  rd->fWriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  HMODULE mod = rd->fLoadLibraryW( name );

  rd->fEnterCriticalSection( &rd->cs );
  rd->maddModule( rd,mod );
  rd->mreplaceModFuncs( rd );
  rd->fLeaveCriticalSection( &rd->cs );

  return( mod );
}

static BOOL WINAPI new_FreeLibrary( HMODULE mod )
{
  (void)mod;

  return( TRUE );
}


static void *protect_alloc_m( remoteData *rd,size_t s )
{
  while( s%rd->opt.align ) s++;

  DWORD pageSize = rd->pageSize;
  size_t pages = s ? ( s-1 )/pageSize + 2 : 1;

  unsigned char *b = (unsigned char*)rd->fVirtualAlloc(
      NULL,pages*pageSize,MEM_RESERVE,PAGE_NOACCESS );
  if( !b )
    return( NULL );

  size_t slackSize = ( pageSize - (s%pageSize) )%pageSize;

  if( rd->opt.protect>1 )
    b += pageSize;

  if( pages>1 )
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
  if( s==(size_t)-1 ) return;

  DWORD pageSize = rd->pageSize;
  size_t pages = s ? ( s-1 )/pageSize + 2 : 1;

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
  size_t s = i>=0 ? rd->alloc_a[i].size : (size_t)-1;

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
    return( rd->pm_alloc(rd,s) );
  }

  if( !b )
    return( rd->pm_alloc(rd,s) );

  size_t os = rd->pm_alloc_size( rd,b );
  if( os==(size_t)-1 ) return( NULL );

  void *nb = rd->pm_alloc( rd,s );
  if( !nb ) return( NULL );

  size_t cs = os<s ? os : s;
  if( cs )
    rd->fMoveMemory( nb,b,cs );

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

  size_t l = rd->fstrlenW( s ) + 1;
  l *= 2;

  wchar_t *b = rd->pm_alloc( rd,l );
  if( !b ) return( NULL );

  rd->fMoveMemory( b,s,l );

  return( b );
}

static char *protect_getcwd( char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->ogetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = rd->fstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;

  char *cwd_copy = rd->pm_alloc( rd,l );
  if( cwd_copy )
    rd->fMoveMemory( cwd_copy,cwd,l );

  rd->ofree( cwd );

  return( cwd_copy );
}

static wchar_t *protect_wgetcwd( wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->owgetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = rd->fstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  l *= 2;

  wchar_t *cwd_copy = rd->pm_alloc( rd,l );
  if( cwd_copy )
    rd->fMoveMemory( cwd_copy,cwd,l );

  rd->ofree( cwd );

  return( cwd_copy );
}

static char *protect_getdcwd( int drive,char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->ogetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = rd->fstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;

  char *cwd_copy = rd->pm_alloc( rd,l );
  if( cwd_copy )
    rd->fMoveMemory( cwd_copy,cwd,l );

  rd->ofree( cwd );

  return( cwd_copy );
}

static wchar_t *protect_wgetdcwd( int drive,wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->owgetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = rd->fstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  l *= 2;

  wchar_t *cwd_copy = rd->pm_alloc( rd,l );
  if( cwd_copy )
    rd->fMoveMemory( cwd_copy,cwd,l );

  rd->ofree( cwd );

  return( cwd_copy );
}

static char *protect_fullpath( char *absPath,const char *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  char *fp = rd->ofullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  size_t l = rd->fstrlen( fp ) + 1;

  char *fp_copy = rd->pm_alloc( rd,l );
  if( fp_copy )
    rd->fMoveMemory( fp_copy,fp,l );

  rd->ofree( fp );

  return( fp_copy );
}

static wchar_t *protect_wfullpath( wchar_t *absPath,const wchar_t *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  wchar_t *fp = rd->owfullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  size_t l = rd->fstrlenW( fp ) + 1;
  l *= 2;

  wchar_t *fp_copy = rd->pm_alloc( rd,l );
  if( fp_copy )
    rd->fMoveMemory( fp_copy,fp,l );

  rd->ofree( fp );

  return( fp_copy );
}

static char *protect_tempnam( char *dir,char *prefix )
{
  GET_REMOTEDATA( rd );
  char *tn = rd->otempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = rd->fstrlen( tn ) + 1;

  char *tn_copy = rd->pm_alloc( rd,l );
  if( tn_copy )
    rd->fMoveMemory( tn_copy,tn,l );

  rd->ofree( tn );

  return( tn_copy );
}

static wchar_t *protect_wtempnam( wchar_t *dir,wchar_t *prefix )
{
  GET_REMOTEDATA( rd );
  wchar_t *tn = rd->owtempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = rd->fstrlenW( tn ) + 1;
  l *= 2;

  wchar_t *tn_copy = rd->pm_alloc( rd,l );
  if( tn_copy )
    rd->fMoveMemory( tn_copy,tn,l );

  rd->ofree( tn );

  return( tn_copy );
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
          noAccessEnd = ptr + ( size?(size-1)/rd->pageSize+1:0 )*rd->pageSize;
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
  HMODULE symMod = rd->fLoadLibraryW( dll_dbghelp );
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
    if( rd->opt.useSp )
    {
      ULONG_PTR csp = *(ULONG_PTR*)ep->ContextRecord->csp;
      if( csp ) frames[count++] = (void*)csp;
    }
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

  rd->mexitWait( rd,1 );

  return( EXCEPTION_EXECUTE_HANDLER );
}


#define REL_PTR( base,ofs ) ( ((PBYTE)base)+ofs )

static HMODULE replaceFuncs( remoteData *rd,HMODULE app,
    const char *called,replaceData *rep,unsigned int count )
{
  if( !app ) return( NULL );

  PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)app;
  PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );
  if( IMAGE_NT_SIGNATURE!=inh->Signature )
    return( NULL );

  PIMAGE_IMPORT_DESCRIPTOR iid =
    (PIMAGE_IMPORT_DESCRIPTOR)REL_PTR( idh,inh->OptionalHeader.
        DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress );

  PSTR repModName = NULL;
  HMODULE repModule = NULL;
  UINT i;
  for( i=0; iid[i].Characteristics; i++ )
  {
    if( !iid[i].FirstThunk || !iid[i].OriginalFirstThunk )
      break;

    PSTR curModName = (PSTR)REL_PTR( idh,iid[i].Name );
    if( rd->fIsBadReadPtr(curModName,1) || !curModName[0] ) continue;
    HMODULE curModule = rd->fGetModuleHandle( curModName );
    if( !curModule ) continue;
    if( rd->opt.dlls )
      rd->maddModule( rd,curModule );
    if( called && rd->fstrcmpi(curModName,called) )
      continue;

    PIMAGE_THUNK_DATA thunk =
      (PIMAGE_THUNK_DATA)REL_PTR( idh,iid[i].FirstThunk );
    PIMAGE_THUNK_DATA originalThunk =
      (PIMAGE_THUNK_DATA)REL_PTR( idh,iid[i].OriginalFirstThunk );

    if( !repModName && called )
    {
      repModName = curModName;
      repModule = curModule;
    }
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
      repModule = curModule;

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

  return( repModule );
}

static void addModule( remoteData *rd,HMODULE mod )
{
  int m;
  for( m=0; m<rd->mod_q && rd->mod_a[m]!=mod; m++ );
  if( m<rd->mod_q ) return;

  if( rd->mod_q>=rd->mod_s )
  {
    rd->mod_s += 64;
    HMODULE *mod_an;
    if( !rd->mod_a )
      mod_an = rd->fHeapAlloc(
          rd->heap,0,rd->mod_s*sizeof(HMODULE) );
    else
      mod_an = rd->fHeapReAlloc(
          rd->heap,0,rd->mod_a,rd->mod_s*sizeof(HMODULE) );
    if( !mod_an )
    {
      DWORD written;
      int type = WRITE_MAIN_ALLOC_FAIL;
      rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
      rd->mexitWait( rd,1 );
    }
    rd->mod_a = mod_an;
  }

  rd->mod_a[rd->mod_q++] = mod;
}

static void replaceModFuncs( struct remoteData *rd )
{
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
  char fname_getcwd[] = "_getcwd";
  char fname_wgetcwd[] = "_wgetcwd";
  char fname_getdcwd[] = "_getdcwd";
  char fname_wgetdcwd[] = "_wgetdcwd";
  char fname_fullpath[] = "_fullpath";
  char fname_wfullpath[] = "_wfullpath";
  char fname_tempnam[] = "_tempnam";
  char fname_wtempnam[] = "_wtempnam";
  replaceData rep[] = {
    { fname_malloc         ,&rd->fmalloc         ,rd->mmalloc          },
    { fname_calloc         ,&rd->fcalloc         ,rd->mcalloc          },
    { fname_free           ,&rd->ffree           ,rd->mfree            },
    { fname_realloc        ,&rd->frealloc        ,rd->mrealloc         },
    { fname_strdup         ,&rd->fstrdup         ,rd->mstrdup          },
    { fname_wcsdup         ,&rd->fwcsdup         ,rd->mwcsdup          },
    { fname_op_new         ,&rd->fop_new         ,rd->mop_new          },
    { fname_op_delete      ,&rd->fop_delete      ,rd->mop_delete       },
    { fname_op_new_a       ,&rd->fop_new_a       ,rd->mop_new_a        },
    { fname_op_delete_a    ,&rd->fop_delete_a    ,rd->mop_delete_a     },
    { fname_getcwd         ,&rd->fgetcwd         ,rd->mgetcwd          },
    { fname_wgetcwd        ,&rd->fwgetcwd        ,rd->mwgetcwd         },
    { fname_getdcwd        ,&rd->fgetdcwd        ,rd->mgetdcwd         },
    { fname_wgetdcwd       ,&rd->fwgetdcwd       ,rd->mwgetdcwd        },
    { fname_fullpath       ,&rd->ffullpath       ,rd->mfullpath        },
    { fname_wfullpath      ,&rd->fwfullpath      ,rd->mwfullpath       },
    { fname_tempnam        ,&rd->ftempnam        ,rd->mtempnam         },
    { fname_wtempnam       ,&rd->fwtempnam       ,rd->mwtempnam        },
  };

  char fname_ExitProcess[] = "ExitProcess";
  char fname_SUEF[] = "SetUnhandledExceptionFilter";
  replaceData rep2[] = {
    { fname_ExitProcess,&rd->fExitProcess                ,rd->mExitProcess },
    { fname_SUEF       ,&rd->fSetUnhandledExceptionFilter,rd->mSUEF        },
  };
  unsigned int rep2count = sizeof(rep2)/sizeof(replaceData);
  if( !rd->opt.handleException )
    rep2count--;

  char fname_LoadLibraryA[] = "LoadLibraryA";
  char fname_LoadLibraryW[] = "LoadLibraryW";
  char fname_FreeLibrary[] = "FreeLibrary";
  replaceData repLL[] = {
    { fname_LoadLibraryA  ,&rd->fLoadLibraryA  ,rd->mLoadLibraryA   },
    { fname_LoadLibraryW  ,&rd->fLoadLibraryW  ,rd->mLoadLibraryW   },
    { fname_FreeLibrary   ,&rd->fFreeLibrary   ,rd->mFreeLibrary    },
  };

  for( ; rd->mod_d<rd->mod_q; rd->mod_d++ )
  {
    HMODULE mod = rd->mod_a[rd->mod_d];

    HMODULE dll_msvcrt =
      rd->mreplaceFuncs( rd,mod,NULL,rep,sizeof(rep)/sizeof(replaceData) );
    if( !rd->mod_d )
    {
      if( !dll_msvcrt )
      {
        rd->master = NULL;
        return;
      }
      rd->maddModule( rd,dll_msvcrt );

      if( rd->opt.protect )
      {
        rd->ofree = rd->fGetProcAddress( dll_msvcrt,fname_free );
        rd->ogetcwd = rd->fGetProcAddress( dll_msvcrt,fname_getcwd );
        rd->owgetcwd = rd->fGetProcAddress( dll_msvcrt,fname_wgetcwd );
        rd->ogetdcwd = rd->fGetProcAddress( dll_msvcrt,fname_getdcwd );
        rd->owgetdcwd = rd->fGetProcAddress( dll_msvcrt,fname_wgetdcwd );
        rd->ofullpath = rd->fGetProcAddress( dll_msvcrt,fname_fullpath );
        rd->owfullpath = rd->fGetProcAddress( dll_msvcrt,fname_wfullpath );
        rd->otempnam = rd->fGetProcAddress( dll_msvcrt,fname_tempnam );
        rd->owtempnam = rd->fGetProcAddress( dll_msvcrt,fname_wtempnam );
      }
    }

    unsigned int i;
    for( i=0; i<rep2count; i++ )
      rd->mreplaceFuncs( rd,mod,NULL,rep2+i,1 );

    if( rd->opt.dlls>1 )
      rd->mreplaceFuncs( rd,mod,NULL,repLL,sizeof(repLL)/sizeof(replaceData) );
  }
}

#ifndef _WIN64
static int fixDataFuncAddr( unsigned char *pos,size_t size,void *data )
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

static void trackAllocs( struct remoteData *rd,
    void *free_ptr,void *alloc_ptr,size_t alloc_size,allocType at )
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
            rd->mexitWait( rd,1 );
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

      if( rd->opt.allocMethod && rd->alloc_a[i].at!=at )
      {
        allocation aa[2];
        rd->fMoveMemory( aa,rd->alloc_a+i,sizeof(allocation) );
        void **frames = aa[1].frames;
        int ptrs = rd->fCaptureStackBackTrace( 2,PTRS,frames,NULL );
        if( ptrs<PTRS )
          rd->fZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );
        aa[1].ptr = free_ptr;
        aa[1].size = 0;
        aa[1].at = at;

        rd->mwriteMods( rd,aa,2 );

        int type = WRITE_WRONG_DEALLOC;
        DWORD written;
        rd->fWriteFile( rd->master,&type,sizeof(int),&written,NULL );
        rd->fWriteFile( rd->master,aa,2*sizeof(allocation),&written,NULL );
      }

      rd->alloc_q--;
      if( i<rd->alloc_q ) rd->alloc_a[i] = rd->alloc_a[rd->alloc_q];
    }
    else
    {
      allocation a;
      a.ptr = free_ptr;
      a.size = 0;
      a.at = at;

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
        rd->mexitWait( rd,1 );
      }
      rd->alloc_a = alloc_an;
    }
    allocation *a = rd->alloc_a + rd->alloc_q;
    rd->alloc_q++;
    a->ptr = alloc_ptr;
    a->size = alloc_size;
    a->at = at;

    void **frames = a->frames;
    int ptrs = rd->fCaptureStackBackTrace( 2,PTRS,frames,NULL );
    if( ptrs<PTRS )
      rd->fZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );

    rd->fLeaveCriticalSection( &rd->cs );
  }
  else if( alloc_size!=(size_t)-1 )
  {
    allocation a;
    a.ptr = NULL;
    a.size = alloc_size;
    a.at = at;

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
      if( !rd->fVirtualQuery((void*)ptr,&mbi,
            sizeof(MEMORY_BASIC_INFORMATION)) )
        continue;
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
        rd->mexitWait( rd,1 );
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

static void exitWait( struct remoteData *rd,UINT c )
{
  rd->fCloseHandle( rd->master );

  if( rd->opt.newConsole )
  {
    HANDLE in = rd->fGetStdHandle( STD_INPUT_HANDLE );
    if( rd->fFlushConsoleInputBuffer(in) )
    {
      HANDLE out = rd->fGetStdHandle( STD_OUTPUT_HANDLE );
      DWORD written;
      char exitText[] =
        "\n\n-------------------- APPLICATION EXIT --------------------\n";
      rd->fWriteFile( out,exitText,sizeof(exitText)-1,&written,NULL );

      INPUT_RECORD ir;
      DWORD didread;
      while( rd->fReadConsoleInput(in,&ir,1,&didread) &&
          (ir.EventType!=KEY_EVENT || !ir.Event.KeyEvent.bKeyDown ||
           ir.Event.KeyEvent.wVirtualKeyCode==VK_SHIFT ||
           ir.Event.KeyEvent.wVirtualKeyCode==VK_CAPITAL ||
           ir.Event.KeyEvent.wVirtualKeyCode==VK_CONTROL ||
           ir.Event.KeyEvent.wVirtualKeyCode==VK_MENU ||
           ir.Event.KeyEvent.wVirtualKeyCode==VK_LWIN ||
           ir.Event.KeyEvent.wVirtualKeyCode==VK_RWIN) );
    }
  }

  rd->fExitProcess( c );
}


static DWORD WINAPI remoteCall( remoteData *rd )
{
  HMODULE app = rd->fLoadLibraryW( rd->exePath );
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
  RtlZeroMemory( data,sizeof(remoteData) );

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
  data->fLoadLibraryA =
    (func_LoadLibraryA*)GetProcAddress( kernel32,"LoadLibraryA" );
  data->fLoadLibraryW =
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
  data->fCloseHandle =
    (func_SetEvent*)GetProcAddress( kernel32,"CloseHandle" );
  data->fGetStdHandle =
    (func_GetStdHandle*)GetProcAddress( kernel32,"GetStdHandle" );
  data->fFlushConsoleInputBuffer =
    (func_SetEvent*)GetProcAddress( kernel32,"FlushConsoleInputBuffer" );
  data->fReadConsoleInput =
    (func_ReadConsoleInput*)GetProcAddress( kernel32,"ReadConsoleInputA" );
  data->fExitProcess =
    (func_ExitProcess*)GetProcAddress( kernel32,"ExitProcess" );

  data->fstrlen =
    (func_strlen*)GetProcAddress( kernel32,"lstrlen" );
  data->fstrlenW =
    (func_strlenW*)GetProcAddress( kernel32,"lstrlenW" );
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
    RtlMoveMemory( exePath,data->exePathA,MAX_PATH );
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
    { &replaceFuncs             ,&rd->mreplaceFuncs              },
    { &addModule                ,&rd->maddModule                 },
    { &replaceModFuncs          ,&rd->mreplaceModFuncs           },
    { &trackAllocs              ,&rd->mtrackAllocs               },
#ifndef _WIN64
    { &fixDataFuncAddr          ,&rd->mfixDataFuncAddr           },
#endif
    { &writeMods                ,&rd->mwriteMods                 },
    { &exitWait                 ,&rd->mexitWait                  },
    { &inject                   ,&dataPtr                        },
    { &protect_alloc_m          ,&rd->pm_alloc                   },
    { &protect_free_m           ,&rd->pm_free                    },
    { &alloc_size               ,&rd->pm_alloc_size              },
    { &new_SUEF                 ,&rd->mSUEF                      },
    { &new_FreeLibrary          ,&rd->mFreeLibrary               },
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
    { &new_malloc                  ,&rd->mmalloc                  },
    { &new_calloc                  ,&rd->mcalloc                  },
    { &new_free                    ,&rd->mfree                    },
    { &new_realloc                 ,&rd->mrealloc                 },
    { &new_strdup                  ,&rd->mstrdup                  },
    { &new_wcsdup                  ,&rd->mwcsdup                  },
    { &new_op_new                  ,&rd->mop_new                  },
    { &new_op_delete               ,&rd->mop_delete               },
    { &new_op_new_a                ,&rd->mop_new_a                },
    { &new_op_delete_a             ,&rd->mop_delete_a             },
    { &new_getcwd                  ,&rd->mgetcwd                  },
    { &new_wgetcwd                 ,&rd->mwgetcwd                 },
    { &new_getdcwd                 ,&rd->mgetdcwd                 },
    { &new_wgetdcwd                ,&rd->mwgetdcwd                },
    { &new_fullpath                ,&rd->mfullpath                },
    { &new_wfullpath               ,&rd->mwfullpath               },
    { &new_tempnam                 ,&rd->mtempnam                 },
    { &new_wtempnam                ,&rd->mwtempnam                },
    { &new_ExitProcess             ,&rd->mExitProcess             },
    { &protect_malloc              ,&rd->pmalloc                  },
    { &protect_calloc              ,&rd->pcalloc                  },
    { &protect_free                ,&rd->pfree                    },
    { &protect_realloc             ,&rd->prealloc                 },
    { &protect_strdup              ,&rd->pstrdup                  },
    { &protect_wcsdup              ,&rd->pwcsdup                  },
    { &protect_getcwd              ,&rd->pgetcwd                  },
    { &protect_wgetcwd             ,&rd->pwgetcwd                 },
    { &protect_getdcwd             ,&rd->pgetdcwd                 },
    { &protect_wgetdcwd            ,&rd->pwgetdcwd                },
    { &protect_fullpath            ,&rd->pfullpath                },
    { &protect_wfullpath           ,&rd->pwfullpath               },
    { &protect_tempnam             ,&rd->ptempnam                 },
    { &protect_wtempnam            ,&rd->pwtempnam                },
    { &exceptionWalker             ,&exceptionWalkerV             },
    { &new_LoadLibraryA            ,&rd->mLoadLibraryA            },
    { &new_LoadLibraryW            ,&rd->mLoadLibraryW            },
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
    rd->fgetcwd = rd->pgetcwd;
    rd->fwgetcwd = rd->pwgetcwd;
    rd->fgetdcwd = rd->pgetdcwd;
    rd->fwgetdcwd = rd->pwgetdcwd;
    rd->ffullpath = rd->pfullpath;
    rd->fwfullpath = rd->pwfullpath;
    rd->ftempnam = rd->ptempnam;
    rd->fwtempnam = rd->pwtempnam;
  }

  if( rd->opt.handleException )
    rd->fSetUnhandledExceptionFilter( exceptionWalkerV );

  rd->maddModule( rd,rd->fGetModuleHandle(NULL) );
  rd->mreplaceModFuncs( rd );

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
#ifndef NO_DBGHELP
  func_SymGetLineFromAddr64 *fSymGetLineFromAddr64;
  func_SymFromAddr *fSymFromAddr;
#endif
#ifndef NO_DWARFSTACK
  func_dwstOfFile *fdwstOfFile;
#endif
  textColor *tc;
  options *opt;
}
dbghelp;

static void locFunc(
    uint64_t addr,const char *filename,int lineno,const char *funcname,
    dbghelp *dh )
{
  textColor *tc = dh->tc;

#ifndef NO_DBGHELP
  IMAGEHLP_LINE64 il;
  char buffer[sizeof(SYMBOL_INFO)+100];
  SYMBOL_INFO *si = (SYMBOL_INFO*)&buffer;
  if( lineno==DWST_NO_DBG_SYM && dh->fSymGetLineFromAddr64 )
  {
    RtlZeroMemory( &il,sizeof(IMAGEHLP_LINE64) );
    il.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD dis;
    if( dh->fSymGetLineFromAddr64(dh->process,addr,&dis,&il) )
    {
      filename = il.FileName;
      lineno = il.LineNumber;
    }

    if( dh->fSymFromAddr )
    {
      DWORD64 dis64;
      si->MaxNameLen = 100;
      if( dh->fSymFromAddr(dh->process,addr,&dis64,si) )
        funcname = si->Name;
    }
  }
#endif

  if( !dh->opt->fullPath )
  {
    const char *sep1 = strrchr( filename,'/' );
    const char *sep2 = strrchr( filename,'\\' );
    if( sep2>sep1 ) sep1 = sep2;
    if( sep1 ) filename = sep1 + 1;
  }

  switch( lineno )
  {
    case DWST_BASE_ADDR:
      printf( "    %c%p%c   %c%s%c\n",
          ATT_BASE,(uintptr_t)addr,ATT_NORMAL,
          ATT_BASE,filename,ATT_NORMAL );
      break;

    case DWST_NO_DBG_SYM:
#ifndef NO_DWARFSTACK
    case DWST_NO_SRC_FILE:
    case DWST_NOT_FOUND:
#endif
      printf( "%c      %p",ATT_NORMAL,(uintptr_t)addr );
      if( funcname )
        printf( "   [%c%s%c]",ATT_INFO,funcname,ATT_NORMAL );
      printf( "\n" );
      break;

    default:
      printf( "%c      %p   %c%s%c:%d",
          ATT_NORMAL,(uintptr_t)addr,
          ATT_OK,filename,ATT_NORMAL,(intptr_t)lineno );
      if( funcname )
        printf( " [%c%s%c]",ATT_INFO,funcname,ATT_NORMAL );
      printf( "\n" );
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
      locFunc( frame,"?",DWST_BASE_ADDR,NULL,dh );
      continue;
    }
    modInfo *mi = mi_a + k;

    int l;
    for( l=j+1; l<fc && frames[l]>=mi->base &&
        frames[l]<mi->base+mi->size; l++ );

#ifndef NO_DWARFSTACK
    if( dh->fdwstOfFile )
      dh->fdwstOfFile( mi->path,mi->base,frames+j,l-j,locFunc,dh );
    else
#endif
    {
      locFunc( mi->base,mi->path,DWST_BASE_ADDR,NULL,dh );
      int i;
      for( i=j; i<l; i++ )
        locFunc( frames[i],mi->path,DWST_NO_DBG_SYM,NULL,dh );
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
#ifndef _WIN64
    8,
#else
    16,
#endif
    0xff,
    0xcc,
    0,
    1,
    0,
    0,
    1,
    1,
    0,
    0,
    0,
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
        break;

      case 'c':
        opt.newConsole = atoi( args+2 );
        break;

      case 'F':
        opt.fullPath = atoi( args+2 );
        break;

      case 'm':
        opt.allocMethod = atoi( args+2 );
        break;

      case 'l':
        opt.leakDetails = atoi( args+2 );
        break;

      case 'S':
        opt.useSp = atoi( args+2 );
        break;

      case 'd':
        opt.dlls = atoi( args+2 );
        break;

      case 'P':
        opt.pid = atoi( args+2 );
        break;
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
    printf( "    %c-c%cX%c    create new console [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.newConsole,ATT_NORMAL );
    printf( "    %c-F%cX%c    show full path [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.fullPath,ATT_NORMAL );
    printf( "    %c-m%cX%c    compare allocation/release method [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.allocMethod,ATT_NORMAL );
    printf( "    %c-l%cX%c    show leak details [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.leakDetails,ATT_NORMAL );
    printf( "    %c-S%cX%c    use stack pointer in exception [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.useSp,ATT_NORMAL );
    printf( "    %c-d%cX%c    monitor dlls [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.dlls,ATT_NORMAL );
    printf( "    %c-P%cX%c    show process ID and wait [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.pid,ATT_NORMAL );
    printf( "\nheap-observer " HEOB_VER " (" BITS "bit)\n" );
    ExitProcess( -1 );
  }

  STARTUPINFO si = {0};
  PROCESS_INFORMATION pi = {0};
  si.cb = sizeof(STARTUPINFO);
  BOOL result = CreateProcess( NULL,args,NULL,NULL,FALSE,
      CREATE_SUSPENDED|(opt.newConsole?CREATE_NEW_CONSOLE:0),
      NULL,NULL,&si,&pi );
  if( !result )
  {
    printf( "%ccan't create process for '%s'\n%c",ATT_WARN,args,ATT_NORMAL );
    ExitProcess( -1 );
  }

  HANDLE readPipe = NULL;
  char exePath[MAX_PATH];
  if( isWrongArch(pi.hProcess) )
    printf( "%conly " BITS "bit applications possible\n",ATT_WARN );
  else
    readPipe = inject( pi.hProcess,&opt,exePath,tc );
  if( !readPipe )
    TerminateProcess( pi.hProcess,1 );

  UINT exitCode = -1;
  if( readPipe )
  {
#ifndef NO_DBGHELP
    HMODULE symMod = LoadLibrary( "dbghelp.dll" );
#endif
#ifndef NO_DWARFSTACK
    HMODULE dwstMod = LoadLibrary( "dwarfstack.dll" );
    if( !dwstMod )
      dwstMod = LoadLibrary( "dwarfstack" BITS ".dll" );
#endif
#ifndef NO_DBGHELP
    func_SymSetOptions *fSymSetOptions = NULL;
    func_SymInitialize *fSymInitialize = NULL;
    func_SymCleanup *fSymCleanup = NULL;
#endif
    dbghelp dh = {
      pi.hProcess,
#ifndef NO_DBGHELP
      NULL,
      NULL,
#endif
#ifndef NO_DWARFSTACK
      NULL,
#endif
      tc,
      &opt,
    };
#ifndef NO_DBGHELP
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
      dh.fSymFromAddr =
        (func_SymFromAddr*)GetProcAddress( symMod,"SymFromAddr" );
    }
#endif
#ifndef NO_DWARFSTACK
    if( dwstMod )
    {
      dh.fdwstOfFile =
        (func_dwstOfFile*)GetProcAddress( dwstMod,"dwstOfFile" );
    }
#endif
#ifndef NO_DBGHELP
    if( fSymSetOptions )
      fSymSetOptions( SYMOPT_LOAD_LINES );
    if( fSymInitialize )
    {
      char *delim = strrchr( exePath,'\\' );
      if( delim ) delim[0] = 0;
      fSymInitialize( pi.hProcess,exePath,TRUE );
    }
#endif

    if( opt.pid )
    {
      HANDLE in = GetStdHandle( STD_INPUT_HANDLE );
      if( FlushConsoleInputBuffer(in) )
      {
        HANDLE out = tc->out;
        tc->out = GetStdHandle( STD_ERROR_HANDLE );
        printf( "-------------------- PID %u --------------------\n",
            (uintptr_t)pi.dwProcessId );
        tc->out = out;

        INPUT_RECORD ir;
        DWORD didread;
        while( ReadConsoleInput(in,&ir,1,&didread) &&
            (ir.EventType!=KEY_EVENT || !ir.Event.KeyEvent.bKeyDown ||
             ir.Event.KeyEvent.wVirtualKeyCode==VK_SHIFT ||
             ir.Event.KeyEvent.wVirtualKeyCode==VK_CAPITAL ||
             ir.Event.KeyEvent.wVirtualKeyCode==VK_CONTROL ||
             ir.Event.KeyEvent.wVirtualKeyCode==VK_MENU ||
             ir.Event.KeyEvent.wVirtualKeyCode==VK_LWIN ||
             ir.Event.KeyEvent.wVirtualKeyCode==VK_RWIN) );
      }
    }

    ResumeThread( pi.hThread );

    DWORD didread;
    int type;
    modInfo *mi_a = NULL;
    int mi_q = 0;
    allocation *alloc_a = NULL;
    int alloc_q = -2;
    HANDLE heap = GetProcessHeap();
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

        case WRITE_WRONG_DEALLOC:
          {
            allocation aa[2];
            if( !ReadFile(readPipe,aa,2*sizeof(allocation),&didread,NULL) )
              break;

            printf( "%c\nmismatching allocation/release method"
                " of %p (size %u)\n",ATT_WARN,aa[0].ptr,aa[0].size );
            char *allocMethods[] = {
              "malloc",
              "new",
              "new[]",
            };
            printf( "%c  allocated with '%s'\n",
                ATT_INFO,allocMethods[aa[0].at] );
            char *deallocMethods[] = {
              "free",
              "delete",
              "delete[]",
            };
            printf( "%c  freed with '%s'\n",
                ATT_INFO,deallocMethods[aa[1].at] );
            printf( "%c  allocated on:\n",ATT_SECTION );
            printStack( aa[0].frames,mi_a,mi_q,&dh );
            printf( "%c  freed on:\n",ATT_SECTION );
            printStack( aa[1].frames,mi_a,mi_q,&dh );
          }
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
      int combined_q = 0;
      for( i=0; i<alloc_q; i++ )
      {
        allocation a;
        a = alloc_a[i];

        if( !a.ptr ) continue;

        size_t size = a.size;
        a.count = 1;
        int j;
        for( j=i+1; j<alloc_q; j++ )
        {
          if( !alloc_a[j].ptr ||
              a.size!=alloc_a[j].size ||
              memcmp(a.frames,alloc_a[j].frames,PTRS*sizeof(void*)) )
            continue;

          size += alloc_a[j].size;
          alloc_a[j].ptr = NULL;
          a.count++;
        }
        sumSize += size;

        alloc_a[combined_q++] = a;
      }
      if( opt.leakDetails )
      {
        for( i=0; i<combined_q; i++ )
        {
          int best = -1;
          allocation a;

          int j;
          for( j=0; j<combined_q; j++ )
          {
            allocation b = alloc_a[j];
            if( !b.count ) continue;

            int use = 0;
            if( best<0 )
              use = 1;
            else if( b.size>a.size )
              use = 1;
            else if( b.size==a.size )
            {
              int cmp = memcmp( a.frames,b.frames,PTRS*sizeof(void*) );
              if( cmp<0 )
                use = 1;
              else if( cmp==0 && b.count>a.count )
                use = 1;
            }
            if( use )
            {
              best = j;
              a = b;
            }
          }

          alloc_a[best].count = 0;

          printf( "%c  %u B * %d = %u B\n",
              ATT_WARN,a.size,(intptr_t)a.count,a.size*a.count );

          printStack( a.frames,mi_a,mi_q,&dh );
        }
      }
      printf( "%c  sum: %u B / %d\n",ATT_WARN,sumSize,(intptr_t)alloc_q );
      printf( "%cexit code: %u (%x)\n",
          ATT_SECTION,(uintptr_t)exitCode,exitCode );
    }
    else if( alloc_q<-1 )
    {
      printf( "%c\nunexpected end of application\n",ATT_WARN );
    }

#ifndef NO_DBGHELP
    if( fSymCleanup ) fSymCleanup( pi.hProcess );
    if( symMod ) FreeLibrary( symMod );
#endif
#ifndef NO_DWARFSTACK
    if( dwstMod ) FreeLibrary( dwstMod );
#endif
    HeapFree( heap,0,alloc_a );
    HeapFree( heap,0,mi_a );
    CloseHandle( readPipe );
  }
  CloseHandle( pi.hThread );
  CloseHandle( pi.hProcess );

  printf( "%c",ATT_NORMAL );

  ExitProcess( exitCode );
}
