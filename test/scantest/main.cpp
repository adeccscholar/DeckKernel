// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/**
\file main.cpp
\brief Compute versioned OpenCV image descriptors for Scryfall artwork stored by FirstConnection.
\details Index all pending stored images in bounded batches; optional follow mode picks up new imports.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/features2d.hpp>
#include <libpq-fe.h>
#include <array>
#include <charconv>
#include <cstdint>
#include <chrono>
#include <thread>
#include <string_view>
#include <cstdlib>
#include <format>
#include <iostream>
#include <memory>
#include <limits>
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
unsigned ParseUnsigned(std::string_view value,char const* field) {
   unsigned number{};
   auto const result=std::from_chars(value.data(),value.data()+value.size(),number);
   if(result.ec!=std::errc{}||result.ptr!=value.data()+value.size())
      throw std::runtime_error(std::string("invalid ")+field);
   return number;
}

int main(int argc,char** argv) {
   try {
      // This is an independent PROCESS; it can run concurrently with
      // FirstConnection's image importer without touching the download cache.
      bool follow=false;
      unsigned batchSize=32,limit=0,pollSeconds=5;
      bool explicitLimit=false;
      for(int i=1;i<argc;++i) {
         std::string_view const arg(argv[i]);
         if(arg=="--follow")follow=true;
         else if(arg=="--all"){limit=0;explicitLimit=true;}
         else if(arg.starts_with("--batch-size="))
            batchSize=ParseUnsigned(arg.substr(13),"--batch-size");
         else if(arg.starts_with("--limit=")) {
            limit=ParseUnsigned(arg.substr(8),"--limit");
            explicitLimit=true;
         }
         else if(arg.starts_with("--poll-seconds="))
            pollSeconds=ParseUnsigned(arg.substr(15),"--poll-seconds");
         else if(arg=="--help") {
            std::println("deckkernel_card_scantest [--follow] [--batch-size=32] "
               "[--all|--limit=N] [--poll-seconds=5]");
            std::println("  --follow: index newly downloaded images without restarting "
               "the Scryfall importer (Ctrl+C to stop)");
            std::println("  --all or --limit=0: index all pending images (default)");
            std::println("  --limit=N: opt-in cap on successfully processed images");
            return 0;
         }
         else throw std::runtime_error("unknown scan option; use --help");
      }
      if(batchSize==0||batchSize>256||pollSeconds==0||pollSeconds>300)
         throw std::runtime_error("batch size must be 1..256, poll seconds 1..300");
      if(!explicitLimit)
         limit=ParseUnsigned(Env("DECKKERNEL_SCAN_INDEX_LIMIT","0"),
            "DECKKERNEL_SCAN_INDEX_LIMIT");

      connection_ty aConn=Connect();
      cv::Ptr<cv::ORB> aOrb=cv::ORB::create(500);
      std::size_t indexed=0,failed=0;
      std::uint64_t lastId=0;
      unsigned passes=0;
      std::println("[SCAN-INDEX] mode={} batch={} limit={} poll={}s",
         follow?"follow":"once",batchSize,limit?std::to_string(limit):"ALL",pollSeconds);
      for(;;) {
         // Keyset pagination avoids repeatedly fetching the entire table and
         // respects already committed features from previous processes.
         unsigned const request=limit?
            static_cast<unsigned>(std::min<std::size_t>(
               batchSize,static_cast<std::size_t>(limit)-indexed)):batchSize;
         if(request==0)break;
         std::string const sql=std::format(
            "SELECT a.id::text, a.image_data "
            "FROM deckkernel_test.scryfall_image_assets a "
            "LEFT JOIN deckkernel_test.scryfall_image_features f ON f.asset_id=a.id "
            "WHERE a.id > {} AND a.image_data IS NOT NULL AND "
            "(f.asset_id IS NULL OR f.algorithm<>'dhash64-orb' OR f.algorithm_version<>1) "
            "ORDER BY a.id LIMIT {}",lastId,request);
         result_ty rows(PQexec(aConn.get(),sql.c_str()));
         if(!rows||PQresultStatus(rows.get())!=PGRES_TUPLES_OK)
            throw std::runtime_error(std::string("PostgreSQL feature query failed: ")+
               PQerrorMessage(aConn.get()));
         int const n=PQntuples(rows.get());
         if(!n) {
            if(!follow)break;
            // Recheck committed rows and newly arriving assets on each poll.
            // A failed decode can be retried on a later pass; the failures
            // are reported, not silently treated as indexed.
            lastId=0;
            ++passes;
            std::println("[SCAN-INDEX] idle: indexed={} failed={} sweep={}; "
               "polling in {}s",indexed,failed,passes,pollSeconds);
            rows.reset(); // free PostgreSQL batch before sleeping
            std::this_thread::sleep_for(std::chrono::seconds(pollSeconds));
            continue;
         }
         for(int i=0;i<n;++i) {
            auto const idText=std::string(PQgetvalue(rows.get(),i,0));
            std::uint64_t id{};
            auto const parsed=std::from_chars(idText.data(),
               idText.data()+idText.size(),id);
            if(parsed.ec!=std::errc{}||parsed.ptr!=idText.data()+idText.size())
               throw std::runtime_error("invalid image id returned by PostgreSQL");
            lastId=id;
            try {
               std::size_t size{};
               std::unique_ptr<unsigned char,void(*)(void*)> bytes(
                  PQunescapeBytea(reinterpret_cast<unsigned char const*>(
                     PQgetvalue(rows.get(),i,1)),&size),
                  [](void* p){if(p)PQfreemem(p);});
               if(!bytes||!size)throw std::runtime_error("empty bytea image");
               if(size>static_cast<std::size_t>(std::numeric_limits<int>::max()))
                  throw std::runtime_error("image exceeds OpenCV input bounds");
               cv::Mat encoded(1,static_cast<int>(size),CV_8UC1,bytes.get());
               cv::Mat image=cv::imdecode(encoded,cv::IMREAD_COLOR);
               if(image.empty())throw std::runtime_error("OpenCV cannot decode image");
               std::string const hash=DHash(image);
               std::vector<cv::KeyPoint> keypoints;
               cv::Mat descriptors;
               aOrb->detectAndCompute(image,cv::noArray(),keypoints,descriptors);
               Store(aConn.get(),idText,hash,descriptors,
                  static_cast<int>(keypoints.size()));
               ++indexed;
            } catch(std::exception const& ex) {
               ++failed;
               std::println(std::cerr,"[SCAN-INDEX] asset={} failed: {}",
                  idText,ex.what());
            }
         }
         std::println("[SCAN-INDEX] batch complete: total={} failed={} last-id={}",
            indexed,failed,lastId);
         if(limit&&indexed>=limit)break;
      }
      std::println("SMOKE|CHECK|feature-index|{}|indexed={},failed={}",
         failed?"FAIL":"PASS",indexed,failed);
      std::println("SMOKE|RESULT|{}|DeckKernel artwork feature indexing",
         failed?"FAIL":"PASS");
      return failed?1:0;
   }
   catch(std::exception const& ex) {
      std::println(std::cerr,"[SCAN-INDEX] {}",ex.what());
      std::println("SMOKE|RESULT|FAIL|DeckKernel artwork feature indexing");
      return 1;
   }
}
