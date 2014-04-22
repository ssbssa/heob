
//          Copyright Hannes Domani 2014.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          http://www.boost.org/LICENSE_1_0.txt)

#include <stdlib.h>
#include <stdio.h>
#include <string.h>


void choose( int arg )
{
  char *mem = (char*)malloc( 15 );
  mem[0] = 0;

  switch( arg )
  {
    case 1:
      {
        // memory leaks
        char *copy = strdup( "abcd" );
        char *zeroes = (char*)calloc( 2,500 );
        wchar_t *wcopy = (wchar_t*)wcsdup( L"efgh" );
        mem[1] = copy[0];
        mem[2] = zeroes[0];
        mem[3] = wcopy[0];
        char *newChars = new char[50];
        mem[4] = newChars[0];
      }
      break;

    case 2:
      // access after allowed area
      mem[1] = mem[20];
      mem[25] = 5;
      break;

    case 3:
      // access before allowed area
      mem[1] = mem[-10];
      mem[-5] = 3;
      break;

    case 4:
      // allocation size alignment
      {
        int sum = 0;
        char t[100];
        int i;
        t[0] = 0;
        for( i=0; i<64; i++ )
        {
          char *tc = strdup( t );
          sum += strlen( tc );
          strcat( t,"x" );
        }
        mem[1] = sum;
      }
      break;

    case 5:
      // failed allocation
      {
#ifndef _WIN64
#define BIGNUM 2000000000
#else
#define BIGNUM 0x1000000000000000
#endif
        char *big = (char*)malloc( BIGNUM );
        mem[1] = big[0];
      }
      break;

    case 6:
      // reference pointer after being freed
      {
        char *tmp = (char*)malloc( 15 );
        char *tmp2;
        mem[1] = tmp[0];
        free( tmp );
        tmp2 = (char*)malloc( 15 );
        printf( "ptr1=0x%p; ptr2=0x%p -> %s\n",
            tmp,tmp2,tmp==tmp2?"same":"different" );
        fflush( stdout );
        mem[2] = tmp[1];
        mem[3] = tmp2[0];
        free( tmp2 );
      }
      break;

    case 7:
      // multiple free / free of invalid pointer
      free( mem );
      free( (void*)0x80000000 );
      break;
  }

  mem = (char*)realloc( mem,30 );
  if( mem ) printf( "%s",mem );
  free( mem );
}


#ifdef __GNUC__
void *operator new[]( size_t size )
{
  return( malloc(size) );
}
#endif


int main( int argc,char **argv )
{
  int arg;
  printf( "allocer: main()\n" );
  fflush( stdout );

  arg = argc>1 ? atoi( argv[1] ) : 0;
  choose( arg );

  return( arg );
}
