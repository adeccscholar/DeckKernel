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

std::chrono::milliseconds ImageInterval() {
   std::string const strInterval=Env("DECKKERNEL_IMAGE_INTERVAL_MS","500");
   unsigned uMilliseconds{};
   auto const aParsed=std::from_chars(strInterval.data(),
      strInterval.data()+strInterval.size(),uMilliseconds);
   if(aParsed.ec!=std::errc{} ||
      aParsed.ptr!=strInterval.data()+strInterval.size() ||
      uMilliseconds<100 || uMilliseconds>60000)
      throw std::runtime_error(
         "DECKKERNEL_IMAGE_INTERVAL_MS must be between 100 and 60000");
   return std::chrono::milliseconds{uMilliseconds};
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
struct CurlHeaderDeleter {
   void operator()(curl_slist* pHeaders) const noexcept {
      if(pHeaders) { curl_slist_free_all(pHeaders); }
      }
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
using curl_headers_ty=std::unique_ptr<curl_slist,CurlHeaderDeleter>;

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
   std::string strServer;
   std::string strCfRay;
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
   // Keep only diagnostics from the final HTTP response.
   if(strLower.starts_with("http/")) {
      aHeaders={};
      return uBytes;
      }
   if(strLower.starts_with("server:") || strLower.starts_with("cf-ray:")) {
      std::size_t const uBegin=strLine.find_first_not_of(" \t",strLine.find(':')+1);
      std::size_t const uEnd=strLine.find_last_not_of(" \r\n\t");
      if(uBegin!=std::string::npos && uEnd!=std::string::npos && uBegin<=uEnd) {
         std::string strValue=strLine.substr(
            uBegin,std::min<std::size_t>(128,uEnd-uBegin+1));
         for(char& ch : strValue) {
            if(static_cast<unsigned char>(ch)<32) { ch=' '; }
            }
         if(strLower.starts_with("server:")) {
            aHeaders.strServer=std::move(strValue);
            }
         else {
            aHeaders.strCfRay=std::move(strValue);
            }
         }
      return uBytes;
      }
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

/** \brief Reusable libcurl easy handle for the complete sequential import.
    \details Keeping the handle alive enables HTTP keep-alive, connection
    reuse, and TLS session caching. Callback buffers are owned by this class.
*/
class ImageHttpClient final {
   // Declare headers first so they outlive curl_easy_cleanup.
   curl_headers_ty m_upHeaders {
      curl_slist_append(nullptr,"Accept: image/png,image/jpeg,image/webp,*/*;q=0.8") };
   curl_handle_ty m_upCurl { curl_easy_init() };
   std::string m_strCaBundle=Env("DECKKERNEL_CURL_CA_BUNDLE","");
   std::vector<unsigned char> m_vecData;
   ImageResponseHeaders m_aHeaders;
   std::array<char,CURL_ERROR_SIZE> m_arrError{};

public:
   ImageHttpClient() {
      if(!m_upHeaders) {
         throw std::runtime_error("curl_slist_append failed for image Accept header");
         }
      if(!m_upCurl) {
         throw std::runtime_error("curl_easy_init failed");
         }
      if(!m_strCaBundle.empty() &&
         !std::filesystem::is_regular_file(m_strCaBundle))
         throw std::runtime_error(
            "DECKKERNEL_CURL_CA_BUNDLE points to a missing file");
      CURL* const pCurl=m_upCurl.get();
      curl_easy_setopt(pCurl,CURLOPT_ERRORBUFFER,m_arrError.data());
      curl_easy_setopt(pCurl,CURLOPT_FOLLOWLOCATION,0L);
      curl_easy_setopt(pCurl,CURLOPT_FAILONERROR,1L);
      curl_easy_setopt(pCurl,CURLOPT_TIMEOUT,45L);
      curl_easy_setopt(pCurl,CURLOPT_CONNECTTIMEOUT,10L);
      curl_easy_setopt(pCurl,CURLOPT_SSL_VERIFYPEER,1L);
      curl_easy_setopt(pCurl,CURLOPT_SSL_VERIFYHOST,2L);
      curl_easy_setopt(pCurl,CURLOPT_SSLVERSION,CURL_SSLVERSION_TLSv1_2);
#if defined(CURLSSLOPT_NATIVE_CA)
      curl_easy_setopt(pCurl,CURLOPT_SSL_OPTIONS,
         static_cast<long>(CURLSSLOPT_NATIVE_CA));
#endif
      if(!m_strCaBundle.empty())
         curl_easy_setopt(pCurl,CURLOPT_CAINFO,m_strCaBundle.c_str());
      curl_easy_setopt(pCurl,CURLOPT_PROTOCOLS_STR,"https");
      curl_easy_setopt(pCurl,CURLOPT_REDIR_PROTOCOLS_STR,"https");
      curl_easy_setopt(pCurl,CURLOPT_USERAGENT,
         "adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)");
      curl_easy_setopt(pCurl,CURLOPT_HTTPHEADER,m_upHeaders.get());
      curl_easy_setopt(pCurl,CURLOPT_TCP_KEEPALIVE,1L);
      curl_easy_setopt(pCurl,CURLOPT_FRESH_CONNECT,0L);
      curl_easy_setopt(pCurl,CURLOPT_FORBID_REUSE,0L);
      curl_easy_setopt(pCurl,CURLOPT_WRITEFUNCTION,&Receive);
      curl_easy_setopt(pCurl,CURLOPT_WRITEDATA,&m_vecData);
      curl_easy_setopt(pCurl,CURLOPT_HEADERFUNCTION,&ReceiveHeader);
      curl_easy_setopt(pCurl,CURLOPT_HEADERDATA,&m_aHeaders);
   }

   ImageHttpClient(ImageHttpClient const&)=delete;
   ImageHttpClient& operator=(ImageHttpClient const&)=delete;

   struct Response final {
      std::vector<unsigned char> vecBytes;
      long iNewConnections{};
      double flDns{},flTcp{},flTls{},flFirstByte{};
   };

   [[nodiscard]] Response Download(std::string const& strUrl) {
      if(!strUrl.starts_with("https://cards.scryfall.io/"))
         throw std::runtime_error("unapproved Scryfall image host");
      m_vecData.clear();
      m_aHeaders={};
      m_arrError.fill('\0');
      CURL* const pCurl=m_upCurl.get();
      curl_easy_setopt(pCurl,CURLOPT_URL,strUrl.c_str());
      CURLcode const eCode=curl_easy_perform(pCurl);
      long iHttpStatus{};
      curl_easy_getinfo(pCurl,CURLINFO_RESPONSE_CODE,&iHttpStatus);
      if(eCode!=CURLE_OK) {
         std::string strDetail=std::format(
            "image HTTPS failed (curl={}, HTTP={}): {}",
            static_cast<int>(eCode),iHttpStatus,
            m_arrError[0]?m_arrError.data():curl_easy_strerror(eCode));
         if(iHttpStatus==403 || iHttpStatus==429) {
            strDetail+=std::format(" [Server={}, CF-Ray={}, Retry-After={}s]",
               m_aHeaders.strServer.empty()?"unknown":m_aHeaders.strServer,
               m_aHeaders.strCfRay.empty()?"unknown":m_aHeaders.strCfRay,
               m_aHeaders.aRetryAfter.count());
            }
         throw ImageDownloadError(
            eCode,iHttpStatus,m_aHeaders.aRetryAfter,std::move(strDetail));
         }
      if(m_vecData.empty())
         throw std::runtime_error("image server returned an empty body");
      Response aResult;
      curl_easy_getinfo(pCurl,CURLINFO_NUM_CONNECTS,&aResult.iNewConnections);
      curl_easy_getinfo(pCurl,CURLINFO_NAMELOOKUP_TIME,&aResult.flDns);
      curl_easy_getinfo(pCurl,CURLINFO_CONNECT_TIME,&aResult.flTcp);
      curl_easy_getinfo(pCurl,CURLINFO_APPCONNECT_TIME,&aResult.flTls);
      curl_easy_getinfo(pCurl,CURLINFO_STARTTRANSFER_TIME,&aResult.flFirstByte);
      aResult.vecBytes=std::move(m_vecData);
      return aResult;
   }
};

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
/** \brief One PostgreSQL statement atomically inserts into both schemas.
    \details The writable CTE returns the newly inserted cache row, avoiding
    a second read of that row from the table in the same statement snapshot.
    When the URL is already cached, the existing cache row supplies the
    identical binary payload. Implicit transaction means one network roundtrip
    and one synchronous commit, not BEGIN + 2 INSERTs + COMMIT.
*/
void StoreInCacheAndModel(PGconn* pConn,std::string const& strUrl,
   deckkernel::scryfall::CachedImage const& aImage) {
   std::array<char const*,4> arrValues{
      strUrl.c_str(),aImage.strSha256.c_str(),
      reinterpret_cast<char const*>(aImage.vecBytes.data()),aImage.strMediaType.c_str()};
   std::array<int,4> arrLengths{0,0,static_cast<int>(aImage.vecBytes.size()),0};
   std::array<int,4> arrFormats{0,0,1,0};
   std::array<Oid,4> arrTypes{25,25,17,25}; // text, text, bytea, text

   pg_result_ty aResult(PQexecParams(pConn,
      "WITH newly_cached AS ("
      "  INSERT INTO deckkernel_cache.scryfall_images "
      "  (url,sha256,image_data,media_type) VALUES($1,$2,$3,$4) "
      "  ON CONFLICT(url) DO NOTHING "
      "  RETURNING url,sha256,image_data,media_type,cached_at"
      "), cache_source AS ("
      "  SELECT url,sha256,image_data,media_type,cached_at FROM newly_cached "
      "  UNION ALL "
      "  SELECT url,sha256,image_data,media_type,cached_at "
      "  FROM deckkernel_cache.scryfall_images "
      "  WHERE url=$1 AND NOT EXISTS(SELECT 1 FROM newly_cached)"
      ") "
      "INSERT INTO deckkernel_test.scryfall_image_assets "
      "(image_url,sha256,image_data,media_type,downloaded_at) "
      "SELECT url,sha256,image_data,media_type,cached_at "
      "FROM cache_source WHERE true "
      "ON CONFLICT(image_url) DO UPDATE SET "
      "sha256=EXCLUDED.sha256,image_data=EXCLUDED.image_data,"
      "media_type=EXCLUDED.media_type,downloaded_at=EXCLUDED.downloaded_at "
      "WHERE deckkernel_test.scryfall_image_assets.image_data IS NULL",
      4,arrTypes.data(),arrValues.data(),
      arrLengths.data(),arrFormats.data(),0));
   if(!aResult || PQresultStatus(aResult.get())!=PGRES_COMMAND_OK)
      throw std::runtime_error("atomic cache/model image INSERT failed: "+
         std::string(PQerrorMessage(pConn)));
   if(std::string_view(PQcmdTuples(aResult.get()))!="1")
      throw std::runtime_error(
         "atomic cache/model image INSERT did not fill the model asset");
}

}

/** \brief Import missing images continuously without rebuilding the card model.
    \details First restore PostgreSQL cache entries in bulk, then process
    remaining model asset IDs in bounded keyset pages. No card parsing or
    schema creation occurs here. 0 means unlimited, and --images-only is
    unlimited by default unless DECKKERNEL_IMAGE_LIMIT is explicitly set.
*/
struct ImageImportMetrics final {
   double flHttpSeconds{};
   double flDiskLookupSeconds{};
   double flDiskWriteSeconds{};
   double flDatabaseSeconds{};
   double flThrottleSeconds{};
   double flRetrySeconds{};
   std::uint64_t uHttpBytes{};
   std::uint64_t uNewConnections{},uCacheDeleted{};
   double flDnsSeconds{},flTcpSeconds{},flTlsSeconds{},flFirstByteSeconds{};
};

void ImportImages(postgres_database_ty const& aDatabase,
   ImageImportOptions const& aOptions) {
   (void)aDatabase; // The caller has already opened its normal database context.
   std::size_t const uLimit=
      aOptions.uMaxMissing.value_or(ImageLimit(aOptions.bContinuous));
   // This floor also overrides legacy 100ms environment configuration.
   std::chrono::milliseconds const aRequestedInterval=ImageInterval();
   std::chrono::milliseconds aInterval=
      std::max(aRequestedInterval,std::chrono::milliseconds{500});
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
      "WHERE a.image_url=c.url AND a.image_data IS NULL "
       "AND EXISTS (SELECT 1 FROM deckkernel_test.dom_card_face_image_types t "
       "WHERE t.code=a.image_kind AND t.offline)"));
   if(!aRestored || PQresultStatus(aRestored.get())!=PGRES_COMMAND_OK)
      throw std::runtime_error("image cache restore failed: "+
         std::string(PQerrorMessage(aConn.get())));
   std::println("[IMAGE-CACHE] restored into model: {}",
      PQcmdTuples(aRestored.get()));

   auto const fnCountMissing=[&]() -> std::uint64_t {
      pg_result_ty aCount(PQexec(aConn.get(),
          "SELECT count(*) FROM deckkernel_test.scryfall_image_assets a "
          "JOIN deckkernel_test.dom_card_face_image_types t ON t.code=a.image_kind "
         "WHERE a.image_data IS NULL AND t.offline"));
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
   std::println(
      "[IMAGE] eligible missing={}, limit={}, mode={}, interval={}ms (requested={}ms)",
      uInitiallyMissing,uLimit==0?"unlimited":std::to_string(uLimit),
      aOptions.bAllowNetwork?"network+cache":"offline cache only",
      aInterval.count(),aRequestedInterval.count());
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
   std::unique_ptr<ImageHttpClient> upHttp;
   if(aOptions.bAllowNetwork) upHttp=std::make_unique<ImageHttpClient>();
   if(aOptions.bAllowNetwork) {
      curl_version_info_data const* pInfo=curl_version_info(CURLVERSION_NOW);
      std::println("[IMAGE-TLS] backend={}, native CA requested when supported; "
         "peer verification ON",
         pInfo && pInfo->ssl_version?pInfo->ssl_version:"<unknown>");
   }

   std::uint64_t uLastId{};
   std::size_t uProcessed{},uSaved{},uFailed{},uDiskHits{},uFetched{},uOfflineMissing{};
   ImageImportMetrics aMetrics;
   auto const fnSeconds=[](std::chrono::steady_clock::time_point const aBegin) {
      return std::chrono::duration<double>(
         std::chrono::steady_clock::now()-aBegin).count();
   };
   auto aNextRequest=std::chrono::steady_clock::now();
   unsigned uRateLimitEvents{};
   auto const aStarted=std::chrono::steady_clock::now();

   // Keyset pagination: keep only 512 URL rows in memory at once, not the
   // potentially million-row catalog. Failed IDs are skipped until a future run.
   while(uLimit==0 || uProcessed<uLimit) {
      std::string const strCursor=std::to_string(uLastId);
      char const* arrCursor[]={strCursor.c_str()};
      Oid const arrTypes[]={20}; // PostgreSQL int8 for asset primary key
      pg_result_ty aPage(PQexecParams(aConn.get(),
          "SELECT a.id,a.image_url FROM deckkernel_test.scryfall_image_assets a "
          "JOIN deckkernel_test.dom_card_face_image_types t ON t.code=a.image_kind "
         "WHERE a.id > $1 AND a.image_data IS NULL AND t.offline "
          "ORDER BY a.id LIMIT 512",
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
            auto const aLookupStarted=std::chrono::steady_clock::now();
            auto optCached=aCache.Find(strUrl);
            aMetrics.flDiskLookupSeconds+=fnSeconds(aLookupStarted);
            if(!optCached && !aOptions.bAllowNetwork) {
               ++uOfflineMissing;
               continue;
            }
            if(!optCached) {
               constexpr unsigned uMaxTransientRetries=12;
               for(unsigned uAttempt=0;;++uAttempt) {
                  auto const aThrottleStarted=std::chrono::steady_clock::now();
                  std::this_thread::sleep_until(aNextRequest);
                  aMetrics.flThrottleSeconds+=fnSeconds(aThrottleStarted);
                  aNextRequest=std::chrono::steady_clock::now()+aInterval;
                  auto const aHttpStarted=std::chrono::steady_clock::now();
                  try {
                     auto aResponse=upHttp->Download(strUrl);
                     aMetrics.flHttpSeconds+=fnSeconds(aHttpStarted);
                     aMetrics.uHttpBytes+=aResponse.vecBytes.size();
                     aMetrics.uNewConnections+=
                        static_cast<std::uint64_t>(aResponse.iNewConnections);
                     aMetrics.flDnsSeconds+=aResponse.flDns;
                     aMetrics.flTcpSeconds+=aResponse.flTcp;
                     aMetrics.flTlsSeconds+=aResponse.flTls;
                     aMetrics.flFirstByteSeconds+=aResponse.flFirstByte;
                     // Do not write new images to disk on the success path.
                     // Keep bytes in memory until both PostgreSQL schemas commit.
                     auto optType=deckkernel::scryfall::DetectImageType(
                        aResponse.vecBytes);
                     if(!optType) throw std::runtime_error(
                        "image has an unsupported signature");
                     deckkernel::scryfall::CachedImage aDownloaded{
                        std::move(aResponse.vecBytes),{},std::move(*optType)};
                     aDownloaded.strSha256=deckkernel::scryfall::Sha256(
                        aDownloaded.vecBytes);
                     optCached=std::move(aDownloaded);
                     ++uFetched;
                     break;
                  }
                  catch(ImageDownloadError const& ex) {
                     aMetrics.flHttpSeconds+=fnSeconds(aHttpStarted);
                     // Stop on 403: another URL would likely hit the same block.
                     if(ex.iHttpStatus==403) {
                        throw;
                        }
                     if(ex.iHttpStatus==429) {
                        // One recovery attempt for the entire import.
                        ++uRateLimitEvents;
                        if(uRateLimitEvents>1) {
                           throw;
                           }
                        aInterval=std::max(
                           aInterval*2,std::chrono::milliseconds{1000});
                        std::chrono::seconds const aDelay=std::max(
                           std::chrono::seconds{60},ex.aRetryAfter);
                        std::println(std::cerr,
                           "[IMAGE] HTTP 429: pause={}s, retry=1, interval={}ms, URL={}",
                           aDelay.count(),aInterval.count(),strUrl);
                        auto const aRetryStarted=std::chrono::steady_clock::now();
                        std::this_thread::sleep_for(aDelay);
                        aMetrics.flRetrySeconds+=fnSeconds(aRetryStarted);
                        aNextRequest=std::chrono::steady_clock::now()+aInterval;
                        continue;
                        }
                     if(ex.IsFatal()) {
                        throw;
                        }
                     if(!ex.IsTransient() || uAttempt>=uMaxTransientRetries) {
                        throw;
                        }
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
                     auto const aRetryStarted=std::chrono::steady_clock::now();
                     std::this_thread::sleep_for(aDelay);
                     aMetrics.flRetrySeconds+=fnSeconds(aRetryStarted);
                  }
               }
            }
            else ++uDiskHits;
            // Both inserts are one SQL statement and one atomic transaction.
            auto const aDatabaseStarted=std::chrono::steady_clock::now();
            try {
               StoreInCacheAndModel(aConn.get(),strUrl,*optCached);
            }
            catch(...) {
               // A downloaded image is still available in memory; preserve
               // it as a recovery copy if PostgreSQL failed. A cache hit
               // already has its durable local file.
               if(!aCache.Find(strUrl)) {
                  try {
                     static_cast<void>(aCache.Store(strUrl,optCached->vecBytes));
                  }
                  catch(...) {
                     std::println(std::cerr,
                        "[IMAGE-CACHE] WARNING: recovery file write failed for {}",strUrl);
                  }
               }
               throw;
            }
            aMetrics.flDatabaseSeconds+=fnSeconds(aDatabaseStarted);
            ++uSaved;
            try {
               if(aCache.RemoveCommitted(strUrl,optCached->strSha256))
                  ++aMetrics.uCacheDeleted;
            }
            catch(std::exception const& ex) {
               // Cleanup is best-effort after PostgreSQL has committed.
               std::println(std::cerr,
                  "[IMAGE-CACHE] cleanup warning for {}: {}",strUrl,ex.what());
            }
         }
         catch(ImageDownloadError const& ex) {
            ++uFailed;
            std::println(std::cerr,"[IMAGE] FAIL {}: {}",strUrl,ex.what());
            if(ex.iHttpStatus==403 || ex.iHttpStatus==429) {
               throw std::runtime_error(std::format(
                  "Scryfall image access rejected (HTTP {}). "
                  "Import stopped without modifying earlier commits. "
                  "Failed URL: {}",
                  ex.iHttpStatus,strUrl));
               }
            if(ex.IsFatal()) {
               throw std::runtime_error(
                  "TLS certificate or handshake failure; image import stopped "
                  "without losing previous database commits");
               }
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
            double const flHttpMbitSec=flElapsed>0.0?
               static_cast<double>(aMetrics.uHttpBytes)*8.0/
                  (flElapsed*1000000.0):0.0;
            double const flDbMsPerImage=uSaved>0?
               aMetrics.flDatabaseSeconds*1000.0/
                  static_cast<double>(uSaved):0.0;
            std::println("[IMAGE] processed={}, saved={}, failed={}, disk_hits={}, "
               "network={}, remaining~={}, elapsed={:.0f}s, ETA~={:.0f}s",
               uProcessed,uSaved,uFailed,uDiskHits,uFetched,
               uEstimateRemaining,flElapsed,flEtaSeconds);
            std::println("[IMAGE-PERF] HTTP={:.1f}s ({:.2f} Mbit/s overall), "
               "PG={:.1f}s ({:.1f} ms/image), disk_read={:.1f}s, "
               "disk_write={:.1f}s, throttle={:.1f}s, retries={:.1f}s, "
               "received={} MiB, new_connections={}, evicted={}",
               aMetrics.flHttpSeconds,flHttpMbitSec,
               aMetrics.flDatabaseSeconds,flDbMsPerImage,
               aMetrics.flDiskLookupSeconds,aMetrics.flDiskWriteSeconds,
               aMetrics.flThrottleSeconds,aMetrics.flRetrySeconds,
               aMetrics.uHttpBytes/(1024*1024),
               aMetrics.uNewConnections,aMetrics.uCacheDeleted);
         }
      }
   }

   std::uint64_t const uStillMissing=fnCountMissing();
   std::println("[IMAGE] result: processed={}, saved={}, failed={}, "
      "offline_missing={}, disk_hits={}, network_downloads={}, "
      "remaining={}, root={}",
      uProcessed,uSaved,uFailed,uOfflineMissing,uDiskHits,uFetched,
      uStillMissing,aCache.Root().string());
   std::println("[IMAGE-PERF] total: HTTP={:.1f}s, PG={:.1f}s, "
      "disk_read={:.1f}s, disk_write={:.1f}s, throttle={:.1f}s, "
      "retry_wait={:.1f}s, received={} MiB, new_connections={}, evicted={}",
      aMetrics.flHttpSeconds,aMetrics.flDatabaseSeconds,
      aMetrics.flDiskLookupSeconds,aMetrics.flDiskWriteSeconds,
      aMetrics.flThrottleSeconds,aMetrics.flRetrySeconds,
      aMetrics.uHttpBytes/(1024*1024),
      aMetrics.uNewConnections,aMetrics.uCacheDeleted);
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
