
echo "%lib%" |find /i "lib\amd64" >NUL
if errorlevel 1 (
  echo "%lib%" |find /i "lib\x64" >NUL
  if errorlevel 1 (
    set bits=32
  ) else (
    set bits=64
  )
) else (
  set bits=64
)

if not exist obj%bits% mkdir obj%bits%

if defined HEOB_VERSION set HEOB_VERSION_RC=/d HEOB_VER_STR=\"%HEOB_VERSION%\"
if defined HEOB_VER_NUM set HEOB_VER_NUM_RC=/d HEOB_VER_NUM=%HEOB_VER_NUM%
if defined HEOB_PRERELEASE set HEOB_PRERELEASE_RC=/d HEOB_PRERELEASE=%HEOB_PRERELEASE%
if defined HEOB_COPYRIGHT_YEARS set HEOB_COPYRIGHT_YEARS_RC=/d HEOB_COPYRIGHT_YEARS=\"%HEOB_COPYRIGHT_YEARS%\"
set RCFLAGS=%HEOB_VERSION_RC% %HEOB_VER_NUM_RC% %HEOB_PRERELEASE_RC% %HEOB_COPYRIGHT_YEARS_RC%

if not defined DWSTFLAGS set DWSTFLAGS=/D NO_DWARFSTACK
if not defined HEOB_VERSION set HEOB_VERSION=vc-dev
set HEOBVER=/D "HEOB_VER=\"%HEOB_VERSION%\""

set CFLAGS=/GS- /W3 /Gy- /Zc:wchar_t %DWSTFLAGS% /Gm- /O2 /Ob0 /fp:precise /D "NDEBUG" /D "_CONSOLE" %HEOBVER% /D "_MBCS" /errorReport:prompt /GF- /WX /Zc:forScope /GR- /Gd /Oy- /Oi /MD /openmp- /nologo /Fo"obj%bits%\\" /Ot /wd4996
set LDFLAGS=/NXCOMPAT /DYNAMICBASE "kernel32.lib" /OPT:REF /INCREMENTAL:NO /SUBSYSTEM:CONSOLE /OPT:ICF /ERRORREPORT:PROMPT /NOLOGO /NODEFAULTLIB /TLBID:1

rc %RCFLAGS% /foobj%bits%\heob-ver.res heob-ver.rc
if errorlevel 1 goto error

cl /c %CFLAGS% heob.c
if errorlevel 1 goto error

cl /c %CFLAGS% heob-inj.c
if errorlevel 1 goto error

link /OUT:"heob%bits%.exe" %LDFLAGS% /IMPLIB:obj%bits%\heob.lib obj%bits%\heob.obj obj%bits%\heob-inj.obj obj%bits%\heob-ver.res
if errorlevel 1 goto error
goto eof

:error

:eof
