// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_database.cpp
\brief Lesson 3: PostgreSQL connection and transactional persistence.
*/

#include "scryfall_database.h"

#include "convert_integral.h"
#include "type_transformations.h"
#include "type_lists.h"
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
#include <functional>

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
   // Cache schema and table are provisioned by the administrator in
   // test/postgresql_sspi_setup.sql. No database-level CREATE privilege is
   // granted to the FirstConnection application role.
   // The native named-parameter translator deliberately rejects PostgreSQL
   // dollar-quoted blocks. Query ordinary scalar values instead of DO $...$.
   // These preflight checks never create or mutate cache schema objects.
   auto const aDatabaseCheck = aDatabase.ExecuteOne<int>(
      "SELECT CASE WHEN current_database() = 'DeckKernel' THEN 1 ELSE 0 END");
   if(!aDatabaseCheck || std::get<0>(*aDatabaseCheck) != 1) {
      throw std::runtime_error(
         "FirstConnection requires the DeckKernel database");
      }

   auto const aCacheCheck = aDatabase.ExecuteOne<int>(
      "SELECT CASE WHEN to_regnamespace('deckkernel_cache') IS NOT NULL "
      "AND to_regclass('deckkernel_cache.scryfall_images') IS NOT NULL "
      "THEN 1 ELSE 0 END");
   if(!aCacheCheck || std::get<0>(*aCacheCheck) != 1) {
      throw std::runtime_error(
         "Persistent image cache is not provisioned; run "
         "test/postgresql_sspi_setup.sql as PostgreSQL administrator");
      }

   auto const aPermissionCheck = aDatabase.ExecuteOne<int>(
      "SELECT CASE WHEN "
      "has_schema_privilege(current_user, 'deckkernel_cache', 'USAGE') AND "
      "has_table_privilege(current_user, 'deckkernel_cache.scryfall_images', 'SELECT') AND "
      "has_table_privilege(current_user, 'deckkernel_cache.scryfall_images', 'INSERT') AND "
      "has_table_privilege(current_user, 'deckkernel_cache.scryfall_images', 'UPDATE') AND "
      "has_table_privilege(current_user, 'deckkernel_cache.scryfall_images', 'DELETE') "
      "THEN 1 ELSE 0 END");
   if(!aPermissionCheck || std::get<0>(*aPermissionCheck) != 1) {
      throw std::runtime_error(
         "Insufficient permissions for persistent image cache; run "
         "test/postgresql_sspi_setup.sql as PostgreSQL administrator");
      }

   // Migrate previous model image bytes before model tables are dropped.
   // On fresh installations the old table does not exist: skip migration.
   // This conditional is deliberately in C++, not a PostgreSQL DO block.
   auto const aPreviousAssets = aDatabase.ExecuteOne<int>(
      "SELECT CASE WHEN "
      "to_regclass('deckkernel_test.scryfall_image_assets') IS NOT NULL "
      "THEN 1 ELSE 0 END");
   if(!aPreviousAssets) {
      throw std::runtime_error(
         "Cannot inspect previous FirstConnection image assets");
      }
   if(std::get<0>(*aPreviousAssets) == 1) {
      aDatabase.ExecuteCommand(
         "INSERT INTO deckkernel_cache.scryfall_images(url,image_data,sha256,media_type,cached_at) "
         "SELECT image_url,image_data,sha256,media_type,COALESCE(downloaded_at,now()) "
         "FROM deckkernel_test.scryfall_image_assets "
         "WHERE image_data IS NOT NULL AND sha256 IS NOT NULL AND media_type IS NOT NULL "
         "ON CONFLICT (url) DO NOTHING");
      }
   aDatabase.ExecuteCommand("DROP VIEW IF EXISTS deckkernel_test.scryfall_cards_view");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_image_features");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_card_image_variants");
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.scryfall_image_assets");
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
   aDatabase.ExecuteCommand("DROP TABLE IF EXISTS deckkernel_test.rarities");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.land_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.creatures_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.artifact_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.supertypes (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.card_types (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.rarities (id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, code text NOT NULL UNIQUE)");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_sets ("
      "id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, scryfall_id text NOT NULL UNIQUE, code text NOT NULL UNIQUE,"
      "name text NOT NULL, parent_set_code integer REFERENCES deckkernel_test.scryfall_sets(id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_cards ("
      "id integer GENERATED ALWAYS AS IDENTITY PRIMARY KEY, scryfall_id text NOT NULL UNIQUE, oracle_id text,"
      "name text NOT NULL, set_id integer NOT NULL REFERENCES deckkernel_test.scryfall_sets(id),"
      "released_at date NOT NULL,"
      "variation_of_id integer REFERENCES deckkernel_test.scryfall_cards(id),"
      "rarity_id integer NOT NULL REFERENCES deckkernel_test.rarities(id),"
      "collector_number text NOT NULL, lang text NOT NULL,"
      "illustration_id text)");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_faces ("
      "card_id integer NOT NULL REFERENCES deckkernel_test.scryfall_cards(id),"
      "face_id integer NOT NULL CHECK (face_id >= 0),"
      "is_main boolean GENERATED ALWAYS AS (face_id = 0) STORED,"
      "name text NOT NULL, type_line text NOT NULL,"
      "layout text, loyalty text, mana_cost text NOT NULL, power text,"
      "illustration_id text, image_url_normal text, image_url_art_crop text, image_url_png text,"
      "image_url_small text, image_url_large text, image_url_border_crop text,"
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
   // Scryfall has no separate rarity catalog; use its documented domain.
   std::map<std::string, int> mpRaritySource{
      {"common", 0}, {"uncommon", 0}, {"rare", 0},
      {"mythic", 0}, {"special", 0}, {"bonus", 0}
      };
   auto mpRarityIds = fnStoreCatalog(mpRaritySource, TRarity{});
   std::println("[STORE] Rarity domain written: {} values", mpRarityIds.size());
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

   // Even all_cards may have incomplete historical variation references.
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
   // Mutable carrier: printing columns 0..10 plus the dependent face collection.
   using import_types = adecc::defined_type_list<int, std::string,
      std::optional<std::string>, std::string, int, adecc::date_ty,
      std::optional<int>, int, std::string, std::string, std::optional<std::string>,
      std::vector<StagedFace>>;
   using printing_projection = std::make_index_sequence<11>;
   using id_projection = std::index_sequence<0>;
   std::vector<import_types::type_list> vecImport;
   vecImport.reserve(vecResolvedCards.size());
   std::map<std::string, std::size_t> mpImportIndices;
   for (StagedCard const& aCard : vecResolvedCards) {
      staged_card_types::type_list tplStaged{
         0, aCard.strUuid, aCard.optOracleId, aCard.strName,
         aCard.strSetCode, aCard.aReleasedAt, std::nullopt, aCard.strRarity,
         aCard.strCollectorNumber, aCard.strLanguage, aCard.optIllustrationId};
      auto tplPrinting = adecc::Transform<staged_card_types, card_types>(
         std::move(tplStaged),
         adecc::BindMapping<4, staged_card_types, card_types>(mpSetIds),
         adecc::BindMapping<7, staged_card_types, card_types>(mpRarityIds));
      mpImportIndices.emplace(aCard.strUuid, vecImport.size());
      vecImport.emplace_back(std::tuple_cat(std::move(tplPrinting),
         std::tuple{aCard.vecFaces}));
      }
   auto aPrintingRows = vecCardOrder | std::views::transform([&](std::size_t uIndex) {
      auto& tplData = vecImport[uIndex];
      StagedCard const& aCard = vecResolvedCards[uIndex];
      if (aCard.optVariationOf) {
         std::get<6>(tplData) = mpCardIds.at(*aCard.optVariationOf);
         }
      return adecc::selectTplValues<printing_projection>(tplData);
      });
   std::ranges::for_each(aCardOutput(aPrintingRows), [&](auto const& tplInserted) {
      int const iId = std::get<0>(tplInserted);
      std::string const& strUuid = std::get<1>(tplInserted);
      if (iId <= 0 || !mpCardIds.emplace(strUuid, iId).second) {
         throw std::runtime_error("invalid or duplicate generated card identity");
         }
      adecc::assignTplValues<id_projection>(
         vecImport.at(mpImportIndices.at(strUuid)), std::tuple{iId});
      });
   auto aFaceRows = vecImport
      | std::views::transform([](import_types::type_list const& tplData) {
         return std::get<11>(tplData)
            | std::views::transform([&tplData](StagedFace const& aFace) {
               return card_face_types::type_list{
                  std::get<0>(tplData), aFace.iFaceId, aFace.strName, aFace.strTypeLine,
                  aFace.optLayout, aFace.optLoyalty, aFace.strManaCost, aFace.optPower,
                  aFace.optIllustrationId, aFace.optImageNormalUrl,
                  aFace.optImageArtCropUrl, aFace.optImagePngUrl,
                  aFace.optImageSmallUrl, aFace.optImageLargeUrl, aFace.optImageBorderCropUrl};
               });
         })
      | std::views::join;
   aFaceSink(aFaceRows);

   // One generic transformation pipeline for all five face-scoped type relations.
   auto fnWriteTypeLinks = [&]<typename sink_ty>(
      auto fnCodes, std::map<std::string, int> const& mpIds, sink_ty& aSink) {
      auto aRows = vecImport
         | std::views::transform([&](import_types::type_list const& tplData) {
            return std::get<11>(tplData)
               | std::views::transform([&](StagedFace const& aFace) {
                  auto const& vecCodes = std::invoke(fnCodes, aFace);
                  return std::views::iota(std::size_t{0}, vecCodes.size())
                     | std::views::transform([&](std::size_t uPos) {
                        return card_type_link_types::type_list{
                           std::get<0>(tplData), aFace.iFaceId,
                           mpIds.at(vecCodes[uPos]), static_cast<int>(uPos)};
                        });
                  }) | std::views::join;
            }) | std::views::join;
      std::size_t const uRows = std::ranges::distance(aRows);
      aSink(aRows);
      return uRows;
      };
   std::size_t const uSuperLinks = fnWriteTypeLinks(
      [](StagedFace const& aFace) -> auto const& { return aFace.vecSuperTypes; },
      mpSuperIds, aSuperLinkSink);
   std::size_t const uMainLinks = fnWriteTypeLinks(
      [](StagedFace const& aFace) -> auto const& { return aFace.vecMainTypes; },
      mpMainIds, aMainLinkSink);
   std::size_t const uArtifactLinks = fnWriteTypeLinks(
      [](StagedFace const& aFace) -> auto const& { return aFace.vecArtifactTypes; },
      mpArtifactIds, aArtifactLinkSink);
   std::size_t const uLandLinks = fnWriteTypeLinks(
      [](StagedFace const& aFace) -> auto const& { return aFace.vecLandTypes; },
      mpLandIds, aLandLinkSink);
   std::size_t const uCreatureLinks = fnWriteTypeLinks(
      [](StagedFace const& aFace) -> auto const& { return aFace.vecCreatureTypes; },
      mpCreatureIds, aCreatureLinkSink);
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_oracle_id ON deckkernel_test.scryfall_cards(oracle_id)");
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_set_number_lang ON deckkernel_test.scryfall_cards(set_id, collector_number, lang)");
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_illustration ON deckkernel_test.scryfall_cards(illustration_id)");
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_faces_illustration ON deckkernel_test.scryfall_card_faces(illustration_id)");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_image_assets ("
      "id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY, image_url text NOT NULL UNIQUE,"
      "image_kind text NOT NULL DEFAULT 'art_crop', illustration_id text,"
      "sha256 text, media_type text, image_data bytea, downloaded_at timestamptz,"
      "CHECK (sha256 IS NULL OR length(sha256)=64))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_image_features ("
      "asset_id bigint PRIMARY KEY REFERENCES deckkernel_test.scryfall_image_assets(id) ON DELETE CASCADE,"
      "algorithm text NOT NULL, algorithm_version integer NOT NULL,"
      "perceptual_hash bit(64), orb_descriptors bytea, orb_keypoint_count integer NOT NULL DEFAULT 0,"
      "calculated_at timestamptz NOT NULL DEFAULT now())");
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_image_assets_illustration ON deckkernel_test.scryfall_image_assets(illustration_id)");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_image_variants ("
      "card_id integer NOT NULL, face_id integer NOT NULL,"
      "variant text NOT NULL, asset_id bigint NOT NULL REFERENCES deckkernel_test.scryfall_image_assets(id),"
      "PRIMARY KEY (card_id,face_id,variant),"
      "FOREIGN KEY (card_id,face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id,face_id))");
   aDatabase.ExecuteCommand(
      "CREATE INDEX ix_scryfall_card_image_variants_asset ON deckkernel_test.scryfall_card_image_variants(asset_id)");
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_released ON deckkernel_test.scryfall_cards(released_at DESC)");
   aDatabase.ExecuteCommand(
      "CREATE VIEW deckkernel_test.scryfall_cards_view AS "
      "SELECT c.*, r.code AS rarity, f.face_id, f.is_main, f.name AS face_name, f.type_line, "
      "f.layout, f.loyalty, f.mana_cost, f.power, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_supertypes x "
      "JOIN deckkernel_test.supertypes t ON t.id=x.supertype_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS supertypes, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_main_types x "
      "JOIN deckkernel_test.card_types t ON t.id=x.main_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS main_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_artifact_types x "
      "JOIN deckkernel_test.artifact_types t ON t.id=x.artifact_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS artifact_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_land_types x "
      "JOIN deckkernel_test.land_types t ON t.id=x.land_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS land_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_creature_types x "
      "JOIN deckkernel_test.creatures_types t ON t.id=x.creature_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS creature_types "
      "FROM deckkernel_test.scryfall_cards c "
      "JOIN deckkernel_test.scryfall_card_faces f ON f.card_id=c.id "
      "JOIN deckkernel_test.rarities r ON r.id=c.rarity_id");
   aCardTransaction.CommitAndClose();
   // One original blob per URL in the resettable model, plus normalized per-face
   // image-variant links. Multiple printings may reference the same URL.
   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.scryfall_image_assets(image_url,image_kind,illustration_id) "
      "SELECT DISTINCT ON (v.url) v.url,v.variant,f.illustration_id "
      "FROM deckkernel_test.scryfall_card_faces f "
      "CROSS JOIN LATERAL (VALUES "
      "('small',f.image_url_small),('normal',f.image_url_normal),"
      "('large',f.image_url_large),('png',f.image_url_png),"
      "('art_crop',f.image_url_art_crop),('border_crop',f.image_url_border_crop)"
      ") v(variant,url) "
      "WHERE v.url IS NOT NULL "
      "ORDER BY v.url,f.illustration_id NULLS LAST "
      "ON CONFLICT (image_url) DO NOTHING");
   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.scryfall_card_image_variants(card_id,face_id,variant,asset_id) "
      "SELECT f.card_id,f.face_id,v.variant,a.id "
      "FROM deckkernel_test.scryfall_card_faces f "
      "CROSS JOIN LATERAL (VALUES "
      "('small',f.image_url_small),('normal',f.image_url_normal),"
      "('large',f.image_url_large),('png',f.image_url_png),"
      "('art_crop',f.image_url_art_crop),('border_crop',f.image_url_border_crop)"
      ") v(variant,url) "
      "JOIN deckkernel_test.scryfall_image_assets a ON a.image_url=v.url "
      "WHERE v.url IS NOT NULL");
   std::println("[STORE] Model image catalog created: per-card face/variant links and unique URLs");

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
