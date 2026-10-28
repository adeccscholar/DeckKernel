// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/** \file main.cpp
    \brief First-stage camera card detector: present, stable, changed, absent.
    \details No Scryfall requests, no database access, no physical edition
             identification. Image evidence is optional and local.
*/
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#if defined(DECKKERNEL_CAMERA_HAS_SDL_SKIA)
#include "CameraDashboard.h"
#endif
#include "CameraOcrZones.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry.hpp>
#include <opencv2/imgcodecs.hpp>
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
#include <libpq-fe.h>
#endif
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
#include <tesseract/baseapi.h>
#endif
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
#include <opencv2/videoio.hpp>
#endif
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <charconv>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <opencv2/features2d.hpp>
#include <cctype>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using hash_ty=std::uint64_t;
hash_ty DHash(cv::Mat const& source) {
   cv::Mat gray,small;
   if(source.channels()==3)cv::cvtColor(source,gray,cv::COLOR_BGR2GRAY);
   else if(source.channels()==4)cv::cvtColor(source,gray,cv::COLOR_BGRA2GRAY);
   else gray=source;
   cv::resize(gray,small,cv::Size(9,8),0,0,cv::INTER_AREA);
   hash_ty uResult{};
   for(int y=0;y<8;++y)
      for(int x=0;x<8;++x)
         uResult=(uResult<<1)|
            (small.at<unsigned char>(y,x)>small.at<unsigned char>(y,x+1)?1ULL:0ULL);
   return uResult;
}
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
// The camera owns a bounded, single-slot worker; only the worker may use
// its PostgreSQL connection or OpenCV ORB matcher. No camera-frame IO or
// expensive SQL query runs on the capture/preview thread.
class ArtworkMatcher {
   struct Task {unsigned event{}; cv::Mat card;};
   std::mutex m_mutex;
   std::condition_variable m_cv;
   std::optional<Task> m_pending;
   bool m_stopping{};
   std::thread m_worker;
   unsigned m_candidates{};
   static std::string Env(char const* k,char const* fallback) {
      if(auto const* value=std::getenv(k);value&&*value)return value;
      return fallback;
   }
   struct PgDelete {void operator()(PGconn* p) const noexcept {if(p)PQfinish(p);}};
   struct RowsDelete {void operator()(PGresult* p) const noexcept {if(p)PQclear(p);}};
   using Conn=std::unique_ptr<PGconn,PgDelete>;
   using Rows=std::unique_ptr<PGresult,RowsDelete>;
   static Conn Connect() {
      std::array<std::string,7> fields{
         Env("DECKKERNEL_PGHOST","localhost"),Env("DECKKERNEL_PGPORT","5432"),
         Env("DECKKERNEL_PGDATABASE","DeckKernel"),
         Env("DECKKERNEL_PGUSER","deckkernel_user"),
         Env("DECKKERNEL_PGPASSWORD",""),
         Env("DECKKERNEL_PGSSLMODE","prefer"),
         Env("DECKKERNEL_PG_GSSENCMODE","disable")};
      char const* keys[]={"host","port","dbname","user","password","sslmode","gssencmode",nullptr};
      char const* values[]={fields[0].c_str(),fields[1].c_str(),fields[2].c_str(),
         fields[3].c_str(),fields[4].c_str(),fields[5].c_str(),fields[6].c_str(),nullptr};
      Conn db(PQconnectdbParams(keys,values,0));
      if(!db||PQstatus(db.get())!=CONNECTION_OK)
         throw std::runtime_error("artwork worker PostgreSQL connection failed");
      return db;
   }
   static cv::Mat Art(cv::Mat const& card) {
      int const x=card.cols/12,y=card.rows*19/100;
      return card(cv::Rect(x,y,card.cols-x*2,card.rows*33/100));
   }
   static std::string Bits(hash_ty h) {
      std::string bits(64,'0');
      for(unsigned i=0;i<64;++i)bits[i]=((h>>(63-i))&1U)?'1':'0';
      return bits;
   }
   void Match(PGconn* db,Task const& task) {
      cv::Mat const source=Art(task.card);
      std::string const hash=Bits(DHash(source));
      std::string const count=std::to_string(m_candidates);
      char const* params[]={hash.c_str(),count.c_str()};
      // Query scans the small 64-bit feature records, not a blob full scan.
      // The feature indexer is a separate, incremental producer.
      Rows rows(PQexecParams(db,
         "SELECT a.id::text, cf.name, a.image_data, "
         "bit_count(f.perceptual_hash # $1::bit(64))::text "
         "FROM deckkernel_test.scryfall_image_features f "
         "JOIN deckkernel_test.scryfall_image_assets a ON a.id=f.asset_id "
         "JOIN deckkernel_test.scryfall_card_image_variants v "
         "  ON v.asset_id=a.id AND v.variant='art_crop' "
         "JOIN deckkernel_test.scryfall_card_faces cf "
         "  ON cf.card_id=v.card_id AND cf.face_id=v.face_id "
         "WHERE f.algorithm='dhash64-orb' AND f.algorithm_version=1 "
         "  AND f.perceptual_hash IS NOT NULL AND a.image_data IS NOT NULL "
         "ORDER BY bit_count(f.perceptual_hash # $1::bit(64)),a.id "
         "LIMIT $2::integer",
         2,nullptr,params,nullptr,nullptr,0));
      if(!rows||PQresultStatus(rows.get())!=PGRES_TUPLES_OK)
         throw std::runtime_error(std::string("artwork feature query failed: ")+PQerrorMessage(db));
      cv::Ptr<cv::ORB> orb=cv::ORB::create(500);
      std::vector<cv::KeyPoint> sourcePoints;
      cv::Mat sourceDesc;
      orb->detectAndCompute(source,cv::noArray(),sourcePoints,sourceDesc);
      cv::BFMatcher matcher(cv::NORM_HAMMING,false);
      int bestOrb=-1,bestHash=65;
      std::string bestName,bestId;
      int tested=0;
      for(int i=0;i<PQntuples(rows.get());++i) {
         std::size_t n{};
         std::unique_ptr<unsigned char,void(*)(void*)> bytes(
            PQunescapeBytea(reinterpret_cast<unsigned char const*>(PQgetvalue(rows.get(),i,2)),&n),
            [](void* p){if(p)PQfreemem(p);});
         if(!bytes||!n||n>8*1024*1024)continue;
         cv::Mat encoded(1,static_cast<int>(n),CV_8UC1,bytes.get());
         cv::Mat reference=cv::imdecode(encoded,cv::IMREAD_COLOR);
         if(reference.empty())continue;
         std::vector<cv::KeyPoint> points;
         cv::Mat desc;
         orb->detectAndCompute(reference,cv::noArray(),points,desc);
         int good=0;
         if(!sourceDesc.empty()&&!desc.empty()) {
            std::vector<std::vector<cv::DMatch>> pairs;
            matcher.knnMatch(sourceDesc,desc,pairs,2);
            for(auto const& pair:pairs)
               if(pair.size()==2 && pair[0].distance<0.75f*pair[1].distance)
                  ++good;
         }
         ++tested;
         int distance=std::stoi(PQgetvalue(rows.get(),i,3));
         if(good>bestOrb||(good==bestOrb&&distance<bestHash)) {
            bestOrb=good;bestHash=distance;
            bestId=PQgetvalue(rows.get(),i,0);
            bestName=PQgetvalue(rows.get(),i,1);
         }
      }
      if(tested)
         std::println("[CAM-ART] card #{} candidate=\"{}\" asset={} dhash={}/64 "
            "orb-good={} shortlisted={} status=UNVERIFIED",
            task.event,bestName,bestId,bestHash,bestOrb,tested);
      else
         std::println("[CAM-ART] card #{} NO_INDEXED_ART; run scantest --all",task.event);
   }
   void Run() {
      try {
         Conn db=Connect();
         for(;;) {
            Task task;
            {
               std::unique_lock lock(m_mutex);
               m_cv.wait(lock,[this]{return m_stopping||m_pending.has_value();});
               if(m_stopping&&!m_pending)break;
               task=std::move(*m_pending);
               m_pending.reset();
            }
            try {Match(db.get(),task);}
            catch(std::exception const& e) {
               std::println(std::cerr,"[CAM-ART] card #{} error: {}",task.event,e.what());
            }
         }
      } catch(std::exception const& e) {
         std::println(std::cerr,"[CAM-ART] worker unavailable: {}",e.what());
      }
   }
public:
   explicit ArtworkMatcher(unsigned candidates):m_candidates(candidates),
      m_worker([this]{Run();}) {}
   ~ArtworkMatcher() {
      {
         std::lock_guard lock(m_mutex);
         m_stopping=true;
      }
      m_cv.notify_one();
      if(m_worker.joinable())m_worker.join();
   }
   ArtworkMatcher(ArtworkMatcher const&)=delete;
   ArtworkMatcher& operator=(ArtworkMatcher const&)=delete;
   void Search(cv::Mat const& card,unsigned eventNumber) {
      {
         std::lock_guard lock(m_mutex);
         // Replace outdated queued work, never allow unbounded frames/jobs.
         m_pending=Task{eventNumber,card.clone()};
      }
      m_cv.notify_one();
      std::println("[CAM-ART] card #{} queued in background",eventNumber);
   }
};
#endif

