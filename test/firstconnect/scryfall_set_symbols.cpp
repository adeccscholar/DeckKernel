// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/** \file scryfall_set_symbols.cpp
    \brief Acquire and retain original Scryfall set symbols for offline matching.
*/
#include "scryfall_set_symbols.h"
#include "scryfall_load.h"
#include <curl/curl.h>
#include <libpq-fe.h>
#include <openssl/evp.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <format>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace deckkernel::test {
namespace {
struct CurlDeleter {void operator()(CURL* p)const noexcept {if(p)curl_easy_cleanup(p);}};
struct PgDeleter {void operator()(PGconn* p)const noexcept {if(p)PQfinish(p);}};
struct ResultDeleter {void operator()(PGresult* p)const noexcept {if(p)PQclear(p);}};
using Curl=std::unique_ptr<CURL,CurlDeleter>;
using Conn=std::unique_ptr<PGconn,PgDeleter>;
using Rows=std::unique_ptr<PGresult,ResultDeleter>;

std::string Env(char const* key,char const* fallback) {
   if(auto p=std::getenv(key);p&&*p)return p;
   return fallback;
}
Conn Connect() {
   std::array<std::string,7> v{
      Env("DECKKERNEL_PGHOST","localhost"),Env("DECKKERNEL_PGPORT","5432"),
      Env("DECKKERNEL_PGDATABASE","DeckKernel"),
      Env("DECKKERNEL_PGUSER","deckkernel_user"),
      Env("DECKKERNEL_PGPASSWORD",""),Env("DECKKERNEL_PGSSLMODE","prefer"),
      Env("DECKKERNEL_PG_GSSENCMODE","disable")};
   char const* keys[]={"host","port","dbname","user","password","sslmode","gssencmode",nullptr};
   char const* vals[]={v[0].c_str(),v[1].c_str(),v[2].c_str(),v[3].c_str(),
      v[4].c_str(),v[5].c_str(),v[6].c_str(),nullptr};
   Conn c(PQconnectdbParams(keys,vals,0));
   if(!c||PQstatus(c.get())!=CONNECTION_OK)
      throw std::runtime_error("set symbol DB connection failed: "+
         std::string(c?PQerrorMessage(c.get()):"null"));
   return c;
}
Rows Execute(PGconn* c,char const* sql) {
   Rows r(PQexec(c,sql));
   if(!r || (PQresultStatus(r.get())!=PGRES_TUPLES_OK &&
      PQresultStatus(r.get())!=PGRES_COMMAND_OK))
      throw std::runtime_error("set symbol SQL failed: "+std::string(PQerrorMessage(c)));
   return r;
}
size_t Receive(char* bytes,size_t unit,size_t n,void* user) {
   auto& out=*static_cast<std::string*>(user);
   size_t const size=unit*n;
   if(size>1024*1024 || out.size()>1024*1024-size)return 0;
   out.append(bytes,size);
   return size;
}
bool Allowed(std::string const& url) {
   // This endpoint is untrusted input from API metadata. Restrict downloads
   // to the official SVG origin and exact /sets/ path, no user-info/port.
   constexpr std::string_view prefix="https://svgs.scryfall.io/sets/";
   if(!url.starts_with(prefix))return false;
   auto const suffix=std::string_view(url).substr(prefix.size());
   if(suffix.empty() || suffix.size()>220 || suffix.find(".svg")==std::string_view::npos)
      return false;
   for(char ch:suffix)
      if(!((ch>='a'&&ch<='z')||(ch>='A'&&ch<='Z')||
         (ch>='0'&&ch<='9')||ch=='-'||ch=='_'||ch=='.'||ch=='?'||
         ch=='='||ch=='&'))return false;
   return true;
}
std::string Download(CURL* curl,std::string const& url) {
   std::string result;
   curl_easy_setopt(curl,CURLOPT_URL,url.c_str());
   curl_easy_setopt(curl,CURLOPT_WRITEDATA,&result);
   CURLcode const rc=curl_easy_perform(curl);
   long status{};
   curl_easy_getinfo(curl,CURLINFO_RESPONSE_CODE,&status);
   if(rc!=CURLE_OK || status!=200)
      throw std::runtime_error(std::format("set SVG download failed curl={} HTTP={}",
         static_cast<int>(rc),status));
   return result;
}
std::string Sha256(std::string const& data) {
   std::array<unsigned char,EVP_MAX_MD_SIZE> buffer{};
   unsigned length{};
   if(EVP_Digest(data.data(),data.size(),buffer.data(),&length,EVP_sha256(),nullptr)!=1
      ||length!=32)throw std::runtime_error("set SVG SHA256 failed");
   constexpr char chars[]="0123456789abcdef";
   std::string result;
   result.reserve(64);
   for(unsigned i=0;i<length;++i) {
      result.push_back(chars[buffer[i]>>4]);
      result.push_back(chars[buffer[i]&15]);
   }
   return result;
}
void CheckSvg(std::string const& svg) {
   if(svg.size()<30 || svg.size()>1024*1024 ||
      svg.find("<svg")==std::string::npos ||
      svg.find("</svg>")==std::string::npos ||
      svg.find("<script")!=std::string::npos ||
      svg.find("<!ENTITY")!=std::string::npos ||
      svg.find("<!DOCTYPE")!=std::string::npos ||
      svg.find("javascript:")!=std::string::npos ||
      svg.find("<foreignObject")!=std::string::npos)
      throw std::runtime_error("set symbol is not a safe bounded SVG payload");
   // Store original bytes only. An SVG renderer must still run in restricted
   // no-script/no-network mode before any raster template is generated.
}
void Store(PGconn* db,int setId,std::string const& url,std::string const& svg) {
   std::string const id=std::to_string(setId);
   std::string const digest=Sha256(svg);
   std::array<char const*,4> values{id.c_str(),url.c_str(),digest.c_str(),svg.data()};
   std::array<int,4> lengths{0,0,0,static_cast<int>(svg.size())};
   std::array<int,4> formats{0,0,0,1};
   std::array<Oid,4> types{23,25,25,17};
   Rows result(PQexecParams(db,
      "INSERT INTO deckkernel_test.scryfall_set_symbols "
      "(set_id,icon_svg_uri,sha256,svg_data) VALUES ($1,$2,$3,$4) "
      "ON CONFLICT(set_id) DO UPDATE SET "
      "icon_svg_uri=EXCLUDED.icon_svg_uri, "
      "sha256=EXCLUDED.sha256, svg_data=EXCLUDED.svg_data, "
      "downloaded_at=now() "
      "WHERE deckkernel_test.scryfall_set_symbols.sha256 IS DISTINCT FROM EXCLUDED.sha256 "
      "OR deckkernel_test.scryfall_set_symbols.icon_svg_uri IS DISTINCT FROM EXCLUDED.icon_svg_uri",
      4,types.data(),values.data(),lengths.data(),formats.data(),0));
   if(!result||PQresultStatus(result.get())!=PGRES_COMMAND_OK)
      throw std::runtime_error("set SVG storage failed: "+std::string(PQerrorMessage(db)));
}
} // namespace

