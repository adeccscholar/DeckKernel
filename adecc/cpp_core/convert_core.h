// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file convert_core.h
\brief Core conversion architecture and the uniform ConvertTo entry point.

\details
Defines the concepts, conversion customization points, optional-value semantics, and generic dispatch used to
move values between compatible type worlds. ConvertTo keeps call sites uniform while specialized Convert
implementations retain responsibility for the actual semantic conversion.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Conversion as a Central Architectural Element".
- "Safe Conversions Instead of Accidental Casts".
- "Concepts as Gatekeepers of Conversion".
- "Conversion as Investment Protection".

\see ARCHITECTURE.md#value-types-policies-and-conversion

\version 1.0
\date 26.09.2026
\author Volker Hillmann (adecc Systemhaus GmbH)

\copyright Copyright © 2021 - 2026 adecc Systemhaus GmbH

\licenseblock{LicenseRef-PolyForm-Noncommercial-1.0.0}
This file is licensed under the PolyForm Noncommercial License 1.0.0.
Use, modification, and distribution are permitted only as defined by that license.
The complete and controlling terms are available at
https://polyformproject.org/licenses/noncommercial/1.0.0/.
Any use not permitted by that license requires separate permission or a separate
license from adecc Systemhaus GmbH.
\endlicenseblock

*/

#pragma once

#include "type_traits_ext.h"

#include <string>
#include <string_view>
#include <sstream>
#include <charconv>
#include <chrono>
#include <type_traits>
#include <concepts>
#include <stdexcept>
#include <algorithm> // transform, tolower
#include <cctype>    // std::tolower
#include <utility>
#include <format>


#if defined CHRONO_WORK_AROUND
   #include "chrono_workaround.h"
#endif

