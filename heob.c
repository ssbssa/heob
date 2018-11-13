
//          Copyright Hannes Domani 2014 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// includes {{{

#include "heob-internal.h"

#ifndef NO_DWARFSTACK
#include <dwarfstack.h>
#else
#include <stdint.h>
#define DWST_BASE_ADDR   0
#define DWST_NO_DBG_SYM -1
#endif

#ifndef NO_DBGENG
#include <dbgeng.h>
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
  int canWriteWideChar;
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
    // argument {{{
    if( ptr[0]=='%' && ptr[1] )
    {
      // % = argument
      if( ptr>format )
        tc->fWriteText( tc,format,ptr-format );
      switch( ptr[1] )
      {
        // strings {{{

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

          // }}}
          // decimal numbers {{{

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
                  if( ptr[1]=='D' )
                    argi = va_arg( vl,intptr_t );
                  else
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
                arg = va_arg( vl,unsigned int );
                break;
              case 'U':
                arg = va_arg( vl,uintptr_t );
                break;
            }
            char str[32];
            char *end = str + sizeof(str);
            char *start = num2str( end,arg,minus );
            tc->fWriteText( tc,start,end-start );
          }
          break;

          // }}}
          // hexadecimal numbers {{{

        case 'w': // unsigned short (word)
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
            else if( ptr[1]=='X' )
            {
              arg = va_arg( vl,uintptr_t );
              bytes = sizeof(uintptr_t);
            }
            else if( ptr[1]=='w' )
            {
              arg = (unsigned short)va_arg( vl,unsigned int );
              bytes = sizeof(unsigned short);
            }
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

          // }}}
          // indention {{{

        case 'i': // indent
        case 'E': // new entry
        case 'I': // indent level end
          {
            int indent = va_arg( vl,int );
            int i;
            wchar_t firstSpace = ' ';
            wchar_t bar[2] = { ' ','|' };
            wchar_t specialBars[2][2] = { {' ',' '},{' ',' '} };
            int specialCount = 0;
            if( tc->canWriteWideChar )
            {
              const wchar_t barTopBottom =            0x2502;
              const wchar_t barRightBottom =          0x250c;
              const wchar_t barTopRightBottom =       0x251c;
              const wchar_t barTopLeft =              0x2518;
              const wchar_t barLeftRight =            0x2500;
              const wchar_t barRightDoubleTopBottom = 0x255f;
              bar[1] = barTopBottom;
              if( ptr[1]=='E' )
              {
                specialBars[0][1] = barRightBottom;
                specialCount = 1;
              }
              else if( ptr[1]=='I' )
              {
                if( indent==1 )
                  firstSpace = barRightDoubleTopBottom;
                specialBars[0][1] = barTopRightBottom;
                specialBars[1][0] = barLeftRight;
                specialBars[1][1] = barTopLeft;
                specialCount = 2;
              }
            }
            tc->fWriteSubTextW( tc,&firstSpace,1 );
            for( i=0; i<indent; i++ )
            {
              if( tc->fTextColor )
                tc->fTextColor( tc,i%ATT_BASE );

              if( i>=indent-specialCount )
                RtlMoveMemory( bar,specialBars[i-(indent-specialCount)],4 );
              tc->fWriteSubTextW( tc,bar,2 );
            }
            if( tc->fTextColor )
              tc->fTextColor( tc,ATT_NORMAL );
            tc->fWriteText( tc," ",1 );
          }
          break;

          // }}}
          // time {{{

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

          // }}}
          // last-error code {{{

        case 'e': // last-error code
          {
            DWORD e = va_arg( vl,DWORD );
            wchar_t *s = NULL;
            FormatMessageW(
                FORMAT_MESSAGE_ALLOCATE_BUFFER|FORMAT_MESSAGE_FROM_SYSTEM,
                NULL,e,MAKELANGID(LANG_NEUTRAL,SUBLANG_DEFAULT),
                (LPWSTR)&s,0,NULL );
            if( s && s[0] )
            {
              int l = lstrlenW( s );
              while( l && (s[l-1]=='\r' || s[l-1]=='\n') ) l--;
              if( l )
                tc->fWriteSubTextW( tc,s,l );
            }
            LocalFree( s );
          }
          break;

          // }}}
      }
      ptr += 2;
      format = ptr;
      continue;
    }
    // }}}
    // color {{{
    else if( ptr[0]=='$' && ptr[1] )
    {
      // $ = color
      //   $N = normal
      //   $O = ok
      //   $S = section
      //   $I = info
      //   $W = warn
      //   $B = base
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
    // }}}
    // newline {{{
    else if( ptr[0]=='\n' && tc->fTextColor && tc->color!=ATT_NORMAL )
    {
      if( ptr>format )
        tc->fWriteText( tc,format,ptr-format );
      tc->fTextColor( tc,ATT_NORMAL );
      format = ptr;
    }
    // }}}
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

NOINLINE wchar_t *mstrrchrW( const wchar_t *s,wchar_t c )
{
  wchar_t *ret = NULL;
  for( ; *s; s++ )
    if( *s==c ) ret = (wchar_t*)s;
  return( ret );
}
#define strrchrW mstrrchrW

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
  char quot[] = "&quot;";
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
    if( c!='<' && c!='>' && c!='&' && c!='"' ) continue;

    if( next>t )
      WriteFile( tc->out,t,(DWORD)(next-t),&written,NULL );
    if( c=='<' )
      WriteFile( tc->out,lt,sizeof(lt)-1,&written,NULL );
    else if( c=='>' )
      WriteFile( tc->out,gt,sizeof(gt)-1,&written,NULL );
    else if( c=='&' )
      WriteFile( tc->out,amp,sizeof(amp)-1,&written,NULL );
    else
      WriteFile( tc->out,quot,sizeof(quot)-1,&written,NULL );
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
  char quot[] = "&quot;";
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
    else if( c32=='"' )
      WriteFile( tc->out,quot,sizeof(quot)-1,&written,NULL );
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

static void checkOutputVariant( textColor *tc,HANDLE out,const char *exeName )
{
  // default
  tc->fWriteText = &WriteText;
  tc->fWriteSubText = &WriteText;
  tc->fWriteSubTextW = &WriteTextW;
  tc->fTextColor = NULL;
  tc->canWriteWideChar = 0;
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

    // windows console {{{
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( tc->out,&csbi );
    int bg = csbi.wAttributes&0xf0;

    tc->fWriteSubTextW = &WriteTextConsoleW;
    tc->fTextColor = &TextColorConsole;
    tc->canWriteWideChar = 1;

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
      if( tc->colors[i]==bg ) tc->colors[i] ^= 0x08;
    // }}}
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
        // html file {{{
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
          "<title>";
        const char *styleInit1 =
          "heob " HEOB_VER "</title>\n"
          "</head><body>\n"
          "<h3>";
        const char *styleInit2 =
          "</h3>\n"
          "<pre>\n";
        DWORD written;
        WriteFile( tc->out,styleInit,lstrlen(styleInit),&written,NULL );
        if( exeName )
        {
          WriteFile( tc->out,exeName,lstrlen(exeName),&written,NULL );
          WriteFile( tc->out," - ",3,&written,NULL );
        }
        WriteFile( tc->out,styleInit1,lstrlen(styleInit1),&written,NULL );
        const wchar_t *cmdLineW = GetCommandLineW();
        WriteTextHtmlW( tc,cmdLineW,lstrlenW(cmdLineW) );
        WriteFile( tc->out,styleInit2,lstrlen(styleInit2),&written,NULL );

        tc->fWriteText = &WriteTextHtml;
        tc->fWriteSubText = &WriteTextHtml;
        tc->fWriteSubTextW = &WriteTextHtmlW;
        tc->fTextColor = &TextColorHtml;
        tc->canWriteWideChar = 1;

        tc->styles[ATT_NORMAL]  = NULL;
        tc->styles[ATT_OK]      = "ok";
        tc->styles[ATT_SECTION] = "section";
        tc->styles[ATT_INFO]    = "info";
        tc->styles[ATT_WARN]    = "warn";
        tc->styles[ATT_BASE]    = "base";
        // }}}
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

static HANDLE openErrorPipe( int *writeProcessPid )
{
  char errorPipeName[32] = "\\\\.\\Pipe\\heob.error.";
  char *end = num2hexstr( errorPipeName+lstrlen(errorPipeName),
      GetCurrentProcessId(),8 );
  end[0] = 0;

  *writeProcessPid = 0;
  HANDLE errorPipe = CreateFile( errorPipeName,GENERIC_READ|GENERIC_WRITE,
      0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL );
  if( errorPipe==INVALID_HANDLE_VALUE )
  {
    errorPipe = CreateFile( errorPipeName,GENERIC_WRITE,
        0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL );
    if( errorPipe==INVALID_HANDLE_VALUE )
      errorPipe = NULL;
  }
  else
    *writeProcessPid = 1;

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
  // special values which signal the start of the program
  HEOB_PID_ATTACH=0x10000000,
};

enum
{
  HEOB_CONTROL_NONE,
  HEOB_CONTROL_ATTACH,
};

// }}}
// main data {{{

typedef struct appData
{
  HANDLE heap;
  HANDLE errorPipe;
  int writeProcessPid;
  HANDLE attachEvent;
  PROCESS_INFORMATION pi;
  char exePath[MAX_PATH];
  wchar_t exePathW[MAX_PATH];
  wchar_t subCurDir[MAX_PATH];
  textColor *tcOut;
  textColor *tcOutOrig;
  size_t *raise_alloc_a;
  int raise_alloc_q;
  char *outName;
  char *xmlName;
  char *svgName;
  char *specificOptions;
  modInfo *a2l_mi_a;
  int a2l_mi_q;
  DWORD ppid;
  DWORD dbgPid;
  attachedProcessInfo *api;
  wchar_t *cmdLineW;
  wchar_t *argsW;
#ifndef NO_DBGENG
  HANDLE exceptionWait;
#endif
  HANDLE in;
  HANDLE err;
  HANDLE readPipe;
  HANDLE controlPipe;
  unsigned *heobExit;
  unsigned *heobExitData;
  int *recordingRemote;
  size_t svgSum;
}
appData;

static appData *initHeob( HANDLE heap )
{
  appData *ad = HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(appData) );
  ad->heap = heap;
  ad->errorPipe = openErrorPipe( &ad->writeProcessPid );
  return( ad );
}

static NORETURN void exitHeob( appData *ad,
    unsigned exitStatus,unsigned extraArg,DWORD exitCode )
{
  HANDLE heap = ad->heap;
  HANDLE errorPipe = ad->errorPipe;

  if( ad->attachEvent )
  {
    SetEvent( ad->attachEvent );
    CloseHandle( ad->attachEvent );
  }
  if( ad->pi.hThread ) CloseHandle( ad->pi.hThread );
  if( ad->pi.hProcess ) CloseHandle( ad->pi.hProcess );
  if( ad->tcOut ) HeapFree( heap,0,ad->tcOut );
  if( ad->tcOutOrig ) HeapFree( heap,0,ad->tcOutOrig );
  if( ad->raise_alloc_a ) HeapFree( heap,0,ad->raise_alloc_a );
  if( ad->outName ) HeapFree( heap,0,ad->outName );
  if( ad->xmlName ) HeapFree( heap,0,ad->xmlName );
  if( ad->svgName ) HeapFree( heap,0,ad->svgName );
  if( ad->specificOptions ) HeapFree( heap,0,ad->specificOptions );
  if( ad->a2l_mi_a ) HeapFree( heap,0,ad->a2l_mi_a );
  if( ad->api ) HeapFree( heap,0,ad->api );
#ifndef NO_DBGENG
  if( ad->exceptionWait ) CloseHandle( ad->exceptionWait );
#endif
  if( ad->readPipe ) CloseHandle( ad->readPipe );
  if( ad->controlPipe ) CloseHandle( ad->controlPipe );
  HeapFree( heap,0,ad );

  writeCloseErrorPipe( errorPipe,exitStatus,extraArg );
  ExitProcess( exitCode );
}

// }}}
// code injection {{{

typedef VOID CALLBACK func_heob( ULONG_PTR );
typedef VOID NTAPI func_inj( void*,remoteData*,void* );

static CODE_SEG(".text$1") VOID NTAPI remoteCall(
    void *apc,remoteData *rd,void *ac )
{
  (void)apc;
  (void)ac;

  HMODULE kernel32;
  if( UNLIKELY(rd->fLdrGetDllHandle(NULL,NULL,&rd->kernelName,&kernel32)) )
  {
    // kernel32.dll is not loaded -> notify heob and wait
    rd->master = INVALID_HANDLE_VALUE;
    rd->fNtSetEvent( rd->initFinished,NULL );

    LARGE_INTEGER delay;
    delay.LowPart = 0;
    delay.HighPart = 0x7fffffff;
    rd->fNtDelayExecution( FALSE,&delay );
    return;
  }

  HMODULE app = rd->fLoadLibraryW( rd->exePath );
  rd->heobMod = app;

  func_heob *fheob = (func_heob*)( (size_t)app + rd->injOffset );
  rd->fQueueUserAPC( fheob,rd->fGetCurrentThread(),(ULONG_PTR)rd );
}

static CODE_SEG(".text$2") HANDLE inject(
    appData *ad,options *opt,options *globalopt,textColor *tc,
    const char *subOutName,const char *subXmlName,const char *subSvgName,
    const wchar_t *subCurDir )
{
  // injection data {{{
  func_inj *finj = &remoteCall;
  size_t funcOffset = sizeof(remoteData) + ad->raise_alloc_q*sizeof(size_t);
  size_t funcSize = (size_t)&inject - (size_t)finj;
  size_t soOffset = funcOffset + funcSize;
  size_t soSize =
    ( ad->specificOptions ? lstrlen(ad->specificOptions) + 1 : 0 );
  size_t fullSize = soOffset + soSize;
  HANDLE process = ad->pi.hProcess;
  HANDLE thread = ad->pi.hThread;
  wchar_t *exePath = ad->exePathW;

  unsigned char *fullDataRemote = VirtualAllocEx( process,NULL,
      fullSize,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE );

  HANDLE heap = ad->heap;
  unsigned char *fullData = HeapAlloc( heap,0,fullSize );
  RtlMoveMemory( fullData+funcOffset,finj,funcSize );
  remoteData *data = (remoteData*)fullData;
  RtlZeroMemory( data,sizeof(remoteData) );

  PKNORMAL_ROUTINE remoteFuncStart =
    (PKNORMAL_ROUTINE)( fullDataRemote+funcOffset );

  HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );
  HMODULE ntdll = GetModuleHandle( "ntdll.dll" );

  data->kernel32 = kernel32;
  data->fQueueUserAPC =
    (func_QueueUserAPC*)GetProcAddress( kernel32,"QueueUserAPC" );
  data->fGetCurrentThread =
    (func_GetCurrentThread*)GetProcAddress( kernel32,"GetCurrentThread" );
  data->fVirtualProtect =
    (func_VirtualProtect*)GetProcAddress( kernel32,"VirtualProtect" );
  data->fGetCurrentProcess =
    (func_GetCurrentProcess*)GetProcAddress( kernel32,"GetCurrentProcess" );
  data->fFlushInstructionCache =
    (func_FlushInstructionCache*)GetProcAddress(
        kernel32,"FlushInstructionCache" );
  data->fLoadLibraryW =
    (func_LoadLibraryW*)GetProcAddress( kernel32,"LoadLibraryW" );
  data->fGetProcAddress =
    (func_GetProcAddress*)GetProcAddress( kernel32,"GetProcAddress" );

  data->fLdrGetDllHandle =
    (func_LdrGetDllHandle*)GetProcAddress( ntdll,"LdrGetDllHandle" );
  data->fNtSetEvent =
    (func_NtSetEvent*)GetProcAddress( ntdll,"NtSetEvent" );
  data->fNtDelayExecution =
    (func_NtDelayExecution*)GetProcAddress( ntdll,"NtDelayExecution" );
  lstrcpyW( data->kernelNameBuffer,L"kernel32.dll" );
  data->kernelName.Length = lstrlenW( data->kernelNameBuffer )*2;
  data->kernelName.MaximumLength = data->kernelName.Length + 2;
  data->kernelName.Buffer = ((remoteData*)fullDataRemote)->kernelNameBuffer;

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
    CreatePipe( &controlReadPipe,&ad->controlPipe,NULL,0 );
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

#ifndef NO_DBGENG
  if( opt->exceptionDetails>1 && opt->handleException )
  {
    ad->exceptionWait = CreateEvent( NULL,FALSE,FALSE,NULL );
    DuplicateHandle( GetCurrentProcess(),ad->exceptionWait,
        process,&data->exceptionWait,0,FALSE,
        DUPLICATE_SAME_ACCESS );
  }
#endif

  RtlMoveMemory( &data->opt,opt,sizeof(options) );
  RtlMoveMemory( &data->globalopt,globalopt,sizeof(options) );

  if( subOutName ) lstrcpyn( data->subOutName,subOutName,MAX_PATH );
  if( subXmlName ) lstrcpyn( data->subXmlName,subXmlName,MAX_PATH );
  if( subSvgName ) lstrcpyn( data->subSvgName,subSvgName,MAX_PATH );
  if( subCurDir ) lstrcpynW( data->subCurDir,subCurDir,MAX_PATH );

  data->raise_alloc_q = ad->raise_alloc_q;
  if( ad->raise_alloc_q )
    RtlMoveMemory( data->raise_alloc_a,
        ad->raise_alloc_a,ad->raise_alloc_q*sizeof(size_t) );

  if( soSize )
  {
    data->specificOptions = (char*)fullDataRemote + soOffset;
    RtlMoveMemory( fullData+soOffset,ad->specificOptions,soSize );
  }
  // }}}

  // injection {{{
  WriteProcessMemory( process,fullDataRemote,fullData,fullSize,NULL );

  func_NtQueueApcThread *fNtQueueApcThread =
    (func_NtQueueApcThread*)GetProcAddress( ntdll,"NtQueueApcThread" );
  fNtQueueApcThread(
      thread,remoteFuncStart,NULL,fullDataRemote,(void*)(LONG_PTR)-1 );
  ResumeThread( thread );
  // }}}

  // wait for finished injection {{{
  COORD consoleCoord;
  int errColor;
  HANDLE in = ad->in;
  HANDLE err = ad->err;
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
    *ad->heobExit = HEOB_PROCESS_KILLED;
    return( NULL );
  }
  // }}}

  // data of injected process {{{
  ReadProcessMemory( process,fullDataRemote,data,sizeof(remoteData),NULL );

  if( !data->master || data->master==INVALID_HANDLE_VALUE )
  {
    CloseHandle( readPipe );
    readPipe = NULL;
    if( !data->master )
      printf( "$Wonly works with dynamically linked CRT\n" );
    else
      printf( "$Wkernel32.dll is not loaded in target process\n" );
    *ad->heobExit = HEOB_NO_CRT;
  }
  else
  {
    data->exePath[MAX_PATH-1] = 0;
    GetFullPathNameW( data->exePath,MAX_PATH,exePath,NULL );
  }

  if( data->noCRT==2 )
  {
    opt->handleException = 2;
    printf( "\n$Ino CRT found\n" );
  }

  if( data->api )
  {
    ad->api = HeapAlloc( heap,0,sizeof(attachedProcessInfo) );
    ReadProcessMemory( process,data->api,ad->api,
        sizeof(attachedProcessInfo),NULL );
  }

  ad->recordingRemote = data->recordingRemote;
  // }}}

  HeapFree( heap,0,fullData );

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

  // MSVC debug info {{{
