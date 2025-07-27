heob - heap observer
====================

[![build](https://github.com/ssbssa/heob/actions/workflows/build.yml/badge.svg?branch=master)](https://github.com/ssbssa/heob/actions/workflows/build.yml?query=branch%3Amaster)
[![latest build](logo-16.png)](https://ci.appveyor.com/api/projects/ssbssa/heob/artifacts/heob.7z?branch=master "latest AppVeyor artifact")

heob overrides the heap functions of the called process to detect
buffer overruns and memory leaks.

On buffer overruns an access violation is raised, and stacktraces
of the offending instruction and buffer allocation are provided.

When the program exits normally, stacktraces for all leaks are shown.


## compilation:

### MinGW

The location of dwarfstack.h has to be provided.

    make CPPFLAGS="-I../dwarfstack/include"

Or disable dwarfstack completely (this is the default).

    make CPPFLAGS="-DNO_DWARFSTACK"

### MSVC

Run `build.bat` in the source directory.

## notes:

To get file/line information in stacktraces of executables with
DWARF debug information (gcc), dwarfstack.dll needs to be available.
For PDB debug information, dbghelp.dll is used.


## examples:

### memory leaks

Show all unfreed memory at target exit as flame graph in `leaks.svg`.

    heob64 -vleaks.svg -p0 TARGET-EXE-PLUS-ARGUMENTS

With `-k1` or `-k2` it's possible to interactively enable/disable leak
recording, or write all currently recorded unfreed memory at any time before
target exit.

    heob64 -vleaks.svg -p0 -k1 TARGET-EXE-PLUS-ARGUMENTS

### heap check

Usually when looking for heap overflow or similar errors, the memory leak
output is not desired, so disable it with `-l0`.

Check for heap buffer overflow.

    heob64 -p1 -l0 TARGET-EXE-PLUS-ARGUMENTS

Check for heap buffer underflow.

    heob64 -p2 -l0 TARGET-EXE-PLUS-ARGUMENTS

Check for use-after-free of heap buffers (needs also `-p1` or `-p2` enabled
to work).

    heob64 -p1 -f1 -l0 TARGET-EXE-PLUS-ARGUMENTS

These heap check options work by reserving extra unaccessible pages
after/before all buffer allocations to detect overflow/underflow, which
needs a lot of address space, so works (for any non-trivial program) only
with 64bit executables.

To get the heob output in a file instead of the console, use `-o`, and if
the output file name ends in `.html`, it will also be colored.
In that case I also suggest enabling full exception details with `-D15` and
full paths with `-F1`.

    heob64 -p1 -f1 -l0 -ocrash.html -D15 -F1 TARGET-EXE-PLUS-ARGUMENTS

### profiling

Show sampling profiler result as flame graph in `prof.svg`.

    heob64 -vprof.svg -I10 TARGET-EXE-PLUS-ARGUMENTS

Similar to memory leaks, this can also be interactively controlled with
`-k1` or `-k2`.

    heob64 -vprof.svg -I10 -k1 TARGET-EXE-PLUS-ARGUMENTS

### sub-processes

It's possible to automatically inject heob in all subprocesses if either
`%p` or `%c` is part of the `-o` file name.
Option `-h2` disables all memory leak and heap check functionality, so this
just gives an output file for each process and sub-process started by
the target executable.

    heob64 -oprocs-%c-%n-%p-%N-%P.html -h2 TARGET-EXE-PLUS-ARGUMENTS

### dll dependencies

If a target program can't be started because of some dll dependencies
problem (like missing exported functions), heob will give the exact reason.
But it's also possible to check for this explicitely, without running any
target code, and which works for dll's as well.

    heob64 -Y1 SOME-EXE-OR-DLL

Or list all exported and imported symbols.

    heob64 -Y2 SOME-EXE-OR-DLL

### minidumps

Show exception information (including stacktrace) of minidump.

    heob64 -D15 -F1 SOME-MINIDUMP

Create minidump of a running process.

    heob64 -#PID


## code signing:

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate by [SignPath Foundation](https://signpath.org)
