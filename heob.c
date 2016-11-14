
//          Copyright Hannes Domani 2014 - 2016.
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

static inline char *num2hexstr( char *str,uintptr_t arg,int count )
{
  int b;
  for( b=count-1; b>=0; b-- )
    str++[0] = num2hex( (unsigned)(arg>>(b*4)) );
  return( str );
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
              case 'D':
                {
                  intptr_t argi;
#ifdef _WIN64
                  if( ptr[1]=='D' )
                    argi = va_arg( vl,intptr_t );
                  else
#endif
                    argi = va_arg( vl,int );
                  if( argi<0 )
                  {
                    arg = -argi;
                    minus = 1;
                  }
                  else
                    arg = argi;
                }
                break;
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
        case 'X': // uintptr_t
        case 'p': // void*
          {
            uintptr_t arg;
            int bytes;
            if( ptr[1]=='p' )
            {
              arg = (uintptr_t)va_arg( vl,void* );
              bytes = sizeof(void*);
            }
#ifdef _WIN64
            else if( ptr[1]=='X' )
            {
              arg = va_arg( vl,uintptr_t );
              bytes = sizeof(uintptr_t);
            }
#endif
            else
            {
              arg = va_arg( vl,unsigned int );
              bytes = sizeof(unsigned int);
            }
            char str[20];
            char *end = str;
            end++[0] = '0';
            end++[0] = 'x';
            end = num2hexstr( end,arg,bytes*2 );
            tc->fWriteText( tc,str,end-str );
          }
          break;
      }
      ptr += 2;
      format = ptr;
      continue;
    }
    else if( ptr[0]=='$' && ptr[1] )
    {
      if( ptr>format )
        tc->fWriteText( tc,format,ptr-format );
      if( tc->fTextColor )
      {
        textColorAtt color = ATT_NORMAL;
        switch( ptr[1] )
        {
          case 'O': color = ATT_OK;      break;
          case 'S': color = ATT_SECTION; break;
          case 'I': color = ATT_INFO;    break;
          case 'W': color = ATT_WARN;    break;
          case 'B': color = ATT_BASE;    break;
        }
        if( tc->color!=color )
        {
          tc->fTextColor( tc,color );
          tc->color = color;
        }
      }
      ptr += 2;
      format = ptr;
      continue;
    }
    else if( ptr[0]=='\n' && tc->fTextColor && tc->color!=ATT_NORMAL )
    {
      if( ptr>format )
        tc->fWriteText( tc,format,ptr-format );
      tc->fTextColor( tc,ATT_NORMAL );
      tc->color = ATT_NORMAL;
      format = ptr;
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

static NOINLINE uint64_t atou64( const char *s )
{
  uint64_t ret = 0;

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
static inline uintptr_t atop( const char *s )
{
  return( (uintptr_t)atou64(s) );
}
static inline int matoi( const char *s )
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
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( tc->out,&csbi );
    int bg = csbi.wAttributes&0xf0;

    tc->fTextColor = &TextColorConsole;

    tc->colors[ATT_NORMAL]  = csbi.wAttributes&0xff;
    tc->colors[ATT_OK]      = bg|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
    tc->colors[ATT_SECTION] =
      bg|FOREGROUND_BLUE|FOREGROUND_GREEN|FOREGROUND_INTENSITY;
    tc->colors[ATT_INFO]    =
      bg|FOREGROUND_BLUE|FOREGROUND_RED|FOREGROUND_INTENSITY;
    tc->colors[ATT_WARN]    = bg|FOREGROUND_RED|FOREGROUND_INTENSITY;
    tc->colors[ATT_BASE]    = ( bg^BACKGROUND_INTENSITY )|( bg>>4 );

    int i;
    bg = bg | ( bg>>4 );
    for( i=0; i<ATT_COUNT; i++ )
      if( tc->colors[i]==bg ) tc->colors[i] ^=0x08;
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

typedef void func_inj( remoteData*,HMODULE );

static CODE_SEG(".text$1") DWORD WINAPI remoteCall( remoteData *rd )
{
  HMODULE app = rd->fLoadLibraryW( rd->exePath );
  func_inj *finj = (func_inj*)( (size_t)app + rd->injOffset );
  finj( rd,app );

  UNREACHABLE;

  return( 0 );
}

static CODE_SEG(".text$2") HANDLE inject(
    HANDLE process,options *opt,char *exePath,textColor *tc,
    int raise_alloc_q,size_t *raise_alloc_a,HANDLE *controlPipe )
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
  func_inj *finj = &inj;
  data->injOffset = (size_t)finj - (size_t)GetModuleHandle( NULL );

  char pipeName[32] = "\\\\.\\Pipe\\heob.data.";
  char *end = num2hexstr( pipeName+lstrlen(pipeName),GetCurrentProcessId(),8 );
  end[0] = 0;
  HANDLE readPipe = CreateNamedPipe( pipeName,
      PIPE_ACCESS_INBOUND|FILE_FLAG_OVERLAPPED,PIPE_TYPE_BYTE,
      1,1024,1024,0,NULL );
  HANDLE writePipe = CreateFile( pipeName,
      GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL );
  DuplicateHandle( GetCurrentProcess(),writePipe,
      process,&data->master,0,FALSE,
      DUPLICATE_CLOSE_SOURCE|DUPLICATE_SAME_ACCESS );

  if( opt->leakRecording )
  {
    HANDLE controlReadPipe;
    CreatePipe( &controlReadPipe,controlPipe,NULL,0 );
    DuplicateHandle( GetCurrentProcess(),controlReadPipe,
        process,&data->controlPipe,0,FALSE,
        DUPLICATE_CLOSE_SOURCE|DUPLICATE_SAME_ACCESS );
  }
  data->recording = opt->leakRecording!=1;

  HANDLE initFinished = CreateEvent( NULL,FALSE,FALSE,NULL );
  DuplicateHandle( GetCurrentProcess(),initFinished,
      process,&data->initFinished,0,FALSE,
      DUPLICATE_SAME_ACCESS );

  RtlMoveMemory( &data->opt,opt,sizeof(options) );

  data->raise_alloc_q = raise_alloc_q;
  if( raise_alloc_q )
    RtlMoveMemory( data->raise_alloc_a,
        raise_alloc_a,raise_alloc_q*sizeof(size_t) );

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
    printf( "$Wprocess failed to initialize\n" );
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
    printf( "$Wonly works with dynamically linked CRT\n" );
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
typedef size_t func_dwstDemangle( const char*,char*,size_t );
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
  func_UnDecorateSymbolName *fUnDecorateSymbolName;
  IMAGEHLP_LINE64 *il;
  SYMBOL_INFO *si;
  char *undname;
#endif
#ifndef NO_DWARFSTACK
  HMODULE dwstMod;
  func_dwstOfFile *fdwstOfFile;
  func_dwstDemangle *fdwstDemangle;
#endif
  char *absPath;
  textColor *tc;
  options *opt;
  const char **funcnames;
  uintptr_t threadInitAddr;
  HANDLE heap;
}
dbgsym;

void dbgsym_init( dbgsym *ds,HANDLE process,textColor *tc,options *opt,
    const char **funcnames,HANDLE heap,const char *dbgPath,BOOL invade,
    void *threadInitAddr )
{
  RtlZeroMemory( ds,sizeof(dbgsym) );
  ds->process = process;
  ds->tc = tc;
  ds->opt = opt;
  ds->funcnames = funcnames;
  ds->threadInitAddr = (uintptr_t)threadInitAddr;
  ds->heap = heap;

#ifndef NO_DBGHELP
  ds->symMod = LoadLibrary( "dbghelp" BITS ".dll" );
  if( !ds->symMod )
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
    ds->fUnDecorateSymbolName =
      (func_UnDecorateSymbolName*)GetProcAddress(
          ds->symMod,"UnDecorateSymbolName" );
    ds->il = HeapAlloc( heap,0,sizeof(IMAGEHLP_LINE64) );
    ds->si = HeapAlloc( heap,0,sizeof(SYMBOL_INFO)+MAX_SYM_NAME );
    ds->undname = HeapAlloc( heap,0,MAX_SYM_NAME+1 );

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
  ds->dwstMod = LoadLibrary( "dwarfstack" BITS ".dll" );
  if( !ds->dwstMod )
    ds->dwstMod = LoadLibrary( "dwarfstack.dll" );
  if( ds->dwstMod )
  {
    ds->fdwstOfFile =
      (func_dwstOfFile*)GetProcAddress( ds->dwstMod,"dwstOfFile" );
    ds->fdwstDemangle =
      (func_dwstDemangle*)GetProcAddress( ds->dwstMod,"dwstDemangle" );
  }
#endif

  ds->absPath = HeapAlloc( heap,0,MAX_PATH );
}

void dbgsym_close( dbgsym *ds )
{
  HANDLE heap = ds->heap;

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
  if( ds->undname ) HeapFree( heap,0,ds->undname );
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
    void *context,int columnno )
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
              locFunc( printAddr,il->FileName,il->LineNumber,si->Name,ds,0 );
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

        if( lineno==DWST_NO_DBG_SYM )
        {
          if( si->Name[0]=='?' && ds->fUnDecorateSymbolName &&
              ds->fUnDecorateSymbolName(si->Name,
                ds->undname,MAX_SYM_NAME,UNDNAME_NAME_ONLY) )
          {
            ds->undname[MAX_SYM_NAME] = 0;
            funcname = ds->undname;
          }
#ifndef NO_DWARFSTACK
          else if( si->Name[0]=='_' && si->Name[1]=='Z' && ds->fdwstDemangle &&
              ds->fdwstDemangle(si->Name,ds->undname,MAX_SYM_NAME) )
          {
            ds->undname[MAX_SYM_NAME] = 0;
            funcname = ds->undname;
          }
#endif
        }
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
      printf( "    $B%X$N   $B%s\n",(uintptr_t)addr,filename );
      break;

    case DWST_NO_DBG_SYM:
#ifndef NO_DWARFSTACK
    case DWST_NO_SRC_FILE:
    case DWST_NOT_FOUND:
#endif
      printf( "      %X",(uintptr_t)addr );
      if( funcname )
        printf( "   [$I%s$N]",funcname );
      printf( "\n" );
      break;

    default:
      if( printAddr )
        printf( "      %X",(uintptr_t)printAddr );
      else
        printf( "        " PTR_SPACES );
      printf( "   $O%s$N:%d",filename,lineno );
      if( columnno>0 )
        printf( ":$S%d$N",columnno );
      if( funcname )
        printf( " [$I%s$N]",funcname );
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
                if( columnno>0 ) columnno--;
                for( i=1; i<=lastLine; i++ )
                {
                  const char *eol = memchr( bol,'\n',eof-bol );
                  if( !eol ) eol = eof;
                  else eol++;

                  if( i>=firstLine )
                  {
                    if( i==lineno )
                    {
                      printf( "$S>\t" );
                      if( columnno>0 && columnno<eol-bol )
                      {
                        printf( "$N" );
                        tc->fWriteText( tc,bol,columnno );
                        bol += columnno;
                        printf( "$S" );
                      }
                      tc->fWriteText( tc,bol,eol-bol );
                      printf( "$N" );
                    }
                    else
                    {
                      printf( "\t" );
                      tc->fWriteText( tc,bol,eol-bol );
                    }
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
    printf( "        " PTR_SPACES "   [$I%s$N]\n",ds->funcnames[ft] );
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
      locFunc( frame,"?",DWST_BASE_ADDR,NULL,ds,0 );
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
      locFunc( mi->base,mi->path,DWST_BASE_ADDR,NULL,ds,0 );
      int i;
      for( i=j; i<l; i++ )
        locFunc( frames[i],mi->path,DWST_NO_DBG_SYM,NULL,ds,0 );
    }

    j = l - 1;
  }
}

