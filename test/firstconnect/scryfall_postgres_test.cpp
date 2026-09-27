// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_postgres_test.cpp
\brief End-to-end Scryfall bulk-data, PostgreSQL, and adecc core functional test.

\details
This file is intentionally both a functional test and a readable first example for the
DeckKernel data path. It separates the work into four explicit processes: load, parse,
store and evaluate. Each process is timed independently so that the cost of network I/O,
JSON decomposition, database writes and database evaluation remains visible.

The example prefers RAII for every owned resource. Raw pointers occur only where a C API
requires them or returns a borrowed pointer. Those cases are documented at the call site.
The persistent Scryfall model is kept in scryfall_model.h, while the PostgreSQL/libpqxx
adapter is supplied from adecc/postgre.

Third-party roles in this source:
- libcurl performs HTTPS metadata and bulk-data transfers.
- OpenSSL is the TLS backend used by the bootstrapped libcurl build.
- nlohmann/json parses Scryfall JSON and JSONL records.
- zlib reads the gzip-compressed JSONL bulk file.
- libpq/libpqxx are used behind the adecc PostgreSQL adapter.
- libarchive is deliberately not used in this first-connect path; gzip is handled
  directly by zlib because the Scryfall payload is a single gzip stream, not an archive.

\author Volker Hillmann (adecc Systemhaus GmbH)
\date 27.09.2026
*/

#include "scryfall_model.h"

#include "pqxx_database.h"

#include "convert_integral.h"
#include "database.h"
#include "text_grid_wrapper.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
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
#include <type_traits>
#include <utility>
#include <vector>

namespace deckkernel::test {

using json_ty = nlohmann::json;

using postgres_database_ty = adecc::db::logical_database<
   adecc::db::postgres::postgres_database,
   adecc::db::postgres::fw_query
   >;

struct BulkDescriptor {
   std::string strDownloadUri;
   std::string strUpdatedAt;
   bool boGzipJsonLines{ false };
   };

struct LoadedBulkData {
   BulkDescriptor aDescriptor;
   std::filesystem::path aPath;
   bool boDownloaded{ false };
   };

struct ParsedBulkData {
   std::map<std::string, TScryfallSet::data_ty> mpSets;
   std::vector<TScryfallCard::data_ty> vecCards;
   };


/**
\brief RAII guard for libcurl process-wide initialization.
\details
libcurl exposes curl_global_init/curl_global_cleanup as a C API pair. The guard makes the
lifetime explicit and guarantees cleanup on every normal or exceptional exit.
*/
class CurlRuntime final {
public:
   /**
   \brief Initializes libcurl for the lifetime of this guard.
   \throws std::runtime_error If curl_global_init reports an error.
   */
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

   /**
   \brief Releases the process-wide libcurl state.
   */
   ~CurlRuntime() {
      curl_global_cleanup();
      }
   };


/**
\brief Deleter used by std::unique_ptr for a libcurl easy handle.
\details
CURL is an opaque C type. libcurl requires curl_easy_cleanup to release a handle, so a
custom deleter adapts that C lifetime rule to C++ RAII.
*/
struct CurlEasyDeleter {
   void operator()(CURL* pCurl) const noexcept {
      if (pCurl != nullptr) {
         curl_easy_cleanup(pCurl);
         }
      }
   };

using curl_handle_ty = std::unique_ptr<CURL, CurlEasyDeleter>;


/**
\brief RAII owner for a libcurl header list.
\details
curl_slist is a linked C structure created incrementally by curl_slist_append. The raw
pointer is kept private because the C API requires that representation. Ownership remains
inside this class and curl_slist_free_all is always called by the destructor.
*/
class CurlHeaders final {
public:
   CurlHeaders() = default;
   CurlHeaders(CurlHeaders const&) = delete;
   CurlHeaders& operator=(CurlHeaders const&) = delete;

   ~CurlHeaders() {
      curl_slist_free_all(pHeaders);
      }

