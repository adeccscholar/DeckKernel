// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_postgres_test.cpp
\brief End-to-end Scryfall bulk-data, PostgreSQL, and adecc core functional test.
\details
Downloads Scryfall default card bulk data over HTTPS, parses the card stream, stores the
minimal card and set model through PersistentSystemData and database output sinks, reads
the newest 20 cards through the typed PostgreSQL query range, and transfers the result
directly into the text-grid abstraction.

The test intentionally exercises curl, OpenSSL, nlohmann/json, zlib, libpq/libpqxx,
the PostgreSQL adapter, PersistentSystemData, typed database ranges, output sinks, and
the backend-neutral text grid in one small program.

\author Volker Hillmann (adecc Systemhaus GmbH)
\date 26.09.2026
*/

#include "postgres_pqxx_database.h"

#include "convert_integral.h"
#include "database.h"
#include "system_data_persistent.h"
#include "text_grid_wrapper.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace deckkernel::test {

using json_ty = nlohmann::json;

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
   };


using postgres_database_ty = adecc::db::logical_database<
   adecc::db::postgres::postgres_database,
   adecc::db::postgres::fw_query
   >;


/**
\brief Description of the selected Scryfall bulk download.
*/
struct BulkDescriptor {
   std::string strDownloadUri;
   std::string strUpdatedAt;
   bool boGzipJsonLines{ false };
   };


/**
\brief Owns global curl initialization for the lifetime of the test.
*/
class CurlRuntime final {
public:
   CurlRuntime() {
      CURLcode const iResult = curl_global_init(CURL_GLOBAL_DEFAULT);

      if (iResult != CURLE_OK) {
         throw std::runtime_error{
            std::format("curl_global_init failed: {}", curl_easy_strerror(iResult))
            };
         }
      }

   CurlRuntime(CurlRuntime const&) = delete;
   CurlRuntime& operator=(CurlRuntime const&) = delete;

   ~CurlRuntime() {
      curl_global_cleanup();
      }
   };


std::string Environment(char const* const szName, std::string_view const svFallback) {
   if (char const* const szValue = std::getenv(szName); szValue != nullptr && *szValue != '\0') {
      return std::string{ szValue };
      }

   return std::string{ svFallback };
   }


bool EnvironmentFlag(
   char const* const szName,
   bool const boFallback
) {
   std::string const strValue = Environment(
      szName,
      boFallback ? "true" : "false"
      );

   return strValue == "1" ||
          strValue == "true" ||
          strValue == "yes" ||
          strValue == "on";
   }


std::size_t WriteString(
   char* pData,
   std::size_t const uSize,
   std::size_t const uCount,
   void* pUser
) {
   std::size_t const uBytes = uSize * uCount;
   auto* const pTarget = static_cast<std::string*>(pUser);
   pTarget->append(pData, uBytes);
   return uBytes;
   }


std::size_t WriteFile(
   char* pData,
   std::size_t const uSize,
   std::size_t const uCount,
   void* pUser
) {
   std::size_t const uBytes = uSize * uCount;
   auto* const pStream = static_cast<std::ofstream*>(pUser);
   pStream->write(pData, static_cast<std::streamsize>(uBytes));

   return *pStream ? uBytes : 0U;
   }


void ConfigureCurl(
   CURL* const pCurl,
   std::string const& strUrl,
   curl_slist* const pHeaders
) {
   if (pCurl == nullptr) {
      throw std::runtime_error{ "curl_easy_init returned null" };
      }

   curl_easy_setopt(pCurl, CURLOPT_URL, strUrl.c_str());
   curl_easy_setopt(pCurl, CURLOPT_FOLLOWLOCATION, 1L);
   curl_easy_setopt(pCurl, CURLOPT_FAILONERROR, 1L);
   curl_easy_setopt(pCurl, CURLOPT_USERAGENT, "DeckKernel/0.1 Scryfall functional test");
   curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, pHeaders);
   curl_easy_setopt(pCurl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);
   curl_easy_setopt(pCurl, CURLOPT_PROTOCOLS_STR, "https");
   }


