// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file fixed_numeric.h
\brief Policy-based fixed-precision numeric value type with controlled rounding semantics.

\details
Implements adecc::numeric, which combines a floating-point storage type, a compile-time decimal count, and a
rounding policy in one value type. Arithmetic and assignment preserve the configured rounding rules so that
numeric semantics live in the type rather than in calling conventions.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Properties, Behavior, and Domain Value Types".
- "Policies as Typed Rules: Validation, Conversion, and Representation".
- "numeric: Behavior in the Type Instead of Convention in Code".
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

#include <iostream>
#include <iomanip>
#include <cmath>
#include <charconv>
#include <compare>
#include <string>
#include <concepts>
#include <cstddef>
#include <format>
#include <ostream>
#include <type_traits>
#include <stdexcept>

namespace adecc {

template<std::floating_point ty>
struct RoundHalfAwayFromZeroPolicy final {
   [[nodiscard]] static constexpr ty RoundIntegral(ty const val) noexcept {
      return std::round(val);
      }
   };

template<std::floating_point ty>
struct RoundTowardZeroPolicy final {
   [[nodiscard]] static constexpr ty RoundIntegral(ty const val) noexcept {
      return std::trunc(val);
      }
   };

template<std::floating_point ty>
struct RoundUpPolicy final {
   [[nodiscard]] static constexpr ty RoundIntegral(ty const val) noexcept {
      return std::ceil(val);
      }
   };



template<std::floating_point ty>
struct RoundHalfToEvenPolicy final {
   [[nodiscard]] static constexpr ty RoundIntegral(ty const val) noexcept {
      if (!std::isfinite(val)) {
         return val;
         }

      ty tIntegralPart {};
      ty const tFractionalPart    { std::modf(val, &tIntegralPart) };
      ty const tAbsFractionalPart { std::fabs(tFractionalPart) };
      ty constexpr tHalf          { static_cast<ty>(0.5) };

      if (tAbsFractionalPart < tHalf) {
         return tIntegralPart;
         }

      if (tAbsFractionalPart > tHalf) {
         return tIntegralPart + std::copysign(static_cast<ty>(1), val);
         }

      auto const iIntegralAsLongLong {static_cast<long long>(tIntegralPart)};
      bool const bIsEven {(iIntegralAsLongLong % 2LL) == 0LL};

      if (bIsEven) {
         return tIntegralPart;
         }

      return tIntegralPart + std::copysign(static_cast<ty>(1), val);
      }
   };

template<typename T, typename TPolicy>
concept RoundingPolicy =
   std::floating_point<T> &&
   requires (T const tValue) {
      { TPolicy::RoundIntegral(tValue) } noexcept -> std::same_as<T>;
   };


template<std::floating_point T, std::size_t uDecimals, typename TRoundingPolicy>
requires RoundingPolicy<T, TRoundingPolicy>
class numeric;

/**
\brief Checks whether a type is a specialization of adecc::numeric.
\tparam T Type to check.
*/
template<typename T>
struct is_numeric : std::false_type {};

template<std::floating_point T, std::size_t uDecimals, typename TRoundingPolicy>
struct is_numeric<numeric<T, uDecimals, TRoundingPolicy>> : std::true_type {};

template<typename T>
inline constexpr bool is_numeric_v {is_numeric<std::remove_cvref_t<T>>::value};

template <typename ty>
concept numeric_type = is_numeric_v<ty>;


/**
\brief Concept for raw or wrapped numeric operands.
\details
Arithmetic types and specializations of adecc::numeric are accepted.

\tparam T Type to check.
*/
template<typename T>
concept NumericOperand =
   std::is_arithmetic_v<std::remove_cvref_t<T>> || is_numeric_v<T>;

/**
\brief Wraps a floating-point type with automatic rounding.
\details
This class stores a floating-point value and rounds it to a fixed number of decimal places
on every assignment and after every modifying operation.

The number of decimal places is a template parameter. The rounding strategy is supplied
through a policy.

Interoperation zwischen unterschiedlichen Spezialisierungen von numeric
is supported. Assignment and modifying operations always use
the rules of the target instance.

\tparam T Underlying floating-point type.
\tparam uDecimals Number of decimal places.
\tparam TRoundingPolicy Rounding policy.
*/
template<std::floating_point T, std::size_t uDecimals, typename TRoundingPolicy>
    requires RoundingPolicy<T, TRoundingPolicy>
class numeric final {
   public:
      using ValueType = T;
      using RoundingPolicyType = TRoundingPolicy;

