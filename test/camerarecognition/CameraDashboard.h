// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/** \file CameraDashboard.h
    \brief SDL2/Skia graphical diagnostic dashboard.
    \details SDL owns window/events/textures, Skia renders all chrome and text.
             Image input and recognition state are supplied by the camera pipeline.
*/
#include <SDL.h>
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMgr.h"
#include "include/core/SkTypeface.h"
#include "include/core/SkFontStyle.h"
#include "include/ports/SkTypeface_win.h"
#include "CameraTypography.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <format>
#include <memory>
#include <vector>
#include <stdexcept>
#include <string>
#include <string_view>

class CameraDashboard {
   SDL_Window* m_pWindow{};
   SDL_Renderer* m_pRenderer{};
   SDL_Texture* m_pRaw{};
   SDL_Texture* m_pCard{};
   SDL_Texture* m_pReference{};
   bool m_bRunning{true};
   bool m_bFlip{};
   float m_cardZoom{1.0f};
   std::string m_qualityStatus{"Waiting for card"};
   std::string m_strName{"--"};
   std::string m_strType{"--"};
   std::string m_strRules{"--"};
   std::string m_strMana{"--"};
   std::string m_strStatus{"Waiting for card"};
   std::string m_strIdentity{"No verified identity"};
   std::string m_strOrientation{"Automatic orientation pending"};
   std::string m_strGuidance{"Place a card in the camera"};
   std::string m_strReferenceLabel{"No database match yet"};
   std::string m_strReferenceDiagnostic;
   std::string m_strSet, m_strSetName, m_strCollector, m_strLanguage;
   std::string m_strRarity, m_strPrintingId, m_strOracleId, m_strManaCost;
   bool m_bCardLocked{};
   static sk_sp<SkTypeface>& Typeface() {
      static sk_sp<SkTypeface> face=[] {
         auto manager=SkFontMgr_New_DirectWrite();
         if(manager) return manager->matchFamilyStyle("Segoe UI",SkFontStyle());
         return sk_sp<SkTypeface>{};
      }();
      return face;
   }
   static void Text(SkCanvas* canvas,float x,float y,
      std::string const& value,float size,SkColor color) {
      if(value.empty())return;
      if(!Typeface()) {
         deckkernel_camera::typography::Draw(canvas,x,y,value,size,color);
         return;
      }
      SkPaint paint;
      paint.setAntiAlias(true);
      paint.setColor(color);
      SkFont font(Typeface(),size);
      font.setEdging(SkFont::Edging::kSubpixelAntiAlias);
      canvas->drawSimpleText(value.data(),value.size(),SkTextEncoding::kUTF8,
         x,y,font,paint);
   }
   static void Fill(SkCanvas* pCanvas,SkRect const& aRect,SkColor uColor) {
      SkPaint aPaint;
      aPaint.setColor(uColor);
      aPaint.setAntiAlias(true);
      pCanvas->drawRoundRect(aRect,10,10,aPaint);
   }
   void Upload(SDL_Texture*& pTexture,cv::Mat const& aImage) {
      if(aImage.empty()) {
         if(pTexture){SDL_DestroyTexture(pTexture);pTexture=nullptr;}
         return;
      }
      cv::Mat aRgba;
      if(aImage.channels()==3)cv::cvtColor(aImage,aRgba,cv::COLOR_BGR2RGBA);
      else if(aImage.channels()==4)cv::cvtColor(aImage,aRgba,cv::COLOR_BGRA2RGBA);
      else cv::cvtColor(aImage,aRgba,cv::COLOR_GRAY2RGBA);
      int iW{},iH{};
      if(pTexture)SDL_QueryTexture(pTexture,nullptr,nullptr,&iW,&iH);
      if(!pTexture||iW!=aRgba.cols||iH!=aRgba.rows) {
         if(pTexture)SDL_DestroyTexture(pTexture);
         pTexture=SDL_CreateTexture(m_pRenderer,SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_STREAMING,aRgba.cols,aRgba.rows);
      }
      if(pTexture)SDL_UpdateTexture(pTexture,nullptr,aRgba.data,
         static_cast<int>(aRgba.step));
   }
   void Image(SDL_Texture* pTexture,SDL_Rect aArea,bool rotated=false,
      float zoom=1.0f) {
      if(!pTexture)return;
      int iW{},iH{};
      SDL_QueryTexture(pTexture,nullptr,nullptr,&iW,&iH);
      if(iW<=0||iH<=0)return;
      double const flScale=std::min(
         static_cast<double>(aArea.w)/iW,static_cast<double>(aArea.h)/iH);
      SDL_Rect const aDest{aArea.x+(aArea.w-static_cast<int>(iW*flScale))/2,
         aArea.y+(aArea.h-static_cast<int>(iH*flScale))/2,
         static_cast<int>(iW*flScale),static_cast<int>(iH*flScale)};
      SDL_Rect source{0,0,iW,iH};
      if(zoom>1.01f) {
         source.w=std::max(1,static_cast<int>(iW/zoom));
         source.h=std::max(1,static_cast<int>(iH/zoom));
         source.x=(iW-source.w)/2;
         source.y=(iH-source.h)/2;
      }
      SDL_RenderCopyEx(m_pRenderer,pTexture,&source,&aDest,
         rotated?180.0:0.0,nullptr,SDL_FLIP_NONE);
   }
public:
   CameraDashboard() {
      SDL_SetMainReady();
      if(SDL_InitSubSystem(SDL_INIT_VIDEO|SDL_INIT_EVENTS)!=0)
         throw std::runtime_error(SDL_GetError());
      m_pWindow=SDL_CreateWindow("DeckKernel - Card Recognition",
         SDL_WINDOWPOS_CENTERED,SDL_WINDOWPOS_CENTERED,1440,900,
         SDL_WINDOW_RESIZABLE|SDL_WINDOW_ALLOW_HIGHDPI);
      if(!m_pWindow)throw std::runtime_error(SDL_GetError());
      m_pRenderer=SDL_CreateRenderer(m_pWindow,-1,SDL_RENDERER_SOFTWARE);
      if(!m_pRenderer)throw std::runtime_error(SDL_GetError());
   }
   ~CameraDashboard() {
      if(m_pRaw)SDL_DestroyTexture(m_pRaw);
      if(m_pCard)SDL_DestroyTexture(m_pCard);
      if(m_pReference)SDL_DestroyTexture(m_pReference);
      if(m_pRenderer)SDL_DestroyRenderer(m_pRenderer);
      if(m_pWindow)SDL_DestroyWindow(m_pWindow);
      SDL_QuitSubSystem(SDL_INIT_VIDEO);
   }
   CameraDashboard(CameraDashboard const&)=delete;
   CameraDashboard& operator=(CameraDashboard const&)=delete;
   [[nodiscard]] bool Messages() {
      SDL_Event aEvent;
      while(SDL_PollEvent(&aEvent)) {
         if(aEvent.type==SDL_QUIT)m_bRunning=false;
         if(aEvent.type==SDL_KEYDOWN) {
            if(aEvent.key.keysym.sym==SDLK_ESCAPE)m_bRunning=false;
            if(aEvent.key.keysym.sym==SDLK_r)m_bFlip=!m_bFlip;
            if(aEvent.key.keysym.sym==SDLK_PLUS ||
               aEvent.key.keysym.sym==SDLK_EQUALS ||
               aEvent.key.keysym.sym==SDLK_KP_PLUS)
               m_cardZoom=std::min(4.0f,m_cardZoom+0.5f);
            if(aEvent.key.keysym.sym==SDLK_MINUS ||
               aEvent.key.keysym.sym==SDLK_KP_MINUS)
               m_cardZoom=std::max(1.0f,m_cardZoom-0.5f);
         }
      }
      return m_bRunning;
   }
   [[nodiscard]] bool Flip180() const noexcept{return m_bFlip;}
   /**
   \brief Update the read-only DB recognition fields.
   \details No OCR field is required for image-based identification.
   \param strName Card face name from the DB candidate.
   \param strType Type line from the DB candidate.
   \param strRules Optional diagnostic text.
   \param strMana Optional OCR/mana diagnostic.
   \param strStatus Current recognition stage or result.
   \param strIdentity Candidate printing summary.
   \param strOrientation Capture rotation diagnostic.
   \param strQuality Capture lock / input quality summary.
   \param boCardLocked Whether an input card is frozen.
   \param strGuidance Next interaction.
   \param strReferenceLabel Source variant for right-hand image.
   \param strReferenceDiagnostic Image load/integrity result.
   \param strSet Printed set code.
   \param strSetName Printed set name.
   \param strCollector Collector number.
   \param strLanguage Printed language.
   \param strRarity Rarity.
   \param strPrintingId Scryfall printing UUID.
   \param strOracleId Oracle identity UUID.
   \param strManaCost Raw Scryfall mana cost text.
   \returns Nothing.
   \throw std::bad_alloc On allocation failure.
   */
   void SetFields(std::string strName,std::string strType,std::string strRules,
      std::string strMana,std::string strStatus,
      std::string strIdentity="No verified identity",
      std::string strOrientation="Automatic orientation pending",
      std::string strQuality="Waiting for card",bool boCardLocked=false,
      std::string strGuidance="Place a card in the camera",
      std::string strReferenceLabel="No database match yet",
      std::string strReferenceDiagnostic={},
      std::string strSet={},std::string strSetName={},
      std::string strCollector={},std::string strLanguage={},
      std::string strRarity={},std::string strPrintingId={},
      std::string strOracleId={},std::string strManaCost={}) {
      m_strName=std::move(strName);
      m_strType=std::move(strType);
      m_strRules=std::move(strRules);
      m_strMana=std::move(strMana);
      m_strStatus=std::move(strStatus);
      m_strIdentity=std::move(strIdentity);
      m_strOrientation=std::move(strOrientation);
      m_qualityStatus=std::move(strQuality);
      m_bCardLocked=boCardLocked;
      m_strGuidance=std::move(strGuidance);
      m_strReferenceLabel=std::move(strReferenceLabel);
      m_strReferenceDiagnostic=std::move(strReferenceDiagnostic);
      m_strSet=std::move(strSet);
      m_strSetName=std::move(strSetName);
      m_strCollector=std::move(strCollector);
      m_strLanguage=std::move(strLanguage);
      m_strRarity=std::move(strRarity);
      m_strPrintingId=std::move(strPrintingId);
      m_strOracleId=std::move(strOracleId);
      m_strManaCost=std::move(strManaCost);
   }

