
//          Copyright Hannes Domani 2014 - 2018.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

// includes {{{

#include "heob-internal.h"

#include <stdint.h>

// }}}
// defines {{{

#define SPLIT_MASK 0x3fff

#define REL_PTR( base,ofs ) ( ((PBYTE)base)+ofs )

#define CAPTURE_STACK_TRACE( skip,capture,frames,caller,maxFrames ) \
  do { \
    void **frames_ = frames; \
    int ptrs_ = CaptureStackBackTrace( \
        skip,min((maxFrames)-(skip),capture),frames_,NULL ); \
    if( !ptrs_ ) frames[ptrs_++] = caller; \
    if( ptrs_<capture ) RtlZeroMemory( \
        frames_+ptrs_,(capture-ptrs_)*sizeof(void*) ); \
  } while( 0 )

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
#ifndef NO_THREADNAMES
  int threadNameIdx;
#endif
}
freed;

typedef struct
{
  CRITICAL_SECTION cs;
  freed *freed_a;
  int freed_q;
  int freed_s;
}
splitFreed;

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
  func_LoadLibraryExA *fLoadLibraryExA;
  func_LoadLibraryExW *fLoadLibraryExW;
  func_FreeLibrary *fFreeLibrary;
  func_GetProcAddress *fGetProcAddress;
  func_ExitProcess *fExitProcess;
  func_TerminateProcess *fTerminateProcess;
  func_FreeLibraryAndExitThread *fFreeLibraryAET;
  func_CreateProcessA *fCreateProcessA;
  func_CreateProcessW *fCreateProcessW;

#ifndef NO_THREADNAMES
  func_RaiseException *fRaiseException;
  func_NtQueryInformationThread *fNtQueryInformationThread;
#endif

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
  func_free_dbg *ffree_dbg;
  func_recalloc *frecalloc;

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

  int is_cygwin;

  HANDLE controlPipe;
#ifndef NO_DBGENG
  HANDLE exceptionWait;
#endif
  HMODULE heobMod;
  HMODULE kernel32;
  HMODULE msvcrt;
  HMODULE ucrtbase;

  splitAllocation *splits;
  int ptrShift;
  allocType newArrAllocMethod;

  splitFreed *freeds;

  HANDLE heap;
  DWORD pageSize;
  size_t pageAdd;
  HANDLE crtHeap;
  int processors;
  exceptionInfo *ei;
  int maxStackFrames;
  int noCRT;

#ifndef NO_THREADNAMES
  DWORD threadNameTls;
  int threadNameIdx;
#endif

  DWORD freeSizeTls;

  options opt;
  options globalopt;
  char *specificOptions;
  uint64_t slackInit64;

  int recording;

  char subOutName[MAX_PATH];
  char subXmlName[MAX_PATH];
  wchar_t subCurDir[MAX_PATH];

  CRITICAL_SECTION csMod;
  CRITICAL_SECTION csAllocId;
  CRITICAL_SECTION csWrite;
  CRITICAL_SECTION csFreedMod;

  // protected by csMod {{{

  HMODULE *mod_a;
  int mod_q;
  int mod_s;
  int mod_d;

  HMODULE *crt_mod_a;
  int crt_mod_q;
  int crt_mod_s;

  modMemType *mod_mem_a;
  int mod_mem_q;
  int mod_mem_s;

  // }}}
  // protected by csAllocId {{{

  size_t cur_id;
  size_t raise_id;
  size_t *raise_alloc_a;

  // }}}
  // protected by csWrite {{{

  HANDLE master;

#ifndef NO_THREADNAMES
  threadNameInfo *threadName_a;
  int threadName_q;
  int threadName_s;
  int threadName_w;
#endif

  // }}}
  // protected by csFreedMod {{{

  HMODULE *freed_mod_a;
  int freed_mod_q;
  int freed_mod_s;
  int inExit;

  // }}}
}
localData;

static localData g_ld;
#define GET_REMOTEDATA( ld ) localData *ld = &g_ld

// }}}
// process exit {{{

