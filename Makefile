
# detect if mingw32-make is used
MINGW32_MAKE:=$(strip $(filter %mingw32-make,$(MAKE)) $(filter %mingw32-make.exe,$(MAKE)))
ifeq ($(MINGW32_MAKE),)
DATE:=$(shell date +%Y%m%d)
else
DATE:=$(strip $(subst .,,$(shell date /t)))
endif

HEOB_VERSION:=3.2-dev-$(DATE)
HEOB_VER_NUM:=3,2,0,99
HEOB_PRERELEASE:=1
HEOB_COPYRIGHT_YEARS:=2014-2020

ifeq ($(MINGW32_MAKE),)
BITS=32
ifeq ($(BITS),32)
  PREF=i686-w64-mingw32-
else ifeq ($(BITS),64)
  PREF=x86_64-w64-mingw32-
else
  PREF=
endif
else
ifeq ($(strip $(filter x86_64-%,$(shell gcc -dumpmachine))),)
  BITS=32
else
  BITS=64
endif
PREF=
endif

CC=$(PREF)gcc
CXX=$(PREF)g++
WINDRES=$(PREF)windres
ifeq ($(wildcard dwarfstack/include/dwarfstack.h),)
CPPFLAGS=-DNO_DWARFSTACK
else
CPPFLAGS=-Idwarfstack/include
endif
CFLAGS=-Wall -Wextra -Wshadow -Wwrite-strings -Werror \
       -Wno-cast-function-type \
       -fno-omit-frame-pointer -fno-optimize-sibling-calls
CFLAGS_HEOB=$(CPPFLAGS) $(CFLAGS) -O3 -g -DHEOB_VER="\"$(HEOB_VERSION)\"" \
	    -ffreestanding
LDFLAGS_HEOB=-nostdlib -lkernel32 -Wl,-dynamicbase,--build-id
CFLAGS_TEST=$(CFLAGS) -O3 -g -D_GLIBCXX_INCLUDE_NEXT_C_HEADERS


all: heob$(BITS).exe allocer$(BITS).exe

heob$(BITS).exe: heob.c heob-inj.c heob-internal.h heob.h heob-ver$(BITS).o
	$(CC) $(CFLAGS_HEOB) -o$@ heob.c heob-inj.c heob-ver$(BITS).o $(LDFLAGS_HEOB) || { rm -f $@; exit 1; }

heob-ver$(BITS).o: heob-ver.rc heob.manifest heob.ico svg.js Makefile
	$(WINDRES) -DHEOB_VER_STR=\\\"$(HEOB_VERSION)\\\" -DHEOB_VER_NUM=$(HEOB_VER_NUM) -DHEOB_PRERELEASE=$(HEOB_PRERELEASE) -DHEOB_COPYRIGHT_YEARS=\\\"$(HEOB_COPYRIGHT_YEARS)\\\" $< $@

strip-heob$(BITS): heob$(BITS).exe
	$(PREF)strip -s $<

allocer$(BITS).exe: allocer.cpp libheobcpp$(BITS).a dll-alloc$(BITS).dll dll-alloc-shared$(BITS).dll
	$(CXX) $(CFLAGS_TEST) -o$@ $^ -nostdlib -lmsvcrt -lkernel32

dll-alloc$(BITS).dll: dll-alloc.cpp libheobcpp$(BITS).a
	$(CXX) $(CFLAGS_TEST) -shared -o$@ $^ -static-libgcc

dll-alloc-shared$(BITS).dll: dll-alloc$(BITS).dll
	cp -f $< $@

libheobcpp$(BITS).a: crt$(BITS).def
	$(PREF)dlltool -k -d $< -l $@


package-dbg: heob-$(HEOB_VERSION)-dbg.7z

package: heob-$(HEOB_VERSION).7z

package-src:
	git archive "HEAD^{tree}" |xz >heob-$(HEOB_VERSION).tar.xz

packages: package-src package-dbg package

heob-$(HEOB_VERSION)-dbg.7z: heob32.exe heob64.exe
	7z a -mx=9 $@ $^

heob-$(HEOB_VERSION).7z: strip-heob32 strip-heob64
	7z a -mx=9 $@ heob32.exe heob64.exe


.PHONY: force