   /**
   \brief Show input capture beside the DB reference card.
   \details Before capture the left pane shows the live video. After capture
   the two large panels are stable and the reference is only replaced
   when a new match result is published.
   \param aRaw Live camera frame.
   \param aCard Frozen rectified input card.
   \param aReference Decoded original image asset from PostgreSQL.
   \param iCamera Camera index.
   \param flFps Measured framerate.
   \param flBrightness Camera brightness diagnostic.
   \returns Nothing.
   \throw None.
   */
   void Show(cv::Mat const& aRaw,cv::Mat const& aCard,
      cv::Mat const& aReference,int iCamera,double flFps,
      double flBrightness) {
      // Never upload the live image to the captured-card panel.
      if(m_bCardLocked)Upload(m_pCard,aCard);
      else Upload(m_pCard,aRaw);
      Upload(m_pReference,aReference);
      int iWidth{},iHeight{};
      SDL_GetRendererOutputSize(m_pRenderer,&iWidth,&iHeight);
      if(iWidth<400 || iHeight<400)return;
      auto const aSurface=SkSurfaces::Raster(
         SkImageInfo::MakeN32Premul(iWidth,iHeight));
      if(!aSurface)return;
      SkCanvas* pCanvas=aSurface->getCanvas();
      pCanvas->clear(0xff0a141d);
      float const flGap=18.f;
      float const flPaneWidth=(iWidth-48.f-flGap)/2.f;
      float const flPaneTop=58.f;
      float const flPaneHeight=std::max(190.f,iHeight*0.62f);
      float const flRight=16.f+flPaneWidth+flGap;
      float const flBottom=flPaneTop+flPaneHeight+16.f;
      float const flFooterHeight=std::max(80.f,iHeight-flBottom-14.f);
      Fill(pCanvas,SkRect::MakeXYWH(16.f,flPaneTop,
         flPaneWidth,flPaneHeight),0xff142633);
      Fill(pCanvas,SkRect::MakeXYWH(flRight,flPaneTop,
         flPaneWidth,flPaneHeight),0xff142633);
      Fill(pCanvas,SkRect::MakeXYWH(16.f,flBottom,
         iWidth-32.f,flFooterHeight),0xff172936);
      Text(pCanvas,25.f,35.f,"DECKKERNEL  /  IMAGE RECOGNITION",23.f,0xfff5f7fa);
      Text(pCanvas,30.f,84.f,
         m_bCardLocked?"INPUT / CAPTURED CARD":"INPUT / CAMERA",
         16.f,0xff8ed5ee);
      Text(pCanvas,flRight+14.f,84.f,"REFERENCE / DATABASE",16.f,0xff8ed5ee);
      auto const Clip=[&](std::string const& strValue,
         int iMaxPixels)->std::string {
         std::size_t const uMax=static_cast<std::size_t>(
            std::max(12,iMaxPixels/8));
         if(strValue.size()<=uMax)return strValue;
         return strValue.substr(0,uMax-3)+"...";
      };
      if(m_bCardLocked) {
         Text(pCanvas,30.f,flPaneTop+flPaneHeight-18.f,
            "Captured: stable until card removal",14.f,0xff9dddb9);
      }
      Text(pCanvas,flRight+14.f,flPaneTop+flPaneHeight-18.f,
         Clip(m_strReferenceLabel,static_cast<int>(flPaneWidth-26.f)),
         14.f,0xff9dddb9);
      int const iFrameX=static_cast<int>(flRight+12.f);
      int const iCardY=104;
      int const iCardWidth=std::max(1,static_cast<int>(flPaneWidth-24.f));
      int const iCardHeight=std::max(1,static_cast<int>(flPaneHeight-154.f));
      if(!m_pReference) {
         Text(pCanvas,flRight+26.f,flPaneTop+flPaneHeight/2.f,
            "No matching DB image available",15.f,0xffffcc79);
      }
      float flY=flBottom+27.f;
      Text(pCanvas,30.f,flY,
         Clip("Status: "+m_strStatus,iWidth-80),17.f,0xffffcc79);
      flY+=24.f;
      Text(pCanvas,30.f,flY,
         Clip("Card: "+m_strName+"  |  "+m_strType,iWidth-80),
         16.f,0xfff5f7fa);
      flY+=22.f;
      Text(pCanvas,30.f,flY,Clip(std::format(
         "Set: {} ({})  |  No. {}  |  {}  |  {}",
         m_strSetName,m_strSet,m_strCollector,m_strLanguage,m_strRarity),
         iWidth-80),15.f,0xffc9d9e2);
      flY+=20.f;
      Text(pCanvas,30.f,flY,Clip("Mana: "+m_strManaCost+
         "  |  Printing ID: "+m_strPrintingId,iWidth-80),
         14.f,0xffc9d9e2);
      flY+=20.f;
      Text(pCanvas,30.f,flY,Clip("Oracle ID: "+m_strOracleId+
         "  |  "+m_strReferenceDiagnostic,iWidth-80),
         13.f,0xff9dddb9);
      flY+=19.f;
      Text(pCanvas,30.f,flY,
         Clip("Next: "+m_strGuidance,iWidth-80),
         13.f,0xff8ed5ee);
      // Reuse the SDL background raster; actual card textures are rendered above it.
      SkPixmap aPixels;
      if(aSurface->peekPixels(&aPixels)) {
         SDL_Texture* pBackground=SDL_CreateTexture(m_pRenderer,
            SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,
            iWidth,iHeight);
         if(pBackground) {
            SDL_UpdateTexture(pBackground,nullptr,aPixels.addr(),
               static_cast<int>(aPixels.rowBytes()));
            SDL_RenderCopy(m_pRenderer,pBackground,nullptr,nullptr);
            SDL_DestroyTexture(pBackground);
         }
      }
      // The image panels deliberately occupy equal areas for visual comparison.
      Image(m_pCard,SDL_Rect{28,iCardY,iCardWidth,iCardHeight},
         false,m_cardZoom);
      Image(m_pReference,SDL_Rect{iFrameX,iCardY,iCardWidth,iCardHeight},
         false,m_cardZoom);
      SDL_RenderPresent(m_pRenderer);
   }

};
