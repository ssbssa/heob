
BITS=
ifeq ($(BITS),32)
  PREF=i686-w64-mingw32-
else ifeq ($(BITS),64)
  PREF=x86_64-w64-mingw32-
else
  PREF=
endif

CC=$(PREF)gcc
CXX=$(PREF)g++
CPPFLAGS=
CFLAGS=-Wall -Wextra -fno-omit-frame-pointer -fno-optimize-sibling-calls
CFLAGS_HEOB=$(CPPFLAGS) $(CFLAGS) -O3
LDFLAGS_HEOB=-s -Wl,--entry=_smain -nostdlib -lkernel32
CFLAGS_TEST=$(CFLAGS) -O3 -g


all: heob$(BITS).exe allocer$(BITS).exe

heob$(BITS).exe: heob.c
	$(CC) $(CFLAGS_HEOB) -o$@ $^ $(LDFLAGS_HEOB)

allocer$(BITS).exe: allocer.cpp
	$(CXX) $(CFLAGS_TEST) -o$@ $^


ifeq ($(BITS),)
.PHONY: force

release: heob32.exe heob64.exe allocer32.exe allocer64.exe

heob32.exe allocer32.exe heob64.exe allocer64.exe: force
	$(MAKE) BITS=$(findstring 32,$@)$(findstring 64,$@) $@
endif


clean:
	rm -f *.exe