ifneq ($(BITS),32)
heob32.exe allocer32.exe strip-heob32: force
	$(MAKE) BITS=32 $@
endif
ifneq ($(BITS),64)
heob64.exe allocer64.exe strip-heob64: force
	$(MAKE) BITS=64 $@
endif


T_H01=-p1 -a4 -f0
T_A01=0
T_H02=-p1 -a4 -f0
T_A02=1
T_H03=-p1 -a4 -f0
T_A03=2
T_H04=-p2 -a16 -f0 -s0xcc
T_A04=2
T_H05=-p1 -a16 -f0 -s0xcc
T_A05=3
T_H06=-p2 -a4 -f0
T_A06=3
T_H07=-p1 -a4 -f0
T_A07=4
T_H08=-p1 -a1 -f0
T_A08=4
T_H09=-p1 -a4 -f0
T_A09=5
T_H10=-p1 -a4 -f0
T_A10=6
T_H11=-p1 -a4 -f1
T_A11=6
T_H12=-p1 -a4 -f0
T_A12=7
T_H13=-p1 -a4 -f0 -m2
T_A13=8
T_H14=-p1 -a4 -f0 -m0
T_A14=8
T_H15=-p1 -a4 -f0 -l0
T_A15=1
T_H16=-p1 -a4 -f0 -d0
T_A16=10
T_H17=-p1 -a4 -f0 -d1
T_A17=10
T_H18=-p1 -a4 -f0 -d2
T_A18=10
T_H19=-p1 -a4 -f0 -e1
T_A19=0
T_H20=-p1 -a4 -f0 -m1
T_A20=8
T_H21=-h0
T_A21=12
T_H22=-h1 -n0
T_A22=12
T_H23=-p1 -a4 -f1
T_A23=7
T_H24=-p1 -a4 -f0 -d3
T_A24=10
T_H25=-p1 -a4 -f0 -r1
T_A25=7
T_H26=-p1 -a4 -f0 -r1
T_A26=5
T_H27=-p1 -a4 -f0 -r0
T_A27=13
T_H28=-p1 -a4 -f1 -r0
T_A28=13
T_H29=-p1 -a4 -f0 -r1
T_A29=13
T_H30=-p1 -a4 -f1 -r1
T_A30=13
T_H31=-p1 -a4 -f0 -M1 -n0
T_A31=14
T_H32=-p1 -a4 -f0 -M5000
T_A32=14
T_H33=-p1 -a8 -f0 -l0
T_A33=15
T_H34=-p1 -a8 -f0 -l1
T_A34=15
T_H35=-p1 -a8 -f0 -l2 -d0
T_A35=15
T_H36=-p1 -a8 -f0 -l3 -d0
T_A36=15
T_H37=-p1 -a8 -f0 -l4 -d0
T_A37=15
T_H38=-p1 -a8 -f0 -l5 -d0
T_A38=15
T_H39=-p1 -a4 -f0 -m1 -r2
T_A39=8
T_H40=-p1 -a4 -f0 -M1 -n1
T_A40=14
T_H41=-p1 -a4 -f1 -M1 -n0
T_A41=16
T_H42=-p1 -a4 -f1 -M5000 -n0
T_A42=16
T_H43=-p1 -a4 -f1 -M1 -n1
T_A43=16
T_H44=-p1 -a16 -f0 -L0
T_A44=17
T_H45=-p1 -a16 -f0 -L10
T_A45=17
T_H46=-p1 -a16 -f0 -L100
T_A46=17
T_H47=-p1 -a16 -f0 -L100 -g1
T_A47=18
T_H48=-p1 -a16 -f0 -L100 -g0
T_A48=18
T_H49=-p1 -a4 -f0 -R3 -R5
T_A49=1
T_H50=-p0 -a4 -f0
T_A50=19
T_H51=-p1 -a4 -f0
T_A51=19
T_H52=-p1 -a4 -f0
T_A52=20
T_H53=-p0 -a4 -f0
T_A53=21
T_H54=-p1 -a4 -f0
T_A54=21
T_H55=-p1 -a4 -f0
T_A55=22
T_H56=-p1 -a4 -f0 -L100 -i0x7c:1
T_A56=23
T_H57=-p1 -a16 -f0 -L100 -i0x7c:1
T_A57=23
T_H58=-p1 -a16 -f0 -L100 -i0x7c:2
T_A58=23
T_H59=-p1 -a16 -f0 -L100 -i0x7c:4
T_A59=23
T_H60=-p1 -a16 -f0 -L100 -i0x7c:8
T_A60=23
T_H61=-p1 -a16 -f0 -L100 -i0x7c00000000000000:8
T_A61=23
T_H62=-p1 -a16 -f0 -g0
T_A62=26
T_H63=-p1 -a4 -f0 -z50
T_A63=1
T_H64=-p1 -a16 -f0 -g2
T_A64=26
T_H65=-p1 -a16 -f0 -l5 -g2 -d0
T_A65=15
T_H66=-p1 -a16 -f0 -l0
T_A66=29
T_H67=-p1 -a16 -f1 -l0
T_A67=31
T_H68=-p1 -a16 -f1 -l0
T_A68=32
T_H69=-p0 -a16 -f0 -l0
T_A69=33
T_H70=-p1 -a16 -f0 -l0
T_A70=33
T_H71=-p0 -a16 -f0 -l0
T_A71=34
T_H72=-p1 -a16 -f0 -l0
T_A72=34
T_H73=-p0 -a16 -f0 -l1
T_A73=35
T_H74=-p0 -a16 -f0 -l1
T_A74=36
T_H75=-p1 -a16 -f0 -l1
T_A75=36
T_H76=-p1 -a16 -f0
T_A76=39
T_H77=-p1 -a16 -f1
T_A77=39
T_H78=-p1 -a16 -f1
T_A78=40
T_H79=-h2
T_A79=0
T_H80="-Oallocer$(BITS):-X;"
T_A80=0
T_H81=-p1 -a16 -f0
T_A81=43
T_H82=-h2
T_A82=10
T_H83=-h2
T_A83=45
T_H84=-p1 -a8 -f0 -l2 -d0
T_A84=46
T_H85=-p1 -a8 -f0
T_A85=48
T_H86=-p0 -a16 -l0
T_A86=50
T_H87=-p1 -a16 -l0
T_A87=55
T_H88=-p1 -a16 -l1
T_A88=56
T_H89=-p1 -a4 -f0 -d4
T_A89=10
ifeq ($(MINGW32_MAKE),)
TESTS:=$(shell seq -w 01 89)
else
TESTS:=01
endif