   /**
   \brief Appends one HTTP header.
   \param svHeader Complete header line, for example "Accept: application/json".
   \throws std::bad_alloc If libcurl cannot allocate the linked-list node.
   */
   void Add(std::string_view const svHeader) {
      std::string const strHeader{ svHeader };

      // curl_slist_append is a C API and therefore takes/returns raw pointers.
      // pHeaders remains the sole owned handle and is released by this RAII class.
      curl_slist* const pNew = curl_slist_append(pHeaders, strHeader.c_str());

      if (pNew == nullptr) {
         throw std::bad_alloc{};
         }

      pHeaders = pNew;
      }

   /**
   \brief Returns the borrowed C handle required by CURLOPT_HTTPHEADER.
   \returns Non-owning pointer valid for the lifetime of this CurlHeaders instance.
   */
   curl_slist* Get() const noexcept {
      return pHeaders;
      }

private:
   curl_slist* pHeaders{};
   };


/**
\brief Deleter used by std::unique_ptr for a zlib gzip stream.
\details
gzFile is an opaque zlib C handle. gzclose is the matching release function.
*/
struct GzipDeleter {
   void operator()(std::remove_pointer_t<gzFile>* pFile) const noexcept {
      if (pFile != nullptr) {
         (void)gzclose(pFile);
         }
      }
   };

using gzip_handle_ty = std::unique_ptr<std::remove_pointer_t<gzFile>, GzipDeleter>;


template <typename fn_ty>
using process_result_ty = std::invoke_result_t<fn_ty>;


/**
\brief Executes one named process and prints its elapsed wall-clock duration.
\tparam fn_ty Callable type.
\param svName Human-readable process name printed in the timing line.
\param fnProcess Callable that implements the process.
\returns The callable result for non-void processes.
*/
template <typename fn_ty>
process_result_ty<fn_ty> RunTimedProcess(
   std::string_view const svName,
   fn_ty&& fnProcess
) {
   auto const aStart = std::chrono::steady_clock::now();

   if constexpr (std::is_void_v<process_result_ty<fn_ty>>) {
      std::invoke(std::forward<fn_ty>(fnProcess));

      double const flSeconds = std::chrono::duration<double>(
         std::chrono::steady_clock::now() - aStart
         ).count();

      std::println("[TIME] {:<10}: {:.3f} s", svName, flSeconds);
      }
   else {
      process_result_ty<fn_ty> aResult =
         std::invoke(std::forward<fn_ty>(fnProcess));

      double const flSeconds = std::chrono::duration<double>(
         std::chrono::steady_clock::now() - aStart
         ).count();

      std::println("[TIME] {:<10}: {:.3f} s", svName, flSeconds);
      return aResult;
      }
   }


/**
\brief Reads an environment variable with a fallback value.
\param szName Null-terminated environment-variable name required by std::getenv.
\param svFallback Value returned when the variable is absent or empty.
\returns Environment value or the supplied fallback.
\details
std::getenv is a C API and returns a borrowed raw pointer owned by the C runtime. The
pointer is copied immediately into std::string and is never stored.
*/
std::string Environment(char const* const szName, std::string_view const svFallback) {
   if (char const* const szValue = std::getenv(szName); szValue != nullptr && *szValue != '\0') {
      return std::string{ szValue };
      }

   return std::string{ svFallback };
   }


/**
\brief Reads a boolean environment switch.
\param szName Null-terminated environment-variable name.
\param boFallback Boolean value used when the variable is not defined.
\returns true for 1, true, yes or on; otherwise false.
*/
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


/**
\brief libcurl write callback that appends received bytes to std::string.
\param pData Borrowed byte buffer supplied by libcurl for this callback only.
\param uSize Size of one transferred element.
\param uCount Number of transferred elements.
\param pUser Borrowed context pointer previously registered with CURLOPT_WRITEDATA.
\returns Number of bytes consumed.
\details
The raw pointers are mandatory parts of libcurl's C callback ABI. Ownership is not
transferred. pUser points to a live std::string owned by HttpGet.
*/
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


/**
\brief libcurl write callback that writes received bytes to std::ofstream.
\param pData Borrowed byte buffer supplied by libcurl for this callback only.
\param uSize Size of one transferred element.
\param uCount Number of transferred elements.
\param pUser Borrowed context pointer previously registered with CURLOPT_WRITEDATA.
\returns Number of bytes consumed, or zero if the stream failed.
\details
The raw pointers are required by libcurl's C callback ABI. The std::ofstream itself is a
RAII object owned by DownloadFile.
*/
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


/**
\brief Applies the common HTTPS policy used by all Scryfall requests.
\param aCurl Live libcurl easy handle owned by curl_handle_ty.
\param strUrl HTTPS URL to request.
\param aHeaders RAII-owned HTTP header list.
\param arrError Caller-owned libcurl error buffer kept alive through curl_easy_perform.
\details
The function receives references to C++ owners rather than owning raw handles. Raw
pointers are produced only at the libcurl call boundary because curl_easy_setopt is a C
API. Certificate verification remains enabled, Windows native CAs are requested when the
libcurl build supports that option, redirects stay HTTPS-only, and the User-Agent
identifies this educational DeckKernel client.
*/
void ConfigureCurl(
   CURL& aCurl,
   std::string const& strUrl,
   CurlHeaders const& aHeaders,
   std::array<char, CURL_ERROR_SIZE>& arrError
) {
   // libcurl stores the address of this caller-owned buffer until the transfer ends.
   curl_easy_setopt(&aCurl, CURLOPT_ERRORBUFFER, arrError.data());

   // strUrl stays alive for the complete transfer performed by the caller.
   curl_easy_setopt(&aCurl, CURLOPT_URL, strUrl.c_str());

   // Scryfall may redirect bulk-download URLs; follow a bounded number of redirects.
   curl_easy_setopt(&aCurl, CURLOPT_FOLLOWLOCATION, 1L);
   curl_easy_setopt(&aCurl, CURLOPT_MAXREDIRS, 5L);

   // Convert HTTP status >= 400 into CURLE_HTTP_RETURNED_ERROR.
   curl_easy_setopt(&aCurl, CURLOPT_FAILONERROR, 1L);

   // Scryfall asks clients to send an identifying User-Agent.
   curl_easy_setopt(
      &aCurl,
      CURLOPT_USERAGENT,
      "adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)"
      );

   // Get() returns a borrowed C handle; CurlHeaders retains ownership.
   curl_easy_setopt(&aCurl, CURLOPT_HTTPHEADER, aHeaders.Get());

   // Empty string enables all built-in content decoders advertised by libcurl.
   curl_easy_setopt(&aCurl, CURLOPT_ACCEPT_ENCODING, "");

   // Never weaken peer or hostname verification in the functional example.
   curl_easy_setopt(&aCurl, CURLOPT_SSL_VERIFYPEER, 1L);
   curl_easy_setopt(&aCurl, CURLOPT_SSL_VERIFYHOST, 2L);

   // TLS 1.2 is the minimum accepted protocol version.
   curl_easy_setopt(&aCurl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

#if defined CURLSSLOPT_NATIVE_CA
   // On Windows/OpenSSL this lets libcurl also use the native Windows CA store.
   curl_easy_setopt(
      &aCurl,
      CURLOPT_SSL_OPTIONS,
      static_cast<long>(CURLSSLOPT_NATIVE_CA)
      );
#endif

   // Only HTTPS is accepted for the original request and every redirect target.
   curl_easy_setopt(&aCurl, CURLOPT_PROTOCOLS_STR, "https");
   curl_easy_setopt(&aCurl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
   }


/**
\brief Performs a small HTTPS GET and returns the complete response body.
\param strUrl HTTPS URL to request.
\returns Response body as std::string.
\throws std::runtime_error If the easy handle cannot be created or the transfer fails.
*/
std::string HttpGet(std::string const& strUrl) {
   // curl_easy_init returns an owning raw C handle. It is adopted immediately by
   // curl_handle_ty so every later exit path calls curl_easy_cleanup automatically.
   curl_handle_ty upCurl{ curl_easy_init() };

   if (!upCurl) {
      throw std::runtime_error{ "curl_easy_init returned null" };
      }

   CurlHeaders aHeaders;
   aHeaders.Add("Accept: application/json;q=0.9,*/*;q=0.8");

   std::array<char, CURL_ERROR_SIZE> arrError{};
   ConfigureCurl(*upCurl, strUrl, aHeaders, arrError);

   std::string strResult;

   // libcurl's callback ABI is a C function pointer plus void* context. strResult owns
   // the storage; CURLOPT_WRITEDATA receives only a borrowed pointer during perform().
   curl_easy_setopt(upCurl.get(), CURLOPT_WRITEFUNCTION, &WriteString);
   curl_easy_setopt(upCurl.get(), CURLOPT_WRITEDATA, std::addressof(strResult));

   CURLcode const iResult = curl_easy_perform(upCurl.get());

   if (iResult != CURLE_OK) {
      throw std::runtime_error{
         std::format(
            "HTTPS request failed for {}: {}",
            strUrl,
            arrError[0] != '\0' ? arrError.data() : curl_easy_strerror(iResult)
            )
         };
      }

   return strResult;
   }


/**
\brief Downloads one Scryfall bulk-data object into a local file.
\param strUrl HTTPS bulk-download URL.
\param aTarget Target file path.
\throws std::runtime_error If the file or libcurl handle cannot be created or the transfer fails.
\details
std::ofstream owns the operating-system file resource. curl_handle_ty owns the libcurl
easy handle and CurlHeaders owns the C header list. No owning raw pointer escapes.
*/
void DownloadFile(std::string const& strUrl, std::filesystem::path const& aTarget) {
   std::ofstream osFile{ aTarget, std::ios::binary | std::ios::trunc };

   if (!osFile) {
      throw std::runtime_error{
         std::format("cannot create bulk-data file '{}'", aTarget.string())
         };
      }

   curl_handle_ty upCurl{ curl_easy_init() };

   if (!upCurl) {
      throw std::runtime_error{ "curl_easy_init returned null" };
      }

   CurlHeaders aHeaders;
   aHeaders.Add("Accept: application/json;q=0.9,*/*;q=0.8");

   std::array<char, CURL_ERROR_SIZE> arrError{};
   ConfigureCurl(*upCurl, strUrl, aHeaders, arrError);

   // The callback and void* context are required by libcurl's C ABI. osFile remains the
   // RAII owner and outlives curl_easy_perform.
   curl_easy_setopt(upCurl.get(), CURLOPT_WRITEFUNCTION, &WriteFile);
   curl_easy_setopt(upCurl.get(), CURLOPT_WRITEDATA, std::addressof(osFile));

   CURLcode const iResult = curl_easy_perform(upCurl.get());

   if (iResult != CURLE_OK) {
      throw std::runtime_error{
         std::format(
            "bulk download failed for {}: {}",
            strUrl,
            arrError[0] != '\0' ? arrError.data() : curl_easy_strerror(iResult)
            )
         };
      }
   }


/**
\brief Resolves Scryfall's current default_cards bulk object.
\returns Download URI, update timestamp and compression information.
\throws std::runtime_error If the metadata does not contain default_cards.
\details
Only the small Scryfall metadata endpoint is fetched here. The actual card data is
downloaded in LoadProcess so metadata discovery and the large transfer remain distinct.
*/
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


/**
\brief Tests whether the cached bulk file was written on the current local day.
\param aPath Candidate cache file.
\returns true when the file exists and its local calendar day equals today.
*/
bool IsFileFromToday(std::filesystem::path const& aPath) {
   if (!std::filesystem::exists(aPath)) {
      return false;
      }

   auto const aFileTime = std::filesystem::last_write_time(aPath);
   auto const aSystemTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      aFileTime - std::filesystem::file_time_type::clock::now() +
      std::chrono::system_clock::now()
      );

   std::time_t const iFileTime = std::chrono::system_clock::to_time_t(aSystemTime);
   std::time_t const iNow = std::time(nullptr);

   std::tm aFileLocal{};
   std::tm aNowLocal{};

#if defined _WIN32
   localtime_s(&aFileLocal, &iFileTime);
   localtime_s(&aNowLocal, &iNow);
#else
   localtime_r(&iFileTime, &aFileLocal);
   localtime_r(&iNow, &aNowLocal);
#endif

   return aFileLocal.tm_year == aNowLocal.tm_year &&
          aFileLocal.tm_yday == aNowLocal.tm_yday;
   }


/**
\brief Asks whether a same-day cache file should be downloaded again.
\param aPath Candidate cache file.
\returns true when a download should be performed.
*/
bool AskForBulkDownload(std::filesystem::path const& aPath) {
   if (!IsFileFromToday(aPath)) {
      return true;
      }

   std::print(
      "Bulk data file '{}' is from today. Download it again? [y/N]: ",
      aPath.string()
      );

   std::string strAnswer;
   std::getline(std::cin, strAnswer);

   std::ranges::transform(
      strAnswer,
      strAnswer.begin(),
      [](unsigned char const chValue) {
         return static_cast<char>(std::tolower(chValue));
         }
      );

   return strAnswer == "y" || strAnswer == "yes";
   }


/**
\brief Process 1: resolve and obtain the Scryfall bulk file.
\returns Descriptor plus local cache path.
\details
This process deliberately does not parse card records. Its responsibility is transport:
resolve metadata, decide about cache reuse, and ensure that the bulk file exists locally.
*/
LoadedBulkData LoadProcess() {
   BulkDescriptor aBulk = ResolveBulkData();

   std::println(
      "Scryfall default_cards updated at: {}",
      aBulk.strUpdatedAt.empty() ? "<not supplied>" : aBulk.strUpdatedAt
      );

   std::filesystem::path const aBulkPath =
      std::filesystem::temp_directory_path() /
      (aBulk.boGzipJsonLines
         ? "deckkernel-scryfall-default-cards.jsonl.gz"
         : "deckkernel-scryfall-default-cards.json");

   bool const boDownload = AskForBulkDownload(aBulkPath);

   if (boDownload) {
      std::println("Downloading bulk data to: {}", aBulkPath.string());
      DownloadFile(aBulk.strDownloadUri, aBulkPath);
      }
   else {
      std::println("Using today's existing bulk data: {}", aBulkPath.string());
      }

   return LoadedBulkData{
      .aDescriptor = std::move(aBulk),
      .aPath = aBulkPath,
      .boDownloaded = boDownload
      };
   }


/**
\brief Decomposes one Scryfall card JSON object into relationally useful values.
\param aCard Parsed Scryfall card object.
\param aParsed Destination for normalized set and card rows.
\details
The bulk format repeats set information in every card object. For the relational model
we split that denormalized JSON record into two structures:

- scryfall_sets receives one row per set, keyed by Scryfall set_id.
- scryfall_cards receives one row per printing and stores set_id as the foreign key.

std::map::try_emplace performs the set deduplication while parsing. The card vector keeps
the bulk-file order and provides a contiguous range for the later database sink. Only the
small field subset needed by this first functional test is materialized.
*/
void AppendCard(
   json_ty const& aCard,
   ParsedBulkData& aParsed
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

   aParsed.mpSets.try_emplace(
      strSetId,
      TScryfallSet::data_ty{
         strSetId,
         strSetCode,
         strSetName
         }
      );

   aParsed.vecCards.emplace_back(
      strId,
      std::move(optOracleId),
      strName,
      strSetId,
      adecc::ConvertTo<adecc::date_ty>(strReleasedAt)
      );
   }


/**
\brief Reads gzip-compressed Scryfall JSONL and decomposes it record by record.
\param aPath Path of the .jsonl.gz bulk file.
\param aParsed Destination that collects normalized set and card tuples.
\throws std::runtime_error If zlib cannot open or read the gzip stream.
\details
Scryfall's JSONL representation contains one complete JSON card object per line. zlib is
used directly because this is one gzip stream rather than a multi-entry archive. The
stream is owned by gzip_handle_ty, so gzclose is guaranteed even when JSON parsing throws.
*/
void ReadGzipJsonLines(
   std::filesystem::path const& aPath,
   ParsedBulkData& aParsed
) {
   // gzopen returns an owning opaque C handle. Adopt it immediately into unique_ptr.
   gzip_handle_ty upFile{ gzopen(aPath.string().c_str(), "rb") };

   if (!upFile) {
      throw std::runtime_error{
         std::format("cannot open gzip bulk-data file '{}'", aPath.string())
         };
      }

   // A fixed read buffer limits allocations. strPending carries an incomplete JSONL
   // record across block boundaries when a newline is not part of the current block.
   std::array<char, 64U * 1024U> arrBuffer{};
   std::string strPending;

   while (true) {
      int const iRead = gzread(
         upFile.get(),
         arrBuffer.data(),
         static_cast<unsigned int>(arrBuffer.size())
         );

      if (iRead < 0) {
         int iError{};
         char const* const szError = gzerror(upFile.get(), &iError);
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
            AppendCard(json_ty::parse(svLine), aParsed);
            }

         uStart = uNewLine + 1;
         }
      }

   if (!strPending.empty()) {
      AppendCard(json_ty::parse(strPending), aParsed);
      }
   }


/**
\brief Reads an uncompressed Scryfall bulk file.
\param aPath Path of the JSON or JSONL file.
\param aParsed Destination for normalized rows.
\details
The fallback accepts either a single JSON array or line-delimited JSON. Production
Scryfall bulk data currently uses the gzip JSONL path preferred by ResolveBulkData.
*/
void ReadPlainBulk(
   std::filesystem::path const& aPath,
   ParsedBulkData& aParsed
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
         AppendCard(aCard, aParsed);
         }