#ifndef NO_DBGHELP
  if( lineno==DWST_NO_DBG_SYM && ds->fSymGetLineFromAddr64 )
  {
    // inlined functions {{{
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
    // }}}

    // source file/line info {{{
    IMAGEHLP_LINE64 *il = ds->il;
    RtlZeroMemory( il,sizeof(IMAGEHLP_LINE64) );
    il->SizeOfStruct = sizeof(IMAGEHLP_LINE64);
    DWORD dis;
    if( ds->fSymGetLineFromAddr64(ds->process,addr,&dis,il) )
    {
      filename = il->FileName;
      lineno = il->LineNumber;
    }
    // }}}

    // function name {{{
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
                ds->undname,MAX_SYM_NAME,UNDNAME_NO_MS_KEYWORDS) )
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
    // }}}
  }
#endif
  // }}}

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

static void cacheSymbolData(
    allocation *alloc_a,const int *alloc_idxs,int alloc_q,
    modInfo *mi_a,int mi_q,dbgsym *ds,int initFrames )
{
  cacheClear( ds );

  // initialize frames {{{
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
  // }}}

  // find unique frames {{{
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
  // }}}

  // get symbol data for frames {{{
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

    // GCC debug info {{{
#ifndef NO_DWARFSTACK
    if( ds->fdwstOfFile )
      ds->fdwstOfFile( mi->path,mi->base,frames+j,l-j,locFuncCache,ds );
    else
#endif
    // }}}
    {
      for( i=j; i<l; i++ )
        locFuncCache( frames[i],mi->path,DWST_NO_DBG_SYM,NULL,ds,0 );
    }

    j = l - 1;
  }
  ds->sslCount = ds->sslIdx + 1;
  // }}}

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

  printf( "%i",indent );
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
      if( addr || !funcname )
        printf( "    %X",addr );
      else
        printf( "      " PTR_SPACES );
      if( funcname )
        printf( "   [$I%s$N]",funcname );
      printf( "\n" );
      break;

    default:
      if( addr )
        printf( "    %X",addr );
      else
        printf( "      " PTR_SPACES );
      printf( "   $O%s$N:$S%d$N",printFilename,lineno );
      if( columnno>0 )
        printf( ":%d",columnno );
      if( funcname )
        printf( " [$I%s$N]",funcname );
      printf( "\n" );

      // show source code {{{
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
                  printf( "%i      ...\n",indent );
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
                      printf( "%i$S  >   ",indent );
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
                      printf( "%i      ",indent );
                      tc->fWriteText( tc,bol,eol-bol );
                    }
                  }

                  bol = eol;
                  if( bol==eof ) break;
                }
                if( bol>map && bol[-1]!='\n' )
                  printf( "\n" );
                if( bol!=eof )
                  printf( "%i      ...\n",indent );
                printf( "%i\n",indent );

                UnmapViewOfFile( map );
              }

              CloseHandle( mapping );
            }
          }

          CloseHandle( file );
        }
      }
      // }}}
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
  if( addr || (!mi && !funcname && lineno<=0) )
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
      const char *p = filename;
      const char slash = '/';
      while( 1 )
      {
        const char *backslash = strchr( p,'\\' );
        if( !backslash ) break;
        if( backslash>p )
          tc->fWriteSubText( tc,p,backslash-p );
        p = backslash + 1;
        if( p>sep ) break;
        tc->fWriteSubText( tc,&slash,1 );
      }
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

  ASSUME( mi_a || !mi_q );

  if( ft<FT_COUNT )
  {
    if( indent>=0 )
      printf( "%i      " PTR_SPACES "   [$I%s$N]\n",
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

static char *undecorateVCsymbol( dbgsym *ds,char *decorName )
{
#ifndef NO_DBGHELP
  if( decorName[0]!='.' || !ds->fUnDecorateSymbolName )
    return( decorName );

  int decorLen = lstrlen( decorName );
  char *decor = HeapAlloc( ds->heap,0,decorLen+9 );
  lstrcpy( decor,"?X@@YK" );
  lstrcat( decor,decorName+1 );
  lstrcat( decor,"XZ" );
  if( ds->fUnDecorateSymbolName(decor,
        ds->undname,MAX_SYM_NAME,UNDNAME_NO_MS_KEYWORDS) )
  {
    ds->undname[MAX_SYM_NAME] = 0;
    int undecorLen = lstrlen( ds->undname );
    if( undecorLen>8 &&
        !lstrcmp(ds->undname+undecorLen-8," X(void)") )
    {
      ds->undname[undecorLen-8] = 0;
      decorName = ds->undname;
    }
  }
  HeapFree( ds->heap,0,decor );
#else
  (void)ds;
#endif

  return( decorName );
}

// }}}
// thread name {{{

#ifndef NO_THREADNAMES
static void printThreadName( int threadNameIdx,
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

#ifndef NO_THREADNAMES
  if( a->threadNameIdx>b->threadNameIdx ) return( -2 );
  if( a->threadNameIdx<b->threadNameIdx ) return( 2 );
#endif

  return( a->id>b->id ? 1 : -1 );
}

static int cmp_time_allocation( const void *av,const void *bv )
{
  const allocation *a = av;
  const allocation *b = bv;

  if( a->lt>b->lt ) return( 2 );
  if( a->lt<b->lt ) return( -2 );

#ifndef NO_THREADNAMES
  if( a->threadNameIdx>b->threadNameIdx ) return( -2 );
  if( a->threadNameIdx<b->threadNameIdx ) return( 2 );
#endif

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

#ifndef NO_THREADNAMES
  if( a->threadNameIdx>b->threadNameIdx ) return( -2 );
  if( a->threadNameIdx<b->threadNameIdx ) return( 2 );
#endif

  return( a->id>b->id ? 1 : -1 );
}

// }}}
// leak recording status {{{

static void showRecording( const char *title,HANDLE err,int recording,
    COORD *consoleCoord,int *errColorP )
{
  DWORD didwrite;
  WriteFile( err,"\n",1,&didwrite,NULL );

  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo( err,&csbi );
  *consoleCoord = csbi.dwCursorPosition;
  int errColor = *errColorP = csbi.wAttributes&0xff;

  const char *recText = " on   off   clear   show ";
  int titleLen = lstrlen( title );
  WriteFile( err,title,titleLen,&didwrite,NULL );
  if( recording>0 )
  {
    SetConsoleTextAttribute( err,errColor^BACKGROUND_INTENSITY );
    WriteFile( err,recText+0,4,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+4,3,&didwrite,NULL );
    SetConsoleTextAttribute( err,
        errColor^(FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY) );
    WriteFile( err,recText+7,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+8,2,&didwrite,NULL );
  }
  else
  {
    WriteFile( err,recText+0,2,&didwrite,NULL );
    SetConsoleTextAttribute( err,
        errColor^(FOREGROUND_RED|FOREGROUND_BLUE|FOREGROUND_INTENSITY) );
    WriteFile( err,recText+2,3,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^BACKGROUND_INTENSITY );
    WriteFile( err,recText+5,5,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
  }
  WriteFile( err,recText+10,1,&didwrite,NULL );
  if( recording>=0 )
  {
    WORD highlight = FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY;
    WORD xorClear = recording==2 ? BACKGROUND_GREEN : 0;
    WORD xorShow = recording==3 ? BACKGROUND_GREEN : 0;
    SetConsoleTextAttribute( err,errColor^xorClear );
    WriteFile( err,recText+11,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^(highlight|xorClear) );
    WriteFile( err,recText+12,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^xorClear );
    WriteFile( err,recText+13,5,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+18,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^xorShow );
    WriteFile( err,recText+19,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^(highlight|xorShow) );
    WriteFile( err,recText+20,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^xorShow );
    WriteFile( err,recText+21,4,&didwrite,NULL );
  }
  else
  {
    SetConsoleTextAttribute( err,errColor^BACKGROUND_INTENSITY );
    WriteFile( err,recText+11,7,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
    WriteFile( err,recText+18,1,&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor^BACKGROUND_INTENSITY );
    WriteFile( err,recText+19,6,&didwrite,NULL );
  }
  SetConsoleTextAttribute( err,errColor );
}

static void clearRecording( const char *title,HANDLE err,
    COORD consoleCoord,int errColor )
{
  COORD moveCoord = { consoleCoord.X,consoleCoord.Y-1 };
  SetConsoleCursorPosition( err,moveCoord );

  DWORD didwrite;
  int recTextLen = lstrlen( title ) + 25;
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
    allocation *alloc_a,const int *alloc_idxs,int alloc_q,
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
    allocation *alloc_a,const int *alloc_idxs,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    unsigned char **content_ptrs,modInfo *mi_a,int mi_q,dbgsym *ds,
    int sampling )
{
  options *opt = ds->opt;
  size_t minLeakSize = opt->minLeakSize;
  if( sg->allocSumSize<minLeakSize ) return;

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
        content_ptrs,mi_a,mi_q,ds,sampling );
  }

  int allocStart = sg->allocStart;
  int allocCount = sg->allocCount;
  int stackIndent = sg->stackIndent;
  allocation *a = alloc_a + alloc_idxs[allocStart];
  textColor *tc = ds->tc;
  int stackIsPrinted = 0;
  if( sg->stackStart+sg->stackCount==a->frameCount )
  {
    for( i=0; i<allocCount; i++ )
    {
      int idx = alloc_idxs[allocStart+i];
      a = alloc_a + idx;

      size_t combSize = a->size*a->count;
      if( combSize<minLeakSize ) continue;

      int indent = stackIndent + ( allocCount>1 );
      if( !sampling )
      {
        printf( "%E$W%U B ",indent,a->size );
        if( a->count>1 )
          printf( "* %d = %U B ",a->count,combSize );
      }
      else
        printf( "%E$W%d sample%s ",indent,a->count,a->count>1?"s":NULL );
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
      // leak contents {{{
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
          printf( "%i      $I%s$N ",indent,text );
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
      // }}}
      if( tc->canWriteWideChar && opt->groupLeaks>1 )
        printf( "%I\n",indent );
    }
  }
  if( allocCount>1 )
  {
    int indent = stackIndent;
    if( !sampling )
      printf( "%i$Wsum: %U B / %d\n",indent,sg->allocSumSize,sg->allocSum );
    else
      printf( "%i$Wsum: %d samples\n",indent,sg->allocSum );
  }
  if( !stackIsPrinted && sg->stackCount )
  {
    printStackCount( a->frames+(a->frameCount-(sg->stackStart+sg->stackCount)),
        sg->stackCount,mi_a,mi_q,ds,FT_COUNT,stackIndent );
    if( tc->canWriteWideChar && opt->groupLeaks>1 )
      printf( "%I\n",stackIndent );
  }
}

static int printStackGroupXml( stackGroup *sg,
    allocation *alloc_a,const int *alloc_idxs,int alloc_q,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    modInfo *mi_a,int mi_q,dbgsym *ds,const char **leakTypeNames,
    int xmlRecordNum,int sampling )
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
        mi_a,mi_q,ds,leakTypeNames,xmlRecordNum,sampling );
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
  if( sampling ) xmlLeakTypeNames[0] = "SyscallParam";
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
      if( !sampling )
        printf( "    <text>%U bytes in %d blocks are %s"
            " in loss record %d of %d (#%U)</text>\n",
            combSize,a->count,leakTypeNames[a->lt],
            xmlRecordNum,alloc_q,a->id );
      else
        printf( "    <text>%d sample%s (#%U)</text>\n",
            a->count,a->count>1?"s":NULL,a->id );
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

static void printFullStackGroupSvg( appData *ad,stackGroup *sg,textColor *tc,
    allocation *alloc_a,const int *alloc_idxs,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    modInfo *mi_a,int mi_q,dbgsym *ds,
    const char *groupName,const char *groupTypeName,int sampling );

static void printLeaks( allocation *alloc_a,int alloc_q,
    int alloc_ignore_q,size_t alloc_ignore_sum,
    int alloc_ignore_ind_q,size_t alloc_ignore_ind_sum,
    unsigned char **content_ptrs,modInfo *mi_a,int mi_q,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    options *opt,textColor *tc,dbgsym *ds,HANDLE heap,textColor *tcXml,
    appData *ad,textColor *tcSvg,int sampling )
{
  if( !tc->out && !tcXml && !tcSvg ) return;

  printf( "\n" );
  if( opt->handleException>=2 && !sampling )
    return;

  if( !alloc_q && !alloc_ignore_q && !alloc_ignore_ind_q )
  {
    if( !sampling )
      printf( "$Ono leaks found\n" );
    else
      printf( "$Ino profiling samples\n" );
    return;
  }

  int i;
  int leakDetails = opt->leakDetails;
  if( sampling ) leakDetails = 1;
  int combined_q = alloc_q;
  for( i=0; i<alloc_q; i++ )
    alloc_a[i].count = 1;
  int *alloc_idxs = NULL;
  if( leakDetails )
  {
    alloc_idxs = HeapAlloc( heap,0,alloc_q*sizeof(int) );
    if( !alloc_idxs ) return;
    for( i=0; i<alloc_q; i++ )
      alloc_idxs[i] = i;
  }
  // merge identical leaks {{{
  if( opt->groupLeaks && leakDetails )
  {
    if( opt->groupLeaks!=3 )
      sort_allocations( alloc_a,alloc_idxs,alloc_q,sizeof(allocation),
          heap,cmp_merge_allocation );
    else
      sort_allocations( alloc_a,alloc_idxs,alloc_q,sizeof(allocation),
          heap,cmp_time_allocation );
    combined_q = 0;
    for( i=0; i<alloc_q; )
    {
      allocation a;
      int idx = alloc_idxs[i];
      RtlMoveMemory( &a,&alloc_a[idx],sizeof(allocation) );
      int j;
      for( j=i+1; j<alloc_q; j++ )
      {
        int c = cmp_merge_allocation( &a,alloc_a+alloc_idxs[j] );
        if( c<-1 || c>1 ) break;

        a.count++;
      }

      RtlMoveMemory( &alloc_a[idx],&a,sizeof(allocation) );
      alloc_idxs[combined_q++] = idx;
      i = j;
    }
  }
  // }}}
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
    // leak sorting {{{
    uintptr_t threadInitAddr = ds->threadInitAddr;
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
    if( opt->groupLeaks==3 );
    else if( opt->groupLeaks>1 )
      sort_allocations( alloc_a,alloc_idxs,combined_q,sizeof(allocation),
          heap,cmp_frame_allocation );
    else
      sort_allocations( alloc_a,alloc_idxs,combined_q,sizeof(allocation),
          heap,cmp_type_allocation );
    for( i=0; i<combined_q; i++ )
      alloc_a[alloc_idxs[i]].size /= alloc_a[alloc_idxs[i]].count;
    // }}}

    // group leaks with partially identical stack {{{
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
              0,1 );
        }
        if( opt->groupLeaks!=3 )
          sortStackGroup( sg,heap );
      }
    }
    // }}}
    // grouping for merged leaks {{{
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
    // }}}
  }

  // cache symbol data {{{
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
  // }}}

  // print leaks {{{
  if( lMax==1 )
  {
    sg_a[LT_LOST].allocSum += alloc_ignore_q;
    sg_a[LT_LOST].allocSumSize += alloc_ignore_sum;
  }
  else
  {
    sg_a[LT_REACHABLE].allocSum += alloc_ignore_q;
    sg_a[LT_REACHABLE].allocSumSize += alloc_ignore_sum;
    sg_a[LT_INDIRECTLY_REACHABLE].allocSum += alloc_ignore_ind_q;
    sg_a[LT_INDIRECTLY_REACHABLE].allocSumSize += alloc_ignore_ind_sum;
  }
  int xmlRecordNum = 0;
  for( l=0; l<lMax; l++ )
  {
    stackGroup *sg = sg_a + l;
    const char *groupName = !sampling ? "leaks" : "profiling samples";
    const char *groupTypeName = leakTypeNamesRef ? leakTypeNamesRef[l] : NULL;
    if( sg->allocSum && tc->out )
    {
      printf( "$S%s",groupName );
      if( groupTypeName )
        printf( " (%s)",groupTypeName );
      printf( ":\n" );
      if( l<lDetails )
        printStackGroup( sg,alloc_a,alloc_idxs,
#ifndef NO_THREADNAMES
            threadName_a,threadName_q,
#endif
            content_ptrs,mi_a,mi_q,ds,sampling );
      if( !sampling )
        printf( "  $Wsum: %U B / %d\n",sg->allocSumSize,sg->allocSum );
      else
        printf( "  $Wsum: %d samples\n",sg->allocSum );
    }
    if( sg->allocSum && tcXml && l<lDetails )
    {
      textColor *tcOrig = tc;
      ds->tc = tcXml;
      xmlRecordNum = printStackGroupXml( sg,alloc_a,alloc_idxs,combined_q,
#ifndef NO_THREADNAMES
          threadName_a,threadName_q,
#endif
          mi_a,mi_q,ds,leakTypeNames,xmlRecordNum,sampling );
      ds->tc = tcOrig;
    }
    if( sg->allocSum && tcSvg && l<lDetails )
      printFullStackGroupSvg( ad,sg,tcSvg,alloc_a,alloc_idxs,
#ifndef NO_THREADNAMES
          threadName_a,threadName_q,
#endif
          mi_a,mi_q,ds,groupName,groupTypeName,sampling );
    freeStackGroup( sg,heap );
  }
  // }}}

  if( alloc_idxs )
    HeapFree( heap,0,alloc_idxs );
  HeapFree( heap,0,sg_a );
}

