/* Test program for PKEYS registers.

   Copyright 2015-2026 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#include <stddef.h>
#include <stdio.h>
#include <windows.h>

#define bit_OSXSAVE	(1 << 27)
#define bit_PKU		(1 << 3)

#define XSTATE_PKRU	(1 << 9)

#define __cpuid(level, a, b, c, d)					\
  __asm__ __volatile__ ("cpuid\n\t"					\
			: "=a" (a), "=b" (b), "=c" (c), "=d" (d)	\
			: "0" (level))

#define __cpuid_count(level, count, a, b, c, d)				\
  __asm__ __volatile__ ("cpuid\n\t"					\
			: "=a" (a), "=b" (b), "=c" (c), "=d" (d)	\
			: "0" (level), "2" (count))

static __inline unsigned int
__get_cpuid_max (unsigned int __ext, unsigned int *__sig)
{
  unsigned int __eax, __ebx, __ecx, __edx;

  /* Host supports cpuid.  Return highest supported cpuid input value.  */
  __cpuid (__ext, __eax, __ebx, __ecx, __edx);

  if (__sig)
    *__sig = __ebx;

  return __eax;
}

static __inline int
__get_cpuid (unsigned int __leaf,
	     unsigned int *__eax, unsigned int *__ebx,
	     unsigned int *__ecx, unsigned int *__edx)
{
  unsigned int __ext = __leaf & 0x80000000;
  unsigned int __maxlevel = __get_cpuid_max (__ext, 0);

  if (__maxlevel == 0 || __maxlevel < __leaf)
    return 0;

  __cpuid (__leaf, *__eax, *__ebx, *__ecx, *__edx);
  return 1;
}

#ifndef NOINLINE
#define NOINLINE __attribute__ ((noinline))
#endif

unsigned int have_pkru (void) NOINLINE;

static inline unsigned long
rdpkru (void)
{
  unsigned int eax, edx;
  unsigned int ecx = 0;
  unsigned int pkru;

  asm volatile (".byte 0x0f,0x01,0xee\n\t"
	       : "=a" (eax), "=d" (edx)
	       : "c" (ecx));
  pkru = eax;
  return pkru;
}

static inline void
wrpkru (unsigned int pkru)
{
  unsigned int eax = pkru;
  unsigned int ecx = 0;
  unsigned int edx = 0;

  asm volatile (".byte 0x0f,0x01,0xef\n\t"
	       : : "a" (eax), "c" (ecx), "d" (edx));
}

unsigned int NOINLINE
have_pkru (void)
{
  unsigned int eax, ebx, ecx, edx;

  if (!__get_cpuid (1, &eax, &ebx, &ecx, &edx))
    return 0;

  if ((ecx & bit_OSXSAVE) == bit_OSXSAVE)
    {
      if (__get_cpuid_max (0, NULL) < 7)
	return 0;

      __cpuid_count (7, 0, eax, ebx, ecx, edx);

      if ((ecx & bit_PKU) == bit_PKU)
	return 1;
    }
  return 0;
}

int
main (int argc, char **argv)
{
  unsigned int wr_value = 0x12345678;
  unsigned int rd_value = 0x0;

  if (have_pkru ())
    {
      printf("CPU: PKRU available\n");

      DWORD64 features = GetEnabledXStateFeatures();
      printf("GetEnabledXStateFeatures: 0x%llx, PKRU %savailable\n",
	  features, (features & XSTATE_PKRU) ? "" : "not ");

      wrpkru (wr_value);
      asm ("nop\n\t");	/* break here 1.  */

      rd_value = rdpkru ();
      asm ("nop\n\t");	/* break here 2.  */

      printf("finish\n");
    }
  else
    printf("CPU: PKRU not available\n");

  return 0;
}
