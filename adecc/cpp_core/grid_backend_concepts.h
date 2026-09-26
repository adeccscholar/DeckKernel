// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file grid_backend_concepts.h
\brief Backend concepts defining the minimal physical contracts required by grid models.

\details
Separates the typed grid model from concrete UI frameworks by expressing capabilities such as dimensions,
cell access, row insertion, reset, freeze, and optional backend services as compile-time requirements.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Grid Models as the Next Step".
- "Grids as Ranges: The UI Loses Its Special Status".
- "The Grid as a Projection, Not as Truth".
- "From Text and Grid to a General Adapter Strategy".

\see ARCHITECTURE.md#grid-projection-and-ui-boundaries

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

#include "wrapper_basic.h"
#include "type_lists.h"
#include "value_types.h"

#include <concepts>
#include <cstddef>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace adecc {
namespace grid {

// ============================================================================
// Allgemeine Tuple-Hilfen
// ============================================================================

template <class ty>
struct is_tuple : std::false_type {};

template <class... Ts>
struct is_tuple<std::tuple<Ts...>> : std::true_type {};

template <class ty>
inline constexpr bool is_tuple_v = is_tuple<std::remove_cvref_t<ty>>::value;

// ============================================================================
// Gemeinsame Basisschnittstellen
// ============================================================================

template <class backend_ty>
concept grid_dimension_backend_type =
   requires(backend_ty& a) {
      { a.rows() }    -> std::convertible_to<std::size_t>;
      { a.columns() } -> std::convertible_to<std::size_t>;
      };

template <class backend_ty>
concept grid_reset_backend_type = adecc::wrapper::has_reset_for<backend_ty>;

template <class backend_ty>
concept grid_freeze_backend_type = adecc::wrapper::has_freeze_for<backend_ty>;

template <class backend_ty>
concept grid_base_backend_type =
   grid_dimension_backend_type<backend_ty> &&
   grid_reset_backend_type<backend_ty> &&
   grid_freeze_backend_type<backend_ty>;

// ============================================================================
// Caption / Layout / Repository
// ============================================================================

template <class backend_ty, class... Hdr>
concept has_set_caption_for =
   requires(backend_ty& b, std::vector<std::tuple<Hdr...>> const& vecCaps, bool bClear) {
      { b.template set_caption<Hdr...>(vecCaps, bClear) } -> std::same_as<void>;
      };

template <class backend_ty, class... Hdr>
concept has_write_set_caption_for = has_set_caption_for<backend_ty, Hdr...>;

template <class backend_ty, class bands_ty, class caps_ty>
concept has_prepare_layout_for =
   requires(backend_ty& a, bands_ty const& theBands, caps_ty const& theCaps, bool bClear) {
      { a.prepare_layout(theBands, theCaps, bClear) } -> std::same_as<void>;
      };

template <class backend_ty, class repository_ty>
concept has_set_repository_for =
   requires(backend_ty& a, repository_ty const& theRepository) {
      { a.set_repository(theRepository) } -> std::same_as<void>;
      };

// ============================================================================
// Random Access Grid: get/set
// ============================================================================

template <class backend_ty, class value_ty>
concept has_get_cell_value_for =
   requires(backend_ty& a, std::size_t iRow, std::size_t iCol) {
      { a.template get<value_ty>(iRow, iCol) } -> std::same_as<value_ty>;
      };

template <class backend_ty, class value_ty>
concept has_get_cell_opt_for =
   requires(backend_ty& a, std::size_t iRow, std::size_t iCol) {
      { a.template get<std::optional<value_ty>>(iRow, iCol) } -> std::same_as<std::optional<value_ty>>;
      };

template <class backend_ty, class value_ty>
concept has_set_cell_value_for =
   requires(backend_ty& a, std::size_t iRow, std::size_t iCol, value_ty const& theValue) {
      { a.template set<value_ty>(iRow, iCol, theValue) } -> std::same_as<void>;
      };

template <class backend_ty, class value_ty>
concept has_set_cell_opt_for =
   requires(backend_ty& a, std::size_t iRow, std::size_t iCol, std::optional<value_ty> const& theValue) {
      { a.template set<std::optional<value_ty>>(iRow, iCol, theValue) } -> std::same_as<void>;
      };

template <class backend_ty, class List>
struct check_get_all_opt : std::false_type {};

template <class backend_ty, class List>
struct check_get_all_val : std::false_type {};

template <class backend_ty, class List>
struct check_set_all_val : std::false_type {};

template <class backend_ty, class List>
struct check_set_all_opt : std::false_type {};

template <class backend_ty, class... Ts>
struct check_get_all_opt<backend_ty, defined_type_list<Ts...>>
   : std::bool_constant<(has_get_cell_opt_for<backend_ty, Ts> && ...)> {};

template <class backend_ty, class... Ts>
struct check_get_all_val<backend_ty, defined_type_list<Ts...>>
   : std::bool_constant<(has_get_cell_value_for<backend_ty, Ts> && ...)> {};

template <class backend_ty, class... Ts>
struct check_set_all_val<backend_ty, defined_type_list<Ts...>>
   : std::bool_constant<(has_set_cell_value_for<backend_ty, Ts> && ...)> {};

template <class backend_ty, class... Ts>
struct check_set_all_opt<backend_ty, defined_type_list<Ts...>>
   : std::bool_constant<(has_set_cell_opt_for<backend_ty, Ts> && ...)> {};

template <class backend_ty>
concept grid_random_access_read_backend_type =
   check_get_all_val<backend_ty, defined_values_types>::value &&
   check_get_all_opt<backend_ty, defined_values_types>::value;

template <class backend_ty>
concept grid_random_access_write_backend_type =
   check_set_all_val<backend_ty, defined_param_types>::value &&
   check_set_all_opt<backend_ty, defined_param_types>::value;

// ============================================================================
// Regular table/grid backends
// ============================================================================

template <class backend_ty>
concept has_insert_row_for =
   requires(backend_ty& a) {
      { a.insert_row() } -> std::convertible_to<std::size_t>;
      };

template <class backend_ty>
concept has_current_cell_for =
   requires(backend_ty& a) {
      { a.current_row() }    -> std::convertible_to<std::size_t>;
      { a.current_column() } -> std::convertible_to<std::size_t>;
      };

template <class backend_ty>
concept has_focus_row_for =
   requires(backend_ty& a, std::size_t iRow) {
      { a.focus_row(iRow) } -> std::same_as<bool>;
      };

template <class backend_ty>
concept grid_table_backend_type =
   grid_base_backend_type<backend_ty> &&
   grid_random_access_read_backend_type<backend_ty> &&
   grid_random_access_write_backend_type<backend_ty> &&
   has_insert_row_for<backend_ty> &&
   has_current_cell_for<backend_ty> &&
   has_focus_row_for<backend_ty>;

template <class backend_ty>
concept table_type = grid_table_backend_type<backend_ty>;

// ============================================================================
// Sequential Write Grid: append_row / append_col
// ============================================================================

template <class backend_ty, class value_ty>
concept has_append_col_value_for =
   requires(backend_ty& a, value_ty const& theValue) {
      { a.template append_col<value_ty>(theValue) } -> std::same_as<void>;
      };

template <class backend_ty, class value_ty>
concept has_append_col_opt_for =
   requires(backend_ty& a, std::optional<value_ty> const& theValue) {
      { a.template append_col<std::optional<value_ty>>(theValue) } -> std::same_as<void>;
      };

template <class backend_ty, class List>
struct check_append_col_all_val : std::false_type {};

template <class backend_ty, class List>
struct check_append_col_all_opt : std::false_type {};

template <class backend_ty, class... Ts>
struct check_append_col_all_val<backend_ty, defined_type_list<Ts...>>
   : std::bool_constant<(has_append_col_value_for<backend_ty, Ts> && ...)> {};

template <class backend_ty, class... Ts>
struct check_append_col_all_opt<backend_ty, defined_type_list<Ts...>>
   : std::bool_constant<(has_append_col_opt_for<backend_ty, Ts> && ...)> {};

template <class backend_ty>
concept has_append_row_for =
   requires(backend_ty& a) {
      { a.append_row() } -> std::same_as<void>;
      };

template <class backend_ty>
concept grid_sequential_write_backend_type =
   grid_base_backend_type<backend_ty> &&
   has_append_row_for<backend_ty> &&
   check_append_col_all_val<backend_ty, defined_param_types>::value &&
   check_append_col_all_opt<backend_ty, defined_param_types>::value;

template <class backend_ty>
concept sequential_write_grid_backend_type = grid_sequential_write_backend_type<backend_ty>;

template <class backend_ty>
concept has_write_reset_for = grid_reset_backend_type<backend_ty>;

template <class backend_ty>
concept has_write_freeze_for =
   requires(backend_ty& a) {
      { a.freeze() } -> std::same_as<bool>;
      };

template <class backend_ty>
concept has_write_unfreeze_for =
   requires(backend_ty& a, bool bFrozen) {
      { a.unfreeze(bFrozen) } -> std::same_as<void>;
      };

// ============================================================================
// Gemeinsamer WriteGrid Begriff
// ============================================================================

template <class backend_ty>
concept grid_write_backend_type =
   grid_sequential_write_backend_type<backend_ty> ||
   grid_table_backend_type<backend_ty>;

} // namespace grid
} // namespace adecc
