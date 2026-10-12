#pragma once

// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file policies_hawkers_tray.h
\brief special collection for several policies.

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

#include "convert_core.h"
#include "fixed_numeric.h"
#include "convert_fixed.h"
#include "fixed_string.h"

#include <string>
#include <string_view>
#include <stdexcept>
#include <regex>
#include <format>

namespace adecc {

   template<std::size_t uMinLength, std::size_t uMaxLength>
   class StringLengthPolicy {
   public:
      using value_type = std::string;

      static_assert(uMinLength <= uMaxLength);

      [[nodiscard]] static std::expected<void, PolicyError> Check(std::string const& strValue) {
         auto const uLength = CountUtf8(strValue);

         if (uLength >= uMinLength && uLength <= uMaxLength) {
            return {};
         }

         return std::unexpected{
            PolicyError {
               .strMessage = std::format("String length {} violates policy: expected length in [{}, {}]",
                                         uLength, uMinLength, uMaxLength)
               }
         };
      }

      [[nodiscard]] static bool IsValid(std::string const& strValue) {
         return Check(strValue).has_value();
      }

   private:
      [[nodiscard]] static constexpr std::size_t CountUtf8(std::string_view const svValue) noexcept {
         return static_cast<std::size_t>(
            std::ranges::count_if(svValue, [](char const chValue) {
               auto const uValue = static_cast<unsigned char>(chValue);

               return (uValue & 0b1100'0000U) != 0b1000'0000U;
               })
            );
      }
   };



   template<adecc::FixedString strPattern>
   class RegexStringPolicy {
   public:
      using value_type = std::string;

      [[nodiscard]] static std::expected<void, PolicyError> Check(std::string const& strValue) {
         auto const& theRegex = Regex();

         if (std::regex_match(strValue.begin(), strValue.end(), theRegex)) {
            return {};
         }

         return std::unexpected{
            PolicyError {
               .strMessage = std::format("String does not match required pattern '{}'",
                                         std::string_view{ strPattern })
               }
         };
      }

      [[nodiscard]] static bool IsValid(std::string const& strValue) {
         return Check(strValue).has_value();
      }

   private:
      [[nodiscard]] static std::regex const& Regex() {
         static std::regex const theRegex{
            strPattern.data(),
            std::regex::ECMAScript
         };

         return theRegex;
      }
   };

   using Url_Policy   = RegexStringPolicy<R"(^(https?)://[A-Za-z0-9\-._~%]+(?:\.[A-Za-z0-9\-._~%]+)+(?:/[A-Za-z0-9\-._~%!$&'()*+,;=:@/]*)?(?:\?[A-Za-z0-9\-._~%!$&'()*+,;=:@/?]*)?(?:#[A-Za-z0-9\-._~%!$&'()*+,;=:@/?]*)?$)">;
   using Email_Policy = RegexStringPolicy<R"(^[A-Za-z0-9._%+\-]+@(?:[A-Za-z0-9](?:[A-Za-z0-9\-]{0,61}[A-Za-z0-9])?\.)+[A-Za-z]{2,63}$)">;
   using Phone_Policy = RegexStringPolicy<R"(^\+[1-9][0-9]{1,14}$)">;


   // ----------------------------------------------------------------------------------------------

   template<std::floating_point val_ty>
   class PositiveNumberPolicy {
   public:
      using value_type = val_ty;

      [[nodiscard]] static std::expected<void, PolicyError> Check(val_ty const& flValue) {
         if (flValue > static_cast<val_ty>(0)) {
            return {};
         }

         return std::unexpected{
            PolicyError {
               .strMessage = std::format("Value {} must be positive", flValue)
               }
         };
      }

      [[nodiscard]] static bool IsValid(val_ty const& flValue) {
         return Check(flValue).has_value();
      }
   };

   template<std::floating_point val_ty>
   class NonNegativeFloatingPointPolicy {
   public:
      using value_type = val_ty;

      [[nodiscard]] static std::expected<void, PolicyError> Check(value_type const& flValue) {
         if (flValue >= static_cast<value_type>(0)) {
            return {};
         }

         return std::unexpected {
            PolicyError {
               .strMessage = std::format("Value {} must be non negative", flValue)
               }
         };
      }

      [[nodiscard]] static bool IsValid(value_type const& flValue) {
         return Check(flValue).has_value();
      }
   };

   enum class EIntervalType : uint32_t { Open, Closed, LeftOpen, RightOpen };

