#pragma once

// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file policy_common.h
\brief base for policies.

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
#include "convert_core.h"
#include "type_traits_ext.h"

#include <iterator>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <algorithm>
#include <utility>
#include <array>
#include <regex>
#include <chrono>
#include <type_traits>
#include <string_view>
#include <optional>
#include <charconv>
#include <ranges>
#include <concepts>
#include <format>
#include <expected>

namespace adecc {

struct PolicyError {
   std::string strMessage;
};



class PolicyViolation : public std::invalid_argument {
public:
   PolicyViolation(std::string_view const svKind,
                   std::string_view const svSource,
                   std::string_view const svMessage)
      : std::invalid_argument(BuildWhat(svKind, svSource, svMessage)),
        m_strKind(svKind),
        m_strSource(svSource),
        m_strMessage(svMessage) {
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
      return std::format("Policy violation\nKind: {}\nSource: '{}'\nMessage: {}",
                         svKind, svSource, svMessage);
      }

private:
   std::string m_strKind;
   std::string m_strSource;
   std::string m_strMessage;
};



template<typename policy_ty>
concept ConversionPolicy =
   requires(typename policy_ty::input_type const& aInput) {
      typename policy_ty::input_type;
      typename policy_ty::value_type;

        { policy_ty::Convert(aInput)
           } -> std::same_as<std::expected<typename policy_ty::value_type, PolicyError>>;
      };

// abgespeckt Validation Policy, für conditional conversions
// wird später in Validation erhalten, so dass auch Policies genutzt werden
template<typename policy_ty, typename val_ty>
concept ConditionPolicy = requires(val_ty const& aValue) {
   { policy_ty::IsValid(aValue) } -> std::same_as<bool>;
};

template<typename condition_policy_ty, ConversionPolicy conversion_policy_ty>
class CaseConversion {
public:
   using condition_policy  = condition_policy_ty;
   using conversion_policy = conversion_policy_ty;
   using input_type = typename conversion_policy::input_type;
   using value_type = typename conversion_policy::value_type;

   static_assert(ConditionPolicy<condition_policy, input_type>);
};

template<typename fst_case_ty, typename scnd_case_ty>
concept SameCaseInputType =
            std::same_as<typename fst_case_ty::input_type, typename scnd_case_ty::input_type>;



template<typename fst_case_ty, typename scnd_case_ty>
concept SameCaseValueType =
            std::same_as<typename fst_case_ty::value_type, typename scnd_case_ty::value_type>;



template<typename fst_case_ty, typename... case_tys>
concept CompatibleCaseConversions = (... && SameCaseInputType<fst_case_ty, case_tys>) &&
                                    (... && SameCaseValueType<fst_case_ty, case_tys>);

template<typename fst_case_ty, typename... case_tys>
   requires (sizeof...(case_tys) > 0) && CompatibleCaseConversions<fst_case_ty, case_tys...>
class CaseConversionPolicy {
public:
   using input_type = typename fst_case_ty::input_type;
   using value_type = typename fst_case_ty::value_type;

   [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& aInput) {
      if constexpr (adecc::is_optional_v<std::remove_cvref_t<value_type>>) {
         if constexpr (std::same_as<std::remove_cvref_t<input_type>, std::string> ||
            std::same_as<std::remove_cvref_t<input_type>, std::string_view>) {
            if (aInput.empty()) {
               return value_type{};
               }
            }
         }

      std::string strMessages;

      auto aResult = TryConvert<fst_case_ty, case_tys...>(aInput, strMessages);

      if (aResult.has_value()) {
         return aResult;
         }

      return std::unexpected {
         PolicyError {
            .strMessage = std::format("No matching conversion case succeeded: {}", strMessages)
            }
         };
      }

private:
   template<typename case_ty, typename... remaining_case_tys>
   [[nodiscard]] static std::expected<value_type, PolicyError>
      TryConvert(input_type const& aInput, std::string& strMessages) {
      if constexpr (!ConditionPolicy<typename case_ty::condition_policy, input_type>) {
         return std::unexpected {
            PolicyError {
               .strMessage = "Invalid case condition policy"
               }
            };
         }
      else {
         if (case_ty::condition_policy::IsValid(aInput)) {
            auto aResult = case_ty::conversion_policy::Convert(aInput);

            if (aResult.has_value()) {
               return aResult;
               }

            AppendMessage(strMessages, aResult.error().strMessage);
            }
         }

      if constexpr (sizeof...(remaining_case_tys) == 0) {
         return std::unexpected {
            PolicyError {
               .strMessage = strMessages.empty()
                  ? "No condition matched"
                  : strMessages
               }
            };
         }
      else {
         return TryConvert<remaining_case_tys...>(aInput, strMessages);
         }
      }

