#ifndef __SFPU_INTERNALS_H
#define __SFPU_INTERNALS_H

/****************************************************
 User level SFPU Internals header

 To be included in every user-level program that is
 compiled by this compiler.
 ****************************************************/

typedef float v64sf __attribute__((vector_size(64*4)));

#if defined __riscv_sfpu_wormhole

#define __builtin_riscv_sfpload(a, b, c, d)  \
        __builtin_riscv_wormhole_sfpload((a), (b), (c), (d))

#endif  // defined __riscv_sfpu_wormhole

#endif // __SFPU_INTERNALS_H