struct FoundCard {
   cv::Mat rectified;
   hash_ty fingerprint{};
   std::array<cv::Point,4> corners{};
};
std::optional<FoundCard> FindCard(cv::Mat const& frame) {
   if(frame.empty()||frame.cols<64||frame.rows<64)return std::nullopt;
   cv::Mat gray,blur,edges;
   if(frame.channels()==3)cv::cvtColor(frame,gray,cv::COLOR_BGR2GRAY);
   else gray=frame;
   cv::GaussianBlur(gray,blur,cv::Size(5,5),0);
   cv::Canny(blur,edges,60,140);
   std::vector<std::vector<cv::Point>> contours;
   cv::findContours(edges,contours,cv::RETR_LIST,cv::CHAIN_APPROX_SIMPLE);
   double bestArea{};
   std::vector<cv::Point> best;
   double const frameArea=static_cast<double>(frame.cols)*frame.rows;
   for(auto const& contour:contours) {
      double const area=std::abs(cv::contourArea(contour));
      if(area<frameArea*0.04||area>frameArea*0.90||area<bestArea)continue;
      std::vector<cv::Point> corners;
      cv::approxPolyDP(contour,corners,cv::arcLength(contour,true)*0.025,true);
      if(corners.size()!=4||!cv::isContourConvex(corners))continue;
      auto const r=cv::minAreaRect(corners);
      double const shortSide=std::min(r.size.width,r.size.height);
      double const longSide=std::max(r.size.width,r.size.height);
      if(shortSide<20||longSide/shortSide<1.25||longSide/shortSide>1.65)continue;
      bestArea=area;
      best=std::move(corners);
   }
   if(best.size()!=4)return std::nullopt;
   // First test: near-upright cards. Arbitrary rotated/split layouts require
   // an orientation-aware model in stage 2.
   std::array<cv::Point2f,4> pts{};
   double smallest=std::numeric_limits<double>::max(),largest=-smallest;
   double smallDiff=smallest,largeDiff=largest;
   for(auto const& point:best) {
      double const sum=point.x+point.y;
      double const diff=point.x-point.y;
      if(sum<smallest){smallest=sum;pts[0]=point;}
      if(diff>largeDiff){largeDiff=diff;pts[1]=point;}
      if(sum>largest){largest=sum;pts[2]=point;}
      if(diff<smallDiff){smallDiff=diff;pts[3]=point;}
   }
   // The short edge must be horizontal after rectification, including
   // cards presented sideways. Orientation of the printed content is
   // decided independently by OCR.
   if(cv::norm(pts[0]-pts[1])>cv::norm(pts[1]-pts[2]))
      std::rotate(pts.begin(),pts.begin()+1,pts.end());
   std::array<cv::Point2f,4> target{{
      {0,0},{479,0},{479,671},{0,671}
   }};
   cv::Mat homography=cv::getPerspectiveTransform(pts.data(),target.data());
   cv::Mat rectified;
   cv::warpPerspective(frame,rectified,homography,cv::Size(480,672));
   std::array<cv::Point,4> quad{};
   for(unsigned i=0;i<4;++i)quad[i]=cv::Point(cvRound(pts[i].x),cvRound(pts[i].y));
   return FoundCard{rectified,DHash(rectified),quad};
}
// Manual 180-degree correction of an upside-down card. Use the same pixels
// for OCR, artwork matching, saved evidence and the diagnostic preview.
// This is NOT automatic orientation detection.
void FlipRectifiedCard(FoundCard& card) {
   cv::Mat rotated;
   cv::rotate(card.rectified,rotated,cv::ROTATE_180);
   card.rectified=std::move(rotated);
   card.fingerprint=DHash(card.rectified);
}
// A continuous card-presence session produces exactly one presentation event
// and, after sustained absence, exactly one removal event.
// dHash fluctuations must NOT produce either event on their own.
class StableCardTrigger {
   std::optional<hash_ty> m_previous;
   cv::Mat m_bestFrame;
   double m_bestScore{-1.0};
   std::optional<unsigned> m_activeNumber;
   hash_ty m_activeFingerprint{};
   unsigned m_stable{},m_absent{},m_events{};
   bool m_armed{true};
public:
   enum class EventKind { Presented, Removed };
   struct Event {
      EventKind kind;
      unsigned number;
      hash_ty fingerprint;
      cv::Mat card;
   };
   std::optional<Event> Accept(std::optional<FoundCard> const& found) {
      constexpr unsigned stableFrames=5;
      constexpr unsigned removalFrames=45; // 1.5 seconds at 30 FPS
      if(!found) {
         m_previous.reset();
         m_stable=0;
         m_bestFrame.release();
         m_bestScore=-1.0;
         if(m_absent<removalFrames)++m_absent;
         if(m_absent==removalFrames) {
            m_armed=true;
            if(m_activeNumber) {
               Event event{EventKind::Removed,*m_activeNumber,
                  m_activeFingerprint,{}};
               m_activeNumber.reset(); // A single removal per presentation.
               return event;
            }
         }
         return std::nullopt;
      }
      m_absent=0;
      if(!m_armed)return std::nullopt;
      if(m_previous && std::popcount(*m_previous^found->fingerprint)<=8)
         ++m_stable;
      else {
         m_stable=1;
         m_bestFrame.release();
         m_bestScore=-1.0;
      }
      cv::Mat aGray,aLap;
      cv::cvtColor(found->rectified,aGray,cv::COLOR_BGR2GRAY);
      cv::Laplacian(aGray,aLap,CV_32F);
      cv::Scalar aMean,aStd;
      cv::meanStdDev(aLap,aMean,aStd);
      double const flSharpness=aStd[0]*aStd[0];
      if(flSharpness>m_bestScore) {
         m_bestScore=flSharpness;
         m_bestFrame=found->rectified.clone();
      }
      m_previous=found->fingerprint;
      if(m_stable<stableFrames)return std::nullopt;
      m_armed=false;
      m_activeNumber=++m_events;
      m_activeFingerprint=found->fingerprint;
      return Event{EventKind::Presented,*m_activeNumber,
         m_activeFingerprint,m_bestFrame.empty()?
            found->rectified.clone():m_bestFrame.clone()};
   }
};

