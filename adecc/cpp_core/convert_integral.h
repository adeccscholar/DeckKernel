// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file convert_integral.h
\brief Checked integral conversions for the ConvertTo architecture.

\details
Adds explicit conversion rules for integral types and supports range validation before narrowing or otherwise
potentially unsafe conversions. The checks make the requested safety level part of the conversion path instead
of scattering casts through application code.

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

#include "convert_core.h"

#include <concepts>
#include <format>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace adecc {
	
enum class EIntegralConvertMode {
   safe
   };

class TSafeConvertException : public std::runtime_error {
   public:
      explicit TSafeConvertException(std::string const& strMessage)
           : std::runtime_error(strMessage) { }
   };

namespace details {

template <typename From_ty, typename To_ty>
concept SafeIntegralPair =
   Integral<std::remove_cvref_t<From_ty>> &&
   Integral<std::remove_cvref_t<To_ty>>;

template <typename To_ty, typename From_ty>
constexpr bool IsSafeIntegralConvertable(From_ty const from) noexcept {
   using FromBase_ty = std::remove_cvref_t<From_ty>;
   using ToBase_ty   = std::remove_cvref_t<To_ty>;

   static_assert(Integral<FromBase_ty>);
   static_assert(Integral<ToBase_ty>);

   return std::in_range<ToBase_ty>(from);
}

template <typename To_ty, typename From_ty>
[[noreturn]] inline void ThrowSafeIntegralConvertException(From_ty const from) {
   using FromBase_ty = std::remove_cvref_t<From_ty>;
   using ToBase_ty   = std::remove_cvref_t<To_ty>;

   throw TSafeConvertException{
      std::format("safe integral conversion failed: value '{}' from '{}' is out of range for '{}'.",
                  ConvertTo<std::string>(from),
                  typeid(FromBase_ty).name(),
                  typeid(ToBase_ty).name())
   };
}

} // namespace details



template <Integral From_ty, Integral To_ty>
   requires (!std::same_as<std::remove_cvref_t<From_ty>, std::remove_cvref_t<To_ty>>)
struct Convert<From_ty, To_ty> {
   static constexpr To_ty apply(From_ty const from, EIntegralConvertMode const eMode) {
      if (eMode == EIntegralConvertMode::safe) {
         if (!details::IsSafeIntegralConvertable<To_ty>(from)) {
            details::ThrowSafeIntegralConvertException<To_ty>(from);
            }
         }

      return static_cast<To_ty>(from);
      }
   };


} // namespace adecc
