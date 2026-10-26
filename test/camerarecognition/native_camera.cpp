// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/**
\file native_camera.cpp
\brief DirectShow hardware activation probe, independent of OpenCV.
\details Lists actual Windows video-input devices, binds one by moniker,
         constructs a native DirectShow filter graph and starts the source.
         It tests hardware activation, not OpenCV's videoio wrapper.
*/
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <objbase.h>
#include <ocidl.h>
#include <initguid.h>
#include <dshow.h>

#include <charconv>
#include <chrono>
#include <format>
#include <iostream>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace {

void Require(HRESULT hr,std::string_view operation) {
   if(FAILED(hr))
      throw std::runtime_error(std::format("{} failed: HRESULT=0x{:08X}",
         operation,static_cast<unsigned>(hr)));
}
std::string Utf8(wchar_t const* value) {
   if(!value)return {};
   int length=WideCharToMultiByte(CP_UTF8,0,value,-1,nullptr,0,nullptr,nullptr);
   if(length<=1)return {};
   std::string out(static_cast<std::size_t>(length),'\0');
   WideCharToMultiByte(CP_UTF8,0,value,-1,out.data(),length,nullptr,nullptr);
   out.resize(static_cast<std::size_t>(length-1));
   return out;
}
template<class T>
class Com final {
   T* m_pointer{};
public:
   Com()=default;
   ~Com(){Reset();}
   Com(Com const&)=delete;
   Com& operator=(Com const&)=delete;
   Com(Com&& other) noexcept:m_pointer(std::exchange(other.m_pointer,nullptr)) {}
   Com& operator=(Com&& other) noexcept {
      if(this!=&other){Reset();m_pointer=std::exchange(other.m_pointer,nullptr);}
      return *this;
   }
   void Reset() noexcept {if(m_pointer){m_pointer->Release();m_pointer=nullptr;}}
   T* Get() const noexcept {return m_pointer;}
   T** Put() noexcept {Reset();return &m_pointer;}
   void** VoidPut() noexcept {return reinterpret_cast<void**>(Put());}
   T* operator->() const noexcept {return m_pointer;}
   explicit operator bool() const noexcept {return m_pointer!=nullptr;}
};

struct ComSession {
   ComSession(){Require(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED),"CoInitializeEx");}
   ~ComSession(){CoUninitialize();}
   ComSession(ComSession const&)=delete;
   ComSession& operator=(ComSession const&)=delete;
};

std::string Property(IMoniker* moniker,wchar_t const* key) {
   Com<IPropertyBag> bag;
   if(FAILED(moniker->BindToStorage(nullptr,nullptr,IID_IPropertyBag,bag.VoidPut())))
      return {};
   VARIANT value{};
   VariantInit(&value);
   std::string out;
   if(SUCCEEDED(bag->Read(key,&value,nullptr)) && value.vt==VT_BSTR)
      out=Utf8(value.bstrVal);
   VariantClear(&value);
   return out;
}

struct Device {
   unsigned index{};
   std::string name;
   std::string path;
   Com<IMoniker> moniker;
};

std::vector<Device> Devices() {
   Com<ICreateDevEnum> enumerator;
   Require(CoCreateInstance(CLSID_SystemDeviceEnum,nullptr,CLSCTX_INPROC_SERVER,
      IID_ICreateDevEnum,enumerator.VoidPut()),"Create SystemDeviceEnum");
   Com<IEnumMoniker> list;
   HRESULT hr=enumerator->CreateClassEnumerator(
      CLSID_VideoInputDeviceCategory,list.Put(),0);
   if(hr==S_FALSE)return {};
   Require(hr,"Enumerate video input devices");

   std::vector<Device> devices;
   for(;;) {
      Com<IMoniker> moniker;
      ULONG fetched=0;
      hr=list->Next(1,moniker.Put(),&fetched);
      if(hr!=S_OK||fetched!=1)break;
      Device device;
      device.index=static_cast<unsigned>(devices.size());
      device.name=Property(moniker.Get(),L"FriendlyName");
      device.path=Property(moniker.Get(),L"DevicePath");
      device.moniker=std::move(moniker);
      devices.push_back(std::move(device));
   }
   return devices;
}

void ShowDevices(std::vector<Device> const& devices) {
   std::println("[DSHOW] Windows video-input devices: {}",devices.size());
   for(auto const& device:devices){
      std::println("[DSHOW] [{}] {}",device.index,device.name);
      if(!device.path.empty())
         std::println("[DSHOW]     path={}",device.path);
   }
   if(devices.empty())
      std::println("[DSHOW] No native DirectShow video-input devices enumerated.");
}