      return;
      }

   std::string strLine;

   while (std::getline(isFile, strLine)) {
      if (!strLine.empty()) {
         AppendCard(json_ty::parse(strLine), aParsed);
         }
      }
   }


/**
\brief Process 2: parse and normalize the local Scryfall bulk file.
\param aLoaded Result of LoadProcess.
\returns Relationally shaped set and card tuple collections.
\details
Parsing is intentionally separate from transport and persistence. That makes the format
boundary measurable and demonstrates how one external denormalized JSON record becomes
typed PersistentSystemData-compatible tuples before the database is involved.
*/
ParsedBulkData ParseProcess(LoadedBulkData const& aLoaded) {
   ParsedBulkData aParsed;

   if (aLoaded.aDescriptor.boGzipJsonLines) {
      ReadGzipJsonLines(aLoaded.aPath, aParsed);
      }
   else {
      ReadPlainBulk(aLoaded.aPath, aParsed);
      }

   std::println(
      "Parsed {} cards from {} sets.",
      aParsed.vecCards.size(),
      aParsed.mpSets.size()
      );

   return aParsed;
   }


/**
\brief Ensures the two first-connect test tables exist.
\param aDatabase Connected logical PostgreSQL database.
\details
The schema itself is provisioned administratively. The functional program only owns its
test tables. Secondary indexes are handled by StoreProcess because rebuilding them after
the bulk insert is cheaper than maintaining them for every inserted card.
*/
void EnsureSchema(postgres_database_ty const& aDatabase) {
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

   }


