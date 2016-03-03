
//          Copyright Hannes Domani 2014 - 2015.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// includes {{{

#include "heob.h"

#ifndef NO_DWARFSTACK
#include <dwarfstack.h>
#else
#include <stdint.h>
#define DWST_BASE_ADDR   0
#define DWST_NO_DBG_SYM -1
#endif

// }}}
// output variant declarations {{{

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
typedef void func_WriteText( struct textColor*,const char*,size_t );
typedef void func_TextColor( struct textColor*,textColorAtt );

typedef struct textColor
{
  func_WriteText *fWriteText;
  func_TextColor *fTextColor;
  HANDLE out;
  union {
    int colors[ATT_COUNT];
    const char *styles[ATT_COUNT];
  };
  textColorAtt color;
}
textColor;

// }}}
// CRT replacements {{{

static inline char num2hex( unsigned int bits )
{
  bits &= 0xf;
  return( bits>=10 ? bits - 10 + 'A' : bits + '0' );
}

static NOINLINE void mprintf( textColor *tc,const char *format,... )
{
  va_list vl;
  va_start( vl,format );
  const char *ptr = format;
  while( ptr[0] )
  {
    if( ptr[0]=='%' && ptr[1] )
    {
      if( ptr>format )
        tc->fWriteText( tc,format,ptr-format );
      switch( ptr[1] )
      {
        case 's': // const char*
          {
            const char *arg = va_arg( vl,const char* );
            if( arg && arg[0] )
            {
              size_t l = 0;
              while( arg[l] ) l++;
              tc->fWriteText( tc,arg,l );
            }
          }
          break;

        case 'd': // int
        case 'D': // intptr_t
        case 'u': // unsigned int
        case 'U': // uintptr_t
          {
            uintptr_t arg;
            int minus = 0;
            switch( ptr[1] )
            {
              case 'd':
#ifndef _WIN64
              case 'D':
#endif
                {
                  int argi = va_arg( vl,int );
                  if( argi<0 )
                  {
                    arg = -argi;
                    minus = 1;
                  }
                  else
                    arg = argi;
                }
                break;
#ifdef _WIN64
              case 'D':
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
                break;
#endif
              case 'u':
#ifndef _WIN64
              case 'U':
#endif
                arg = va_arg( vl,unsigned int );
                break;
#ifdef _WIN64
              case 'U':
                arg = va_arg( vl,uintptr_t );
                break;
#endif
            }
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
            tc->fWriteText( tc,start,end-start );
          }
          break;

        case 'x': // unsigned int
        case 'p': // uintptr_t
          {
            uintptr_t arg;
            int bytes;
#ifdef _WIN64
            if( ptr[1]=='p' )
            {
              arg = va_arg( vl,uintptr_t );
              bytes = sizeof(uintptr_t);
            }
            else
#endif
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
              end++[0] = num2hex( (unsigned)(arg>>(b*4)) );
            tc->fWriteText( tc,str,end-str );
          }
          break;

        case 'c': // textColorAtt
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
    tc->fWriteText( tc,format,ptr-format );
  va_end( vl );
}
#define printf(...) mprintf(tc,__VA_ARGS__)

static NOINLINE char *mstrchr( const char *s,char c )
{
  for( ; *s; s++ )
    if( *s==c ) return( (char*)s );
  return( NULL );
}
#define strchr mstrchr

static NOINLINE char *mstrrchr( const char *s,char c )
{
  char *ret = NULL;
  for( ; *s; s++ )
    if( *s==c ) ret = (char*)s;
  return( ret );
}
#define strrchr mstrrchr

static NOINLINE uintptr_t atop( const char *s )
{
  uintptr_t ret = 0;

  if( s[0]=='0' && s[1]=='x' )
  {
    s += 2;
    for( ; ; s++ )
    {
      char c = *s;
      int add;
      if( c>='0' && c<='9' )
        add = c - '0';
      else if( c>='a' && c<='f' )
        add = c - 'a' + 10;
      else if( c>='A' && c<='F' )
        add = c - 'A' + 10;
      else break;
      ret = ( ret<<4 ) + add;
    }
    return( ret );
  }

  for( ; *s>='0' && *s<='9'; s++ )
    ret = ret*10 + ( *s - '0' );
  return( ret );
}
static NOINLINE int matoi( const char *s )
{
  return( (int)atop(s) );
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

static const void *mmemchr( const void *p,int ch,size_t s )
{
  const unsigned char *b = p;
  const unsigned char *eob = b + s;
  unsigned char c = ch;
  for( ; b<eob; b++ )
    if( *b==c ) return( b );
  return( NULL );
}
#define memchr mmemchr

// }}}
// output variants {{{

static void WriteText( textColor *tc,const char *t,size_t l )
{
  DWORD written;
  WriteFile( tc->out,t,(DWORD)l,&written,NULL );
}

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

static void WriteTextHtml( textColor *tc,const char *t,size_t l )
{
  const char *end = t + l;
  const char *next;
  char lt[] = "&lt;";
  char gt[] = "&gt;";
  char amp[] = "&amp;";
  DWORD written;
  for( next=t; next<end; next++ )
  {
    char c = next[0];
    if( c!='<' && c!='>' && c!='&' ) continue;

    if( next>t )
      WriteFile( tc->out,t,(DWORD)(next-t),&written,NULL );
    if( c=='<' )
      WriteFile( tc->out,lt,sizeof(lt)-1,&written,NULL );
    else if( c=='>' )
      WriteFile( tc->out,gt,sizeof(gt)-1,&written,NULL );
    else
      WriteFile( tc->out,amp,sizeof(amp)-1,&written,NULL );
    t = next + 1;
  }
  if( next>t )
    WriteFile( tc->out,t,(DWORD)(next-t),&written,NULL );
}

static void TextColorHtml( textColor *tc,textColorAtt color )
{
  DWORD written;
  if( tc->color )
  {
    const char *spanEnd = "</span>";
    WriteFile( tc->out,spanEnd,lstrlen(spanEnd),&written,NULL );
  }
  if( color )
  {
    const char *span1 = "<span class=\"";
    const char *style = tc->styles[color];
    const char *span2 = "\">";
    WriteFile( tc->out,span1,lstrlen(span1),&written,NULL );
    WriteFile( tc->out,style,lstrlen(style),&written,NULL );
    WriteFile( tc->out,span2,lstrlen(span2),&written,NULL );
  }
}

static void checkOutputVariant( textColor *tc,const char *cmdLine,HANDLE out )
{
  // default
  tc->fWriteText = &WriteText;
  tc->fTextColor = NULL;
  tc->out = out;
  tc->color = ATT_NORMAL;

  DWORD flags;
  if( GetConsoleMode(tc->out,&flags) )
  {
    // windows console
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
  typedef LONG NTAPI func_NtQueryObject(
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
      if( (size_t)oni->Name.Length/2>l1+l2 &&
          !memcmp(oni->Name.Buffer,namedPipe,l1*2) &&
          !memcmp(oni->Name.Buffer+(oni->Name.Length/2-l2),toMaster,l2*2) )
      {
        // terminal emulator
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
          (size_t)oni->Name.Length/2>hl &&
          !memcmp(oni->Name.Buffer+(oni->Name.Length/2-hl),html,hl*2) )
      {
        // html file
        const char *styleInit =
          "<head>\n"
          "<style type=\"text/css\">\n"
          "body { color:lightgrey; background-color:black; }\n"
          ".ok { color:lime; }\n"
          ".section { color:turquoise; }\n"
          ".info { color:violet; }\n"
          ".warn { color:red; }\n"
          ".base { color:black; background-color:grey; }\n"
          "</style>\n"
          "<title>heob</title>\n"
          "</head><body>\n"
          "<h3>";
        const char *styleInit2 =
          "</h3>\n"
          "<pre>\n";
        DWORD written;
        WriteFile( tc->out,styleInit,lstrlen(styleInit),&written,NULL );
        WriteTextHtml( tc,cmdLine,lstrlen(cmdLine) );
        WriteFile( tc->out,styleInit2,lstrlen(styleInit2),&written,NULL );

        tc->fWriteText = &WriteTextHtml;
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

// }}}
// compare process bitness {{{

static int isWrongArch( HANDLE process )
{
  BOOL remoteWow64,meWow64;
  IsWow64Process( process,&remoteWow64 );
  IsWow64Process( GetCurrentProcess(),&meWow64 );
  return( remoteWow64!=meWow64 );
}

// }}}
// code injection {{{

#ifdef __MINGW32__
#define CODE_SEG(seg) __attribute__((section(seg)))
#else
#define CODE_SEG(seg) __declspec(code_seg(seg))
#endif

static CODE_SEG(".text$1") DWORD WINAPI remoteCall( remoteData *rd )
{
  HMODULE app = rd->fLoadLibraryW( rd->exePath );
  char inj_name[] = { 'i','n','j',0 };
  DWORD (*func_inj)( remoteData*,void* );
  func_inj = rd->fGetProcAddress( app,inj_name );
  func_inj( rd,app );

  return( 0 );
}

static CODE_SEG(".text$2") HANDLE inject(
    HANDLE process,options *opt,char *exePath,textColor *tc,
    int raise_alloc_q,int *raise_alloc_a )
{
  size_t funcSize = (size_t)&inject - (size_t)&remoteCall;
  size_t fullSize = funcSize + sizeof(remoteData) + raise_alloc_q*sizeof(int);

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
  data->kernel32 = kernel32;
  data->fVirtualProtect =
    (func_VirtualProtect*)GetProcAddress( kernel32,"VirtualProtect" );
  data->fGetCurrentProcess =
    (func_GetCurrentProcess*)GetProcAddress( kernel32,"GetCurrentProcess" );
  data->fFlushInstructionCache =
    (func_FlushInstructionCache*)GetProcAddress(
        kernel32,"FlushInstructionCache" );
  data->fLoadLibraryA =
    (func_LoadLibraryA*)GetProcAddress( kernel32,"LoadLibraryA" );
  data->fLoadLibraryW =
    (func_LoadLibraryW*)GetProcAddress( kernel32,"LoadLibraryW" );
  data->fFreeLibrary =
    (func_FreeLibrary*)GetProcAddress( kernel32,"FreeLibrary" );
  data->fGetProcAddress =
    (func_GetProcAddress*)GetProcAddress( kernel32,"GetProcAddress" );
  data->fSetUnhandledExceptionFilter =
    (func_SetUnhandledExceptionFilter*)GetProcAddress(
        kernel32,"SetUnhandledExceptionFilter" );
  data->fExitProcess =
    (func_ExitProcess*)GetProcAddress( kernel32,"ExitProcess" );

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

  data->raise_alloc_q = raise_alloc_q;
  if( raise_alloc_q )
    RtlMoveMemory( data->raise_alloc_a,
        raise_alloc_a,raise_alloc_q*sizeof(int) );

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
  VirtualFreeEx( process,fullDataRemote,0,MEM_RELEASE );

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

// }}}
// stacktrace {{{

struct dbgsym;
#ifndef NO_DWARFSTACK
typedef int func_dwstOfFile( const char*,uint64_t,uint64_t*,int,
    dwstCallback*,struct dbgsym* );
#endif

#ifdef _WIN64
#define BITS "64"
#else
#define BITS "32"
#endif

typedef struct dbgsym
{
  HANDLE process;
#ifndef NO_DBGHELP
  HMODULE symMod;
  func_SymGetLineFromAddr64 *fSymGetLineFromAddr64;
  func_SymFromAddr *fSymFromAddr;
  func_SymAddrIncludeInlineTrace *fSymAddrIncludeInlineTrace;
  func_SymQueryInlineTrace *fSymQueryInlineTrace;
  func_SymGetLineFromInlineContext *fSymGetLineFromInlineContext;
  func_SymFromInlineContext *fSymFromInlineContext;
  func_SymGetModuleInfo64 *fSymGetModuleInfo64;
  func_SymLoadModule64 *fSymLoadModule64;
  IMAGEHLP_LINE64 *il;
  SYMBOL_INFO *si;
#endif
#ifndef NO_DWARFSTACK
  HMODULE dwstMod;
  func_dwstOfFile *fdwstOfFile;
#endif
  char *absPath;
  textColor *tc;
  options *opt;
  const char **funcnames;
}
dbgsym;

void dbgsym_init( dbgsym *ds,HANDLE process,textColor *tc,options *opt,
    const char **funcnames,HANDLE heap,const char *dbgPath,BOOL invade )
{
  RtlZeroMemory( ds,sizeof(dbgsym) );
  ds->process = process;
  ds->tc = tc;
  ds->opt = opt;
  ds->funcnames = funcnames;

#ifndef NO_DBGHELP
  ds->symMod = LoadLibrary( "dbghelp.dll" );
  if( ds->symMod )
  {
    func_SymSetOptions *fSymSetOptions =
      (func_SymSetOptions*)GetProcAddress( ds->symMod,"SymSetOptions" );
    func_SymInitialize *fSymInitialize =
      (func_SymInitialize*)GetProcAddress( ds->symMod,"SymInitialize" );
    ds->fSymGetLineFromAddr64 =
      (func_SymGetLineFromAddr64*)GetProcAddress(
          ds->symMod,"SymGetLineFromAddr64" );
    ds->fSymFromAddr =
      (func_SymFromAddr*)GetProcAddress( ds->symMod,"SymFromAddr" );
    ds->fSymAddrIncludeInlineTrace =
      (func_SymAddrIncludeInlineTrace*)GetProcAddress(
          ds->symMod,"SymAddrIncludeInlineTrace" );
    ds->fSymQueryInlineTrace =
      (func_SymQueryInlineTrace*)GetProcAddress(
          ds->symMod,"SymQueryInlineTrace" );
    ds->fSymGetLineFromInlineContext =
      (func_SymGetLineFromInlineContext*)GetProcAddress( ds->symMod,
          "SymGetLineFromInlineContext" );
    ds->fSymFromInlineContext =
      (func_SymFromInlineContext*)GetProcAddress(
          ds->symMod,"SymFromInlineContext" );
    if( !ds->fSymQueryInlineTrace || !ds->fSymGetLineFromInlineContext ||
        !ds->fSymFromInlineContext )
      ds->fSymAddrIncludeInlineTrace = NULL;
    ds->fSymGetModuleInfo64 =
      (func_SymGetModuleInfo64*)GetProcAddress(
          ds->symMod,"SymGetModuleInfo64" );
    ds->fSymLoadModule64 =
      (func_SymLoadModule64*)GetProcAddress( ds->symMod,"SymLoadModule64" );
    ds->il = HeapAlloc( heap,0,sizeof(IMAGEHLP_LINE64) );
    ds->si = HeapAlloc( heap,0,sizeof(SYMBOL_INFO)+MAX_SYM_NAME );

    if( fSymSetOptions )
      fSymSetOptions( SYMOPT_LOAD_LINES );
    if( fSymInitialize )
      fSymInitialize( ds->process,dbgPath,invade );
  }
#else
  (void)dbgPath;
  (void)invade;
#endif

#ifndef NO_DWARFSTACK
  ds->dwstMod = LoadLibrary( "dwarfstack.dll" );
  if( !ds->dwstMod )
    ds->dwstMod = LoadLibrary( "dwarfstack" BITS ".dll" );
  if( ds->dwstMod )
  {
    ds->fdwstOfFile =
      (func_dwstOfFile*)GetProcAddress( ds->dwstMod,"dwstOfFile" );
  }
#endif

  ds->absPath = HeapAlloc( heap,0,MAX_PATH );
}

void dbgsym_close( dbgsym *ds,HANDLE heap )
{
#ifndef NO_DBGHELP
  if( ds->symMod )
  {
    func_SymCleanup *fSymCleanup =
      (func_SymCleanup*)GetProcAddress( ds->symMod,"SymCleanup" );
    if( fSymCleanup ) fSymCleanup( ds->process );
    FreeLibrary( ds->symMod );
  }
  if( ds->il ) HeapFree( heap,0,ds->il );
  if( ds->si ) HeapFree( heap,0,ds->si );
#endif

#ifndef NO_DWARFSTACK
  if( ds->dwstMod ) FreeLibrary( ds->dwstMod );
#endif

  if( ds->absPath ) HeapFree( heap,0,ds->absPath );
}

#ifndef _WIN64
#define PTR_SPACES "        "
#else
#define PTR_SPACES "                "
#endif

static void locFunc(
    uint64_t addr,const char *filename,int lineno,const char *funcname,
    void *context )
{
  dbgsym *ds = context;
  textColor *tc = ds->tc;

  uint64_t printAddr = addr;
#ifndef NO_DBGHELP
  if( lineno==DWST_NO_DBG_SYM && ds->fSymGetLineFromAddr64 )
  {
    int inlineTrace;
    if( ds->fSymAddrIncludeInlineTrace &&
        (inlineTrace=ds->fSymAddrIncludeInlineTrace(ds->process,addr)) )
    {
      DWORD inlineContext,frameIndex;
      if( ds->fSymQueryInlineTrace(ds->process,addr,0,addr,addr,
            &inlineContext,&frameIndex) )
      {
        int i;
        DWORD dis;
        DWORD64 dis64;
        IMAGEHLP_LINE64 *il = ds->il;
        SYMBOL_INFO *si = ds->si;
        for( i=0; i<inlineTrace; i++ )
        {
          RtlZeroMemory( il,sizeof(IMAGEHLP_LINE64) );
          il->SizeOfStruct = sizeof(IMAGEHLP_LINE64);
          if( ds->fSymGetLineFromInlineContext(
                ds->process,addr,inlineContext,0,&dis,il) )
          {
            si->SizeOfStruct = sizeof(SYMBOL_INFO);
            si->MaxNameLen = MAX_SYM_NAME;
            if( ds->fSymFromInlineContext(
                  ds->process,addr,inlineContext,&dis64,si) )
            {
              si->Name[MAX_SYM_NAME] = 0;
              locFunc( printAddr,il->FileName,il->LineNumber,si->Name,ds );
              printAddr = 0;
            }
          }
          inlineContext++;
        }
      }
    }

    IMAGEHLP_LINE64 *il = ds->il;
    RtlZeroMemory( il,sizeof(IMAGEHLP_LINE64) );
    il->SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD dis;
    if( ds->fSymGetLineFromAddr64(ds->process,addr,&dis,il) )
    {
      filename = il->FileName;
      lineno = il->LineNumber;
    }

    if( ds->fSymFromAddr )
    {
      SYMBOL_INFO *si = ds->si;
      DWORD64 dis64;
      si->SizeOfStruct = sizeof(SYMBOL_INFO);
      si->MaxNameLen = MAX_SYM_NAME;
      if( ds->fSymFromAddr(ds->process,addr,&dis64,si) )
      {
        si->Name[MAX_SYM_NAME] = 0;
        funcname = si->Name;
      }
    }
  }
#endif

  if( ds->opt->fullPath || ds->opt->sourceCode )
  {
    if( !GetFullPathNameA(filename,MAX_PATH,ds->absPath,NULL) )
      ds->absPath[0] = 0;
  }

  if( !ds->opt->fullPath )
  {
    const char *sep1 = strrchr( filename,'/' );
    const char *sep2 = strrchr( filename,'\\' );
    if( sep2>sep1 ) sep1 = sep2;
    if( sep1 ) filename = sep1 + 1;
  }
  else
  {
    if( ds->absPath[0] )
      filename = ds->absPath;
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
      if( printAddr )
        printf( "%c      %p",ATT_NORMAL,(uintptr_t)printAddr );
      else
        printf( "        " PTR_SPACES );
      printf( "   %c%s%c:%d",
          ATT_OK,filename,ATT_NORMAL,lineno );
      if( funcname )
        printf( " [%c%s%c]",ATT_INFO,funcname,ATT_NORMAL );
      printf( "\n" );

      if( ds->opt->sourceCode && ds->absPath[0] )
      {
        HANDLE file = CreateFile( ds->absPath,GENERIC_READ,FILE_SHARE_READ,
            NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0 );
        if( file!=INVALID_HANDLE_VALUE )
        {
          BY_HANDLE_FILE_INFORMATION fileInfo;
          if( GetFileInformationByHandle(file,&fileInfo) &&
              fileInfo.nFileSizeLow && !fileInfo.nFileSizeHigh )
          {
            HANDLE mapping = CreateFileMapping(
                file,NULL,PAGE_READONLY,0,0,NULL );
            if( mapping )
            {
              const char *map = MapViewOfFile( mapping,FILE_MAP_READ,0,0,0 );
              if( map )
              {
                const char *bol = map;
                const char *eof = map + fileInfo.nFileSizeLow;
                int firstLine = lineno + 1 - ds->opt->sourceCode;
                if( firstLine<1 ) firstLine = 1;
                int lastLine = lineno - 1 + ds->opt->sourceCode;
                if( firstLine>1 )
                  printf( "\t...\n" );
                int i;
                for( i=1; i<=lastLine; i++ )
                {
                  const char *eol = memchr( bol,'\n',eof-bol );
                  if( !eol ) eol = eof;
                  else eol++;

                  if( i>=firstLine )
                  {
                    if( i==lineno ) printf( "%c>",ATT_SECTION );
                    printf( "\t" );
                    tc->fWriteText( tc,bol,eol-bol );
                    if( i==lineno ) printf( "%c",ATT_NORMAL );
                  }

                  bol = eol;
                  if( bol==eof ) break;
                }
                if( bol>map && bol[-1]!='\n' )
                  printf( "\n" );
                if( bol!=eof )
                  printf( "\t...\n\n" );
                else
                  printf( "\n" );

                UnmapViewOfFile( map );
              }

              CloseHandle( mapping );
            }
          }

          CloseHandle( file );
        }
      }
      break;
  }
}

static void printStackCount( uint64_t *frames,int fc,
    modInfo *mi_a,int mi_q,dbgsym *ds,funcType ft )
{
  if( ft<FT_COUNT )
  {
    textColor *tc = ds->tc;
    printf( "%c        " PTR_SPACES "   [%c%s%c]\n",
        ATT_NORMAL,ATT_INFO,ds->funcnames[ft],ATT_NORMAL );
  }

  int j;
  for( j=0; j<fc; j++ )
  {
    int k;
    uint64_t frame = frames[j];
    for( k=0; k<mi_q && (frame<mi_a[k].base ||
          frame>=mi_a[k].base+mi_a[k].size); k++ );
    if( k>=mi_q )
    {
      locFunc( frame,"?",DWST_BASE_ADDR,NULL,ds );
      continue;
    }
    modInfo *mi = mi_a + k;

    int l;
    for( l=j+1; l<fc && frames[l]>=mi->base &&
        frames[l]<mi->base+mi->size; l++ );

#ifndef NO_DWARFSTACK
    if( ds->fdwstOfFile )
      ds->fdwstOfFile( mi->path,mi->base,frames+j,l-j,locFunc,ds );
    else
#endif
    {
      locFunc( mi->base,mi->path,DWST_BASE_ADDR,NULL,ds );
      int i;
      for( i=j; i<l; i++ )
        locFunc( frames[i],mi->path,DWST_NO_DBG_SYM,NULL,ds );
    }

    j = l - 1;
  }
}

static void printStack( void **framesV,modInfo *mi_a,int mi_q,dbgsym *ds,
    funcType ft )
{
  uint64_t frames[PTRS];
  int j;
  for( j=0; j<PTRS; j++ )
  {
    if( !framesV[j] ) break;
    frames[j] = ((uintptr_t)framesV[j]) - 1;
  }
  printStackCount( frames,j,mi_a,mi_q,ds,ft );
}

// }}}
// read all requested data from pipe {{{

static int readFile( HANDLE file,void *destV,size_t count )
{
  char *dest = destV;
  while( count>0 )
  {
    DWORD didread;
    DWORD readcount = count>0x10000000 ? 0x10000000 : (DWORD)count;
    if( !ReadFile(file,dest,readcount,&didread,NULL) ) return( 0 );
    dest += didread;
    count -= didread;
  }
  return( 1 );
}

// }}}
// main {{{

void mainCRTStartup( void )
{
  textColor tc_o;
  textColor *tc = &tc_o;

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
    0,
    0,
    0,
    1,
    1,
    0,
    1,
  };
  options opt = defopt;
  HANDLE heap = GetProcessHeap();
  int raise_alloc_q = 0;
  int *raise_alloc_a = NULL;
  HANDLE out = NULL;
  modInfo *a2l_mi_a = NULL;
  int a2l_mi_q = 0;
  char badArg = 0;
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

      case 'e':
        opt.exitTrace = atoi( args+2 );
        break;

      case 'C':
        opt.sourceCode = atoi( args+2 );
        break;

      case 'r':
        opt.raiseException = atoi( args+2 );
        break;

      case 'M':
        opt.minProtectSize = atoi( args+2 );
        if( opt.minProtectSize<1 ) opt.minProtectSize = 1;
        break;

      case 'n':
        opt.findNearest = atoi( args+2 );
        break;

      case 'L':
        opt.leakContents = atoi( args+2 );
        break;

      case 'I':
        opt.mergeLeaks = atoi( args+2 );
        break;

      case 'R':
        {
          int id = atoi( args+2 );
          if( !id ) break;
          int i;
          for( i=0; i<raise_alloc_q && raise_alloc_a[i]<id; i++ );
          if( i<raise_alloc_q && raise_alloc_a[i]==id ) break;
          raise_alloc_q++;
          if( !raise_alloc_a )
            raise_alloc_a = HeapAlloc( heap,0,raise_alloc_q*sizeof(int) );
          else
            raise_alloc_a = HeapReAlloc(
                heap,0,raise_alloc_a,raise_alloc_q*sizeof(int) );
          if( i<raise_alloc_q-1 )
            RtlMoveMemory( raise_alloc_a+i+1,raise_alloc_a+i,
                (raise_alloc_q-1-i)*sizeof(int) );
          raise_alloc_a[i] = id;
        }
        break;

      case 'o':
        if( out ) break;
        if( (args[2]=='0' || args[2]=='1') && (args[3]==' ' || !args[3]) )
          out = GetStdHandle(
              args[2]=='0' ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
        else
        {
          char *start = args + 2;
          char *end = start;
          while( *end && *end!=' ' ) end++;
          if( end>start )
          {
            size_t len = end - start;
            char *name = HeapAlloc( heap,0,len+1 );
            RtlMoveMemory( name,start,len );
            name[len] = 0;
            out = CreateFile( name,GENERIC_WRITE,FILE_SHARE_READ,
                NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
            HeapFree( heap,0,name );
          }
        }
        break;

      case '"':
        {
          char *start = args + 2;
          char *end = start;
          while( *end && *end!='"' ) end++;
          if( !*end || end<=start ) break;
          uintptr_t base = atop( end+1 );
          if( !base ) break;
          a2l_mi_q++;
          if( !a2l_mi_a )
            a2l_mi_a = HeapAlloc( heap,0,a2l_mi_q*sizeof(modInfo) );
          else
            a2l_mi_a = HeapReAlloc(
                heap,0,a2l_mi_a,a2l_mi_q*sizeof(modInfo) );
          modInfo *mi = a2l_mi_a + ( a2l_mi_q-1 );
          mi->base = base;
          mi->size = 0;
          size_t len = end - start;
          char localName[MAX_PATH];
          RtlMoveMemory( localName,start,len );
          localName[len] = 0;
          if( !SearchPath(NULL,localName,NULL,MAX_PATH,mi->path,NULL) )
            RtlMoveMemory( mi->path,localName,len+1 );
        }
        break;

      default:
        badArg = args[1];
        args = NULL;
        break;
    }
    while( args && args[0] && args[0]!=' ' ) args++;
  }
  if( !out || out==INVALID_HANDLE_VALUE )
    out = GetStdHandle( STD_OUTPUT_HANDLE );
  if( opt.protect<1 ) opt.protectFree = 0;
  checkOutputVariant( tc,cmdLine,out );

  if( badArg )
  {
    char arg0[2] = { badArg,0 };
    printf( "%cunknown argument: %c-%s\n%c",
        ATT_WARN,ATT_INFO,arg0,ATT_NORMAL );

    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    if( a2l_mi_a ) HeapFree( heap,0,a2l_mi_a );
    ExitProcess( -1 );
  }

  const char *funcnames[FT_COUNT] = {
    "malloc",
    "calloc",
    "free",
    "realloc",
    "strdup",
    "wcsdup",
    "operator new",
    "operator delete",
    "operator new[]",
    "operator delete[]",
    "getcwd",
    "wgetcwd",
    "getdcwd",
    "wgetdcwd",
    "fullpath",
    "wfullpath",
    "tempnam",
    "wtempnam",
  };

  if( a2l_mi_a )
  {
    uint64_t *ptr_a = NULL;
    int ptr_q = 0;

    while( args && args[0]>='0' && args[0]<='9' )
    {
      uintptr_t ptr = atop( args );
      if( ptr )
      {
        ptr_q++;
        if( !ptr_a )
          ptr_a = HeapAlloc( heap,0,ptr_q*sizeof(uint64_t) );
        else
          ptr_a = HeapReAlloc(
              heap,0,ptr_a,ptr_q*sizeof(uint64_t) );
        ptr_a[ptr_q-1] = ptr;

        int i;
        int idx = -1;
        for( i=0; i<a2l_mi_q; i++ )
        {
          if( ptr>=a2l_mi_a[i].base &&
              (idx<0 || a2l_mi_a[idx].base<a2l_mi_a[i].base) )
            idx = i;
        }
        if( idx>=0 && ptr>=a2l_mi_a[idx].base+a2l_mi_a[idx].size )
          a2l_mi_a[idx].size = ptr - a2l_mi_a[idx].base + 1;
      }
      while( args[0] && args[0]!=' ' ) args++;
      while( args[0]==' ' ) args++;
    }

    if( ptr_q )
    {
      dbgsym ds;
      dbgsym_init( &ds,(HANDLE)0x1,tc,&opt,funcnames,heap,NULL,FALSE );

#ifndef NO_DBGHELP
      if( ds.fSymLoadModule64 )
      {
        int i;
        for( i=0; i<a2l_mi_q; i++ )
          ds.fSymLoadModule64( ds.process,NULL,a2l_mi_a[i].path,NULL,
              a2l_mi_a[i].base,(DWORD)a2l_mi_a[i].size );
      }
#endif

      printf( "%c\ntrace:\n",ATT_SECTION );
      printStackCount( ptr_a,ptr_q,a2l_mi_a,a2l_mi_q,&ds,FT_COUNT );
      printf( "%c",ATT_NORMAL );

      dbgsym_close( &ds,heap );
    }

    if( ptr_a ) HeapFree( heap,0,ptr_a );
    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    raise_alloc_a = NULL;
    HeapFree( heap,0,a2l_mi_a );

    if( ptr_q ) ExitProcess( 0 );
    args = NULL;
  }

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
    printf( "    %c-o%cX%c    heob output"
        " (%c0%c = stdout, %c1%c = stderr, %c...%c = file) [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,ATT_NORMAL,ATT_INFO,ATT_NORMAL,ATT_INFO,ATT_NORMAL,
        ATT_INFO,0,ATT_NORMAL );
    printf( "    %c-P%cX%c    show process ID and wait [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.pid,ATT_NORMAL );
    printf( "    %c-c%cX%c    create new console [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.newConsole,ATT_NORMAL );
    printf( "    %c-p%cX%c    page protection"
        " (%c0%c = off, %c1%c = after, %c2%c = before) [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,ATT_NORMAL,ATT_INFO,ATT_NORMAL,ATT_INFO,ATT_NORMAL,
        ATT_INFO,defopt.protect,ATT_NORMAL );
    printf( "    %c-f%cX%c    freed memory protection [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.protectFree,ATT_NORMAL );
    printf( "    %c-d%cX%c    monitor dlls [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.dlls,ATT_NORMAL );
    printf( "    %c-a%cX%c    alignment [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.align,ATT_NORMAL );
    printf( "    %c-M%cX%c    minimum page protection size [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,defopt.minProtectSize,ATT_NORMAL );
    printf( "    %c-i%cX%c    initial value [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.init,ATT_NORMAL );
    printf( "    %c-s%cX%c    initial value for slack [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.slackInit,ATT_NORMAL );
    printf( "    %c-h%cX%c    handle exceptions [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,defopt.handleException,ATT_NORMAL );
    printf( "    %c-R%cX%c    "
        "raise breakpoint exception on allocation # [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,0,ATT_NORMAL );
    printf( "    %c-r%cX%c    raise breakpoint exception on error [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,defopt.raiseException,ATT_NORMAL );
    printf( "    %c-S%cX%c    use stack pointer in exception [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.useSp,ATT_NORMAL );
    printf( "    %c-m%cX%c    compare allocation/release method [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.allocMethod,ATT_NORMAL );
    printf( "    %c-n%cX%c    find nearest allocation [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,defopt.findNearest,ATT_NORMAL );
    printf( "    %c-I%cX%c    merge identical leaks [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.mergeLeaks,ATT_NORMAL );
    printf( "    %c-F%cX%c    show full path [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.fullPath,ATT_NORMAL );
    printf( "    %c-l%cX%c    show leak details [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.leakDetails,ATT_NORMAL );
    printf( "    %c-L%cX%c    show leak contents [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.leakContents,ATT_NORMAL );
    printf( "    %c-C%cX%c    show source code [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.sourceCode,ATT_NORMAL );
    printf( "    %c-e%cX%c    show exit trace [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.exitTrace,ATT_NORMAL );
    printf( "\nheap-observer " HEOB_VER " (" BITS "bit)\n" );
    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    ExitProcess( -1 );
  }

  STARTUPINFO si;
  PROCESS_INFORMATION pi;
  RtlZeroMemory( &si,sizeof(STARTUPINFO) );
  RtlZeroMemory( &pi,sizeof(PROCESS_INFORMATION) );
  si.cb = sizeof(STARTUPINFO);
  BOOL result = CreateProcess( NULL,args,NULL,NULL,FALSE,
      CREATE_SUSPENDED|(opt.newConsole?CREATE_NEW_CONSOLE:0),
      NULL,NULL,&si,&pi );
  if( !result )
  {
    printf( "%ccan't create process for '%s'\n%c",ATT_WARN,args,ATT_NORMAL );
    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    ExitProcess( -1 );
  }

  HANDLE readPipe = NULL;
  char exePath[MAX_PATH];
  if( isWrongArch(pi.hProcess) )
    printf( "%conly " BITS "bit applications possible\n",ATT_WARN );
  else
    readPipe = inject( pi.hProcess,&opt,exePath,tc,
        raise_alloc_q,raise_alloc_a );
  if( !readPipe )
    TerminateProcess( pi.hProcess,1 );

  UINT exitCode = -1;
  if( readPipe )
  {
    char *delim = strrchr( exePath,'\\' );
    if( delim ) delim[0] = 0;
    dbgsym ds;
    dbgsym_init( &ds,pi.hProcess,tc,&opt,funcnames,heap,exePath,TRUE );

    if( opt.pid )
    {
      HANDLE in = GetStdHandle( STD_INPUT_HANDLE );
      if( FlushConsoleInputBuffer(in) )
      {
        tc->out = GetStdHandle( STD_ERROR_HANDLE );
        printf( "-------------------- PID %u --------------------\n",
            pi.dwProcessId );
        printf( "press any key to continue..." );

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

        printf( " done\n\n" );
        tc->out = out;
      }
    }

    ResumeThread( pi.hThread );

    int type;
    modInfo *mi_a = NULL;
    int mi_q = 0;
    allocation *alloc_a = NULL;
    int alloc_q = -2;
    int content_q = 0;
    unsigned char *contents = NULL;
    unsigned char **content_ptrs = NULL;
    while( readFile(readPipe,&type,sizeof(int)) )
    {
      switch( type )
      {
#if WRITE_DEBUG_STRINGS
        case WRITE_STRING:
          {
            char buf[1024];
            char *bufpos = buf;
            while( readFile(readPipe,bufpos,1) )
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
          if( !readFile(readPipe,&exitCode,sizeof(UINT)) )
          {
            alloc_q = -2;
            break;
          }
          if( !readFile(readPipe,&alloc_q,sizeof(int)) )
          {
            alloc_q = -2;
            break;
          }
          if( !alloc_q ) break;
          if( alloc_a ) HeapFree( heap,0,alloc_a );
          alloc_a = HeapAlloc( heap,0,alloc_q*sizeof(allocation) );
          if( !readFile(readPipe,alloc_a,alloc_q*sizeof(allocation)) )
          {
            alloc_q = -2;
            break;
          }
          break;

        case WRITE_MODS:
          if( !readFile(readPipe,&mi_q,sizeof(int)) )
            mi_q = 0;
          if( !mi_q ) break;
          if( mi_a ) HeapFree( heap,0,mi_a );
          mi_a = HeapAlloc( heap,0,mi_q*sizeof(modInfo) );
          if( !readFile(readPipe,mi_a,mi_q*sizeof(modInfo)) )
          {
            mi_q = 0;
            break;
          }
#ifndef NO_DBGHELP
          if( ds.fSymGetModuleInfo64 && ds.fSymLoadModule64 )
          {
            int m;
            IMAGEHLP_MODULE64 im;
            im.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
            for( m=0; m<mi_q; m++ )
            {
              if( !ds.fSymGetModuleInfo64(ds.process,mi_a[m].base,&im) )
                ds.fSymLoadModule64( ds.process,NULL,mi_a[m].path,NULL,
                    mi_a[m].base,0 );
            }
          }
#endif
          break;

        case WRITE_EXCEPTION:
          {
            exceptionInfo ei;
            if( !readFile(readPipe,&ei,sizeof(exceptionInfo)) )
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
            printStack( ei.aa[0].frames,mi_a,mi_q,&ds,FT_COUNT );

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
                printf( "%c  %s%s %p (size %U, offset %s%D)\n",
                    ATT_INFO,ei.nearest?"near ":"",
                    ei.aq>2?"freed block":"protected area of",
                    ptr,size,addr>ptr?"+":"",addr-ptr );
                printf( "%c  allocated on: %c(#%d)\n",ATT_SECTION,
                    ATT_NORMAL,ei.aa[1].id );
                printStack( ei.aa[1].frames,mi_a,mi_q,&ds,ei.aa[1].ft );

                if( ei.aq>2 )
                {
                  printf( "%c  freed on:\n",ATT_SECTION );
                  printStack( ei.aa[2].frames,mi_a,mi_q,&ds,ei.aa[2].ft );
                }
              }
            }

            alloc_q = -1;
          }
          break;

        case WRITE_ALLOC_FAIL:
          {
            allocation a;
            if( !readFile(readPipe,&a,sizeof(allocation)) )
              break;

            printf( "%c\nallocation failed of %U bytes\n",
                ATT_WARN,a.size );
            printf( "%c  called on: %c(#%d)\n",ATT_SECTION,
                ATT_NORMAL,a.id );
            printStack( a.frames,mi_a,mi_q,&ds,a.ft );
          }
          break;

        case WRITE_FREE_FAIL:
          {
            allocation a;
            if( !readFile(readPipe,&a,sizeof(allocation)) )
              break;

            printf( "%c\ndeallocation of invalid pointer %p\n",
                ATT_WARN,a.ptr );
            printf( "%c  called on:\n",ATT_SECTION );
            printStack( a.frames,mi_a,mi_q,&ds,a.ft );
          }
          break;

        case WRITE_DOUBLE_FREE:
          {
            allocation aa[3];
            if( !readFile(readPipe,aa,3*sizeof(allocation)) )
              break;

            printf( "%c\ndouble free of %p (size %U)\n",
                ATT_WARN,aa[1].ptr,aa[1].size );
            printf( "%c  called on:\n",ATT_SECTION );
            printStack( aa[0].frames,mi_a,mi_q,&ds,aa[0].ft );

            printf( "%c  allocated on: %c(#%d)\n",ATT_SECTION,
                ATT_NORMAL,aa[1].id );
            printStack( aa[1].frames,mi_a,mi_q,&ds,aa[1].ft );

            printf( "%c  freed on:\n",ATT_SECTION );
            printStack( aa[2].frames,mi_a,mi_q,&ds,aa[2].ft );
          }
          break;

        case WRITE_SLACK:
          {
            allocation aa[2];
            if( !readFile(readPipe,aa,2*sizeof(allocation)) )
              break;

            printf( "%c\nwrite access violation at %p\n",
                ATT_WARN,aa[1].ptr );
            printf( "%c  slack area of %p (size %U, offset %s%D)\n",
                ATT_INFO,aa[0].ptr,aa[0].size,
                aa[1].ptr>aa[0].ptr?"+":"",(char*)aa[1].ptr-(char*)aa[0].ptr );
            printf( "%c  allocated on: %c(#%d)\n",ATT_SECTION,
                ATT_NORMAL,aa[0].id );
            printStack( aa[0].frames,mi_a,mi_q,&ds,aa[0].ft );
            printf( "%c  freed on:\n",ATT_SECTION );
            printStack( aa[1].frames,mi_a,mi_q,&ds,aa[1].ft );
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
            if( !readFile(readPipe,aa,2*sizeof(allocation)) )
              break;

            printf( "%c\nmismatching allocation/release method"
                " of %p (size %U)\n",ATT_WARN,aa[0].ptr,aa[0].size );
            printf( "%c  allocated on: %c(#%d)\n",ATT_SECTION,
                ATT_NORMAL,aa[0].id );
            printStack( aa[0].frames,mi_a,mi_q,&ds,aa[0].ft );
            printf( "%c  freed on:\n",ATT_SECTION );
            printStack( aa[1].frames,mi_a,mi_q,&ds,aa[1].ft );
          }
          break;

        case WRITE_LEAK_CONTENTS:
          {
            if( !readFile(readPipe,&content_q,sizeof(int)) ||
                content_q>alloc_q  )
              break;
            size_t content_size;
            if( !readFile(readPipe,&content_size,sizeof(size_t)) )
              break;
            contents = HeapAlloc( heap,0,content_size );
            if( !readFile(readPipe,contents,content_size) )
              break;
            content_ptrs =
              HeapAlloc( heap,0,content_q*sizeof(unsigned char*) );
            int lc;
            size_t leakContents = opt.leakContents;
            size_t content_pos = 0;
            int lDetails = opt.leakDetails ?
              ( opt.leakDetails&1 ? LT_COUNT : LT_REACHABLE ) : 0;
            for( lc=0; lc<content_q; lc++ )
            {
              content_ptrs[lc] = contents + content_pos;
              allocation *a = alloc_a + lc;
              if( a->lt>=lDetails ) continue;
              size_t s = a->size;
              content_pos += s<leakContents ? s : leakContents;
            }
          }
          break;

        case WRITE_RAISE_ALLOCATION:
          {
            int id;
            if( !readFile(readPipe,&id,sizeof(int)) )
              break;
            funcType ft;
            if( !readFile(readPipe,&ft,sizeof(funcType)) )
              break;

            printf( "%c\nreached allocation #%d %c[%c%s%c]\n",
                ATT_SECTION,id,ATT_NORMAL,
                ATT_INFO,funcnames[ft],ATT_NORMAL );
          }
          break;
      }
    }

    allocation exitTrace;
    exitTrace.at = AT_MALLOC;
    if( alloc_q>0 && alloc_a[alloc_q-1].at==AT_EXIT )
    {
      alloc_q--;
      exitTrace = alloc_a[alloc_q];
    }

    if( content_ptrs && content_q!=alloc_q )
    {
      HeapFree( heap,0,content_ptrs );
      content_q = 0;
      content_ptrs = NULL;
    }

    if( !alloc_q )
    {
      printf( "%c\n",ATT_OK );

      if( opt.handleException<2 )
        printf( "no leaks found\n" );

      if( exitTrace.at==AT_EXIT )
      {
        printf( "%cexit on:\n",ATT_SECTION );
        printStack( exitTrace.frames,mi_a,mi_q,&ds,FT_COUNT );
      }
      printf( "%cexit code: %u (%x)\n",
          ATT_SECTION,exitCode,exitCode );
    }
    else if( alloc_q>0 )
    {
      int i;
      size_t sumSize = 0;
      int combined_q = 0;
      for( i=0; i<alloc_q; i++ )
      {
        allocation a;
        a = alloc_a[i];

        if( !a.ptr ) continue;

        size_t size = a.size;
        int content_idx = i;
        a.count = 1;
        if( opt.mergeLeaks )
        {
          int j;
          for( j=i+1; j<alloc_q; j++ )
          {
            if( !alloc_a[j].ptr ||
                a.size!=alloc_a[j].size ||
                a.lt!=alloc_a[j].lt ||
                a.ft!=alloc_a[j].ft ||
                memcmp(a.frames,alloc_a[j].frames,PTRS*sizeof(void*)) )
              continue;

            size += alloc_a[j].size;
            alloc_a[j].ptr = NULL;
            a.count++;
            if( alloc_a[j].id<a.id )
            {
              a.id = alloc_a[j].id;
              content_idx = j;
            }
          }
        }
        sumSize += size;

        if( content_ptrs && content_idx!=combined_q )
          content_ptrs[combined_q] = content_ptrs[content_idx];
        alloc_a[combined_q++] = a;
      }
      if( opt.leakDetails<=1 )
        printf( "%c\nleaks:\n",ATT_SECTION );
      else
        printf( "\n" );
      int l;
      int lMax = opt.leakDetails ? LT_COUNT : 0;
      int lDetails = opt.leakDetails&1 ? LT_COUNT : LT_REACHABLE;
      for( l=0; l<lMax; l++ )
      {
        int ltCount = 0;
        size_t ltSumSize = 0;
        for( i=0; i<combined_q; i++ )
        {
          int best = -1;
          allocation a;

          int j;
          for( j=0; j<combined_q; j++ )
          {
            allocation b = alloc_a[j];
            if( !b.count || b.lt!=l ) continue;

            int use = 0;
            if( best<0 )
              use = 1;
            else if( b.size>a.size )
              use = 1;
            else if( b.size==a.size )
            {
              if( b.ft<a.ft )
                use = 1;
              else if( b.ft==a.ft )
              {
                if( b.id<a.id )
                  use = 1;
              }
            }
            if( use )
            {
              best = j;
              a = b;
            }
          }
          if( best<0 ) break;

          alloc_a[best].count = 0;

          if( opt.leakDetails>1 && !i )
            printf( "%cleaks (%s):\n",ATT_SECTION,
                l==LT_LOST?"lost":l==LT_JOINTLY_LOST?"jointly lost":
                l==LT_INDIRECTLY_LOST?"indirectly lost":
                l==LT_REACHABLE?"reachable":"indirectly reachable" );

          ltCount += a.count;
          ltSumSize += a.size*a.count;

          if( l<lDetails )
          {
            printf( "%c  %U B * %d = %U B %c(#%d)\n",
                ATT_WARN,a.size,a.count,a.size*a.count,
                ATT_NORMAL,a.id );

            printStack( a.frames,mi_a,mi_q,&ds,a.ft );

            if( content_ptrs && a.size )
            {
              int s = a.size<(size_t)opt.leakContents ?
                (int)a.size : opt.leakContents;
              char text[5] = { 0,0,0,0,0 };
              unsigned char *content = content_ptrs[best];
              int lines = ( s+15 )/16;
              int lc;
              for( lc=0; lc<lines; lc++,s-=16 )
              {
                unsigned char *line = content + lc*16;
                int line_size = s>16 ? 16 : s;
                text[0] = num2hex( lc>>8 );
                text[1] = num2hex( lc>>4 );
                text[2] = num2hex( lc );
                text[3] = '0';
                printf( "        %c%s%c ",ATT_INFO,text,ATT_NORMAL );
                int p;
                text[2] = 0;
                for( p=0; p<line_size; p++ )
                {
                  unsigned char c = line[p];
                  text[0] = num2hex( c>>4 );
                  text[1] = num2hex( c );
                  printf( " %s",text );
                }
                for( ; p<16; p++ )
                  printf( "   " );
                printf( "  %c",ATT_SECTION );
                text[1] = 0;
                for( p=0; p<line_size; p++ )
                {
                  unsigned char c = line[p];
                  text[0] = c>=0x20 && c<=0x7e ? c : '.';
                  printf( "%s",text );
                }
                printf( "\n" );
              }
            }
          }
        }
        if( opt.leakDetails>1 && ltCount )
          printf( "%c  sum: %U B / %d\n",ATT_WARN,ltSumSize,ltCount );
      }
      if( opt.leakDetails<=1 )
        printf( "%c  sum: %U B / %d\n",ATT_WARN,sumSize,alloc_q );

      if( exitTrace.at==AT_EXIT )
      {
        printf( "%cexit on:\n",ATT_SECTION );
        printStack( exitTrace.frames,mi_a,mi_q,&ds,FT_COUNT );
      }
      printf( "%cexit code: %u (%x)\n",
          ATT_SECTION,exitCode,exitCode );
    }
    else if( alloc_q<-1 )
    {
      printf( "%c\nunexpected end of application\n",ATT_WARN );
    }

    dbgsym_close( &ds,heap );
    if( alloc_a ) HeapFree( heap,0,alloc_a );
    if( mi_a ) HeapFree( heap,0,mi_a );
    if( contents ) HeapFree( heap,0,contents );
    if( content_ptrs ) HeapFree( heap,0,content_ptrs );
    CloseHandle( readPipe );
  }
  CloseHandle( pi.hThread );
  CloseHandle( pi.hProcess );

  if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );

  printf( "%c",ATT_NORMAL );

  ExitProcess( exitCode );
}

// }}}

// vim:fdm=marker
