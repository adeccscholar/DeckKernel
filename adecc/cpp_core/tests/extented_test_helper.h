// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file extented_test_helper.h
\brief Bridge, lookup, loading, and set-conversion helpers for persistent integration tests.

\details
Provides generic range transformations that turn persistent tuples into sets and lookup maps, load typed
persistent values, and resolve external keys to internal identities. These utilities keep identity bridging
explicit without introducing a dynamic mapping framework.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Practical Example: Financial Data as a Typed Data Flow".
- "Tests as Architectural Proof".
- "Data Movement Between Source and Sink".
- "The Core Belongs in C++".

\see ../ARCHITECTURE.md#tests-as-architectural-proof

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

#include "database.h"
#include "system_data_persistent.h"

#include "extented_test_types.h"

#include <set>

namespace extented_test {

   template <adecc::db::persistent_system_data_type data_ty,
             std::ranges::input_range range_ty>
        requires adecc::db::persistent_system_data_tuple_range<range_ty, data_ty>
   [[nodiscard]] auto ToPersistentSet(range_ty&& rngValues) {
      return std::forward<range_ty>(rngValues)
                   | std::views::transform([](auto&& val) {
                           return data_ty { std::forward<decltype(val)>(val) };
                           })
                   | std::ranges::to<std::set<data_ty>>();
      }

   template <adecc::db::persistent_system_data_type data_ty,
             adecc::db::logical_database_type db_ty>
   auto LoadValues(db_ty& db) {
      return data_ty::types_list::invoke([&]<class... Ts>() {
         return ToPersistentSet<data_ty>(
            db.template Execute<Ts...>(data_ty::CreateSelectFromSql(), {} )
            );
         });
      }

   template <std::size_t uKeyIndex,std::size_t uValueIndex,
             std::ranges::input_range range_ty>
      requires adecc::tuple_index_available<std::ranges::range_value_t<range_ty>, uKeyIndex> &&
               adecc::tuple_index_available<std::ranges::range_value_t<range_ty>, uValueIndex>
   [[nodiscard]] auto BuildBridge(range_ty&& rngValues) {
      using entry_ty = std::remove_cvref_t<std::ranges::range_value_t<range_ty>>;
      using key_ty = std::remove_cvref_t<std::tuple_element_t<uKeyIndex, entry_ty>>;
      using value_ty = std::remove_cvref_t<std::tuple_element_t<uValueIndex, entry_ty>>;

      return std::forward<range_ty>(rngValues) 
               | std::views::transform([](entry_ty const& entry) {
                    return std::pair<key_ty, value_ty> {
                                     std::get<uKeyIndex>(entry),
                                     std::get<uValueIndex>(entry)
                                     };
                    })
               | std::ranges::to<std::map<key_ty, value_ty>>();
      }


   /**
     \brief Looks up a key in a bridge map.
     \details
        The function returns the mapped value or throws an exception with a
        formatted error message if the key does not exist.
     \tparam key_ty Key type.
     \tparam value_ty Value type.
     \tparam compare_ty Map comparison type.
     \tparam allocator_ty Map allocator type.
     \param mpBridge Bridge map.
     \param theKey Key to look up.
     \returns Mapped value.
     \throw std::out_of_range If the key is not found.
   */
   template <typename key_ty, typename value_ty, typename compare_ty, typename allocator_ty>
   [[nodiscard]] value_ty BridgeValue(std::map<key_ty, value_ty, compare_ty, allocator_ty> const& mpBridge,
                                      key_ty const& theKey ) {
      if (auto const it = mpBridge.find(theKey); it != mpBridge.end()) [[likely]] {
         return it->second;
         }
      else {
         throw std::out_of_range { std::format("Bridge key '{}' not found.", theKey) };
         }
      }

   template <typename key_ty, typename value_ty, typename compare_ty, typename allocator_ty>
   [[nodiscard]] std::optional<value_ty> BridgeValue(std::map<key_ty, value_ty, compare_ty, allocator_ty> const& mpBridge,
                                                     std::optional<key_ty> const& optKey) {
      if (!optKey) {
         return std::nullopt;
         }
      else {
         if (auto const it = mpBridge.find(*optKey); it != mpBridge.end()) [[likely]] {
            return it->second;
            }
         else {
            throw std::out_of_range { std::format("Bridge key '{}' not found.", *optKey) };
            }
         }
      }

   template <typename value_ty, typename compare_ty, typename allocator_ty, typename key_ty>
   [[nodiscard]] value_ty const& FindInSet(std::set<value_ty, compare_ty, allocator_ty> const& Values,
                                           key_ty const& Value ) {
      if(auto const it = Values.find(value_ty(Value)); it == Values.end()) {
         throw std::out_of_range{ std::format("Value '{}' not found in set.", Value) };
         }
      else return *it;
      }


} // namespace extented_test