      static constexpr std::size_t Decimals {uDecimals};

   private:
      T m_tValue {};

      /**
      \brief Computes the scale factor at compile time.
      \returns Scale factor.
      */
      [[nodiscard]]
      static consteval T ComputeScale() noexcept {
         T tScale {static_cast<T>(1)};

         for (std::size_t uIndex {0}; uIndex < uDecimals; ++uIndex) {
            tScale *= static_cast<T>(10);
         }

         return tScale;
      }

      inline static constexpr T c_tScale {ComputeScale()};

      /**
      \brief Extracts a value as type T.
      \details
      Raw arithmetic operands are converted to T. Other numeric specializations contribute
      their stored value
      read and converted to T.

      \tparam U Operand type.
      \param aValue Operand.
      \returns Extracted value as T.
      */
      template<NumericOperand U>
      [[nodiscard]]
      static constexpr T ConvertToValueType(U const& aValue) noexcept {
         if constexpr (is_numeric_v<U>) {
            return static_cast<T>(aValue.Get());
         }
         else {
            return static_cast<T>(aValue);
         }
      }

      /**
      \brief Rounds a value according to the configured policy and decimal count.
      \param tValue Value to round.
      \returns Rounded value.
      */
      [[nodiscard]]
      static constexpr T RoundValue(T const tValue) noexcept {
         if (!std::isfinite(tValue)) {
            // Preserve non-finite values; throwing would require removing noexcept
            return tValue;
         }

         T const tScaledValue {tValue * c_tScale};

         if (!std::isfinite(tScaledValue)) {
            // Preserve overflow here; throwing would require a different contract
            return tValue;
         }

         return TRoundingPolicy::RoundIntegral(tScaledValue) / c_tScale;
      }

      /**
      \brief Stores a value after applying the rounding policy.
      \param tValue New value.
      */
      constexpr void AssignRounded(T const tValue) noexcept {
         m_tValue = RoundValue(tValue);
      }

   public:
      /**
      \brief Constructs a zero value.
      */
      constexpr numeric() noexcept = default;

      /**
      \brief Constructs from a floating-point value.
      \param tValue Initial value.
      */
      constexpr explicit numeric(T const tValue) noexcept {
         AssignRounded(tValue);
      }

      /**
      \brief Constructs from any supported operand.
      \tparam U Source type.
      \param aValue Initial value.
      */
      template<NumericOperand U>
      requires (!std::same_as<std::remove_cvref_t<U>, numeric>)
      constexpr explicit numeric(U const& aValue) noexcept {
         AssignRounded(ConvertToValueType(aValue));
      }

      /**
      \brief Constructs from another numeric specialization.
      \tparam U Source floating-point type.
      \tparam uOtherDecimals Quell Genauigkeit.
      \tparam TOtherPolicy Quell Policy.
      \param theOther Initial value.
      */
      template<std::floating_point U, std::size_t uOtherDecimals, typename TOtherPolicy>
      requires RoundingPolicy<U, TOtherPolicy>
      constexpr explicit numeric(numeric<U, uOtherDecimals, TOtherPolicy> const& theOther) noexcept {
         AssignRounded(static_cast<T>(theOther.Get()));
      }

      /**
      \brief Returns the stored value.
      \returns Internally stored rounded value.
      */
      [[nodiscard]]
      constexpr T Get() const noexcept {
         return m_tValue;
      }

      /**
      \brief Implicit conversion to the underlying type.
      \returns Internally stored value.
      */
      constexpr operator T() const noexcept {
         return m_tValue;
      }