// }}}
// information of attached process {{{

static void printAttachedProcessInfo( appData *ad,textColor *tc )
{
  attachedProcessInfo *api = ad->api;
  if( !api ) return;
  printf( "\n$Iapplication: $N%S\n",ad->exePathW );
  if( api->type>=0 && api->type<=3 )
  {
    const char *types[4] = {
      "$Icommand line: $N%S\n",
      "$Icygwin exec:\n",
      "$Icygwin spawn:\n",
      "$Icygwin fork\n",
    };
    const wchar_t *cmdLine = api->commandLine;
    printf( types[api->type],cmdLine );

    int i;
    int cyg_argc = api->cyg_argc;
    for( i=0; i<cyg_argc; i++ )
    {
      printf( "$I  argv$N[$O%d$N]: %S\n",i,cmdLine );
      cmdLine += lstrlenW( cmdLine ) + 1;
    }
  }
  if( api->currentDirectory[0] )
    printf( "$Idirectory: $N%S\n",api->currentDirectory );
  printf( "$IPID: $N%u\n",ad->pi.dwProcessId );
  if( ad->ppid )
    printf( "$Iparent PID: $N%u\n",ad->ppid );
  if( api->stdinName[0] )
    printf( "$Istdin: $N%S\n",api->stdinName );
  if( api->stdoutName[0] )
    printf( "$Istdout: $N%S\n",api->stdoutName );
  if( api->stderrName[0] )
    printf( "$Istderr: $N%S\n",api->stderrName );
  if( ad->dbgPid )
    printf( "$Idebugger PID: $N%u\n",ad->dbgPid );
  printf( "\n" );
}

// }}}
// common options {{{

static char *readOption( char *args,options *opt,appData *ad,HANDLE heap )
{
  if( !args || args[0]!='-' ) return( NULL );

  int raise_alloc_q = ad->raise_alloc_q;
  size_t *raise_alloc_a = ad->raise_alloc_a;

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

    case 'D':
      opt->exceptionDetails = atoi( args+2 );
      break;

    case 'I':
      opt->samplingInterval = args[2]=='-' ? -atoi( args+3 ) : atoi( args+2 );
      break;

    default:
      return( NULL );
  }
  while( args[0] && args[0]!=' ' && args[0]!=';' ) args++;

  ad->raise_alloc_q = raise_alloc_q;
  ad->raise_alloc_a = raise_alloc_a;

  return( args );
}

static char *getStringOption( const char *start,HANDLE heap )
{
  const char *end = start;
  while( *end && *end!=' ' ) end++;
  if( end<=start ) return( NULL );

  size_t len = end - start;
  char *str = HeapAlloc( heap,0,len+1 );
  if( !str ) return( NULL );

  RtlMoveMemory( str,start,len );
  str[len] = 0;
  return( str );
}

static char *expandFileNameVars( appData *ad,const char *origName,
    const char *exePath )
{
  HANDLE heap = ad->heap;
  char *name = NULL;

  char *replaced = strreplacenum( origName,"%p",ad->pi.dwProcessId,heap );
  if( replaced )
    origName = name = replaced;

  replaced = strreplacenum( origName,"%P",ad->ppid,heap );
  if( replaced )
  {
    if( name ) HeapFree( heap,0,name );
    origName = name = replaced;
  }

  char *lastPoint = NULL;
  if( !exePath )
  {
    char *delim = strrchr( ad->exePath,'\\' );
    if( delim ) delim++;
    else delim = ad->exePath;
    lastPoint = strrchr( delim,'.' );
    if( lastPoint ) lastPoint[0] = 0;
    exePath = delim;
  }
  replaced = strreplace( origName,"%n",exePath,heap );
  if( lastPoint ) lastPoint[0] = '.';
  if( replaced )
  {
    if( name ) HeapFree( heap,0,name );
    name = replaced;
  }

  return( name );
}

// }}}
// disassembler {{{

#ifndef NO_DBGENG
static char *disassemble( DWORD pid,void *addr,HANDLE heap )
{
  HMODULE dbgeng = NULL;
  IDebugClient *dbgclient = NULL;
  IDebugControl3 *dbgcontrol = NULL;
  int attached = 0;
  char *asmbuf = NULL;
  char *dis = NULL;
  do
  {
    dbgeng = LoadLibrary( "dbgeng.dll" );
    if( !dbgeng ) break;

    typedef HRESULT WINAPI func_DebugCreate( REFIID,PVOID* );
    func_DebugCreate *fDebugCreate =
      (func_DebugCreate*)GetProcAddress( dbgeng,"DebugCreate" );
    if( !fDebugCreate ) break;

    HRESULT res;

    const GUID IID_IDebugClient =
    { 0x27fe5639,0x8407,0x4f47,{0x83,0x64,0xee,0x11,0x8f,0xb0,0x8a,0xc8} };
    res = fDebugCreate( &IID_IDebugClient,(void**)&dbgclient );
    if( res!=S_OK ) break;

    const GUID IID_IDebugControl3 =
    { 0x7df74a86,0xb03f,0x407f,{0x90,0xab,0xa2,0x0d,0xad,0xce,0xad,0x08} };
    res = dbgclient->lpVtbl->QueryInterface( dbgclient,
        &IID_IDebugControl3,(void**)&dbgcontrol );
    if( res!=S_OK ) break;

    res = dbgcontrol->lpVtbl->SetAssemblyOptions( dbgcontrol,
        DEBUG_ASMOPT_NO_CODE_BYTES );
    if( res!=S_OK ) break;

    res = dbgclient->lpVtbl->AttachProcess( dbgclient,
        0,pid,
        DEBUG_ATTACH_NONINVASIVE|DEBUG_ATTACH_NONINVASIVE_NO_SUSPEND );
    if( res!=S_OK ) break;
    attached = 1;

    res = dbgcontrol->lpVtbl->WaitForEvent( dbgcontrol,
        0,INFINITE );
    if( res!=S_OK ) break;

    asmbuf = HeapAlloc( heap,0,65536 );
    ULONG64 offset = (uintptr_t)addr;
    res = dbgcontrol->lpVtbl->Disassemble( dbgcontrol,
        offset,0,asmbuf,65536,NULL,&offset );
    if( res!=S_OK ) break;

    char *asmc = asmbuf;
    while( asmc[0]=='`' ||
        (asmc[0]>='0' && asmc[0]<='9') ||
        (asmc[0]>='a' && asmc[0]<='f') ||
        (asmc[0]>='A' && asmc[0]<='F') )
      asmc++;
    while( asmc[0]==' ' ) asmc++;
    size_t asml = lstrlen( asmc );
    while( asml && (asmc[asml-1]=='\r' || asmc[asml-1]=='\n') )
      asmc[--asml] = 0;
    if( !asml || asmc[0]=='?' ) break;

    dis = HeapAlloc( heap,0,asml+1 );
    lstrcpy( dis,asmc );
  }
  while( 0 );
  if( asmbuf ) HeapFree( heap,0,asmbuf );
  if( attached ) dbgclient->lpVtbl->DetachProcesses( dbgclient );
  if( dbgcontrol ) dbgcontrol->lpVtbl->Release( dbgcontrol );
  if( dbgclient ) dbgclient->lpVtbl->Release( dbgclient );
  if( dbgeng ) FreeLibrary( dbgeng );
  return( dis );
}
#endif