   template<std::floating_point theValueType, auto theMinValue, auto theMaxValue, 
            EIntervalType kind = EIntervalType::Open>
   class FloatingPointClosedRangePolicy {
   public:
      using value_type = theValueType;

      static_assert(static_cast<theValueType>(theMinValue) <= static_cast<theValueType>(theMaxValue));

      [[nodiscard]] static std::expected<void, PolicyError> Check(theValueType const& flValue) {
         auto const flMinValue = static_cast<theValueType>(theMinValue);
         auto const flMaxValue = static_cast<theValueType>(theMaxValue);

         if constexpr (kind == EIntervalType::Open) {
            if (flValue >= flMinValue && flValue <= flMaxValue) {
               return {};
               }
            return std::unexpected{
               PolicyError {
                   .strMessage = std::format("Value {} violates policy: expected value in [{}, {}]",
                                             flValue, flMinValue, flMaxValue)
                   }
               };
            }
         else if constexpr (kind == EIntervalType::Closed) {
            if (flValue > flMinValue && flValue < flMaxValue) {
               return {};
               }
            else {
               return std::unexpected {
                  PolicyError {
                      .strMessage = std::format("Value {} violates policy: expected value in ({}, {})",
                                                flValue, flMinValue, flMaxValue)
                      }
                  };
               }
            }
         else if constexpr (kind == EIntervalType::LeftOpen) {
            if (flValue > flMinValue && flValue <= flMaxValue) {
               return {};
               }
            else {
               return std::unexpected {
                  PolicyError {
                     .strMessage = std::format("Value {} violates policy: expected value in ({}, {}]",
                                               flValue, flMinValue, flMaxValue)
                     }
                  };
               }
            }
         else if constexpr (kind == EIntervalType::RightOpen) {
            if (flValue >= flMinValue && flValue < flMaxValue) {
               return {};
               }
            else {
               return std::unexpected {
                  PolicyError {
                     .strMessage = std::format("Value {} violates policy: expected value in [{}, {})",
                                               flValue, flMinValue, flMaxValue)
                     }
                  };
               }
            }
         }
     

      [[nodiscard]] static bool IsValid(theValueType const& flValue) {
         return Check(flValue).has_value();
      }
   };


   template <numeric_type T>
   class PositiveNumericPolicy {
   public:
      using value_type = T;
      using raw_type = std::remove_cvref_t<decltype(std::declval<T const&>().Get())>;

      static_assert(std::floating_point<raw_type>);

      static std::expected<void, PolicyError> Check(T const& v) {
         if (v.Get() < raw_type{}) {
            return std::unexpected{ PolicyError{.strMessage = "numeric value must be positive" } };
         }
         return {};
      }

      static bool IsValid(T const& v) {
         return Check(v).has_value();
      }
   };

   template <numeric_type val_ty>
   class NumericToStringPolicy {
   public:
      using value_type = val_ty;

      [[nodiscard]] static std::string ToString(val_ty const& aValue) {
         return std::format("{}", aValue);
      }
   };




// ===================================================================================================
// Date validation examples
// ===================================================================================================

class ValidYearMonthDayPolicy {
public:
   using value_type = std::chrono::year_month_day;

   [[nodiscard]] static std::expected<void, PolicyError> Check(std::chrono::year_month_day const& aValue) {
      if (aValue.ok()) {
         return {};
         }

      return std::unexpected {
         PolicyError {
            .strMessage = "Date is not a valid calendar date"
            }
         };
      }

   [[nodiscard]] static bool IsValid(std::chrono::year_month_day const& aValue) {
      return Check(aValue).has_value();
      }
};



class NotAfterTodayPolicy {
public:
   using value_type = std::chrono::year_month_day;

   [[nodiscard]] static std::expected<void, PolicyError> Check(std::chrono::year_month_day const& aValue) {
      if (!aValue.ok()) {
         return std::unexpected {
            PolicyError {
               .strMessage = "Date cannot be compared because it is not a valid calendar date"
               }
            };
         }

      auto const aTodayTimePoint = std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now());
      std::chrono::year_month_day const aToday { aTodayTimePoint };

      if (std::chrono::sys_days{ aValue } <= std::chrono::sys_days{ aToday }) {
         return {};
         }

      return std::unexpected {
         PolicyError {
            .strMessage = std::format("Date '{}' is after today '{}'",
                                      adecc::ConvertTo<std::string>(aValue),
                                      adecc::ConvertTo<std::string>(aToday))
            }
         };
      }