      /**
      \brief Assigns a floating-point value.
      \param tValue New value.
      \returns Reference to the current object.
      */
      constexpr numeric& operator=(T const tValue) noexcept {
         AssignRounded(tValue);
         return *this;
      }

      /**
      \brief Assigns a raw arithmetic value.
      \tparam U Source type.
      \param aValue New value.
      \returns Reference to the current object.
      */
      template<typename U>
      requires (std::is_arithmetic_v<std::remove_cvref_t<U>> && !std::same_as<std::remove_cvref_t<U>, T>)
      constexpr numeric& operator=(U const& aValue) noexcept {
         AssignRounded(static_cast<T>(aValue));
         return *this;
      }

      /**
      \brief Assigns a value from another numeric specialization.
      \tparam U Source floating-point type.
      \tparam uOtherDecimals Quell Genauigkeit.
      \tparam TOtherPolicy Quell Policy.
      \param theOther New value.
      \returns Reference to the current object.
      */
      template<std::floating_point U, std::size_t uOtherDecimals, typename TOtherPolicy>
      requires RoundingPolicy<U, TOtherPolicy>
      constexpr numeric& operator=(numeric<U, uOtherDecimals, TOtherPolicy> const& theOther) noexcept {
         AssignRounded(static_cast<T>(theOther.Get()));
         return *this;
      }

      /**
      \brief Adds a supported operand and rounds according to this instance's policy.
      \tparam U Right-hand operand type.
      \param aValue Right-hand operand.
      \returns Reference to the current object.
      */
      template<NumericOperand U>
      constexpr numeric& operator+=(U const& aValue) noexcept {
         AssignRounded(m_tValue + ConvertToValueType(aValue));
         return *this;
      }

      /**
      \brief Subtracts a supported operand and rounds according to this instance's policy.
      \tparam U Right-hand operand type.
      \param aValue Right-hand operand.
      \returns Reference to the current object.
      */
      template<NumericOperand U>
      constexpr numeric& operator-=(U const& aValue) noexcept {
         AssignRounded(m_tValue - ConvertToValueType(aValue));
         return *this;
      }

      /**
      \brief Multiplies by a supported operand and rounds according to this instance's policy.
      \tparam U Right-hand operand type.
      \param aValue Right-hand operand.
      \returns Reference to the current object.
      */
      template<NumericOperand U>
      constexpr numeric& operator*=(U const& aValue) noexcept {
         AssignRounded(m_tValue * ConvertToValueType(aValue));
         return *this;
      }

      /**
      \brief Divides by a supported operand and rounds according to this instance's policy.
      \tparam U Right-hand operand type.
      \param aValue Right-hand operand.
      \returns Reference to the current object.
      */
      template<NumericOperand U>
      constexpr numeric& operator/=(U const& aValue) noexcept {
         AssignRounded(m_tValue / ConvertToValueType(aValue));
         return *this;
      }

      /**
      \brief Prefix increment.
      \returns Reference to the current object.
      */
      constexpr numeric& operator++() noexcept {
         AssignRounded(m_tValue + static_cast<T>(1));
         return *this;
      }

      /**
      \brief Postfix Inkrement.
      \param iDummy Dummy parameter.
      \returns Previous value.
      */
      constexpr numeric operator++(int const iDummy) noexcept {
         static_cast<void>(iDummy);
         numeric const aOld {*this};
         ++(*this);
         return aOld;
      }

      /**
      \brief Prefix decrement.
      \returns Reference to the current object.
      */
      constexpr numeric& operator--() noexcept {
         AssignRounded(m_tValue - static_cast<T>(1));
         return *this;
      }

      /**
      \brief Postfix Dekrement.
      \param iDummy Dummy parameter.
      \returns Previous value.
      */
      constexpr numeric operator--(int const iDummy) noexcept {
         static_cast<void>(iDummy);
         numeric const aOld {*this};
         --(*this);
         return aOld;
      }

      /**
      \brief Unary plus.
      \returns Copy of the current value.
      */
      [[nodiscard]]
      constexpr numeric operator+() const noexcept {
         return *this;
      }

