
//          Copyright Hannes Domani 2014 - 2016.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// includes {{{

#include "heob.h"

#include <stdint.h>

#ifdef _MSC_VER
#include <intrin.h>
#endif

// }}}
// defines {{{

#define SPLIT_MASK 0x3fff

#define REL_PTR( base,ofs ) ( ((PBYTE)base)+ofs )

// }}}
// local data {{{

typedef struct
{
  CRITICAL_SECTION cs;
  allocation *alloc_a;
  int alloc_q;
  int alloc_s;
}
splitAllocation;

typedef struct
{
  allocation a;
  void *frames[PTRS];
}
freed;

typedef struct
{
  const void **start;
  const void **end;
}
modMemType;

typedef struct localData
{
  func_LoadLibraryA *fLoadLibraryA;
  func_LoadLibraryW *fLoadLibraryW;
  func_FreeLibrary *fFreeLibrary;
  func_GetProcAddress *fGetProcAddress;
  func_ExitProcess *fExitProcess;
  func_TerminateProcess *fTerminateProcess;

  func_malloc *fmalloc;
  func_calloc *fcalloc;
  func_free *ffree;
  func_realloc *frealloc;
  func_strdup *fstrdup;
  func_wcsdup *fwcsdup;
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

  func_free *ofree;
  func_free *oop_delete;
  func_free *oop_delete_a;
  func_getcwd *ogetcwd;
  func_wgetcwd *owgetcwd;
  func_getdcwd *ogetdcwd;
  func_wgetdcwd *owgetdcwd;
  func_fullpath *ofullpath;
  func_wfullpath *owfullpath;
  func_tempnam *otempnam;
  func_wtempnam *owtempnam;

  HANDLE master;
  HMODULE kernel32;

  splitAllocation *splits;
  int ptrShift;
  allocType newArrAllocMethod;

  HANDLE heap;
  DWORD pageSize;
  size_t pageAdd;
  HANDLE crtHeap;

  options opt;

  CRITICAL_SECTION csWrite;
  CRITICAL_SECTION csFreed;

  // protected by csWrite {{{

  HMODULE *mod_a;
  int mod_q;
  int mod_s;
  int mod_d;

  modMemType *mod_mem_a;
  int mod_mem_q;
  int mod_mem_s;

  int cur_id;
  int raise_id;
  int raise_alloc_q;
  int *raise_alloc_a;

  // }}}
  // protected by csFreed {{{

  freed *freed_a;
  int freed_q;
  int freed_s;

  HMODULE *freed_mod_a;
  int freed_mod_q;
  int freed_mod_s;
  int inExit;

  // }}}
}
localData;

static localData *g_ld = NULL;
#define GET_REMOTEDATA( ld ) localData *ld = g_ld

// }}}
// process exit {{{

static NORETURN void exitWait( UINT c,int terminate )
{
  GET_REMOTEDATA( rd );

  FlushFileBuffers( rd->master );
  CloseHandle( rd->master );
  rd->opt.raiseException = 0;

  if( rd->opt.newConsole )
  {
    HANDLE in = GetStdHandle( STD_INPUT_HANDLE );
    if( FlushConsoleInputBuffer(in) )
    {
      HANDLE out = GetStdHandle( STD_OUTPUT_HANDLE );
      DWORD written;
      const char *exitText =
        "\n\n-------------------- APPLICATION EXIT --------------------\n";
      WriteFile( out,exitText,lstrlen(exitText),&written,NULL );

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

  if( !terminate )
    rd->fExitProcess( c );
  else
    rd->fTerminateProcess( GetCurrentProcess(),c );

  UNREACHABLE;
}

// }}}
// send module information {{{

static void writeModsFind( allocation *alloc_a,int alloc_q,
    modInfo **p_mi_a,int *p_mi_q )
{
  GET_REMOTEDATA( rd );

  int mi_q = *p_mi_q;
  modInfo *mi_a = *p_mi_a;

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
      if( !VirtualQuery((void*)ptr,&mbi,
            sizeof(MEMORY_BASIC_INFORMATION)) ||
          !(mbi.Protect&(PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE)) )
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
        mi_a = HeapAlloc( rd->heap,0,mi_q*sizeof(modInfo) );
      else
        mi_a = HeapReAlloc(
            rd->heap,0,mi_a,mi_q*sizeof(modInfo) );
      if( !mi_a )
      {
        DWORD written;
        int type = WRITE_MAIN_ALLOC_FAIL;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );
        exitWait( 1,0 );
      }
      if( !GetModuleFileName((HMODULE)base,mi_a[mi_q-1].path,MAX_PATH) )
      {
        mi_q--;
        continue;
      }

      PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)base;
      PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );
      PIMAGE_OPTIONAL_HEADER ioh = (PIMAGE_OPTIONAL_HEADER)REL_PTR(
          inh,sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER) );
      if( ioh->SizeOfImage>size ) size = ioh->SizeOfImage;

      mi_a[mi_q-1].base = base;
      mi_a[mi_q-1].size = size;
    }
  }

  *p_mi_q = mi_q;
  *p_mi_a = mi_a;
}

static void writeModsSend( modInfo *mi_a,int mi_q )
{
  GET_REMOTEDATA( rd );

  int type = WRITE_MODS;
  DWORD written;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,&mi_q,sizeof(int),&written,NULL );
  if( mi_q )
    WriteFile( rd->master,mi_a,mi_q*sizeof(modInfo),&written,NULL );
}

static void writeMods( allocation *alloc_a,int alloc_q )
{
  GET_REMOTEDATA( rd );

  int mi_q = 0;
  modInfo *mi_a = NULL;
  writeModsFind( alloc_a,alloc_q,&mi_a,&mi_q );
  writeModsSend( mi_a,mi_q );
  if( mi_a )
    HeapFree( rd->heap,0,mi_a );
}

// }}}
// low-level function for memory allocation tracking {{{

static NOINLINE size_t heap_block_size( HANDLE heap,void *ptr )
{
  PROCESS_HEAP_ENTRY phe;
  phe.lpData = NULL;
  size_t s = -1;

  HeapLock( heap );
  while( HeapWalk(heap,&phe) )
  {
    if( !(phe.wFlags&PROCESS_HEAP_ENTRY_BUSY) || ptr!=phe.lpData )
      continue;

    s = phe.cbData;
    break;
  }
  HeapUnlock( heap );

  return( s );
}

