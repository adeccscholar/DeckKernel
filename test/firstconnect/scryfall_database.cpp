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

   // FirstConnection is a fresh-install-only schema creator.
   // Refuse to destroy or replace any pre-existing model relation.
   auto const aModelCheck = aDatabase.ExecuteOne<int>(
      "SELECT CASE WHEN EXISTS ("
      "SELECT 1 FROM pg_class c "
      "JOIN pg_namespace n ON n.oid = c.relnamespace "
      "WHERE n.nspname = 'deckkernel_test' "
      "AND c.relkind IN ('r','p','v','m','f')"
      ") THEN 1 ELSE 0 END");
   if(!aModelCheck || std::get<0>(*aModelCheck) != 0) {
      throw std::runtime_error(
         "Existing model detected: FirstConnection --all refuses destructive reset. "
         "Run the schema migration and use non-destructive test modes.");
      }
   // The Scryfall image rendition catalog is independent of the card-face URL
   // columns; their normalization is a separate, data-preserving migration.
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.\"dom_card_face_image_types\" ("
      "id integer GENERATED ALWAYS AS IDENTITY, "
      "code text NOT NULL, description text, notice text, resolution text, "
      "offline boolean NOT NULL DEFAULT FALSE, "
      "CONSTRAINT \"pk_dom_card_face_image_types\" PRIMARY KEY (id), "
      "CONSTRAINT \"uk_dom_card_face_image_types_code\" UNIQUE (code))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.dom_layout_types ("
      "id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, "
      "description text, notice text, "
      "CONSTRAINT pk_dom_layout_types PRIMARY KEY (id), "
      "CONSTRAINT uk_dom_layout_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.dom_price_types ("
      "id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, "
      "currency text NOT NULL, description text, notices text, "
      "CONSTRAINT pk_dom_price_types PRIMARY KEY (id), "
      "CONSTRAINT uk_dom_price_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.dom_card_finish_types ("
      "id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, "
      "description text, notice text, "
      "CONSTRAINT pk_dom_card_finish_types PRIMARY KEY (id), "
      "CONSTRAINT uk_dom_card_finish_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.format_types ("
      "id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, "
      "description text, notice text, "
      "CONSTRAINT pk_format_types PRIMARY KEY (id), "
      "CONSTRAINT uk_format_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.dom_legality_types ("
      "id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, "
      "description text, notice text, "
      "CONSTRAINT pk_dom_legality_types PRIMARY KEY (id), "
      "CONSTRAINT uk_dom_legality_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.dom_land_types (id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, description text, notice text, CONSTRAINT pk_dom_land_types PRIMARY KEY (id), CONSTRAINT uk_dom_land_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.dom_creature_types (id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, description text, notice text, CONSTRAINT pk_dom_creature_types PRIMARY KEY (id), CONSTRAINT uk_dom_creature_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.dom_artifact_types (id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, description text, notice text, CONSTRAINT pk_dom_artifact_types PRIMARY KEY (id), CONSTRAINT uk_dom_artifact_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.dom_supertypes (id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, description text, notice text, CONSTRAINT pk_dom_supertypes PRIMARY KEY (id), CONSTRAINT uk_dom_supertypes_code UNIQUE (code))");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.dom_card_types (id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, description text, notice text, CONSTRAINT pk_dom_card_types PRIMARY KEY (id), CONSTRAINT uk_dom_card_types_code UNIQUE (code))");
   aDatabase.ExecuteCommand("CREATE TABLE deckkernel_test.dom_rarities (id integer GENERATED ALWAYS AS IDENTITY, code text NOT NULL, description text, notice text, CONSTRAINT pk_dom_rarities PRIMARY KEY (id), CONSTRAINT uk_dom_rarities_code UNIQUE (code))");
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
      "rarity_id integer NOT NULL REFERENCES deckkernel_test.dom_rarities(id),"
      "collector_number text NOT NULL, lang text NOT NULL,"
      "illustration_id text)");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_faces ("
      "card_id integer NOT NULL REFERENCES deckkernel_test.scryfall_cards(id),"
      "face_id integer NOT NULL CHECK (face_id >= 0),"
      "is_main boolean GENERATED ALWAYS AS (face_id = 0) STORED,"
      "name text NOT NULL, type_line text NOT NULL,"
      "layout text, layout_id integer REFERENCES deckkernel_test.dom_layout_types(id), "
      "loyalty text, mana_cost text NOT NULL, power text,"
      "illustration_id text, image_url_normal text, image_url_art_crop text, image_url_png text,"
      "image_url_small text, image_url_large text, image_url_border_crop text,"
      "PRIMARY KEY (card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_artifact_types ("
      "card_id integer NOT NULL, face_id integer NOT NULL,"
      "artifact_type_id integer NOT NULL REFERENCES deckkernel_test.dom_artifact_types(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, artifact_type_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_supertypes ("
      "card_id integer NOT NULL, face_id integer NOT NULL,"
      "supertype_id integer NOT NULL REFERENCES deckkernel_test.dom_supertypes(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, supertype_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_main_types ("
      "card_id integer NOT NULL, face_id integer NOT NULL,"
      "main_type_id integer NOT NULL REFERENCES deckkernel_test.dom_card_types(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, main_type_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_land_types ("
      "card_id integer NOT NULL,"
      "face_id integer NOT NULL,"
      "land_type_id integer NOT NULL REFERENCES deckkernel_test.dom_land_types(id),"
      "position integer NOT NULL CHECK (position >= 0),"
      "PRIMARY KEY (card_id, face_id, position),"
      "UNIQUE (card_id, face_id, land_type_id),"
      "FOREIGN KEY (card_id, face_id) REFERENCES deckkernel_test.scryfall_card_faces(card_id, face_id))");
   aDatabase.ExecuteCommand(
      "CREATE TABLE deckkernel_test.scryfall_card_creature_types ("
      "card_id integer NOT NULL,"
      "face_id integer NOT NULL,"
      "creature_type_id integer NOT NULL REFERENCES deckkernel_test.dom_creature_types(id),"
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

/**
\brief Synchronizes one code catalog without modifying existing values.
\details Locks the domain table for this transaction, reads existing identities,
and inserts only codes absent from the database. New description and notice
columns are intentionally omitted, so PostgreSQL stores NULL. All original
IDs and all existing descriptions and notices remain unchanged.
\tparam catalog_ty Persistent catalog record type.
\param aDatabase Open database with an active transaction.
\param mpSource Codes to synchronize.
\param svTableName Fully qualified, compile-time defined domain table name.
\returns Mapping from every source code to its preserved or newly created ID.
\throw std::runtime_error If database identities or inserted rows are invalid.
*/
template <typename catalog_ty>
std::map<std::string, int> SynchronizeCatalog(
   postgres_database_ty& aDatabase,
   std::map<std::string, int> const& mpSource,
   std::string_view const svTableName) {

   aDatabase.ExecuteCommand(std::format(
      "LOCK TABLE {} IN SHARE ROW EXCLUSIVE MODE", svTableName));

   std::map<std::string, int> mpIds;
   for (auto const& [iId, strCode] :
      aDatabase.Execute<int, std::string>(
         std::format("SELECT id,code FROM {}", svTableName))) {
      if (iId <= 0 || !mpIds.emplace(strCode, iId).second) {
         throw std::runtime_error(
            std::format("Invalid or duplicate existing catalog key in {}", svTableName));
         }
      }

   auto aOutput = catalog_types::invoke([&]<typename... types_ty>() {
      return aDatabase.template MakeOutputRange<types_ty...>(
         catalog_ty::CreateInsertSql(), catalog_ty::CreateInsertOutputParameters());
      });

   std::size_t uNew = 0;
   std::size_t uExisting = 0;
   for (auto const& [strCode, iUnused] : mpSource) {
      if (mpIds.contains(strCode)) {
         ++uExisting;
         continue;
         }

      std::array<catalog_types::type_list, 1> arrInput{{
         catalog_types::type_list{0, strCode}
         }};
      bool boInserted = false;
      for (auto const& tplResult : aOutput(arrInput)) {
         int const iId = std::get<0>(tplResult);
         if (boInserted || iId <= 0 || !mpIds.emplace(strCode, iId).second) {
            throw std::runtime_error(
               std::format("Invalid generated catalog identity in {}", svTableName));
            }
         boInserted = true;
         ++uNew;
         }
      if (!boInserted) {
         throw std::runtime_error(
            std::format("Missing INSERT result for code '{}' in {}", strCode, svTableName));
         }
      }

   std::println("[DOMAIN] {}: existing={}, inserted={}, total={}",
      svTableName, uExisting, uNew, mpIds.size());
   return mpIds;
   }


struct DomainCatalogIds {
   std::map<std::string, int> mpLandIds;
   std::map<std::string, int> mpCreatureIds;
   std::map<std::string, int> mpArtifactIds;
   std::map<std::string, int> mpSuperIds;
   std::map<std::string, int> mpMainIds;
   std::map<std::string, int> mpRarityIds;
   };


/**
\brief Inserts missing domain values while preserving all existing domain records.
\details Synchronizes all six Scryfall catalogs and the six fixed image rendition
codes in one transaction. Existing descriptions, notices, resolutions, and IDs
are never changed. New catalog descriptions and notices are NULL.
\param aDatabase Connected PostgreSQL database.
\param aParsed Parsed Scryfall domain catalogs.
\returns Existing and new IDs for references created by the card importer.
\throw std::runtime_error On missing schema, conflicts, or invalid identifiers.
*/
DomainCatalogIds SynchronizeDomainCatalogs(
   postgres_database_ty& aDatabase, ParsedBulkData const& aParsed) {
   auto aCatalogTransaction = aDatabase.Transaction();

   DomainCatalogIds aIds{
      .mpLandIds = SynchronizeCatalog<TLandType>(
         aDatabase, aParsed.mpLandTypes, LandTypeMetaData::svTableName),
      .mpCreatureIds = SynchronizeCatalog<TCreatureType>(
         aDatabase, aParsed.mpCreatureTypes, CreatureTypeMetaData::svTableName),
      .mpArtifactIds = SynchronizeCatalog<TArtifactType>(
         aDatabase, aParsed.mpArtifactTypes, ArtifactTypeMetaData::svTableName),
      .mpSuperIds = SynchronizeCatalog<TSuperType>(
         aDatabase, aParsed.mpSuperTypes, SuperTypeMetaData::svTableName),
      .mpMainIds = SynchronizeCatalog<TMainType>(
         aDatabase, aParsed.mpMainTypes, MainTypeMetaData::svTableName),
      .mpRarityIds = SynchronizeCatalog<TRarity>(
         aDatabase, std::map<std::string, int>{
            {"common", 0}, {"uncommon", 0}, {"rare", 0},
            {"mythic", 0}, {"special", 0}, {"bonus", 0}
            }, RarityMetaData::svTableName)
      };

   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.format_types (code,description,notice) VALUES "
      "('standard','Standard: a rotating constructed format using the current Standard-legal card pool.',NULL),"
      "('future','Future Standard: projected Standard legality based on upcoming releases and rotation.',NULL),"
      "('historic','Historic: a non-rotating Magic Arena format including digital-only and rebalanced cards.',NULL),"
      "('timeless','Timeless: a non-rotating Magic Arena format with an extensive card pool and its own restrictions.',NULL),"
      "('gladiator','Gladiator: a 100-card singleton Magic Arena constructed format.',NULL),"
      "('pioneer','Pioneer: a non-rotating constructed format with a card pool beginning with Return to Ravnica.',NULL),"
      "('modern','Modern: a non-rotating constructed format whose eligible sets begin with Eighth Edition.',NULL),"
      "('legacy','Legacy: an Eternal constructed format with its own banned-card list.',NULL),"
      "('pauper','Pauper: a constructed format based on cards printed at common rarity, subject to a banned list.',NULL),"
      "('vintage','Vintage: an Eternal format that distinguishes banned and restricted cards.',NULL),"
      "('penny','Penny Dreadful: a community format with seasonally changing eligibility based on Magic Online prices.',NULL),"
      "('commander','Commander: a singleton multiplayer format using a commander, color identity and a banned list.',NULL),"
      "('oathbreaker','Oathbreaker: a singleton format using a planeswalker Oathbreaker and its signature spell.',NULL),"
      "('standardbrawl','Standard Brawl: a singleton Brawl variant using the rotating Standard card pool.',NULL),"
      "('brawl','Brawl: a commander-style singleton format tracked separately from Standard Brawl.',NULL),"
      "('competitivebrawl','Competitive Brawl: a separately tracked competitive variant of Brawl.',NULL),"
      "('alchemy','Alchemy: a rotating Magic Arena format using digital-only and rebalanced cards as well as eligible paper cards.',NULL),"
      "('paupercommander','Pauper Commander: a Commander variant using common cards and an eligible uncommon creature commander.',NULL),"
      "('duel','Duel Commander: a one-versus-one competitive Commander variant.',NULL),"
      "('oldschool','Old School (93/94): a constructed format centered on the earliest Magic releases.',NULL),"
      "('premodern','Premodern: a constructed format using an early historical card pool, from Fourth Edition through Scourge.',NULL),"
      "('predh','PreDH: a Commander variant using a historically limited card pool predating modern Commander releases.',NULL),"
      "('tlr','Tiny Leaders Reborn: a separately tracked singleton format with low mana-value restrictions.',NULL) "
      "ON CONFLICT (code) DO NOTHING");

   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.dom_legality_types (code,description,notice) VALUES "
      "('legal','The card is permitted in the format under the usual deck-construction rules.',NULL),"
      "('not_legal','The card does not qualify for the format''s eligible card pool; this is distinct from being banned.',NULL),"
      "('banned','The card is explicitly prohibited in the format by its ban list or governing rules.',NULL),"
      "('restricted','The card is permitted with a reduced copy limit under the format''s restricted list.',NULL) "
      "ON CONFLICT (code) DO NOTHING");

   // Seed the six published Scryfall price keys without changing existing values.
   // Price quotes themselves are not persisted by this catalog-only change.
   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.dom_price_types (code,currency,description,notices) VALUES "
      "('usd','USD','Indicative nonfoil paper price in US dollars, typically sourced from the US card market.',NULL),"
      "('usd_foil','USD','Indicative traditional foil paper price in US dollars.',NULL),"
      "('usd_etched','USD','Indicative etched-foil paper price in US dollars.',NULL),"
      "('eur','EUR','Indicative nonfoil paper price in euros, typically sourced from the European card market.',NULL),"
      "('eur_foil','EUR','Indicative foil paper price in euros.',NULL),"
      "('tix','TIX','Indicative Magic Online digital card price in MTGO event tickets, not a fiat currency.',NULL) "
      "ON CONFLICT (code) DO NOTHING");

   // The finish domain is additive and independent of the existing price types.
   // Keep curated descriptions and all IDs whenever a code already exists.
   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.dom_card_finish_types (code,description,notice) VALUES "
      "('nonfoil','A printing without a foil or reflective metallic finish. Availability of this finish is a property of the specific card printing.',NULL),"
      "('foil','A printing with a traditional reflective foil finish. It is distinct from etched foil and does not imply a particular rarity.',NULL),"
      "('etched','A printing with an etched-foil treatment, distinct from the traditional foil finish. Availability is specific to the printing.',NULL),"
      "('glossy','A specialty printing with a glossy surface treatment. Scryfall distinguishes glossy from nonfoil, traditional foil, and etched foil.',NULL) "
      "ON CONFLICT (code) DO NOTHING");

   // A fixed catalog: retain previously authored metadata and nominal resolution.
   // Only missing codes are inserted, with NULL description and notice.
   // Six already-imported Scryfall image_uris variants start enabled for offline use.
   // Six additional Scryfall image_uris variants remain offline-disabled until imported.
   // Existing domain rows and manually selected offline values are preserved.
   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.dom_card_face_image_types "
      "(code,resolution,offline,description,notice) VALUES "
      "('art_crop',NULL,TRUE,'A crop of card artwork without the card frame or text.',NULL),"
      "('border_crop','480x680',TRUE,'A full card JPEG with most outer borders removed.',NULL),"
      "('large','672x936',TRUE,'A large JPEG rendition of the full card.',NULL),"
      "('normal','488x680',TRUE,'A standard JPEG rendition of the full card.',NULL),"
      "('png','745x1040',TRUE,'A high-resolution PNG of the full card.',NULL),"
      "('small','146x204',TRUE,'A small JPEG thumbnail of the full card.',NULL),"
      "('grid',NULL,FALSE,'A Scryfall WebP card image for compact grid and gallery views (image_uris.grid).',NULL),"
      "('display',NULL,FALSE,'A Scryfall WebP card image for detailed display (image_uris.display).',NULL),"
      "('art',NULL,FALSE,'A Scryfall WebP artwork image distinct from JPEG art_crop (image_uris.art).',NULL),"
      "('jpeg',NULL,FALSE,'A Scryfall JPEG card image (image_uris.jpeg), aliasing normal in the observed export.',NULL),"
      "('thumb',NULL,FALSE,'A Scryfall WebP thumbnail image (image_uris.thumb).',NULL),"
      "('crop',NULL,FALSE,'A Scryfall WebP crop image (image_uris.crop).',NULL) "
      "ON CONFLICT (code) DO NOTHING");

   // On fresh installations seed the documented layout descriptions as well.
   // This is additive: previously curated descriptions are never overwritten.
   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.dom_layout_types (code,description,notice) VALUES "
      "('normal','A standard Magic card layout with a single card face.',NULL),"
      "('split','A card whose front is divided into two spell halves, each with its own characteristics.',NULL),"
      "('flip','A single-sided card with two vertically opposed orientations that can be flipped.',NULL),"
      "('transform','A double-faced card that can transform between its front and back faces.',NULL),"
      "('modal_dfc','A modal double-faced card whose front or back face can be played, subject to applicable rules.',NULL),"
      "('meld','A card used in a meld pair; the reverse side depicts part of the combined melded permanent.',NULL),"
      "('leveler','A card with level up and a level-based text box showing different characteristics at different levels.',NULL),"
      "('class','A Class enchantment with levels that can be unlocked to gain additional abilities.',NULL),"
      "('case','A Case enchantment that can be solved by fulfilling a stated condition, unlocking an additional effect.',NULL),"
      "('saga','A Saga enchantment with chapter abilities tied to lore counters.',NULL),"
      "('adventure','A card combining an Adventure spell with another spell or permanent card face.',NULL),"
      "('mutate','A card with the mutate mechanic and its distinctive alternate casting presentation.',NULL),"
      "('prototype','A card showing both a normal cost and a prototype cost with alternate power and toughness.',NULL),"
      "('battle','A Battle card layout with defense and rules associated with defending and defeating it.',NULL),"
      "('planar','A Plane or Phenomenon card used in the Planechase multiplayer variant.',NULL),"
      "('scheme','A Scheme card used by the archenemy in the Archenemy multiplayer variant.',NULL),"
      "('vanguard','A Vanguard avatar card used to modify a player''s starting conditions or abilities.',NULL),"
      "('token','A printed representation of a token object, rather than a conventional deck card.',NULL),"
      "('double_faced_token','A double-faced token card presenting a token representation on each side.',NULL),"
      "('emblem','A printed representation of an emblem created by a card effect; an emblem is not a card or permanent.',NULL),"
      "('augment','An Augment card intended to combine with a Host creature in the Unstable environment.',NULL),"
      "('host','A Host creature card that can combine with an Augment card in the Unstable environment.',NULL),"
      "('art_series','A collectible art-series card displaying artwork, rather than a regular playable card.',NULL),"
      "('reversible_card','A reversible double-faced card with independently identifiable, unrelated front and back faces.',NULL) "
      "ON CONFLICT (code) DO NOTHING");

   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.dom_layout_types (code,description,notice) "
      "VALUES ('prepare','A card layout featuring a creature and its associated prepared spell. The creature can become prepared, allowing its controller to cast a copy of the associated spell under the preparation rules.',NULL) ON CONFLICT (code) DO NOTHING");
   aDatabase.ExecuteCommand(
      "UPDATE deckkernel_test.dom_layout_types SET description='A card layout featuring a creature and its associated prepared spell. The creature can become prepared, allowing its controller to cast a copy of the associated spell under the preparation rules.' "
      "WHERE code='prepare' AND description IS NULL");
   aDatabase.ExecuteCommand(
      "INSERT INTO deckkernel_test.dom_layout_types (code,description,notice) "
      "VALUES ('front_card','A nontraditional card layout used for introductory or theme-identification cards, such as Jumpstart front cards. These cards identify a pack or theme rather than representing a conventional playable Magic card.',NULL) ON CONFLICT (code) DO NOTHING");
   aDatabase.ExecuteCommand(
      "UPDATE deckkernel_test.dom_layout_types SET description='A nontraditional card layout used for introductory or theme-identification cards, such as Jumpstart front cards. These cards identify a pack or theme rather than representing a conventional playable Magic card.' "
      "WHERE code='front_card' AND description IS NULL");

   // Register every non-empty layout seen on any card face.
   // The Scryfall API may add layout codes without requiring a schema change.
   std::set<std::string> setLayouts;
   for (StagedCard const& aCard : aParsed.vecCards) {
      for (StagedFace const& aFace : aCard.vecFaces) {
         if (aFace.optLayout && !aFace.optLayout->empty()) {
            setLayouts.emplace(*aFace.optLayout);
            }
         }
      }
   aDatabase.ExecuteCommand(
      "LOCK TABLE deckkernel_test.dom_layout_types IN SHARE ROW EXCLUSIVE MODE");
   std::set<std::string> setExisting;
   for (auto const& tplRow : aDatabase.Execute<std::string>(
      "SELECT code FROM deckkernel_test.dom_layout_types")) {
      setExisting.emplace(std::get<0>(tplRow));
      }
   std::size_t uAddedLayouts = 0;
   for (std::string const& strCode : setLayouts) {
      if (setExisting.contains(strCode)) { continue; }
      // Reuse the same safe SQL quoting strategy as the existing code-based
      // catalog loader, preserving the exact Scryfall value.
      std::string strQuotedCode;
      strQuotedCode.reserve(strCode.size() + 2);
      strQuotedCode += '\'';
      for (char const ch : strCode) {
         strQuotedCode += ch;
         if (ch == '\'') { strQuotedCode += '\''; }
         }
      strQuotedCode += '\'';
      aDatabase.ExecuteCommand(std::format(
         "INSERT INTO deckkernel_test.dom_layout_types "
         "(code,description,notice) VALUES ({},NULL,NULL) "
         "ON CONFLICT (code) DO NOTHING", strQuotedCode));
      ++uAddedLayouts;
      }
   std::println("[DOMAIN] dom_layout_types: existing={}, added={}, source={}",
      setExisting.size(), uAddedLayouts, setLayouts.size());

   aCatalogTransaction.CommitAndClose();
   return aIds;
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

