// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/** \file main.cpp
    \brief First-stage camera card detector: present, stable, changed, absent.
    \details No Scryfall requests, no database access, no physical edition
             identification. Image evidence is optional and local.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/imgcodecs.hpp>
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
#include <opencv2/videoio.hpp>
#endif
#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
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
struct FoundCard {
   cv::Mat rectified;
   hash_ty fingerprint{};
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
   std::array<cv::Point2f,4> target{{
      {0,0},{479,0},{479,671},{0,671}
   }};
   cv::Mat homography=cv::getPerspectiveTransform(pts.data(),target.data());
   cv::Mat rectified;
   cv::warpPerspective(frame,rectified,homography,cv::Size(480,672));
   return FoundCard{rectified,DHash(rectified)};
}
class StableCardTrigger {
   std::optional<hash_ty> m_previous;
   std::optional<hash_ty> m_reported;
   unsigned m_stable{},m_absent{},m_events{};
   bool m_ready{true};
public:
   struct Event {unsigned number;hash_ty fingerprint;cv::Mat card;};
   std::optional<Event> Accept(std::optional<FoundCard> const& found) {
      if(!found){
         m_previous.reset();m_stable=0;
         if(++m_absent>=6)m_ready=true;
         return std::nullopt;
      }
      m_absent=0;
      if(m_previous&&std::popcount(*m_previous^found->fingerprint)<=8)++m_stable;
      else m_stable=1;
      m_previous=found->fingerprint;
      if(m_stable<5)return std::nullopt;
      if(!m_ready&&m_reported&&
         std::popcount(*m_reported^found->fingerprint)<=14)
         return std::nullopt;
      m_ready=false;
      m_reported=found->fingerprint;
      return Event{++m_events,found->fingerprint,found->rectified.clone()};
   }
};
class Processor {
   StableCardTrigger m_detector;
   bool m_save{};
   std::filesystem::path m_root;
public:
   explicit Processor(bool save):m_save(save),
      m_root(std::getenv("DECKKERNEL_CAMERA_EVIDENCE_ROOT")?
         std::getenv("DECKKERNEL_CAMERA_EVIDENCE_ROOT"):
         ".cache/camera-recognition") {}
   bool Process(cv::Mat const& frame) {
      auto event=m_detector.Accept(FindCard(frame));
      if(!event)return false;
      std::println("[CAM] NEW-STABLE-CARD #{} fingerprint={:016x}; edition=UNKNOWN",
         event->number,event->fingerprint);
      if(m_save){
         std::filesystem::create_directories(m_root);
         auto const name=m_root/std::format("event-{:06}.png",event->number);
         if(!cv::imwrite(name.string(),event->card))
            throw std::runtime_error("could not save rectified camera evidence");
      }
      return true;
   }
};
void SelfTest() {
   Processor processor(false);
   cv::Mat blank(720,1280,CV_8UC3,cv::Scalar(20,20,20));
   cv::Mat card=blank.clone();
   cv::rectangle(card,cv::Rect(475,175,250,350),cv::Scalar(240,240,240),-1);
   unsigned events{};
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=1)throw std::runtime_error("first stable card must emit one event");
   for(int i=0;i<8;++i)processor.Process(blank);
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=2)throw std::runtime_error("reinserted card must emit second event");
   std::println("SMOKE|RESULT|PASS|stable card, no duplicates, removal and reentry");
}
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
   std::println("   or: --camera=0 [--save-events] (requires OpenCV videoio)");
}
}
int main(int argc,char** argv) {
   try{
      bool selfTest=false,save=false;
      std::string folder;
      std::optional<int> camera;
      for(int i=1;i<argc;++i){
         std::string_view arg(argv[i]);
         if(arg=="--help"){Usage();return 0;}
         if(arg=="--self-test")selfTest=true;
         else if(arg=="--save-events")save=true;
         else if(arg.starts_with("--sequence="))folder=arg.substr(11);
         else if(arg.starts_with("--camera="))camera=CameraIndex(arg.substr(9));
         else throw std::runtime_error("unknown argument (use --help)");
      }
      if(selfTest){SelfTest();return 0;}
      if(folder.empty()==!camera)
         throw std::runtime_error("select one of --sequence or --camera");
      Processor processor(save);
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
      cv::VideoCapture capture(*camera,cv::CAP_ANY);
      if(!capture.isOpened())
         throw std::runtime_error("cannot open camera; check permissions and ID");
      std::println("[CAM] live camera={}; Ctrl+C stops; no network or database writes",
         *camera);
      cv::Mat frame;
      while(capture.read(frame)&&!frame.empty())processor.Process(frame);
      throw std::runtime_error("camera stopped delivering frames");
#else
      throw std::runtime_error("OpenCV videoio not present in installed BuildEngine stack; use --sequence");
#endif
   }
   catch(std::exception const& e){
      std::println(std::cerr,"[CAM] ERROR: {}",e.what());return 1;
   }
}
