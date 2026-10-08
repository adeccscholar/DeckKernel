// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file value_types_visitor.h
\brief Visitor utilities for rendering supported database and core value types as text plus type information.

\details
Provides a uniform std::variant visitor for diagnostic serialization of strings, numbers, money, booleans,
optionals, and chrono-based values. It is used to make typed parameter and value state visible in
diagnostics without exposing framework-specific representations.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Properties, Behavior, and Domain Value Types".
- "Values, Parameters, and the Defined Database Type Space".
- "Optionality: No Value Is No Value".
- "Conversion as a Central Architectural Element".

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

#include "value_types.h"
#include "convert_core.h"

#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace adecc {

namespace details {

   template <class ty>
   consteval std::string_view type_token() {
      using U = std::remove_cvref_t<ty>;
      if constexpr (std::same_as<U, std::string>)                    return "string";
      else if constexpr (std::same_as<U, std::string_view>)           return "string_view";
      else if constexpr (std::same_as<U, char const*>)                return "char const*";
      else if constexpr (std::same_as<U, std::wstring>)               return "wstring";
      else if constexpr (std::same_as<U, std::wstring_view>)          return "wstring_view";
      else if constexpr (std::same_as<U, wchar_t const*>)             return "wchar_t const*";
	  else if constexpr (std::same_as<U, double>)                    return "double";
      else if constexpr (std::same_as<U, money_ty>)                  return "money_ty";
      else if constexpr (std::same_as<U, int>)                       return "int";
	  else if constexpr (std::same_as<U, unsigned int>)              return "uint";
	  else if constexpr (std::same_as<U, long long>)                 return "long long";
	  else if constexpr (std::same_as<U, unsigned long long>)        return "ulong long";
	  else if constexpr (std::same_as<U, bool>)                      return "bool";
	  else if constexpr (std::same_as<U, adecc::date_ty>)            return "date";
	  else if constexpr (std::same_as<U, adecc::timestamp_ty>)       return "timestamp";
      else if constexpr (std::same_as<U, adecc::time_ty>)            return "time";
      else                                                           return "unknown";
      }

   template <class ty>
   std::string optional_token() {
      return std::format("optional<{}>", details::type_token<ty>());
      }


   inline std::string WideDiagnosticText(std::wstring_view const svValue) {
      std::string strResult;
      strResult.reserve(svValue.size());

      for (wchar_t const ch : svValue) {
         std::uint32_t const uValue = static_cast<std::uint32_t>(ch);

         if (uValue >= 0x20 && uValue <= 0x7e) {
            strResult.push_back(static_cast<char>(uValue));
            }
         else if (uValue <= 0xffff) {
            strResult += std::format("\\u{:04X}", uValue);
            }
         else {
            strResult += std::format("\\U{:08X}", uValue);
            }
         }

      return strResult;
      }

} // end of namespace details


struct value_types_visit {
   using return_ty = std::pair<std::string, std::string>;

public:

   template <class ty>
   return_ty operator()(std::optional<ty> const& optValue) const {
      if (!optValue) {
         auto const [strValue, strType] = (*this)(ty{});
         return { "<empty>", std::format("std::optional<{}>", strType) };
      }

      auto const [strValue, strType] = (*this)(*optValue);
      return { strValue, std::format("std::optional<{}>", strType) };
   }

   // Strings: validate ConvertTo support for these types.
   return_ty operator()(std::string_view const sv) const {
      return { adecc::ConvertTo<std::string>(sv), "std::string_view" };
   }

   return_ty operator()(char const* const p) const {
      return { adecc::ConvertTo<std::string>(p), "char const*" };
   }

   return_ty operator()(std::string const& s) const {
      return { adecc::ConvertTo<std::string>(s), "std::string" };
   }

   return_ty operator()(std::wstring_view const sv) const {
      return { details::WideDiagnosticText(sv), "std::wstring_view" };
   }

   return_ty operator()(wchar_t const* const p) const {
      return {
         p ? details::WideDiagnosticText(p) : std::string{ "<null>" },
         "wchar_t const*"
         };
   }

   return_ty operator()(std::wstring const& s) const {
      return { details::WideDiagnosticText(s), "std::wstring" };
   }

   // Numbers and bool.
   return_ty operator()(double const v) const {
      return { adecc::ConvertTo<std::string>(v), "double" };
   }

   return_ty operator()(money_ty const v) const {
      return { adecc::ConvertTo<std::string>(v), "money_ty" };
   }

   return_ty operator()(int const v) const {
      return { adecc::ConvertTo<std::string>(v), "int" };
   }

   return_ty operator()(unsigned int const v) const {
      return { adecc::ConvertTo<std::string>(v), "uint" };
   }

   return_ty operator()(long long const v) const {
      return { adecc::ConvertTo<std::string>(v), "long long" };
   }

   return_ty operator()(unsigned long long const v) const {
      return { adecc::ConvertTo<std::string>(v), "ulong long" };
   }

   return_ty operator()(bool const v) const {
      return { adecc::ConvertTo<std::string>(v), "bool" };
   }

   // Date and time values based on adecc chrono aliases.
   return_ty operator()(adecc::date_ty const& v) const {
      return { adecc::ConvertTo<std::string>(v), "date" };
   }

   return_ty operator()(adecc::timestamp_ty const& v) const {
      return { adecc::ConvertTo<std::string>(v), "timestamp" };
   }

   return_ty operator()(adecc::time_ty const& v) const {
      return { adecc::ConvertTo<std::string>(v), "time" };
   }
};
} // namespace adecc