static NOINLINE void trackAllocs(
    void *free_ptr,void *alloc_ptr,size_t alloc_size,allocType at,funcType ft,
    void *caller )
{
  GET_REMOTEDATA( rd );

  if( free_ptr )
  {
    int splitIdx = (((uintptr_t)free_ptr)>>rd->ptrShift)&SPLIT_MASK;
    splitAllocation *sa = rd->splits + splitIdx;

    EnterCriticalSection( &sa->cs );

    int i;
    for( i=sa->alloc_q-1; i>=0 && sa->alloc_a[i].ptr!=free_ptr; i-- );
    if( i>=0 )
    {
      allocation a;
      RtlMoveMemory( &a,&sa->alloc_a[i],sizeof(allocation) );
      sa->alloc_q--;
      if( i<sa->alloc_q ) sa->alloc_a[i] = sa->alloc_a[sa->alloc_q];

      LeaveCriticalSection( &sa->cs );

      if( rd->opt.protectFree )
      {
        freed f;
        RtlMoveMemory( &f.a,&a,sizeof(allocation) );
        f.a.ftFreed = ft;

        void **frames = f.frames;
        int ptrs = CaptureStackBackTrace( 2,PTRS,frames,NULL );
        if( ptrs<PTRS )
          RtlZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );

        EnterCriticalSection( &rd->csFreed );

        if( rd->freed_q>=rd->freed_s )
        {
          rd->freed_s += 65536;
          freed *freed_an;
          if( !rd->freed_a )
            freed_an = HeapAlloc(
                rd->heap,0,rd->freed_s*sizeof(freed) );
          else
            freed_an = HeapReAlloc(
                rd->heap,0,rd->freed_a,rd->freed_s*sizeof(freed) );
          if( !freed_an )
          {
            LeaveCriticalSection( &rd->csFreed );
            EnterCriticalSection( &rd->csWrite );

            DWORD written;
            int type = WRITE_MAIN_ALLOC_FAIL;
            WriteFile( rd->master,&type,sizeof(int),&written,NULL );

            LeaveCriticalSection( &rd->csWrite );

            exitWait( 1,0 );
          }
          rd->freed_a = freed_an;
        }

        RtlMoveMemory( rd->freed_a+rd->freed_q,&f,sizeof(freed) );
        rd->freed_q++;

        LeaveCriticalSection( &rd->csFreed );
      }

      if( rd->opt.allocMethod && a.at!=at )
      {
        allocation aa[2];
        RtlMoveMemory( aa,&a,sizeof(allocation) );
        void **frames = aa[1].frames;
        int ptrs = CaptureStackBackTrace( 2,PTRS,frames,NULL );
        if( ptrs<PTRS )
          RtlZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );
        aa[1].ptr = free_ptr;
        aa[1].size = 0;
        aa[1].at = at;
        aa[1].lt = LT_LOST;
        aa[1].ft = ft;

        EnterCriticalSection( &rd->csWrite );

        writeMods( aa,2 );

        int type = WRITE_WRONG_DEALLOC;
        DWORD written;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );
        WriteFile( rd->master,aa,2*sizeof(allocation),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );

        if( rd->opt.raiseException>1 )
          DebugBreak();
      }
    }
    else
    {
      LeaveCriticalSection( &sa->cs );
      EnterCriticalSection( &rd->csFreed );

      int inExit = rd->inExit;

      for( i=rd->freed_q-1; i>=0 && rd->freed_a[i].a.ptr!=free_ptr; i-- );
      if( i>=0 )
      {
        allocation aa[3];
        RtlMoveMemory( &aa[1],&rd->freed_a[i].a,sizeof(allocation) );
        RtlMoveMemory( aa[2].frames,rd->freed_a[i].frames,PTRS*sizeof(void*) );

        LeaveCriticalSection( &rd->csFreed );

        aa[2].ft = aa[1].ftFreed;
        void **frames = aa[0].frames;
        int ptrs = CaptureStackBackTrace( 2,PTRS,frames,NULL );
        if( ptrs<PTRS )
          RtlZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );
        aa[0].ft = ft;

        EnterCriticalSection( &rd->csWrite );

        writeMods( aa,3 );

        int type = WRITE_DOUBLE_FREE;
        DWORD written;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );
        WriteFile( rd->master,aa,3*sizeof(allocation),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );

        if( rd->opt.raiseException )
          DebugBreak();
      }
      else
        LeaveCriticalSection( &rd->csFreed );

      if( i>=0 );
      else if( rd->crtHeap &&
          heap_block_size(rd->crtHeap,free_ptr)!=(size_t)-1 )
      {
        if( at==AT_MALLOC )
          rd->ofree( free_ptr );
        else if( ft==FT_OP_DELETE )
          rd->oop_delete( free_ptr );
        else if( ft==FT_OP_DELETE_A )
          rd->oop_delete_a( free_ptr );
      }
      else if( !inExit )
      {
        allocation a;
        a.ptr = free_ptr;
        a.size = 0;
        a.at = at;
        a.lt = LT_LOST;
        a.ft = ft;

        void **frames = a.frames;
        int ptrs = CaptureStackBackTrace( 2,PTRS,frames,NULL );
        if( ptrs<PTRS )
          RtlZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );

        EnterCriticalSection( &rd->csWrite );

        writeMods( &a,1 );

        DWORD written;
        int type = WRITE_FREE_FAIL;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );
        WriteFile( rd->master,&a,sizeof(allocation),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );

        if( rd->opt.raiseException )
          DebugBreak();
      }
    }
  }

  if( alloc_ptr )
  {
    uintptr_t align = rd->opt.align;
    alloc_size += ( align - (alloc_size%align) )%align;

    allocation a;
    a.ptr = alloc_ptr;
    a.size = alloc_size;
    a.at = at;
    a.lt = LT_LOST;
    a.ft = ft;

    void **frames = a.frames;
    int ptrs = CaptureStackBackTrace( 2,PTRS,frames,NULL );
    if( !ptrs ) frames[ptrs++] = caller;
    if( ptrs<PTRS )
      RtlZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );

    EnterCriticalSection( &rd->csWrite );

    int id = a.id = ++rd->cur_id;

    int raiseException = 0;
    if( id==rd->raise_id && id )
    {
      DWORD written;
      int type = WRITE_RAISE_ALLOCATION;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );
      WriteFile( rd->master,&id,sizeof(int),&written,NULL );
      WriteFile( rd->master,&ft,sizeof(funcType),&written,NULL );

      rd->raise_id = rd->raise_alloc_q-- ? *(rd->raise_alloc_a++) : 0;

      raiseException = 1;
    }

    LeaveCriticalSection( &rd->csWrite );

    int splitIdx = (((uintptr_t)alloc_ptr)>>rd->ptrShift)&SPLIT_MASK;
    splitAllocation *sa = rd->splits + splitIdx;

    EnterCriticalSection( &sa->cs );

    if( sa->alloc_q>=sa->alloc_s )
    {
      sa->alloc_s += 64;
      allocation *alloc_an;
      if( !sa->alloc_a )
        alloc_an = HeapAlloc(
            rd->heap,0,sa->alloc_s*sizeof(allocation) );
      else
        alloc_an = HeapReAlloc(
            rd->heap,0,sa->alloc_a,sa->alloc_s*sizeof(allocation) );
      if( !alloc_an )
      {
        LeaveCriticalSection( &sa->cs );
        EnterCriticalSection( &rd->csWrite );

        DWORD written;
        int type = WRITE_MAIN_ALLOC_FAIL;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );

        exitWait( 1,0 );
      }
      sa->alloc_a = alloc_an;
    }
    RtlMoveMemory( sa->alloc_a+sa->alloc_q,&a,sizeof(allocation) );
    sa->alloc_q++;

    LeaveCriticalSection( &sa->cs );

    if( raiseException )
      DebugBreak();
  }
  else if( alloc_size!=(size_t)-1 )
  {
    allocation a;
    a.ptr = NULL;
    a.size = alloc_size;
    a.at = at;
    a.lt = LT_LOST;
    a.ft = ft;

    void **frames = a.frames;
    int ptrs = CaptureStackBackTrace( 2,PTRS,frames,NULL );
    if( ptrs<PTRS )
      RtlZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );

    EnterCriticalSection( &rd->csWrite );

    int id = a.id = ++rd->cur_id;

    writeMods( &a,1 );

    DWORD written;
    int type = WRITE_ALLOC_FAIL;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    WriteFile( rd->master,&a,sizeof(allocation),&written,NULL );

    int raiseException = rd->opt.raiseException;
    if( id==rd->raise_id && id )
    {
      type = WRITE_RAISE_ALLOCATION;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );
      WriteFile( rd->master,&id,sizeof(int),&written,NULL );
      WriteFile( rd->master,&ft,sizeof(funcType),&written,NULL );

      rd->raise_id = rd->raise_alloc_q-- ? *(rd->raise_alloc_a++) : 0;

      raiseException = 1;
    }

    LeaveCriticalSection( &rd->csWrite );

    if( raiseException )
      DebugBreak();
  }
}
#ifndef _MSC_VER
#define RETURN_ADDRESS() __builtin_return_address(0)
#else
#define RETURN_ADDRESS() _ReturnAddress()
#endif
#define trackAllocs(f,a,s,at,ft) trackAllocs(f,a,s,at,ft,RETURN_ADDRESS())

// }}}
// replacements for memory allocation tracking {{{

static void addModule( HMODULE mod );
static void replaceModFuncs( void );