namespace adecc {

// -------------------- Concepts & Aliases --------------------
template <typename ty>
concept Floating = std::floating_point<ty>;

template <typename ty>
concept Integral = std::integral<ty> && (!std::same_as<bool, ty>);

namespace details {

// -------------------- Time helpers --------------------
inline constexpr double kSecPerDay          = 86400.0;
inline constexpr int    kUnixEpochExcelDays = 25569; // 1970-01-01 ? 1899-12-30

inline double DaysSinceUnixEpoch(std::chrono::system_clock::time_point const tp) {
   using namespace std::chrono;
   auto const secs = duration_cast<seconds>(tp.time_since_epoch()).count();
   return static_cast<double>(secs) / kSecPerDay;
   }

inline std::chrono::system_clock::time_point TpFromDaysSinceUnix(double const days) {
   using namespace std::chrono;
   auto const secs = static_cast<long long>(days * kSecPerDay);
   return std::chrono::system_clock::time_point{seconds{secs}};
   }

inline double DaysFromYmd(std::chrono::year_month_day const ymd) {
   std::chrono::sys_days sd{ymd};
   std::chrono::sys_days unix0{std::chrono::year{1970}/1/1};
   auto diff = sd - unix0;
   return static_cast<double>(diff.count());
   }


inline std::string ToLowerAscii(std::string str) {
   std::transform(str.begin(), str.end(), str.begin(),
				  [](unsigned char const ch) { return static_cast<char>(std::tolower(ch)); });
   return str;
   }

inline std::string_view TrimAscii(std::string_view const sv) noexcept {
   auto const IsWs = [](char const ch) constexpr {
      return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
      };

   auto const itBegin = std::find_if_not(sv.begin(), sv.end(), IsWs);
   if (itBegin == sv.end()) {
	  return {};
	  }

   auto const itEndRev = std::find_if_not(sv.rbegin(), sv.rend(), IsWs);
   auto const itEnd = itEndRev.base();

   return sv.substr(static_cast<std::size_t>(itBegin - sv.begin()),
					static_cast<std::size_t>(itEnd - itBegin));
   }

// string/string_view -> bool (robust and case-insensitive)
inline bool IsTrueLiteral(std::string_view const sv) {
   auto str = ToLowerAscii(std::string{TrimAscii(sv)});
   return str == "1" || str == "true" || str == "on" || str == "ja" || str == "active";
   }

inline bool IsFalseLiteral(std::string_view const sv) {
   auto str = ToLowerAscii(std::string{TrimAscii(sv)});
   return str == "0" || str == "false" || str == "off" || str == "nein" || str == "inactive";
   }

inline std::string_view MakeStringView(char const* const sz) {
   if (sz == nullptr) {
	  throw std::runtime_error("null char pointer");
	  }
   return std::string_view{sz};
   }

} // namespace details


// ---------------------------------------------------------------------------
//     Conversion foundation
// ---------------------------------------------------------------------------


template <typename From_ty, typename To_ty>
struct Convert;

template <typename from_ty, typename to_ty, typename... Args>
concept Convertible = requires (from_ty && from, Args&&... args) {
   {
	  Convert<std::remove_cvref_t<from_ty>, to_ty>::apply(std::forward<from_ty>(from),
		 std::forward<Args>(args)...)
   } -> std::same_as<to_ty>;
};

template <typename from_ty, typename to_ty, typename... Args>
concept StdConvertible = sizeof...(Args) == 0 && std::convertible_to<from_ty, to_ty>;



template <typename from_ty, typename to_ty, typename... Args>
inline constexpr bool ConvertHasNoexcept = []() constexpr {
   using FromDecay = std::remove_cvref_t<from_ty>;

   if constexpr (std::same_as<FromDecay, to_ty> && sizeof...(Args) == 0) {
	   return std::is_nothrow_constructible_v<to_ty, from_ty>;
      }
   else if constexpr (Convertible<from_ty, to_ty, Args...>) {
	   return noexcept(Convert<FromDecay, to_ty>::apply(std::declval<from_ty>(), std::declval<Args>()... ));
      }
   else if constexpr (StdConvertible<from_ty, to_ty, Args...>) {
	   return std::is_nothrow_constructible_v<to_ty, from_ty>;
      }
   else {
	   return false;
      }
   }();



template <typename To_ty, typename From_ty, typename... Args>
   requires Convertible<From_ty, To_ty, Args...> || StdConvertible<From_ty, To_ty, Args...>
constexpr To_ty ConvertTo(From_ty&& from, Args&&... args) noexcept(ConvertHasNoexcept<From_ty&&, To_ty, Args&&...>) {
   if constexpr (std::same_as<std::remove_cvref_t<From_ty>, To_ty> && sizeof...(Args) == 0) {
	   return std::forward<From_ty>(from);
      }
   else if constexpr (Convertible<From_ty, To_ty, Args...>) {
	   return Convert<std::remove_cvref_t<From_ty>, To_ty>::apply(std::forward<From_ty>(from),
		                                                           std::forward<Args>(args)...);
      }
   else {
	   return static_cast<To_ty>(std::forward<From_ty>(from));
      }
   }

template <typename from_ty, typename to_ty, typename... args_tys>
concept HasConvertTo =
   requires(from_ty&& from, args_tys&&... args) {
      { ConvertTo<to_ty>(std::forward<from_ty>(from), std::forward<args_tys>(args)...) } -> std::same_as<to_ty>;
      };

// --------------------------------------------------------------------------
//    Central handling of std::optional conversions
// --------------------------------------------------------------------------

class TEmptyOptionalConvertException : public std::runtime_error {
public:
   explicit TEmptyOptionalConvertException(std::string const& strMessage)
      : std::runtime_error(strMessage) {
      }
   };



template <typename To_ty>
struct Convert<std::string, std::optional<To_ty>> {
   template<typename... args_tys>
   static std::optional<To_ty> apply(std::string const& strValue,
                                     args_tys&&... theArguments) {
      if (details::TrimAscii(strValue).empty()) {
         return std::nullopt;
         }

      return std::optional<To_ty> {
         ConvertTo<To_ty>(strValue, std::forward<args_tys>(theArguments)...)
         };
      }