void RunNativeGraph(Device const& device,unsigned seconds) {
   Com<IGraphBuilder> graph;
   Require(CoCreateInstance(CLSID_FilterGraph,nullptr,CLSCTX_INPROC_SERVER,
      IID_IGraphBuilder,graph.VoidPut()),"Create DirectShow FilterGraph");
   Com<ICaptureGraphBuilder2> builder;
   Require(CoCreateInstance(CLSID_CaptureGraphBuilder2,nullptr,
      CLSCTX_INPROC_SERVER,IID_ICaptureGraphBuilder2,builder.VoidPut()),
      "Create CaptureGraphBuilder2");
   Require(builder->SetFiltergraph(graph.Get()),"SetFiltergraph");
   Com<IBaseFilter> source;
   Require(device.moniker->BindToObject(nullptr,nullptr,IID_IBaseFilter,
      source.VoidPut()),"Bind native camera filter");
   Require(graph->AddFilter(source.Get(),L"DeckKernel camera"),
      "Add camera filter to graph");

   std::println("[DSHOW] Hardware graph binding succeeded: {}",
      device.name);
   HRESULT hr=builder->RenderStream(&PIN_CATEGORY_PREVIEW,
      &MEDIATYPE_Video,source.Get(),nullptr,nullptr);
   if(FAILED(hr)){
      std::println("[DSHOW] No preview pin: HRESULT=0x{:08X}; trying capture pin",
         static_cast<unsigned>(hr));
      hr=builder->RenderStream(&PIN_CATEGORY_CAPTURE,&MEDIATYPE_Video,
         source.Get(),nullptr,nullptr);
   }
   Require(hr,"Render native camera video stream");

   Com<IMediaControl> control;
   Require(graph->QueryInterface(IID_IMediaControl,control.VoidPut()),
      "Query IMediaControl");
   hr=control->Run();
   Require(hr,"IMediaControl::Run");
   OAFilterState state=State_Stopped;
   HRESULT const stateHr=control->GetState(5000,&state);
   Require(stateHr,"GetState");
   std::println("[DSHOW] Filter graph state={} | camera={} | duration={}s",
      state==State_Running?"RUNNING":
      (state==State_Paused?"PAUSED":"STOPPED"),device.name,seconds);
   if(state!=State_Running)
      throw std::runtime_error("DirectShow graph did not enter RUNNING state");

   // DirectShow's Video Renderer creates its own preview. Keep the COM
   // apartment responsive while the hardware stream is active.
   Com<IVideoWindow> videoWindow;
   if(SUCCEEDED(graph->QueryInterface(IID_IVideoWindow,videoWindow.VoidPut())))
      videoWindow->put_Visible(OATRUE);
   auto const deadline=std::chrono::steady_clock::now()+
      std::chrono::seconds(seconds);
   while(std::chrono::steady_clock::now()<deadline) {
      MSG msg{};
      while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){
         TranslateMessage(&msg);
         DispatchMessageW(&msg);
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(25));
   }
   Require(control->Stop(),"Stop native capture graph");
   std::println("SMOKE|CHECK|native-dshow-graph|PASS|graph entered RUNNING; "
      "observe camera LED and preview independently");
}

unsigned Number(std::string_view text) {
   unsigned out{};
   auto const parsed=std::from_chars(text.data(),text.data()+text.size(),out);
   if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size())
      throw std::runtime_error("camera device index and seconds must be unsigned integers");
   return out;
}

void Help() {
   std::println("DeckKernel native Windows DirectShow camera probe (no OpenCV)");
   std::println("  --list              List friendly names and DirectShow device paths");
   std::println("  --device=N          Start hardware capture graph for device N");
   std::println("  --seconds=N         Preview duration (default 8, maximum 30)");
   std::println("  --help              Show this help");
}
}

int main(int argc,char** argv) {
   try {
      bool list=false;
      unsigned seconds=8;
      bool hasDevice=false;
      unsigned index{};
      for(int n=1;n<argc;++n) {
         std::string_view arg(argv[n]);
         if(arg=="--help"){Help();return 0;}
         if(arg=="--list")list=true;
         else if(arg.starts_with("--device=")){
            index=Number(arg.substr(9));hasDevice=true;
         } else if(arg.starts_with("--seconds="))
            seconds=Number(arg.substr(10));
         else throw std::runtime_error("unknown option: use --help");
      }
      if(!list && !hasDevice) {Help();return 2;}
      if(seconds<1||seconds>30)
         throw std::runtime_error("--seconds must be between 1 and 30");
      ComSession com;
      auto devices=Devices();
      ShowDevices(devices);
      if(!hasDevice)return devices.empty()?1:0;
      if(index>=devices.size())
         throw std::runtime_error("device index not present in native DirectShow enumeration");
      RunNativeGraph(devices[index],seconds);
      return 0;
   } catch(std::exception const& ex){
      std::println(std::cerr,"[DSHOW] FAIL: {}",ex.what());
      std::println("SMOKE|RESULT|FAIL|native Windows camera capture");
      return 1;
   }
}