static void *new_malloc( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fmalloc( s );

  trackAllocs( NULL,b,s,AT_MALLOC,FT_MALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_malloc\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void *new_calloc( size_t n,size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fcalloc( n,s );

  trackAllocs( NULL,b,n*s,AT_MALLOC,FT_CALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_calloc\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void new_free( void *b )
{
  GET_REMOTEDATA( rd );
  rd->ffree( b );

  trackAllocs( b,NULL,-1,AT_MALLOC,FT_FREE );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_free\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif
}

static void *new_realloc( void *b,size_t s )
{
  GET_REMOTEDATA( rd );
  void *nb = rd->frealloc( b,s );

  trackAllocs( b,nb,s,AT_MALLOC,FT_REALLOC );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_realloc\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( nb );
}

static char *new_strdup( const char *s )
{
  GET_REMOTEDATA( rd );
  char *b = rd->fstrdup( s );

  trackAllocs( NULL,b,lstrlen(s)+1,AT_MALLOC,FT_STRDUP );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_strdup\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static wchar_t *new_wcsdup( const wchar_t *s )
{
  GET_REMOTEDATA( rd );
  wchar_t *b = rd->fwcsdup( s );

  trackAllocs( NULL,b,(lstrlenW(s)+1)*2,AT_MALLOC,FT_WCSDUP );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wcsdup\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void *new_op_new( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fop_new( s );

  trackAllocs( NULL,b,s,AT_NEW,FT_OP_NEW );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_new\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void new_op_delete( void *b )
{
  GET_REMOTEDATA( rd );
  rd->fop_delete( b );

  trackAllocs( b,NULL,-1,AT_NEW,FT_OP_DELETE );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_delete\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif
}

static void *new_op_new_a( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fop_new_a( s );

  trackAllocs( NULL,b,s,rd->newArrAllocMethod,FT_OP_NEW_A );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_new_a\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( b );
}

static void new_op_delete_a( void *b )
{
  GET_REMOTEDATA( rd );
  rd->fop_delete_a( b );

  trackAllocs( b,NULL,-1,rd->newArrAllocMethod,FT_OP_DELETE_A );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_op_delete_a\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif
}

static char *new_getcwd( char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->fgetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  trackAllocs( NULL,cwd,l,AT_MALLOC,FT_GETCWD );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_getcwd\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( cwd );
}

static wchar_t *new_wgetcwd( wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->fwgetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  trackAllocs( NULL,cwd,l*2,AT_MALLOC,FT_WGETCWD );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wgetcwd\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( cwd );
}

static char *new_getdcwd( int drive,char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->fgetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  trackAllocs( NULL,cwd,l,AT_MALLOC,FT_GETDCWD );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_getdcwd\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( cwd );
}

static wchar_t *new_wgetdcwd( int drive,wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->fwgetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  trackAllocs( NULL,cwd,l*2,AT_MALLOC,FT_WGETDCWD );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wgetdcwd\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( cwd );
}

static char *new_fullpath( char *absPath,const char *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  char *fp = rd->ffullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  size_t l = lstrlen( fp ) + 1;
  trackAllocs( NULL,fp,l,AT_MALLOC,FT_FULLPATH );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_fullpath\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( fp );
}

static wchar_t *new_wfullpath( wchar_t *absPath,const wchar_t *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  wchar_t *fp = rd->fwfullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  size_t l = lstrlenW( fp ) + 1;
  trackAllocs( NULL,fp,l*2,AT_MALLOC,FT_WFULLPATH );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wfullpath\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( fp );
}

static char *new_tempnam( char *dir,char *prefix )
{
  GET_REMOTEDATA( rd );
  char *tn = rd->ftempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = lstrlen( tn ) + 1;
  trackAllocs( NULL,tn,l,AT_MALLOC,FT_TEMPNAM );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_tempnam\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( tn );
}

static wchar_t *new_wtempnam( wchar_t *dir,wchar_t *prefix )
{
  GET_REMOTEDATA( rd );
  wchar_t *tn = rd->fwtempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = lstrlenW( tn ) + 1;
  trackAllocs( NULL,tn,l*2,AT_MALLOC,FT_WTEMPNAM );

#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_wtempnam\n";
  DWORD written;
  int type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  return( tn );
}

// }}}
// low-level functions for leak type detection {{{

static void addModMem( PBYTE start,PBYTE end )
{
  uintptr_t startPtr = (uintptr_t)start;
  if( startPtr%sizeof(uintptr_t) )
    startPtr += sizeof(uintptr_t) - startPtr%sizeof(uintptr_t);
  uintptr_t endPtr = (uintptr_t)end;
  if( endPtr%sizeof(uintptr_t) )
    endPtr -= endPtr%sizeof(uintptr_t);
  if( endPtr<=startPtr ) return;

  GET_REMOTEDATA( rd );

  if( rd->mod_mem_q>=rd->mod_mem_s )
  {
    rd->mod_mem_s += 64;
    modMemType *mod_mem_an;
    if( !rd->mod_mem_a )
      mod_mem_an = HeapAlloc(
          rd->heap,0,rd->mod_mem_s*sizeof(modMemType) );
    else
      mod_mem_an = HeapReAlloc(
          rd->heap,0,rd->mod_mem_a,rd->mod_mem_s*sizeof(modMemType) );
    if( !mod_mem_an )
    {
      DWORD written;
      int type = WRITE_MAIN_ALLOC_FAIL;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );
      exitWait( 1,0 );
    }
    rd->mod_mem_a = mod_mem_an;
  }

  modMemType *mod_mem = &rd->mod_mem_a[rd->mod_mem_q++];
  mod_mem->start = (const void**)startPtr;
  mod_mem->end = (const void**)endPtr;
}

static void findLeakType( leakType lt )
{
  GET_REMOTEDATA( rd );

  modMemType *mod_mem_a = rd->mod_mem_a;
  int mod_mem_q = rd->mod_mem_q;
  rd->mod_mem_a = NULL;
  rd->mod_mem_q = rd->mod_mem_s = 0;
  if( !mod_mem_a ) return;

  int i,j,k;
  int compareExact = rd->opt.leakDetails<4;
  int ltUse = lt==LT_INDIRECTLY_LOST ? LT_JOINTLY_LOST : LT_LOST;
  for( i=0; i<=SPLIT_MASK; i++ )
  {
    splitAllocation *sa = rd->splits + i;
    int alloc_q = sa->alloc_q;
    allocation *alloc_a = sa->alloc_a;
    for( j=0; j<alloc_q; j++ )
    {
      allocation *a = alloc_a + j;
      if( a->lt!=ltUse ) continue;
      uintptr_t ptr = (uintptr_t)a->ptr;
      uintptr_t ptrEnd = ptr + a->size;
      for( k=0; k<mod_mem_q; k++ )
      {
        modMemType *mod_mem = mod_mem_a + k;
        const void **start = mod_mem->start;
        if( (uintptr_t)start==ptr ) continue;
        const void **end = mod_mem->end;
        for( ; start<end; start++ )
        {
          uintptr_t memPtr = (uintptr_t)*start;
          if( compareExact )
          {
            if( memPtr!=ptr ) continue;
          }
          else
          {
            if( memPtr<ptr || memPtr>=ptrEnd ) continue;
          }
          a->lt = lt;
          if( lt!=LT_JOINTLY_LOST )
          {
            PBYTE memStart = a->ptr;
            addModMem( memStart,memStart+a->size );
          }
          break;
        }
        if( start<end ) break;
      }
    }
  }
  HeapFree( rd->heap,0,mod_mem_a );
}

// }}}
// replacements for ExitProcess/TerminateProcess {{{

static VOID WINAPI new_ExitProcess( UINT c )
{
  GET_REMOTEDATA( rd );

  int type,i;
  DWORD written;
#if WRITE_DEBUG_STRINGS
  char t[] = "called: new_ExitProcess\n";
  type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  EnterCriticalSection( &rd->csWrite );
  for( i=0; i<=SPLIT_MASK; i++ )
    EnterCriticalSection( &rd->splits[i].cs );

  if( rd->opt.leakDetails>1 )
  {
    findLeakType( LT_REACHABLE );
    while( rd->mod_mem_a )
      findLeakType( LT_INDIRECTLY_REACHABLE );

    int k;
    for( k=0; k<2; k++ )
    {
      int j;
      for( i=0; i<=SPLIT_MASK; i++ )
      {
        splitAllocation *sa = rd->splits + i;
        int alloc_q = sa->alloc_q;
        allocation *alloc_a = sa->alloc_a;
        for( j=0; j<alloc_q; j++ )
        {
          allocation *a = alloc_a + j;
          if( a->lt!=LT_LOST ) continue;
          PBYTE memStart = a->ptr;
          addModMem( memStart,memStart+a->size );
        }
      }
      leakType lt = k ? LT_INDIRECTLY_LOST : LT_JOINTLY_LOST;
      while( rd->mod_mem_a )
        findLeakType( lt );
    }
  }

  if( rd->opt.exitTrace )
    trackAllocs( NULL,(void*)-1,0,AT_EXIT,FT_COUNT );

  int mi_q = 0;
  modInfo *mi_a = NULL;
  for( i=0; i<=SPLIT_MASK; i++ )
  {
    splitAllocation *sa = rd->splits + i;
    writeModsFind( sa->alloc_a,sa->alloc_q,&mi_a,&mi_q );
  }

  if( rd->freed_mod_q )
  {
    LeaveCriticalSection( &rd->csWrite );
    for( i=0; i<=SPLIT_MASK; i++ )
      LeaveCriticalSection( &rd->splits[i].cs );

    EnterCriticalSection( &rd->csFreed );
    rd->inExit = 1;
    LeaveCriticalSection( &rd->csFreed );

    for( i=0; i<rd->freed_mod_q; i++ )
      rd->fFreeLibrary( rd->freed_mod_a[i] );

    EnterCriticalSection( &rd->csWrite );
    for( i=0; i<=SPLIT_MASK; i++ )
      EnterCriticalSection( &rd->splits[i].cs );
  }

  writeModsSend( mi_a,mi_q );
  if( mi_a )
    HeapFree( rd->heap,0,mi_a );

  // make sure exit trace is still the last {{{
  int alloc_q;
  if( rd->freed_mod_q && rd->opt.exitTrace )
  {
    splitAllocation *sa = rd->splits + SPLIT_MASK;
    alloc_q = sa->alloc_q;
    allocation *alloc_a = sa->alloc_a;
    if( alloc_q>1 && alloc_a[alloc_q-1].at!=AT_EXIT )
    {
      for( i=alloc_q-2; i>=0; i-- )
      {
        if( alloc_a[i].at!=AT_EXIT ) continue;

        allocation exitTrace = alloc_a[i];
        alloc_a[i] = alloc_a[alloc_q-1];
        alloc_a[alloc_q-1] = exitTrace;
        break;
      }
    }
  }
  // }}}

  alloc_q = 0;
  for( i=0; i<=SPLIT_MASK; i++ )
    alloc_q += rd->splits[i].alloc_q;
  type = WRITE_LEAKS;
  int terminated = 0;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,&c,sizeof(UINT),&written,NULL );
  WriteFile( rd->master,&terminated,sizeof(int),&written,NULL );
  WriteFile( rd->master,&alloc_q,sizeof(int),&written,NULL );
  int alloc_sum = 0;
  size_t alloc_mem_sum = 0;
  size_t leakContents = rd->opt.leakContents;
  int lDetails = rd->opt.leakDetails ?
    ( rd->opt.leakDetails&1 ? LT_COUNT : LT_REACHABLE ) : 0;
  for( i=0; i<=SPLIT_MASK; i++ )
  {
    splitAllocation *sa = rd->splits + i;
    if( !sa->alloc_q ) continue;
    WriteFile( rd->master,sa->alloc_a,sa->alloc_q*sizeof(allocation),
        &written,NULL );

    if( leakContents )
    {
      alloc_q = sa->alloc_q;
      if( sa->alloc_a[alloc_q-1].at==AT_EXIT ) alloc_q--;
      alloc_sum += alloc_q;
      int j;
      for( j=0; j<alloc_q; j++ )
      {
        allocation *a = sa->alloc_a + j;
        if( a->lt>=lDetails ) continue;
        size_t s = a->size;
        alloc_mem_sum += s<leakContents ? s : leakContents;
      }
    }
  }

  if( alloc_mem_sum )
  {
    type = WRITE_LEAK_CONTENTS;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    WriteFile( rd->master,&alloc_sum,sizeof(int),&written,NULL );
    WriteFile( rd->master,&alloc_mem_sum,sizeof(size_t),&written,NULL );
    for( i=0; i<=SPLIT_MASK; i++ )
    {
      splitAllocation *sa = rd->splits + i;
      alloc_q = sa->alloc_q;
      if( alloc_q>0 && sa->alloc_a[alloc_q-1].at==AT_EXIT ) alloc_q--;
      int j;
      for( j=0; j<alloc_q; j++ )
      {
        allocation *a = sa->alloc_a + j;
        if( a->lt>=lDetails ) continue;
        size_t s = a->size;
        if( leakContents<s ) s = leakContents;
        if( s )
          WriteFile( rd->master,a->ptr,(DWORD)s,&written,NULL );
      }
    }
  }

  LeaveCriticalSection( &rd->csWrite );
  for( i=0; i<=SPLIT_MASK; i++ )
    LeaveCriticalSection( &rd->splits[i].cs );

  exitWait( c,0 );
}

static BOOL WINAPI new_TerminateProcess( HANDLE p,UINT c )
{
  GET_REMOTEDATA( rd );

  if( p==GetCurrentProcess() )
  {
    EnterCriticalSection( &rd->csWrite );

    DWORD written;
    int type = WRITE_LEAKS;
    int terminated = 1;
    int alloc_q = 0;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    WriteFile( rd->master,&c,sizeof(UINT),&written,NULL );
    WriteFile( rd->master,&terminated,sizeof(int),&written,NULL );
    WriteFile( rd->master,&alloc_q,sizeof(int),&written,NULL );

    LeaveCriticalSection( &rd->csWrite );

    exitWait( c,1 );
  }

  return( rd->fTerminateProcess(p,c) );
}

// }}}
// replacements for LoadLibrary/FreeLibrary {{{

static HMODULE WINAPI new_LoadLibraryA( LPCSTR name )
{
  GET_REMOTEDATA( rd );

#if WRITE_DEBUG_STRINGS
  int type;
  DWORD written;
  char t[] = "called: new_LoadLibraryA\n";
  type = WRITE_STRING;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  HMODULE mod = rd->fLoadLibraryA( name );

  EnterCriticalSection( &rd->csWrite );
  addModule( mod );
  replaceModFuncs();
  LeaveCriticalSection( &rd->csWrite );

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
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,t,sizeof(t)-1,&written,NULL );
#endif

  HMODULE mod = rd->fLoadLibraryW( name );

  EnterCriticalSection( &rd->csWrite );
  addModule( mod );
  replaceModFuncs();
  LeaveCriticalSection( &rd->csWrite );

  return( mod );
}

static BOOL WINAPI new_FreeLibrary( HMODULE mod )
{
  GET_REMOTEDATA( rd );

  if( rd->opt.dlls<=2 ) return( TRUE );

  EnterCriticalSection( &rd->csFreed );

  if( rd->inExit )
  {
    LeaveCriticalSection( &rd->csFreed );
    return( TRUE );
  }

  if( rd->freed_mod_q>=rd->freed_mod_s )
  {
    rd->freed_mod_s += 64;
    HMODULE *freed_mod_an;
    if( !rd->freed_mod_a )
      freed_mod_an = HeapAlloc(
          rd->heap,0,rd->freed_mod_s*sizeof(HMODULE) );
    else
      freed_mod_an = HeapReAlloc(
          rd->heap,0,rd->freed_mod_a,rd->freed_mod_s*sizeof(HMODULE) );
    if( !freed_mod_an )
    {
      LeaveCriticalSection( &rd->csFreed );
      EnterCriticalSection( &rd->csWrite );

      DWORD written;
      int type = WRITE_MAIN_ALLOC_FAIL;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );

      LeaveCriticalSection( &rd->csWrite );

      exitWait( 1,0 );
    }
    rd->freed_mod_a = freed_mod_an;
  }
  rd->freed_mod_a[rd->freed_mod_q++] = mod;

  LeaveCriticalSection( &rd->csFreed );

  return( TRUE );
}

// }}}
// low-level functions for page protection {{{

static size_t alloc_size( void *p )
{
  GET_REMOTEDATA( rd );

  int splitIdx = (((uintptr_t)p)>>rd->ptrShift)&SPLIT_MASK;
  splitAllocation *sa = rd->splits + splitIdx;

  EnterCriticalSection( &sa->cs );

  int i;
  for( i=sa->alloc_q-1; i>=0 && sa->alloc_a[i].ptr!=p; i-- );
  size_t s = i>=0 ? sa->alloc_a[i].size : (size_t)-1;

  LeaveCriticalSection( &sa->cs );

  return( s );
}

static void *protect_alloc_m( size_t s )
{
  GET_REMOTEDATA( rd );

  uintptr_t align = rd->opt.align;
  s += ( align - (s%align) )%align;

  DWORD pageSize = rd->pageSize;
  size_t pages = ( s ? (s-1)/pageSize + 1 : 0 ) + rd->pageAdd;

  unsigned char *b = (unsigned char*)VirtualAlloc(
      NULL,pages*pageSize,MEM_RESERVE,PAGE_NOACCESS );
  if( !b )
    return( NULL );

  size_t slackSize = ( pageSize - (s%pageSize) )%pageSize;

  if( rd->opt.protect>1 )
    b += pageSize*rd->pageAdd;

  if( pages>rd->pageAdd )
    VirtualAlloc( b,(pages-rd->pageAdd)*pageSize,MEM_COMMIT,PAGE_READWRITE );

  if( slackSize && rd->opt.slackInit )
  {
    unsigned char *slackStart = b;
    if( rd->opt.protect>1 ) slackStart += s;
    RtlFillMemory( slackStart,slackSize,(UCHAR)rd->opt.slackInit );
  }

  if( rd->opt.protect==1 )
    b += slackSize;

  return( b );
}

static NOINLINE void protect_free_m( void *b,funcType ft )
{
  if( !b ) return;

  size_t s = alloc_size( b );
  if( s==(size_t)-1 ) return;

  GET_REMOTEDATA( rd );

  DWORD pageSize = rd->pageSize;
  size_t pages = ( s ? (s-1)/pageSize + 1 : 0 ) + rd->pageAdd;

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
    p -= pageSize*rd->pageAdd;
  }

  if( slackSize )
  {
    size_t i;
    for( i=0; i<slackSize && slackStart[i]==rd->opt.slackInit; i++ );
    if( i<slackSize )
    {
      int splitIdx = (((uintptr_t)b)>>rd->ptrShift)&SPLIT_MASK;
      splitAllocation *sa = rd->splits + splitIdx;

      EnterCriticalSection( &sa->cs );

      int j;
      for( j=sa->alloc_q-1; j>=0 && sa->alloc_a[j].ptr!=b; j-- );
      if( j>=0 )
      {
        allocation aa[2];
        RtlMoveMemory( aa,sa->alloc_a+j,sizeof(allocation) );

        LeaveCriticalSection( &sa->cs );

        void **frames = aa[1].frames;
        int ptrs = CaptureStackBackTrace( 3,PTRS,frames,NULL );
        if( ptrs<PTRS )
          RtlZeroMemory( frames+ptrs,(PTRS-ptrs)*sizeof(void*) );
        aa[1].ptr = slackStart + i;
        aa[1].ft = ft;

        EnterCriticalSection( &rd->csWrite );

        writeMods( aa,2 );

        int type = WRITE_SLACK;
        DWORD written;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );
        WriteFile( rd->master,aa,2*sizeof(allocation),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );
      }
      else
        LeaveCriticalSection( &sa->cs );
    }
  }

  b = (void*)p;

  VirtualFree( b,pages*pageSize,MEM_DECOMMIT );

  if( !rd->opt.protectFree )
    VirtualFree( b,0,MEM_RELEASE );
}

// }}}
// replacements for page protection {{{

static void *protect_malloc( size_t s )
{
  GET_REMOTEDATA( rd );

  void *b = protect_alloc_m( s );
  if( !b ) return( NULL );

  if( s )
  {
    uint64_t init = rd->opt.init;
    if( init )
    {
      uintptr_t align = rd->opt.align;
      s += ( align - (s%align) )%align;
      size_t count = s>>3;
      ASSUME( count>0 );
      uint64_t *u64 = ASSUME_ALIGNED( b,MEMORY_ALLOCATION_ALIGNMENT );
      size_t i;
      for( i=0; i<count; i++ )
        u64[i] = init;
    }
  }

  return( b );
}

static void *protect_calloc( size_t n,size_t s )
{
#ifndef _MSC_VER
#if defined(__GNUC__) && __GNUC__>=5
  size_t res;
  if( __builtin_mul_overflow(n,s,&res) )
    return( NULL );
#else
  if( s && n>SIZE_MAX/s )
    return( NULL );
  size_t res = n*s;
#endif
#else
#ifndef _WIN64
  unsigned __int64 res64 = __emulu( n,s );
  if( res64>SIZE_MAX )
    return( NULL );
  size_t res = (size_t)res64;
#else
  size_t res,resHigh;
  res = _umul128( n,s,&resHigh );
  if( resHigh )
    return( NULL );
#endif
#endif

  return( protect_alloc_m(res) );
}

static void protect_free( void *b )
{
  protect_free_m( b,FT_FREE );
}

static void *protect_realloc( void *b,size_t s )
{
  GET_REMOTEDATA( rd );

  if( !s )
  {
    protect_free_m( b,FT_REALLOC );
    return( protect_alloc_m(s) );
  }

  if( !b )
    return( protect_alloc_m(s) );

  size_t os = alloc_size( b );
  int extern_alloc = os==(size_t)-1;
  if( extern_alloc )
  {
    if( !rd->crtHeap )
      return( NULL );

    os = heap_block_size( rd->crtHeap,b );
    if( os==(size_t)-1 )
      return( NULL );
  }

  void *nb = protect_alloc_m( s );
  if( !nb ) return( NULL );

  size_t cs = os<s ? os : s;
  if( cs )
    RtlMoveMemory( nb,b,cs );

  if( s>os )
  {
    uint64_t init = rd->opt.init;
    if( init )
    {
      uintptr_t align = rd->opt.align;
      s += ( align - (s%align) )%align;
      size_t count = ( s-os )>>3;
      ASSUME( count>0 );
      uint64_t *u64 = (uint64_t*)ASSUME_ALIGNED(
          (char*)nb+os,MEMORY_ALLOCATION_ALIGNMENT );
      size_t i;
      for( i=0; i<count; i++ )
        u64[i] = init;
    }
  }

  if( !extern_alloc )
    protect_free_m( b,FT_REALLOC );

  return( nb );
}

static char *protect_strdup( const char *s )
{
  size_t l = lstrlen( s ) + 1;

  char *b = protect_alloc_m( l );
  if( !b ) return( NULL );

  RtlMoveMemory( b,s,l );

  return( b );
}

static wchar_t *protect_wcsdup( const wchar_t *s )
{
  size_t l = lstrlenW( s ) + 1;
  l *= 2;

  wchar_t *b = protect_alloc_m( l );
  if( !b ) return( NULL );

  RtlMoveMemory( b,s,l );

  return( b );
}

static char *protect_getcwd( char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->ogetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;

  char *cwd_copy = protect_alloc_m( l );
  if( cwd_copy )
    RtlMoveMemory( cwd_copy,cwd,l );

  rd->ofree( cwd );

  return( cwd_copy );
}

static wchar_t *protect_wgetcwd( wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->owgetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  l *= 2;

  wchar_t *cwd_copy = protect_alloc_m( l );
  if( cwd_copy )
    RtlMoveMemory( cwd_copy,cwd,l );

  rd->ofree( cwd );

  return( cwd_copy );
}

static char *protect_getdcwd( int drive,char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->ogetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;

  char *cwd_copy = protect_alloc_m( l );
  if( cwd_copy )
    RtlMoveMemory( cwd_copy,cwd,l );

  rd->ofree( cwd );

  return( cwd_copy );
}

static wchar_t *protect_wgetdcwd( int drive,wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->owgetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  l *= 2;

  wchar_t *cwd_copy = protect_alloc_m( l );
  if( cwd_copy )
    RtlMoveMemory( cwd_copy,cwd,l );

  rd->ofree( cwd );

  return( cwd_copy );
}

static char *protect_fullpath( char *absPath,const char *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  char *fp = rd->ofullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  size_t l = lstrlen( fp ) + 1;

  char *fp_copy = protect_alloc_m( l );
  if( fp_copy )
    RtlMoveMemory( fp_copy,fp,l );

  rd->ofree( fp );

  return( fp_copy );
}

static wchar_t *protect_wfullpath( wchar_t *absPath,const wchar_t *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  wchar_t *fp = rd->owfullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  size_t l = lstrlenW( fp ) + 1;
  l *= 2;

  wchar_t *fp_copy = protect_alloc_m( l );
  if( fp_copy )
    RtlMoveMemory( fp_copy,fp,l );

  rd->ofree( fp );

  return( fp_copy );
}

static char *protect_tempnam( char *dir,char *prefix )
{
  GET_REMOTEDATA( rd );
  char *tn = rd->otempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = lstrlen( tn ) + 1;

  char *tn_copy = protect_alloc_m( l );
  if( tn_copy )
    RtlMoveMemory( tn_copy,tn,l );

  rd->ofree( tn );

  return( tn_copy );
}

static wchar_t *protect_wtempnam( wchar_t *dir,wchar_t *prefix )
{
  GET_REMOTEDATA( rd );
  wchar_t *tn = rd->owtempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = lstrlenW( tn ) + 1;
  l *= 2;

  wchar_t *tn_copy = protect_alloc_m( l );
  if( tn_copy )
    RtlMoveMemory( tn_copy,tn,l );

  rd->ofree( tn );

  return( tn_copy );
}

// }}}
// function replacement {{{

typedef struct
{
  const char *funcName;
  void *origFunc;
  void *myFunc;
}
replaceData;

static void addModule( HMODULE mod )
{
  GET_REMOTEDATA( rd );

  if( mod==rd->kernel32 ) return;

  int m;
  for( m=0; m<rd->mod_q && rd->mod_a[m]!=mod; m++ );
  if( m<rd->mod_q ) return;

  if( rd->mod_q>=rd->mod_s )
  {
    rd->mod_s += 64;
    HMODULE *mod_an;
    if( !rd->mod_a )
      mod_an = HeapAlloc(
          rd->heap,0,rd->mod_s*sizeof(HMODULE) );
    else
      mod_an = HeapReAlloc(
          rd->heap,0,rd->mod_a,rd->mod_s*sizeof(HMODULE) );
    if( !mod_an )
    {
      DWORD written;
      int type = WRITE_MAIN_ALLOC_FAIL;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );
      exitWait( 1,0 );
    }
    rd->mod_a = mod_an;
  }

  rd->mod_a[rd->mod_q++] = mod;
}

