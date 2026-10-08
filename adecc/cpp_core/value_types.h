// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file value_types.h
\brief Shared value-type vocabulary for dates, times, money, database values, and parameters.

\details
Defines framework-independent core aliases and variants used by conversion and database layers, including
chrono-based date/time types, policy-based money values, optional result support, named database parameters,
and output parameter roles.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Properties, Behavior, and Domain Value Types".
- "Values, Parameters, and the Defined Database Type Space".
- "Optionality: No Value Is No Value".
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

#include "type_traits_ext.h"
#include "type_lists.h"
#include "fixed_numeric.h"

#include "convert_core.h"
#include "convert_fixed.h"

#include <string>
#include <string_view>
#include <vector>
#include <tuple>
#include <chrono>
#include <type_traits>
#include <concepts>

#include <format>
#include <source_location>

namespace adecc {

using date_ty = std::chrono::year_month_day;
using timestamp_ty = std::chrono::system_clock::time_point;
using time_ty = std::chrono::hh_mm_ss<std::chrono::seconds>;
using src_loc = std::source_location;

using money_ty = numeric<double, 2, RoundHalfAwayFromZeroPolicy<double>>;

enum class EAlignmentType : uint32_t { left = 1, right = 2, center = 3, unknown = 100 };


using defined_values_types = defined_type_list<std::string, std::wstring,
                                               double, money_ty,
                                               int, long long, bool,
                                               unsigned int, unsigned long long,
                                               date_ty, timestamp_ty, time_ty>;

using defined_param_types  = defined_type_list<std::string_view, char const*, std::string,
                                               std::wstring_view, wchar_t const*, std::wstring,
                                               double, money_ty,
                                               int, long long, bool,
                                               unsigned int, unsigned long long,
                                               date_ty, timestamp_ty, time_ty>;

using db_value   = typename defined_values_types::type_variant;
using db_param   = typename defined_param_types::param_type_variant;
using db_param2  = std::tuple<std::string, db_param, bool>;
using db_params  = std::vector<db_param2>;

enum class db_output_param_role {
   needed_value,   // Value must occur in the SQL statement
   may_be_missing, // Value may be absent from the SQL statement and then remains unchanged
   key,            // Value is a key and must occur in the SQL statement
   identity        // Value is not supplied as input and may be replaced after INSERT
};

using db_output_parameter  = std::tuple<std::string, db_output_param_role>;
using db_output_parameters = std::vector<db_output_parameter>;

/**
\brief Folds over \c Args... to check whether all result arguments are supported
\details Supported types are direct database value types or \c std::optional wrappers of them
*/
template <class... Args>
struct all_result_args_ok
   : std::bool_constant<
        ( ... && (
           is_in_type_list_v<Args, defined_values_types> ||
           ( is_optional_v<Args> &&
             is_in_type_list_v<optional_value_type_t<Args>, defined_values_types> )
        ) )
     > {};

template <class... Args>
inline constexpr bool all_result_args_ok_v = all_result_args_ok<Args...>::value;

/**
\brief Checks whether the named parameter list has exactly the expected type
*/
template <class ty>
inline constexpr bool is_param_list_v = std::same_as<remove_cvref_t<ty>, db_params>;

// ============================================================================
// CONCEPTS
// ============================================================================

/*!
\brief Single result type: a defined value type or an optional wrapper of it
*/
template <class ty>
concept db_result_type =
   is_in_type_list_v<ty, defined_values_types> ||
   ( is_optional_v<ty> && is_in_type_list_v<optional_value_type_t<ty>, defined_values_types> );

/**
\brief Complete result-row tuple, for example std::tuple<int, std::optional<std::string>, date_ty>
*/
template <class ty>
concept db_result_tuple =  requires { typename remove_cvref_t<ty>; }
   && []<class... Es>(std::tuple<Es...>*) {
         return all_result_args_ok_v<Es...>;
      }( static_cast<typename std::add_pointer_t<remove_cvref_t<ty>>>(nullptr) );


/**
\brief Single parameter atom from defined_param_types, for example string_view or const char*
*/
template <class ty>
concept db_param_atom = is_in_type_list_v<ty, defined_param_types>;

/**
\brief Parameter variant exactly matching db_param
*/
template <class ty>
concept db_param_variant = std::same_as<remove_cvref_t<ty>, db_param>;

/**
\brief Named parameter record: tuple<string, db_param, bool>
*/
template <class ty>
concept db_named_param = std::same_as<remove_cvref_t<ty>, db_param2>;

/**
\brief Parameter list: vector<db_param2>
*/
template <class ty>
concept db_param_list = std::same_as<remove_cvref_t<ty>, db_params>;

} // namespace adecc

namespace std {

   template <>
   struct formatter<std::source_location> : formatter<std::string_view> {
	  template <typename FormatContext>
	  auto format(const std::source_location& loc, FormatContext& ctx) {
		 std::string_view format_str = "{} ({},{}) [{}]";
		 return std::format_to(ctx.out(), format_str, loc.file_name(), loc.line(), loc.column(), loc.function_name());
	  }
   };

}



