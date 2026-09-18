# legacy/

`Project_Final.cpp` is the **original submission** (Computer Architecture course, Semnan University),
kept verbatim — only the Windows line endings were normalised — as the baseline that the current
implementation is measured against (`tools/legacy_accuracy.cpp`, figure in the main README).

Known defects of this version, all fixed in `include/fp32/fp32.hpp`:

| Defect | Effect |
|--------|--------|
| No guard/round/sticky bits — the aligned operand is simply shifted right | result is truncated instead of rounded: 1 ulp low in ≈ 40 % of random additions |
| Normalisation loop `while (mant < 2^23)` has no exit for `mant == 0` | `x + (−x)` never terminates |
| Zero, infinity, NaN and subnormal encodings are not recognised | wrong results or garbage for those inputs |
| No exponent overflow/underflow handling | wrap-around instead of ±∞ / subnormal |
| `pow(2, 23)` (a `double`) used inside integer bit manipulation | works by accident; not how hardware is modelled |

It is compiled only by the `legacy_accuracy` tool and is not part of the library.