std::string HttpGet(std::string const& strUrl) {
   using curl_ptr_ty = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

   curl_ptr_ty upCurl{ curl_easy_init(), &curl_easy_cleanup };

   curl_slist* pRawHeaders = nullptr;
   pRawHeaders = curl_slist_append(
      pRawHeaders,
      "Accept: application/json;q=0.9,*/*;q=0.8"
      );

   using header_ptr_ty = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
   header_ptr_ty upHeaders{ pRawHeaders, &curl_slist_free_all };

   ConfigureCurl(upCurl.get(), strUrl, upHeaders.get());

   std::string strResult;
   curl_easy_setopt(upCurl.get(), CURLOPT_WRITEFUNCTION, &WriteString);
   curl_easy_setopt(upCurl.get(), CURLOPT_WRITEDATA, &strResult);

   CURLcode const iResult = curl_easy_perform(upCurl.get());

   if (iResult != CURLE_OK) {
      throw std::runtime_error{
         std::format("HTTPS request failed for {}: {}", strUrl, curl_easy_strerror(iResult))
         };
      }

   return strResult;
   }


void DownloadFile(std::string const& strUrl, std::filesystem::path const& aTarget) {
   using curl_ptr_ty = std::unique_ptr<CURL, decltype(&curl_easy_cleanup)>;

   std::ofstream osFile{ aTarget, std::ios::binary | std::ios::trunc };

   if (!osFile) {
      throw std::runtime_error{
         std::format("cannot create bulk-data file '{}'", aTarget.string())
         };
      }

   curl_ptr_ty upCurl{ curl_easy_init(), &curl_easy_cleanup };

   curl_slist* pRawHeaders = nullptr;
   pRawHeaders = curl_slist_append(
      pRawHeaders,
      "Accept: application/json;q=0.9,*/*;q=0.8"
      );

   using header_ptr_ty = std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)>;
   header_ptr_ty upHeaders{ pRawHeaders, &curl_slist_free_all };

   ConfigureCurl(upCurl.get(), strUrl, upHeaders.get());

   curl_easy_setopt(upCurl.get(), CURLOPT_WRITEFUNCTION, &WriteFile);
   curl_easy_setopt(upCurl.get(), CURLOPT_WRITEDATA, &osFile);

   CURLcode const iResult = curl_easy_perform(upCurl.get());

   if (iResult != CURLE_OK) {
      throw std::runtime_error{
         std::format("bulk download failed for {}: {}", strUrl, curl_easy_strerror(iResult))
         };
      }
   }


BulkDescriptor ResolveBulkData() {
   std::string const strMetadata = HttpGet("https://api.scryfall.com/bulk-data");
   json_ty const aRoot = json_ty::parse(strMetadata);

   for (json_ty const& aEntry : aRoot.at("data")) {
      if (aEntry.at("type").get<std::string>() != "default_cards") {
         continue;
         }

      BulkDescriptor aResult;
      aResult.strUpdatedAt = aEntry.value("updated_at", "");

      if (auto const it = aEntry.find("jsonl_download_uri");
          it != aEntry.end() && it->is_string()) {
         aResult.strDownloadUri = it->get<std::string>();
         aResult.boGzipJsonLines = aResult.strDownloadUri.ends_with(".gz");
         return aResult;
         }

      aResult.strDownloadUri = aEntry.at("download_uri").get<std::string>();
      aResult.boGzipJsonLines = aResult.strDownloadUri.ends_with(".gz");

      return aResult;
      }

   throw std::runtime_error{ "Scryfall default_cards bulk data was not found" };
   }


void AppendCard(
   json_ty const& aCard,
   std::map<std::string, TScryfallSet::data_ty>& mpSets,
   std::vector<TScryfallCard::data_ty>& vecCards
) {
   std::string const strId = aCard.at("id").get<std::string>();
   std::string const strSetId = aCard.at("set_id").get<std::string>();
   std::string const strSetCode = aCard.at("set").get<std::string>();
   std::string const strSetName = aCard.at("set_name").get<std::string>();
   std::string const strName = aCard.at("name").get<std::string>();
   std::string const strReleasedAt = aCard.at("released_at").get<std::string>();

   std::optional<std::string> optOracleId;

   if (auto const it = aCard.find("oracle_id");
       it != aCard.end() && !it->is_null()) {
      optOracleId = it->get<std::string>();
      }

   mpSets.try_emplace(
      strSetId,
      TScryfallSet::data_ty{
         strSetId,
         strSetCode,
         strSetName
         }
      );

   vecCards.emplace_back(
      strId,
      std::move(optOracleId),
      strName,
      strSetId,
      adecc::ConvertTo<adecc::date_ty>(strReleasedAt)
      );
   }


