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
#include <SDL.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkFontStyle.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"
#include "include/ports/SkTypeface_win.h"
#include "CameraTypography.h"
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
#include <format>
#include <memory>
#include <string_view>

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
   \brief SDL2/Skia graphical configuration window for the camera application.
   \details The interface shares its renderer and typography stack with
      CameraDashboard. Click the left/right arrows to cycle field values;
      Enter starts, Escape cancels, and keyboard Up/Down selects a field.
   \note Camera devices are only enumerated, never opened by this dialog.
   */
   class CameraSettingsDialog final {
      CameraLaunchSettings m_aSettings;
      std::vector<std::pair<int,std::wstring>> m_vecDevices;
      int m_iResolution{};
      int m_iFps{};
      int m_iSelected{};
      bool m_boAccepted{};
      static constexpr int ciWidth=920;
      static constexpr int ciHeight=670;
      static constexpr int ciTop=157;
      static constexpr int ciRowHeight=65;
      static constexpr int ciCount=6;
      static constexpr std::array<std::pair<int,int>,5> carrRes{{
         {3840,2160},{2560,1440},{1920,1080},{1280,720},{640,480}
      }};
      static constexpr std::array<int,4> carrFps{{30,25,20,15}};

      [[nodiscard]] static std::string Utf8(std::wstring const& strText) {
         if(strText.empty())return {};
         int const iLength=WideCharToMultiByte(CP_UTF8,0,
            strText.data(),static_cast<int>(strText.size()),
            nullptr,0,nullptr,nullptr);
         if(iLength<=0)return {};
         std::string strResult(static_cast<std::size_t>(iLength),'\0');
         WideCharToMultiByte(CP_UTF8,0,strText.data(),
            static_cast<int>(strText.size()),strResult.data(),
            iLength,nullptr,nullptr);
         return strResult;
      }
      [[nodiscard]] static sk_sp<SkTypeface>& Typeface() {
         static sk_sp<SkTypeface> aFace=[] {
            auto aManager=SkFontMgr_New_DirectWrite();
            if(aManager)
               return aManager->matchFamilyStyle(
                  "Segoe UI",SkFontStyle());
            return sk_sp<SkTypeface>{};
         }();
         return aFace;
      }
      static void Text(SkCanvas* pCanvas,float flX,float flY,
         std::string const& strText,float flSize,SkColor uColor) {
         if(strText.empty())return;
         if(!Typeface()) {
            deckkernel_camera::typography::Draw(pCanvas,
               flX,flY,strText,flSize,uColor);
            return;
         }
         SkFont aFont(Typeface(),flSize);
         aFont.setEdging(SkFont::Edging::kSubpixelAntiAlias);
         SkPaint aPaint;
         aPaint.setAntiAlias(true);
         aPaint.setColor(uColor);
         pCanvas->drawSimpleText(strText.data(),strText.size(),
            SkTextEncoding::kUTF8,flX,flY,aFont,aPaint);
      }
      static void Rectangle(SkCanvas* pCanvas,SkRect const& aRect,
         SkColor uColor,float flRadius=12.0f) {
         SkPaint aPaint;
         aPaint.setAntiAlias(true);
         aPaint.setColor(uColor);
         pCanvas->drawRoundRect(aRect,flRadius,flRadius,aPaint);
      }
      [[nodiscard]] std::string Value(int iRow) const {
         switch(iRow) {
            case 0: {
               if(m_vecDevices.empty())return "No camera devices found";
               int const iIndex=std::clamp(
                  m_iSelected,0,static_cast<int>(m_vecDevices.size())-1);
               auto const& [iCamera,strName]=m_vecDevices[iIndex];
               std::string strResult=std::format(
                  "{}: {}",iCamera,Utf8(strName));
               if(strResult.size()>43)
                  strResult=strResult.substr(0,40)+"...";
               return strResult;
            }
            case 1:return std::format("{} x {}",
               carrRes[m_iResolution].first,
               carrRes[m_iResolution].second);
            case 2:return std::format("{} FPS",carrFps[m_iFps]);
            case 3: {
               constexpr std::array<std::string_view,5> arrLabels{{
                  "Automatic","Top","Bottom","Left","Right"
               }};
               return std::string(arrLabels[std::clamp(
                  static_cast<int>(m_aSettings.eOrientation),0,4)]);
            }
            case 4:return m_aSettings.boSaveEvents?"Enabled":"Disabled";
            case 5:return m_aSettings.boVerbose?"Enabled":"Disabled";
            default:return {};
         }
      }
      void Cycle(int iRow,int iDelta) {
         auto const Wrap=[&](int iCurrent,int iCount) {
            return (iCurrent+iDelta+iCount)%iCount;
         };
         switch(iRow) {
            case 0:
               if(!m_vecDevices.empty())
                  m_iSelected=Wrap(m_iSelected,
                     static_cast<int>(m_vecDevices.size()));
               break;
            case 1:m_iResolution=Wrap(m_iResolution,5);break;
            case 2:m_iFps=Wrap(m_iFps,4);break;
            case 3:
               m_aSettings.eOrientation=static_cast<CardOrientation>(
                  Wrap(static_cast<int>(m_aSettings.eOrientation),5));
               break;
            case 4:m_aSettings.boSaveEvents=!m_aSettings.boSaveEvents;break;
            case 5:m_aSettings.boVerbose=!m_aSettings.boVerbose;break;
         }
      }
      void Accept() {
         if(!m_vecDevices.empty())
            m_aSettings.iCamera=m_vecDevices[m_iSelected].first;
         m_aSettings.iWidth=carrRes[m_iResolution].first;
         m_aSettings.iHeight=carrRes[m_iResolution].second;
         m_aSettings.iFps=carrFps[m_iFps];
         CameraSettingsFile::Save(m_aSettings);
         m_boAccepted=true;
      }
      void Draw(SDL_Renderer* pRenderer) const {
         auto const aSurface=SkSurfaces::Raster(
            SkImageInfo::MakeN32Premul(ciWidth,ciHeight));
         if(!aSurface)return;
         SkCanvas* pCanvas=aSurface->getCanvas();
         pCanvas->clear(0xff0a141d);
         Rectangle(pCanvas,SkRect::MakeXYWH(
            28.f,24.f,864.f,106.f),0xff142633);
         Text(pCanvas,51.f,69.f,
            "DECKKERNEL  /  CAMERA SETTINGS",25.f,0xfff5f7fa);
         Text(pCanvas,52.f,102.f,
            "Configure capture and card orientation before scanning",
            14.f,0xff9cbdcf);
         constexpr std::array<std::string_view,ciCount> arrLabels{{
            "Camera device","Resolution","Frame rate",
            "Card top edge","Save event images","Verbose diagnostics"
         }};
         for(int iRow=0;iRow<ciCount;++iRow) {
            float const flY=static_cast<float>(ciTop+iRow*ciRowHeight);
            Rectangle(pCanvas,SkRect::MakeXYWH(
               28.f,flY,864.f,55.f),
               iRow==m_iSelected?0xff214758:0xff142633);
            Text(pCanvas,49.f,flY+35.f,
               std::string(arrLabels[iRow]),16.f,0xffc9d9e2);
            Rectangle(pCanvas,SkRect::MakeXYWH(
               410.f,flY+7.f,448.f,41.f),0xff0b202c,8.f);
            Text(pCanvas,428.f,flY+33.f,"<",
               22.f,0xff8ed5ee);
            Text(pCanvas,472.f,flY+33.f,Value(iRow),
               16.f,0xfff5f7fa);
            Text(pCanvas,817.f,flY+33.f,">",
               22.f,0xff8ed5ee);
         }
         Rectangle(pCanvas,SkRect::MakeXYWH(
            594.f,569.f,168.f,54.f),0xff218a78);
         Rectangle(pCanvas,SkRect::MakeXYWH(
            772.f,569.f,120.f,54.f),0xff263b48);
         Text(pCanvas,624.f,602.f,"Start camera",17.f,0xffffffff);
         Text(pCanvas,797.f,602.f,"Cancel",17.f,0xffd5e5ed);
         Text(pCanvas,37.f,603.f,
            "Arrow keys: select / change   Enter: start   Esc: cancel",
            12.f,0xff9cbdcf);
         SkPixmap aPixels;
         if(!aSurface->peekPixels(&aPixels))return;
         SDL_Texture* pTexture=SDL_CreateTexture(
            pRenderer,SDL_PIXELFORMAT_BGRA32,
            SDL_TEXTUREACCESS_STREAMING,ciWidth,ciHeight);
         if(!pTexture)return;
         SDL_UpdateTexture(pTexture,nullptr,aPixels.addr(),
            static_cast<int>(aPixels.rowBytes()));
         SDL_RenderClear(pRenderer);
         SDL_RenderCopy(pRenderer,pTexture,nullptr,nullptr);
         SDL_RenderPresent(pRenderer);
         SDL_DestroyTexture(pTexture);
      }
   public:
      CameraSettingsDialog() {
         (void)CameraSettingsFile::Load(m_aSettings);
         m_vecDevices=EnumerateCameraDevices();
         if(m_vecDevices.empty()) {
            for(int iIndex=0;iIndex<16;++iIndex)
               m_vecDevices.emplace_back(iIndex,
                  L"Camera index "+std::to_wstring(iIndex));
         }
         for(int iIndex=0;iIndex<
            static_cast<int>(m_vecDevices.size());++iIndex)
            if(m_vecDevices[iIndex].first==m_aSettings.iCamera) {
               m_iSelected=iIndex;
               break;
            }
         for(int iIndex=0;iIndex<5;++iIndex)
            if(carrRes[iIndex].first==m_aSettings.iWidth &&
               carrRes[iIndex].second==m_aSettings.iHeight)
               m_iResolution=iIndex;
         for(int iIndex=0;iIndex<4;++iIndex)
            if(carrFps[iIndex]==m_aSettings.iFps)
               m_iFps=iIndex;
      }
      /**
      \brief Show graphical settings and optionally persist accepted values.
      \details Uses SDL2 for window and interaction and Skia for rendering.
      \param aSettings Receives the selected settings after acceptance.
      \returns True after Start camera, false after Cancel or window close.
      \throw std::runtime_error If SDL cannot initialize the GUI.
      */
      [[nodiscard]] bool Show(CameraLaunchSettings& aSettings) {
         SDL_SetMainReady();
         if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0)
            throw std::runtime_error(SDL_GetError());
         SDL_Window* pWindow=SDL_CreateWindow(
            "DeckKernel - Camera Settings",SDL_WINDOWPOS_CENTERED,
            SDL_WINDOWPOS_CENTERED,ciWidth,ciHeight,
            SDL_WINDOW_ALLOW_HIGHDPI);
         if(!pWindow) {
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw std::runtime_error(SDL_GetError());
         }
         SDL_Renderer* pRenderer=SDL_CreateRenderer(
            pWindow,-1,SDL_RENDERER_SOFTWARE);
         if(!pRenderer) {
            SDL_DestroyWindow(pWindow);
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
            throw std::runtime_error(SDL_GetError());
         }
         Draw(pRenderer);
         bool boFinished{};
         while(!boFinished) {
            SDL_Event aEvent;
            if(!SDL_WaitEvent(&aEvent))break;
            if(aEvent.type==SDL_QUIT)boFinished=true;
            else if(aEvent.type==SDL_KEYDOWN) {
               switch(aEvent.key.keysym.sym) {
                  case SDLK_ESCAPE:boFinished=true;break;
                  case SDLK_RETURN:
                  case SDLK_KP_ENTER:Accept();boFinished=true;break;
                  case SDLK_UP:m_iSelected=(m_iSelected+ciCount-1)%ciCount;break;
                  case SDLK_DOWN:m_iSelected=(m_iSelected+1)%ciCount;break;
                  case SDLK_LEFT:Cycle(m_iSelected,-1);break;
                  case SDLK_RIGHT:Cycle(m_iSelected,1);break;
                  default:break;
               }
            }
            else if(aEvent.type==SDL_MOUSEBUTTONDOWN &&
               aEvent.button.button==SDL_BUTTON_LEFT) {
               int const iX=aEvent.button.x;
               int const iY=aEvent.button.y;
               if(iY>=569 && iY<623) {
                  if(iX>=594 && iX<762) {
                     Accept();
                     boFinished=true;
                  }
                  else if(iX>=772 && iX<892)boFinished=true;
               }
               else if(iX>=28 && iX<892 && iY>=ciTop &&
                  iY<ciTop+ciCount*ciRowHeight) {
                  int const iRow=(iY-ciTop)/ciRowHeight;
                  if(iRow>=0 && iRow<ciCount &&
                     iY<ciTop+iRow*ciRowHeight+55) {
                     m_iSelected=iRow;
                     if(iX>=410 && iX<858)
                        Cycle(iRow,iX<460?-1:1);
                  }
               }
            }
            if(!boFinished)Draw(pRenderer);
         }
         SDL_DestroyRenderer(pRenderer);
         SDL_DestroyWindow(pWindow);
         SDL_QuitSubSystem(SDL_INIT_VIDEO);
         if(m_boAccepted)aSettings=m_aSettings;
         return m_boAccepted;
      }
   };
}
