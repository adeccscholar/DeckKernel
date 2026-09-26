// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file system_data_persistent.h
\brief PersistentSystemData extension combining SystemData values with table metadata and SQL generation.

\details
Augments SystemData with one or more persistence metadata descriptions, supporting keys, identities,
read-only attributes, projections, and multi-table mappings. Persistence remains derived from the typed
structure while SQL stays explicit and inspectable.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "SystemData as an Application of the Type List".
- "PersistentSystemData as an Extension of SystemData".
- "Database as Source, Transformation, and Sink".
- "Tests as Architectural Proof".

\see ARCHITECTURE.md#persistentsystemdata

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

#include "system_data.h"
#include "details/system_data_persistent_basic.h"
#include "details/system_data_persistent_builder.h"
#include "database.h"

#include <array>
#include <concepts>
#include <functional>
#include <optional>
#include <ranges>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>

namespace adecc::db {

   // PersistentSystemData with one or more metadata classes.
   template <adecc::defined_type_list_ty types_ty, typename... meta_entry_ty>
      requires persistent_system_data_meta_pack_ty<types_ty, meta_entry_ty...>
   class PersistentSystemData : public SystemData<types_ty> {
   public:
      using base_ty = SystemData<types_ty>;
      using types_list = typename base_ty::types_list;
      using data_ty = typename base_ty::data_ty;

      using meta_data_tuple_ty = std::tuple<meta_entry_ty...>;
      using persistent_base_ty = PersistentSystemData<types_ty, meta_entry_ty...>;

      static constexpr std::size_t size = std::tuple_size_v<data_ty>;
      static constexpr std::size_t meta_count = sizeof...(meta_entry_ty);
      static constexpr bool is_multi_table = meta_count > 1;

      template <std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count)
      using meta_entry_at_ty = std::tuple_element_t<t_uMetaIndex, meta_data_tuple_ty>;