void SyncSetSymbols(SetSymbolImportOptions options) {
   Conn db=Connect();
   Execute(db.get(),
      "CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_set_symbols ("
      "set_id integer PRIMARY KEY REFERENCES deckkernel_test.scryfall_sets(id),"
      "icon_svg_uri text NOT NULL,"
      "sha256 text NOT NULL,"
      "svg_data bytea NOT NULL,"
      "downloaded_at timestamptz NOT NULL DEFAULT now())");
   auto const rows=Execute(db.get(),
      "SELECT s.id::text,s.code,COALESCE(x.sha256,'') "
      "FROM deckkernel_test.scryfall_sets s "
      "LEFT JOIN deckkernel_test.scryfall_set_symbols x ON x.set_id=s.id "
      "ORDER BY s.code");
   if(options.offlineOnly) {
      unsigned missing=0;
      for(int i=0;i<PQntuples(rows.get());++i)
         if(*PQgetvalue(rows.get(),i,2)=='\0')++missing;
      std::println("[SET-SYMBOL] offline sets={} missing={} (no network)",
         PQntuples(rows.get()),missing);
      return;
   }
   // The set catalog includes icon_svg_uri, whereas bulk cards do not.
   auto const catalog=nlohmann::json::parse(ReadCardSets());
   std::map<std::string,std::string> icons;
   for(auto const& entry:catalog.at("data")) {
      if(!entry.contains("icon_svg_uri") || entry["icon_svg_uri"].is_null())continue;
      icons.emplace(entry.at("code").get<std::string>(),
         entry.at("icon_svg_uri").get<std::string>());
   }
   Curl curl(curl_easy_init());
   if(!curl)throw std::runtime_error("curl_easy_init failed");
   curl_easy_setopt(curl.get(),CURLOPT_WRITEFUNCTION,&Receive);
   curl_easy_setopt(curl.get(),CURLOPT_FOLLOWLOCATION,0L);
   curl_easy_setopt(curl.get(),CURLOPT_FAILONERROR,1L);
   curl_easy_setopt(curl.get(),CURLOPT_CONNECTTIMEOUT,10L);
   curl_easy_setopt(curl.get(),CURLOPT_TIMEOUT,35L);
   curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYPEER,1L);
   curl_easy_setopt(curl.get(),CURLOPT_SSL_VERIFYHOST,2L);
   curl_easy_setopt(curl.get(),CURLOPT_USERAGENT,
      "adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)");
   unsigned downloaded=0,skipped=0,missing=0;
   for(int i=0;i<PQntuples(rows.get());++i) {
      std::string const code=PQgetvalue(rows.get(),i,1);
      auto const it=icons.find(code);
      if(it==icons.end()) {++missing;continue;}
      if(!Allowed(it->second))
         throw std::runtime_error("Scryfall set symbol URL rejected: "+code);
      if(*PQgetvalue(rows.get(),i,2)!='\0'){++skipped;continue;}
      // Rate-limit independent symbol requests. Never query from the camera.
      std::this_thread::sleep_for(std::chrono::milliseconds(250));
      std::string const svg=Download(curl.get(),it->second);
      CheckSvg(svg);
      Store(db.get(),std::stoi(PQgetvalue(rows.get(),i,0)),it->second,svg);
      ++downloaded;
      if(options.probeOnly)break;
   }
   std::println("[SET-SYMBOL] stored={} already-cached={} without-icon={}",
      downloaded,skipped,missing);
}
} // namespace deckkernel::test