static HMODULE replaceFuncs( HMODULE app,
    replaceData *rep,unsigned int count )
{
  if( !app ) return( NULL );

  PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)app;
  PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );
  if( IMAGE_NT_SIGNATURE!=inh->Signature )
    return( NULL );

  PIMAGE_DATA_DIRECTORY idd =
    &inh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
  if( !idd->Size ) return( NULL );

  GET_REMOTEDATA( rd );

  PIMAGE_IMPORT_DESCRIPTOR iid =
    (PIMAGE_IMPORT_DESCRIPTOR)REL_PTR( idh,idd->VirtualAddress );

  HMODULE repModule = NULL;
  UINT i;
  for( i=0; iid[i].Characteristics; i++ )
  {
    if( !iid[i].FirstThunk || !iid[i].OriginalFirstThunk )
      break;

    PSTR curModName = (PSTR)REL_PTR( idh,iid[i].Name );
    if( !curModName[0] ) continue;
    HMODULE curModule = GetModuleHandle( curModName );
    if( !curModule ) continue;

    if( rd->opt.dlls )
      addModule( curModule );

    PIMAGE_THUNK_DATA thunk =
      (PIMAGE_THUNK_DATA)REL_PTR( idh,iid[i].FirstThunk );
    PIMAGE_THUNK_DATA originalThunk =
      (PIMAGE_THUNK_DATA)REL_PTR( idh,iid[i].OriginalFirstThunk );

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
        if( lstrcmp((LPCSTR)import->Name,rep[j].funcName) ) continue;
        origFunc = rep[j].origFunc;
        myFunc = rep[j].myFunc;
        break;
      }
      if( !origFunc ) continue;

      repModule = curModule;

      DWORD prot;
      if( !VirtualProtect(thunk,sizeof(size_t),
            PAGE_EXECUTE_READWRITE,&prot) )
        break;

      if( !*origFunc )
        *origFunc = (void*)thunk->u1.Function;
      thunk->u1.Function = (DWORD_PTR)myFunc;

      if( !VirtualProtect(thunk,sizeof(size_t),
            prot,&prot) )
        break;
    }
  }

  return( repModule );
}

