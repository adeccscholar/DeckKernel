// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file system_data_persistent_builder.h
\brief Compile-time SQL and parameter builder for PersistentSystemData metadata.

\details
Derives SELECT, INSERT, UPDATE, DELETE, key predicates, and parameter descriptions from validated persistence
metadata. SQL stays visible and deterministic while repetitive structural information is generated from the
typed metadata source of truth.

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

#include "system_data_persistent_basic.h"
#include "database.h"

#include <string>
#include <string_view>
#include <ranges>
#include <array>
#include <optional>
#include <type_traits>
#include <utility>

namespace adecc::db {

   // SQL and parameter generation for exactly one concrete metadata class.
     // PersistentSystemData delegates SQL construction to this builder.
   template <typename meta_ty, std::size_t t_uSize>
      requires persistent_system_data_meta_ty<meta_ty, t_uSize>
   struct PersistentQueryBuilder {
      static constexpr std::size_t size = t_uSize;
      static constexpr std::size_t key_count = std::tuple_size_v<decltype(meta_ty::arrKeyIndices)>;

      static constexpr std::string_view TableName() noexcept {
         return meta_ty::svTableName;
      }

      static constexpr std::array<std::string_view, size> const& AttributeNames() noexcept {
         return meta_ty::arrAttributeNames;
      }

      template <std::size_t t_uIndex>
      static constexpr std::string_view AttributeName() noexcept {
         static_assert(t_uIndex < size);
         return meta_ty::arrAttributeNames[t_uIndex];
      }

      static constexpr decltype(meta_ty::arrKeyIndices) const& KeyIndices() noexcept {
         return meta_ty::arrKeyIndices;
      }

      static constexpr std::optional<std::size_t> IdentityIndex() noexcept {
         return meta_ty::optIdentityIndex;
      }

      static constexpr auto const& ReadOnlyIndices() noexcept {
         return meta_ty::arrReadOnlyIndices;
      }

      static consteval bool HasValidKeyIndices() {
         for (std::size_t uIndex : meta_ty::arrKeyIndices) {
            if (uIndex >= size) {
               return false;
            }
         }
         return true;
      }

      static consteval bool HasValidIdentityIndex() {
         if (meta_ty::optIdentityIndex.has_value() && *meta_ty::optIdentityIndex >= size) {
            return false;
         }
         return true;
      }

      static consteval bool HasValidReadOnlyIndices() {
         for (std::size_t uIndex : meta_ty::arrReadOnlyIndices) {
            if (uIndex >= size) {
               return false;
            }
         }
         return true;
      }

      static consteval bool HasNonEmptyNames() {
         if (meta_ty::svTableName.empty()) {
            return false;
         }

         for (std::string_view const svName : meta_ty::arrAttributeNames) {
            if (svName.empty()) {
               return false;
            }
         }
         return true;
      }

      static_assert(HasValidKeyIndices(), "arrKeyIndices contains invalid tuple indices");
      static_assert(HasValidIdentityIndex(), "optIdentityIndex contains an invalid tuple index");
      static_assert(HasValidReadOnlyIndices(), "arrReadOnlyIndices contains invalid tuple indices");
      static_assert(HasNonEmptyNames(), "table name and attribute names must not be empty");

      static constexpr bool IsKeyIndex(std::size_t const uIndex) noexcept {
         for (std::size_t const uKeyIndex : meta_ty::arrKeyIndices) {
            if (uKeyIndex == uIndex) {
               return true;
            }
         }
         return false;
      }

      template <std::size_t t_uIndex>
      static consteval bool IsKeyIndex() noexcept {
         static_assert(t_uIndex < size);

         for (std::size_t const uKeyIndex : meta_ty::arrKeyIndices) {
            if (uKeyIndex == t_uIndex) {
               return true;
            }
         }
         return false;
      }

      static constexpr bool IsIdentityIndex(std::size_t const uIndex) noexcept {
         auto const optIndex = IdentityIndex();
         return optIndex.has_value() && *optIndex == uIndex;
      }

      static constexpr bool IsReadOnlyIndex(std::size_t const uIndex) noexcept {
         for (std::size_t const uReadOnlyIndex : ReadOnlyIndices()) {
            if (uReadOnlyIndex == uIndex) {
               return true;
            }
         }
         return false;
      }

