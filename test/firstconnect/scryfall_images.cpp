// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/**
\file scryfall_images.cpp
\brief Optional, resumable image download into PostgreSQL bytea using libcurl.
\details Binary images are kept separately from card rows and reused across imports.
*/
#include "scryfall_images.h"
#include "../../src/scryfall/image_cache.h"
#include <chrono>
#include <thread>
#include <filesystem>
#include <curl/curl.h>
#include <libpq-fe.h>
#include <openssl/evp.h>
#include <array>
#include <charconv>
#include <algorithm>
#include <cctype>
#include <ctime>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>

namespace deckkernel::test {
namespace {

std::string Env(char const* szName,char const* szDefault) {
   if(char const* szValue=std::getenv(szName);szValue && *szValue) return szValue;
   return szDefault;
}

std::size_t ImageLimit(bool bContinuous) {
   // The full rebuild stays bounded; image-only mode needs no external loop.
   std::string const strLimit=Env("DECKKERNEL_IMAGE_LIMIT",bContinuous?"0":"100");
   std::size_t uLimit{};
   auto const aResult=std::from_chars(strLimit.data(),strLimit.data()+strLimit.size(),uLimit);
   if(aResult.ec!=std::errc{}||aResult.ptr!=strLimit.data()+strLimit.size())
      throw std::runtime_error("invalid DECKKERNEL_IMAGE_LIMIT (nonnegative integer, 0 = unlimited)");
   return uLimit;
}

struct PgDeleter {
   void operator()(PGconn* pConn) const noexcept { if(pConn) PQfinish(pConn); }
};
struct PgResultDeleter {
   void operator()(PGresult* pResult) const noexcept { if(pResult) PQclear(pResult); }
};
struct CurlDeleter {
   void operator()(CURL* pCurl) const noexcept { if(pCurl) curl_easy_cleanup(pCurl); }
};
struct ImageDownloadError : std::runtime_error {
   CURLcode eCode;
   long iHttpStatus;
   std::chrono::seconds aRetryAfter;
   ImageDownloadError(CURLcode eNewCode,long iNewHttpStatus,
      std::chrono::seconds aNewRetryAfter,std::string strDetail):
      std::runtime_error(std::move(strDetail)),eCode(eNewCode),
      iHttpStatus(iNewHttpStatus),aRetryAfter(aNewRetryAfter) {}