// }}}
// console functions {{{

static void waitForKey( textColor *tc,HANDLE in )
{
  printf( "press any key to continue..." );

  DWORD flags;
  int hasConMode;
  if( (hasConMode=GetConsoleMode(in,&flags)) )
    SetConsoleMode( in,flags & ~ENABLE_MOUSE_INPUT );

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

  if( hasConMode )
    SetConsoleMode( in,flags );
}

static int isConsoleOwner( void )
{
  DWORD conPid;
  return( GetConsoleProcessList(&conPid,1)==1 );
}

static void waitForKeyIfConsoleOwner( textColor *tc,HANDLE in )
{
  if( !in || !isConsoleOwner() ) return;

  printf( "\n" );
  waitForKey( tc,in );
}

static void showConsole( void )
{
  HMODULE user32 = LoadLibrary( "user32.dll" );
  if( !user32 ) return;

  typedef BOOL WINAPI func_ShowWindow( HWND,int );
  func_ShowWindow *fShowWindow =
    (func_ShowWindow*)GetProcAddress( user32,"ShowWindow" );
  if( fShowWindow )
    fShowWindow( GetConsoleWindow(),SW_RESTORE );
  FreeLibrary( user32 );
}

static void setHeobConsoleTitle( HANDLE heap,const wchar_t *prog )
{
  wchar_t *title = HeapAlloc( heap,0,(10+lstrlenW(prog))*2 );
  lstrcpyW( title,L"heob" BITS " - " );
  lstrcatW( title,prog );
  SetConsoleTitleW( title );
  HeapFree( heap,0,title );
}

// }}}
// xml {{{

