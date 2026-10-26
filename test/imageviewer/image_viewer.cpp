// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/** \file image_viewer.cpp \brief Read-only native Windows image viewer and audit. */
#include <Windows.h>
#include <wincodec.h>
#include <shellapi.h>
#include <libpq-fe.h>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
constexpr int kList=1001,kPrev=1002,kNext=1003,kRefresh=1004,kEdit=1005,kGo=1006,kInfo=1007;
constexpr int kPageSize=60;
std::string Env(char const* key,char const* fallback) {
   if(char const* p=std::getenv(key);p&&*p)return p;
   return fallback;
}
std::wstring Wide(std::string_view s) {
   if(s.empty())return {};
   int n=MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0);
   if(n<1)return L"<invalid UTF8>";
   std::wstring out(static_cast<std::size_t>(n),L'\0');
   MultiByteToWideChar(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n);
   return out;
}
std::string Narrow(std::wstring_view s) {
   if(s.empty())return {};
   int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
   if(n<1)throw std::runtime_error("invalid wide text");
   std::string out(static_cast<std::size_t>(n),'\0');
   WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);
   return out;
}
std::uint64_t Number(std::string_view s) {
   std::uint64_t n{};
   auto r=std::from_chars(s.data(),s.data()+s.size(),n);
   if(r.ec!=std::errc{}||r.ptr!=s.data()+s.size())
      throw std::runtime_error("invalid positive integer");
   return n;
}
struct PgDispose{void operator()(PGconn* p)const noexcept{if(p)PQfinish(p);}};
struct RowDispose{void operator()(PGresult* p)const noexcept{if(p)PQclear(p);}};
using conn_t=std::unique_ptr<PGconn,PgDispose>;
using rows_t=std::unique_ptr<PGresult,RowDispose>;
template<class T>struct Com{
   T* p{};
   ~Com(){if(p)p->Release();}
   Com()=default;
   Com(Com const&)=delete;
   Com& operator=(Com const&)=delete;
   T** out(){return &p;}
   T* operator->()const{return p;}
};
rows_t Select(PGconn* p,char const* sql,std::optional<std::string> const& param={}) {
   char const* vals[]={param?param->c_str():nullptr};
   Oid types[]={25};
   rows_t r(param?PQexecParams(p,sql,1,types,vals,nullptr,nullptr,0):PQexec(p,sql));
   if(!r||PQresultStatus(r.get())!=PGRES_TUPLES_OK)
      throw std::runtime_error(std::string{"PostgreSQL SELECT: "}+PQerrorMessage(p));
   return r;
}
conn_t Connect(){
   std::array<std::string,7> arr{
      Env("DECKKERNEL_PGHOST","localhost"),Env("DECKKERNEL_PGPORT","5432"),
      Env("DECKKERNEL_PGDATABASE","DeckKernel"),Env("DECKKERNEL_PGUSER","deckkernel_user"),
      Env("DECKKERNEL_PGPASSWORD",""),Env("DECKKERNEL_PGSSLMODE","prefer"),
      Env("DECKKERNEL_PG_GSSENCMODE","disable")};
   char const* keys[]={"host","port","dbname","user","password","sslmode","gssencmode","application_name",nullptr};
   char const* vals[]={arr[0].c_str(),arr[1].c_str(),arr[2].c_str(),arr[3].c_str(),arr[4].c_str(),
      arr[5].c_str(),arr[6].c_str(),"DeckKernel-ImageViewer",nullptr};
   conn_t c(PQconnectdbParams(keys,vals,0));
   if(!c||PQstatus(c.get())!=CONNECTION_OK)
      throw std::runtime_error(std::string{"PostgreSQL connection: "}+(c?PQerrorMessage(c.get()):"null connection"));
   rows_t ro(PQexec(c.get(),"SET default_transaction_read_only=on"));
   if(!ro||PQresultStatus(ro.get())!=PGRES_COMMAND_OK)
      throw std::runtime_error("PostgreSQL read-only session setup failed");
   return c;
}
struct Entry{std::uint64_t id{};std::string kind;};
std::vector<Entry> FetchPage(PGconn* p,std::uint64_t cursor,bool backwards=false) {
   auto r=Select(p,backwards?
      "SELECT id::text,image_kind FROM deckkernel_test.scryfall_image_assets "
      "WHERE id<$1::bigint AND image_data IS NOT NULL ORDER BY id DESC LIMIT 60":
      "SELECT id::text,image_kind FROM deckkernel_test.scryfall_image_assets "
      "WHERE id>$1::bigint AND image_data IS NOT NULL ORDER BY id LIMIT 60",
      std::to_string(cursor));
   std::vector<Entry> result;
   for(int i=0;i<PQntuples(r.get());++i)
      result.push_back({Number(PQgetvalue(r.get(),i,0)),PQgetvalue(r.get(),i,1)});
   if(backwards)std::reverse(result.begin(),result.end());
   return result;
}
struct Image{
   std::uint64_t id{};
   std::string name,kind,url,sha,mime;
   std::vector<unsigned char> bytes;
};
Image ReadImage(PGconn* p,std::uint64_t id){
   auto r=Select(p,
      "SELECT a.id::text,a.image_kind,a.image_url,COALESCE(a.sha256,''),"
      "COALESCE(a.media_type,''),a.image_data,"
      "COALESCE((SELECT c.name||' / '||s.code||' / '||v.variant "
      "FROM deckkernel_test.scryfall_card_image_variants v "
      "JOIN deckkernel_test.scryfall_cards c ON c.id=v.card_id "
      "JOIN deckkernel_test.scryfall_sets s ON s.id=c.set_id "
      "WHERE v.asset_id=a.id ORDER BY c.id,v.face_id LIMIT 1),'[no linked card]') "
      "FROM deckkernel_test.scryfall_image_assets a "
      "WHERE a.id=$1::bigint AND a.image_data IS NOT NULL",std::to_string(id));
   if(PQntuples(r.get())!=1)throw std::runtime_error("asset absent or not yet downloaded");
   Image a;
   a.id=Number(PQgetvalue(r.get(),0,0));
   a.kind=PQgetvalue(r.get(),0,1);a.url=PQgetvalue(r.get(),0,2);
   a.sha=PQgetvalue(r.get(),0,3);a.mime=PQgetvalue(r.get(),0,4);
   a.name=PQgetvalue(r.get(),0,6);
   std::size_t len{};
   unsigned char* raw=PQunescapeBytea(
      reinterpret_cast<unsigned char const*>(PQgetvalue(r.get(),0,5)),&len);
   if(!raw)throw std::runtime_error("bytea decode failed");
   std::unique_ptr<unsigned char,decltype(&PQfreemem)> freeBytes(raw,&PQfreemem);
   if(!len||len>12U*1024U*1024U)throw std::runtime_error("invalid image length");
   a.bytes.assign(raw,raw+len);
   return a;
}
std::string Hash(std::vector<unsigned char> const& data){
   std::array<unsigned char,EVP_MAX_MD_SIZE> hash{};
   unsigned int n{};
   if(EVP_Digest(data.data(),data.size(),hash.data(),&n,EVP_sha256(),nullptr)!=1||n!=32)
      throw std::runtime_error("SHA-256 failed");
   constexpr char digits[]="0123456789abcdef";
   std::string h;h.reserve(64);
   for(unsigned i=0;i<n;++i){h+=digits[hash[i]>>4];h+=digits[hash[i]&15];}
   return h;
}
std::string Signature(std::vector<unsigned char> const& a){
   if(a.size()>3&&a[0]==255&&a[1]==216&&a[a.size()-2]==255&&a.back()==217)return "image/jpeg";
   if(a.size()>=8&&a[0]==137&&a[1]==80&&a[2]==78&&a[3]==71&&a[4]==13&&a[5]==10&&a[6]==26&&a[7]==10)return "image/png";
   if(a.size()>=12&&a[0]=='R'&&a[1]=='I'&&a[2]=='F'&&a[3]=='F'&&a[8]=='W'&&a[9]=='E'&&a[10]=='B'&&a[11]=='P')return "image/webp";
   return "unsupported";
}
struct Bitmap{UINT width{},height{};std::vector<unsigned char> bgra;};
Bitmap Decode(IWICImagingFactory* factory,std::vector<unsigned char>& data){
   Com<IWICStream> stream;
   if(FAILED(factory->CreateStream(stream.out()))||
      FAILED(stream->InitializeFromMemory(data.data(),static_cast<DWORD>(data.size()))))
      throw std::runtime_error("WIC memory stream failed");
   Com<IWICBitmapDecoder> decoder;
   if(FAILED(factory->CreateDecoderFromStream(stream.p,nullptr,WICDecodeMetadataCacheOnDemand,decoder.out())))
      throw std::runtime_error("WIC image decoder failed");
   Com<IWICBitmapFrameDecode> frame;
   if(FAILED(decoder->GetFrame(0,frame.out())))throw std::runtime_error("WIC first frame missing");
   Bitmap b;
   if(FAILED(frame->GetSize(&b.width,&b.height))||!b.width||!b.height||
      static_cast<std::uint64_t>(b.width)*b.height>40000000)
      throw std::runtime_error("invalid image dimensions");
   Com<IWICFormatConverter> converter;
   if(FAILED(factory->CreateFormatConverter(converter.out()))||
      FAILED(converter->Initialize(frame.p,GUID_WICPixelFormat32bppBGRA,WICBitmapDitherTypeNone,
         nullptr,0,WICBitmapPaletteTypeCustom)))
      throw std::runtime_error("WIC pixel conversion failed");
   auto const stride=static_cast<std::uint64_t>(b.width)*4;
   auto const bytes=stride*b.height;
   if(bytes>std::numeric_limits<UINT>::max())throw std::runtime_error("bitmap too large");
   b.bgra.resize(static_cast<std::size_t>(bytes));
   if(FAILED(converter->CopyPixels(nullptr,static_cast<UINT>(stride),
      static_cast<UINT>(bytes),b.bgra.data())))
      throw std::runtime_error("WIC pixel copy failed");
   return b;
}
struct Validated{Image image;Bitmap bitmap;bool hashOk{},mimeOk{};};
Validated Validate(PGconn* p,IWICImagingFactory* factory,std::uint64_t id){
   Validated a;
   a.image=ReadImage(p,id);
   a.hashOk=Hash(a.image.bytes)==a.image.sha;
   a.mimeOk=Signature(a.image.bytes)==a.image.mime;
   a.bitmap=Decode(factory,a.image.bytes);
   return a;
}
struct Viewer{
   conn_t conn;
   Com<IWICImagingFactory> factory;
   HWND win{},list{},info{},edit{},prev{},next{},refresh{},go{};
   std::vector<Entry> page;
   std::optional<Validated> current;
   std::uint64_t selected{};
   void Status(std::wstring const& s){SetWindowTextW(info,s.c_str());}
   void Display(std::uint64_t id){
      try {
         current=Validate(conn.get(),factory.p,id);
         selected=id;
         auto const& x=*current;
         Status(std::format(L"{} | Asset #{} | {} | {}x{} | {} KiB | SHA {} | Format {}\r\n{}\r\n{}",
            Wide(x.image.name),id,Wide(x.image.kind),x.bitmap.width,x.bitmap.height,
            x.image.bytes.size()/1024,x.hashOk?L"PASS":L"FAIL",x.mimeOk?L"PASS":L"FAIL",
            Wide(x.image.url),Wide(x.image.sha)));
      }catch(std::exception const& ex){
         current.reset();Status(std::format(L"Asset #{}: {}",id,Wide(ex.what())));
      }
      InvalidateRect(win,nullptr,TRUE);
   }
   void Fill(std::vector<Entry> nextPage,std::uint64_t preferred=0){
      page=std::move(nextPage);
      SendMessageW(list,LB_RESETCONTENT,0,0);
      int select=0;
      for(std::size_t i=0;i<page.size();++i){
         auto label=std::format(L"#{} {}",page[i].id,Wide(page[i].kind));
         SendMessageW(list,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(label.c_str()));
         if(page[i].id==preferred)select=static_cast<int>(i);
      }
      if(page.empty()){current.reset();Status(L"No downloaded assets on this page.");InvalidateRect(win,nullptr,TRUE);return;}
      SendMessageW(list,LB_SETCURSEL,select,0);Display(page[static_cast<std::size_t>(select)].id);
   }
   void Select(){
      auto i=SendMessageW(list,LB_GETCURSEL,0,0);
      if(i>=0&&static_cast<std::size_t>(i)<page.size())Display(page[static_cast<std::size_t>(i)].id);
   }
   void Refresh(){
      auto cursor=page.empty()?std::uint64_t{}:page.front().id-1;
      Fill(FetchPage(conn.get(),cursor),selected);
   }
   void Navigate(bool backwards){
      auto cursor=page.empty()?(backwards?std::numeric_limits<std::uint64_t>::max():0):
         (backwards?page.front().id:page.back().id);
      auto newPage=FetchPage(conn.get(),cursor,backwards);
      if(!newPage.empty())Fill(std::move(newPage));
   }
   void Go(){
      wchar_t raw[64]{};GetWindowTextW(edit,raw,64);
      auto id=Number(Narrow(raw));
      if(!id)throw std::runtime_error("ID cannot be zero");
      auto newPage=FetchPage(conn.get(),id-1);
      if(newPage.empty()||newPage.front().id!=id){Display(id);return;}
      Fill(std::move(newPage),id);
   }
   void Layout(int w,int h){
      MoveWindow(prev,8,6,104,25,TRUE);MoveWindow(next,116,6,104,25,TRUE);
      MoveWindow(refresh,224,6,90,25,TRUE);MoveWindow(edit,322,6,130,25,TRUE);
      MoveWindow(go,458,6,45,25,TRUE);MoveWindow(list,8,38,264,std::max(10,h-46),TRUE);
      MoveWindow(info,280,std::max(38,h-96),std::max(10,w-288),89,TRUE);
   }
   void Paint(HDC hdc,RECT rc){
      RECT area{280,38,rc.right-8,rc.bottom-102};
      HBRUSH brush=CreateSolidBrush(RGB(25,27,30));FillRect(hdc,&area,brush);DeleteObject(brush);
      if(!current||area.right<=area.left||area.bottom<=area.top)return;
      auto const& b=current->bitmap;
      double scale=std::min(static_cast<double>(area.right-area.left)/b.width,
         static_cast<double>(area.bottom-area.top)/b.height);
      int w=std::max(1,static_cast<int>(b.width*scale));
      int h=std::max(1,static_cast<int>(b.height*scale));
      int x=area.left+(area.right-area.left-w)/2,y=area.top+(area.bottom-area.top-h)/2;
      BITMAPINFO bmp{};bmp.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
      bmp.bmiHeader.biWidth=static_cast<LONG>(b.width);bmp.bmiHeader.biHeight=-static_cast<LONG>(b.height);
      bmp.bmiHeader.biPlanes=1;bmp.bmiHeader.biBitCount=32;bmp.bmiHeader.biCompression=BI_RGB;
      SetStretchBltMode(hdc,HALFTONE);
      StretchDIBits(hdc,x,y,w,h,0,0,static_cast<int>(b.width),static_cast<int>(b.height),
         b.bgra.data(),&bmp,DIB_RGB_COLORS,SRCCOPY);
   }
};
template<class F>void Safe(Viewer* p,F&& fn){
   try{fn();}catch(std::exception const& ex){p->Status(L"Error: "+Wide(ex.what()));}
}
LRESULT CALLBACK Proc(HWND hwnd,UINT message,WPARAM wp,LPARAM lp){
   auto* p=reinterpret_cast<Viewer*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
   if(message==WM_NCCREATE){
      auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
      SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(cs->lpCreateParams));return TRUE;
   }
   if(!p)return DefWindowProcW(hwnd,message,wp,lp);
   switch(message){
      case WM_CREATE:{
         p->win=hwnd;HINSTANCE inst=reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(hwnd,GWLP_HINSTANCE));
         auto button=[&](wchar_t const* label,int id){
            return CreateWindowExW(0,L"BUTTON",label,WS_CHILD|WS_VISIBLE|BS_PUSHBUTTON,
               0,0,10,10,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),inst,nullptr);
         };
         p->prev=button(L"Previous",kPrev);p->next=button(L"Next",kNext);
         p->refresh=button(L"Refresh",kRefresh);p->go=button(L"Go",kGo);
         p->edit=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
            0,0,10,10,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kEdit)),inst,nullptr);
         p->list=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",
            WS_CHILD|WS_VISIBLE|WS_VSCROLL|LBS_NOTIFY,0,0,10,10,hwnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(kList)),inst,nullptr);
         p->info=CreateWindowExW(0,L"STATIC",L"Connecting...",WS_CHILD|WS_VISIBLE|SS_LEFT,
            0,0,10,10,hwnd,reinterpret_cast<HMENU>(static_cast<INT_PTR>(kInfo)),inst,nullptr);
         Safe(p,[&]{p->Refresh();});return 0;
      }
      case WM_COMMAND:{
         int id=LOWORD(wp),notification=HIWORD(wp);
         if(id==kList&&notification==LBN_SELCHANGE)Safe(p,[&]{p->Select();});
         if(notification==BN_CLICKED){
            if(id==kPrev)Safe(p,[&]{p->Navigate(true);});
            if(id==kNext)Safe(p,[&]{p->Navigate(false);});
            if(id==kRefresh)Safe(p,[&]{p->Refresh();});
            if(id==kGo)Safe(p,[&]{p->Go();});
         }return 0;
      }
      case WM_KEYDOWN:if(wp==VK_F5){Safe(p,[&]{p->Refresh();});return 0;}break;
      case WM_SIZE:p->Layout(LOWORD(lp),HIWORD(lp));InvalidateRect(hwnd,nullptr,TRUE);return 0;
      case WM_PAINT:{
         PAINTSTRUCT ps{};auto hdc=BeginPaint(hwnd,&ps);
         RECT rect{};GetClientRect(hwnd,&rect);p->Paint(hdc,rect);EndPaint(hwnd,&ps);return 0;
      }
      case WM_DESTROY:PostQuitMessage(0);return 0;
   }
   return DefWindowProcW(hwnd,message,wp,lp);
}
int Audit(PGconn* p,IWICImagingFactory* factory,std::uint64_t limit){
   std::uint64_t cursor{},done{},failed{},passed{};
   while(true){
      auto page=FetchPage(p,cursor);
      if(page.empty())break;
      for(auto const& e:page){
         cursor=e.id;
         try{
            auto image=Validate(p,factory,cursor);
            if(image.hashOk&&image.mimeOk)++passed;
            else{++failed;std::println(stderr,"[IMAGE-CHECK] FAIL #{}: hash={} mime={}",cursor,image.hashOk,image.mimeOk);}
         }catch(std::exception const& ex){++failed;std::println(stderr,"[IMAGE-CHECK] FAIL #{} {}",cursor,ex.what());}
         ++done;
         if(done%100==0)std::println("[IMAGE-CHECK] checked={},pass={},failed={}",done,passed,failed);
         if(limit&&done>=limit)break;
      }
      if(limit&&done>=limit)break;
   }
   std::println("SMOKE|RESULT|{}|read-only-image-check checked={} passed={} failed={}",
      failed?"FAIL":"PASS",done,passed,failed);
   return failed?1:0;
}
}
int WINAPI WinMain(HINSTANCE inst,HINSTANCE,LPSTR,int show){
   if(FAILED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)))return 1;
   struct Uninit{~Uninit(){CoUninitialize();}} uninit;
   try{
      Viewer viewer;viewer.conn=Connect();
      if(FAILED(CoCreateInstance(CLSID_WICImagingFactory,nullptr,CLSCTX_INPROC_SERVER,
         IID_IWICImagingFactory,reinterpret_cast<void**>(viewer.factory.out()))))
         throw std::runtime_error("Windows Imaging Component initialization failed");
      int argc{};LPWSTR* argv=CommandLineToArgvW(GetCommandLineW(),&argc);
      struct FreeArgs{LPWSTR* p;~FreeArgs(){if(p)LocalFree(p);}} args{argv};
      if(argc>1&&argv){
         std::wstring_view arg=argv[1];
         if(arg==L"--check"||arg.starts_with(L"--check=")){
            std::uint64_t limit=arg==L"--check"?100:Number(Narrow(arg.substr(8)));
            if(!AttachConsole(ATTACH_PARENT_PROCESS))AllocConsole();
            std::freopen("CONOUT$","w",stdout);std::freopen("CONOUT$","w",stderr);
            return Audit(viewer.conn.get(),viewer.factory.p,limit);
         }
         throw std::runtime_error("Use --check[=N], or start without arguments");
      }
      WNDCLASSEXW wc{};wc.cbSize=sizeof(wc);wc.hInstance=inst;wc.lpfnWndProc=Proc;
      wc.lpszClassName=L"DeckKernelImageViewer";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
      wc.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
      if(!RegisterClassExW(&wc))throw std::runtime_error("RegisterClassExW failed");
      HWND window=CreateWindowExW(0,wc.lpszClassName,L"DeckKernel ImageViewer | read-only",
         WS_OVERLAPPEDWINDOW|WS_CLIPCHILDREN,CW_USEDEFAULT,CW_USEDEFAULT,
         1140,780,nullptr,nullptr,inst,&viewer);
      if(!window)throw std::runtime_error("CreateWindowExW failed");
      ShowWindow(window,show);UpdateWindow(window);
      MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
      return static_cast<int>(msg.wParam);
   }catch(std::exception const& ex){
      auto error=Wide(ex.what());MessageBoxW(nullptr,error.c_str(),L"DeckKernel ImageViewer",MB_ICONERROR|MB_OK);return 1;
   }
}
