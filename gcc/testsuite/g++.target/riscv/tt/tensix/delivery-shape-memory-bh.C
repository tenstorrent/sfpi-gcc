/* Upstream now rejects a runtime immediate with a null instruction-buffer
   pointer: the synthesised instruction word has to be written somewhere.
   These subjects are deliberately dynamic, so they hand it a buffer.  */
namespace ckernel { volatile unsigned long *instrn_buffer; }
// { dg-options "-mcpu=tt-bh-tensix -O2 -fno-exceptions -fno-rtti -mtt-tensix-optimize-delivery-shape -fdump-tree-rvtt_delivery_shape" }
// A volatile memory read in the row body refuses by name.
// { dg-final { scan-tree-dump "refused .delivery-shape-memory." "rvtt_delivery_shape" } }
// { dg-final { scan-assembler-not "TTREPLAY" } }

extern volatile unsigned ds_shared_word;

void ds_memory ()
{
  for (int d = 0; d < 32; ++d)
    {
      auto v = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
      auto c = __builtin_rvtt_sfploadi (ckernel::instrn_buffer, ds_shared_word, 0, 0, 0);
      auto t = __builtin_rvtt_sfpmad (v, c, v, 0);
      __builtin_rvtt_sfpstore (nullptr, t, 0, 0, 0, 0, 7);
      __builtin_rvtt_ttincrwc (0, 2, 0, 0);
    }
}
