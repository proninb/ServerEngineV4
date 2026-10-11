// Standalone synthetic CPU benchmark; not a UnitProXL benchmark.
#include "../server_engine/project/parser/header_syntax_fast_01.hpp"
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>
using namespace cw::server;
int main(int argc, char** argv) {
    const std::size_t records = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 23333;
    const std::size_t members = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 30;
    const std::size_t repeats = argc > 3 ? std::strtoull(argv[3], nullptr, 10) : 20;
    std::vector<token_kind> tokens;
    tokens.reserve(records * (members * 3 + 4));
    for (std::size_t r = 0; r < records; ++r) {
        tokens.insert(tokens.end(), {token_kind::kw_struct, token_kind::identifier,
                                      token_kind::l_brace});
        for (std::size_t m = 0; m < members; ++m)
            tokens.insert(tokens.end(), {token_kind::kw_int, token_kind::identifier,
                                          token_kind::semicolon});
        tokens.insert(tokens.end(), {token_kind::r_brace, token_kind::semicolon});
    }
    header_syntax_result_01 result;
    result.records.reserve(records);
    result.members.reserve(records * members);
    if (!header_syntax_fast_01::scan(tokens, result) ||
        result.records.size() != records || result.members.size() != records * members)
        return 2;
    std::vector<double> elapsed;
    for (std::size_t n = 0; n < repeats + 3; ++n) {
        auto a = std::chrono::steady_clock::now();
        if (!header_syntax_fast_01::scan(tokens, result)) return 3;
        auto b = std::chrono::steady_clock::now();
        if (n >= 3) elapsed.push_back(std::chrono::duration<double, std::milli>(b-a).count());
    }
    std::sort(elapsed.begin(), elapsed.end());
    std::cout << "mode=synthetic-token-kind-only\n"
              << "tokens=" << tokens.size() << " records=" << result.records.size()
              << " members=" << result.members.size() << " skipped="
              << result.skipped_declarations << " malformed=" << result.malformed
              << "\nmedian_syntax_ms=" << elapsed[elapsed.size()/2]
              << "\nmodel_bytes=" << result.records.size()*sizeof(header_syntax_record_01)
                  + result.members.size()*sizeof(header_syntax_member_01) << '\n';
}