   [[nodiscard]] static bool IsValid(std::chrono::year_month_day const& aValue) {
      return Check(aValue).has_value();
      }
};



class NotBeforeDatePolicyBase {
protected:
   [[nodiscard]] static std::expected<std::chrono::year_month_day, PolicyError>
   ParseFixedIsoDate(std::string_view const svValue) {
      try {
         auto const aValue = adecc::ConvertTo<std::chrono::year_month_day>(svValue);

         if (!aValue.ok()) {
            return std::unexpected {
               PolicyError {
                  .strMessage = std::format("Fixed date '{}' is not a valid calendar date", svValue)
                  }
               };
            }

         return aValue;
         }
      catch (std::exception const& aException) {
         return std::unexpected {
            PolicyError {
               .strMessage = std::format("Fixed date '{}' cannot be converted: {}", svValue, aException.what())
               }
            };
         }
      catch (...) {
         return std::unexpected {
            PolicyError {
               .strMessage = std::format("Fixed date '{}' cannot be converted", svValue)
               }
            };
         }
      }
};



template<adecc::FixedString strMinDate>
class NotBeforeFixedDatePolicy : private NotBeforeDatePolicyBase {
public:
   using value_type = std::chrono::year_month_day;

   [[nodiscard]] static std::expected<void, PolicyError> Check(std::chrono::year_month_day const& aValue) {
      if (!aValue.ok()) {
         return std::unexpected {
            PolicyError {
               .strMessage = "Date cannot be compared because it is not a valid calendar date"
               }
            };
         }

      auto const aMinDate = ParseFixedIsoDate(std::string_view{ strMinDate });

      if (!aMinDate.has_value()) {
         return std::unexpected { aMinDate.error() };
         }

      if (std::chrono::sys_days{ aValue } >= std::chrono::sys_days{ aMinDate.value() }) {
         return {};
         }

      return std::unexpected {
         PolicyError {
            .strMessage = std::format("Date '{}' is before minimum date '{}'",
                                      adecc::ConvertTo<std::string>(aValue),
                                      adecc::ConvertTo<std::string>(aMinDate.value()))
            }
         };
      }

   [[nodiscard]] static bool IsValid(std::chrono::year_month_day const& aValue) {
      return Check(aValue).has_value();
      }
};



// ===================================================================================================
// Stringify policies
// ===================================================================================================




template<std::floating_point val_ty, int iScale>
class FloatToGermanDecimalStringPolicy {
public:
   [[nodiscard]] static std::string ToString(val_ty const& flValue) {
      std::string strResult = adecc::ConvertTo<std::string>(flValue, iScale);

      if (auto const uPosition = strResult.find('.'); uPosition != std::string::npos) {
         strResult[uPosition] = ',';
      }

      return strResult;
   }
};



template<std::floating_point val_ty, int iScale>
class FloatToGermanDecimalWithThousandsStringPolicy {
public:
   [[nodiscard]] static std::string ToString(val_ty const& flValue) {
      std::array<char, 128> aTarget{};
      auto const [theEnd, aErrorCode] = std::to_chars(
         aTarget.data(),
         aTarget.data() + aTarget.size(),
         flValue,
         std::chars_format::fixed,
         iScale
      );

      if (aErrorCode != std::errc{}) {
         throw PolicyViolation { "Stringify", {},
                                 "German decimal formatting failed" };
         }

      std::string strResult{
         aTarget.data(),
         static_cast<std::size_t>(theEnd - aTarget.data())
      };

      auto const uPointPosition = strResult.find('.');

      if (uPointPosition != std::string::npos) {
         strResult[uPointPosition] = ',';
      }

      auto const uIntegralEnd = uPointPosition == std::string::npos
         ? strResult.size()
         : uPointPosition;

      auto const uStart = strResult.starts_with('-') ? std::size_t{ 1 } : std::size_t{ 0 };
      auto uInsertPosition = uIntegralEnd;

      while (uInsertPosition > uStart + 3U) {
         uInsertPosition -= 3U;
         strResult.insert(strResult.begin() + static_cast<std::ptrdiff_t>(uInsertPosition), '.');
      }

      return strResult;
   }
};


template<typename numeric_ty>
class NumericToGermanDecimalStringPolicy;

template<std::floating_point ty, std::size_t uDecimals, typename rounding_policy_ty>
class NumericToGermanDecimalStringPolicy<
   adecc::numeric<ty, uDecimals, rounding_policy_ty>
> {
public:
   using numeric_type = adecc::numeric<ty, uDecimals, rounding_policy_ty>;

   [[nodiscard]] static std::string ToString(numeric_type const& value) {
      return FloatToGermanDecimalStringPolicy<ty, static_cast<int>(uDecimals)>
         ::ToString(value.Get());
   }
};

template<typename numeric_ty>
class NumericToGermanDecimalWithThousandsStringPolicy;

template<std::floating_point ty, std::size_t uDecimals, typename rounding_policy_ty>
class NumericToGermanDecimalWithThousandsStringPolicy<
   adecc::numeric<ty, uDecimals, rounding_policy_ty>
> {
public:
   using numeric_type = adecc::numeric<ty, uDecimals, rounding_policy_ty>;

   [[nodiscard]] static std::string ToString(numeric_type const& value) {
      return FloatToGermanDecimalWithThousandsStringPolicy<ty, static_cast<int>(uDecimals)>
         ::ToString(value.Get());
   }
};

// ===================================================================================================
// Conversion policies
// ===================================================================================================



template<typename theFromType, typename theToType, auto... theArguments>
class ConvertToConversionPolicy {
public:
   using input_type = theFromType;
   using value_type = theToType;

