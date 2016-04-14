@echo off

echo "%lib%" |find /i "lib\amd64" >NUL
if errorlevel 1 (
  set bits=32

  echo "%include%" |find /i "include\shared" >NUL
  if errorlevel 1 (
    set vc6lib=crt32-vc6.lib
  )
) else (
  set bits=64
)

cl /O2 /Oy- dll-alloc.cpp -Fodll-alloc%bits%.o -Fedll-alloc%bits%.dll /LD /MD /Zi
copy dll-alloc%bits%.dll dll-alloc-shared%bits%.dll

if "%vc6lib%" == "crt32-vc6.lib" (
  lib /def:crt32-vc6.def /out:crt32-vc6.lib
)

cl /O2 /Oy- allocer.cpp -Foallocer%bits%.o -Feallocer%bits%.exe /MD /Zi %vc6lib% dll-alloc%bits%.lib

:eof