   static void AppendMessage(std::string& strMessages, std::string_view const svMessage) {
      if (!strMessages.empty()) {
         strMessages += " | ";
         }

      strMessages += svMessage;
      }
};


template<typename policy_ty, typename val_ty>
concept ValidationPolicy = requires(val_ty const& aValue) {
      { policy_ty::Check(aValue) } -> std::same_as<std::expected<void, PolicyError>>;
      { policy_ty::IsValid(aValue) } -> std::same_as<bool>;
    } ||
   ( adecc::is_optional_v<std::remove_cvref_t<val_ty>> &&
     requires(adecc::optional_value_t<val_ty> const& aValue) {
        { policy_ty::Check(aValue) } -> std::same_as<std::expected<void, PolicyError>>;
        { policy_ty::IsValid(aValue) } -> std::same_as<bool>;
     }
   );


    template<typename policy_ty, typename val_ty>
    [[nodiscard]] static std::expected<void, PolicyError> CheckPolicy(val_ty const& aValue) {
       if constexpr (requires {
          {
             policy_ty::Check(aValue)
          } -> std::same_as<std::expected<void, PolicyError>>;
       }) {
          return policy_ty::Check(aValue);
       }
       else {
          if (!aValue.has_value()) {
             return {};
          }

          return policy_ty::Check(aValue.value());
       }
    }


template<typename policy_ty, typename val_ty>
concept StringifyPolicy =
   requires(val_ty const& aValue) {
      {
         policy_ty::ToString(aValue)
      } -> std::same_as<std::string>;
   };



template<typename fst_policy_ty, typename scnd_policy_ty>
concept ComposableConversionPolicy =
   ConversionPolicy<fst_policy_ty> &&
   ConversionPolicy<scnd_policy_ty> &&
   std::same_as<typename fst_policy_ty::value_type, typename scnd_policy_ty::input_type>;



template<typename val_ty>
class ConvertToStringPolicy {
public:
   [[nodiscard]] static std::string ToString(val_ty const& aValue) {
      if constexpr (adecc::is_optional_v<std::remove_cvref_t<val_ty>>) {
         if (!aValue.has_value()) {
            return {};
            }

         return adecc::ConvertTo<std::string>(aValue.value());
         }
      else {
         return adecc::ConvertTo<std::string>(aValue);
         }
      }
};

template<typename val_ty, StringifyPolicy<adecc::value_type_or_self_t<val_ty>> stringify_policy_ty>
class OptionalAwareStringifyPolicy {
public:
   [[nodiscard]] static std::string ToString(val_ty const& aValue) {
      if constexpr (adecc::is_optional_v<std::remove_cvref_t<val_ty>>) {
         if (!aValue.has_value()) {
            return {};
            }
         return stringify_policy_ty::ToString(aValue.value());
         }
      else {
         return stringify_policy_ty::ToString(aValue);
         }
      }
};







template<typename val_ty, auto... theArguments>
class ConvertToStringWithArgumentsPolicy {
public:
   [[nodiscard]] static std::string ToString(val_ty const& aValue) {
      if constexpr (adecc::is_optional_v<std::remove_cvref_t<val_ty>>) {
         if (!aValue.has_value()) {
            return {};
            }

         return adecc::ConvertTo<std::string>(aValue.value(), theArguments...);
         }
      else {
         return adecc::ConvertTo<std::string>(aValue, theArguments...);
         }
      }
};




template<ConversionPolicy... policies_tys>
class ConversionChainPolicy;

template<ConversionPolicy policy_ty>
class ConversionChainPolicy<policy_ty> {
public:
   using input_type = typename policy_ty::input_type;
   using value_type = typename policy_ty::value_type;

   [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& aInput) {
      return policy_ty::Convert(aInput);
      }
};

template<ConversionPolicy fst_policy_ty,
         ConversionPolicy scnd_policy_ty,
         ConversionPolicy... remaining_policies_tys>
   requires ComposableConversionPolicy<fst_policy_ty, scnd_policy_ty>
class ConversionChainPolicy<fst_policy_ty, scnd_policy_ty, remaining_policies_tys...> {
public:
   using input_type = typename fst_policy_ty::input_type;

private:
   using conversions_tails = ConversionChainPolicy<scnd_policy_ty, remaining_policies_tys...>;

public:
   using value_type = typename conversions_tails::value_type;