static void replaceModFuncs( void )
{
  GET_REMOTEDATA( rd );

  const char *fname_malloc = "malloc";
  const char *fname_calloc = "calloc";
  const char *fname_free = "free";
  const char *fname_realloc = "realloc";
  const char *fname_strdup = "_strdup";
  const char *fname_wcsdup = "_wcsdup";
#ifndef _WIN64
  const char *fname_op_new = "??2@YAPAXI@Z";
  const char *fname_op_delete = "??3@YAXPAX@Z";
  const char *fname_op_new_a = "??_U@YAPAXI@Z";
  const char *fname_op_delete_a = "??_V@YAXPAX@Z";
#else
  const char *fname_op_new = "??2@YAPEAX_K@Z";
  const char *fname_op_delete = "??3@YAXPEAX@Z";
  const char *fname_op_new_a = "??_U@YAPEAX_K@Z";
  const char *fname_op_delete_a = "??_V@YAXPEAX@Z";
#endif
  const char *fname_getcwd = "_getcwd";
  const char *fname_wgetcwd = "_wgetcwd";
  const char *fname_getdcwd = "_getdcwd";
  const char *fname_wgetdcwd = "_wgetdcwd";
  const char *fname_fullpath = "_fullpath";
  const char *fname_wfullpath = "_wfullpath";
  const char *fname_tempnam = "_tempnam";
  const char *fname_wtempnam = "_wtempnam";
  replaceData rep[] = {
    { fname_malloc         ,&rd->fmalloc         ,&new_malloc          },
    { fname_calloc         ,&rd->fcalloc         ,&new_calloc          },
    { fname_free           ,&rd->ffree           ,&new_free            },
    { fname_realloc        ,&rd->frealloc        ,&new_realloc         },
    { fname_strdup         ,&rd->fstrdup         ,&new_strdup          },
    { fname_wcsdup         ,&rd->fwcsdup         ,&new_wcsdup          },
    { fname_op_new         ,&rd->fop_new         ,&new_op_new          },
    { fname_op_delete      ,&rd->fop_delete      ,&new_op_delete       },
    { fname_op_new_a       ,&rd->fop_new_a       ,&new_op_new_a        },
    { fname_op_delete_a    ,&rd->fop_delete_a    ,&new_op_delete_a     },
    { fname_getcwd         ,&rd->fgetcwd         ,&new_getcwd          },
    { fname_wgetcwd        ,&rd->fwgetcwd        ,&new_wgetcwd         },
    { fname_getdcwd        ,&rd->fgetdcwd        ,&new_getdcwd         },
    { fname_wgetdcwd       ,&rd->fwgetdcwd       ,&new_wgetdcwd        },
    { fname_fullpath       ,&rd->ffullpath       ,&new_fullpath        },
    { fname_wfullpath      ,&rd->fwfullpath      ,&new_wfullpath       },
    { fname_tempnam        ,&rd->ftempnam        ,&new_tempnam         },
    { fname_wtempnam       ,&rd->fwtempnam       ,&new_wtempnam        },
  };

  const char *fname_ExitProcess = "ExitProcess";
  const char *fname_TerminateProcess = "TerminateProcess";
  replaceData rep2[] = {
    { fname_ExitProcess      ,&rd->fExitProcess      ,&new_ExitProcess      },
    { fname_TerminateProcess ,&rd->fTerminateProcess ,&new_TerminateProcess },
  };
  unsigned int rep2count = sizeof(rep2)/sizeof(replaceData);

  const char *fname_LoadLibraryA = "LoadLibraryA";
  const char *fname_LoadLibraryW = "LoadLibraryW";
  const char *fname_FreeLibrary = "FreeLibrary";
  replaceData repLL[] = {
    { fname_LoadLibraryA   ,&rd->fLoadLibraryA   ,&new_LoadLibraryA    },
    { fname_LoadLibraryW   ,&rd->fLoadLibraryW   ,&new_LoadLibraryW    },
    { fname_FreeLibrary    ,&rd->fFreeLibrary    ,&new_FreeLibrary     },
  };

  HMODULE dll_ucrtbase = GetModuleHandle( "ucrtbase.dll" );
  if( dll_ucrtbase && !rd->opt.protect )
  {
    unsigned int i;
    for( i=0; i<sizeof(rep)/sizeof(replaceData); i++ )
    {
      *(void**)rep[i].origFunc =
        rd->fGetProcAddress( dll_ucrtbase,rep[i].funcName );
    }
  }

  for( ; rd->mod_d<rd->mod_q; rd->mod_d++ )
  {
    HMODULE mod = rd->mod_a[rd->mod_d];

    HMODULE dll_msvcrt = rd->opt.handleException>=2 ? NULL :
      replaceFuncs( mod,rep,sizeof(rep)/sizeof(replaceData) );
    if( !rd->mod_d && rd->opt.handleException<2 )
    {
      if( !dll_msvcrt )
      {
        rd->master = NULL;
        return;
      }
      if( dll_ucrtbase ) dll_msvcrt = dll_ucrtbase;
      addModule( dll_msvcrt );

      if( rd->opt.protect )
      {
        rd->ofree = rd->fGetProcAddress( dll_msvcrt,fname_free );
        rd->oop_delete = rd->fGetProcAddress( dll_msvcrt,fname_op_delete );
        rd->oop_delete_a = rd->fGetProcAddress( dll_msvcrt,fname_op_delete_a );
        rd->ogetcwd = rd->fGetProcAddress( dll_msvcrt,fname_getcwd );
        rd->owgetcwd = rd->fGetProcAddress( dll_msvcrt,fname_wgetcwd );
        rd->ogetdcwd = rd->fGetProcAddress( dll_msvcrt,fname_getdcwd );
        rd->owgetdcwd = rd->fGetProcAddress( dll_msvcrt,fname_wgetdcwd );
        rd->ofullpath = rd->fGetProcAddress( dll_msvcrt,fname_fullpath );
        rd->owfullpath = rd->fGetProcAddress( dll_msvcrt,fname_wfullpath );
        rd->otempnam = rd->fGetProcAddress( dll_msvcrt,fname_tempnam );
        rd->owtempnam = rd->fGetProcAddress( dll_msvcrt,fname_wtempnam );

        HANDLE (*fget_heap_handle)( void ) =
          rd->fGetProcAddress( dll_msvcrt,"_get_heap_handle" );
        if( fget_heap_handle )
          rd->crtHeap = fget_heap_handle();
      }
    }

    if( rd->opt.leakDetails>1 && dll_msvcrt )
    {
      PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)mod;
      PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );

      PIMAGE_FILE_HEADER ifh = &inh->FileHeader;
      PIMAGE_SECTION_HEADER ish = (PIMAGE_SECTION_HEADER)REL_PTR( inh,
          sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER)+ifh->SizeOfOptionalHeader );

      int i;
      for( i=0; i<ifh->NumberOfSections; i++ )
      {
        if( (ish[i].Characteristics&
              (IMAGE_SCN_MEM_EXECUTE|IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE))!=
            (IMAGE_SCN_MEM_READ|IMAGE_SCN_MEM_WRITE) )
          continue;

        PBYTE sectionStart = REL_PTR( idh,ish[i].VirtualAddress );
        PBYTE sectionEnd = sectionStart + ish[i].Misc.VirtualSize;

        addModMem( sectionStart,sectionEnd );
      }
    }

    unsigned int i;
    for( i=0; i<rep2count; i++ )
      replaceFuncs( mod,rep2+i,1 );

    if( rd->opt.dlls>1 )
      replaceFuncs( mod,repLL,sizeof(repLL)/sizeof(replaceData) );
  }
}