static void printStack( void **framesV,modInfo *mi_a,int mi_q,dbgsym *ds,
    funcType ft )
{
  uint64_t frames[PTRS];
  int j;
  uintptr_t threadInitAddr = ds->threadInitAddr;
  for( j=0; j<PTRS; j++ )
  {
    if( !framesV[j] ) break;
    uintptr_t frame = (uintptr_t)framesV[j];
    if( frame==threadInitAddr ) break;
    frames[j] = frame - 1;
  }
  printStackCount( frames,j,mi_a,mi_q,ds,ft );
}

// }}}
// thread name {{{

#ifndef NO_THREADNAMES
void printThreadName( int threadNameIdx,
    textColor *tc,int threadName_q,threadNameInfo *threadName_a )
{
  if( threadNameIdx>0 && threadNameIdx<=threadName_q )
    printf( " $S\"%s\"\n",threadName_a[threadNameIdx-1].name );
  else if( threadNameIdx<-1 )
  {
    unsigned unnamedIdx = -threadNameIdx;
    printf( " $S'%u'\n",unnamedIdx );
  }
  else
    printf( "\n" );
}
#define printThreadName(tni) printThreadName(tni,tc,threadName_q,threadName_a)
#else
#define printThreadName(tni) printf("\n")
#endif

// }}}
// read all requested data from pipe {{{