   [[nodiscard]] static std::expected<value_type, PolicyError> Convert(input_type const& aInput) {
      auto aFirstResult = fst_policy_ty::Convert(aInput);

      if (!aFirstResult.has_value()) {
         return std::unexpected {
            aFirstResult.error()
            };
         }

      return conversions_tails::Convert(aFirstResult.value());
      }
};

// ===================================================================================================
// Validation policies
// ===================================================================================================

template<typename val_ty>
class AlwaysValidPolicy {
public:
   using value_type = val_ty;

   [[nodiscard]] static std::expected<void, PolicyError> Check(val_ty const&) {
      return {};
      }

   [[nodiscard]] static bool IsValid(val_ty const&) {
      return true;
      }
};



template<typename val_ty, ValidationPolicy<val_ty>... policies_tys>
   requires (sizeof...(policies_tys) > 0)
class AllOfPolicy {
public:
   using value_type = val_ty;

   [[nodiscard]] static std::expected<void, PolicyError> Check(val_ty const& aValue) {
      return CheckNext<policies_tys...>(aValue);
   }

   [[nodiscard]] static bool IsValid(val_ty const& aValue) {
      return Check(aValue).has_value();
   }

private:
   template<ValidationPolicy<val_ty> fst_policy_ty,
      ValidationPolicy<val_ty>... remaining_policies_tys>
   [[nodiscard]] static std::expected<void, PolicyError> CheckNext(val_ty const& aValue) {
      if (auto const aResult = CheckPolicy<fst_policy_ty>(aValue); !aResult.has_value()) {
         return aResult;
      }

      if constexpr (sizeof...(remaining_policies_tys) == 0) {
         return {};
      }
      else {
         return CheckNext<remaining_policies_tys...>(aValue);
      }
   }
};

template<typename val_ty, ValidationPolicy<val_ty>... policies_tys>
   requires (sizeof...(policies_tys) > 0)
class AnyOfPolicy {
public:
   using value_type = val_ty;

   [[nodiscard]] static std::expected<void, PolicyError> Check(val_ty const& aValue) {
      std::string strMessages;

      if (CheckAny<policies_tys...>(aValue, strMessages)) {
         return {};
      }

      return std::unexpected{
         PolicyError {
            .strMessage = std::format("None of the alternative policies matched: {}", strMessages)
            }
      };
   }

   [[nodiscard]] static bool IsValid(val_ty const& aValue) {
      return Check(aValue).has_value();
   }

private:
   template<ValidationPolicy<val_ty> fst_policy_ty,
      ValidationPolicy<val_ty>... remaining_policies_tys>
   [[nodiscard]] static bool CheckAny(val_ty const& aValue, std::string& strMessages) {
      auto const aResult = CheckPolicy<fst_policy_ty>(aValue);

      if (aResult.has_value()) {
         return true;
      }

      AppendMessage(strMessages, aResult.error().strMessage);

      if constexpr (sizeof...(remaining_policies_tys) == 0) {
         return false;
      }
      else {
         return CheckAny<remaining_policies_tys...>(aValue, strMessages);
      }
   }

   static void AppendMessage(std::string& strMessages, std::string_view const svMessage) {
      if (!strMessages.empty()) {
         strMessages += " | ";
      }

      strMessages += svMessage;
   }
};


// ===================================================================================================
// Generic semantic value without conversion
// ===================================================================================================

template<adecc::FixedString strKind, typename val_ty,
         ValidationPolicy<val_ty> validation_policy_ty,
         StringifyPolicy<val_ty> stringify_policy_ty = ConvertToStringPolicy<val_ty>>
class PolicyValue {
public:
   using value_ty          = val_ty;
   using validation_policy = validation_policy_ty;
   using stringify_policy  = stringify_policy_ty;

   PolicyValue() requires std::default_initializable<value_ty> = default;

   PolicyValue(PolicyValue const&) = default;
   PolicyValue(PolicyValue&&) noexcept = default;

   PolicyValue& operator=(PolicyValue const&) = default;
   PolicyValue& operator=(PolicyValue&&) noexcept = default;

   PolicyValue(value_ty const& aValue)
      : m_aValue(Checked(aValue)) {
      }

   PolicyValue(value_ty&& aValue)
      : m_aValue(Checked(std::move(aValue))) {
      }

   PolicyValue& operator=(value_ty const& aValue) {
      m_aValue = Checked(aValue);
      return *this;
      }

