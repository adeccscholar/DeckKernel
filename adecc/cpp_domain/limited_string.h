#pragma once

// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file limited_string.h
\brief special size policies for strings.

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

#include "fixed_string.h"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <expected>
#include <format>
#include <ranges>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>



namespace adecc::old {

   struct StringPolicyError {
      std::string strMessage;
   };



   class StringPolicyViolation : public std::invalid_argument {
   public:
      StringPolicyViolation(std::string_view const svKind,
         std::string_view const svSource,
         std::string_view const svMessage)
         : std::invalid_argument(BuildWhat(svKind, svSource, svMessage)),
         m_strKind(svKind),
         m_strSource(svSource),
         m_strMessage(svMessage) {
      }

      explicit StringPolicyViolation(std::string const& strMessage)
         : std::invalid_argument(strMessage),
         m_strKind("StringPolicy"),
         m_strSource(),
         m_strMessage(strMessage) {
      }

      [[nodiscard]] std::string const& Kind() const noexcept {
         return m_strKind;
      }

      [[nodiscard]] std::string const& Source() const noexcept {
         return m_strSource;
      }

      [[nodiscard]] std::string const& Message() const noexcept {
         return m_strMessage;
      }

   private:
      [[nodiscard]] static std::string BuildWhat(std::string_view const svKind,
         std::string_view const svSource,
         std::string_view const svMessage) {
         return std::format("String policy violation\nKind: {}\nSource: '{}'\nMessage: {}",
            svKind, svSource, svMessage);
      }

   private:
      std::string m_strKind;
      std::string m_strSource;
      std::string m_strMessage;
   };



   template<typename theType>
   concept CharacterStringPointer =
      std::is_pointer_v<std::remove_cvref_t<theType>> &&
      std::same_as<
      std::remove_cv_t<std::remove_pointer_t<std::remove_cvref_t<theType>>>,
      char
      >;



   template<typename theType>
   concept StringViewConvertible =
      !CharacterStringPointer<theType> &&
      requires(theType const& theValue) {
      std::string_view{ theValue };
   };



   template<typename policy_ty>
   concept StringPolicy = requires(std::string_view const svValue) {
      { policy_ty::Check(svValue) } -> std::same_as<std::expected<void, StringPolicyError>>;
      { policy_ty::IsValid(svValue) } -> std::same_as<bool>;
   };



   template<typename policy_ty>
   concept StringLengthPolicyLike = StringPolicy<policy_ty> &&
      requires {
         { policy_ty::MinLength() } -> std::same_as<std::size_t>;
         { policy_ty::MaxLength() } -> std::same_as<std::size_t>;
   };



   template<std::size_t uMinLength, std::size_t uMaxLength>
   class StringLengthPolicy {
   public:
      static_assert(uMinLength <= uMaxLength);

      [[nodiscard]] static std::expected<void, StringPolicyError> Check(std::string_view const svValue) {
         auto const uLength = CountUtf8(svValue);

         if (uLength >= MinLength() && uLength <= MaxLength()) {
            return {};
         }

         return std::unexpected{
            StringPolicyError {
               .strMessage = std::format("String length {} violates policy: expected length in [{}, {}]",
                                         uLength, MinLength(), MaxLength())
               }
         };
      }

      [[nodiscard]] static bool IsValid(std::string_view const svValue) {
         return Check(svValue).has_value();
      }

      [[nodiscard]] static consteval std::size_t MinLength() noexcept {
         return uMinLength;
      }


      [[nodiscard]] static consteval std::size_t MaxLength() noexcept {
         return uMaxLength;
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
      [[nodiscard]] static std::expected<void, StringPolicyError> Check(std::string_view const svValue) {
         auto const& theRegex = Regex();

         if (std::regex_match(svValue.begin(), svValue.end(), theRegex)) {
            return {};
         }

         return std::unexpected{
            StringPolicyError {
               .strMessage = std::format("String does not match required pattern '{}'",
                                         std::string_view{ strPattern })
               }
         };
      }

      [[nodiscard]] static bool IsValid(std::string_view const svValue) {
         return Check(svValue).has_value();
      }

   private:
      [[nodiscard]] static std::regex const& Regex() {
         static std::regex const theRegex{ strPattern.data(), std::regex::ECMAScript };

         return theRegex;
      }
   };



   template<adecc::FixedString strAdditionalAllowedCharacters = adecc::FixedString{ "" } >
   class SepaLatinCharacterPolicy {
   public:
      [[nodiscard]] static std::expected<void, StringPolicyError> Check(std::string_view const svValue) {
         auto const theIterator = std::ranges::find_if_not(svValue, IsAllowedCharacter);

         if (theIterator == svValue.end()) {
            return {};
         }

         return std::unexpected{
            StringPolicyError {
               .strMessage = std::format("String contains character '{}' outside the SEPA latin character set",
                                         *theIterator)
               }
         };
      }

      [[nodiscard]] static bool IsValid(std::string_view const svValue) {
         return Check(svValue).has_value();
      }

   private:
      [[nodiscard]] static constexpr bool IsAllowedCharacter(char const chValue) noexcept {
         return IsSepaLatinCharacter(chValue) || IsAdditionalAllowedCharacter(chValue);
      }

      [[nodiscard]] static constexpr bool IsSepaLatinCharacter(char const chValue) noexcept {
         return IsAsciiLetter(chValue) || IsAsciiDigit(chValue) || IsAllowedSpecialCharacter(chValue);
      }

      [[nodiscard]] static constexpr bool IsAsciiLetter(char const chValue) noexcept {
         return (chValue >= 'A' && chValue <= 'Z')
            || (chValue >= 'a' && chValue <= 'z');
      }

