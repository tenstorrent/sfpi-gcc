The following code:

```cpp
sfpi::vFloat tmp = sfpi::vConstFloatPrgm0;
dst_reg[0] = tmp;
```

Incorrectly generates the following, which is a no-op as SFPSTORE only works with L0-L11

```asm
sfpstore  0,L12,0,3
```