#if defined(DECKKERNEL_CAMERA_HAS_OCR)
class CardOcr {
   tesseract::TessBaseAPI m_api;
   bool m_ready{};
   bool m_verbose{};
   static std::string Clean(std::string text) {
      for(char& ch:text)if(ch=='\n'||ch=='\r'||ch=='\t')ch=' ';
      auto start=text.find_first_not_of(' ');
      if(start==std::string::npos)return {};
      auto end=text.find_last_not_of(' ');
      return text.substr(start,end-start+1);
   }
   struct OcrResult {std::string text;int confidence{};};
   OcrResult ReadRegion(cv::Mat const& image,cv::Rect region,int mode) {
      cv::Mat gray,scaled;
      cv::cvtColor(image(region),gray,cv::COLOR_BGR2GRAY);
      cv::resize(gray,scaled,cv::Size(),2.0,2.0,cv::INTER_CUBIC);
      m_api.SetPageSegMode(static_cast<tesseract::PageSegMode>(mode));
      m_api.SetImage(scaled.data,scaled.cols,scaled.rows,1,
         static_cast<int>(scaled.step));
      if(m_api.Recognize(nullptr)!=0)return {};
      int const confidence=m_api.MeanTextConf();
      std::unique_ptr<char[]> text(m_api.GetUTF8Text());
      std::string cleaned=text?Clean(text.get()):std::string{};
      unsigned letters=0;
      for(unsigned char ch:cleaned)if(std::isalnum(ch))++letters;
      if(confidence<45||letters<3)return {{},confidence};
      return {std::move(cleaned),confidence};
   }
public:
   explicit CardOcr(std::string const& tessdata,bool verbose=false):m_verbose(verbose) {
      if(m_api.Init(tessdata.c_str(),"eng",tesseract::OEM_LSTM_ONLY)!=0)
         throw std::runtime_error("Tesseract could not initialize eng.traineddata at "+tessdata);
      m_ready=true;
   }
   ~CardOcr(){if(m_ready)m_api.End();}
   bool Orient(cv::Mat& aCard) {
      if(aCard.empty())return false;
      cv::Mat reversed;
      cv::rotate(aCard,reversed,cv::ROTATE_180);
      auto const normal=deckkernel_camera::ocr::Detect(aCard);
      auto const flipped=deckkernel_camera::ocr::Detect(reversed);
      auto const normalName=ReadRegion(aCard,normal.name,tesseract::PSM_SINGLE_LINE);
      auto const normalText=normal.typeSeparated?
         ReadRegion(aCard,normal.rules,tesseract::PSM_AUTO):OcrResult{};
      auto const reversedName=ReadRegion(reversed,flipped.name,tesseract::PSM_SINGLE_LINE);
      auto const reversedText=flipped.typeSeparated?
         ReadRegion(reversed,flipped.rules,tesseract::PSM_AUTO):OcrResult{};
      // Compare title and rules evidence. A single upside-down title fragment
      // can receive an artificially high OCR confidence.
      auto score=[](OcrResult const& name,OcrResult const& body) {
         return (name.text.empty()?0:name.confidence/2)+
            (body.text.empty()?0:body.confidence);
      };
      int const normalScore=score(normalName,normalText);
      int const reversedScore=score(reversedName,reversedText);
      bool const flip=!reversedName.text.empty() &&
         reversedScore>=normalScore+22 &&
         (reversedText.confidence>=60 || reversedName.confidence>=65);
      if(m_verbose)
         std::println("[CAM-ROT] title normal={} reversed={} body normal={} reversed={} chosen={}",
            normalName.confidence,reversedName.confidence,
            normalText.confidence,reversedText.confidence,flip?180:0);
      if(flip)aCard=std::move(reversed);
      return flip;
   }
   std::string m_strLastName;
   std::string m_strLastRules;
   std::string m_strLastType;
   int m_typeConfidence{};
   int m_nameConfidence{};
   int m_rulesConfidence{};
   [[nodiscard]] int NameConfidence() const noexcept{return m_nameConfidence;}
   [[nodiscard]] int RulesConfidence() const noexcept{return m_rulesConfidence;}
   [[nodiscard]] int TypeConfidence() const noexcept{return m_typeConfidence;}
   [[nodiscard]] std::string const& LastType() const noexcept{return m_strLastType;}
   [[nodiscard]] std::string const& LastName() const noexcept{return m_strLastName;}
   [[nodiscard]] std::string const& LastRules() const noexcept{return m_strLastRules;}
   bool Read(cv::Mat const& card,unsigned eventNumber,unsigned attempt) {
      if(card.empty()||card.cols<100||card.rows<200)return false;
      auto const zones=deckkernel_camera::ocr::Detect(card);
      auto const title=ReadRegion(card,zones.name,tesseract::PSM_SINGLE_LINE);
      auto const proposedType=zones.typeSeparated?
         ReadRegion(card,zones.type,tesseract::PSM_SINGLE_LINE):OcrResult{};
      bool const semanticType=zones.typeSeparated &&
         deckkernel_camera::ocr::LooksLikeType(proposedType.text);
      auto const type=semanticType?proposedType:OcrResult{};
      auto const rules=semanticType?
         ReadRegion(card,zones.rules,tesseract::PSM_AUTO):OcrResult{};
      if(m_verbose)
         std::println("[CAM-ZONES] card #{} layout={} separator-confidence={:.2f} "
            "type-y={}..{} rules-y={}..{}",eventNumber,
            semanticType?"segmented":"unclassified",
            zones.separationConfidence,zones.type.y,zones.type.br().y,
            zones.rules.y,zones.rules.br().y);
      m_strLastName=title.text;
      m_strLastType=semanticType?type.text:"Unclassified typeline";
      m_strLastRules=rules.text;
      m_nameConfidence=title.confidence;
      m_typeConfidence=type.confidence;
      m_rulesConfidence=rules.confidence;
      if(m_verbose) {
         std::println("[CAM-OCR] card #{} attempt={} name=\"{}\" confidence={}",
            eventNumber,attempt,title.text,title.confidence);
         std::println("[CAM-OCR] card #{} attempt={} type=\"{}\" confidence={}",
            eventNumber,attempt,type.text,type.confidence);
         std::println("[CAM-OCR] card #{} attempt={} rules=\"{}\" confidence={}",
            eventNumber,attempt,rules.text,rules.confidence);
      }
      return !title.text.empty();
   }
};
#endif


