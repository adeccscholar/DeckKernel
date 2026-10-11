// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_database.cpp
\brief Lesson 3: PostgreSQL connection and transactional persistence.
*/

#include "scryfall_database.h"

#include "convert_integral.h"
#include "type_transformations.h"
#include <map>
#include <tuple>
#include <vector>
#include <stdexcept>
#include <algorithm>
#include <array>
#include <chrono>
#include <set>

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <format>

namespace deckkernel::test {
namespace {

std::string Environment(char const* const szName, std::string_view const svFallback) {
   if (char const* const szValue = std::getenv(szName); szValue != nullptr && *szValue != '\0') {
      return std::string{ szValue };
      }

   return std::string{ svFallback };
   }


bool EnvironmentFlag(char const* const szName, bool const boFallback) {
   std::string const strValue = Environment(szName, boFallback ? "true" : "false" );

   return strValue == "1" || strValue == "true" || strValue == "yes" || strValue == "on";
   }


adecc::db::postgres::postgres_credentials MakeCredentials() {
   adecc::db::postgres::postgres_credentials aCredentials;

   aCredentials.strHost = Environment("DECKKERNEL_PGHOST", "localhost");
   aCredentials.uPort = adecc::ConvertTo<std::uint16_t>( Environment("DECKKERNEL_PGPORT", "5432"));
   aCredentials.strDatabase        = Environment("DECKKERNEL_PGDATABASE", "DeckKernel");
   aCredentials.strUser            = Environment("DECKKERNEL_PGUSER", "deckkernel_user");
   aCredentials.strPassword        = Environment("DECKKERNEL_PGPASSWORD", "");
   aCredentials.boIntegrated       = EnvironmentFlag("DECKKERNEL_PG_INTEGRATED", true);
   aCredentials.strSslMode         = Environment("DECKKERNEL_PGSSLMODE", "prefer");
   aCredentials.strGssEncMode      = Environment("DECKKERNEL_PG_GSSENCMODE", "disable" );
   aCredentials.strGssLib          = Environment("DECKKERNEL_PG_GSSLIB", "");
   aCredentials.strKrbSrvName      = Environment("DECKKERNEL_PG_KRBSRVNAME", "postgres");
   aCredentials.strApplicationName = "DeckKernel-Scryfall-Test";

   return aCredentials;
   }


void EnsureSchema(postgres_database_ty const& aDatabase) {
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_cards");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_sets");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.land_types");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.creatures_types");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.land_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.creatures_types (id integer PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_sets ("
      "id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, scryfall_id text NOT NULL UNIQUE, code text NOT NULL UNIQUE,"
      "name text NOT NULL, parent_set_code integer REFERENCES deckkernel_test.scryfall_sets(id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_cards ("
      "id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, scryfall_id text NOT NULL UNIQUE, oracle_id text,"
      "name text NOT NULL, set_id integer NOT NULL REFERENCES deckkernel_test.scryfall_sets(id),"
      "released_at date NOT NULL,"
      "land_type_id integer REFERENCES deckkernel_test.land_types(id),"
      "creature_type_id integer REFERENCES deckkernel_test.creatures_types(id),"
      "variation_of_id integer REFERENCES deckkernel_test.scryfall_cards(id))");
   }

/**
\brief Orders a directed parent relation before insertion.
\details DFS detects both cycles and unknown parents; returned indices have parent before child.
\tparam row_ty Input row.
\tparam key_fn_ty Identity extractor.
\tparam parent_fn_ty Parent extractor.
\param vecRows Source rows.
\param fnKey Identity extractor.
\param fnParent Optional parent extractor.
\returns Topologically sorted row indices.
\throw std::runtime_error For cycles, missing parents or duplicate keys.
*/
template<typename row_ty, typename key_fn_ty, typename parent_fn_ty>
std::vector<std::size_t> TopologicalOrder(std::vector<row_ty> const& vecRows,
   key_fn_ty fnKey, parent_fn_ty fnParent) {
   std::map<std::string, std::size_t> mpIndex;
   for (std::size_t uIndex = 0; uIndex < vecRows.size(); ++uIndex) {
      if (!mpIndex.emplace(fnKey(vecRows[uIndex]), uIndex).second) {
         throw std::runtime_error("duplicate source identity in dependency graph");
         }
      }
   std::vector<unsigned char> vecColor(vecRows.size(), 0);
   std::vector<std::size_t> vecOrder;
   vecOrder.reserve(vecRows.size());
   auto fnVisit = [&](auto&& fnSelf, std::size_t const uIndex) -> void {
      if (vecColor[uIndex] == 1) { throw std::runtime_error("cyclic parent dependency"); }
      if (vecColor[uIndex] == 2) { return; }
      vecColor[uIndex] = 1;
      if (auto const optParent = fnParent(vecRows[uIndex]); optParent) {
         auto const it = mpIndex.find(*optParent);
         if (it == mpIndex.end()) { throw std::runtime_error("unknown parent: " + *optParent); }
         fnSelf(fnSelf, it->second);
         }
      vecColor[uIndex] = 2;
      vecOrder.emplace_back(uIndex);
      };
   for (std::size_t uIndex = 0; uIndex < vecRows.size(); ++uIndex) { fnVisit(fnVisit, uIndex); }
   return vecOrder;
   }

} // namespace