static textColor *writeXmlHeader( appData *ad,DWORD startTicks )
{
  if( !ad->xmlName ) return( NULL );

  char *fullName = expandFileNameVars( ad,ad->xmlName,NULL );
  if( !fullName )
  {
    fullName = ad->xmlName;
    ad->xmlName = NULL;
  }

  HANDLE xml = CreateFile( fullName,GENERIC_WRITE,FILE_SHARE_READ,
      NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
  HeapFree( ad->heap,0,fullName );
  if( xml==INVALID_HANDLE_VALUE ) return( NULL );

  textColor *tc = HeapAlloc( ad->heap,HEAP_ZERO_MEMORY,sizeof(textColor) );
  tc->fWriteText = &WriteText;
  tc->fWriteSubText = &WriteTextHtml;
  tc->fWriteSubTextW = &WriteTextHtmlW;
  tc->fTextColor = NULL;
  tc->out = xml;
  tc->color = ATT_NORMAL;

  const wchar_t *exePathW = ad->exePathW;

  printf( "<?xml version=\"1.0\"?>\n\n" );
  printf( "<valgrindoutput>\n\n" );
  printf( "<protocolversion>4</protocolversion>\n" );
  printf( "<protocoltool>memcheck</protocoltool>\n\n" );
  printf( "<preamble>\n" );
  printf( "  <line>heap-observer " HEOB_VER " (" BITS "bit)</line>\n" );
  attachedProcessInfo *api = ad->api;
  if( api )
  {
    printf( "  <line>application: %S</line>\n",exePathW );
    if( api->type>=0 && api->type<=3 )
    {
      const char *types[4] = {
        "  <line>command line: %S</line>\n",
        "  <line>cygwin exec:</line>\n",
        "  <line>cygwin spawn:</line>\n",
        "  <line>cygwin fork</line>\n",
      };
      const wchar_t *cl = api->commandLine;
      printf( types[api->type],cl );

      int i;
      int cyg_argc = api->cyg_argc;
      for( i=0; i<cyg_argc; i++ )
      {
        printf( "  <line>  argv[%d]: %S</line>\n",i,cl );
        cl += lstrlenW( cl ) + 1;
      }
    }
    if( api->currentDirectory[0] )
      printf( "  <line>directory: %S</line>\n",api->currentDirectory );
    if( api->stdinName[0] )
      printf( "  <line>stdin: %S</line>\n",api->stdinName );
    if( api->stdoutName[0] )
      printf( "  <line>stdout: %S</line>\n",api->stdoutName );
    if( api->stderrName[0] )
      printf( "  <line>stderr: %S</line>\n",api->stderrName );
  }
  if( ad->dbgPid )
    printf( "  <line>debugger PID: %u</line>\n",ad->dbgPid );
  printf( "</preamble>\n\n" );
  printf( "<pid>%u</pid>\n<ppid>%u</ppid>\n<tool>heob</tool>\n\n",
      ad->pi.dwProcessId,ad->ppid );

  const wchar_t *cmdLineW = ad->cmdLineW;
  const wchar_t *argsW = ad->argsW;
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
    if( l && api && api->type>0 )
    {
      const wchar_t *cl = api->commandLine;
      int i;
      int cyg_argc = api->cyg_argc;
      if( api->type==3 )
      {
        const wchar_t *lastDelim = strrchrW( exePathW,'\\' );
        if( lastDelim ) lastDelim++;
        else lastDelim = exePathW;
        lstrcpynW( api->commandLine,lastDelim,32768 );
        wchar_t *lastPointW = strrchrW( cl,'.' );
        if( lastPointW ) lastPointW[0] = 0;
        cyg_argc = 1;
      }
      for( i=0; i<cyg_argc; i++ )
      {
        const char *argstr = i ? "arg" : "exe";
        printf( "    <%s>%S</%s>\n",argstr,cl,argstr );
        cl += lstrlenW( cl ) + 1;
      }
      printf( "  </%s>\n",argvstr );
      break;
    }
    int i = 0;
    while( i<argl )
    {
      int startI = i;
      if( !l && i+1<argl && argv[i]=='-' && argv[i+1]=='O' )
      {
        i += 2;
        while( i<argl && argv[i]!=' ' )
        {
          while( i<argl && argv[i]!=':' ) i++;
          while( i<argl && argv[i]!=';' ) i++;
          if( i<argl ) i++;
        }
      }
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

  return( tc );
}

static void writeXmlFooter( textColor *tc,HANDLE heap,DWORD startTicks )
{
  if( !tc ) return;

  printf( "<status>\n  <state>FINISHED</state>\n"
      "  <time>%t</time>\n</status>\n\n",
      GetTickCount()-startTicks );

  printf( "</valgrindoutput>\n" );

  CloseHandle( tc->out );
  HeapFree( heap,0,tc );
}

static void writeXmlException( textColor *tc,dbgsym *ds,
    exceptionInfo *ei,const char *desc,char *addr,const char *violationType,
    const char *nearBlock,const char *blockType,modInfo *mi_a,int mi_q )
{
  if( !tc ) return;

  textColor *tcOrig = ds->tc;
  ds->tc = tc;

  printf( "<error>\n" );
  printf( "  <kind>InvalidRead</kind>\n" );
  printf( "  <what>unhandled exception code: %x%s</what>\n",
      ei->er.ExceptionCode,desc );
  printf( "  <auxwhat>exception on</auxwhat>\n" );
  printf( "  <stack>\n" );
  printStackCount( ei->aa[0].frames,ei->aa[0].frameCount,
      mi_a,mi_q,ds,FT_COUNT,-1 );
  printf( "  </stack>\n" );
  if( violationType )
  {
    printf( "  <auxwhat>%s violation at %p</auxwhat>\n",
        violationType,addr );
    printf( "  <stack>\n" );
    printf( "  </stack>\n" );
  }

  if( ei->aq>1 )
  {
    char *ptr = (char*)ei->aa[1].ptr;
    size_t size = ei->aa[1].size;
    printf(
        "  <auxwhat>%s%s %p (size %U, offset %s%D)</auxwhat>\n",
        nearBlock,blockType,
        ptr,size,addr>ptr?"+":"",addr-ptr );
    printf( "  <stack>\n" );
    printf( "  </stack>\n" );
    printf( "  <auxwhat>allocated on (#%U)</auxwhat>\n",
        ei->aa[1].id );
    printf( "  <stack>\n" );
    printStackCount( ei->aa[1].frames,ei->aa[1].frameCount,
        mi_a,mi_q,ds,ei->aa[1].ft,-1 );
    printf( "  </stack>\n" );

    if( ei->aq>2 )
    {
      printf( "  <auxwhat>freed on</auxwhat>\n" );
      printf( "  <stack>\n" );
      printStackCount( ei->aa[2].frames,ei->aa[2].frameCount,
          mi_a,mi_q,ds,ei->aa[2].ft,-1 );
      printf( "  </stack>\n" );
    }
  }
  else if( ei->throwName[0] )
  {
    char *throwName = undecorateVCsymbol( ds,ei->throwName );
    printf( "  <auxwhat>VC c++ exception: %s</auxwhat>\n",
        throwName );
    printf( "  <stack>\n" );
    printf( "  </stack>\n" );
  }
  printf( "</error>\n\n" );

  ds->tc = tcOrig;
}

static void writeXmlAllocFail( textColor *tc,dbgsym *ds,
    size_t mul,int mulOverflow,allocation *aa,modInfo *mi_a,int mi_q )
{
  if( !tc ) return;

  textColor *tcOrig = ds->tc;
  ds->tc = tc;

  printf( "<error>\n" );
  printf( "  <kind>UninitValue</kind>\n" );
  if( mulOverflow )
    printf( "  <what>multiplication overflow in allocation"
        " of %U * %U bytes (#%U)</what>\n",aa->size,mul,aa->id );
  else
    printf( "  <what>allocation failed of %U bytes (#%U)</what>\n",
        aa->size,aa->id );
  printf( "  <stack>\n" );
  printStackCount( aa->frames,aa->frameCount,
      mi_a,mi_q,ds,aa->ft,-1 );
  printf( "  </stack>\n" );
  printf( "</error>\n\n" );

  ds->tc = tcOrig;
}

static void writeXmlFreeFail( textColor *tc,dbgsym *ds,
    modInfo *allocMi,allocation *aa,modInfo *mi_a,int mi_q )
{
  if( !tc ) return;

  textColor *tcOrig = ds->tc;
  ds->tc = tc;

  printf( "<error>\n" );
  printf( "  <kind>InvalidFree</kind>\n" );
  printf( "  <what>deallocation of invalid pointer %p</what>\n",
      aa->ptr );
  printf( "  <stack>\n" );
  printStackCount( aa->frames,aa->frameCount,
      mi_a,mi_q,ds,aa->ft,-1 );
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
    printf( "  <stack>\n" );
    printf( "  </stack>\n" );
    printf( "  <auxwhat>allocated on (#%U)</auxwhat>\n",
        aa[1].id );
    printf( "  <stack>\n" );
    printStackCount( aa[1].frames,aa[1].frameCount,
        mi_a,mi_q,ds,aa[1].ft,-1 );
    printf( "  </stack>\n" );

    if( aa[2].ptr )
    {
      printf( "  <auxwhat>freed on</auxwhat>\n" );
      printf( "  <stack>\n" );
      printStackCount( aa[2].frames,aa[2].frameCount,
          mi_a,mi_q,ds,aa[2].ft,-1 );
      printf( "  </stack>\n" );
    }
  }
  else if( aa[1].id==1 )
  {
    printf( "  <auxwhat>pointing to stack</auxwhat>\n" );
    printf( "  <stack>\n" );
    printf( "  </stack>\n" );
    printf( "  <auxwhat>possibly same frame as</auxwhat>\n" );
    printf( "  <stack>\n" );
    printStackCount( aa[1].frames,aa[1].frameCount,
        mi_a,mi_q,ds,FT_COUNT,-1 );
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
    printf( "  <stack>\n" );
    printf( "  </stack>\n" );
    printf( "  <auxwhat>allocated on (#%U)</auxwhat>\n",
        aa[3].id );
    printf( "  <stack>\n" );
    printStackCount( aa[3].frames,aa[3].frameCount,
        mi_a,mi_q,ds,aa[3].ft,-1 );
    printf( "  </stack>\n" );
  }
  printf( "</error>\n\n" );

  ds->tc = tcOrig;
}

static void writeXmlDoubleFree( textColor *tc,dbgsym *ds,
    allocation *aa,modInfo *mi_a,int mi_q )
{
  if( !tc ) return;

  textColor *tcOrig = ds->tc;
  ds->tc = tc;

  printf( "<error>\n" );
  printf( "  <kind>InvalidFree</kind>\n" );
  printf( "  <what>double free of %p (size %U)</what>\n",
      aa[1].ptr,aa[1].size );
  printf( "  <auxwhat>called on</auxwhat>\n" );
  printf( "  <stack>\n" );
  printStackCount( aa[0].frames,aa[0].frameCount,
      mi_a,mi_q,ds,aa[0].ft,-1 );
  printf( "  </stack>\n" );
  printf( "  <auxwhat>allocated on (#%U)</auxwhat>\n",
      aa[1].id );
  printf( "  <stack>\n" );
  printStackCount( aa[1].frames,aa[1].frameCount,
      mi_a,mi_q,ds,aa[1].ft,-1 );
  printf( "  </stack>\n" );
  printf( "  <auxwhat>freed on</auxwhat>\n" );
  printf( "  <stack>\n" );
  printStackCount( aa[2].frames,aa[2].frameCount,
      mi_a,mi_q,ds,aa[2].ft,-1 );
  printf( "  </stack>\n" );
  printf( "</error>\n\n" );

  ds->tc = tcOrig;
}

static void writeXmlSlack( textColor *tc,dbgsym *ds,
    allocation *aa,modInfo *mi_a,int mi_q )
{
  if( !tc ) return;

  textColor *tcOrig = ds->tc;
  ds->tc = tc;

  printf( "<error>\n" );
  printf( "  <kind>InvalidWrite</kind>\n" );
  printf( "  <what>write access violation at %p</what>\n",
      aa[1].ptr );
  printf( "  <auxwhat>slack area of %p"
      " (size %U, offset %s%D)</auxwhat>\n",
      aa[0].ptr,aa[0].size,
      aa[1].ptr>aa[0].ptr?"+":"",
      (char*)aa[1].ptr-(char*)aa[0].ptr );
  printf( "  <stack>\n" );
  printf( "  </stack>\n" );
  printf( "  <auxwhat>allocated on (#%U)</auxwhat>\n",
      aa[0].id );
  printf( "  <stack>\n" );
  printStackCount( aa[0].frames,aa[0].frameCount,
      mi_a,mi_q,ds,aa[0].ft,-1 );
  printf( "  </stack>\n" );
  printf( "  <auxwhat>freed on</auxwhat>\n" );
  printf( "  <stack>\n" );
  printStackCount( aa[1].frames,aa[1].frameCount,
      mi_a,mi_q,ds,aa[1].ft,-1 );
  printf( "  </stack>\n" );
  printf( "</error>\n\n" );

  ds->tc = tcOrig;
}

static void writeXmlWrongDealloc( textColor *tc,dbgsym *ds,
    allocation *aa,modInfo *mi_a,int mi_q )
{
  if( !tc ) return;

  textColor *tcOrig = ds->tc;
  ds->tc = tc;

  printf( "<error>\n" );
  printf( "  <kind>MismatchedFree</kind>\n" );
  printf(
      "  <what>mismatching allocation/release method</what>\n" );
  printf( "  <auxwhat>allocated on (#%U)</auxwhat>\n",
      aa[0].id );
  printf( "  <stack>\n" );
  printStackCount( aa[0].frames,aa[0].frameCount,
      mi_a,mi_q,ds,aa[0].ft,-1 );
  printf( "  </stack>\n" );
  printf( "  <auxwhat>freed on</auxwhat>\n" );
  printf( "  <stack>\n" );
  printStackCount( aa[1].frames,aa[1].frameCount,
      mi_a,mi_q,ds,aa[1].ft,-1 );
  printf( "  </stack>\n" );
  printf( "</error>\n\n" );

  ds->tc = tcOrig;
}

// }}}
// svg {{{

static void writeResource( textColor *tc,int resID )
{
  HRSRC hrsrc = FindResource( NULL,MAKEINTRESOURCE(resID),RT_RCDATA );
  if( !hrsrc ) return;

  HGLOBAL hglobal = LoadResource( NULL,hrsrc );
  if( !hglobal ) return;

  WriteText( tc,LockResource(hglobal),SizeofResource(NULL,hrsrc) );
}

static textColor *writeSvgHeader( appData *ad )
{
  if( !ad->svgName ) return( NULL );

  char *fullName = expandFileNameVars( ad,ad->svgName,NULL );
  if( !fullName )
  {
    fullName = ad->svgName;
    ad->svgName = NULL;
  }

  HANDLE svg = CreateFile( fullName,GENERIC_WRITE,FILE_SHARE_READ,
      NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
  HeapFree( ad->heap,0,fullName );
  if( svg==INVALID_HANDLE_VALUE ) return( NULL );

  textColor *tc = HeapAlloc( ad->heap,HEAP_ZERO_MEMORY,sizeof(textColor) );
  tc->fWriteText = &WriteText;
  tc->fWriteSubText = &WriteTextHtml;
  tc->fWriteSubTextW = &WriteTextHtmlW;
  tc->fTextColor = NULL;
  tc->out = svg;
  tc->color = ATT_NORMAL;

  printf( "<svg width=\"1280\" height=\"100\" onload=\"heobInit()\""
      " xmlns=\"http://www.w3.org/2000/svg\">\n" );
  printf( "  <style type=\"text/css\">\n" );
  printf( "    .sample:hover { stroke:black; stroke-width:0.5;"
      " cursor:pointer; }\n" );
  printf( "  </style>\n" );
  printf( "  <script type=\"text/ecmascript\">\n" );
  printf( "    <![CDATA[\n" );

  writeResource( tc,100 );

  printf( "    ]]>\n" );
  printf( "  </script>\n" );

  char *exePath = ad->exePath;
  char *delim = strrchr( exePath,'\\' );
  if( delim ) delim++;
  else delim = exePath;
  char *lastPoint = strrchr( delim,'.' );
  if( lastPoint ) lastPoint[0] = 0;

  printf( "  <title>%s - heob " HEOB_VER "</title>\n",delim );
  printf( "  <text id=\"cmd\" heobCmd=\"%S\">%s</text>\n",
      ad->cmdLineW,delim );

  if( lastPoint ) lastPoint[0] = '.';

  return( tc );
}

static void locSvg( textColor *tc,uintptr_t addr,int useAddr,
    size_t samples,size_t ofs,int stack,int allocs,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,int threadNameIdx,
#endif
    const char *filename,int lineno,const char *funcname,const char *modname )
{
  if( stack<=1 ) printf( "\n" );

  printf( "  <svg heobSum=\"%U\" heobOfs=\"%U\" heobStack=\"%d\"",
      samples,ofs,stack );
  if( allocs )
    printf( " heobAllocs=\"%d\"",allocs );
  if( useAddr )
    printf( " heobAddr=\"%X\"",addr );
  if( lineno>0 )
    printf( " heobSource=\"%s:%d\"",filename,lineno );
  if( funcname )
    printf( " heobFunc=\"%s\"",funcname );
  if( modname )
    printf( " heobMod=\"%s\"",modname );
#ifndef NO_THREADNAMES
  if( threadNameIdx>0 && threadNameIdx<=threadName_q )
    printf( " heobThread=\"%s\"",threadName_a[threadNameIdx-1].name );
  else if( threadNameIdx<0 )
    printf( " heobThread=\"thread %u\"",(unsigned)-threadNameIdx );
#endif
  printf( "/>\n" );
}

static int printStackCountSvg( void **framesV,int fc,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,int threadNameIdx,
#endif
    textColor *tc,modInfo *mi_a,int mi_q,dbgsym *ds,funcType ft,
    size_t samples,size_t ofs,int stack,int allocs,int sampling )
{
  uintptr_t *frames = (uintptr_t*)framesV;
  stackSourceLocation *ssl = ds->ssl;
  int sslCount = ds->sslCount;
  int stackCount = 0;
  int j;
  for( j=fc-1; j>=0; )
  {
    int k;
    uintptr_t frame = frames[j];
    for( k=0; k<mi_q && (frame<mi_a[k].base ||
          frame>=mi_a[k].base+mi_a[k].size); k++ );
    if( k>=mi_q )
    {
      locSvg( tc,frame,1,samples,ofs,stack+stackCount,sampling?0:allocs,
#ifndef NO_THREADNAMES
          threadName_a,threadName_q,threadNameIdx,
#endif
          NULL,0,NULL,NULL );
      j--;
      stackCount++;
      continue;
    }
    modInfo *mi = mi_a + k;

    int l;
    for( l=j-1; l>=0 && frames[l]>=mi->base &&
        frames[l]<mi->base+mi->size; l-- );

    for( ; j>l; j-- )
    {
      frame = frames[j];
      stackSourceLocation *s = findStackSourceLocation( frame,ssl,sslCount );
      if( !s )
      {
        locSvg( tc,frame,1,samples,ofs,stack+stackCount,sampling?0:allocs,
#ifndef NO_THREADNAMES
            threadName_a,threadName_q,threadNameIdx,
#endif
            NULL,0,NULL,mi->path );
        stackCount++;
        continue;
      }

      int inlineCount = 0;
      sourceLocation *sl = &s->sl;
      while( 1 )
      {
        inlineCount++;
        sourceLocation *nextSl = sl->inlineLocation;
        if( !nextSl ) break;
        sl = nextSl;
      }
      int stackPos = stack + stackCount;
      // output first the bottom stack
      locSvg( tc,frame,1,samples,ofs,stackPos,sampling?0:allocs,
#ifndef NO_THREADNAMES
          threadName_a,threadName_q,threadNameIdx,
#endif
          sl->filename,sl->lineno,sl->funcname,mi->path );
      // then the rest from top to bottom+1
      sl = &s->sl;
      int inlinePos = inlineCount - 1;
      while( inlinePos>0 )
      {
        locSvg( tc,0,0,samples,ofs,stackPos+inlinePos,sampling?0:allocs,
#ifndef NO_THREADNAMES
            threadName_a,threadName_q,threadNameIdx,
#endif
            sl->filename,sl->lineno,sl->funcname,mi->path );

        inlinePos--;
        sl = sl->inlineLocation;
      }
      stackCount += inlineCount;
    }
  }
  if( ft<FT_COUNT )
  {
    locSvg( tc,0,0,samples,ofs,stack+stackCount,sampling?0:allocs,
#ifndef NO_THREADNAMES
        threadName_a,threadName_q,threadNameIdx,
#endif
        NULL,0,ds->funcnames[ft],NULL );
    stackCount++;
  }
  return( stackCount );
}

static void printStackGroupSvg( stackGroup *sg,textColor *tc,
    allocation *alloc_a,const int *alloc_idxs,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    modInfo *mi_a,int mi_q,dbgsym *ds,size_t ofs,int stack,int sampling )
{
  int i;
  int allocStart = sg->allocStart;
  int allocCount = sg->allocCount;

  allocation *a = alloc_a + alloc_idxs[allocStart];
  if( sg->stackCount &&
      (sg->stackStart+sg->stackCount!=a->frameCount ||
       allocCount>1) )
  {
#ifndef NO_THREADNAMES
    int threadNameIdx = 0;
    for( i=0; i<allocCount; i++ )
    {
      int idx = alloc_idxs[allocStart+i];
      allocation *aIdx = alloc_a + idx;
      if( !threadNameIdx )
        threadNameIdx = aIdx->threadNameIdx;
      else if( threadNameIdx!=aIdx->threadNameIdx )
      {
        threadNameIdx = 0;
        break;
      }
    }
#endif

    stack += printStackCountSvg(
        a->frames+(a->frameCount-(sg->stackStart+sg->stackCount)),
        sg->stackCount,
#ifndef NO_THREADNAMES
        threadName_a,threadName_q,threadNameIdx,
#endif
        tc,mi_a,mi_q,ds,FT_COUNT,
        sg->allocSumSize,ofs,stack,sg->allocSum,sampling );
  }

  size_t minLeakSize = ds->opt->minLeakSize;
  if( sg->stackStart+sg->stackCount==a->frameCount )
  {
    for( i=0; i<allocCount; i++ )
    {
      int idx = alloc_idxs[allocStart+i];
      a = alloc_a + idx;
      size_t combSize = a->size*a->count;
      if( combSize<minLeakSize ) continue;
      if( allocCount>1 )
      {
#ifndef NO_THREADNAMES
        if( sampling )
          locSvg( tc,0,0,combSize,ofs,stack,0,
              threadName_a,threadName_q,a->threadNameIdx,
              NULL,0,NULL,NULL );
        else
#endif
          printStackCountSvg( NULL,0,
#ifndef NO_THREADNAMES
              threadName_a,threadName_q,a->threadNameIdx,
#endif
              tc,NULL,0,ds,a->ft,
              combSize,ofs,stack,a->count,sampling );
      }
      else
        stack += printStackCountSvg(
            a->frames+(a->frameCount-(sg->stackStart+sg->stackCount)),
            sg->stackCount,
#ifndef NO_THREADNAMES
            threadName_a,threadName_q,a->threadNameIdx,
#endif
            tc,mi_a,mi_q,ds,a->ft,
            combSize,ofs,stack,a->count,sampling );
      ofs += combSize;
    }
  }

  stackGroup *child_a = sg->child_a;
  int *childSorted_a = sg->childSorted_a;
  int child_q = sg->child_q;
  for( i=0; i<child_q; i++ )
  {
    int idx = childSorted_a ? childSorted_a[i] : i;
    stackGroup *sgc = child_a + idx;
    size_t allocSumSize = sgc->allocSumSize;
    if( allocSumSize<minLeakSize ) continue;
    printStackGroupSvg( sgc,tc,alloc_a,alloc_idxs,
#ifndef NO_THREADNAMES
        threadName_a,threadName_q,
#endif
        mi_a,mi_q,ds,ofs,stack,sampling );
    ofs += allocSumSize;
  }
}

static void printFullStackGroupSvg( appData *ad,stackGroup *sg,textColor *tc,
    allocation *alloc_a,const int *alloc_idxs,
#ifndef NO_THREADNAMES
    threadNameInfo *threadName_a,int threadName_q,
#endif
    modInfo *mi_a,int mi_q,dbgsym *ds,
    const char *groupName,const char *groupTypeName,int sampling )
{
  char *fullTypeName = NULL;
  const char *fullName = groupName;
  if( groupTypeName )
  {
    fullTypeName = HeapAlloc( ad->heap,0,
        lstrlen(groupName)+lstrlen(groupTypeName)+4 );
    lstrcpy( fullTypeName,groupName );
    lstrcat( fullTypeName," (" );
    lstrcat( fullTypeName,groupTypeName );
    lstrcat( fullTypeName,")" );
    fullName = fullTypeName;
  }
  locSvg( tc,0,0,sg->allocSumSize,ad->svgSum,1,sampling?0:sg->allocSum,
#ifndef NO_THREADNAMES
      NULL,0,0,
#endif
      NULL,0,fullName,NULL );
  if( fullTypeName ) HeapFree( ad->heap,0,fullTypeName );

  printStackGroupSvg( sg,tc,alloc_a,alloc_idxs,
#ifndef NO_THREADNAMES
      threadName_a,threadName_q,
#endif
      mi_a,mi_q,ds,ad->svgSum,2,sampling );
  ad->svgSum += sg->allocSumSize;
}

static void writeSvgFooter( textColor *tc,appData *ad,int sample_times )
{
  if( !tc ) return;

  if( ad->svgSum )
    printf( "\n  <svg heobSum=\"%U\" heobOfs=\"0\" heobStack=\"0\""
        " heobFunc=\"all\" heobSamples=\"%d\"/>\n",ad->svgSum,sample_times );

  printf( "</svg>\n" );

  CloseHandle( tc->out );
  HeapFree( ad->heap,0,tc );
}

// }}}
// main loop {{{

static void mainLoop( appData *ad,dbgsym *ds,DWORD startTicks,UINT *exitCode )
{
  textColor *tcXml = writeXmlHeader( ad,startTicks );
  textColor *tcSvg = writeSvgHeader( ad );

  HANDLE heap = ad->heap;
  options *opt = ds->opt;
  textColor *tc = ds->tc;
  int type;
  modInfo *mi_a = NULL;
  int mi_q = 0;
  int terminated = -2;
  int alloc_show_q = 0;
  int error_q = 0;
#ifndef NO_THREADNAMES
  int threadName_q = 0;
  threadNameInfo *threadName_a = NULL;
#endif
  HANDLE in = ad->in;
  if( !opt->leakRecording ) in = NULL;
  HANDLE err = ad->err;
  HANDLE readPipe = ad->readPipe;
  int recording = opt->leakRecording!=1 ? 1 : -1;
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
  DWORD flashStart = 0;
  int sample_times = 0;
  if( in ) showConsole();
  const char *title = opt->handleException>=2 ?
    "profiling sample recording: " : "leak recording: ";
  while( 1 )
  {
    if( needData )
    {
      if( !ReadFile(readPipe,&type,sizeof(int),NULL,&ov) &&
          GetLastError()!=ERROR_IO_PENDING ) break;
      needData = 0;

      if( in )
        showRecording( title,err,recording,&consoleCoord,&errColor );
    }

    DWORD waitTime = INFINITE;
    // timeout of clear/show text flash {{{
    if( recording>=2 )
    {
#define FLASH_TIMEOUT 500
      DWORD flashTime = GetTickCount() - flashStart;
      if( flashTime>=FLASH_TIMEOUT )
      {
        flashStart = 0;
        recording = 1;
        clearRecording( title,err,consoleCoord,errColor );
        showRecording( title,err,recording,&consoleCoord,&errColor );
      }
      else
        waitTime = FLASH_TIMEOUT - flashTime;
    }
    // }}}
    DWORD didread;
    DWORD waitRet = WaitForMultipleObjects(
        waitCount,handles,FALSE,waitTime );
    if( waitRet==WAIT_TIMEOUT )
    {
      flashStart = GetTickCount() - 2*FLASH_TIMEOUT;
      continue;
    }
    else if( waitRet==WAIT_OBJECT_0+1 )
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
            cmd = HEOB_LEAK_RECORDING_START;
            break;

          case 'F':
            if( recording<=0 ) break;
            cmd = HEOB_LEAK_RECORDING_STOP;
            break;

          case 'C':
            if( recording<0 ) break;
            cmd = HEOB_LEAK_RECORDING_CLEAR;
            break;

          case 'S':
            if( recording<0 ) break;
            cmd = HEOB_LEAK_RECORDING_SHOW;
            break;
        }

        if( cmd>=HEOB_LEAK_RECORDING_CLEAR )
        {
          WriteFile( ad->controlPipe,&cmd,sizeof(int),&didread,NULL );

          // start flash of text to visualize clear/show was done {{{
          if( recording>0 )
          {
            flashStart = GetTickCount();
            recording = cmd;
            clearRecording( title,err,consoleCoord,errColor );
            showRecording( title,err,recording,&consoleCoord,&errColor );
          }
          // }}}
        }
        else if( cmd>=HEOB_LEAK_RECORDING_STOP )
        {
          // start & stop only set the recording flag, and by doing this
          // directly, it also works if the target process is
          // suspended in a debugger
          WriteProcessMemory( ad->pi.hProcess,
              ad->recordingRemote,&cmd,sizeof(int),NULL );

          int prevRecording = recording;
          if( cmd==HEOB_LEAK_RECORDING_START || recording>0 )
            recording = cmd;
          if( prevRecording!=recording )
          {
            clearRecording( title,err,consoleCoord,errColor );
            showRecording( title,err,recording,&consoleCoord,&errColor );
          }
        }
      }
      continue;
      // }}}
    }

    if( in )
      clearRecording( title,err,consoleCoord,errColor );

    if( !GetOverlappedResult(readPipe,&ov,&didread,TRUE) ||
        didread<sizeof(int) )
      break;
    needData = 1;

    switch( type )
    {
      // leaks {{{

      case WRITE_LEAKS:
        {
          allocation *alloc_a = NULL;
          int alloc_q = 0;
          unsigned char *contents = NULL;
          unsigned char **content_ptrs = NULL;
          int alloc_ignore_q = 0;
          size_t alloc_ignore_sum = 0;
          int alloc_ignore_ind_q = 0;
          size_t alloc_ignore_ind_sum = 0;

          alloc_show_q = 0;

          if( !readFile(readPipe,&alloc_q,sizeof(int),&ov) )
            break;
          if( !readFile(readPipe,&alloc_ignore_q,sizeof(int),&ov) )
            break;
          if( !readFile(readPipe,&alloc_ignore_sum,sizeof(size_t),&ov) )
            break;
          if( !readFile(readPipe,&alloc_ignore_ind_q,sizeof(int),&ov) )
            break;
          if( !readFile(readPipe,&alloc_ignore_ind_sum,sizeof(size_t),&ov) )
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
          int lDetails = opt->leakDetails ?
            ( (opt->leakDetails&1) ? LT_COUNT : LT_REACHABLE ) : 0;
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
            {
              HeapFree( heap,0,contents );
              break;
            }
            content_ptrs =
              HeapAlloc( heap,0,alloc_q*sizeof(unsigned char*) );
            size_t leakContents = opt->leakContents;
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

          printLeaks( alloc_a,alloc_q,
              alloc_ignore_q,alloc_ignore_sum,
              alloc_ignore_ind_q,alloc_ignore_ind_sum,
              content_ptrs,mi_a,mi_q,
#ifndef NO_THREADNAMES
              threadName_a,threadName_q,
#endif
              opt,tc,ds,heap,tcXml,ad,tcSvg,0 );

          if( alloc_a ) HeapFree( heap,0,alloc_a );
          if( contents ) HeapFree( heap,0,contents );
          if( content_ptrs ) HeapFree( heap,0,content_ptrs );
        }
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
        if( ds->fSymGetModuleInfo64 && ds->fSymLoadModule64 )
        {
          int m;
          IMAGEHLP_MODULE64 im;
          im.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
          for( m=0; m<mi_q; m++ )
          {
            if( !ds->fSymGetModuleInfo64(ds->process,mi_a[m].base,&im) )
              ds->fSymLoadModule64( ds->process,NULL,mi_a[m].path,NULL,
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
          ei.throwName[sizeof(ei.throwName)-1] = 0;

          cacheSymbolData( ei.aa,NULL,ei.aq,mi_a,mi_q,ds,1 );

          // exception code {{{
          const char *desc = NULL;
          switch( ei.er.ExceptionCode )
          {
#define EXCEPTION_FATAL_APP_EXIT STATUS_FATAL_APP_EXIT
#define EX_DESC( name ) \
            case EXCEPTION_##name: \
              desc = " (" #name ")"; break

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
            EX_DESC( VC_CPP_EXCEPTION );
          }
          printf( "\n$Wunhandled exception code: %x%s\n",
              ei.er.ExceptionCode,desc );
          // }}}

          if( opt->exceptionDetails && tc->out )
          {
            // modules {{{
            if( opt->exceptionDetails>2 )
            {
              printf( "$S  modules:\n" );
              int m;
              for( m=0; m<mi_q; m++ )
                printf( "    %X   %s\n",mi_a[m].base,mi_a[m].path );
            }
            // }}}

            // assembly instruction {{{
#ifndef NO_DBGENG
            if( ad->exceptionWait &&
                ei.er.ExceptionCode!=EXCEPTION_BREAKPOINT )
            {
              char *dis = disassemble(
                  ad->pi.dwProcessId,ei.aa[0].frames[0],heap );
              if( dis )
              {
                printf( "$S  assembly instruction:\n" );
                char *space = strchr( dis,' ' );
                if( space ) space[0] = 0;
                printf( "    $O%s",dis );
                if( space )
                {
                  space[0] = ' ';
                  printf( "$N%s",space );
                }
                printf( "\n" );
                HeapFree( heap,0,dis );
              }
            }
#endif
            // }}}

            // registers {{{
            printf( "$S  registers:\n" );
#define PREG( name,reg,type,before,after ) \
            printf( before "$I" name "$N=" type after,ei.c.reg )
            if( ei.c.ContextFlags&CONTEXT_INTEGER )
            {
#ifndef _WIN64
              PREG( "edi"   ,Edi   ,"%X","    "           ,     );
              PREG( "esi"   ,Esi   ,"%X","       "        ,     );
              PREG( "ebx"   ,Ebx   ,"%X","       "        ,"\n" );
              PREG( "edx"   ,Edx   ,"%X","    "           ,     );
              PREG( "ecx"   ,Ecx   ,"%X","       "        ,     );
              PREG( "eax"   ,Eax   ,"%X","       "        ,"\n" );
#else
              PREG( "rax"   ,Rax   ,"%X","    "           ,     );
              PREG( "rcx"   ,Rcx   ,"%X","  "             ,     );
              PREG( "rdx"   ,Rdx   ,"%X","  "             ,"\n" );
              PREG( "rbx"   ,Rbx   ,"%X","    "           ,     );
              PREG( "rbp"   ,Rbp   ,"%X","  "             ,     );
              PREG( "rsi"   ,Rsi   ,"%X","  "             ,"\n" );
              PREG( "rdi"   ,Rdi   ,"%X","    "           ,     );
              PREG( "r8"    ,R8    ,"%X","  "             ,     );
              PREG( "r9"    ,R9    ,"%X","   "            ,"\n" );
              PREG( "r10"   ,R10   ,"%X","    "           ,     );
              PREG( "r11"   ,R11   ,"%X","  "             ,     );
              PREG( "r12"   ,R12   ,"%X","  "             ,"\n" );
              PREG( "r13"   ,R13   ,"%X","    "           ,     );
              PREG( "r14"   ,R14   ,"%X","  "             ,     );
              PREG( "r15"   ,R15   ,"%X","  "             ,"\n" );
#endif
            }
            if( ei.c.ContextFlags&CONTEXT_CONTROL )
            {
#ifndef _WIN64
              PREG( "ebp"   ,Ebp   ,"%X","    "           ,     );
              PREG( "eip"   ,Eip   ,"%X","       "        ,     );
              PREG( "cs"    ,SegCs ,"%w","       "        ,"\n" );
              PREG( "eflags",EFlags,"%x","    "           ,     );
              PREG( "esp"   ,Esp   ,"%X","    "           ,     );
              PREG( "ss"    ,SegSs ,"%w","       "        ,"\n" );
#else
              PREG( "ss"    ,SegSs ,"%w","    "           ,     );
              PREG( "rsp"   ,Rsp   ,"%X","               ", );
              PREG( "cs"    ,SegCs ,"%w","  "             ,"\n" );
              PREG( "rip"   ,Rip   ,"%X","    "           ,     );
              PREG( "eflags",EFlags,"%x","  "             ,"\n" );
#endif
            }
            if( ei.c.ContextFlags&CONTEXT_SEGMENTS )
            {
#ifndef _WIN64
              PREG( "gs"    ,SegGs ,"%w","    "           ,     );
              PREG( "fs"    ,SegFs ,"%w","     "          ,     );
              PREG( "es"    ,SegEs ,"%w","     "          ,     );
              PREG( "ds"    ,SegDs ,"%w","     "          ,"\n" );
#else
              PREG( "ds"    ,SegDs ,"%w","    "           ,     );
              PREG( "es"    ,SegEs ,"%w","       "        ,     );
              PREG( "fs"    ,SegFs ,"%w","       "        ,     );
              PREG( "gs"    ,SegGs ,"%w","       "        ,"\n" );
#endif
            }
            // }}}
          }
#ifndef NO_DBGENG
          if( ad->exceptionWait &&
              ei.er.ExceptionCode!=EXCEPTION_BREAKPOINT )
            SetEvent( ad->exceptionWait );
#endif

          printf( "$S  exception on:" );
          printThreadName( ei.aa[0].threadNameIdx );
          printStackCount( ei.aa[0].frames,ei.aa[0].frameCount,
              mi_a,mi_q,ds,FT_COUNT,0 );

          char *addr = NULL;
          const char *violationType = NULL;
          const char *nearBlock = NULL;
          const char *blockType = NULL;
          // access violation {{{
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
                  mi_a,mi_q,ds,ei.aa[1].ft,0 );

              if( ei.aq>2 )
              {
                printf( "$S  freed on:" );
                printThreadName( ei.aa[2].threadNameIdx );
                printStackCount( ei.aa[2].frames,ei.aa[2].frameCount,
                    mi_a,mi_q,ds,ei.aa[2].ft,0 );
              }
            }
          }
          // }}}
          // VC c++ exception {{{
          else if( ei.throwName[0] )
          {
            char *throwName = undecorateVCsymbol( ds,ei.throwName );
            printf( "$I  VC c++ exception: $N%s\n",throwName );
          }
          // }}}

          writeXmlException( tcXml,ds,eiPtr,desc,addr,violationType,
              nearBlock,blockType,mi_a,mi_q );

          terminated = -1;
          *ad->heobExit = HEOB_EXCEPTION;
          *ad->heobExitData = ei.er.ExceptionCode;
#undef ei
        }
        break;

        // }}}
        // allocation failure {{{

      case WRITE_ALLOC_FAIL:
        {
          size_t mul;
          if( !readFile(readPipe,&mul,sizeof(size_t),&ov) )
            break;
          if( !readFile(readPipe,aa,sizeof(allocation),&ov) )
            break;

          cacheSymbolData( aa,NULL,1,mi_a,mi_q,ds,1 );

          size_t product;
          int mulOverflow = mul_overflow(aa->size,mul,&product);
          if( !mulOverflow ) aa->size = product;

          if( mulOverflow )
            printf( "\n$Wmultiplication overflow in allocation"
                " of %U * %U bytes\n",aa->size,mul );
          else
            printf( "\n$Wallocation failed of %U bytes\n",aa->size );
          printf( "$S  called on: $N(#%U)",aa->id );
          printThreadName( aa->threadNameIdx );
          printStackCount( aa->frames,aa->frameCount,
              mi_a,mi_q,ds,aa->ft,0 );

          writeXmlAllocFail( tcXml,ds,mul,mulOverflow,aa,mi_a,mi_q );

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

          cacheSymbolData( aa,NULL,4,mi_a,mi_q,ds,1 );

          printf( "\n$Wdeallocation of invalid pointer %p\n",aa->ptr );
          printf( "$S  called on:" );
          printThreadName( aa->threadNameIdx );
          printStackCount( aa->frames,aa->frameCount,
              mi_a,mi_q,ds,aa->ft,0 );

          // type of invalid pointer {{{
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
                mi_a,mi_q,ds,aa[1].ft,0 );

            if( aa[2].ptr )
            {
              printf( "$S  freed on:" );
              printThreadName( aa[2].threadNameIdx );
              printStackCount( aa[2].frames,aa[2].frameCount,
                  mi_a,mi_q,ds,aa[2].ft,0 );
            }
          }
          else if( aa[1].id==1 )
          {
            printf( "$I  pointing to stack\n" );
            printf( "$S  possibly same frame as:" );
            printThreadName( aa[1].threadNameIdx );
            printStackCount( aa[1].frames,aa[1].frameCount,
                mi_a,mi_q,ds,FT_COUNT,0 );
          }
          else if( aa[1].id==2 )
          {
            printf( "$I  allocated (size %U) from:\n",aa[1].size );
            if( allocMi )
              locOut( tc,allocMi->base,allocMi->path,
                  DWST_BASE_ADDR,0,NULL,opt,0 );
          }
          else if( aa[1].id==3 )
          {
            printf( "$I  pointing to global area of:\n" );
            if( allocMi )
              locOut( tc,allocMi->base,allocMi->path,
                  DWST_BASE_ADDR,0,NULL,opt,0 );
          }
          // }}}

          if( aa[3].ptr )
          {
            printf( "$I  referenced by block %p (size %U, offset +%U)\n",
                aa[3].ptr,aa[3].size,aa[2].size );
            printf( "$S  allocated on: $N(#%U)",aa[3].id );
            printThreadName( aa[3].threadNameIdx );
            printStackCount( aa[3].frames,aa[3].frameCount,
                mi_a,mi_q,ds,aa[3].ft,0 );
          }

          writeXmlFreeFail( tcXml,ds,allocMi,aa,mi_a,mi_q );

          error_q++;
        }
        break;

        // }}}
        // double free {{{

      case WRITE_DOUBLE_FREE:
        {
          if( !readFile(readPipe,aa,3*sizeof(allocation),&ov) )
            break;

          cacheSymbolData( aa,NULL,3,mi_a,mi_q,ds,1 );

          printf( "\n$Wdouble free of %p (size %U)\n",aa[1].ptr,aa[1].size );
          printf( "$S  called on:" );
          printThreadName( aa[0].threadNameIdx );
          printStackCount( aa[0].frames,aa[0].frameCount,
              mi_a,mi_q,ds,aa[0].ft,0 );

          printf( "$S  allocated on: $N(#%U)",aa[1].id );
          printThreadName( aa[1].threadNameIdx );
          printStackCount( aa[1].frames,aa[1].frameCount,
              mi_a,mi_q,ds,aa[1].ft,0 );

          printf( "$S  freed on:" );
          printThreadName( aa[2].threadNameIdx );
          printStackCount( aa[2].frames,aa[2].frameCount,
              mi_a,mi_q,ds,aa[2].ft,0 );

          writeXmlDoubleFree( tcXml,ds,aa,mi_a,mi_q );

          error_q++;
        }
        break;

        // }}}
        // slack access {{{

      case WRITE_SLACK:
        {
          if( !readFile(readPipe,aa,2*sizeof(allocation),&ov) )
            break;

          cacheSymbolData( aa,NULL,2,mi_a,mi_q,ds,1 );

          printf( "\n$Wwrite access violation at %p\n",aa[1].ptr );
          printf( "$I  slack area of %p (size %U, offset %s%D)\n",
              aa[0].ptr,aa[0].size,
              aa[1].ptr>aa[0].ptr?"+":"",(char*)aa[1].ptr-(char*)aa[0].ptr );
          printf( "$S  allocated on: $N(#%U)",aa[0].id );
          printThreadName( aa[0].threadNameIdx );
          printStackCount( aa[0].frames,aa[0].frameCount,
              mi_a,mi_q,ds,aa[0].ft,0 );
          printf( "$S  freed on:" );
          printThreadName( aa[1].threadNameIdx );
          printStackCount( aa[1].frames,aa[1].frameCount,
              mi_a,mi_q,ds,aa[1].ft,0 );

          writeXmlSlack( tcXml,ds,aa,mi_a,mi_q );

          error_q++;
        }
        break;

        // }}}
        // main allocation failure {{{

      case WRITE_MAIN_ALLOC_FAIL:
        printf( "\n$Wnot enough memory to keep track of allocations\n" );
        terminated = -1;
        *ad->heobExit = HEOB_OUT_OF_MEMORY;
        break;

        // }}}
        // mismatching allocation/release method {{{

      case WRITE_WRONG_DEALLOC:
        {
          if( !readFile(readPipe,aa,2*sizeof(allocation),&ov) )
            break;

          cacheSymbolData( aa,NULL,2,mi_a,mi_q,ds,1 );

          printf( "\n$Wmismatching allocation/release method"
              " of %p (size %U)\n",aa[0].ptr,aa[0].size );
          printf( "$S  allocated on: $N(#%U)",aa[0].id );
          printThreadName( aa[0].threadNameIdx );
          printStackCount( aa[0].frames,aa[0].frameCount,
              mi_a,mi_q,ds,aa[0].ft,0 );
          printf( "$S  freed on:" );
          printThreadName( aa[1].threadNameIdx );
          printStackCount( aa[1].frames,aa[1].frameCount,
              mi_a,mi_q,ds,aa[1].ft,0 );

          writeXmlWrongDealloc( tcXml,ds,aa,mi_a,mi_q );

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
              id,ds->funcnames[ft] );
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
        // exit trace {{{

      case WRITE_EXIT_TRACE:
        {
          allocation *exitTrace = eiPtr->aa;
          if( !readFile(readPipe,exitTrace,sizeof(allocation),&ov) )
            break;

          cacheSymbolData( exitTrace,NULL,1,mi_a,mi_q,ds,1 );
          printf( "$Sexit on:" );
          printThreadName( exitTrace->threadNameIdx );
          printStackCount( exitTrace->frames,exitTrace->frameCount,
              mi_a,mi_q,ds,FT_COUNT,0 );
        }
        break;

        // }}}
        // exit information {{{

      case WRITE_EXIT:
        if( !readFile(readPipe,exitCode,sizeof(UINT),&ov) )
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
          printf( "$Sexit code: %u (%x)\n",*exitCode,*exitCode );
        else
          printf( "\n$Stermination code: %u (%x)\n",*exitCode,*exitCode );

        *ad->heobExitData = *exitCode;
        if( opt->leakErrorExitCode )
          *exitCode = alloc_show_q + error_q;
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
            case HEOB_LEAK_RECORDING_START:
              recording = 1;
              break;
            case HEOB_LEAK_RECORDING_STOP:
              if( recording>0 ) recording = 0;
              break;
            case HEOB_LEAK_RECORDING_CLEAR:
            case HEOB_LEAK_RECORDING_SHOW:
              if( !recording ) recording = -1;
              break;
          }
        }
        break;

        // }}}
        // sampling profiler {{{

      case WRITE_SAMPLING:
        {
          int sample_times_cur = 0;
          allocation *samp_a = NULL;
          int samp_q = 0;

          if( !readFile(readPipe,&sample_times_cur,sizeof(int),&ov) )
            break;
          if( !readFile(readPipe,&samp_q,sizeof(int),&ov) )
            break;
          if( samp_q )
          {
            samp_a = HeapAlloc( heap,0,samp_q*sizeof(allocation) );
            if( !readFile(readPipe,samp_a,samp_q*sizeof(allocation),&ov) )
              break;
          }

          sample_times += sample_times_cur;

          printLeaks( samp_a,samp_q,0,0,0,0,NULL,mi_a,mi_q,
#ifndef NO_THREADNAMES
              threadName_a,threadName_q,
#endif
              opt,tc,ds,heap,tcXml,ad,tcSvg,1 );

          if( samp_a ) HeapFree( heap,0,samp_a );
        }
        break;

        // }}}
    }
  }

  if( terminated==-2 )
  {
    printf( "\n$Wunexpected end of application\n" );
    *ad->heobExit = HEOB_UNEXPECTED_END;
  }

  CloseHandle( ov.hEvent );
  HeapFree( heap,0,aa );
  HeapFree( heap,0,eiPtr );
  if( mi_a ) HeapFree( heap,0,mi_a );