   PolicyValue& operator=(value_ty&& aValue) {
      m_aValue = Checked(std::move(aValue));
      return *this;
      }

   [[nodiscard]] value_ty const& Value() const noexcept {
      return m_aValue;
      }

   [[nodiscard]] value_ty& Value() noexcept {
      return m_aValue;
      }

   [[nodiscard]] operator value_ty const&() const noexcept {
      return m_aValue;
      }

   [[nodiscard]] operator value_ty() const requires std::copy_constructible<value_ty> {
      return m_aValue;
      }

   [[nodiscard]] std::string str() const {
      return stringify_policy::ToString(m_aValue);
      }

   [[nodiscard]] operator std::string() const {
      return str();
      }

private:
   static void RequireChecked(value_ty const& aValue) {
      if (auto const aResult = CheckPolicy<validation_policy>(aValue); !aResult.has_value()) {
         throw PolicyViolation{ std::string_view { strKind }, {},
                                 aResult.error().strMessage };
         }
      }

   [[nodiscard]] static value_ty Checked(value_ty const& aValue) {
      RequireChecked(aValue);
      return aValue;
      }

   [[nodiscard]] static value_ty Checked(value_ty&& aValue) {
      RequireChecked(aValue);
      return std::move(aValue);
      }

private:
   value_ty m_aValue {};
};


// ===================================================================================================
// Generic semantic value with conversion from string_view
// ===================================================================================================

template<adecc::FixedString strKind,
         ConversionPolicy conversion_policy_ty,
         ValidationPolicy<typename conversion_policy_ty::value_type> validation_policy_ty,
         StringifyPolicy<typename conversion_policy_ty::value_type> stringify_policy_ty =
                          ConvertToStringPolicy<typename conversion_policy_ty::value_type>>
class ConvertedPolicyValue {
public:
   using conversion_policy = conversion_policy_ty;
   using validation_policy = validation_policy_ty;
   using value_type = typename conversion_policy::value_type;
   using stringify_policy = stringify_policy_ty;

   ConvertedPolicyValue()
      requires std::default_initializable<value_type>
      = default;

   ConvertedPolicyValue(ConvertedPolicyValue const&) = default;
   ConvertedPolicyValue(ConvertedPolicyValue&&) noexcept = default;

   ConvertedPolicyValue& operator=(ConvertedPolicyValue const&) = default;
   ConvertedPolicyValue& operator=(ConvertedPolicyValue&&) noexcept = default;

   ConvertedPolicyValue(std::nullptr_t) = delete;
   ConvertedPolicyValue& operator=(std::nullptr_t) = delete;

   ConvertedPolicyValue(char const* const szValue)
      : m_aValue(ConvertAndValidate(szValue)) {
      }

   ConvertedPolicyValue(char* const szValue)
      : ConvertedPolicyValue(static_cast<char const*>(szValue)) {
      }

   ConvertedPolicyValue(std::string_view const svValue)
      : m_aValue(ConvertAndValidate(svValue)) {
      }

   ConvertedPolicyValue(std::string const& strValue)
      : m_aValue(ConvertAndValidate(std::string_view{ strValue })) {
      }

   ConvertedPolicyValue(std::string&& strValue)
      : m_aValue(ConvertAndValidate(std::string_view{ strValue })) {
      }

   ConvertedPolicyValue(value_type const& aValue)
      requires(!std::same_as<value_type, std::string>)
      : m_aValue(Checked(aValue)) {
      }

   ConvertedPolicyValue(value_type&& aValue)
      requires(!std::same_as<value_type, std::string>)
      : m_aValue(Checked(std::move(aValue))) {
      }

   ConvertedPolicyValue& operator=(char const* const szValue) {
      m_aValue = ConvertAndValidate(szValue);

      return *this;
      }

   ConvertedPolicyValue& operator=(char* const szValue) {
      return operator=(static_cast<char const*>(szValue));
      }

   ConvertedPolicyValue& operator=(std::string_view const svValue) {
      m_aValue = ConvertAndValidate(svValue);

      return *this;
      }

   ConvertedPolicyValue& operator=(std::string const& strValue) {
      m_aValue = ConvertAndValidate(std::string_view{ strValue });

      return *this;
      }

   ConvertedPolicyValue& operator=(std::string&& strValue) {
      m_aValue = ConvertAndValidate(std::string_view{ strValue });

      return *this;
      }

