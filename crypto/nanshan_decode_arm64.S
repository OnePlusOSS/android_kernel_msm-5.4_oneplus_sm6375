// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2018-2022 Oplus. All rights reserved.
*/

#include "asm.h"


/*

  int64_t nanshan_decode_asm(
    uint8_t ** dst_ptr,                     *dst_ptr points to next output byte to write
    uint8_t * dst_begin,                    points to first valid output byte we can access, dst_begin <= dst
    uint8_t * dst_end,                      "relaxed" end of output buffer (see below)
    const uint8_t ** src_ptr,               *src_ptr points to next input byte to read
    const uint8_t * src_end)                "relaxed" end of input buffer (see below)

  We test the position of the pointers only to ensure we don't access past src_end/dst_end + some fixed constant.
  We never read before dst_begin.

  Return 0 on success, -1 on failure
  On output, (*src_ptr,*dst_ptr) receives the last position in both buffers corresponding to the beginning of a NAN_SHAN instruction.
 
*/

.globl nanshan_decode_asm

#define dst                x0   // arg0
#define dst_begin          x1   // arg1
#define dst_end            x2   // arg2
#define src                x3   // arg3
#define src_end            x4   // arg4

#define w_n_matches        w5   // lower 32 bits of n_matches
#define n_matches          x5
#define n_literals         x6
#define copy_src           x7   // match/literal copy source
#define copy_dst           x8   // match/literal copy destination

#define w_aux1             w9   // lower 32 bits of aux1
#define aux1               x9
#define aux2              x10

#define w_match_distance  w11   // lower 32 bits of match_distance
#define match_distance    x11

#define match_permtable   x12
#define match_disttable   x13

#define dst_good          x19
#define src_good          x20