// Coarse visual candidate detection only, NOT a definitive mana symbol reader.
// Hue distinguishes chromatic candidates; white/colorless and hybrid symbols
// require template verification against known Scryfall symbol graphics.
std::string ReportManaColorCandidates(cv::Mat const& aCard,unsigned uEvent,bool verbose=false) {
   if(aCard.empty())return "No mana evidence";
   cv::Rect const aBand=deckkernel_camera::ocr::Detect(aCard).mana;
   cv::Mat aHsv;
   cv::cvtColor(aCard(aBand),aHsv,cv::COLOR_BGR2HSV);
   std::array<unsigned,5> arrHits{};
   for(int y=0;y<aHsv.rows;++y) {
      for(int x=0;x<aHsv.cols;++x) {
         auto const aPixel=aHsv.at<cv::Vec3b>(y,x);
         int const iHue=aPixel[0],iSat=aPixel[1],iValue=aPixel[2];
         if(iSat<65||iValue<65)continue;
         if(iHue<11||iHue>171)++arrHits[0]; // red
         else if(iHue>=11&&iHue<36)++arrHits[1]; // yellow/gold, not W!
         else if(iHue>=36&&iHue<85)++arrHits[2]; // green
         else if(iHue>=85&&iHue<132)++arrHits[3]; // blue
         else ++arrHits[4]; // purple/other
      }
   }
   if(verbose)std::println("[CAM-MANA] card #{} RGB hue candidate pixels R={} Y={} G={} U={} other={} status=UNVERIFIED",
      uEvent,arrHits[0],arrHits[1],arrHits[2],arrHits[3],arrHits[4]);
   return std::format("Color pixels (unverified) R:{} Y:{} G:{} U:{} other:{}",
      arrHits[0],arrHits[1],arrHits[2],arrHits[3],arrHits[4]);
}

class Processor {
   StableCardTrigger m_detector;
   bool m_save{};
   bool m_verbose{};
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
   std::unique_ptr<ArtworkMatcher> m_artwork;
#endif
   unsigned m_removedEvents{};
   unsigned m_liveFrames{},m_ocrAttempts{};
   double m_bestSharpness{};
   std::optional<unsigned> m_ocrCard;
   bool m_ocrAccepted{};
   bool m_autoFlipped{};
   std::string m_strName{"--"};
   std::string m_strRules{"--"};
   std::string m_strType{"--"};
   std::string m_strMana{"No mana evidence"};
   std::string m_strOrientation{"Automatic orientation pending"};
   cv::Mat m_displayCard;
   std::string m_strStatus{"Waiting for card"};
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
   std::unique_ptr<CardOcr> m_ocr;
#endif
   std::filesystem::path m_root;
public:
   explicit Processor(bool save,bool verbose=false):m_save(save),m_verbose(verbose),
      m_root(std::getenv("DECKKERNEL_CAMERA_EVIDENCE_ROOT")?
         std::getenv("DECKKERNEL_CAMERA_EVIDENCE_ROOT"):
         ".cache/camera-recognition") {}
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
   void EnableOcr(std::string const& data){m_ocr=std::make_unique<CardOcr>(data,m_verbose);}
#endif
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
   void EnableArtwork(unsigned limit){m_artwork=std::make_unique<ArtworkMatcher>(limit);}
#endif
   [[nodiscard]] unsigned RemovedEvents() const noexcept {return m_removedEvents;}
   [[nodiscard]] std::string const& Name() const noexcept {return m_strName;}
   [[nodiscard]] std::string const& Rules() const noexcept {return m_strRules;}
   [[nodiscard]] std::string const& Type() const noexcept {return m_strType;}
   [[nodiscard]] std::string const& Status() const noexcept {return m_strStatus;}
   [[nodiscard]] std::string const& Mana() const noexcept {return m_strMana;}
   [[nodiscard]] std::string const& Orientation() const noexcept{return m_strOrientation;}
   [[nodiscard]] cv::Mat const& DisplayCard() const noexcept{return m_displayCard;}
   void ClearDisplayCard() {m_displayCard.release();}
   void ReevaluateOcrAfterRotation() noexcept {
      // Changing the orientation must be allowed to rerun OCR even when a
      // previous, wrong-side-up title passed the provisional confidence test.
      m_ocrAccepted=false;
      m_ocrAttempts=0;
      m_liveFrames=90;
      m_bestSharpness=0;
      // Keep the active presentation event: a manual rotation must cause
      // another OCR pass without requiring the user to remove the card.
      m_autoFlipped=false;
      if(!m_displayCard.empty())
         cv::rotate(m_displayCard,m_displayCard,cv::ROTATE_180);
      m_strOrientation="Manual 180 deg correction requested";
   }
   bool Process(cv::Mat const& frame) {return Process(FindCard(frame));}
   bool Process(std::optional<FoundCard> const& found) {
      auto event=m_detector.Accept(found);
      if(found && m_ocrCard && !m_ocrAccepted) {
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
         if(m_ocr && m_ocrAttempts<8) {
            ++m_liveFrames;
            if(m_liveFrames>=30) {
               cv::Mat gray;
               cv::cvtColor(found->rectified,gray,cv::COLOR_BGR2GRAY);
               cv::Mat lap;
               cv::Laplacian(gray,lap,CV_64F);
               cv::Scalar mean,dev;
               cv::meanStdDev(lap,mean,dev);
               double const sharpness=dev[0]*dev[0];
               if(sharpness>m_bestSharpness*1.10 || m_liveFrames>=90) {
                  m_liveFrames=0;
                  m_bestSharpness=std::max(m_bestSharpness,sharpness);
                  ++m_ocrAttempts;
                  cv::Mat aCandidate=found->rectified.clone();
                  if(m_autoFlipped)cv::rotate(aCandidate,aCandidate,cv::ROTATE_180);
                  m_ocrAccepted=m_ocr->Read(aCandidate,*m_ocrCard,m_ocrAttempts);
                  m_strName=m_ocr->LastName();m_strType=m_ocr->LastType();m_strRules=m_ocr->LastRules();
                  m_strStatus=std::format("OCR name {}% type {}% text {}% - unverified",
                     m_ocr->NameConfidence(),m_ocr->TypeConfidence(),m_ocr->RulesConfidence());
                  if(m_ocrAccepted)std::println("[CAM-OCR] card #{} accepted after {} attempts",*m_ocrCard,m_ocrAttempts);
               }
            }
         }
#endif
      }
      if(!event)return false;
      if(event->kind==StableCardTrigger::EventKind::Removed) {
         ++m_removedEvents;
         m_ocrCard.reset();m_ocrAccepted=false;m_ocrAttempts=0;
         m_liveFrames=0;m_bestSharpness=0;m_autoFlipped=false;
         m_strStatus="Card removed - last OCR result retained";
         m_displayCard.release();
         std::println("[CAM] CARD-NO-LONGER-DETECTED #{} fingerprint={:016x}; "
            "absent-frames=45; ready-for-next-card",
            event->number,event->fingerprint);
         return false;
      }
      std::println("[CAM] NEW-STABLE-CARD #{} fingerprint={:016x}; edition=UNKNOWN",
         event->number,event->fingerprint);
      m_ocrCard=event->number;
      m_strStatus="Card detected - identity unverified";
      m_strMana=ReportManaColorCandidates(event->card,event->number,m_verbose);
      m_liveFrames=0;m_bestSharpness=0;m_ocrAttempts=0;m_ocrAccepted=false;
      m_autoFlipped=false;
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(m_ocr) {
         m_autoFlipped=m_ocr->Orient(event->card);
         m_strOrientation=m_autoFlipped?"Auto rotated 180 deg":"Auto 0 deg / uncertain";
         m_ocrAttempts=1;
         m_ocrAccepted=m_ocr->Read(event->card,event->number,m_ocrAttempts);
         m_strName=m_ocr->LastName();m_strType=m_ocr->LastType();m_strRules=m_ocr->LastRules();
         m_strStatus=std::format("OCR name {}% type {}% text {}% - unverified",
            m_ocr->NameConfidence(),m_ocr->TypeConfidence(),m_ocr->RulesConfidence());
      }
#endif
      m_displayCard=event->card.clone();
      if(m_save) {
         auto const zones=deckkernel_camera::ocr::Detect(event->card);
         if(!deckkernel_camera::ocr::Valid(zones,event->card.size()))
            throw std::runtime_error("invalid/non-disjoint OCR zones");
         std::filesystem::create_directories(m_root);
         auto saveZone=[&](char const* label,cv::Rect region) {
            auto const file=m_root/std::format("event-{:06}-{}.png",event->number,label);
            if(!cv::imwrite(file.string(),event->card(region)))
               throw std::runtime_error("cannot save OCR zone");
         };
         saveZone("name",zones.name);
         saveZone("mana",zones.mana);
         saveZone("type",zones.type);
         saveZone("set-symbol",zones.setSymbol);
         saveZone("rules",zones.rules);
         auto const file=m_root/std::format("event-{:06}-zones.png",event->number);
         cv::imwrite(file.string(),deckkernel_camera::ocr::Diagram(event->card,zones));
      }
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
      if(m_artwork)m_artwork->Search(event->card,event->number);
#endif
      if(m_save){
         std::filesystem::create_directories(m_root);
         auto const name=m_root/std::format("event-{:06}.png",event->number);
         if(!cv::imwrite(name.string(),event->card))
            throw std::runtime_error("could not save rectified camera evidence");
      }
      return true;
   }
};