      static constexpr bool IsInsertValueIndex(std::size_t const uIndex) noexcept {
         return !IsIdentityIndex(uIndex) && !IsReadOnlyIndex(uIndex);
      }

      static constexpr bool IsUpdateSetIndex(std::size_t const uIndex) noexcept {
         return !IsKeyIndex(uIndex) && !IsIdentityIndex(uIndex) && !IsReadOnlyIndex(uIndex);
      }

      template <std::ranges::input_range range_ty>
      static std::string JoinStrings(range_ty&& theRange, std::string_view const svSeparator) {
         std::string strResult;
         bool bFirst = true;
         for (auto&& thePart : theRange) {
            if (!bFirst) {
               strResult += svSeparator;
            }
            strResult += thePart;
            bFirst = false;
         }
         return strResult;
      }

      static constexpr auto AttributeIndexRange() noexcept {
         return std::views::iota(std::size_t{ 0 }, size);
      }

      static constexpr auto InsertValueIndexRange() noexcept {
         return AttributeIndexRange() |
            std::views::filter([](std::size_t const uIndex) {
            return IsInsertValueIndex(uIndex);
               });
      }

      static constexpr auto UpdateSetIndexRange() noexcept {
         return AttributeIndexRange() |
            std::views::filter([](std::size_t const uIndex) {
            return IsUpdateSetIndex(uIndex);
               });
      }

      static std::string CreateSqlParameterName(std::size_t const uIndex) {
         std::string strResult;
         strResult.reserve(meta_ty::arrAttributeNames[uIndex].size() + 1);
         strResult += ":";
         strResult += meta_ty::arrAttributeNames[uIndex];
         return strResult;
      }

      static std::string CreateSqlKeyParameterName(std::size_t const uIndex) {
         std::string strResult;
         strResult.reserve(meta_ty::arrAttributeNames[uIndex].size() + 4);
         strResult += ":key";
         strResult += meta_ty::arrAttributeNames[uIndex];
         return strResult;
      }

      static std::string CreateParameterName(std::size_t const uIndex) {
         return std::string{ meta_ty::arrAttributeNames[uIndex] };
      }

      static std::string CreateKeyParameterName(std::size_t const uIndex) {
         std::string strResult;
         strResult.reserve(meta_ty::arrAttributeNames[uIndex].size() + 3);
         strResult += "key";
         strResult += meta_ty::arrAttributeNames[uIndex];
         return strResult;
      }

      static constexpr auto AttributeNameRange() noexcept {
         return meta_ty::arrAttributeNames |
            std::views::transform([](std::string_view const svName) -> std::string_view {
            return svName;
               });
      }

      template <std::ranges::input_range range_ty>
      static constexpr auto AssignmentRange(range_ty&& theIndexRange) noexcept {
         return std::forward<range_ty>(theIndexRange) |
            std::views::transform([](std::size_t const uIndex) {
            std::string strResult;
            strResult.reserve(meta_ty::arrAttributeNames[uIndex].size() * 2 + 4);
            strResult += meta_ty::arrAttributeNames[uIndex];
            strResult += " = ";
            strResult += CreateSqlParameterName(uIndex);
            return strResult;
               });
      }

      template <std::ranges::input_range range_ty>
      static constexpr auto KeyConditionRange(range_ty&& theIndexRange) noexcept {
         return std::forward<range_ty>(theIndexRange) |
            std::views::transform([](std::size_t const uIndex) {
            std::string strResult;
            strResult.reserve(meta_ty::arrAttributeNames[uIndex].size() * 2 + 8);
            strResult += meta_ty::arrAttributeNames[uIndex];
            strResult += " = ";
            strResult += CreateSqlKeyParameterName(uIndex);
            return strResult;
               });
      }

      template <std::size_t t_uIndex>
      static std::string ParameterName() {
         static_assert(t_uIndex < size);
         return CreateParameterName(t_uIndex);
      }

      template <std::size_t t_uIndex>
      static std::string KeyParameterName() {
         static_assert(t_uIndex < size);
         static_assert(IsKeyIndex<t_uIndex>());
         return CreateKeyParameterName(t_uIndex);
      }

      static std::string ParameterName(std::size_t const uIndex) {
         return CreateParameterName(uIndex);
      }