static NORETURN void exitWait( UINT c,int terminate )
{
  GET_REMOTEDATA( rd );

  EnterCriticalSection( &rd->csWrite );

  FlushFileBuffers( rd->master );
  CloseHandle( rd->master );
  rd->master = INVALID_HANDLE_VALUE;

  LeaveCriticalSection( &rd->csWrite );

  rd->opt.raiseException = 0;

  if( terminate<2 && rd->opt.newConsole&1 )
  {
    HANDLE in = GetStdHandle( STD_INPUT_HANDLE );
    if( FlushConsoleInputBuffer(in) )
    {
      HANDLE out = GetStdHandle( STD_OUTPUT_HANDLE );
      DWORD written;
      const char *exitText =
        "\n\n-------------------- APPLICATION EXIT --------------------\n";
      WriteFile( out,exitText,lstrlen(exitText),&written,NULL );

      STARTUPINFOW si;
      RtlZeroMemory( &si,sizeof(STARTUPINFOW) );
      si.cb = sizeof(STARTUPINFOW);
      PROCESS_INFORMATION pi;
      RtlZeroMemory( &pi,sizeof(PROCESS_INFORMATION) );
      wchar_t pause[] = L"cmd /c pause";
      if( rd->fCreateProcessW(NULL,pause,NULL,NULL,FALSE,0,NULL,NULL,&si,&pi) )
      {
        CloseHandle( pi.hProcess );
        CloseHandle( pi.hThread );
      }
      else
      {
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
      size_t size = (size_t)mbi.BaseAddress + mbi.RegionSize - base;

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
      if( UNLIKELY(!mi_a) )
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

#ifndef NO_THREADNAMES
  if( rd->threadName_q>rd->threadName_w )
  {
    type = WRITE_THREAD_NAMES;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    type = rd->threadName_q - rd->threadName_w;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    WriteFile( rd->master,rd->threadName_a+rd->threadName_w,
        type*sizeof(threadNameInfo),&written,NULL );
    rd->threadName_w = rd->threadName_q;
  }
#endif
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
// memory allocation tracking {{{

static NOINLINE int allocSizeAndState( void *p,size_t *s,int enable )
{
  GET_REMOTEDATA( rd );

  int splitIdx = (((uintptr_t)p)>>rd->ptrShift)&SPLIT_MASK;
  splitAllocation *sa = rd->splits + splitIdx;
  int prevEnable = 0;
  size_t freeSize = -1;

  EnterCriticalSection( &sa->cs );

  int i;
  for( i=sa->alloc_q-1; i>=0; i-- )
  {
    allocation *a = sa->alloc_a + i;
    if( a->ptr!=p ) continue;
    if( !a->frameCount ) break;

    prevEnable = 1;
    a->frameCount = enable;
    freeSize = a->size;
    break;
  }

  LeaveCriticalSection( &sa->cs );

  *s = freeSize;

  return( prevEnable );
}

static NOINLINE size_t heap_block_size( HANDLE heap,void *ptr )
{
  PROCESS_HEAP_ENTRY phe;
  phe.lpData = NULL;
  size_t s = -1;
  char *p = ptr;

  HeapLock( heap );
  while( HeapWalk(heap,&phe) )
  {
    if( !(phe.wFlags&PROCESS_HEAP_ENTRY_BUSY) ||
        p<(char*)phe.lpData || p>=(char*)phe.lpData+phe.cbData )
      continue;

    if( p==(char*)phe.lpData )
      s = phe.cbData;
    else
    {
      s = (char*)phe.lpData + phe.cbData - p;
      if( s>4 ) s -= 4;
    }

    uintptr_t align = MEMORY_ALLOCATION_ALIGNMENT;
    s += ( align - (s%align) )%align;
    break;
  }
  HeapUnlock( heap );

  return( s );
}

static NOINLINE void trackFree(
    void *free_ptr,allocType at,funcType ft,int failed_realloc,int enable,
    void *caller )
{
  if( free_ptr )
  {
    GET_REMOTEDATA( rd );

    freed f;
    int splitIdx = (((uintptr_t)free_ptr)>>rd->ptrShift)&SPLIT_MASK;
    splitAllocation *sa = rd->splits + splitIdx;
    size_t freeSize = -1;

    EnterCriticalSection( &sa->cs );

    int i;
    for( i=sa->alloc_q-1; i>=0 && sa->alloc_a[i].ptr!=free_ptr; i-- );
    // successful free {{{
    if( LIKELY(i>=0 && (sa->alloc_a[i].frameCount || enable)) )
    {
      RtlMoveMemory( &f.a,&sa->alloc_a[i],sizeof(allocation) );

      if( !failed_realloc )
      {
        sa->alloc_q--;
        if( i<sa->alloc_q ) RtlMoveMemory(
            &sa->alloc_a[i],&sa->alloc_a[sa->alloc_q],sizeof(allocation) );
      }
      else
        sa->alloc_a[i].frameCount = enable;

      LeaveCriticalSection( &sa->cs );

      freeSize = f.a.size;

      // freed memory information {{{
      if( rd->opt.protectFree && !failed_realloc )
      {
        f.a.ftFreed = ft;
#ifndef NO_THREADNAMES
        f.threadNameIdx = (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
#endif

        CAPTURE_STACK_TRACE( 2,PTRS,f.frames,caller,rd->maxStackFrames );

        splitFreed *sf = rd->freeds + splitIdx;

        EnterCriticalSection( &sf->cs );

        if( sf->freed_q>=sf->freed_s )
        {
          sf->freed_s += 64;
          freed *freed_an;
          if( !sf->freed_a )
            freed_an = HeapAlloc(
                rd->heap,0,sf->freed_s*sizeof(freed) );
          else
            freed_an = HeapReAlloc(
                rd->heap,0,sf->freed_a,sf->freed_s*sizeof(freed) );
          if( UNLIKELY(!freed_an) )
          {
            LeaveCriticalSection( &sf->cs );
            EnterCriticalSection( &rd->csWrite );

            DWORD written;
            int type = WRITE_MAIN_ALLOC_FAIL;
            WriteFile( rd->master,&type,sizeof(int),&written,NULL );

            LeaveCriticalSection( &rd->csWrite );

            exitWait( 1,0 );
          }
          sf->freed_a = freed_an;
        }

        RtlMoveMemory( sf->freed_a+sf->freed_q,&f,sizeof(freed) );
        sf->freed_q++;

        LeaveCriticalSection( &sf->cs );
      }
      // }}}

      // mismatching allocation/release method {{{
      if( UNLIKELY(rd->opt.allocMethod && f.a.at!=at) )
      {
        allocation *aa = HeapAlloc( rd->heap,0,2*sizeof(allocation) );
        if( UNLIKELY(!aa) )
        {
          EnterCriticalSection( &rd->csWrite );

          DWORD written;
          int type = WRITE_MAIN_ALLOC_FAIL;
          WriteFile( rd->master,&type,sizeof(int),&written,NULL );

          LeaveCriticalSection( &rd->csWrite );

          exitWait( 1,0 );
        }

        RtlMoveMemory( aa,&f.a,sizeof(allocation) );
        CAPTURE_STACK_TRACE( 2,PTRS,aa[1].frames,caller,rd->maxStackFrames );
        aa[1].ptr = free_ptr;
        aa[1].size = 0;
        aa[1].at = at;
        aa[1].lt = LT_LOST;
        aa[1].ft = ft;
#ifndef NO_THREADNAMES
        aa[1].threadNameIdx = (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
#endif

        EnterCriticalSection( &rd->csWrite );

        writeMods( aa,2 );

        int type = WRITE_WRONG_DEALLOC;
        DWORD written;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );
        WriteFile( rd->master,aa,2*sizeof(allocation),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );

        HeapFree( rd->heap,0,aa );

        if( rd->opt.raiseException>1 )
          DebugBreak();
      }
      // }}}
    }
    // }}}
    // free of invalid pointer {{{
    else
    {
      LeaveCriticalSection( &sa->cs );
      EnterCriticalSection( &rd->csFreedMod );

      int inExit = rd->inExit;

      LeaveCriticalSection( &rd->csFreedMod );

      // double free {{{
      if( rd->opt.protectFree )
      {
        splitFreed *sf = rd->freeds + splitIdx;

        EnterCriticalSection( &sf->cs );

        for( i=sf->freed_q-1; i>=0 && sf->freed_a[i].a.ptr!=free_ptr; i-- );
        if( i>=0 )
        {
          RtlMoveMemory( &f,&sf->freed_a[i],sizeof(freed) );

          LeaveCriticalSection( &sf->cs );

          allocation *aa = HeapAlloc( rd->heap,0,3*sizeof(allocation) );
          if( UNLIKELY(!aa) )
          {
            EnterCriticalSection( &rd->csWrite );

            DWORD written;
            int type = WRITE_MAIN_ALLOC_FAIL;
            WriteFile( rd->master,&type,sizeof(int),&written,NULL );

            LeaveCriticalSection( &rd->csWrite );

            exitWait( 1,0 );
          }

          CAPTURE_STACK_TRACE( 2,PTRS,aa[0].frames,caller,rd->maxStackFrames );
          aa[0].ft = ft;
#ifndef NO_THREADNAMES
          aa[0].threadNameIdx =
            (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
#endif

          RtlMoveMemory( &aa[1],&f.a,sizeof(allocation) );

          RtlMoveMemory( aa[2].frames,f.frames,PTRS*sizeof(void*) );
          aa[2].ft = aa[1].ftFreed;
#ifndef NO_THREADNAMES
          aa[2].threadNameIdx = f.threadNameIdx;
#endif

          EnterCriticalSection( &rd->csWrite );

          writeMods( aa,3 );

          int type = WRITE_DOUBLE_FREE;
          DWORD written;
          WriteFile( rd->master,&type,sizeof(int),&written,NULL );
          WriteFile( rd->master,aa,3*sizeof(allocation),&written,NULL );

          LeaveCriticalSection( &rd->csWrite );

          HeapFree( rd->heap,0,aa );

          if( rd->opt.raiseException )
            DebugBreak();
        }
        else
          LeaveCriticalSection( &sf->cs );
      }
      // }}}

      if( i>=0 || !rd->crtHeap );
      else if( heap_block_size(rd->crtHeap,free_ptr)!=(size_t)-1 )
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
        allocation *aa = HeapAlloc(
            rd->heap,HEAP_ZERO_MEMORY,4*sizeof(allocation) );
        if( UNLIKELY(!aa) )
        {
          EnterCriticalSection( &rd->csWrite );

          DWORD written;
          int type = WRITE_MAIN_ALLOC_FAIL;
          WriteFile( rd->master,&type,sizeof(int),&written,NULL );

          LeaveCriticalSection( &rd->csWrite );

          exitWait( 1,0 );
        }

        aa->ptr = free_ptr;
        aa->size = 0;
        aa->at = at;
        aa->lt = LT_LOST;
        aa->ft = ft;
#ifndef NO_THREADNAMES
        aa->threadNameIdx = (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
#endif

        CAPTURE_STACK_TRACE( 2,PTRS,aa->frames,caller,rd->maxStackFrames );

        int protect = rd->opt.protect;
        uintptr_t ptr = (uintptr_t)free_ptr;
        size_t pageAdd = rd->pageAdd;
        DWORD pageSize = rd->pageSize;
        int j;
        int foundAlloc = 0;
        int foundRef = ptr==(uintptr_t)rd->opt.init;
#ifndef _WIN64
        foundRef |= ptr==(uintptr_t)(rd->opt.init>>32);
#endif

        // block address with offset {{{
        for( j=0; j<=SPLIT_MASK; j++ )
        {
          sa = rd->splits + j;

          EnterCriticalSection( &sa->cs );

          allocation *alloc_a = sa->alloc_a;
          int alloc_q = sa->alloc_q;
          for( i=0; i<alloc_q; i++ )
          {
            allocation *a = alloc_a + i;
            uintptr_t p = (uintptr_t)a->ptr;
            size_t s = a->size;

            if( !foundAlloc )
            {
              uintptr_t realStart;
              uintptr_t realEnd;
              size_t slackSize;
              if( protect==1 )
              {
                slackSize = p%pageSize;
                realStart = p - slackSize;
                realEnd = p + s + pageSize*pageAdd;
              }
              else
              {
                slackSize = ( pageSize - (s%pageSize) )%pageSize;
                realStart = p - pageSize*pageAdd;
                realEnd = p + s + slackSize;
              }

              if( ptr>=realStart && ptr<realEnd )
              {
                RtlMoveMemory( &aa[1],a,sizeof(allocation) );
                foundAlloc = 1;
                if( foundRef ) break;
              }
            }

            if( !foundRef && a->frameCount )
            {
              uintptr_t *refP = a->ptr;
              size_t refS = s/sizeof(void*);
              size_t k;
              for( k=0; k<refS; k++ )
              {
                if( refP[k]!=ptr ) continue;

                RtlMoveMemory( &aa[3],a,sizeof(allocation) );
                // in [2], because it's the only big enough unused field
                aa[2].size = k*sizeof(void*);
                foundRef = 1;
                break;
              }
              if( foundAlloc && foundRef ) break;
            }
          }

          LeaveCriticalSection( &sa->cs );

          if( foundAlloc && foundRef ) break;
        }
        // }}}

        // freed block address with offset {{{
        if( rd->opt.protectFree && !foundAlloc )
        {
          for( j=0; j<=SPLIT_MASK; j++ )
          {
            splitFreed *sf = rd->freeds + j;

            EnterCriticalSection( &sf->cs );

            freed *freed_a = sf->freed_a;
            int freed_q = sf->freed_q;
            for( i=0; i<freed_q; i++ )
            {
              freed *ff = freed_a + i;
              allocation *a = &ff->a;
              uintptr_t p = (uintptr_t)a->ptr;
              size_t s = a->size;

              if( !foundAlloc )
              {
                uintptr_t realStart;
                uintptr_t realEnd;
                size_t slackSize;
                if( protect==1 )
                {
                  slackSize = p%pageSize;
                  realStart = p - slackSize;
                  realEnd = p + s + pageSize*pageAdd;
                }
                else
                {
                  slackSize = ( pageSize - (s%pageSize) )%pageSize;
                  realStart = p - pageSize*pageAdd;
                  realEnd = p + s + slackSize;
                }

                if( ptr>=realStart && ptr<realEnd )
                {
                  RtlMoveMemory( &aa[1],a,sizeof(allocation) );
                  aa[2].ptr = aa[1].ptr;
                  RtlMoveMemory( aa[2].frames,ff->frames,PTRS*sizeof(void*) );
                  aa[2].ft = ff->a.ftFreed;
#ifndef NO_THREADNAMES
                  aa[2].threadNameIdx = ff->threadNameIdx;
#endif
                  foundAlloc = 1;
                  break;
                }
              }
            }

            LeaveCriticalSection( &sf->cs );

            if( foundAlloc ) break;
          }
        }
        // }}}

        // stack address {{{
        if( !foundAlloc )
        {
          TEB *teb = NtCurrentTeb();
          if( free_ptr>=teb->StackLimit && free_ptr<teb->StackBase )
          {
            // is otherwise unused since !foundAlloc
            aa[1].id = 1;

            if( ptr%sizeof(uintptr_t) )
              ptr -= ptr%sizeof(uintptr_t);
            void **frame = FRAME_ADDRESS();
            void **endFrame = (void**)ptr;
            int frameIdx = 0;
            void *prevFrame = NULL;
            void *curFrame = aa[0].frames[frameIdx];
            while( curFrame && frame<endFrame )
            {
              if( *frame==curFrame )
              {
                frameIdx++;
                if( frameIdx==PTRS )
                {
                  prevFrame = NULL;
                  break;
                }
                prevFrame = curFrame;
                curFrame = aa[0].frames[frameIdx];
              }
              frame++;
            }
            if( !curFrame ) prevFrame = NULL;
            aa[1].frames[0] = prevFrame;

            foundAlloc = 1;
          }
        }
        // }}}

        // block of different CRT {{{
        if( !foundAlloc )
        {
          EnterCriticalSection( &rd->csMod );

          HMODULE *crt_mod_a = rd->crt_mod_a;
          int crt_mod_q = rd->crt_mod_q;
          for( i=0; i<crt_mod_q; i++ )
          {
            HANDLE (*fget_heap_handle)( void ) =
              rd->fGetProcAddress( crt_mod_a[i],"_get_heap_handle" );
            HANDLE crtHeap = fget_heap_handle();
            if( crtHeap==rd->crtHeap ) continue;

            size_t s = heap_block_size( crtHeap,free_ptr );
            if( s==(size_t)-1 ) continue;

            aa[1].id = 2;
            aa[1].size = s;
            // send info of this CRT module
            aa[1].frames[0] = fget_heap_handle;

            foundAlloc = 1;
            break;
          }

          LeaveCriticalSection( &rd->csMod );
        }
        // }}}

        // global area address {{{
        if( !foundAlloc )
        {
          EnterCriticalSection( &rd->csMod );

          modMemType *mod_mem_a = rd->mod_mem_a;
          int mod_mem_q = rd->mod_mem_q;
          for( i=0; i<mod_mem_q; i++ )
          {
            if( free_ptr>=(void*)mod_mem_a[i].start &&
                free_ptr<(void*)mod_mem_a[i].end )
            {
              MEMORY_BASIC_INFORMATION mbi;
              if( VirtualQuery(free_ptr,&mbi,
                    sizeof(MEMORY_BASIC_INFORMATION)) )
              {
                HMODULE mod = mbi.AllocationBase;
                PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)mod;
                PIMAGE_NT_HEADERS inh =
                  (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );

                aa[1].id = 3;
                aa[1].frames[0] =
                  REL_PTR( idh,inh->OptionalHeader.BaseOfCode );

                foundAlloc = 1;
              }
              break;
            }
          }

          LeaveCriticalSection( &rd->csMod );
        }
        // }}}

        EnterCriticalSection( &rd->csWrite );

        writeMods( aa,4 );

        DWORD written;
        int type = WRITE_FREE_FAIL;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );
        WriteFile( rd->master,aa,4*sizeof(allocation),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );

        HeapFree( rd->heap,0,aa );

        if( rd->opt.raiseException )
          DebugBreak();
      }
    }
    // }}}

    TlsSetValue( rd->freeSizeTls,(void*)freeSize );
  }
}
#define trackFree(f,at,ft,fr,e) trackFree(f,at,ft,fr,e,RETURN_ADDRESS())

static NOINLINE void trackAllocSuccess(
    void *alloc_ptr,size_t alloc_size,allocType at,funcType ft,void *caller )
{
  GET_REMOTEDATA( rd );

  {
    uintptr_t align = rd->opt.align;
    alloc_size += ( align - (alloc_size%align) )%align;

    allocation a;
    a.ptr = alloc_ptr;
    a.size = alloc_size;
    a.at = at;
    a.recording = rd->recording;
    a.lt = LT_LOST;
    a.ft = ft;
    a.frameCount = 1; // is 0 while realloc() is called (state unknown)
    a.id = IL_INC( (IL_INT*)&rd->cur_id );
#ifndef NO_THREADNAMES
    a.threadNameIdx = (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
    if( UNLIKELY(!a.threadNameIdx) )
    {
      DWORD threadNameTls = rd->threadNameTls;

      EnterCriticalSection( &rd->csWrite );

      int threadNameIdx = (int)(uintptr_t)TlsGetValue( threadNameTls );
      if( !threadNameIdx )
      {
        threadNameIdx = --rd->threadNameIdx;
        TlsSetValue( threadNameTls,(void*)(uintptr_t)threadNameIdx );
      }

      LeaveCriticalSection( &rd->csWrite );

      a.threadNameIdx = threadNameIdx;
    }
#endif

    CAPTURE_STACK_TRACE( 2,PTRS,a.frames,caller,rd->maxStackFrames );

    int is_next_raise = 0;
    if( rd->raise_id )
    {
      EnterCriticalSection( &rd->csAllocId );

      is_next_raise = a.id==rd->raise_id && a.id;

      if( UNLIKELY(is_next_raise) )
        rd->raise_id = *(rd->raise_alloc_a++);

      LeaveCriticalSection( &rd->csAllocId );
    }

    int raiseException = 0;
    if( UNLIKELY(is_next_raise) )
    {
      EnterCriticalSection( &rd->csWrite );

      DWORD written;
      int type = WRITE_RAISE_ALLOCATION;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );
      WriteFile( rd->master,&a.id,sizeof(size_t),&written,NULL );
      WriteFile( rd->master,&ft,sizeof(funcType),&written,NULL );

      LeaveCriticalSection( &rd->csWrite );

      raiseException = 1;
    }

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
      if( UNLIKELY(!alloc_an) )
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
}
static NOINLINE void trackAllocFailure(
    size_t alloc_size,size_t mul,allocType at,funcType ft,void *caller )
{
  GET_REMOTEDATA( rd );

  {
    allocation a;
    a.ptr = NULL;
    a.size = alloc_size;
    a.at = at;
    a.lt = LT_LOST;
    a.ft = ft;
    a.id = IL_INC( (IL_INT*)&rd->cur_id );
#ifndef NO_THREADNAMES
    a.threadNameIdx = (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
#endif

    CAPTURE_STACK_TRACE( 2,PTRS,a.frames,caller,rd->maxStackFrames );

    int is_next_raise = 0;
    if( rd->raise_id )
    {
      EnterCriticalSection( &rd->csAllocId );

      is_next_raise = a.id==rd->raise_id && a.id;

      if( UNLIKELY(is_next_raise) )
        rd->raise_id = *(rd->raise_alloc_a++);

      LeaveCriticalSection( &rd->csAllocId );
    }

    EnterCriticalSection( &rd->csWrite );

    writeMods( &a,1 );

    DWORD written;
    int type = WRITE_ALLOC_FAIL;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    WriteFile( rd->master,&mul,sizeof(size_t),&written,NULL );
    WriteFile( rd->master,&a,sizeof(allocation),&written,NULL );

    int raiseException = rd->opt.raiseException;
    if( UNLIKELY(is_next_raise) )
    {
      type = WRITE_RAISE_ALLOCATION;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );
      WriteFile( rd->master,&a.id,sizeof(size_t),&written,NULL );
      WriteFile( rd->master,&ft,sizeof(funcType),&written,NULL );

      raiseException = 1;
    }

    LeaveCriticalSection( &rd->csWrite );

    if( raiseException )
      DebugBreak();
  }
}
#define trackAlloc(a,s,at,ft) \
  do { \
    size_t sVal = s; \
    if( LIKELY(a) ) \
      trackAllocSuccess( a,sVal,at,ft,RETURN_ADDRESS() ); \
    else if( sVal ) \
      trackAllocFailure( sVal,1,at,ft,RETURN_ADDRESS() ); \
  } while( 0 )
#define trackCalloc(a,s,m,at,ft) \
  do { \
    if( LIKELY(a) ) \
      trackAllocSuccess( a,(s)*(m),at,ft,RETURN_ADDRESS() ); \
    else if( (s) && (m) ) \
      trackAllocFailure( s,m,at,ft,RETURN_ADDRESS() ); \
  } while( 0 )

// }}}
// replacements for memory allocation tracking {{{

static void *new_malloc( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fmalloc( s );

  trackAlloc( b,s,AT_MALLOC,FT_MALLOC );

  return( b );
}

static void *new_calloc( size_t n,size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fcalloc( n,s );

  trackCalloc( b,n,s,AT_MALLOC,FT_CALLOC );

  return( b );
}

static void new_free( void *b )
{
  trackFree( b,AT_MALLOC,FT_FREE,0,0 );

  GET_REMOTEDATA( rd );
  rd->ffree( b );
}

static void *new_realloc( void *b,size_t s )
{
  GET_REMOTEDATA( rd );

  size_t os = -1;
  int enable = 0;
  if( b )
  {
    enable = allocSizeAndState( b,&os,0 );
    TlsSetValue( rd->freeSizeTls,(void*)os );
  }

  void *nb = rd->frealloc( b,s );

  trackFree( b,AT_MALLOC,FT_REALLOC,!nb && s,enable );
  trackAlloc( nb,s,AT_MALLOC,FT_REALLOC );

  return( nb );
}

static char *new_strdup( const char *s )
{
  GET_REMOTEDATA( rd );
  char *b = rd->fstrdup( s );

  trackAlloc( b,lstrlen(s)+1,AT_MALLOC,FT_STRDUP );

  return( b );
}

static wchar_t *new_wcsdup( const wchar_t *s )
{
  GET_REMOTEDATA( rd );
  wchar_t *b = rd->fwcsdup( s );

  trackAlloc( b,(lstrlenW(s)+1)*2,AT_MALLOC,FT_WCSDUP );

  return( b );
}

static void *new_op_new( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fop_new( s );

  trackAlloc( b,s,AT_NEW,FT_OP_NEW );

  return( b );
}

static void new_op_delete( void *b )
{
  trackFree( b,AT_NEW,FT_OP_DELETE,0,0 );

  GET_REMOTEDATA( rd );
  rd->fop_delete( b );
}

static void *new_op_new_a( size_t s )
{
  GET_REMOTEDATA( rd );
  void *b = rd->fop_new_a( s );

  trackAlloc( b,s,rd->newArrAllocMethod,FT_OP_NEW_A );

  return( b );
}

static void new_op_delete_a( void *b )
{
  GET_REMOTEDATA( rd );

  trackFree( b,rd->newArrAllocMethod,FT_OP_DELETE_A,0,0 );

  rd->fop_delete_a( b );
}

static char *new_getcwd( char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->fgetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  trackAlloc( cwd,l,AT_MALLOC,FT_GETCWD );

  return( cwd );
}

static wchar_t *new_wgetcwd( wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->fwgetcwd( buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  trackAlloc( cwd,l*2,AT_MALLOC,FT_WGETCWD );

  return( cwd );
}

static char *new_getdcwd( int drive,char *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  char *cwd = rd->fgetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlen( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  trackAlloc( cwd,l,AT_MALLOC,FT_GETDCWD );

  return( cwd );
}

static wchar_t *new_wgetdcwd( int drive,wchar_t *buffer,int maxlen )
{
  GET_REMOTEDATA( rd );
  wchar_t *cwd = rd->fwgetdcwd( drive,buffer,maxlen );
  if( !cwd || buffer ) return( cwd );

  size_t l = lstrlenW( cwd ) + 1;
  if( maxlen>0 && (unsigned)maxlen>l ) l = maxlen;
  trackAlloc( cwd,l*2,AT_MALLOC,FT_WGETDCWD );

  return( cwd );
}

static char *new_fullpath( char *absPath,const char *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  char *fp = rd->ffullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  size_t l = lstrlen( fp ) + 1;
  trackAlloc( fp,l,AT_MALLOC,FT_FULLPATH );

  return( fp );
}

static wchar_t *new_wfullpath( wchar_t *absPath,const wchar_t *relPath,
    size_t maxLength )
{
  GET_REMOTEDATA( rd );
  wchar_t *fp = rd->fwfullpath( absPath,relPath,maxLength );
  if( !fp || absPath ) return( fp );

  size_t l = lstrlenW( fp ) + 1;
  trackAlloc( fp,l*2,AT_MALLOC,FT_WFULLPATH );

  return( fp );
}

static char *new_tempnam( char *dir,char *prefix )
{
  GET_REMOTEDATA( rd );
  char *tn = rd->ftempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = lstrlen( tn ) + 1;
  trackAlloc( tn,l,AT_MALLOC,FT_TEMPNAM );

  return( tn );
}

static wchar_t *new_wtempnam( wchar_t *dir,wchar_t *prefix )
{
  GET_REMOTEDATA( rd );
  wchar_t *tn = rd->fwtempnam( dir,prefix );
  if( !tn ) return( tn );

  size_t l = lstrlenW( tn ) + 1;
  trackAlloc( tn,l*2,AT_MALLOC,FT_WTEMPNAM );

  return( tn );
}

static void new_free_dbg( void *b,int blockType )
{
  trackFree( b,AT_MALLOC,FT_FREE_DBG,0,0 );

  GET_REMOTEDATA( rd );
  rd->ffree_dbg( b,blockType );
}

static void *new_recalloc( void *b,size_t n,size_t s )
{
  GET_REMOTEDATA( rd );

  size_t os = -1;
  int enable = 0;
  if( b )
  {
    enable = allocSizeAndState( b,&os,0 );
    TlsSetValue( rd->freeSizeTls,(void*)os );
  }

  void *nb = rd->frecalloc( b,n,s );

  trackFree( b,AT_MALLOC,FT_RECALLOC,!nb && n && s,enable );
  trackCalloc( nb,n,s,AT_MALLOC,FT_RECALLOC );

  return( nb );
}

// }}}
// transfer leak data {{{

static void writeLeakMods( allocation *exitTrace )
{
  GET_REMOTEDATA( rd );

  int i;
  int mi_q = 0;
  modInfo *mi_a = NULL;
  if( rd->splits )
  {
    for( i=0; i<=SPLIT_MASK; i++ )
    {
      splitAllocation *sa = rd->splits + i;
      writeModsFind( sa->alloc_a,sa->alloc_q,&mi_a,&mi_q );
    }
  }
  if( exitTrace )
    writeModsFind( exitTrace,1,&mi_a,&mi_q );
  writeModsSend( mi_a,mi_q );
  if( mi_a )
    HeapFree( rd->heap,0,mi_a );
}

static void writeLeakData( void )
{
  GET_REMOTEDATA( rd );

  // no leak data available {{{
  if( !rd->splits )
  {
    DWORD written;
    int type = WRITE_LEAKS;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    int i = 0;
    size_t s = 0;
    // alloc_q
    WriteFile( rd->master,&i,sizeof(int),&written,NULL );
    // alloc_ignore_q
    WriteFile( rd->master,&i,sizeof(int),&written,NULL );
    // alloc_ignore_sum
    WriteFile( rd->master,&s,sizeof(size_t),&written,NULL );
    // alloc_ignore_ind_q
    WriteFile( rd->master,&i,sizeof(int),&written,NULL );
    // alloc_ignore_ind_sum
    WriteFile( rd->master,&s,sizeof(size_t),&written,NULL );
    // alloc_mem_sum
    WriteFile( rd->master,&s,sizeof(size_t),&written,NULL );
    return;
  }
  // }}}

  // leak count {{{
  int i;
  int alloc_q = 0;
  int alloc_ignore_q = 0;
  size_t alloc_ignore_sum = 0;
  int alloc_ignore_ind_q = 0;
  size_t alloc_ignore_ind_sum = 0;
  int lDetails = rd->opt.leakDetails ?
    ( (rd->opt.leakDetails&1) ? LT_COUNT : LT_REACHABLE ) : 0;
  for( i=0; i<=SPLIT_MASK; i++ )
  {
    splitAllocation *sa = rd->splits + i;
    int j;
    int part_q = sa->alloc_q;
    for( j=0; j<part_q; j++ )
    {
      allocation *a = sa->alloc_a + j;
      if( a->recording && a->frameCount )
      {
        if( a->lt<lDetails )
          alloc_q++;
        else if( a->lt!=LT_INDIRECTLY_REACHABLE )
        {
          alloc_ignore_q++;
          alloc_ignore_sum += a->size;
        }
        else
        {
          alloc_ignore_ind_q++;
          alloc_ignore_ind_sum += a->size;
        }
      }
    }
  }
  DWORD written;
  int type = WRITE_LEAKS;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,&alloc_q,sizeof(int),&written,NULL );
  WriteFile( rd->master,&alloc_ignore_q,sizeof(int),&written,NULL );
  WriteFile( rd->master,&alloc_ignore_sum,sizeof(size_t),&written,NULL );
  WriteFile( rd->master,&alloc_ignore_ind_q,sizeof(int),&written,NULL );
  WriteFile( rd->master,&alloc_ignore_ind_sum,sizeof(size_t),&written,NULL );
  // }}}

  // leak data {{{
  size_t alloc_mem_sum = 0;
  size_t leakContents = rd->opt.leakContents;
  for( i=0; i<=SPLIT_MASK; i++ )
  {
    splitAllocation *sa = rd->splits + i;
    alloc_q = sa->alloc_q;
    if( !alloc_q ) continue;

    int j;
    allocation *a_send = NULL;
    int a_count = 0;
    for( j=0; j<alloc_q; j++ )
    {
      allocation *a = sa->alloc_a + j;
      if( a->recording && a->frameCount && a->lt<lDetails )
      {
        if( a_send!=a )
        {
          if( a_send )
            WriteFile( rd->master,
                a_send-a_count,a_count*sizeof(allocation),&written,NULL );
          a_send = a;
          a_count = 0;
        }
        a_send++;
        a_count++;
      }
    }
    if( a_send )
      WriteFile( rd->master,
          a_send-a_count,a_count*sizeof(allocation),&written,NULL );

    if( leakContents )
    {
      for( j=0; j<alloc_q; j++ )
      {
        allocation *a = sa->alloc_a + j;
        if( !a->recording || !a->frameCount || a->lt>=lDetails ) continue;
        size_t s = a->size;
        alloc_mem_sum += s<leakContents ? s : leakContents;
      }
    }
  }
  // }}}

  // leak contents {{{
  WriteFile( rd->master,&alloc_mem_sum,sizeof(size_t),&written,NULL );
  if( alloc_mem_sum )
  {
    for( i=0; i<=SPLIT_MASK; i++ )
    {
      splitAllocation *sa = rd->splits + i;
      alloc_q = sa->alloc_q;
      int j;
      for( j=0; j<alloc_q; j++ )
      {
        allocation *a = sa->alloc_a + j;
        if( !a->recording || !a->frameCount || a->lt>=lDetails ) continue;
        size_t s = a->size;
        if( leakContents<s ) s = leakContents;
        if( s )
          WriteFile( rd->master,a->ptr,(DWORD)s,&written,NULL );
      }
    }
  }
  // }}}
}

// }}}
// leak type detection {{{

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
    if( UNLIKELY(!mod_mem_an) )
    {
      EnterCriticalSection( &rd->csWrite );

      DWORD written;
      int type = WRITE_MAIN_ALLOC_FAIL;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );

      LeaveCriticalSection( &rd->csWrite );

      exitWait( 1,0 );
    }
    rd->mod_mem_a = mod_mem_an;
  }

  modMemType *mod_mem = &rd->mod_mem_a[rd->mod_mem_q++];
  mod_mem->start = (const void**)startPtr;
  mod_mem->end = (const void**)endPtr;
}

typedef struct
{
  leakType lt;
  modMemType *mod_mem_a;
  int mod_mem_q;
  LONG idx;
}
leakTypeInfo;

typedef struct
{
  HANDLE startEvent;
  HANDLE finishedEvent;
  leakTypeInfo *lti;
}
leakTypeThreadInfo;

static void findLeakTypeWork( leakTypeInfo *lti )
{
  GET_REMOTEDATA( rd );

  leakType lt = lti->lt;
  modMemType *mod_mem_a = lti->mod_mem_a;
  int mod_mem_q = lti->mod_mem_q;

  int compareExact = rd->opt.leakDetails<4;
  int ltUse = lt==LT_INDIRECTLY_LOST ? LT_JOINTLY_LOST : LT_LOST;
  while( 1 )
  {
    int i = InterlockedIncrement( &lti->idx );
    if( i>SPLIT_MASK ) break;

    int j;
    splitAllocation *sa = rd->splits + i;
    int alloc_q = sa->alloc_q;
    allocation *alloc_a = sa->alloc_a;
    for( j=0; j<alloc_q; j++ )
    {
      allocation *a = alloc_a + j;
      if( a->lt!=ltUse || !a->frameCount ) continue;
      int k;
      uintptr_t ptr = (uintptr_t)a->ptr;
      size_t size = a->size;
      int compareExactNow = compareExact || !size;
      uintptr_t ptrEnd = ptr + size;
      for( k=0; k<mod_mem_q; k++ )
      {
        modMemType *mod_mem = mod_mem_a + k;
        const void **start = mod_mem->start;
        if( (uintptr_t)start==ptr ) continue;
        const void **end = mod_mem->end;
        if( compareExactNow )
        {
          for( ; start<end; start++ )
          {
            uintptr_t memPtr = (uintptr_t)*start;
            if( memPtr==ptr ) break;
          }
        }
        else
        {
          for( ; start<end; start++ )
          {
            uintptr_t memPtr = (uintptr_t)*start;
            if( memPtr>=ptr && memPtr<ptrEnd ) break;
          }
        }
        if( start<end ) break;
      }
      if( k<mod_mem_q )
      {
        a->lt = lt;
        if( lt!=LT_JOINTLY_LOST )
        {
          PBYTE memStart = a->ptr;
          EnterCriticalSection( &rd->csMod );
          addModMem( memStart,memStart+a->size );
          LeaveCriticalSection( &rd->csMod );
        }
      }
    }
  }
}

static DWORD WINAPI findLeakTypeThread( LPVOID arg )
{
  leakTypeThreadInfo *ltti = arg;
  HANDLE startEvent = ltti->startEvent;
  HANDLE finishedEvent = ltti->finishedEvent;

  while( 1 )
  {
    WaitForSingleObject( startEvent,INFINITE );

    findLeakTypeWork( ltti->lti );

    SetEvent( finishedEvent );
  }

  return( 0 );
}

static void findLeakType( leakType lt,
    leakTypeThreadInfo *ltti,HANDLE *finishedEvents )
{
  GET_REMOTEDATA( rd );

  modMemType *mod_mem_a = rd->mod_mem_a;
  int mod_mem_q = rd->mod_mem_q;
  rd->mod_mem_a = NULL;
  rd->mod_mem_q = rd->mod_mem_s = 0;
  if( !mod_mem_a ) return;

  leakTypeInfo lti;
  lti.lt = lt;
  lti.mod_mem_a = mod_mem_a;
  lti.mod_mem_q = mod_mem_q;
  lti.idx = -1;

  if( ltti )
  {
    int t;
    for( t=0; t<rd->processors; t++ )
    {
      ltti[t].lti = &lti;
      SetEvent( ltti[t].startEvent );
    }
    WaitForMultipleObjects( rd->processors,finishedEvents,TRUE,INFINITE );
  }
  else
    findLeakTypeWork( &lti );

  HeapFree( rd->heap,0,mod_mem_a );
}

void findLeakTypes( void )
{
  GET_REMOTEDATA( rd );

  SetPriorityClass( GetCurrentProcess(),BELOW_NORMAL_PRIORITY_CLASS );

  leakTypeThreadInfo *ltti = NULL;
  HANDLE *finishedEvents = NULL;
  int processors = rd->processors;
  if( processors>1 )
  {
    ltti = HeapAlloc( rd->heap,0,processors*sizeof(leakTypeThreadInfo) );
    finishedEvents = HeapAlloc( rd->heap,0,processors*sizeof(HANDLE) );
    int t;
    for( t=0; t<processors; t++ )
    {
      ltti[t].startEvent = CreateEvent( NULL,FALSE,FALSE,NULL );
      ltti[t].finishedEvent = CreateEvent( NULL,FALSE,FALSE,NULL );
      finishedEvents[t] = ltti[t].finishedEvent;
      HANDLE thread = CreateThread( NULL,0,&findLeakTypeThread,ltti+t,0,NULL );
      CloseHandle( thread );
    }
  }

  findLeakType( LT_REACHABLE,ltti,finishedEvents );
  while( rd->mod_mem_a )
    findLeakType( LT_INDIRECTLY_REACHABLE,ltti,finishedEvents );

  int k;
  for( k=0; k<2; k++ )
  {
    int i,j;
    for( i=0; i<=SPLIT_MASK; i++ )
    {
      splitAllocation *sa = rd->splits + i;
      int alloc_q = sa->alloc_q;
      allocation *alloc_a = sa->alloc_a;
      for( j=0; j<alloc_q; j++ )
      {
        allocation *a = alloc_a + j;
        if( a->lt!=LT_LOST || !a->frameCount ) continue;
        PBYTE memStart = a->ptr;
        EnterCriticalSection( &rd->csMod );
        addModMem( memStart,memStart+a->size );
        LeaveCriticalSection( &rd->csMod );
      }
    }
    leakType lt = k ? LT_INDIRECTLY_LOST : LT_JOINTLY_LOST;
    while( rd->mod_mem_a )
      findLeakType( lt,ltti,finishedEvents );
  }

  if( ltti ) HeapFree( rd->heap,0,ltti );
  if( finishedEvents ) HeapFree( rd->heap,0,finishedEvents );
}

// }}}
// replacements for ExitProcess/TerminateProcess {{{

static VOID WINAPI new_ExitProcess( UINT c )
{
  GET_REMOTEDATA( rd );

  EnterCriticalSection( &rd->csFreedMod );
  rd->inExit = 1;
  LeaveCriticalSection( &rd->csFreedMod );

  // exit trace {{{
  allocation exitTrace;
  allocation *exitTracePtr = NULL;
  if( rd->opt.exitTrace )
  {
#ifndef NO_THREADNAMES
    exitTrace.threadNameIdx = (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
#endif

    CAPTURE_STACK_TRACE( 1,PTRS,exitTrace.frames,RETURN_ADDRESS(),
        rd->maxStackFrames );

    exitTracePtr = &exitTrace;
  }
  // }}}

  int i;
  EnterCriticalSection( &rd->csWrite );
  if( rd->splits )
  {
    for( i=0; i<=SPLIT_MASK; i++ )
      EnterCriticalSection( &rd->splits[i].cs );
  }

  if( rd->opt.leakDetails>1 )
    findLeakTypes();

  writeLeakMods( exitTracePtr );

  // free modules {{{
  if( rd->freed_mod_q )
  {
    if( rd->splits )
    {
      for( i=0; i<=SPLIT_MASK; i++ )
        LeaveCriticalSection( &rd->splits[i].cs );
    }
    LeaveCriticalSection( &rd->csWrite );

    for( i=0; i<rd->freed_mod_q; i++ )
      rd->fFreeLibrary( rd->freed_mod_a[i] );

    EnterCriticalSection( &rd->csWrite );
    if( rd->splits )
    {
      for( i=0; i<=SPLIT_MASK; i++ )
        EnterCriticalSection( &rd->splits[i].cs );
    }
  }
  // }}}

  writeLeakData();

  if( exitTracePtr )
  {
    int type = WRITE_EXIT_TRACE;
    DWORD written;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    WriteFile( rd->master,exitTracePtr,sizeof(allocation),&written,NULL );
  }

  int type = WRITE_EXIT;
  int terminated = 0;
  DWORD written;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,&c,sizeof(UINT),&written,NULL );
  WriteFile( rd->master,&terminated,sizeof(int),&written,NULL );

  if( rd->splits )
  {
    for( i=0; i<=SPLIT_MASK; i++ )
      LeaveCriticalSection( &rd->splits[i].cs );
  }
  LeaveCriticalSection( &rd->csWrite );

  exitWait( c,0 );
}

static BOOL WINAPI new_TerminateProcess( HANDLE p,UINT c )
{
  GET_REMOTEDATA( rd );

  if( p==GetCurrentProcess() )
  {
    EnterCriticalSection( &rd->csWrite );

    DWORD written;
    int type = WRITE_EXIT;
    int terminated = 1;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    WriteFile( rd->master,&c,sizeof(UINT),&written,NULL );
    WriteFile( rd->master,&terminated,sizeof(int),&written,NULL );

    LeaveCriticalSection( &rd->csWrite );

    exitWait( c,1 );
  }

  return( rd->fTerminateProcess(p,c) );
}

// }}}
// replacement for RaiseException {{{

#ifndef NO_THREADNAMES
#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
  DWORD dwType;
  LPCSTR szName;
  DWORD dwThreadID;
  DWORD dwFlags;
} THREADNAME_INFO;
#pragma pack(pop)

static VOID WINAPI new_RaiseException(
    DWORD dwExceptionCode,DWORD dwExceptionFlags,
    DWORD nNumberOfArguments,const ULONG_PTR *lpArguments )
{
  GET_REMOTEDATA( rd );

  if( dwExceptionCode==0x406d1388 &&
      nNumberOfArguments>=sizeof(THREADNAME_INFO)/sizeof(ULONG_PTR) )
  {
    const THREADNAME_INFO *tni = (const THREADNAME_INFO*)lpArguments;

    if( tni->dwType==0x1000 )
    {
      EnterCriticalSection( &rd->csWrite );

      // thread name index {{{
      int newNameIdx = 0;
      if( tni->szName && tni->szName[0] )
      {
        for( newNameIdx=rd->threadName_q; newNameIdx>0 &&
            lstrcmp(tni->szName,rd->threadName_a[newNameIdx-1].name);
            newNameIdx-- );

        if( !newNameIdx )
        {
          if( rd->threadName_q>=rd->threadName_s )
          {
            rd->threadName_s += 64;
            threadNameInfo *threadName_an;
            if( !rd->threadName_a )
              threadName_an = HeapAlloc(
                  rd->heap,0,rd->threadName_s*sizeof(threadNameInfo) );
            else
              threadName_an = HeapReAlloc(
                  rd->heap,0,rd->threadName_a,
                  rd->threadName_s*sizeof(threadNameInfo) );
            if( UNLIKELY(!threadName_an) )
            {
              DWORD written;
              int type = WRITE_MAIN_ALLOC_FAIL;
              WriteFile( rd->master,&type,sizeof(int),&written,NULL );

              LeaveCriticalSection( &rd->csWrite );

              exitWait( 1,0 );
            }
            rd->threadName_a = threadName_an;
          }
          threadNameInfo *threadName = &rd->threadName_a[rd->threadName_q++];
          size_t len = lstrlen( tni->szName );
          if( len>=sizeof(threadName->name) )
            len = sizeof(threadName->name) - 1;
          RtlMoveMemory( threadName->name,tni->szName,len );
          threadName->name[len] = 0;

          newNameIdx = rd->threadName_q;
        }
      }
      // }}}

      // attach name to thread {{{
      DWORD threadId = tni->dwThreadID;
      DWORD threadNameTls = rd->threadNameTls;
      if( threadId==(DWORD)-1 || threadId==GetCurrentThreadId() )
        TlsSetValue( threadNameTls,(void*)(uintptr_t)newNameIdx );
      else if( rd->fNtQueryInformationThread )
      {
        HANDLE thread = OpenThread(
            THREAD_QUERY_INFORMATION,FALSE,threadId );
        if( thread )
        {
          THREAD_BASIC_INFORMATION tbi;
          RtlZeroMemory( &tbi,sizeof(THREAD_BASIC_INFORMATION) );
          if( rd->fNtQueryInformationThread(thread,
                ThreadBasicInformation,&tbi,sizeof(tbi),NULL)==0 &&
              (ULONG_PTR)tbi.ClientId.UniqueProcess==GetCurrentProcessId() )
          {
            PTEB teb = tbi.TebBaseAddress;
            if( threadNameTls<64 )
            {
              void **tlsArr = teb->TlsSlots;
              tlsArr[threadNameTls] = (void*)(uintptr_t)newNameIdx;
            }
            else
            {
              void **tlsArr = teb->TlsExpansionSlots;
              if( tlsArr )
                tlsArr[threadNameTls-64] = (void*)(uintptr_t)newNameIdx;
            }
          }
          CloseHandle( thread );
        }
      }
      // }}}

      LeaveCriticalSection( &rd->csWrite );
    }
  }

  rd->fRaiseException( dwExceptionCode,dwExceptionFlags,
      nNumberOfArguments,lpArguments );
}
#endif

// }}}
// replacement for CreateProcess {{{

static void addOption( char *cmdLine,const char *optionStr,
    uintptr_t val,uintptr_t defaultVal,char *numEnd )
{
  if( val==defaultVal ) return;

  lstrcat( cmdLine,optionStr );
  lstrcat( cmdLine,num2str(numEnd,val,0) );
}

int heobSubProcess(
    DWORD creationFlags,LPPROCESS_INFORMATION processInformation,
    HMODULE heobMod,HANDLE heap,options *opt,
    func_CreateProcessW *fCreateProcessW,
    const char *subOutName,const char *subXmlName,const wchar_t *subCurDir,
    int raise_alloc_q,size_t *raise_alloc_a,const char *specificOptions )
{
  wchar_t heobPath[MAX_PATH];
  if( !GetModuleFileNameW(heobMod,heobPath,MAX_PATH) )
    heobPath[0] = 0;
  wchar_t *heobEnd = mstrrchrW( heobPath,'\\' );
  int withHeob = 0;
  int keepSuspended = ( creationFlags&CREATE_SUSPENDED )!=0;
  if( heobEnd )
  {
    heobEnd++;
    if( isWrongArch(processInformation->hProcess) )
    {
#ifndef _WIN64
#define OTHER_HEOB L"heob64.exe"
#else
#define OTHER_HEOB L"heob32.exe"
#endif
      lstrcpyW( heobEnd,OTHER_HEOB );
    }

    char *heobCmd = HeapAlloc( heap,0,32768 );
    wchar_t *heobCmdW = HeapAlloc( heap,0,32768*2 );
    if( heobCmd && heobCmdW )
    {
      // heob command line {{{
      char num[32];
      char *numEnd = num + sizeof(num);
      *(--numEnd) = 0;
      heobCmd[0] = 0;
#define ADD_OPTION( option,val,defVal ) \
      addOption( heobCmd,option,opt->val,defVal,numEnd )
      addOption( heobCmd," -A",processInformation->dwThreadId,0,numEnd );
      if( heobMod )
      {
        addOption( heobCmd,"/",GetCurrentProcessId(),0,numEnd );
        addOption( heobCmd,"+",keepSuspended,0,numEnd );
      }
      else
        ADD_OPTION( " -c",newConsole,0 );
      if( subOutName && subOutName[0] )
      {
        lstrcat( heobCmd," -o" );
        lstrcat( heobCmd,subOutName );
      }
      if( subXmlName && subXmlName[0] )
      {
        lstrcat( heobCmd," -x" );
        lstrcat( heobCmd,subXmlName );
      }
      ADD_OPTION( " -p",protect,1 );
      ADD_OPTION( " -a",align,MEMORY_ALLOCATION_ALIGNMENT );
      if( opt->init!=0xffffffffffffffffULL )
      {
        lstrcat( heobCmd," -i0x" );
        *num2hexstr( num,opt->init,16 ) = 0;
        lstrcat( heobCmd,num );
        lstrcat( heobCmd,":8" );
      }
      ADD_OPTION( " -s",slackInit,-1 );
      ADD_OPTION( " -f",protectFree,0 );
      ADD_OPTION( " -h",handleException,1 );
      ADD_OPTION( " -F",fullPath,0 );
      ADD_OPTION( " -m",allocMethod,0 );
      ADD_OPTION( " -l",leakDetails,1 );
      ADD_OPTION( " -S",useSp,0 );
      ADD_OPTION( " -d",dlls,3 );
      ADD_OPTION( " -P",pid,0 );
      ADD_OPTION( " -e",exitTrace,0 );
      ADD_OPTION( " -C",sourceCode,0 );
      ADD_OPTION( " -r",raiseException,0 );
      ADD_OPTION( " -M",minProtectSize,1 );
      ADD_OPTION( " -n",findNearest,1 );
      ADD_OPTION( " -L",leakContents,0 );
      ADD_OPTION( " -g",groupLeaks,1 );
      ADD_OPTION( " -z",minLeakSize,0 );
      ADD_OPTION( " -k",leakRecording,0 );
#undef ADD_OPTION
      int i;
      for( i=0; i<raise_alloc_q; i++ )
        addOption( heobCmd," -R",raise_alloc_a[i],0,numEnd );
      if( specificOptions )
      {
        lstrcat( heobCmd," -O" );
        lstrcat( heobCmd,specificOptions );
      }

      lstrcpyW( heobCmdW,heobEnd );
      char *hc = heobCmd;
      wchar_t *hcw = heobCmdW + lstrlenW( heobCmdW );
      while( *hc ) hcw++[0] = hc++[0];
      hcw[0] = 0;
      // }}}

      if( subCurDir && !subCurDir[0] ) subCurDir = NULL;

      STARTUPINFOW si;
      RtlZeroMemory( &si,sizeof(STARTUPINFOW) );
      si.cb = sizeof(STARTUPINFOW);
      si.dwFlags = STARTF_USESHOWWINDOW;
      si.wShowWindow = SW_SHOWMINNOACTIVE;
      PROCESS_INFORMATION pi;
      DWORD newConsole = heobMod || opt->newConsole>1 ? CREATE_NEW_CONSOLE : 0;
      RtlZeroMemory( &pi,sizeof(PROCESS_INFORMATION) );
      if( fCreateProcessW(heobPath,heobCmdW,NULL,NULL,FALSE,
            CREATE_SUSPENDED|newConsole,NULL,subCurDir,&si,&pi) )
      {
        char eventName[32] = "heob.attach.";
        char *end = num2hexstr(
            eventName+lstrlen(eventName),pi.dwProcessId,8 );
        end[0] = 0;
        HANDLE attachEvent = CreateEvent( NULL,FALSE,FALSE,eventName );
        HANDLE waitHandles[2] = { attachEvent,pi.hProcess };

        ResumeThread( pi.hThread );
        DWORD w = WaitForMultipleObjects( 2,waitHandles,FALSE,INFINITE );
        CloseHandle( attachEvent );

        if( w==WAIT_OBJECT_0 )
          withHeob = 1;
        else
          newConsole = 1;

        CloseHandle( pi.hThread );
        if( newConsole )
          CloseHandle( pi.hProcess );
        else
        {
          CloseHandle( processInformation->hProcess );
          processInformation->hProcess = pi.hProcess;
        }
      }
    }
    if( heobCmd )
      HeapFree( heap,0,heobCmd );
    if( heobCmdW )
      HeapFree( heap,0,heobCmdW );
  }

  if( !withHeob && heobMod && !keepSuspended )
    ResumeThread( processInformation->hThread );

  return( withHeob );
}

BOOL WINAPI new_CreateProcessA(
    LPCSTR applicationName,LPSTR commandLine,
    LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes,BOOL inheritHandles,
    DWORD creationFlags,LPVOID environment,LPCSTR currentDirectory,
    LPSTARTUPINFO startupInfo,LPPROCESS_INFORMATION processInformation )
{
  GET_REMOTEDATA( rd );

  DWORD suspend = (creationFlags&(DEBUG_PROCESS|DEBUG_ONLY_THIS_PROCESS)) ?
    0 : CREATE_SUSPENDED;

  BOOL ret = rd->fCreateProcessA(
      applicationName,commandLine,processAttributes,threadAttributes,
      inheritHandles,creationFlags|suspend,environment,
      currentDirectory,startupInfo,processInformation );
  if( !ret ) return( 0 );

  // no heob injection for debugged child processes
  if( !suspend ) return( 1 );

  heobSubProcess( creationFlags,processInformation,
      rd->heobMod,rd->heap,&rd->globalopt,rd->fCreateProcessW,
      rd->subOutName,rd->subXmlName,rd->subCurDir,0,NULL,rd->specificOptions );

  return( 1 );
}

BOOL WINAPI new_CreateProcessW(
    LPCWSTR applicationName,LPWSTR commandLine,
    LPSECURITY_ATTRIBUTES processAttributes,
    LPSECURITY_ATTRIBUTES threadAttributes,BOOL inheritHandles,
    DWORD creationFlags,LPVOID environment,LPCWSTR currentDirectory,
    LPSTARTUPINFOW startupInfo,LPPROCESS_INFORMATION processInformation )
{
  GET_REMOTEDATA( rd );

  DWORD suspend = (creationFlags&(DEBUG_PROCESS|DEBUG_ONLY_THIS_PROCESS)) ?
    0 : CREATE_SUSPENDED;

  BOOL ret = rd->fCreateProcessW(
      applicationName,commandLine,processAttributes,threadAttributes,
      inheritHandles,creationFlags|suspend,environment,
      currentDirectory,startupInfo,processInformation );
  if( !ret ) return( 0 );

  // no heob injection for debugged child processes
  if( !suspend ) return( 1 );

  heobSubProcess( creationFlags,processInformation,
      rd->heobMod,rd->heap,&rd->globalopt,rd->fCreateProcessW,
      rd->subOutName,rd->subXmlName,rd->subCurDir,0,NULL,rd->specificOptions );

  return( 1 );
}

// }}}
// replacements for LoadLibrary/FreeLibrary {{{

static void addModule( HMODULE mod );
static void replaceModFuncs( void );

static void addLoadedModule( HMODULE mod )
{
  GET_REMOTEDATA( rd );

  EnterCriticalSection( &rd->csMod );
  addModule( mod );
  replaceModFuncs();
  LeaveCriticalSection( &rd->csMod );
}

static HMODULE WINAPI new_LoadLibraryA( LPCSTR name )
{
  GET_REMOTEDATA( rd );

  HMODULE mod = rd->fLoadLibraryA( name );

  if( mod ) addLoadedModule( mod );

  return( mod );
}

static HMODULE WINAPI new_LoadLibraryW( LPCWSTR name )
{
  GET_REMOTEDATA( rd );

  HMODULE mod = rd->fLoadLibraryW( name );

  if( mod ) addLoadedModule( mod );

  return( mod );
}

static HMODULE WINAPI new_LoadLibraryExA( LPCSTR name,HANDLE h,DWORD flags )
{
  GET_REMOTEDATA( rd );

  HMODULE mod = rd->fLoadLibraryExA( name,h,flags );

  if( mod && !(flags&(DONT_RESOLVE_DLL_REFERENCES|LOAD_LIBRARY_AS_DATAFILE|
          LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE|LOAD_LIBRARY_AS_IMAGE_RESOURCE)) )
    addLoadedModule( mod );

  return( mod );
}

static HMODULE WINAPI new_LoadLibraryExW( LPCWSTR name,HANDLE h,DWORD flags )
{
  GET_REMOTEDATA( rd );

  HMODULE mod = rd->fLoadLibraryExW( name,h,flags );

  if( mod && !(flags&(DONT_RESOLVE_DLL_REFERENCES|LOAD_LIBRARY_AS_DATAFILE|
          LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE|LOAD_LIBRARY_AS_IMAGE_RESOURCE)) )
    addLoadedModule( mod );

  return( mod );
}

static BOOL WINAPI new_FreeLibrary( HMODULE mod )
{
  GET_REMOTEDATA( rd );

  EnterCriticalSection( &rd->csMod );

  int m;
  for( m=rd->mod_q-1; m>=0 && rd->mod_a[m]!=mod; m-- );

  LeaveCriticalSection( &rd->csMod );

  if( m<0 ) return( rd->fFreeLibrary(mod) );

  if( rd->opt.dlls<=2 ) return( TRUE );

  EnterCriticalSection( &rd->csFreedMod );

  if( rd->inExit )
  {
    LeaveCriticalSection( &rd->csFreedMod );
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
    if( UNLIKELY(!freed_mod_an) )
    {
      LeaveCriticalSection( &rd->csFreedMod );
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

  LeaveCriticalSection( &rd->csFreedMod );

  return( TRUE );
}

static VOID WINAPI new_FreeLibraryAET( HMODULE mod,DWORD exitCode )
{
  new_FreeLibrary( mod );

  ExitThread( exitCode );
}

// }}}
// page protection {{{

static void *protect_alloc_m( size_t s )
{
  GET_REMOTEDATA( rd );

  uintptr_t align = rd->opt.align;
  s += ( align - (s%align) )%align;

  size_t pageAdd = rd->pageAdd;
  DWORD pageSize = rd->pageSize;
  size_t pages = ( s ? (s-1)/pageSize + 1 : 0 ) + pageAdd;

  unsigned char *b = (unsigned char*)VirtualAlloc(
      NULL,pages*pageSize,MEM_RESERVE,PAGE_NOACCESS );
  if( UNLIKELY(!b) )
    return( NULL );

  size_t slackSize = ( pageSize - (s%pageSize) )%pageSize;

  if( rd->opt.protect>1 )
    b += pageSize*pageAdd;

  if( pages>pageAdd )
    VirtualAlloc( b,(pages-pageAdd)*pageSize,MEM_COMMIT,PAGE_READWRITE );

  // initialize slack {{{
  if( slackSize && rd->opt.slackInit>0 )
  {
    unsigned char *slackStart = b;
    if( rd->opt.protect>1 ) slackStart += s;
    size_t count = slackSize>>3;
    ASSUME( count>0 );
    uint64_t *u64 = ASSUME_ALIGNED( (uint64_t*)slackStart,
        MEMORY_ALLOCATION_ALIGNMENT );
    size_t i;
    uint64_t slackInit64 = rd->slackInit64;
    for( i=0; i<count; i++ )
      u64[i] = slackInit64;
  }
  // }}}

  if( rd->opt.protect==1 )
    b += slackSize;

  return( b );
}

static NOINLINE void protect_free_m( void *b,funcType ft )
{
  if( !b ) return;

  GET_REMOTEDATA( rd );

  size_t s = (size_t)TlsGetValue( rd->freeSizeTls );
  if( UNLIKELY(s==(size_t)-1) ) return;

  size_t pageAdd = rd->pageAdd;
  DWORD pageSize = rd->pageSize;
  size_t pages = ( s ? (s-1)/pageSize + 1 : 0 ) + pageAdd;

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
    p -= pageSize*pageAdd;
  }

  // check slack {{{
  if( slackSize && rd->opt.slackInit>=0 )
  {
    size_t count = slackSize>>3;
    ASSUME( count>0 );
    uint64_t *u64 = ASSUME_ALIGNED( (uint64_t*)slackStart,
        MEMORY_ALLOCATION_ALIGNMENT );
    size_t i;
    uint64_t slackInit64 = rd->slackInit64;
    for( i=0; i<count && u64[i]==slackInit64; i++ );
    if( UNLIKELY(i<count) )
    {
      int slackInit = rd->opt.slackInit;
      for( i*=8; i<slackSize && slackStart[i]==slackInit; i++ );

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

        CAPTURE_STACK_TRACE( 3,PTRS,aa[1].frames,NULL,rd->maxStackFrames );
        aa[1].ptr = slackStart + i;
        aa[1].ft = ft;
#ifndef NO_THREADNAMES
        aa[1].threadNameIdx = (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
#endif

        EnterCriticalSection( &rd->csWrite );

        writeMods( aa,2 );

        int type = WRITE_SLACK;
        DWORD written;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );
        WriteFile( rd->master,aa,2*sizeof(allocation),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );

        if( rd->opt.raiseException )
          DebugBreak();
      }
      else
        LeaveCriticalSection( &sa->cs );
    }
  }
  // }}}

  b = (void*)p;

  if( !rd->opt.protectFree )
    VirtualFree( b,0,MEM_RELEASE );
  else
    VirtualFree( b,pages*pageSize,MEM_DECOMMIT );
}

// }}}
// replacements for page protection {{{

static void *protect_malloc( size_t s )
{
  GET_REMOTEDATA( rd );

  void *b = protect_alloc_m( s );
  if( UNLIKELY(!b) ) return( NULL );

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
  size_t res;
  if( UNLIKELY(mul_overflow(n,s,&res)) )
    return( NULL );

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

  size_t os = (size_t)TlsGetValue( rd->freeSizeTls );
  int extern_alloc = os==(size_t)-1;
  if( UNLIKELY(extern_alloc) )
  {
    if( !rd->crtHeap )
      return( NULL );

    os = heap_block_size( rd->crtHeap,b );
    if( os==(size_t)-1 )
      return( NULL );
  }

  void *nb = protect_alloc_m( s );
  if( UNLIKELY(!nb) ) return( NULL );

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
  if( UNLIKELY(!b) ) return( NULL );

  RtlMoveMemory( b,s,l );

  return( b );
}

static wchar_t *protect_wcsdup( const wchar_t *s )
{
  size_t l = lstrlenW( s ) + 1;
  l *= 2;

  wchar_t *b = protect_alloc_m( l );
  if( UNLIKELY(!b) ) return( NULL );

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
  if( LIKELY(cwd_copy) )
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
  if( LIKELY(cwd_copy) )
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
  if( LIKELY(cwd_copy) )
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
  if( LIKELY(cwd_copy) )
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
  if( LIKELY(fp_copy) )
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
  if( LIKELY(fp_copy) )
    RtlMoveMemory( fp_copy,fp,l );

  rd->ofree( fp );

  return( fp_copy );
}

static char *protect_tempnam( char *dir,char *prefix )
{
  GET_REMOTEDATA( rd );
  char *tn = rd->otempnam( dir,prefix );
  if( UNLIKELY(!tn) ) return( tn );

  size_t l = lstrlen( tn ) + 1;

  char *tn_copy = protect_alloc_m( l );
  if( LIKELY(tn_copy) )
    RtlMoveMemory( tn_copy,tn,l );

  rd->ofree( tn );

  return( tn_copy );
}

static wchar_t *protect_wtempnam( wchar_t *dir,wchar_t *prefix )
{
  GET_REMOTEDATA( rd );
  wchar_t *tn = rd->owtempnam( dir,prefix );
  if( UNLIKELY(!tn) ) return( tn );

  size_t l = lstrlenW( tn ) + 1;
  l *= 2;

  wchar_t *tn_copy = protect_alloc_m( l );
  if( LIKELY(tn_copy) )
    RtlMoveMemory( tn_copy,tn,l );

  rd->ofree( tn );

  return( tn_copy );
}

static void protect_free_dbg( void *b,int blockType )
{
  (void)blockType;
  protect_free_m( b,FT_FREE );
}

static void *protect_recalloc( void *b,size_t n,size_t s )
{
  size_t res;
  if( UNLIKELY(mul_overflow(n,s,&res)) )
    return( NULL );

  GET_REMOTEDATA( rd );

  if( !res )
  {
    protect_free_m( b,FT_RECALLOC );
    return( NULL );
  }

  if( !b )
    return( protect_alloc_m(res) );

  size_t os = (size_t)TlsGetValue( rd->freeSizeTls );
  int extern_alloc = os==(size_t)-1;
  if( UNLIKELY(extern_alloc) )
  {
    if( !rd->crtHeap )
      return( NULL );

    os = heap_block_size( rd->crtHeap,b );
    if( os==(size_t)-1 )
      return( NULL );
  }

  void *nb = protect_alloc_m( res );
  if( UNLIKELY(!nb) ) return( NULL );

  size_t cs = os<res ? os : res;
  if( cs )
    RtlMoveMemory( nb,b,cs );

  if( !extern_alloc )
    protect_free_m( b,FT_RECALLOC );

  return( nb );
}

static size_t protect_msize( void *b )
{
  GET_REMOTEDATA( rd );
  size_t s = -1;
  if( b )
    allocSizeAndState( b,&s,1 );
  if( s==(size_t)-1 && rd->crtHeap )
    s = heap_block_size( rd->crtHeap,b );
  return( s );
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
    if( UNLIKELY(!mod_an) )
    {
      EnterCriticalSection( &rd->csWrite );

      DWORD written;
      int type = WRITE_MAIN_ALLOC_FAIL;
      WriteFile( rd->master,&type,sizeof(int),&written,NULL );

      LeaveCriticalSection( &rd->csWrite );

      exitWait( 1,0 );
    }
    rd->mod_a = mod_an;
  }

  rd->mod_a[rd->mod_q++] = mod;

  // crt module {{{
  HANDLE (*fget_heap_handle)( void ) =
    rd->fGetProcAddress( mod,"_get_heap_handle" );
  if( fget_heap_handle )
  {
    if( rd->crt_mod_q>=rd->crt_mod_s )
    {
      rd->crt_mod_s += 8;
      HMODULE *crt_mod_an;
      if( !rd->crt_mod_a )
        crt_mod_an = HeapAlloc(
            rd->heap,0,rd->crt_mod_s*sizeof(HMODULE) );
      else
        crt_mod_an = HeapReAlloc(
            rd->heap,0,rd->crt_mod_a,rd->crt_mod_s*sizeof(HMODULE) );
      if( UNLIKELY(!crt_mod_an) )
      {
        EnterCriticalSection( &rd->csWrite );

        DWORD written;
        int type = WRITE_MAIN_ALLOC_FAIL;
        WriteFile( rd->master,&type,sizeof(int),&written,NULL );

        LeaveCriticalSection( &rd->csWrite );

        exitWait( 1,0 );
      }
      rd->crt_mod_a = crt_mod_an;
    }

    rd->crt_mod_a[rd->crt_mod_q++] = mod;
  }
  // }}}

  // modules of forwarded functions {{{
  if( rd->opt.dlls )
  {
    PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)mod;
    PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );
    if( IMAGE_NT_SIGNATURE==inh->Signature )
    {
      PIMAGE_DATA_DIRECTORY idd =
        &inh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
      if( idd->Size )
      {
        char forwardName[128];
        forwardName[0] = 0;
        int forwardLen = 0;
        DWORD iedStart = idd->VirtualAddress;
        DWORD iedEnd = iedStart + idd->Size;
        PIMAGE_EXPORT_DIRECTORY ied =
          (PIMAGE_EXPORT_DIRECTORY)REL_PTR( idh,iedStart );
        DWORD number = ied->NumberOfFunctions;
        DWORD *functions = (DWORD*)REL_PTR( idh,ied->AddressOfFunctions );
        DWORD i;
        for( i=0; i<number; i++ )
        {
          DWORD f = functions[i];
          if( f<=iedStart || f>=iedEnd ) continue;

          const char *funcName = (const char*)REL_PTR( idh,f );
          const char *point;
          for( point=funcName; *point && *point!='.'; point++ );
          if( *point!='.' || point==funcName ) continue;

          uintptr_t pointPos = point - funcName;
          if( pointPos>=sizeof(forwardName) ) continue;
          if( forwardLen==(int)pointPos && strstart(funcName,forwardName) )
            continue;

          forwardLen = (int)pointPos;
          RtlMoveMemory( forwardName,funcName,forwardLen );
          forwardName[forwardLen] = 0;

          HMODULE forwardMod = GetModuleHandle( forwardName );
          if( forwardMod )
            addModule( forwardMod );
        }
      }
    }
  }
  // }}}
}

