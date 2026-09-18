// fp32calc — command-line front end for the fp32 software FPU.
//
//   fp32calc <a> <op> <b> [--mode nearest|zero|up|down] [--trace]
//   fp32calc                      (interactive: prompts for the operands)
//
// Operands may be written as 32-bit binary strings ("0 01111111 000…", spaces
// optional), hexadecimal words ("0x3F800000") or decimal literals ("1.5", "-2e-3").

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <string_view>

#include "fp32/fp32.hpp"

namespace {

using fp32::bits_t;

void usage() {
  std::puts(
      "usage: fp32calc <a> <op> <b> [--mode nearest|zero|up|down] [--trace]\n"
      "       fp32calc            (interactive)\n"
      "\n"
      "  <op>   one of  +  -  x  *  /\n"
      "  <a>,<b> binary32 words as 32 binary digits (spaces allowed), 0x-prefixed hex,\n"
      "         or decimal literals such as 1.5, -2e-3, inf, nan\n"
      "  --mode rounding direction (default: nearest = round-to-nearest-even)\n"
      "  --trace print the unpacked fields of the operands and the result");
}

bool looks_binary(std::string_view s) {
  int digits = 0;
  for (char c : s) {
    if (c == '0' || c == '1') ++digits;
    else if (c != ' ' && c != '_' && c != '\'') return false;
  }
  return digits == 32;
}

bits_t parse_operand(const std::string& text) {
  if (looks_binary(text)) return fp32::from_binary_string(text);
  if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) {
    char* end = nullptr;
    const unsigned long v = std::strtoul(text.c_str(), &end, 16);
    if (*end != '\0' || v > 0xFFFFFFFFul) throw std::invalid_argument("bad hexadecimal word: " + text);
    return static_cast<bits_t>(v);
  }
  char* end = nullptr;
  const float f = std::strtof(text.c_str(), &end);
  if (end == text.c_str() || *end != '\0') throw std::invalid_argument("cannot parse operand: " + text);
  return fp32::from_float(f);
}

fp32::Rounding parse_mode(std::string_view m) {
  if (m == "nearest" || m == "even") return fp32::Rounding::NearestEven;
  if (m == "zero" || m == "trunc") return fp32::Rounding::TowardZero;
  if (m == "up" || m == "+inf") return fp32::Rounding::TowardPositive;
  if (m == "down" || m == "-inf") return fp32::Rounding::TowardNegative;
  throw std::invalid_argument(std::string("unknown rounding mode: ") + std::string(m));
}

const char* mode_name(fp32::Rounding m) {
  switch (m) {
    case fp32::Rounding::NearestEven: return "round to nearest, ties to even";
    case fp32::Rounding::TowardZero: return "round toward zero";
    case fp32::Rounding::TowardPositive: return "round toward +inf";
    case fp32::Rounding::TowardNegative: return "round toward -inf";
  }
  return "?";
}

const char* classify(bits_t x) {
  if (fp32::is_nan(x)) return "NaN";
  if (fp32::is_inf(x)) return "infinity";
  if (fp32::is_zero(x)) return "zero";
  if (fp32::is_subnormal(x)) return "subnormal";
  return "normal";
}

void print_word(const char* label, bits_t x) {
  std::printf("%-8s %s  0x%08X  % .9g  (%s)\n", label, fp32::to_binary_string(x).c_str(), x, fp32::to_float(x), classify(x));
}

void print_trace(const char* label, bits_t x) {
  const fp32::Fields f = fp32::decompose(x);
  const int unbiased = f.exponent == 0 ? 1 - fp32::kBias : static_cast<int>(f.exponent) - fp32::kBias;
  std::printf("  %-6s sign=%d  exponent=%3u (2^%+d)  fraction=0x%06X  significand=%s\n", label, f.sign, f.exponent, unbiased,
              f.fraction, f.exponent == 0 ? "0.f (no hidden bit)" : "1.f");
}

int compute(const std::string& a_text, const std::string& op, const std::string& b_text, fp32::Rounding mode, bool trace) {
  const bits_t a = parse_operand(a_text);
  const bits_t b = parse_operand(b_text);

  bits_t r;
  char symbol;
  if (op == "+") { r = fp32::add(a, b, mode); symbol = '+'; }
  else if (op == "-") { r = fp32::sub(a, b, mode); symbol = '-'; }
  else if (op == "*" || op == "x" || op == "X") { r = fp32::mul(a, b, mode); symbol = '*'; }
  else if (op == "/") { r = fp32::div(a, b, mode); symbol = '/'; }
  else throw std::invalid_argument("unknown operator: " + op);

  print_word("a", a);
  print_word("b", b);
  std::printf("%-8s %s\n", "op", std::string(1, symbol).append("   [").append(mode_name(mode)).append("]").c_str());
  print_word("result", r);
  if (trace) {
    std::puts("trace:");
    print_trace("a", a);
    print_trace("b", b);
    print_trace("result", r);
  }
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  fp32::Rounding mode = fp32::Rounding::NearestEven;
  bool trace = false;
  std::string positional[3];
  int n = 0;

  try {
    for (int i = 1; i < argc; ++i) {
      const std::string_view arg = argv[i];
      if (arg == "-h" || arg == "--help") { usage(); return 0; }
      if (arg == "--trace") { trace = true; continue; }
      if (arg == "--mode") {
        if (i + 1 >= argc) throw std::invalid_argument("--mode needs a value");
        mode = parse_mode(argv[++i]);
        continue;
      }
      if (n == 3) throw std::invalid_argument("too many arguments");
      positional[n++] = std::string(arg);
    }

    if (n == 0) {  // interactive mode
      std::cout << "First operand  (32-bit binary, 0x-hex or decimal): ";
      if (!std::getline(std::cin, positional[0])) return 1;
      std::cout << "Operator (+ - * /): ";
      if (!std::getline(std::cin, positional[1])) return 1;
      std::cout << "Second operand: ";
      if (!std::getline(std::cin, positional[2])) return 1;
      n = 3;
    }
    if (n != 3) { usage(); return 2; }
    return compute(positional[0], positional[1], positional[2], mode, trace);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "error: %s\n", e.what());
    return 2;
  }
}