   [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& aInput) {
      try {
         return adecc::ConvertTo<value_type>(aInput, theArguments...);
      }
      catch (std::exception const& aException) {
         return std::unexpected{
            PolicyError {
               .strMessage = std::format("Conversion failed: {}", aException.what())
               }
         };
      }
      catch (...) {
         return std::unexpected {
            PolicyError {
               .strMessage = "Conversion failed with unknown exception"
               }
         };
      }
   }
};




// Bausteim, um eine Eingabe zu trimmen (überflüssige Whitespace- Zeichen entfernen)

class TrimStringConversionPolicy {
public:
   using input_type = std::string_view;
   using value_type = std::string;

   [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& svValue) {
      constexpr std::string_view svWhitespace = " \t\n\r\f\v";

      auto const uBegin = svValue.find_first_not_of(svWhitespace);

      if (uBegin == std::string_view::npos) {
         return std::string{};
      }

      auto const uEnd = svValue.find_last_not_of(svWhitespace);

      return std::string{
         svValue.substr(uBegin, uEnd - uBegin + 1)
      };
   }
};



template<adecc::FixedString strCharactersToRemove>
class RemoveCharactersStringConversionPolicy {
public:
   using input_type = std::string;
   using value_type = std::string;

   [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& strValue) {
      std::string strResult;
      strResult.reserve(strValue.size());

      std::ranges::copy_if(
         strValue,
         std::back_inserter(strResult),
         [](char const chValue) {
            return !IsCharacterToRemove(chValue);
         }
      );

      return strResult;
   }

private:
   [[nodiscard]] static constexpr bool IsCharacterToRemove(char const chValue) noexcept {
      constexpr std::string_view svCharactersToRemove{
         strCharactersToRemove.data(),
         strCharactersToRemove.size()
      };

      return std::ranges::find(svCharactersToRemove, chValue)
         != svCharactersToRemove.end();
   }
};



template<adecc::FixedString strPattern, adecc::FixedString strReplacement>
class RegexStringConversionPolicy {
public:
   using input_type = std::string;
   using value_type = std::string;

   [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& strValue) {
      try {
         return std::regex_replace(
            strValue,
            Regex(),
            std::string{ std::string_view{ strReplacement } }
         );
      }
      catch (std::exception const& aException) {
         return std::unexpected{
            PolicyError {
               .strMessage = std::format("Regex conversion failed: {}", aException.what())
               }
         };
      }
      catch (...) {
         return std::unexpected{
            PolicyError {
               .strMessage = "Regex conversion failed with unknown exception"
               }
         };
      }
   }

private:
   [[nodiscard]] static std::regex const& Regex() {
      static std::regex const theRegex{
         strPattern.data(),
         std::regex::ECMAScript
      };

      return theRegex;
   }
};



template<std::floating_point val_ty>
class GermanDecimalToFloatConversionPolicy {
public:
   using input_type = std::string;
   using value_type = val_ty;

   [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& strValue) {
      try {
         std::string strSource{ strValue };
         std::erase(strSource, '.');
         if (auto const uPosition = strSource.find(','); uPosition != std::string::npos) {
            strSource[uPosition] = '.';
         }

         return adecc::ConvertTo<value_type>(strSource);
      }
      catch (std::exception const& aException) {
         return std::unexpected{
            PolicyError {
               .strMessage = std::format("German decimal conversion failed: {}", aException.what())
               }
         };
      }
      catch (...) {
         return std::unexpected{
            PolicyError {
               .strMessage = "German decimal conversion failed with unknown exception"
               }
         };
      }
   }
};






} // end of namespace adecc