#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
class CameraWindow {
   HWND m_hwnd{};
   cv::Mat m_pixels;
   cv::Mat m_rectifiedPixels;
   bool m_flip180{};
   static void PaintImage(HDC dc,cv::Mat const& image,RECT const& area) {
      if(image.empty())return;
      int const availableWidth=area.right-area.left;
      int const availableHeight=area.bottom-area.top;
      if(availableWidth<=0||availableHeight<=0)return;
      double const scale=std::min(
         static_cast<double>(availableWidth)/image.cols,
         static_cast<double>(availableHeight)/image.rows);
      int const width=std::max(1,static_cast<int>(image.cols*scale));
      int const height=std::max(1,static_cast<int>(image.rows*scale));
      BITMAPINFO info{};
      info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth=image.cols;
      info.bmiHeader.biHeight=-image.rows; // top-down BGRA bitmap
      info.bmiHeader.biPlanes=1;
      info.bmiHeader.biBitCount=32;
      info.bmiHeader.biCompression=BI_RGB;
      SetStretchBltMode(dc,HALFTONE);
      StretchDIBits(dc,area.left+(availableWidth-width)/2,
         area.top+(availableHeight-height)/2,width,height,
         0,0,image.cols,image.rows,image.data,&info,DIB_RGB_COLORS,SRCCOPY);
   }
   static cv::Mat ToDisplayPixels(cv::Mat const& image) {
      if(image.empty())return {};
      cv::Mat pixels;
      if(image.channels()==3)
         cv::cvtColor(image,pixels,cv::COLOR_BGR2BGRA);
      else if(image.channels()==4)
         pixels=image.clone();
      else
         cv::cvtColor(image,pixels,cv::COLOR_GRAY2BGRA);
      return pixels;
   }
   static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
      if(msg==WM_NCCREATE) {
         auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
         SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
         return TRUE;
      }
      auto* self=reinterpret_cast<CameraWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
      if(msg==WM_ERASEBKGND)return 1;
      if(msg==WM_KEYDOWN && self && wp==static_cast<WPARAM>('R')) {
         self->m_flip180=!self->m_flip180;
         std::println("[CAM] manual card orientation={} degrees (R toggles)",
            self->m_flip180?180:0);
         InvalidateRect(hwnd,nullptr,FALSE);
         return 0;
      }
      if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}
      if(msg==WM_DESTROY){if(self)self->m_hwnd=nullptr;return 0;}
      if(msg==WM_PAINT) {
         PAINTSTRUCT ps{};
         HDC dc=BeginPaint(hwnd,&ps);
         RECT rc{};GetClientRect(hwnd,&rc);
         int const clientWidth=rc.right-rc.left;
         int const clientHeight=rc.bottom-rc.top;
         HDC buffer=CreateCompatibleDC(dc);
         HBITMAP bitmap=(buffer && clientWidth>0 && clientHeight>0)?
            CreateCompatibleBitmap(dc,clientWidth,clientHeight):nullptr;
         if(bitmap) {
            HGDIOBJ oldBitmap=SelectObject(buffer,bitmap);
            FillRect(buffer,&rc,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            int const divider=clientWidth*57/100;
            RECT const left{12,42,divider-10,clientHeight-12};
            RECT const right{divider+10,42,clientWidth-12,clientHeight-12};
            SetBkMode(buffer,TRANSPARENT);
            SetTextColor(buffer,RGB(232,237,242));
            wchar_t const* titleLeft=L"CAMERA - DETECTED CONTOUR";
            TextOutW(buffer,12,13,titleLeft,static_cast<int>(wcslen(titleLeft)));
            wchar_t const* titleRight=self&&self->m_flip180?
               L"RECTIFIED CARD - ROTATED 180 (R)":
               L"RECTIFIED CARD - ROTATION 0 (R)";
            TextOutW(buffer,divider+10,13,titleRight,
               static_cast<int>(wcslen(titleRight)));
            if(self) {
               PaintImage(buffer,self->m_pixels,left);
               PaintImage(buffer,self->m_rectifiedPixels,right);
               if(self->m_rectifiedPixels.empty()) {
                  wchar_t const* waiting=L"NO CARD CONTOUR";
                  TextOutW(buffer,divider+22,clientHeight/2,waiting,
                     static_cast<int>(wcslen(waiting)));
               }
            }
            RECT separator{divider,12,divider+1,clientHeight-12};
            FillRect(buffer,&separator,
               reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            BitBlt(dc,0,0,clientWidth,clientHeight,buffer,0,0,SRCCOPY);
            SelectObject(buffer,oldBitmap);
            DeleteObject(bitmap);
         }
         if(buffer)DeleteDC(buffer);
         EndPaint(hwnd,&ps);
         return 0;
      }
      return DefWindowProcW(hwnd,msg,wp,lp);
   }
public:
   explicit CameraWindow(int index) {
      HINSTANCE instance=GetModuleHandleW(nullptr);
      WNDCLASSW cls{};
      cls.hInstance=instance;
      cls.lpfnWndProc=Proc;
      cls.lpszClassName=L"DeckKernelLiveCamera";
      cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
      if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
         throw std::runtime_error("camera preview class registration failed");
      m_hwnd=CreateWindowExW(0,cls.lpszClassName,L"DeckKernel - camera + rectified card",
         WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,
         1450,850,nullptr,nullptr,instance,this);
      if(!m_hwnd)throw std::runtime_error("camera preview creation failed");
      ShowWindow(m_hwnd,SW_SHOW);
      std::println("[CAM] preview: left=camera, right=actual rectified OCR/artwork "
         "input; press R to toggle 0/180 degrees");
   }
   [[nodiscard]] bool Flip180() const noexcept {return m_flip180;}
   bool Messages() {
      MSG message{};
      while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
         TranslateMessage(&message);
         DispatchMessageW(&message);
      }
      return m_hwnd!=nullptr;
   }
   void Show(cv::Mat const& frame,cv::Mat const& rectified,
      int index,double fps,double brightness) {
      m_pixels=ToDisplayPixels(frame);
      m_rectifiedPixels=ToDisplayPixels(rectified);
      auto title=std::format(L"DeckKernel - camera {} - {}x{} - {:.1f} fps - brightness {:.0f}",
         index,frame.cols,frame.rows,fps,brightness);
      SetWindowTextW(m_hwnd,title.c_str());
      InvalidateRect(m_hwnd,nullptr,FALSE);
   }
};
#endif

