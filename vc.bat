
echo %lib% |find /i "lib\amd64" >NUL
if errorlevel 1 (
  set bits=32
) else (
  set bits=64
)

cl dll-alloc.c -Fodll-alloc%bits%.o -Fedll-alloc%bits%.dll /LD /MD /Zi
copy dll-alloc%bits%.dll dll-alloc-shared%bits%.dll

if "%1" == "vc6" goto vc6

cl allocer.cpp -Foallocer%bits%.o -Feallocer%bits%.exe /MD /Zi dll-alloc%bits%.lib
mt.exe -manifest allocer.exe.manifest -outputresource:allocer%bits%.exe;1
goto eof

:vc6
lib /def:crt32-vc6.def /out:crt32-vc6.lib
cl allocer.cpp -Foallocer%bits%.o -Feallocer%bits%.exe /MD /Zi crt32-vc6.lib dll-alloc%bits%.lib

:eof