void ReadGzipJsonLines(
   std::filesystem::path const& aPath,
   std::map<std::string, TScryfallSet::data_ty>& mpSets,
   std::vector<TScryfallCard::data_ty>& vecCards
) {
   gzFile pFile = gzopen(aPath.string().c_str(), "rb");

   if (pFile == nullptr) {
      throw std::runtime_error{
         std::format("cannot open gzip bulk-data file '{}'", aPath.string())
         };
      }

   struct GzipGuard {
      gzFile pFile{};

      ~GzipGuard() {
         if (pFile != nullptr) {
            gzclose(pFile);
            }
         }
      } aGuard{ pFile };

   std::array<char, 64U * 1024U> arrBuffer{};
   std::string strPending;

   while (true) {
      int const iRead = gzread(
         pFile,
         arrBuffer.data(),
         static_cast<unsigned int>(arrBuffer.size())
         );

      if (iRead < 0) {
         int iError{};
         char const* const szError = gzerror(pFile, &iError);
         (void)iError;

         throw std::runtime_error{
            std::format("gzip read failed: {}", szError == nullptr ? "unknown error" : szError)
            };
         }

      if (iRead == 0) {
         break;
         }

      strPending.append(arrBuffer.data(), static_cast<std::size_t>(iRead));

      std::size_t uStart{};

      while (true) {
         std::size_t const uNewLine = strPending.find('\n', uStart);

         if (uNewLine == std::string::npos) {
            strPending.erase(0, uStart);
            break;
            }

         std::string_view const svLine{
            strPending.data() + uStart,
            uNewLine - uStart
            };

         if (!svLine.empty()) {
            AppendCard(json_ty::parse(svLine), mpSets, vecCards);
            }

         uStart = uNewLine + 1;
         }
      }

   if (!strPending.empty()) {
      AppendCard(json_ty::parse(strPending), mpSets, vecCards);
      }
   }


void ReadPlainBulk(
   std::filesystem::path const& aPath,
   std::map<std::string, TScryfallSet::data_ty>& mpSets,
   std::vector<TScryfallCard::data_ty>& vecCards
) {
   std::ifstream isFile{ aPath, std::ios::binary };

   if (!isFile) {
      throw std::runtime_error{
         std::format("cannot open bulk-data file '{}'", aPath.string())
         };
      }

   char chFirst{};

   while (isFile.get(chFirst)) {
      if (!std::isspace(static_cast<unsigned char>(chFirst))) {
         break;
         }
      }

   isFile.clear();
   isFile.seekg(0);

   if (chFirst == '[') {
      json_ty const aCards = json_ty::parse(isFile);

      for (json_ty const& aCard : aCards) {
         AppendCard(aCard, mpSets, vecCards);
         }

      return;
      }

   std::string strLine;

   while (std::getline(isFile, strLine)) {
      if (!strLine.empty()) {
         AppendCard(json_ty::parse(strLine), mpSets, vecCards);
         }
      }
   }


void ReadBulkData(
   std::filesystem::path const& aPath,
   bool const boGzipJsonLines,
   std::map<std::string, TScryfallSet::data_ty>& mpSets,
   std::vector<TScryfallCard::data_ty>& vecCards
) {
   if (boGzipJsonLines) {
      ReadGzipJsonLines(aPath, mpSets, vecCards);
      }
   else {
      ReadPlainBulk(aPath, mpSets, vecCards);
      }
   }


