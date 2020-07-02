
//          Copyright Hannes Domani 2014 - 2020.
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

#include <shobjidl.h>

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

wchar_t *num2hexstrW( wchar_t *str,UINT64 arg,int count )
{
  char s[16];
  char *e = num2hexstr( s,arg,count );
  int l = (int)( e - s );
  int i;
  for( i=0; i<l; i++ )
    str++[i] = s[i];
  return( str );
}

static NOINLINE char *num2str( char *start,uintptr_t arg,int minus )
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

NOINLINE wchar_t *num2strW( wchar_t *start,uintptr_t arg,int minus )
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
  if( !tc || !tc->out ) return;

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

        case 't': // milliseconds
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

        case 'T': // FILETIME*
          {
            FILETIME *ft = va_arg( vl,FILETIME* );
            SYSTEMTIME st,stl;
            if( !FileTimeToSystemTime(ft,&st) ||
                !SystemTimeToTzSpecificLocalTime(NULL,&st,&stl) )
              RtlZeroMemory( &stl,sizeof(stl) );
            int y = stl.wYear;
            int m = stl.wMonth;
            int d = stl.wDay;
            int h = stl.wHour;
            int mi = stl.wMinute;
            int s = stl.wSecond;
            char timestr[19] = {
              y/1000+'0',(y/100)%10+'0',(y/10)%10+'0',y%10+'0','.',
              m  /10+'0',m      %10+'0','.',
              d  /10+'0',d      %10+'0',' ',
              h  /10+'0',h      %10+'0',':',
              mi /10+'0',mi     %10+'0',':',
              s  /10+'0',s      %10+'0',
            };
            tc->fWriteText( tc,timestr,19 );
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
          // percent (%) character {{{

        case '%':
          format = ptr + 1;
          ptr += 2;
          continue;

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

#ifndef NO_DBGENG
static NOINLINE char *mstrchr( const char *s,char c )
{
  for( ; *s; s++ )
    if( *s==c ) return( (char*)s );
  return( NULL );
}
#define strchr mstrchr
#endif

static NOINLINE wchar_t *mstrchrW( const wchar_t *s,wchar_t c )
{
  for( ; *s; s++ )
    if( *s==c ) return( (wchar_t*)s );
  return( NULL );
}
#define strchrW mstrchrW

NOINLINE wchar_t *mstrrchrW( const wchar_t *s,wchar_t c )
{
  wchar_t *ret = NULL;
  for( ; *s; s++ )
    if( *s==c ) ret = (wchar_t*)s;
  return( ret );
}
#define strrchrW mstrrchrW

static NOINLINE uint64_t wtou64( const wchar_t *s )
{
  uint64_t ret = 0;

  if( s[0]=='0' && s[1]=='x' )
  {
    s += 2;
    for( ; ; s++ )
    {
      wchar_t c = *s;
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
static inline uintptr_t wtop( const wchar_t *s )
{
  return( (uintptr_t)wtou64(s) );
}
static inline int mwtoi( const wchar_t *s )
{
  return( (int)wtop(s) );
}
#define wtoi mwtoi

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

static int ptrcmp( const uintptr_t *p1,const uintptr_t *p2,size_t s )
{
  size_t i;
  for( i=0; i<s; i++ )
    if( p1[i]!=p2[i] ) return( p2[i]>p1[i] ? 1 : -1 );
  return( 0 );
}

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

static NOINLINE wchar_t *mstrstrW( const wchar_t *s,const wchar_t *f )
{
  int ls = lstrlenW( s );
  int lf = lstrlenW( f );
  if( lf>ls ) return( NULL );
  if( !lf ) return( (wchar_t*)s );
  int ld = ls - lf + 1;
  int i;
  for( i=0; i<ld; i++ )
    if( !mmemcmp(s+i,f,2*lf) ) return( (wchar_t*)s+i );
  return( NULL );
}
#define strstrW mstrstrW

static NOINLINE wchar_t *strreplace(
    const wchar_t *str,const wchar_t *from,const wchar_t *to,HANDLE heap )
{
  const wchar_t *pos = strstrW( str,from );
  if( !pos ) return( NULL );

  int strLen = lstrlenW( str );
  int fromLen = lstrlenW( from );
  int toLen = lstrlenW( to );
  wchar_t *replace = HeapAlloc( heap,0,2*(strLen-fromLen+toLen+1) );
  if( !replace ) return( NULL );

  int replacePos = 0;
  if( pos>str )
  {
    RtlMoveMemory( replace,str,2*(pos-str) );
    replacePos += (int)( pos - str );
  }
  if( toLen )
  {
    RtlMoveMemory( replace+replacePos,to,2*toLen );
    replacePos += toLen;
  }
  if( str+strLen>pos+fromLen )
  {
    int endLen = (int)( (str+strLen) - (pos+fromLen) );
    RtlMoveMemory( replace+replacePos,pos+fromLen,2*endLen );
    replacePos += endLen;
  }
  replace[replacePos] = 0;
  return( replace );
}

static NOINLINE wchar_t *strreplacenum(
    const wchar_t *str,const wchar_t *from,uintptr_t to,HANDLE heap )
{
  wchar_t numStr[32];
  wchar_t *numEnd = numStr + sizeof(numStr)/2;
  (--numEnd)[0] = 0;
  wchar_t *numStart = num2strW( numEnd,to,0 );

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

static int strstartW( const wchar_t *str,const wchar_t *start )
{
  int l1 = lstrlenW( str );
  int l2 = lstrlenW( start );
  if( l1<l2 ) return( 0 );
  return( CompareStringW(LOCALE_SYSTEM_DEFAULT,NORM_IGNORECASE,
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

static void writeHeobInfo( textColor *tc,const char *t )
{
  printf(
      "<!-- %s of memory leak / profiling data.\n"
      "     Generated by heob %s.\n"
      "     https://github.com/ssbssa/heob -->\n\n",
      t,HEOB_VER );
}

static void checkOutputVariant( textColor *tc,HANDLE out,
    const wchar_t *exeName )
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

        writeHeobInfo( tc,"HTML output" );
        printf(
            "<head>\n"
            "<style type=\"text/css\">\n"
            "body { color:lightgrey; background-color:black; }\n"
            ".ok { color:lime; }\n"
            ".section { color:turquoise; }\n"
            ".info { color:violet; }\n"
            ".warn { color:red; }\n"
            ".base { color:black; background-color:grey; }\n"
            "</style>\n"
            "<title>%S%sheob %s</title>\n"
            "</head><body>\n"
            "<h3>%S</h3>\n"
            "<pre>\n",
            exeName,exeName?" - ":NULL,HEOB_VER,GetCommandLineW() );
        // }}}
      }
    }
    HeapFree( heap,0,oni );
  }
}

static void deleteFileOnClose( textColor *tc )
{
  if( !tc ) return;

  HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );

  typedef struct _FILE_DISPOSITION_INFO {
    BOOLEAN DeleteFile;
  } FILE_DISPOSITION_INFO;

  typedef enum _FILE_INFO_BY_HANDLE_CLASS {
    FileDispositionInfo=4,
  } FILE_INFO_BY_HANDLE_CLASS;

  typedef BOOL WINAPI func_SetFileInformationByHandle(
      HANDLE,FILE_INFO_BY_HANDLE_CLASS,LPVOID,DWORD );

  func_SetFileInformationByHandle *fSetFileInformationByHandle =
    (func_SetFileInformationByHandle*)GetProcAddress(
        kernel32,"SetFileInformationByHandle" );
  if( !fSetFileInformationByHandle ) return;

  FILE_DISPOSITION_INFO fdi;
  fdi.DeleteFile = TRUE;
  fSetFileInformationByHandle( tc->out,FileDispositionInfo,&fdi,sizeof(fdi) );
}

// }}}
// process bitness / name {{{

int isWrongArch( HANDLE process )
{
  BOOL remoteWow64,meWow64;
  IsWow64Process( process,&remoteWow64 );
  IsWow64Process( GetCurrentProcess(),&meWow64 );
  return( remoteWow64!=meWow64 );
}

int convertDeviceName( const wchar_t *in,wchar_t *out,int outlen )
{
  wchar_t drives[128];
  if( !GetLogicalDriveStringsW(127,drives) ) return( 0 );

  wchar_t name[MAX_PATH];
  wchar_t drive[3] = L" :";
  wchar_t *p = drives;
  do
  {
    drive[0] = *p;

    if( QueryDosDeviceW(drive,name,MAX_PATH) && strstartW(in,name) )
    {
      if( lstrlenW(in+lstrlenW(name))+2>=outlen ) return( 0 );
      lstrcpyW( out,drive );
      lstrcatW( out,in+lstrlenW(name) );
      return( 1 );
    }

    while( *p++ );
  }
  while( *p );

  return( 0 );
}

static void nameOfProcess( HMODULE ntdll,HANDLE heap,
    HANDLE process,wchar_t *name,int noExt )
{
  name[0] = 0;
  if( !ntdll ) return;

  func_NtQueryInformationProcess *fNtQueryInformationProcess =
    (func_NtQueryInformationProcess*)GetProcAddress(
        ntdll,"NtQueryInformationProcess" );
  if( !fNtQueryInformationProcess ) return;

  OBJECT_NAME_INFORMATION *oni =
    HeapAlloc( heap,0,sizeof(OBJECT_NAME_INFORMATION) );
  if( !oni ) return;

  ULONG len;
  if( !fNtQueryInformationProcess(process,ProcessImageFileName,
        oni,sizeof(OBJECT_NAME_INFORMATION),&len) )
  {
    oni->Name.Buffer[oni->Name.Length/2] = 0;
    if( noExt>=0 || !convertDeviceName(oni->Name.Buffer,name,MAX_PATH) )
    {
      wchar_t *lastDelim = strrchrW( oni->Name.Buffer,'\\' );
      if( lastDelim ) lastDelim++;
      else lastDelim = oni->Name.Buffer;
      if( noExt>0 )
      {
        wchar_t *lastPoint = strrchrW( lastDelim,'.' );
        if( lastPoint ) lastPoint[0] = 0;
      }
      lstrcpynW( name,lastDelim,MAX_PATH );
    }
  }
  HeapFree( heap,0,oni );
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

struct dbgsym;

typedef struct sharedAppCounter
{
  LONG count;
}
sharedAppCounter;

#if USE_STACKWALK
typedef struct dump_memory_loc
{
  const char *ptr;
  size_t size;
  size_t address;
}
dump_memory_loc;
#endif

typedef struct appData
{
  HANDLE heap;
  options *opt;
  int globalHotkeys;
  struct dbgsym *ds;
  HANDLE errorPipe;
  int writeProcessPid;
  HANDLE attachEvent;
  PROCESS_INFORMATION pi;
  wchar_t exePathW[MAX_PATH];
  wchar_t subCurDir[MAX_PATH];
  textColor *tcOut;
  textColor *tcOutOrig;
  size_t *raise_alloc_a;
  int raise_alloc_q;
  wchar_t *outName;
  wchar_t *xmlName;
  wchar_t *svgName;
  wchar_t *symPath;
  wchar_t *specificOptions;
  modInfo *mi_a;
  int mi_q;
  DWORD ppid;
  wchar_t pExePath[MAX_PATH];
  DWORD dbgPid;
  attachedProcessInfo *api;
  wchar_t *cmdLineW;
  wchar_t *argsW;
  HANDLE exceptionWait;
#ifndef NO_DBGHELP
  HANDLE miniDumpWait;
#endif
  HANDLE in;
  HANDLE err;
  HANDLE readPipe;
  HANDLE controlPipe;
  unsigned *heobExit;
  unsigned *heobExitData;
  int *recordingRemote;
  size_t svgSum;
  int appCounter;
  DWORD appCounterID;
  HANDLE appCounterMapping;
  size_t kernel32offset;
  DWORD startTicks;

#if USE_STACKWALK
  int *noStackWalkRemote;

  CRITICAL_SECTION csSampling;
  HANDLE samplingInit;
  HANDLE samplingStop;
  int recordingSamples;
  allocation *samp_a;
  int samp_q;
  int samp_s;
  size_t samp_id;
  threadSamplingType *thread_samp_a;
  int thread_samp_q;
  int thread_samp_s;

  dump_memory_loc *dump_mem_a;
  int dump_mem_q;
  const char **dump_mi_map_a;
  MINIDUMP_MODULE *dump_mod_a;
#endif
}
appData;

static appData *initHeob( HANDLE heap )
{
  appData *ad = HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(appData) );
  ad->heap = heap;
  ad->errorPipe = openErrorPipe( &ad->writeProcessPid );
  ad->startTicks = GetTickCount();
#if USE_STACKWALK
  HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );
  func_InitializeCriticalSectionEx *fInitCritSecEx =
    (func_InitializeCriticalSectionEx*)GetProcAddress(
        kernel32,"InitializeCriticalSectionEx" );
  if( fInitCritSecEx )
    fInitCritSecEx( &ad->csSampling,4000,CRITICAL_SECTION_NO_DEBUG_INFO );
  else
    InitializeCriticalSection( &ad->csSampling );
#endif
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
  if( ad->symPath ) HeapFree( heap,0,ad->symPath );
  if( ad->specificOptions ) HeapFree( heap,0,ad->specificOptions );
  if( ad->mi_a ) HeapFree( heap,0,ad->mi_a );
  if( ad->api ) HeapFree( heap,0,ad->api );

#if USE_STACKWALK
  DeleteCriticalSection( &ad->csSampling );
  if( ad->samplingStop ) CloseHandle( ad->samplingStop );
  if( ad->thread_samp_a ) HeapFree( heap,0,ad->thread_samp_a );
  if( ad->samp_a ) HeapFree( heap,0,ad->samp_a );

  if( ad->dump_mem_a ) HeapFree( heap,0,ad->dump_mem_a );
#endif

  if( ad->exceptionWait ) CloseHandle( ad->exceptionWait );
#ifndef NO_DBGHELP
  if( ad->miniDumpWait ) CloseHandle( ad->miniDumpWait );
#endif
  if( ad->readPipe ) CloseHandle( ad->readPipe );
  if( ad->controlPipe ) CloseHandle( ad->controlPipe );
  if( ad->appCounterMapping ) CloseHandle( ad->appCounterMapping );
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

  if( UNLIKELY(kernel32!=rd->kernel32) )
  {
    // kernel32.dll address changed -> rebase function pointers
    size_t modDiff = (size_t)kernel32 - (size_t)rd->kernel32;
    void *funcs[] = {
      &rd->fQueueUserAPC,
      &rd->fGetCurrentThread,
      &rd->fVirtualProtect,
      &rd->fGetCurrentProcess,
      &rd->fFlushInstructionCache,
      &rd->fLoadLibraryW,
      &rd->fGetProcAddress,
    };
    unsigned funcCount = sizeof(funcs)/sizeof(funcs[0]);
    unsigned f;
    for( f=0; f<funcCount; f++ )
      *(size_t*)funcs[f] += modDiff;
    rd->kernel32 = kernel32;
  }

  HMODULE app = rd->fLoadLibraryW( rd->exePath );
  rd->heobMod = app;

  func_heob *fheob = (func_heob*)( (size_t)app + rd->injOffset );
  rd->fQueueUserAPC( fheob,rd->fGetCurrentThread(),(ULONG_PTR)rd );
}

static CODE_SEG(".text$2") HANDLE inject(
    appData *ad,options *globalopt,textColor *tc,
    const wchar_t *subOutName,const wchar_t *subXmlName,
    const wchar_t *subSvgName,const wchar_t *subCurDir,
    const wchar_t *subSymPath )
{
  // injection data {{{
  func_inj *finj = &remoteCall;
  size_t funcOffset = sizeof(remoteData) + ad->raise_alloc_q*sizeof(size_t);
  size_t funcSize = (size_t)&inject - (size_t)finj;
  size_t soOffset = funcOffset + funcSize;
  size_t soSize =
    ( ad->specificOptions ? 2*lstrlenW(ad->specificOptions) + 2 : 0 );
  size_t fullSize = soOffset + soSize;
  HANDLE process = ad->pi.hProcess;
  HANDLE thread = ad->pi.hThread;
  wchar_t *exePath = ad->exePathW;
  options *opt = ad->opt;

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

  if( opt->leakRecording || ad->globalHotkeys )
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

  {
    ad->exceptionWait = CreateEvent( NULL,FALSE,FALSE,NULL );
    DuplicateHandle( GetCurrentProcess(),ad->exceptionWait,
        process,&data->exceptionWait,0,FALSE,
        DUPLICATE_SAME_ACCESS );
  }

#ifndef NO_DBGHELP
  if( opt->exceptionDetails<0 )
  {
    ad->miniDumpWait = CreateEvent( NULL,FALSE,FALSE,NULL );
    DuplicateHandle( GetCurrentProcess(),ad->miniDumpWait,
        process,&data->miniDumpWait,0,FALSE,
        DUPLICATE_SAME_ACCESS );
  }
#endif

#if USE_STACKWALK
  DuplicateHandle( GetCurrentProcess(),GetCurrentProcess(),
      process,&data->heobProcess,0,FALSE,DUPLICATE_SAME_ACCESS );
  if( opt->samplingInterval )
  {
    ad->samplingStop = CreateEvent( NULL,TRUE,FALSE,NULL );
    DuplicateHandle( GetCurrentProcess(),ad->samplingStop,
        process,&data->samplingStop,0,FALSE,DUPLICATE_SAME_ACCESS );
  }
#endif

  RtlMoveMemory( &data->opt,opt,sizeof(options) );
  RtlMoveMemory( &data->globalopt,globalopt,sizeof(options) );

  if( subOutName ) lstrcpynW( data->subOutName,subOutName,MAX_PATH );
  if( subXmlName ) lstrcpynW( data->subXmlName,subXmlName,MAX_PATH );
  if( subSvgName ) lstrcpynW( data->subSvgName,subSvgName,MAX_PATH );
  if( subCurDir ) lstrcpynW( data->subCurDir,subCurDir,MAX_PATH );
  if( subSymPath ) lstrcpynW( data->subSymPath,subSymPath,16384 );

  data->raise_alloc_q = ad->raise_alloc_q;
  if( ad->raise_alloc_q )
    RtlMoveMemory( data->raise_alloc_a,
        ad->raise_alloc_a,ad->raise_alloc_q*sizeof(size_t) );

  if( soSize )
  {
    data->specificOptions = (wchar_t*)( fullDataRemote + soOffset );
    RtlMoveMemory( fullData+soOffset,ad->specificOptions,soSize );
  }

  data->appCounterID = ad->appCounterID;
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

  HANDLE h[3] = { initFinished,process,in };
  int waitCount = in ? 3 : 2;
  while( 1 )
  {
    DWORD w = WaitForMultipleObjects( waitCount,h,FALSE,INFINITE );
    if( w==WAIT_OBJECT_0 )
      break;

    if( w==WAIT_OBJECT_0+1 )
    {
      CloseHandle( readPipe );
      readPipe = NULL;
      *ad->heobExit = HEOB_PROCESS_FAIL;
      break;
    }

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

    if( *ad->heobExit==HEOB_PROCESS_FAIL )
      return( NULL );

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

#if USE_STACKWALK
  ad->noStackWalkRemote = data->noStackWalkRemote;
#endif
  ad->recordingRemote = data->recordingRemote;

  ad->kernel32offset = (size_t)data->kernel32 - (size_t)kernel32;
  // }}}

  HeapFree( heap,0,fullData );

  SuspendThread( thread );
  CONTEXT context;
  RtlZeroMemory( &context,sizeof(CONTEXT) );
  context.ContextFlags = CONTEXT_FULL;
  GetThreadContext( thread,&context );
  SetEvent( startMain );
  CloseHandle( startMain );

  return( readPipe );
}

// }}}
// stacktrace {{{

#ifndef NO_DWARFSTACK
typedef int func_dwstOfFile( const char*,uint64_t,uint64_t*,int,
    dwstCallback*,struct dbgsym* );
typedef int func_dwstOfFileW( const wchar_t*,uint64_t,uint64_t*,int,
    dwstCallbackW*,struct dbgsym* );
typedef size_t func_dwstDemangle( const char*,char*,size_t );
#endif

#ifdef _WIN64
#define BITS "64"
#else
#define BITS "32"
#endif

typedef struct sourceLocation
{
  const wchar_t *filename;
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
  func_SymGetLineFromAddrW64 *fSymGetLineFromAddrW64;
  func_SymFromAddr *fSymFromAddr;
  func_SymAddrIncludeInlineTrace *fSymAddrIncludeInlineTrace;
  func_SymQueryInlineTrace *fSymQueryInlineTrace;
  func_SymGetLineFromInlineContextW *fSymGetLineFromInlineContextW;
  func_SymFromInlineContext *fSymFromInlineContext;
  func_SymGetModuleInfo64 *fSymGetModuleInfo64;
  func_SymLoadModule64 *fSymLoadModule64;
  func_SymLoadModuleExW *fSymLoadModuleExW;
  func_UnDecorateSymbolName *fUnDecorateSymbolName;
  func_MiniDumpWriteDump *fMiniDumpWriteDump;
#if USE_STACKWALK
  stackwalkFunctions swf;
  func_SymFindFileInPathW *fSymFindFileInPathW;
#endif
  IMAGEHLP_LINE64 *il;
  IMAGEHLP_LINEW64 *ilW;
  SYMBOL_INFO *si;
  char *undname;
#endif
#ifndef NO_DWARFSTACK
  HMODULE dwstMod;
  func_dwstOfFile *fdwstOfFile;
  func_dwstOfFileW *fdwstOfFileW;
  func_dwstDemangle *fdwstDemangle;
#endif
  wchar_t *absPath;
  wchar_t *filenameWide;
  char *ansiPath;
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
  wchar_t **file_a;
  int file_q;
}
dbgsym;

#define sorted_add_func( name,type,cmp_fump,len_func,len_fact ) \
static const type *name( const type *str, \
    type ***str_a,int *str_q,HANDLE heap ) \
{ \
  if( !str || !str[0] ) return( NULL ); \
  \
  type **a = *str_a; \
  int q = *str_q; \
  \
  int s = 0; \
  int e = q; \
  int i = q/2; \
  while( e>s ) \
  { \
    int cmp = cmp_fump( str,a[i] ); \
    if( !cmp ) return( a[i] ); \
    if( cmp<0 ) e = i; \
    else s = i + 1; \
    i = ( s+e )/2; \
  } \
  \
  if( !(q&63) ) \
  { \
    int c = q + 64; \
    if( !q ) a = HeapAlloc( heap,0,c*sizeof(type*) ); \
    else a = HeapReAlloc( heap,0,a,c*sizeof(type*) ); \
  } \
  \
  if( i<q ) RtlMoveMemory( a+i+1,a+i,(q-i)*sizeof(type*) ); \
  \
  int l = len_fact*( len_func(str) + 1 ); \
  type *copy = HeapAlloc( heap,0,l ); \
  RtlMoveMemory( copy,str,l ); \
  a[i] = copy; \
  q++; \
  \
  *str_a = a; \
  *str_q = q; \
  \
  return( copy ); \
}
sorted_add_func( strings_add,char,lstrcmp,lstrlen,1 )
sorted_add_func( strings_addW,wchar_t,lstrcmpW,lstrlenW,2 )

static void dbgsym_init( dbgsym *ds,HANDLE process,textColor *tc,options *opt,
    const char **funcnames,HANDLE heap,const wchar_t *dbgPath,BOOL invade,
    void *threadInitAddr )
{
  RtlZeroMemory( ds,sizeof(dbgsym) );
  ds->process = process;
  ds->tc = tc;
  ds->opt = opt;
  ds->funcnames = funcnames;
  ds->threadInitAddr = (uintptr_t)threadInitAddr;
  ds->heap = heap;
  ds->ansiPath = HeapAlloc( heap,0,MAX_PATH );

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
    func_SymInitializeW *fSymInitializeW =
      (func_SymInitializeW*)GetProcAddress( ds->symMod,"SymInitializeW" );
    ds->fSymGetLineFromAddr64 =
      (func_SymGetLineFromAddr64*)GetProcAddress(
          ds->symMod,"SymGetLineFromAddr64" );
    ds->fSymGetLineFromAddrW64 =
      (func_SymGetLineFromAddrW64*)GetProcAddress(
          ds->symMod,"SymGetLineFromAddrW64" );
    ds->fSymFromAddr =
      (func_SymFromAddr*)GetProcAddress( ds->symMod,"SymFromAddr" );
    ds->fSymAddrIncludeInlineTrace =
      (func_SymAddrIncludeInlineTrace*)GetProcAddress(
          ds->symMod,"SymAddrIncludeInlineTrace" );
    ds->fSymQueryInlineTrace =
      (func_SymQueryInlineTrace*)GetProcAddress(
          ds->symMod,"SymQueryInlineTrace" );
    ds->fSymGetLineFromInlineContextW =
      (func_SymGetLineFromInlineContextW*)GetProcAddress( ds->symMod,
          "SymGetLineFromInlineContextW" );
    ds->fSymFromInlineContext =
      (func_SymFromInlineContext*)GetProcAddress(
          ds->symMod,"SymFromInlineContext" );
    if( !ds->fSymQueryInlineTrace || !ds->fSymGetLineFromInlineContextW ||
        !ds->fSymFromInlineContext )
      ds->fSymAddrIncludeInlineTrace = NULL;
    ds->fSymGetModuleInfo64 =
      (func_SymGetModuleInfo64*)GetProcAddress(
          ds->symMod,"SymGetModuleInfo64" );
    ds->fSymLoadModule64 =
      (func_SymLoadModule64*)GetProcAddress( ds->symMod,"SymLoadModule64" );
    ds->fSymLoadModuleExW =
      (func_SymLoadModuleExW*)GetProcAddress( ds->symMod,"SymLoadModuleExW" );
    ds->fUnDecorateSymbolName =
      (func_UnDecorateSymbolName*)GetProcAddress(
          ds->symMod,"UnDecorateSymbolName" );
#if USE_STACKWALK
    ds->swf.fStackWalk64 = (func_StackWalk64*)GetProcAddress(
        ds->symMod,"StackWalk64" );
    ds->swf.fSymFunctionTableAccess64 =
      (PFUNCTION_TABLE_ACCESS_ROUTINE64)GetProcAddress(
          ds->symMod,"SymFunctionTableAccess64" );
    ds->swf.fSymGetModuleBase64 =
      (PGET_MODULE_BASE_ROUTINE64)GetProcAddress(
          ds->symMod,"SymGetModuleBase64" );
    ds->swf.fReadProcessMemory = NULL;
    ds->fSymFindFileInPathW =
      (func_SymFindFileInPathW*)GetProcAddress(
          ds->symMod,"SymFindFileInPathW" );
#endif
    ds->fMiniDumpWriteDump = (func_MiniDumpWriteDump*)GetProcAddress(
        ds->symMod,"MiniDumpWriteDump" );
    ds->il = HeapAlloc( heap,0,sizeof(IMAGEHLP_LINE64) );
    ds->ilW = HeapAlloc( heap,0,sizeof(IMAGEHLP_LINEW64) );
    ds->si = HeapAlloc( heap,0,sizeof(SYMBOL_INFO)+MAX_SYM_NAME );
    ds->undname = HeapAlloc( heap,0,MAX_SYM_NAME+1 );

    if( fSymSetOptions )
      fSymSetOptions( SYMOPT_LOAD_LINES|SYMOPT_DEFERRED_LOADS );
    if( fSymInitializeW )
      fSymInitializeW( ds->process,dbgPath,invade );
    else if( fSymInitialize )
    {
      char *ansiPath = NULL;
      if( dbgPath )
      {
        int count = WideCharToMultiByte( CP_ACP,0,
            dbgPath,-1,NULL,0,NULL,NULL );
        if( count>0 )
        {
          ansiPath = HeapAlloc( heap,0,count+1 );
          int count2 = WideCharToMultiByte( CP_ACP,0,
              dbgPath,-1,ansiPath,count,NULL,NULL );
          if( count<=0 || count2>count )
          {
            HeapFree( heap,0,ansiPath );
            ansiPath = NULL;
          }
        }
      }
      fSymInitialize( ds->process,ansiPath,invade );
      if( ansiPath ) HeapFree( heap,0,ansiPath );
    }
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
    ds->fdwstOfFileW =
      (func_dwstOfFileW*)GetProcAddress( ds->dwstMod,"dwstOfFileW" );
    ds->fdwstDemangle =
      (func_dwstDemangle*)GetProcAddress( ds->dwstMod,"dwstDemangle" );
  }
#endif

  ds->absPath = HeapAlloc( heap,0,2*MAX_PATH );
  ds->filenameWide = HeapAlloc( heap,0,2*MAX_PATH );
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

  char **func_a = ds->func_a;
  int func_q = ds->func_q;
  for( i=0; i<func_q; i++ )
    HeapFree( heap,0,func_a[i] );
  if( func_a )
    HeapFree( heap,0,func_a );

  wchar_t **file_a = ds->file_a;
  int file_q = ds->file_q;
  for( i=0; i<file_q; i++ )
    HeapFree( heap,0,file_a[i] );
  if( file_a )
    HeapFree( heap,0,file_a );

  ds->ssl = NULL;
  ds->sslCount = 0;
  ds->func_a = NULL;
  ds->func_q = 0;
  ds->file_a = NULL;
  ds->file_q = 0;
}

#ifndef NO_DBGHELP
static void dbgsym_loadmodule( dbgsym *ds,
    const wchar_t *path,DWORD64 base,DWORD size )
{
  if( ds->fSymLoadModuleExW )
    ds->fSymLoadModuleExW( ds->process,NULL,path,NULL,base,size,NULL,0 );
  else
  {
    char *ansiPath = ds->ansiPath;
    int count = WideCharToMultiByte( CP_ACP,0,
        path,-1,ansiPath,MAX_PATH,NULL,NULL );
    if( count>0 && count<MAX_PATH )
      ds->fSymLoadModule64( ds->process,NULL,ansiPath,NULL,base,size );
  }
}
#endif

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
  if( ds->ilW ) HeapFree( heap,0,ds->ilW );
  if( ds->si ) HeapFree( heap,0,ds->si );
  if( ds->undname ) HeapFree( heap,0,ds->undname );
#endif

#ifndef NO_DWARFSTACK
  if( ds->dwstMod ) FreeLibrary( ds->dwstMod );
#endif

  if( ds->absPath ) HeapFree( heap,0,ds->absPath );
  if( ds->filenameWide ) HeapFree( heap,0,ds->filenameWide );
  if( ds->ansiPath ) HeapFree( heap,0,ds->ansiPath );

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
    uint64_t addr,const wchar_t *filename,int lineno,const char *funcname,
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
        IMAGEHLP_LINEW64 *il = ds->ilW;
        SYMBOL_INFO *si = ds->si;
        for( i=0; i<inlineTrace; i++ )
        {
          RtlZeroMemory( il,sizeof(IMAGEHLP_LINEW64) );
          il->SizeOfStruct = sizeof(IMAGEHLP_LINEW64);
          if( ds->fSymGetLineFromInlineContextW(
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
    if( ds->fSymGetLineFromAddrW64 )
    {
      IMAGEHLP_LINEW64 *il = ds->ilW;
      RtlZeroMemory( il,sizeof(IMAGEHLP_LINEW64) );
      il->SizeOfStruct = sizeof(IMAGEHLP_LINEW64);
      DWORD dis;
      if( ds->fSymGetLineFromAddrW64(ds->process,addr,&dis,il) )
      {
        filename = il->FileName;
        lineno = il->LineNumber;
      }
    }
    else
    {
      IMAGEHLP_LINE64 *il = ds->il;
      RtlZeroMemory( il,sizeof(IMAGEHLP_LINEW64) );
      il->SizeOfStruct = sizeof(IMAGEHLP_LINEW64);
      DWORD dis;
      if( ds->fSymGetLineFromAddr64(ds->process,addr,&dis,il) )
      {
        wchar_t *filenameWide = ds->filenameWide;
        int len = MultiByteToWideChar( CP_ACP,0,
            il->FileName,-1,filenameWide,MAX_PATH );
        if( len<=0 || len>=MAX_PATH ) filenameWide[0] = 0;

        filename = filenameWide;
        lineno = il->LineNumber;
      }
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
    const wchar_t *absPath = filename;
    if( GetFullPathNameW(filename,MAX_PATH,ds->absPath,NULL) )
      absPath = ds->absPath;
    sl->filename = strings_addW( absPath,&ds->file_a,&ds->file_q,ds->heap );
  }
  sl->funcname = strings_add( funcname,&ds->func_a,&ds->func_q,ds->heap );
  sl->lineno = lineno;
  sl->columnno = columnno;
}

#ifndef NO_DWARFSTACK
static void locFuncCacheAnsi(
    uint64_t addr,const char *filename,int lineno,const char *funcname,
    void *context,int columnno )
{
  wchar_t *filenameWide = NULL;
  if( filename )
  {
    dbgsym *ds = context;
    filenameWide = ds->filenameWide;
    int len = MultiByteToWideChar( CP_ACP,0,
        filename,-1,filenameWide,MAX_PATH );
    if( len<=0 || len>=MAX_PATH ) filenameWide[0] = 0;
  }
  locFuncCache( addr,filenameWide,lineno,funcname,context,columnno );
}
#endif

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
    if( ds->fdwstOfFileW )
      ds->fdwstOfFileW( mi->path,mi->base,frames+j,l-j,locFuncCache,ds );
    else if( ds->fdwstOfFile )
    {
      char *ansiPath = ds->ansiPath;
      int count = WideCharToMultiByte( CP_ACP,0,
          mi->path,-1,ansiPath,MAX_PATH,NULL,NULL );
      if( count>0 && count<MAX_PATH )
        ds->fdwstOfFile( ansiPath,mi->base,frames+j,l-j,locFuncCacheAnsi,ds );
    }
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

static const char *mapOfFile( const wchar_t *name,size_t *size )
{
  HANDLE file = CreateFileW( name,GENERIC_READ,FILE_SHARE_READ,
      NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,0 );
  if( file==INVALID_HANDLE_VALUE ) return( NULL );

  const char *map = NULL;

  BY_HANDLE_FILE_INFORMATION bhfi;
  if( GetFileInformationByHandle(file,&bhfi) && (
#ifndef _WIN64
        !bhfi.nFileSizeHigh &&
#else
        bhfi.nFileSizeHigh ||
#endif
        bhfi.nFileSizeLow) )
  {
    HANDLE mapping = CreateFileMapping( file,NULL,PAGE_READONLY,0,0,NULL );
    if( mapping )
    {
      map = MapViewOfFile( mapping,FILE_MAP_READ,0,0,0 );

      CloseHandle( mapping );
    }
  }

  CloseHandle( file );

  if( !map ) return( NULL );

  if( size )
  {
    *size = bhfi.nFileSizeLow;
#ifdef _WIN64
    *size |= (size_t)bhfi.nFileSizeHigh << 32;
#endif
  }

  return( map );
}

static void locOut( textColor *tc,uintptr_t addr,
    const wchar_t *filename,int lineno,int columnno,const char *funcname,
    options *opt,int indent )
{
  const wchar_t *printFilename = NULL;
  if( filename )
  {
    printFilename = opt->fullPath ? NULL : strrchrW( filename,'\\' );
    if( !printFilename ) printFilename = filename;
    else printFilename++;
  }

  printf( "%i",indent );
  switch( lineno )
  {
    case DWST_BASE_ADDR:
      printf( "  $B%X$N   $B%S\n",addr,printFilename );
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
      printf( "   $O%S$N:$S%d$N",printFilename,lineno );
      if( columnno>0 )
        printf( ":%d",columnno );
      if( funcname )
        printf( " [$I%s$N]",funcname );
      printf( "\n" );

      // show source code {{{
      if( opt->sourceCode )
      {
        size_t filesize;
        const char *map = mapOfFile( filename,&filesize );
        if( map )
        {
          const char *bol = map;
          const char *eof = map + filesize;
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
    const wchar_t *filename,int lineno,const char *funcname,
    modInfo *mi )
{
  printf( "    <frame>\n" );
  if( addr || (!mi && !funcname && lineno<=0) )
    printf( "      <ip>%X</ip>\n",addr );
  if( mi )
  {
    if( !addr && !funcname && !lineno )
      printf( "      <ip>%X</ip>\n",mi->base );
    printf( "      <obj>%S</obj>\n",mi->path );
  }
  if( funcname )
    printf( "      <fn>%s</fn>\n",funcname );
  if( lineno>0 )
  {
    const wchar_t *sep = strrchrW( filename,'\\' );
    const wchar_t *filepart;
    if( sep )
    {
      printf( "      <dir>" );
      const wchar_t *p = filename;
      const char slash = '/';
      while( 1 )
      {
        const wchar_t *backslash = strchrW( p,'\\' );
        if( !backslash ) break;
        if( backslash>p )
          tc->fWriteSubTextW( tc,p,backslash-p );
        p = backslash + 1;
        if( p>sep ) break;
        tc->fWriteSubText( tc,&slash,1 );
      }
      printf( "</dir>\n" );
      filepart = sep + 1;
    }
    else
      filepart = filename;
    printf( "      <file>%S</file>\n",filepart );
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
        locOut( tc,frame,L"?",DWST_BASE_ADDR,0,NULL,ds->opt,indent );
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

#ifndef NO_THREADS
static void printThreadName( int threadNum,
    textColor *tc,int threadName_q,char **threadName_a )
{
  ASSUME( threadName_a || !threadName_q );

  if( threadNum>0 && threadNum<=threadName_q && threadName_a[threadNum-1] )
    printf( " $S'%d: %s'\n",threadNum,threadName_a[threadNum-1] );
  else if( threadNum>1 )
    printf( " $S'%d'\n",threadNum );
  else
    printf( "\n" );
}
#define printThreadName(tni) printThreadName(tni,tc,threadName_q,threadName_a)
#else
#define printThreadName(tni) printf("\n")
#endif

static void printAllocatedFreed( allocation *aa,int withFreed,
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,
#endif
    modInfo *mi_a,int mi_q,dbgsym *ds )
{
  textColor *tc = ds->tc;

  printf( "$S  allocated on: $N(#%U)",aa[0].id );
  printThreadName( aa[0].threadNum );
  printStackCount( aa[0].frames,aa[0].frameCount,mi_a,mi_q,ds,aa[0].ft,0 );

  if( withFreed )
  {
    printf( "$S  freed on:" );
    printThreadName( aa[1].threadNum );
    printStackCount( aa[1].frames,aa[1].frameCount,mi_a,mi_q,ds,aa[1].ft,0 );
  }
}
#ifndef NO_THREADS
#define printAllocatedFreed(aa,wf,mia,miq,ds) \
  printAllocatedFreed(aa,wf,threadName_a,threadName_q,mia,miq,ds)
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

  int c = ptrcmp( (const uintptr_t*)a->frames,
      (const uintptr_t*)b->frames,PTRS );
  if( c ) return( c>0 ? 2 : -2 );

#ifndef NO_THREADS
  if( a->threadNum>b->threadNum ) return( -2 );
  if( a->threadNum<b->threadNum ) return( 2 );
#endif

  return( a->id>b->id ? 1 : -1 );
}

static int cmp_time_allocation( const void *av,const void *bv )
{
  const allocation *a = av;
  const allocation *b = bv;

  if( a->lt>b->lt ) return( 2 );
  if( a->lt<b->lt ) return( -2 );

#ifndef NO_THREADS
  if( a->threadNum>b->threadNum ) return( -2 );
  if( a->threadNum<b->threadNum ) return( 2 );
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

#ifndef NO_THREADS
  if( a->threadNum>b->threadNum ) return( -2 );
  if( a->threadNum<b->threadNum ) return( 2 );
#endif

  return( a->id>b->id ? 1 : -1 );
}

// }}}
// leak recording status {{{

static void showRecording( const char *title,HANDLE err,int recording,
    COORD *consoleCoord,int *errColorP,int threads )
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

  if( threads )
  {
    const char *threadText = "     threads";
    WriteFile( err,threadText,4,&didwrite,NULL );
    char str[16];
    char *end = str + sizeof(str);
    char *start = num2str( end,threads,0 );
    SetConsoleTextAttribute( err,
        errColor^(FOREGROUND_RED|FOREGROUND_INTENSITY) );
    WriteFile( err,start,(DWORD)(end-start),&didwrite,NULL );
    WriteFile( err,threadText+4,7+(threads>1),&didwrite,NULL );
    SetConsoleTextAttribute( err,errColor );
  }
}

static void clearRecording( const char *title,HANDLE err,
    COORD consoleCoord,int errColor )
{
  COORD moveCoord = { consoleCoord.X,consoleCoord.Y-1 };
  SetConsoleCursorPosition( err,moveCoord );

  DWORD didwrite;
  int recTextLen = lstrlen( title ) + 25 + 22;
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
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,
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
#ifndef NO_THREADS
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
      printThreadName( a->threadNum );
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
      printf( "%i$Wsum: %d sample%s\n",
          indent,sg->allocSum,sg->allocSum>1?"s":NULL );
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
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,
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
#ifndef NO_THREADS
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
#ifndef NO_THREADS
      int threadNum = a->threadNum;
      if( threadNum )
        printf( "  <tid>%d</tid>\n",threadNum );
      if( threadNum>0 && threadNum<=threadName_q && threadName_a[threadNum-1] )
        printf( "  <threadname>%s</threadname>\n",threadName_a[threadNum-1] );
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
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,
#endif
    modInfo *mi_a,int mi_q,dbgsym *ds,
    const char *groupName,const char *groupTypeName,int sampling );

static void writeFileSeekBack( textColor *tc,const char *text )
{
  if( !tc || !text || !text[0] ) return;

  LARGE_INTEGER pos;
  pos.LowPart = pos.HighPart = 0;
  if( SetFilePointerEx(tc->out,pos,&pos,FILE_CURRENT) )
  {
    tc->fWriteText( tc,text,lstrlen(text) );
    SetFilePointerEx( tc->out,pos,NULL,FILE_BEGIN );
  }
}

static void printLeaks( allocation *alloc_a,int alloc_q,
    int alloc_ignore_q,size_t alloc_ignore_sum,
    int alloc_ignore_ind_q,size_t alloc_ignore_ind_sum,
    unsigned char **content_ptrs,modInfo *mi_a,int mi_q,
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,
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
  int lDetails = leakDetails>1 ?
    ( (leakDetails&1) ? LT_COUNT : LT_REACHABLE ) : ( leakDetails ? 1 : 0 );
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
#ifndef NO_THREADS
            threadName_a,threadName_q,
#endif
            content_ptrs,mi_a,mi_q,ds,sampling );
      if( !sampling )
        printf( "  $Wsum: %U B / %d\n",sg->allocSumSize,sg->allocSum );
      else
        printf( "  $Wsum: %d sample%s\n",
            sg->allocSum,sg->allocSum>1?"s":NULL );
    }
    if( sg->allocSum && tcXml && l<lDetails )
    {
      textColor *tcOrig = tc;
      ds->tc = tcXml;
      xmlRecordNum = printStackGroupXml( sg,alloc_a,alloc_idxs,combined_q,
#ifndef NO_THREADS
          threadName_a,threadName_q,
#endif
          mi_a,mi_q,ds,leakTypeNames,xmlRecordNum,sampling );
      ds->tc = tcOrig;
    }
    if( sg->allocSum && tcSvg && l<lDetails )
      printFullStackGroupSvg( ad,sg,tcSvg,alloc_a,alloc_idxs,
#ifndef NO_THREADS
          threadName_a,threadName_q,
#endif
          mi_a,mi_q,ds,groupName,groupTypeName,sampling );
    freeStackGroup( sg,heap );
  }
  // }}}

  writeFileSeekBack( tcXml,"</valgrindoutput>\n" );
  writeFileSeekBack( tcSvg,"</svg>\n" );

  if( alloc_idxs )
    HeapFree( heap,0,alloc_idxs );
  HeapFree( heap,0,sg_a );
}

// }}}
// sampling profiler {{{

#if USE_STACKWALK
static DWORD WINAPI samplingThread( LPVOID arg )
{
  appData *ad = arg;
  options *opt = ad->opt;
  HANDLE heap = ad->heap;
  HANDLE process = ad->pi.hProcess;
  stackwalkFunctions *swf = &ad->ds->swf;

  int interval = opt->samplingInterval;
  if( interval<0 ) interval = -interval;

  typedef BOOL WINAPI func_QueryThreadCycleTime( HANDLE,PULONG64 );
  func_QueryThreadCycleTime *fQueryThreadCycleTime = NULL;
  if( ad->svgName )
  {
    HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );
    fQueryThreadCycleTime = (func_QueryThreadCycleTime*)GetProcAddress(
        kernel32,"QueryThreadCycleTime" );
  }
  ULONG64 maxCycleBlocked = 1000000;

  SetEvent( ad->samplingInit );

  CONTEXT context;
  EnterCriticalSection( &ad->csSampling );
  while( 1 )
  {
    LeaveCriticalSection( &ad->csSampling );
    DWORD w = WaitForSingleObject( ad->samplingStop,interval );
    EnterCriticalSection( &ad->csSampling );

    if( w!=WAIT_TIMEOUT ) break;
    if( !ad->recordingSamples ) continue;

    int thread_samp_q = ad->thread_samp_q;
    if( !thread_samp_q ) continue;
    threadSamplingType *thread_samp_a = ad->thread_samp_a;

    int samp_sum_q = ad->samp_q + thread_samp_q;
    int samp_add = 0;
    while( samp_sum_q > ad->samp_s + samp_add ) samp_add += 1024;
    if( samp_add>0 )
    {
      allocation *samp_a = ad->samp_a;
      size_t s = (ad->samp_s+samp_add)*sizeof(allocation);
      if( !samp_a )
        samp_a = HeapAlloc( heap,0,s );
      else
        samp_a = HeapReAlloc( heap,0,samp_a,s );
      if( !samp_a ) break;
      ad->samp_a = samp_a;
      ad->samp_s += samp_add;
    }

    int ts;
    for( ts=0; ts<thread_samp_q; ts++ )
    {
      allocation *a = &ad->samp_a[ad->samp_q];
      RtlZeroMemory( a,sizeof(allocation) );
      a->size = 1;
      a->ft = FT_COUNT;
      a->id = ++ad->samp_id;

#ifndef NO_THREADS
      a->threadNum = thread_samp_a[ts].threadNum;
#endif

      HANDLE thread = thread_samp_a[ts].thread;

      RtlZeroMemory( &context,sizeof(CONTEXT) );
      context.ContextFlags = CONTEXT_FULL;

      if( UNLIKELY(SuspendThread(thread)==(DWORD)-1) ) continue;

      GetThreadContext( thread,&context );
      stackwalkDbghelp( swf,opt,process,thread,&context,a->frames );

      if( fQueryThreadCycleTime )
      {
        ULONG64 cycleTime = 0;
        if( fQueryThreadCycleTime(thread,&cycleTime) &&
            cycleTime-thread_samp_a[ts].cycleTime<=maxCycleBlocked )
          a->ft = FT_BLOCKED;
        thread_samp_a[ts].cycleTime = cycleTime;
      }

      ResumeThread( thread );

      ad->samp_q++;
    }
  }
  LeaveCriticalSection( &ad->csSampling );

  return( 0 );
}
#endif

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
  if( ad->pExePath[0] )
    printf( "$Iparent application: $N%S\n",ad->pExePath );
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

static wchar_t *readOption( wchar_t *args,appData *ad,HANDLE heap )
{
  if( !args || args[0]!='-' ) return( NULL );

  options *opt = ad->opt;
  int raise_alloc_q = ad->raise_alloc_q;
  size_t *raise_alloc_a = ad->raise_alloc_a;

  switch( args[1] )
  {
    case 'p':
      opt->protect = wtoi( args+2 );
      if( opt->protect<0 ) opt->protect = 0;
      break;

    case 'a':
      {
        int align = wtoi( args+2 );
        if( align>0 && !(align&(align-1)) )
          opt->align = align;
      }
      break;

    case 'i':
      {
        const wchar_t *pos = args + 2;
        uint64_t init = wtou64( pos );
        int initSize = 1;
        while( *pos && *pos!=' ' && *pos!=':' ) pos++;
        if( *pos==':' )
          initSize = wtoi( pos+1 );
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
      opt->slackInit = args[2]=='-' ? -wtoi( args+3 ) : wtoi( args+2 );
      if( opt->slackInit>0xff ) opt->slackInit &= 0xff;
      break;

    case 'f':
      opt->protectFree = wtoi( args+2 );
      break;

    case 'h':
      opt->handleException = wtoi( args+2 );
      break;

    case 'F':
      opt->fullPath = wtoi( args+2 );
      break;

    case 'm':
      opt->allocMethod = wtoi( args+2 );
      break;

    case 'l':
      opt->leakDetails = wtoi( args+2 );
      break;

    case 'S':
      opt->useSp = wtoi( args+2 );
      break;

    case 'd':
      opt->dlls = wtoi( args+2 );
      break;

    case 'P':
      opt->pid = wtoi( args+2 );
      break;

    case 'e':
      opt->exitTrace = wtoi( args+2 );
      break;

    case 'C':
      opt->sourceCode = wtoi( args+2 );
      break;

    case 'r':
      opt->raiseException = wtoi( args+2 );
      break;

    case 'M':
      opt->minProtectSize = wtoi( args+2 );
      if( opt->minProtectSize<1 ) opt->minProtectSize = 1;
      break;

    case 'n':
      opt->findNearest = wtoi( args+2 );
      break;

    case 'L':
      opt->leakContents = wtoi( args+2 );
      break;

    case 'g':
      opt->groupLeaks = wtoi( args+2 );
      break;

    case 'z':
      opt->minLeakSize = wtop( args+2 );
      break;

    case 'k':
      opt->leakRecording = wtoi( args+2 );
      break;

    case 'R':
      {
        size_t id = wtop( args+2 );
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
      opt->exceptionDetails = args[2]=='-' ? -wtoi( args+3 ) : wtoi( args+2 );
      break;

#if USE_STACKWALK
    case 'I':
      opt->samplingInterval = args[2]=='-' ? -wtoi( args+3 ) : wtoi( args+2 );
      break;
#endif

    default:
      return( NULL );
  }
  while( args[0] && args[0]!=' ' && args[0]!=';' ) args++;

  ad->raise_alloc_q = raise_alloc_q;
  ad->raise_alloc_a = raise_alloc_a;

  return( args );
}

static wchar_t *getStringOption( const wchar_t *start,HANDLE heap )
{
  const wchar_t *end = start;
  while( *end && *end!=' ' ) end++;
  if( end<=start ) return( NULL );

  size_t len = end - start;
  wchar_t *str = HeapAlloc( heap,0,2*(len+1) );
  if( !str ) return( NULL );

  RtlMoveMemory( str,start,2*len );
  str[len] = 0;
  return( str );
}

static wchar_t *getQuotedStringOption( wchar_t *start,HANDLE heap,
    wchar_t **endPtr )
{
  if( *start!='"' ) return( getStringOption(start,heap) );

  start++;
  wchar_t *end = strchrW( start,'"' );
  if( !end || end==start ) return( NULL );

  size_t len = end - start;
  wchar_t *str = HeapAlloc( heap,0,2*(len+1) );
  *endPtr = end;
  if( !str ) return( NULL );

  RtlMoveMemory( str,start,2*len );
  str[len] = 0;
  return( str );
}

static wchar_t *expandFileNameVars( appData *ad,const wchar_t *origName,
    const wchar_t *exePath )
{
  HANDLE heap = ad->heap;
  wchar_t *name = NULL;

  wchar_t *replaced = strreplacenum( origName,L"%p",ad->pi.dwProcessId,heap );
  if( replaced )
    origName = name = replaced;

  replaced = strreplacenum( origName,L"%P",ad->ppid,heap );
  if( replaced )
  {
    if( name ) HeapFree( heap,0,name );
    origName = name = replaced;
  }

  replaced = strreplacenum( origName,L"%c",ad->appCounter,heap );
  if( replaced )
  {
    if( name ) HeapFree( heap,0,name );
    origName = name = replaced;
  }

  wchar_t *lastPoint = NULL;
  if( !exePath )
  {
    wchar_t *delim = strrchrW( ad->exePathW,'\\' );
    if( delim ) delim++;
    else delim = ad->exePathW;
    lastPoint = strrchrW( delim,'.' );
    if( lastPoint ) lastPoint[0] = 0;
    exePath = delim;
  }
  replaced = strreplace( origName,L"%n",exePath,heap );
  if( lastPoint ) lastPoint[0] = '.';
  if( replaced )
  {
    if( name ) HeapFree( heap,0,name );
    origName = name = replaced;
  }

  replaced = strreplace( origName,L"%N",ad->pExePath,heap );
  if( replaced )
  {
    if( name ) HeapFree( heap,0,name );
    name = replaced;
  }

  return( name );
}

static void getAppCounter( appData *ad,
    DWORD appCounterID,int create,int useCounter )
{
  if( !appCounterID ) return;

  char counterName[32] = "heob.counter.";
  char *end = num2hexstr( counterName+lstrlen(counterName),appCounterID,8 );
  end[0] = 0;
  HANDLE mapping;
  if( create )
    mapping = CreateFileMapping( INVALID_HANDLE_VALUE,
        NULL,PAGE_READWRITE,0,sizeof(sharedAppCounter),counterName );
  else
    mapping = OpenFileMapping( FILE_MAP_ALL_ACCESS,FALSE,counterName );
  if( !mapping ) return;

  sharedAppCounter *sac = MapViewOfFile( mapping,
      FILE_MAP_ALL_ACCESS,0,0,sizeof(sharedAppCounter) );
  if( !sac )
  {
    CloseHandle( mapping );
    return;
  }

  if( useCounter )
    ad->appCounter = InterlockedIncrement( &sac->count );
  ad->appCounterID = appCounterID;
  ad->appCounterMapping = mapping;

  UnmapViewOfFile( sac );
}

// }}}
// disassembler {{{

#ifndef NO_DBGENG
static char *disassemble( DWORD pid,size_t addr,HANDLE heap )
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
    ULONG64 offset = addr;
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

static WORD waitForKey( textColor *tc,HANDLE in )
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

  return( ir.Event.KeyEvent.wVirtualKeyCode );
}

static int isConsoleOwner( void )
{
  DWORD conPid;
  return( GetConsoleProcessList(&conPid,1)==1 );
}

static WORD waitForKeyIfConsoleOwner( textColor *tc,HANDLE in )
{
  if( !in || !isConsoleOwner() ) return( 0 );

  printf( "\n" );
  return( waitForKey(tc,in) );
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

static int setTaskbarStatus( ITaskbarList3 *tl3,HWND conHwnd )
{
  if( !tl3 ) return( 0 );

  tl3->lpVtbl->SetProgressState( tl3,conHwnd,TBPF_ERROR );
  tl3->lpVtbl->SetProgressValue( tl3,conHwnd,1,1 );
  return( -2 );
}

// }}}
// xml {{{

static textColor *createExpandedXml( appData *ad,const wchar_t *name )
{
  if( !name ) return( NULL );

  wchar_t *fullName = expandFileNameVars( ad,name,NULL );
  const wchar_t *usedName = fullName ? fullName : name;

  int access = GENERIC_WRITE | ( ad->opt->leakErrorExitCode>1 ? DELETE : 0 );
  HANDLE xml = CreateFileW( usedName,access,FILE_SHARE_READ,
      NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
  if( fullName ) HeapFree( ad->heap,0,fullName );
  if( xml==INVALID_HANDLE_VALUE ) return( NULL );

  textColor *tc = HeapAlloc( ad->heap,HEAP_ZERO_MEMORY,sizeof(textColor) );
  tc->fWriteText = &WriteText;
  tc->fWriteSubText = &WriteTextHtml;
  tc->fWriteSubTextW = &WriteTextHtmlW;
  tc->fTextColor = NULL;
  tc->out = xml;
  tc->color = ATT_NORMAL;

  return( tc );
}

static textColor *writeXmlHeader( appData *ad )
{
  textColor *tc = createExpandedXml( ad,ad->xmlName );
  if( !tc ) return( NULL );

  const wchar_t *exePathW = ad->exePathW;

  printf( "<?xml version=\"1.0\"?>\n\n" );
  writeHeobInfo( tc,"XML output" );
  printf(
      "<valgrindoutput>\n\n"
      "<protocolversion>4</protocolversion>\n"
      "<protocoltool>memcheck</protocoltool>\n\n"
      "<preamble>\n"
      "  <line>heap-observer %s (" BITS "bit)</line>\n",HEOB_VER );
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
  printf(
      "</preamble>\n\n"
      "<pid>%u</pid>\n"
      "<ppid>%u</ppid>\n"
      "<tool>heob</tool>\n\n",
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
      GetTickCount()-ad->startTicks );

  return( tc );
}

static void writeXmlFooter( textColor *tc,appData *ad )
{
  if( !tc ) return;

  printf( "<status>\n  <state>FINISHED</state>\n"
      "  <time>%t</time>\n</status>\n\n",
      GetTickCount()-ad->startTicks );

  printf( "</valgrindoutput>\n" );

  CloseHandle( tc->out );
  HeapFree( ad->heap,0,tc );
}

static void writeXmlAllocatedFreed( textColor *tc,dbgsym *ds,
    allocation *aa,int withFreed,modInfo *mi_a,int mi_q )
{
  printf( "  <auxwhat>allocated on (#%U)</auxwhat>\n",aa[0].id );
  printf( "  <stack>\n" );
  printStackCount( aa[0].frames,aa[0].frameCount,mi_a,mi_q,ds,aa[0].ft,-1 );
  printf( "  </stack>\n" );

  if( withFreed )
  {
    printf( "  <auxwhat>freed on</auxwhat>\n" );
    printf( "  <stack>\n" );
    printStackCount( aa[1].frames,aa[1].frameCount,mi_a,mi_q,ds,aa[1].ft,-1 );
    printf( "  </stack>\n" );
  }
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
    writeXmlAllocatedFreed( tc,ds,&ei->aa[1],ei->aq>2,mi_a,mi_q );
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
    writeXmlAllocatedFreed( tc,ds,aa+1,aa[2].ptr!=NULL,mi_a,mi_q );
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
    writeXmlAllocatedFreed( tc,ds,aa+3,0,mi_a,mi_q );
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
  writeXmlAllocatedFreed( tc,ds,aa+1,1,mi_a,mi_q );
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
  writeXmlAllocatedFreed( tc,ds,aa,1,mi_a,mi_q );
  printf( "</error>\n\n" );

  ds->tc = tcOrig;
}

static void writeXmlFreeWhileRealloc( textColor *tc,dbgsym *ds,
    allocation *aa,modInfo *mi_a,int mi_q )
{
  if( !tc ) return;

  textColor *tcOrig = ds->tc;
  ds->tc = tc;

  printf( "<error>\n" );
  printf( "  <kind>InvalidFree</kind>\n" );
  printf( "  <what>free while realloc</what>\n" );
  writeXmlAllocatedFreed( tc,ds,aa,1,mi_a,mi_q );
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
  printf( "  <what>mismatching allocation/release method</what>\n" );
  writeXmlAllocatedFreed( tc,ds,aa,1,mi_a,mi_q );
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
  textColor *tc = createExpandedXml( ad,ad->svgName );
  if( !tc ) return( NULL );

  int svgWidth = 1280;
  HMODULE user32 = LoadLibrary( "user32.dll" );
  if( user32 )
  {
    typedef int WINAPI func_GetSystemMetrics( int );
    func_GetSystemMetrics *fGetSystemMetrics =
      (func_GetSystemMetrics*)GetProcAddress( user32,"GetSystemMetrics" );
    if( fGetSystemMetrics )
      svgWidth = fGetSystemMetrics( SM_CXSCREEN ) -
        fGetSystemMetrics( SM_CXVSCROLL );
    FreeLibrary( user32 );
  }

  printf( "<?xml version=\"1.0\"?>\n\n" );
  writeHeobInfo( tc,"Flame graph" );
  printf(
      "<svg width=\"%d\" height=\"100\" onload=\"heobInit()\""
      " xmlns=\"http://www.w3.org/2000/svg\">\n"
      "  <style type=\"text/css\">\n"
      "    .sample:hover { stroke:black; stroke-width:0.5;"
      " cursor:pointer; }\n"
      "  </style>\n"
      "  <script type=\"text/ecmascript\">\n"
      "    <![CDATA[\n",
      svgWidth );

  writeResource( tc,100 );

  printf(
      "    ]]>\n"
      "  </script>\n" );

  wchar_t *exePath = ad->exePathW;
  wchar_t *delim = strrchrW( exePath,'\\' );
  if( delim ) delim++;
  else delim = exePath;
  wchar_t *lastPoint = strrchrW( delim,'.' );
  if( lastPoint ) lastPoint[0] = 0;

  printf( "  <title>%S - heob %s</title>\n",delim,HEOB_VER );
  printf( "  <text id=\"cmd\" heobCmd=\"%S\">%S</text>\n",
      ad->cmdLineW,delim );

  if( lastPoint ) lastPoint[0] = '.';

  return( tc );
}

static void locSvg( textColor *tc,uintptr_t addr,int useAddr,
    size_t samples,size_t ofs,int stack,int allocs,
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,int threadNum,
#endif
    const wchar_t *filename,int lineno,const char *funcname,
    const wchar_t *modname,int blocked,size_t id )
{
  if( stack<=1 ) printf( "\n" );

  printf( "  <svg heobSum=\"%U\" heobOfs=\"%U\" heobStack=\"%d\"",
      samples,ofs,stack );
  if( allocs )
    printf( " heobAllocs=\"%d\"",allocs );
  if( useAddr )
    printf( " heobAddr=\"%X\"",addr );
  if( lineno>0 )
    printf( " heobSource=\"%S:%d\"",filename,lineno );
  if( funcname )
    printf( " heobFunc=\"%s\"",funcname );
  if( modname )
    printf( " heobMod=\"%S\"",modname );
#ifndef NO_THREADS
  if( threadNum>0 && threadNum<=threadName_q && threadName_a[threadNum-1] )
    printf( " heobThread=\"thread %d: %s\"",
        threadNum,threadName_a[threadNum-1] );
  else if( threadNum )
    printf( " heobThread=\"thread %d\"",threadNum );
#endif
  if( blocked )
    printf( " heobBlocked=\"%d\"",blocked );
  if( id )
    printf( " heobId=\"#%U\"",id );
  printf( "/>\n" );
}

static int printStackCountSvg( void **framesV,int fc,
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,int threadNum,
#endif
    textColor *tc,modInfo *mi_a,int mi_q,dbgsym *ds,funcType ft,
    size_t samples,size_t ofs,int stack,int allocs,int sampling,size_t id )
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
#ifndef NO_THREADS
          threadName_a,threadName_q,threadNum,
#endif
          NULL,0,NULL,NULL,ft==FT_BLOCKED,0 );
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
#ifndef NO_THREADS
            threadName_a,threadName_q,threadNum,
#endif
            NULL,0,NULL,mi->path,ft==FT_BLOCKED,0 );
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
      size_t bottomId = 0;
      size_t inlineId = 0;
      if( !j && id && ft>=FT_COUNT )
      {
        if( inlineCount>1 )
          inlineId = id;
        else
          bottomId = id;
      }
      int stackPos = stack + stackCount;
      // output first the bottom stack
      locSvg( tc,frame,1,samples,ofs,stackPos,sampling?0:allocs,
#ifndef NO_THREADS
          threadName_a,threadName_q,threadNum,
#endif
          sl->filename,sl->lineno,sl->funcname,mi->path,ft==FT_BLOCKED,
          bottomId );
      // then the rest from top to bottom+1
      sl = &s->sl;
      int inlinePos = inlineCount - 1;
      while( inlinePos>0 )
      {
        locSvg( tc,0,0,samples,ofs,stackPos+inlinePos,sampling?0:allocs,
#ifndef NO_THREADS
            threadName_a,threadName_q,threadNum,
#endif
            sl->filename,sl->lineno,sl->funcname,mi->path,ft==FT_BLOCKED,
            inlineId );
        inlineId = 0;

        inlinePos--;
        sl = sl->inlineLocation;
      }
      stackCount += inlineCount;
    }
  }
  if( ft<FT_COUNT )
  {
    locSvg( tc,0,0,samples,ofs,stack+stackCount,sampling?0:allocs,
#ifndef NO_THREADS
        threadName_a,threadName_q,threadNum,
#endif
        NULL,0,ds->funcnames[ft],NULL,0,id );
    stackCount++;
  }
  return( stackCount );
}

static void printStackGroupSvg( stackGroup *sg,textColor *tc,
    allocation *alloc_a,const int *alloc_idxs,
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,
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
#ifndef NO_THREADS
    int threadNum = 0;
    for( i=0; i<allocCount; i++ )
    {
      int idx = alloc_idxs[allocStart+i];
      allocation *aIdx = alloc_a + idx;
      if( !threadNum )
        threadNum = aIdx->threadNum;
      else if( threadNum!=aIdx->threadNum )
      {
        threadNum = 0;
        break;
      }
    }
#endif

    funcType blocked = FT_COUNT;
    if( sampling )
    {
      blocked = FT_BLOCKED;
      for( i=0; i<allocCount; i++ )
      {
        int idx = alloc_idxs[allocStart+i];
        if( alloc_a[idx].ft!=FT_BLOCKED )
        {
          blocked = FT_COUNT;
          break;
        }
      }
    }

    stack += printStackCountSvg(
        a->frames+(a->frameCount-(sg->stackStart+sg->stackCount)),
        sg->stackCount,
#ifndef NO_THREADS
        threadName_a,threadName_q,threadNum,
#endif
        tc,mi_a,mi_q,ds,blocked,
        sg->allocSumSize,ofs,stack,sg->allocSum,sampling,0 );
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
#ifndef NO_THREADS
        if( sampling )
          locSvg( tc,0,0,combSize,ofs,stack,0,
              threadName_a,threadName_q,a->threadNum,
              NULL,0,NULL,NULL,a->ft==FT_BLOCKED,a->id );
        else
#endif
          printStackCountSvg( NULL,0,
#ifndef NO_THREADS
              threadName_a,threadName_q,a->threadNum,
#endif
              tc,NULL,0,ds,a->ft,
              combSize,ofs,stack,a->count,sampling,a->id );
      }
      else
        stack += printStackCountSvg(
            a->frames+(a->frameCount-(sg->stackStart+sg->stackCount)),
            sg->stackCount,
#ifndef NO_THREADS
            threadName_a,threadName_q,a->threadNum,
#endif
            tc,mi_a,mi_q,ds,a->ft,
            combSize,ofs,stack,a->count,sampling,a->id );
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
#ifndef NO_THREADS
        threadName_a,threadName_q,
#endif
        mi_a,mi_q,ds,ofs,stack,sampling );
    ofs += allocSumSize;
  }
}

