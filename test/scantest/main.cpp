// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/**
\file main.cpp
\brief Compute versioned OpenCV image descriptors for Scryfall artwork stored by FirstConnection.
\details One scan benchmark pass processes image assets lacking versioned descriptors.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/features2d.hpp>
#include <libpq-fe.h>
#include <array>
#include <charconv>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <print>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string Env(char const* szName,char const* szFallback) {
   if(char const* p=std::getenv(szName);p&&*p) return p;
   return szFallback;
}
struct PgRelease {
   void operator()(PGconn* p) const noexcept { if(p) PQfinish(p); }
};
struct ResultRelease {
   void operator()(PGresult* p) const noexcept { if(p) PQclear(p); }
};
using connection_ty=std::unique_ptr<PGconn,PgRelease>;
using result_ty=std::unique_ptr<PGresult,ResultRelease>;

connection_ty Connect() {
   std::array<std::string,7> arr{
      Env("DECKKERNEL_PGHOST","localhost"), Env("DECKKERNEL_PGPORT","5432"),
      Env("DECKKERNEL_PGDATABASE","DeckKernel"),Env("DECKKERNEL_PGUSER","deckkernel_user"),
      Env("DECKKERNEL_PGPASSWORD",""),Env("DECKKERNEL_PGSSLMODE","prefer"),
      Env("DECKKERNEL_PG_GSSENCMODE","disable")};
   char const* keys[]={"host","port","dbname","user","password","sslmode","gssencmode",nullptr};
   char const* values[]={arr[0].c_str(),arr[1].c_str(),arr[2].c_str(),arr[3].c_str(),
      arr[4].c_str(),arr[5].c_str(),arr[6].c_str(),nullptr};
   connection_ty aConn(PQconnectdbParams(keys,values,0));
   if(!aConn||PQstatus(aConn.get())!=CONNECTION_OK)
      throw std::runtime_error("PostgreSQL image feature connection failed");
   return aConn;
}

/** \brief Return a 64-bit difference hash represented as a binary SQL literal.
    \param aImage An OpenCV decoded image.
    \returns Exactly 64 characters containing 0 or 1.
*/
std::string DHash(cv::Mat const& aImage) {
   cv::Mat aGray,aSmall;
   if(aImage.channels()==1) aGray=aImage;
   else if(aImage.channels()==4) cv::cvtColor(aImage,aGray,cv::COLOR_BGRA2GRAY);
   else cv::cvtColor(aImage,aGray,cv::COLOR_BGR2GRAY);
   cv::resize(aGray,aSmall,cv::Size(9,8),0,0,cv::INTER_AREA);
   std::string strBits;
   strBits.reserve(64);
   for(int y=0;y<8;++y)
      for(int x=0;x<8;++x)
         strBits.push_back(aSmall.at<unsigned char>(y,x)>
            aSmall.at<unsigned char>(y,x+1)?'1':'0');
   return strBits;
}

