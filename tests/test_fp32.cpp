// Conformance tests for fp32: every operation is checked bit-for-bit against the
// host's hardware FPU (which is assumed to be IEEE 754 compliant — true for any
// SSE/NEON/RISC-V machine) in all four rounding modes.
//
//   1. hand-picked edge cases (zeros, infinities, NaNs, subnormals, overflow,
//      cancellation, round-to-even ties, the original program's failure cases);
//   2. exhaustive coverage of special-value combinations;
//   3. millions of random operand pairs drawn from distributions that stress
//      alignment shifts, cancellation, subnormal results and overflow.
//
// Build with -frounding-math so the compiler does not constant-fold or reorder
// floating-point operations across fesetround().

#include <cfenv>
#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <vector>

#include "fp32/fp32.hpp"

namespace {

using fp32::bits_t;
using fp32::Rounding;

struct ModeInfo {
  Rounding mode;
  int fe_mode;
  const char* name;
};

const ModeInfo kModes[] = {
    {Rounding::NearestEven, FE_TONEAREST, "nearest-even"},
    {Rounding::TowardZero, FE_TOWARDZERO, "toward-zero"},
    {Rounding::TowardPositive, FE_UPWARD, "toward-+inf"},
    {Rounding::TowardNegative, FE_DOWNWARD, "toward--inf"},
};

struct OpInfo {
  char symbol;
  bits_t (*model)(bits_t, bits_t, Rounding);
  float (*hardware)(float, float);
};

// `volatile` stops the compiler from folding the operation at compile time,
// which would bypass the dynamic rounding mode.
float hw_add(float a, float b) { volatile float r = a + b; return r; }
float hw_sub(float a, float b) { volatile float r = a - b; return r; }
float hw_mul(float a, float b) { volatile float r = a * b; return r; }
float hw_div(float a, float b) { volatile float r = a / b; return r; }

const OpInfo kOps[] = {
    {'+', fp32::add, hw_add},
    {'-', fp32::sub, hw_sub},
    {'*', fp32::mul, hw_mul},
    {'/', fp32::div, hw_div},
};

// NaN payload/sign are implementation-defined; treat any two NaNs as equal.
bool same_bits(bits_t expected, bits_t actual) {
  if (fp32::is_nan(expected) && fp32::is_nan(actual)) return true;
  return expected == actual;
}

struct Stats {
  std::uint64_t checked = 0;
  std::uint64_t failed = 0;
  std::vector<std::string> first_failures;
};

void check(Stats& s, const OpInfo& op, const ModeInfo& m, bits_t a, bits_t b) {
  std::fesetround(m.fe_mode);
  const bits_t expected = fp32::from_float(op.hardware(fp32::to_float(a), fp32::to_float(b)));
  std::fesetround(FE_TONEAREST);
  const bits_t actual = op.model(a, b, m.mode);
  ++s.checked;
  if (!same_bits(expected, actual)) {
    ++s.failed;
    if (s.first_failures.size() < 10) {
      char buf[256];
      std::snprintf(buf, sizeof buf, "  %s  0x%08" PRIX32 " %c 0x%08" PRIX32 "  expected 0x%08" PRIX32 "  got 0x%08" PRIX32,
                    m.name, a, op.symbol, b, expected, actual);
      s.first_failures.emplace_back(buf);
    }
  }
}

void check_all_modes(Stats& s, bits_t a, bits_t b) {
  for (const OpInfo& op : kOps)
    for (const ModeInfo& m : kModes) check(s, op, m, a, b);
}

// --- 1. hand-picked cases ---------------------------------------------------

const bits_t kInteresting[] = {
    0x00000000, 0x80000000,              // ±0
    0x00000001, 0x80000001,              // ±min subnormal
    0x00400000, 0x007FFFFF,              // subnormals
    0x00800000, 0x80800000,              // ±min normal
    0x00800001, 0x00FFFFFF,
    0x3F800000, 0xBF800000,              // ±1
    0x3F800001, 0x3FFFFFFF,              // 1+ulp, just under 2
    0x40000000, 0x40400000, 0x40490FDB,  // 2, 3, π
    0x3DCCCCCD, 0x3E4CCCCD,              // 0.1, 0.2
    0x3EAAAAAB,                          // 1/3
    0x4B800000, 0x4B000000,              // 2^24, 2^23
    0x7F000000, 0x7F7FFFFF, 0xFF7FFFFF,  // 2^127, ±max finite
    0x7F7FFFFE, 0x7E800000,
    0x7F800000, 0xFF800000,              // ±∞
    0x7FC00000, 0x7F800001, 0xFFC00001,  // NaNs (quiet, signalling, negative)
    0x33800000, 0x34000000, 0x34800000,  // 2^-24, 2^-23, 2^-22  (ulp-of-1 neighbourhood)
    0x1E3CE508, 0x5F000000, 0x1F800000,  // assorted magnitudes
    0x2F800000, 0x0F800000, 0x6F800000,
};

void run_hand_picked(Stats& s) {
  for (bits_t a : kInteresting)
    for (bits_t b : kInteresting) check_all_modes(s, a, b);

  // Cases that broke the original program.
  check_all_modes(s, fp32::from_float(1.5f), fp32::from_float(-1.5f));   // x + (−x): infinite loop
  check_all_modes(s, fp32::from_float(0.1f), fp32::from_float(0.2f));    // missing rounding: 1 ulp low
  check_all_modes(s, fp32::from_float(1e-40f), fp32::from_float(1e-40f));  // subnormals mishandled
  check_all_modes(s, fp32::from_float(16777216.0f), fp32::from_float(1.0f));  // tie → even
  check_all_modes(s, fp32::from_float(16777217.0f), fp32::from_float(1.0f));
  check_all_modes(s, fp32::from_float(3.0f), fp32::from_float(1.0f));    // 3/1 exact, 1/3 inexact
}

// --- 2. random operands -----------------------------------------------------

struct Generator {
  std::mt19937_64 rng{0x5EEDu};