void SelfTest() {
   Processor processor(false);
   cv::Mat zoneCard(672,480,CV_8UC3,cv::Scalar(255,255,255));
   auto drawSeparators=[&](int y1,int y2) {
      zoneCard.setTo(cv::Scalar(255,255,255));
      cv::line(zoneCard,cv::Point(34,y1),cv::Point(446,y1),
         cv::Scalar(20,20,20),3);
      cv::line(zoneCard,cv::Point(34,y2),cv::Point(446,y2),
         cv::Scalar(20,20,20),3);
   };
   drawSeparators(368,412);
   auto const firstZones=deckkernel_camera::ocr::Detect(zoneCard);
   drawSeparators(291,337);
   auto const zones=deckkernel_camera::ocr::Detect(zoneCard);
   if(!firstZones.typeSeparated || !zones.typeSeparated ||
      std::abs(zones.rules.y-firstZones.rules.y)<40)
      throw std::runtime_error("dynamic OCR type/rules boundary not detected");
   if(!deckkernel_camera::ocr::Valid(zones,zoneCard.size()) ||
      zones.name.br().x>zones.mana.x ||
      zones.type.br().x>zones.setSymbol.x ||
      zones.rules.y<zones.type.br().y)
      throw std::runtime_error("OCR text/symbol zones overlap");
   cv::Mat blank(720,1280,CV_8UC3,cv::Scalar(20,20,20));
   cv::Mat card=blank.clone();
   cv::rectangle(card,cv::Rect(475,175,250,350),cv::Scalar(240,240,240),-1);
   // The manual 180-degree correction must be lossless and preserve the hash
   // on two successive toggles. Both panels and downstream consumers use it.
   auto sample=FindCard(card);
   if(!sample)throw std::runtime_error("synthetic card contour missing");
   cv::Mat const before=sample->rectified.clone();
   hash_ty const hashBefore=sample->fingerprint;
   FlipRectifiedCard(*sample);
   FlipRectifiedCard(*sample);
   if(cv::norm(before,sample->rectified,cv::NORM_INF)!=0 ||
      sample->fingerprint!=hashBefore)
      throw std::runtime_error("180-degree preview orientation roundtrip failed");
   // A blank view before the first card must never emit a removal.
   for(int i=0;i<60;++i)processor.Process(blank);
   if(processor.RemovedEvents()!=0)
      throw std::runtime_error("empty startup scene emitted a removal");
   unsigned events{};
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=1)throw std::runtime_error("first stable card must emit one event");
   // Hash fluctuations with uninterrupted presence are NOT a new card.
   cv::Mat fluctuating=card.clone();
   cv::rectangle(fluctuating,cv::Rect(495,205,140,140),
      cv::Scalar(70,80,90),-1);
   for(int i=0;i<30;++i)
      if(processor.Process(i%2?card:fluctuating))++events;
   if(events!=1)throw std::runtime_error("hash jitter caused duplicate card event");
   // Short contour dropouts must not emit a removal or a duplicate.
   for(int i=0;i<12;++i)processor.Process(blank);
   if(processor.RemovedEvents()!=0)
      throw std::runtime_error("short contour dropout emitted a removal");
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=1)throw std::runtime_error("brief contour loss caused a duplicate");
   // One removal after exactly 45 absent frames, never repeated while empty.
   for(int i=0;i<44;++i)processor.Process(blank);
   if(processor.RemovedEvents()!=0)
      throw std::runtime_error("card removal signalled before frame 45");
   processor.Process(blank);
   if(processor.RemovedEvents()!=1)
      throw std::runtime_error("missing removal after 45 empty frames");
   for(int i=0;i<60;++i)processor.Process(blank);
   if(processor.RemovedEvents()!=1)
      throw std::runtime_error("repeated removal while scene remained empty");
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=2)throw std::runtime_error("reinserted card must emit second event");
   for(int i=0;i<45;++i)processor.Process(blank);
   if(processor.RemovedEvents()!=2)
      throw std::runtime_error("second recognized card did not emit removal");
   std::println("SMOKE|RESULT|PASS|stable presentation, one removal per card, "
      "jitter suppression, short dropout and reentry");
}

#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
/** \brief Translate a Windows camera's FOURCC to readable characters. */
std::string FourccName(double code) {
   if(!std::isfinite(code)||code<=0)return "unspecified";
   std::uint32_t const number=static_cast<std::uint32_t>(code);
   std::string out;
   for(unsigned shift=0;shift<32;shift+=8) {
      char const ch=static_cast<char>((number>>shift)&0xff);
      if(ch<32||ch>126)return std::format("0x{:08x}",number);
      out.push_back(ch);
   }
   return out;
}

int Api(std::string_view backend) {
   if(backend=="any")return cv::CAP_ANY;
   if(backend=="dshow")return cv::CAP_DSHOW;
   if(backend=="msmf")return cv::CAP_MSMF;
   throw std::runtime_error("backend must be any, dshow or msmf");
}

int CameraFourcc(std::string_view value) {
   if(value=="mjpg")return cv::VideoWriter::fourcc('M','J','P','G');
   if(value=="yuy2")return cv::VideoWriter::fourcc('Y','U','Y','2');
   throw std::runtime_error("FOURCC must be auto, mjpg or yuy2");
}

void ConfigureCapture(cv::VideoCapture& capture,int width,int height,int fps,
   std::string_view fourcc) {
   // Drivers may restart their capture graph when these properties change.
   // The requested mode is not necessarily the mode ultimately negotiated.
   if(fourcc!="auto") {
      bool const result=capture.set(cv::CAP_PROP_FOURCC,CameraFourcc(fourcc));
      std::println("[CAM] FOURCC request={} accepted={}",fourcc,result);
   }
   bool const w=capture.set(cv::CAP_PROP_FRAME_WIDTH,width);
   bool const h=capture.set(cv::CAP_PROP_FRAME_HEIGHT,height);
   bool const f=capture.set(cv::CAP_PROP_FPS,fps);
   std::println("[CAM] mode request={}x{} @{}fps accepted={}/{}/{}",
      width,height,fps,w,h,f);
   std::println("[CAM] negotiated={}x{} @{}fps FOURCC={}",
      capture.get(cv::CAP_PROP_FRAME_WIDTH),
      capture.get(cv::CAP_PROP_FRAME_HEIGHT),
      capture.get(cv::CAP_PROP_FPS),
      FourccName(capture.get(cv::CAP_PROP_FOURCC)));
}

struct FrameSignal {
   double brightness{};
   double maximum{};
   double minimum{};
   bool nonblack{};
};

