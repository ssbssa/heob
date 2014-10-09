
HEOB_VERSION:=1.1-dev-$(shell date +%Y%m%d)

BITS=32
ifeq ($(BITS),32)
  PREF=i686-w64-mingw32-
else ifeq ($(BITS),64)
  PREF=x86_64-w64-mingw32-
else
  PREF=
endif

CC=$(PREF)gcc
CXX=$(PREF)g++
CPPFLAGS=-DNO_DWARFSTACK
CFLAGS=-Wall -Wextra -fno-omit-frame-pointer -fno-optimize-sibling-calls
CFLAGS_HEOB=$(CPPFLAGS) $(CFLAGS) -O3 -DHEOB_VER="\"$(HEOB_VERSION)\"" \
	    -ffreestanding
LDFLAGS_HEOB=-s -Wl,--entry=_smain -nostdlib -lkernel32
CFLAGS_TEST=$(CFLAGS) -O3 -g


all: heob$(BITS).exe allocer$(BITS).exe

heob$(BITS).exe: heob.c
	$(CC) $(CFLAGS_HEOB) -o$@ $^ $(LDFLAGS_HEOB)

allocer$(BITS).exe: allocer.cpp libcrt$(BITS).a dll-alloc$(BITS).dll dll-alloc-shared$(BITS).dll
	$(CXX) $(CFLAGS_TEST) -o$@ $^

dll-alloc$(BITS).dll: dll-alloc.c
	$(CC) $(CFLAGS_TEST) -shared -o$@ $^

dll-alloc-shared$(BITS).dll: dll-alloc$(BITS).dll
	cp -f $< $@

libcrt$(BITS).a: crt$(BITS).def
	$(PREF)dlltool -k -d $< -l $@


package: heob-$(HEOB_VERSION).7z

package-src:
	git archive "HEAD^{tree}" |xz >heob-$(HEOB_VERSION).tar.xz

heob-$(HEOB_VERSION).7z: heob32.exe heob64.exe
	7z a -mx=9 $@ $^


.PHONY: force

ifneq ($(BITS),32)
heob32.exe allocer32.exe: force
	$(MAKE) BITS=32 $@
endif
ifneq ($(BITS),64)
heob64.exe allocer64.exe: force
	$(MAKE) BITS=64 $@
endif


T_H01=-p1 -a4 -f0
T_A01=0
T_H02=-p1 -a4 -f0
T_A02=1
T_H03=-p1 -a4 -f0
T_A03=2
T_H04=-p2 -a4 -f0
T_A04=2
T_H05=-p1 -a4 -f0
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
TESTS=01 02 03 04 05 06 07 08 09 10 11 12 13 14 15 16 17 18 19 20

testres:
	mkdir -p $@

testc: heob$(BITS).exe allocer$(BITS).exe | testres
	@$(foreach t,$(TESTS),echo heob$(BITS) $(T_H$(t)) allocer$(BITS) $(T_A$(t)) "->" test$(BITS)-$(t).txt; ./heob$(BITS).exe $(T_H$(t)) allocer$(BITS) $(T_A$(t)) |sed 's/0x[0-9A-Z]*/0xPTR/g;/^ *0xPTR/d;s/\<of [1-9][0-9]*/of NUM/g' >testres/test-$(t).txt;)

TOK=[0;32mOK[0m
TFAIL=[0;31mFAIL[0m

test: heob$(BITS).exe allocer$(BITS).exe
	@$(foreach t,$(TESTS),echo test$(BITS)-$(t): heob$(BITS) $(T_H$(t)) allocer$(BITS) $(T_A$(t)) "->" `./heob$(BITS).exe $(T_H$(t)) allocer$(BITS) $(T_A$(t)) |sed 's/0x[0-9A-Z]*/0xPTR/g;/^ *0xPTR/d;s/\<of [1-9][0-9]*/of NUM/g' |diff - testres/test-$(t).txt >test$(BITS)-$(t).diff && echo "$(TOK)" && rm -f test$(BITS)-$(t).diff || echo "$(TFAIL)"`;)

testsc:
	$(MAKE) BITS=32 testc

tests:
	$(MAKE) BITS=32 test
	$(MAKE) BITS=64 test


clean:
	rm -f *.exe *.a dll-alloc*.dll