   template<typename... args_tys>
   static std::optional<To_ty> apply(std::string&& strValue,
                                     args_tys&&... theArguments) {
      if (details::TrimAscii(strValue).empty()) {
         return std::nullopt;
         }

      return std::optional<To_ty> {
         ConvertTo<To_ty>(std::move(strValue),
                          std::forward<args_tys>(theArguments)...)
         };
      }
   };



template <typename To_ty>
struct Convert<std::string_view, std::optional<To_ty>> {
   template<typename... args_tys>
   static std::optional<To_ty> apply(std::string_view const svValue,
                                     args_tys&&... theArguments) {
      if (details::TrimAscii(svValue).empty()) {
         return std::nullopt;
         }

      return std::optional<To_ty> {
         ConvertTo<To_ty>(svValue, std::forward<args_tys>(theArguments)...)
         };
      }
   };



template <typename From_ty, typename To_ty>
   requires PlainValuePair<From_ty, To_ty> &&
            (!std::same_as<std::remove_cvref_t<From_ty>, std::string>) &&
            (!std::same_as<std::remove_cvref_t<From_ty>, std::string_view>)
struct Convert<From_ty, std::optional<To_ty>> {
   template<typename... args_tys>
   static std::optional<To_ty> apply(From_ty const& from,
                                     args_tys&&... theArguments) {
      return std::optional<To_ty> {
         ConvertTo<To_ty>(from, std::forward<args_tys>(theArguments)...)
         };
      }

   template<typename... args_tys>
   static std::optional<To_ty> apply(From_ty&& from,
                                     args_tys&&... theArguments) {
      return std::optional<To_ty> {
         ConvertTo<To_ty>(std::forward<From_ty>(from),
                          std::forward<args_tys>(theArguments)...)
         };
      }
   };



template <typename From_ty, typename To_ty>
   requires PlainValuePair<From_ty, To_ty>
struct Convert<std::optional<From_ty>, std::optional<To_ty>> {
   template<typename... args_tys>
   static std::optional<To_ty> apply(std::optional<From_ty> const& from,
                                     args_tys&&... theArguments) {
      if (!from.has_value()) {
         return std::nullopt;
         }

      return std::optional<To_ty> {
         ConvertTo<To_ty>(*from, std::forward<args_tys>(theArguments)...)
         };
      }

   template<typename... args_tys>
   static std::optional<To_ty> apply(std::optional<From_ty>&& from,
                                     args_tys&&... theArguments) {
      if (!from.has_value()) {
         return std::nullopt;
         }

      return std::optional<To_ty> {
         ConvertTo<To_ty>(std::move(*from),
                          std::forward<args_tys>(theArguments)...)
         };
      }
   };



template <typename From_ty, typename To_ty>
   requires PlainValuePair<From_ty, To_ty>
struct Convert<std::optional<From_ty>, To_ty> {
   template<typename... args_tys>
   static To_ty apply(std::optional<From_ty> const& from,
                      args_tys&&... theArguments) {
      if (!from.has_value()) {
         throw TEmptyOptionalConvertException {
            "Convert<std::optional<From_ty>, To_ty>: source optional is empty."
            };
         }

      return ConvertTo<To_ty>(*from, std::forward<args_tys>(theArguments)...);
      }

   template<typename... args_tys>
   static To_ty apply(std::optional<From_ty>&& from,
                      args_tys&&... theArguments) {
      if (!from.has_value()) {
         throw TEmptyOptionalConvertException {
            "Convert<std::optional<From_ty>, To_ty>: source optional is empty."
            };
         }

      return ConvertTo<To_ty>(std::move(*from),
                              std::forward<args_tys>(theArguments)...);
      }
   };

// ---------------------------------------------------------------------------
//     Konkrete Konvertierungen
// ---------------------------------------------------------------------------

// ========================================================
//    numerisch/bool / Strings (std::string / string_view)
// ========================================================

// Integral/Floating -> std::string
template <Integral I>
struct Convert<I, std::string> {
   static std::string apply(I const v) {
      char buf[64];
      auto [p, ec] = std::to_chars(std::begin(buf), std::end(buf), v);
      if (ec != std::errc{}) throw std::runtime_error("to_chars(int) failed");
      return std::string(buf, p);
   }
};

template <Floating F>
struct Convert<F, std::string> {
   static std::string apply(F const v) {
      char buf[128];
      auto [p, ec] = std::to_chars(std::begin(buf), std::end(buf), v,
                                   std::chars_format::general);
      if (ec != std::errc{}) throw std::runtime_error("to_chars(fp) failed");
      return std::string(buf, p);
      }

