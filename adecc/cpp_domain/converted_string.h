#pragma once

// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file convert_string.h
\brief Policies for string transformations.

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
#include "limited_string.h"

#include <algorithm>
#include <array>
#include <concepts>
#include <expected>
#include <format>
#include <functional>
#include <iterator>
#include <numeric>
#include <optional>
#include <ranges>
#include <regex>
#include <string>
#include <string_view>
#include <utility>


namespace adecc::old {

   template<typename policy_ty>
   concept StringConversionPolicy = requires(std::string_view const svValue) {
      { policy_ty::Convert(svValue) } -> std::same_as<std::string>;
   };



   template<typename policy_ty>
   concept StringValidationPolicy = requires(std::string const& strValue) {
      { policy_ty::Check(strValue) } -> std::same_as<std::expected<void, StringPolicyError>>;
      { policy_ty::IsValid(strValue) } -> std::same_as<bool>;
   };



   class IdentityStringConversionPolicy {
   public:
      [[nodiscard]] static std::string Convert(std::string_view const svValue) {
         return std::string{ svValue };
      }
   };



   class GermanSepaStringConversionPolicy {
   public:
      [[nodiscard]] static std::string Convert(std::string_view const svValue) {
         std::string strResult;
         strResult.reserve(svValue.size());

         AppendConverted(strResult, svValue);

         return strResult;
      }

   private:
      using Replacement = std::pair<std::string_view, std::string_view>;

      using ReplacementsType = std::array<Replacement, 7>;

      inline static constexpr ReplacementsType m_aReplacements{
         Replacement{ std::string_view{ "Ä" }, std::string_view{ "Ae" } },
         Replacement{ std::string_view{ "Ö" }, std::string_view{ "Oe" } },
         Replacement{ std::string_view{ "Ü" }, std::string_view{ "Ue" } },
         Replacement{ std::string_view{ "ä" }, std::string_view{ "ae" } },
         Replacement{ std::string_view{ "ö" }, std::string_view{ "oe" } },
         Replacement{ std::string_view{ "ü" }, std::string_view{ "ue" } },
         Replacement{ std::string_view{ "ß" }, std::string_view{ "ss" } }
      };

      [[nodiscard]] static constexpr std::optional<std::reference_wrapper<Replacement const>>
         FindReplacement(std::string_view const svValue) noexcept {
         auto const theIterator = std::ranges::find_if(m_aReplacements, [svValue](Replacement const& aReplacement) {
            return svValue.starts_with(aReplacement.first);
            });

         if (theIterator == m_aReplacements.end()) {
            return std::nullopt;
         }

         return std::cref(*theIterator);
      }

      static void AppendConverted(std::string& strResult, std::string_view const svValue) {
         if (svValue.empty()) {
            return;
         }

         auto const aReplacement = FindReplacement(svValue);

         if (aReplacement.has_value()) {
            Replacement const& theReplacement = aReplacement->get();

            strResult += theReplacement.second;
            AppendConverted(strResult, svValue.substr(theReplacement.first.size()));
         }
         else {
            strResult += svValue.front();
            AppendConverted(strResult, svValue.substr(1));
         }
      }
   };