static HMODULE replaceFuncs( HMODULE app,
    replaceData *rep,unsigned int count,
    HMODULE msvcrt,int findCRT,HMODULE ucrtbase )
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

    if( msvcrt && curModule!=msvcrt ) continue;

    if( rd->opt.handleException>=2 && !rd->is_cygwin && !rd->mod_d &&
        rd->fGetProcAddress(curModule,"cygwin_stackdump")!=NULL )
      rd->is_cygwin = 1;

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

      if( ucrtbase && curModule!=ucrtbase )
      {
        HMODULE funcMod;
        if( GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
              (LPCSTR)thunk->u1.Function,&funcMod) &&
            funcMod==ucrtbase )
          curModule = ucrtbase;
        else
          break;
      }

      if( findCRT )
      {
        if( rd->fGetProcAddress(curModule,"cygwin_stackdump")!=NULL )
        {
          rd->is_cygwin = 1;
          break;
        }

        ucrtbase = GetModuleHandle( "ucrtbase.dll" );
        if( ucrtbase )
        {
          HMODULE funcMod;
          if( GetModuleHandleEx(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                (LPCSTR)thunk->u1.Function,&funcMod) &&
              funcMod==ucrtbase )
          {
            curModule = ucrtbase;
            rd->ucrtbase = ucrtbase;
          }
          else
            ucrtbase = NULL;
        }
        findCRT = 0;
      }
      if( !repModule )
        repModule = curModule;

      // look for, but don't actually replace the function;
      // used with "exit" to help identify CRT
      if( !myFunc ) continue;

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
  const char *fname_free_dbg = "_free_dbg";
  const char *fname_recalloc = "_recalloc";
  const char *fname_exit = "exit";
  const char *fname_msize = "_msize";
  void *fexit = NULL;
  void *fmsize = NULL;
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
    { fname_free_dbg       ,&rd->ffree_dbg       ,&new_free_dbg        },
    { fname_recalloc       ,&rd->frecalloc       ,&new_recalloc        },
    { fname_exit           ,&fexit               ,NULL                 },
    // needs to be last, only used with page protection
    { fname_msize          ,&fmsize              ,&protect_msize       },
  };
  unsigned int repcount = sizeof(rep)/sizeof(replaceData);
  if( !rd->opt.protect ) repcount--;

  const char *fname_ExitProcess = "ExitProcess";
  const char *fname_TerminateProcess = "TerminateProcess";
