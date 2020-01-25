
//          Copyright Hannes Domani 2018-2020.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#ifndef __HEOB_H__
#define __HEOB_H__

#define WIN32_LEAN_AND_MEAN
#include <windows.h>


// input commands for heob_control()
enum
{
  // stop leak recording
  HEOB_LEAK_RECORDING_STOP,
  // start leak recording
  HEOB_LEAK_RECORDING_START,
  // clear all recorded leaks
  HEOB_LEAK_RECORDING_CLEAR,
  // show all recorded leaks
  HEOB_LEAK_RECORDING_SHOW,

  // return if leak recording is enabled
  HEOB_LEAK_RECORDING_STATE,
  // return number of recorded leaks
  HEOB_LEAK_COUNT,
};

// error return values of heob_control()
enum
{
  // heob was configured to handle exceptions only
  HEOB_HANDLE_EXCEPTIONS_ONLY = -1,
  // target process doesn't use a CRT
  HEOB_NO_CRT_FOUND = -2,
  // invalid command
  HEOB_INVALID_CMD = -3,

  // process was not started with heob
  HEOB_NOT_FOUND = -1024,
};


#ifndef HEOB_INTERNAL

#ifndef _WIN64
#define HEOB_BITS "32"
#else
#define HEOB_BITS "64"
#endif

typedef int func_heob_control( int );

static inline int heob_control( int cmd )
{
  HMODULE heob = GetModuleHandleA( "heob" HEOB_BITS ".exe" );
  func_heob_control *fheob_control = heob ?
    (func_heob_control*)GetProcAddress( heob,"heob_control" ) : NULL;
  if( !fheob_control )
    return( HEOB_NOT_FOUND );

  return( fheob_control(cmd) );
}

#endif

#endif