      /**
      \brief Unary minus.
      \returns Negated and rounded result.
      */
      [[nodiscard]]
      constexpr numeric operator-() const noexcept {
         return numeric {-m_tValue};
      }

      /**
      \brief Compares with another numeric specialization.
      \details
      Both values are converted to a common type before comparison.
      \param theOther Other operand.
      \returns Vergleichsergebnis.
      */
      template<std::floating_point U, std::size_t uOtherDecimals, typename TOtherPolicy>
      [[nodiscard]]
      constexpr auto operator<=>(numeric<U, uOtherDecimals, TOtherPolicy> const& theOther) const noexcept {
         using CommonType = std::common_type_t<T, U>;
         return static_cast<CommonType>(m_tValue) <=> static_cast<CommonType>(theOther.Get());
      }

      /**
      \brief Compares with another numeric specialization.
      \param theOther Other operand.
      \returns true if and only if both values are equal.
      */
      template<std::floating_point U, std::size_t uOtherDecimals, typename TOtherPolicy>
      [[nodiscard]]
      constexpr bool operator==(numeric<U, uOtherDecimals, TOtherPolicy> const& theOther) const noexcept {
         using CommonType = std::common_type_t<T, U>;
         return static_cast<CommonType>(m_tValue) == static_cast<CommonType>(theOther.Get());
      }

      /**
      \brief Compares with a raw arithmetic type.
      \param aValue Other operand.
      \returns Vergleichsergebnis.
      */
      template<typename U>
      requires std::is_arithmetic_v<std::remove_cvref_t<U>>
      [[nodiscard]]
      constexpr auto operator<=>(U const& aValue) const noexcept {
         using CommonType = std::common_type_t<T, std::remove_cvref_t<U>>;
         return static_cast<CommonType>(m_tValue) <=> static_cast<CommonType>(aValue);
      }

      /**
      \brief Compares with a raw arithmetic type.
      \param aValue Other operand.
      \returns true if and only if both values are equal.
      */
      template<typename U>
      requires std::is_arithmetic_v<std::remove_cvref_t<U>>
      [[nodiscard]]
      constexpr bool operator==(U const& aValue) const noexcept {
         using CommonType = std::common_type_t<T, std::remove_cvref_t<U>>;
         return static_cast<CommonType>(m_tValue) == static_cast<CommonType>(aValue);
      }

      /**
      \brief Adds a supported operand.
      \details
      The rules of the left numeric operand apply.
      \tparam U Right-hand operand type.
      \param theLeft Linker Operand.
      \param aValue Rechter Operand.
      \returns Rounded result as the left-hand operand type.
      */
      template<NumericOperand U>
      [[nodiscard]]
      friend constexpr numeric operator+(numeric const& theLeft, U const& aValue) noexcept {
         numeric aResult {theLeft};
         aResult += aValue;
         return aResult;
      }

      /**
      \brief Subtracts a supported operand.
      \details
      The rules of the left numeric operand apply.
      \tparam U Right-hand operand type.
      \param theLeft Linker Operand.
      \param aValue Rechter Operand.
      \returns Rounded result as the left-hand operand type.
      */
      template<NumericOperand U>
      [[nodiscard]]
      friend constexpr numeric operator-(numeric const& theLeft, U const& aValue) noexcept {
         numeric aResult {theLeft};
         aResult -= aValue;
         return aResult;
      }

      /**
      \brief Multiplies by a supported operand.
      \details
      The rules of the left numeric operand apply.
      \tparam U Right-hand operand type.
      \param theLeft Linker Operand.
      \param aValue Rechter Operand.
      \returns Rounded result as the left-hand operand type.
      */
      template<NumericOperand U>
      [[nodiscard]]
      friend constexpr numeric operator*(numeric const& theLeft, U const& aValue) noexcept {
         numeric aResult {theLeft};
         aResult *= aValue;
         return aResult;
      }

