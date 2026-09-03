# ⚡ IEEE 754 Single-Precision (32-bit) Floating-Point Adder

[![C++](https://img.shields.io/badge/Language-C%2B%2B17-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white)](https://isocpp.org/)
[![Computer Architecture](https://img.shields.io/badge/Domain-Computer_Architecture-orange?style=for-the-badge)](https://en.wikipedia.org/wiki/IEEE_754)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg?style=for-the-badge)](https://opensource.org/licenses/MIT)

A bit-level C++ software simulation of a hardware Floating-Point Arithmetic Unit (FPU). The project implements IEEE 754 single-precision (binary32) addition and subtraction directly from raw 32-bit binary string inputs without using native floating-point types for calculation.

---

## 📌 Features

- **Field Parsing:** Extracts Sign (1 bit), Biased Exponent (8 bits), and Fraction/Mantissa (23 bits) directly from binary strings.
- **Hidden Bit Restoration:** Automatically restores the implicit leading bit ($1.M$) for normalized operands.
- **Exponent Alignment:** Identifies exponent differences and performs bitwise right-shifting to align mantissa scales.
- **Signed Mantissa Arithmetic:** Handles both effective addition and subtraction depending on operand signs.
- **Post-Normalization Pipeline:**
  - Handles carry overflows ($\ge 2^{24}$) with right-shift and exponent increment.
  - Resolves cancellation underflows ($< 2^{23}$) with left-shift and exponent decrement.
- **Bit-Accurate Serialization:** Re-packs sign, 8-bit exponent, and 23-bit mantissa using `std::bitset`.

---

## 🔄 Arithmetic Pipeline

```text
Input X (32-bit)  ───►  [Sign_X]  [Exp_X]  [Mant_X + 2^23] ───┐
                                                              ├──► [Exponent Alignment]
Input Y (32-bit)  ───►  [Sign_Y]  [Exp_Y]  [Mant_Y + 2^23] ───┘           │
                                                                           ▼
                                                              [Signed Mantissa Add/Sub]
                                                                           │
                                                                           ▼
Output (32-bit)   ◄─── [Re-pack]  ◄─── [Normalization]  ◄────── [Overflow / Underflow Check]
