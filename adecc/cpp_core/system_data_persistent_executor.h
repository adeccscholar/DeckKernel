// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file system_data_persistent_executor.h
\brief Execution layer for selecting, inserting, updating, and deleting PersistentSystemData objects.

\details
Connects generated persistence metadata and SQL with the logical database abstraction. It materializes and
transforms typed tuples, executes persistence operations, and maps returned values such as generated
identities back into persistent C++ objects.

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

#include "system_data_persistent.h"
#include "database.h"

#include <array>
#include <format>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace adecc::db {

   struct PersistentDataExecutor {
      template <persistent_system_data_type persistent_data_ty>
      using data_ty = typename persistent_data_ty::data_ty;

      template <persistent_system_data_type persistent_data_ty>
      static constexpr std::size_t meta_count_v = persistent_data_ty::meta_count;

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      using builder_ty = typename persistent_data_ty::template query_builder_ty<t_uMetaIndex>;

      template <persistent_system_data_type persistent_data_ty,
         std::size_t t_uMetaIndex, bool t_bMultiTable>
      struct meta_tuple_selector;

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex>
      struct meta_tuple_selector<persistent_data_ty, t_uMetaIndex, false> {
         using type = data_ty<persistent_data_ty>;
      };

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex>
      struct meta_tuple_selector<persistent_data_ty, t_uMetaIndex, true> {
         using type = std::remove_cvref_t<decltype (
            adecc::selectTplValues<typename persistent_data_ty::template meta_sequence_ty<t_uMetaIndex>>(
               std::declval<data_ty<persistent_data_ty> const&>()))>;
      };

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      using meta_tuple_ty = typename meta_tuple_selector<persistent_data_ty, t_uMetaIndex,
         persistent_data_ty::is_multi_table>::type;

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      using meta_types_list_ty = defined_type_list_from_tuple_t<meta_tuple_ty<persistent_data_ty,
         t_uMetaIndex>>;

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      [[nodiscard]] static meta_tuple_ty<persistent_data_ty, t_uMetaIndex> SelectTuple(persistent_data_ty const& theData) {
         if constexpr (persistent_data_ty::is_multi_table) {
            return adecc::selectTplValues<typename persistent_data_ty::template meta_sequence_ty<t_uMetaIndex>>(
               theData.data);
         }
         else {
            return theData.data;
         }
      }


      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex, logical_database_type app_db>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      [[nodiscard]] static meta_tuple_ty<persistent_data_ty, t_uMetaIndex> InsertOne(app_db& db,
         persistent_data_ty const& theData) {
         auto tplInput = SelectTuple<persistent_data_ty, t_uMetaIndex>(theData);
         return meta_types_list_ty<persistent_data_ty, t_uMetaIndex>::invoke([&]<class... Ts>() {
            auto aRange = db.template MakeOutputRange<Ts...>(
               builder_ty<persistent_data_ty, t_uMetaIndex>::CreateInsertSql(),
               builder_ty<persistent_data_ty, t_uMetaIndex>::CreateInsertOutputParameters()
            );

            std::array<meta_tuple_ty<persistent_data_ty, t_uMetaIndex>, 1> arrInput{
                      meta_tuple_ty<persistent_data_ty, t_uMetaIndex> { tplInput }
            };

            auto vecResult = aRange(arrInput) | std::ranges::to<std::vector>();

            if (vecResult.size() != 1) {
               throw std::runtime_error{
                         std::format("insert metadata projection {} returned {} rows",
                                        t_uMetaIndex,
                                        vecResult.size())
               };
            }

            return vecResult.front();
         });
      }

      template <logical_database_type app_db>
      static void ExecuteCommand(app_db& db,
         std::string const& strSql,
         adecc::db_params const& vecParams) {
         auto rngResult = db.template Execute<>(strSql, vecParams);

         for ([[maybe_unused]] auto&& tplIgnored : rngResult) {
         }
      }

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex,
         logical_database_type app_db>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      static void UpdateOne(app_db& db, persistent_data_ty const& theData) {
         auto tplInput = SelectTuple<persistent_data_ty, t_uMetaIndex>(theData);

         auto vecParams = builder_ty<persistent_data_ty, t_uMetaIndex>::template CreateAllParams<false>(tplInput);
         auto vecKeyParams = builder_ty<persistent_data_ty, t_uMetaIndex>::template CreateKeyParams<true>(tplInput);

         vecParams.insert(vecParams.end(), std::make_move_iterator(vecKeyParams.begin()),
            std::make_move_iterator(vecKeyParams.end()));

         ExecuteCommand(db, builder_ty<persistent_data_ty, t_uMetaIndex>::CreateUpdateSql(),
            vecParams);
      }

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex,
         logical_database_type app_db>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      static void DeleteOne(app_db& db, persistent_data_ty const& theData) {
         auto tplInput = SelectTuple<persistent_data_ty, t_uMetaIndex>(theData);
         auto vecParams = builder_ty<persistent_data_ty, t_uMetaIndex>::template CreateKeyParams<true>(tplInput);

         ExecuteCommand(db, builder_ty<persistent_data_ty, t_uMetaIndex>::CreateDeleteSql(),
            vecParams);
      }

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex, logical_database_type app_db>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      [[nodiscard]] static meta_tuple_ty<persistent_data_ty, t_uMetaIndex> SelectOne(app_db& db,
         persistent_data_ty const& theKeyData) {
         auto tplInput = SelectTuple<persistent_data_ty, t_uMetaIndex>(theKeyData);
         std::string strSql = builder_ty<persistent_data_ty, t_uMetaIndex>::CreateSelectFromSql();
         strSql += builder_ty<persistent_data_ty, t_uMetaIndex>::CreateWhereKeySql();

         return meta_types_list_ty<persistent_data_ty, t_uMetaIndex>::invoke([&]<class... Ts>() {
            auto vecResult = db.template Execute<Ts...>(strSql,
               builder_ty<persistent_data_ty, t_uMetaIndex>::template CreateKeyParams<true>(tplInput))
               | std::ranges::to<std::vector>();

            if (vecResult.size() != 1) {
               throw std::runtime_error{
                  std::format("select metadata projection {} returned {} rows",
                              t_uMetaIndex,
                              vecResult.size())
               };
            }

            return vecResult.front();
         });
      }


      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex,
         logical_database_type app_db>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      [[nodiscard]] static persistent_data_ty InsertStep(app_db& db, persistent_data_ty const& theData) {
         auto tplResult = InsertOne<persistent_data_ty, t_uMetaIndex>(db, theData);
         return persistent_data_ty{
            theData.template MapMetaTuple<t_uMetaIndex>(tplResult)
         };
      }

      template <persistent_system_data_type persistent_data_ty, std::size_t t_uMetaIndex,
         logical_database_type app_db>
         requires (t_uMetaIndex < meta_count_v<persistent_data_ty>)
      [[nodiscard]] static persistent_data_ty SelectStep(app_db& db, persistent_data_ty const& theData) {
         auto tplResult = SelectOne<persistent_data_ty, t_uMetaIndex>(db, theData);
         return persistent_data_ty{
            theData.template MapMetaTuple<t_uMetaIndex>(tplResult)
         };
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::size_t... Is>
      [[nodiscard]] static persistent_data_ty InsertImpl(app_db& db, persistent_data_ty theData,
         std::index_sequence<Is...>) {
         ((theData = InsertStep<persistent_data_ty, Is>(db, theData)), ...);
         return theData;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::size_t... Is>
      static void UpdateImpl(app_db& db, persistent_data_ty const& theData, std::index_sequence<Is...>) {
         (UpdateOne<persistent_data_ty, Is>(db, theData), ...);
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::size_t... Is>
      static void DeleteImpl(app_db& db, persistent_data_ty const& theData, std::index_sequence<Is...>) {
         (DeleteOne<persistent_data_ty, meta_count_v<persistent_data_ty> -1U - Is>(db, theData), ...);
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db, std::size_t... Is>
      [[nodiscard]] static persistent_data_ty SelectImpl(app_db& db, persistent_data_ty theData,
         std::index_sequence<Is...>) {
         ((theData = SelectStep<persistent_data_ty, Is>(db, theData)), ...);
         return theData;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::size_t... Is>
      [[nodiscard]] static persistent_data_ty SelectRestImpl(app_db& db, persistent_data_ty theData,
         std::index_sequence<Is...>) {
         ((theData = SelectStep<persistent_data_ty, Is + 1U>(db, theData)), ...);
         return theData;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> Insert(app_db& db,
         data_ty<persistent_data_ty> const& tplData) {
         persistent_data_ty aData{ tplData };
         return InsertImpl(db, std::move(aData),
            std::make_index_sequence<meta_count_v<persistent_data_ty>> {}).data;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> Insert(app_db& db,
         data_ty<persistent_data_ty>&& tplData) {
         persistent_data_ty aData{ std::move(tplData) };

         return InsertImpl(db, std::move(aData),
            std::make_index_sequence<meta_count_v<persistent_data_ty>> {}).data;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> Update(app_db& db,
         data_ty<persistent_data_ty> const& tplData) {
         persistent_data_ty aData{ tplData };
         UpdateImpl(db, aData, std::make_index_sequence<meta_count_v<persistent_data_ty>> {});
         return aData.data;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> Update(app_db& db,
         data_ty<persistent_data_ty>&& tplData) {
         persistent_data_ty aData{ std::move(tplData) };
         UpdateImpl(db, aData, std::make_index_sequence<meta_count_v<persistent_data_ty>> {});
         return std::move(aData.data);
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> Delete(app_db& db,
         data_ty<persistent_data_ty> const& tplData) {
         persistent_data_ty aData{ tplData };
         DeleteImpl(db, aData, std::make_index_sequence<meta_count_v<persistent_data_ty>> {});
         return aData.data;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> Delete(app_db& db,
         data_ty<persistent_data_ty>&& tplData) {
         persistent_data_ty aData{ std::move(tplData) };
         DeleteImpl(db, aData, std::make_index_sequence<meta_count_v<persistent_data_ty>> {});
         return std::move(aData.data);
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> Select(app_db& db,
         data_ty<persistent_data_ty> const& tplKeyData) {
         persistent_data_ty aData{ tplKeyData };
         return SelectImpl(db, std::move(aData),
            std::make_index_sequence<meta_count_v<persistent_data_ty>> {}).data;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> Select(app_db& db,
         data_ty<persistent_data_ty>&& tplKeyData) {
         persistent_data_ty aData{ std::move(tplKeyData) };
         return SelectImpl(db, std::move(aData),
            std::make_index_sequence<meta_count_v<persistent_data_ty>> {}).data;
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db, class... Ts>
      [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> SelectAllTyped(app_db& db) {
         std::string strSql = builder_ty<persistent_data_ty, 0>::CreateSelectFromSql();
         adecc::db_params vecParams{};
         auto rngFirst = db.template Execute<Ts...>(strSql, vecParams);
         for (auto&& tplFirst : rngFirst) {
            persistent_data_ty aData{ persistent_data_ty::template MapSelectTuple<0>(tplFirst) };
            if constexpr (meta_count_v<persistent_data_ty> > 1U) {
               co_yield SelectRestImpl(db, std::move(aData),
                  std::make_index_sequence<meta_count_v<persistent_data_ty> -1U> {}).data;
            }
            else {
               co_yield std::move(aData.data);
            }
         }
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> SelectAll(app_db& db) {
         return meta_types_list_ty<persistent_data_ty, 0>::invoke([&]<class... Ts>() {
            return SelectAllTyped<persistent_data_ty, app_db, Ts...>(db);
         });
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::ranges::input_range range_ty>
         requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
      data_ty<persistent_data_ty>>
         [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> Insert(app_db& db,
            range_ty&& rngData) {
         for (auto&& tplData : rngData) {
            co_yield Insert<persistent_data_ty>(db, std::forward<decltype(tplData)>(tplData));
         }
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::ranges::input_range range_ty>
         requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
      data_ty<persistent_data_ty>>
         [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> Update(app_db& db,
            range_ty&& rngData) {
         for (auto&& tplData : rngData) {
            co_yield Update<persistent_data_ty>(db, std::forward<decltype(tplData)>(tplData));
         }
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::ranges::input_range range_ty>
         requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
      data_ty<persistent_data_ty>>
         [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> Delete(app_db& db,
            range_ty&& rngData) {
         for (auto&& tplData : rngData) {
            co_yield Delete<persistent_data_ty>(db, std::forward<decltype(tplData)>(tplData));
         }
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::ranges::input_range range_ty>
         requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
      data_ty<persistent_data_ty>>
         [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> Select(app_db& db,
            range_ty&& rngKeyData) {
         for (auto&& tplKeyData : rngKeyData) {
            co_yield Select<persistent_data_ty>(db, std::forward<decltype(tplKeyData)>(tplKeyData));
         }
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> InsertPersistent(app_db& db,
         persistent_data_ty const& theData) {
         return Insert<persistent_data_ty>(db, theData.data);
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> InsertPersistent(app_db& db,
         persistent_data_ty&& theData) {
         return Insert<persistent_data_ty>(db, std::move(theData.data));
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> UpdatePersistent(app_db& db,
         persistent_data_ty const& theData) {
         return Update<persistent_data_ty>(db, theData.data);
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> UpdatePersistent(app_db& db,
         persistent_data_ty&& theData) {
         return Update<persistent_data_ty>(db, std::move(theData.data));
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> DeletePersistent(app_db& db,
         persistent_data_ty const& theData) {
         return Delete<persistent_data_ty>(db, theData.data);
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> DeletePersistent(app_db& db,
         persistent_data_ty&& theData) {
         return Delete<persistent_data_ty>(db, std::move(theData.data));
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> SelectPersistent(app_db& db,
         persistent_data_ty const& theData) {
         return Select<persistent_data_ty>(db, theData.data);
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db>
      [[nodiscard]] static data_ty<persistent_data_ty> SelectPersistent(app_db& db,
         persistent_data_ty&& theData) {
         return Select<persistent_data_ty>(db, std::move(theData.data));
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::ranges::input_range range_ty>
         requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
      persistent_data_ty>
      [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> InsertPersistent(app_db& db,
         range_ty&& rngData) {
         for (auto&& theData : rngData) {
            co_yield Insert<persistent_data_ty>(db, std::forward<decltype(theData)>(theData).data);
         }
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::ranges::input_range range_ty>
         requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
      persistent_data_ty>
      [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> UpdatePersistent(app_db& db,
         range_ty&& rngData) {
         for (auto&& theData : rngData) {
            co_yield Update<persistent_data_ty>(db, std::forward<decltype(theData)>(theData).data);
         }
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::ranges::input_range range_ty>
         requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
      persistent_data_ty>
      [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> DeletePersistent(app_db& db,
         range_ty&& rngData) {
         for (auto&& theData : rngData) {
            co_yield Delete<persistent_data_ty>(db, std::forward<decltype(theData)>(theData).data);
         }
      }

      template <persistent_system_data_type persistent_data_ty, logical_database_type app_db,
         std::ranges::input_range range_ty>
         requires std::same_as<std::remove_cvref_t<std::ranges::range_value_t<range_ty>>,
      persistent_data_ty>
      [[nodiscard]] static adecc::Generator<data_ty<persistent_data_ty>> SelectPersistent(app_db& db,
         range_ty&& rngKeyData) {
         for (auto&& theKeyData : rngKeyData) {
            co_yield Select<persistent_data_ty>(db, std::forward<decltype(theKeyData)>(theKeyData).data);
         }
      }
   };

} // namespace adecc::db