/** \brief Measure real pixel values, not merely successful VideoCapture.read(). */
FrameSignal MeasureFrame(cv::Mat const& frame) {
   if(frame.empty())return {};
   cv::Mat thumbnail;
   cv::resize(frame,thumbnail,cv::Size(80,60),0,0,cv::INTER_AREA);
   cv::Mat const oneChannel=thumbnail.reshape(1);
   FrameSignal signal;
   cv::minMaxLoc(oneChannel,&signal.minimum,&signal.maximum);
   cv::Scalar const mean=cv::mean(thumbnail);
   if(frame.channels()>=3)
      signal.brightness=(mean[0]+mean[1]+mean[2])/3.0;
   else signal.brightness=mean[0];
   signal.nonblack=signal.maximum>5.0;
   return signal;
}

struct ProbeProfile {
   char const* name{};
   int backend{};
   int width{},height{},fps{};
   char const* fourcc{};
};

/** \brief Try several normal camera delivery modes without touching the DB.
    \returns True if any mode yielded nonblack image pixels.
*/
bool ProbeCamera(int index) {
   std::array<ProbeProfile,4> const modes{{
      {"DirectShow MJPG",cv::CAP_DSHOW,640,480,30,"mjpg"},
      {"DirectShow YUY2",cv::CAP_DSHOW,640,480,30,"yuy2"},
      {"MediaFoundation auto",cv::CAP_MSMF,640,480,30,"auto"},
      {"MediaFoundation MJPG",cv::CAP_MSMF,640,480,30,"mjpg"}
   }};
   unsigned successful=0;
   for(auto const& mode:modes) {
      std::println("[CAM-PROBE] START index={} mode={}",index,mode.name);
      try {
         cv::VideoCapture capture;
         if(!capture.open(index,mode.backend)||!capture.isOpened()) {
            std::println("[CAM-PROBE] UNAVAILABLE {}",mode.name);
            continue;
         }
         ConfigureCapture(capture,mode.width,mode.height,mode.fps,mode.fourcc);
         unsigned received=0,bright=0,black=0,changed=0,empty=0;
         cv::Mat previous,frame;
         double maximum=0;
         auto const beginning=std::chrono::steady_clock::now();
         for(unsigned n=0;n<45;++n) {
            if(!capture.read(frame)||frame.empty()){
               ++empty;
            } else {
               ++received;
               auto const signal=MeasureFrame(frame);
               maximum=std::max(maximum,signal.maximum);
               if(signal.nonblack)++bright;else ++black;
               cv::Mat small;
               cv::resize(frame,small,cv::Size(80,60),0,0,cv::INTER_AREA);
               if(!previous.empty()&&cv::norm(previous,small,cv::NORM_L1)>50)
                  ++changed;
               previous=std::move(small);
            }
            if(std::chrono::duration<double>(
               std::chrono::steady_clock::now()-beginning).count()>=5.0)
               break;
         }
         double const seconds=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-beginning).count();
         std::println("[CAM-PROBE] RESULT {} backend={} frames={} empty={} "
            "nonblack={} black={} changed={} maxPixel={:.0f} elapsed={:.1f}s",
            mode.name,capture.getBackendName(),received,empty,bright,black,
            changed,maximum,seconds);
         if(bright>0)++successful;
         capture.release();
      } catch(cv::Exception const& ex) {
         std::println(std::cerr,
            "[CAM-PROBE] BACKEND ERROR {}: {}",mode.name,ex.what());
      }
   }
   std::println("SMOKE|CHECK|camera-pixels|{}|camera={} valid_modes={}/4",
      successful?"PASS":"FAIL",index,successful);
   if(!successful)
      std::println("[CAM-PROBE] No usable image data. Check the camera in the "
         "Windows Camera application before changing the card recognition algorithm.");
   return successful>0;
}
#endif