      static std::string KeyParameterName(std::size_t const uIndex) {
         return CreateKeyParameterName(uIndex);
      }

      static std::string CreateSelectFromSql() {
         std::string strResult = "SELECT ";
         strResult += JoinStrings(AttributeNameRange(), ", ");
         strResult += " FROM ";
         strResult += TableName();
         return strResult;
      }

      template <std::size_t... Indices>
         requires (sizeof...(Indices) > 0) && ((Indices < size) && ...)
      static std::string CreateSelectFromSql() {
         std::array<std::string_view, sizeof...(Indices)> const arrNames{
                            meta_ty::arrAttributeNames[Indices]... };

         std::string strResult = "SELECT ";
         strResult += JoinStrings(arrNames, ", ");
         strResult += " FROM ";
         strResult += TableName();
         return strResult;
      }

      static std::string CreateWhereKeySql() {
         if constexpr (key_count == 0) {
            return {};
         }
         else {
            std::string strResult = " WHERE ";
            strResult += JoinStrings(KeyConditionRange(meta_ty::arrKeyIndices), " AND ");
            return strResult;
         }
      }

      template <std::size_t... Indices> requires (sizeof...(Indices) > 0) &&
         ((Indices < size) && ...) &&
         ((IsKeyIndex<Indices>()) && ...)
         static std::string CreateWhereKeySql() {
         std::array<std::string, sizeof...(Indices)> const arrConditions{
            (std::string { meta_ty::arrAttributeNames[Indices] } + " = " +
                           CreateSqlKeyParameterName(Indices))...
         };

         std::string strResult = " WHERE ";
         strResult += JoinStrings(arrConditions, " AND ");
         return strResult;
      }

      template <std::size_t... Indices> requires (sizeof...(Indices) > 0) &&
         ((Indices < size) && ...)
         static std::string CreateWhereSql() {
         std::array<std::string, sizeof...(Indices)> const arrConditions{
            (std::string { meta_ty::arrAttributeNames[Indices] } + " = " +
                           CreateSqlKeyParameterName(Indices))...
         };

         std::string strResult = " WHERE ";
         strResult += JoinStrings(arrConditions, " AND ");
         return strResult;
      }

      static std::string CreateInsertSql() {
         std::string strResult = "INSERT INTO ";
         strResult += TableName();
         strResult += " (";
         strResult += JoinStrings(InsertValueIndexRange()
            | std::views::transform([](std::size_t const uIndex) {
               return std::string{ meta_ty::arrAttributeNames[uIndex] };
               }), ", ");
         strResult += ") VALUES (";
         strResult += JoinStrings(InsertValueIndexRange()
            | std::views::transform([](std::size_t const uIndex) {
               return CreateSqlParameterName(uIndex);
               }), ", ");
         strResult += ")";
         return strResult;
      }

      template <std::size_t... Indices> requires (sizeof...(Indices) > 0) &&
         ((Indices < size) && ...)
         static std::string CreateInsertSql() {
         std::array<std::string_view, sizeof...(Indices)> const arrNames{
                    meta_ty::arrAttributeNames[Indices]...
         };

         std::array<std::string, sizeof...(Indices)> const arrParams{
                    CreateSqlParameterName(Indices)...
         };

         std::string strResult = "INSERT INTO ";
         strResult += TableName();
         strResult += " (";
         strResult += JoinStrings(arrNames, ", ");
         strResult += ") VALUES (";
         strResult += JoinStrings(arrParams, ", ");
         strResult += ")";
         return strResult;
      }

      static std::string CreateUpdateSql() {
         std::string strAssignments = JoinStrings(AssignmentRange(UpdateSetIndexRange()), ", ");
         std::string strResult = "UPDATE ";
         strResult += TableName();
         if (!strAssignments.empty()) {
            strResult += " SET ";
            strResult += strAssignments;
         }
         strResult += CreateWhereKeySql();
         return strResult;
      }

      template <std::size_t... Indices> requires (sizeof...(Indices) > 0) &&
         ((Indices < size) && ...)
         static std::string CreateUpdateSql() {
         std::array<std::string, sizeof...(Indices)> const arrAssignments{
            (std::string { meta_ty::arrAttributeNames[Indices] } + " = " +
                           CreateSqlParameterName(Indices))...
         };

         std::string strResult = "UPDATE ";
         strResult += TableName();
         strResult += " SET ";
         strResult += JoinStrings(arrAssignments, ", ");
         strResult += CreateWhereKeySql();
         return strResult;
      }