static void printFullStackGroupSvg( appData *ad,stackGroup *sg,textColor *tc,
    allocation *alloc_a,const int *alloc_idxs,
#ifndef NO_THREADS
    char **threadName_a,int threadName_q,
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
#ifndef NO_THREADS
      NULL,0,0,
#endif
      NULL,0,fullName,NULL,0,0 );
  if( fullTypeName ) HeapFree( ad->heap,0,fullTypeName );

  printStackGroupSvg( sg,tc,alloc_a,alloc_idxs,
#ifndef NO_THREADS
      threadName_a,threadName_q,
#endif
      mi_a,mi_q,ds,ad->svgSum,2,sampling );
  ad->svgSum += sg->allocSumSize;
}

static void writeSvgFooter( textColor *tc,appData *ad )
{
  if( !tc ) return;

  printf( "</svg>\n" );

  CloseHandle( tc->out );
  HeapFree( ad->heap,0,tc );
}

// }}}
// process startup failure {{{

static int checkModule( appData *ad,textColor *tc,textColor *tcXml,int level,
    HMODULE mod,DWORD errCode )
{
  PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)mod;
  PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );

  PIMAGE_DATA_DIRECTORY idd =
    &inh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if( !idd->Size ) return( 1 );

  PIMAGE_IMPORT_DESCRIPTOR iid =
    (PIMAGE_IMPORT_DESCRIPTOR)REL_PTR( idh,idd->VirtualAddress );

  wchar_t *exePath = ad->exePathW;
  GetModuleFileNameW( mod,exePath,MAX_PATH );
  printf( "%i%S\n",level,exePath );
  mprintf( tcXml,
      "    <frame>\n"
      "      <obj>%S</obj>\n"
      "    </frame>\n",
      exePath );

  UINT i;
  for( i=0; iid[i].Characteristics; i++ )
  {
    if( !iid[i].FirstThunk || !iid[i].OriginalFirstThunk )
      break;

    PSTR curModName = (PSTR)REL_PTR( idh,iid[i].Name );
    if( !curModName[0] ) continue;

    HMODULE subMod = LoadLibrary( curModName );
    if( subMod )
    {
      exePath[0] = 0;

      PIMAGE_THUNK_DATA originalThunk =
        (PIMAGE_THUNK_DATA)REL_PTR( idh,iid[i].OriginalFirstThunk );

      for( ; originalThunk->u1.Function; originalThunk++ )
      {
        if( !(originalThunk->u1.Ordinal&IMAGE_ORDINAL_FLAG) )
        {
          PIMAGE_IMPORT_BY_NAME import = (PIMAGE_IMPORT_BY_NAME)REL_PTR(
              idh,originalThunk->u1.AddressOfData );
          if( !GetProcAddress(subMod,(LPCSTR)import->Name) )
          {
            if( !exePath[0] )
            {
              GetModuleFileNameW( subMod,exePath,MAX_PATH );
              printf( "%i%S\n",level+1,exePath );
              mprintf( tcXml,
                  "    <frame>\n"
                  "      <obj>%S</obj>\n"
                  "    </frame>\n",
                  exePath );
            }
            printf( "%i$Wcan't find symbol $I%s\n",level+2,import->Name );
            mprintf( tcXml,
                "  </stack>\n"
                "  <auxwhat>can't find symbol %s</auxwhat>\n"
                "  <stack>\n",
                import->Name );
          }
        }
        else
        {
          WORD ordinal = (WORD)originalThunk->u1.Ordinal;
          if( !GetProcAddress(subMod,(LPCSTR)(UINT_PTR)ordinal) )
          {
            if( !exePath[0] )
            {
              GetModuleFileNameW( subMod,exePath,MAX_PATH );
              printf( "%i%S\n",level+1,exePath );
              mprintf( tcXml,
                  "    <frame>\n"
                  "      <obj>%S</obj>\n"
                  "    </frame>\n",
                  exePath );
            }
            printf( "%i$Wcan't find ordinal $I%d\n",level+2,ordinal );
            mprintf( tcXml,
                "  </stack>\n"
                "  <auxwhat>can't find ordinal %d</auxwhat>\n"
                "  <stack>\n",
                ordinal );
          }
        }
      }

      FreeLibrary( subMod );
      if( exePath[0] ) return( 0 );
      continue;
    }

    DWORD e = GetLastError();
    subMod = LoadLibraryEx( curModName,NULL,DONT_RESOLVE_DLL_REFERENCES );
    if( subMod )
    {
      checkModule( ad,tc,tcXml,level+1,subMod,e );
      FreeLibrary( subMod );
    }
    else
    {
      printf( "%i$Wcan't load $O%s\n",level+1,curModName );
      mprintf( tcXml,
          "  </stack>\n"
          "  <auxwhat>can't load %s</auxwhat>\n"
          "  <stack>\n",
          curModName );
    }

    return( 0 );
  }

  if( errCode==ERROR_DLL_INIT_FAILED )
  {
    printf( "%i$Wdll initialization routine failed\n",level+1 );
    mprintf( tcXml,
        "  </stack>\n"
        "  <auxwhat>dll initialization routine failed</auxwhat>\n"
        "  <stack>\n" );
  }
  else
  {
    printf( "%i$Werror: %x (%e)\n",level+1,errCode,errCode );
    mprintf( tcXml,
        "  </stack>\n"
        "  <auxwhat>error: %x (%e)</auxwhat>\n"
        "  <stack>\n",
        errCode,errCode );
  }

  return( 1 );
}