/**
\brief Process 3: replace the relational test data in one transaction.
\param aDatabase Connected logical PostgreSQL database.
\param aParsed Normalized rows produced by ParseProcess.
\details
The process uses one transaction for truncate, both data ranges, secondary index rebuild
and commit. Sets are exposed directly through std::views::values, avoiding an additional
vector copy. The generic adecc OutputSink consumes the existing tuple ranges directly.
Secondary indexes are dropped before the 118k-card load and rebuilt afterwards so each
individual INSERT does not also maintain those trees.

The primary and unique constraints stay active during loading because they are part of
the integrity contract, not optional query accelerators.
*/
void StoreProcess(
   postgres_database_ty& aDatabase,
   ParsedBulkData const& aParsed
) {
   RunTimedProcess(
      "  Schema",
      [&aDatabase]() {
         EnsureSchema(aDatabase);
         }
      );

   auto aTransaction = aDatabase.Transaction();

   RunTimedProcess(
      "  Prepare",
      [&aDatabase]() {
         aDatabase.ExecuteCommand(
            "DROP INDEX IF EXISTS deckkernel_test.ix_scryfall_cards_oracle_id"
            );

         aDatabase.ExecuteCommand(
            "DROP INDEX IF EXISTS deckkernel_test.ix_scryfall_cards_released"
            );

         aDatabase.ExecuteCommand(
            "TRUNCATE TABLE deckkernel_test.scryfall_cards, "
            "deckkernel_test.scryfall_sets"
            );
         }
      );

   auto aSetSink = aDatabase.template MakeOutputSink<
      std::string,
      std::string,
      std::string
      >(
         TScryfallSet::CreateInsertSql(),
         TScryfallSet::CreateInsertOutputParameters()
         );

   auto rngSets = aParsed.mpSets | std::views::values;

   RunTimedProcess(
      "  Sets",
      [&aSetSink, &rngSets]() {
         aSetSink = rngSets;
         }
      );

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

   RunTimedProcess(
      "  Cards",
      [&aCardSink, &aParsed]() {
         aCardSink = aParsed.vecCards;
         }
      );

   RunTimedProcess(
      "  Indexes",
      [&aDatabase]() {
         aDatabase.ExecuteCommand(
            "CREATE INDEX ix_scryfall_cards_oracle_id "
            "ON deckkernel_test.scryfall_cards(oracle_id)"
            );

         aDatabase.ExecuteCommand(
            "CREATE INDEX ix_scryfall_cards_released "
            "ON deckkernel_test.scryfall_cards(released_at DESC)"
            );
         }
      );

   RunTimedProcess(
      "  Commit",
      [&aTransaction]() {
         aTransaction.Commit();
         }
      );

   std::println(
      "Stored {} cards and {} sets through adecc output sinks.",
      aParsed.vecCards.size(),
      aParsed.mpSets.size()
      );
   }