      static std::string CreateDeleteSql() {
         std::string strResult = "DELETE FROM ";
         strResult += TableName();
         strResult += CreateWhereKeySql();
         return strResult;
      }

      template <std::size_t... Indices> requires (sizeof...(Indices) > 0) &&
         ((Indices < size) && ...)
         static std::string CreateDeleteSql() {
         std::string strResult = "DELETE FROM ";
         strResult += TableName();
         strResult += CreateWhereSql<Indices...>();
         return strResult;
      }

      static db_output_param_role CreateInsertOutputRole(std::size_t const uIndex) noexcept {
         if (IsIdentityIndex(uIndex)) {
            return db_output_param_role::identity;
         }
         if (IsReadOnlyIndex(uIndex)) {
            return db_output_param_role::may_be_missing;
         }
         return db_output_param_role::needed_value;
      }

      static db_output_param_role CreateUpdateOutputRole(std::size_t const uIndex) noexcept {
         if (IsKeyIndex(uIndex)) {
            return db_output_param_role::key;
         }
         if (IsIdentityIndex(uIndex) || IsReadOnlyIndex(uIndex)) {
            return db_output_param_role::may_be_missing;
         }
         return db_output_param_role::needed_value;
      }

      static db_output_parameters CreateInsertOutputParameters() {
         db_output_parameters vecParameters;
         vecParameters.reserve(size);

         for (std::size_t uIndex{}; uIndex < size; ++uIndex) {
            vecParameters.emplace_back(CreateParameterName(uIndex),
               CreateInsertOutputRole(uIndex));
         }
         return vecParameters;
      }

      static db_output_parameters CreateUpdateOutputParameters() {
         db_output_parameters vecParameters;
         vecParameters.reserve(size);

         for (std::size_t uIndex{}; uIndex < size; ++uIndex) {
            if (IsKeyIndex(uIndex)) {
               vecParameters.emplace_back(CreateKeyParameterName(uIndex),
                  db_output_param_role::key);
            }
            else {
               vecParameters.emplace_back(CreateParameterName(uIndex),
                  CreateUpdateOutputRole(uIndex));
            }
         }
         return vecParameters;
      }

   private:
      template <typename value_ty>
      static adecc::db_param ToDbParamValue(value_ty const& theValue) {
         using clean_ty = std::remove_cvref_t<value_ty>;

         if constexpr (adecc::is_optional_v<clean_ty>) {
            using inner_ty = adecc::optional_value_type_t<clean_ty>;

            static_assert(adecc::db_param_atom<inner_ty>,
               "optional value type is not a valid db_param atom");
         }
         else {
            static_assert(adecc::db_param_atom<clean_ty>,
               "value type is not a valid db_param atom");
         }

         return adecc::db_param{ theValue };
      }

      template <bool t_bUseKeyPrefix, std::size_t t_uIndex>
      static std::string CreateParamName() {
         static_assert(t_uIndex < size);

         if constexpr (t_bUseKeyPrefix) {
            return KeyParameterName<t_uIndex>();
         }
         else {
            return ParameterName<t_uIndex>();
         }
      }

      template <class tuple_ty, bool t_bUseKeyPrefix, std::size_t... Indices>
         requires (sizeof...(Indices) > 0) && ((Indices < size) && ...) &&
      (std::tuple_size_v<std::remove_cvref_t<tuple_ty>> == size)
         static adecc::db_params CreateParamsImpl(tuple_ty const& tplData) {
         adecc::db_params vecParams;
         vecParams.reserve(sizeof...(Indices));

         (vecParams.emplace_back(CreateParamName<t_bUseKeyPrefix, Indices>(),
            ToDbParamValue(std::get<Indices>(tplData)), true), ...);
         return vecParams;
      }

      template <bool t_bUseKeyPrefix, std::size_t t_uIndex>
      static std::string CreateWhereParamName() {
         static_assert(t_uIndex < size);

         if constexpr (t_bUseKeyPrefix) {
            return CreateKeyParameterName(t_uIndex);
         }
         else {
            return CreateParameterName(t_uIndex);
         }
      }