testres:
	mkdir -p $@

testc: heob$(BITS).exe allocer$(BITS).exe | testres
	@$(foreach t,$(TESTS),echo heob$(BITS) $(T_H$(t)) allocer$(BITS) $(T_A$(t)) "->" test$(BITS)-$(t).txt; ./heob$(BITS).exe $(T_H$(t)) allocer$(BITS) $(T_A$(t)) |sed 's/0x[0-9A-Z]*/0xPTR/g;/^[ |]*0xPTR/d;s/\<of [1-9][0-9]*/of NUM/g;s/^[ |]*\[/    \[/;/^           *[^\[]/d' >testres/test-$(t).txt;)

TOK=[0;32mOK[0m
TFAIL=[0;31mFAIL[0m

test: heob$(BITS).exe allocer$(BITS).exe
	@$(foreach t,$(TESTS),echo test$(BITS)-$(t): heob$(BITS) $(T_H$(t)) allocer$(BITS) $(T_A$(t)) "->" `./heob$(BITS).exe $(T_H$(t)) allocer$(BITS) $(T_A$(t)) </dev/null |sed 's/0x[0-9A-Z]*/0xPTR/g;/^[ |]*0xPTR/d;s/\<of [1-9][0-9]*/of NUM/g;s/^[ |]*\[/    \[/;/^           *[^\[]/d' |diff -Naur --label "expected result" --label "actual result" testres/test-$(t).txt - >test$(BITS)-$(t).diff && echo "$(TOK)" && rm -f test$(BITS)-$(t).diff || echo "$(TFAIL)"`;)

testsc:
	$(MAKE) BITS=32 testc

tests:
	$(MAKE) BITS=32 test
	$(MAKE) BITS=64 test


clean:
	rm -f *.o *.exe *.a dll-alloc*.dll