static DWORD unexpectedEnd( appData *ad,textColor *tcXml,int *errorWritten )
{
  *errorWritten = 0;

  DWORD ec;
  if( !GetExitCodeProcess(ad->pi.hProcess,&ec) ) ec = 0;

  if( ec!=STATUS_DLL_NOT_FOUND && ec!=STATUS_ORDINAL_NOT_FOUND &&
      ec!=STATUS_ENTRYPOINT_NOT_FOUND && ec!=STATUS_DLL_INIT_FAILED )
    return( ec );

  *errorWritten = 1;

  HANDLE heap = ad->heap;
  wchar_t *exePath = ad->exePathW;
  textColor *tc = ad->tcOut;

  int xmlCreated = 0;
  if( !tcXml )
  {
    tcXml = writeXmlHeader( ad );
    xmlCreated = 1;
  }

  mprintf( tcXml,
      "<error>\n"
      "  <kind>SyscallParam</kind>\n" );

  nameOfProcess( GetModuleHandle("ntdll.dll"),
      heap,ad->pi.hProcess,exePath,-1 );
  HMODULE exeMod =
    LoadLibraryExW( exePath,NULL,DONT_RESOLVE_DLL_REFERENCES );
  if( !exeMod )
  {
    printf( "\n$Wcan't load %S\n",exePath );
    mprintf( tcXml,
        "  <what>can't load %S</what>\n"
        "</error>\n\n",
        exePath );
    if( xmlCreated ) writeXmlFooter( tcXml,ad );
    return( ec );
  }

  const char *ecStr =
    ec==STATUS_DLL_NOT_FOUND ? "DLL_NOT_FOUND" :
    ec==STATUS_ORDINAL_NOT_FOUND ? "ORDINAL_NOT_FOUND" :
    ec==STATUS_ENTRYPOINT_NOT_FOUND ? "ENTRYPOINT_NOT_FOUND" :
    "DLL_INIT_FAILED";
  printf( "\n$Sdll loading failure on process startup (%s):\n",ecStr );

  mprintf( tcXml,
      "  <what>dll loading failure on process startup (%s)</what>\n"
      "  <auxwhat>dll dependencies</auxwhat>\n"
      "  <stack>\n",
      ecStr );

  wchar_t *lastDelim = strrchrW( exePath,'\\' );
  if( lastDelim && lastDelim>exePath )
  {
    *lastDelim = 0;
    SetDllDirectoryW( exePath );
  }

  checkModule( ad,tc,tcXml,0,exeMod,0 );

  mprintf( tcXml,
      "  </stack>\n"
      "</error>\n\n" );
  if( xmlCreated ) writeXmlFooter( tcXml,ad );

  FreeLibrary( exeMod );

  return( ec );
}

