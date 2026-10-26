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
// A single uninterrupted card-presence session produces exactly one event.
// Hash changes (illumination, corner jitter, exposure) MUST NOT be treated as
// card swaps. A new session is armed only after a sustained empty scene.
class StableCardTrigger {
   std::optional<hash_ty> m_previous;
   unsigned m_stable{},m_absent{},m_events{};
   bool m_armed{true};
public:
   struct Event {unsigned number;hash_ty fingerprint;cv::Mat card;};
   std::optional<Event> Accept(std::optional<FoundCard> const& found) {
      constexpr unsigned stableFrames=5;
      constexpr unsigned removalFrames=45; // 1.5 seconds at 30 FPS
      if(!found) {
         m_previous.reset();
         m_stable=0;
         if(++m_absent>=removalFrames)m_armed=true;
         return std::nullopt;
      }
      m_absent=0;
      if(!m_armed)return std::nullopt;
      if(m_previous && std::popcount(*m_previous^found->fingerprint)<=8)
         ++m_stable;
      else
         m_stable=1;
      m_previous=found->fingerprint;
      if(m_stable<stableFrames)return std::nullopt;
      m_armed=false;
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
      if(msg==WM_ERASEBKGND)return 1; // double-buffered WM_PAINT paints all pixels
      if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}
      if(msg==WM_DESTROY){if(self)self->m_hwnd=nullptr;return 0;}
      if(msg==WM_PAINT) {
         PAINTSTRUCT ps{};
         HDC dc=BeginPaint(hwnd,&ps);
         RECT rc{};GetClientRect(hwnd,&rc);
         int const clientWidth=rc.right-rc.left;
         int const clientHeight=rc.bottom-rc.top;
         HDC buffer=CreateCompatibleDC(dc);
         HBITMAP bitmap=(clientWidth>0&&clientHeight>0)?
            CreateCompatibleBitmap(dc,clientWidth,clientHeight):nullptr;
         HGDIOBJ oldBitmap=bitmap?SelectObject(buffer,bitmap):nullptr;
         if(bitmap)
            FillRect(buffer,&rc,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
         if(bitmap&&self&&!self->m_pixels.empty()&&clientWidth>0&&clientHeight>0) {
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
            SetStretchBltMode(buffer,HALFTONE);
            StretchDIBits(buffer,(rc.right-w)/2,(rc.bottom-h)/2,w,h,
               0,0,img.cols,img.rows,img.data,&info,DIB_RGB_COLORS,SRCCOPY);
         }
         if(bitmap) {
            BitBlt(dc,0,0,clientWidth,clientHeight,buffer,0,0,SRCCOPY);
            SelectObject(buffer,oldBitmap);
            DeleteObject(bitmap);
         }
         DeleteDC(buffer);
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
   // Hash fluctuations with uninterrupted presence are NOT a new card.
   cv::Mat fluctuating=card.clone();
   cv::rectangle(fluctuating,cv::Rect(495,205,140,140),
      cv::Scalar(70,80,90),-1);
   for(int i=0;i<30;++i)
      if(processor.Process(i%2?card:fluctuating))++events;
   if(events!=1)throw std::runtime_error("hash jitter caused duplicate card event");
   // Short contour dropouts must not count as the card being removed.
   for(int i=0;i<12;++i)processor.Process(blank);
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=1)throw std::runtime_error("brief contour loss caused a duplicate");
   for(int i=0;i<48;++i)processor.Process(blank);
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=2)throw std::runtime_error("reinserted card must emit second event");
   std::println("SMOKE|RESULT|PASS|stable event, jitter suppression, short occlusion and reentry");
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
   std::println("   or: --camera=0 [--backend=any|dshow|msmf] [--width=640 --height=480 --fps=30 --fourcc=auto|mjpg|yuy2] [--save-events]");
   std::println("   or: --probe-camera=0          test 4 Windows camera delivery modes");
}
}
int main(int argc,char** argv) {
   try{
      bool selfTest=false,save=false;
      std::string backend="any";
      std::string fourcc="auto";
      int width=640,height=480,fpsRequested=30;
      std::optional<int> probeCamera;
      std::string folder;
      std::optional<int> camera;
      for(int i=1;i<argc;++i){
         std::string_view arg(argv[i]);
         if(arg=="--help"){Usage();return 0;}
         if(arg=="--self-test")selfTest=true;
         else if(arg=="--save-events")save=true;
         else if(arg.starts_with("--backend="))backend=arg.substr(10);
         else if(arg.starts_with("--fourcc="))fourcc=arg.substr(9);
         else if(arg.starts_with("--width="))width=CameraIndex(arg.substr(8));
         else if(arg.starts_with("--height="))height=CameraIndex(arg.substr(9));
         else if(arg.starts_with("--fps="))fpsRequested=CameraIndex(arg.substr(6));
         else if(arg.starts_with("--probe-camera="))probeCamera=CameraIndex(arg.substr(15));
         else if(arg.starts_with("--sequence="))folder=arg.substr(11);
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
      if(width<160||height<120||width>3840||height>2160||
         fpsRequested<1||fpsRequested>120)
         throw std::runtime_error("invalid camera width, height or fps");
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
      int api=Api(backend);
      std::println("[CAM] opening camera={} backend-request={}",*camera,backend);
      cv::VideoCapture capture(*camera,api);
      if(!capture.isOpened())
         throw std::runtime_error("cannot open camera: try another index/backend or Windows camera privacy settings");
      std::println("[CAM] opened camera={} backend={}",*camera,capture.getBackendName());
      ConfigureCapture(capture,width,height,fpsRequested,fourcc);
      CameraWindow preview(*camera);
      cv::Mat frame;
      unsigned long long count=0;
      unsigned failures=0,blackFrames=0;
      auto interval=std::chrono::steady_clock::now();
      double fps=0.0;
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
         if(signal.nonblack) {
            blackFrames=0;
            processor.Process(frame);
         } else {
            ++blackFrames;
         }
         preview.Show(frame,*camera,fps,signal.brightness);
         auto now=std::chrono::steady_clock::now();
         double const seconds=std::chrono::duration<double>(now-interval).count();
         if(seconds>=2.0){
            fps=count/seconds;
            std::println("[CAM] backend={} frames={} size={}x{} fps={:.1f} "
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