#ifndef NO_THREADNAMES
  const char *fname_RaiseException = "RaiseException";
#endif
  const char *fname_CreateProcessA = "CreateProcessA";
  const char *fname_CreateProcessW = "CreateProcessW";
  replaceData rep2[] = {
    { fname_ExitProcess      ,&rd->fExitProcess      ,&new_ExitProcess      },
    { fname_TerminateProcess ,&rd->fTerminateProcess ,&new_TerminateProcess },
#ifndef NO_THREADNAMES
    { fname_RaiseException   ,&rd->fRaiseException   ,&new_RaiseException   },
#endif
    // only used with children hook
    { fname_CreateProcessA   ,&rd->fCreateProcessA   ,&new_CreateProcessA   },
    { fname_CreateProcessW   ,&rd->fCreateProcessW   ,&new_CreateProcessW   },
  };
  unsigned int rep2count = sizeof(rep2)/sizeof(replaceData);
  if( rd->opt.children<1 ) rep2count -= 2;

  const char *fname_LoadLibraryA = "LoadLibraryA";
  const char *fname_LoadLibraryW = "LoadLibraryW";
  const char *fname_LoadLibraryExA = "LoadLibraryExA";
  const char *fname_LoadLibraryExW = "LoadLibraryExW";
  const char *fname_FreeLibrary = "FreeLibrary";
  const char *fname_FreeLibraryAET = "FreeLibraryAndExitThread";
  replaceData repLL[] = {
    { fname_LoadLibraryA   ,&rd->fLoadLibraryA   ,&new_LoadLibraryA    },
    { fname_LoadLibraryW   ,&rd->fLoadLibraryW   ,&new_LoadLibraryW    },
    { fname_LoadLibraryExA ,&rd->fLoadLibraryExA ,&new_LoadLibraryExA  },
    { fname_LoadLibraryExW ,&rd->fLoadLibraryExW ,&new_LoadLibraryExW  },
    { fname_FreeLibrary    ,&rd->fFreeLibrary    ,&new_FreeLibrary     },
    { fname_FreeLibraryAET ,&rd->fFreeLibraryAET ,&new_FreeLibraryAET  },
  };

  HMODULE msvcrt = rd->msvcrt;
  HMODULE ucrtbase = rd->ucrtbase;
  int noCRT = rd->noCRT;
  for( ; rd->mod_d<rd->mod_q; rd->mod_d++ )
  {
    HMODULE mod = rd->mod_a[rd->mod_d];

    HMODULE dll_msvcrt = noCRT ? NULL :
      replaceFuncs( mod,rep,repcount,msvcrt,!rd->mod_d,ucrtbase );
    if( !rd->mod_d && rd->opt.handleException<2 )
    {
      if( !dll_msvcrt && !rd->opt.children )
      {
        rd->master = NULL;
        return;
      }

      if( dll_msvcrt )
      {
        ucrtbase = rd->ucrtbase;
        if( !ucrtbase )
          msvcrt = dll_msvcrt;
        addModule( dll_msvcrt );
      }
      else
      {
        rd->noCRT = noCRT = 2;

        rd->opt.protect = rd->opt.protectFree = rd->opt.leakDetails = 0;
        if( rd->splits )
        {
          int i;
          for( i=0; i<=SPLIT_MASK; i++ )
            DeleteCriticalSection( &rd->splits[i].cs );
          HeapFree( rd->heap,0,rd->splits );
          DeleteCriticalSection( &rd->csAllocId );
          rd->splits = NULL;
        }
        if( rd->freeds )
        {
          int i;
          for( i=0; i<=SPLIT_MASK; i++ )
            DeleteCriticalSection( &rd->freeds[i].cs );
          HeapFree( rd->heap,0,rd->freeds );
          rd->freeds = NULL;
        }
      }

      if( dll_msvcrt && rd->opt.protect )
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

    // global data sections {{{
    if( dll_msvcrt )
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
    // }}}

    unsigned int i;
    for( i=0; i<rep2count; i++ )
      replaceFuncs( mod,rep2+i,1,NULL,0,NULL );

    if( rd->opt.dlls>1 )
      replaceFuncs( mod,repLL,sizeof(repLL)/sizeof(replaceData),NULL,0,NULL );
  }
  rd->msvcrt = msvcrt;
}