// }}}
// exception {{{

static void writeException( appData *ad,textColor *tcXml,
#ifndef NO_THREADS
    int threadName_q,char **threadName_a,
#endif
#ifndef NO_DBGENG
    size_t ip,
#endif
    exceptionInfo *ei,modInfo *mi_a,int mi_q )
{
  dbgsym *ds = ad->ds;
  options *opt = ds->opt;
  textColor *tc = ad->tcOut;

  cacheSymbolData( ei->aa,NULL,ei->aq,mi_a,mi_q,ds,1 );

  // exception code {{{
  const char *desc = NULL;
  switch( ei->er.ExceptionCode )
  {
#define EXCEPTION_FATAL_APP_EXIT STATUS_FATAL_APP_EXIT
#define EXCEPTION_ASSERTION_FAILURE STATUS_ASSERTION_FAILURE
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
    EX_DESC( ASSERTION_FAILURE );
    EX_DESC( VC_CPP_EXCEPTION );
  }
  printf( "\n$Wunhandled exception code: %x%s\n",
      ei->er.ExceptionCode,desc );
  // }}}

  if( opt->exceptionDetails>0 && tc->out )
  {
    // modules {{{
    if( opt->exceptionDetails>2 )
    {
      printf( "$S  modules:\n" );
      int m;
      for( m=0; m<mi_q; m++ )
      {
        modInfo *mi = mi_a + m;
        printf( "    %X   %S",mi->base,mi->path );
        if( mi->versionMS || mi->versionLS )
          printf( " [$I%u.%u.%u.%u$N]",
              mi->versionMS>>16,mi->versionMS&0xffff,
              mi->versionLS>>16,mi->versionLS&0xffff );
        printf( "\n" );
      }
    }
    // }}}

    // assembly instruction {{{
#ifndef NO_DBGENG
    if( ip )
    {
      HANDLE heap = ad->heap;
      char *dis = disassemble( ad->pi.dwProcessId,ip,heap );
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
    printf( before "$I" name "$N=" type after,ei->c.reg )
    if( ei->c.ContextFlags&CONTEXT_INTEGER )
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
    if( ei->c.ContextFlags&CONTEXT_CONTROL )
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
    if( ei->c.ContextFlags&CONTEXT_SEGMENTS )
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

  printf( "$S  exception on:" );
  printThreadName( ei->aa[0].threadNum );
  printStackCount( ei->aa[0].frames,ei->aa[0].frameCount,
      mi_a,mi_q,ds,FT_COUNT,0 );

  char *addr = NULL;
  const char *violationType = NULL;
  const char *nearBlock = NULL;
  const char *blockType = NULL;
  // access violation {{{
  if( ei->er.ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
      ei->er.NumberParameters==2 )
  {
    ULONG_PTR flag = ei->er.ExceptionInformation[0];
    addr = (char*)ei->er.ExceptionInformation[1];
    violationType = flag==8 ? "data execution prevention" :
      ( flag ? "write access" : "read access" );
    printf( "$W  %s violation at %p\n",violationType,addr );

    if( ei->aq>1 )
    {
      char *ptr = (char*)ei->aa[1].ptr;
      size_t size = ei->aa[1].size;
      nearBlock = ei->nearest ? "near " : "";
      intptr_t accessPos = addr - ptr;
      blockType = ei->aq>2 ? "freed block" :
        ( accessPos>=0 && (size_t)accessPos<size ?
          "accessible (!) area of" : "protected area of" );
      printf( "$I  %s%s %p (size %U, offset %s%D)\n",
          nearBlock,blockType,
          ptr,size,accessPos>0?"+":"",accessPos );
      printAllocatedFreed( &ei->aa[1],ei->aq>2,mi_a,mi_q,ds );
    }
  }
  // }}}
  // VC c++ exception {{{
  else if( ei->throwName[0] )
  {
    char *throwName = undecorateVCsymbol( ds,ei->throwName );
    printf( "$I  VC c++ exception: $N%s\n",throwName );
  }
  // }}}

  writeXmlException( tcXml,ds,ei,desc,addr,violationType,
      nearBlock,blockType,mi_a,mi_q );
}

// }}}
// minidump {{{

#if USE_STACKWALK
static int addDumpMemoryLoc( appData *ad,
    const char *ptr,size_t size,size_t address )
{
  if( !(ad->dump_mem_q%64) )
  {
    dump_memory_loc *dump_mem_a = ad->dump_mem_a;
    int dump_mem_q = ad->dump_mem_q + 64;
    if( !dump_mem_a )
      dump_mem_a = HeapAlloc( ad->heap,0,dump_mem_q*sizeof(dump_memory_loc) );
    else
      dump_mem_a = HeapReAlloc(
          ad->heap,0,dump_mem_a,dump_mem_q*sizeof(dump_memory_loc) );
    if( !dump_mem_a ) return( 0 );

    ad->dump_mem_a = dump_mem_a;
  }

  dump_memory_loc *dml = ad->dump_mem_a + ad->dump_mem_q;
  dml->ptr = ptr;
  dml->size = size;
  dml->address = address;

  ad->dump_mem_q++;

  return( 1 );
}

static const void *getDumpLoc( appData *ad,size_t address,size_t *size )
{
  dump_memory_loc *dump_mem_a = ad->dump_mem_a;
  int dump_mem_q = ad->dump_mem_q;
  int i;
  for( i=0; i<dump_mem_q; i++ )
  {
    dump_memory_loc *dml = dump_mem_a + i;
    if( address>=dml->address && address<dml->address+dml->size )
    {
      if( size ) *size = dml->address + dml->size - address;
      return( dml->ptr + address - dml->address );
    }
  }
  return( NULL );
}

static BOOL WINAPI readDumpMemory( HANDLE process,
    DWORD64 address,PVOID buffer,DWORD size,LPDWORD bytesRead )
{
  appData *ad = process;

  size_t maxSize;
  const void *loc = getDumpLoc( ad,(size_t)address,&maxSize );
  if( loc )
  {
    if( size>maxSize ) size = (DWORD)maxSize;
    RtlMoveMemory( buffer,loc,size );
    *bytesRead = size;
    return( TRUE );
  }

  if( !ad->dump_mi_map_a ) return( FALSE );

  int i;
  for( i=0; i<ad->mi_q; i++ )
  {
    if( address<ad->mi_a[i].base ||
        address>=ad->mi_a[i].base+ad->mi_a[i].size )
      continue;

    if( ad->dump_mi_map_a[i] ) break;

    const char *map = mapOfFile( ad->mi_a[i].path,NULL );
    if( !map )
    {
      ad->dump_mi_map_a[i] = (char*)1;
      break;
    }

    PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)map;
    PIMAGE_NT_HEADERS inh = REL_PTR( idh,idh->e_lfanew );

    size_t base = ad->mi_a[i].base;

    addDumpMemoryLoc( ad,map,inh->OptionalHeader.SizeOfHeaders,base );

    PIMAGE_SECTION_HEADER ish = IMAGE_FIRST_SECTION( inh );
    int j;
    for( j=0; j<inh->FileHeader.NumberOfSections; j++,ish++ )
    {
      if( !ish->SizeOfRawData ) continue;

      addDumpMemoryLoc( ad,
          map+ish->PointerToRawData,ish->SizeOfRawData,
          base+ish->VirtualAddress );
    }

    ad->dump_mi_map_a[i] = map;

    return( readDumpMemory(process,address,buffer,size,bytesRead) );
  }

  *bytesRead = 0;
  return( FALSE );
}

static BOOL WINAPI symbolCallback( HANDLE process,
    ULONG action,ULONG64 data,ULONG64 context )
{
  (void)context;

  switch( action )
  {
    case CBA_READ_MEMORY:
      {
        IMAGEHLP_CBA_READ_MEMORY *icrm =
          (IMAGEHLP_CBA_READ_MEMORY*)(size_t)data;
        return( readDumpMemory(process,
              icrm->addr,icrm->buf,icrm->bytes,icrm->bytesread) );
      }
    case CBA_DEFERRED_SYMBOL_LOAD_START:
      {
        appData *ad = process;
        dbgsym *ds = ad->ds;
        if( !ds->fSymFindFileInPathW ) return( FALSE );

        IMAGEHLP_DEFERRED_SYMBOL_LOAD64 *symload =
          (IMAGEHLP_DEFERRED_SYMBOL_LOAD64*)(size_t)data;
        int m;
        for( m=0; m<ad->mi_q; m++ )
          if( ad->dump_mod_a[m].BaseOfImage==symload->BaseOfImage ) break;
        if( m==ad->mi_q ) return( FALSE );

        wchar_t *filename = ad->mi_a[m].path;
        BOOL ret = ds->fSymFindFileInPathW( ds->process,NULL,filename,
            &ad->dump_mod_a[m].TimeDateStamp,ad->dump_mod_a[m].SizeOfImage,
            0,SSRVOPT_DWORDPTR,ds->absPath,NULL,NULL );
        if( ret )
        {
          lstrcpynW( filename,ds->absPath,MAX_PATH );
          return( TRUE );
        }

        return( FALSE );
      }
  }

  return( FALSE );
}

static int isMinidump( appData *ad,const wchar_t *name )
{
  if( ad->pi.hProcess || !name ) return( 0 );

  HANDLE heap = ad->heap;
  textColor *tc = ad->tcOut;

  const wchar_t *symPath = L".";
  wchar_t *nameCopy = NULL;
  const wchar_t *end = NULL;
  if( name[0]=='"' )
  {
    name++;
    end = strchrW( name,'"' );
  }
  else end = strchrW( name,' ' );
  if( end )
  {
    int l = (int)( end - name );
    nameCopy = HeapAlloc( heap,0,(l+1)*2 );
    RtlMoveMemory( nameCopy,name,l*2 );
    nameCopy[l] = 0;
    name = nameCopy;

    if( end[0]=='"' ) end++;
    while( end[0]==' ') end++;
    if( end[0] ) symPath = end;
  }

  int l = lstrlenW( name );
  if( l<=4 ||
      CompareStringW(LOCALE_SYSTEM_DEFAULT,NORM_IGNORECASE,
        name+l-4,4,L".dmp",4)!=2 )
  {
    if( nameCopy ) HeapFree( heap,0,nameCopy );
    return( 0 );
  }

  WIN32_FILE_ATTRIBUTE_DATA wfad;
  if( !GetFileAttributesExW(name,GetFileExInfoStandard,&wfad) )
    RtlZeroMemory( &wfad,sizeof(wfad) );

  size_t size;
  const char *dump = mapOfFile( name,&size );
  if( nameCopy ) HeapFree( heap,0,nameCopy );
  if( !dump ) return( 0 );

  if( size<sizeof(MINIDUMP_HEADER) ||
      dump[0]!='M' || dump[1]!='D' || dump[2]!='M' || dump[3]!='P' )
  {
    UnmapViewOfFile( dump );
    return( 0 );
  }

  dbgsym ds;
  dbgsym_init( &ds,ad,tc,ad->opt,NULL,heap,symPath,FALSE,NULL );
  ad->ds = &ds;

  if( !ds.swf.fStackWalk64 )
  {
    printf( "$Wminidump reader needs StackWalk64() from dbghelp.dll\n" );
    UnmapViewOfFile( dump );
    dbgsym_close( &ds );
    return( 1 );
  }

  MINIDUMP_MODULE_LIST *mods = NULL;
  MINIDUMP_MEMORY_LIST *mml = NULL;
  MINIDUMP_MEMORY64_LIST *mm64l = NULL;
  MINIDUMP_THREAD_LIST *mtl = NULL;
  MINIDUMP_MISC_INFO *misc = NULL;
  MINIDUMP_EXCEPTION_STREAM *exception = NULL;

  MINIDUMP_HEADER *header = REL_PTR( dump,0 );
  MINIDUMP_DIRECTORY *dir = REL_PTR( dump,header->StreamDirectoryRva );
  uint32_t s;
  for( s=0; s<header->NumberOfStreams; s++)
  {
    switch( dir[s].StreamType )
    {
      case ModuleListStream:
        mods = REL_PTR( dump,dir[s].Location.Rva );
        break;
      case MemoryListStream:
        mml = REL_PTR( dump,dir[s].Location.Rva );
        break;
      case Memory64ListStream:
        mm64l = REL_PTR( dump,dir[s].Location.Rva );
        break;
      case ThreadListStream:
        mtl = REL_PTR( dump,dir[s].Location.Rva );
        break;
      case MiscInfoStream:
        misc = REL_PTR( dump,dir[s].Location.Rva );
        break;
      case ExceptionStream:
        exception = REL_PTR( dump,dir[s].Location.Rva );
        break;
    }
  }

  if( !exception || exception->ThreadContext.DataSize!=sizeof(CONTEXT) )
  {
    if( !exception )
      printf( "$Wminidump doesn't contain exception information stream\n" );
    else
      printf( "$Wminidump exception context size mismatch\n" );
    UnmapViewOfFile( dump );
    dbgsym_close( &ds );
    return( 1 );
  }

  if( mml )
  {
    uint32_t r;
    for( r=0; r<mml->NumberOfMemoryRanges; r++)
    {
      addDumpMemoryLoc( ad,
          dump+mml->MemoryRanges[r].Memory.Rva,
          mml->MemoryRanges[r].Memory.DataSize,
          (size_t)mml->MemoryRanges[r].StartOfMemoryRange );
    }
  }

  if( mm64l )
  {
    size_t rva = (size_t)mm64l->BaseRva;
    size_t r;
    for( r=0; r<mm64l->NumberOfMemoryRanges; r++ )
    {
      addDumpMemoryLoc( ad,
          dump+rva,(size_t)mm64l->MemoryRanges[r].DataSize,
          (size_t)mm64l->MemoryRanges[r].StartOfMemoryRange );

      rva += (size_t)mm64l->MemoryRanges[r].DataSize;
    }
  }

  if( mods && mods->NumberOfModules )
  {
    ad->dump_mod_a = mods->Modules;

    ad->mi_q = mods->NumberOfModules;
    ad->mi_a = HeapAlloc( heap,0,ad->mi_q*sizeof(modInfo) );
    if( !ad->mi_a ) ad->mi_q = 0;

    int m;
    for( m=0; m<ad->mi_q; m++ )
    {
      MINIDUMP_MODULE *mod = mods->Modules + m;
      MINIDUMP_STRING *moduleName = REL_PTR( dump,mod->ModuleNameRva );

      modInfo *mi = ad->mi_a + m;
      mi->base = (size_t)mod->BaseOfImage;
      mi->size = mod->SizeOfImage;
      mi->versionMS = mod->VersionInfo.dwFileVersionMS;
      mi->versionLS = mod->VersionInfo.dwFileVersionLS;
      lstrcpynW( mi->path,moduleName->Buffer,MAX_PATH );

      if( ds.fSymLoadModule64 )
        dbgsym_loadmodule( &ds,mi->path,mi->base,(DWORD)mi->size );
    }
    if( ad->mi_q )
      ad->dump_mi_map_a =
        HeapAlloc( heap,HEAP_ZERO_MEMORY,ad->mi_q*sizeof(char*) );
  }

  int threadNum = 0;
  if( mtl && ad->dump_mod_a )
  {
    MINIDUMP_STRING *appName = REL_PTR( dump,ad->dump_mod_a[0].ModuleNameRva );
    printf( "\n$Iapplication: $N%S\n",appName->Buffer );

    uint32_t t;
    for( t=0; t<mtl->NumberOfThreads &&
        mtl->Threads[t].ThreadId!=exception->ThreadId; t++ );
    if( t<mtl->NumberOfThreads )
    {
      MINIDUMP_THREAD *thread = mtl->Threads + t;
      threadNum = t + 1;

      addDumpMemoryLoc( ad,
          dump+thread->Stack.Memory.Rva,thread->Stack.Memory.DataSize,
          (size_t)thread->Stack.StartOfMemoryRange );

      size_t maxSize;
      const TEB *teb = getDumpLoc( ad,(size_t)thread->Teb,&maxSize );
      if( teb && maxSize>=sizeof(TEB) )
      {
        const PEB *peb = getDumpLoc( ad,(size_t)teb->Peb,&maxSize );
        if( peb && maxSize>=sizeof(PEB) )
        {
          const RTL_USER_PROCESS_PARAMETERS *upp =
            getDumpLoc( ad,(size_t)peb->ProcessParameters,&maxSize );
          if( upp && maxSize>=sizeof(RTL_USER_PROCESS_PARAMETERS) )
          {
            const wchar_t *commandLine = getDumpLoc(
                ad,(size_t)upp->CommandLine.Buffer,&maxSize );
            if( commandLine && maxSize>=upp->CommandLine.Length )
            {
              wchar_t *cl = HeapAlloc(
                  heap,HEAP_ZERO_MEMORY,upp->CommandLine.Length+2 );
              RtlMoveMemory( cl,commandLine,upp->CommandLine.Length );
              printf( "$Icommand line: $N%S\n",cl );
              HeapFree( heap,0,cl );
            }

            const wchar_t *currentDirectory = getDumpLoc(
                ad,(size_t)upp->CurrentDirectory.Buffer,&maxSize );
            if( currentDirectory && maxSize>=upp->CurrentDirectory.Length )
            {
              wchar_t *cd = HeapAlloc(
                  heap,HEAP_ZERO_MEMORY,upp->CurrentDirectory.Length+2 );
              RtlMoveMemory( cd,currentDirectory,
                  upp->CurrentDirectory.Length );
              printf( "$Idirectory: $N%S\n",cd );
              HeapFree( heap,0,cd );
            }
          }
        }
      }
    }

    if( misc && misc->Flags1&MINIDUMP_MISC1_PROCESS_ID )
      printf( "$IPID: $N%u\n",misc->ProcessId );
    if( misc && misc->Flags1&MINIDUMP_MISC1_PROCESS_TIMES )
    {
      printf( "$Iuser CPU time:   $N%t\n",misc->ProcessUserTime*1000 );
      printf( "$Ikernel CPU time: $N%t\n",misc->ProcessKernelTime*1000 );

      LONGLONG ll = Int32x32To64( misc->ProcessCreateTime,10000000 ) +
        116444736000000000LL;
      FILETIME ft;
      ft.dwLowDateTime = (DWORD)ll;
      ft.dwHighDateTime = (DWORD)( ll>>32 );
      printf( "$Iprocess creation time:  $N%T\n",&ft );

      if( wfad.ftLastWriteTime.dwLowDateTime ||
          wfad.ftLastWriteTime.dwHighDateTime )
        printf( "$Iminidump modified time: $N%T\n",&wfad.ftLastWriteTime );
    }
  }

  ds.swf.fReadProcessMemory = readDumpMemory;
  func_SymRegisterCallback64 *fSymRegisterCallback64 =
    (func_SymRegisterCallback64*)GetProcAddress(
        ds.symMod,"SymRegisterCallback64" );
  if( fSymRegisterCallback64 )
    fSymRegisterCallback64( ds.process,symbolCallback,0 );

  CONTEXT *context = REL_PTR( dump,exception->ThreadContext.Rva );
  MINIDUMP_EXCEPTION *me = &exception->ExceptionRecord;

  exceptionInfo *ei = HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(exceptionInfo) );
  ei->aa[0].threadNum = threadNum;
  stackwalkDbghelp( &ad->ds->swf,ad->opt,ad,(HANDLE)2,context,ei->aa->frames );
  ei->aq = 1;
  RtlMoveMemory( &ei->c,context,sizeof(CONTEXT) );
#ifndef _WIN64
  ei->er.ExceptionCode = me->ExceptionCode;
  ei->er.ExceptionFlags = me->ExceptionFlags;
  ei->er.ExceptionRecord = (EXCEPTION_RECORD*)(DWORD)me->ExceptionRecord;
  ei->er.ExceptionAddress = (PVOID)(DWORD)me->ExceptionAddress;
  ei->er.NumberParameters = me->NumberParameters;
  int j;
  for( j=0; j<EXCEPTION_MAXIMUM_PARAMETERS; j++ )
    ei->er.ExceptionInformation[j] = (ULONG_PTR)me->ExceptionInformation[j];
#else
  RtlMoveMemory( &ei->er,me,sizeof(MINIDUMP_EXCEPTION) );
#endif

  // VC c++ exception {{{
  if( me->ExceptionCode==EXCEPTION_VC_CPP_EXCEPTION &&
      me->NumberParameters==THROW_ARGS )
  {
    const DWORD *ptr = (DWORD*)ei->er.ExceptionInformation[2];
#ifdef _WIN64
    char *mod = (char*)ei->er.ExceptionInformation[3];
#endif
    size_t maxSize;
    ptr = getDumpLoc( ad,(size_t)ptr,&maxSize );
    if( ptr && maxSize>=4*sizeof(DWORD) )
    {
      ptr = getDumpLoc( ad,CALC_THROW_ARG(mod,ptr[3]),&maxSize );
      if( ptr && maxSize>=2*sizeof(DWORD) )
      {
        ptr = getDumpLoc( ad,CALC_THROW_ARG(mod,ptr[1]),&maxSize );
        if( ptr && maxSize>=2*sizeof(DWORD) )
        {
          ptr = getDumpLoc( ad,
              CALC_THROW_ARG(mod,ptr[1])+2*sizeof(void*),&maxSize );
          if( ptr && maxSize )
          {
            if( maxSize>=sizeof(ei->throwName) )
              maxSize = sizeof(ei->throwName) - 1;
            RtlMoveMemory( ei->throwName,ptr,maxSize );
            ei->throwName[maxSize] = 0;
          }
        }
      }
    }
  }
  // }}}

#ifndef NO_DBGENG
  size_t ip = 0;
  if( ei->aa->frames[0] && ad->opt->exceptionDetails>1 )
  {
    ip = (size_t)getDumpLoc( ad,(size_t)ei->aa->frames[0]-1,NULL );
    ad->pi.dwProcessId = GetCurrentProcessId();
  }
#endif

  writeException( ad,NULL,
#ifndef NO_THREADS
      0,NULL,
#endif
#ifndef NO_DBGENG
      ip,
#endif
      ei,ad->mi_a,ad->mi_q );

  if( ad->dump_mi_map_a )
  {
    int i;
    for( i=0; i<ad->mi_q; i++ )
      if( (size_t)ad->dump_mi_map_a[i]>1 )
        UnmapViewOfFile( ad->dump_mi_map_a[i] );
    HeapFree( heap,0,(void*)ad->dump_mi_map_a );
  }

  UnmapViewOfFile( dump );
  dbgsym_close( &ds );
  HeapFree( heap,0,ei );

  return( 1 );
}
#endif

