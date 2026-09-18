// fp32.hpp — a bit-exact software model of an IEEE 754 binary32 arithmetic unit.
//
// The four basic operations (+ − × ÷) are implemented with integer arithmetic
// only, following the classic hardware datapath:
//
//     unpack → (align | multiply | divide) → normalise → round → pack
//
// Every result is bit-identical to a compliant hardware FPU for all finite,
// infinite, NaN and subnormal operands, in all four IEEE 754 rounding modes.
// NaN *payloads* are implementation-defined by the standard (§6.2); this model
// propagates the payload of the first NaN operand and returns the default NaN
// (0x7FC00000) for invalid operations.
//
// Header-only, C++17, no dependencies.  SPDX-License-Identifier: MIT

#pragma once

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>

namespace fp32 {

using bits_t = std::uint32_t;

// ---------------------------------------------------------------------------
// binary32 layout:  [31] sign | [30:23] biased exponent | [22:0] fraction
// ---------------------------------------------------------------------------
inline constexpr int kFractionBits = 23;
inline constexpr int kExponentBits = 8;
inline constexpr int kBias = 127;
inline constexpr int kExponentMax = 0xFF;  // reserved for Inf / NaN

inline constexpr bits_t kSignMask = 0x8000'0000u;
inline constexpr bits_t kExponentMask = 0x7F80'0000u;
inline constexpr bits_t kFractionMask = 0x007F'FFFFu;
inline constexpr bits_t kHiddenBit = 0x0080'0000u;  // implicit leading 1
inline constexpr bits_t kQuietBit = 0x0040'0000u;   // MSB of the fraction marks a quiet NaN

inline constexpr bits_t kPositiveZero = 0x0000'0000u;
inline constexpr bits_t kNegativeZero = 0x8000'0000u;
inline constexpr bits_t kPositiveInfinity = 0x7F80'0000u;
inline constexpr bits_t kNegativeInfinity = 0xFF80'0000u;
inline constexpr bits_t kDefaultNaN = 0x7FC0'0000u;
inline constexpr bits_t kMaxFinite = 0x7F7F'FFFFu;      // 3.4028235e38
inline constexpr bits_t kMinNormal = 0x0080'0000u;      // 2^-126
inline constexpr bits_t kMinSubnormal = 0x0000'0001u;   // 2^-149

/// IEEE 754 rounding-direction attributes.
enum class Rounding { NearestEven, TowardZero, TowardPositive, TowardNegative };

/// The three raw fields of a binary32 word.
struct Fields {
  bool sign;
  unsigned exponent;  // biased, 0..255
  bits_t fraction;    // 23 bits, without the hidden bit
};

// ---------------------------------------------------------------------------
// Classification and field access
// ---------------------------------------------------------------------------
constexpr Fields decompose(bits_t x) noexcept {
  return {(x & kSignMask) != 0, (x >> kFractionBits) & 0xFFu, x & kFractionMask};
}

constexpr bits_t compose(bool sign, unsigned exponent, bits_t fraction) noexcept {
  return (sign ? kSignMask : 0u) | ((exponent & 0xFFu) << kFractionBits) | (fraction & kFractionMask);
}

constexpr bool sign_bit(bits_t x) noexcept { return (x & kSignMask) != 0; }
constexpr bits_t negate(bits_t x) noexcept { return x ^ kSignMask; }
constexpr bits_t magnitude(bits_t x) noexcept { return x & ~kSignMask; }

constexpr bool is_nan(bits_t x) noexcept {
  return (x & kExponentMask) == kExponentMask && (x & kFractionMask) != 0;
}
constexpr bool is_inf(bits_t x) noexcept { return magnitude(x) == kPositiveInfinity; }
constexpr bool is_zero(bits_t x) noexcept { return magnitude(x) == 0; }
constexpr bool is_finite(bits_t x) noexcept { return (x & kExponentMask) != kExponentMask; }
constexpr bool is_subnormal(bits_t x) noexcept {
  return (x & kExponentMask) == 0 && (x & kFractionMask) != 0;
}
constexpr bool is_normal(bits_t x) noexcept {
  return (x & kExponentMask) != 0 && (x & kExponentMask) != kExponentMask;
}

// ---------------------------------------------------------------------------
// Implementation details
// ---------------------------------------------------------------------------
namespace detail {

/// A finite operand taken apart:  value = (−1)^sign · significand · 2^(exponent − bias − 23).
/// Normal numbers carry the hidden bit at position 23.  Zero and subnormals are
/// represented with exponent == 1 (the scale of the smallest normal number) and
/// significand < 2^23, which makes alignment in the adder uniform.
struct Unpacked {
  bool sign;
  int exponent;               // biased; may leave the 1..254 range in intermediate steps
  std::uint32_t significand;  // 24 significant bits
};

constexpr Unpacked unpack(bits_t x) noexcept {
  const Fields f = decompose(x);
  if (f.exponent == 0) return {f.sign, 1, f.fraction};
  return {f.sign, static_cast<int>(f.exponent), f.fraction | kHiddenBit};
}

/// Shift a subnormal significand left until the hidden bit is set (used by × and ÷,
/// whose datapaths need a normalised 1.xxx operand).  The exponent may become ≤ 0.
constexpr Unpacked normalize(Unpacked u) noexcept {
  while (u.significand != 0 && (u.significand & kHiddenBit) == 0) {
    u.significand <<= 1;
    --u.exponent;
  }
  return u;
}

/// Logical right shift that ORs every shifted-out bit into the LSB ("sticky bit").
/// This is what lets a finite-width datapath round correctly: the sticky bit records
/// that *something* non-zero was discarded below the round bit.
constexpr std::uint64_t shift_right_sticky(std::uint64_t x, int n) noexcept {
  if (n <= 0) return x;
  if (n >= 64) return x != 0 ? 1u : 0u;
  const std::uint64_t lost = x & ((std::uint64_t{1} << n) - 1u);
  return (x >> n) | (lost != 0 ? 1u : 0u);
}

/// Result for a magnitude that is too large to represent, per rounding mode
/// (IEEE 754 §7.4: overflow rounds to ±∞ or ±MaxFinite depending on the direction).
constexpr bits_t overflow(bool sign, Rounding mode) noexcept {
  const bool to_infinity = mode == Rounding::NearestEven ||
                           (mode == Rounding::TowardPositive && !sign) ||
                           (mode == Rounding::TowardNegative && sign);
  const bits_t mag = to_infinity ? kPositiveInfinity : kMaxFinite;
  return sign ? (mag | kSignMask) : mag;
}

/// Round a result significand and pack it into a binary32 word.
///
/// `sig` uses the "GRS" layout, i.e. the 24-bit significand followed by three
/// extra bits that hold everything the datapath knows about the discarded tail:
///
///     bit 26      leading bit (0 ⇒ the result is subnormal)
///     bits 25..3  the 23 fraction bits
///     bit 2       guard bit   — first discarded bit (worth ½ ulp)
///     bit 1       round bit   — second discarded bit
///     bit 0       sticky bit  — OR of all remaining discarded bits
///
/// `exponent` is the biased exponent that belongs to bit 26; callers guarantee
/// exponent ≥ 1 and that bit 26 is set whenever exponent > 1.
constexpr bits_t round_and_pack(bool sign, int exponent, std::uint32_t sig, Rounding mode) noexcept {
  const std::uint32_t lsb = (sig >> 3) & 1u;
  const std::uint32_t guard = (sig >> 2) & 1u;
  const std::uint32_t below_guard = sig & 0x3u;  // round | sticky
  const bool inexact = (sig & 0x7u) != 0;

  bool increment = false;
  switch (mode) {
    case Rounding::NearestEven:
      // Round up if the tail is > ½ ulp, or == ½ ulp and the result is odd.
      increment = guard != 0 && (below_guard != 0 || lsb != 0);
      break;
    case Rounding::TowardZero:
      break;  // truncation
    case Rounding::TowardPositive:
      increment = !sign && inexact;
      break;
    case Rounding::TowardNegative:
      increment = sign && inexact;
      break;
  }

  std::uint32_t significand = (sig >> 3) + (increment ? 1u : 0u);
  if (significand == (1u << 24)) {  // 1.111…1 rounded up to 10.000…0
    significand = kHiddenBit;
    ++exponent;
  }
  if (significand < kHiddenBit) return compose(sign, 0, significand);  // subnormal or zero
  if (exponent >= kExponentMax) return overflow(sign, mode);
  return compose(sign, static_cast<unsigned>(exponent), significand & kFractionMask);
}

/// Quiet NaN propagation: keep the payload of the first NaN operand, set the quiet bit.
constexpr bits_t propagate_nan(bits_t a, bits_t b) noexcept {
  return (is_nan(a) ? a : b) | kQuietBit;
}

}  // namespace detail

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

/// a + b
constexpr bits_t add(bits_t a, bits_t b, Rounding mode = Rounding::NearestEven) noexcept {
  // --- special operands -----------------------------------------------------
  if (is_nan(a) || is_nan(b)) return detail::propagate_nan(a, b);
  if (is_inf(a)) return (is_inf(b) && sign_bit(a) != sign_bit(b)) ? kDefaultNaN : a;  // ∞ − ∞ is invalid
  if (is_inf(b)) return b;
  if (is_zero(a) && is_zero(b)) {
    if (sign_bit(a) == sign_bit(b)) return a;  // (+0)+(+0)=+0, (−0)+(−0)=−0
    return mode == Rounding::TowardNegative ? kNegativeZero : kPositiveZero;
  }

  // --- unpack and order so that |x| ≥ |y| -----------------------------------
  detail::Unpacked x = detail::unpack(a);
  detail::Unpacked y = detail::unpack(b);
  if (x.exponent < y.exponent || (x.exponent == y.exponent && x.significand < y.significand)) {
    const detail::Unpacked t = x;  // (std::swap is not constexpr before C++20)
    x = y;
    y = t;
  }

  // --- align: shift the smaller operand right by the exponent difference -----
  // Both significands are widened by the three guard/round/sticky bits first.
  const std::uint64_t sx = std::uint64_t{x.significand} << 3;
  const std::uint64_t sy = detail::shift_right_sticky(std::uint64_t{y.significand} << 3, x.exponent - y.exponent);
  int exponent = x.exponent;

  // --- add or subtract magnitudes; the sign is that of the larger operand ----
  std::uint64_t sum = (x.sign == y.sign) ? sx + sy : sx - sy;
  if (sum == 0) {  // exact cancellation: x + (−x)
    return mode == Rounding::TowardNegative ? kNegativeZero : kPositiveZero;
  }

  // --- normalise: bring the leading one to bit 26 -----------------------------
  if (sum >> 27) {  // carry out (1x.xxx): shift right once
    sum = detail::shift_right_sticky(sum, 1);
    ++exponent;
  } else {  // cancellation (0.0…1x): shift left, but never below the subnormal scale
    while ((sum >> 26) == 0 && exponent > 1) {
      sum <<= 1;
      --exponent;
    }
  }
  return detail::round_and_pack(x.sign, exponent, static_cast<std::uint32_t>(sum), mode);
}

/// a − b  (identical to a + (−b), as required by IEEE 754)
constexpr bits_t sub(bits_t a, bits_t b, Rounding mode = Rounding::NearestEven) noexcept {
  return add(a, negate(b), mode);
}

/// a × b
constexpr bits_t mul(bits_t a, bits_t b, Rounding mode = Rounding::NearestEven) noexcept {
  const bool sign = sign_bit(a) != sign_bit(b);

  if (is_nan(a) || is_nan(b)) return detail::propagate_nan(a, b);
  if (is_inf(a) || is_inf(b)) {
    if (is_zero(a) || is_zero(b)) return kDefaultNaN;  // 0 × ∞ is invalid
    return sign ? kNegativeInfinity : kPositiveInfinity;
  }
  if (is_zero(a) || is_zero(b)) return sign ? kNegativeZero : kPositiveZero;

  const detail::Unpacked x = detail::normalize(detail::unpack(a));
  const detail::Unpacked y = detail::normalize(detail::unpack(b));

  // Exponents add (one bias must be removed); 24 × 24-bit significands give a
  // 48-bit product whose leading one is at bit 46 or 47.
  int exponent = x.exponent + y.exponent - kBias;
  std::uint64_t product = std::uint64_t{x.significand} * y.significand;

  if (product >> 47) {  // 1x.xxx — leading one at bit 47
    product = detail::shift_right_sticky(product, 47 - 26);
    ++exponent;
  } else {  // 1.xxx — leading one at bit 46
    product = detail::shift_right_sticky(product, 46 - 26);
  }
  if (exponent < 1) {  // result is below the normal range: denormalise
    product = detail::shift_right_sticky(product, 1 - exponent);
    exponent = 1;
  }
  return detail::round_and_pack(sign, exponent, static_cast<std::uint32_t>(product), mode);
}

/// a ÷ b
constexpr bits_t div(bits_t a, bits_t b, Rounding mode = Rounding::NearestEven) noexcept {
  const bool sign = sign_bit(a) != sign_bit(b);

  if (is_nan(a) || is_nan(b)) return detail::propagate_nan(a, b);
  if (is_inf(a)) return is_inf(b) ? kDefaultNaN : (sign ? kNegativeInfinity : kPositiveInfinity);  // ∞/∞ invalid
  if (is_inf(b)) return sign ? kNegativeZero : kPositiveZero;
  if (is_zero(b)) return is_zero(a) ? kDefaultNaN : (sign ? kNegativeInfinity : kPositiveInfinity);  // 0/0 invalid
  if (is_zero(a)) return sign ? kNegativeZero : kPositiveZero;

  const detail::Unpacked x = detail::normalize(detail::unpack(a));
  const detail::Unpacked y = detail::normalize(detail::unpack(b));

  // Exponents subtract (the bias must be re-added).  The significand ratio lies
  // in (½, 2); doubling the dividend when it is smaller keeps it in [1, 2).
  int exponent = x.exponent - y.exponent + kBias;
  std::uint64_t dividend = x.significand;
  if (dividend < y.significand) {
    dividend <<= 1;
    --exponent;
  }

  // Long division producing 27 quotient bits (1 + 23 fraction + guard + round + 1);
  // a non-zero remainder becomes the sticky bit.
  dividend <<= 26;
  std::uint64_t quotient = dividend / y.significand;  // in [2^26, 2^27)
  if (dividend % y.significand != 0) quotient |= 1u;

  if (exponent < 1) {  // denormalise
    quotient = detail::shift_right_sticky(quotient, 1 - exponent);
    exponent = 1;
  }
  return detail::round_and_pack(sign, exponent, static_cast<std::uint32_t>(quotient), mode);
}

// ---------------------------------------------------------------------------
// Conversions (host float ⇄ bits, text ⇄ bits)
// ---------------------------------------------------------------------------

inline bits_t from_float(float f) noexcept {
  static_assert(sizeof(float) == sizeof(bits_t), "float must be 32 bits wide");
  bits_t b;
  std::memcpy(&b, &f, sizeof b);
  return b;
}

inline float to_float(bits_t b) noexcept {
  float f;
  std::memcpy(&f, &b, sizeof f);
  return f;
}

/// Parse a 32-character string of 0/1 (spaces and underscores between fields are ignored).
inline bits_t from_binary_string(std::string_view text) {
  bits_t value = 0;
  int count = 0;
  for (const char c : text) {
    if (c == ' ' || c == '_' || c == '\'') continue;
    if (c != '0' && c != '1') throw std::invalid_argument("binary string may contain only 0 and 1");
    value = (value << 1) | static_cast<bits_t>(c - '0');
    ++count;
  }
  if (count != 32) throw std::invalid_argument("binary string must contain exactly 32 bits");
  return value;
}

/// Render the word as "s eeeeeeee fffffffffffffffffffffff" (or without separators).
inline std::string to_binary_string(bits_t x, bool separate_fields = true) {
  std::string out;
  out.reserve(34);
  for (int i = 31; i >= 0; --i) {
    out.push_back(((x >> i) & 1u) ? '1' : '0');
    if (separate_fields && (i == 31 || i == 23)) out.push_back(' ');
  }
  return out;
}

}  // namespace fp32
