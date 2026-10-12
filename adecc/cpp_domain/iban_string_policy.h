#pragma once

// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file iban_string_policy.h
\brief variant for iban / string policies.

\details

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

#include "converted_string.h"

#include <algorithm>
#include <cctype>
#include <format>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>

namespace adecc::old {

   class IbanPolicy {
   public:
      using input_type = std::string_view;
      using value_type = std::string;

      [[nodiscard]] static std::expected<void, StringPolicyError> Check(std::string_view const svValue) {
         std::string const strIban = Normalize(svValue);
         std::string_view const svIban{ strIban };

         if (!HasValidLength(svIban)) {
            return std::unexpected{
               StringPolicyError {
                  .strMessage = std::format("IBAN length {} violates policy: expected length in [{}, {}]",
                                            svIban.size(), uMinLength, uMaxLength)
                  }
            };
         }

         if (!HasCountryCode(svIban)) {
            return std::unexpected{
               StringPolicyError {
                  .strMessage = "IBAN has no valid country code"
                  }
            };
         }

         if (!HasOnlyDigitsAfterCountryCode(svIban)) {
            return std::unexpected{
               StringPolicyError {
                  .strMessage = "IBAN contains invalid characters after country code"
                  }
            };
         }

         if (CalculateModulo97(svIban) != 1) {
            return std::unexpected{
               StringPolicyError {
                  .strMessage = "IBAN modulo 97 checksum is invalid"
                  }
            };
         }

         return {};
      }

      [[nodiscard]] static bool IsValid(std::string_view const svValue) {
         return Check(svValue).has_value();
      }

      [[nodiscard]] static std::string Normalize(std::string_view const svValue) {
         std::string strIban;
         strIban.reserve(svValue.size());

         std::ranges::copy_if(
            svValue | std::views::transform(ToUpper),
            std::back_inserter(strIban),
            IsNotSpace
         );

         return strIban;
      }

   private:
      static constexpr std::size_t uMinLength = 15U;
      static constexpr std::size_t uMaxLength = 34U;

      [[nodiscard]] static char ToUpper(char const chValue) noexcept {
         return static_cast<char>(std::toupper(static_cast<unsigned char>(chValue)));
      }

      [[nodiscard]] static bool IsNotSpace(char const chValue) noexcept {
         return !std::isspace(static_cast<unsigned char>(chValue));
      }

      [[nodiscard]] static constexpr bool IsIbanLetter(char const chValue) noexcept {
         return chValue >= 'A' && chValue <= 'Z';
      }

      [[nodiscard]] static constexpr bool IsIbanDigit(char const chValue) noexcept {
         return chValue >= '0' && chValue <= '9';
      }

      [[nodiscard]] static bool HasCountryCode(std::string_view const svIban) noexcept {
         return svIban.size() >= 2U &&
            std::ranges::all_of(svIban | std::views::take(2), IsIbanLetter);
      }

      [[nodiscard]] static bool HasOnlyDigitsAfterCountryCode(std::string_view const svIban) noexcept {
         return std::ranges::all_of(svIban | std::views::drop(2), IsIbanDigit);
      }

      [[nodiscard]] static constexpr int ApplyDigit(int const iModulo, int const iDigit) noexcept {
         return ((iModulo * 10) + iDigit) % 97;
      }

      [[nodiscard]] static constexpr int ApplyIbanCharacter(int const iModulo, char const chValue) noexcept {
         if (IsIbanDigit(chValue)) {
            return ApplyDigit(iModulo, chValue - '0');
         }
         else {
            int const iLetterValue = chValue - 'A' + 10;

            return ApplyDigit(ApplyDigit(iModulo, iLetterValue / 10), iLetterValue % 10);
         }
      }

      template<std::ranges::input_range theRangeType>
      [[nodiscard]] static int AccumulateModulo97(theRangeType&& theRange, int const iModulo) noexcept {
         return std::accumulate(
            std::ranges::begin(theRange),
            std::ranges::end(theRange),
            iModulo,
            [](int const iCurrentModulo, char const chValue) noexcept {
               return ApplyIbanCharacter(iCurrentModulo, chValue);
            }
         );
      }

      [[nodiscard]] static int CalculateModulo97(std::string_view const svIban) noexcept {
         int const iModuloAfterBody = AccumulateModulo97(svIban | std::views::drop(4), 0);

         return AccumulateModulo97(svIban | std::views::take(4), iModuloAfterBody);
      }

      [[nodiscard]] static constexpr bool HasValidLength(std::string_view const svIban) noexcept {
         return svIban.size() >= uMinLength && svIban.size() <= uMaxLength;
      }
   };



   class IbanNormalizeStringConversionPolicy {
   public:
      [[nodiscard]] static std::string Convert(std::string_view const svValue) {
         return IbanPolicy::Normalize(svValue);
      }
   };



   using Iban = ConvertedPolicyString<"IBAN", IbanNormalizeStringConversionPolicy,
      IbanPolicy>;

} // end of namespace adecc::old