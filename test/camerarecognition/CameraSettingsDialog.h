// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file CameraSettingsDialog.h
\brief Native Windows launch settings for the graphical DeckKernel camera.
\details Stores capture and detection preferences in the local Cache directory.
   The SDL/Skia camera dashboard remains unchanged after initialization.
*/
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <opencv2/core.hpp>
#include <filesystem>
#include <string>
#include <array>
#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>
#include <fstream>
#include <regex>
#include <iterator>
#include <optional>

namespace deckkernel::camera {
   /**
   \brief Direction toward which the physical card's TOP edge points.
   \details Automatic preserves legacy portrait-only geometry and 0/180
      layout matching; all named orientations are absolute image directions.
   */
   enum class CardOrientation { Automatic, Top, Bottom, Left, Right };

   /** \brief Enumerate named DirectShow video-input devices in native index order. */
   [[nodiscard]] std::vector<std::pair<int,std::wstring>> EnumerateCameraDevices();

   struct CameraLaunchSettings {
      int iCamera{0};
      int iWidth{3840};
      int iHeight{2160};
      int iFps{30};
      bool boSaveEvents{};
      bool boVerbose{};
      CardOrientation eOrientation{CardOrientation::Automatic};
   };

   /**
   \brief Local JSON configuration for the graphical camera.
   \details Only the small fixed settings schema is supported; missing or
      invalid data is not treated as a valid previous configuration.
   */
   class CameraSettingsFile final {
      [[nodiscard]] static std::filesystem::path Path() {
         return std::filesystem::absolute(
            ".cache/camera-recognition/camera-settings.json");
      }
      [[nodiscard]] static std::optional<int> Integer(
         std::string const& strContents,char const* szKey) {
         std::regex const aExpression(
            std::string{"\""}+szKey+"\"\\s*:\\s*(-?[0-9]+)");
         std::smatch aMatch;
         if(!std::regex_search(strContents,aMatch,aExpression))
            return std::nullopt;
         try { return std::stoi(aMatch[1].str()); }
         catch(std::exception const&) { return std::nullopt; }
      }
      [[nodiscard]] static std::optional<bool> Boolean(
         std::string const& strContents,char const* szKey) {
         std::regex const aExpression(
            std::string{"\""}+szKey+"\"\\s*:\\s*(true|false)");
         std::smatch aMatch;
         if(!std::regex_search(strContents,aMatch,aExpression))
            return std::nullopt;
         return aMatch[1].str()=="true";
      }
   public:
      /**
      \brief Load existing JSON settings.
      \details No file or invalid schema returns false, requesting the dialog.
      \param aSettings Receives validated configuration.
      \returns True for a complete valid configuration.
      \throw None.
      */
      [[nodiscard]] static bool Load(CameraLaunchSettings& aSettings) {
         std::ifstream aStream(Path(),std::ios::binary);
         if(!aStream)return false;
         std::string const strContents(
            std::istreambuf_iterator<char>{aStream},
            std::istreambuf_iterator<char>{});
         auto const iVersion=Integer(strContents,"version");
         auto const iCamera=Integer(strContents,"camera");
         auto const iWidth=Integer(strContents,"width");
         auto const iHeight=Integer(strContents,"height");
         auto const iFps=Integer(strContents,"fps");
         auto const iOrientation=Integer(strContents,"orientation");
         auto const boSave=Boolean(strContents,"saveEvents");
         auto const boVerbose=Boolean(strContents,"verbose");
         if(!iVersion || *iVersion!=1 || !iCamera || !iWidth ||
            !iHeight || !iFps || !iOrientation || !boSave ||
            !boVerbose || *iCamera<0 || *iCamera>15 ||
            *iWidth<160 || *iWidth>3840 || *iHeight<120 ||
            *iHeight>2160 || *iFps<1 || *iFps>120 ||
            *iOrientation<0 || *iOrientation>4)
            return false;
         aSettings=CameraLaunchSettings{
            *iCamera,*iWidth,*iHeight,*iFps,*boSave,*boVerbose,
            static_cast<CardOrientation>(*iOrientation)};
         return true;
      }
      /**
      \brief Persist confirmed camera options as JSON.
      \param aSettings Values to write.
      \returns Nothing.
      \throw std::runtime_error On file creation or write failure.
      */
      static void Save(CameraLaunchSettings const& aSettings) {
         std::filesystem::create_directories(Path().parent_path());
         std::ofstream aStream(Path(),std::ios::binary|std::ios::trunc);
         if(!aStream)throw std::runtime_error(
            "cannot create camera-settings.json");
         aStream<<"{\n"
            "  \"version\": 1,\n"
            "  \"camera\": "<<aSettings.iCamera<<",\n"
            "  \"width\": "<<aSettings.iWidth<<",\n"
            "  \"height\": "<<aSettings.iHeight<<",\n"
            "  \"fps\": "<<aSettings.iFps<<",\n"
            "  \"orientation\": "<<static_cast<int>(
               aSettings.eOrientation)<<",\n"
            "  \"saveEvents\": "<<(aSettings.boSaveEvents?
               "true":"false")<<",\n"
            "  \"verbose\": "<<(aSettings.boVerbose?
               "true":"false")<<"\n}\n";
         aStream.flush();
         if(!aStream)throw std::runtime_error(
            "cannot write camera-settings.json");
      }
   };