      [[nodiscard]] static constexpr bool IsAsciiDigit(char const chValue) noexcept {
         return chValue >= '0' && chValue <= '9';
      }

      [[nodiscard]] static constexpr bool IsAllowedSpecialCharacter(char const chValue) noexcept {
         constexpr std::string_view svAllowedSpecialCharacters = "/-?:().,'+ ";

         return std::ranges::find(svAllowedSpecialCharacters, chValue)
            != svAllowedSpecialCharacters.end();
      }

      [[nodiscard]] static constexpr bool IsAdditionalAllowedCharacter(char const chValue) noexcept {
         constexpr std::string_view svAdditionalAllowedCharacters{
            strAdditionalAllowedCharacters.data(),
            strAdditionalAllowedCharacters.size()
         };

         return std::ranges::find(svAdditionalAllowedCharacters, chValue)
            != svAdditionalAllowedCharacters.end();
      }
   };



   template<StringPolicy... policies_tys>
   class StringAllOfPolicy {
   public:
      static_assert(sizeof...(policies_tys) > 0);

      [[nodiscard]] static std::expected<void, StringPolicyError> Check(std::string_view const svValue) {
         return CheckNext<policies_tys...>(svValue);
      }

      [[nodiscard]] static bool IsValid(std::string_view const svValue) {
         return Check(svValue).has_value();
      }

   private:
      template<StringPolicy fst_policy_ty, StringPolicy... theRemainingPolicies>
      [[nodiscard]] static std::expected<void, StringPolicyError> CheckNext(std::string_view const svValue) {
         auto const aResult = fst_policy_ty::Check(svValue);

         if (!aResult.has_value()) {
            return aResult;
         }

         if constexpr (sizeof...(theRemainingPolicies) == 0) {
            return {};
         }
         else {
            return CheckNext<theRemainingPolicies...>(svValue);
         }
      }
   };



   template<adecc::FixedString strKind, StringPolicy policy_ty>
   class PolicyString {
   public:
      using Policy = policy_ty;

      PolicyString() = default;

      PolicyString(PolicyString const&) = default;
      PolicyString(PolicyString&&) noexcept = default;

      PolicyString& operator=(PolicyString const&) = default;
      PolicyString& operator=(PolicyString&&) noexcept = default;

      PolicyString(std::nullptr_t) = delete;
      PolicyString& operator=(std::nullptr_t) = delete;

      explicit PolicyString(char const* const szValue)
         : m_strValue(Checked(szValue)) {
      }

      explicit PolicyString(char* const szValue)
         : PolicyString(static_cast<char const*>(szValue)) {
      }

      explicit PolicyString(std::string_view const svValue)
         : m_strValue(Checked(svValue)) {
      }

      explicit PolicyString(std::string const& strValue)
         : m_strValue(Checked(std::string_view{ strValue })) {
      }

      explicit PolicyString(std::string&& strValue)
         : m_strValue(Checked(std::move(strValue))) {
      }

      template<StringViewConvertible theType>
         requires(!std::same_as<std::remove_cvref_t<theType>, PolicyString>)
      explicit PolicyString(theType&& theValue)
         : m_strValue(Checked(std::string_view{ std::forward<theType>(theValue) })) {
      }

      PolicyString& operator=(char const* const szValue) {
         m_strValue = Checked(szValue);

         return *this;
      }

      PolicyString& operator=(char* const szValue) {
         return operator=(static_cast<char const*>(szValue));
      }

      PolicyString& operator=(std::string_view const svValue) {
         m_strValue = Checked(svValue);

         return *this;
      }

      PolicyString& operator=(std::string const& strValue) {
         m_strValue = Checked(std::string_view{ strValue });

         return *this;
      }

      PolicyString& operator=(std::string&& strValue) {
         m_strValue = Checked(std::move(strValue));

         return *this;
      }

      template<StringViewConvertible theType>
         requires(!std::same_as<std::remove_cvref_t<theType>, PolicyString>)
      PolicyString& operator=(theType&& theValue) {
         m_strValue = Checked(std::string_view{ std::forward<theType>(theValue) });

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

      [[nodiscard]] explicit operator std::string() const {
         return m_strValue;
      }

   private:
      [[nodiscard]] static std::string Checked(char const* const szValue) {
         if (szValue == nullptr) {
            throw StringPolicyViolation{
               std::string_view{ strKind },
               {},
               "Null pointer is not a valid string value"
            };
         }

         return Checked(std::string_view{ szValue });
      }

      static void RequireChecked(std::string_view const svValue) {
         auto const aResult = Policy::Check(svValue);

         if (!aResult.has_value()) {
            throw StringPolicyViolation{
               std::string_view{ strKind },
               svValue,
               aResult.error().strMessage
            };
         }
      }

      [[nodiscard]] static std::string Checked(std::string_view const svValue) {
         RequireChecked(svValue);

         return std::string{ svValue };
      }

      [[nodiscard]] static std::string Checked(std::string&& strValue) {
         RequireChecked(strValue);

         return std::move(strValue);
      }

   private:
      std::string m_strValue;
   };

} // end of namesapce adecc::old

namespace std {

   template<adecc::FixedString strKind, adecc::old::StringPolicy policy_ty>
   struct formatter<adecc::old::PolicyString<strKind, policy_ty>, char> {
      formatter<string_view, char> m_aFormatter;

      constexpr auto parse(format_parse_context& aContext) {
         return m_aFormatter.parse(aContext);
      }

      template<typename theFormatContext>
      auto format(adecc::old::PolicyString<strKind, policy_ty> const& aValue,
         theFormatContext& aContext) const {
         return m_aFormatter.format(aValue.View(), aContext);
      }
   };

}
