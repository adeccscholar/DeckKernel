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

namespace deckkernel::camera {
   enum class SearchRegion { Full, Top, Bottom, Left, Right };

   struct CameraLaunchSettings {
      int iCamera{0};
      int iWidth{3840};
      int iHeight{2160};
      int iFps{30};
      bool boSaveEvents{};
      bool boVerbose{};
      SearchRegion eRegion{SearchRegion::Full};
   };

   /**
   \brief Compute the scan rectangle from the full camera dimensions.
   \details Directional zones overlap the center by ten percent so objects
      near a half-frame boundary are not immediately lost.
   \param aSize Full capture dimensions.
   \param eRegion Preferred card position.
   \returns Scan rectangle in native source pixels.
   \throw None.
   */
   [[nodiscard]] inline cv::Rect CameraSearchRect(
      cv::Size const& aSize,SearchRegion eRegion) {
      int const iW=aSize.width,iH=aSize.height;
      switch(eRegion) {
         case SearchRegion::Top:
            return {0,0,iW,iH*60/100};
         case SearchRegion::Bottom:
            return {0,iH*40/100,iW,iH-iH*40/100};
         case SearchRegion::Left:
            return {0,0,iW*60/100,iH};
         case SearchRegion::Right:
            return {iW*40/100,0,iW-iW*40/100,iH};
         default: return {0,0,iW,iH};
      }
   }

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
      bool m_boAccepted{};
      bool m_boFinished{};

      [[nodiscard]] static std::filesystem::path SettingsPath() {
         return std::filesystem::absolute(
            ".cache/camera-recognition/camera-settings.ini");
      }
      [[nodiscard]] static int ReadInt(wchar_t const* szKey,int iDefault) {
         auto const aPath=SettingsPath();
         return static_cast<int>(GetPrivateProfileIntW(
            L"Camera",szKey,iDefault,aPath.c_str()));
      }
      static void WriteInt(wchar_t const* szKey,int iValue) {
         std::wstring const strValue=std::to_wstring(iValue);
         auto const aPath=SettingsPath();
         WritePrivateProfileStringW(L"Camera",szKey,
            strValue.c_str(),aPath.c_str());
      }
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
         for(int iIndex=0;iIndex<16;++iIndex) {
            std::wstring const strLabel=L"Camera "+std::to_wstring(iIndex);
            AddItem(m_hCamera,strLabel.c_str());
         }
         Select(m_hCamera,std::clamp(m_aSettings.iCamera,0,15));
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
         AddLabel(m_hWindow,L"Card position",148);
         m_hRegion=AddCombo(m_hWindow,Region,144);
         for(wchar_t const* szRegion:{
            L"Full image",L"Top",L"Bottom",L"Left",L"Right"})
            AddItem(m_hRegion,szRegion);
         Select(m_hRegion,std::clamp(
            static_cast<int>(m_aSettings.eRegion),0,4));
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
         m_aSettings.iCamera=std::max(0,Selection(m_hCamera));
         std::array<std::pair<int,int>,5> const arrRes{{
            {3840,2160},{2560,1440},{1920,1080},
            {1280,720},{640,480}
         }};
         int const iResolution=std::clamp(Selection(m_hResolution),0,4);
         m_aSettings.iWidth=arrRes[iResolution].first;
         m_aSettings.iHeight=arrRes[iResolution].second;
         std::array<int,4> const arrFps{{30,25,20,15}};
         m_aSettings.iFps=arrFps[std::clamp(Selection(m_hFps),0,3)];
         m_aSettings.eRegion=static_cast<SearchRegion>(
            std::clamp(Selection(m_hRegion),0,4));
         m_aSettings.boSaveEvents=SendMessageW(
            m_hSave,BM_GETCHECK,0,0)==BST_CHECKED;
         m_aSettings.boVerbose=SendMessageW(
            m_hVerbose,BM_GETCHECK,0,0)==BST_CHECKED;
         std::filesystem::create_directories(SettingsPath().parent_path());
         WriteInt(L"Index",m_aSettings.iCamera);
         WriteInt(L"Width",m_aSettings.iWidth);
         WriteInt(L"Height",m_aSettings.iHeight);
         WriteInt(L"Fps",m_aSettings.iFps);
         WriteInt(L"Region",static_cast<int>(m_aSettings.eRegion));
         WriteInt(L"SaveEvents",m_aSettings.boSaveEvents);
         WriteInt(L"Verbose",m_aSettings.boVerbose);
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
         m_aSettings.iCamera=ReadInt(L"Index",0);
         m_aSettings.iWidth=ReadInt(L"Width",3840);
         m_aSettings.iHeight=ReadInt(L"Height",2160);
         m_aSettings.iFps=ReadInt(L"Fps",30);
         m_aSettings.eRegion=static_cast<SearchRegion>(
            std::clamp(ReadInt(L"Region",0),0,4));
         m_aSettings.boSaveEvents=ReadInt(L"SaveEvents",0)!=0;
         m_aSettings.boVerbose=ReadInt(L"Verbose",0)!=0;
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
