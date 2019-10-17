/* gcc -g -march=armv8-a immedTests.c --save-temps -o immedTests 
   objdump -d immedTests > immedTests.asm
*/

#include <stdio.h>
#include <inttypes.h>

uint32_t word_test() {
  asm( "movz x0, #0xCCDD, lsl  #0" )  ; /* MoveLo+Zero other bits */
  asm( "movk x0, #0xAABB, lsl #16" )  ; /* MoveHi+Keep other bits */
}

uint64_t long_test() {
  asm( "movz x0, #0x3210, lsl  #0" ) ;
  asm( "movk x0, #0x7654, lsl #16" ) ;
  asm( "movk x0, #0xBA98, lsl #32" ) ;
  asm( "movk x0, #0xFEDC, lsl #48" ) ;
}

void main()
{ 
  uint32_t word_val = word_test();
  uint64_t long_val = long_test();
  printf( "\nImmediate inline 32 is %lu = 0x%lX \n",    word_val, word_val );
  printf( "\nImmediate inline 64 is %lu = 0x%lX \n\n",  long_val, long_val );
}
