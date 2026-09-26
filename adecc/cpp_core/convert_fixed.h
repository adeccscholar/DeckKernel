// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file convert_fixed.h
\brief Conversions for policy-based fixed-precision numeric value types.

\details
Connects adecc::numeric values to the general ConvertTo architecture, including textual representations that
respect the precision encoded in the numeric type. The conversion layer exposes domain-oriented numeric values
at technical boundaries without weakening their core semantics.

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

#include "fixed_numeric.h"
#include "convert_core.h"

#include <string>
#include <string_view>
#include <charconv>
#include <stdexcept>
#include <system_error>
#include <concepts>

namespace adecc {


   template <std::floating_point source_ty, std::floating_point target_value_ty,
             std::size_t uDecimals, typename rounding_policy_ty>
   struct Convert<source_ty, numeric<target_value_ty, uDecimals, rounding_policy_ty>> {
      using target_ty = numeric<target_value_ty, uDecimals, rounding_policy_ty>;

      static target_ty apply(source_ty const& aValue) {
         return target_ty { static_cast<target_value_ty>(aValue) };
         }
      };

   template<std::floating_point source_value_ty, std::size_t uDecimals,
            typename rounding_policy_ty, std::floating_point target_ty>
   struct Convert<numeric<source_value_ty, uDecimals, rounding_policy_ty>, target_ty> {
      using source_ty = numeric<source_value_ty, uDecimals, rounding_policy_ty>;

      static target_ty apply(source_ty const& aValue) {
         return static_cast<target_ty>(aValue.Get());
         }
      };


template<std::floating_point ty, std::size_t uDecimals, typename TRoundingPolicy>
struct Convert<adecc::numeric<ty, uDecimals, TRoundingPolicy>, std::string> final {
   using TNumeric = adecc::numeric<ty, uDecimals, TRoundingPolicy>;

   static std::string apply(TNumeric const& aValue) {
      std::array<char, 128> arrBuffer {};

      auto const [pOut, theError] {
         std::to_chars(
            arrBuffer.data(),
            arrBuffer.data() + arrBuffer.size(),
            aValue.Get(),
            std::chars_format::fixed,
            static_cast<int>(uDecimals)
         )
      };

      if (theError != std::errc {}) {
         throw std::runtime_error("to_chars(adecc::numeric) failed");
         }

      return std::string(arrBuffer.data(), pOut);
      }

   static std::string apply(TNumeric const& aValue, int const iScale) {
      std::array<char, 128> arrBuffer {};

      auto const [pOut, theError] {
         std::to_chars(
            arrBuffer.data(),
            arrBuffer.data() + arrBuffer.size(),
            aValue.Get(),
            std::chars_format::fixed,
            iScale
         )
      };

      if (theError != std::errc {}) {
         throw std::runtime_error("to_chars(adecc::numeric) failed");
         }

      return std::string(arrBuffer.data(), pOut);
      }
};


template<std::floating_point ty, std::size_t uDecimals, typename TRoundingPolicy>
struct Convert<std::string, adecc::numeric<ty, uDecimals, TRoundingPolicy>> final {
   using numeric_type = adecc::numeric<ty, uDecimals, TRoundingPolicy>;

   static numeric_type apply(std::string const& strValue) {
      ty value {};
      char const* b {strValue.data()};
      char const* e {b + strValue.size()};

      auto const theResult {
         std::from_chars( b, e, value, std::chars_format::fixed )
      };

      if (theResult.ec != std::errc {} || theResult.ptr != e) {
         throw std::runtime_error("from_chars(adecc::numeric) failed");
         }

      return numeric_type {value};
      }

};


template<std::floating_point ty, std::size_t uDecimals, typename TRoundingPolicy>
struct Convert<std::string_view, adecc::numeric<ty, uDecimals, TRoundingPolicy>> final {
   using numeric_ty = adecc::numeric<ty, uDecimals, TRoundingPolicy>;

   static numeric_ty apply(std::string_view const svValue) {
      ty value {};
      char const* b {svValue.data()};
      char const* e {b + svValue.size()};

      auto const theResult {
         std::from_chars(b, e, value, std::chars_format::fixed )
      };

      if (theResult.ec != std::errc {} || theResult.ptr != e) {
         throw std::runtime_error("from_chars(adecc::numeric) failed");
         }

      return numeric_ty {value};
      }
};
                           
} // namespace adecc