postgres_database_ty OpenDatabase() {
   postgres_database_ty aDatabase{
      MakeCredentials()
      };

   std::println("PostgreSQL connection:     {}", aDatabase.GetServer());
   std::println("PostgreSQL authentication: {}", aDatabase.Credentials().boIntegrated ? "integrated SSPI" : "password");

   return aDatabase;
   }


static double ElapsedSeconds(std::chrono::steady_clock::time_point const aStart) {
   return std::chrono::duration<double>(std::chrono::steady_clock::now() - aStart).count();
   }

void StoreProcess(postgres_database_ty& aDatabase, ParsedBulkData const& aParsed) {
   // DDL is committed independently: a later data error must not restore an obsolete schema.
   EnsureSchema(aDatabase);
   std::println("[STORE] Schema created: land_types, creatures_types, scryfall_sets, scryfall_cards");
   auto aCatalogTransaction = aDatabase.Transaction();
   auto const aLandStart = std::chrono::steady_clock::now();
   // Insert land types with PostgreSQL-generated identities via the adecc output range.
   auto aLandOutput = aDatabase.template MakeOutputRange<int, std::string>(
      TLandType::CreateInsertSql(), TLandType::CreateInsertOutputParameters());
   std::map<std::string, int> mpLandIds;
   for (auto const& [strCode, iIgnored] : aParsed.mpLandTypes) {
      std::array<TLandType::data_ty, 1> arrInput{{ TLandType::data_ty{0, strCode} }};
      for (auto const& tplResult : aLandOutput(arrInput)) {
         if (!mpLandIds.emplace(strCode, std::get<0>(tplResult)).second) {
            throw std::runtime_error("duplicate land type: " + strCode);
            }
         }
      }
   std::println("[TIME] Store LandTypes: {:.3f} s", ElapsedSeconds(aLandStart));
   std::println("[STORE] LandTypes written: {} rows", mpLandIds.size());
   auto const aCreatureStart = std::chrono::steady_clock::now();
   auto aCreatureSink = aDatabase.template MakeOutputSink<int, std::string>(
      TCreatureType::CreateInsertSql(), TCreatureType::CreateInsertOutputParameters());
   std::vector<std::tuple<int, std::string>> vecCreature;
   for (auto const& [strCode, iId] : aParsed.mpCreatureTypes) {
      vecCreature.emplace_back(iId, strCode);
      }
   aCreatureSink = vecCreature;
   std::println("[TIME] Store CreatureTypes: {:.3f} s", ElapsedSeconds(aCreatureStart));
   std::println("[STORE] CreatureTypes written: {} rows", vecCreature.size());
   aCatalogTransaction.CommitAndClose();
   std::println("[STORE] Catalogs committed: {} land types, {} creature types",
      mpLandIds.size(), vecCreature.size());

   // Set graph: root sets are persisted before any child.
   std::vector<StagedSet> vecSets;
   for (auto const& [strCode, aSet] : aParsed.mpSets) { vecSets.push_back(aSet); }
   // The API documents parent_set_code as a code. Some feeds contain UUID-shaped
   // references; resolve those through the set catalog before sorting.
   std::map<std::string, std::string> mpUuidToCode;
   for (StagedSet const& aSet : vecSets) {
      if (!mpUuidToCode.emplace(aSet.strUuid, aSet.strCode).second) {
         throw std::runtime_error("duplicate Scryfall set UUID: " + aSet.strUuid);
         }
      }
   std::size_t uUuidReferences = 0;
   for (StagedSet& aSet : vecSets) {
      if (!aSet.optParentCode) { continue; }
      if (aParsed.mpSets.contains(*aSet.optParentCode)) { continue; }
      auto const it = mpUuidToCode.find(*aSet.optParentCode);
      if (it != mpUuidToCode.end()) {
         aSet.optParentCode = it->second;
         ++uUuidReferences;
         }
      else {
         throw std::runtime_error(std::format(
            "CardSets unresolved parent: set code='{}', name='{}', uuid='{}', "
            "parent_set_code='{}' (neither set code nor UUID in catalog)",
            aSet.strCode, aSet.strName, aSet.strUuid, *aSet.optParentCode));
         }
      }
   std::println("[STORE] Validating {} CardSets, {} UUID-form parent references",
      vecSets.size(), uUuidReferences);
   std::vector<std::size_t> vecSetOrder;
   try {
      vecSetOrder = TopologicalOrder(vecSets,
         [](StagedSet const& aSet) { return aSet.strCode; },
         [](StagedSet const& aSet) { return aSet.optParentCode; });
      }
   catch (std::exception const& ex) {
      throw std::runtime_error(std::format(
         "CardSets dependency validation failed ({} sets): {}", vecSets.size(), ex.what()));
      }
   std::println("[STORE] Topological order validated: {} CardSets", vecSetOrder.size());
   auto aSetTransaction = aDatabase.Transaction();
   // PersistentSystemData omits identity from INSERT; the output range returns it.
   std::map<std::string, int> mpSetIds;
   auto aSetOutput = aDatabase.template MakeOutputRange<int, std::string, std::string,
      std::string, std::optional<int>>(
      TScryfallSet::CreateInsertSql(), TScryfallSet::CreateInsertOutputParameters());
   for (std::size_t const uIndex : vecSetOrder) {
      StagedSet const& aSet = vecSets[uIndex];
      std::optional<int> optParent;
      if (aSet.optParentCode) { optParent = mpSetIds.at(*aSet.optParentCode); }
      std::array<TScryfallSet::data_ty, 1> arrInput{{
         TScryfallSet::data_ty{0, aSet.strUuid, aSet.strCode, aSet.strName, optParent}
         }};
      for (auto const& tplInserted : aSetOutput(arrInput)) {
         int const iId = std::get<0>(tplInserted);
         if (iId <= 0 || !mpSetIds.emplace(aSet.strCode, iId).second) {
            throw std::runtime_error("invalid or duplicate generated set identity");
            }
         }
      }

   aSetTransaction.CommitAndClose();
   std::println("[STORE] CardSets committed: {} rows (parents before children)", mpSetIds.size());

   std::map<std::optional<std::string>, std::optional<int>> mpLand, mpCreature;
   mpLand.emplace(std::nullopt, std::nullopt);
   mpCreature.emplace(std::nullopt, std::nullopt);
   for (auto const& [strCode, iId] : mpLandIds) { mpLand.emplace(strCode, iId); }
   for (auto const& [strCode, iId] : aParsed.mpCreatureTypes) { mpCreature.emplace(strCode, iId); }

   // The default_cards bulk may omit localized printings referenced by variation_of.
   // Keep the reference diagnostic, but do not manufacture an invalid foreign key.
   std::set<std::string> setAvailableCards;
   for (StagedCard const& aCard : aParsed.vecCards) {
      if (!setAvailableCards.emplace(aCard.strUuid).second) {
         throw std::runtime_error("duplicate Scryfall card UUID: " + aCard.strUuid);
         }
      }
   std::vector<StagedCard> vecResolvedCards = aParsed.vecCards;
   std::size_t uMissingVariations = 0;
   constexpr std::size_t uMaxExamples = 12;
   for (StagedCard& aCard : vecResolvedCards) {
      if (!aCard.optVariationOf || setAvailableCards.contains(*aCard.optVariationOf)) { continue; }
      ++uMissingVariations;
      if (uMissingVariations <= uMaxExamples) {
         std::println(std::cerr,
            "[WARN] Card variation target missing: card='{}', uuid='{}', set='{}', variation_of='{}'",
            aCard.strName, aCard.strUuid, aCard.strSetCode, *aCard.optVariationOf);
         }
      aCard.optVariationOf.reset();
      }
   if (uMissingVariations > uMaxExamples) {
      std::println(std::cerr, "[WARN] {} additional missing variation targets not printed",
         uMissingVariations - uMaxExamples);
      }
   std::println("[STORE] Card dependency check: {} cards, {} unresolved variation_of references",
      vecResolvedCards.size(), uMissingVariations);
   std::vector<std::size_t> vecCardOrder;
   try {
      vecCardOrder = TopologicalOrder(vecResolvedCards,
         [](StagedCard const& aCard) { return aCard.strUuid; },
         [](StagedCard const& aCard) { return aCard.optVariationOf; });
      }
   catch (std::exception const& ex) {
      throw std::runtime_error(std::format(
         "Card dependency validation failed ({} cards): {}", vecResolvedCards.size(), ex.what()));
      }
   std::println("[STORE] Topological order validated: {} Cards", vecCardOrder.size());
   auto aCardTransaction = aDatabase.Transaction();
   auto const aCardStart = std::chrono::steady_clock::now();
   std::map<std::string, int> mpCardIds;
   auto aCardOutput = aDatabase.template MakeOutputRange<int, std::string,
      std::optional<std::string>, std::string, int, adecc::date_ty,
      std::optional<int>, std::optional<int>, std::optional<int>>(
      TScryfallCard::CreateInsertSql(), TScryfallCard::CreateInsertOutputParameters());
   for (std::size_t const uIndex : vecCardOrder) {
      StagedCard const& aCard = vecResolvedCards[uIndex];
      std::optional<int> optVariation;
      if (aCard.optVariationOf) { optVariation = mpCardIds.at(*aCard.optVariationOf); }
      staged_card_types::type_list tplStaged{
         0, aCard.strUuid, aCard.optOracleId, aCard.strName,
         aCard.strSetCode, aCard.aReleasedAt, aCard.optLandType,
         aCard.optCreatureType, optVariation };
      TScryfallCard::data_ty tplCard = adecc::Transform<staged_card_types, card_types>(
         std::move(tplStaged),
         adecc::BindMapping<4, staged_card_types, card_types>(mpSetIds),
         adecc::BindMapping<6, staged_card_types, card_types>(mpLand),
         adecc::BindMapping<7, staged_card_types, card_types>(mpCreature));
      std::array<TScryfallCard::data_ty, 1> arrInput{{ std::move(tplCard) }};
      for (auto const& tplInserted : aCardOutput(arrInput)) {
         int const iId = std::get<0>(tplInserted);
         if (iId <= 0 || !mpCardIds.emplace(aCard.strUuid, iId).second) {
            throw std::runtime_error("invalid or duplicate generated card identity");
            }
         }
      }
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_oracle_id ON deckkernel_test.scryfall_cards(oracle_id)");
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_released ON deckkernel_test.scryfall_cards(released_at DESC)");
   aCardTransaction.CommitAndClose();
   std::println("[TIME] Store Cards: {:.3f} s", ElapsedSeconds(aCardStart));
   std::println("[STORE] Cards committed: {} rows, {} missing variation targets",
      mpCardIds.size(), uMissingVariations);
   std::println("Stored {} cards and {} topologically ordered sets.", aParsed.vecCards.size(), vecSets.size());
   }

} // namespace deckkernel::test
