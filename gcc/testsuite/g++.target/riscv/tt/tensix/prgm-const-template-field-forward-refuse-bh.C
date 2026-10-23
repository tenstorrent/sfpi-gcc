// Safe local template initialization is not permission to skip an unsafe
// aggregate programmer in the independently audited call subtree.
// { dg-do compile }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-unroll-loops -fno-early-inlining -mtt-tensix-optimize-prgm-const -fdump-tree-rvtt_prgm_const-details" }
// { dg-final { scan-tree-dump "mop-template-word-unproven: unaudited raw opcode .template slot 5, word 0x9f000000." "rvtt_prgm_const" } }
// { dg-final { scan-tree-dump-not "prgm-const: allocated" "rvtt_prgm_const" } }
// { dg-final { scan-assembler-not "SFPMAD" } }

#define POISON_CONTEXT_PROGRAMMER
#include "prgm-const-template-field-forward-bh.C"
