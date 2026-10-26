// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/** \file main.cpp
    \brief First-stage camera card detector: present, stable, changed, absent.
    \details No Scryfall requests, no database access, no physical edition
             identification. Image evidence is optional and local.
*/
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry.hpp>
#include <opencv2/imgcodecs.hpp>
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
#include <opencv2/videoio.hpp>
#endif
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <chrono>
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

#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
class CameraWindow {
   HWND m_hwnd{};
   cv::Mat m_pixels;
   static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
      if(msg==WM_NCCREATE) {
         auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
         SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
         return TRUE;
      }
      auto* self=reinterpret_cast<CameraWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
      if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}
      if(msg==WM_DESTROY){if(self)self->m_hwnd=nullptr;return 0;}
      if(msg==WM_PAINT) {
         PAINTSTRUCT ps{};
         HDC dc=BeginPaint(hwnd,&ps);
         RECT rc{};GetClientRect(hwnd,&rc);
         FillRect(dc,&rc,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
         if(self&&!self->m_pixels.empty()&&rc.right>0&&rc.bottom>0) {
            auto const& img=self->m_pixels;
            double scale=std::min(static_cast<double>(rc.right)/img.cols,
               static_cast<double>(rc.bottom)/img.rows);
            int w=std::max(1,static_cast<int>(img.cols*scale));
            int h=std::max(1,static_cast<int>(img.rows*scale));
            BITMAPINFO info{};
            info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
            info.bmiHeader.biWidth=img.cols;
            info.bmiHeader.biHeight=-img.rows;
            info.bmiHeader.biPlanes=1;
            info.bmiHeader.biBitCount=32;
            info.bmiHeader.biCompression=BI_RGB;
            SetStretchBltMode(dc,HALFTONE);
            StretchDIBits(dc,(rc.right-w)/2,(rc.bottom-h)/2,w,h,
               0,0,img.cols,img.rows,img.data,&info,DIB_RGB_COLORS,SRCCOPY);
         }
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
      m_hwnd=CreateWindowExW(0,cls.lpszClassName,L"DeckKernel - camera preview",
         WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,
         920,730,nullptr,nullptr,instance,this);
      if(!m_hwnd)throw std::runtime_error("camera preview creation failed");
      ShowWindow(m_hwnd,SW_SHOW);
   }
   bool Messages() {
      MSG message{};
      while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
         TranslateMessage(&message);
         DispatchMessageW(&message);
      }
      return m_hwnd!=nullptr;
   }
   void Show(cv::Mat const& frame,int index,double fps,double brightness) {
      if(frame.channels()==3)cv::cvtColor(frame,m_pixels,cv::COLOR_BGR2BGRA);
      else if(frame.channels()==4)m_pixels=frame.clone();
      else cv::cvtColor(frame,m_pixels,cv::COLOR_GRAY2BGRA);
      auto title=std::format(L"DeckKernel - camera {} - {}x{} - {:.1f} fps - brightness {:.0f}",
         index,frame.cols,frame.rows,fps,brightness);
      SetWindowTextW(m_hwnd,title.c_str());
      InvalidateRect(m_hwnd,nullptr,FALSE);
   }
};
#endif

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
   std::println("   or: --camera=0 [--backend=any|dshow|msmf] [--save-events]");
}
}
int main(int argc,char** argv) {
   try{
      bool selfTest=false,save=false;
      std::string backend="any";
      std::string folder;
      std::optional<int> camera;
      for(int i=1;i<argc;++i){
         std::string_view arg(argv[i]);
         if(arg=="--help"){Usage();return 0;}
         if(arg=="--self-test")selfTest=true;
         else if(arg=="--save-events")save=true;
         else if(arg.starts_with("--backend="))backend=arg.substr(10);
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
      int api=cv::CAP_ANY;
      if(backend=="dshow")api=cv::CAP_DSHOW;
      else if(backend=="msmf")api=cv::CAP_MSMF;
      else if(backend!="any")throw std::runtime_error("use --backend=any|dshow|msmf");
      std::println("[CAM] opening camera={} backend-request={}",*camera,backend);
      cv::VideoCapture capture(*camera,api);
      if(!capture.isOpened())
         throw std::runtime_error("cannot open camera: try another index/backend or Windows camera privacy settings");
      std::println("[CAM] opened camera={} backend={}",*camera,capture.getBackendName());
      CameraWindow preview(*camera);
      cv::Mat frame;
      unsigned long long count=0;
      unsigned failures=0;
      auto interval=std::chrono::steady_clock::now();
      double fps=0.0;
      while(preview.Messages()){
         if(!capture.read(frame)||frame.empty()){
            if(++failures==1 || failures%30==0)
               std::println(std::cerr,"[CAM] no frame returned, consecutive failures={}",failures);
            if(failures>=90)throw std::runtime_error("camera open but no frames received");
            continue;
         }
         failures=0;
         ++count;
         cv::Scalar const mean=cv::mean(frame);
         double const brightness=frame.channels()==3?
            (mean[0]+mean[1]+mean[2])/3.0:mean[0];
         processor.Process(frame);
         preview.Show(frame,*camera,fps,brightness);
         auto now=std::chrono::steady_clock::now();
         double seconds=std::chrono::duration<double>(now-interval).count();
         if(seconds>=2.0){
            fps=count/seconds;
            std::println("[CAM] backend={} frames={} size={}x{} fps={:.1f} brightness={:.0f}",
               capture.getBackendName(),count,frame.cols,frame.rows,fps,brightness);
            if(brightness<8)
               std::println("[CAM] nearly black frames: check lens/privacy shutter");
            count=0;interval=now;
         }
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