/**
\brief Updates the existing domain tables without changing cards, sets, or images.
\details Uses the same safe code/identity synchronization as a fresh import.
\param aDatabase Connected PostgreSQL database.
\param aParsed Parsed domain codes.
\returns Nothing.
\throw std::runtime_error If a domain table is missing or synchronization fails.
*/
void UpdateDomainValues(postgres_database_ty& aDatabase, ParsedBulkData const& aParsed) {
   (void)SynchronizeDomainCatalogs(aDatabase, aParsed);
   }


void StoreProcess(postgres_database_ty& aDatabase, ParsedBulkData const& aParsed) {
   // DDL is committed independently: a later data error must not restore an obsolete schema.
   EnsureSchema(aDatabase);
   std::println("[STORE] Fresh schema created; domain catalogs will be synchronized");
   // Resolve existing code identities first; insert only previously unknown values.
   // Never overwrite descriptions/notices maintained in the domain tables.
   DomainCatalogIds const aDomainIds = SynchronizeDomainCatalogs(aDatabase, aParsed);
   auto const& mpLandIds = aDomainIds.mpLandIds;
   auto const& mpCreatureIds = aDomainIds.mpCreatureIds;
   auto const& mpArtifactIds = aDomainIds.mpArtifactIds;
   auto const& mpSuperIds = aDomainIds.mpSuperIds;
   auto const& mpMainIds = aDomainIds.mpMainIds;
   auto const& mpRarityIds = aDomainIds.mpRarityIds;

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
   // Preserve layout TEXT as a reversible legacy copy while establishing the FK.
   // The domain synchronization has already inserted all observed layouts.
   aDatabase.ExecuteCommand(
      "UPDATE deckkernel_test.scryfall_card_faces f "
      "SET layout_id=d.id FROM deckkernel_test.dom_layout_types d "
      "WHERE f.layout=d.code AND f.layout_id IS NULL");
   auto const aMissingLayout = aDatabase.ExecuteOne<int>(
      "SELECT CASE WHEN EXISTS (SELECT 1 FROM deckkernel_test.scryfall_card_faces "
      "WHERE layout IS NOT NULL AND layout <> '' AND layout_id IS NULL) "
      "THEN 1 ELSE 0 END");
   if(!aMissingLayout || std::get<0>(*aMissingLayout) != 0) {
      throw std::runtime_error("Unresolved card-face layout references");
      }


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
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_card_faces_layout_id ON deckkernel_test.scryfall_card_faces(layout_id)");
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
      "JOIN deckkernel_test.dom_supertypes t ON t.id=x.supertype_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS supertypes, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_main_types x "
      "JOIN deckkernel_test.dom_card_types t ON t.id=x.main_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS main_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_artifact_types x "
      "JOIN deckkernel_test.dom_artifact_types t ON t.id=x.artifact_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS artifact_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_land_types x "
      "JOIN deckkernel_test.dom_land_types t ON t.id=x.land_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS land_types, "
      "COALESCE((SELECT array_agg(t.code ORDER BY x.position) "
      "FROM deckkernel_test.scryfall_card_creature_types x "
      "JOIN deckkernel_test.dom_creature_types t ON t.id=x.creature_type_id "
      "WHERE x.card_id=c.id AND x.face_id=f.face_id), ARRAY[]::text[]) AS creature_types "
      "FROM deckkernel_test.scryfall_cards c "
      "JOIN deckkernel_test.scryfall_card_faces f ON f.card_id=c.id "
      "JOIN deckkernel_test.dom_rarities r ON r.id=c.rarity_id");
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