#ifndef NO_THREADNAMES
  if( threadName_a ) HeapFree( heap,0,threadName_a );
#endif

  writeXmlFooter( tcXml,heap,startTicks );
  writeSvgFooter( tcSvg,ad,sample_times );
}

// }}}
// help text {{{

static void showHelpText( appData *ad,options *defopt,int fullhelp )
{
  textColor *tc = ad->tcOut;
  char *exePath = ad->exePath;
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
  {
    printf( "    $I-x$BX$N    xml output\n" );
    printf( "    $I-v$BX$N    svg output\n" );
  }
  printf( "    $I-P$BX$N    show process ID and wait [$I%d$N]\n",
      defopt->pid );
  printf( "    $I-c$BX$N    create new console [$I%d$N]\n",
      defopt->newConsole );
  printf( "    $I-p$BX$N    page protection"
      " ($I0$N = off, $I1$N = after, $I2$N = before) [$I%d$N]\n",
      defopt->protect );
  printf( "    $I-f$BX$N    freed memory protection [$I%d$N]\n",
      defopt->protectFree );
  printf( "    $I-d$BX$N    monitor dlls [$I%d$N]\n",
      defopt->dlls );
  if( fullhelp )
  {
    printf( "    $I-a$BX$N    alignment [$I%d$N]\n",
        defopt->align );
    printf( "    $I-M$BX$N    minimum page protection size [$I%d$N]\n",
        defopt->minProtectSize );
    printf( "    $I-i$BX$N    initial value [$I%d$N]\n",
        (int)(defopt->init&0xff) );
    printf( "    $I-s$BX$N    initial value for slack"
        " ($I-1$N = off) [$I%d$N]\n",
        defopt->slackInit );
  }
  printf( "    $I-h$BX$N    handle exceptions [$I%d$N]\n",
      defopt->handleException );
  printf( "    $I-R$BX$N    "
      "raise breakpoint exception on allocation # [$I%d$N]\n",
      0 );
  printf( "    $I-r$BX$N    raise breakpoint exception on error [$I%d$N]\n",
      defopt->raiseException );
  if( fullhelp )
  {
    printf( "    $I-D$BX$N    show exception details [$I%d$N]\n",
        defopt->exceptionDetails );
    printf( "    $I-S$BX$N    use stack pointer in exception [$I%d$N]\n",
        defopt->useSp );
    printf( "    $I-m$BX$N    compare allocation/release method [$I%d$N]\n",
        defopt->allocMethod );
    printf( "    $I-n$BX$N    find nearest allocation [$I%d$N]\n",
        defopt->findNearest );
    printf( "    $I-g$BX$N    group identical leaks [$I%d$N]\n",
        defopt->groupLeaks );
  }
  printf( "    $I-F$BX$N    show full path [$I%d$N]\n",
      defopt->fullPath );
  printf( "    $I-l$BX$N    show leak details [$I%d$N]\n",
      defopt->leakDetails );
  printf( "    $I-z$BX$N    minimum leak size [$I%U$N]\n",
      defopt->minLeakSize );
  printf( "    $I-k$BX$N    control leak recording [$I%d$N]\n",
      defopt->leakRecording );
  printf( "    $I-L$BX$N    show leak contents [$I%d$N]\n",
      defopt->leakContents );
  if( fullhelp )
  {
    printf( "    $I-C$BX$N    show source code [$I%d$N]\n",
        defopt->sourceCode );
    printf( "    $I-e$BX$N    show exit trace [$I%d$N]\n",
        defopt->exitTrace );
    printf( "    $I-E$BX$N    "
        "use leak and error count for exit code [$I%d$N]\n",
        defopt->leakErrorExitCode );
    printf( "    $I-I$BX$N    sampling profiler interval [$I%d$N]\n",
        defopt->samplingInterval );
    printf( "    $I-O$BA$I:$BO$I; a$Npplication specific $Io$Nptions\n" );
    printf( "    $I-X$N     "
        "disable heob via application specific options\n" );
    printf( "    $I-\"$BM$I\"$BB$N  trace mode:"
        " load $Im$Nodule on $Ib$Nase address\n" );
  }
  printf( "    $I-H$N     show full help\n" );
  printf( "\n$Ohe$Nap-$Oob$Nserver " HEOB_VER " ($O" BITS "$Nbit)\n" );
  waitForKeyIfConsoleOwner( tc,ad->in );
  exitHeob( ad,HEOB_HELP,0,0x7fffffff );
}

