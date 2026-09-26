// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file type_traits_ext.h
\brief Extended type traits and concepts for optionals, tuples, variants, strings, and ranges.

\details
Collects reusable compile-time predicates that classify values and containers for conversion, tuple,
persistence, and wrapper code. These traits make structural constraints explicit and allow Concepts to
describe valid relationships instead of relying on runtime checks.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Type Space, Core, and Boundary: The Conceptual Framework".
- "Tuple Calculus and Concepts".
- "Type Lists as Structured Type Relations".
- "Compile-Time Transformations as Algebra".

\see ARCHITECTURE.md#type-space-core-and-boundary

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

#include <string>
#include <vector>
#include <type_traits>
#include <optional>
#include <tuple>
#include <variant>
#include <concepts>

namespace adecc {

// ---------------------------------------------------------------
// Helper traits: optional detection 
// ---------------------------------------------------------------
template <class ty> struct is_optional : std::false_type {};
template <class ty> struct is_optional<std::optional<ty>> : std::true_type {};
template <class ty> inline constexpr bool is_optional_v = is_optional<ty>::value;



template <class ty>
struct optional_value {};

template <class ty>
struct optional_value<std::optional<ty>> { using type = ty; };

template <class ty>
using optional_value_t = typename optional_value<std::remove_cvref_t<ty>>::type;
                                                 

// Compatibility implementation until the standard facility is available in all supported toolchains



// ----------------------------------------------------------------------------
// Helper: checks whether a type is exactly Cell or optional<Cell>
// ----------------------------------------------------------------------------
template <typename ty, typename U>
concept value_or_optional = std::same_as<ty, U> || std::same_as<ty, std::optional<U>>;

template <typename ty> 
struct optional_value_type { using type = void; };

template <typename ty> 
struct optional_value_type<std::optional<ty>> { using type = ty; };

template <typename ty> 
using optional_value_type_t = typename optional_value_type<std::remove_cvref_t<ty>>::type;


template <typename ty>
struct value_type_or_self {
	using type = std::remove_cvref_t<ty>;
   };

template <typename ty>
struct value_type_or_self<std::optional<ty>> {
	using type = ty;
   };

template <typename ty>
using value_type_or_self_t = typename value_type_or_self<std::remove_cvref_t<ty>>::type;

// ---------------------------------------------------------------------------

template <class ty>
struct is_tuple : std::false_type { };

template <class... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type { };

template <class ty>
inline constexpr bool is_tuple_v = is_tuple<std::remove_cvref_t<ty>>::value;


template <class ty, class = void> struct is_tuple_like : std::false_type {};

template <class ty>
struct is_tuple_like<ty, std::void_t<decltype(std::tuple_size<std::remove_cvref_t<ty>>::value)>>
  : std::true_type {};
  
template <class ty> inline constexpr bool is_tuple_like_v = is_tuple_like<ty>::value;


template <class ty>
concept tuple_like_ty = is_tuple_like_v<ty>;

template <class ty, std::size_t uIndex>
concept tuple_index_available = tuple_like_ty<ty> && 
              (uIndex < std::tuple_size_v<std::remove_cvref_t<ty>>);

// ----------------------------------------------------------------------------

template <typename ty>
concept NotOptional = !is_optional_v<std::remove_cvref_t<ty>>;

template <typename ty>
concept NotTupleLike = !requires {
   typename std::tuple_size<std::remove_cvref_t<ty>>::type;
   };

template <typename ty>
concept NotVariant = !requires {
   typename std::variant_size<std::remove_cvref_t<ty>>::type;
   };

template <typename ty>
concept StringLike = std::same_as<std::remove_cvref_t<ty>, std::string> ||
					 std::same_as<std::remove_cvref_t<ty>, std::string_view> ||
					 std::same_as<std::remove_cvref_t<ty>, std::wstring> ||
					 std::same_as<std::remove_cvref_t<ty>, std::wstring_view> ||
					 std::same_as<std::remove_cvref_t<ty>, std::u8string> ||
					 std::same_as<std::remove_cvref_t<ty>, std::u8string_view> ||
					 std::same_as<std::remove_cvref_t<ty>, std::u16string> ||
					 std::same_as<std::remove_cvref_t<ty>, std::u16string_view> ||
					 std::same_as<std::remove_cvref_t<ty>, std::u32string> ||
					 std::same_as<std::remove_cvref_t<ty>, std::u32string_view>;

template <typename ty>
concept NotContainer = StringLike<ty> ||
					   !std::ranges::range<std::remove_cvref_t<ty>>;


template <typename ty>
concept PlainValue = NotOptional<ty> &&
					 NotTupleLike<ty> &&
					 NotVariant<ty> &&
					 NotContainer<ty>;

// OptionalConvertibleBase
template <typename From_ty, typename To_ty>
concept PlainValuePair = PlainValue<From_ty> && PlainValue<To_ty>;

template <typename val_ty>
struct is_vector : std::false_type {};

template <typename val_ty, typename alloc_ty>
struct is_vector<std::vector<val_ty, alloc_ty>> : std::true_type {};

template <typename val_ty>
concept vector_type = is_vector<std::remove_cvref_t<val_ty>>::value;

} // namespace adecc