   class TrimStringConversionPolicy {
   public:
      [[nodiscard]] static std::string Convert(std::string_view const svValue) {
         constexpr std::string_view svWhitespace = " \t\n\r\f\v";

         auto const uBegin = svValue.find_first_not_of(svWhitespace);

         if (uBegin == std::string_view::npos) {
            return {};
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
      [[nodiscard]] static std::string Convert(std::string_view const svValue) {
         std::string strResult;
         strResult.reserve(svValue.size());

         std::ranges::copy_if(
            svValue,
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
      [[nodiscard]] static std::string Convert(std::string_view const svValue) {
         return std::regex_replace(
            std::string{ svValue },
            Regex(),
            std::string{ std::string_view{ strReplacement } }
         );
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



   template<StringConversionPolicy... policies_tys>
   class StringConversionChainPolicy {
   public:
      static_assert(sizeof...(policies_tys) > 0);

      [[nodiscard]] static std::string Convert(std::string_view const svValue) {
         return ConvertNext<policies_tys...>(std::string{ svValue });
      }

   private:
      template<StringConversionPolicy fst_policy_ty, StringConversionPolicy... theRemainingPolicies>
      [[nodiscard]] static std::string ConvertNext(std::string strValue) {
         std::string strConvertedValue = fst_policy_ty::Convert(strValue);

         if constexpr (sizeof...(theRemainingPolicies) == 0) {
            return strConvertedValue;
         }
         else {
            return ConvertNext<theRemainingPolicies...>(std::move(strConvertedValue));
         }
      }
   };



   template<adecc::FixedString strKind, StringConversionPolicy conversion_policy_ty,
      StringValidationPolicy validation_policy_ty>
   class ConvertedPolicyString {
   public:
      using ConversionPolicy = conversion_policy_ty;
      using ValidationPolicy = validation_policy_ty;

      ConvertedPolicyString() = default;

      ConvertedPolicyString(ConvertedPolicyString const&) = default;
      ConvertedPolicyString(ConvertedPolicyString&&) noexcept = default;

      ConvertedPolicyString& operator=(ConvertedPolicyString const&) = default;
      ConvertedPolicyString& operator=(ConvertedPolicyString&&) noexcept = default;

      ConvertedPolicyString(std::nullptr_t) = delete;
      ConvertedPolicyString& operator=(std::nullptr_t) = delete;

      ConvertedPolicyString(char const* const szValue)
         : m_strValue(ConvertAndValidate(szValue)) {
      }

      ConvertedPolicyString(char* const szValue)
         : ConvertedPolicyString(static_cast<char const*>(szValue)) {
      }

      ConvertedPolicyString(std::string_view const svValue)
         : m_strValue(ConvertAndValidate(svValue)) {
      }

      ConvertedPolicyString(std::string const& strValue)
         : m_strValue(ConvertAndValidate(std::string_view{ strValue })) {
      }

      ConvertedPolicyString(std::string&& strValue)
         : m_strValue(ConvertAndValidate(std::string_view{ strValue })) {
      }

      template<StringViewConvertible theType>
         requires(!std::same_as<std::remove_cvref_t<theType>, ConvertedPolicyString>)
      ConvertedPolicyString(theType&& theValue)
         : m_strValue(ConvertAndValidate(std::string_view{ std::forward<theType>(theValue) })) {
      }

      ConvertedPolicyString& operator=(char const* const szValue) {
         m_strValue = ConvertAndValidate(szValue);

         return *this;
      }

      ConvertedPolicyString& operator=(char* const szValue) {
         return operator=(static_cast<char const*>(szValue));
      }

      ConvertedPolicyString& operator=(std::string_view const svValue) {
         m_strValue = ConvertAndValidate(svValue);

         return *this;
      }

      ConvertedPolicyString& operator=(std::string const& strValue) {
         m_strValue = ConvertAndValidate(std::string_view{ strValue });

         return *this;
      }

      ConvertedPolicyString& operator=(std::string&& strValue) {
         m_strValue = ConvertAndValidate(std::string_view{ strValue });

         return *this;
      }

      template<StringViewConvertible theType>
         requires(!std::same_as<std::remove_cvref_t<theType>, ConvertedPolicyString>)
      ConvertedPolicyString& operator=(theType&& theValue) {
         m_strValue = ConvertAndValidate(std::string_view{ std::forward<theType>(theValue) });

         return *this;
      }

      [[nodiscard]] std::string const& Value() const noexcept {
         return m_strValue;
      }

      [[nodiscard]] std::string_view View() const noexcept {
         return m_strValue;
      }

      [[nodiscard]] operator std::string_view() const noexcept {
         return m_strValue;
      }

      [[nodiscard]] operator std::string() const {
         return m_strValue;
      }

   private:
      [[nodiscard]] static std::string ConvertAndValidate(char const* const szValue) {
         if (szValue == nullptr) {
            throw StringPolicyViolation{
               std::string_view{ strKind },
               {},
               "Null pointer is not a valid string value"
            };
         }

         return ConvertAndValidate(std::string_view{ szValue });
      }

      [[nodiscard]] static std::string ConvertAndValidate(std::string_view const svValue) {
         std::string strConvertedValue = ConversionPolicy::Convert(svValue);

         auto const aResult = ValidationPolicy::Check(strConvertedValue);

         if (!aResult.has_value()) {
            throw StringPolicyViolation{
               std::string_view{ strKind },
               svValue,
               std::format("{}\nConverted source: '{}'",
                           aResult.error().strMessage,
                           strConvertedValue)
            };
         }

         return strConvertedValue;
      }

   private:
      std::string m_strValue;
   };

} // end of namespace adecc::old 

template<adecc::FixedString strKind,
   adecc::old::StringConversionPolicy conversion_policy_ty,
   adecc::old::StringValidationPolicy validation_policy_ty>
struct std::formatter<adecc::old::ConvertedPolicyString<strKind, conversion_policy_ty, validation_policy_ty>, char> {
   std::formatter<std::string_view, char> m_aFormatter;

   constexpr auto parse(std::format_parse_context& aContext) {
      return m_aFormatter.parse(aContext);
   }

   template<typename theFormatContext>
   auto format(adecc::old::ConvertedPolicyString<strKind, conversion_policy_ty, validation_policy_ty> const& aValue,
      theFormatContext& aContext) const {
      return m_aFormatter.format(aValue.View(), aContext);
   }
};
