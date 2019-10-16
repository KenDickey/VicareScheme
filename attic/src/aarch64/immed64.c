/* gcc -g -march=armv8-a immed64.c --save-temps -o immed64 
   objdump -d immed64 > immed64.asm
*/

#include <stdio.h>
#include <inttypes.h>

/* No AArch64 PC register */
uint64_t da_test() {
  asm ( "adr x1, #16" ) ; /* X1 := 2 opcodes + 64 bits = 16 bytes ahead */
  asm ( "br  x1" ) ; /* br ahead: */
  asm ( "orn  x0, x8,  x2, lsl #4" )   ; /* AA221100 */
  asm ( "orn x29, x6, x27, asr #51 " ) ; /* AABBCCDD */
  /* ahead: */
  asm ( "ldr x0, [x1, #-8]" ) ; /* load 64 bit literal -> x0 */
  /* and return */
}

void main()
{ 
  uint64_t pc_val = da_test();
  printf( "\nImmediate inline 64 is %lu = 0x%lX \n\n",  pc_val, pc_val );
}

/*
d503201fd503201f0000000000000838 <da_test>:
adr+0  838:   10000081        adr     x1, 848 <da_test+0x10>
adr+4  83c:   d61f0020        br      x1
adr+8  840:   aa221100        orn     x0, x8, x2, lsl #4
adr+16 844:   aabbccdd        orn     x29, x6, x27, asr #51
adr+20 848:   f85f8020        ldur    x0, [x1, #-8]
adr+24 84c:   d503201f        nop
adr+28 850:   d65f03c0        ret

 >>> ./immed64 

 Immediate inline 64 is 12302652059506839808 = 0xAABBCCDDAA221100 

 */
