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
#include <curl/curl.h>
#include <libpq-fe.h>
#include <openssl/evp.h>
#include <array>
#include <charconv>
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

std::size_t ImageLimit() {
   std::string const strLimit=Env("DECKKERNEL_IMAGE_LIMIT","100");
   std::size_t uLimit{};
   auto const aResult=std::from_chars(strLimit.data(),strLimit.data()+strLimit.size(),uLimit);
   if(aResult.ec!=std::errc{}||aResult.ptr!=strLimit.data()+strLimit.size()||uLimit>1000000)
      throw std::runtime_error("invalid DECKKERNEL_IMAGE_LIMIT (0..1000000)");
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

std::vector<unsigned char> DownloadImage(std::string const& strUrl) {
   if(!strUrl.starts_with("https://cards.scryfall.io/"))
      throw std::runtime_error("unapproved Scryfall image host");
   curl_handle_ty aCurl(curl_easy_init());
   if(!aCurl) throw std::runtime_error("curl_easy_init failed");
   std::vector<unsigned char> vecData;
   curl_easy_setopt(aCurl.get(),CURLOPT_URL,strUrl.c_str());
   curl_easy_setopt(aCurl.get(),CURLOPT_FOLLOWLOCATION,0L);
   curl_easy_setopt(aCurl.get(),CURLOPT_FAILONERROR,1L);
   curl_easy_setopt(aCurl.get(),CURLOPT_TIMEOUT,45L);
   curl_easy_setopt(aCurl.get(),CURLOPT_CONNECTTIMEOUT,10L);
   curl_easy_setopt(aCurl.get(),CURLOPT_SSL_VERIFYPEER,1L);
   curl_easy_setopt(aCurl.get(),CURLOPT_SSL_VERIFYHOST,2L);
   curl_easy_setopt(aCurl.get(),CURLOPT_USERAGENT,"adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)");
   curl_easy_setopt(aCurl.get(),CURLOPT_WRITEFUNCTION,&Receive);
   curl_easy_setopt(aCurl.get(),CURLOPT_WRITEDATA,&vecData);
   CURLcode const eCode=curl_easy_perform(aCurl.get());
   if(eCode!=CURLE_OK||vecData.empty())
      throw std::runtime_error(std::format("image download failed: {}",curl_easy_strerror(eCode)));
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
}

/** \brief Incrementally import missing Scryfall images into the asset table.
    \details Environment variable DECKKERNEL_IMAGE_LIMIT controls work per run;
    0 imports all pending images. SHA-256 is computed on downloaded bytes.
*/
void ImportImages(postgres_database_ty const& aDatabase) {
   (void)aDatabase; // Ensures FirstConnection has established its normal DB context.
   std::size_t const uLimit=ImageLimit();
   std::filesystem::path const aCacheRoot=Env("DECKKERNEL_IMAGE_CACHE_ROOT","");
   deckkernel::scryfall::ImageCache aCache(aCacheRoot.empty()?std::filesystem::path{".cache/scryfall/images"}:aCacheRoot);
   std::size_t uHits{},uFetched{};
   auto aNextRequest=std::chrono::steady_clock::now();
   pg_connection_ty const aConn=Connect();
   // Restore every available image into the newly created model before any HTTP.
   pg_result_ty aRestored(PQexec(aConn.get(),
      "UPDATE deckkernel_test.scryfall_image_assets a SET "
      "image_data=c.image_data,sha256=c.sha256,media_type=c.media_type,downloaded_at=c.cached_at "
      "FROM deckkernel_cache.scryfall_images c "
      "WHERE a.image_url=c.url AND a.image_data IS NULL"));
   if(!aRestored||PQresultStatus(aRestored.get())!=PGRES_COMMAND_OK)
      throw std::runtime_error("image cache restore failed: "+std::string(PQerrorMessage(aConn.get())));
   std::println("[IMAGE-CACHE] restored into model: {}",PQcmdTuples(aRestored.get()));
   std::string const strSql=
      "SELECT image_url FROM deckkernel_test.scryfall_image_assets "
      "WHERE image_data IS NULL ORDER BY id"
      +(uLimit?std::format(" LIMIT {}",uLimit):std::string{});
   pg_result_ty aRows(PQexec(aConn.get(),strSql.c_str()));
   if(!aRows||PQresultStatus(aRows.get())!=PGRES_TUPLES_OK)
      throw std::runtime_error("image catalog query failed: "+std::string(PQerrorMessage(aConn.get())));
   if(curl_global_init(CURL_GLOBAL_DEFAULT)!=CURLE_OK)
      throw std::runtime_error("image import curl_global_init failed");
   struct CurlCleanup { ~CurlCleanup(){curl_global_cleanup();} } aCurlCleanup;
   std::size_t uSaved{},uFailed{};
   for(int i=0;i<PQntuples(aRows.get());++i) {
      std::string const strUrl=PQgetvalue(aRows.get(),i,0);
      try {
         auto optCached=aCache.Find(strUrl);
         if(!optCached) {
            std::this_thread::sleep_until(aNextRequest);
            aNextRequest=std::chrono::steady_clock::now()+std::chrono::milliseconds(250);
            auto vecDownloaded=DownloadImage(strUrl);
            optCached=aCache.Store(strUrl,vecDownloaded);
            ++uFetched;
         }
         else ++uHits;
         auto const& vecBytes=optCached->vecBytes;
         std::string const& strSha=optCached->strSha256;
         // Durable PostgreSQL cache is the authority, model tables are disposable.
         std::array<char const*,4> arrCacheValues{
            strUrl.c_str(),strSha.c_str(),
            reinterpret_cast<char const*>(vecBytes.data()),optCached->strMediaType.c_str()};
         std::array<int,4> arrCacheLengths{0,0,static_cast<int>(vecBytes.size()),0};
         std::array<int,4> arrCacheFormats{0,0,1,0};
         std::array<Oid,4> arrCacheTypes{25,25,17,25};
         pg_result_ty aCacheWrite(PQexecParams(aConn.get(),
            "INSERT INTO deckkernel_cache.scryfall_images(url,sha256,image_data,media_type) "
            "VALUES($1,$2,$3,$4) ON CONFLICT(url) DO NOTHING",
            4,arrCacheTypes.data(),arrCacheValues.data(),
            arrCacheLengths.data(),arrCacheFormats.data(),0));
         if(!aCacheWrite||PQresultStatus(aCacheWrite.get())!=PGRES_COMMAND_OK)
            throw std::runtime_error("durable PostgreSQL cache write failed");

         // libpq binary parameter format 1 stores the original bytes directly.
         std::array<char const*,4> arrValues{
            strUrl.c_str(),strSha.c_str(),reinterpret_cast<char const*>(vecBytes.data()),optCached->strMediaType.c_str()};
         std::array<int,4> arrLengths{0,0,static_cast<int>(vecBytes.size()),0};
         std::array<int,4> arrFormats{0,0,1,0};
         std::array<Oid,4> arrTypes{25,25,17,25}; // text, text, bytea
         pg_result_ty aUpdate(PQexecParams(aConn.get(),
            "UPDATE deckkernel_test.scryfall_image_assets "
            "SET sha256=$2, media_type=$4, image_data=$3, downloaded_at=now() "
            "WHERE image_url=$1 AND image_data IS NULL",
            4,arrTypes.data(),arrValues.data(),arrLengths.data(),arrFormats.data(),0));
         if(!aUpdate||PQresultStatus(aUpdate.get())!=PGRES_COMMAND_OK)
            throw std::runtime_error("PostgreSQL bytea update failed: "+
               std::string(PQerrorMessage(aConn.get())));
         ++uSaved;
      }
      catch(std::exception const& ex) {
         ++uFailed;
         std::println(std::cerr,"[IMAGE] FAIL {}: {}",strUrl,ex.what());
      }
      if((i+1)%100==0) std::println("[IMAGE] {}/{} processed; saved={}, failed={}",
         i+1,PQntuples(aRows.get()),uSaved,uFailed);
   }
   std::println("[IMAGE] result: saved={}, failed={}, candidates={}, limit={}",
      uSaved,uFailed,PQntuples(aRows.get()),uLimit);
   std::println("[IMAGE-CACHE] hits={}, fetched={}, root={}",uHits,uFetched,aCache.Root().string());
   if(uFailed) throw std::runtime_error(std::format("{} image downloads failed",uFailed));
}
}