// }}}
// exported functions for debugger {{{

static allocation *heob_find_allocation_a( uintptr_t addr,allocation *aa )
{
  GET_REMOTEDATA( rd );

  if( !rd->opt.protect ) return( NULL );

  int protect = rd->opt.protect;
  size_t sizeAdd = rd->pageSize*rd->pageAdd;
  DWORD pageSize = rd->pageSize;

  int i,j;
  splitAllocation *sa;
  for( j=SPLIT_MASK,sa=rd->splits; j>=0; j--,sa++ )
  {
    EnterCriticalSection( &sa->cs );

    for( i=sa->alloc_q-1; i>=0; i-- )
    {
      allocation *a = sa->alloc_a + i;

      uintptr_t ptr = (uintptr_t)a->ptr;
      size_t size = a->size;
      uintptr_t blockStart;
      uintptr_t blockEnd;
      if( protect==1 )
      {
        blockStart = ptr - ( ptr%pageSize );
        blockEnd = ptr + size + sizeAdd;
      }
      else
      {
        blockStart = ptr - sizeAdd;
        blockEnd = ptr + ( size?(size-1)/pageSize+1:0 )*pageSize;
      }

      if( addr>=blockStart && addr<blockEnd )
      {
        RtlMoveMemory( aa,a,sizeof(allocation) );
        LeaveCriticalSection( &sa->cs );
        return( aa );
      }
    }

    LeaveCriticalSection( &sa->cs );
  }

  return( NULL );
}