/**
\brief Demonstrates direct assignment from a typed database range into a text grid.
\param aDatabase Connected logical PostgreSQL database.
*/
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
      "SELECT c.name AS card_name, s.name AS set_name "
      "FROM deckkernel_test.scryfall_cards c "
      "JOIN deckkernel_test.scryfall_sets s ON s.id = c.set_id "
      "ORDER BY c.released_at DESC, c.name, c.id "
      "LIMIT 20"
      );

   aGrid = rngLatest;
   }


/**
\brief Demonstrates std::ranges::copy from PostgreSQL into a text-grid sink.
\param aDatabase Connected logical PostgreSQL database.
*/
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
      "SELECT c.name AS card_name, s.name AS set_name "
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


/**
\brief Process 4: read stored data back and project it into two grid paths.
\param aDatabase Connected logical PostgreSQL database.
\details
This closes the functional loop: network input -> typed parsing -> relational storage ->
typed database range -> presentation sink.
*/
void EvaluateProcess(postgres_database_ty const& aDatabase) {
   std::println("\nNewest 20 cards: direct range assignment to text grid");
   ShowLatestCards(aDatabase);

   std::println("\nNewest 20 cards: std::ranges::copy to text-grid sink");
   ShowLatestCardsThroughSink(aDatabase);
   }


