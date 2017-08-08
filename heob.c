
//          Copyright Hannes Domani 2014 - 2017.
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
typedef void func_WriteTextW( struct textColor*,const wchar_t*,size_t );
typedef void func_TextColor( struct textColor*,textColorAtt );

typedef struct textColor
{
  func_WriteText *fWriteText;
  func_WriteText *fWriteSubText;
  func_WriteTextW *fWriteSubTextW;
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

char *num2hexstr( char *str,UINT64 arg,int count )
{
#ifndef _WIN64
  uint32_t a32[2];
  RtlMoveMemory( a32,&arg,sizeof(arg) );
  int b;
  for( b=count-1; b>=0; b-- )
  {
    uint32_t v = a32[b>=8];
    str++[0] = num2hex( v>>((b%8)*4) );
  }
  return( str );
#else
  int b;
  for( b=count-1; b>=0; b-- )
    str++[0] = num2hex( (unsigned)(arg>>(b*4)) );
  return( str );
#endif
}

NOINLINE char *num2str( char *start,uintptr_t arg,int minus )
{
  if( !arg )
    (--start)[0] = '0';
  while( arg )
  {
    (--start)[0] = arg%10 + '0';
    arg /= 10;
  }
  if( minus )
    (--start)[0] = '-';
  return( start );
}

static NOINLINE void mprintf( textColor *tc,const char *format,... )
{
  if( !tc->out ) return;

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
              tc->fWriteSubText( tc,arg,lstrlen(arg) );
          }
          break;

        case 'S': // const wchar_t*
          {
            const wchar_t *arg = va_arg( vl,const wchar_t* );
            if( arg && arg[0] )
              tc->fWriteSubTextW( tc,arg,lstrlenW(arg) );
          }
          break;

        case 'd': // int
        case 'D': // intptr_t
        case 'u': // unsigned int
        case 'U': // uintptr_t
          {
            uintptr_t arg = 0;
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
            char *start = num2str( end,arg,minus );
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

        case 'i': // indent
          {
            int indent = va_arg( vl,int );
            int i;
            for( i=0; i<indent; i++ )
            {
              if( tc->fTextColor )
                tc->fTextColor( tc,i%ATT_BASE );

              tc->fWriteText( tc,"| ",2 );
            }
            if( tc->fTextColor )
              tc->fTextColor( tc,ATT_NORMAL );
          }
          break;

        case 't': // time
          {
            DWORD ticks = va_arg( vl,DWORD );
            unsigned milli = ticks%1000;
            ticks /= 1000;
            unsigned sec = ticks%60;
            ticks /= 60;
            unsigned min = ticks%60;
            ticks /= 60;
            unsigned hour = ticks%24;
            ticks /= 24;
            unsigned day = ticks;
            char timestr[15] = {
              day  /10 +'0',day       %10+'0',':',
              hour /10 +'0',hour      %10+'0',':',
              min  /10 +'0',min       %10+'0',':',
              sec  /10 +'0',sec       %10+'0','.',
              milli/100+'0',(milli/10)%10+'0',milli%10+'0',
            };
            tc->fWriteText( tc,timestr,15 );
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
        tc->fTextColor( tc,color );
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

NOINLINE char *mstrrchr( const char *s,char c )
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

static NOINLINE char *mstrstr( const char *s,const char *f )
{
  int ls = lstrlen( s );
  int lf = lstrlen( f );
  if( lf>ls ) return( NULL );
  if( !lf ) return( (char*)s );
  int ld = ls - lf + 1;
  int i;
  for( i=0; i<ld; i++ )
    if( !mmemcmp(s+i,f,lf) ) return( (char*)s+i );
  return( NULL );
}
#define strstr mstrstr

static NOINLINE char *strreplace(
    const char *str,const char *from,const char *to,HANDLE heap )
{
  const char *pos = strstr( str,from );
  if( !pos ) return( NULL );

  int strLen = lstrlen( str );
  int fromLen = lstrlen( from );
  int toLen = lstrlen( to );
  char *replace = HeapAlloc( heap,0,strLen-fromLen+toLen+1 );
  if( !replace ) return( NULL );

  int replacePos = 0;
  if( pos>str )
  {
    RtlMoveMemory( replace,str,pos-str );
    replacePos += (int)( pos - str );
  }
  if( toLen )
  {
    RtlMoveMemory( replace+replacePos,to,toLen );
    replacePos += toLen;
  }
  if( str+strLen>pos+fromLen )
  {
    int endLen = (int)( (str+strLen) - (pos+fromLen) );
    RtlMoveMemory( replace+replacePos,pos+fromLen,endLen );
    replacePos += endLen;
  }
  replace[replacePos] = 0;
  return( replace );
}

static NOINLINE char *strreplacenum(
    const char *str,const char *from,uintptr_t to,HANDLE heap )
{
  char numStr[32];
  char *numEnd = numStr + sizeof(numStr);
  (--numEnd)[0] = 0;
  char *numStart = num2str( numEnd,to,0 );

  return( strreplace(str,from,numStart,heap) );
}

int strstart( const char *str,const char *start )
{
  int l1 = lstrlen( str );
  int l2 = lstrlen( start );
  if( l1<l2 ) return( 0 );
  return( CompareString(LOCALE_SYSTEM_DEFAULT,NORM_IGNORECASE,
        str,l2,start,l2)==2 );
}

// }}}
// output variants {{{

static void WriteText( textColor *tc,const char *t,size_t l )
{
  DWORD written;
  WriteFile( tc->out,t,(DWORD)l,&written,NULL );
}

static int UTF16toUTF32( const uint16_t *u16,size_t l,uint32_t *c32 )
{
  uint16_t c16_1 = u16[0];
  if( c16_1<0xd800 )                     // 11010AAA AAAAAAAA
    *c32 = c16_1;                        // -> 11010AAA AAAAAAAA
  else if( c16_1<0xdc00 )                // 110110AA AAAAAAAA
  {
    if( l<2 ) return( 0 );
    uint16_t c16_2 = u16[1];
    if( (c16_2&0xdc00)!=0xdc00 )         // 110111BB BBBBBBBB
      return( 0 );
    *c32 =
      ( (((uint32_t)c16_1&0x3ff)<<10) |  // -> 0000AAAA AAAAAABB BBBBBBBB
        ((uint32_t)c16_2&0x3ff) ) +
      0x10000;
    return( 2 );
  }
  else if( c16_1<0xe000 )                // 110XXXXX XXXXXXXX
    return( 0 );
  else                                   // AAAAAAAA AAAAAAAA
    *c32 = c16_1;                        // -> 00000000 AAAAAAAA AAAAAAAA

  return( 1 );
}

static void WriteTextW( textColor *tc,const wchar_t *t,size_t l )
{
  const uint16_t *u16 = t;
  uint32_t c32;
  DWORD written;
  size_t i;
  for( i=0; i<l; i++ )
  {
    int chars = UTF16toUTF32( u16+i,l-i,&c32 );
    if( !chars ) continue;

    uint8_t c8 = c32;
    if( c32>=0x80 )
      c8 = '?';
    else
      c8 = c32;
    WriteFile( tc->out,&c8,1,&written,NULL );

    if( chars>1 ) i++;
  }
}

static void WriteTextConsoleW( textColor *tc,const wchar_t *t,size_t l )
{
  DWORD written;
  WriteConsoleW( tc->out,t,(DWORD)l,&written,NULL );
}

static void TextColorConsole( textColor *tc,textColorAtt color )
{
  if( tc->color==color ) return;

  SetConsoleTextAttribute( tc->out,tc->colors[color] );

  tc->color = color;
}

static void TextColorTerminal( textColor *tc,textColorAtt color )
{
  if( tc->color==color ) return;

  int c = tc->colors[color];
  char text[] = { 27,'[',(c/10000)%10+'0',(c/1000)%10+'0',(c/100)%10+'0',';',
    (c/10)%10+'0',c%10+'0','m' };
  DWORD written;
  WriteFile( tc->out,text,sizeof(text),&written,NULL );

  tc->color = color;
}

static void WriteTextHtml( textColor *tc,const char *ts,size_t l )
{
  const unsigned char *t = (const unsigned char*)ts;
  const unsigned char *end = t + l;
  const unsigned char *next;
  char lt[] = "&lt;";
  char gt[] = "&gt;";
  char amp[] = "&amp;";
  DWORD written;
  for( next=t; next<end; next++ )
  {
    unsigned char c = next[0];
    if( c<0x09 || (c>=0x0b && c<=0x0c) || (c>=0x0e && c<=0x1f) || c>=0x7f )
    {
      if( next>t )
        WriteFile( tc->out,t,(DWORD)(next-t),&written,NULL );
      char hex[] = "&#x00;";
      num2hexstr( hex+3,c,2 );
      WriteFile( tc->out,hex,sizeof(hex)-1,&written,NULL );
      t = next + 1;
      continue;
    }
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

static void WriteTextHtmlW( textColor *tc,const wchar_t *ts,size_t l )
{
  const uint16_t *u16 = ts;
  uint32_t c32;
  char lt[] = "&lt;";
  char gt[] = "&gt;";
  char amp[] = "&amp;";
  DWORD written;
  size_t i;
  for( i=0; i<l; i++ )
  {
    int chars = UTF16toUTF32( u16+i,l-i,&c32 );
    if( !chars ) continue;

    if( c32<0x09 || (c32>=0x0b && c32<=0x0c) ||
        (c32>=0x0e && c32<=0x1f) || c32>=0x7f )
    {
      char hex[] = "&#x000000;";
      int bytes = c32>=0x10000 ? 3 : ( c32>=0x100 ? 2 : 1 );
      char *end = num2hexstr( hex+3,c32,bytes*2 );
      end++[0] = ';';
      WriteFile( tc->out,hex,(DWORD)(end-hex),&written,NULL );
    }
    else if( c32=='<' )
      WriteFile( tc->out,lt,sizeof(lt)-1,&written,NULL );
    else if( c32=='>' )
      WriteFile( tc->out,gt,sizeof(gt)-1,&written,NULL );
    else if( c32=='&' )
      WriteFile( tc->out,amp,sizeof(amp)-1,&written,NULL );
    else
    {
      uint8_t c8 = c32;
      WriteFile( tc->out,&c8,1,&written,NULL );
    }

    if( chars>1 ) i++;
  }
}

static void TextColorHtml( textColor *tc,textColorAtt color )
{
  if( tc->color==color ) return;

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

  tc->color = color;
}

static void setTextColorTerminal( textColor *tc )
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

static void checkOutputVariant( textColor *tc,HANDLE out )
{
  // default
  tc->fWriteText = &WriteText;
  tc->fWriteSubText = &WriteText;
  tc->fWriteSubTextW = &WriteTextW;
  tc->fTextColor = NULL;
  tc->out = out;
  tc->color = ATT_NORMAL;

  if( !out ) return;

  HMODULE ntdll = GetModuleHandle( "ntdll.dll" );
  if( !ntdll ) return;

  DWORD flags;
  if( GetConsoleMode(tc->out,&flags) )
  {
    if( GetProcAddress(ntdll,"wine_get_version") )
    {
      // wine terminal
      setTextColorTerminal( tc );
      return;
    }

    // windows console
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( tc->out,&csbi );
    int bg = csbi.wAttributes&0xf0;

    tc->fWriteSubTextW = &WriteTextConsoleW;
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

  func_NtQueryObject *fNtQueryObject =
    (func_NtQueryObject*)GetProcAddress( ntdll,"NtQueryObject" );
  if( fNtQueryObject )
  {
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
      wchar_t deviceNull[] = L"\\Device\\Null";
      size_t dnl = sizeof(deviceNull)/2 - 1;
      if( (size_t)oni->Name.Length/2>l1+l2 &&
          !memcmp(oni->Name.Buffer,namedPipe,l1*2) &&
          !memcmp(oni->Name.Buffer+(oni->Name.Length/2-l2),toMaster,l2*2) )
      {
        // terminal emulator
        setTextColorTerminal( tc );
      }
      else if( oni->Name.Length/2==dnl &&
          !memcmp(oni->Name.Buffer,deviceNull,dnl*2) )
      {
        // null device
        tc->out = NULL;
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
          "<title>heob " HEOB_VER "</title>\n"
          "</head><body>\n"
          "<h3>";
        const char *styleInit2 =
          "</h3>\n"
          "<pre>\n";
        DWORD written;
        WriteFile( tc->out,styleInit,lstrlen(styleInit),&written,NULL );
        const wchar_t *cmdLineW = GetCommandLineW();
        WriteTextHtmlW( tc,cmdLineW,lstrlenW(cmdLineW) );
        WriteFile( tc->out,styleInit2,lstrlen(styleInit2),&written,NULL );

        tc->fWriteText = &WriteTextHtml;
        tc->fWriteSubText = &WriteTextHtml;
        tc->fWriteSubTextW = &WriteTextHtmlW;
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

int isWrongArch( HANDLE process )
{
  BOOL remoteWow64,meWow64;
  IsWow64Process( process,&remoteWow64 );
  IsWow64Process( GetCurrentProcess(),&meWow64 );
  return( remoteWow64!=meWow64 );
}

// }}}
// error pipe {{{

static HANDLE openErrorPipe( void )
{
  char errorPipeName[32] = "\\\\.\\Pipe\\heob.error.";
  char *end = num2hexstr( errorPipeName+lstrlen(errorPipeName),
      GetCurrentProcessId(),8 );
  end[0] = 0;

  HANDLE errorPipe = CreateFile( errorPipeName,
      GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL );
  if( errorPipe==INVALID_HANDLE_VALUE ) errorPipe = NULL;

  return( errorPipe );
}

static void writeCloseErrorPipe( HANDLE errorPipe,
    unsigned exitStatus,unsigned extraArg )
{
  if( !errorPipe ) return;

  unsigned data[2] = { exitStatus,extraArg };
  DWORD didwrite;
  WriteFile( errorPipe,data,sizeof(data),&didwrite,NULL );

  CloseHandle( errorPipe );
}

enum
{
  HEOB_OK,
  HEOB_HELP,
  HEOB_BAD_ARG,
  HEOB_PROCESS_FAIL,
  HEOB_WRONG_BITNESS,
  HEOB_PROCESS_KILLED,
  HEOB_NO_CRT,
  HEOB_EXCEPTION,
  HEOB_OUT_OF_MEMORY,
  HEOB_UNEXPECTED_END,
  HEOB_TRACE,
  HEOB_CONSOLE,
};

// }}}
// code injection {{{

typedef DWORD WINAPI func_heob( LPVOID );
typedef VOID CALLBACK func_inj( remoteData* );

static CODE_SEG(".text$1") VOID CALLBACK remoteCall( remoteData *rd )
{
  HMODULE app = rd->fLoadLibraryW( rd->exePath );
  rd->heobMod = app;

  func_heob *fheob = (func_heob*)( (size_t)app + rd->injOffset );
  HANDLE heobThread = rd->fCreateThread( NULL,0,fheob,rd,0,NULL );
  rd->fCloseHandle( heobThread );

  rd->fWaitForSingleObject( rd->startMain,INFINITE );
  rd->fCloseHandle( rd->startMain );
}

static CODE_SEG(".text$2") HANDLE inject(
    HANDLE process,HANDLE thread,options *opt,options *globalopt,
    const char *specificOptions,wchar_t *exePath,textColor *tc,
    int raise_alloc_q,size_t *raise_alloc_a,HANDLE *controlPipe,
    HANDLE in,HANDLE err,attachedProcessInfo **api,
    const char *subOutName,const char *subXmlName,const char *subCurDir,
    unsigned *heobExit )
{
  func_inj *finj = &remoteCall;
  size_t funcSize = (size_t)&inject - (size_t)finj;
  size_t soOffset = funcSize + sizeof(remoteData) +
    raise_alloc_q*sizeof(size_t);
  size_t soSize = ( specificOptions ? lstrlen(specificOptions) + 1 : 0 );
  size_t fullSize = soOffset + soSize;

  unsigned char *fullDataRemote =
    VirtualAllocEx( process,NULL,fullSize,MEM_COMMIT,PAGE_EXECUTE_READWRITE );

  HANDLE heap = GetProcessHeap();
  unsigned char *fullData = HeapAlloc( heap,0,fullSize );
  RtlMoveMemory( fullData,finj,funcSize );
  remoteData *data = (remoteData*)( fullData+funcSize );
  RtlZeroMemory( data,sizeof(remoteData) );

  PAPCFUNC remoteFuncStart = (PAPCFUNC)( fullDataRemote );

  HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );
  data->kernel32 = kernel32;
  data->fCreateThread =
    (func_CreateThread*)GetProcAddress( kernel32,"CreateThread" );
  data->fWaitForSingleObject = (func_WaitForSingleObject*)GetProcAddress(
      kernel32,"WaitForSingleObject" );
  data->fCloseHandle =
    (func_CloseHandle*)GetProcAddress( kernel32,"CloseHandle" );
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
  func_heob *fheob = &heob;
  data->injOffset = (size_t)fheob - (size_t)GetModuleHandle( NULL );

  // create 2 null device handles so GetStdHandle() won't return
  // the wrong handles in applications without a console
  HANDLE nullDevice = CreateFile( "NUL",GENERIC_READ|GENERIC_WRITE,
      FILE_SHARE_READ|FILE_SHARE_WRITE,NULL,OPEN_EXISTING,0,NULL );
  if( nullDevice!=INVALID_HANDLE_VALUE )
  {
    HANDLE nullDeviceOut;
    DuplicateHandle( GetCurrentProcess(),nullDevice,
        process,&nullDeviceOut,0,FALSE,DUPLICATE_SAME_ACCESS );
    DuplicateHandle( GetCurrentProcess(),nullDevice,
        process,&nullDeviceOut,0,FALSE,DUPLICATE_SAME_ACCESS );
    CloseHandle( nullDevice );
  }

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

  HANDLE startMain = CreateEvent( NULL,FALSE,FALSE,NULL );
  DuplicateHandle( GetCurrentProcess(),startMain,
      process,&data->startMain,0,FALSE,
      DUPLICATE_SAME_ACCESS );

  RtlMoveMemory( &data->opt,opt,sizeof(options) );
  RtlMoveMemory( &data->globalopt,globalopt,sizeof(options) );

  if( subOutName ) lstrcpy( data->subOutName,subOutName );
  if( subXmlName ) lstrcpy( data->subXmlName,subXmlName );
  if( subCurDir ) lstrcpy( data->subCurDir,subCurDir );

  data->raise_alloc_q = raise_alloc_q;
  if( raise_alloc_q )
    RtlMoveMemory( data->raise_alloc_a,
        raise_alloc_a,raise_alloc_q*sizeof(size_t) );

  if( soSize )
  {
    data->specificOptions = (char*)fullDataRemote + soOffset;
    RtlMoveMemory( fullData+soOffset,specificOptions,soSize );
  }

  WriteProcessMemory( process,fullDataRemote,fullData,fullSize,NULL );

  QueueUserAPC( remoteFuncStart,thread,(ULONG_PTR)(fullDataRemote+funcSize) );
  ResumeThread( thread );

  COORD consoleCoord;
  int errColor;
  if( in )
  {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( err,&csbi );
    consoleCoord = csbi.dwCursorPosition;
    errColor = csbi.wAttributes&0xff;
    const char *killText = " kill ";
    DWORD didwrite;
    WriteFile( err,killText,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,
        errColor^(FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY) );
    WriteFile( err,killText+1,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,killText+2,4,&didwrite,NULL );
  }

  HANDLE h[2] = { initFinished,in };
  int waitCount = in ? 2 : 1;
  while( 1 )
  {
    if( WaitForMultipleObjects(waitCount,h,FALSE,INFINITE)==WAIT_OBJECT_0 )
      break;

    INPUT_RECORD ir;
    DWORD didread;
    if( ReadConsoleInput(in,&ir,1,&didread) &&
        ir.EventType==KEY_EVENT &&
        ir.Event.KeyEvent.bKeyDown &&
        ir.Event.KeyEvent.wVirtualKeyCode=='K' )
    {
      CloseHandle( readPipe );
      readPipe = NULL;
      break;
    }
  }

  if( in )
  {
    SetConsoleCursorPosition( err,consoleCoord );

    DWORD didwrite;
    int textLen = 6;
    FillConsoleOutputAttribute( err,errColor,textLen,consoleCoord,&didwrite );
    FillConsoleOutputCharacter( err,' ',textLen,consoleCoord,&didwrite );
  }

  CloseHandle( initFinished );
  if( !readPipe )
  {
    CloseHandle( startMain );
    HeapFree( heap,0,fullData );
    printf( "$Wprocess killed\n" );
    *heobExit = HEOB_PROCESS_KILLED;
    return( NULL );
  }

  ReadProcessMemory( process,fullDataRemote+funcSize,data,
      sizeof(remoteData),NULL );

  if( !data->master )
  {
    CloseHandle( readPipe );
    readPipe = NULL;
    printf( "$Wonly works with dynamically linked CRT\n" );
    *heobExit = HEOB_NO_CRT;
  }
  else
    RtlMoveMemory( exePath,data->exePath,MAX_PATH*2 );
  HeapFree( heap,0,fullData );

  if( data->noCRT==2 )
  {
    opt->handleException = 2;
    printf( "\n$Ino CRT found\n" );
  }

  if( data->api )
  {
    *api = HeapAlloc( heap,0,sizeof(attachedProcessInfo) );
    ReadProcessMemory( process,data->api,*api,
        sizeof(attachedProcessInfo),NULL );
    VirtualFreeEx( process,data->api,0,MEM_RELEASE );
  }

  SuspendThread( thread );
  SetEvent( startMain );
  CloseHandle( startMain );

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

typedef struct sourceLocation
{
  const char *filename;
  const char *funcname;
  int lineno;
  int columnno;
  struct sourceLocation *inlineLocation;
}
sourceLocation;

typedef struct stackSourceLocation
{
  uintptr_t addr;
  sourceLocation sl;
}
stackSourceLocation;

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
  stackSourceLocation *ssl;
  int sslCount;
  int sslIdx;
  char **func_a;
  int func_q;
  char **file_a;
  int file_q;
}
dbgsym;

static const char *strings_add( const char *str,
    char ***str_a,int *str_q,HANDLE heap )
{
  if( !str || !str[0] ) return( NULL );

  char **a = *str_a;
  int q = *str_q;

  int s = 0;
  int e = q;
  int i = q/2;
  while( e>s )
  {
    int cmp = lstrcmp( str,a[i] );
    if( !cmp ) return( a[i] );
    if( cmp<0 )
      e = i;
    else
      s = i + 1;
    i = ( s+e )/2;
  }

  if( !(q&63) )
  {
    int c = q + 64;
    if( !q )
      a = HeapAlloc( heap,0,c*sizeof(char*) );
    else
      a = HeapReAlloc( heap,0,a,c*sizeof(char*) );
  }

  if( i<q )
    RtlMoveMemory( a+i+1,a+i,(q-i)*sizeof(char*) );

  int l = lstrlen( str ) + 1;
  char *copy = HeapAlloc( heap,0,l );
  RtlMoveMemory( copy,str,l );
  a[i] = copy;
  q++;

  *str_a = a;
  *str_q = q;

  return( copy );
}

static void dbgsym_init( dbgsym *ds,HANDLE process,textColor *tc,options *opt,
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

static void cacheClear( dbgsym *ds )
{
  if( !ds->ssl ) return;

  HANDLE heap = ds->heap;
  stackSourceLocation *ssl = ds->ssl;
  int sslCount = ds->sslCount;
  int i;
  for( i=0; i<sslCount; i++ )
  {
    sourceLocation *sl = ssl[i].sl.inlineLocation;
    while( sl )
    {
      sourceLocation *il = sl->inlineLocation;
      HeapFree( heap,0,sl );
      sl = il;
    }
  }
  HeapFree( heap,0,ssl );

  char **str_a = ds->func_a;
  int str_q = ds->func_q;
  for( i=0; i<str_q; i++ )
    HeapFree( heap,0,str_a[i] );
  if( str_a )
    HeapFree( heap,0,str_a );

  str_a = ds->file_a;
  str_q = ds->file_q;
  for( i=0; i<str_q; i++ )
    HeapFree( heap,0,str_a[i] );
  if( str_a )
    HeapFree( heap,0,str_a );

  ds->ssl = NULL;
  ds->sslCount = 0;
  ds->func_a = NULL;
  ds->func_q = 0;
  ds->file_a = NULL;
  ds->file_q = 0;
}

static void dbgsym_close( dbgsym *ds )
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

  cacheClear( ds );
}

#ifndef _WIN64
#define PTR_SPACES "        "
#else
#define PTR_SPACES "                "
#endif

static int *sort_allocations( void *base,int *idxs,int num,int size,
    HANDLE heap,int (*compar)(const void*,const void*) );

static int cmp_ptr( const void *av,const void *bv )
{
  const uintptr_t *a = av;
  const uintptr_t *b = bv;
  return( *a>*b ? 1 : ( *a<*b ? -1 : 0 ) );
}

static void locFuncCache(
    uint64_t addr,const char *filename,int lineno,const char *funcname,
    void *context,int columnno )
{
  if( lineno==DWST_BASE_ADDR ) return;

  dbgsym *ds = context;
  uintptr_t printAddr = (uintptr_t)addr;

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
              locFuncCache(
                  printAddr,il->FileName,il->LineNumber,si->Name,ds,0 );
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

  stackSourceLocation *ssl = ds->ssl;
  int sslIdx = ds->sslIdx;
  if( printAddr )
  {
    sslIdx++;
    if( sslIdx>=ds->sslCount ) return;
    ds->sslIdx = sslIdx;
    ssl[sslIdx].addr = printAddr;
  }

  sourceLocation *sl = &ssl[sslIdx].sl;
  while( sl->inlineLocation ) sl = sl->inlineLocation;
  if( sl->lineno )
  {
    sl->inlineLocation = HeapAlloc(
        ds->heap,HEAP_ZERO_MEMORY,sizeof(sourceLocation) );
    sl = sl->inlineLocation;
  }
  if( lineno>0 )
  {
    const char *absPath = filename;
    if( GetFullPathNameA(filename,MAX_PATH,ds->absPath,NULL) )
      absPath = ds->absPath;
    sl->filename = strings_add( absPath,&ds->file_a,&ds->file_q,ds->heap );
  }
  sl->funcname = strings_add( funcname,&ds->func_a,&ds->func_q,ds->heap );
  sl->lineno = lineno;
  sl->columnno = columnno;
}

static stackSourceLocation *findStackSourceLocation(
    uintptr_t addr,stackSourceLocation *ssl_a,int ssl_q )
{
  int s = 0;
  int e = ssl_q;
  int i = ssl_q/2;
  while( e>s )
  {
    uintptr_t curAddr = ssl_a[i].addr;
    if( addr==curAddr ) return( ssl_a+i );
    if( addr<curAddr )
      e = i;
    else
      s = i + 1;
    i = ( s+e )/2;
  }
  return( NULL );
}

static void cacheSymbolData( allocation *alloc_a,int *alloc_idxs,int alloc_q,
    modInfo *mi_a,int mi_q,dbgsym *ds,int initFrames )
{
  cacheClear( ds );

  int i;
  int fc = 0;
  uintptr_t threadInitAddr = ds->threadInitAddr;
  for( i=0; i<alloc_q; i++ )
  {
    int idx = alloc_idxs ? alloc_idxs[i] : i;
    allocation *a = alloc_a + idx;

    if( initFrames )
    {
      uintptr_t *frames = (uintptr_t*)a->frames;
      int c;
      for( c=0; c<PTRS && frames[c] && frames[c]!=threadInitAddr; c++ )
        frames[c]--;
      a->frameCount = c;
    }

    fc += a->frameCount;
  }
  if( !fc ) return;
  uintptr_t *frame_a = HeapAlloc( ds->heap,0,fc*sizeof(uintptr_t) );
  fc = 0;
  for( i=0; i<alloc_q; i++ )
  {
    int idx = alloc_idxs ? alloc_idxs[i] : i;
    allocation *a = alloc_a + idx;
    RtlMoveMemory( frame_a+fc,a->frames,a->frameCount*sizeof(void*) );
    fc += a->frameCount;
  }

  int *frame_idxs = sort_allocations(
      frame_a,NULL,fc,sizeof(uintptr_t),ds->heap,cmp_ptr );
  int unique_frames = 0;
  uintptr_t prev_frame = 0;
  for( i=0; i<fc; i++ )
  {
    uintptr_t cur_frame = frame_a[frame_idxs[i]];
    if( cur_frame==prev_frame ) continue;
    prev_frame = cur_frame;
    unique_frames++;
  }
  uint64_t *frames = HeapAlloc( ds->heap,0,unique_frames*sizeof(uint64_t) );
  unique_frames = 0;
  prev_frame = 0;
  for( i=0; i<fc; i++ )
  {
    uintptr_t cur_frame = frame_a[frame_idxs[i]];
    if( cur_frame==prev_frame ) continue;
    prev_frame = cur_frame;
    frames[unique_frames++] = cur_frame;
  }
  HeapFree( ds->heap,0,frame_a );
  HeapFree( ds->heap,0,frame_idxs );
  fc = unique_frames;

  ds->sslCount = fc;
  ds->sslIdx = -1;
  ds->ssl = HeapAlloc(
      ds->heap,HEAP_ZERO_MEMORY,fc*sizeof(stackSourceLocation) );
  int j;
  for( j=0; j<fc; j++ )
  {
    int k;
    uintptr_t frame = (uintptr_t)frames[j];
    for( k=0; k<mi_q && (frame<mi_a[k].base ||
          frame>=mi_a[k].base+mi_a[k].size); k++ );
    if( k>=mi_q ) continue;
    modInfo *mi = mi_a + k;

    int l;
    for( l=j+1; l<fc && frames[l]>=mi->base &&
        frames[l]<mi->base+mi->size; l++ );

#ifndef NO_DWARFSTACK
    if( ds->fdwstOfFile )
      ds->fdwstOfFile( mi->path,mi->base,frames+j,l-j,locFuncCache,ds );
    else
#endif
    {
      for( i=j; i<l; i++ )
        locFuncCache( frames[i],mi->path,DWST_NO_DBG_SYM,NULL,ds,0 );
    }

    j = l - 1;
  }
  ds->sslCount = ds->sslIdx + 1;

  HeapFree( ds->heap,0,frames );
}

static void locOut( textColor *tc,uintptr_t addr,
    const char *filename,int lineno,int columnno,const char *funcname,
    options *opt,int indent )
{
  const char *printFilename = NULL;
  if( filename )
  {
    printFilename = opt->fullPath ? NULL : strrchr( filename,'\\' );
    if( !printFilename ) printFilename = filename;
    else printFilename++;
  }

  printf( "  %i",indent );
  switch( lineno )
  {
    case DWST_BASE_ADDR:
      printf( "  $B%X$N   $B%s\n",addr,printFilename );
      break;

    case DWST_NO_DBG_SYM:
#ifndef NO_DWARFSTACK
    case DWST_NO_SRC_FILE:
    case DWST_NOT_FOUND:
#endif
      printf( "    %X",addr );
      if( funcname )
        printf( "   [$I%s$N]",funcname );
      printf( "\n" );
      break;

    default:
      if( addr )
        printf( "    %X",addr );
      else
        printf( "      " PTR_SPACES );
      printf( "   $O%s$N:%d",printFilename,lineno );
      if( columnno>0 )
        printf( ":$S%d$N",columnno );
      if( funcname )
        printf( " [$I%s$N]",funcname );
      printf( "\n" );

      if( opt->sourceCode )
      {
        HANDLE file = CreateFile( filename,GENERIC_READ,FILE_SHARE_READ,
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
                int firstLine = lineno + 1 - opt->sourceCode;
                if( firstLine<1 ) firstLine = 1;
                int lastLine = lineno - 1 + opt->sourceCode;
                if( firstLine>1 )
                  printf( "  %i      ...\n",indent );
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
                      printf( "$S> " );
                      if( indent )
                        printf( "$N%i$S",indent );
                      printf( "      " );
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
                      printf( "  %i      ",indent );
                      tc->fWriteText( tc,bol,eol-bol );
                    }
                  }

                  bol = eol;
                  if( bol==eof ) break;
                }
                if( bol>map && bol[-1]!='\n' )
                  printf( "\n" );
                if( bol!=eof )
                  printf( "  %i      ...\n",indent );
                printf( "  %i\n",indent );

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

static void sslOut( textColor *tc,
    stackSourceLocation *ssl,options *opt,int indent )
{
  uintptr_t addr = ssl->addr;
  sourceLocation *sl = &ssl->sl;
  while( sl )
  {
    locOut( tc,addr,sl->filename,sl->lineno,sl->columnno,sl->funcname,
        opt,indent );

    addr = 0;
    sl = sl->inlineLocation;
  }
}

static void locXml( textColor *tc,uintptr_t addr,
    const char *filename,int lineno,const char *funcname,
    modInfo *mi )
{
  printf( "    <frame>\n" );
  if( addr )
    printf( "      <ip>%X</ip>\n",addr );
  if( mi )
  {
    if( !addr && !funcname && !lineno )
      printf( "      <ip>%X</ip>\n",mi->base );
    printf( "      <obj>%s</obj>\n",mi->path );
  }
  if( funcname )
    printf( "      <fn>%s</fn>\n",funcname );
  if( lineno>0 )
  {
    const char *sep = strrchr( filename,'\\' );
    const char *filepart;
    if( sep )
    {
      printf( "      <dir>" );
      tc->fWriteSubText( tc,filename,sep-filename );
      printf( "</dir>\n" );
      filepart = sep + 1;
    }
    else
      filepart = filename;
    printf( "      <file>%s</file>\n",filepart );
    printf( "      <line>%d</line>\n",lineno );
  }
  printf( "    </frame>\n" );
}

static void sslXml( textColor *tc,
    stackSourceLocation *ssl,modInfo *mi )
{
  uintptr_t addr = ssl->addr;
  sourceLocation *sl = &ssl->sl;
  while( sl )
  {
    locXml( tc,addr,sl->filename,sl->lineno,sl->funcname,mi );

    addr = 0;
    sl = sl->inlineLocation;
  }
}

static void printStackCount( void **framesV,int fc,
    modInfo *mi_a,int mi_q,dbgsym *ds,funcType ft,int indent )
{
  textColor *tc = ds->tc;
  if( !tc->out ) return;

  if( ft<FT_COUNT )
  {
    if( indent>=0 )
      printf( "  %i      " PTR_SPACES "   [$I%s$N]\n",
          indent,ds->funcnames[ft] );
    else
      locXml( tc,0,NULL,0,ds->funcnames[ft],NULL );
  }

  uintptr_t *frames = (uintptr_t*)framesV;
  stackSourceLocation *ssl = ds->ssl;
  int sslCount = ds->sslCount;
  int j;
  for( j=0; j<fc; )
  {
    int k;
    uintptr_t frame = frames[j];
    for( k=0; k<mi_q && (frame<mi_a[k].base ||
          frame>=mi_a[k].base+mi_a[k].size); k++ );
    if( k>=mi_q )
    {
      if( indent>=0 )
        locOut( tc,frame,"?",DWST_BASE_ADDR,0,NULL,ds->opt,indent );
      else
        locXml( tc,frame,NULL,0,NULL,NULL );
      j++;
      continue;
    }
    modInfo *mi = mi_a + k;

    int l;
    for( l=j+1; l<fc && frames[l]>=mi->base &&
        frames[l]<mi->base+mi->size; l++ );

    if( indent>=0 )
      locOut( tc,mi->base,mi->path,DWST_BASE_ADDR,0,NULL,ds->opt,indent );

    for( ; j<l; j++ )
    {
      frame = frames[j];
      stackSourceLocation *s = findStackSourceLocation( frame,ssl,sslCount );
      if( !s )
      {
        if( indent>=0 )
          locOut( tc,frame,mi->path,DWST_NO_DBG_SYM,0,NULL,ds->opt,indent );
        else
          locXml( tc,frame,NULL,0,NULL,mi );
        continue;
      }
      if( indent>=0 )
        sslOut( tc,s,ds->opt,indent );
      else
        sslXml( tc,s,mi );
    }
  }
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

static int *sort_allocations( void *base,int *idxs,int num,int size,
    HANDLE heap,int (*compar)(const void*,const void*) )
{
  int i,c,r;
  char *b = base;

  if( !idxs )
  {
    idxs = HeapAlloc( heap,0,num*sizeof(int) );
    for( i=0; i<num; i++ )
      idxs[i] = i;
  }

  for( i=num/2-1; i>=0; i-- )
  {
    for( r=i; r*2+1<num; r=c )
    {
      c = r*2 + 1;
      if( c<num-1 && compar(b+idxs[c]*size,b+idxs[c+1]*size)<0 )
        c++;
      if( compar(b+idxs[r]*size,b+idxs[c]*size)>=0 )
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
      if( c<i-1 && compar(b+idxs[c]*size,b+idxs[c+1]*size)<0 )
        c++;
      if( compar(b+idxs[r]*size,b+idxs[c]*size)>=0 )
        break;
      swap_idxs( idxs+r,idxs+c );
    }
  }

  return( idxs );
}

static int cmp_merge_allocation( const void *av,const void *bv )
{
  const allocation *a = av;
  const allocation *b = bv;

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

static int cmp_type_allocation( const void *av,const void *bv )
{
  const allocation *a = av;
  const allocation *b = bv;

  if( a->lt>b->lt ) return( 2 );
  if( a->lt<b->lt ) return( -2 );

  if( a->size>b->size ) return( -2 );
  if( a->size<b->size ) return( 2 );

  if( a->ft>b->ft ) return( 2 );
  if( a->ft<b->ft ) return( -2 );

  return( a->id>b->id ? 1 : -1 );
}

static int cmp_frame_allocation( const void *av,const void *bv )
{
  const allocation *a = av;
  const allocation *b = bv;

  if( a->lt>b->lt ) return( 2 );
  if( a->lt<b->lt ) return( -2 );

  uintptr_t *frames1 = (uintptr_t*)a->frames;
  uintptr_t *frames2 = (uintptr_t*)b->frames;
  int c1 = a->frameCount;
  int c2 = b->frameCount;
  int c = c1<c2 ? c1 : c2;
  frames1 += c1 - c;
  frames2 += c2 - c;
  int i;
  for( i=c-1; i>=0 && frames1[i]==frames2[i]; i-- );
  if( i>=0 ) return( frames1[i]>frames2[i] ? 1 : -1 );
  if( c1!=c2 ) return( c1>c2 ? 1 : -1 );

  if( a->size>b->size ) return( -2 );
  if( a->size<b->size ) return( 2 );

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

typedef struct stackGroup
{
  unsigned char stackStart;
  unsigned char stackCount;
  unsigned char stackIndent;
  int allocStart;
  int allocCount;
  int allocSum;
  size_t allocSumSize;
  struct stackGroup *child_a;
  int *childSorted_a;
  int child_q;
  size_t id;
}
stackGroup;

static void stackChildGrouping(
    allocation *alloc_a,int *alloc_idxs,int alloc_q,
    int alloc_s,HANDLE heap,stackGroup *sgParent,
    int stackIdx,int stackIndent )
{
  sgParent->child_q++;
  if( !sgParent->child_a )
    sgParent->child_a = HeapAlloc( heap,0,
        sgParent->child_q*sizeof(stackGroup) );
  else
    sgParent->child_a = HeapReAlloc( heap,0,
        sgParent->child_a,sgParent->child_q*sizeof(stackGroup) );
  stackGroup *sg = sgParent->child_a + ( sgParent->child_q - 1 );
  sg->stackStart = stackIdx;
  sg->stackIndent = stackIndent;
  sg->allocStart = alloc_s;
  sg->allocCount = alloc_q;
  sg->allocSum = 0;
  sg->allocSumSize = 0;
  sg->child_a = NULL;
  sg->childSorted_a = NULL;
  sg->child_q = 0;

  int i = 0;
  int startStackIdx = stackIdx++;
  while( i<alloc_q )
  {
    allocation *a = alloc_a + alloc_idxs[i];
    int startIdx = i;
    int fc = a->frameCount;
    if( stackIdx>=fc ) break;
    void *cmpFrame = a->frames[fc-1-stackIdx];

    for( i++; i<alloc_q; i++ )
    {
      a = alloc_a + alloc_idxs[i];
      fc = a->frameCount;
      if( stackIdx>=fc || cmpFrame!=a->frames[fc-1-stackIdx] ) break;
    }

    if( i-startIdx==alloc_q )
    {
      stackIdx++;
      i = 0;
    }
    else
      stackChildGrouping( alloc_a,alloc_idxs+startIdx,i-startIdx,
          alloc_s+startIdx,heap,sg,
          stackIdx,stackIndent+1 );
  }

  allocation *a = alloc_a + alloc_idxs[0];
  sg->stackCount = stackIdx - startStackIdx;
  sg->id = a->id;

  if( stackIdx==a->frameCount )
  {
    int curAllocSum = 0;
    size_t curAllocSumSize = 0;
    for( i=0; i<alloc_q; i++ )
    {
      a = alloc_a + alloc_idxs[i];
      curAllocSum += a->count;
      curAllocSumSize += a->size*a->count;
    }
    sg->allocSum += curAllocSum;
    sg->allocSumSize += curAllocSumSize;
  }

  sgParent->allocSum += sg->allocSum;
  sgParent->allocSumSize += sg->allocSumSize;
}

static int cmp_stack_group( const void *av,const void *bv )
{
  const stackGroup *a = av;
  const stackGroup *b = bv;

  if( a->allocSumSize>b->allocSumSize ) return( -1 );
  if( a->allocSumSize<b->allocSumSize ) return( 1 );

  return( a->id>b->id ? 1 : -1 );
}

static void sortStackGroup( stackGroup *sg,HANDLE heap )
{
  int i;
  stackGroup *child_a = sg->child_a;
  int child_q = sg->child_q;
  for( i=0; i<child_q; i++ )
    sortStackGroup( child_a+i,heap );

  sg->childSorted_a = sort_allocations( child_a,NULL,child_q,
      sizeof(stackGroup),heap,cmp_stack_group );
}

static void printStackGroup( stackGroup *sg,
    allocation *alloc_a,int *alloc_idxs,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    unsigned char **content_ptrs,modInfo *mi_a,int mi_q,dbgsym *ds )
{
  int i;
  stackGroup *child_a = sg->child_a;
  int *childSorted_a = sg->childSorted_a;
  int child_q = sg->child_q;
  for( i=0; i<child_q; i++ )
  {
    int idx = childSorted_a ? childSorted_a[i] : i;
    printStackGroup( child_a+idx,alloc_a,alloc_idxs,
#ifndef NO_THREADNAMES
        threadName_a,threadName_q,
#endif
        content_ptrs,mi_a,mi_q,ds );
  }

  int allocStart = sg->allocStart;
  int allocCount = sg->allocCount;
  int stackIndent = sg->stackIndent;
  allocation *a = alloc_a + alloc_idxs[allocStart];
  textColor *tc = ds->tc;
  options *opt = ds->opt;
  int stackIsPrinted = 0;
  if( sg->stackStart+sg->stackCount==a->frameCount )
  {
    for( i=0; i<allocCount; i++ )
    {
      int idx = alloc_idxs[allocStart+i];
      a = alloc_a + idx;

      size_t combSize = a->size*a->count;
      if( combSize<opt->minLeakSize ) continue;

      int indent = stackIndent + ( allocCount>1 );
      printf( "  %i$W%U B ",indent,a->size );
      if( a->count>1 )
        printf( "* %d = %U B ",a->count,combSize );
      printf( "$N(#%U)",a->id );
      printThreadName( a->threadNameIdx );
      if( allocCount>1 )
        printStackCount( NULL,0,NULL,0,ds,a->ft,indent );
      else
      {
        printStackCount(
            a->frames+(a->frameCount-(sg->stackStart+sg->stackCount)),
            sg->stackCount,mi_a,mi_q,ds,a->ft,stackIndent );
        stackIsPrinted = 1;
      }
      if( content_ptrs && a->size )
      {
        int s = a->size<(size_t)opt->leakContents ?
          (int)a->size : opt->leakContents;
        char text[5] = { 0,0,0,0,0 };
        unsigned char *content = content_ptrs[idx];
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
          printf( "  %i      $I%s$N ",indent,text );
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
  if( allocCount>1 )
  {
    int indent = stackIndent + 1;
    printf( "  %i$Wsum: %U B / %d\n",indent,sg->allocSumSize,sg->allocSum );
  }
  if( !stackIsPrinted )
    printStackCount( a->frames+(a->frameCount-(sg->stackStart+sg->stackCount)),
        sg->stackCount,mi_a,mi_q,ds,FT_COUNT,stackIndent );
}

static int printStackGroupXml( stackGroup *sg,
    allocation *alloc_a,int *alloc_idxs,int alloc_q,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    modInfo *mi_a,int mi_q,dbgsym *ds,const char **leakTypeNames,
    int xmlRecordNum )
{
  int i;
  stackGroup *child_a = sg->child_a;
  int *childSorted_a = sg->childSorted_a;
  int child_q = sg->child_q;
  for( i=0; i<child_q; i++ )
  {
    int idx = childSorted_a ? childSorted_a[i] : i;
    xmlRecordNum = printStackGroupXml( child_a+idx,alloc_a,alloc_idxs,alloc_q,
#ifndef NO_THREADNAMES
        threadName_a,threadName_q,
#endif
        mi_a,mi_q,ds,leakTypeNames,xmlRecordNum );
  }

  int allocStart = sg->allocStart;
  int allocCount = sg->allocCount;
  allocation *a = alloc_a + alloc_idxs[allocStart];
  const char *xmlLeakTypeNames[LT_COUNT] = {
    "Leak_DefinitelyLost",
    "Leak_DefinitelyLost",
    "Leak_IndirectlyLost",
    "Leak_StillReachable",
    "Leak_StillReachable",
  };
  textColor *tc = ds->tc;
  size_t minLeakSize = ds->opt->minLeakSize;
  if( sg->stackStart+sg->stackCount==a->frameCount )
  {
    for( i=0; i<allocCount; i++ )
    {
      int idx = alloc_idxs[allocStart+i];
      a = alloc_a + idx;

      xmlRecordNum++;

      size_t combSize = a->size*a->count;
      if( combSize<minLeakSize ) continue;

      printf( "<error>\n" );
      printf( "  <unique>%X</unique>\n",a->id );
#ifndef NO_THREADNAMES
      int threadNameIdx = a->threadNameIdx;
      if( threadNameIdx>0 && threadNameIdx<=threadName_q )
        printf( "  <threadname>%s</threadname>\n",
            threadName_a[threadNameIdx-1].name );
      else if( threadNameIdx<0 )
      {
        unsigned unnamedIdx = -threadNameIdx;
        printf( "  <tid>%u</tid>\n",unnamedIdx );
      }
#endif
      printf( "  <kind>%s</kind>\n",xmlLeakTypeNames[a->lt] );
      printf( "  <xwhat>\n" );
      printf( "    <text>%U bytes in %d blocks are %s"
          " in loss record %d of %d</text>\n",
          combSize,a->count,leakTypeNames[a->lt],xmlRecordNum,alloc_q );
      printf( "    <leakedbytes>%U</leakedbytes>\n",a->size );
      printf( "    <leakedblocks>%d</leakedblocks>\n",a->count );
      printf( "  </xwhat>\n" );
      printf( "  <stack>\n" );
      printStackCount( a->frames,a->frameCount,mi_a,mi_q,ds,a->ft,-1 );
      printf( "  </stack>\n" );
      printf( "</error>\n\n" );
    }
  }

  return( xmlRecordNum );
}

static void freeStackGroup( stackGroup *sg,HANDLE heap )
{
  int i;
  stackGroup *child_a = sg->child_a;
  int child_q = sg->child_q;
  for( i=0; i<child_q; i++ )
    freeStackGroup( child_a+i,heap );
  if( child_a )
    HeapFree( heap,0,child_a );
  if( sg->childSorted_a )
    HeapFree( heap,0,sg->childSorted_a );
}

static void printLeaks( allocation *alloc_a,int alloc_q,
    unsigned char **content_ptrs,modInfo *mi_a,int mi_q,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    options *opt,textColor *tc,dbgsym *ds,HANDLE heap,textColor *tcXml,
    uintptr_t threadInitAddr )
{
  if( !tc->out && !tcXml ) return;

  printf( "\n" );
  if( opt->handleException>=2 )
    return;

  if( alloc_q>0 && alloc_a[alloc_q-1].at==AT_EXIT )
    alloc_q--;

  if( !alloc_q )
  {
    printf( "$Ono leaks found\n" );
    return;
  }

  int i;
  int leakDetails = opt->leakDetails;
  size_t sumSize = 0;
  int combined_q = alloc_q;
  int *alloc_idxs =
    leakDetails ? HeapAlloc( heap,0,alloc_q*sizeof(int) ) : NULL;
  for( i=0; i<alloc_q; i++ )
  {
    sumSize += alloc_a[i].size;
    alloc_a[i].count = 1;
    if( alloc_idxs )
      alloc_idxs[i] = i;
  }
  if( opt->groupLeaks && leakDetails )
  {
    sort_allocations( alloc_a,alloc_idxs,alloc_q,sizeof(allocation),
        heap,cmp_merge_allocation );
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
  int l;
  int lMax = leakDetails>1 ? LT_COUNT : 1;
  int lDetails = leakDetails>1 ? ( leakDetails&1 ? LT_COUNT : LT_REACHABLE ) :
    ( leakDetails ? 1 : 0 );
  stackGroup *sg_a =
    HeapAlloc( heap,HEAP_ZERO_MEMORY,lMax*sizeof(stackGroup) );
  const char *leakTypeNames[LT_COUNT] = {
    "lost",
    "jointly lost",
    "indirectly lost",
    "reachable",
    "indirectly reachable",
  };
  const char **leakTypeNamesRef = leakDetails>1 ? leakTypeNames : NULL;
  if( leakDetails )
  {
    for( i=0; i<combined_q; i++ )
    {
      allocation *a = alloc_a + alloc_idxs[i];
      a->size *= a->count;

      uintptr_t *frames = (uintptr_t*)a->frames;
      int c;
      for( c=0; c<PTRS && frames[c] && frames[c]!=threadInitAddr; c++ )
        frames[c]--;
      a->frameCount = c;
    }
    if( opt->groupLeaks>1 )
      sort_allocations( alloc_a,alloc_idxs,combined_q,sizeof(allocation),
          heap,cmp_frame_allocation );
    else
      sort_allocations( alloc_a,alloc_idxs,combined_q,sizeof(allocation),
          heap,cmp_type_allocation );
    for( i=0; i<combined_q; i++ )
      alloc_a[alloc_idxs[i]].size /= alloc_a[alloc_idxs[i]].count;

    if( opt->groupLeaks>1 )
    {
      for( l=0,i=0; l<lDetails; l++ )
      {
        stackGroup *sg = sg_a + l;
        while( i<combined_q )
        {
          allocation *a = alloc_a + alloc_idxs[i];
          leakType lt = a->lt;
          if( lt!=(leakType)l ) break;
          int startIdx = i;
          int fc = a->frameCount;
          void *cmpFrame = fc ? a->frames[fc-1] : NULL;

          for( i++; i<combined_q; i++ )
          {
            a = alloc_a + alloc_idxs[i];
            if( lt!=a->lt ) break;
            fc = a->frameCount;
            void *frame = fc ? a->frames[fc-1] : NULL;
            if( cmpFrame!=frame ) break;
          }
          stackChildGrouping( alloc_a,alloc_idxs+startIdx,i-startIdx,
              startIdx,heap,sg,
              0,0 );
        }
        sortStackGroup( sg,heap );
      }
    }
    else
    {
      for( l=0,i=0; l<lDetails; l++ )
      {
        stackGroup *sg = sg_a + l;
        int startI = i;
        for( ; i<combined_q && alloc_a[alloc_idxs[i]].lt==l; i++ );
        int countI = i - startI;
        sg->child_q = countI;
        sg->child_a = HeapAlloc(
            heap,HEAP_ZERO_MEMORY,countI*sizeof(stackGroup) );
        int curAllocSum = 0;
        size_t curAllocSumSize = 0;
        for( i=0; i<countI; i++ )
        {
          int idx = startI + i;
          allocation *a = alloc_a + alloc_idxs[idx];
          stackGroup *sgChild = sg->child_a + i;
          sgChild->stackCount = a->frameCount;
          sgChild->allocStart = idx;
          sgChild->allocCount = 1;
          sgChild->allocSum = a->count;
          sgChild->allocSumSize = a->size*a->count;
          sgChild->id = a->id;

          curAllocSum += sgChild->allocSum;
          curAllocSumSize += sgChild->allocSumSize;
        }
        sg->allocSum = curAllocSum;
        sg->allocSumSize = curAllocSumSize;
        i = startI + countI;
      }
    }
  }

  if( lDetails==lMax )
    i = combined_q;
  else
  {
    for( i=0; i<combined_q; i++ )
    {
      int idx = alloc_idxs ? alloc_idxs[i] : i;
      if( alloc_a[idx].lt>=lDetails ) break;
    }
  }
  cacheSymbolData( alloc_a,alloc_idxs,i,mi_a,mi_q,ds,0 );

  for( l=lDetails,i=0; l<lMax; l++ )
  {
    stackGroup *sg = sg_a + l;
    for( ; i<combined_q; i++ )
    {
      int idx = alloc_idxs ? alloc_idxs[i] : i;
      allocation *a = alloc_a + idx;
      if( l>a->lt ) continue;
      if( l<a->lt ) break;
      sg->allocSum += a->count;
      sg->allocSumSize += a->size*a->count;
    }
  }
  int xmlRecordNum = 0;
  for( l=0; l<lMax; l++ )
  {
    stackGroup *sg = sg_a + l;
    if( sg->allocSum && tc->out )
    {
      printf( "$Sleaks" );
      if( leakTypeNamesRef )
        printf( " (%s)",leakTypeNamesRef[l] );
      printf( ":\n" );
      if( l<lDetails )
        printStackGroup( sg,alloc_a,alloc_idxs,
#ifndef NO_THREADNAMES
            threadName_a,threadName_q,
#endif
            content_ptrs,mi_a,mi_q,ds );
      printf( "  $Wsum: %U B / %d\n",sg->allocSumSize,sg->allocSum );
    }
    if( tcXml && l<lDetails )
    {
      textColor *tcOrig = tc;
      ds->tc = tcXml;
      xmlRecordNum = printStackGroupXml( sg,alloc_a,alloc_idxs,combined_q,
#ifndef NO_THREADNAMES
          threadName_a,threadName_q,
#endif
          mi_a,mi_q,ds,leakTypeNames,xmlRecordNum );
      ds->tc = tcOrig;
    }
    freeStackGroup( sg,heap );
  }
  if( alloc_idxs )
    HeapFree( heap,0,alloc_idxs );
  HeapFree( heap,0,sg_a );
}

// }}}
// information of attached process {{{

static void printAttachedProcessInfo(
    const wchar_t *exePath,attachedProcessInfo *api,textColor *tc,DWORD pid )
{
  if( !api ) return;
  printf( "\n$Iapplication: $N%S\n",exePath );
  if( api->commandLine[0] )
    printf( "$Icommand line: $N%S\n",api->commandLine );
  if( api->currentDirectory[0] )
    printf( "$Idirectory: $N%S\n",api->currentDirectory );
  printf( "$IPID: $N%u\n",pid );
  if( api->stdinName[0] )
    printf( "$Istdin: $N%S\n",api->stdinName );
  if( api->stdoutName[0] )
    printf( "$Istdout: $N%S\n",api->stdoutName );
  if( api->stderrName[0] )
    printf( "$Istderr: $N%S\n",api->stderrName );
  printf( "\n" );
}

// }}}
// common options {{{

char *readOption( char *args,options *opt,int *raq,size_t **raa,HANDLE heap )
{
  if( !args || args[0]!='-' ) return( NULL );

  int raise_alloc_q = *raq;
  size_t *raise_alloc_a = *raa;

  switch( args[1] )
  {
    case 'p':
      opt->protect = atoi( args+2 );
      if( opt->protect<0 ) opt->protect = 0;
      break;

    case 'a':
      {
        int align = atoi( args+2 );
        if( align>0 && !(align&(align-1)) )
          opt->align = align;
      }
      break;

    case 'i':
      {
        const char *pos = args + 2;
        uint64_t init = atou64( pos );
        int initSize = 1;
        while( *pos && *pos!=' ' && *pos!=':' ) pos++;
        if( *pos==':' )
          initSize = atoi( pos+1 );
        if( initSize<2 )
          init = init | ( init<<8 );
        if( initSize<4 )
          init = init | ( init<<16 );
        if( initSize<8 )
          init = init | ( init<<32 );
        opt->init = init;
      }
      break;

    case 's':
      opt->slackInit = args[2]=='-' ? -atoi( args+3 ) : atoi( args+2 );
      if( opt->slackInit>0xff ) opt->slackInit &= 0xff;
      break;

    case 'f':
      opt->protectFree = atoi( args+2 );
      break;

    case 'h':
      opt->handleException = atoi( args+2 );
      break;

    case 'F':
      opt->fullPath = atoi( args+2 );
      break;

    case 'm':
      opt->allocMethod = atoi( args+2 );
      break;

    case 'l':
      opt->leakDetails = atoi( args+2 );
      break;

    case 'S':
      opt->useSp = atoi( args+2 );
      break;

    case 'd':
      opt->dlls = atoi( args+2 );
      break;

    case 'P':
      opt->pid = atoi( args+2 );
      break;

    case 'e':
      opt->exitTrace = atoi( args+2 );
      break;

    case 'C':
      opt->sourceCode = atoi( args+2 );
      break;

    case 'r':
      opt->raiseException = atoi( args+2 );
      break;

    case 'M':
      opt->minProtectSize = atoi( args+2 );
      if( opt->minProtectSize<1 ) opt->minProtectSize = 1;
      break;

    case 'n':
      opt->findNearest = atoi( args+2 );
      break;

    case 'L':
      opt->leakContents = atoi( args+2 );
      break;

    case 'g':
      opt->groupLeaks = atoi( args+2 );
      break;

    case 'z':
      opt->minLeakSize = atop( args+2 );
      break;

    case 'k':
      opt->leakRecording = atoi( args+2 );
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

    default:
      return( NULL );
  }
  while( args[0] && args[0]!=' ' ) args++;

  *raq = raise_alloc_q;
  *raa = raise_alloc_a;

  return( args );
}

// }}}
// main {{{

void mainCRTStartup( void )
{
  DWORD startTicks = GetTickCount();
  HANDLE errorPipe = openErrorPipe();

  // command line arguments {{{
  char *cmdLine = GetCommandLineA();
  char *args;
  if( cmdLine[0]=='"' && (args=strchr(cmdLine+1,'"')) )
    args++;
  else
    args = strchr( cmdLine,' ' );
  options defopt = {
    1,                              // page protection
    MEMORY_ALLOCATION_ALIGNMENT,    // alignment
    0xffffffffffffffffULL,          // initial value
    0xcc,                           // initial value for slack
    0,                              // freed memory protection
    1,                              // handle exceptions
    0,                              // create new console
    0,                              // show full path
    0,                              // compare allocation/release method
    1,                              // show leak details
    0,                              // use stack pointer in exception
    3,                              // monitor dlls
    0,                              // show process ID and wait
    0,                              // show exit trace
    0,                              // show source code
    0,                              // raise breakpoint exception on error
    1,                              // minimum page protection size
    1,                              // find nearest allocation
    0,                              // show leak contents
    1,                              // group identical leaks
    0,                              // minimum leak size
    0,                              // control leak recording
    0,                              // attach to thread
    0,                              // hook children
    0,                              // use leak and error count for exit code
  };
  options opt = defopt;
  HANDLE heap = GetProcessHeap();
  int raise_alloc_q = 0;
  size_t *raise_alloc_a = NULL;
  char *outName = NULL;
  modInfo *a2l_mi_a = NULL;
  int a2l_mi_q = 0;
  int fullhelp = 0;
  char badArg = 0;
  char *xmlName = NULL;
  PROCESS_INFORMATION pi;
  RtlZeroMemory( &pi,sizeof(PROCESS_INFORMATION) );
  HANDLE attachEvent = NULL;
  int fakeAttached = 0;
  char *specificOptions = NULL;
  while( args )
  {
    while( args[0]==' ' ) args++;
    if( args[0]!='-' ) break;
    char *ro = readOption( args,&opt,&raise_alloc_q,&raise_alloc_a,heap );
    if( ro )
    {
      args = ro;
      continue;
    }
    switch( args[1] )
    {
      case 'c':
        opt.newConsole = atoi( args+2 );
        break;

      case 'o':
        {
          if( outName ) break;
          char *start = args + 2;
          char *end = start;
          while( *end && *end!=' ' ) end++;
          if( end>start )
          {
            size_t len = end - start;
            outName = HeapAlloc( heap,0,len+1 );
            RtlMoveMemory( outName,start,len );
            outName[len] = 0;
          }
        }
        break;

      case 'x':
        {
          if( xmlName ) break;
          char *start = args + 2;
          char *end = start;
          while( *end && *end!=' ' ) end++;
          if( end>start )
          {
            size_t len = end - start;
            xmlName = HeapAlloc( heap,0,len+1 );
            RtlMoveMemory( xmlName,start,len );
            xmlName[len] = 0;
          }
        }
        break;

      case 'A':
        {
          if( args[2]==' ' )
          {
            fakeAttached = 1;
            break;
          }

          HMODULE ntdll = GetModuleHandle( "ntdll.dll" );
          if( !ntdll ) break;

          func_NtQueryInformationThread *fNtQueryInformationThread =
            (func_NtQueryInformationThread*)GetProcAddress(
                ntdll,"NtQueryInformationThread" );
          if( !fNtQueryInformationThread ) break;

          if( pi.hProcess ) break;
          char *start = args + 2;
          pi.dwThreadId = (DWORD)atop( start );

          pi.hThread = OpenThread(
              STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0x3ff,
              FALSE,pi.dwThreadId );
          if( !pi.hThread ) break;

          THREAD_BASIC_INFORMATION tbi;
          RtlZeroMemory( &tbi,sizeof(THREAD_BASIC_INFORMATION) );
          if( fNtQueryInformationThread(pi.hThread,ThreadBasicInformation,
                &tbi,sizeof(THREAD_BASIC_INFORMATION),NULL)!=0 )
          {
            CloseHandle( pi.hThread );
            pi.hThread = NULL;
            break;
          }
          pi.dwProcessId = (DWORD)(ULONG_PTR)tbi.ClientId.UniqueProcess;

          pi.hProcess = OpenProcess(
              STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0xfff,
              FALSE,pi.dwProcessId );
          if( !pi.hProcess )
          {
            CloseHandle( pi.hThread );
            pi.hThread = NULL;
            break;
          }

          char eventName[32] = "heob.attach.";
          char *end = num2hexstr(
              eventName+lstrlen(eventName),GetCurrentProcessId(),8 );
          end[0] = 0;
          attachEvent = OpenEvent( EVENT_ALL_ACCESS,FALSE,eventName );

          opt.attached = 1;
        }
        break;

      case 'E':
        opt.leakErrorExitCode = atoi( args+2 );
        break;

      case 'O':
        {
          char *optionStart = args + 2;
          char *optionEnd = optionStart;
          while( *optionEnd && *optionEnd!=' ' )
          {
            optionEnd = strchr( optionEnd,':' );
            if( !optionEnd ) break;
            optionEnd = strchr( optionEnd+1,';' );
            if( !optionEnd ) break;
            optionEnd++;
          }
          if( optionEnd && optionEnd>optionStart )
          {
            size_t curLen = specificOptions ? lstrlen( specificOptions ) : 0;
            size_t addLen = optionEnd - optionStart;
            if( !specificOptions )
              specificOptions = HeapAlloc( heap,0,curLen+addLen+1 );
            else
              specificOptions = HeapReAlloc(
                  heap,0,specificOptions,curLen+addLen+1 );
            RtlMoveMemory( specificOptions+curLen,optionStart,addLen );
            specificOptions[curLen+addLen] = 0;
          }
          args = optionEnd;
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
  if( opt.align<MEMORY_ALLOCATION_ALIGNMENT )
  {
    opt.init = 0;
    opt.slackInit = -1;
  }
  HANDLE out = GetStdHandle( STD_OUTPUT_HANDLE );
  if( opt.protect<1 ) opt.protectFree = 0;
  textColor *tcOut = HeapAlloc( heap,0,sizeof(textColor) );
  textColor *tc = tcOut;
  checkOutputVariant( tc,out );

  if( badArg )
  {
    char arg0[2] = { badArg,0 };
    printf( "$Wunknown argument: $I-%s\n",arg0 );

    HeapFree( heap,0,tcOut );
    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    if( a2l_mi_a ) HeapFree( heap,0,a2l_mi_a );
    if( outName ) HeapFree( heap,0,outName );
    if( xmlName ) HeapFree( heap,0,xmlName );
    if( attachEvent )
    {
      SetEvent( attachEvent );
      CloseHandle( attachEvent );
    }
    if( opt.attached )
    {
      CloseHandle( pi.hThread );
      CloseHandle( pi.hProcess );
    }
    if( specificOptions ) HeapFree( heap,0,specificOptions );
    writeCloseErrorPipe( errorPipe,HEOB_BAD_ARG,badArg );
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
    allocation *a = HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(allocation) );

    while( args && args[0]>='0' && args[0]<='9' )
    {
      uintptr_t ptr = atop( args );
      if( ptr && a->frameCount<PTRS )
      {
        a->frames[a->frameCount++] = (void*)ptr;

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

    if( a->frameCount )
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
      cacheSymbolData( a,NULL,1,a2l_mi_a,a2l_mi_q,&ds,0 );
      printStackCount( a->frames,a->frameCount,
          a2l_mi_a,a2l_mi_q,&ds,FT_COUNT,0 );

      dbgsym_close( &ds );
    }

    int fc = a->frameCount;
    HeapFree( heap,0,a );
    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    raise_alloc_a = NULL;
    HeapFree( heap,0,a2l_mi_a );

    if( fc )
    {
      HeapFree( heap,0,tcOut );
      if( outName ) HeapFree( heap,0,outName );
      if( xmlName ) HeapFree( heap,0,xmlName );
      if( attachEvent )
      {
        SetEvent( attachEvent );
        CloseHandle( attachEvent );
      }
      if( opt.attached )
      {
        CloseHandle( pi.hThread );
        CloseHandle( pi.hProcess );
      }
      if( specificOptions ) HeapFree( heap,0,specificOptions );
      writeCloseErrorPipe( errorPipe,HEOB_TRACE,0 );
      ExitProcess( 0 );
    }
    args = NULL;
  }

  if( (!args || !args[0]) && !opt.attached )
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
    if( fullhelp )
      printf( "    $I-A$BX$N    attach to thread\n" );
    printf( "    $I-o$BX$N    heob output"
        " ($I0$N = none, $I1$N = stdout, $I2$N = stderr, $I...$N = file)"
        " [$I%d$N]\n",
        1 );
    if( fullhelp )
      printf( "    $I-x$BX$N    xml output\n" );
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
      printf( "    $I-s$BX$N    initial value for slack"
          " ($I-1$N = off) [$I%d$N]\n",
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
      printf( "    $I-g$BX$N    group identical leaks [$I%d$N]\n",
          defopt.groupLeaks );
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
      printf( "    $I-E$BX$N    "
          "use leak and error count for exit code [$I%d$N]\n",
          defopt.leakErrorExitCode );
      printf( "    $I-O$BA$I:$BO$I; a$Npplication specific $Io$Nptions\n" );
      printf( "    $I-\"$BM$I\"$BB$N  trace mode:"
          " load $Im$Nodule on $Ib$Nase address\n" );
    }
    printf( "    $I-H$N     show full help\n" );
    printf( "\n$Ohe$Nap-$Oob$Nserver " HEOB_VER " ($O" BITS "$Nbit)\n" );
    HeapFree( heap,0,tcOut );
    if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
    if( outName ) HeapFree( heap,0,outName );
    if( xmlName ) HeapFree( heap,0,xmlName );
    if( specificOptions ) HeapFree( heap,0,specificOptions );
    writeCloseErrorPipe( errorPipe,HEOB_HELP,0 );
    ExitProcess( -1 );
  }
  // }}}

  HMODULE ntdll = GetModuleHandle( "ntdll.dll" );
  if( ntdll )
  {
    typedef const char *func_wine_get_version( void );
    func_wine_get_version *fwine_get_version =
      (func_wine_get_version*)GetProcAddress( ntdll,"wine_get_version" );
    if( fwine_get_version )
    {
      printf( "$Wheob does not work with Wine\n" );
      HeapFree( heap,0,tcOut );
      if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
      if( outName ) HeapFree( heap,0,outName );
      if( xmlName ) HeapFree( heap,0,xmlName );
      if( specificOptions ) HeapFree( heap,0,specificOptions );
      writeCloseErrorPipe( errorPipe,HEOB_WRONG_BITNESS,0 );
      ExitProcess( -1 );
    }
  }

  HANDLE in = GetStdHandle( STD_INPUT_HANDLE );
  if( !FlushConsoleInputBuffer(in) ) in = NULL;
  if( !in && (opt.attached || opt.newConsole<=1) )
    opt.pid = opt.leakRecording = 0;

  if( opt.leakRecording && !opt.newConsole )
    opt.newConsole = 1;

  wchar_t *cmdLineW = GetCommandLineW();
  wchar_t *argsW = cmdLineW;
  if( argsW[0]=='"' )
  {
    argsW++;
    while( argsW[0] && argsW[0]!='"' ) argsW++;
    if( argsW[0]=='"' ) argsW++;
  }
  else
  {
    while( argsW[0] && argsW[0]!=' ' ) argsW++;
  }
  while( 1 )
  {
    while( argsW[0]==' ' ) argsW++;
    if( argsW[0]!='-' ) break;
    if( argsW[1]=='O' )
    {
      argsW += 2;
      while( *argsW && *argsW!=' ' )
      {
        while( argsW[0] && argsW[0]!=':' ) argsW++;
        while( argsW[0] && argsW[0]!=';' ) argsW++;
        if( argsW[0] ) argsW++;
      }
    }
    while( argsW[0] && argsW[0]!=' ' ) argsW++;
  }

  if( !opt.attached )
  {
    STARTUPINFOW si;
    RtlZeroMemory( &si,sizeof(STARTUPINFO) );
    si.cb = sizeof(STARTUPINFO);
    BOOL result = CreateProcessW( NULL,argsW,NULL,NULL,FALSE,
        CREATE_SUSPENDED|(opt.newConsole&1?CREATE_NEW_CONSOLE:0),
        NULL,NULL,&si,&pi );
    if( !result )
    {
      printf( "$Wcan't create process for '%s'\n",args );
      HeapFree( heap,0,tcOut );
      if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
      if( outName ) HeapFree( heap,0,outName );
      if( xmlName ) HeapFree( heap,0,xmlName );
      if( specificOptions ) HeapFree( heap,0,specificOptions );
      writeCloseErrorPipe( errorPipe,HEOB_PROCESS_FAIL,0 );
      ExitProcess( -1 );
    }

    if( opt.newConsole>1 )
    {
      HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );
      func_CreateProcessA *fCreateProcessA =
        (func_CreateProcessA*)GetProcAddress( kernel32,"CreateProcessA" );
      DWORD exitCode = 0;
      if( !heobSubProcess(0,&pi,NULL,heap,&opt,fCreateProcessA,
            outName,xmlName,NULL,raise_alloc_q,raise_alloc_a,specificOptions) )
      {
        printf( "$Wcan't create process for 'heob'\n" );
        TerminateProcess( pi.hProcess,1 );
        exitCode = -1;
      }
      else if( !(opt.newConsole&1) )
      {
        WaitForSingleObject( pi.hProcess,INFINITE );
        GetExitCodeProcess( pi.hProcess,&exitCode );
      }

      CloseHandle( pi.hThread );
      CloseHandle( pi.hProcess );
      HeapFree( heap,0,tcOut );
      if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
      if( outName ) HeapFree( heap,0,outName );
      if( xmlName ) HeapFree( heap,0,xmlName );
      if( specificOptions ) HeapFree( heap,0,specificOptions );
      writeCloseErrorPipe( errorPipe,HEOB_CONSOLE,0 );
      ExitProcess( exitCode );
    }

    opt.attached = fakeAttached;
  }
  else
    opt.newConsole = 0;

  char exePath[MAX_PATH];
  // executable name {{{
  exePath[0] = 0;
  if( specificOptions ||
      (outName && strstr(outName,"%n")) ||
      (xmlName && strstr(xmlName,"%n")) )
  {
    func_NtQueryInformationProcess *fNtQueryInformationProcess =
      ntdll ? (func_NtQueryInformationProcess*)GetProcAddress(
          ntdll,"NtQueryInformationProcess" ) : NULL;
    if( fNtQueryInformationProcess )
    {
      OBJECT_NAME_INFORMATION *oni =
        HeapAlloc( heap,0,sizeof(OBJECT_NAME_INFORMATION) );
      if( oni )
      {
        ULONG len;
        if( !fNtQueryInformationProcess(pi.hProcess,ProcessImageFileName,
              oni,sizeof(OBJECT_NAME_INFORMATION),&len) )
        {
          oni->Name.Buffer[oni->Name.Length/2] = 0;
          wchar_t *lastDelim = NULL;
          wchar_t *pathPos;
          for( pathPos=oni->Name.Buffer; *pathPos; pathPos++ )
            if( *pathPos=='\\' ) lastDelim = pathPos;
          if( lastDelim ) lastDelim++;
          else lastDelim = oni->Name.Buffer;
          int count = WideCharToMultiByte( CP_ACP,0,
              lastDelim,-1,exePath,MAX_PATH,NULL,NULL );
          if( count<0 || count>=MAX_PATH )
            count = 0;
          exePath[count] = 0;
          char *lastPoint = strrchr( exePath,'.' );
          if( lastPoint ) lastPoint[0] = 0;
        }
        HeapFree( heap,0,oni );
      }
    }
  }
  // }}}

  defopt = opt;
  if( specificOptions )
  {
    int nameLen = lstrlen( exePath );
    char *name = specificOptions;
    char *so = NULL;
    lstrcpy( exePath+nameLen,":" );
    while( 1 )
    {
      char *nameEnd = strchr( name,':' );
      if( !nameEnd ) break;
      if( strstart(name,exePath) )
        so = name + nameLen + 1;
      name = strchr( nameEnd+1,';' );
      if( !name ) break;
      name++;
    }
    exePath[nameLen] = 0;
    while( so )
    {
      while( so[0]==' ' ) so++;
      if( so[0]!='-' ) break;
      so = readOption( so,&opt,&raise_alloc_q,&raise_alloc_a,heap );
    }
  }

  const char *subOutName = NULL;
  textColor *tcOutOrig = NULL;
  if( outName )
  {
    if( (outName[0]>='0' && outName[0]<='2') && !outName[1] )
    {
      out = outName[0]=='0' ? NULL : GetStdHandle(
          outName[0]=='1' ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
      checkOutputVariant( tc,out );
    }
    else
    {
      char *fullName = strreplacenum( outName,"%p",pi.dwProcessId,heap );
      char *usedName;
      if( !fullName )
        usedName = outName;
      else
      {
        usedName = fullName;
        subOutName = outName;
        opt.children = 1;
      }

      char *replaced = strreplace( usedName,"%n",exePath,heap );
      if( replaced )
        usedName = replaced;

      out = CreateFile( usedName,GENERIC_WRITE,FILE_SHARE_READ,
          NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
      if( out==INVALID_HANDLE_VALUE ) out = tc->out;

      if( fullName ) HeapFree( heap,0,fullName );
      if( replaced ) HeapFree( heap,0,replaced );
    }
    if( out!=tc->out )
    {
      tcOutOrig = tcOut;
      tc = tcOut = HeapAlloc( heap,0,sizeof(textColor) );
      checkOutputVariant( tc,out );
    }
  }
  else if( xmlName )
    out = tc->out = NULL;
  if( !tc->out && !tcOutOrig && opt.attached )
  {
    tcOutOrig = HeapAlloc( heap,0,sizeof(textColor) );
    checkOutputVariant( tcOutOrig,GetStdHandle(STD_OUTPUT_HANDLE) );
  }
  if( !out )
    opt.sourceCode = opt.leakContents = 0;

  const char *subXmlName = NULL;
  if( xmlName && strstr(xmlName,"%p") )
  {
    subXmlName = xmlName;
    opt.children = 1;
  }

  char *subCurDir = NULL;
  if( opt.children )
  {
    subCurDir = HeapAlloc( heap,0,MAX_PATH );
    if( !GetCurrentDirectory(MAX_PATH,subCurDir) )
      subCurDir[0] = 0;
  }

  if( opt.leakRecording )
  {
    DWORD flags;
    if( GetConsoleMode(in,&flags) )
      SetConsoleMode( in,flags & ~ENABLE_MOUSE_INPUT );
  }

  HANDLE readPipe = NULL;
  HANDLE controlPipe = NULL;
  HANDLE err = GetStdHandle( STD_ERROR_HANDLE );
  attachedProcessInfo *api = NULL;
  unsigned heobExit = HEOB_OK;
  unsigned heobExitData = 0;
  wchar_t *exePathW = HeapAlloc( heap,0,MAX_PATH*2 );
  if( isWrongArch(pi.hProcess) )
  {
    printf( "$Wonly " BITS "bit applications possible\n" );
    heobExit = HEOB_WRONG_BITNESS;
  }
  else
    readPipe = inject( pi.hProcess,pi.hThread,&opt,&defopt,specificOptions,
        exePathW,tc,raise_alloc_q,raise_alloc_a,&controlPipe,in,err,&api,
        subOutName,subXmlName,subCurDir,&heobExit );
  if( !readPipe )
    TerminateProcess( pi.hProcess,1 );

  UINT exitCode = -1;
  if( readPipe )
  {
    int count = WideCharToMultiByte( CP_ACP,0,
        exePathW,-1,exePath,MAX_PATH,NULL,NULL );
    if( count<0 || count>=MAX_PATH ) count = 0;
    exePath[count] = 0;
    char *delim = strrchr( exePath,'\\' );
    if( delim ) delim[0] = 0;
    dbgsym ds;
    dbgsym_init( &ds,pi.hProcess,tc,&opt,funcnames,heap,exePath,TRUE,
        RETURN_ADDRESS() );
    if( delim ) delim[0] = '\\';

    printAttachedProcessInfo( exePathW,api,tc,pi.dwProcessId );
    if( tcOutOrig )
      printAttachedProcessInfo( exePathW,api,tcOutOrig,pi.dwProcessId );

    if( opt.pid )
    {
      tc->out = err;
      printf( "\n-------------------- PID %u --------------------\n",
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

    // xml header {{{
    textColor *tcXml = NULL;
    if( xmlName )
    {
      char *fullName = strreplacenum( xmlName,"%p",pi.dwProcessId,heap );
      if( !fullName )
      {
        fullName = xmlName;
        xmlName = NULL;
      }

      if( delim ) delim++;
      else delim = exePath;
      char *lastPoint = strrchr( delim,'.' );
      if( lastPoint ) lastPoint[0] = 0;
      char *replaced = strreplace( fullName,"%n",delim,heap );
      if( lastPoint ) lastPoint[0] = '.';
      if( replaced )
      {
        HeapFree( heap,0,fullName );
        fullName = replaced;
      }

      HANDLE xml = CreateFile( fullName,GENERIC_WRITE,FILE_SHARE_READ,
          NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
      if( xml!=INVALID_HANDLE_VALUE )
      {
        tcXml = HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(textColor) );
        tcXml->fWriteText = &WriteText;
        tcXml->fWriteSubText = &WriteTextHtml;
        tcXml->fWriteSubTextW = &WriteTextHtmlW;
        tcXml->fTextColor = NULL;
        tcXml->out = xml;
        tcXml->color = ATT_NORMAL;
      }

      HeapFree( heap,0,fullName );
    }

    if( tcXml )
    {
      tc = tcXml;

      printf( "<?xml version=\"1.0\"?>\n\n" );
      printf( "<valgrindoutput>\n\n" );
      printf( "<protocolversion>4</protocolversion>\n" );
      printf( "<protocoltool>memcheck</protocoltool>\n\n" );
      printf( "<preamble>\n" );
      printf( "  <line>heap-observer " HEOB_VER " (" BITS "bit)</line>\n" );
      if( api )
      {
        printf( "  <line>application: %S</line>\n",exePathW );
        if( api->commandLine[0] )
          printf( "  <line>command line: %S</line>\n",api->commandLine );
        if( api->currentDirectory[0] )
          printf( "  <line>directory: %S</line>\n",api->currentDirectory );
        if( api->stdinName[0] )
          printf( "  <line>stdin: %S</line>\n",api->stdinName );
        if( api->stdoutName[0] )
          printf( "  <line>stdout: %S</line>\n",api->stdoutName );
        if( api->stderrName[0] )
          printf( "  <line>stderr: %S</line>\n",api->stderrName );
      }
      printf( "</preamble>\n\n" );
      printf( "<pid>%u</pid>\n<ppid>%u</ppid>\n<tool>heob</tool>\n\n",
          pi.dwProcessId,GetCurrentProcessId() );

      const wchar_t *argva[2] = { cmdLineW,argsW };
      if( api ) argva[1] = api->commandLine;
      int l = (int)( argsW - cmdLineW );
      while( l>0 && cmdLineW[l-1]==' ' ) l--;
      int argvl[2] = { l,lstrlenW(argva[1]) };
      printf( "<args>\n" );
      for( l=0; l<2; l++ )
      {
        const char *argvstr = l ? "argv" : "vargv";
        const wchar_t *argv = argva[l];
        int argl = argvl[l];
        printf( "  <%s>\n",argvstr );
        int i = 0;
        while( i<argl )
        {
          int startI = i;
          while( i<argl && argv[i]!=' ' )
          {
            wchar_t c = argv[i];
            i++;
            if( c=='"' )
            {
              while( i<argl && argv[i]!='"' ) i++;
              if( i<argl && argv[i]=='"' ) i++;
            }
          }
          if( i>startI )
          {
            const char *argstr = startI ? "arg" : "exe";
            printf( "    <%s>",argstr );
            tc->fWriteSubTextW( tc,argv+startI,i-startI );
            printf( "</%s>\n",argstr );
          }
          while( i<argl && argv[i]==' ' ) i++;
        }
        printf( "  </%s>\n",argvstr );
      }
      printf( "</args>\n\n" );

      printf( "<status>\n  <state>RUNNING</state>\n"
          "  <time>%t</time>\n</status>\n\n",
          GetTickCount()-startTicks );

      tc = tcOut;
    }
    // }}}

    ResumeThread( pi.hThread );

    if( attachEvent )
    {
      SetEvent( attachEvent );
      CloseHandle( attachEvent );
      attachEvent = NULL;
    }

    // main loop {{{
    int type;
    modInfo *mi_a = NULL;
    int mi_q = 0;
    allocation *alloc_a = NULL;
    int alloc_q = 0;
    int terminated = -2;
    unsigned char *contents = NULL;
    unsigned char **content_ptrs = NULL;
    int alloc_show_q = 0;
    int error_q = 0;
#ifndef NO_THREADNAMES
    int threadName_q = 0;
    threadNameInfo *threadName_a = NULL;
#endif
    if( opt.handleException>=2 ) opt.leakRecording = 0;
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
    allocation *aa = HeapAlloc( heap,0,4*sizeof(allocation) );
    exceptionInfo *eiPtr = HeapAlloc( heap,0,sizeof(exceptionInfo) );
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
        // control leak recording {{{
        INPUT_RECORD ir;
        if( ReadConsoleInput(in,&ir,1,&didread) &&
            ir.EventType==KEY_EVENT &&
            ir.Event.KeyEvent.bKeyDown )
        {
          int cmd = -1;

          switch( ir.Event.KeyEvent.wVirtualKeyCode )
          {
            case 'N':
              if( recording>0 ) break;
              cmd = LEAK_RECORDING_START;
              break;

            case 'F':
              if( recording<=0 ) break;
              cmd = LEAK_RECORDING_STOP;
              break;

            case 'C':
              if( recording<0 ) break;
              cmd = LEAK_RECORDING_CLEAR;
              break;

            case 'S':
              if( recording<0 ) break;
              cmd = LEAK_RECORDING_SHOW;
              break;
          }

          if( cmd>=0 )
            WriteFile( controlPipe,&cmd,sizeof(int),&didread,NULL );
        }
        continue;
        // }}}
      }

      if( in )
        clearRecording( err,consoleCoord,errColor,1 );

      if( !GetOverlappedResult(readPipe,&ov,&didread,TRUE) ||
          didread<sizeof(int) )
        break;
      needData = 1;

      switch( type )
      {
        // leaks {{{

        case WRITE_LEAKS:
          {
            alloc_q = 0;
            if( alloc_a ) HeapFree( heap,0,alloc_a );
            alloc_a = NULL;
            if( contents ) HeapFree( heap,0,contents );
            contents = NULL;
            if( content_ptrs ) HeapFree( heap,0,content_ptrs );
            content_ptrs = NULL;
            alloc_show_q = 0;

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

            int lc;
            int lDetails = opt.leakDetails ?
              ( (opt.leakDetails&1) ? LT_COUNT : LT_REACHABLE ) : 0;
            for( lc=0; lc<alloc_q; lc++ )
            {
              allocation *a = alloc_a + lc;
              if( a->lt>=lDetails ) continue;
              alloc_show_q++;
            }

            if( content_size )
            {
              contents = HeapAlloc( heap,0,content_size );
              if( !readFile(readPipe,contents,content_size,&ov) )
                break;
              content_ptrs =
                HeapAlloc( heap,0,alloc_q*sizeof(unsigned char*) );
              size_t leakContents = opt.leakContents;
              size_t content_pos = 0;
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
              &opt,tc,&ds,heap,tcXml,(uintptr_t)RETURN_ADDRESS() );
          break;

          // }}}
          // modules {{{

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

          // }}}
          // exception {{{

        case WRITE_EXCEPTION:
          {
#define ei (*eiPtr)
            if( !readFile(readPipe,&ei,sizeof(exceptionInfo),&ov) )
              break;

            cacheSymbolData( ei.aa,NULL,ei.aq,mi_a,mi_q,&ds,1 );

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
            printStackCount( ei.aa[0].frames,ei.aa[0].frameCount,
                mi_a,mi_q,&ds,FT_COUNT,0 );

            char *addr = NULL;
            const char *violationType = NULL;
            const char *nearBlock = NULL;
            const char *blockType = NULL;
            if( ei.er.ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
                ei.er.NumberParameters==2 )
            {
              ULONG_PTR flag = ei.er.ExceptionInformation[0];
              addr = (char*)ei.er.ExceptionInformation[1];
              violationType = flag==8 ? "data execution prevention" :
                ( flag ? "write access" : "read access" );
              printf( "$W  %s violation at %p\n",violationType,addr );

              if( ei.aq>1 )
              {
                char *ptr = (char*)ei.aa[1].ptr;
                size_t size = ei.aa[1].size;
                nearBlock = ei.nearest ? "near " : "";
                intptr_t accessPos = addr - ptr;
                blockType = ei.aq>2 ? "freed block" :
                  ( accessPos>=0 && (size_t)accessPos<size ?
                    "accessible (!) area of" : "protected area of" );
                printf( "$I  %s%s %p (size %U, offset %s%D)\n",
                    nearBlock,blockType,
                    ptr,size,accessPos>0?"+":"",accessPos );
                printf( "$S  allocated on: $N(#%U)",ei.aa[1].id );
                printThreadName( ei.aa[1].threadNameIdx );
                printStackCount( ei.aa[1].frames,ei.aa[1].frameCount,
                    mi_a,mi_q,&ds,ei.aa[1].ft,0 );

                if( ei.aq>2 )
                {
                  printf( "$S  freed on:" );
                  printThreadName( ei.aa[2].threadNameIdx );
                  printStackCount( ei.aa[2].frames,ei.aa[2].frameCount,
                      mi_a,mi_q,&ds,ei.aa[2].ft,0 );
                }
              }
            }

            if( tcXml )
            {
              ds.tc = tc = tcXml;

              printf( "<error>\n" );
              printf( "  <kind>InvalidRead</kind>\n" );
              printf( "  <what>unhandled exception code: %x%s</what>",
                  ei.er.ExceptionCode,desc );
              printf( "  <auxwhat>" );
              if( violationType )
                printf( "%s violation at %p</auxwhat>\n  <auxwhat>\n",
                    violationType,addr );
              printf( "exception on</auxwhat>\n" );
              printf( "  <stack>\n" );
              printStackCount( ei.aa[0].frames,ei.aa[0].frameCount,
                  mi_a,mi_q,&ds,FT_COUNT,-1 );
              printf( "  </stack>\n" );

              if( ei.aq>1 )
              {
                char *ptr = (char*)ei.aa[1].ptr;
                size_t size = ei.aa[1].size;
                printf(
                    "  <auxwhat>%s%s %p (size %U, offset %s%D)</auxwhat>\n",
                    nearBlock,blockType,
                    ptr,size,addr>ptr?"+":"",addr-ptr );
                printf( "  <auxwhat>\nallocated on</auxwhat>\n" );
                printf( "  <stack>\n" );
                printStackCount( ei.aa[1].frames,ei.aa[1].frameCount,
                    mi_a,mi_q,&ds,ei.aa[1].ft,-1 );
                printf( "  </stack>\n" );

                if( ei.aq>2 )
                {
                  printf( "  <auxwhat>freed on</auxwhat>\n" );
                  printf( "  <stack>\n" );
                  printStackCount( ei.aa[2].frames,ei.aa[2].frameCount,
                      mi_a,mi_q,&ds,ei.aa[2].ft,-1 );
                  printf( "  </stack>\n" );
                }
              }
              printf( "</error>\n\n" );

              ds.tc = tc = tcOut;
            }

            terminated = -1;
            heobExit = HEOB_EXCEPTION;
            heobExitData = ei.er.ExceptionCode;
#undef ei
          }
          break;

          // }}}
          // allocation failure {{{

        case WRITE_ALLOC_FAIL:
          {
            if( !readFile(readPipe,aa,sizeof(allocation),&ov) )
              break;

            cacheSymbolData( aa,NULL,1,mi_a,mi_q,&ds,1 );

            printf( "\n$Wallocation failed of %U bytes\n",aa->size );
            printf( "$S  called on: $N(#%U)",aa->id );
            printThreadName( aa->threadNameIdx );
            printStackCount( aa->frames,aa->frameCount,
                mi_a,mi_q,&ds,aa->ft,0 );

            if( tcXml )
            {
              ds.tc = tc = tcXml;

              printf( "<error>\n" );
              printf( "  <kind>UninitValue</kind>\n" );
              printf( "  <what>allocation failed of %U bytes</what>\n",
                  aa->size );
              printf( "  <stack>\n" );
              printStackCount( aa->frames,aa->frameCount,
                  mi_a,mi_q,&ds,aa->ft,-1 );
              printf( "  </stack>\n" );
              printf( "</error>\n\n" );

              ds.tc = tc = tcOut;
            }

            error_q++;
          }
          break;

          // }}}
          // free of invalid pointer {{{

        case WRITE_FREE_FAIL:
          {
            if( !readFile(readPipe,aa,4*sizeof(allocation),&ov) )
              break;

            modInfo *allocMi = NULL;
            if( !aa[1].ptr && (aa[1].id==2 || aa[1].id==3) )
            {
              uintptr_t frame = (uintptr_t)aa[1].frames[0];
              int k;
              for( k=0; k<mi_q; k++ )
              {
                modInfo *mi = mi_a + k;
                if( frame<mi->base || frame>=mi->base+mi->size ) continue;
                allocMi = mi;
                break;
              }
            }

            cacheSymbolData( aa,NULL,4,mi_a,mi_q,&ds,1 );

            printf( "\n$Wdeallocation of invalid pointer %p\n",aa->ptr );
            printf( "$S  called on:" );
            printThreadName( aa->threadNameIdx );
            printStackCount( aa->frames,aa->frameCount,
                mi_a,mi_q,&ds,aa->ft,0 );

            if( aa[1].ptr )
            {
              char *ptr = aa->ptr;
              char *addr = aa[1].ptr;
              size_t size = aa[1].size;
              const char *block = aa[2].ptr ? "freed " : "";
              printf( "$I  pointing to %sblock %p (size %U, offset %s%D)\n",
                  block,addr,size,ptr>addr?"+":"",ptr-addr );
              printf( "$S  allocated on: $N(#%U)",aa[1].id );
              printThreadName( aa[1].threadNameIdx );
              printStackCount( aa[1].frames,aa[1].frameCount,
                  mi_a,mi_q,&ds,aa[1].ft,0 );

              if( aa[2].ptr )
              {
                printf( "$S  freed on:" );
                printThreadName( aa[2].threadNameIdx );
                printStackCount( aa[2].frames,aa[2].frameCount,
                    mi_a,mi_q,&ds,aa[2].ft,0 );
              }
            }
            else if( aa[1].id==1 )
            {
              printf( "$I  pointing to stack\n" );
              printf( "$S  possibly same frame as:" );
              printThreadName( aa[1].threadNameIdx );
              printStackCount( aa[1].frames,aa[1].frameCount,
                  mi_a,mi_q,&ds,FT_COUNT,0 );
            }
            else if( aa[1].id==2 )
            {
              printf( "$I  allocated (size %U) from:\n",aa[1].size );
              if( allocMi )
                locOut( tc,allocMi->base,allocMi->path,
                    DWST_BASE_ADDR,0,NULL,ds.opt,0 );
            }
            else if( aa[1].id==3 )
            {
              printf( "$I  pointing to global area of:\n" );
              if( allocMi )
                locOut( tc,allocMi->base,allocMi->path,
                    DWST_BASE_ADDR,0,NULL,ds.opt,0 );
            }

            if( aa[3].ptr )
            {
              printf( "$I  referenced by block %p (size %U, offset +%U)\n",
                  aa[3].ptr,aa[3].size,aa[2].size );
              printf( "$S  allocated on: $N(#%U)",aa[3].id );
              printThreadName( aa[3].threadNameIdx );
              printStackCount( aa[3].frames,aa[3].frameCount,
                  mi_a,mi_q,&ds,aa[3].ft,0 );
            }

            if( tcXml )
            {
              ds.tc = tc = tcXml;

              printf( "<error>\n" );
              printf( "  <kind>InvalidFree</kind>\n" );
              printf( "  <what>deallocation of invalid pointer %p",
                  aa->ptr );
              printf( "</what>\n" );
              printf( "  <stack>\n" );
              printStackCount( aa->frames,aa->frameCount,
                  mi_a,mi_q,&ds,aa->ft,-1 );
              printf( "  </stack>\n" );

              if( aa[1].ptr )
              {
                char *ptr = aa->ptr;
                char *addr = aa[1].ptr;
                size_t size = aa[1].size;
                const char *block = aa[2].ptr ? "freed " : "";
                printf(
                    "  <auxwhat>pointing to %sblock %p"
                    " (size %U, offset %s%D)</auxwhat>\n",
                    block,addr,size,ptr>addr?"+":"",ptr-addr );
                printf( "  <auxwhat>\nallocated on</auxwhat>\n" );
                printf( "  <stack>\n" );
                printStackCount( aa[1].frames,aa[1].frameCount,
                    mi_a,mi_q,&ds,aa[1].ft,-1 );
                printf( "  </stack>\n" );

                if( aa[2].ptr )
                {
                  printf( "  <auxwhat>freed on</auxwhat>\n" );
                  printf( "  <stack>\n" );
                  printStackCount( aa[2].frames,aa[2].frameCount,
                      mi_a,mi_q,&ds,aa[2].ft,-1 );
                  printf( "  </stack>\n" );
                }
              }
              else if( aa[1].id==1 )
              {
                printf( "  <auxwhat>pointing to stack</auxwhat>\n" );
                printf( "  <auxwhat>\npossibly same frame as</auxwhat>\n" );
                printf( "  <stack>\n" );
                printStackCount( aa[1].frames,aa[1].frameCount,
                    mi_a,mi_q,&ds,FT_COUNT,-1 );
                printf( "  </stack>\n" );
              }
              else if( aa[1].id==2 )
              {
                printf( "  <auxwhat>allocated (size %U) from</auxwhat>\n",
                    aa[1].size );
                if( allocMi )
                {
                  printf( "  <stack>\n" );
                  locXml( tc,0,NULL,0,NULL,allocMi );
                  printf( "  </stack>\n" );
                }
              }
              else if( aa[1].id==3 )
              {
                printf( "  <auxwhat>pointing to global area of</auxwhat>\n" );
                if( allocMi )
                {
                  printf( "  <stack>\n" );
                  locXml( tc,0,NULL,0,NULL,allocMi );
                  printf( "  </stack>\n" );
                }
              }

              if( aa[3].ptr )
              {
                printf(
                    "  <auxwhat>referenced by block %p"
                    " (size %U, offset +%U)</auxwhat>\n",
                    aa[3].ptr,aa[3].size,aa[2].size );
                printf( "  <auxwhat>\nallocated on</auxwhat>\n" );
                printf( "  <stack>\n" );
                printStackCount( aa[3].frames,aa[3].frameCount,
                    mi_a,mi_q,&ds,aa[3].ft,-1 );
                printf( "  </stack>\n" );
              }
              printf( "</error>\n\n" );

              ds.tc = tc = tcOut;
            }

            error_q++;
          }
          break;

          // }}}
          // double free {{{

        case WRITE_DOUBLE_FREE:
          {
            if( !readFile(readPipe,aa,3*sizeof(allocation),&ov) )
              break;

            cacheSymbolData( aa,NULL,3,mi_a,mi_q,&ds,1 );

            printf( "\n$Wdouble free of %p (size %U)\n",aa[1].ptr,aa[1].size );
            printf( "$S  called on:" );
            printThreadName( aa[0].threadNameIdx );
            printStackCount( aa[0].frames,aa[0].frameCount,
                mi_a,mi_q,&ds,aa[0].ft,0 );

            printf( "$S  allocated on: $N(#%U)",aa[1].id );
            printThreadName( aa[1].threadNameIdx );
            printStackCount( aa[1].frames,aa[1].frameCount,
                mi_a,mi_q,&ds,aa[1].ft,0 );

            printf( "$S  freed on:" );
            printThreadName( aa[2].threadNameIdx );
            printStackCount( aa[2].frames,aa[2].frameCount,
                mi_a,mi_q,&ds,aa[2].ft,0 );

            if( tcXml )
            {
              ds.tc = tc = tcXml;

              printf( "<error>\n" );
              printf( "  <kind>InvalidFree</kind>\n" );
              printf( "  <what>double free of %p (size %U)</what>\n",
                  aa[1].ptr,aa[1].size );
              printf( "  <auxwhat>called on</auxwhat>\n" );
              printf( "  <stack>\n" );
              printStackCount( aa[0].frames,aa[0].frameCount,
                  mi_a,mi_q,&ds,aa[0].ft,-1 );
              printf( "  </stack>\n" );
              printf( "  <auxwhat>allocated on</auxwhat>\n" );
              printf( "  <stack>\n" );
              printStackCount( aa[1].frames,aa[1].frameCount,
                  mi_a,mi_q,&ds,aa[1].ft,-1 );
              printf( "  </stack>\n" );
              printf( "  <auxwhat>freed on</auxwhat>\n" );
              printf( "  <stack>\n" );
              printStackCount( aa[2].frames,aa[2].frameCount,
                  mi_a,mi_q,&ds,aa[2].ft,-1 );
              printf( "  </stack>\n" );
              printf( "</error>\n\n" );

              ds.tc = tc = tcOut;
            }

            error_q++;
          }
          break;

          // }}}
          // slack access {{{

        case WRITE_SLACK:
          {
            if( !readFile(readPipe,aa,2*sizeof(allocation),&ov) )
              break;

            cacheSymbolData( aa,NULL,2,mi_a,mi_q,&ds,1 );

            printf( "\n$Wwrite access violation at %p\n",aa[1].ptr );
            printf( "$I  slack area of %p (size %U, offset %s%D)\n",
                aa[0].ptr,aa[0].size,
                aa[1].ptr>aa[0].ptr?"+":"",(char*)aa[1].ptr-(char*)aa[0].ptr );
            printf( "$S  allocated on: $N(#%U)",aa[0].id );
            printThreadName( aa[0].threadNameIdx );
            printStackCount( aa[0].frames,aa[0].frameCount,
                mi_a,mi_q,&ds,aa[0].ft,0 );
            printf( "$S  freed on:" );
            printThreadName( aa[1].threadNameIdx );
            printStackCount( aa[1].frames,aa[1].frameCount,
                mi_a,mi_q,&ds,aa[1].ft,0 );

            if( tcXml )
            {
              ds.tc = tc = tcXml;

              printf( "<error>\n" );
              printf( "  <kind>InvalidWrite</kind>\n" );
              printf( "  <what>write access violation at %p</what>\n",
                  aa[1].ptr );
              printf( "  <auxwhat>slack area of %p"
                  " (size %U, offset %s%D)</auxwhat>\n",
                  aa[0].ptr,aa[0].size,
                  aa[1].ptr>aa[0].ptr?"+":"",
                  (char*)aa[1].ptr-(char*)aa[0].ptr );
              printf( "  <auxwhat>\nallocated on</auxwhat>\n" );
              printf( "  <stack>\n" );
              printStackCount( aa[0].frames,aa[0].frameCount,
                  mi_a,mi_q,&ds,aa[0].ft,-1 );
              printf( "  </stack>\n" );
              printf( "  <auxwhat>freed on</auxwhat>\n" );
              printf( "  <stack>\n" );
              printStackCount( aa[1].frames,aa[1].frameCount,
                  mi_a,mi_q,&ds,aa[1].ft,-1 );
              printf( "  </stack>\n" );
              printf( "</error>\n\n" );

              ds.tc = tc = tcOut;
            }

            error_q++;
          }
          break;

          // }}}
          // main allocation failure {{{

        case WRITE_MAIN_ALLOC_FAIL:
          printf( "\n$Wnot enough memory to keep track of allocations\n" );
          terminated = -1;
          heobExit = HEOB_OUT_OF_MEMORY;
          break;

          // }}}
          // mismatching allocation/release method {{{

        case WRITE_WRONG_DEALLOC:
          {
            if( !readFile(readPipe,aa,2*sizeof(allocation),&ov) )
              break;

            cacheSymbolData( aa,NULL,2,mi_a,mi_q,&ds,1 );

            printf( "\n$Wmismatching allocation/release method"
                " of %p (size %U)\n",aa[0].ptr,aa[0].size );
            printf( "$S  allocated on: $N(#%U)",aa[0].id );
            printThreadName( aa[0].threadNameIdx );
            printStackCount( aa[0].frames,aa[0].frameCount,
                mi_a,mi_q,&ds,aa[0].ft,0 );
            printf( "$S  freed on:" );
            printThreadName( aa[1].threadNameIdx );
            printStackCount( aa[1].frames,aa[1].frameCount,
                mi_a,mi_q,&ds,aa[1].ft,0 );

            if( tcXml )
            {
              ds.tc = tc = tcXml;

              printf( "<error>\n" );
              printf( "  <kind>MismatchedFree</kind>\n" );
              printf(
                  "  <what>mismatching allocation/release method</what>\n" );
              printf( "  <auxwhat>allocated on</auxwhat>\n" );
              printf( "  <stack>\n" );
              printStackCount( aa[0].frames,aa[0].frameCount,
                  mi_a,mi_q,&ds,aa[0].ft,-1 );
              printf( "  </stack>\n" );
              printf( "  <auxwhat>freed on</auxwhat>\n" );
              printf( "  <stack>\n" );
              printStackCount( aa[1].frames,aa[1].frameCount,
                  mi_a,mi_q,&ds,aa[1].ft,-1 );
              printf( "  </stack>\n" );
              printf( "</error>\n\n" );

              ds.tc = tc = tcOut;
            }

            error_q++;
          }
          break;

          // }}}
          // exception of allocation # {{{

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

          // }}}
          // thread names {{{

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

          // }}}
          // exit information {{{

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
              cacheSymbolData( exitTrace,NULL,1,mi_a,mi_q,&ds,1 );
              printf( "$Sexit on:" );
              printThreadName( exitTrace->threadNameIdx );
              printStackCount( exitTrace->frames,exitTrace->frameCount,
                  mi_a,mi_q,&ds,FT_COUNT,0 );
            }

            printf( "$Sexit code: %u (%x)\n",exitCode,exitCode );
          }
          else
          {
            printf( "\n$Stermination code: %u (%x)\n",exitCode,exitCode );
          }
          heobExitData = exitCode;
          if( opt.leakErrorExitCode )
            exitCode = alloc_show_q + error_q;
          break;

          // }}}
          // leak recording {{{

        case WRITE_RECORDING:
          {
            int cmd;
            if( !readFile(readPipe,&cmd,sizeof(int),&ov) )
              break;

            switch( cmd )
            {
              case LEAK_RECORDING_START:
                recording = 1;
                break;
              case LEAK_RECORDING_STOP:
                if( recording>0 ) recording = 0;
                break;
              case LEAK_RECORDING_CLEAR:
              case LEAK_RECORDING_SHOW:
                if( !recording ) recording = -1;
                break;
            }
          }
          break;

          // }}}
      }
    }
    CloseHandle( ov.hEvent );
    HeapFree( heap,0,aa );
    HeapFree( heap,0,eiPtr );
    // }}}

    if( terminated==-2 )
    {
      printf( "\n$Wunexpected end of application\n" );
      heobExit = HEOB_UNEXPECTED_END;
    }

    // xml footer {{{
    if( tcXml )
    {
      tc = tcXml;

      printf( "<status>\n  <state>FINISHED</state>\n"
          "  <time>%t</time>\n</status>\n\n",
          GetTickCount()-startTicks );

      printf( "</valgrindoutput>\n" );

      tc = tcOut;

      CloseHandle( tcXml->out );
      HeapFree( heap,0,tcXml );
    }
    // }}}

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
  if( attachEvent )
  {
    SetEvent( attachEvent );
    CloseHandle( attachEvent );
  }
  CloseHandle( pi.hThread );
  CloseHandle( pi.hProcess );

  HeapFree( heap,0,tcOut );
  if( tcOutOrig ) HeapFree( heap,0,tcOutOrig );
  if( raise_alloc_a ) HeapFree( heap,0,raise_alloc_a );
  if( outName ) HeapFree( heap,0,outName );
  if( xmlName ) HeapFree( heap,0,xmlName );
  if( subCurDir ) HeapFree( heap,0,subCurDir );
  if( api ) HeapFree( heap,0,api );
  if( specificOptions ) HeapFree( heap,0,specificOptions );
  HeapFree( heap,0,exePathW );

  writeCloseErrorPipe( errorPipe,heobExit,heobExitData );
  ExitProcess( exitCode );
}

// }}}

// vim:fdm=marker