   static std::string apply(F const v, int scale) {
      char buf[128];
      auto [p, ec] = std::to_chars(std::begin(buf), std::end(buf), v,
                                   std::chars_format::fixed, scale);

      if (ec != std::errc{}) throw std::runtime_error("to_chars(fp) failed");
      return std::string(buf, p);
      }

};

template <>
struct Convert<bool, std::string> {
   static std::string apply(bool const b) { return b ? "true" : "false"; }
};

// std::string/_view -> Integral/Floating/bool

template <Integral I>
struct Convert<std::string_view, I> {
   static I apply(std::string_view const s) {
	  I v{};
	  auto const sv = details::TrimAscii(s);
	  auto* b = sv.data();
	  auto* e = b + sv.size();
	  auto const res = std::from_chars(b, e, v, 10);

	  if (res.ec != std::errc{} || res.ptr != e) {
		 throw std::runtime_error(std::format("from_chars(int) failed for '{}'", std::string{sv}));
		 }

	  return v;
	  }
   };

template <Integral I>
struct Convert<std::string, I> {
   static I apply(std::string const& s) {
	  return Convert<std::string_view, I>::apply(std::string_view{s});
	  }
   };


template <Floating F>
struct Convert<std::string_view, F> {
   static F apply(std::string_view const s) {
	  F v{};
	  auto const sv = details::TrimAscii(s);
	  auto* b = sv.data();
	  auto* e = b + sv.size();
	  auto res = std::from_chars(b, e, v, std::chars_format::general);
	  if (res.ec != std::errc{} || res.ptr != e) throw std::runtime_error("from_chars(fp) failed");
	  return v;
   }
};

template <Floating F>
struct Convert<std::string, F> {
   static F apply(std::string const& s) {
	  return Convert<std::string_view, F>::apply(std::string_view{s});
	  }
   };


// string_view -> string (bequem)
template <>
struct Convert<std::string_view, std::string> {
   static std::string apply(std::string_view const sv) { return std::string {sv.data(), sv.size() }; }
   };


// string -> string_view (referenziert Argument)
template <>
struct Convert<std::string, std::string_view> {
   static std::string_view apply(std::string const& s) { return std::string_view{s}; }
};


template <>
struct Convert<std::string_view, bool> {
   static bool apply(std::string_view const sv) {
	  if (details::IsTrueLiteral(sv))  return true;
	  if (details::IsFalseLiteral(sv)) return false;
	  throw std::runtime_error("Convert<string_view,bool>: invalid literal '" + std::string{sv} + "'");
	  }
   };

 template <>
struct Convert<std::string, bool> {
   static bool apply(std::string const& s) {
	  return Convert<std::string_view, bool>::apply(std::string_view{s});
	  }
   };


// ======================================================
// Date/time and string conversions.
// ======================================================

#if !defined(CHRONO_WORK_AROUND)

// ------------------------------------------------------
// Normaler Pfad
// ------------------------------------------------------

// year_month_day <-> string
template <>
struct Convert<std::chrono::year_month_day, std::string> {
   static std::string apply(std::chrono::year_month_day const& ymd) {
      return std::format("{:%F}", ymd);
      }

   static std::string apply(std::chrono::year_month_day const& ymd, std::string_view const fmt) {
      return std::vformat(std::string{"{:"} + std::string{fmt} + "}",
                          std::make_format_args(ymd));
      }
   };

template <>
struct Convert<std::string_view, std::chrono::year_month_day> {
   static std::chrono::year_month_day apply(std::string_view const s) {
      std::istringstream is{std::string{s}};
      std::chrono::sys_days sd{};
      std::chrono::from_stream(is, "%F", sd);
      if (!is) {
         throw std::runtime_error("YMD parse failed");
         }
      return std::chrono::year_month_day{sd};
      }

   static std::chrono::year_month_day apply(std::string_view const s, std::string_view const fmt) {
      std::istringstream is{std::string{s}};
      std::string const strFmt{fmt};
      std::chrono::sys_days sd{};
      std::chrono::from_stream(is, strFmt.c_str(), sd);
      if (!is) {
         throw std::runtime_error("YMD parse failed (format)");
         }
      return std::chrono::year_month_day{sd};
      }
   };

template <>
struct Convert<std::string, std::chrono::year_month_day> {
   static std::chrono::year_month_day apply(std::string const& s) {
      return Convert<std::string_view, std::chrono::year_month_day>::apply(std::string_view{s});
      }

