// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file system_data_persistent_basic.h
\brief Compile-time metadata concepts and projection machinery for persistent system data.

\details
Validates persistence metadata, table descriptions, key and identity positions, read-only fields, and
multi-table projections. The file translates metadata declarations into a form that can be checked at compile
time before SQL generation or execution.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "SystemData as an Application of the Type List".
- "PersistentSystemData as an Extension of SystemData".
- "Database as Source, Transformation, and Sink".
- "Tests as Architectural Proof".

\see ../ARCHITECTURE.md#persistentsystemdata

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

#include <tuple>
#include <string_view>
#include <type_traits>
#include <concepts>

namespace adecc::db {

/**
 \brief Checks whether a metadata type provides the information required for persistence.
 \details Expects a table name, attribute names, key indices, an optional identity index, and read-only indices.
 */
template <typename ty>
concept size_t_array_type = requires {
      typename std::tuple_size<std::remove_cvref_t<ty>>::type;
      } && []<std::size_t... Is>(std::index_sequence<Is...>) {
                 return (std::same_as<std::tuple_element_t<Is, std::remove_cvref_t<ty>>,
                         std::size_t> && ...);
                 }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<ty>>>{});

template <typename ty>
concept string_view_array_type = requires {
      typename std::tuple_size<std::remove_cvref_t<ty>>::type;
      } && []<std::size_t... Is>(std::index_sequence<Is...>) {
                return (std::same_as<std::tuple_element_t<Is, std::remove_cvref_t<ty>>, 
                        std::string_view> && ...);
                }(std::make_index_sequence<std::tuple_size_v<std::remove_cvref_t<ty>>>{});


// Existing metadata class for one table.
template <typename meta_ty, std::size_t t_uSize>
concept persistent_system_data_meta_ty = requires {
      { meta_ty::svTableName } -> std::convertible_to<std::string_view>;
      { meta_ty::arrAttributeNames } -> std::same_as<std::array<std::string_view, t_uSize> const&>;
      { meta_ty::optIdentityIndex } -> std::same_as<std::optional<std::size_t> const&>;
      requires size_t_array_type<decltype(meta_ty::arrKeyIndices)>;
      requires size_t_array_type<decltype(meta_ty::arrReadOnlyIndices)>;
      };

// Projection for multi-table cases.
// sequence_ty is a std::index_sequence<...> describing
// which fields from the complete tuple belong to this table.
template <non_empty_index_sequence_ty sequence_ty,
          persistent_system_data_meta_ty<index_sequence_size_v<sequence_ty>> meta_ty>
struct persistent_meta_projection {
   using meta_type = meta_ty;
   using sequence_type = std::remove_cvref_t<sequence_ty>;

   static constexpr std::size_t sequence_size = index_sequence_size_v<sequence_type>;
   };


// Stricter variant when the source size is known at the projection site.
template <non_empty_index_sequence_ty sequence_ty,
          persistent_system_data_meta_ty<index_sequence_size_v<sequence_ty>> meta_ty,
          std::size_t t_uSourceSize>
   requires index_sequence_fits_size_v<sequence_ty, t_uSourceSize>
struct persistent_sized_meta_projection {
   using meta_type = meta_ty;
   using sequence_type = std::remove_cvref_t<sequence_ty>;

   static constexpr std::size_t source_size = t_uSourceSize;
   static constexpr std::size_t sequence_size = index_sequence_size_v<sequence_type>;
   };


// Traits for direct metadata classes and projections.
// Every template parameter is either a valid metadata class
// or a valid projection.

template <typename entry_ty>
concept persistent_meta_projection_entry_ty = requires {
        typename std::remove_cvref_t<entry_ty>::meta_type;
        typename std::remove_cvref_t<entry_ty>::sequence_type;
        { std::remove_cvref_t<entry_ty>::sequence_size } -> std::convertible_to<std::size_t>;
       } && 
       index_sequence_ty<typename std::remove_cvref_t<entry_ty>::sequence_type> && 
       persistent_system_data_meta_ty<typename std::remove_cvref_t<entry_ty>::meta_type,
               index_sequence_size_v<typename std::remove_cvref_t<entry_ty>::sequence_type>>;


// Validate metadata entries when the source size is known.
// Single table: direct meta_ty must match the complete type list.
// Multi-table: the projection must match the source size.

template <persistent_meta_projection_entry_ty projection_ty>
   struct persistent_meta_projection_traits {
   using projection_clean_ty = std::remove_cvref_t<projection_ty>;
   using meta_type = typename projection_clean_ty::meta_type;
   using sequence_type = typename projection_clean_ty::sequence_type;

   static constexpr bool is_projection = true;
   };

template <typename entry_ty, std::size_t t_uSourceSize>
concept persistent_single_meta_entry_ty =
      persistent_system_data_meta_ty<std::remove_cvref_t<entry_ty>, t_uSourceSize>;


template <typename entry_ty, std::size_t t_uSourceSize>
concept persistent_multi_meta_entry_ty = 
           persistent_meta_projection_entry_ty<std::remove_cvref_t<entry_ty>> && 
           index_sequence_fits_size_v<
             typename persistent_meta_projection_traits<std::remove_cvref_t<entry_ty>>::sequence_type,
             t_uSourceSize>;

template <typename entry_ty, std::size_t t_uSourceSize, bool t_bMultiTable>
concept persistent_meta_entry_for_size_ty = 
            (t_bMultiTable && persistent_multi_meta_entry_ty<entry_ty, t_uSourceSize>) || 
            (!t_bMultiTable && persistent_single_meta_entry_ty<entry_ty, t_uSourceSize>);



template <std::size_t t_uSourceSize, bool t_bMultiTable,
          persistent_meta_entry_for_size_ty<t_uSourceSize, t_bMultiTable> entry_ty>
struct persistent_meta_resolver;


template <std::size_t t_uSourceSize,
          persistent_single_meta_entry_ty<t_uSourceSize> entry_ty>
struct persistent_meta_resolver<t_uSourceSize, false, entry_ty> {
   using meta_type = std::remove_cvref_t<entry_ty>;
   using sequence_type = void;

   static constexpr bool is_projection = false;
   static constexpr std::size_t meta_size = t_uSourceSize;
};

template <std::size_t t_uSourceSize,
          persistent_multi_meta_entry_ty<t_uSourceSize> entry_ty>
struct persistent_meta_resolver<t_uSourceSize, true, entry_ty> {
   using projection_traits_ty =
                  persistent_meta_projection_traits<std::remove_cvref_t<entry_ty>>;

   using meta_type     = typename projection_traits_ty::meta_type;
   using sequence_type = typename projection_traits_ty::sequence_type;

   static constexpr bool is_projection    = true;
   static constexpr std::size_t meta_size = index_sequence_size_v<sequence_type>;
};


template <class types_ty, class... meta_entry_ty>
concept persistent_system_data_meta_pack_ty = adecc::defined_type_list_ty<types_ty> && 
            (sizeof...(meta_entry_ty) >= 1) && 
            ( ( (sizeof...(meta_entry_ty) == 1) && 
                 (persistent_single_meta_entry_ty<meta_entry_ty,
                                   std::tuple_size_v<typename types_ty::type_list>> && ...) ) ||
              ( (sizeof...(meta_entry_ty) > 1) && 
                 (persistent_multi_meta_entry_ty<meta_entry_ty,
                                   std::tuple_size_v<typename types_ty::type_list>> && ...) )
            );


} // namespace adecc::db