   [[nodiscard]] bool IsFatal() const noexcept {
      return eCode==CURLE_PEER_FAILED_VERIFICATION ||
         eCode==CURLE_SSL_CERTPROBLEM ||
         eCode==CURLE_SSL_CACERT_BADFILE ||
         eCode==CURLE_SSL_CONNECT_ERROR;
   }
   [[nodiscard]] bool IsTransient() const noexcept {
      return iHttpStatus==429 || iHttpStatus==500 || iHttpStatus==502 ||
         iHttpStatus==503 || iHttpStatus==504 ||
         eCode==CURLE_OPERATION_TIMEDOUT || eCode==CURLE_COULDNT_CONNECT ||
         eCode==CURLE_COULDNT_RESOLVE_HOST || eCode==CURLE_RECV_ERROR ||
         eCode==CURLE_SEND_ERROR || eCode==CURLE_GOT_NOTHING;
   }
};
using pg_connection_ty=std::unique_ptr<PGconn,PgDeleter>;
using pg_result_ty=std::unique_ptr<PGresult,PgResultDeleter>;
using curl_handle_ty=std::unique_ptr<CURL,CurlDeleter>;

/** \brief Append libcurl response blocks to a bounded memory buffer. */
std::size_t Receive(char* pData,std::size_t uSize,std::size_t uCount,void* pState) {
   auto& vecBytes=*static_cast<std::vector<unsigned char>*>(pState);
   std::size_t const uBytes=uSize*uCount;
   constexpr std::size_t uMaximum=12U*1024U*1024U;
   if(uBytes>uMaximum-vecBytes.size()) return 0;
   auto const* pBegin=reinterpret_cast<unsigned char const*>(pData);
   vecBytes.insert(vecBytes.end(),pBegin,pBegin+uBytes);
   return uBytes;
}

struct ImageResponseHeaders final {
   std::chrono::seconds aRetryAfter{};
};

/** \brief Honor Retry-After on HTTP 429/503 without disabling TLS verification. */
std::size_t ReceiveHeader(char* pData,std::size_t uSize,std::size_t uCount,
   void* pUser) {
   std::size_t const uBytes=uSize*uCount;
   auto& aHeaders=*static_cast<ImageResponseHeaders*>(pUser);
   std::string strLine(pData,uBytes);
   auto const fnLower=[](unsigned char ch){return static_cast<char>(std::tolower(ch));};
   std::string strLower=strLine;
   std::transform(strLower.begin(),strLower.end(),strLower.begin(),fnLower);
   if(!strLower.starts_with("retry-after:")) return uBytes;
   std::string strValue=strLine.substr(12);
   std::size_t const uBegin=strValue.find_first_not_of(" \t");
   if(uBegin==std::string::npos) return uBytes;
   strValue.erase(0,uBegin);
   std::size_t const uEnd=strValue.find_last_not_of(" \r\n\t");
   if(uEnd!=std::string::npos) strValue.resize(uEnd+1);
   std::uint64_t uDelay{};
   auto const aParsed=std::from_chars(strValue.data(),
      strValue.data()+strValue.size(),uDelay);
   if(aParsed.ec==std::errc{} &&
      aParsed.ptr==strValue.data()+strValue.size()) {
      aHeaders.aRetryAfter=std::chrono::seconds{
         static_cast<std::int64_t>(std::min<std::uint64_t>(uDelay,86400))};
      }
   else {
      std::time_t const iRetry=curl_getdate(strValue.c_str(),nullptr);
      std::time_t const iNow=std::time(nullptr);
      if(iRetry>iNow) {
         aHeaders.aRetryAfter=std::chrono::seconds{
            std::min<std::int64_t>(
               static_cast<std::int64_t>(iRetry-iNow),86400)};
         }
      }
   return uBytes;
}

std::vector<unsigned char> DownloadImage(std::string const& strUrl) {
   if(!strUrl.starts_with("https://cards.scryfall.io/"))
      throw std::runtime_error("unapproved Scryfall image host");
   curl_handle_ty aCurl(curl_easy_init());
   if(!aCurl) throw std::runtime_error("curl_easy_init failed");
   std::vector<unsigned char> vecData;
   ImageResponseHeaders aHeaders;
   std::array<char,CURL_ERROR_SIZE> arrError{};
   curl_easy_setopt(aCurl.get(),CURLOPT_ERRORBUFFER,arrError.data());
   curl_easy_setopt(aCurl.get(),CURLOPT_URL,strUrl.c_str());
   curl_easy_setopt(aCurl.get(),CURLOPT_FOLLOWLOCATION,0L);
   curl_easy_setopt(aCurl.get(),CURLOPT_FAILONERROR,1L);
   curl_easy_setopt(aCurl.get(),CURLOPT_TIMEOUT,45L);
   curl_easy_setopt(aCurl.get(),CURLOPT_CONNECTTIMEOUT,10L);
   curl_easy_setopt(aCurl.get(),CURLOPT_SSL_VERIFYPEER,1L);
   curl_easy_setopt(aCurl.get(),CURLOPT_SSL_VERIFYHOST,2L);
   curl_easy_setopt(aCurl.get(),CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
#if defined(CURLSSLOPT_NATIVE_CA)
   // Match the working Scryfall bulk downloader: use Windows trusted roots.
   curl_easy_setopt(aCurl.get(),CURLOPT_SSL_OPTIONS,static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
   // Optional explicit CA bundle for OpenSSL deployments with no native trust.
   std::string const strCaBundle=Env("DECKKERNEL_CURL_CA_BUNDLE","");
   if(!strCaBundle.empty()) {
      if(!std::filesystem::is_regular_file(strCaBundle))
         throw std::runtime_error("DECKKERNEL_CURL_CA_BUNDLE points to a missing file");
      curl_easy_setopt(aCurl.get(),CURLOPT_CAINFO,strCaBundle.c_str());
   }
   curl_easy_setopt(aCurl.get(),CURLOPT_PROTOCOLS_STR,"https");
   curl_easy_setopt(aCurl.get(),CURLOPT_REDIR_PROTOCOLS_STR,"https");
   curl_easy_setopt(aCurl.get(),CURLOPT_USERAGENT,"adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)");
   curl_easy_setopt(aCurl.get(),CURLOPT_WRITEFUNCTION,&Receive);
   curl_easy_setopt(aCurl.get(),CURLOPT_WRITEDATA,&vecData);
   curl_easy_setopt(aCurl.get(),CURLOPT_HEADERFUNCTION,&ReceiveHeader);
   curl_easy_setopt(aCurl.get(),CURLOPT_HEADERDATA,&aHeaders);
   CURLcode const eCode=curl_easy_perform(aCurl.get());
   long iHttpStatus{};
   curl_easy_getinfo(aCurl.get(),CURLINFO_RESPONSE_CODE,&iHttpStatus);
   if(eCode!=CURLE_OK)
      throw ImageDownloadError(eCode,iHttpStatus,aHeaders.aRetryAfter,std::format(
         "image HTTPS failed (curl={}, HTTP={}): {}",static_cast<int>(eCode),iHttpStatus,
         arrError[0]?arrError.data():curl_easy_strerror(eCode)));
   if(vecData.empty()) throw std::runtime_error("image server returned an empty body");
   return vecData;
}

std::string Sha256(std::vector<unsigned char> const& vecData) {
   std::array<unsigned char,EVP_MAX_MD_SIZE> arrDigest{};
   unsigned int uLength{};
   if(EVP_Digest(vecData.data(),vecData.size(),arrDigest.data(),&uLength,EVP_sha256(),nullptr)!=1
      ||uLength!=32)
      throw std::runtime_error("OpenSSL SHA-256 failed");
   static constexpr char szHex[]="0123456789abcdef";
   std::string strHex;
   strHex.reserve(uLength*2);
   for(unsigned int i=0;i<uLength;++i) {
      strHex.push_back(szHex[arrDigest[i]>>4]);
      strHex.push_back(szHex[arrDigest[i]&15]);
   }
   return strHex;
}

pg_connection_ty Connect() {
   std::string const strHost=Env("DECKKERNEL_PGHOST","localhost");
   std::string const strPort=Env("DECKKERNEL_PGPORT","5432");
   std::string const strDb=Env("DECKKERNEL_PGDATABASE","DeckKernel");
   std::string const strUser=Env("DECKKERNEL_PGUSER","deckkernel_user");
   std::string const strPassword=Env("DECKKERNEL_PGPASSWORD","");
   std::string const strSsl=Env("DECKKERNEL_PGSSLMODE","prefer");
   std::string const strGss=Env("DECKKERNEL_PG_GSSENCMODE","disable");
   char const* arrKeys[]={"host","port","dbname","user","password","sslmode","gssencmode","application_name",nullptr};
   char const* arrValues[]={strHost.c_str(),strPort.c_str(),strDb.c_str(),strUser.c_str(),
      strPassword.c_str(),strSsl.c_str(),strGss.c_str(),"DeckKernel-Scryfall-Images",nullptr};
   pg_connection_ty aConnection(PQconnectdbParams(arrKeys,arrValues,0));
   if(!aConnection||PQstatus(aConnection.get())!=CONNECTION_OK)
      throw std::runtime_error("image import PostgreSQL connection failed: "+
         std::string(aConnection?PQerrorMessage(aConnection.get()):"PQconnectdbParams failed"));
   return aConnection;
}
/** \brief Execute a libpq command and fail with the PostgreSQL diagnostic. */
pg_result_ty Command(PGconn* pConn,char const* szSql) {
   pg_result_ty aResult(PQexec(pConn,szSql));
   if(!aResult || PQresultStatus(aResult.get())!=PGRES_COMMAND_OK)
      throw std::runtime_error("image SQL command failed: "+
         std::string(PQerrorMessage(pConn)));
   return aResult;
}

/** \brief One transaction covers both writes, including image bytes.
    \details The durable PostgreSQL cache is the source of truth. The model
    upsert copies the bytes from the cache rather than re-encoding or
    uploading an independent blob.
*/
void StoreInCacheAndModel(PGconn* pConn,std::string const& strUrl,
   deckkernel::scryfall::CachedImage const& aImage) {
   Command(pConn,"BEGIN");
   bool bCommitted=false;
   try {
      std::array<char const*,4> arrValues{
         strUrl.c_str(),aImage.strSha256.c_str(),
         reinterpret_cast<char const*>(aImage.vecBytes.data()),aImage.strMediaType.c_str()};
      std::array<int,4> arrLengths{
         0,0,static_cast<int>(aImage.vecBytes.size()),0};
      std::array<int,4> arrFormats{0,0,1,0};
      std::array<Oid,4> arrTypes{25,25,17,25}; // text, text, bytea, text
      pg_result_ty aCacheWrite(PQexecParams(pConn,
         "INSERT INTO deckkernel_cache.scryfall_images(url,sha256,image_data,media_type) "
         "VALUES($1,$2,$3,$4) ON CONFLICT(url) DO NOTHING",
         4,arrTypes.data(),arrValues.data(),
         arrLengths.data(),arrFormats.data(),0));
      if(!aCacheWrite||PQresultStatus(aCacheWrite.get())!=PGRES_COMMAND_OK)
         throw std::runtime_error("cache image INSERT failed: "+
            std::string(PQerrorMessage(pConn)));

      // Insert an asset when absent; update the existing URL-only placeholder.
      // Both branches use exactly the original bytes of the persistent cache.
      // The face/printing links and image_kind are left untouched.
      char const* arrModelValues[]={strUrl.c_str()};
      Oid const arrModelTypes[]={25};
      pg_result_ty aModelWrite(PQexecParams(pConn,
         "INSERT INTO deckkernel_test.scryfall_image_assets "
         "(image_url,sha256,image_data,media_type,downloaded_at) "
         "SELECT url,sha256,image_data,media_type,cached_at "
         "FROM deckkernel_cache.scryfall_images WHERE url=$1 "
         "ON CONFLICT(image_url) DO UPDATE SET "
         "sha256=EXCLUDED.sha256,image_data=EXCLUDED.image_data,"
         "media_type=EXCLUDED.media_type,downloaded_at=EXCLUDED.downloaded_at "
         "WHERE deckkernel_test.scryfall_image_assets.image_data IS NULL",
         1,arrModelTypes,arrModelValues,nullptr,nullptr,0));
      if(!aModelWrite||PQresultStatus(aModelWrite.get())!=PGRES_COMMAND_OK)
         throw std::runtime_error("model image INSERT/UPSERT failed: "+
            std::string(PQerrorMessage(pConn)));
      if(std::string_view(PQcmdTuples(aModelWrite.get()))!="1")
         throw std::runtime_error(
            "model image INSERT/UPSERT did not insert or fill a missing asset");

      Command(pConn,"COMMIT");
      bCommitted=true;
   }
   catch(...) {
      if(!bCommitted) {
         // Ignore a secondary rollback error; preserve the primary exception.
         pg_result_ty aRollback(PQexec(pConn,"ROLLBACK"));
      }
      throw;
   }
}

}

/** \brief Import missing images continuously without rebuilding the card model.
    \details First restore PostgreSQL cache entries in bulk, then process
    remaining model asset IDs in bounded keyset pages. No card parsing or
    schema creation occurs here. 0 means unlimited, and --images-only is
    unlimited by default unless DECKKERNEL_IMAGE_LIMIT is explicitly set.
*/
void ImportImages(postgres_database_ty const& aDatabase,
   ImageImportOptions const& aOptions) {
   (void)aDatabase; // The caller has already opened its normal database context.
   std::size_t const uLimit=
      aOptions.uMaxMissing.value_or(ImageLimit(aOptions.bContinuous));
   std::filesystem::path const aCacheRoot=Env("DECKKERNEL_IMAGE_CACHE_ROOT","");
   deckkernel::scryfall::ImageCache aCache(
      aCacheRoot.empty()?std::filesystem::path{".cache/scryfall/images"}:aCacheRoot);
   pg_connection_ty const aConn=Connect();

   // All permanent-cache hits are restored before selecting network candidates.
   pg_result_ty aRestored(PQexec(aConn.get(),
      "UPDATE deckkernel_test.scryfall_image_assets a SET "
      "image_data=c.image_data,sha256=c.sha256,media_type=c.media_type,"
      "downloaded_at=c.cached_at "
      "FROM deckkernel_cache.scryfall_images c "
      "WHERE a.image_url=c.url AND a.image_data IS NULL"));
   if(!aRestored || PQresultStatus(aRestored.get())!=PGRES_COMMAND_OK)
      throw std::runtime_error("image cache restore failed: "+
         std::string(PQerrorMessage(aConn.get())));
   std::println("[IMAGE-CACHE] restored into model: {}",
      PQcmdTuples(aRestored.get()));

   auto const fnCountMissing=[&]() -> std::uint64_t {
      pg_result_ty aCount(PQexec(aConn.get(),
         "SELECT count(*) FROM deckkernel_test.scryfall_image_assets "
         "WHERE image_data IS NULL"));
      if(!aCount || PQresultStatus(aCount.get())!=PGRES_TUPLES_OK ||
         PQntuples(aCount.get())!=1)
         throw std::runtime_error("image count query failed: "+
            std::string(PQerrorMessage(aConn.get())));
      std::string_view const svNumber=PQgetvalue(aCount.get(),0,0);
      std::uint64_t uCount{};
      auto const aParsed=std::from_chars(svNumber.data(),
         svNumber.data()+svNumber.size(),uCount);
      if(aParsed.ec!=std::errc{} || aParsed.ptr!=svNumber.data()+svNumber.size())
         throw std::runtime_error("image count is not an integer");
      return uCount;
   };
   std::uint64_t const uInitiallyMissing=fnCountMissing();
   std::println("[IMAGE] missing={}, limit={}, mode={}",
      uInitiallyMissing,uLimit==0?"unlimited":std::to_string(uLimit),
      aOptions.bAllowNetwork?"network+cache":"offline cache only");
   if(uInitiallyMissing==0) {
      std::println("[IMAGE] all model images already present; no downloads");
      return;
   }

   if(aOptions.bAllowNetwork && curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)
      throw std::runtime_error("image import curl_global_init failed");
   struct CurlCleanup {
      bool bInitialized;
      ~CurlCleanup(){if(bInitialized)curl_global_cleanup();}
   } aCurlCleanup{aOptions.bAllowNetwork};
   if(aOptions.bAllowNetwork) {
      curl_version_info_data const* pInfo=curl_version_info(CURLVERSION_NOW);
      std::println("[IMAGE-TLS] backend={}, native CA requested when supported; "
         "peer verification ON",
         pInfo && pInfo->ssl_version?pInfo->ssl_version:"<unknown>");
   }

   std::uint64_t uLastId{};
   std::size_t uProcessed{},uSaved{},uFailed{},uDiskHits{},uFetched{},uOfflineMissing{};
   auto aNextRequest=std::chrono::steady_clock::now();
   auto const aStarted=std::chrono::steady_clock::now();
   constexpr int iPageSize=512;

   // Keyset pagination: keep only 512 URL rows in memory at once, not the
   // potentially million-row catalog. Failed IDs are skipped until a future run.
   while(uLimit==0 || uProcessed<uLimit) {
      std::string const strCursor=std::to_string(uLastId);
      char const* arrCursor[]={strCursor.c_str()};
      Oid const arrTypes[]={20}; // PostgreSQL int8 for asset primary key
      pg_result_ty aPage(PQexecParams(aConn.get(),
         "SELECT id,image_url FROM deckkernel_test.scryfall_image_assets "
         "WHERE id > $1 AND image_data IS NULL ORDER BY id LIMIT 512",
         1,arrTypes,arrCursor,nullptr,nullptr,0));
      if(!aPage || PQresultStatus(aPage.get())!=PGRES_TUPLES_OK)
         throw std::runtime_error("image page query failed: "+
            std::string(PQerrorMessage(aConn.get())));
      int const iRows=PQntuples(aPage.get());
      if(iRows==0) break;

      for(int i=0;i<iRows && (uLimit==0 || uProcessed<uLimit);++i) {
         std::string_view const svId=PQgetvalue(aPage.get(),i,0);
         std::uint64_t uId{};
         auto const aParsed=std::from_chars(svId.data(),
            svId.data()+svId.size(),uId);
         if(aParsed.ec!=std::errc{} || aParsed.ptr!=svId.data()+svId.size() ||
            uId<=uLastId)
            throw std::runtime_error("invalid or unordered image asset ID");
         uLastId=uId;
         std::string const strUrl=PQgetvalue(aPage.get(),i,1);
         ++uProcessed;
         try {
            auto optCached=aCache.Find(strUrl);
            if(!optCached && !aOptions.bAllowNetwork) {
               ++uOfflineMissing;
               continue;
            }
            if(!optCached) {
               constexpr unsigned uMaxTransientRetries=12;
               for(unsigned uAttempt=0;;++uAttempt) {
                  std::this_thread::sleep_until(aNextRequest);
                  aNextRequest=std::chrono::steady_clock::now()+
                     std::chrono::milliseconds(250);
                  try {
                     auto vecDownloaded=DownloadImage(strUrl);
                     optCached=aCache.Store(strUrl,vecDownloaded);
                     ++uFetched;
                     break;
                  }
                  catch(ImageDownloadError const& ex) {
                     if(ex.IsFatal()) throw; // TLS trust is not transient.
                     if(!ex.IsTransient() || uAttempt>=uMaxTransientRetries)
                        throw;
                     // Exponential backoff, limited to five minutes unless the
                     // server explicitly requests a longer Retry-After delay.
                     std::chrono::seconds const aBackoff{
                        static_cast<int>(std::min(300U,5U << std::min(uAttempt,6U)))};
                     std::chrono::seconds const aDelay=
                        std::max(aBackoff,ex.aRetryAfter);
                     std::println(std::cerr,
                        "[IMAGE] transient HTTP={} curl={} retry={}/{} "
                        "in {}s: {}",
                        ex.iHttpStatus,static_cast<int>(ex.eCode),
                        uAttempt+1,uMaxTransientRetries,aDelay.count(),strUrl);
                     std::this_thread::sleep_for(aDelay);
                  }
               }
            }
            else ++uDiskHits;
            // Both PostgreSQL writes share one transaction; the model bytes
            // are selected from the authoritative permanent-cache row.
            StoreInCacheAndModel(aConn.get(),strUrl,*optCached);
            ++uSaved;
         }
         catch(ImageDownloadError const& ex) {
            ++uFailed;
            std::println(std::cerr,"[IMAGE] FAIL {}: {}",strUrl,ex.what());
            if(ex.IsFatal())
               throw std::runtime_error(
                  "TLS certificate or handshake failure; image import stopped "
                  "without losing previous database commits");
         }
         catch(std::exception const& ex) {
            ++uFailed;
            std::println(std::cerr,"[IMAGE] FAIL {}: {}",strUrl,ex.what());
            // Storage, configuration and database errors must stop immediately,
            // not repeat blindly across the remaining model.
            throw;
         }

         if(uProcessed%100==0) {
            double const flElapsed=std::chrono::duration<double>(
               std::chrono::steady_clock::now()-aStarted).count();
            std::uint64_t const uEstimateRemaining=
               uInitiallyMissing>uSaved?uInitiallyMissing-uSaved:0;
            double const flEtaSeconds=uSaved>0 ?
               flElapsed*static_cast<double>(uEstimateRemaining)/
                  static_cast<double>(uSaved):0.0;
            std::println("[IMAGE] processed={}, saved={}, failed={}, disk_hits={}, "
               "network={}, remaining~={}, elapsed={:.0f}s, ETA~={:.0f}s",
               uProcessed,uSaved,uFailed,uDiskHits,uFetched,
               uEstimateRemaining,flElapsed,flEtaSeconds);
         }
      }
   }

   std::uint64_t const uStillMissing=fnCountMissing();
   std::println("[IMAGE] result: processed={}, saved={}, failed={}, "
      "offline_missing={}, disk_hits={}, network_downloads={}, "
      "remaining={}, root={}",
      uProcessed,uSaved,uFailed,uOfflineMissing,uDiskHits,uFetched,
      uStillMissing,aCache.Root().string());
   if(uFailed)
      throw std::runtime_error(std::format(
         "{} image URLs could not be downloaded; saved images remain in "
         "both PostgreSQL schemas, rerun --images-only to retry",uFailed));
   if(uStillMissing==0)
      std::println("[IMAGE] completed: all known model image URLs populated");
   else if(uLimit!=0 && uProcessed>=uLimit)
      std::println("[IMAGE] batch limit reached; use --images-only without "
         "DECKKERNEL_IMAGE_LIMIT to continue automatically");
   else if(!aOptions.bAllowNetwork)
      std::println("[IMAGE] offline cache recovery complete; remaining URLs "
         "require a network import");
}
}
