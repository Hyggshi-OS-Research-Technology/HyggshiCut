#pragma once

// Feature detection for C++23 library facilities.
//
// The build targets C++23 when the compiler accepts -std=c++23 (see
// CMakeLists.txt), but accepting the flag says nothing about which library
// pieces are present. GCC 12 is the concrete case this guards against: it
// compiles as C++23 and provides <expected>, yet has neither std::format nor
// std::print, and no <ranges> zip/enumerate adaptors.
//
// So: never branch on __cplusplus to decide whether a library facility is
// usable. Include this header and branch on the HC_HAS_* macros below, each
// of which checks the specific __cpp_lib_* macro the standard defines for
// that facility.
//
//   #include "CxxFeatures.h"
//   #if HC_HAS_STD_EXPECTED
//       std::expected<Frame, DecodeError> decode();
//   #else
//       // C++20 fallback
//   #endif

#if __has_include(<version>)
#  include <version>
#endif

// std::expected<T, E> — C++23, GCC 12+, Clang 16+.
#if defined(__cpp_lib_expected) && __cpp_lib_expected >= 202202L
#  define HC_HAS_STD_EXPECTED 1
#else
#  define HC_HAS_STD_EXPECTED 0
#endif

// std::format — C++20 on paper, but libstdc++ only shipped it in GCC 13.
// The project is Qt-native and uses QString::arg for user-facing text, so
// this is only relevant for internal/log formatting.
#if defined(__cpp_lib_format) && __cpp_lib_format >= 201907L
#  define HC_HAS_STD_FORMAT 1
#else
#  define HC_HAS_STD_FORMAT 0
#endif

// std::print / std::println — C++23, GCC 14+.
#if defined(__cpp_lib_print) && __cpp_lib_print >= 202207L
#  define HC_HAS_STD_PRINT 1
#else
#  define HC_HAS_STD_PRINT 0
#endif

// Ranges adaptors added in C++23: views::zip, views::enumerate,
// views::chunk / slide. GCC 13+.
#if defined(__cpp_lib_ranges_zip) && __cpp_lib_ranges_zip >= 202110L
#  define HC_HAS_RANGES_ZIP 1
#else
#  define HC_HAS_RANGES_ZIP 0
#endif

#if defined(__cpp_lib_ranges_enumerate) && __cpp_lib_ranges_enumerate >= 202302L
#  define HC_HAS_RANGES_ENUMERATE 1
#else
#  define HC_HAS_RANGES_ENUMERATE 0
#endif

// std::ranges::contains / find_last — C++23, GCC 13+.
#if defined(__cpp_lib_ranges_contains) && __cpp_lib_ranges_contains >= 202207L
#  define HC_HAS_RANGES_CONTAINS 1
#else
#  define HC_HAS_RANGES_CONTAINS 0
#endif

// std::to_underlying — C++23, GCC 11+. Widely available, so this is the one
// C++23 utility the codebase can lean on almost everywhere.
#if defined(__cpp_lib_to_underlying) && __cpp_lib_to_underlying >= 202102L
#  define HC_HAS_TO_UNDERLYING 1
#else
#  define HC_HAS_TO_UNDERLYING 0
#endif

#include <type_traits>

namespace hc {

// std::to_underlying where available, otherwise the C++20 equivalent.
// Used for the scoped enums that are serialised to .hcproj as integers
// (TrackType, TransitionType, MediaKind, CheckStatus).
template <typename E>
[[nodiscard]] constexpr auto toUnderlying(E e) noexcept {
    static_assert(std::is_enum_v<E>, "toUnderlying requires an enum type");
    return static_cast<std::underlying_type_t<E>>(e);
}

} // namespace hc