   static std::chrono::year_month_day apply(std::string const& s, std::string_view const fmt) {
      return Convert<std::string_view, std::chrono::year_month_day>::apply(std::string_view{s}, fmt);
      }
   };

template <>
struct Convert<char const*, std::chrono::year_month_day> {
   static std::chrono::year_month_day apply(char const* const sz) {
      return Convert<std::string_view, std::chrono::year_month_day>::apply(details::MakeStringView(sz));
      }

   static std::chrono::year_month_day apply(char const* const sz, std::string_view const fmt) {
      return Convert<std::string_view, std::chrono::year_month_day>::apply(details::MakeStringView(sz), fmt);
      }
   };


// hh_mm_ss<seconds> <-> string
template <>
struct Convert<std::chrono::hh_mm_ss<std::chrono::seconds>, std::string> {
   static std::string apply(std::chrono::hh_mm_ss<std::chrono::seconds> const& hms) {
      return std::format("{:%T}", hms);
      }

   static std::string apply(std::chrono::hh_mm_ss<std::chrono::seconds> const& hms, std::string_view const fmt) {
      return std::vformat(std::string{"{:"} + std::string{fmt} + "}",
                          std::make_format_args(hms));
      }
   };

template <>
struct Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>> {
   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(std::string_view const s) {
      std::istringstream is{std::string{s}};
      std::chrono::hh_mm_ss<std::chrono::seconds> hms{};
      std::chrono::from_stream(is, "%T", hms);
      if (!is) {
         throw std::runtime_error("HMS parse failed");
         }
      return hms;
      }

   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(std::string_view const s, std::string_view const fmt) {
      std::istringstream is{std::string{s}};
      std::string const strFmt{fmt};
      std::chrono::hh_mm_ss<std::chrono::seconds> hms{};
      std::chrono::from_stream(is, strFmt.c_str(), hms);
      if (!is) {
         throw std::runtime_error("HMS parse failed (format)");
         }
      return hms;
      }
   };

template <>
struct Convert<std::string, std::chrono::hh_mm_ss<std::chrono::seconds>> {
   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(std::string const& s) {
      return Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>>::apply(std::string_view{s});
      }

   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(std::string const& s, std::string_view const fmt) {
      return Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>>::apply(std::string_view{s}, fmt);
      }
   };

template <>
struct Convert<char const*, std::chrono::hh_mm_ss<std::chrono::seconds>> {
   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(char const* const sz) {
      return Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>>::apply(details::MakeStringView(sz));
      }

   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(char const* const sz, std::string_view const fmt) {
      return Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>>::apply(details::MakeStringView(sz), fmt);
      }
   };


// system_clock::time_point <-> string
template <>
struct Convert<std::chrono::system_clock::time_point, std::string> {
   static std::string apply(std::chrono::system_clock::time_point const& tp) {
      return std::format("{:%FT%T}", std::chrono::floor<std::chrono::seconds>(tp));
      }

   static std::string apply(std::chrono::system_clock::time_point const& tp, std::string_view const fmt) {
      return std::vformat(std::string{"{:"} + std::string{fmt} + "}",
                          std::make_format_args(std::chrono::floor<std::chrono::seconds>(tp)));
      }
   };

template <>
struct Convert<std::string_view, std::chrono::system_clock::time_point> {
   static std::chrono::system_clock::time_point apply(std::string_view const s) {
      std::istringstream is{std::string{s}};
      std::chrono::sys_time<std::chrono::seconds> tp{};
      std::chrono::from_stream(is, "%FT%T", tp);
      if (!is) {
         throw std::runtime_error("time_point parse failed");
         }
      return tp;
      }

