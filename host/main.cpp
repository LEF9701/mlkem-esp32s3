// main.cpp (Ubuntu host) - run the ML-KEM experiments on your ThinkPad.
#include <chrono>
#include <cstdio>
#include <sys/random.h>

#include "experiments.hpp"

// mlkem-native needs a source of secure randomness and leaves it to us.
// On Linux, getrandom() reads the kernel CSPRNG. Return 0 = success.
extern "C" int randombytes(std::uint8_t* out, std::size_t len) {
  while (len > 0) {
    ssize_t n = getrandom(out, len, 0);
    if (n < 0) return -1;
    out += n;
    len -= static_cast<std::size_t>(n);
  }
  return 0;
}

static std::uint64_t now_us() {
  using namespace std::chrono;
  return static_cast<std::uint64_t>(
      duration_cast<microseconds>(steady_clock::now().time_since_epoch()).count());
}

int main() {
  std::printf("ML-KEM-%d lab on Ubuntu (x86-64)\n", pq::kParameterSet);

  bool ok = true;
  ok &= lab::exp1_known_answer_test();
  ok &= lab::exp2_key_exchange();
  ok &= lab::exp3_tampering();
  lab::exp4_benchmark(now_us, 1000);

  std::printf("\n%s\n", ok ? "All experiments passed." : "Some experiments FAILED.");
  return ok ? 0 : 1;
}
