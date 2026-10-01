/* Licensed multi-use MAD-fusion test shape: a product with TWO
   consumers -- the store keeps it live, the add offers the fusion.
   The default single-use mul+add->mad combine refuses (the mul cannot
   die); only the reassociation license admits fusing the add while
   keeping the mul.  Names macro-parameterized (charter genericity).

   Hooks:
     RMF_KERNEL	 kernel name
     RMF_SINGLE_USE  define to drop the product store (single-use
		     control: the DEFAULT fuse rule must fire and the
		     licensed rule must defer)  */

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