   static std::chrono::system_clock::time_point apply(std::string_view const s, std::string_view const fmt) {
      std::istringstream is{std::string{s}};
      std::string const strFmt{fmt};
      std::chrono::sys_time<std::chrono::seconds> tp{};
      std::chrono::from_stream(is, strFmt.c_str(), tp);
      if (!is) {
         throw std::runtime_error("time_point parse failed (format)");
         }
      return tp;
      }
   };

template <>
struct Convert<std::string, std::chrono::system_clock::time_point> {
   static std::chrono::system_clock::time_point apply(std::string const& s) {
      return Convert<std::string_view, std::chrono::system_clock::time_point>::apply(std::string_view{s});
      }

   static std::chrono::system_clock::time_point apply(std::string const& s, std::string_view const fmt) {
      return Convert<std::string_view, std::chrono::system_clock::time_point>::apply(std::string_view{s}, fmt);
      }
   };

template <>
struct Convert<char const*, std::chrono::system_clock::time_point> {
   static std::chrono::system_clock::time_point apply(char const* const sz) {
      return Convert<std::string_view, std::chrono::system_clock::time_point>::apply(details::MakeStringView(sz));
      }

   static std::chrono::system_clock::time_point apply(char const* const sz, std::string_view const fmt) {
      return Convert<std::string_view, std::chrono::system_clock::time_point>::apply(details::MakeStringView(sz), fmt);
      }
   };

#else

// ------------------------------------------------------
// Workaround-Pfad
// ------------------------------------------------------

// year_month_day <-> string
template <>
struct Convert<std::chrono::year_month_day, std::string> {
   static std::string apply(std::chrono::year_month_day const& ymd) {
      return formatDate(ymd, DateFmt::ISO);
      }

   static std::string apply(std::chrono::year_month_day const& ymd, DateFmt const fmt) {
      return formatDate(ymd, fmt);
      }
   };

template <>
struct Convert<std::string_view, std::chrono::year_month_day> {
   static std::chrono::year_month_day apply(std::string_view const s) {
      auto const aResult = parseDate(s, DateFmt::ISO);

      if (!aResult.has_value()) {
         throw std::runtime_error {
            std::format("YMD parse failed for '{}' with format ISO (YYYY-MM-DD)", s)
            };
         }

      return aResult.value();
      }

   static std::chrono::year_month_day apply(std::string_view const s, DateFmt const fmt) {
      auto const aResult = parseDate(s, fmt);

      if (!aResult.has_value()) {
         throw std::runtime_error {
            std::format("YMD parse failed for '{}' with format {}", s, fmt)
            };
         }

      return aResult.value();
      }
};
template <>
struct Convert<std::string, std::chrono::year_month_day> {
   static std::chrono::year_month_day apply(std::string const& s) {
      return Convert<std::string_view, std::chrono::year_month_day>::apply(std::string_view{s});
      }

   static std::chrono::year_month_day apply(std::string const& s, DateFmt const fmt) {
      return Convert<std::string_view, std::chrono::year_month_day>::apply(std::string_view{s}, fmt);
      }
   };

template <>
struct Convert<char const*, std::chrono::year_month_day> {
   static std::chrono::year_month_day apply(char const* const sz) {
      return Convert<std::string_view, std::chrono::year_month_day>::apply(details::MakeStringView(sz));
      }

   static std::chrono::year_month_day apply(char const* const sz, DateFmt const fmt) {
      return Convert<std::string_view, std::chrono::year_month_day>::apply(details::MakeStringView(sz), fmt);
      }
   };


// hh_mm_ss<seconds> <-> string
template <>
struct Convert<std::chrono::hh_mm_ss<std::chrono::seconds>, std::string> {
   static std::string apply(std::chrono::hh_mm_ss<std::chrono::seconds> const& hms) {
      return formatTime(hms, TimeFmt::HMS);
      }