   ConvertedPolicyValue& operator=(value_type const& aValue)
      requires(!std::same_as<value_type, std::string>) {
      m_aValue = Checked(aValue);

      return *this;
      }

   ConvertedPolicyValue& operator=(value_type&& aValue)
      requires(!std::same_as<value_type, std::string>) {
      m_aValue = Checked(std::move(aValue));

      return *this;
      }

   [[nodiscard]] value_type const& Value() const noexcept {
      return m_aValue;
      }

   [[nodiscard]] value_type& Value() noexcept {
      return m_aValue;
      }

   [[nodiscard]] operator value_type const&() const noexcept {
      return m_aValue;
      }

   [[nodiscard]] operator value_type() const
      requires std::copy_constructible<value_type> {
      return m_aValue;
      }

   [[nodiscard]] std::string str() const {
      return stringify_policy::ToString(m_aValue);
      }

   [[nodiscard]] operator std::string() const {
      return str();
      }

private:
   static_assert(std::same_as<typename conversion_policy::input_type, std::string_view>,
                 "ConvertedPolicyValue expects a conversion chain whose first input_type is std::string_view");

   [[nodiscard]] static value_type ConvertAndValidate(char const* const szValue) {
      if (szValue == nullptr) {
         throw PolicyViolation { std::string_view { strKind }, {},
                                 "Null pointer is not a valid source value" };
         }
      return ConvertAndValidate(std::string_view{ szValue });
      }

   [[nodiscard]] static value_type ConvertAndValidate(std::string_view const svValue) {
      auto aConversionResult = conversion_policy::Convert(svValue);

      if (!aConversionResult.has_value()) {
         throw PolicyViolation { std::string_view { strKind }, svValue,
                                 aConversionResult.error().strMessage };
         }

      return Checked(std::move(aConversionResult.value()), svValue);
      }

   static void RequireChecked(value_type const& aValue, std::string_view const svSource = {}) {
      auto const aResult = CheckPolicy<validation_policy>(aValue);

      if (!aResult.has_value()) {
         throw PolicyViolation { std::string_view{ strKind }, svSource,
                                 aResult.error().strMessage };
         }
      }
    
   [[nodiscard]] static value_type Checked(value_type const& aValue) {
      RequireChecked(aValue);

      return aValue;
      }

   [[nodiscard]] static value_type Checked(value_type&& aValue) {
      RequireChecked(aValue);

      return std::move(aValue);
      }

