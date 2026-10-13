#pragma once

// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file sepa_policies_hawkers_tray.h
\brief Policies around sepa transmissions.

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

#include "policy_common.h"
#include "policies_hawkers_tray.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <expected>
#include <format>
#include <iterator>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>



namespace adecc {

   class IbanPolicy {
   public:
      using value_type = std::string;

      [[nodiscard]] static std::expected<void, PolicyError> Check(std::string const& strValue) {
         std::string_view const svIban{ strValue };

         if (!HasValidLength(svIban)) {
            return std::unexpected{
               PolicyError {
                  .strMessage = std::format("IBAN length {} violates policy: expected length in [{}, {}]",
                                            svIban.size(),
                                            uMinLength,
                                            uMaxLength)
                  }
            };
         }

         if (!HasCountryCode(svIban)) {
            return std::unexpected{
               PolicyError {
                  .strMessage = "IBAN has no valid country code"
                  }
            };
         }

         if (!HasOnlyIbanCharactersAfterCountryCode(svIban)) {
            return std::unexpected{
               PolicyError {
                  .strMessage = "IBAN contains invalid characters after country code"
                  }
            };
         }

         if (CalculateModulo97(svIban) != 1) {
            return std::unexpected{
               PolicyError {
                  .strMessage = "IBAN modulo 97 checksum is invalid"
                  }
            };
         }

         return {};
      }

      [[nodiscard]] static bool IsValid(std::string const& strValue) {
         return Check(strValue).has_value();
      }

   private:
      static constexpr std::size_t uMinLength = 15U;
      static constexpr std::size_t uMaxLength = 34U;

      [[nodiscard]] static constexpr bool IsIbanLetter(char const chValue) noexcept {
         return chValue >= 'A' && chValue <= 'Z';
      }

      [[nodiscard]] static constexpr bool IsIbanDigit(char const chValue) noexcept {
         return chValue >= '0' && chValue <= '9';
      }

      [[nodiscard]] static constexpr bool IsIbanCharacter(char const chValue) noexcept {
         return IsIbanLetter(chValue) || IsIbanDigit(chValue);
      }

      [[nodiscard]] static constexpr bool HasValidLength(std::string_view const svIban) noexcept {
         return svIban.size() >= uMinLength && svIban.size() <= uMaxLength;
      }

      [[nodiscard]] static bool HasCountryCode(std::string_view const svIban) noexcept {
         return svIban.size() >= 2U &&
            std::ranges::all_of(svIban | std::views::take(2), IsIbanLetter);
      }

      [[nodiscard]] static bool HasOnlyIbanCharactersAfterCountryCode(std::string_view const svIban) noexcept {
         return std::ranges::all_of(svIban | std::views::drop(2), IsIbanCharacter);
      }

      [[nodiscard]] static constexpr int ApplyDigit(int const iModulo, int const iDigit) noexcept {
         return ((iModulo * 10) + iDigit) % 97;
      }

      [[nodiscard]] static constexpr int ApplyIbanCharacter(int const iModulo, char const chValue) noexcept {
         if (IsIbanDigit(chValue)) {
            return ApplyDigit(iModulo, chValue - '0');
         }

         int const iLetterValue = chValue - 'A' + 10;

         return ApplyDigit(ApplyDigit(iModulo, iLetterValue / 10), iLetterValue % 10);
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
   };



   class IbanNormalizeConversionPolicy {
   public:
      using input_type = std::string_view;
      using value_type = std::string;

      [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& svValue) {
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
      [[nodiscard]] static char ToUpper(char const chValue) noexcept {
         return static_cast<char>(std::toupper(static_cast<unsigned char>(chValue)));
      }

      [[nodiscard]] static bool IsNotSpace(char const chValue) noexcept {
         return !std::isspace(static_cast<unsigned char>(chValue));
      }
   };


   class IbanGroupedStringPolicy {
   public:
      [[nodiscard]] static std::string ToString(std::string const& strValue) {
         std::string strResult;
         strResult.reserve(strValue.size() + strValue.size() / 4U);

         auto uIndex = std::size_t{ 0 };

         std::ranges::for_each(strValue, [&strResult, &uIndex](char const chValue) {
            if (uIndex > 0U && uIndex % 4U == 0U) {
               strResult.push_back(' ');
            }

            strResult.push_back(chValue);
            ++uIndex;
            });

         return strResult;
      }
   };



   class VatIdDePolicy {
   public:
      using value_type = std::string;

