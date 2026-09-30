
/* These exact bodies are duplicated by the default-off identity test.  Any
   option-induced register allocation or scheduling change in an ineligible
   function therefore fails one side or the other.  */

/*
**_Z10unknown_ccv:
**	SFPLOAD	L0, 0, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/

/* No immediately preceding all-lanes proof.  */
void unknown_cc ()
{
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 7);
}

/* A macro store inherits the launch row; address-counter updates are not
   equivalent to an ordinary load plus ordinary store.  */
void dst_counter_update ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 1);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 1);
}

/*
**_Z18dst_counter_updatev:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 0, 0, 1
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 1
**	ret
*/

/* WH/BH address bit zero contributes the macro VD-high bit.  */
void odd_dst_row ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 1, 0, 0, 0, 7);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 1, 0, 0, 0, 7);
}

/*
**_Z11odd_dst_rowv:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 1, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 1, 0, 7
**	ret
*/

/* The macro's ordinary VD result is the shifted integer, whereas the explicit
   cast leaves FP32 0/1.  A later use of the cast result must block formation. */
void cast_value_live_out ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, converted, 2, 0, 0, 0, 7);
}

/*
**_Z19cast_value_live_outv:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 0, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 7
**	SFPSTORE	L0, 2, 0, 7
**	ret
*/

/* Even an empty opaque asm may own unmodelled macro configuration.  */
void opaque_config_owner ()
{
  asm volatile ("" ::: "memory");
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto loaded = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, loaded, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 7);
}


/*
**_Z19opaque_config_ownerv:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 0, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 7
**	ret
*/

/* A second simultaneously live row consumes L1.  Formation must retain the
   explicit high-pressure body instead of clobbering it as canonical macro VD. */
void lreg_pressure ()
{
  __builtin_rvtt_sfppushc (0);
  __builtin_rvtt_sfppopc (0);
  auto lhs = __builtin_rvtt_sfpload (nullptr, 0, 0, 0, 0, 7);
  auto rhs = __builtin_rvtt_sfpload (nullptr, 2, 0, 0, 0, 7);
  auto shifted = __builtin_rvtt_sfpshft_i (nullptr, lhs, -31, 0, 0, 0);
  auto converted = __builtin_rvtt_sfpcast (shifted, 0);
  __builtin_rvtt_sfpstore (nullptr, converted, 0, 0, 0, 0, 7);
  __builtin_rvtt_sfpstore (nullptr, rhs, 2, 0, 0, 0, 7);
}


/*
**_Z13lreg_pressurev:
**	SFPENCC	3, 10
**	SFPLOAD	L0, 0, 0, 7
**	SFPLOAD	L1, 2, 0, 7
**	SFPSHFT	L0, L0, -31, 5
**	SFPCAST	L0, L0, 0
**	SFPSTORE	L0, 0, 0, 7
**	SFPSTORE	L1, 2, 0, 7
**	ret
*/