// }}}
// exported functions for debugger {{{

DLLEXPORT allocation *heob_find_allocation( uintptr_t addr )
{
  GET_REMOTEDATA( rd );

  int i,j;
  splitAllocation *sa;
  for( j=SPLIT_MASK,sa=rd->splits; j>=0; j--,sa++ )
    for( i=sa->alloc_q-1; i>=0; i-- )
    {
      allocation *a = sa->alloc_a + i;

      uintptr_t ptr = (uintptr_t)a->ptr;
      uintptr_t noAccessStart;
      uintptr_t noAccessEnd;
      if( rd->opt.protect==1 )
      {
        noAccessStart = ptr + a->size;
        noAccessEnd = noAccessStart + rd->pageSize*rd->pageAdd;
      }
      else
      {
        noAccessStart = ptr - rd->pageSize*rd->pageAdd;
        noAccessEnd = ptr;
      }

      if( addr>=noAccessStart && addr<noAccessEnd )
        return( a );
    }

  return( NULL );
}

DLLEXPORT freed *heob_find_freed( uintptr_t addr )
{
  GET_REMOTEDATA( rd );

  int i;
  for( i=rd->freed_q-1; i>=0; i-- )
  {
    freed *f = rd->freed_a + i;

    uintptr_t ptr = (uintptr_t)f->a.ptr;
    size_t size = f->a.size;
    uintptr_t noAccessStart;
    uintptr_t noAccessEnd;
    if( rd->opt.protect==1 )
    {
      noAccessStart = ptr - ( ptr%rd->pageSize );
      noAccessEnd = ptr + f->a.size + rd->pageSize*rd->pageAdd;
    }
    else
    {
      noAccessStart = ptr - rd->pageSize*rd->pageAdd;
      noAccessEnd = ptr + ( size?(size-1)/rd->pageSize+1:0 )*rd->pageSize;
    }

    if( addr>=noAccessStart && addr<noAccessEnd )
      return( f );
  }

  return( NULL );
}