      /**
      \brief Divides by a supported operand.
      \details
      The rules of the left numeric operand apply.
      \tparam U Right-hand operand type.
      \param theLeft Linker Operand.
      \param aValue Rechter Operand.
      \returns Rounded result as the left-hand operand type.
      */
      template<NumericOperand U>
      [[nodiscard]]
      friend constexpr numeric operator/(numeric const& theLeft, U const& aValue) noexcept {
         numeric aResult {theLeft};
         aResult /= aValue;
         return aResult;
      }

      /**
      \brief Adds a numeric value to a raw left-hand operand.
      \details
      The rules of the numeric operand apply.
      \tparam U Left-hand operand type.
      \param aValue Left-hand operand.
      \param theRight Right-hand numeric operand.
      \returns Rounded result as the numeric operand type.
      */
      template<typename U>
      requires std::is_arithmetic_v<std::remove_cvref_t<U>>
      [[nodiscard]]
      friend constexpr numeric operator+(U const& aValue, numeric const& theRight) noexcept {
         numeric aResult {aValue};
         aResult += theRight;
         return aResult;
      }

      /**
      \brief Subtracts a numeric value from a raw left-hand operand.
      \details
      The rules of the numeric operand apply.
      \tparam U Left-hand operand type.
      \param aValue Left-hand operand.
      \param theRight Right-hand numeric operand.
      \returns Rounded result as the numeric operand type.
      */
      template<typename U>
      requires std::is_arithmetic_v<std::remove_cvref_t<U>>
      [[nodiscard]]
      friend constexpr numeric operator-(U const& aValue, numeric const& theRight) noexcept {
         numeric aResult {aValue};
         aResult -= theRight;
         return aResult;
      }

      /**
      \brief Multiplies a raw left-hand operand by a numeric value.
      \details
      The rules of the numeric operand apply.
      \tparam U Left-hand operand type.
      \param aValue Left-hand operand.
      \param theRight Right-hand numeric operand.
      \returns Rounded result as the numeric operand type.
      */
      template<typename U>
      requires std::is_arithmetic_v<std::remove_cvref_t<U>>
      [[nodiscard]]
      friend constexpr numeric operator*(U const& aValue, numeric const& theRight) noexcept {
         numeric aResult {aValue};
         aResult *= theRight;
         return aResult;
      }

      /**
      \brief Divides a raw left-hand operand by a numeric value.
      \details
      The rules of the numeric operand apply.
      \tparam U Left-hand operand type.
      \param aValue Left-hand operand.
      \param theRight Right-hand numeric operand.
      \returns Rounded result as the numeric operand type.
      */
      template<typename U>
      requires std::is_arithmetic_v<std::remove_cvref_t<U>>
      [[nodiscard]]
      friend constexpr numeric operator/(U const& aValue, numeric const& theRight) noexcept {
         numeric aResult {aValue};
         aResult /= theRight;
         return aResult;
      }

      /**
      \brief Writes the stored value to a stream.
      \param theOs Target stream.
      \param theValue Value to write.
      \returns Reference to the target stream.
      */
      friend std::ostream& operator<<(std::ostream& theOs, numeric const& theValue) {
         // add fixed and add showpoint
         return theOs << std::setprecision(uDecimals) << theValue.m_tValue;
      }
};



} // namespace adecc

/**
\brief Formatter for adecc::numeric.
\details
This specialization delegates formatting to the formatter of the underlying floating-point
value type, so format specifications such as {:.2f}, {:>10.3f}, or {:g} can be used directly.

\tparam T Underlying floating-point type.
\tparam uDecimals Number of decimal places.
\tparam TRoundingPolicy Rounding policy.
*/
template<std::floating_point T, std::size_t uDecimals, typename TRoundingPolicy>
struct std::formatter<adecc::numeric<T, uDecimals, TRoundingPolicy>, char> : std::formatter<T, char> {
   /**
   \brief Formats an adecc::numeric value.
   \param theValue Value to format.
   \param theContext Format context.
   \returns Iterator past the formatted output.
   */
   template<typename TFormatContext>
   auto format(adecc::numeric<T, uDecimals, TRoundingPolicy> const& theValue, TFormatContext& theContext) const {
      return std::formatter<T, char>::format(theValue.Get(), theContext);
   }
};