void EnsureSchema(postgres_database_ty const& aDatabase) {
   aDatabase.ExecuteCommand("CREATE SCHEMA IF NOT EXISTS deckkernel_test");

   aDatabase.ExecuteCommand(
      "CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_sets ("
      "id text PRIMARY KEY,"
      "code text NOT NULL UNIQUE,"
      "name text NOT NULL"
      ")"
      );

   aDatabase.ExecuteCommand(
      "CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_cards ("
      "id text PRIMARY KEY,"
      "oracle_id text NULL,"
      "name text NOT NULL,"
      "set_id text NOT NULL REFERENCES deckkernel_test.scryfall_sets(id),"
      "released_at date NOT NULL"
      ")"
      );

   aDatabase.ExecuteCommand(
      "CREATE INDEX IF NOT EXISTS ix_scryfall_cards_oracle_id "
      "ON deckkernel_test.scryfall_cards(oracle_id)"
      );

   aDatabase.ExecuteCommand(
      "CREATE INDEX IF NOT EXISTS ix_scryfall_cards_released "
      "ON deckkernel_test.scryfall_cards(released_at DESC)"
      );
   }


void StoreBulkData(
   postgres_database_ty& aDatabase,
   std::map<std::string, TScryfallSet::data_ty> const& mpSets,
   std::vector<TScryfallCard::data_ty> const& vecCards
) {
   std::vector<TScryfallSet::data_ty> vecSets;
   vecSets.reserve(mpSets.size());

   std::ranges::transform(
      mpSets,
      std::back_inserter(vecSets),
      [](auto const& aEntry) {
         return aEntry.second;
         }
      );

   auto aTransaction = aDatabase.Transaction();

   aDatabase.ExecuteCommand(
      "TRUNCATE TABLE deckkernel_test.scryfall_cards, "
      "deckkernel_test.scryfall_sets"
      );

   auto aSetSink = aDatabase.template MakeOutputSink<
      std::string,
      std::string,
      std::string
      >(
         TScryfallSet::CreateInsertSql(),
         TScryfallSet::CreateInsertOutputParameters()
         );

   aSetSink = vecSets;

   auto aCardSink = aDatabase.template MakeOutputSink<
      std::string,
      std::optional<std::string>,
      std::string,
      std::string,
      adecc::date_ty
      >(
         TScryfallCard::CreateInsertSql(),
         TScryfallCard::CreateInsertOutputParameters()
         );

   aCardSink = vecCards;

   aTransaction.Commit();
   }


void ShowLatestCards(postgres_database_ty const& aDatabase) {
   using backend_ty = adecc::grid::OStreamGridBackend<adecc::AnsiStreamPolicy>;
   using grid_ty = adecc::grid::WriteGridModel<backend_ty>;

   adecc::vecCaptions<adecc::AnsiStreamPolicy> const vecCaptions {
      { "Name", 54, adecc::EAlignmentType::left },
      { "Set", 40, adecc::EAlignmentType::left }
      };

   backend_ty aBackend{
      std::cout,
      backend_ty::TableSeparators()
      };

   grid_ty aGrid{
      std::move(aBackend),
      vecCaptions
      };

   auto rngLatest = aDatabase.template Execute<std::string, std::string>(
      "SELECT c.name, s.name "
      "FROM deckkernel_test.scryfall_cards c "
      "JOIN deckkernel_test.scryfall_sets s ON s.id = c.set_id "
      "ORDER BY c.released_at DESC, c.name, c.id "
      "LIMIT 20"
      );

   aGrid = rngLatest;
   }


void ShowLatestCardsThroughSink(postgres_database_ty const& aDatabase) {
   using backend_ty = adecc::grid::OStreamGridBackend<adecc::AnsiStreamPolicy>;
   using grid_ty = adecc::grid::WriteGridModel<backend_ty>;

   adecc::vecCaptions<adecc::AnsiStreamPolicy> const vecCaptions {
      { "Name", 54, adecc::EAlignmentType::left },
      { "Set", 40, adecc::EAlignmentType::left }
      };

   backend_ty aBackend{
      std::cout,
      backend_ty::CompactTableSeparators()
      };

   grid_ty aGrid{
      std::move(aBackend),
      vecCaptions
      };

   auto rngLatest = aDatabase.template Execute<std::string, std::string>(
      "SELECT c.name, s.name "
      "FROM deckkernel_test.scryfall_cards c "
      "JOIN deckkernel_test.scryfall_sets s ON s.id = c.set_id "
      "ORDER BY c.released_at DESC, c.name, c.id "
      "LIMIT 20"
      );

   std::ranges::copy(
      rngLatest,
      aGrid.template sink<std::string, std::string>()
      );
   }