DLLEXPORT allocation *heob_find_nearest_allocation( uintptr_t addr )
{
  GET_REMOTEDATA( rd );

  int i,j;
  splitAllocation *sa;
  uintptr_t nearestPtr = 0;
  allocation *nearestA = NULL;
  for( j=SPLIT_MASK,sa=rd->splits; j>=0; j--,sa++ )
    for( i=sa->alloc_q-1; i>=0; i-- )
    {
      allocation *a = sa->alloc_a + i;

      uintptr_t ptr = (uintptr_t)a->ptr;

      if( addr>=ptr && (!nearestPtr || ptr>nearestPtr) &&
          addr-ptr<INTPTR_MAX )
      {
        nearestPtr = ptr;
        nearestA = a;
      }
    }

  return( nearestA );
}

DLLEXPORT freed *heob_find_nearest_freed( uintptr_t addr )
{
  GET_REMOTEDATA( rd );

  int i;
  uintptr_t nearestPtr = 0;
  freed *nearestF = NULL;
  for( i=rd->freed_q-1; i>=0; i-- )
  {
    freed *f = rd->freed_a + i;

    uintptr_t ptr = (uintptr_t)f->a.ptr;

    if( addr>=ptr && (!nearestPtr || ptr>nearestPtr) &&
        addr-ptr<INTPTR_MAX )
    {
      nearestPtr = ptr;
      nearestF = f;
    }
  }

  return( nearestF );
}

DLLEXPORT VOID heob_exit( UINT c )
{
  new_ExitProcess( c );
}

// }}}
// exception handler {{{

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
  ei.nearest = 0;

  if( ep->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION &&
      ep->ExceptionRecord->NumberParameters==2 )
  {
    uintptr_t addr = ep->ExceptionRecord->ExceptionInformation[1];

    allocation *a = heob_find_allocation( addr );
    if( a )
    {
      RtlMoveMemory( &ei.aa[1],a,sizeof(allocation) );
      ei.aq++;
    }
    else
    {
      freed *f = heob_find_freed( addr );
      if( f )
      {
        RtlMoveMemory( &ei.aa[1],&f->a,sizeof(allocation) );
        RtlMoveMemory( &ei.aa[2].frames,&f->frames,PTRS*sizeof(void*) );
        ei.aa[2].ft = f->a.ftFreed;
        ei.aq += 2;
      }
      else if( rd->opt.findNearest )
      {
        a = heob_find_nearest_allocation( addr );
        f = heob_find_nearest_freed( addr );
        if( a && (!f || a->ptr>f->a.ptr) )
        {
          RtlMoveMemory( &ei.aa[1],a,sizeof(allocation) );
          ei.aq++;
          ei.nearest = 1;
        }
        else if( f )
        {
          RtlMoveMemory( &ei.aa[1],&f->a,sizeof(allocation) );
          RtlMoveMemory( &ei.aa[2].frames,&f->frames,PTRS*sizeof(void*) );
          ei.aa[2].ft = f->a.ftFreed;
          ei.aq += 2;
          ei.nearest = 2;
        }
      }
    }
  }

  type = WRITE_EXCEPTION;

  int count = 0;
  void **frames = ei.aa[0].frames;

#if USE_STACKWALK
  HMODULE symMod = rd->fLoadLibraryA( "dbghelp.dll" );
  func_SymInitialize *fSymInitialize = NULL;
  func_StackWalk64 *fStackWalk64 = NULL;
  func_SymCleanup *fSymCleanup = NULL;
  if( symMod )
  {
    fSymInitialize = rd->fGetProcAddress( symMod,"SymInitialize" );
    fStackWalk64 = rd->fGetProcAddress( symMod,"StackWalk64" );
    fSymCleanup = rd->fGetProcAddress( symMod,"SymCleanup" );
  }

  if( fSymInitialize && fStackWalk64 && fSymCleanup )
  {
    CONTEXT context;
    RtlMoveMemory( &context,ep->ContextRecord,sizeof(CONTEXT) );

    STACKFRAME64 stack;
    RtlZeroMemory( &stack,sizeof(STACKFRAME64) );
    stack.AddrPC.Offset = context.cip;
    stack.AddrPC.Mode = AddrModeFlat;
    stack.AddrStack.Offset = context.csp;
    stack.AddrStack.Mode = AddrModeFlat;
    stack.AddrFrame.Offset = context.cfp;
    stack.AddrFrame.Mode = AddrModeFlat;

    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();

    fSymInitialize( process,NULL,TRUE );

    PFUNCTION_TABLE_ACCESS_ROUTINE64 fSymFunctionTableAccess64 =
      rd->fGetProcAddress( symMod,"SymFunctionTableAccess64" );
    PGET_MODULE_BASE_ROUTINE64 fSymGetModuleBase64 =
      rd->fGetProcAddress( symMod,"SymGetModuleBase64" );

    while( count<PTRS )
    {
      if( !fStackWalk64(MACH_TYPE,process,thread,&stack,&context,
            NULL,fSymFunctionTableAccess64,fSymGetModuleBase64,NULL) )
        break;

      uintptr_t frame = (uintptr_t)stack.AddrPC.Offset;
      if( !frame ) break;

      if( !count ) frame++;
      frames[count++] = (void*)frame;

      if( count==1 && rd->opt.useSp )
      {
        ULONG_PTR csp = *(ULONG_PTR*)ep->ContextRecord->csp;
        if( csp ) frames[count++] = (void*)csp;
      }
    }

    fSymCleanup( process );
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
      if( IsBadReadPtr(sp,2*sizeof(ULONG_PTR)) || !sp[0] || !sp[1] )
        break;

      ULONG_PTR *np = (ULONG_PTR*)sp[0];
      frames[count++] = (void*)sp[1];

      sp = np;
    }
  }
  if( count<PTRS )
    RtlZeroMemory( frames+count,(PTRS-count)*sizeof(void*) );

#if USE_STACKWALK
  if( symMod )
    rd->fFreeLibrary( symMod );
#endif

  writeMods( ei.aa,ei.aq );

  RtlMoveMemory( &ei.er,ep->ExceptionRecord,sizeof(EXCEPTION_RECORD) );
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,&ei,sizeof(exceptionInfo),&written,NULL );

  if( ep->ExceptionRecord->ExceptionCode==EXCEPTION_BREAKPOINT )
  {
#ifndef _WIN64
    ep->ContextRecord->Eip++;
#else
    ep->ContextRecord->Rip++;
#endif
    return( EXCEPTION_CONTINUE_EXECUTION );
  }

  exitWait( 1,1 );

  return( EXCEPTION_EXECUTE_HANDLER );
}

// }}}
// injected main {{{

void inj( remoteData *rd,HMODULE app )
{
  PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)app;
  PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );

  // base relocation {{{