// }}}
// main loop {{{

static void mainLoop( appData *ad,UINT *exitCode )
{
  textColor *tcXml = writeXmlHeader( ad );
  textColor *tcSvg = writeSvgHeader( ad );

  HANDLE heap = ad->heap;
  dbgsym *ds = ad->ds;
  options *opt = ds->opt;
  textColor *tc = ds->tc;
  int type;
  modInfo *mi_a = NULL;
  int mi_q = 0;
  int terminated = -2;
  int alloc_show_q = 0;
  int error_q = 0;
#ifndef NO_THREADS
  int threadName_q = 0;
  char **threadName_a = NULL;
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
  exceptionInfo *ei = HeapAlloc( heap,0,sizeof(exceptionInfo) );
  DWORD flashStart = 0;
  int tsq = 0;

  // global hotkeys {{{
  typedef BOOL WINAPI func_RegisterHotkey( HWND,int,UINT,UINT );
  typedef BOOL WINAPI func_UnregisterHotkey( HWND,int );
  typedef DWORD WINAPI func_MsgWaitForMultipleObjects(
      DWORD,const HANDLE*,BOOL,DWORD,DWORD );
  typedef BOOL WINAPI func_PeekMessageA( LPMSG,HWND,UINT,UINT,UINT );

  HMODULE user32 = NULL;
  func_RegisterHotkey *fRegisterHotKey = NULL;
  func_MsgWaitForMultipleObjects *fMsgWaitForMultipleObjects = NULL;
  func_PeekMessageA *fPeekMessageA = NULL;
  func_UnregisterHotkey *fUnregisterHotkey = NULL;

  if( ad->globalHotkeys ) user32 = LoadLibrary( "user32.dll" );
  if( user32 )
  {
    fRegisterHotKey = (func_RegisterHotkey*)GetProcAddress(
        user32,"RegisterHotKey" );
    fMsgWaitForMultipleObjects =
      (func_MsgWaitForMultipleObjects*)GetProcAddress(
          user32,"MsgWaitForMultipleObjects" );
    fPeekMessageA = (func_PeekMessageA*)GetProcAddress(
        user32,"PeekMessageA" );
    fUnregisterHotkey = (func_UnregisterHotkey*)GetProcAddress(
        user32,"UnregisterHotKey" );
  }

  if( fRegisterHotKey )
  {
    fRegisterHotKey( NULL,HEOB_LEAK_RECORDING_STOP,MOD_CONTROL|MOD_ALT,'F' );
    fRegisterHotKey( NULL,HEOB_LEAK_RECORDING_START,MOD_CONTROL|MOD_ALT,'D' );
    fRegisterHotKey( NULL,HEOB_LEAK_RECORDING_CLEAR,MOD_CONTROL|MOD_ALT,'C' );
    fRegisterHotKey( NULL,HEOB_LEAK_RECORDING_SHOW,MOD_CONTROL|MOD_ALT,'S' );
  }
  // }}}

  // recording status in taskbar {{{
  typedef HRESULT WINAPI func_CoInitialize( LPVOID );
  typedef void WINAPI func_CoUninitialize( VOID );
  typedef HRESULT WINAPI func_CoCreateInstance(
      REFCLSID,LPUNKNOWN,DWORD,REFIID,LPVOID* );

  HWND conHwnd = GetConsoleWindow();
  HMODULE ole32 = NULL;
  ITaskbarList3 *tl3 = NULL;
  func_CoUninitialize *fCoUninitialize = NULL;
  int taskbarRecording = recording + 1;

  if( conHwnd && (in || fRegisterHotKey) &&
      (opt->newConsole || isConsoleOwner()) )
  {
    ole32 = LoadLibrary( "ole32.dll" );
    func_CoInitialize *fCoInitialize = NULL;
    func_CoCreateInstance *fCoCreateInstance = NULL;
    if( ole32 )
    {
      fCoInitialize =
        (func_CoInitialize*)GetProcAddress( ole32,"CoInitialize" );
      fCoUninitialize =
        (func_CoUninitialize*)GetProcAddress( ole32,"CoUninitialize" );
      fCoCreateInstance =
        (func_CoCreateInstance*)GetProcAddress( ole32,"CoCreateInstance" );
    }
    if( fCoInitialize && fCoUninitialize && fCoCreateInstance )
    {
      fCoInitialize( NULL );

      const GUID CLSID_TaskbarList =
      { 0x56fdf344,0xfd6d,0x11d0,{0x95,0x8a,0x00,0x60,0x97,0xc9,0xa0,0x90} };
      const GUID IID_ITaskbarList3 =
      { 0xea1afb91,0x9e28,0x4b86,{0x90,0xe9,0x9e,0x9f,0x8a,0x5e,0xef,0xaf} };

      fCoCreateInstance( &CLSID_TaskbarList,NULL,CLSCTX_INPROC_SERVER,
          &IID_ITaskbarList3,(void**)&tl3 );
      if( !tl3 ) fCoUninitialize();
    }
    if( ole32 && !tl3 )
    {
      FreeLibrary( ole32 );
      ole32 = NULL;
      fCoUninitialize = NULL;
    }
  }
  // }}}

  if( in ) showConsole();
  const char *title = opt->handleException>=2 ?
    "profiling sample recording: " : "leak recording: ";
#if USE_STACKWALK
  int tsqShow = in && opt->handleException>=2;
  ad->recordingSamples = opt->handleException>=2 ? recording>0 : 1;
#endif
  while( 1 )
  {
    if( needData )
    {
      if( !ReadFile(readPipe,&type,sizeof(int),NULL,&ov) &&
          GetLastError()!=ERROR_IO_PENDING ) break;
      needData = 0;

      if( in )
        showRecording( title,err,recording,&consoleCoord,&errColor,tsq );
    }

    if( tl3 && recording!=taskbarRecording )
    {
      int tbpf = recording<0 ? TBPF_NOPROGRESS :
        ( recording>0 ? TBPF_NORMAL : TBPF_PAUSED );
      tl3->lpVtbl->SetProgressState( tl3,conHwnd,tbpf );
      if( recording>=0 )
        tl3->lpVtbl->SetProgressValue( tl3,conHwnd,1,1 );
      taskbarRecording = recording;
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
        showRecording( title,err,recording,&consoleCoord,&errColor,tsq );
      }
      else
        waitTime = FLASH_TIMEOUT - flashTime;
    }
    // }}}
    DWORD didread;
    DWORD waitRet;
    if( !fMsgWaitForMultipleObjects )
      waitRet = WaitForMultipleObjects(
          waitCount,handles,FALSE,waitTime );
    else
      waitRet = fMsgWaitForMultipleObjects(
          waitCount,handles,FALSE,waitTime,QS_ALLEVENTS );
    if( waitRet==WAIT_TIMEOUT )
    {
      flashStart = GetTickCount() - 2*FLASH_TIMEOUT;
      continue;
    }
    else if( waitRet==WAIT_OBJECT_0+1 || waitRet==WAIT_OBJECT_0+2 )
    {
      // control leak recording {{{
      int cmd = -1;
      INPUT_RECORD ir;
      if( in && waitRet==WAIT_OBJECT_0+1 &&
          ReadConsoleInput(in,&ir,1,&didread) &&
          ir.EventType==KEY_EVENT &&
          ir.Event.KeyEvent.bKeyDown )
      {
        switch( ir.Event.KeyEvent.wVirtualKeyCode )
        {
          case 'N':
            cmd = HEOB_LEAK_RECORDING_START;
            break;

          case 'F':
            cmd = HEOB_LEAK_RECORDING_STOP;
            break;

          case 'C':
            cmd = HEOB_LEAK_RECORDING_CLEAR;
            break;

          case 'S':
            cmd = HEOB_LEAK_RECORDING_SHOW;
            break;
        }
      }
      else if( fPeekMessageA && waitRet==WAIT_OBJECT_0+waitCount )
      {
        MSG msg;
        while( fPeekMessageA(&msg,NULL,0,0,PM_REMOVE) )
          if( msg.message==WM_HOTKEY &&
              msg.wParam<=HEOB_LEAK_RECORDING_SHOW )
            cmd = (int)msg.wParam;
      }
      if( (recording>0 && cmd==HEOB_LEAK_RECORDING_START) ||
          (recording<0 && cmd!=HEOB_LEAK_RECORDING_START) ||
          (recording==0 && cmd==HEOB_LEAK_RECORDING_STOP) )
        cmd = -1;

      if( cmd>=HEOB_LEAK_RECORDING_STOP )
      {
        if( cmd>=HEOB_LEAK_RECORDING_CLEAR )
        {
          WriteFile( ad->controlPipe,&cmd,sizeof(int),&didread,NULL );

          // start flash of text to visualize clear/show was done {{{
          if( recording>0 && in )
          {
            flashStart = GetTickCount();
            recording = cmd;
            clearRecording( title,err,consoleCoord,errColor );
            showRecording( title,err,recording,&consoleCoord,&errColor,tsq );
          }
          // }}}
        }
        else
        {
          // start & stop only set the recording flag, and by doing this
          // directly, it also works if the target process is
          // suspended in a debugger
          WriteProcessMemory( ad->pi.hProcess,
              ad->recordingRemote,&cmd,sizeof(int),NULL );

          int prevRecording = recording;
          if( cmd==HEOB_LEAK_RECORDING_START || recording>0 )
            recording = cmd;
          if( prevRecording!=recording && in )
          {
            clearRecording( title,err,consoleCoord,errColor );
            showRecording( title,err,recording,&consoleCoord,&errColor,tsq );
          }

#if USE_STACKWALK
          if( opt->handleException>=2 )
          {
            EnterCriticalSection( &ad->csSampling );
            ad->recordingSamples = cmd;
            LeaveCriticalSection( &ad->csSampling );
          }
#endif
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

#if USE_STACKWALK
    EnterCriticalSection( &ad->csSampling );
#endif

    switch( type )
    {
      // leaks {{{

      case WRITE_LEAKS:
        {
          taskbarRecording = setTaskbarStatus( tl3,conHwnd );

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
#ifndef NO_THREADS
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
              dbgsym_loadmodule( ds,mi_a[m].path,mi_a[m].base,
                  (DWORD)mi_a[m].size );
          }
        }
#endif
        break;

        // }}}
        // exception {{{

      case WRITE_EXCEPTION:
        {
          taskbarRecording = setTaskbarStatus( tl3,conHwnd );

          if( !readFile(readPipe,ei,sizeof(exceptionInfo),&ov) )
            break;
          ei->throwName[sizeof(ei->throwName)-1] = 0;

#if USE_STACKWALK
          if( ds->swf.fStackWalk64 )
          {
            stackwalkDbghelp( &ds->swf,opt,ad->pi.hProcess,
                ei->thread,&ei->c,ei->aa[0].frames );
            CloseHandle( ei->thread );
          }
#endif

#ifndef NO_DBGENG
          size_t ip = 0;
          if( opt->exceptionDetails>1 &&
              ei->er.ExceptionCode!=EXCEPTION_BREAKPOINT )
            ip = (size_t)ei->aa[0].frames[0] - 1;
#endif

          writeException( ad,tcXml,
#ifndef NO_THREADS
              threadName_q,threadName_a,
#endif
#ifndef NO_DBGENG
              ip,
#endif
              ei,mi_a,mi_q );

          SetEvent( ad->exceptionWait );

          terminated = -1;
          *ad->heobExit = HEOB_EXCEPTION;
          *ad->heobExitData = ei->er.ExceptionCode;
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
          printThreadName( aa->threadNum );
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
          printThreadName( aa->threadNum );
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
            printAllocatedFreed( &aa[1],aa[2].ptr!=NULL,mi_a,mi_q,ds );
          }
          else if( aa[1].id==1 )
          {
            printf( "$I  pointing to stack\n" );
            printf( "$S  possibly same frame as:" );
            printThreadName( aa[1].threadNum );
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
            printAllocatedFreed( &aa[3],0,mi_a,mi_q,ds );
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
          printThreadName( aa[0].threadNum );
          printStackCount( aa[0].frames,aa[0].frameCount,
              mi_a,mi_q,ds,aa[0].ft,0 );

          printAllocatedFreed( &aa[1],1,mi_a,mi_q,ds );

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
          printAllocatedFreed( aa,1,mi_a,mi_q,ds );

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
        // free while realloc {{{

      case WRITE_FREE_WHILE_REALLOC:
        {
          if( !readFile(readPipe,aa,2*sizeof(allocation),&ov) )
            break;

          cacheSymbolData( aa,NULL,2,mi_a,mi_q,ds,1 );

          printf( "\n$Wfree while realloc"
              " of %p (size %U)\n",aa[0].ptr,aa[0].size );
          printAllocatedFreed( aa,1,mi_a,mi_q,ds );

          writeXmlFreeWhileRealloc( tcXml,ds,aa,mi_a,mi_q );

          error_q++;
        }
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
          printAllocatedFreed( aa,1,mi_a,mi_q,ds );

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

#ifndef NO_THREADS
      case WRITE_THREAD_NAME:
        {
          int threadNum;
          if( !readFile(readPipe,&threadNum,sizeof(int),&ov) )
            break;
          ASSUME( threadNum>0 );
          int tnq = threadName_q;
          while( tnq<threadNum ) tnq += 64;
          if( tnq>threadName_q )
          {
            if( !threadName_a )
              threadName_a = HeapAlloc( heap,0,
                  tnq*sizeof(char*) );
            else
              threadName_a = HeapReAlloc( heap,0,
                  threadName_a,tnq*sizeof(char*) );
            RtlZeroMemory( &threadName_a[threadName_q],
                (tnq-threadName_q)*sizeof(char*) );
            threadName_q = tnq;
          }

          int len;
          if( !readFile(readPipe,&len,sizeof(int),&ov) )
            break;
          if( threadName_a[threadNum-1] )
            HeapFree( heap,0,threadName_a[threadNum-1] );
          threadName_a[threadNum-1] =
            HeapAlloc( heap,HEAP_ZERO_MEMORY,len+1 );
          if( !readFile(readPipe,threadName_a[threadNum-1],len,&ov) )
            break;
        }
        break;
#endif

        // }}}
        // exit trace {{{

      case WRITE_EXIT_TRACE:
        {
          allocation *exitTrace = ei->aa;
          if( !readFile(readPipe,exitTrace,sizeof(allocation),&ov) )
            break;

          cacheSymbolData( exitTrace,NULL,1,mi_a,mi_q,ds,1 );
          printf( "$Sexit on:" );
          printThreadName( exitTrace->threadNum );
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
#if USE_STACKWALK
              if( opt->handleException>=2 ) ad->recordingSamples = 1;
#endif
              break;
            case HEOB_LEAK_RECORDING_STOP:
              if( recording>0 ) recording = 0;
#if USE_STACKWALK
              if( opt->handleException>=2 ) ad->recordingSamples = 0;
#endif
              break;
            case HEOB_LEAK_RECORDING_CLEAR:
#if USE_STACKWALK
              if( opt->handleException>=2 )
                ad->samp_q = 0;
#endif
              // fallthrough
            case HEOB_LEAK_RECORDING_SHOW:
              if( !recording ) recording = -1;
              break;
          }
        }
        break;

        // }}}
        // sampling profiler {{{

#if USE_STACKWALK
      case WRITE_SAMPLING:
        {
          taskbarRecording = setTaskbarStatus( tl3,conHwnd );

          printLeaks( ad->samp_a,ad->samp_q,0,0,0,0,NULL,mi_a,mi_q,
#ifndef NO_THREADS
              threadName_a,threadName_q,
#endif
              opt,tc,ds,heap,tcXml,ad,tcSvg,1 );

          ad->samp_q = 0;
        }
        break;

      case WRITE_ADD_SAMPLING_THREAD:
        {
          threadSamplingType tst;
          if( !readFile(readPipe,&tst,sizeof(tst),&ov) )
            break;

          threadSamplingType *thread_samp_a = ad->thread_samp_a;
          if( ad->thread_samp_q>=ad->thread_samp_s )
          {
            int thread_samp_s = ad->thread_samp_s + 64;
            if( !thread_samp_a )
              thread_samp_a = HeapAlloc( heap,0,
                  thread_samp_s*sizeof(threadSamplingType) );
            else
              thread_samp_a = HeapReAlloc( heap,0,thread_samp_a,
                  thread_samp_s*sizeof(threadSamplingType) );
            if( thread_samp_a )
              ad->thread_samp_s = thread_samp_s;
          }
          if( thread_samp_a )
          {
            thread_samp_a[ad->thread_samp_q] = tst;
            ad->thread_samp_a = thread_samp_a;
            ad->thread_samp_q++;
          }
          else
            CloseHandle( tst.thread );
          if( tsqShow ) tsq = ad->thread_samp_q;
        }
        break;

      case WRITE_REMOVE_SAMPLING_THREAD:
        {
          DWORD threadId;
          if( !readFile(readPipe,&threadId,sizeof(threadId),&ov) )
            break;

          threadSamplingType *thread_samp_a = ad->thread_samp_a;
          int thread_samp_q = ad->thread_samp_q;
          int ts;
          for( ts=thread_samp_q-1; ts>=0; ts-- )
          {
            if( thread_samp_a[ts].threadId!=threadId ) continue;

            thread_samp_q = --ad->thread_samp_q;
            CloseHandle( thread_samp_a[ts].thread );
            if( ts<thread_samp_q )
              thread_samp_a[ts] = thread_samp_a[thread_samp_q];
            break;
          }
          if( tsqShow ) tsq = thread_samp_q;
        }
        break;
#endif

        // }}}
        // crashdump {{{

#ifndef NO_DBGHELP
      case WRITE_CRASHDUMP:
        {
          taskbarRecording = setTaskbarStatus( tl3,conHwnd );

          DWORD threadId;
          PEXCEPTION_POINTERS ep;

          if( !readFile(readPipe,&threadId,sizeof(threadId),&ov) )
            break;
          if( !readFile(readPipe,&ep,sizeof(PEXCEPTION_POINTERS),&ov) )
            break;

          if( ds->fMiniDumpWriteDump )
          {
            // filename {{{
            const wchar_t *dumpNameFrom;
            if( ad->outName ) dumpNameFrom = ad->outName;
            else if( ad->xmlName ) dumpNameFrom = ad->xmlName;
            else if( ad->svgName ) dumpNameFrom = ad->svgName;
            else dumpNameFrom = L"crash-%p-%n.dmp";

            wchar_t *dumpName = expandFileNameVars( ad,dumpNameFrom,NULL );
            if( !dumpName )
            {
              dumpName = HeapAlloc( heap,0,(lstrlenW(dumpNameFrom)+1)*2 );
              lstrcpyW( dumpName,dumpNameFrom );
            }

            wchar_t *lastDelim = strrchrW( dumpName,'\\' );
            wchar_t *lastDelim2 = strrchrW( dumpName,'/' );
            if( lastDelim2>lastDelim ) lastDelim = lastDelim2;
            if( !lastDelim ) lastDelim = dumpName;
            else lastDelim++;
            wchar_t *lastPoint = strrchrW( lastDelim,'.' );
            if( !lastPoint || lastPoint==lastDelim )
              lastPoint = &lastDelim[lstrlenW(lastDelim)];
            if( lstrcmpiW(lastPoint,L".dmp") )
            {
              wchar_t *dumpNameOld = dumpName;
              dumpName = HeapAlloc( heap,0,(lstrlenW(dumpNameOld)+5)*2 );
              lstrcpyW( dumpName,dumpNameOld );
              lstrcpyW( &dumpName[lastPoint-dumpNameOld],L".dmp" );
              HeapFree( heap,0,dumpNameOld );
            }
            // }}}

            HANDLE dumpFile = CreateFileW( dumpName,GENERIC_WRITE,
                0,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
            if( dumpFile!=INVALID_HANDLE_VALUE )
            {
              MINIDUMP_EXCEPTION_INFORMATION mei;
              mei.ThreadId = threadId;
              mei.ExceptionPointers = ep;
              mei.ClientPointers = TRUE;

              MINIDUMP_TYPE dumpType = opt->exceptionDetails==-1 ?
                MiniDumpWithFullMemory : MiniDumpNormal;
              ds->fMiniDumpWriteDump( ad->pi.hProcess,ad->pi.dwProcessId,
                  dumpFile,dumpType,&mei,NULL,NULL );

              CloseHandle( dumpFile );

              printf( "\n$Screated minidump file: $O%S\n",dumpName );
            }

            HeapFree( heap,0,dumpName );
          }

          SetEvent( ad->miniDumpWait );
        }
        break;
#endif

        // }}}
    }

#if USE_STACKWALK
    LeaveCriticalSection( &ad->csSampling );
#endif
  }

  if( fUnregisterHotkey )
  {
    int i;
    for( i=HEOB_LEAK_RECORDING_STOP; i<=HEOB_LEAK_RECORDING_SHOW; i++ )
      fUnregisterHotkey( NULL,i );
  }
  if( user32 ) FreeLibrary( user32 );

  if( tl3 )
  {
    tl3->lpVtbl->SetProgressState( tl3,conHwnd,TBPF_NOPROGRESS );
    tl3->lpVtbl->Release( tl3 );
  }
  if( fCoUninitialize ) fCoUninitialize();
  if( ole32 ) FreeLibrary( ole32 );

  dbgsym_close( ds );

  if( terminated==-2 )
  {
    int errorWritten;
    DWORD ec = unexpectedEnd( ad,tcXml,&errorWritten );
    printf( "\n$Wunexpected end of application" );
    if( ec ) printf( " (%x)",ec );
    printf( "\n" );
    if( !errorWritten )
      *ad->heobExit = HEOB_UNEXPECTED_END;
    else
    {
      *ad->heobExit = HEOB_OK;
      *ad->heobExitData = ec;
    }
  }

  CloseHandle( ov.hEvent );
  HeapFree( heap,0,aa );
  HeapFree( heap,0,ei );
  if( mi_a ) HeapFree( heap,0,mi_a );
#ifndef NO_THREADS
  if( threadName_a )
  {
    int i;
    for( i=0; i<threadName_q; i++ )
      if( threadName_a[i] ) HeapFree( heap,0,threadName_a[i] );
    HeapFree( heap,0,threadName_a );
  }
#endif

  if( opt->leakErrorExitCode>1 && !*exitCode )
  {
    deleteFileOnClose( tc );
    deleteFileOnClose( tcXml );
    deleteFileOnClose( tcSvg );
  }

  writeXmlFooter( tcXml,ad );
  writeSvgFooter( tcSvg,ad );
}

// }}}
// help text {{{

static void showHelpText( appData *ad,options *defopt,int fullhelp )
{
  textColor *tc = ad->tcOut;
  wchar_t *exePath = ad->exePathW;
  GetModuleFileNameW( NULL,exePath,MAX_PATH );
  wchar_t *delim = strrchrW( exePath,'\\' );
  if( delim ) delim++;
  else delim = exePath;
  wchar_t *point = strrchrW( delim,'.' );
  if( point ) point[0] = 0;

  printf( "Usage: $O%S $I[OPTION]... $SAPP [APP-OPTION]...\n",
      delim );
#if USE_STACKWALK
  if( fullhelp )
    printf( "       $O%S $I[OPTION]... $SCRASH.DMP [SYMBOL-PATH]\n",
        delim );
#endif
  printf( "\n" );
  if( fullhelp )
    printf( "    $I-A$BX$N    attach to thread\n" );
  printf( "    $I-o$BX$N    heob output [$I%d$N]",1 );
  if( fullhelp>1 )
  {
    printf( "%i file format specifiers\n",1 );
    printf( "              $I0$N = none    %i   "
        "$I%%p$N = PID (enables child process injection)\n",1 );
    printf( "              $I1$N = stdout  %i   $I%%n$N = name\n",1 );
    printf( "              $I2$N = stderr  %i   $I%%P$N = parent PID\n",1 );
    printf( "              $I...$N = file  %i   $I%%N$N = parent name\n",1 );
  }
  else
    printf( "\n" );
  if( fullhelp )
  {
    printf( "    $I-x$BX$N    xml output" );
    if( fullhelp>1 )
      printf( "     %i   "
          "$I%%c$N = counter (enables child process injection)",1 );
    printf( "\n" );
    printf( "    $I-v$BX$N    svg output" );
    if( fullhelp>1 )
      printf( "     %i",1 );
    printf( "\n" );
    printf( "    $I-y$BX$N    symbol path\n" );
  }
  printf( "    $I-P$BX$N    show process ID and wait [$I%d$N]\n",
      defopt->pid );
  printf( "    $I-c$BX$N    create new console [$I%d$N]\n",
      defopt->newConsole );
  if( fullhelp>1 )
  {
    printf( "              $I0$N = none\n" );
    printf( "              $I1$N = target application\n" );
    printf( "              $I2$N = heob\n" );
    printf( "              $I3$N = target application and heob\n" );
  }
  printf( "    $I-p$BX$N    page protection [$I%d$N]\n",
      defopt->protect );
  if( fullhelp>1 )
  {
    printf( "              $I0$N = off\n" );
    printf( "              $I1$N = after\n" );
    printf( "              $I2$N = before\n" );
  }
  printf( "    $I-f$BX$N    freed memory protection [$I%d$N]\n",
      defopt->protectFree );
  printf( "    $I-d$BX$N    monitor dlls [$I%d$N]\n",
      defopt->dlls );
  if( fullhelp>1 )
  {
    printf( "              $I0$N = off\n" );
    printf( "              $I1$N = static\n" );
    printf( "              $I2$N = static and dynamic (never free)\n" );
    printf( "              $I3$N = static and dynamic (free on end)\n" );
    printf( "              $I4$N = static and dynamic"
        " (leak detection after dll unload)\n" );
  }
  if( fullhelp )
  {
    printf( "    $I-a$BX$N    alignment [$I%d$N]\n",
        defopt->align );
    printf( "    $I-M$BX$N    minimum page protection size [$I%d$N]\n",
        defopt->minProtectSize );
    printf( "    $I-i$BX$N    initial value [$I%d$N]\n",
        (int)(defopt->init&0xff) );
    if( fullhelp>1 )
    {
      printf( "              $I1$N   = 0x0101010101010101\n" );
      printf( "    $I-i$BV$I:$BS$N  initial $Iv$Nalue with $Is$Ntride\n" );
      printf( "              $I1$N:$I2$N = 0x0001000100010001\n" );
      printf( "              $I1$N:$I4$N = 0x0000000100000001\n" );
      printf( "              $I1$N:$I8$N = 0x0000000000000001\n" );
    }
    printf( "    $I-s$BX$N    initial value for slack"
        " ($I-1$N = off) [$I%d$N]\n",
        defopt->slackInit );
  }
  printf( "    $I-h$BX$N    handle exceptions [$I%d$N]\n",
      defopt->handleException );
  if( fullhelp>1 )
  {
    printf( "              $I0$N = off\n" );
    printf( "              $I1$N = on\n" );
    printf( "              $I2$N = only\n" );
  }
  printf( "    $I-R$BX$N    "
      "raise breakpoint exception on allocation # [$I%d$N]\n",
      0 );
  printf( "    $I-r$BX$N    raise breakpoint exception on error [$I%d$N]\n",
      defopt->raiseException );
  if( fullhelp>1 )
  {
    printf( "              $I0$N = off\n" );
    printf( "              $I1$N = on\n" );
    printf( "              $I2$N = on,"
        " mismatching allocation/release method\n" );
  }
  if( fullhelp )
    printf( "    $I-D$BX$N    show exception details [$I%d$N]\n",
        defopt->exceptionDetails );
  if( fullhelp>1 )
  {
    printf( "             $I-2$N = write minidump\n" );
    printf( "             $I-1$N = write minidump with full memory\n" );
    printf( "              $I0$N = none\n" );
    printf( "              $I1$N = registers\n" );
    printf( "              $I2$N = registers / assembly instruction\n" );
    printf( "              $I3$N ="
        " registers / assembly instruction / modules\n" );
  }
  if( fullhelp )
  {
    printf( "    $I-S$BX$N    use stack pointer in exception [$I%d$N]\n",
        defopt->useSp );
    printf( "    $I-m$BX$N    compare allocation/release method [$I%d$N]\n",
        defopt->allocMethod );
  }
  if( fullhelp>1 )
  {
    printf( "              $I0$N = off\n" );
    printf( "              $I1$N = on\n" );
    printf( "              $I2$N = on, compare delete/delete[]\n" );
  }
  if( fullhelp )
  {
    printf( "    $I-n$BX$N    find nearest allocation [$I%d$N]\n",
        defopt->findNearest );
    printf( "    $I-g$BX$N    group identical leaks [$I%d$N]\n",
        defopt->groupLeaks );
  }
  if( fullhelp>1 )
  {
    printf( "              $I0$N = off\n" );
    printf( "              $I1$N = on\n" );
    printf( "              $I2$N = on, merge common stack frames\n" );
    printf( "              $I3$N = sort by thread and time\n" );
  }
  printf( "    $I-F$BX$N    show full path [$I%d$N]\n",
      defopt->fullPath );
  printf( "    $I-l$BX$N    show leak details [$I%d$N]\n",
      defopt->leakDetails );
  if( fullhelp>1 )
  {
    printf( "              $I0$N = none\n" );
    printf( "              $I1$N = simple\n" );
    printf( "              $I2$N = detect leak types\n" );
    printf( "              $I3$N = detect leak types (show reachable)\n" );
    printf( "              $I4$N = fuzzy detect leak types\n" );
    printf( "              $I5$N ="
        " fuzzy detect leak types (show reachable)\n" );
  }
  printf( "    $I-z$BX$N    minimum leak size [$I%U$N]\n",
      defopt->minLeakSize );
  printf( "    $I-k$BX$N    control leak recording [$I%d$N]\n",
      defopt->leakRecording );
  if( fullhelp>1 )
  {
    printf( "              $I0$N = off\n" );
    printf( "              $I1$N = on (start disabled)\n" );
    printf( "              $I2$N = on (start enabled)\n" );
  }
  if( fullhelp )
    printf( "    $I-G$BX$N    "
        "use global hotkeys to control leak recording [$I%d$N]\n",0 );
  if( fullhelp>1 )
  {
    printf( "              $ICtrl$N+$IAlt$N+$IF$N = off\n" );
    printf( "              $ICtrl$N+$IAlt$N+$ID$N = on\n" );
    printf( "              $ICtrl$N+$IAlt$N+$IC$N = clear\n" );
    printf( "              $ICtrl$N+$IAlt$N+$IS$N = show\n" );
  }
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
  }
  if( fullhelp>1 )
  {
    printf( "              $I0$N = off\n" );
    printf( "              $I1$N = on\n" );
    printf( "              $I2$N = on,"
        " only keep output files with leaks or errors\n" );
  }
#if USE_STACKWALK
  if( fullhelp )
    printf( "    $I-I$BX$N    sampling profiler interval [$I%d$N]\n",
        defopt->samplingInterval );
  if( fullhelp>1 )
  {
    printf( "             <$I0$N = only main thread\n" );
    printf( "              $I0$N = off\n" );
    printf( "             >$I0$N = all threads\n" );
  }
#endif
  if( fullhelp )
  {
    printf( "    $I-O$BA$I:$BO$I; a$Npplication specific $Io$Nptions\n" );
    printf( "    $I-X$N     "
        "disable heob via application specific options\n" );
    printf( "    $I-\"$BM$I\"$BB$N  trace mode:"
        " load $Im$Nodule on $Ib$Nase address\n" );
  }

  int helpKey = tc->fTextColor==&TextColorConsole &&
    fullhelp<2 && ad->in && isConsoleOwner();
  printf( "    $I-H$N[$IH$N]  show full " );
  printf( helpKey ? "$Wh$N" : "h" );
  printf( "elp\n" );

  printf( "\n$Ohe$Nap-$Oob$Nserver %s ($O" BITS "$Nbit)\n",HEOB_VER );

  if( waitForKeyIfConsoleOwner(tc,ad->in)=='H' && helpKey )
  {
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo( tc->out,&csbi );
    COORD coord = { 0,0 };
    DWORD w;
    FillConsoleOutputCharacter( tc->out,' ',
        csbi.dwSize.X*csbi.dwSize.Y,coord,&w );
    FillConsoleOutputAttribute( tc->out,tc->colors[ATT_NORMAL],
        csbi.dwSize.X*csbi.dwSize.Y,coord,&w );
    SetConsoleCursorPosition( tc->out,coord );

    showHelpText( ad,defopt,fullhelp+1 );
  }

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
  SetErrorMode( SEM_FAILCRITICALERRORS );

  HANDLE heap = GetProcessHeap();
  appData *ad = initHeob( heap );
  ad->in = GetStdHandle( STD_INPUT_HANDLE );
  if( !FlushConsoleInputBuffer(ad->in) ) ad->in = NULL;

  // command line arguments {{{
  wchar_t *cmdLine = GetCommandLineW();
  wchar_t *args;
  if( cmdLine[0]=='"' && (args=strchrW(cmdLine+1,'"')) )
    args++;
  else
    args = strchrW( cmdLine,' ' );
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
#if USE_STACKWALK
    0,                              // sampling profiler interval
#endif
  };
  // }}}
  options opt = defopt;
  ad->opt = &opt;
  int fullhelp = 0;
  wchar_t badArg = 0;
  int keepSuspended = 0;
  int fakeAttached = 0;
  // permanent options {{{
  opt.groupLeaks = -1;
#if USE_STACKWALK
  opt.handleException = -1;
#endif
  while( args )
  {
    while( args[0]==' ' ) args++;
    if( args[0]!='-' ) break;
    wchar_t *ro = readOption( args,ad,heap );
    if( ro )
    {
      args = ro;
      continue;
    }
    switch( args[1] )
    {
      case 'c':
        opt.newConsole = wtoi( args+2 );
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

      case 'y':
        if( ad->symPath ) break;
        ad->symPath = getQuotedStringOption( args+2,heap,&args );
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
          wchar_t *start = args + 2;
          ad->pi.dwThreadId = (DWORD)wtop( start );

          while( start[0] && start[0]!=' ' && start[0]!='/' && start[0]!='+' &&
              start[0]!='*' ) start++;
          if( start[0]=='/' )
          {
            ad->ppid = (DWORD)wtop( start+1 );
            while( start[0] && start[0]!=' ' && start[0]!='+' &&
                start[0]!='*' ) start++;

            if( ad->ppid )
            {
              HANDLE pProcess = OpenProcess(
                  PROCESS_QUERY_INFORMATION,FALSE,ad->ppid );
              if( pProcess )
              {
                nameOfProcess( ntdll,heap,pProcess,ad->pExePath,0 );
                CloseHandle( pProcess );
              }
            }
          }
          if( start[0]=='+' )
          {
            keepSuspended = wtoi( start+1 );
            while( start[0] && start[0]!=' ' && start[0]!='*' ) start++;
          }
          if( start[0]=='*' )
            getAppCounter( ad,wtoi(start+1),0,1 );

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
        opt.leakErrorExitCode = wtoi( args+2 );
        break;

      case 'G':
        ad->globalHotkeys = wtoi( args+2 );
        break;

      case 'O':
        {
          wchar_t *optionStart = args + 2;
          wchar_t *optionEnd = optionStart;
          while( *optionEnd && *optionEnd!=' ' )
          {
            optionEnd = strchrW( optionEnd,':' );
            if( !optionEnd ) break;
            optionEnd = strchrW( optionEnd+1,';' );
            if( !optionEnd ) break;
            optionEnd++;
          }
          if( optionEnd && optionEnd>optionStart )
          {
            size_t curLen =
              ad->specificOptions ? lstrlenW( ad->specificOptions ) : 0;
            size_t addLen = optionEnd - optionStart;
            if( !ad->specificOptions )
              ad->specificOptions = HeapAlloc( heap,0,2*(curLen+addLen+1) );
            else
              ad->specificOptions = HeapReAlloc(
                  heap,0,ad->specificOptions,2*(curLen+addLen+1) );
            RtlMoveMemory( ad->specificOptions+curLen,optionStart,2*addLen );
            ad->specificOptions[curLen+addLen] = 0;
          }
          args = optionEnd;
        }
        break;

      case '"':
        {
          wchar_t *start = args + 2;
          wchar_t *end = start;
          while( *end && *end!='"' ) end++;
          if( !*end || end<=start ) break;
          uintptr_t base = wtop( end+1 );
          if( !base ) break;
          ad->mi_q++;
          if( !ad->mi_a )
            ad->mi_a = HeapAlloc( heap,0,ad->mi_q*sizeof(modInfo) );
          else
            ad->mi_a = HeapReAlloc( heap,0,ad->mi_a,ad->mi_q*sizeof(modInfo) );
          modInfo *mi = ad->mi_a + ( ad->mi_q-1 );
          mi->base = base;
          mi->size = 0;
          size_t len = end - start;
          wchar_t *localName = HeapAlloc( heap,0,2*MAX_PATH );
          RtlMoveMemory( localName,start,2*len );
          localName[len] = 0;
          if( !SearchPathW(NULL,localName,NULL,MAX_PATH,mi->path,NULL) )
            RtlMoveMemory( mi->path,localName,2*len+2 );
          HeapFree( heap,0,localName );

          args = end;
        }
        break;

      case 'H':
        fullhelp = 1;
        while( args[1+fullhelp]=='H' ) fullhelp++;
        args = NULL;
        break;

      default:
        badArg = args[1];
        args = NULL;
        break;
    }
    while( args && args[0] && args[0]!=' ' ) args++;
  }
  // }}}
  if( opt.align<MEMORY_ALLOCATION_ALIGNMENT )
  {
    opt.init = 0;
    opt.slackInit = -1;
  }
  int outNameNum = -1;
  if( ad->outName && ad->outName[0]>='0' &&
      ad->outName[0]<='2' && !ad->outName[1] )
    outNameNum = ad->outName[0] - '0';
  HANDLE out = GetStdHandle( STD_OUTPUT_HANDLE );
  ad->tcOut = HeapAlloc( heap,0,sizeof(textColor) );
  textColor *tc = ad->tcOut;
  checkOutputVariant( tc,out,NULL );

  // bad argument {{{
  if( badArg )
  {
    wchar_t arg0[2] = { badArg,0 };
    printf( "$Wunknown argument: $I-%S\n",arg0 );

    exitHeob( ad,HEOB_BAD_ARG,badArg,0x7fffffff );
  }
  // }}}

  // trace mode {{{
  if( ad->mi_a )
  {
    allocation *a = HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(allocation) );
    modInfo *a2l_mi_a = ad->mi_a;
    int a2l_mi_q = ad->mi_q;

    while( args && args[0]>='0' && args[0]<='9' )
    {
      uintptr_t ptr = wtop( args );
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
      dbgsym_init( &ds,(HANDLE)0x1,tc,&opt,NULL,heap,ad->symPath,FALSE,NULL );

#ifndef NO_DBGHELP
      if( ds.fSymLoadModule64 )
      {
        int i;
        for( i=0; i<a2l_mi_q; i++ )
          dbgsym_loadmodule( &ds,a2l_mi_a[i].path,
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

#if USE_STACKWALK
  if( isMinidump(ad,args) )
    exitHeob( ad,HEOB_TRACE,0,0 );
#endif

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

  if( !opt.newConsole && (opt.leakRecording ||
        // check if console output is possible with global hotkey Ctrl+Alt+S
        (ad->globalHotkeys &&
         (outNameNum>0 ||
          (!ad->outName && !ad->xmlName && !ad->svgName &&
           tc->fTextColor==&TextColorConsole)))) )
    opt.newConsole = 1;

  // create target application {{{
  ad->cmdLineW = cmdLine;
  ad->argsW = args;

  if( !opt.attached )
  {
    STARTUPINFOW si;
    RtlZeroMemory( &si,sizeof(STARTUPINFO) );
    si.cb = sizeof(STARTUPINFO);
    BOOL result = CreateProcessW( NULL,args,NULL,NULL,FALSE,
        CREATE_SUSPENDED|((opt.newConsole&1)?CREATE_NEW_CONSOLE:0),
        NULL,NULL,&si,&ad->pi );
    if( !result )
    {
      DWORD e = GetLastError();
      printf( "$Wcan't create process for '%S' (%e)\n",args,e );
      exitHeob( ad,HEOB_PROCESS_FAIL,e,0x7fffffff );
    }

    int needNewHeob = ( opt.newConsole>1 || isWrongArch(ad->pi.hProcess) );

    if( (ad->outName && strstrW(ad->outName,L"%c")) ||
        (ad->xmlName && strstrW(ad->xmlName,L"%c")) ||
        (ad->svgName && strstrW(ad->svgName,L"%c")) )
      getAppCounter( ad,GetCurrentProcessId(),1,!needNewHeob );

    if( needNewHeob )
    {
      HMODULE kernel32 = GetModuleHandle( "kernel32.dll" );
      func_CreateProcessW *fCreateProcessW =
        (func_CreateProcessW*)GetProcAddress( kernel32,"CreateProcessW" );
      DWORD exitCode = 0;
      if( !heobSubProcess(0,&ad->pi,NULL,heap,&opt,ad->appCounterID,
            fCreateProcessW,ad->outName,ad->xmlName,ad->svgName,NULL,
            ad->symPath,ad->raise_alloc_q,ad->raise_alloc_a,
            ad->specificOptions) )
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
  wchar_t *exePath = ad->exePathW;
  if( ad->specificOptions || opt.attached || ad->outName )
  {
    nameOfProcess( ntdll,heap,ad->pi.hProcess,exePath,1 );
    setHeobConsoleTitle( heap,exePath );
  }
  // }}}

  // application specific options {{{
  defopt = opt;
  // enable extended stack grouping for svg by default
  if( defopt.groupLeaks<0 )
    defopt.groupLeaks = ( !ad->xmlName && ad->svgName ) ? 2 : 1;
#if USE_STACKWALK
  // disable heap monitoring for sampling profiler by default
  if( defopt.handleException<0 )
    defopt.handleException = defopt.samplingInterval ? 2 : 1;
#endif
  if( ad->specificOptions )
  {
    int nameLen = lstrlenW( exePath );
    wchar_t *name = ad->specificOptions;
    lstrcpyW( exePath+nameLen,L":" );
    int disable = 0;
    while( 1 )
    {
      wchar_t *nameEnd = strchrW( name,':' );
      if( !nameEnd ) break;
      wchar_t *so = NULL;
      if( strstartW(name,exePath) || strstartW(name,L"*" BITS ":") )
        so = nameEnd + 1;
      name = strchrW( nameEnd+1,';' );
      if( !name ) break;
      name++;

      while( so )
      {
        while( so[0]==' ' ) so++;
        if( so[0]!='-' ) break;
        if( so[1]=='X' ) disable = 1;
        so = readOption( so,ad,heap );
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
  if( opt.groupLeaks<0 ) opt.groupLeaks = defopt.groupLeaks;
#if USE_STACKWALK
  // disable heap monitoring for sampling profiler by default
  if( opt.handleException<0 )
    opt.handleException = opt.samplingInterval ? 2 : 1;
#endif
  // disable depending options
  if( opt.protect<1 ) opt.protectFree = 0;
  if( opt.handleException>=2 )
  {
    opt.protect = opt.protectFree = opt.leakDetails = 0;
#if USE_STACKWALK
    if( !opt.samplingInterval )
#endif
      opt.leakRecording = ad->globalHotkeys = 0;
  }
  if( !opt.handleException ) opt.exceptionDetails = 0;
  // }}}

  // output destination {{{
  const wchar_t *subOutName = NULL;
  if( ad->outName )
  {
    if( outNameNum>=0 )
    {
      out = outNameNum==0 ? NULL : GetStdHandle(
          outNameNum==1 ? STD_OUTPUT_HANDLE : STD_ERROR_HANDLE );
      checkOutputVariant( tc,out,NULL );

      HeapFree( heap,0,ad->outName );
      ad->outName = NULL;
    }
    else
    {
      if( strstrW(ad->outName,L"%p") || strstrW(ad->outName,L"%c") )
      {
        subOutName = ad->outName;
        opt.children = 1;
      }

      wchar_t *fullName = expandFileNameVars( ad,ad->outName,exePath );
      wchar_t *usedName = fullName ? fullName : ad->outName;

      int access = GENERIC_WRITE | ( opt.leakErrorExitCode>1 ? DELETE : 0 );
      out = CreateFileW( usedName,access,FILE_SHARE_READ,
          NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL );
      if( out==INVALID_HANDLE_VALUE ) out = tc->out;

      if( fullName ) HeapFree( heap,0,fullName );
    }
    if( out!=tc->out )
    {
      ad->tcOutOrig = ad->tcOut;
      tc = ad->tcOut = HeapAlloc( heap,0,sizeof(textColor) );
      checkOutputVariant( tc,out,ad->exePathW );
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

  const wchar_t *subXmlName = NULL;
  if( ad->xmlName &&
      (strstrW(ad->xmlName,L"%p") || strstrW(ad->xmlName,L"%c")) )
  {
    subXmlName = ad->xmlName;
    opt.children = 1;
  }
  const wchar_t *subSvgName = NULL;
  if( ad->svgName &&
      (strstrW(ad->svgName,L"%p") || strstrW(ad->svgName,L"%c")) )
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
    ad->readPipe = inject( ad,&defopt,tc,
        subOutName,subXmlName,subSvgName,subCurDir,ad->symPath );
  if( !ad->readPipe )
  {
    if( heobExit==HEOB_PROCESS_FAIL )
    {
      int errorWritten;
      DWORD ec = unexpectedEnd( ad,NULL,&errorWritten );
      printf( "\n$Wprocess initialization failed" );
      if( ec ) printf( " (%x)",ec );
      printf( "\n" );

      if( errorWritten )
      {
        heobExit = HEOB_OK;
        heobExitData = ec;
      }
    }
    TerminateProcess( ad->pi.hProcess,1 );
  }

  UINT exitCode = 0x7fffffff;
  if( ad->readPipe )
  {
    wchar_t *delim = strrchrW( ad->exePathW,'\\' );
    if( delim ) delim[0] = 0;
    const wchar_t *symPath = exePath;
    wchar_t *symPathBuf = NULL;
    if( ad->symPath )
    {
      symPath = symPathBuf = HeapAlloc( heap,0,
          2*(lstrlenW(exePath)+lstrlenW(ad->symPath)+2) );
      lstrcpyW( symPathBuf,exePath );
      lstrcatW( symPathBuf,L";" );
      lstrcatW( symPathBuf,ad->symPath );
    }
    dbgsym ds;
    dbgsym_init( &ds,ad->pi.hProcess,tc,&opt,funcnames,heap,symPath,TRUE,
        RETURN_ADDRESS() );
    ds.threadInitAddr += ad->kernel32offset;
    ad->ds = &ds;
    if( delim ) delim[0] = '\\';
    if( symPathBuf ) HeapFree( heap,0,symPathBuf );

#if USE_STACKWALK
    if( opt.samplingInterval &&
        (!ds.swf.fStackWalk64 || !ds.swf.fSymFunctionTableAccess64 ||
         !ds.swf.fSymGetModuleBase64) )
    {
      printf( "$Wsampling profiler needs StackWalk64() from dbghelp.dll\n" );
      heobExit = HEOB_NO_CRT;
      dbgsym_close( &ds );
      exitHeob( ad,heobExit,heobExitData,exitCode );
    }

    if( !ds.swf.fStackWalk64 )
    {
      int noStackWalk = 1;
      WriteProcessMemory( ad->pi.hProcess,
          ad->recordingRemote,&noStackWalk,sizeof(int),NULL );
    }
#endif

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

    if( ad->in && !opt.leakRecording && tc->fTextColor!=&TextColorConsole &&
        isConsoleOwner() )
      FreeConsole();

#if USE_STACKWALK
    HANDLE sampler = NULL;
    if( opt.samplingInterval )
    {
      ad->samplingInit = CreateEvent( NULL,TRUE,FALSE,NULL );
      sampler = CreateThread( NULL,0,&samplingThread,ad,0,NULL );
      WaitForSingleObject( ad->samplingInit,INFINITE );
      CloseHandle( ad->samplingInit );
    }
#endif

    if( !keepSuspended )
      ResumeThread( ad->pi.hThread );

    if( ad->attachEvent )
    {
      SetEvent( ad->attachEvent );
      CloseHandle( ad->attachEvent );
      ad->attachEvent = NULL;
    }

    mainLoop( ad,&exitCode );

#if USE_STACKWALK
    if( sampler )
    {
      SetEvent( ad->samplingStop );
      WaitForSingleObject( sampler,INFINITE );
      CloseHandle( sampler );
    }
#endif
  }

  exitHeob( ad,heobExit,heobExitData,exitCode );
}

// }}}

// vim:fdm=marker