   [[nodiscard]] static value_type Checked(value_type&& aValue, std::string_view const svSource) {
      RequireChecked(aValue, svSource);

      return std::move(aValue);
      }

private:
   value_type m_aValue {};
};

// -----------------------------------------------------------------------------
// PolicyValue -> std::string
// Darstellung geht immer über Stringify / str().
// Keine Konvertierung über input_type.
// -----------------------------------------------------------------------------
template<
   adecc::FixedString strKind,
   typename val_ty,
   adecc::ValidationPolicy<val_ty> validation_policy_ty,
   adecc::StringifyPolicy<val_ty> stringify_policy_ty
>
struct Convert<
   PolicyValue<
   strKind,
   val_ty,
   validation_policy_ty,
   stringify_policy_ty
   >,
   std::string
> {
   using source_ty =
      PolicyValue<
      strKind,
      val_ty,
      validation_policy_ty,
      stringify_policy_ty
      >;

   static std::string apply(source_ty const& aValue) {
      return aValue.str();
   }
};


// -----------------------------------------------------------------------------
// ConvertedPolicyValue -> std::string
// Darstellung geht immer über Stringify / str().
// Keine Konvertierung über conversion_policy_ty::input_type.
// -----------------------------------------------------------------------------
template<
   adecc::FixedString strKind,
   adecc::ConversionPolicy conversion_policy_ty,
   adecc::ValidationPolicy<typename conversion_policy_ty::value_type> validation_policy_ty,
   adecc::StringifyPolicy<typename conversion_policy_ty::value_type> stringify_policy_ty
>
struct Convert<
   ConvertedPolicyValue<
   strKind,
   conversion_policy_ty,
   validation_policy_ty,
   stringify_policy_ty
   >,
   std::string
> {
   using source_ty =
      ConvertedPolicyValue<
      strKind,
      conversion_policy_ty,
      validation_policy_ty,
      stringify_policy_ty
      >;

   static std::string apply(source_ty const& aValue) {
      return aValue.str();
   }
};


// -----------------------------------------------------------------------------
// PolicyValue -> target_ty
// Für alles außer std::string und außer std::optional<T>:
// nativen Wert verwenden und von dort weiter konvertieren.
//
// std::optional<T>-Ziele bleiben bewusst der Optional-Konvertierung
// Convert<From_ty, std::optional<T>> in convert_core.h vorbehalten.
// -----------------------------------------------------------------------------
template<
   adecc::FixedString strKind,
   typename val_ty,
   adecc::ValidationPolicy<val_ty> validation_policy_ty,
   adecc::StringifyPolicy<val_ty> stringify_policy_ty,
   typename target_ty
>
   requires (
!std::same_as<std::remove_cvref_t<target_ty>, std::string> &&
!adecc::is_optional_v<std::remove_cvref_t<target_ty>>
)
struct Convert<
   PolicyValue<
   strKind,
   val_ty,
   validation_policy_ty,
   stringify_policy_ty
   >,
   target_ty
> {
   using source_ty =
      PolicyValue<
      strKind,
      val_ty,
      validation_policy_ty,
      stringify_policy_ty
      >;

   static target_ty apply(source_ty const& aValue) {
      return adecc::ConvertTo<target_ty>(
         aValue.Value()
      );
   }
};


// -----------------------------------------------------------------------------
// ConvertedPolicyValue -> target_ty
// Für alles außer std::string und außer std::optional<T>:
// nativen Wert verwenden und von dort weiter konvertieren.
//
// std::optional<T>-Ziele bleiben bewusst der Optional-Konvertierung
// Convert<From_ty, std::optional<T>> in convert_core.h vorbehalten.
// Damit bleibt BirthDay_ty -> std::optional<date_ty> frei von Mehrdeutigkeit.
// -----------------------------------------------------------------------------
template<
   adecc::FixedString strKind,
   adecc::ConversionPolicy conversion_policy_ty,
   adecc::ValidationPolicy<typename conversion_policy_ty::value_type> validation_policy_ty,
   adecc::StringifyPolicy<typename conversion_policy_ty::value_type> stringify_policy_ty,
   typename target_ty
>
   requires (
!std::same_as<std::remove_cvref_t<target_ty>, std::string> &&
!adecc::is_optional_v<std::remove_cvref_t<target_ty>>
)
struct Convert<
   ConvertedPolicyValue<
   strKind,
   conversion_policy_ty,
   validation_policy_ty,
   stringify_policy_ty
   >,
   target_ty
> {
   using source_ty =
      ConvertedPolicyValue<
      strKind,
      conversion_policy_ty,
      validation_policy_ty,
      stringify_policy_ty
      >;

   static target_ty apply(source_ty const& aValue) {
      return adecc::ConvertTo<target_ty>(
         aValue.Value()
      );
   }
};
} // end of namespace adecc


namespace std {

   template<adecc::FixedString strKind, adecc::ConversionPolicy conversion_policy_ty,
      adecc::ValidationPolicy<typename conversion_policy_ty::value_type> validation_policy_ty,
      adecc::StringifyPolicy<typename conversion_policy_ty::value_type> stringify_policy_ty>
   struct formatter<adecc::ConvertedPolicyValue<strKind, conversion_policy_ty, validation_policy_ty, stringify_policy_ty>, char> {
      formatter<string_view, char> m_aFormatter;

      constexpr auto parse(format_parse_context& aContext) {
         return m_aFormatter.parse(aContext);
      }

      template<typename theFormatContext>
      auto format(adecc::ConvertedPolicyValue<strKind, conversion_policy_ty, validation_policy_ty, stringify_policy_ty> const& aValue,
         theFormatContext& aContext) const {
         std::string const strValue = aValue.str();

         return m_aFormatter.format(std::string_view{ strValue }, aContext);
      }
   };



   template<adecc::FixedString strKind, typename val_ty,
            adecc::ValidationPolicy<val_ty> validation_policy_ty,
            adecc::StringifyPolicy<val_ty> stringify_policy_ty>
   struct formatter<adecc::PolicyValue<strKind, val_ty, validation_policy_ty, stringify_policy_ty>, char> {
      formatter<string_view, char> m_aFormatter;

      constexpr auto parse(format_parse_context& aContext) {
         return m_aFormatter.parse(aContext);
      }

      template<typename theFormatContext>
      auto format(adecc::PolicyValue<strKind, val_ty, validation_policy_ty, stringify_policy_ty> const& aValue,
         theFormatContext& aContext) const {
         std::string const strValue = aValue.str();

         return m_aFormatter.format(std::string_view{ strValue }, aContext);
      }
   };
}