void Store(PGconn* pConn,std::string const& strId,std::string const& strHash,
   cv::Mat const& aDescriptors,int iKeypoints) {
   std::string const strCount=std::to_string(iKeypoints);
   std::size_t const uSize=aDescriptors.total()*aDescriptors.elemSize();
   char const* pBytes=uSize?reinterpret_cast<char const*>(aDescriptors.data):"";
   std::array<char const*,4> vals{strId.c_str(),strHash.c_str(),pBytes,strCount.c_str()};
   std::array<int,4> lens{0,0,static_cast<int>(uSize),0};
   std::array<int,4> formats{0,0,1,0};
   std::array<Oid,4> types{20,1560,17,23}; // bigint, bit, bytea, integer
   result_ty aResult(PQexecParams(pConn,
      "INSERT INTO deckkernel_test.scryfall_image_features"
      "(asset_id,algorithm,algorithm_version,perceptual_hash,orb_descriptors,orb_keypoint_count)"
      "VALUES($1,'dhash64-orb',1,$2,$3,$4) "
      "ON CONFLICT(asset_id) DO UPDATE SET "
      "algorithm=excluded.algorithm,algorithm_version=excluded.algorithm_version,"
      "perceptual_hash=excluded.perceptual_hash,orb_descriptors=excluded.orb_descriptors,"
      "orb_keypoint_count=excluded.orb_keypoint_count,calculated_at=now()",
      4,types.data(),vals.data(),lens.data(),formats.data(),0));
   if(!aResult||PQresultStatus(aResult.get())!=PGRES_COMMAND_OK)
      throw std::runtime_error("PostgreSQL image feature write failed");
}
}
int main() {
   try {
      connection_ty aConn=Connect();
      std::string strLimit=Env("DECKKERNEL_SCAN_INDEX_LIMIT","100");
      unsigned uLimit{};
      auto aNumber=std::from_chars(strLimit.data(),strLimit.data()+strLimit.size(),uLimit);
      if(aNumber.ec!=std::errc{}||aNumber.ptr!=strLimit.data()+strLimit.size()||uLimit>1000000)
         throw std::runtime_error("invalid DECKKERNEL_SCAN_INDEX_LIMIT");
      std::string strSql=
         "SELECT a.id::text, a.image_data FROM deckkernel_test.scryfall_image_assets a "
         "LEFT JOIN deckkernel_test.scryfall_image_features f ON a.id=f.asset_id "
         "WHERE a.image_data IS NOT NULL AND "
         "(f.asset_id IS NULL OR f.algorithm<>'dhash64-orb' OR f.algorithm_version<>1) "
         "ORDER BY a.id"+(uLimit?std::format(" LIMIT {}",uLimit):std::string{});
      result_ty aRows(PQexec(aConn.get(),strSql.c_str()));
      if(!aRows||PQresultStatus(aRows.get())!=PGRES_TUPLES_OK)
         throw std::runtime_error("PostgreSQL feature source query failed");
      cv::Ptr<cv::ORB> aOrb=cv::ORB::create(500);
      std::size_t uPass{},uFail{};
      for(int i=0;i<PQntuples(aRows.get());++i) {
         try {
            std::string const strId=PQgetvalue(aRows.get(),i,0);
            std::size_t uSize{};
            std::unique_ptr<unsigned char,void(*)(void*)> aBytes(
               PQunescapeBytea(reinterpret_cast<unsigned char const*>(PQgetvalue(aRows.get(),i,1)),&uSize),
               [](void* p){if(p) PQfreemem(p);});
            if(!aBytes||!uSize) throw std::runtime_error("empty bytea image");
            cv::Mat aEncoded(1,static_cast<int>(uSize),CV_8UC1,aBytes.get());
            cv::Mat aImage=cv::imdecode(aEncoded,cv::IMREAD_COLOR);
            if(aImage.empty()) throw std::runtime_error("OpenCV cannot decode stored image");
            std::string const strHash=DHash(aImage);
            std::vector<cv::KeyPoint> vecKeypoints;
            cv::Mat aDescriptors;
            aOrb->detectAndCompute(aImage,cv::noArray(),vecKeypoints,aDescriptors);
            Store(aConn.get(),strId,strHash,aDescriptors,static_cast<int>(vecKeypoints.size()));
            ++uPass;
         }
         catch(std::exception const& ex) {
            ++uFail;
            std::println(std::cerr,"[SCAN-INDEX] FAIL: {}",ex.what());
         }
      }
      std::println("SMOKE|CHECK|feature-index|{}|indexed={},failed={}",
         uFail?"FAIL":"PASS",uPass,uFail);
      std::println("SMOKE|RESULT|{}|DeckKernel artwork feature indexing",uFail?"FAIL":"PASS");
      return uFail?1:0;
   }
   catch(std::exception const& ex) {
      std::println(std::cerr,"[SCAN-INDEX] {}",ex.what());
      std::println("SMOKE|RESULT|FAIL|DeckKernel artwork feature indexing");
      return 1;
   }
}
