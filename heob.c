
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
        case 's':
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
            tc->fWriteText( tc,start,end-start );
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
              unsigned int bits = ( arg>>(b*4) )&0xf;
              if( bits>=10 )
                end++[0] = bits - 10 + 'A';
              else
                end++[0] = bits + '0';
            }
            tc->fWriteText( tc,str,end-str );
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

static NOINLINE int matoi( const char *s )
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

static void checkOutputVariant( textColor *tc,const char *cmdLine )
{
  // default
  tc->fWriteText = &WriteText;
  tc->fTextColor = NULL;
  tc->out = GetStdHandle( STD_OUTPUT_HANDLE );
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
    HANDLE process,options *opt,char *exePath,textColor *tc )
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

struct dbghelp;
#ifndef NO_DWARFSTACK
typedef int func_dwstOfFile( const char*,uint64_t,uint64_t*,int,
    void(*)(uint64_t,const char*,int,const char*,struct dbghelp*),
    struct dbghelp* );
#endif

typedef struct dbghelp
{
  HANDLE process;
#ifndef NO_DBGHELP
  func_SymGetLineFromAddr64 *fSymGetLineFromAddr64;
  func_SymFromAddr *fSymFromAddr;
  IMAGEHLP_LINE64 *il;
  SYMBOL_INFO *si;
#endif
#ifndef NO_DWARFSTACK
  func_dwstOfFile *fdwstOfFile;
#endif
  char *absPath;
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
  if( lineno==DWST_NO_DBG_SYM && dh->fSymGetLineFromAddr64 )
  {
    IMAGEHLP_LINE64 *il = dh->il;
    RtlZeroMemory( il,sizeof(IMAGEHLP_LINE64) );
    il->SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD dis;
    if( dh->fSymGetLineFromAddr64(dh->process,addr,&dis,il) )
    {
      filename = il->FileName;
      lineno = il->LineNumber;
    }

    if( dh->fSymFromAddr )
    {
      SYMBOL_INFO *si = dh->si;
      DWORD64 dis64;
      si->SizeOfStruct = sizeof(SYMBOL_INFO);
      si->MaxNameLen = MAX_SYM_NAME;
      if( dh->fSymFromAddr(dh->process,addr,&dis64,si) )
      {
        si->Name[MAX_SYM_NAME] = 0;
        funcname = si->Name;
      }
    }
  }
#endif

  if( dh->opt->fullPath || dh->opt->sourceCode )
  {
    if( !GetFullPathNameA(filename,MAX_PATH,dh->absPath,NULL) )
      dh->absPath[0] = 0;
  }

  if( !dh->opt->fullPath )
  {
    const char *sep1 = strrchr( filename,'/' );
    const char *sep2 = strrchr( filename,'\\' );
    if( sep2>sep1 ) sep1 = sep2;
    if( sep1 ) filename = sep1 + 1;
  }
  else
  {
    if( dh->absPath[0] )
      filename = dh->absPath;
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

      if( dh->opt->sourceCode && dh->absPath[0] )
      {
        HANDLE file = CreateFile( dh->absPath,GENERIC_READ,FILE_SHARE_READ,
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
                int firstLine = lineno + 1 - (int)dh->opt->sourceCode;
                if( firstLine<1 ) firstLine = 1;
                int lastLine = lineno - 1 + (int)dh->opt->sourceCode;
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

// }}}
// read all requested data from pipe {{{

static int readFile( HANDLE file,void *destV,int count )
{
  char *dest = destV;
  while( count>0 )
  {
    DWORD didread;
    if( !ReadFile(file,dest,count,&didread,NULL) ) return( 0 );
    dest += didread;
    count -= didread;
  }
  return( 1 );
}

// }}}
// main {{{

#ifdef _WIN64
#define BITS "64"
#else
#define BITS "32"
#endif
void mainCRTStartup( void )
{
  textColor tc_o;
  textColor *tc = &tc_o;

  char *cmdLine = GetCommandLineA();
  checkOutputVariant( tc,cmdLine );
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
    printf( "    %c-e%cX%c    show exit trace [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.exitTrace,ATT_NORMAL );
    printf( "    %c-C%cX%c    show source code [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,ATT_INFO,defopt.sourceCode,ATT_NORMAL );
    printf( "    %c-r%cX%c    raise exception [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,defopt.raiseException,ATT_NORMAL );
    printf( "    %c-M%cX%c    minimum page protection size [%c%d%c]\n",
        ATT_INFO,ATT_BASE,ATT_NORMAL,
        ATT_INFO,defopt.minProtectSize,ATT_NORMAL );
    printf( "\nheap-observer " HEOB_VER " (" BITS "bit)\n" );
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
    HANDLE heap = GetProcessHeap();
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
      NULL,
      NULL,
#endif
#ifndef NO_DWARFSTACK
      NULL,
#endif
      NULL,
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
      dh.il = HeapAlloc( heap,0,sizeof(IMAGEHLP_LINE64) );
      dh.si = HeapAlloc( heap,0,sizeof(SYMBOL_INFO)+MAX_SYM_NAME );
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
    dh.absPath = HeapAlloc( heap,0,MAX_PATH );

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

    int type;
    modInfo *mi_a = NULL;
    int mi_q = 0;
    allocation *alloc_a = NULL;
    int alloc_q = -2;
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
              EX_DESC( INVALID_FREE );
              EX_DESC( DOUBLE_FREE );
              EX_DESC( ALLOCATION_FAILED );
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
            else if( ei.er.ExceptionCode==EXCEPTION_INVALID_FREE &&
                ei.er.NumberParameters==1 )
            {
              char *addr = (char*)ei.er.ExceptionInformation[0];
              printf( "%c  invalid free of %p\n",ATT_INFO,addr );
            }
            else if( ei.er.ExceptionCode==EXCEPTION_DOUBLE_FREE &&
                ei.er.NumberParameters==1 && ei.aq==3 )
            {
              char *ptr = (char*)ei.aa[1].ptr;
              size_t size = ei.aa[1].size;
              printf( "%c  double free of %p (size %u)\n",
                  ATT_INFO,ptr,size );
              printf( "%c  allocated on:\n",ATT_SECTION );
              printStack( ei.aa[1].frames,mi_a,mi_q,&dh );
              printf( "%c  freed on:\n",ATT_SECTION );
              printStack( ei.aa[2].frames,mi_a,mi_q,&dh );
            }
            else if( ei.er.ExceptionCode==EXCEPTION_ALLOCATION_FAILED &&
                ei.er.NumberParameters==1 )
            {
              size_t size = ei.er.ExceptionInformation[0];
              printf( "%c  allocation failed of %u bytes\n",ATT_INFO,size );
            }

            alloc_q = -1;
          }
          break;

        case WRITE_ALLOC_FAIL:
          {
            allocation a;
            if( !readFile(readPipe,&a,sizeof(allocation)) )
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
            if( !readFile(readPipe,&a,sizeof(allocation)) )
              break;

            printf( "%c\ndeallocation of invalid pointer %p\n",
                ATT_WARN,a.ptr );
            printf( "%c  called on:\n",ATT_SECTION );
            printStack( a.frames,mi_a,mi_q,&dh );
          }
          break;

        case WRITE_DOUBLE_FREE:
          {
            allocation aa[3];
            if( !readFile(readPipe,aa,3*sizeof(allocation)) )
              break;

            printf( "%c\ndouble free of %p (size %u)\n",
                ATT_WARN,aa[1].ptr,aa[1].size );
            printf( "%c  called on:\n",ATT_SECTION );
            printStack( aa[0].frames,mi_a,mi_q,&dh );

            printf( "%c  allocated on:\n",ATT_SECTION );
            printStack( aa[1].frames,mi_a,mi_q,&dh );

            printf( "%c  freed on:\n",ATT_SECTION );
            printStack( aa[2].frames,mi_a,mi_q,&dh );
          }
          break;

        case WRITE_SLACK:
          {
            allocation aa[2];
            if( !readFile(readPipe,aa,2*sizeof(allocation)) )
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
            if( !readFile(readPipe,aa,2*sizeof(allocation)) )
              break;

            printf( "%c\nmismatching allocation/release method"
                " of %p (size %u)\n",ATT_WARN,aa[0].ptr,aa[0].size );
            const char *allocMethods[] = {
              "malloc",
              "new",
              "new[]",
            };
            printf( "%c  allocated with '%s'\n",
                ATT_INFO,allocMethods[aa[0].at] );
            const char *deallocMethods[] = {
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

    allocation exitTrace;
    exitTrace.at = AT_MALLOC;
    if( alloc_q>0 && alloc_a[alloc_q-1].at==AT_EXIT )
    {
      alloc_q--;
      exitTrace = alloc_a[alloc_q];
    }

    if( !alloc_q )
    {
      printf( "%c\nno leaks found\n",ATT_OK );

      if( exitTrace.at==AT_EXIT )
      {
        printf( "%cexit on:\n",ATT_SECTION );
        printStack( exitTrace.frames,mi_a,mi_q,&dh );
      }
      printf( "%cexit code: %u (%x)\n",
          ATT_SECTION,(uintptr_t)exitCode,exitCode );
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
        a.count = 1;
        int j;
        for( j=i+1; j<alloc_q; j++ )
        {
          if( !alloc_a[j].ptr ||
              a.size!=alloc_a[j].size ||
              a.lt!=alloc_a[j].lt ||
              memcmp(a.frames,alloc_a[j].frames,PTRS*sizeof(void*)) )
            continue;

          size += alloc_a[j].size;
          alloc_a[j].ptr = NULL;
          a.count++;
        }
        sumSize += size;

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
        intptr_t ltCount = 0;
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
          if( best<0 ) break;

          alloc_a[best].count = 0;

          if( opt.leakDetails>1 && !i )
            printf( "%cleaks (%s):\n",ATT_SECTION,
                l==LT_LOST?"lost":l==LT_INDIRECTLY_LOST?"indirectly lost":
                l==LT_REACHABLE?"reachable":"indirectly reachable" );

          ltCount += a.count;
          ltSumSize += a.size*a.count;

          if( l<lDetails )
          {
            printf( "%c  %u B * %d = %u B\n",
                ATT_WARN,a.size,(intptr_t)a.count,a.size*a.count );

            printStack( a.frames,mi_a,mi_q,&dh );
          }
        }
        if( opt.leakDetails>1 && ltCount )
          printf( "%c  sum: %u B / %d\n",ATT_WARN,ltSumSize,ltCount );
      }
      if( opt.leakDetails<=1 )
        printf( "%c  sum: %u B / %d\n",ATT_WARN,sumSize,(intptr_t)alloc_q );

      if( exitTrace.at==AT_EXIT )
      {
        printf( "%cexit on:\n",ATT_SECTION );
        printStack( exitTrace.frames,mi_a,mi_q,&dh );
      }
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
    if( dh.il ) HeapFree( heap,0,dh.il );
    if( dh.si ) HeapFree( heap,0,dh.si );
#endif
#ifndef NO_DWARFSTACK
    if( dwstMod ) FreeLibrary( dwstMod );
#endif
    if( dh.absPath ) HeapFree( heap,0,dh.absPath );
    if( alloc_a ) HeapFree( heap,0,alloc_a );
    if( mi_a ) HeapFree( heap,0,mi_a );
    CloseHandle( readPipe );
  }
  CloseHandle( pi.hThread );
  CloseHandle( pi.hProcess );

  printf( "%c",ATT_NORMAL );

  ExitProcess( exitCode );
}

// }}}

// vim:fdm=marker
