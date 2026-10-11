#include <cstdint>
#include <cstdlib>
#include <iostream>
extern "C" std::uint64_t v343_probe_baseline(std::uint64_t);
extern "C" std::uint64_t v343_probe_lazy(std::uint64_t);
int main() {
  for(std::uint64_t x=0;x<1024;++x) {
    if(v343_probe_baseline(x)!=v343_probe_lazy(x)) {
      std::cerr << "V343_CODEGEN_PARITY_FAIL input="<<x<<'\n';
      return 1;
    }
  }
  std::cout << "V343_CODEGEN_RUNTIME_PASS cases=1024\n";
}