      template <std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count)
      using meta_data_ty = typename persistent_meta_resolver<size, is_multi_table,
                                                   meta_entry_at_ty<t_uMetaIndex>>::meta_type;

      template <std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count)
      using meta_sequence_ty = typename persistent_meta_resolver<size, is_multi_table,
                                                   meta_entry_at_ty<t_uMetaIndex>>::sequence_type;

   private:
      template <std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count)
      static consteval std::size_t MetaSizeImpl() {
         if constexpr (is_multi_table) {
            return persistent_meta_resolver<size, is_multi_table,
                                            meta_entry_at_ty<t_uMetaIndex>>::meta_size;
            }
         else {
            return size;
            }
         }

   public:
      template <std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count)
      static constexpr std::size_t meta_size = MetaSizeImpl<t_uMetaIndex>();

      template <std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count)
      using query_builder_ty = PersistentQueryBuilder<meta_data_ty<t_uMetaIndex>, meta_size<t_uMetaIndex>>;

      static constexpr std::size_t key_count = query_builder_ty<0>::key_count;

      template <std::size_t Index>
      using element_ty = std::tuple_element_t<Index, data_ty>;

      using base_ty::base_ty;
      using base_ty::operator =;
      using base_ty::operator <=>;
      using base_ty::operator ==;

      PersistentSystemData() = default;
      PersistentSystemData(PersistentSystemData const&) = default;
      PersistentSystemData(PersistentSystemData&&) noexcept = default;

      PersistentSystemData(base_ty const& theBase) : base_ty(theBase) { }
      PersistentSystemData(base_ty&& theBase) noexcept : base_ty(std::move(theBase)) { }

      PersistentSystemData& operator = (PersistentSystemData const&) = default;
      PersistentSystemData& operator = (PersistentSystemData&&) noexcept = default;

      PersistentSystemData& operator = (base_ty const& theBase) {
         static_cast<base_ty&>(*this) = theBase;
         return *this;
      }

      PersistentSystemData& operator = (base_ty&& theBase) noexcept {
         static_cast<base_ty&>(*this) = std::move(theBase);
         return *this;
         }

      auto operator <=> (PersistentSystemData const&) const = default;
      bool operator == (PersistentSystemData const&) const = default;

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr std::string_view TableName() noexcept {
         return query_builder_ty<t_uMetaIndex>::TableName();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr auto const& AttributeNames() noexcept {
         return query_builder_ty<t_uMetaIndex>::AttributeNames();
         }

      template <std::size_t t_uMetaIndex = 0, std::size_t t_uAttributeIndex>
         requires (t_uMetaIndex < meta_count) && (t_uAttributeIndex < meta_size<t_uMetaIndex>)
      static constexpr std::string_view AttributeName() noexcept {
         return query_builder_ty<t_uMetaIndex>::template AttributeName<t_uAttributeIndex>();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr auto const& KeyIndices() noexcept {
         return query_builder_ty<t_uMetaIndex>::KeyIndices();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr std::optional<std::size_t> IdentityIndex() noexcept {
         return query_builder_ty<t_uMetaIndex>::IdentityIndex();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr auto const& ReadOnlyIndices() noexcept {
         return query_builder_ty<t_uMetaIndex>::ReadOnlyIndices();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr bool IsKeyIndex(std::size_t const uIndex) noexcept {
         return query_builder_ty<t_uMetaIndex>::IsKeyIndex(uIndex);
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr bool IsIdentityIndex(std::size_t const uIndex) noexcept {
         return query_builder_ty<t_uMetaIndex>::IsIdentityIndex(uIndex);
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr bool IsReadOnlyIndex(std::size_t const uIndex) noexcept {
         return query_builder_ty<t_uMetaIndex>::IsReadOnlyIndex(uIndex);
         }

      template <std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count)
      using selected_data_ty = std::conditional_t<is_multi_table,
            decltype(adecc::selectTplValues<meta_sequence_ty<t_uMetaIndex>>(std::declval<data_ty const&>())),
                                                  data_ty const&>;

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static constexpr selected_data_ty<t_uMetaIndex> SelectData(data_ty const& tplData) {
         if constexpr (is_multi_table) {
            return adecc::selectTplValues<meta_sequence_ty<t_uMetaIndex>>(tplData);
            }
         else {
            return tplData;
            }
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      [[nodiscard]] constexpr auto SelectData() const {
         return SelectData<t_uMetaIndex>(this->data);
         }

      template <std::size_t t_uMetaIndex, class source_tuple_ty>
         requires (t_uMetaIndex < meta_count)
      using mapped_data_ty = std::conditional_t<is_multi_table, data_ty,
                                                source_tuple_ty const&>;

      template <std::size_t t_uMetaIndex = 0, class source_tuple_ty>
         requires (t_uMetaIndex < meta_count) && 
                  adecc::is_tuple_like_v<std::remove_cvref_t<source_tuple_ty>> && 
                  (std::tuple_size_v<std::remove_cvref_t<source_tuple_ty>> == meta_size<t_uMetaIndex>)
      [[nodiscard]] constexpr mapped_data_ty<t_uMetaIndex, source_tuple_ty> MapMetaTuple(
                                                          source_tuple_ty const& tplSource) const {
         if constexpr (is_multi_table) {
            data_ty tplTarget{ this->data };
            [&] <std::size_t... TargetIs>(std::index_sequence<TargetIs...>) {
               [&] <std::size_t... SourceIs>(std::index_sequence<SourceIs...>) {
                  ((std::get<TargetIs>(tplTarget) = std::get<SourceIs>(tplSource)), ...);
                  }(std::make_index_sequence<sizeof...(TargetIs)> {});
               }(meta_sequence_ty<t_uMetaIndex> {});

            return tplTarget;
            }
         else {
            return tplSource;
            }
         }

      template <std::size_t t_uMetaIndex = 0, class source_tuple_ty>
         requires (t_uMetaIndex < meta_count) && 
                   adecc::is_tuple_like_v<std::remove_cvref_t<source_tuple_ty>> && 
                   (std::tuple_size_v<std::remove_cvref_t<source_tuple_ty>> == meta_size<t_uMetaIndex>)
      [[nodiscard]] static constexpr mapped_data_ty<t_uMetaIndex, source_tuple_ty> MapSelectTuple(
                                                                  source_tuple_ty const& tplSource) {
         if constexpr (is_multi_table) {
            data_ty tplTarget{};

            [&] <std::size_t... TargetIs>(std::index_sequence<TargetIs...>) {
               [&] <std::size_t... SourceIs>(std::index_sequence<SourceIs...>) {
                  ((std::get<TargetIs>(tplTarget) = std::get<SourceIs>(tplSource)), ...);
                  }(std::make_index_sequence<sizeof...(TargetIs)> {});
               }(meta_sequence_ty<t_uMetaIndex> {});
            return tplTarget;
            }
         else {
            return tplSource;
            }
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static std::string CreateSelectFromSql() {
         return query_builder_ty<t_uMetaIndex>::CreateSelectFromSql();
         }

      template <std::size_t t_uMetaIndex = 0, std::size_t... Indices>
         requires (t_uMetaIndex < meta_count) && (sizeof...(Indices) > 0) && 
                  ((Indices < meta_size<t_uMetaIndex>) && ...)
      static std::string CreateSelectFromSql() {
         return query_builder_ty<t_uMetaIndex>::template CreateSelectFromSql<Indices...>();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static std::string CreateInsertSql() {
         return query_builder_ty<t_uMetaIndex>::CreateInsertSql();
         }

      template <std::size_t t_uMetaIndex = 0, std::size_t... Indices>
         requires (t_uMetaIndex < meta_count) && (sizeof...(Indices) > 0) && 
                  ((Indices < meta_size<t_uMetaIndex>) && ...)
      static std::string CreateInsertSql() {
         return query_builder_ty<t_uMetaIndex>::template CreateInsertSql<Indices...>();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static std::string CreateUpdateSql() {
         return query_builder_ty<t_uMetaIndex>::CreateUpdateSql();
         }

      template <std::size_t t_uMetaIndex = 0, std::size_t... Indices>
         requires (t_uMetaIndex < meta_count) && (sizeof...(Indices) > 0) && 
                  ((Indices < meta_size<t_uMetaIndex>) && ...)
      static std::string CreateUpdateSql() {
         return query_builder_ty<t_uMetaIndex>::template CreateUpdateSql<Indices...>();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static std::string CreateDeleteSql() {
         return query_builder_ty<t_uMetaIndex>::CreateDeleteSql();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static db_output_parameters CreateInsertOutputParameters() {
         return query_builder_ty<t_uMetaIndex>::CreateInsertOutputParameters();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static db_output_parameters CreateUpdateOutputParameters() {
         return query_builder_ty<t_uMetaIndex>::CreateUpdateOutputParameters();
         }

      template <std::size_t t_uMetaIndex = 0>
         requires (t_uMetaIndex < meta_count)
      static db_output_parameters CreateDeleteOutputParameters() {
         return query_builder_ty<t_uMetaIndex>::CreateDeleteOutputParameters();
         }

   private:
      template <std::size_t... Is>
      static std::array<std::string, sizeof...(Is)> CreateSelectFromSqlAllImpl(std::index_sequence<Is...>) {
         return { CreateSelectFromSql<Is>()... };
         }

      template <std::size_t... Is>
      static std::array<std::string, sizeof...(Is)> CreateInsertSqlAllImpl(std::index_sequence<Is...>) {
         return { CreateInsertSql<Is>()... };
         }

      template <std::size_t... Is>
      static std::array<std::string, sizeof...(Is)> CreateUpdateSqlAllImpl(std::index_sequence<Is...>) {
         return { CreateUpdateSql<Is>()... };
         }

      template <std::size_t... Is>
      static std::array<std::string, sizeof...(Is)> CreateDeleteSqlAllImpl(std::index_sequence<Is...>) {
         return { CreateDeleteSql<Is>()... };
         }

      template <std::size_t... Is>
      static std::array<db_output_parameters, sizeof...(Is)> CreateInsertOutputParametersAllImpl(std::index_sequence<Is...>) {
         return { CreateInsertOutputParameters<Is>()... };
            }

      template <std::size_t... Is>
      static std::array<db_output_parameters, sizeof...(Is)> CreateUpdateOutputParametersAllImpl(std::index_sequence<Is...>) {
         return { CreateUpdateOutputParameters<Is>()... };
         }

      template <std::size_t... Is>
      static std::array<db_output_parameters, sizeof...(Is)> CreateDeleteOutputParametersAllImpl(std::index_sequence<Is...>) {
         return { CreateDeleteOutputParameters<Is>()... };
         }

   public:
      static std::array<std::string, meta_count> CreateSelectFromSqlAll() {
         return CreateSelectFromSqlAllImpl(std::make_index_sequence<meta_count> {});
         }

      static std::array<std::string, meta_count> CreateInsertSqlAll() {
         return CreateInsertSqlAllImpl(std::make_index_sequence<meta_count> {});
         }

      static std::array<std::string, meta_count> CreateUpdateSqlAll() {
         return CreateUpdateSqlAllImpl(std::make_index_sequence<meta_count> {});
         }

      static std::array<std::string, meta_count> CreateDeleteSqlAll() {
         return CreateDeleteSqlAllImpl(std::make_index_sequence<meta_count> {});
         }

      static std::array<db_output_parameters, meta_count> CreateInsertOutputParametersAll() {
         return CreateInsertOutputParametersAllImpl(std::make_index_sequence<meta_count> {});
         }

      static std::array<db_output_parameters, meta_count> CreateUpdateOutputParametersAll() {
         return CreateUpdateOutputParametersAllImpl(std::make_index_sequence<meta_count> {});
         }

      static std::array<db_output_parameters, meta_count> CreateDeleteOutputParametersAll() {
         return CreateDeleteOutputParametersAllImpl(std::make_index_sequence<meta_count> {});
         }

      template <std::size_t t_uMetaIndex = 0, bool t_bUseKeyPrefix = false>
         requires (t_uMetaIndex < meta_count)
      adecc::db_params CreateAllParams() const {
         auto tplSelected = SelectData<t_uMetaIndex>();
         return query_builder_ty<t_uMetaIndex>::template CreateAllParams<t_bUseKeyPrefix>(tplSelected);
         }

      template <std::size_t t_uMetaIndex = 0, bool t_bUseKeyPrefix = true>
         requires (t_uMetaIndex < meta_count)
      adecc::db_params CreateKeyParams() const {
         auto tplSelected = SelectData<t_uMetaIndex>();
         return query_builder_ty<t_uMetaIndex>::template CreateKeyParams<t_bUseKeyPrefix>(tplSelected);
         }

      template <std::size_t t_uMetaIndex = 0, bool t_bUseKeyPrefix = false, std::size_t... Indices>
         requires (t_uMetaIndex < meta_count) && (sizeof...(Indices) > 0) && 
                  ((Indices < meta_size<t_uMetaIndex>) && ...)
      adecc::db_params CreateParams() const {
         auto tplSelected = SelectData<t_uMetaIndex>();
         return query_builder_ty<t_uMetaIndex>::template CreateParams<t_bUseKeyPrefix, 
                                                         decltype(tplSelected), Indices...>(tplSelected);
         }

      auto KeyTuple() const {
         return KeyTupleImpl(std::make_index_sequence<key_count> {});
         }

      constexpr auto CompareKey(PersistentSystemData const& rhs) const {
         return CompareKeyImpl(rhs, std::make_index_sequence<key_count> {});
         }

      constexpr bool EqualKey(PersistentSystemData const& rhs) const {
         return CompareKey(rhs) == 0;
         }

   private:
      template <std::size_t... KeyPos>
      constexpr auto CompareKeyImpl(PersistentSystemData const& rhs, std::index_sequence<KeyPos...>) const {
         return this->template CompareSelected<query_builder_ty<0>::KeyIndices()[KeyPos]...>(this->data, 
                                                       rhs.data);
         }

      template <std::size_t... KeyPos>
      auto KeyTupleImpl(std::index_sequence<KeyPos...>) const {
         return std::tuple<element_ty<query_builder_ty<0>::KeyIndices()[KeyPos]>...> {
            std::get<query_builder_ty<0>::KeyIndices()[KeyPos]>(this->data)...
            };
         }
   };

} // namespace adecc::db


namespace adecc {

   template <typename ty>
   concept system_data_type = requires {
        typename std::remove_cvref_t<ty>::types_list;
        typename std::remove_cvref_t<ty>::data_ty;

        { std::remove_cvref_t<ty>::size } -> std::convertible_to<std::size_t>;
      } &&
      std::derived_from<std::remove_cvref_t<ty>, SystemData<typename std::remove_cvref_t<ty>::types_list>> &&
      ( std::tuple_size_v<typename std::remove_cvref_t<ty>::data_ty> == std::remove_cvref_t<ty>::size );

   namespace db {

      template <typename ty>
      concept persistent_system_data_type = requires {
         typename std::remove_cvref_t<ty>::types_list;
         typename std::remove_cvref_t<ty>::data_ty;
         typename std::remove_cvref_t<ty>::meta_data_tuple_ty;
         typename std::remove_cvref_t<ty>::persistent_base_ty;

         { std::remove_cvref_t<ty>::size } -> std::convertible_to<std::size_t>;
         { std::remove_cvref_t<ty>::meta_count } -> std::convertible_to<std::size_t>;
         { std::remove_cvref_t<ty>::key_count } -> std::convertible_to<std::size_t>;

         { std::remove_cvref_t<ty>::template TableName<0>() } -> std::convertible_to<std::string_view>;
         { std::remove_cvref_t<ty>::template AttributeNames<0>() };
         { std::remove_cvref_t<ty>::template KeyIndices<0>() };
         { std::remove_cvref_t<ty>::template IdentityIndex<0>() } -> std::same_as<std::optional<std::size_t>>;
         { std::remove_cvref_t<ty>::template ReadOnlyIndices<0>() };

         { std::remove_cvref_t<ty>::template CreateInsertSql<0>() } -> std::same_as<std::string>;
         { std::remove_cvref_t<ty>::template CreateUpdateSql<0>() } -> std::same_as<std::string>;
         { std::remove_cvref_t<ty>::template CreateDeleteSql<0>() } -> std::same_as<std::string>;
         { std::remove_cvref_t<ty>::template CreateSelectFromSql<0>() } -> std::same_as<std::string>;

         { std::remove_cvref_t<ty>::template CreateInsertOutputParameters<0>() } -> std::same_as<db_output_parameters>;
         { std::remove_cvref_t<ty>::template CreateUpdateOutputParameters<0>() } -> std::same_as<db_output_parameters>;
         { std::remove_cvref_t<ty>::template CreateDeleteOutputParameters<0>() } -> std::same_as<db_output_parameters>;
        } &&
         std::derived_from<std::remove_cvref_t<ty>, typename std::remove_cvref_t<ty>::persistent_base_ty>;

      template <typename range_ty, typename data_ty>
      concept persistent_system_data_tuple_range = std::ranges::input_range<range_ty> && 
                                                   persistent_system_data_type<data_ty> && 
           ( std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
                          typename std::remove_cvref_t<data_ty>::data_ty> ||
            std::constructible_from<typename std::remove_cvref_t<data_ty>::data_ty,
                                    std::ranges::range_reference_t<range_ty>>
           );

      template <typename transform_fn_ty, typename input_list_ty,
                typename persistent_data_ty, typename... context_ty>
      concept persistent_insert_transform_from = defined_type_list_ty<input_list_ty> && 
                                                 persistent_system_data_type<persistent_data_ty> && 
               requires(transform_fn_ty & Transform, typename input_list_ty::type_list const& tplInput,
                        context_ty const&... context) {
               { std::invoke(Transform, tplInput, context...) } 
                               -> std::same_as<typename persistent_data_ty::data_ty>;
      };

      template <typename persistent_data_ty, typename input_list_ty,
                typename input_range_ty, typename transform_fn_ty,
                typename... context_ty>
      concept persistent_insert_input = persistent_system_data_type<persistent_data_ty> && 
                                        adecc::defined_type_list_ty<input_list_ty> && 
                                        std::ranges::input_range<input_range_ty> && 
                                        defined_type_list_input_range<input_list_ty, input_range_ty> && 
                                        persistent_insert_transform_from<transform_fn_ty, input_list_ty,
                                        persistent_data_ty, context_ty... >;

   } // namespace db

} // namespace adecc
