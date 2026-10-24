/* Shared multi-use MAD-fusion shape: a product consumed by a store and an add.

   Hooks:
     RMF_KERNEL	 kernel name
     RMF_SINGLE_USE  define to drop the product store (single-use shape)  */

#ifndef RMF_ADDR_MODE
#define RMF_ADDR_MODE 7
#endif

void
RMF_KERNEL (void)
{
  auto RMF_A = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RMF_ADDR_MODE);
  auto RMF_B = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RMF_ADDR_MODE);
  auto RMF_C = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 6, RMF_ADDR_MODE);
  auto RMF_P = __builtin_rvtt_sfpmul (RMF_A, RMF_B, 0);
#ifndef RMF_SINGLE_USE
  __builtin_rvtt_sfpstore (nullptr, RMF_P, 0, 0, 0, 6, RMF_ADDR_MODE);
#endif
  auto RMF_R = __builtin_rvtt_sfpadd (RMF_P, RMF_C, 0);
  __builtin_rvtt_sfpstore (nullptr, RMF_R, 0, 0, 0, 6, RMF_ADDR_MODE);
}
