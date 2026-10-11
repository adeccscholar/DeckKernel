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
   aDatabase.ExecuteCommand("DROP VIEW IF EXISTS deckkernel_test.scryfall_cards_view");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_card_land_types");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_card_creature_types");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_card_artifact_types");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_card_supertypes");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_card_main_types");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_card_faces");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_cards");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_sets");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.land_types");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.creatures_types");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.artifact_types");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.supertypes");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.card_types");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.land_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.creatures_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.artifact_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.supertypes (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.card_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_sets ("
      "id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, scryfall_id text NOT NULL UNIQUE, code text NOT NULL UNIQUE,"
      "name text NOT NULL, parent_set_code integer REFERENCES deckkernel_test.scryfall_sets(id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_cards ("
      "id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, scryfall_id text NOT NULL UNIQUE, oracle_id text,"
      "name text NOT NULL, set_id integer NOT NULL REFERENCES deckkernel_test.scryfall_sets(id),"
      "released_at date NOT NULL,"
      "variation_of_id integer REFERENCES deckkernel_test.scryfall_cards(id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_faces ("
      "card_id integer NOT NULL REFERENCES deckkernel_test.scryfall_cards(id),"
      "face_id integer NOT NULL CHECK (face_id >= 0),"
      "is_main boolean GENERATED ALWAYS AS (face_id = 0) STORED,"
      "name text NOT NULL, type_line text NOT NULL,"
      "PRIMARY KEY (card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_artifact_types ("
      "card_id integer NOT NULL, face_id integer NOT NULL,"
      "artifact_type_id integer NOT NULL REFERENCES deckkernel_test.artifact_types(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, artifact_type_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_supertypes ("
      "card_id integer NOT NULL, face_id integer NOT NULL,"
      "supertype_id integer NOT NULL REFERENCES deckkernel_test.supertypes(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, supertype_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_main_types ("
      "card_id integer NOT NULL, face_id integer NOT NULL,"
      "main_type_id integer NOT NULL REFERENCES deckkernel_test.card_types(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, main_type_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_land_types ("
      "card_id integer NOT NULL,"
      "face_id integer NOT NULL,"
      "land_type_id integer NOT NULL REFERENCES deckkernel_test.land_types(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, land_type_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_creature_types ("
      "card_id integer NOT NULL,"
      "face_id integer NOT NULL,"
      "creature_type_id integer NOT NULL REFERENCES deckkernel_test.creatures_types(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, creature_type_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
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
   auto aLandOutput = catalog_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputRange<types_ty...>(
         TLandType::CreateInsertSql(), TLandType::CreateInsertOutputParameters());
      });
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
   auto aCreatureOutput = catalog_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputRange<types_ty...>(
         TCreatureType::CreateInsertSql(), TCreatureType::CreateInsertOutputParameters());
      });
   std::map<std::string, int> mpCreatureIds;
   for (auto const& [strCode, iUnused] : aParsed.mpCreatureTypes) {
      std::array<TCreatureType::data_ty, 1> arrInput{{ TCreatureType::data_ty{0, strCode} }};
      for (auto const& tplResult : aCreatureOutput(arrInput)) {
         if (!mpCreatureIds.emplace(strCode, std::get<0>(tplResult)).second) {
            throw std::runtime_error("duplicate creature type: " + strCode);
            }
         }
      }
   std::println("[TIME] Store CreatureTypes: {:.3f} s", ElapsedSeconds(aCreatureStart));
   std::println("[STORE] CreatureTypes written: {} rows", mpCreatureIds.size());
   auto fnStoreCatalog = [&]<typename meta_ty>(
      std::map<std::string, int> const& mpSource, meta_ty) {
      auto aOutput = catalog_types::invoke([&]<typename... types_ty>() {
         return aDatabase.template MakeOutputRange<types_ty...>(
            meta_ty::CreateInsertSql(), meta_ty::CreateInsertOutputParameters());
         });
      std::map<std::string, int> mpIds;
      for (auto const& [strCode, iUnused] : mpSource) {
         std::array<catalog_types::type_list, 1> arrInput{{catalog_types::type_list{0, strCode}}};
         for (auto const& tplResult : aOutput(arrInput)) {
            if (!mpIds.emplace(strCode, std::get<0>(tplResult)).second) {
               throw std::runtime_error("duplicate catalog type: " + strCode);
               }
            }
         }
      return mpIds;
      };
   auto const aSuperStart = std::chrono::steady_clock::now();
   auto mpArtifactIds = fnStoreCatalog(aParsed.mpArtifactTypes, TArtifactType{});
   std::println("[STORE] ArtifactTypes written: {} rows", mpArtifactIds.size());
   auto mpSuperIds = fnStoreCatalog(aParsed.mpSuperTypes, TSuperType{});
   std::println("[TIME] Store SuperTypes: {:.3f} s", ElapsedSeconds(aSuperStart));
   std::println("[STORE] SuperTypes written: {} rows", mpSuperIds.size());
   auto const aMainStart = std::chrono::steady_clock::now();
   auto mpMainIds = fnStoreCatalog(aParsed.mpMainTypes, TMainType{});
   std::println("[TIME] Store MainTypes: {:.3f} s", ElapsedSeconds(aMainStart));
   std::println("[STORE] MainTypes written: {} rows", mpMainIds.size());
   aCatalogTransaction.CommitAndClose();
   std::println("[STORE] Catalogs committed: {} land types, {} creature types",
      mpLandIds.size(), mpCreatureIds.size());

   // Set graph: root sets are persisted before any child.
   std::vector<StagedSet> vecSets;
   vecSets.reserve(aParsed.mpSets.size());
   for (auto const& [strCode, aSet] : aParsed.mpSets) { vecSets.emplace_back(aSet); }
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
   std::map<std::optional<std::string>, std::optional<int>> mpParentIds;
   mpParentIds.emplace(std::nullopt, std::nullopt);
   auto aSetOutput = set_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputRange<types_ty...>(
         TScryfallSet::CreateInsertSql(), TScryfallSet::CreateInsertOutputParameters());
      });
   for (std::size_t const uIndex : vecSetOrder) {
      StagedSet const& aSet = vecSets[uIndex];
      staged_set_types::type_list tplStaged{
         0, aSet.strUuid, aSet.strCode, aSet.strName, aSet.optParentCode };
      auto tplSet = adecc::Transform<staged_set_types, set_types>(
         std::move(tplStaged),
         adecc::BindMapping<4, staged_set_types, set_types>(mpParentIds));
      std::array<TScryfallSet::data_ty, 1> arrInput{{ std::move(tplSet) }};
      for (auto const& tplInserted : aSetOutput(arrInput)) {
         int const iId = std::get<0>(tplInserted);
         if (iId <= 0 || !mpSetIds.emplace(aSet.strCode, iId).second) {
            throw std::runtime_error("invalid or duplicate generated set identity");
            }
         mpParentIds.emplace(aSet.strCode, iId);
         }
      }

   aSetTransaction.CommitAndClose();
   std::println("[STORE] CardSets committed: {} rows (parents before children)", mpSetIds.size());

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
   auto aCardOutput = card_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputRange<types_ty...>(
         TScryfallCard::CreateInsertSql(), TScryfallCard::CreateInsertOutputParameters());
      });
   auto aFaceSink = card_face_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputSink<types_ty...>(
         TCardFace::CreateInsertSql(), TCardFace::CreateInsertOutputParameters());
      });
   auto aLandLinkSink = card_type_link_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputSink<types_ty...>(
         TCardLandType::CreateInsertSql(), TCardLandType::CreateInsertOutputParameters());
      });
   auto aCreatureLinkSink = card_type_link_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputSink<types_ty...>(
         TCardCreatureType::CreateInsertSql(), TCardCreatureType::CreateInsertOutputParameters());
      });
   auto aArtifactLinkSink = card_type_link_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputSink<types_ty...>(
         TCardArtifactType::CreateInsertSql(), TCardArtifactType::CreateInsertOutputParameters());
      });
   auto aSuperLinkSink = card_type_link_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputSink<types_ty...>(
         TCardSuperType::CreateInsertSql(), TCardSuperType::CreateInsertOutputParameters());
      });
   auto aMainLinkSink = card_type_link_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputSink<types_ty...>(
         TCardMainType::CreateInsertSql(), TCardMainType::CreateInsertOutputParameters());
      });
   std::size_t uSuperLinks = 0, uMainLinks = 0, uArtifactLinks = 0;
   std::size_t uLandLinks = 0, uCreatureLinks = 0;
   for (std::size_t const uIndex : vecCardOrder) {
      StagedCard const& aCard = vecResolvedCards[uIndex];
      std::optional<int> optVariation;
      if (aCard.optVariationOf) { optVariation = mpCardIds.at(*aCard.optVariationOf); }
      staged_card_types::type_list tplStaged{
         0, aCard.strUuid, aCard.optOracleId, aCard.strName,
         aCard.strSetCode, aCard.aReleasedAt, optVariation };
      TScryfallCard::data_ty tplCard = adecc::Transform<staged_card_types, card_types>(
         std::move(tplStaged),
         adecc::BindMapping<4, staged_card_types, card_types>(mpSetIds));
      std::array<TScryfallCard::data_ty, 1> arrInput{{ std::move(tplCard) }};
      for (auto const& tplInserted : aCardOutput(arrInput)) {
         int const iId = std::get<0>(tplInserted);
         if (iId <= 0 || !mpCardIds.emplace(aCard.strUuid, iId).second) {
            throw std::runtime_error("invalid or duplicate generated card identity");
            }
         for (StagedFace const& aFace : aCard.vecFaces) {
            std::array<card_face_types::type_list, 1> arrFace{{
               card_face_types::type_list{iId, aFace.iFaceId, aFace.strName, aFace.strTypeLine}
               }};
            aFaceSink = arrFace;
            std::vector<card_type_link_types::type_list> vecLandLinks, vecCreatureLinks;
            vecLandLinks.reserve(aFace.vecLandTypes.size());
            vecCreatureLinks.reserve(aFace.vecCreatureTypes.size());
            for (std::size_t uPos = 0; uPos < aFace.vecLandTypes.size(); ++uPos) {
               staged_card_type_link_types::type_list tplStage{
                  iId, aFace.iFaceId, aFace.vecLandTypes[uPos], static_cast<int>(uPos) };
               vecLandLinks.emplace_back(adecc::Transform<
                  staged_card_type_link_types, card_type_link_types>(
                  std::move(tplStage),
                  adecc::BindMapping<2, staged_card_type_link_types, card_type_link_types>(mpLandIds)));
               }
            for (std::size_t uPos = 0; uPos < aFace.vecCreatureTypes.size(); ++uPos) {
               staged_card_type_link_types::type_list tplStage{
                  iId, aFace.iFaceId, aFace.vecCreatureTypes[uPos], static_cast<int>(uPos) };
               vecCreatureLinks.emplace_back(adecc::Transform<
                  staged_card_type_link_types, card_type_link_types>(
                  std::move(tplStage),
                  adecc::BindMapping<2, staged_card_type_link_types, card_type_link_types>(mpCreatureIds)));
               }
            auto fnLinks = [&]<typename sink_ty>(std::vector<std::string> const& vecCodes,
               std::map<std::string, int> const& mpIds, sink_ty& aSink) {
               std::vector<card_type_link_types::type_list> vecLinks;
               vecLinks.reserve(vecCodes.size());
               for (std::size_t uPos = 0; uPos < vecCodes.size(); ++uPos) {
                  staged_card_type_link_types::type_list tplSource{
                     iId, aFace.iFaceId, vecCodes[uPos], static_cast<int>(uPos)};
                  vecLinks.emplace_back(adecc::Transform<
                     staged_card_type_link_types, card_type_link_types>(
                     std::move(tplSource),
                     adecc::BindMapping<2, staged_card_type_link_types, card_type_link_types>(mpIds)));
                  }
               if (!vecLinks.empty()) { aSink = vecLinks; }
               return vecLinks.size();
               };
            uArtifactLinks += fnLinks(aFace.vecArtifactTypes, mpArtifactIds, aArtifactLinkSink);
            uSuperLinks += fnLinks(aFace.vecSuperTypes, mpSuperIds, aSuperLinkSink);
            uMainLinks += fnLinks(aFace.vecMainTypes, mpMainIds, aMainLinkSink);
            if (!vecLandLinks.empty()) { aLandLinkSink = vecLandLinks; }
            if (!vecCreatureLinks.empty()) { aCreatureLinkSink = vecCreatureLinks; }
            uLandLinks += vecLandLinks.size();
            uCreatureLinks += vecCreatureLinks.size();
            }
         }
      }
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_oracle_id ON deckkernel_test.scryfall_cards(oracle_id)");
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_released ON deckkernel_test.scryfall_cards(released_at DESC)");
   aDatabase.ExecuteCommand(
      "CREATE VIEW deckkernel_test.scryfall_cards_view AS "
      "SELECT c.*, f.face_id, f.is_main, f.name AS face_name, f.type_line, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_artifact_types x "
      "JOIN deckkernel_test.artifact_types t ON t.id=x.artifact_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS artifact_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_supertypes x "
      "JOIN deckkernel_test.supertypes t ON t.id=x.supertype_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS supertypes, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_main_types x "
      "JOIN deckkernel_test.card_types t ON t.id=x.main_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS main_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_land_types x "
      "JOIN deckkernel_test.land_types t ON t.id=x.land_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS land_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_creature_types x "
      "JOIN deckkernel_test.creatures_types t ON t.id=x.creature_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS creature_types "
      "FROM deckkernel_test.scryfall_cards c "
      "JOIN deckkernel_test.scryfall_card_faces f ON f.card_id=c.id");
   aCardTransaction.CommitAndClose();
   std::println("[STORE] Card subtype links committed: land={}, creature={}",
      uLandLinks, uCreatureLinks);
   std::println("[STORE] Card supertypes/main types: super={}, main={}",
      uSuperLinks, uMainLinks);
   std::println("[STORE] Artifact type links: {}", uArtifactLinks);
   std::println("[TIME] Store Cards: {:.3f} s", ElapsedSeconds(aCardStart));
   std::println("[STORE] Cards committed: {} rows, {} missing variation targets",
      mpCardIds.size(), uMissingVariations);
   std::println("Stored {} cards and {} topologically ordered sets.", aParsed.vecCards.size(), vecSets.size());
   }

} // namespace deckkernel::test
