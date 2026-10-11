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

using set_types = adecc::defined_type_list<int, std::string, std::string, std::string, std::optional<int>>;
struct SetMetaData {
   static constexpr std::string_view svTableName = "deckkernel_test.scryfall_sets";
   static constexpr std::array<std::string_view, 5> arrAttributeNames{
      "id", "scryfall_id", "code", "name", "parent_set_code" };
   static constexpr std::array<std::size_t, 1> arrKeyIndices{0};
   static constexpr std::optional<std::size_t> optIdentityIndex{0};
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices{};
   };
class TScryfallSet final : public adecc::db::PersistentSystemData<set_types, SetMetaData> {
public:
   using base_ty = adecc::db::PersistentSystemData<set_types, SetMetaData>;
   using base_ty::base_ty;
   using base_ty::operator=;
   element_ty<0> const& Id() const { return Get<0>(); }
   element_ty<1> const& ScryfallId() const { return Get<1>(); }
   element_ty<2> const& Code() const { return Get<2>(); }
   element_ty<3> const& Name() const { return Get<3>(); }
   element_ty<4> const& ParentSetId() const { return Get<4>(); }
   };

using catalog_types = adecc::defined_type_list<int, std::string>;

struct LandTypeMetaData {
   static constexpr std::string_view svTableName = "deckkernel_test.land_types";
   static constexpr std::array<std::string_view, 2> arrAttributeNames{ "id", "code" };
   static constexpr std::array<std::size_t, 1> arrKeyIndices{ 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex{};
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices{};
   };

struct CreatureTypeMetaData {
   static constexpr std::string_view svTableName = "deckkernel_test.creatures_types";
   static constexpr std::array<std::string_view, 2> arrAttributeNames{ "id", "code" };
   static constexpr std::array<std::size_t, 1> arrKeyIndices{ 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex{};
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices{};
   };

using TLandType = adecc::db::PersistentSystemData<catalog_types, LandTypeMetaData>;
using TCreatureType = adecc::db::PersistentSystemData<catalog_types, CreatureTypeMetaData>;

using staged_card_types = adecc::defined_type_list<int, std::string, std::optional<std::string>,
   std::string, std::string, adecc::date_ty, std::optional<std::string>,
   std::optional<std::string>, std::optional<int>>;
using card_types = adecc::defined_type_list<int, std::string, std::optional<std::string>,
   std::string, int, adecc::date_ty, std::optional<int>, std::optional<int>, std::optional<int>>;
struct CardMetaData {
   static constexpr std::string_view svTableName = "deckkernel_test.scryfall_cards";
   static constexpr std::array<std::string_view, 9> arrAttributeNames{
      "id", "scryfall_id", "oracle_id", "name", "set_id", "released_at",
      "land_type_id", "creature_type_id", "variation_of_id" };
   static constexpr std::array<std::size_t, 1> arrKeyIndices{0};
   static constexpr std::optional<std::size_t> optIdentityIndex{0};
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices{};
   };
class TScryfallCard final : public adecc::db::PersistentSystemData<card_types, CardMetaData> {
public:
   using base_ty = adecc::db::PersistentSystemData<card_types, CardMetaData>;
   using base_ty::base_ty;
   using base_ty::operator=;
   element_ty<0> const& Id() const { return Get<0>(); }
   element_ty<1> const& ScryfallId() const { return Get<1>(); }
   element_ty<2> const& OracleId() const { return Get<2>(); }
   element_ty<3> const& Name() const { return Get<3>(); }
   element_ty<4> const& SetId() const { return Get<4>(); }
   element_ty<5> const& ReleasedAt() const { return Get<5>(); }
   element_ty<6> const& LandTypeId() const { return Get<6>(); }
   element_ty<7> const& CreatureTypeId() const { return Get<7>(); }
   element_ty<8> const& VariationOfId() const { return Get<8>(); }
   };
} // namespace deckkernel::test
