#if !defined(_MSC_VER) || !defined(_WIN32)
#error ServerEngineV4MsvcClassAbiOracle requires Microsoft C++ on Windows
#endif

#if !defined(_M_X64) && !defined(_M_IX86)
#error ServerEngineV4MsvcClassAbiOracle supports only MSVC x64 and Win32
#endif

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <type_traits>

namespace {

#if defined(_M_X64)
constexpr std::string_view architecture = "x64";
static_assert(sizeof(void*) == 8);
#else
constexpr std::string_view architecture = "Win32";
static_assert(sizeof(void*) == 4);
#endif

constexpr std::ptrdiff_t no_offset = -1;

template <typename Derived, typename Base>
[[nodiscard]] std::ptrdiff_t base_offset(
    Derived& value) noexcept {

    auto* complete =
        reinterpret_cast<const std::byte*>(
            &value);

    auto* base =
        reinterpret_cast<const std::byte*>(
            static_cast<Base*>(
                &value));

    return base - complete;
}

template <
    typename Derived,
    typename Owner,
    typename Member>
[[nodiscard]] std::ptrdiff_t member_offset(
    Derived& value,
    Member Owner::* member) noexcept {

    auto* complete =
        reinterpret_cast<const std::byte*>(
            &value);

    auto* address =
        reinterpret_cast<const std::byte*>(
            &(value.*member));

    return address - complete;
}

template <typename Value>
[[nodiscard]] std::ptrdiff_t array_stride() noexcept {
    Value values[2]{};

    auto* first =
        reinterpret_cast<const std::byte*>(
            &values[0]);

    auto* second =
        reinterpret_cast<const std::byte*>(
            &values[1]);

    return second - first;
}

template <typename Base, typename Derived>
void emit_single_common(
    std::uint32_t pack,
    std::string_view case_name,
    std::ptrdiff_t base_member0_local,
    std::ptrdiff_t base_member1_local,
    std::ptrdiff_t derived_member0) {

    Derived value{};

    std::cout
        << _MSC_VER << ','
        << _MSC_FULL_VER << ','
        << architecture << ','
        << sizeof(void*) << ','
        << pack << ','
        << case_name << ','
        << sizeof(Base) << ','
        << alignof(Base) << ','
        << 0 << ','
        << 0 << ','
        << 0 << ','
        << 0 << ','
        << sizeof(Derived) << ','
        << alignof(Derived) << ','
        << base_offset<Derived, Base>(
               value) << ','
        << no_offset << ','
        << no_offset << ','
        << base_member0_local << ','
        << base_member1_local << ','
        << no_offset << ','
        << no_offset << ','
        << no_offset << ','
        << no_offset << ','
        << derived_member0 << ','
        << array_stride<Derived>() << ','
        << (std::is_polymorphic_v<Base> ? 1 : 0) << ','
        << 0 << ','
        << 0 << ','
        << (std::is_polymorphic_v<Derived> ? 1 : 0) << ','
        << (std::is_empty_v<Base> ? 1 : 0) << ','
        << 0 << ','
        << 0
        << '\n';
}

template <
    typename Base,
    typename Derived,
    typename BaseMember0,
    typename DerivedMember0>
void emit_single_one(
    std::uint32_t pack,
    std::string_view case_name,
    BaseMember0 Base::* base_member0,
    DerivedMember0 Derived::* derived_member0) {

    Derived value{};

    const auto base =
        base_offset<Derived, Base>(
            value);

    emit_single_common<Base, Derived>(
        pack,
        case_name,
        member_offset(
            value,
            base_member0) -
            base,
        no_offset,
        member_offset(
            value,
            derived_member0));
}

template <
    typename Base,
    typename Derived,
    typename BaseMember0,
    typename BaseMember1,
    typename DerivedMember0>
void emit_single_two(
    std::uint32_t pack,
    std::string_view case_name,
    BaseMember0 Base::* base_member0,
    BaseMember1 Base::* base_member1,
    DerivedMember0 Derived::* derived_member0) {

    Derived value{};

    const auto base =
        base_offset<Derived, Base>(
            value);

    emit_single_common<Base, Derived>(
        pack,
        case_name,
        member_offset(
            value,
            base_member0) -
            base,
        member_offset(
            value,
            base_member1) -
            base,
        member_offset(
            value,
            derived_member0));
}

template <
    typename Base,
    typename Derived,
    typename DerivedMember0>
void emit_single_none(
    std::uint32_t pack,
    std::string_view case_name,
    DerivedMember0 Derived::* derived_member0) {

    Derived value{};

    emit_single_common<Base, Derived>(
        pack,
        case_name,
        no_offset,
        no_offset,
        member_offset(
            value,
            derived_member0));
}

template <typename Base0, typename Base1, typename Derived>
void emit_multiple_common(
    std::uint32_t pack,
    std::string_view case_name,
    std::ptrdiff_t base0_member0_local,
    std::ptrdiff_t base0_member1_local,
    std::ptrdiff_t base1_member0_local,
    std::ptrdiff_t base1_member1_local,
    std::ptrdiff_t derived_member0) {

    Derived value{};

    std::cout
        << _MSC_VER << ','
        << _MSC_FULL_VER << ','
        << architecture << ','
        << sizeof(void*) << ','
        << pack << ','
        << case_name << ','
        << sizeof(Base0) << ','
        << alignof(Base0) << ','
        << sizeof(Base1) << ','
        << alignof(Base1) << ','
        << 0 << ','
        << 0 << ','
        << sizeof(Derived) << ','
        << alignof(Derived) << ','
        << base_offset<Derived, Base0>(
               value) << ','
        << base_offset<Derived, Base1>(
               value) << ','
        << no_offset << ','
        << base0_member0_local << ','
        << base0_member1_local << ','
        << base1_member0_local << ','
        << base1_member1_local << ','
        << no_offset << ','
        << no_offset << ','
        << derived_member0 << ','
        << array_stride<Derived>() << ','
        << (std::is_polymorphic_v<Base0> ? 1 : 0) << ','
        << (std::is_polymorphic_v<Base1> ? 1 : 0) << ','
        << 0 << ','
        << (std::is_polymorphic_v<Derived> ? 1 : 0) << ','
        << (std::is_empty_v<Base0> ? 1 : 0) << ','
        << (std::is_empty_v<Base1> ? 1 : 0) << ','
        << 0
        << '\n';
}

template <
    typename Base0,
    typename Base1,
    typename Derived,
    typename Base0Member0,
    typename Base1Member0,
    typename DerivedMember0>
void emit_multiple_one_one(
    std::uint32_t pack,
    std::string_view case_name,
    Base0Member0 Base0::* base0_member0,
    Base1Member0 Base1::* base1_member0,
    DerivedMember0 Derived::* derived_member0) {

    Derived value{};

    const auto base0 =
        base_offset<Derived, Base0>(
            value);

    const auto base1 =
        base_offset<Derived, Base1>(
            value);

    emit_multiple_common<
        Base0,
        Base1,
        Derived>(
        pack,
        case_name,
        member_offset(
            value,
            base0_member0) -
            base0,
        no_offset,
        member_offset(
            value,
            base1_member0) -
            base1,
        no_offset,
        member_offset(
            value,
            derived_member0));
}

template <typename Base0, typename Base1, typename Base2, typename Derived>
void emit_multiple_three_common(
    std::uint32_t pack,
    std::string_view case_name,
    std::ptrdiff_t base0_member0_local,
    std::ptrdiff_t base1_member0_local,
    std::ptrdiff_t base2_member0_local,
    std::ptrdiff_t derived_member0) {

    Derived value{};

    std::cout
        << _MSC_VER << ','
        << _MSC_FULL_VER << ','
        << architecture << ','
        << sizeof(void*) << ','
        << pack << ','
        << case_name << ','
        << sizeof(Base0) << ','
        << alignof(Base0) << ','
        << sizeof(Base1) << ','
        << alignof(Base1) << ','
        << sizeof(Base2) << ','
        << alignof(Base2) << ','
        << sizeof(Derived) << ','
        << alignof(Derived) << ','
        << base_offset<Derived, Base0>(
               value) << ','
        << base_offset<Derived, Base1>(
               value) << ','
        << base_offset<Derived, Base2>(
               value) << ','
        << base0_member0_local << ','
        << no_offset << ','
        << base1_member0_local << ','
        << no_offset << ','
        << base2_member0_local << ','
        << no_offset << ','
        << derived_member0 << ','
        << array_stride<Derived>() << ','
        << (std::is_polymorphic_v<Base0> ? 1 : 0) << ','
        << (std::is_polymorphic_v<Base1> ? 1 : 0) << ','
        << (std::is_polymorphic_v<Base2> ? 1 : 0) << ','
        << (std::is_polymorphic_v<Derived> ? 1 : 0) << ','
        << (std::is_empty_v<Base0> ? 1 : 0) << ','
        << (std::is_empty_v<Base1> ? 1 : 0) << ','
        << (std::is_empty_v<Base2> ? 1 : 0)
        << '\n';
}

template <
    typename Base0,
    typename Base1,
    typename Base2,
    typename Derived,
    typename Base0Member0,
    typename Base1Member0,
    typename Base2Member0,
    typename DerivedMember0>
void emit_multiple_three_one_one_one(
    std::uint32_t pack,
    std::string_view case_name,
    Base0Member0 Base0::* base0_member0,
    Base1Member0 Base1::* base1_member0,
    Base2Member0 Base2::* base2_member0,
    DerivedMember0 Derived::* derived_member0) {

    Derived value{};

    const auto base0 =
        base_offset<Derived, Base0>(
            value);

    const auto base1 =
        base_offset<Derived, Base1>(
            value);

    const auto base2 =
        base_offset<Derived, Base2>(
            value);

    emit_multiple_three_common<
        Base0,
        Base1,
        Base2,
        Derived>(
        pack,
        case_name,
        member_offset(
            value,
            base0_member0) -
            base0,
        member_offset(
            value,
            base1_member0) -
            base1,
        member_offset(
            value,
            base2_member0) -
            base2,
        member_offset(
            value,
            derived_member0));
}

template <typename Derived, typename Base>
[[nodiscard]] std::ptrdiff_t reference_slot_offset(
    Derived& value,
    const int* referenced) noexcept {

    const auto base =
        base_offset<Derived, Base>(
            value);

    const auto* complete =
        reinterpret_cast<const std::byte*>(
            &value);

    const auto expected =
        reinterpret_cast<std::uintptr_t>(
            referenced);

    for (std::size_t offset = 0;
         offset + sizeof(expected) <=
             sizeof(Base);
         ++offset) {

        std::uintptr_t candidate = 0;

        std::memcpy(
            &candidate,
            complete +
                static_cast<std::size_t>(
                    base) +
                offset,
            sizeof(candidate));

        if (candidate == expected) {
            return static_cast<std::ptrdiff_t>(
                offset);
        }
    }

    return no_offset;
}

#define CW_DEFINE_PACK_ORACLE(NS, PACK_VALUE)                             \
namespace NS {                                                           \
__pragma(pack(push, PACK_VALUE))                                         \
struct ordinary_base {                                                   \
    int a = 1;                                                           \
};                                                                       \
struct ordinary_derived : ordinary_base {                                \
    int b = 2;                                                           \
};                                                                       \
struct alignment_base {                                                  \
    char a = 1;                                                          \
};                                                                       \
struct alignment_derived : alignment_base {                              \
    double b = 2.0;                                                      \
};                                                                       \
struct tail_base {                                                       \
    double a = 1.0;                                                      \
    char tail = 2;                                                       \
};                                                                       \
struct tail_derived : tail_base {                                        \
    char b = 3;                                                          \
};                                                                       \
struct polymorphic_base {                                                \
    virtual void f() {                                                   \
    }                                                                    \
    int a = 1;                                                           \
};                                                                       \
struct polymorphic_derived : polymorphic_base {                          \
    int b = 2;                                                           \
};                                                                       \
struct introduced_base {                                                 \
    int a = 1;                                                           \
};                                                                       \
struct introduced_derived : introduced_base {                            \
    virtual void f() {                                                   \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct introduced_aligned_base {                                         \
    double a = 1.0;                                                      \
};                                                                       \
struct introduced_aligned_derived : introduced_aligned_base {            \
    virtual void f() {                                                   \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct introduced_own_aligned_base {                                     \
    int a = 1;                                                           \
};                                                                       \
struct introduced_own_aligned_derived                                    \
    : introduced_own_aligned_base {                                      \
    virtual void f() {                                                   \
    }                                                                    \
    double d = 2.0;                                                      \
};                                                                       \
struct override_base {                                                   \
    virtual void f() {                                                   \
    }                                                                    \
    int a = 1;                                                           \
};                                                                       \
struct override_derived final : override_base {                          \
    void f() override final {                                            \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct destructor_base {                                                 \
    virtual ~destructor_base() = default;                                \
    int a = 1;                                                           \
};                                                                       \
struct destructor_derived : destructor_base {                            \
    int b = 2;                                                           \
};                                                                       \
struct pure_base {                                                       \
    virtual void f() = 0;                                                \
    int a = 1;                                                           \
};                                                                       \
struct pure_derived : pure_base {                                        \
    void f() override {                                                  \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct empty_base {                                                      \
};                                                                       \
struct empty_derived : empty_base {                                      \
    int b = 2;                                                           \
};                                                                       \
struct vfonly_base {                                                     \
    virtual void f() {                                                   \
    }                                                                    \
};                                                                       \
struct vfonly_derived : vfonly_base {                                    \
    int b = 2;                                                           \
};                                                                       \
struct polymorphic_aligned_base {                                        \
    virtual void f() {                                                   \
    }                                                                    \
    double a = 1.0;                                                      \
};                                                                       \
struct polymorphic_aligned_derived : polymorphic_aligned_base {          \
    int b = 2;                                                           \
};                                                                       \
struct chain_base {                                                      \
    int a = 1;                                                           \
};                                                                       \
struct chain_middle : chain_base {                                       \
    virtual void f() {                                                   \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct chain_derived : chain_middle {                                    \
    int c = 3;                                                           \
};                                                                       \
struct reference_base {                                                  \
    int out = 7;                                                         \
    int& in;                                                             \
    reference_base()                                                     \
        : in(out) {                                                      \
    }                                                                    \
};                                                                       \
struct reference_derived : reference_base {                              \
    int b = 2;                                                           \
};                                                                       \
struct multiple_plain_a {                                                \
    int a = 1;                                                           \
};                                                                       \
struct multiple_plain_b {                                                \
    double b = 2.0;                                                      \
};                                                                       \
struct multiple_plain_c                                                  \
    : multiple_plain_a, multiple_plain_b {                               \
    int c = 3;                                                           \
};                                                                       \
struct multiple_alignment_a {                                            \
    char a = 1;                                                          \
};                                                                       \
struct multiple_alignment_b {                                            \
    double b = 2.0;                                                      \
};                                                                       \
struct multiple_alignment_c                                              \
    : multiple_alignment_a, multiple_alignment_b {                       \
    char c = 3;                                                          \
};                                                                       \
struct multiple_primary_poly_a {                                         \
    virtual void f() {                                                   \
    }                                                                    \
    int a = 1;                                                           \
};                                                                       \
struct multiple_primary_poly_b {                                         \
    int b = 2;                                                           \
};                                                                       \
struct multiple_primary_poly_c                                           \
    : multiple_primary_poly_a, multiple_primary_poly_b {                 \
    int c = 3;                                                           \
};                                                                       \
struct multiple_secondary_poly_a {                                       \
    int a = 1;                                                           \
};                                                                       \
struct multiple_secondary_poly_b {                                       \
    virtual void f() {                                                   \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct multiple_secondary_poly_c                                         \
    : multiple_secondary_poly_a, multiple_secondary_poly_b {             \
    int c = 3;                                                           \
};                                                                       \
struct multiple_both_poly_a {                                            \
    virtual void f() {                                                   \
    }                                                                    \
    int a = 1;                                                           \
};                                                                       \
struct multiple_both_poly_b {                                            \
    virtual void g() {                                                   \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct multiple_both_poly_c                                              \
    : multiple_both_poly_a, multiple_both_poly_b {                       \
    int c = 3;                                                           \
};                                                                       \
struct multiple_introduced_a {                                           \
    int a = 1;                                                           \
};                                                                       \
struct multiple_introduced_b {                                           \
    double b = 2.0;                                                      \
};                                                                       \
struct multiple_introduced_c                                             \
    : multiple_introduced_a, multiple_introduced_b {                     \
    virtual void f() {                                                   \
    }                                                                    \
    int c = 3;                                                           \
};                                                                       \
struct multiple_three_primary_middle_a {                                 \
    int a = 1;                                                           \
};                                                                       \
struct multiple_three_primary_middle_b {                                 \
    virtual void f() {                                                   \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct multiple_three_primary_middle_c {                                 \
    double c = 3.0;                                                      \
};                                                                       \
struct multiple_three_primary_middle_d                                   \
    : multiple_three_primary_middle_a,                                   \
      multiple_three_primary_middle_b,                                   \
      multiple_three_primary_middle_c {                                  \
    int d = 4;                                                           \
};                                                                       \
struct multiple_three_two_poly_a {                                       \
    int a = 1;                                                           \
};                                                                       \
struct multiple_three_two_poly_b {                                       \
    virtual void f() {                                                   \
    }                                                                    \
    int b = 2;                                                           \
};                                                                       \
struct multiple_three_two_poly_c {                                       \
    virtual void g() {                                                   \
    }                                                                    \
    double c = 3.0;                                                      \
};                                                                       \
struct multiple_three_two_poly_d                                         \
    : multiple_three_two_poly_a,                                         \
      multiple_three_two_poly_b,                                         \
      multiple_three_two_poly_c {                                        \
    int d = 4;                                                           \
};                                                                       \
__pragma(pack(pop))                                                      \
                                                                         \
void emit() {                                                            \
    emit_single_one<                                                     \
        ordinary_base, ordinary_derived>(                                \
        PACK_VALUE, "ordinary",                                          \
        &ordinary_base::a, &ordinary_derived::b);                        \
    emit_single_one<                                                     \
        alignment_base, alignment_derived>(                              \
        PACK_VALUE, "alignment",                                         \
        &alignment_base::a, &alignment_derived::b);                      \
    emit_single_two<                                                     \
        tail_base, tail_derived>(                                        \
        PACK_VALUE, "tail_padding",                                      \
        &tail_base::a, &tail_base::tail, &tail_derived::b);             \
    emit_single_one<                                                     \
        polymorphic_base, polymorphic_derived>(                          \
        PACK_VALUE, "polymorphic_base",                                  \
        &polymorphic_base::a, &polymorphic_derived::b);                  \
    emit_single_one<                                                     \
        introduced_base, introduced_derived>(                            \
        PACK_VALUE, "introduces_virtual",                                \
        &introduced_base::a, &introduced_derived::b);                    \
    emit_single_one<                                                     \
        introduced_aligned_base, introduced_aligned_derived>(            \
        PACK_VALUE, "introduces_virtual_aligned_base",                   \
        &introduced_aligned_base::a,                                     \
        &introduced_aligned_derived::b);                                 \
    emit_single_one<                                                     \
        introduced_own_aligned_base,                                     \
        introduced_own_aligned_derived>(                                \
        PACK_VALUE, "introduces_virtual_own_aligned_member",             \
        &introduced_own_aligned_base::a,                                 \
        &introduced_own_aligned_derived::d);                             \
    emit_single_one<                                                     \
        override_base, override_derived>(                                \
        PACK_VALUE, "override_final",                                    \
        &override_base::a, &override_derived::b);                        \
    emit_single_one<                                                     \
        destructor_base, destructor_derived>(                            \
        PACK_VALUE, "virtual_destructor",                                \
        &destructor_base::a, &destructor_derived::b);                    \
    emit_single_one<                                                     \
        pure_base, pure_derived>(                                        \
        PACK_VALUE, "pure_virtual_base",                                 \
        &pure_base::a, &pure_derived::b);                                \
    emit_single_none<                                                    \
        empty_base, empty_derived>(                                      \
        PACK_VALUE, "empty_base",                                        \
        &empty_derived::b);                                              \
    emit_single_none<                                                    \
        vfonly_base, vfonly_derived>(                                    \
        PACK_VALUE, "vfonly_base",                                       \
        &vfonly_derived::b);                                             \
    emit_single_one<                                                     \
        polymorphic_aligned_base, polymorphic_aligned_derived>(          \
        PACK_VALUE, "polymorphic_aligned_member",                        \
        &polymorphic_aligned_base::a,                                    \
        &polymorphic_aligned_derived::b);                                \
    emit_single_one<                                                     \
        chain_middle, chain_derived>(                                    \
        PACK_VALUE, "inheritance_chain",                                 \
        &chain_middle::b, &chain_derived::c);                            \
                                                                         \
    {                                                                    \
        reference_derived value{};                                       \
        const auto base =                                                \
            base_offset<reference_derived, reference_base>(value);       \
        emit_single_common<reference_base, reference_derived>(           \
            PACK_VALUE, "reference_base",                                \
            member_offset(value, &reference_base::out) - base,          \
            reference_slot_offset<reference_derived, reference_base>(    \
                value, &value.out),                                      \
            member_offset(value, &reference_derived::b));                \
    }                                                                    \
                                                                         \
    emit_multiple_one_one<                                               \
        multiple_plain_a, multiple_plain_b, multiple_plain_c>(           \
        PACK_VALUE, "multiple_plain",                                    \
        &multiple_plain_a::a, &multiple_plain_b::b,                      \
        &multiple_plain_c::c);                                           \
    emit_multiple_one_one<                                               \
        multiple_alignment_a, multiple_alignment_b,                      \
        multiple_alignment_c>(                                          \
        PACK_VALUE, "multiple_alignment",                                \
        &multiple_alignment_a::a, &multiple_alignment_b::b,              \
        &multiple_alignment_c::c);                                       \
    emit_multiple_one_one<                                               \
        multiple_primary_poly_a, multiple_primary_poly_b,                \
        multiple_primary_poly_c>(                                       \
        PACK_VALUE, "multiple_primary_poly",                             \
        &multiple_primary_poly_a::a, &multiple_primary_poly_b::b,        \
        &multiple_primary_poly_c::c);                                    \
    emit_multiple_one_one<                                               \
        multiple_secondary_poly_a, multiple_secondary_poly_b,            \
        multiple_secondary_poly_c>(                                     \
        PACK_VALUE, "multiple_secondary_poly",                           \
        &multiple_secondary_poly_a::a, &multiple_secondary_poly_b::b,    \
        &multiple_secondary_poly_c::c);                                  \
    emit_multiple_one_one<                                               \
        multiple_both_poly_a, multiple_both_poly_b,                      \
        multiple_both_poly_c>(                                          \
        PACK_VALUE, "multiple_both_poly",                                \
        &multiple_both_poly_a::a, &multiple_both_poly_b::b,              \
        &multiple_both_poly_c::c);                                       \
    emit_multiple_one_one<                                               \
        multiple_introduced_a, multiple_introduced_b,                    \
        multiple_introduced_c>(                                         \
        PACK_VALUE, "multiple_introduces_virtual",                       \
        &multiple_introduced_a::a, &multiple_introduced_b::b,            \
        &multiple_introduced_c::c);                                      \
    emit_multiple_three_one_one_one<                                     \
        multiple_three_primary_middle_a,                                 \
        multiple_three_primary_middle_b,                                 \
        multiple_three_primary_middle_c,                                 \
        multiple_three_primary_middle_d>(                                \
        PACK_VALUE, "multiple_three_primary_middle",                     \
        &multiple_three_primary_middle_a::a,                             \
        &multiple_three_primary_middle_b::b,                             \
        &multiple_three_primary_middle_c::c,                             \
        &multiple_three_primary_middle_d::d);                            \
    emit_multiple_three_one_one_one<                                     \
        multiple_three_two_poly_a,                                       \
        multiple_three_two_poly_b,                                       \
        multiple_three_two_poly_c,                                       \
        multiple_three_two_poly_d>(                                      \
        PACK_VALUE, "multiple_three_two_poly",                           \
        &multiple_three_two_poly_a::a,                                   \
        &multiple_three_two_poly_b::b,                                   \
        &multiple_three_two_poly_c::c,                                   \
        &multiple_three_two_poly_d::d);                                  \
}                                                                        \
}

CW_DEFINE_PACK_ORACLE(pack1, 1)
CW_DEFINE_PACK_ORACLE(pack2, 2)
CW_DEFINE_PACK_ORACLE(pack4, 4)
CW_DEFINE_PACK_ORACLE(pack8, 8)
CW_DEFINE_PACK_ORACLE(pack16, 16)

#undef CW_DEFINE_PACK_ORACLE

}

int main() {
    std::cout
        << "msc_ver,"
        << "msc_full_ver,"
        << "arch,"
        << "pointer_size,"
        << "pack,"
        << "case,"
        << "base0_size,"
        << "base0_align,"
        << "base1_size,"
        << "base1_align,"
        << "base2_size,"
        << "base2_align,"
        << "derived_size,"
        << "derived_align,"
        << "base0_offset,"
        << "base1_offset,"
        << "base2_offset,"
        << "base0_member0_offset,"
        << "base0_member1_offset,"
        << "base1_member0_offset,"
        << "base1_member1_offset,"
        << "base2_member0_offset,"
        << "base2_member1_offset,"
        << "derived_member0_offset,"
        << "array_stride,"
        << "base0_polymorphic,"
        << "base1_polymorphic,"
        << "base2_polymorphic,"
        << "derived_polymorphic,"
        << "base0_empty,"
        << "base1_empty,"
        << "base2_empty"
        << '\n';

    pack1::emit();
    pack2::emit();
    pack4::emit();
    pack8::emit();
    pack16::emit();

    return 0;
}
