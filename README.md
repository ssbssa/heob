heob - heap observer
====================

[![build status](https://ci.appveyor.com/api/projects/status/github/ssbssa/heob?branch=master&svg=true)](https://ci.appveyor.com/project/ssbssa/heob/branch/master)
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