adecc::db::postgres::postgres_credentials MakeCredentials() {
   adecc::db::postgres::postgres_credentials aCredentials;

   aCredentials.strHost = Environment("DECKKERNEL_PGHOST", "localhost");
   aCredentials.uPort = adecc::ConvertTo<std::uint16_t>(
      Environment("DECKKERNEL_PGPORT", "5432")
      );
   aCredentials.strDatabase = Environment("DECKKERNEL_PGDATABASE", "DeckKernel");
   aCredentials.strUser = Environment("DECKKERNEL_PGUSER", "");
   aCredentials.strPassword = Environment("DECKKERNEL_PGPASSWORD", "");
   aCredentials.boIntegrated = EnvironmentFlag(
      "DECKKERNEL_PG_INTEGRATED",
      true
      );
   aCredentials.strSslMode = Environment("DECKKERNEL_PGSSLMODE", "prefer");
   aCredentials.strGssEncMode = Environment("DECKKERNEL_PG_GSSENCMODE", "disable");
   aCredentials.strGssLib = Environment("DECKKERNEL_PG_GSSLIB", "");
   aCredentials.strKrbSrvName = Environment("DECKKERNEL_PG_KRBSRVNAME", "postgres");
   aCredentials.strApplicationName = "DeckKernel-Scryfall-Test";

   return aCredentials;
   }

} // namespace deckkernel::test


int main() {
   using namespace deckkernel::test;

   try {
      CurlRuntime aCurlRuntime;

      curl_version_info_data const* const pCurlInfo =
         curl_version_info(CURLVERSION_NOW);

      std::println(
         "OpenSSL: {}",
         OpenSSL_version(OPENSSL_VERSION)
         );

      std::println(
         "curl TLS backend: {}",
         pCurlInfo != nullptr && pCurlInfo->ssl_version != nullptr
            ? pCurlInfo->ssl_version
            : "<unknown>"
         );

      BulkDescriptor const aBulk = ResolveBulkData();

      std::println(
         "Scryfall default_cards updated at: {}",
         aBulk.strUpdatedAt.empty() ? "<not supplied>" : aBulk.strUpdatedAt
         );

      std::filesystem::path const aBulkPath =
         std::filesystem::temp_directory_path() /
         (aBulk.boGzipJsonLines
            ? "deckkernel-scryfall-default-cards.jsonl.gz"
            : "deckkernel-scryfall-default-cards.json");

      std::println("Downloading bulk data to: {}", aBulkPath.string());
      DownloadFile(aBulk.strDownloadUri, aBulkPath);

      std::map<std::string, TScryfallSet::data_ty> mpSets;
      std::vector<TScryfallCard::data_ty> vecCards;

      ReadBulkData(
         aBulkPath,
         aBulk.boGzipJsonLines,
         mpSets,
         vecCards
         );

      std::println(
         "Parsed {} cards from {} sets.",
         vecCards.size(),
         mpSets.size()
         );

      postgres_database_ty aDatabase{
         MakeCredentials()
         };

      std::println(
         "PostgreSQL connection: {}",
         aDatabase.GetServer()
         );

      std::println(
         "PostgreSQL authentication: {}",
         aDatabase.Credentials().boIntegrated
            ? "integrated SSPI"
            : "password"
         );

      EnsureSchema(aDatabase);
      StoreBulkData(aDatabase, mpSets, vecCards);

      std::println(
         "Stored {} cards and {} sets through adecc output sinks.",
         vecCards.size(),
         mpSets.size()
         );

      std::println("\nNewest 20 cards: direct range assignment to text grid");
      ShowLatestCards(aDatabase);

      std::println("\nNewest 20 cards: std::ranges::copy to text-grid sink");
      ShowLatestCardsThroughSink(aDatabase);

      std::println("\nFunctional test completed successfully.");
      return 0;
      }
   catch (std::exception const& ex) {
      std::println(std::cerr, "Functional test failed: {}", ex.what());
      return 1;
      }
   }