.macro establish_frame
    ARM64_STACK_PROLOG
    stp     x29, x30,    [sp, #-16]!
    mov     x29, sp
.endm

.macro clear_frame_and_return
    ldp     x29, x30,    [sp], #16
    ARM64_STACK_EPILOG
.endm

// copy_1x16 SOURCE_ADDR DESTINATION_ADDR
// Copy 16 bytes, clobber: q0
.macro copy_1x16 arg0, arg1
    ldr     q0,[\arg0]
    str     q0,[\arg1]
.endm

// copy_1x16_and_increment SOURCE_ADDR DESTINATION_ADDR
// Copy 16 bytes, and increment both addresses by 16, clobber: q0
.macro copy_1x16_and_increment arg0, arg1
    ldr     q0,[\arg0],#16
    str     q0,[\arg1],#16
.endm

// copy_2x16_and_increment SOURCE_ADDR DESTINATION_ADDR
// Copy 2 times 16 bytes, and increment both addresses by 32, clobber: q0
.macro copy_2x16_and_increment arg0, arg1
    ldr     q0,[\arg0],#16
    str     q0,[\arg1],#16
    ldr     q0,[\arg0],#16
    str     q0,[\arg1],#16
.endm

// copy_1x32_and_increment SOURCE_ADDR DESTINATION_ADDR
// Copy 32 bytes, and increment both addresses by 32, clobber: q0,q1
.macro copy_1x32_and_increment arg0, arg1
    ldp     q0,q1,[\arg0],#32
    stp     q0,q1,[\arg1],#32
.endm

// If we don't branch, src < src_end after this
.macro check_src_end
    cmp     src,src_end
    b.hs    L_done                            // extremely unlikely, DONE when src >= src_end
.endm

// If we don't branch, dst < dst_end after this
.macro check_dst_end
    cmp     dst,dst_end
    b.hs    L_done                            // extremely unlikely, DONE when dst >= dst_end
.endm

.text
.p2align 4
nanshan_decode_asm:
    establish_frame
    stp     x19,x20,[sp,#-16]!                // need to preserve these
    stp     src,dst,[sp,#-16]!                // save src_ptr,dst_ptr on stack
    ldr     src,[src]                         // src = *src_ptr
    ldr     dst,[dst]                         // dst = *dst_ptr
    adr     match_permtable,L_match_permtable
    adr     match_disttable,L_match_disttable

L_decode_command:
    // Keep last known good positions in both streams
    mov     dst_good,dst
    mov     src_good,src

    // Check limits
    check_src_end
    check_dst_end

    // Decode 1-byte command
    ldrb    w_aux1,[src],#1                   // read command byte LLLLMMMM
    lsr     n_literals,aux1,#4                // 0000LLLL. n_literals is now 0..15
    and     n_matches,aux1,#0xf               // 0000MMMM. n_matches is now 0..15
    add     n_matches,n_matches,#4            // n_matches is now 4..19

    // Test number of literals (do not test if n_literals==0, because branch prediction fails on it)
    cmp     n_literals,#14
    b.ls    L_copy_short_literal              // 96% likely: n_literals in 0..14
    // continue to decode_long_literal

    // the number of literals is encoded on more bytes, we need to decode them
L_decode_long_literal:
    check_src_end                             // required here, since we may loop an arbitrarily high number of times
    ldrb    w_aux1,[src],#1
    add     n_literals,n_literals,aux1
    cmp     aux1,#255
    b.eq    L_decode_long_literal             // extremely unlikely
    // continue to copy_long_literal

    // Copy literals, n_literals >= 15
L_copy_long_literal:
    mov     copy_src,src                      // literal copy origin
    mov     copy_dst,dst                      // literal copy destination
    add     src,src,n_literals
    add     dst,dst,n_literals
    // Copy left literals if src >= end
    cmp     src,src_end
    b.hs    L_copy_left_literal                            // extremely unlikely, DONE when src >= src_end
    cmp     dst,dst_end
    b.hs    L_copy_left_literal                            // extremely unlikely, DONE when dst >= dst_end

L_copy_long_literal_loop:
    copy_1x32_and_increment copy_src,copy_dst
    cmp     dst,copy_dst
    b.hi    L_copy_long_literal_loop          // first test occurs after 16 bytes have been copied, and is unlikely to loop back
    b       L_expand_match

    // Copy literals, n_literals <= 14: copy 16 bytes
L_copy_short_literal:
    copy_1x16 src,dst
    add     src,src,n_literals
    add     dst,dst,n_literals
    // continue to expand match

L_expand_match:

    // Decode match distance
    ldrh    w_match_distance,[src],#2         // 16-bit distance
    cbz     match_distance,L_fail             // distance == 0 is invalid
    sub     copy_src,dst,match_distance       // copy_src is the match copy source
    cmp     copy_src,dst_begin
    b.lo    L_fail                            // copy_src < dst_begin: FAIL
    mov     copy_dst,dst                      // copy_dst is the match copy destination
    add     dst,dst,n_matches                 // dst is updated to be the byte after the match; n_matches <= 19 here

    // Do we need to decode a long match?
    cmp     n_matches,#19
    b.lo    L_copy_match               // unlikely, n_matches >= 19 encoded on more bytes

L_decode_long_match:
    check_src_end                             // required here, since we may loop an arbitrarily high number of times
    ldrb    w_aux1,[src],#1
    add     dst,dst,aux1
    add     n_matches,n_matches,aux1
    cmp     aux1,#255
    b.eq    L_decode_long_match               // very unlikely

L_copy_match:
    check_dst_end                             // required here, since dst was incremented by a arbitrarily high value
    cmp     match_distance,n_matches
    b.hs    L_long_match
    cmp     match_distance,#32
    b.hs    L_long_match

    // Copy match, n_matches >= 16, match_distance < 16:
    // load shuffle table, and permute to replicate the pattern on 32 bytes
L_copy_long_match_small_distance:
    ldr     q1,[copy_src]                     // 16 pattern bytes
    add     aux1,match_permtable,match_distance,lsl #5   // index in table
    ldp     q2,q3,[aux1]                      // load 32-byte permutation
    tbl     v0.16b,{v1.16b},v2.16b            // low 16 bytes of pattern in q0
    tbl     v1.16b,{v1.16b},v3.16b            // high 16 bytes of pattern in q1
    cmp     match_distance,#16
    b.lo    L_copy_perm
    ldp     q0,q1,[copy_src]
L_copy_perm:
    ldrb    w_aux1,[match_disttable,match_distance]  // valid pattern length in aux1
    // fixed
    stp     q0,q1,[copy_dst]
    add     copy_dst,copy_dst,aux1
    cmp     dst,copy_dst
    b.ls    L_decode_command
L_copy_long_match_small_distance_loop:
    // loop
    stp     q0,q1,[copy_dst]
    add     copy_dst,copy_dst,aux1
    cmp     dst,copy_dst
    b.hi    L_copy_long_match_small_distance_loop
    b       L_decode_command

    // n_matches > 16
L_long_match:
    // fixed + loop
    copy_1x16_and_increment copy_src,copy_dst
    cmp     dst,copy_dst
    b.ls    L_decode_command
L_copy_long_match_32_loop:
    copy_1x32_and_increment copy_src,copy_dst
    cmp     dst,copy_dst
    b.hi    L_copy_long_match_32_loop
    b       L_decode_command

L_fail:
    mov     aux1,#-1                          // FAIL
    b       L_exit

// copy left literals
L_copy_left_literal:
    cmp     n_literals,#32
    b.lt    L_copy_lt_32
    copy_1x32_and_increment copy_src,copy_dst
    sub     n_literals,n_literals,#32
    b       L_copy_left_literal
L_copy_lt_32:
    cmp     n_literals,#16
    b.lt    L_copy_lt_16
    copy_1x16_and_increment copy_src,copy_dst

    // align the src and dst to 16 Bytes
    // src - (16-(n-16)) = src + n - 32
    sub     n_literals,n_literals,#32
    add     copy_src,copy_src,n_literals
    add     copy_dst,copy_dst,n_literals
    copy_1x16_and_increment copy_src,copy_dst
    b       L_copy_left_done
L_copy_lt_16:
    subs    n_literals,n_literals,#1
    b.lt    L_copy_left_done
    ldrb    w1,[copy_src],#1 
    strb    w1,[copy_dst],#1
    b       L_copy_lt_16
L_copy_left_done:
    mov     src_good,copy_src
    mov     dst_good,copy_dst

L_done:
    mov     aux1,#0                           // OK
    // continue to L_exit

L_exit:
    ldp     src,dst,[sp],#16                  // get back src_ptr,dst_ptr from stack
    str     src_good,[src]                    // *src_ptr = src_good
    str     dst_good,[dst]                    // *dst_ptr = dst_good
    mov     x0,aux1                           // x0 = return value
    ldp     x19,x20,[sp],#16                  // restore
    clear_frame_and_return

// permutation tables for short distance matches, 32 byte result, for match_distance = 0 to 15
// value(d)[i] = i%d for i = 0..31
.p2align 6
L_match_permtable:
.byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // 0
.byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0  // 1
.byte 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1  // 2
.byte 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0,    1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1, 2, 0, 1  // 3
.byte 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3,    0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3, 0, 1, 2, 3  // 4
.byte 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0,    1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 0, 1  // 5
.byte 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3,    4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1  // 6
.byte 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1,    2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5, 6, 0, 1, 2, 3  // 7
.byte 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7,    0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7  // 8
.byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4, 5, 6,    7, 8, 0, 1, 2, 3, 4, 5, 6, 7, 8, 0, 1, 2, 3, 4  // 9
.byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1, 2, 3, 4, 5,    6, 7, 8, 9, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 0, 1  // 10
.byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10, 0, 1, 2, 3, 4,    5, 6, 7, 8, 9,10, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9  // 11
.byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11, 0, 1, 2, 3,    4, 5, 6, 7, 8, 9,10,11, 0, 1, 2, 3, 4, 5, 6, 7  // 12
.byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12, 0, 1, 2,    3, 4, 5, 6, 7, 8, 9,10,11,12, 0, 1, 2, 3, 4, 5  // 13
.byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13, 0, 1,    2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13, 0, 1, 2, 3  // 14
.byte 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, 0,    1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14, 0, 1  // 15

// valid repeating pattern size, for each match_distance = 0 to 15
// value(d) = 32 - (32%d), is the largest a multiple of d <= 32
.p2align 6
L_match_disttable:
.byte 32,32,32,30,32,30,30,28,32,27,30,22,24,26,28,30  // 0  .. 15
.byte 16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31  // 16 .. 31

// #endif // NAN_SHAN_ENABLE_ASSEMBLY_DECODE_ARM64