DLLEXPORT allocation *heob_find_allocation( uintptr_t addr )
{
  GET_REMOTEDATA( rd );

  return( heob_find_allocation_a(addr,&rd->ei->aa[0]) );
}

static allocation *heob_find_freed_a( uintptr_t addr,allocation *aa )
{
  GET_REMOTEDATA( rd );

  if( !rd->opt.protectFree ) return( NULL );

  int protect = rd->opt.protect;
  size_t sizeAdd = rd->pageSize*rd->pageAdd;
  DWORD pageSize = rd->pageSize;

  int i,j;
  splitFreed *sf;
  for( j=SPLIT_MASK,sf=rd->freeds; j>=0; j--,sf++ )
  {
    EnterCriticalSection( &sf->cs );

    for( i=sf->freed_q-1; i>=0; i-- )
    {
      freed *f = sf->freed_a + i;

      uintptr_t ptr = (uintptr_t)f->a.ptr;
      size_t size = f->a.size;
      uintptr_t noAccessStart;
      uintptr_t noAccessEnd;
      if( protect==1 )
      {
        noAccessStart = ptr - ( ptr%pageSize );
        noAccessEnd = ptr + size + sizeAdd;
      }
      else
      {
        noAccessStart = ptr - sizeAdd;
        noAccessEnd = ptr + ( size?(size-1)/pageSize+1:0 )*pageSize;
      }

      if( addr>=noAccessStart && addr<noAccessEnd )
      {
        RtlMoveMemory( &aa[0],&f->a,sizeof(allocation) );
        RtlMoveMemory( &aa[1].frames,&f->frames,PTRS*sizeof(void*) );
        aa[1].ft = f->a.ftFreed;
#ifndef NO_THREADNAMES
        aa[1].threadNameIdx = f->threadNameIdx;
#endif
        LeaveCriticalSection( &sf->cs );
        return( aa );
      }
    }

    LeaveCriticalSection( &sf->cs );
  }

  return( NULL );
}

