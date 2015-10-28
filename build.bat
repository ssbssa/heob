
echo %lib% |find /i "lib\amd64" >NUL
if errorlevel 1 (
  set bits=32
) else (
  set bits=64
)

if not exist obj%bits% mkdir obj%bits%

if not defined DWSTFLAGS set DWSTFLAGS=/D NO_DWARFSTACK
if not defined HEOBVER set HEOBVER=/D "HEOB_VER=\"vc-dev\""

set CFLAGS=/GS- /W3 /Gy- /Zc:wchar_t %DWSTFLAGS% /Gm- /O2 /Ob0 /fp:precise /D "NDEBUG" /D "_CONSOLE" %HEOBVER% /D "_MBCS" /errorReport:prompt /GF- /WX- /Zc:forScope /GR- /Gd /Oy- /Oi /MD /openmp- /nologo /Fo"obj%bits%\\" /Ot
set LDFLAGS=/NXCOMPAT /DYNAMICBASE "kernel32.lib" /OPT:REF /INCREMENTAL:NO /SUBSYSTEM:CONSOLE /OPT:ICF /ERRORREPORT:PROMPT /NOLOGO /NODEFAULTLIB /TLBID:1

cl /c %CFLAGS% heob.c
if errorlevel 1 goto error

cl /c %CFLAGS% heob-inj.c
if errorlevel 1 goto error

link /OUT:"heob%bits%.exe" %LDFLAGS% /IMPLIB:obj%bits%\heob.lib obj%bits%\heob.obj obj%bits%\heob-inj.obj
if errorlevel 1 goto error
goto eof

:error
pause

:eof