      [[nodiscard]] static std::expected<void, PolicyError> Check(std::string const& strVal) {
         std::string_view const svVal { strVal };

         if (!HasValidFormat(svVal)) {
            return std::unexpected {
               PolicyError { .strMessage = "German VAT ID must match pattern DE followed by 9 digits" }
               };
            }

         if (!HasValidChecksum(svVal)) {
            return std::unexpected { PolicyError { .strMessage = "German VAT ID checksum is invalid" }
                                   };
            }
         return {};
         }

      [[nodiscard]] static bool IsValid(std::string const& strValue) {
         return Check(strValue).has_value();
         }

   private:
  
      [[nodiscard]] static bool HasValidFormat(std::string_view const svVal) noexcept {
         return svVal.size() == 11U && svVal[0] == 'D' && svVal[1] == 'E' && 
                std::ranges::all_of(svVal | std::views::drop(2), [](char cVal) { return cVal >= '0' && cVal <= '9'; });
         }

      [[nodiscard]] static constexpr int Digit(char const cVal) noexcept {
         return cVal - '0';
         }

      [[nodiscard]] static bool HasValidChecksum(std::string_view const svVal) {
         std::string_view const svDigits = svVal.substr(2);

         int const iProduct = std::accumulate(svDigits.begin(), svDigits.begin() + 8, 10,
                                              [](int const iCurrentProduct, char const cVal) {
                                                 int iSum = (Digit(cVal) + iCurrentProduct) % 10;
                                                 if (iSum == 0) { iSum = 10; }
                                                 return (2 * iSum) % 11;
                                                 });
         int iCheckDigit = 11 - iProduct;
         if (iCheckDigit == 10) iCheckDigit = 0;
         return iCheckDigit == Digit(svDigits[8]);
         }
      };



   class VatIdNormalizeConversionPolicy {
   public:
      using input_type = std::string_view;
      using value_type = std::string;

      [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& svValue) {
         using std::toupper;
         std::string strResult;
         strResult.reserve(svValue.size());
         std::ranges::copy_if(
            svValue | std::views::transform([](char const cVal) {
               return static_cast<char>(std::toupper(static_cast<unsigned char>(cVal)));
               }),
            std::back_inserter(strResult),
            [](char const cVal) {
               return !std::isspace(static_cast<unsigned char>(cVal)) && cVal != '-';
               });
         return strResult;
         }
   };



   class VatIdPlainStringPolicy {
   public:
      [[nodiscard]] static std::string ToString(std::string const& strValue) {
         return strValue;
         }
      };



   class VatIdGroupedStringPolicy {
   public:
      [[nodiscard]] static std::string ToString(std::string const& strValue) {
         if (strValue.size() != 11U || !strValue.starts_with("DE")) [[unlikely ]] {
            return strValue;
            }
         else {
            return std::format("{} {} {}", strValue.substr(0, 2), strValue.substr(2, 3), strValue.substr(5));
            }
         }
      };




   using Iban = ConvertedPolicyValue<"IBAN", IbanNormalizeConversionPolicy,
                                     IbanPolicy, IbanGroupedStringPolicy>;

   using CheckedIban = PolicyValue<"IBAN", std::string, IbanPolicy,
                                    IbanGroupedStringPolicy>;

   using VatId_DE = ConvertedPolicyValue<"VAT ID DE", VatIdNormalizeConversionPolicy,
                                         VatIdDePolicy, VatIdPlainStringPolicy >;

   using VatId_DE_Grouped = ConvertedPolicyValue<"VAT ID DE", VatIdNormalizeConversionPolicy,
                                                 VatIdDePolicy, VatIdGroupedStringPolicy>;

   using VatId_DE_Regex_Policy = RegexStringPolicy<R"(^DE[0-9]{9}$)">;


   using Bic_Policy = RegexStringPolicy<R"(^[A-Z]{4}[A-Z]{2}[A-Z0-9]{2}([A-Z0-9]{3})?$)">;

   using PostalCode_DE_Policy = RegexStringPolicy<R"(^[0-9]{5}$)">;

   using Uuid_Policy = RegexStringPolicy<R"(^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[1-5][0-9a-fA-F]{3}-[89abAB][0-9a-fA-F]{3}-[0-9a-fA-F]{12}$)">;

   using Date_ISO_Policy = RegexStringPolicy<R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}$)">;

} // namespace adecc


   /*

   Example usage:

      Iban aIban = " de89 3704 0044 0532 0130 00 ";

      std::string const strFormatted = aIban;  // "DE89 3704 0044 0532 0130 00"
      std::string const& strInternal = aIban.Value(); // "DE89370400440532013000"

      CheckedIban aCheckedIban = std::string { "DE89370400440532013000" };
      std::string const strCheckedFormatted = aCheckedIban;

      std::println("IBAN: {}", aIban);

   */