// }}}
// main {{{

static const char *funcnames[FT_COUNT] = {
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

CODE_SEG(".text$7") void mainCRTStartup( void )
{
  DWORD startTicks = GetTickCount();
  HANDLE heap = GetProcessHeap();
  appData *ad = initHeob( heap );
  ad->in = GetStdHandle( STD_INPUT_HANDLE );
  if( !FlushConsoleInputBuffer(ad->in) ) ad->in = NULL;

  // command line arguments {{{
  char *cmdLine = GetCommandLineA();
  char *args;
  if( cmdLine[0]=='"' && (args=strchr(cmdLine+1,'"')) )
    args++;
  else
    args = strchr( cmdLine,' ' );
  // default values {{{
  options defopt = {
    1,                              // page protection
    MEMORY_ALLOCATION_ALIGNMENT,    // alignment
    0xffffffffffffffffULL,          // initial value
    -1,                             // initial value for slack
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
    0,                              // show exception details
    0,                              // sampling profiler interval
  };
  // }}}
  options opt = defopt;
  int fullhelp = 0;
  char badArg = 0;
  int keepSuspended = 0;
  int fakeAttached = 0;
  // permanent options {{{
  opt.groupLeaks = -1;
  opt.handleException = -1;
  while( args )
  {
    while( args[0]==' ' ) args++;
    if( args[0]!='-' ) break;
    char *ro = readOption( args,&opt,ad,heap );
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
        if( ad->outName ) break;
        ad->outName = getStringOption( args+2,heap );
        break;

      case 'x':
        if( ad->xmlName ) break;
        ad->xmlName = getStringOption( args+2,heap );
        break;

      case 'v':
        if( ad->svgName ) break;
        ad->svgName = getStringOption( args+2,heap );
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

          if( ad->pi.hProcess ) break;
          char *start = args + 2;
          ad->pi.dwThreadId = (DWORD)atop( start );

          while( start[0] && start[0]!=' ' && start[0]!='/' && start[0]!='+' )
            start++;
          if( start[0]=='/' )
          {
            ad->ppid = (DWORD)atop( start+1 );
            while( start[0] && start[0]!=' ' && start[0]!='+' ) start++;
          }
          if( start[0]=='+' )
            keepSuspended = atoi( start+1 );

          ad->pi.hThread = OpenThread(
              STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0x3ff,
              FALSE,ad->pi.dwThreadId );
          if( !ad->pi.hThread ) break;

          THREAD_BASIC_INFORMATION tbi;
          RtlZeroMemory( &tbi,sizeof(THREAD_BASIC_INFORMATION) );
          if( fNtQueryInformationThread(ad->pi.hThread,ThreadBasicInformation,
                &tbi,sizeof(THREAD_BASIC_INFORMATION),NULL)!=0 )
          {
            CloseHandle( ad->pi.hThread );
            ad->pi.hThread = NULL;
            break;
          }
          ad->pi.dwProcessId = (DWORD)(ULONG_PTR)tbi.ClientId.UniqueProcess;

          ad->pi.hProcess = OpenProcess(
              STANDARD_RIGHTS_REQUIRED|SYNCHRONIZE|0xfff,
              FALSE,ad->pi.dwProcessId );
          if( !ad->pi.hProcess )
          {
            CloseHandle( ad->pi.hThread );
            ad->pi.hThread = NULL;
            break;
          }

          char eventName[32] = "heob.attach.";
          char *end = num2hexstr(
              eventName+lstrlen(eventName),GetCurrentProcessId(),8 );
          end[0] = 0;
          ad->attachEvent = OpenEvent( EVENT_ALL_ACCESS,FALSE,eventName );

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
            size_t curLen =
              ad->specificOptions ? lstrlen( ad->specificOptions ) : 0;
            size_t addLen = optionEnd - optionStart;
            if( !ad->specificOptions )
              ad->specificOptions = HeapAlloc( heap,0,curLen+addLen+1 );
            else
              ad->specificOptions = HeapReAlloc(
                  heap,0,ad->specificOptions,curLen+addLen+1 );
            RtlMoveMemory( ad->specificOptions+curLen,optionStart,addLen );
            ad->specificOptions[curLen+addLen] = 0;
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
          ad->a2l_mi_q++;
          if( !ad->a2l_mi_a )
            ad->a2l_mi_a = HeapAlloc( heap,0,ad->a2l_mi_q*sizeof(modInfo) );
          else
            ad->a2l_mi_a = HeapReAlloc(
                heap,0,ad->a2l_mi_a,ad->a2l_mi_q*sizeof(modInfo) );
          modInfo *mi = ad->a2l_mi_a + ( ad->a2l_mi_q-1 );
          mi->base = base;
          mi->size = 0;
          size_t len = end - start;
          char *localName = HeapAlloc( heap,0,MAX_PATH );
          RtlMoveMemory( localName,start,len );
          localName[len] = 0;
          if( !SearchPath(NULL,localName,NULL,MAX_PATH,mi->path,NULL) )
            RtlMoveMemory( mi->path,localName,len+1 );
          HeapFree( heap,0,localName );

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
  // enable extended stack grouping for svg by default
  if( !ad->xmlName && ad->svgName ) defopt.groupLeaks = 2;
  if( opt.groupLeaks<0 ) opt.groupLeaks = defopt.groupLeaks;
  // disable heap monitoring for sampling profiler by default
  if( opt.samplingInterval ) defopt.handleException = 2;
  if( opt.handleException<0 ) opt.handleException = defopt.handleException;
  // }}}
  if( opt.align<MEMORY_ALLOCATION_ALIGNMENT )
  {
    opt.init = 0;
    opt.slackInit = -1;
  }
  HANDLE out = GetStdHandle( STD_OUTPUT_HANDLE );
  ad->tcOut = HeapAlloc( heap,0,sizeof(textColor) );
  textColor *tc = ad->tcOut;
  checkOutputVariant( tc,out,NULL );

  // bad argument {{{
  if( badArg )
  {
    char arg0[2] = { badArg,0 };
    printf( "$Wunknown argument: $I-%s\n",arg0 );

    exitHeob( ad,HEOB_BAD_ARG,badArg,0x7fffffff );
  }
  // }}}

  // trace mode {{{
  if( ad->a2l_mi_a )
  {
    allocation *a = HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(allocation) );
    modInfo *a2l_mi_a = ad->a2l_mi_a;
    int a2l_mi_q = ad->a2l_mi_q;

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

    if( fc )
    {
      waitForKeyIfConsoleOwner( tc,ad->in );
      exitHeob( ad,HEOB_TRACE,0,0 );
    }
    args = NULL;
  }
  // }}}

  if( (!args || !args[0]) && !opt.attached )
    showHelpText( ad,&defopt,fullhelp );
  // }}}

  // wine detection {{{
  HMODULE ntdll = GetModuleHandle( "ntdll.dll" );
  if( ntdll )
  {
    typedef const char *func_wine_get_version( void );
    func_wine_get_version *fwine_get_version =
      (func_wine_get_version*)GetProcAddress( ntdll,"wine_get_version" );
    if( fwine_get_version )
    {
      printf( "$Wheob does not work with Wine\n" );
      exitHeob( ad,HEOB_WRONG_BITNESS,0,0x7fffffff );
    }
  }
  // }}}

  if( !ad->in && (opt.attached || opt.newConsole<=1) )
    opt.pid = opt.leakRecording = 0;

  if( opt.leakRecording && !opt.newConsole )
    opt.newConsole = 1;

  // create target application {{{
  ad->cmdLineW = GetCommandLineW();
  wchar_t *argsW = ad->cmdLineW;
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
  ad->argsW = argsW;

  if( !opt.attached )
  {
    STARTUPINFOW si;
    RtlZeroMemory( &si,sizeof(STARTUPINFO) );
    si.cb = sizeof(STARTUPINFO);
    BOOL result = CreateProcessW( NULL,argsW,NULL,NULL,FALSE,
        CREATE_SUSPENDED|(opt.newConsole&1?CREATE_NEW_CONSOLE:0),
        NULL,NULL,&si,&ad->pi );
    if( !result )
    {
      DWORD e = GetLastError();
      printf( "$Wcan't create process for '%s' (%e)\n",args,e );
      exitHeob( ad,HEOB_PROCESS_FAIL,e,0x7fffffff );
    }

    if( opt.newConsole>1 || isWrongArch(ad->pi.hProcess) )
    {
      HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );
      func_CreateProcessW *fCreateProcessW =
        (func_CreateProcessW*)GetProcAddress( kernel32,"CreateProcessW" );
      DWORD exitCode = 0;
      if( !heobSubProcess(0,&ad->pi,NULL,heap,&opt,fCreateProcessW,
            ad->outName,ad->xmlName,ad->svgName,NULL,
            ad->raise_alloc_q,ad->raise_alloc_a,ad->specificOptions) )
      {
        printf( "$Wcan't create process for 'heob'\n" );
        TerminateProcess( ad->pi.hProcess,1 );
        exitCode = 0x7fffffff;
      }
      else if( opt.newConsole<=2 )
      {
        WaitForSingleObject( ad->pi.hProcess,INFINITE );
        GetExitCodeProcess( ad->pi.hProcess,&exitCode );
      }

      exitHeob( ad,HEOB_CONSOLE,0,exitCode );
    }

    opt.attached = fakeAttached;
  }
  else if( ad->ppid )
    opt.newConsole = 0;
  // }}}

  // executable name {{{
  char *exePath = ad->exePath;
  if( ad->specificOptions || opt.attached || ad->outName )
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
        if( !fNtQueryInformationProcess(ad->pi.hProcess,ProcessImageFileName,
              oni,sizeof(OBJECT_NAME_INFORMATION),&len) )
        {
          oni->Name.Buffer[oni->Name.Length/2] = 0;
          wchar_t *lastDelim = strrchrW( oni->Name.Buffer,'\\' );
          if( lastDelim ) lastDelim++;
          else lastDelim = oni->Name.Buffer;
          wchar_t *lastPoint = strrchrW( lastDelim,'.' );
          if( lastPoint ) lastPoint[0] = 0;
          setHeobConsoleTitle( heap,lastDelim );
          int count = WideCharToMultiByte( CP_ACP,0,
              lastDelim,-1,exePath,MAX_PATH,NULL,NULL );
          if( count<0 || count>=MAX_PATH )
            count = 0;
          exePath[count] = 0;
        }
        HeapFree( heap,0,oni );
      }
    }
  }
  // }}}

  // application specific options {{{
  defopt = opt;
  if( ad->specificOptions )
  {
    int nameLen = lstrlen( exePath );
    char *name = ad->specificOptions;
    lstrcpy( exePath+nameLen,":" );
    int disable = 0;
    while( 1 )
    {
      char *nameEnd = strchr( name,':' );
      if( !nameEnd ) break;
      char *so = NULL;
      if( strstart(name,exePath) || strstart(name,"*" BITS ":") )
        so = nameEnd + 1;
      name = strchr( nameEnd+1,';' );
      if( !name ) break;
      name++;

      while( so )
      {
        while( so[0]==' ' ) so++;
        if( so[0]!='-' ) break;
        if( so[1]=='X' ) disable = 1;
        so = readOption( so,&opt,ad,heap );
      }
    }
    exePath[nameLen] = 0;

    if( disable )
    {
      if( !keepSuspended )
        ResumeThread( ad->pi.hThread );

      DWORD exitCode = 0;
      if( !opt.newConsole )
      {
        WaitForSingleObject( ad->pi.hProcess,INFINITE );
        GetExitCodeProcess( ad->pi.hProcess,&exitCode );
      }

      exitHeob( ad,HEOB_OK,0,exitCode );
    }
  }
  if( opt.protect<1 ) opt.protectFree = 0;
  if( opt.handleException>=2 )
    opt.protect = opt.protectFree = opt.leakDetails = 0;
  // }}}

  // output destination {{{
  const char *subOutName = NULL;
  if( ad->outName )
  {
    if( (ad->outName[0]>='0' && ad->outName[0]<='2') && !ad->outName[1] )
    {
      out = ad->outName[0]=='0' ? NULL : GetStdHandle(
          ad->outName[0]=='1' ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
      checkOutputVariant( tc,out,NULL );
    }
    else
    {
      if( strstr(ad->outName,"%p") )
      {
        subOutName = ad->outName;
        opt.children = 1;
      }

      char *fullName = expandFileNameVars( ad,ad->outName,exePath );
      char *usedName = fullName ? fullName : ad->outName;

      out = CreateFile( usedName,GENERIC_WRITE,FILE_SHARE_READ,
          NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
      if( out==INVALID_HANDLE_VALUE ) out = tc->out;

      if( fullName ) HeapFree( heap,0,fullName );
    }
    if( out!=tc->out )
    {
      ad->tcOutOrig = ad->tcOut;
      tc = ad->tcOut = HeapAlloc( heap,0,sizeof(textColor) );
      checkOutputVariant( tc,out,ad->exePath );
    }
  }
  else if( ad->xmlName || ad->svgName )
  {
    out = tc->out = NULL;
    tc->fTextColor = NULL;
  }
  if( !tc->out && !ad->tcOutOrig && opt.attached )
  {
    ad->tcOutOrig = HeapAlloc( heap,0,sizeof(textColor) );
    checkOutputVariant( ad->tcOutOrig,GetStdHandle(STD_OUTPUT_HANDLE),NULL );
  }
  if( !out )
    opt.sourceCode = opt.leakContents = 0;
  // }}}

  const char *subXmlName = NULL;
  if( ad->xmlName && strstr(ad->xmlName,"%p") )
  {
    subXmlName = ad->xmlName;
    opt.children = 1;
  }
  const char *subSvgName = NULL;
  if( ad->svgName && strstr(ad->svgName,"%p") )
  {
    subSvgName = ad->svgName;
    opt.children = 1;
  }

  wchar_t *subCurDir = NULL;
  if( opt.children )
  {
    subCurDir = ad->subCurDir;
    if( !GetCurrentDirectoryW(MAX_PATH,subCurDir) )
      subCurDir[0] = 0;
  }

  if( opt.leakRecording )
  {
    DWORD flags;
    if( GetConsoleMode(ad->in,&flags) )
      SetConsoleMode( ad->in,flags & ~ENABLE_MOUSE_INPUT );
  }

  ad->err = GetStdHandle( STD_ERROR_HANDLE );
  unsigned heobExit = HEOB_OK;
  unsigned heobExitData = 0;
  ad->heobExit = &heobExit;
  ad->heobExitData = &heobExitData;
  if( isWrongArch(ad->pi.hProcess) )
  {
    printf( "$Wonly " BITS "bit applications possible\n" );
    heobExit = HEOB_WRONG_BITNESS;
  }
  else
    ad->readPipe = inject( ad,&opt,&defopt,tc,
        subOutName,subXmlName,subSvgName,subCurDir );
  if( !ad->readPipe )
    TerminateProcess( ad->pi.hProcess,1 );

  UINT exitCode = 0x7fffffff;
  if( ad->readPipe )
  {
    int count = WideCharToMultiByte( CP_ACP,0,
        ad->exePathW,-1,exePath,MAX_PATH,NULL,NULL );
    if( count<0 || count>=MAX_PATH ) count = 0;
    exePath[count] = 0;
    char *delim = strrchr( exePath,'\\' );
    if( delim ) delim[0] = 0;
    dbgsym ds;
    dbgsym_init( &ds,ad->pi.hProcess,tc,&opt,funcnames,heap,exePath,TRUE,
        RETURN_ADDRESS() );
    if( delim ) delim[0] = '\\';

    // debugger PID {{{
    if( ad->writeProcessPid )
    {
      unsigned data[2] = { HEOB_PID_ATTACH,ad->pi.dwProcessId };
      DWORD didreadwrite;
      WriteFile( ad->errorPipe,data,sizeof(data),&didreadwrite,NULL );
      if( !ReadFile(ad->errorPipe,&data,sizeof(data),&didreadwrite,NULL) ||
          didreadwrite!=sizeof(data) )
      {
        data[0] = HEOB_CONTROL_NONE;
        data[1] = 0;
      }
      ad->dbgPid = data[1];
    }
    // }}}

    printAttachedProcessInfo( ad,tc );
    if( ad->tcOutOrig )
      printAttachedProcessInfo( ad,ad->tcOutOrig );

    // console title {{{
    wchar_t *delimW = strrchrW( ad->exePathW,'\\' );
    if( delimW ) delimW++;
    else delimW = ad->exePathW;
    wchar_t *lastPointW = strrchrW( delimW,'.' );
    if( lastPointW ) lastPointW[0] = 0;
    setHeobConsoleTitle( heap,delimW );
    if( lastPointW ) lastPointW[0] = '.';
    // }}}

    if( opt.pid )
    {
      tc->out = ad->err;
      printf( "\n-------------------- PID %u --------------------\n",
          ad->pi.dwProcessId );

      showConsole();
      waitForKey( tc,ad->in );

      printf( " done\n\n" );
      tc->out = out;
    }

    if( opt.handleException>=2 && !opt.samplingInterval )
      opt.leakRecording = 0;

    if( ad->in && !opt.leakRecording && tc->fTextColor!=&TextColorConsole &&
        isConsoleOwner() )
      FreeConsole();

    if( !keepSuspended )
      ResumeThread( ad->pi.hThread );

    if( ad->attachEvent )
    {
      SetEvent( ad->attachEvent );
      CloseHandle( ad->attachEvent );
      ad->attachEvent = NULL;
    }

    mainLoop( ad,&ds,startTicks,&exitCode );

    dbgsym_close( &ds );
  }

  exitHeob( ad,heobExit,heobExitData,exitCode );
}

// }}}

// vim:fdm=marker