   static std::string apply(std::chrono::hh_mm_ss<std::chrono::seconds> const& hms, TimeFmt const fmt) {
      return formatTime(hms, fmt);
      }
   };


template <>
struct Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>> {
   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(std::string_view const s) {
      auto const aResult = parseTime(s, TimeFmt::HMS);

      if (!aResult.has_value()) {
         throw std::runtime_error {
            std::format("HMS parse failed for '{}' with format HMS (HH:MM:SS)", s)
            };
         }

      return aResult.value();
      }

   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(std::string_view const s, TimeFmt const fmt) {
      auto const aResult = parseTime(s, fmt);

      if (!aResult.has_value()) {
         throw std::runtime_error {
            std::format("HMS parse failed for '{}' with format {}", s, fmt)
            };
         }

      return aResult.value();
      }
};

template <>
struct Convert<std::string, std::chrono::hh_mm_ss<std::chrono::seconds>> {
   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(std::string const& s) {
      return Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>>::apply(
         std::string_view{s}
         );
      }

   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(
      std::string const& s,
      TimeFmt const fmt
   ) {
      return Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>>::apply(
         std::string_view{s},
         fmt
         );
      }
   };


template <>
struct Convert<char const*, std::chrono::hh_mm_ss<std::chrono::seconds>> {
   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(char const* const sz) {
      return Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>>::apply(
         details::MakeStringView(sz)
         );
      }

   static std::chrono::hh_mm_ss<std::chrono::seconds> apply(
      char const* const sz,
      TimeFmt const fmt
   ) {
      return Convert<std::string_view, std::chrono::hh_mm_ss<std::chrono::seconds>>::apply(
         details::MakeStringView(sz),
         fmt
         );
      }
   };


// system_clock::time_point <-> string
template <>
struct Convert<std::chrono::system_clock::time_point, std::string> {
   static std::string apply(std::chrono::system_clock::time_point const& tp) {
      return formatDateTime(tp, DateTimeFmt::ISO_HMS);
      }

   static std::string apply(std::chrono::system_clock::time_point const& tp, DateTimeFmt const fmt) {
	  return formatDateTime(tp, fmt);
      }
   };

template <>
struct Convert<std::string_view, std::chrono::system_clock::time_point> {
   static std::chrono::system_clock::time_point apply(std::string_view const s) {
      auto const aResult = parseDateTime(s, DateTimeFmt::ISO_HMS);

      if (!aResult.has_value()) {
         throw std::runtime_error {
            std::format("time_point parse failed for '{}' with format ISO_HMS (YYYY-MM-DDTHH:MM:SS)", s)
            };
         }

      return aResult.value();
      }

   static std::chrono::system_clock::time_point apply(std::string_view const s, DateTimeFmt const fmt) {
      auto const aResult = parseDateTime(s, fmt);

      if (!aResult.has_value()) {
         throw std::runtime_error {
            std::format("time_point parse failed for '{}' with format {}", s, fmt)
            };
         }

      return aResult.value();
      }
};

template <>
struct Convert<std::string, std::chrono::system_clock::time_point> {
   static std::chrono::system_clock::time_point apply(std::string const& s) {
      return Convert<std::string_view, std::chrono::system_clock::time_point>::apply(std::string_view{s});
      }

   static std::chrono::system_clock::time_point apply(std::string const& s, DateTimeFmt const fmt) {
      return Convert<std::string_view, std::chrono::system_clock::time_point>::apply(std::string_view{s}, fmt);
      }
   };

template <>
struct Convert<char const*, std::chrono::system_clock::time_point> {
   static std::chrono::system_clock::time_point apply(char const* const sz) {
      return Convert<std::string_view, std::chrono::system_clock::time_point>::apply(details::MakeStringView(sz));
      }

   static std::chrono::system_clock::time_point apply(char const* const sz, DateTimeFmt const fmt) {
      return Convert<std::string_view, std::chrono::system_clock::time_point>::apply(details::MakeStringView(sz), fmt);
      }
   };

#endif


template <Integral I>
struct Convert<char const*, I> {
   static I apply(char const* const sz) {
	  return Convert<std::string_view, I>::apply(std::string_view{sz});
	  }
   };

template <Floating F>
struct Convert<char const*, F> {
   static F apply(char const* const sz) {
	  return Convert<std::string_view, F>::apply(std::string_view{sz});
	  }
   };


template <>
struct Convert<char const*, std::string> {
   static std::string apply(char const* const sz) {
	  return Convert<std::string_view, std::string>::apply(details::MakeStringView(sz));
      }
   };

template <>
struct Convert<char const*, bool> {
   static bool apply(char const* const sz) {
	  return Convert<std::string_view, bool>::apply(details::MakeStringView(sz));
	  }
   };


} // namespace adecc

