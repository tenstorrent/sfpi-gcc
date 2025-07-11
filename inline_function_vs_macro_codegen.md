```cpp
#define abc_macro(a, b, c, val) ((a * val) + b) * val + c

sfpi_inline sfpi::vFloat abc(sfpi::vFloat a, sfpi::vFloat b, sfpi::vFloat c, sfpi::vFloat val)
{
    return ((a * val) + b) * val + c;
}

// ...
abc(4.00048828125, 8.0009765625, 16.001953125, val);
```

The following suboptimal code is generated:

```asm
sfploadi  L0,16512,8
sfploadi  L0,1024,10
sfploadi  L2,16640,8
sfploadi  L2,1024,10
sfploadi  L1,16768,8
sfploadi  L1,1024,10
sfpmad  L0,L0,L3,L2,0
sfpnop
sfpmad  L0,L0,L3,L1,0
sfpnop
```

The first `sfpnop` can be hidden by moving `sfploadi L1...` (one or more) after the first `sfpmad`.

If `abc_macro` is used instead of `abc`, the following is generated:

```asm
sfploadi  L0,16512,8
sfploadi  L0,1024,10
sfploadi  L1,16640,8
sfploadi  L1,1024,10
sfpmad  L0,L2,L0,L1,0
sfploadi  L1,16768,8
sfploadi  L1,1024,10
sfpmad  L0,L0,L2,L1,0
sfpnop
```
