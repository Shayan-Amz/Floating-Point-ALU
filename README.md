# A software model of an IEEE 754 binary32 floating-point unit

[![CI](https://github.com/Shayan-Amz/Floating-Point-ALU/actions/workflows/ci.yml/badge.svg)](https://github.com/Shayan-Amz/Floating-Point-ALU/actions/workflows/ci.yml)
[![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&logoColor=white)](include/fp32/fp32.hpp)
[![Header-only](https://img.shields.io/badge/library-header--only-blue)](include/fp32/fp32.hpp)
[![IEEE 754](https://img.shields.io/badge/IEEE_754-binary32-orange)](https://en.wikipedia.org/wiki/IEEE_754)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)

A single-precision floating-point unit written in C++17 using **integer arithmetic only**. Addition,
subtraction, multiplication and division follow the classic hardware datapath — unpack →
align/multiply/divide → normalise → round → pack — and return the same bits a hardware FPU would,
for normals, subnormals, zeros, infinities and NaNs, in all four IEEE 754 rounding modes.

The datapath never touches the host's floating-point registers, so the library works the same way on
any machine and is fully `constexpr`: `static_assert(add(0x3F800000u, 0x3F800000u) == 0x40000000u)`
is a compile-time check. It began as the adder I wrote for the Computer Architecture course and grew
into the four operations plus the test suite below.

---

## The binary32 format

```
 31   30      23 22                    0
┌───┬──────────┬────────────────────────┐
│ s │ eeeeeeee │ fffffffffffffffffffffff │      value = (−1)^s · 1.f · 2^(e − 127)      1 ≤ e ≤ 254
└───┴──────────┴────────────────────────┘
sign  exponent        fraction (23 bits)
      (8 bits,
       bias 127)
```

| Encoding | Exponent field | Fraction | Value |
|----------|----------------|----------|-------|
| normal | 1 … 254 | any | (−1)^s · **1**.f · 2^(e−127) — the leading 1 is implicit ("hidden bit") |
| subnormal | 0 | ≠ 0 | (−1)^s · **0**.f · 2^(−126) — gradual underflow, no hidden bit |
| zero | 0 | 0 | ±0 |
| infinity | 255 | 0 | ±∞ |
| NaN | 255 | ≠ 0 | not a number (quiet if bit 22 is set) |

Every row of that table is handled explicitly; the interesting ones are the last three, because they
are where "compute the answer, then round it" stops being the whole story.

---

## Datapath

All four operations share the same skeleton, mirroring how the hardware is organised:

```
              ┌──────────┐     ┌───────────────────────────┐     ┌───────────┐     ┌────────┐     ┌──────┐
 a, b (bits) ─┤  unpack  ├────►│ align + add  |  multiply  ├────►│ normalise ├────►│ round  ├────►│ pack ├─► result
              │ (hidden  │     │              |  divide    │     │ (leading- │     │ (GRS)  │     │      │
              │  bit,    │     └───────────────────────────┘     │  one      │     └────────┘     └──────┘
              │  special │            special cases ──────────►  │  detect)  │
              │  values) │            (0, ∞, NaN) bypass         └───────────┘
              └──────────┘
```

**Addition / subtraction** (`fp32::add`, `fp32::sub` ≡ `add(a, −b)`)

1. Order the operands so that |x| ≥ |y| — the result takes the sign of x.
2. Widen both 24-bit significands by three extra bits and shift y right by the exponent difference,
   **OR-ing every shifted-out bit into the lowest position** (the *sticky* bit).
3. Add the magnitudes if the signs agree, subtract otherwise.
4. Normalise: a carry-out shifts right once (exponent + 1); cancellation shifts left until the
   leading one is back in place — but never below the subnormal scale (exponent 1), which is what
   makes gradual underflow fall out naturally.
5. Round and pack (next section).

**Multiplication** (`fp32::mul`): exponents add (minus one bias), the 24 × 24-bit significands give a
48-bit product with its leading one at bit 46 or 47; the excess bits are folded into
guard/round/sticky and, when the exponent drops below 1, the product is shifted right further to
denormalise.

**Division** (`fp32::div`): exponents subtract (plus one bias); the dividend is doubled if it is
smaller than the divisor so that the quotient lies in [1, 2); an integer long division then yields
1 + 23 + 2 quotient bits, and a non-zero remainder sets the sticky bit.

Zero, infinity and NaN operands are resolved before the datapath: invalid operations such as ∞ − ∞,
0 × ∞ and 0 / 0 return the default quiet NaN `0x7FC00000`, and a NaN operand propagates its payload.

---

## Rounding

A finite-width datapath cannot keep every bit of an intermediate result, but it does not need to.
Correct rounding requires exactly three extra bits beyond the 24-bit significand
(Goldberg 1991, §"Guard digits"):

```
   ┌── 24-bit significand ──┐ G R S
 1 . f f f f f … f f f f f f │ g r s
                            │ │ │ └─ sticky : OR of every bit discarded below r
                            │ │ └─── round  : second discarded bit
                            │ └───── guard  : first discarded bit  (worth ½ ulp)
                            └── unit in the last place (ulp)
```

The tail `g r s` encodes where the exact result lies relative to the two representable neighbours:

| g | r ∨ s | exact result is … | round-to-nearest-even |
|---|-------|-------------------|-----------------------|
| 0 | any | below the midpoint | truncate |
| 1 | 1 | above the midpoint | increment |
| 1 | 0 | **exactly** on the midpoint | increment only if the LSB is 1 (tie → even) |

The directed modes (toward zero / +∞ / −∞) only need to know whether the result is inexact
(`g ∨ r ∨ s`) and what its sign is. Incrementing `1.111…1` produces `10.000…0`, which is
re-normalised (exponent + 1); an exponent that reaches 255 becomes ±∞ in round-to-nearest, or
saturates to ±MaxFinite in the directed modes that must not cross zero-ward (IEEE 754-2019 §7.4).

All of this lives in one function, `detail::round_and_pack`, shared by the four operations.

---

## How it is tested

Correctness is established by **differential testing against the host FPU**: every x86-64 (SSE),
AArch64 (NEON) and RISC-V processor implements IEEE 754 binary32 exactly, so the hardware result is
the oracle. `tests/test_fp32.cpp` compares the model with the hardware **bit for bit** (NaN payloads
excepted, as they are implementation-defined) for

* a 41 × 41 grid of hand-picked edge values (±0, ±min-subnormal, ±min-normal, 1 ± ulp, 2²³, 2²⁴,
  ±MaxFinite, ±∞, quiet/signalling NaNs, and other awkward cases);
* eight families of random operands designed to stress each part of the datapath: arbitrary bit
  patterns, normals, near-equal exponents (short alignment shifts), tiny (subnormal/underflow), huge
  (overflow), subnormal × normal, huge ÷ tiny, and near-cancelling pairs (x + (−x ± k ulp));
* all four rounding modes, set on the hardware with `fesetround()` and compiled with
  `-frounding-math` / `/fp:strict` so the compiler cannot constant-fold the reference operations;
* a set of algebraic properties (signed zeros, invalid operations, overflow saturation, ties to even
  at the subnormal boundary) and `static_assert`s proving the datapath is fully `constexpr`.

The default run (`ctest`) performs **≈ 32 million bit-exact comparisons** in well under a second;
`test_fp32 2000000` extends that to 256 million. A mutation check keeps the suite honest: swapping
round-to-nearest for truncation in the multiplier is caught in 52 % of random products.

CI runs the suite with GCC, Clang, MSVC and on macOS.

---

## Usage

### Build

```bash
git clone https://github.com/Shayan-Amz/Floating-Point-ALU.git
cd Floating-Point-ALU

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure        # runs the conformance suite
```

Without CMake: `make && make test` (GCC or Clang). The library itself is a single header — copy
`include/fp32/fp32.hpp` into any C++17 project.

### Command-line calculator

Operands can be 32-bit binary strings (spaces between fields optional), hexadecimal words, or
decimal literals.

```
$ build/fp32calc 0.1 + 0.2 --trace
a        0 01111011 10011001100110011001101  0x3DCCCCCD   0.100000001  (normal)
b        0 01111100 10011001100110011001101  0x3E4CCCCD   0.200000003  (normal)
op       +   [round to nearest, ties to even]
result   0 01111101 00110011001100110011010  0x3E99999A   0.300000012  (normal)
trace:
  a      sign=0  exponent=123 (2^-4)  fraction=0x4CCCCD  significand=1.f
  b      sign=0  exponent=124 (2^-3)  fraction=0x4CCCCD  significand=1.f
  result sign=0  exponent=125 (2^-2)  fraction=0x19999A  significand=1.f

$ build/fp32calc "0 01111111 00000000000000000000000" / "0 10000000 10000000000000000000000" --mode down
…
result   0 01111101 01010101010101010101010  0x3EAAAAAA   0.333333313  (normal)

$ build/fp32calc 1.5 - 1.5            # exact cancellation
result   0 00000000 00000000000000000000000  0x00000000   0  (zero)
```

`--mode nearest|zero|up|down` selects the rounding direction; running `fp32calc` with no arguments
prompts for the operands interactively. The operators may also be spelled `add`, `sub`, `mul`, `div`
— useful where a shell would otherwise expand `*` or, in Git Bash on Windows, rewrite a bare `/`
into a path.

### Library API

```cpp
#include "fp32/fp32.hpp"

using namespace fp32;

bits_t a = from_float(0.1f), b = from_float(0.2f);
bits_t s = add(a, b);                           // 0x3E99999A
bits_t q = div(a, b, Rounding::TowardZero);
float  f = to_float(mul(a, b));

// The whole datapath is constexpr:
static_assert(add(0x3F800000u, 0x3F800000u) == 0x40000000u);   // 1 + 1 == 2
static_assert(div(0x3F800000u, 0x40400000u) == 0x3EAAAAABu);   // 1 / 3, correctly rounded

// Classification and field access
Fields fl = decompose(a);      // {sign, exponent, fraction}
bool sn   = is_subnormal(0x00000001u);
std::string txt = to_binary_string(a);   // "0 01111011 10011001100110011001101"
```

Everything is `noexcept` and `constexpr` except the string conversions, which throw
`std::invalid_argument` on malformed input.

---

## Project structure

```
.
├── include/fp32/fp32.hpp   the library: format constants, classification, add/sub/mul/div,
│                           round_and_pack, conversions (≈ 360 lines, header-only)
├── src/main.cpp            fp32calc command-line front end
├── tests/test_fp32.cpp     differential conformance tests against the host FPU
├── CMakeLists.txt · Makefile · .github/workflows/ci.yml   (GCC, Clang, MSVC, macOS)
└── LICENSE (MIT)
```

---

## Limitations and future work

* **Exception flags** — the model returns correctly rounded values but does not raise the IEEE 754
  status flags (inexact, underflow, overflow, invalid, divide-by-zero). Adding a flags out-parameter
  to `round_and_pack` is the natural next step.
* **Signalling NaNs** — a signalling NaN is quieted and propagated; no invalid-operation signal is
  raised.
* **Further operations** — fused multiply-add, square root (digit-recurrence or Newton–Raphson),
  comparisons and int ⇄ float conversions would complete the unit.
* **Other formats** — the datapath is written against named constants; templating it on
  `(exponent bits, fraction bits)` would give binary16, bfloat16 and binary64 models.
* **Hardware description** — the algorithms map directly onto a synthesizable datapath; a
  cycle-accurate Verilog/VHDL port validated against this model would close the loop with the
  course's original motivation.

---

## References

1. IEEE Computer Society, *IEEE Standard for Floating-Point Arithmetic*, IEEE Std 754-2019.
2. D. Goldberg, "What Every Computer Scientist Should Know About Floating-Point Arithmetic",
   *ACM Computing Surveys* 23(1), 1991.
3. J.-M. Muller et al., *Handbook of Floating-Point Arithmetic*, 2nd ed., Birkhäuser, 2018.
4. D. A. Patterson and J. L. Hennessy, *Computer Organization and Design*, ch. 3 ("Arithmetic for Computers").

---

## License

Released under the [MIT License](LICENSE).