int CameraIndex(std::string_view str) {
   int n{};
   auto const r=std::from_chars(str.data(),str.data()+str.size(),n);
   if(r.ec!=std::errc{}||r.ptr!=str.data()+str.size()||n<0)
      throw std::runtime_error("invalid camera index");
   return n;
}
void Usage() {
   std::println("Usage: deckkernel_camera_recognition_test --self-test");
   std::println("   or: --sequence=folder [--save-events]");
   std::println("   or: --image=file.png [--ocr] [--save-events] (original phone photo)");
   std::println("   or: --camera=0 [--backend=any|dshow|msmf] [--width=640 --height=480 --fps=30 --fourcc=auto|mjpg|yuy2] [--save-events]");
   std::println("   or: --probe-camera=0          test 4 Windows camera delivery modes");
   std::println("   optional: --ocr --tessdata=PATH_TO_TESSDATA (eng.traineddata required)");
   std::println("   optional: --artwork [--artwork-limit=100] (PostgreSQL art_crop, no scan index)");
   std::println("   optional: --verbose (periodic camera and OCR console diagnostics)");
}
}
int main(int argc,char** argv) {
   try{
      bool selfTest=false,save=false,verbose=false;
      bool ocr=false,artwork=false;
      unsigned artworkLimit=100;
      std::string tessdata;
      std::string backend="any";
      std::string fourcc="auto";
      int width=640,height=480,fpsRequested=30;
      std::optional<int> probeCamera;
      std::string folder;
      std::string imageFile;
      std::optional<int> camera;
      for(int i=1;i<argc;++i){
         std::string_view arg(argv[i]);
         if(arg=="--help"){Usage();return 0;}
         if(arg=="--self-test")selfTest=true;
         else if(arg=="--verbose")verbose=true;
         else if(arg=="--save-events")save=true;
         else if(arg=="--ocr")ocr=true;
         else if(arg=="--artwork")artwork=true;
         else if(arg.starts_with("--artwork-limit="))artworkLimit=static_cast<unsigned>(CameraIndex(arg.substr(16)));
         else if(arg.starts_with("--tessdata="))tessdata=arg.substr(11);
         else if(arg.starts_with("--backend="))backend=arg.substr(10);
         else if(arg.starts_with("--fourcc="))fourcc=arg.substr(9);
         else if(arg.starts_with("--width="))width=CameraIndex(arg.substr(8));
         else if(arg.starts_with("--height="))height=CameraIndex(arg.substr(9));
         else if(arg.starts_with("--fps="))fpsRequested=CameraIndex(arg.substr(6));
         else if(arg.starts_with("--probe-camera="))probeCamera=CameraIndex(arg.substr(15));
         else if(arg.starts_with("--sequence="))folder=arg.substr(11);
         else if(arg.starts_with("--image="))imageFile=arg.substr(8);
         else if(arg.starts_with("--camera="))camera=CameraIndex(arg.substr(9));
         else throw std::runtime_error("unknown argument (use --help)");
      }
      if(selfTest){SelfTest();return 0;}
      if(probeCamera) {
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
         return ProbeCamera(*probeCamera)?0:1;
#else
         throw std::runtime_error("OpenCV videoio is unavailable");
#endif
      }
      if(artworkLimit<1||artworkLimit>1000)
         throw std::runtime_error("--artwork-limit must be 1..1000");
      if(width<160||height<120||width>3840||height>2160||
         fpsRequested<1||fpsRequested>120)
         throw std::runtime_error("invalid camera width, height or fps");
      unsigned const sources=static_cast<unsigned>(!folder.empty())+
         static_cast<unsigned>(camera.has_value())+static_cast<unsigned>(!imageFile.empty());
      if(sources!=1)
         throw std::runtime_error("select exactly one of --image, --sequence or --camera");
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(ocr && tessdata.empty()) {
         if(auto const* env=std::getenv("TESSDATA_PREFIX");env&&*env)tessdata=env;
         else tessdata=(std::filesystem::path(argv[0]).parent_path()/"tessdata").string();
      }
#else
      if(ocr)throw std::runtime_error("OCR was not enabled at CMake configure time");
#endif
      if(!imageFile.empty()) {
#if !defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
         if(artwork)throw std::runtime_error("artwork matching disabled at CMake configuration");
#endif
         cv::Mat const image=cv::imread(imageFile,cv::IMREAD_COLOR);
         if(image.empty())
            throw std::runtime_error("cannot decode original image: "+imageFile);
         std::println("[CAM] still image={} size={}x{}",imageFile,image.cols,image.rows);
         auto found=FindCard(image);
         if(found) {
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
            if(artwork)ArtworkMatcher(artworkLimit).Search(found->rectified,1);
#endif
            std::println("[CAM] card contour rectified to {}x{}",
               found->rectified.cols,found->rectified.rows);
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
            if(ocr) {
               CardOcr stillOcr(tessdata);
               static_cast<void>(stillOcr.Orient(found->rectified));
               static_cast<void>(stillOcr.Read(found->rectified,1,1));
            }
#endif
            if(save) {
               std::filesystem::create_directories(".cache/camera-recognition");
               if(!cv::imwrite(".cache/camera-recognition/still-card.png",found->rectified))
                  throw std::runtime_error("cannot save rectified still image");
            }
         } else {
            std::println("[CAM] NO CARD CONTOUR: preserving original image for OCR");
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
            if(ocr) {
               // Original photo may already be tightly cropped to the card.
               CardOcr stillOcr(tessdata);
               static_cast<void>(stillOcr.Read(image,1,1));
            }
#endif
         }
         return 0;
      }
      Processor processor(save,verbose);
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
      if(artwork)processor.EnableArtwork(artworkLimit);
#else
      if(artwork)throw std::runtime_error("artwork matching disabled at CMake configuration");
#endif
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(ocr)processor.EnableOcr(tessdata);
#endif
      if(!folder.empty()){
         std::vector<std::filesystem::path> frames;
         for(auto const& e:std::filesystem::directory_iterator(folder)){
            if(!e.is_regular_file())continue;
            auto ext=e.path().extension().string();
            if(ext==".jpg"||ext==".png"||ext==".jpeg"||ext==".webp")
               frames.push_back(e.path());
         }
         std::sort(frames.begin(),frames.end());
         for(auto const& path:frames){
            auto frame=cv::imread(path.string(),cv::IMREAD_COLOR);
            if(frame.empty())throw std::runtime_error(
               "cannot read camera frame: "+path.string());
            processor.Process(frame);
         }
         std::println("[CAM] replay finished; frames={}",frames.size());
         return 0;
      }
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
      int api=Api(backend);
      std::println("[CAM] opening camera={} backend-request={}",*camera,backend);
      cv::VideoCapture capture(*camera,api);
      if(!capture.isOpened())
         throw std::runtime_error("cannot open camera: try another index/backend or Windows camera privacy settings");
      std::println("[CAM] opened camera={} backend={}",*camera,capture.getBackendName());
      ConfigureCapture(capture,width,height,fpsRequested,fourcc);
      // Camera-side focus is driver/backend dependent. Enable continuous AF
      // only if supported; image-space sharpness selection always remains on.
      bool const bAutoFocus=capture.set(cv::CAP_PROP_AUTOFOCUS,1.0);
      std::println("[CAM-FOCUS] autofocus-request accepted={} reported={}",
         bAutoFocus,capture.get(cv::CAP_PROP_AUTOFOCUS));
#if defined(DECKKERNEL_CAMERA_HAS_SDL_SKIA)
      CameraDashboard preview;
#else
      CameraWindow preview(*camera);
#endif
      cv::Mat frame;
      unsigned long long count=0;
      unsigned failures=0,blackFrames=0;
      auto interval=std::chrono::steady_clock::now();
      double fps=0.0;
      bool appliedFlip=false;
      auto const beginning=interval;
      while(preview.Messages()){
         if(!capture.read(frame)||frame.empty()){
            if(++failures==1 || failures%30==0)
               std::println(std::cerr,"[CAM] no frame returned, consecutive failures={}",failures);
            if(failures>=90)throw std::runtime_error("camera opened but produced no frames");
            continue;
         }
         failures=0;
         ++count;
         FrameSignal const signal=MeasureFrame(frame);
         // One contour detection/rectification per frame. Use precisely the
         // same pixels for the side panel, the OCR and the artwork worker.
         auto found=signal.nonblack?FindCard(frame):std::optional<FoundCard>{};
         bool const flip=preview.Flip180();
         if(flip!=appliedFlip) {
            processor.ReevaluateOcrAfterRotation();
            appliedFlip=flip;
         }
         if(found&&flip)FlipRectifiedCard(*found);
         if(signal.nonblack) {
            blackFrames=0;
            processor.Process(found);
         } else {
            ++blackFrames;
         }
         // Visual diagnostic: mark the four corners that pass card geometry checks.
         cv::Mat annotated=frame.clone();
         if(found) {
            for(unsigned i=0;i<4;++i)
               cv::line(annotated,found->corners[i],found->corners[(i+1)%4],
                  cv::Scalar(0,220,0),2,cv::LINE_AA);
            cv::putText(annotated,"CARD CONTOUR",cv::Point(12,26),
               cv::FONT_HERSHEY_SIMPLEX,0.7,cv::Scalar(0,220,0),2,cv::LINE_AA);
         } else {
            cv::putText(annotated,"NO CARD CONTOUR",cv::Point(12,26),
               cv::FONT_HERSHEY_SIMPLEX,0.7,cv::Scalar(0,170,240),2,cv::LINE_AA);
         }
#if defined(DECKKERNEL_CAMERA_HAS_SDL_SKIA)
         preview.SetFields(processor.Name(),processor.Type(),processor.Rules(),
            processor.Mana(),processor.Status(),"Edition not verified",
            flip?"Manual 180 deg / "+processor.Orientation():processor.Orientation());
#endif
         preview.Show(annotated,processor.DisplayCard().empty()?
            (found?found->rectified:cv::Mat{}):processor.DisplayCard(),
            *camera,fps,signal.brightness);
         auto now=std::chrono::steady_clock::now();
         double const seconds=std::chrono::duration<double>(now-interval).count();
         if(seconds>=2.0){
            fps=count/seconds;
            if(verbose)std::println("[CAM] backend={} frames={} size={}x{} fps={:.1f} "
               "mean={:.2f} min={:.0f} max={:.0f} black-frames={}",
               capture.getBackendName(),count,frame.cols,frame.rows,fps,
               signal.brightness,signal.minimum,signal.maximum,blackFrames);
            count=0;interval=now;
         }
         // Do not keep silently treating all-zero placeholder frames as
         // successful camera acquisition or running contour recognition.
         if(blackFrames>=5 &&
            std::chrono::duration<double>(now-beginning).count()>=5.0)
            throw std::runtime_error("VIDEO SIGNAL ABSENT: camera device opened, "
               "but returned at least five nearly black frames. "
               "Run --probe-camera=N and check the device in Windows Camera.");
      }
      std::println("[CAM] preview closed");
      return 0;
#else
      throw std::runtime_error("OpenCV videoio not present in installed BuildEngine stack; use --sequence");
#endif
   }
   catch(std::exception const& e){
      std::println(std::cerr,"[CAM] ERROR: {}",e.what());return 1;
   }
}