DLLEXPORT allocation *heob_find_freed( uintptr_t addr )
{
  GET_REMOTEDATA( rd );

  return( heob_find_freed_a(addr,&rd->ei->aa[1]) );
}

static allocation *heob_find_nearest_allocation_a(
    uintptr_t addr,allocation *aa )
{
  GET_REMOTEDATA( rd );

  if( !rd->splits ) return( NULL );

  int i,j;
  splitAllocation *sa;
  uintptr_t nearestPtr = 0;
  allocation *nearestA = NULL;
  CRITICAL_SECTION *nearestCs = NULL;
  for( j=SPLIT_MASK,sa=rd->splits; j>=0; j--,sa++ )
  {
    CRITICAL_SECTION *cs = &sa->cs;
    EnterCriticalSection( cs );

    for( i=sa->alloc_q-1; i>=0; i-- )
    {
      allocation *a = sa->alloc_a + i;

      uintptr_t ptr = (uintptr_t)a->ptr;

      if( addr>=ptr && (!nearestPtr || ptr>nearestPtr) &&
          addr-ptr<INTPTR_MAX )
      {
        if( nearestCs!=cs )
        {
          if( nearestCs )
            LeaveCriticalSection( nearestCs );
          nearestCs = cs;
        }
        nearestPtr = ptr;
        nearestA = a;
      }
    }

    if( nearestCs!=cs )
      LeaveCriticalSection( cs );
  }

  if( nearestA )
  {
    RtlMoveMemory( aa,nearestA,sizeof(allocation) );
    LeaveCriticalSection( nearestCs );
    return( aa );
  }

  return( NULL );
}

DLLEXPORT allocation *heob_find_nearest_allocation( uintptr_t addr )
{
  GET_REMOTEDATA( rd );

  return( heob_find_nearest_allocation_a(addr,&rd->ei->aa[0]) );
}

static allocation *heob_find_nearest_freed_a( uintptr_t addr,allocation *aa )
{
  GET_REMOTEDATA( rd );

  if( !rd->opt.protectFree ) return( NULL );

  int i,j;
  uintptr_t nearestPtr = 0;
  freed *nearestF = NULL;
  CRITICAL_SECTION *nearestCs = NULL;
  splitFreed *sf;
  for( j=SPLIT_MASK,sf=rd->freeds; j>=0; j--,sf++ )
  {
    CRITICAL_SECTION *cs = &sf->cs;
    EnterCriticalSection( cs );

    for( i=sf->freed_q-1; i>=0; i-- )
    {
      freed *f = sf->freed_a + i;

      uintptr_t ptr = (uintptr_t)f->a.ptr;

      if( addr>=ptr && (!nearestPtr || ptr>nearestPtr) &&
          addr-ptr<INTPTR_MAX )
      {
        if( nearestCs!=cs )
        {
          if( nearestCs )
            LeaveCriticalSection( nearestCs );
          nearestCs = cs;
        }
        nearestPtr = ptr;
        nearestF = f;
      }
    }

    if( nearestCs!=cs )
      LeaveCriticalSection( cs );
  }

  if( nearestF )
  {
    RtlMoveMemory( &aa[0],&nearestF->a,sizeof(allocation) );
    RtlMoveMemory( &aa[1].frames,&nearestF->frames,PTRS*sizeof(void*) );
    aa[1].ft = nearestF->a.ftFreed;
#ifndef NO_THREADNAMES
    aa[1].threadNameIdx = nearestF->threadNameIdx;
#endif
    LeaveCriticalSection( nearestCs );
    return( aa );
  }

  return( NULL );
}

