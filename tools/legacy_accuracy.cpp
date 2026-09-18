// legacy_accuracy — measures how often the ORIGINAL adder (legacy/Project_Final.cpp)
// produced the correctly rounded result, and compares it with the new fp32::add.
//
// The legacy routine only understands normal operands and loops forever when the
// result is exactly zero, so the comparison is restricted to random pairs of
// normal numbers whose hardware sum is non-zero.  Results are printed as a table
// and, with --csv, as CSV rows "exponent_gap,legacy_exact,legacy_1ulp,legacy_worse,fp32_exact"
// suitable for plotting (see tools/plot_accuracy.py).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>

#define main legacy_main  // pull in the original program without its entry point
#include "../legacy/Project_Final.cpp"
#undef main

#include "fp32/fp32.hpp"

namespace {

std::string to_plain_bits(fp32::bits_t x) { return fp32::to_binary_string(x, /*separate_fields=*/false); }

fp32::bits_t from_plain_bits(const std::string& s) { return fp32::from_binary_string(s); }

struct Row {
  long exact = 0, one_ulp = 0, worse = 0, fp32_exact = 0, total = 0;
};

long ulp_distance(fp32::bits_t a, fp32::bits_t b) {
  // Both values are finite and non-zero with the same sign in this experiment.
  return std::labs(static_cast<long>(fp32::magnitude(a)) - static_cast<long>(fp32::magnitude(b)));
}

}  // namespace

int main(int argc, char** argv) {
  const long per_gap = argc > 1 && std::strcmp(argv[1], "--csv") != 0 ? std::strtol(argv[1], nullptr, 10) : 100000;
  bool csv = false;
  for (int i = 1; i < argc; ++i) csv = csv || std::strcmp(argv[i], "--csv") == 0;

  std::mt19937_64 rng(2026);
  std::uniform_int_distribution<int> exp_dist(60, 190);          // keep sums well inside the normal range
  std::uniform_int_distribution<fp32::bits_t> frac_dist(0, fp32::kFractionMask);
  std::bernoulli_distribution coin(0.5);

  Row overall;
  if (csv) std::puts("exponent_gap,legacy_exact,legacy_1ulp,legacy_worse,fp32_exact,total");
  else {
    std::printf("Correctly rounded results for %ld random pairs of normal operands per exponent gap\n\n", per_gap);
    std::printf("%-12s | %-24s | %-24s | %-24s | %s\n", "exp. gap", "legacy exact", "legacy 1 ulp off", "legacy > 1 ulp off", "fp32::add exact");
    std::printf("-------------+--------------------------+--------------------------+--------------------------+----------------\n");
  }

  static const int kGaps[] = {0, 1, 2, 3, 4, 6, 8, 12, 16, 20, 23, 24, 25, 26};
  for (const int gap : kGaps) {
    Row row;
    for (long i = 0; i < per_gap; ++i) {
      const int ea = exp_dist(rng);
      const fp32::bits_t a = (coin(rng) ? fp32::kSignMask : 0u) | (static_cast<fp32::bits_t>(ea) << 23) | frac_dist(rng);
      const fp32::bits_t b = (coin(rng) ? fp32::kSignMask : 0u) | (static_cast<fp32::bits_t>(ea - gap) << 23) | frac_dist(rng);
      const float hw = fp32::to_float(a) + fp32::to_float(b);
      if (hw == 0.0f) continue;  // the legacy code would never terminate
      const fp32::bits_t expected = fp32::from_float(hw);

      const fp32::bits_t legacy = from_plain_bits(binary_addition(to_plain_bits(a), to_plain_bits(b)));
      const long d = ulp_distance(legacy, expected);
      ++row.total;
      if (legacy == expected) ++row.exact;
      else if (d == 1) ++row.one_ulp;
      else ++row.worse;
      if (fp32::add(a, b) == expected) ++row.fp32_exact;
    }
    overall.exact += row.exact; overall.one_ulp += row.one_ulp; overall.worse += row.worse;
    overall.fp32_exact += row.fp32_exact; overall.total += row.total;

    if (csv) std::printf("%d,%ld,%ld,%ld,%ld,%ld\n", gap, row.exact, row.one_ulp, row.worse, row.fp32_exact, row.total);
    else
      std::printf("%-12d | %7ld  (%5.1f %%)        | %7ld  (%5.1f %%)        | %7ld  (%5.1f %%)        | %7ld  (%5.1f %%)\n", gap,
                  row.exact, 100.0 * row.exact / row.total, row.one_ulp, 100.0 * row.one_ulp / row.total, row.worse,
                  100.0 * row.worse / row.total, row.fp32_exact, 100.0 * row.fp32_exact / row.total);
  }
  if (!csv) {
    std::printf("-------------+--------------------------+--------------------------+--------------------------+----------------\n");
    std::printf("%-12s | %7ld  (%5.1f %%)        | %7ld  (%5.1f %%)        | %7ld  (%5.1f %%)        | %7ld  (%5.1f %%)\n", "overall",
                overall.exact, 100.0 * overall.exact / overall.total, overall.one_ulp, 100.0 * overall.one_ulp / overall.total,
                overall.worse, 100.0 * overall.worse / overall.total, overall.fp32_exact, 100.0 * overall.fp32_exact / overall.total);
  }
  return 0;
}