static int readFile( HANDLE file,void *destV,size_t count,OVERLAPPED *ov )
{
  char *dest = destV;
  while( count>0 )
  {
    DWORD didread;
    DWORD readcount = count>0x10000000 ? 0x10000000 : (DWORD)count;
    if( !ReadFile(file,dest,readcount,NULL,ov) &&
        GetLastError()!=ERROR_IO_PENDING )
      return( 0 );
    if( !GetOverlappedResult(file,ov,&didread,TRUE) )
      return( 0 );
    dest += didread;
    count -= didread;
  }
  return( 1 );
}

// }}}
// leak sorting {{{

static inline void swap_idxs( int *a,int *b )
{
  int t = *a;
  *a = *b;
  *b = t;
}

static void sort_allocations( allocation *a,int *idxs,int num,
    int (*compar)(const allocation*,const allocation*) )
{
  int c,r;
  int i = num/2 - 1;

  for( ; i>=0; i-- )
  {
    for( r=i; r*2+1<num; r=c )
    {
      c = r*2 + 1;
      if( c<num-1 && compar(a+idxs[c],a+idxs[c+1])<0 )
        c++;
      if( compar(a+idxs[r],a+idxs[c])>=0 )
        break;
      swap_idxs( idxs+r,idxs+c );
    }
  }

  for( i=num-1; i>0; i-- )
  {
    swap_idxs( idxs,idxs+i );
    for( r=0; r*2+1<i; r=c )
    {
      c = r*2 + 1;
      if( c<i-1 && compar(a+idxs[c],a+idxs[c+1])<0 )
        c++;
      if( compar(a+idxs[r],a+idxs[c])>=0 )
        break;
      swap_idxs( idxs+r,idxs+c );
    }
  }
}

static int cmp_merge_allocation( const allocation *a,const allocation *b )
{
  if( a->lt>b->lt ) return( 2 );
  if( a->lt<b->lt ) return( -2 );

  if( a->size>b->size ) return( -2 );
  if( a->size<b->size ) return( 2 );

  if( a->ft>b->ft ) return( 2 );
  if( a->ft<b->ft ) return( -2 );

  int c = memcmp( a->frames,b->frames,PTRS*sizeof(void*) );
  if( c ) return( c>0 ? 2 : -2 );

  return( a->id>b->id ? 1 : -1 );
}

static int cmp_type_allocation( const allocation *a,const allocation *b )
{
  if( a->lt>b->lt ) return( 2 );
  if( a->lt<b->lt ) return( -2 );

  if( a->size>b->size ) return( -2 );
  if( a->size<b->size ) return( 2 );

  if( a->ft>b->ft ) return( 2 );
  if( a->ft<b->ft ) return( -2 );

  return( a->id>b->id ? 1 : -1 );
}

// }}}
// leak recording status {{{