      template <class tuple_ty, bool t_bUseKeyPrefix, std::size_t... Indices>
         requires (sizeof...(Indices) > 0) && ((Indices < size) && ...) &&
      (std::tuple_size_v<std::remove_cvref_t<tuple_ty>> == size)
         static adecc::db_params CreateWhereParamsImpl(tuple_ty const& tplData) {
         adecc::db_params vecParams;
         vecParams.reserve(sizeof...(Indices));

         (vecParams.emplace_back(CreateWhereParamName<t_bUseKeyPrefix, Indices>(),
            ToDbParamValue(std::get<Indices>(tplData)), true), ...);
         return vecParams;
      }

      template <bool t_bUseKeyPrefix, std::size_t t_uIndex, class tuple_ty>
      static void AddAllParamIfNeeded_(adecc::db_params& vecParams, tuple_ty const& tplData) {
         static_assert(t_uIndex < size);

         if constexpr (t_bUseKeyPrefix || IsUpdateSetIndex(t_uIndex)) {
            vecParams.emplace_back(CreateParamName<t_bUseKeyPrefix, t_uIndex>(),
               ToDbParamValue(std::get<t_uIndex>(tplData)), true);
         }
      }

      template <class tuple_ty, bool t_bUseKeyPrefix, std::size_t... Indices>
         requires (std::tuple_size_v<std::remove_cvref_t<tuple_ty>> == size)
      static adecc::db_params CreateAllParamsImpl(tuple_ty const& tplData, std::index_sequence<Indices...>) {
         adecc::db_params vecParams;
         vecParams.reserve(sizeof...(Indices));

         (AddAllParamIfNeeded_<t_bUseKeyPrefix, Indices>(vecParams, tplData), ...);

         return vecParams;
      }

      template <class tuple_ty, bool t_bUseKeyPrefix, std::size_t... KeyPos>
         requires (std::tuple_size_v<std::remove_cvref_t<tuple_ty>> == size)
      static adecc::db_params CreateKeyParamsImpl(tuple_ty const& tplData, std::index_sequence<KeyPos...>) {
         adecc::db_params vecParams;
         vecParams.reserve(sizeof...(KeyPos));

         (vecParams.emplace_back(CreateParamName<t_bUseKeyPrefix, meta_ty::arrKeyIndices[KeyPos]>(),
            ToDbParamValue(std::get<meta_ty::arrKeyIndices[KeyPos]>(tplData)),
            true), ...);

         return vecParams;
      }

   public:
      template <bool t_bUseKeyPrefix = false, class tuple_ty>
         requires (std::tuple_size_v<std::remove_cvref_t<tuple_ty>> == size)
      static adecc::db_params CreateAllParams(tuple_ty const& tplData) {
         return CreateAllParamsImpl<tuple_ty, t_bUseKeyPrefix>(tplData,
            std::make_index_sequence<size> {});
      }

      template <bool t_bUseKeyPrefix = true, class tuple_ty>
         requires (std::tuple_size_v<std::remove_cvref_t<tuple_ty>> == size)
      static adecc::db_params CreateKeyParams(tuple_ty const& tplData) {
         return CreateKeyParamsImpl<tuple_ty, t_bUseKeyPrefix>(tplData,
            std::make_index_sequence<key_count> {});
      }

      template <bool t_bUseKeyPrefix = true, class tuple_ty, std::size_t... Indices>
         requires (sizeof...(Indices) > 0) && ((Indices < size) && ...) &&
      (std::tuple_size_v<std::remove_cvref_t<tuple_ty>> == size)
         static adecc::db_params CreateWhereParams(tuple_ty const& tplData) {
         return CreateWhereParamsImpl<tuple_ty, t_bUseKeyPrefix, Indices...>(tplData);
      }

      template <bool t_bUseKeyPrefix = false, class tuple_ty, std::size_t... Indices>
         requires (sizeof...(Indices) > 0) && ((Indices < size) && ...) &&
      (std::tuple_size_v<std::remove_cvref_t<tuple_ty>> == size)
         static adecc::db_params CreateParams(tuple_ty const& tplData) {
         return CreateParamsImpl<tuple_ty, t_bUseKeyPrefix, Indices...>(tplData);
      }
   };

}  // namespace adecc::db

