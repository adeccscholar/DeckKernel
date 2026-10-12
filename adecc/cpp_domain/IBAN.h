#pragma once

// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file ibanr.h
\brief Policies for iban.

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

#include <algorithm>
#include <cctype>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>
#include <stdexcept>
#include <format>

template <typename policy_tyType>
concept is_valid_iban_policy =
   requires(policy_tyType const& policy_ty, std::string_view const svValue) {
      { policy_ty.IsValidIBAN(svValue) } -> std::same_as<bool>;
   };

template <typename theConvertType>
concept is_convert_iban_policy =
   requires (theConvertType const& policy_ty, std::string_view const svValue) {
      { policy_ty.Normalize(svValue) } -> std::same_as<std::string>;
   };

class IbanPolicy_old {
public:
   [[nodiscard]] static bool IsValidIBAN(std::string_view strValue) noexcept {
      std::string const strIban = Normalize(strValue);
      std::string_view const svIban { strIban };
      return HasValidStructure(svIban)  && CalculateModulo97(svIban) == 1;
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
      return std::ranges::all_of(svIban | std::views::drop(2), IsIbanDigit );
      }


   [[nodiscard]] static std::string Normalize(std::string_view const svValue) noexcept {
      std::string strIban;
      strIban.reserve(svValue.size());

      std::ranges::copy_if( svValue | std::views::transform(ToUpper),
                            std::back_inserter(strIban),
                            IsNotSpace );

      return strIban;
      }
   

   [[nodiscard]] static constexpr int ApplyDigit(int const iModulo, int const iDigit) noexcept {
      return ((iModulo * 10) + iDigit) % 97;
      }

   [[nodiscard]] static constexpr int ApplyIbanCharacter(int const iModulo, char const chValue) noexcept {
      if (IsIbanDigit(chValue)) return ApplyDigit(iModulo, chValue - '0');
      else {
         int const iLetterValue = chValue - 'A' + 10;
         return ApplyDigit(ApplyDigit(iModulo, iLetterValue / 10), iLetterValue % 10);
         }
      }

   template <std::ranges::input_range theRangeType>
   [[nodiscard]] static int AccumulateModulo97(theRangeType&& theRange, int const iModulo) noexcept {
      return std::accumulate(std::ranges::begin(theRange), std::ranges::end(theRange), iModulo,
                 [](int const iCurrentModulo, char const chValue) noexcept {
                    return ApplyIbanCharacter(iCurrentModulo, chValue);
                    } );
      }

   [[nodiscard]] static int CalculateModulo97(std::string_view const svIban) noexcept {
      int const iModuloAfterBody = AccumulateModulo97(svIban | std::views::drop(4), 0 );
      return AccumulateModulo97(svIban | std::views::take(4), iModuloAfterBody );
      }

   [[nodiscard]] static bool HasValidStructure(std::string_view const svIban) noexcept {
      return HasValidLength(svIban) && 
             HasCountryCode(svIban) && 
             HasOnlyDigitsAfterCountryCode(svIban);
      }

   [[nodiscard]] static constexpr bool HasValidLength(std::string_view const svIban) noexcept {
      return svIban.size() >= uMinLength && svIban.size() <= uMaxLength;
      }
};


class InvalidIban final : public std::invalid_argument {
public:
   InvalidIban(std::string_view const svValue) 
         : std::invalid_argument{ std::format("Invalid IBAN: '{}'", svValue) } { }
   };

class EmptyIban final : public std::invalid_argument {
public:
   EmptyIban()
      : std::invalid_argument { "Empty IBAN" } { }
   };



template <is_valid_iban_policy policy_tyType>
class Iban_old final {
   public:
      using policy_type = policy_tyType;

      Iban_old() noexcept = default;

      Iban_old(std::string const& strValue) : m_strValue{ Validated(strValue) } { }

      Iban_old(std::string&& strValue) : m_strValue{ Validated(std::move(strValue)) } { }

      Iban_old(std::string_view const svValue) : m_strValue{ Validated(svValue) } { }

      Iban_old(char const* const szValue) : m_strValue { Validated(szValue) } { }

      ~Iban_old() = default;

      Iban_old& operator = (std::string const& strValue) {
         m_strValue = Validated(strValue);
         return *this;
         }

      Iban_old& operator = (std::string&& strValue) {
         m_strValue = Validated(std::move(strValue));
         return *this;
         }

      Iban_old& operator = (std::string_view const svValue) {
         m_strValue = Validated(svValue);
         return *this;
         }

      Iban_old& operator = (char const* const szValue) {
         m_strValue = Validated(szValue);
         return *this;
         }

      operator std::string const& () const noexcept { return str(); }

      [[nodiscard]] std::string_view value() const noexcept {
         return m_strValue;
         }

      [[nodiscard]] std::string const& str() const noexcept {
         return m_strValue;
         }

      [[nodiscard]] bool empty() const noexcept {
         return m_strValue.empty();
         }

   private:
      [[nodiscard]] static std::string Validated(std::string const& strValue) {
         Validate(strValue);
         return strValue;
         }

      [[nodiscard]] static std::string Validated(std::string&& strValue) {
         Validate(strValue);
         return std::move(strValue);
         }

      [[nodiscard]] static std::string Validated(std::string_view const svValue) {
         Validate(svValue);
         return std::string{ svValue };
         }

      [[nodiscard]] static std::string Validated(char const* const szValue) {
         if (szValue == nullptr) {
            throw EmptyIban{ };
            }
         return Validated(std::string_view { szValue });
         }

      static void Validate(std::string_view const svValue) {
         if (!policy_tyType{}.IsValidIBAN(svValue)) {
            throw InvalidIban { svValue };
            }
         }

      std::string m_strValue;
   };

template <is_valid_iban_policy policy_tyType>
struct std::formatter<Iban_old<policy_tyType>, char> : std::formatter<std::string_view, char> {

   template <typename theFormatContextType>
   auto format(Iban_old<policy_tyType> const& theIban, theFormatContextType& theFormatContext) const {
      return std::formatter<std::string_view, char>::format(theIban.value(), theFormatContext );
      }
   };