/**
\brief Builds PostgreSQL credentials from environment overrides and safe local defaults.
\returns Credential object consumed by the PostgreSQL adapter.
*/
adecc::db::postgres::postgres_credentials MakeCredentials() {
   adecc::db::postgres::postgres_credentials aCredentials;

   aCredentials.strHost = Environment("DECKKERNEL_PGHOST", "localhost");
   aCredentials.uPort = adecc::ConvertTo<std::uint16_t>(
      Environment("DECKKERNEL_PGPORT", "5432")
      );
   aCredentials.strDatabase = Environment("DECKKERNEL_PGDATABASE", "DeckKernel");
   aCredentials.strUser = Environment("DECKKERNEL_PGUSER", "deckkernel_user");
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

      // curl_version_info returns a library-owned, borrowed pointer. It must not be
      // deleted. We only inspect it while libcurl's CurlRuntime is alive.
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

      LoadedBulkData const aLoaded = RunTimedProcess(
         "Load",
         []() {
            return LoadProcess();
            }
         );

      ParsedBulkData const aParsed = RunTimedProcess(
         "Parse",
         [&aLoaded]() {
            return ParseProcess(aLoaded);
            }
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

      RunTimedProcess(
         "Store",
         [&aDatabase, &aParsed]() {
            StoreProcess(aDatabase, aParsed);
            }
         );

      RunTimedProcess(
         "Evaluate",
         [&aDatabase]() {
            EvaluateProcess(aDatabase);
            }
         );

      std::println("\nFunctional test completed successfully.");
      return 0;
      }
   catch (std::exception const& ex) {
      std::println(std::cerr, "Functional test failed: {}", ex.what());
      return 1;
      }
   }
