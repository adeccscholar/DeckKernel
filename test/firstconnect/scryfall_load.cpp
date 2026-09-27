// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_load.cpp
\brief Lesson 1: Scryfall metadata resolution, HTTPS download and same-day cache reuse.
\details
This source is intentionally self-contained so it can be discussed as a transport lesson.
Owned C resources are adapted to RAII immediately. Raw pointers remain only where the
libcurl or C-runtime ABI requires them.
*/

#include "scryfall_load.h"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include <openssl/crypto.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <print>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>

namespace deckkernel::test {
namespace {

using json_ty = nlohmann::json;

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


struct CurlEasyDeleter {
   void operator()(CURL* pCurl) const noexcept {
      if (pCurl != nullptr) {
         curl_easy_cleanup(pCurl);
         }
      }
   };

using curl_handle_ty = std::unique_ptr<CURL, CurlEasyDeleter>;


class CurlHeaders final {
public:
   CurlHeaders() = default;
   CurlHeaders(CurlHeaders const&) = delete;
   CurlHeaders& operator=(CurlHeaders const&) = delete;

   ~CurlHeaders() {
      curl_slist_free_all(pHeaders);
      }

   void Add(std::string_view const svHeader) {
      std::string const strHeader{ svHeader };

      // C API boundary: the returned raw pointer remains owned by this object.
      curl_slist* const pNew = curl_slist_append(pHeaders, strHeader.c_str());

      if (pNew == nullptr) {
         throw std::bad_alloc{};
         }

      pHeaders = pNew;
      }

   curl_slist* Get() const noexcept {
      return pHeaders;
      }

private:
   curl_slist* pHeaders{};
   };


/**
\brief libcurl callback that appends a response block to std::string.
\param pData Borrowed block supplied by libcurl.
\param uSize Size of one element in the block.
\param uCount Number of elements in the block.
\param pUser Borrowed pointer registered with CURLOPT_WRITEDATA.
\returns Number of bytes consumed.
\details
The raw pointers are required by libcurl's C callback ABI. Ownership remains with
libcurl and HttpGet respectively.
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
\brief libcurl callback that writes a response block to std::ofstream.
\param pData Borrowed block supplied by libcurl.
\param uSize Size of one element in the block.
\param uCount Number of elements in the block.
\param pUser Borrowed std::ofstream pointer registered with CURLOPT_WRITEDATA.
\returns Number of bytes consumed, or zero when the stream failed.
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


void ConfigureCurl(
   CURL* const pCurl,
   std::string const& strUrl,
   CurlHeaders const& aHeaders,
   std::array<char, CURL_ERROR_SIZE>& arrError
) {
   if (pCurl == nullptr) {
      throw std::runtime_error{ "curl easy handle is null" };
      }

   /*
   libcurl deliberately exposes CURL as an opaque C handle. In this toolchain CURL is
   typedef'd as void, so a C++ reference such as CURL& cannot exist and unique_ptr<CURL>
   cannot be dereferenced. Ownership is still RAII-managed by curl_handle_ty; this raw
   pointer is only a borrowed handle passed across the libcurl C API boundary.
   */
   curl_easy_setopt(pCurl, CURLOPT_ERRORBUFFER, arrError.data());
   curl_easy_setopt(pCurl, CURLOPT_URL, strUrl.c_str());
   curl_easy_setopt(pCurl, CURLOPT_FOLLOWLOCATION, 1L);
   curl_easy_setopt(pCurl, CURLOPT_MAXREDIRS, 5L);
   curl_easy_setopt(pCurl, CURLOPT_FAILONERROR, 1L);

   curl_easy_setopt(
      pCurl,
      CURLOPT_USERAGENT,
      "adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)"
      );

   curl_easy_setopt(pCurl, CURLOPT_HTTPHEADER, aHeaders.Get());
   curl_easy_setopt(pCurl, CURLOPT_ACCEPT_ENCODING, "");
   curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYPEER, 1L);
   curl_easy_setopt(pCurl, CURLOPT_SSL_VERIFYHOST, 2L);
   curl_easy_setopt(pCurl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1_2);

#if defined CURLSSLOPT_NATIVE_CA
   curl_easy_setopt(
      pCurl,
      CURLOPT_SSL_OPTIONS,
      static_cast<long>(CURLSSLOPT_NATIVE_CA)
      );
#endif

   curl_easy_setopt(pCurl, CURLOPT_PROTOCOLS_STR, "https");
   curl_easy_setopt(pCurl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
   }


std::string HttpGet(std::string const& strUrl) {
   // curl_easy_init returns an owning C handle; unique_ptr adopts it immediately.
   curl_handle_ty upCurl{ curl_easy_init() };

   if (!upCurl) {
      throw std::runtime_error{ "curl_easy_init returned null" };
      }

   CurlHeaders aHeaders;
   aHeaders.Add("Accept: application/json;q=0.9,*/*;q=0.8");

   std::array<char, CURL_ERROR_SIZE> arrError{};
   ConfigureCurl(upCurl.get(), strUrl, aHeaders, arrError);

   std::string strResult;

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


void DownloadFile(
   std::string const& strUrl,
   std::filesystem::path const& aTarget
) {
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
   ConfigureCurl(upCurl.get(), strUrl, aHeaders, arrError);

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

} // namespace


LoadedBulkData LoadProcess() {
   CurlRuntime aCurlRuntime;

   // Borrowed library-owned pointer; it must not be deleted.
   curl_version_info_data const* const pCurlInfo =
      curl_version_info(CURLVERSION_NOW);

   std::println("OpenSSL: {}", OpenSSL_version(OPENSSL_VERSION));
   std::println(
      "curl TLS backend: {}",
      pCurlInfo != nullptr && pCurlInfo->ssl_version != nullptr
         ? pCurlInfo->ssl_version
         : "<unknown>"
      );

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

} // namespace deckkernel::test
