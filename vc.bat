
cl dll-alloc.c /LD /MD /Zi
copy dll-alloc.dll dll-alloc-shared.dll

if "%1" == "vc6" goto vc6

cl allocer.cpp /MD /Zi dll-alloc.lib
mt.exe -manifest allocer.exe.manifest -outputresource:allocer.exe;1
goto eof

:vc6
lib /def:crt32-vc6.def /out:crt32-vc6.lib
cl allocer.cpp /MD /Zi crt32-vc6.lib dll-alloc.lib

:eof
