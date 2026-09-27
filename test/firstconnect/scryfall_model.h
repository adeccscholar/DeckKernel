// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_model.h
\brief Persistent data definitions used by the first DeckKernel connection test.

\details
Keeps the domain-oriented type lists, PostgreSQL metadata and named selectors and
manipulators separate from the transport and process code. The concrete classes follow
the PersistentSystemData pattern described in Rethinking C++ (C++ neu denken): the
generic tuple mechanics remain in core_cpp while the application layer gives fields
domain names through selectors and manipulators.

\author Volker Hillmann (adecc Systemhaus GmbH)
\date 27.09.2026
*/

#pragma once

#include "system_data_persistent.h"

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace deckkernel::test {

using set_types = adecc::defined_type_list<
   std::string,
   std::string,
   std::string
   >;

struct SetMetaData {
   static constexpr std::string_view svTableName = "deckkernel_test.scryfall_sets";

   static constexpr std::array<std::string_view, 3> arrAttributeNames {
      "id",
      "code",
      "name"
      };

   static constexpr std::array<std::size_t, 1> arrKeyIndices { 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex {};
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices {};
   };

class TScryfallSet final
   : public adecc::db::PersistentSystemData<set_types, SetMetaData> {
public:
   using base_ty = adecc::db::PersistentSystemData<set_types, SetMetaData>;
   using base_ty::base_ty;
   using base_ty::operator=;

   element_ty<0> const& Id() const {
      return Get<0>();
      }

   element_ty<1> const& Code() const {
      return Get<1>();
      }

   element_ty<2> const& Name() const {
      return Get<2>();
      }

   template <typename value_ty>
      requires adecc::HasConvertTo<value_ty, element_ty<0>>
   void Id(value_ty&& aValue) {
      Set<0>(std::forward<value_ty>(aValue));
      }

   template <typename value_ty>
      requires adecc::HasConvertTo<value_ty, element_ty<1>>
   void Code(value_ty&& aValue) {
      Set<1>(std::forward<value_ty>(aValue));
      }

   template <typename value_ty>
      requires adecc::HasConvertTo<value_ty, element_ty<2>>
   void Name(value_ty&& aValue) {
      Set<2>(std::forward<value_ty>(aValue));
      }
   };


using card_types = adecc::defined_type_list<
   std::string,
   std::optional<std::string>,
   std::string,
   std::string,
   adecc::date_ty
   >;

struct CardMetaData {
   static constexpr std::string_view svTableName = "deckkernel_test.scryfall_cards";

   static constexpr std::array<std::string_view, 5> arrAttributeNames {
      "id",
      "oracle_id",
      "name",
      "set_id",
      "released_at"
      };

   static constexpr std::array<std::size_t, 1> arrKeyIndices { 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex {};
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices {};
   };

class TScryfallCard final
   : public adecc::db::PersistentSystemData<card_types, CardMetaData> {
public:
   using base_ty = adecc::db::PersistentSystemData<card_types, CardMetaData>;
   using base_ty::base_ty;
   using base_ty::operator=;

   element_ty<0> const& Id() const {
      return Get<0>();
      }

   element_ty<1> const& OracleId() const {
      return Get<1>();
      }

   element_ty<2> const& Name() const {
      return Get<2>();
      }

   element_ty<3> const& SetId() const {
      return Get<3>();
      }

   element_ty<4> const& ReleasedAt() const {
      return Get<4>();
      }

   template <typename value_ty>
      requires adecc::HasConvertTo<value_ty, element_ty<0>>
   void Id(value_ty&& aValue) {
      Set<0>(std::forward<value_ty>(aValue));
      }

   template <typename value_ty>
      requires adecc::HasConvertTo<value_ty, element_ty<1>>
   void OracleId(value_ty&& aValue) {
      Set<1>(std::forward<value_ty>(aValue));
      }

   template <typename value_ty>
      requires adecc::HasConvertTo<value_ty, element_ty<2>>
   void Name(value_ty&& aValue) {
      Set<2>(std::forward<value_ty>(aValue));
      }

   template <typename value_ty>
      requires adecc::HasConvertTo<value_ty, element_ty<3>>
   void SetId(value_ty&& aValue) {
      Set<3>(std::forward<value_ty>(aValue));
      }

   template <typename value_ty>
      requires adecc::HasConvertTo<value_ty, element_ty<4>>
   void ReleasedAt(value_ty&& aValue) {
      Set<4>(std::forward<value_ty>(aValue));
      }
   };

} // namespace deckkernel::test