#ifndef _WIN64
  {
    PIMAGE_OPTIONAL_HEADER ioh = (PIMAGE_OPTIONAL_HEADER)REL_PTR(
        inh,sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER) );
    size_t imageBase = ioh->ImageBase;
    if( imageBase!=(size_t)app )
    {
      size_t baseOfs = (size_t)app - imageBase;

      PIMAGE_DATA_DIRECTORY idd =
        &inh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
      if( idd->Size>0 )
      {
        PIMAGE_BASE_RELOCATION ibr =
          (PIMAGE_BASE_RELOCATION)REL_PTR( idh,idd->VirtualAddress );
        while( ibr->VirtualAddress>0 )
        {
          PBYTE dest = REL_PTR( app,ibr->VirtualAddress );
          UINT16 *relInfo =
            (UINT16*)REL_PTR( ibr,sizeof(IMAGE_BASE_RELOCATION) );
          unsigned int i;
          unsigned int relCount =
            ( ibr->SizeOfBlock-sizeof(IMAGE_BASE_RELOCATION) )/2;
          for( i=0; i<relCount; i++,relInfo++ )
          {
            int type = *relInfo >> 12;
            int offset = *relInfo & 0xfff;

#ifndef _WIN64
            if( type!=IMAGE_REL_BASED_HIGHLOW ) continue;
#else
            if( type!=IMAGE_REL_BASED_DIR64 ) continue;
#endif

            size_t *addr = (size_t*)( dest + offset );

            DWORD prot;
            rd->fVirtualProtect( addr,sizeof(size_t),
                PAGE_EXECUTE_READWRITE,&prot );

            *addr += baseOfs;

            rd->fVirtualProtect( addr,sizeof(size_t),
                prot,&prot );
          }

          ibr = (PIMAGE_BASE_RELOCATION)REL_PTR( ibr,ibr->SizeOfBlock );
        }
      }
    }
  }
#endif
  // }}}

  // import functions {{{
  {
    PIMAGE_DATA_DIRECTORY idd =
      &inh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if( idd->Size>0 )
    {
      PIMAGE_IMPORT_DESCRIPTOR iid =
        (PIMAGE_IMPORT_DESCRIPTOR)REL_PTR( idh,idd->VirtualAddress );
      uintptr_t *thunk;
      if( iid->OriginalFirstThunk )
        thunk = (uintptr_t*)REL_PTR( idh,iid->OriginalFirstThunk );
      else
        thunk = (uintptr_t*)REL_PTR( idh,iid->FirstThunk );
      void **funcPtr = (void**)REL_PTR( idh,iid->FirstThunk );
      for( ; *thunk; thunk++,funcPtr++ )
      {
        LPCSTR funcName;
        if( IMAGE_SNAP_BY_ORDINAL(*thunk) )
          funcName = (LPCSTR)IMAGE_ORDINAL( *thunk );
        else
        {
          PIMAGE_IMPORT_BY_NAME iibn =
            (PIMAGE_IMPORT_BY_NAME)REL_PTR( idh,*thunk );
          funcName = (LPCSTR)&iibn->Name;
        }
        void *func = rd->fGetProcAddress( rd->kernel32,funcName );
        if( !func ) break;

        DWORD prot;
        rd->fVirtualProtect( funcPtr,sizeof(void*),
            PAGE_EXECUTE_READWRITE,&prot );

        *funcPtr = func;

        rd->fVirtualProtect( funcPtr,sizeof(void*),
            prot,&prot );
      }
    }
  }
  // }}}

  rd->fFlushInstructionCache( rd->fGetCurrentProcess(),NULL,0 );

  HANDLE heap = HeapCreate( 0,0,0 );
  localData *ld = HeapAlloc( heap,HEAP_ZERO_MEMORY,
      sizeof(localData)+rd->raise_alloc_q*sizeof(int) );
  g_ld = ld;

  RtlMoveMemory( &ld->opt,&rd->opt,sizeof(options) );
  ld->fLoadLibraryA = rd->fLoadLibraryA;
  ld->fLoadLibraryW = rd->fLoadLibraryW;
  ld->fFreeLibrary = rd->fFreeLibrary;
  ld->fGetProcAddress = rd->fGetProcAddress;
  ld->fExitProcess = rd->fExitProcess;
  ld->master = rd->master;
  ld->kernel32 = rd->kernel32;

  ld->heap = heap;

  SYSTEM_INFO si;
  GetSystemInfo( &si );
  ld->pageSize = si.dwPageSize;
  ld->pageAdd = ( rd->opt.minProtectSize+(ld->pageSize-1) )/ld->pageSize;

  ld->splits = HeapAlloc( heap,HEAP_ZERO_MEMORY,
      (SPLIT_MASK+1)*sizeof(splitAllocation) );

  typedef BOOL WINAPI func_InitializeCriticalSectionEx(
      LPCRITICAL_SECTION,DWORD,DWORD );
  func_InitializeCriticalSectionEx *fInitCritSecEx =
    rd->fGetProcAddress( rd->kernel32,"InitializeCriticalSectionEx" );
  if( fInitCritSecEx )
  {
    fInitCritSecEx( &ld->csWrite,4000,CRITICAL_SECTION_NO_DEBUG_INFO );
    fInitCritSecEx( &ld->csFreed,4000,CRITICAL_SECTION_NO_DEBUG_INFO );
    int i;
    for( i=0; i<=SPLIT_MASK; i++ )
      fInitCritSecEx( &ld->splits[i].cs,4000,CRITICAL_SECTION_NO_DEBUG_INFO );
  }
  else
  {
    InitializeCriticalSection( &ld->csWrite );
    InitializeCriticalSection( &ld->csFreed );
    int i;
    for( i=0; i<=SPLIT_MASK; i++ )
      InitializeCriticalSection( &ld->splits[i].cs );
  }

  ld->ptrShift = 5;
  if( rd->opt.protect )
  {
#ifndef _MSC_VER
    ld->ptrShift = __builtin_ffs( si.dwPageSize ) - 1 + 4;
#else
    DWORD index;
    _BitScanForward( &index,si.dwPageSize );
    ld->ptrShift = index + 4;
#endif
    if( ld->ptrShift<5 ) ld->ptrShift = 5;
  }

  ld->newArrAllocMethod = rd->opt.allocMethod>1 ? AT_NEW_ARR : AT_NEW;

  ld->cur_id = 0;
  ld->raise_alloc_q = rd->raise_alloc_q;
  ld->raise_alloc_a = (int*)( ld + 1 );
  if( rd->raise_alloc_q )
    RtlMoveMemory( ld->raise_alloc_a,
        rd->raise_alloc_a,rd->raise_alloc_q*sizeof(int) );
  ld->raise_id = ld->raise_alloc_q-- ? *(ld->raise_alloc_a++) : 0;

  if( rd->opt.protect )
  {
    ld->fmalloc = &protect_malloc;
    ld->fcalloc = &protect_calloc;
    ld->ffree = &protect_free;
    ld->frealloc = &protect_realloc;
    ld->fstrdup = &protect_strdup;
    ld->fwcsdup = &protect_wcsdup;
    ld->fop_new = &protect_malloc;
    ld->fop_delete = &protect_free;
    ld->fop_new_a = &protect_malloc;
    ld->fop_delete_a = &protect_free;
    ld->fgetcwd = &protect_getcwd;
    ld->fwgetcwd = &protect_wgetcwd;
    ld->fgetdcwd = &protect_getdcwd;
    ld->fwgetdcwd = &protect_wgetdcwd;
    ld->ffullpath = &protect_fullpath;
    ld->fwfullpath = &protect_wfullpath;
    ld->ftempnam = &protect_tempnam;
    ld->fwtempnam = &protect_wtempnam;
  }

  if( rd->opt.handleException )
  {
    rd->fSetUnhandledExceptionFilter( &exceptionWalker );

    void *fp = rd->fSetUnhandledExceptionFilter;
#ifndef _WIN64
    unsigned char doNothing[] = {
      0x31,0xc0,        // xor  %eax,%eax
      0xc2,0x04,0x00    // ret  $0x4
    };
#else
    unsigned char doNothing[] = {
      0x31,0xc0,        // xor  %eax,%eax
      0xc3              // retq
    };
#endif
    DWORD prot;
    VirtualProtect( fp,sizeof(doNothing),PAGE_EXECUTE_READWRITE,&prot );
    RtlMoveMemory( fp,doNothing,sizeof(doNothing) );
    VirtualProtect( fp,sizeof(doNothing),prot,&prot );
    rd->fFlushInstructionCache( rd->fGetCurrentProcess(),NULL,0 );
  }

  addModule( GetModuleHandle(NULL) );
  replaceModFuncs();

  GetModuleFileName( NULL,rd->exePathA,MAX_PATH );
  rd->master = ld->master;

  HANDLE initFinished = rd->initFinished;
  SetEvent( initFinished );
  CloseHandle( initFinished );
  while( 1 ) Sleep( INFINITE );
}

// }}}

// vim:fdm=marker