static void showRecording( HANDLE err,int recording,
    COORD *consoleCoord,int *errColorP )
{
  DWORD didwrite;
  WriteFile( err,"\n",1,&didwrite,NULL );

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo( err,&csbi );
  *consoleCoord = csbi.dwCursorPosition;
  int errColor = *errColorP = csbi.wAttributes&0xff;

  const char *recText = "leak recording:  on   off   clear   show ";
  WriteFile( err,recText,16,&didwrite,NULL );
  if( recording>0 )
  {
    SetConsoleTextAttribute( err,errColor^BACKGROUND_INTENSITY );
    WriteFile( err,recText+16,4,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+20,3,&didwrite,NULL );
    SetConsoleTextAttribute( err,
        errColor^(FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY) );
    WriteFile( err,recText+23,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+24,2,&didwrite,NULL );
  }
  else
  {
    WriteFile( err,recText+16,2,&didwrite,NULL );
    SetConsoleTextAttribute( err,
        errColor^(FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY) );
    WriteFile( err,recText+18,3,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^BACKGROUND_INTENSITY );
    WriteFile( err,recText+21,5,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
  }
  WriteFile( err,recText+26,1,&didwrite,NULL );
  if( recording>=0 )
  {
    WriteFile( err,recText+27,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,
        errColor^(FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY) );
    WriteFile( err,recText+28,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+29,7,&didwrite,NULL );
    SetConsoleTextAttribute( err,
        errColor^(FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY) );
    WriteFile( err,recText+36,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+37,4,&didwrite,NULL );
  }
  else
  {
    SetConsoleTextAttribute( err,errColor^BACKGROUND_INTENSITY );
    WriteFile( err,recText+27,7,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+34,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^BACKGROUND_INTENSITY );
    WriteFile( err,recText+35,6,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
  }
}

static void clearRecording( HANDLE err,
    COORD consoleCoord,int errColor,int clearAll )
{
  COORD moveCoord = { consoleCoord.X,consoleCoord.Y-1 };
  SetConsoleCursorPosition( err,moveCoord );

  if( !clearAll ) return;

  DWORD didwrite;
  int recTextLen = 41;
  FillConsoleOutputAttribute( err,errColor,recTextLen,consoleCoord,&didwrite );
  FillConsoleOutputCharacter( err,' ',recTextLen,consoleCoord,&didwrite );
}

// }}}
// leak data {{{

static void printLeaks( allocation *alloc_a,int alloc_q,
    unsigned char **content_ptrs,modInfo *mi_a,int mi_q,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    options *opt,textColor *tc,dbgsym *ds,HANDLE heap )
{
  if( opt->handleException>=2 )
  {
    printf( "\n" );
    return;
  }

  if( alloc_q>0 && alloc_a[alloc_q-1].at==AT_EXIT )
    alloc_q--;

  if( !alloc_q )
  {
    printf( "\n$Ono leaks found\n" );
    return;
  }

  int i;
  size_t sumSize = 0;
  int combined_q = alloc_q;
  int *alloc_idxs =
    opt->leakDetails ? HeapAlloc( heap,0,alloc_q*sizeof(int) ) : NULL;
  for( i=0; i<alloc_q; i++ )
  {
    sumSize += alloc_a[i].size;
    alloc_a[i].count = 1;
    if( alloc_idxs )
      alloc_idxs[i] = i;
  }
  if( opt->mergeLeaks && opt->leakDetails )
  {
    sort_allocations( alloc_a,alloc_idxs,alloc_q,cmp_merge_allocation );
    combined_q = 0;
    for( i=0; i<alloc_q; )
    {
      allocation a;
      int idx = alloc_idxs[i];
      a = alloc_a[idx];
      int j;
      for( j=i+1; j<alloc_q; j++ )
      {
        int c = cmp_merge_allocation( &a,alloc_a+alloc_idxs[j] );
        if( c<-1 || c>1 ) break;

        a.count++;
      }

      alloc_a[idx] = a;
      alloc_idxs[combined_q++] = idx;
      i = j;
    }
  }
  if( opt->leakDetails<=1 )
    printf( "\n$Sleaks:\n" );
  else
    printf( "\n" );
  int l;
  int lMax = opt->leakDetails ? LT_COUNT : 0;
  int lDetails = ( opt->leakDetails&1 ) ? LT_COUNT : LT_REACHABLE;
  if( opt->leakDetails )
  {
    for( i=0; i<combined_q; i++ )
      alloc_a[alloc_idxs[i]].size *= alloc_a[alloc_idxs[i]].count;
    sort_allocations( alloc_a,alloc_idxs,combined_q,cmp_type_allocation );
    for( i=0; i<combined_q; i++ )
      alloc_a[alloc_idxs[i]].size /= alloc_a[alloc_idxs[i]].count;
  }
  for( l=0,i=0; l<lMax; l++ )
  {
    int ltCount = 0;
    size_t ltSumSize = 0;
    for( ; i<combined_q; i++ )
    {
      allocation a;
      a = alloc_a[alloc_idxs[i]];
      if( a.lt<l ) continue;
      if( a.lt>l ) break;

      if( opt->leakDetails>1 && !ltCount )
        printf( "$Sleaks (%s):\n",
            l==LT_LOST?"lost":l==LT_JOINTLY_LOST?"jointly lost":
            l==LT_INDIRECTLY_LOST?"indirectly lost":
            l==LT_REACHABLE?"reachable":"indirectly reachable" );

      ltCount += a.count;
      size_t combSize = a.size*a.count;
      ltSumSize += combSize;

      if( l<lDetails && combSize>=opt->minLeakSize )
      {
        printf( "$W  %U B ",a.size );
        if( a.count>1 )
          printf( "* %d = %U B ",a.count,combSize );
        printf( "$N(#%U)",a.id );
        printThreadName( a.threadNameIdx );
        printStack( a.frames,mi_a,mi_q,ds,a.ft );

        if( content_ptrs && a.size )
        {
          int s = a.size<(size_t)opt->leakContents ?
            (int)a.size : opt->leakContents;
          char text[5] = { 0,0,0,0,0 };
          unsigned char *content = content_ptrs[alloc_idxs[i]];
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
            printf( "        $I%s$N ",text );
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
            printf( "  $S" );
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
    if( opt->leakDetails>1 && ltCount )
      printf( "$W  sum: %U B / %d\n",ltSumSize,ltCount );
  }
  if( opt->leakDetails<=1 )
    printf( "$W  sum: %U B / %d\n",sumSize,alloc_q );
  if( alloc_idxs )
    HeapFree( heap,0,alloc_idxs );
}

// }}}
// main {{{

void mainCRTStartup( void )
{
  textColor tc_o;
  textColor *tc = &tc_o;

  // command line arguments {{{
  char *cmdLine = GetCommandLineA();
  char *args;
  if( cmdLine[0]=='"' && (args=strchr(cmdLine+1,'"')) )
    args++;
  else
    args = strchr( cmdLine,' ' );
  options defopt = {
    1,
    MEMORY_ALLOCATION_ALIGNMENT,
    0xffffffffffffffffULL,
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
    0,
    0,
  };
  options opt = defopt;
  HANDLE heap = GetProcessHeap();
  int raise_alloc_q = 0;
  size_t *raise_alloc_a = NULL;
  HANDLE out = NULL;
  modInfo *a2l_mi_a = NULL;
  int a2l_mi_q = 0;
  int fullhelp = 0;
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
        {
          int align = atoi( args+2 );
          if( align>0 && !(align&(align-1)) )
            opt.align = align;
        }
        break;

      case 'i':
        {
          const char *pos = args + 2;
          uint64_t init = atou64( pos );
          int initSize = 1;
          while( *pos && *pos!=':' ) pos++;
          if( *pos )
            initSize = atoi( pos+1 );
          if( initSize<2 )
            init = init | ( init<<8 );
          if( initSize<4 )
            init = init | ( init<<16 );
          if( initSize<8 )
            init = init | ( init<<32 );
          opt.init = init;
        }
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
          size_t id = atop( args+2 );
          if( !id ) break;
          int i;
          for( i=0; i<raise_alloc_q && raise_alloc_a[i]<id; i++ );
          if( i<raise_alloc_q && raise_alloc_a[i]==id ) break;
          raise_alloc_q++;
          if( !raise_alloc_a )
            raise_alloc_a = HeapAlloc( heap,0,raise_alloc_q*sizeof(size_t) );
          else
            raise_alloc_a = HeapReAlloc(
                heap,0,raise_alloc_a,raise_alloc_q*sizeof(size_t) );
          if( i<raise_alloc_q-1 )
            RtlMoveMemory( raise_alloc_a+i+1,raise_alloc_a+i,
                (raise_alloc_q-1-i)*sizeof(size_t) );
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

      case 'z':
        opt.minLeakSize = atop( args+2 );
        break;

      case 'k':
        opt.leakRecording = atoi( args+2 );
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

          args = end;
        }
        break;

      case 'H':
        fullhelp = 1;
        args = NULL;
        break;

      default:
        badArg = args[1];
        args = NULL;
        break;
    }
    while( args && args[0] && args[0]!=' ' ) args++;
  }
  if( opt.align<MEMORY_ALLOCATION_ALIGNMENT ) opt.init = 0;
  if( !out || out==INVALID_HANDLE_VALUE )
    out = GetStdHandle( STD_OUTPUT_HANDLE );
  if( opt.protect<1 ) opt.protectFree = 0;
  checkOutputVariant( tc,cmdLine,out );

  if( badArg )
  {
    char arg0[2] = { badArg,0 };
    printf( "$Wunknown argument: $I-%s\n",arg0 );

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
    "free_dbg",
    "recalloc",
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
      dbgsym_init( &ds,(HANDLE)0x1,tc,&opt,NULL,heap,NULL,FALSE,NULL );

#ifndef NO_DBGHELP
      if( ds.fSymLoadModule64 )
      {
        int i;
        for( i=0; i<a2l_mi_q; i++ )
          ds.fSymLoadModule64( ds.process,NULL,a2l_mi_a[i].path,NULL,
              a2l_mi_a[i].base,(DWORD)a2l_mi_a[i].size );
      }
#endif

      printf( "\n$Strace:\n" );
      printStackCount( ptr_a,ptr_q,a2l_mi_a,a2l_mi_q,&ds,FT_COUNT );

      dbgsym_close( &ds );
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

    printf( "Usage: $O%s $I[OPTION]... $SAPP [APP-OPTION]...\n",
        delim );
    printf( "    $I-o$BX$N    heob output"
        " ($I0$N = stdout, $I1$N = stderr, $I...$N = file) [$I%d$N]\n",
        0 );
    printf( "    $I-P$BX$N    show process ID and wait [$I%d$N]\n",
        defopt.pid );
    printf( "    $I-c$BX$N    create new console [$I%d$N]\n",
        defopt.newConsole );
    printf( "    $I-p$BX$N    page protection"
        " ($I0$N = off, $I1$N = after, $I2$N = before) [$I%d$N]\n",
        defopt.protect );
    printf( "    $I-f$BX$N    freed memory protection [$I%d$N]\n",
        defopt.protectFree );
    printf( "    $I-d$BX$N    monitor dlls [$I%d$N]\n",
        defopt.dlls );
    if( fullhelp )
    {
      printf( "    $I-a$BX$N    alignment [$I%d$N]\n",
          defopt.align );
      printf( "    $I-M$BX$N    minimum page protection size [$I%d$N]\n",
          defopt.minProtectSize );
      printf( "    $I-i$BX$N    initial value [$I%d$N]\n",
          (int)(defopt.init&0xff) );
      printf( "    $I-s$BX$N    initial value for slack [$I%d$N]\n",
          defopt.slackInit );
    }
    printf( "    $I-h$BX$N    handle exceptions [$I%d$N]\n",
        defopt.handleException );
    printf( "    $I-R$BX$N    "
        "raise breakpoint exception on allocation # [$I%d$N]\n",
        0 );
    printf( "    $I-r$BX$N    raise breakpoint exception on error [$I%d$N]\n",
        defopt.raiseException );
    if( fullhelp )
    {
      printf( "    $I-S$BX$N    use stack pointer in exception [$I%d$N]\n",
          defopt.useSp );
      printf( "    $I-m$BX$N    compare allocation/release method [$I%d$N]\n",
          defopt.allocMethod );
      printf( "    $I-n$BX$N    find nearest allocation [$I%d$N]\n",
          defopt.findNearest );
      printf( "    $I-I$BX$N    merge identical leaks [$I%d$N]\n",
          defopt.mergeLeaks );
    }
    printf( "    $I-F$BX$N    show full path [$I%d$N]\n",
        defopt.fullPath );
    printf( "    $I-l$BX$N    show leak details [$I%d$N]\n",
        defopt.leakDetails );
    printf( "    $I-z$BX$N    minimum leak size [$I%U$N]\n",
        defopt.minLeakSize );
    printf( "    $I-k$BX$N    control leak recording [$I%d$N]\n",
        defopt.leakRecording );
    printf( "    $I-L$BX$N    show leak contents [$I%d$N]\n",
        defopt.leakContents );
    if( fullhelp )
    {
      printf( "    $I-C$BX$N    show source code [$I%d$N]\n",
          defopt.sourceCode );
      printf( "    $I-e$BX$N    show exit trace [$I%d$N]\n",
          defopt.exitTrace );
      printf( "    $I-\"$BM$I\"$BB$N  trace mode:"
          " load $Im$Nodule on $Ib$Nase address\n" );
    }
    printf( "    $I-H$N     show full help\n" );
    printf( "\n$Ohe$Nap-$Oob$Nserver " HEOB_VER " ($O" BITS "$Nbit)\n" );
    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    ExitProcess( -1 );
  }
  // }}}

  HANDLE in = NULL;
  if( opt.pid || opt.leakRecording )
  {
    in = GetStdHandle( STD_INPUT_HANDLE );
    if( !FlushConsoleInputBuffer(in) ) in = NULL;
    if( !in ) opt.pid = opt.leakRecording = 0;
  }

  if( opt.leakRecording )
    opt.newConsole = 1;

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
    printf( "$Wcan't create process for '%s'\n",args );
    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    ExitProcess( -1 );
  }

  if( opt.leakRecording )
  {
    DWORD flags;
    if( GetConsoleMode(in,&flags) )
      SetConsoleMode( in,flags & ~ENABLE_MOUSE_INPUT );
  }

  HANDLE readPipe = NULL;
  HANDLE controlPipe = NULL;
  char exePath[MAX_PATH];
  if( isWrongArch(pi.hProcess) )
    printf( "$Wonly " BITS "bit applications possible\n" );
  else
    readPipe = inject( pi.hProcess,&opt,exePath,tc,
        raise_alloc_q,raise_alloc_a,&controlPipe );
  if( !readPipe )
    TerminateProcess( pi.hProcess,1 );

  UINT exitCode = -1;
  if( readPipe )
  {
    char *delim = strrchr( exePath,'\\' );
    if( delim ) delim[0] = 0;
    dbgsym ds;
    dbgsym_init( &ds,pi.hProcess,tc,&opt,funcnames,heap,exePath,TRUE,
        RETURN_ADDRESS() );

    HANDLE err = GetStdHandle( STD_ERROR_HANDLE );
    if( opt.pid )
    {
      tc->out = err;
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

    ResumeThread( pi.hThread );

    // main loop {{{
    int type;
    modInfo *mi_a = NULL;
    int mi_q = 0;
    allocation *alloc_a = NULL;
    int alloc_q = 0;
    int terminated = -2;
    unsigned char *contents = NULL;
    unsigned char **content_ptrs = NULL;
#ifndef NO_THREADNAMES
    int threadName_q = 0;
    threadNameInfo *threadName_a = NULL;
#endif
    if( !opt.leakRecording ) in = NULL;
    int recording = opt.leakRecording!=1 ? 1 : -1;
    int needData = 1;
    OVERLAPPED ov;
    ov.Offset = ov.OffsetHigh = 0;
    ov.hEvent = CreateEvent( NULL,TRUE,FALSE,NULL );
    HANDLE handles[2] = { ov.hEvent,in };
    int waitCount = in ? 2 : 1;
    int errColor = 0;
    COORD consoleCoord = { 0,0 };
    while( 1 )
    {
      if( needData )
      {
        if( !ReadFile(readPipe,&type,sizeof(int),NULL,&ov) &&
            GetLastError()!=ERROR_IO_PENDING ) break;
        needData = 0;

        if( in )
          showRecording( err,recording,&consoleCoord,&errColor );
      }

      DWORD didread;
      if( WaitForMultipleObjects(waitCount,handles,
            FALSE,INFINITE)==WAIT_OBJECT_0+1 )
      {
        INPUT_RECORD ir;
        if( ReadConsoleInput(in,&ir,1,&didread) &&
            ir.EventType==KEY_EVENT &&
            ir.Event.KeyEvent.bKeyDown )
        {
          int sendType = -1;

          switch( ir.Event.KeyEvent.wVirtualKeyCode )
          {
            case 'N':
              if( recording>0 ) break;
              recording = 1;
              sendType = LEAK_RECORDING_START;
              break;

            case 'F':
              if( recording<=0 ) break;
              recording = 0;
              sendType = LEAK_RECORDING_STOP;
              break;

            case 'C':
              if( recording<0 ) break;
              if( !recording ) recording = -1;
              sendType = LEAK_RECORDING_CLEAR;
              break;

            case 'S':
              if( recording<0 ) break;
              if( !recording ) recording = -1;
              sendType = LEAK_RECORDING_SHOW;
              break;
          }

          if( sendType>=0 )
          {
            WriteFile( controlPipe,&sendType,sizeof(int),&didread,NULL );

            clearRecording( err,consoleCoord,errColor,0 );
            showRecording( err,recording,&consoleCoord,&errColor );
          }
        }
        continue;
      }

      if( in )
        clearRecording( err,consoleCoord,errColor,1 );

      if( !GetOverlappedResult(readPipe,&ov,&didread,TRUE) ||
          didread<sizeof(int) )
        break;
      needData = 1;

      switch( type )
      {
#if WRITE_DEBUG_STRINGS
        case WRITE_STRING:
          {
            char buf[1024];
            char *bufpos = buf;
            while( readFile(readPipe,bufpos,1,&ov) )
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
          {
            alloc_q = 0;
            if( alloc_a ) HeapFree( heap,0,alloc_a );
            alloc_a = NULL;
            if( contents ) HeapFree( heap,0,contents );
            contents = NULL;
            if( content_ptrs ) HeapFree( heap,0,content_ptrs );
            content_ptrs = NULL;

            if( !readFile(readPipe,&alloc_q,sizeof(int),&ov) )
              break;
            if( alloc_q )
            {
              alloc_a = HeapAlloc( heap,0,alloc_q*sizeof(allocation) );
              if( !readFile(readPipe,alloc_a,alloc_q*sizeof(allocation),&ov) )
                break;
            }

            size_t content_size;
            if( !readFile(readPipe,&content_size,sizeof(size_t),&ov) )
              break;
            if( content_size )
            {
              contents = HeapAlloc( heap,0,content_size );
              if( !readFile(readPipe,contents,content_size,&ov) )
                break;
              content_ptrs =
                HeapAlloc( heap,0,alloc_q*sizeof(unsigned char*) );
              int lc;
              size_t leakContents = opt.leakContents;
              size_t content_pos = 0;
              int lDetails = opt.leakDetails ?
                ( (opt.leakDetails&1) ? LT_COUNT : LT_REACHABLE ) : 0;
              for( lc=0; lc<alloc_q; lc++ )
              {
                content_ptrs[lc] = contents + content_pos;
                allocation *a = alloc_a + lc;
                if( a->lt>=lDetails ) continue;
                size_t s = a->size;
                content_pos += s<leakContents ? s : leakContents;
              }
            }
          }

          printLeaks( alloc_a,alloc_q,content_ptrs,mi_a,mi_q,
#ifndef NO_THREADNAMES
              threadName_a,threadName_q,
#endif
              &opt,tc,&ds,heap );
          break;

        case WRITE_MODS:
          if( !readFile(readPipe,&mi_q,sizeof(int),&ov) )
            mi_q = 0;
          if( !mi_q ) break;
          if( mi_a ) HeapFree( heap,0,mi_a );
          mi_a = HeapAlloc( heap,0,mi_q*sizeof(modInfo) );
          if( !readFile(readPipe,mi_a,mi_q*sizeof(modInfo),&ov) )
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
            if( !readFile(readPipe,&ei,sizeof(exceptionInfo),&ov) )
              break;

            const char *desc = NULL;
            switch( ei.er.ExceptionCode )
            {
#define EXCEPTION_FATAL_APP_EXIT STATUS_FATAL_APP_EXIT
#define EX_DESC( name ) \
              case EXCEPTION_##name: desc = " (" #name ")"; break

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
              EX_DESC( FATAL_APP_EXIT );
            }
            printf( "\n$Wunhandled exception code: %x%s\n",
                ei.er.ExceptionCode,desc );

            printf( "$S  exception on:" );
            printThreadName( ei.aa[0].threadNameIdx );
            printStack( ei.aa[0].frames,mi_a,mi_q,&ds,FT_COUNT );

            if( ei.er.ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
                ei.er.NumberParameters==2 )
            {
              ULONG_PTR flag = ei.er.ExceptionInformation[0];
              char *addr = (char*)ei.er.ExceptionInformation[1];
              printf( "$W  %s violation at %p\n",
                  flag==8?"data execution prevention":
                  (flag?"write access":"read access"),addr );

              if( ei.aq>1 )
              {
                char *ptr = (char*)ei.aa[1].ptr;
                size_t size = ei.aa[1].size;
                printf( "$I  %s%s %p (size %U, offset %s%D)\n",
                    ei.nearest?"near ":"",
                    ei.aq>2?"freed block":"protected area of",
                    ptr,size,addr>ptr?"+":"",addr-ptr );
                printf( "$S  allocated on: $N(#%U)",ei.aa[1].id );
                printThreadName( ei.aa[1].threadNameIdx );
                printStack( ei.aa[1].frames,mi_a,mi_q,&ds,ei.aa[1].ft );

                if( ei.aq>2 )
                {
                  printf( "$S  freed on:" );
                  printThreadName( ei.aa[2].threadNameIdx );
                  printStack( ei.aa[2].frames,mi_a,mi_q,&ds,ei.aa[2].ft );
                }
              }
            }

            terminated = -1;
          }
          break;

        case WRITE_ALLOC_FAIL:
          {
            allocation a;
            if( !readFile(readPipe,&a,sizeof(allocation),&ov) )
              break;

            printf( "\n$Wallocation failed of %U bytes\n",a.size );
            printf( "$S  called on: $N(#%U)",a.id );
            printThreadName( a.threadNameIdx );
            printStack( a.frames,mi_a,mi_q,&ds,a.ft );
          }
          break;

        case WRITE_FREE_FAIL:
          {
            allocation a;
            if( !readFile(readPipe,&a,sizeof(allocation),&ov) )
              break;

            printf( "\n$Wdeallocation of invalid pointer %p\n",a.ptr );
            printf( "$S  called on:" );
            printThreadName( a.threadNameIdx );
            printStack( a.frames,mi_a,mi_q,&ds,a.ft );
          }
          break;

        case WRITE_DOUBLE_FREE:
          {
            allocation aa[3];
            if( !readFile(readPipe,aa,3*sizeof(allocation),&ov) )
              break;

            printf( "\n$Wdouble free of %p (size %U)\n",aa[1].ptr,aa[1].size );
            printf( "$S  called on:" );
            printThreadName( aa[0].threadNameIdx );
            printStack( aa[0].frames,mi_a,mi_q,&ds,aa[0].ft );

            printf( "$S  allocated on: $N(#%U)",aa[1].id );
            printThreadName( aa[1].threadNameIdx );
            printStack( aa[1].frames,mi_a,mi_q,&ds,aa[1].ft );

            printf( "$S  freed on:" );
            printThreadName( aa[2].threadNameIdx );
            printStack( aa[2].frames,mi_a,mi_q,&ds,aa[2].ft );
          }
          break;

        case WRITE_SLACK:
          {
            allocation aa[2];
            if( !readFile(readPipe,aa,2*sizeof(allocation),&ov) )
              break;

            printf( "\n$Wwrite access violation at %p\n",aa[1].ptr );
            printf( "$I  slack area of %p (size %U, offset %s%D)\n",
                aa[0].ptr,aa[0].size,
                aa[1].ptr>aa[0].ptr?"+":"",(char*)aa[1].ptr-(char*)aa[0].ptr );
            printf( "$S  allocated on: $N(#%U)",aa[0].id );
            printThreadName( aa[0].threadNameIdx );
            printStack( aa[0].frames,mi_a,mi_q,&ds,aa[0].ft );
            printf( "$S  freed on:" );
            printThreadName( aa[1].threadNameIdx );
            printStack( aa[1].frames,mi_a,mi_q,&ds,aa[1].ft );
          }
          break;

        case WRITE_MAIN_ALLOC_FAIL:
          printf( "\n$Wnot enough memory to keep track of allocations\n" );
          terminated = -1;
          break;

        case WRITE_WRONG_DEALLOC:
          {
            allocation aa[2];
            if( !readFile(readPipe,aa,2*sizeof(allocation),&ov) )
              break;

            printf( "\n$Wmismatching allocation/release method"
                " of %p (size %U)\n",aa[0].ptr,aa[0].size );
            printf( "$S  allocated on: $N(#%U)",aa[0].id );
            printThreadName( aa[0].threadNameIdx );
            printStack( aa[0].frames,mi_a,mi_q,&ds,aa[0].ft );
            printf( "$S  freed on:" );
            printThreadName( aa[1].threadNameIdx );
            printStack( aa[1].frames,mi_a,mi_q,&ds,aa[1].ft );
          }
          break;

        case WRITE_RAISE_ALLOCATION:
          {
            size_t id;
            if( !readFile(readPipe,&id,sizeof(size_t),&ov) )
              break;
            funcType ft;
            if( !readFile(readPipe,&ft,sizeof(funcType),&ov) )
              break;

            printf( "\n$Sreached allocation #%U $N[$I%s$N]\n",
                id,funcnames[ft] );
          }
          break;

#ifndef NO_THREADNAMES
        case WRITE_THREAD_NAMES:
          {
            int add_q;
            if( !readFile(readPipe,&add_q,sizeof(int),&ov) )
              break;
            int old_q = threadName_q;
            threadName_q += add_q;
            if( !threadName_a )
              threadName_a = HeapAlloc( heap,0,
                  threadName_q*sizeof(threadNameInfo) );
            else
              threadName_a = HeapReAlloc( heap,0,
                  threadName_a,threadName_q*sizeof(threadNameInfo) );
            if( !readFile(readPipe,threadName_a+old_q,
                  add_q*sizeof(threadNameInfo),&ov) )
            {
              threadName_q = 0;
              break;
            }
          }
          break;
#endif

        case WRITE_EXIT:
          if( !readFile(readPipe,&exitCode,sizeof(UINT),&ov) )
          {
            terminated = -2;
            break;
          }
          if( !readFile(readPipe,&terminated,sizeof(int),&ov) )
          {
            terminated = -2;
            break;
          }

          if( !terminated )
          {
            if( alloc_q>0 && alloc_a[alloc_q-1].at==AT_EXIT )
            {
              allocation *exitTrace = &alloc_a[alloc_q-1];
              printf( "$Sexit on:" );
              printThreadName( exitTrace->threadNameIdx );
              printStack( exitTrace->frames,mi_a,mi_q,&ds,FT_COUNT );
            }

            printf( "$Sexit code: %u (%x)\n",exitCode,exitCode );
          }
          else
          {
            printf( "\n$Stermination code: %u (%x)\n",exitCode,exitCode );
          }
          break;
      }
    }
    CloseHandle( ov.hEvent );
    // }}}

    if( terminated==-2 )
      printf( "\n$Wunexpected end of application\n" );

    dbgsym_close( &ds );
    if( alloc_a ) HeapFree( heap,0,alloc_a );
    if( mi_a ) HeapFree( heap,0,mi_a );
    if( contents ) HeapFree( heap,0,contents );
    if( content_ptrs ) HeapFree( heap,0,content_ptrs );
#ifndef NO_THREADNAMES
    if( threadName_a ) HeapFree( heap,0,threadName_a );
#endif
    CloseHandle( readPipe );
  }
  if( controlPipe ) CloseHandle( controlPipe );
  CloseHandle( pi.hThread );
  CloseHandle( pi.hProcess );

  if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );

  ExitProcess( exitCode );
}

// }}}

// vim:fdm=marker