  bits_t any_bits() { return static_cast<bits_t>(rng()); }

  bits_t with_exponent(int lo, int hi) {
    std::uniform_int_distribution<int> e(lo, hi);
    return (static_cast<bits_t>(rng() & 1u) << 31) | (static_cast<bits_t>(e(rng)) << 23) |
           (static_cast<bits_t>(rng()) & fp32::kFractionMask);
  }
  bits_t normal() { return with_exponent(1, 254); }
  bits_t near_one() { return with_exponent(120, 134); }   // alignment shifts of a few bits
  bits_t tiny() { return with_exponent(0, 30); }          // subnormal / underflow territory
  bits_t huge() { return with_exponent(220, 254); }       // overflow territory
  bits_t subnormal() { return (static_cast<bits_t>(rng() & 1u) << 31) | (static_cast<bits_t>(rng()) & fp32::kFractionMask); }

  // Nearly equal magnitudes with opposite signs → massive cancellation.
  std::pair<bits_t, bits_t> cancelling() {
    const bits_t a = normal();
    std::uniform_int_distribution<int> d(-4, 4);
    const bits_t b = (fp32::magnitude(a) + static_cast<bits_t>(d(rng))) | ((a & fp32::kSignMask) ^ fp32::kSignMask);
    return {a, fp32::is_finite(b) ? b : a};
  }
};

void run_random(Stats& s, std::uint64_t pairs_per_family) {
  Generator g;
  for (std::uint64_t i = 0; i < pairs_per_family; ++i) {
    check_all_modes(s, g.any_bits(), g.any_bits());
    check_all_modes(s, g.normal(), g.normal());
    check_all_modes(s, g.near_one(), g.near_one());
    check_all_modes(s, g.tiny(), g.tiny());
    check_all_modes(s, g.huge(), g.huge());
    check_all_modes(s, g.subnormal(), g.normal());
    check_all_modes(s, g.huge(), g.tiny());
    const auto [a, b] = g.cancelling();
    check_all_modes(s, a, b);
  }
}

// --- 3. properties that do not need the hardware ----------------------------

int run_properties() {
  int failures = 0;
  auto expect = [&](bool ok, const char* what) {
    if (!ok) {
      std::printf("  property failed: %s\n", what);
      ++failures;
    }
  };
  using namespace fp32;
  expect(add(from_float(1.5f), from_float(-1.5f)) == kPositiveZero, "1.5 + (-1.5) == +0");
  expect(add(from_float(1.5f), from_float(-1.5f), Rounding::TowardNegative) == kNegativeZero,
         "1.5 + (-1.5) == -0 when rounding toward -inf");
  expect(add(kPositiveInfinity, kNegativeInfinity) == kDefaultNaN, "inf - inf is the default NaN");
  expect(mul(kPositiveZero, kPositiveInfinity) == kDefaultNaN, "0 * inf is the default NaN");
  expect(div(kPositiveZero, kPositiveZero) == kDefaultNaN, "0 / 0 is the default NaN");
  expect(div(from_float(1.0f), kPositiveZero) == kPositiveInfinity, "1 / +0 == +inf");
  expect(div(from_float(1.0f), kNegativeZero) == kNegativeInfinity, "1 / -0 == -inf");
  expect(add(kMaxFinite, kMaxFinite) == kPositiveInfinity, "max + max overflows to +inf");
  expect(add(kMaxFinite, kMaxFinite, Rounding::TowardZero) == kMaxFinite, "overflow toward zero saturates");
  expect(mul(kMinSubnormal, from_float(0.5f)) == kPositiveZero, "min_subnormal * 0.5 ties to even (zero)");
  expect(mul(0x00000003u, from_float(0.5f)) == 0x00000002u, "3*min_sub * 0.5 ties to even (2*min_sub)");
  expect(sub(from_float(1.0f), from_float(1.0f)) == kPositiveZero, "1 - 1 == +0");
  expect(from_binary_string("0 01111111 00000000000000000000000") == from_float(1.0f), "parse 1.0");
  expect(to_binary_string(from_float(-2.0f)) == "1 10000000 00000000000000000000000", "format -2.0");
  bool threw = false;
  try { (void)from_binary_string("0101"); } catch (const std::invalid_argument&) { threw = true; }
  expect(threw, "short binary string is rejected");

  // constexpr evaluation: the whole datapath is usable at compile time.
  constexpr bits_t two = add(0x3F800000u, 0x3F800000u);
  static_assert(two == 0x40000000u, "1 + 1 == 2 at compile time");
  constexpr bits_t third = div(0x3F800000u, 0x40400000u);
  static_assert(third == 0x3EAAAAABu, "1 / 3 rounds to 0x3EAAAAAB at compile time");
  return failures;
}

}  // namespace

int main(int argc, char** argv) {
  const std::uint64_t pairs = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 250'000;

  Stats hand, rnd;
  run_hand_picked(hand);
  run_random(rnd, pairs);
  const int prop_failures = run_properties();

  std::printf("hand-picked cases : %" PRIu64 " checks, %" PRIu64 " failures\n", hand.checked, hand.failed);
  std::printf("random operands   : %" PRIu64 " checks, %" PRIu64 " failures\n", rnd.checked, rnd.failed);
  std::printf("properties        : %d failures\n", prop_failures);
  for (const auto& f : hand.first_failures) std::puts(f.c_str());
  for (const auto& f : rnd.first_failures) std::puts(f.c_str());

  const bool ok = hand.failed == 0 && rnd.failed == 0 && prop_failures == 0;
  std::puts(ok ? "ALL TESTS PASSED" : "TESTS FAILED");
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
