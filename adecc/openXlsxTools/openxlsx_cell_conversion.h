// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include "../cpp_core/value_types.h"

#include <OpenXLSX.hpp>
#include <charconv>
#include <concepts>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <system_error>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace adecc::openxlsx_grid::detail {

inline double ToSerial(adecc::date_ty date) {
   using namespace std::chrono;
   if(!date.ok()) throw std::invalid_argument("Invalid calendar date");
   auto const day = sys_days{date};
   auto const epoch = sys_days{year{1899}/12/31};
   if(day < sys_days{year{1900}/1/1})
      throw std::out_of_range("Excel dates before 1900-01-01 are unsupported");
   auto days = (day - epoch).count();
   if(day >= sys_days{year{1900}/3/1}) ++days; // Excel's fictitious 1900-02-29
   return static_cast<double>(days);
}

inline adecc::date_ty FromSerial(double serial) {
   using namespace std::chrono;
   if(!std::isfinite(serial)) throw std::invalid_argument("Invalid Excel date serial");
   auto const n = static_cast<std::int64_t>(std::floor(serial));
   if(n <= 0 || n == 60 || n > 2958465)
      throw std::out_of_range("Excel serial is outside the supported calendar range");
   auto const adjusted = n > 60 ? n - 1 : n;
   return year_month_day{sys_days{year{1899}/12/31} + days{adjusted}};
}

inline double ToSerial(adecc::timestamp_ty instant) {
   using namespace std::chrono;
   auto const day = floor<days>(instant);
   double const part = duration<double>(instant - day).count() / 86400.0;
   return ToSerial(year_month_day{day}) + part;
}

inline adecc::timestamp_ty FromTimestampSerial(double serial) {
   using namespace std::chrono;
   auto const date = FromSerial(serial);
   auto const fraction = serial - std::floor(serial);
   auto const micros = round<microseconds>(duration<double>{fraction * 86400.0});
   return sys_days{date} + micros;
}

inline double ToSerial(adecc::time_ty time) {
   return std::chrono::duration<double>(time.to_duration()).count() / 86400.0;
}

inline adecc::time_ty FromTimeSerial(double serial) {
   using namespace std::chrono;
   if(!std::isfinite(serial) || serial < 0 || serial >= 1)
      throw std::out_of_range("Excel time must be a fraction of a day");
   return adecc::time_ty{round<seconds>(duration<double>{serial * 86400.0})};
}

template<class Ty>
Ty ParseInteger(std::string const& text) {
   Ty result{};
   auto const [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
   if(error != std::errc{} || end != text.data() + text.size())
      throw std::invalid_argument("Excel cell is not an integral value");
   return result;
}

template<class Ty>
Ty FromNumber(double number) {
   if(!std::isfinite(number) || std::trunc(number) != number)
      throw std::invalid_argument("Excel cell is not an integral value");
   // Conversion via long double avoids out-of-range floating-to-integer casts.
   auto const n = static_cast<long double>(number);
   if(n < static_cast<long double>(std::numeric_limits<Ty>::lowest()) ||
      n > static_cast<long double>(std::numeric_limits<Ty>::max()))
      throw std::out_of_range("Excel integer out of range");
   return static_cast<Ty>(number);
}

inline double AsNumber(OpenXLSX::XLCellValue const& value) {
   switch(value.type()) {
      case OpenXLSX::XLValueType::Integer: return static_cast<double>(value.get<std::int64_t>());
      case OpenXLSX::XLValueType::Float:   return value.get<double>();
      case OpenXLSX::XLValueType::Boolean: return value.get<bool>() ? 1.0 : 0.0;
      default: throw std::invalid_argument("Excel cell is not numeric");
   }
}

template<class Ty>
Ty FromCell(OpenXLSX::XLCellValue value) {
   if constexpr (std::same_as<Ty, std::string>) {
      return value.getString();
   }
   else if constexpr (std::same_as<Ty, bool>) {
      if(value.type() == OpenXLSX::XLValueType::Boolean) return value.get<bool>();
      return AsNumber(value) != 0.0;
   }
   else if constexpr (std::same_as<Ty, adecc::money_ty>) {
      return adecc::ConvertTo<Ty>(AsNumber(value));
   }
   else if constexpr (std::same_as<Ty, adecc::date_ty>) {
      return FromSerial(AsNumber(value));
   }
   else if constexpr (std::same_as<Ty, adecc::timestamp_ty>) {
      return FromTimestampSerial(AsNumber(value));
   }
   else if constexpr (std::same_as<Ty, adecc::time_ty>) {
      return FromTimeSerial(AsNumber(value));
   }
   else if constexpr (std::integral<Ty>) {
      if(value.type() == OpenXLSX::XLValueType::String)
         return ParseInteger<Ty>(value.get<std::string>());
      if(value.type() == OpenXLSX::XLValueType::Integer) {
         auto const number = value.get<std::int64_t>();
         if(!std::in_range<Ty>(number)) throw std::out_of_range("Excel integer out of range");
         return static_cast<Ty>(number);
      }
      return FromNumber<Ty>(AsNumber(value));
   }
   else if constexpr (std::floating_point<Ty>) {
      return static_cast<Ty>(AsNumber(value));
   }
   else {
      static_assert(sizeof(Ty) == 0, "Unsupported Excel grid result type");
   }
}

template<class Ty>
void ToCell(OpenXLSX::XLCellAssignable& cell, Ty const& value) {
   using Value = std::remove_cvref_t<Ty>;
   if constexpr (std::same_as<Value, std::string> || std::same_as<Value, std::string_view>) {
      cell.value() = std::string(value);
   }
   else if constexpr (std::same_as<Value, const char*> || std::same_as<Value, char*>) {
      if(value == nullptr) throw std::invalid_argument("Null text pointer");
      cell.value() = std::string(value);
   }
   else if constexpr (std::same_as<Value, adecc::money_ty>) {
      cell.value() = adecc::ConvertTo<double>(value);
   }
   else if constexpr (std::same_as<Value, adecc::date_ty> ||
                      std::same_as<Value, adecc::timestamp_ty> ||
                      std::same_as<Value, adecc::time_ty>) {
      cell.value() = ToSerial(value);
   }
   else if constexpr (std::same_as<Value, bool>) {
      cell.value() = value;
   }
   else if constexpr (std::integral<Value>) {
      // Excel displays at most 15 significant digits; preserve larger integers as text.
      if constexpr (std::unsigned_integral<Value>) {
         if(value > 999999999999999ULL) { cell.value() = std::to_string(value); return; }
      }
      else if(value > 999999999999999LL || value < -999999999999999LL) {
         cell.value() = std::to_string(value); return;
      }
      cell.value() = static_cast<std::int64_t>(value);
   }
   else if constexpr (std::floating_point<Value>) {
      cell.value() = static_cast<double>(value);
   }
   else {
      static_assert(sizeof(Value) == 0, "Unsupported Excel grid parameter type");
   }
}

} // namespace adecc::openxlsx_grid::detail