   /**
   \brief Modeless-control host used as a blocking startup dialog.
   \details Selection is saved only when Start is pressed.
   */
   class CameraSettingsDialog final {
      enum ControlId {
         Camera=1101,Resolution,Fps,Region,SaveEvents,Verbose,Start,Cancel
      };
      CameraLaunchSettings m_aSettings;
      HWND m_hWindow{};
      HWND m_hCamera{},m_hResolution{},m_hFps{},m_hRegion{};
      HWND m_hSave{},m_hVerbose{};
      std::vector<int> m_vecCameraIndices;
      bool m_boAccepted{};
      bool m_boFinished{};

      static void AddLabel(HWND hWindow,wchar_t const* szText,int iY) {
         CreateWindowExW(0,L"STATIC",szText,
            WS_CHILD|WS_VISIBLE,18,iY,155,23,hWindow,
            nullptr,GetModuleHandleW(nullptr),nullptr);
      }
      static HWND AddCombo(HWND hWindow,int iId,int iY) {
         return CreateWindowExW(0,L"COMBOBOX",L"",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|CBS_DROPDOWNLIST|
               WS_VSCROLL,174,iY,255,220,hWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(iId)),
            GetModuleHandleW(nullptr),nullptr);
      }
      static void AddItem(HWND hCombo,wchar_t const* szText) {
         SendMessageW(hCombo,CB_ADDSTRING,0,
            reinterpret_cast<LPARAM>(szText));
      }
      static int Selection(HWND hCombo) {
         return static_cast<int>(SendMessageW(hCombo,CB_GETCURSEL,0,0));
      }
      static void Select(HWND hCombo,int iIndex) {
         SendMessageW(hCombo,CB_SETCURSEL,
            static_cast<WPARAM>(iIndex),0);
      }
      void CreateControls() {
         AddLabel(m_hWindow,L"Camera device (index)",22);
         m_hCamera=AddCombo(m_hWindow,Camera,18);
         int iSelected=-1;
         auto const vecDevices=EnumerateCameraDevices();
         for(auto const& [iIndex,strName]:vecDevices) {
            m_vecCameraIndices.push_back(iIndex);
            std::wstring const strLabel=std::to_wstring(iIndex)+
               L": "+strName;
            AddItem(m_hCamera,strLabel.c_str());
            if(iIndex==m_aSettings.iCamera)
               iSelected=static_cast<int>(m_vecCameraIndices.size())-1;
         }
         // The camera name/index mapping follows the DirectShow backend.
         // If enumeration fails, retain manual index selection.
         if(m_vecCameraIndices.empty()) {
            for(int iIndex=0;iIndex<16;++iIndex) {
               m_vecCameraIndices.push_back(iIndex);
               std::wstring const strLabel=
                  L"Camera index "+std::to_wstring(iIndex);
               AddItem(m_hCamera,strLabel.c_str());
            }
            iSelected=std::clamp(m_aSettings.iCamera,0,15);
         }
         Select(m_hCamera,std::max(0,iSelected));
         AddLabel(m_hWindow,L"Resolution",64);
         m_hResolution=AddCombo(m_hWindow,Resolution,60);
         for(wchar_t const* szResolution:{
            L"3840 x 2160",L"2560 x 1440",L"1920 x 1080",
            L"1280 x 720",L"640 x 480"})
            AddItem(m_hResolution,szResolution);
         std::array<std::pair<int,int>,5> const arrRes{{
            {3840,2160},{2560,1440},{1920,1080},
            {1280,720},{640,480}
         }};
         int iResolution=0;
         for(int i=0;i<static_cast<int>(arrRes.size());++i)
            if(arrRes[i].first==m_aSettings.iWidth &&
               arrRes[i].second==m_aSettings.iHeight)iResolution=i;
         Select(m_hResolution,iResolution);
         AddLabel(m_hWindow,L"Requested FPS",106);
         m_hFps=AddCombo(m_hWindow,Fps,102);
         for(wchar_t const* szFps:{L"30",L"25",L"20",L"15"})
            AddItem(m_hFps,szFps);
         Select(m_hFps,m_aSettings.iFps==25?1:
            m_aSettings.iFps==20?2:m_aSettings.iFps==15?3:0);
         AddLabel(m_hWindow,L"Card top edge",148);
         m_hRegion=AddCombo(m_hWindow,Region,144);
         for(wchar_t const* szRegion:{
            L"Automatic",L"Top",L"Bottom",L"Left",L"Right"})
            AddItem(m_hRegion,szRegion);
         Select(m_hRegion,std::clamp(
            static_cast<int>(m_aSettings.eOrientation),0,4));
         m_hSave=CreateWindowExW(0,L"BUTTON",L"Save event images",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
            18,195,220,25,m_hWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(SaveEvents)),
            GetModuleHandleW(nullptr),nullptr);
         m_hVerbose=CreateWindowExW(0,L"BUTTON",
            L"Verbose log in Cache",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_AUTOCHECKBOX,
            18,229,220,25,m_hWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(Verbose)),
            GetModuleHandleW(nullptr),nullptr);
         SendMessageW(m_hSave,BM_SETCHECK,m_aSettings.boSaveEvents?
            BST_CHECKED:BST_UNCHECKED,0);
         SendMessageW(m_hVerbose,BM_SETCHECK,m_aSettings.boVerbose?
            BST_CHECKED:BST_UNCHECKED,0);
         CreateWindowExW(0,L"BUTTON",L"Start camera",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_DEFPUSHBUTTON,
            209,284,124,33,m_hWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(Start)),
            GetModuleHandleW(nullptr),nullptr);
         CreateWindowExW(0,L"BUTTON",L"Cancel",
            WS_CHILD|WS_VISIBLE|WS_TABSTOP,
            344,284,84,33,m_hWindow,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(Cancel)),
            GetModuleHandleW(nullptr),nullptr);
      }
      void Accept() {
         int const iSelection=Selection(m_hCamera);
         m_aSettings.iCamera=iSelection>=0 &&
            iSelection<static_cast<int>(m_vecCameraIndices.size())?
               m_vecCameraIndices[iSelection]:0;
         std::array<std::pair<int,int>,5> const arrRes{{
            {3840,2160},{2560,1440},{1920,1080},
            {1280,720},{640,480}
         }};
         int const iResolution=std::clamp(Selection(m_hResolution),0,4);
         m_aSettings.iWidth=arrRes[iResolution].first;
         m_aSettings.iHeight=arrRes[iResolution].second;
         std::array<int,4> const arrFps{{30,25,20,15}};
         m_aSettings.iFps=arrFps[std::clamp(Selection(m_hFps),0,3)];
         m_aSettings.eOrientation=static_cast<CardOrientation>(
            std::clamp(Selection(m_hRegion),0,4));
         m_aSettings.boSaveEvents=SendMessageW(
            m_hSave,BM_GETCHECK,0,0)==BST_CHECKED;
         m_aSettings.boVerbose=SendMessageW(
            m_hVerbose,BM_GETCHECK,0,0)==BST_CHECKED;
         CameraSettingsFile::Save(m_aSettings);
         m_boAccepted=true;
         m_boFinished=true;
         DestroyWindow(m_hWindow);
      }
      static LRESULT CALLBACK WindowProcedure(HWND hWindow,UINT uMessage,
         WPARAM wParam,LPARAM lParam) {
         auto* pDialog=reinterpret_cast<CameraSettingsDialog*>(
            GetWindowLongPtrW(hWindow,GWLP_USERDATA));
         if(uMessage==WM_NCCREATE) {
            auto const* pCreate=reinterpret_cast<CREATESTRUCTW const*>(lParam);
            SetWindowLongPtrW(hWindow,GWLP_USERDATA,
               reinterpret_cast<LONG_PTR>(pCreate->lpCreateParams));
            return TRUE;
         }
         if(!pDialog)return DefWindowProcW(hWindow,uMessage,wParam,lParam);
         if(uMessage==WM_CREATE) {
            pDialog->m_hWindow=hWindow;
            pDialog->CreateControls();
            return 0;
         }
         if(uMessage==WM_COMMAND && HIWORD(wParam)==BN_CLICKED) {
            if(LOWORD(wParam)==Start) {
               pDialog->Accept();
               return 0;
            }
            if(LOWORD(wParam)==Cancel) {
               pDialog->m_boFinished=true;
               DestroyWindow(hWindow);
               return 0;
            }
         }
         if(uMessage==WM_CLOSE) {
            pDialog->m_boFinished=true;
            DestroyWindow(hWindow);
            return 0;
         }
         return DefWindowProcW(hWindow,uMessage,wParam,lParam);
      }
   public:
      CameraSettingsDialog() {
         (void)CameraSettingsFile::Load(m_aSettings);
      }
      /**
      \brief Display editable startup settings and wait for user confirmation.
      \details Does not open any camera device before Start is confirmed.
      \param aSettings Receives the saved settings on confirmation.
      \returns True if Start was chosen; false on cancellation.
      \throw std::runtime_error If the Win32 window cannot be created.
      */
      [[nodiscard]] bool Show(CameraLaunchSettings& aSettings) {
         WNDCLASSW aClass{};
         aClass.lpfnWndProc=&WindowProcedure;
         aClass.hInstance=GetModuleHandleW(nullptr);
         aClass.lpszClassName=L"DeckKernelCameraSettingsWindow";
         aClass.hCursor=LoadCursorW(nullptr,IDC_ARROW);
         aClass.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);
         RegisterClassW(&aClass);
         m_hWindow=CreateWindowExW(WS_EX_DLGMODALFRAME,
            aClass.lpszClassName,L"DeckKernel - Camera Settings",
            WS_CAPTION|WS_SYSMENU|WS_VISIBLE,
            CW_USEDEFAULT,CW_USEDEFAULT,466,372,
            nullptr,nullptr,aClass.hInstance,this);
         if(!m_hWindow)
            throw std::runtime_error("cannot open camera settings dialog");
         MSG aMessage;
         while(!m_boFinished && GetMessageW(&aMessage,nullptr,0,0)>0) {
            if(!IsDialogMessageW(m_hWindow,&aMessage)) {
               TranslateMessage(&aMessage);
               DispatchMessageW(&aMessage);
            }
         }
         if(m_boAccepted)aSettings=m_aSettings;
         return m_boAccepted;
      }
   };
}