DLLEXPORT allocation *heob_find_nearest_freed( uintptr_t addr )
{
  GET_REMOTEDATA( rd );

  return( heob_find_nearest_freed_a(addr,&rd->ei->aa[1]) );
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
#define THROW_ARGS 4
#define CALC_THROW_ARG(mod,ofs) ((char*)(mod)+(ofs))
#else
#define csp Esp
#define cip Eip
#define cfp Ebp
#if USE_STACKWALK
#define MACH_TYPE IMAGE_FILE_MACHINE_I386
#endif
#define THROW_ARGS 3
#define CALC_THROW_ARG(mod,ofs) (ofs)
#endif
static LONG WINAPI exceptionWalker( LPEXCEPTION_POINTERS ep )
{
  GET_REMOTEDATA( rd );

  exceptionInfo *eiPtr = rd->ei;
#define ei (*eiPtr)

  ei.aq = 1;
  ei.nearest = 0;
  DWORD ec = ep->ExceptionRecord->ExceptionCode;

  // access violation {{{
  if( ec==EXCEPTION_ACCESS_VIOLATION &&
      ep->ExceptionRecord->NumberParameters==2 )
  {
    uintptr_t addr = ep->ExceptionRecord->ExceptionInformation[1];

    allocation *a = heob_find_allocation_a( addr,&ei.aa[1] );
    if( a )
    {
      ei.aq++;
    }
    else
    {
      allocation *af = heob_find_freed_a( addr,&ei.aa[1] );
      if( af )
      {
        ei.aq += 2;
      }
      else if( rd->opt.findNearest )
      {
        a = heob_find_nearest_allocation_a( addr,&ei.aa[0] );
        af = heob_find_nearest_freed_a( addr,&ei.aa[1] );
        if( a && (!af || a->ptr>af->ptr) )
        {
          RtlMoveMemory( &ei.aa[1],&ei.aa[0],sizeof(allocation) );
          ei.aq++;
          ei.nearest = 1;
        }
        else if( af )
        {
          ei.aq += 2;
          ei.nearest = 2;
        }
      }
    }
  }
  // }}}
  // VC c++ exception {{{
  else if( ec==EXCEPTION_VC_CPP_EXCEPTION &&
      ep->ExceptionRecord->NumberParameters==THROW_ARGS )
  {
    DWORD *ptr = (DWORD*)ep->ExceptionRecord->ExceptionInformation[2];
#ifdef _WIN64
    char *mod = (char*)ep->ExceptionRecord->ExceptionInformation[3];
#endif
    if( !IsBadReadPtr(ptr,4*sizeof(DWORD)) )
    {
      ptr = (DWORD*)CALC_THROW_ARG( mod,ptr[3] );
      if( !IsBadReadPtr(ptr,2*sizeof(DWORD)) )
      {
        ptr = (DWORD*)CALC_THROW_ARG( mod,ptr[1] );
        if( !IsBadReadPtr(ptr,2*sizeof(DWORD)) )
        {
          char *exception_type =
            (char*)CALC_THROW_ARG( mod,ptr[1] ) + 2*sizeof(void*);
          if( !IsBadReadPtr(exception_type,sizeof(void*)) )
          {
            size_t l = lstrlen( exception_type );
            if( l>=sizeof(ei.throwName) ) l = sizeof(ei.throwName) - 1;
            RtlMoveMemory( ei.throwName,exception_type,l );
            ei.throwName[l] = 0;
          }
        }
      }
    }
  }
  // }}}

  int count = 0;
  void **frames = ei.aa[0].frames;
#ifndef NO_THREADNAMES
  ei.aa[0].threadNameIdx = (int)(uintptr_t)TlsGetValue( rd->threadNameTls );
#endif

  // stackwalk with dbghelp {{{
#if USE_STACKWALK
  HMODULE symMod = NULL;
  if( ec!=EXCEPTION_STACK_OVERFLOW )
    symMod = rd->fLoadLibraryA( "dbghelp.dll" );
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
      if( !count ) frame++;
      else if( !frame ) break;
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
  // }}}
  // manual stackwalk {{{
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
  // }}}
  if( count<PTRS )
    RtlZeroMemory( frames+count,(PTRS-count)*sizeof(void*) );

#if USE_STACKWALK
  if( symMod )
    rd->fFreeLibrary( symMod );
#endif

  RtlMoveMemory( &ei.er,ep->ExceptionRecord,sizeof(EXCEPTION_RECORD) );
  RtlMoveMemory( &ei.c,ep->ContextRecord,sizeof(CONTEXT) );

  EnterCriticalSection( &rd->csWrite );

  writeMods( ei.aa,ei.aq );

  int type = WRITE_EXCEPTION;
  DWORD written;
  WriteFile( rd->master,&type,sizeof(int),&written,NULL );
  WriteFile( rd->master,&ei,sizeof(exceptionInfo),&written,NULL );

  LeaveCriticalSection( &rd->csWrite );

#undef ei

  if( ec==EXCEPTION_BREAKPOINT )
  {
    ep->ContextRecord->cip++;
    return( EXCEPTION_CONTINUE_EXECUTION );
  }

#ifndef NO_DBGENG
  if( rd->exceptionWait )
    WaitForSingleObject( rd->exceptionWait,1000 );
#endif

  exitWait( 1,ec==EXCEPTION_STACK_OVERFLOW ? 2 : 1 );

  return( EXCEPTION_EXECUTE_HANDLER );
}

// }}}
// get type/path of standard device {{{

static int getHandleName( HANDLE h,wchar_t *buf,int buflen,HANDLE heap )
{
  if( !h || h==INVALID_HANDLE_VALUE ) return( 0 );

  DWORD flags;
  if( GetConsoleMode(h,&flags) )
  {
    lstrcpyW( buf,L"console" );
    return( 1 );
  }

  GET_REMOTEDATA( rd );
  typedef DWORD WINAPI func_GetFinalPathNameByHandleW(
      HANDLE,LPWSTR,DWORD,DWORD );
  func_GetFinalPathNameByHandleW *fGetFinalPathNameByHandleW =
    rd->fGetProcAddress( rd->kernel32,"GetFinalPathNameByHandleW" );
  if( fGetFinalPathNameByHandleW &&
      fGetFinalPathNameByHandleW(h,buf,buflen,VOLUME_NAME_DOS) )
    return( 1 );

  HMODULE ntdll = GetModuleHandle( "ntdll.dll" );
  if( !ntdll ) return( 0 );

  func_NtQueryObject *fNtQueryObject =
    rd->fGetProcAddress( ntdll,"NtQueryObject" );
  if( !fNtQueryObject ) return( 0 );

  OBJECT_NAME_INFORMATION *oni =
    HeapAlloc( heap,0,sizeof(OBJECT_NAME_INFORMATION) );
  if( !oni ) return( 0 );

  ULONG len;
  int ret = 0;
  if( !fNtQueryObject(h,ObjectNameInformation,
        oni,sizeof(OBJECT_NAME_INFORMATION),&len) )
  {
    int half = oni->Name.Length/2;
    if( half>0 && half<buflen )
    {
      oni->Name.Buffer[half] = 0;
      lstrcpyW( buf,oni->Name.Buffer );

      if( lstrcmpW(buf,L"\\Device\\Null") )
        ret = 1;
    }
  }
  else if( GetFileType(h)==FILE_TYPE_PIPE )
  {
    lstrcpyW( buf,L"pipe" );
    ret = 1;
  }
  HeapFree( heap,0,oni );
  return( ret );
}

// }}}
// control leak recording {{{

DLLEXPORT int heob_control( int cmd )
{
  GET_REMOTEDATA( rd );

  if( rd->noCRT ) return( -rd->noCRT );

  int prevRecording = rd->recording;

  switch( cmd )
  {
    // stop/start {{{
    case HEOB_LEAK_RECORDING_STOP:
    case HEOB_LEAK_RECORDING_START:
      rd->recording = cmd;
      break;
      // }}}

      // clear {{{
    case HEOB_LEAK_RECORDING_CLEAR:
      {
        int i;
        for( i=0; i<=SPLIT_MASK; i++ )
        {
          int j;
          splitAllocation *sa = rd->splits + i;

          EnterCriticalSection( &sa->cs );

          int alloc_q = sa->alloc_q;
          allocation *alloc_a = sa->alloc_a;
          for( j=0; j<alloc_q; j++ )
            alloc_a[j].recording = 0;

          LeaveCriticalSection( &sa->cs );
        }
      }
      break;
      // }}}

      // show {{{
    case HEOB_LEAK_RECORDING_SHOW:
      {
        int i;
        EnterCriticalSection( &rd->csWrite );
        for( i=0; i<=SPLIT_MASK; i++ )
          EnterCriticalSection( &rd->splits[i].cs );

        writeLeakMods( NULL );
        writeLeakData();

        LeaveCriticalSection( &rd->csWrite );

        for( i=0; i<=SPLIT_MASK; i++ )
        {
          int j;
          splitAllocation *sa = rd->splits + i;
          int alloc_q = sa->alloc_q;
          allocation *alloc_a = sa->alloc_a;
          for( j=0; j<alloc_q; j++ )
            alloc_a[j].recording = 0;

          LeaveCriticalSection( &rd->splits[i].cs );
        }
      }
      break;
      // }}}

      // count {{{
    case HEOB_LEAK_COUNT:
      {
        int i,j;
        int count = 0;
        for( i=0; i<=SPLIT_MASK; i++ )
        {
          EnterCriticalSection( &rd->splits[i].cs );

          splitAllocation *sa = rd->splits + i;
          int alloc_q = sa->alloc_q;
          allocation *alloc_a = sa->alloc_a;
          for( j=0; j<alloc_q; j++ )
            if( alloc_a[j].recording ) count++;

          LeaveCriticalSection( &rd->splits[i].cs );
        }
        return( count );
      }
      // }}}
  }

  if( cmd>=HEOB_LEAK_RECORDING_STOP && cmd<=HEOB_LEAK_RECORDING_SHOW )
  {
    EnterCriticalSection( &rd->csWrite );

    int type = WRITE_RECORDING;
    DWORD written;
    WriteFile( rd->master,&type,sizeof(int),&written,NULL );
    WriteFile( rd->master,&cmd,sizeof(int),&written,NULL );

    LeaveCriticalSection( &rd->csWrite );
  }

  return( prevRecording );
}

static DWORD WINAPI controlThread( LPVOID arg )
{
  HANDLE controlPipe = arg;

  int cmd;
  DWORD didread;
  while( ReadFile(controlPipe,&cmd,sizeof(int),&didread,NULL) )
    heob_control( cmd );

  return( 0 );
}

// }}}
// injected main {{{

VOID CALLBACK heob( ULONG_PTR arg )
{
  remoteData *rd = (remoteData*)arg;
  HMODULE app = rd->heobMod;
  PIMAGE_DOS_HEADER idh = (PIMAGE_DOS_HEADER)app;
  PIMAGE_NT_HEADERS inh = (PIMAGE_NT_HEADERS)REL_PTR( idh,idh->e_lfanew );

  // base relocation {{{
  {
    PIMAGE_OPTIONAL_HEADER ioh = (PIMAGE_OPTIONAL_HEADER)REL_PTR(
        inh,sizeof(DWORD)+sizeof(IMAGE_FILE_HEADER) );
    size_t imageBase = ioh->ImageBase;
    if( imageBase!=(size_t)app )
    {
      size_t baseOfs = (size_t)app - imageBase;

      PIMAGE_DATA_DIRECTORY idd =
        &inh->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
      DWORD iddSize = idd->Size;
      if( iddSize>0 )
      {
        PIMAGE_BASE_RELOCATION ibr =
          (PIMAGE_BASE_RELOCATION)REL_PTR( idh,idd->VirtualAddress );
        while( iddSize && iddSize>=ibr->SizeOfBlock && ibr->VirtualAddress>0 )
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

          iddSize -= ibr->SizeOfBlock;
          ibr = (PIMAGE_BASE_RELOCATION)REL_PTR( ibr,ibr->SizeOfBlock );
        }
      }
    }
  }
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
  localData *ld = &g_ld;

  RtlMoveMemory( &ld->opt,&rd->opt,sizeof(options) );
  RtlMoveMemory( &ld->globalopt,&rd->globalopt,sizeof(options) );
  if( rd->specificOptions )
  {
    ld->specificOptions = HeapAlloc( heap,0,lstrlen(rd->specificOptions)+1 );
    lstrcpy( ld->specificOptions,rd->specificOptions );
  }
  ld->slackInit64 = rd->opt.slackInit;
  ld->slackInit64 |= ld->slackInit64<<8;
  ld->slackInit64 |= ld->slackInit64<<16;
  ld->slackInit64 |= ld->slackInit64<<32;
  ld->fLoadLibraryA = rd->fGetProcAddress( rd->kernel32,"LoadLibraryA" );
  ld->fLoadLibraryW = rd->fLoadLibraryW;
  ld->fFreeLibrary = rd->fGetProcAddress( rd->kernel32,"FreeLibrary" );
  ld->fGetProcAddress = rd->fGetProcAddress;
  ld->fExitProcess = rd->fGetProcAddress( rd->kernel32,"ExitProcess" );
  ld->fTerminateProcess =
    rd->fGetProcAddress( rd->kernel32,"TerminateProcess" );
  ld->fCreateProcessW = rd->fGetProcAddress( rd->kernel32,"CreateProcessW" );
  ld->master = rd->master;
  ld->controlPipe = rd->controlPipe;
#ifndef NO_DBGENG
  ld->exceptionWait = rd->exceptionWait;
#endif
  ld->heobMod = rd->heobMod;
  ld->kernel32 = rd->kernel32;
  ld->recording = rd->recording;
  HANDLE controlPipe = rd->controlPipe;
  ld->noCRT = ld->opt.handleException>=2;

  ld->heap = heap;

  SYSTEM_INFO si;
  GetSystemInfo( &si );
  ld->pageSize = si.dwPageSize;
  ld->pageAdd = ( rd->opt.minProtectSize+(ld->pageSize-1) )/ld->pageSize;
  ld->processors = si.dwNumberOfProcessors;
  ld->ei = HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(exceptionInfo) );
  ld->maxStackFrames = LOBYTE(LOWORD(GetVersion()))>=6 ? 1024 : 62;

  lstrcpy( ld->subOutName,rd->subOutName );
  lstrcpy( ld->subXmlName,rd->subXmlName );
  lstrcpyW( ld->subCurDir,rd->subCurDir );

  if( !ld->noCRT )
    ld->splits = HeapAlloc( heap,HEAP_ZERO_MEMORY,
        (SPLIT_MASK+1)*sizeof(splitAllocation) );
  if( rd->opt.protectFree )
    ld->freeds = HeapAlloc( heap,HEAP_ZERO_MEMORY,
        (SPLIT_MASK+1)*sizeof(splitFreed) );

  // initialize critical sections {{{
  typedef BOOL WINAPI func_InitializeCriticalSectionEx(
      LPCRITICAL_SECTION,DWORD,DWORD );
  func_InitializeCriticalSectionEx *fInitCritSecEx =
    rd->fGetProcAddress( rd->kernel32,"InitializeCriticalSectionEx" );
  if( fInitCritSecEx )
  {
    fInitCritSecEx( &ld->csMod,4000,CRITICAL_SECTION_NO_DEBUG_INFO );
    fInitCritSecEx( &ld->csWrite,4000,CRITICAL_SECTION_NO_DEBUG_INFO );
    fInitCritSecEx( &ld->csFreedMod,4000,CRITICAL_SECTION_NO_DEBUG_INFO );
    int i;
    if( ld->splits )
    {
      fInitCritSecEx( &ld->csAllocId,4000,CRITICAL_SECTION_NO_DEBUG_INFO );
      for( i=0; i<=SPLIT_MASK; i++ )
      {
        fInitCritSecEx( &ld->splits[i].cs,
            4000,CRITICAL_SECTION_NO_DEBUG_INFO );
        if( rd->opt.protectFree )
          fInitCritSecEx( &ld->freeds[i].cs,
              4000,CRITICAL_SECTION_NO_DEBUG_INFO );
      }
    }
  }
  else
  {
    InitializeCriticalSection( &ld->csMod );
    InitializeCriticalSection( &ld->csWrite );
    InitializeCriticalSection( &ld->csFreedMod );
    int i;
    if( ld->splits )
    {
      InitializeCriticalSection( &ld->csAllocId );
      for( i=0; i<=SPLIT_MASK; i++ )
      {
        InitializeCriticalSection( &ld->splits[i].cs );
        if( rd->opt.protectFree )
          InitializeCriticalSection( &ld->freeds[i].cs );
      }
    }
  }
  // }}}

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

  ld->cur_id = ld->raise_id = 0;
  if( rd->raise_alloc_q && !ld->noCRT )
  {
    ld->raise_alloc_a = HeapAlloc(
        heap,0,(rd->raise_alloc_q+1)*sizeof(size_t) );
    RtlMoveMemory( ld->raise_alloc_a,
        rd->raise_alloc_a,rd->raise_alloc_q*sizeof(size_t) );
    ld->raise_alloc_a[rd->raise_alloc_q] = 0;
    ld->raise_id = *(ld->raise_alloc_a++);
  }

#ifndef NO_THREADNAMES
  HMODULE ntdll = GetModuleHandle( "ntdll.dll" );
  if( ntdll )
    ld->fNtQueryInformationThread = rd->fGetProcAddress(
        ntdll,"NtQueryInformationThread" );
  ld->threadNameTls = TlsAlloc();
#endif

  ld->freeSizeTls = TlsAlloc();

  // page protection {{{
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
    ld->ffree_dbg = &protect_free_dbg;
    ld->frecalloc = &protect_recalloc;
  }
  // }}}

  // handle exceptions {{{
  if( rd->opt.handleException )
  {
    func_SetUnhandledExceptionFilter *fSetUnhandledExceptionFilter =
      rd->fGetProcAddress( rd->kernel32,"SetUnhandledExceptionFilter" );
    fSetUnhandledExceptionFilter( &exceptionWalker );

    void *fp = fSetUnhandledExceptionFilter;
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
  // }}}

  HMODULE appMod = GetModuleHandle( NULL );
  if( appMod==rd->heobMod && ld->opt.children )
    ld->opt.children = -1;
  addModule( appMod );
  replaceModFuncs();

  GetModuleFileNameW( NULL,rd->exePath,MAX_PATH );
  rd->master = ld->master;
  rd->noCRT = ld->noCRT;

  // attached process info {{{
  if( ld->master && rd->opt.attached )
  {
    attachedProcessInfo *api = rd->api =
      HeapAlloc( heap,HEAP_ZERO_MEMORY,sizeof(attachedProcessInfo) );
    // cygwin command line {{{
    if( ld->is_cygwin )
    {
      STARTUPINFO startup;
      RtlZeroMemory( &startup,sizeof(STARTUPINFO) );
      startup.cb = sizeof(STARTUPINFO);
      GetStartupInfo( &startup );
      typedef struct
      {
        int argc;
        const char **argv;
      }
      cygheap_exec_info;
      typedef struct
      {
        uint32_t padding1[4];
        unsigned short type;
        void *padding2[9];
        uint32_t padding3[6];
        cygheap_exec_info *moreinfo;
      }
      cyg_child_info;
      cyg_child_info *ci = (cyg_child_info*)startup.lpReserved2;
      int type = 0;
      if( startup.cbReserved2>=sizeof(cyg_child_info) &&
          ci && (ci->type>=1 || ci->type<=3) )
        type = ci->type;
      if( (type==1 || type==2) &&
          ci->moreinfo && ci->moreinfo->argc && ci->moreinfo->argv )
      {
        int argc = ci->moreinfo->argc;
        const char **argv = ci->moreinfo->argv;
        wchar_t *cmdLine = api->commandLine;
        wchar_t *cmdLineEnd = cmdLine + 32768 - 1;
        int i;
        for( i=0; i<argc && cmdLine<cmdLineEnd; i++ )
        {
          const char *arg_i = argv[i];
          int arglen = lstrlen( arg_i );
          arglen = MultiByteToWideChar(
              CP_UTF8,0,arg_i,arglen,cmdLine,(int)(cmdLineEnd-cmdLine) );
          cmdLine += arglen;
          cmdLine++[0] = 0;
        }
        api->type = type;
        api->cyg_argc = i;
      }
      else if( type==3 )
        api->type = type;
    }
    // }}}
    if( !api->commandLine[0] )
      lstrcpyW( api->commandLine,GetCommandLineW() );
    if( !GetCurrentDirectoryW(MAX_PATH,api->currentDirectory) )
      api->currentDirectory[0] = 0;
    if( !getHandleName(GetStdHandle(STD_INPUT_HANDLE),
          api->stdinName,32768,heap) )
      api->stdinName[0] = 0;
    if( !getHandleName(GetStdHandle(STD_OUTPUT_HANDLE),
          api->stdoutName,32768,heap) )
      api->stdoutName[0] = 0;
    if( !getHandleName(GetStdHandle(STD_ERROR_HANDLE),
          api->stderrName,32768,heap) )
      api->stderrName[0] = 0;
  }
  // }}}

  rd->recordingRemote = &ld->recording;

  HANDLE initFinished = rd->initFinished;
  SetEvent( initFinished );
  CloseHandle( initFinished );

  HANDLE startMain = rd->startMain;
  WaitForSingleObject( startMain,INFINITE );
  CloseHandle( startMain );

  if( rd->api )
    HeapFree( heap,0,rd->api );
  VirtualFree( rd,0,MEM_RELEASE );

  if( controlPipe )
  {
    HANDLE thread = CreateThread( NULL,0,&controlThread,controlPipe,0,NULL );
    CloseHandle( thread );
  }
}

// }}}

// vim:fdm=marker
