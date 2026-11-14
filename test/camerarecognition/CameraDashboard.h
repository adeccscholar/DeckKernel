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
#include "CameraCardSession.h"
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
   SDL_Cursor* m_pHandCursor{};
   SDL_Cursor* m_pArrowCursor{};
   SDL_Texture* m_pBackground{};
   int m_iBackgroundWidth{};
   int m_iBackgroundHeight{};
   SDL_Texture* m_pRaw{};
   SDL_Texture* m_pCard{};
   SDL_Texture* m_pReference{};
   unsigned char const* m_pUploadedCardPixels{};
   unsigned char const* m_pUploadedReferencePixels{};
   cv::Size m_aUploadedCardSize{};
   cv::Size m_aUploadedReferenceSize{};
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
   int m_iNavigation{};
   bool m_boConfirm{};
   bool m_boReject{};
   bool m_boAllPrintings{};
   bool m_boOptions{};
   std::string m_strSettingsNotice;
   std::vector<deckkernel::camera::CollectedCard> m_vecCollected;
   bool m_boFocusReset{};
   bool m_boSaveUndetected{};
   bool m_boFocusToggle{};
   bool m_boTableAutoFocus{};
   std::string m_strPhase{"01 SEARCHING"};
   std::size_t m_uCandidateIndex{};
   std::size_t m_uCandidateCount{};
   bool m_boPrintingConfirmed{};
   enum class ButtonId {
      None,TableFocus,Options,Accept,Discard,Previous,Next,AllPrintings
   };
   ButtonId m_eHovered{ButtonId::None};
   ButtonId m_ePressed{ButtonId::None};

   /**
   \brief Resolve an enabled button from render-space mouse coordinates.
   \details Same rectangles are used by painting and hit testing.
   \param iX Horizontal renderer pixel coordinate.
   \param iY Vertical renderer pixel coordinate.
   \returns Enabled button or None.
   \throw None.
   */
   [[nodiscard]] ButtonId HitButton(int iX,int iY) const {
      int iWidth{},iHeight{};
      SDL_GetRendererOutputSize(m_pRenderer,&iWidth,&iHeight);
      auto const Inside=[&](float flX,float flY,float flWidth,float flHeight) {
         return iX>=flX && iX<flX+flWidth &&
            iY>=flY && iY<flY+flHeight;
      };
      if(Inside(iWidth-306.f,9.f,153.f,38.f))return ButtonId::TableFocus;
      if(Inside(iWidth-145.f,9.f,128.f,38.f))return ButtonId::Options;
      if(!m_bCardLocked)return ButtonId::None;
      float const flY=static_cast<float>(iHeight-77);
      if(Inside(28.f,flY,117.f,43.f))return ButtonId::Accept;
      if(Inside(153.f,flY,117.f,43.f))return ButtonId::Discard;
      float const flPaneWidth=(iWidth-32.f-36.f-
         std::max(230.f,iWidth*0.23f))/2.f;
      float const flFirst=16.f+flPaneWidth+18.f+12.f;
      float const flNavWidth=std::min(140.f,(flPaneWidth-44.f)/3.f);
      if(m_uCandidateCount>1) {
         if(Inside(flFirst,flY,flNavWidth,43.f))
            return ButtonId::Previous;
         if(Inside(flFirst+flNavWidth+8.f,flY,flNavWidth,43.f))
            return ButtonId::Next;
      }
      if(m_uCandidateCount>0 && !m_strOracleId.empty() &&
         Inside(flFirst+2.f*(flNavWidth+8.f),flY,flNavWidth,43.f))
         return ButtonId::AllPrintings;
      return ButtonId::None;
   }
   [[nodiscard]] ButtonId MouseButton(int iX,int iY) const {
      int iWidth{},iHeight{},iWindowWidth{},iWindowHeight{};
      SDL_GetRendererOutputSize(m_pRenderer,&iWidth,&iHeight);
      SDL_GetWindowSize(m_pWindow,&iWindowWidth,&iWindowHeight);
      return HitButton(iX*std::max(1,iWidth)/std::max(1,iWindowWidth),
         iY*std::max(1,iHeight)/std::max(1,iWindowHeight));
   }
   void SetHover(ButtonId eButton) {
      if(m_eHovered==eButton)return;
      m_eHovered=eButton;
      SDL_Cursor* pCursor=eButton==ButtonId::None?
         m_pArrowCursor:m_pHandCursor;
      if(pCursor)SDL_SetCursor(pCursor);
   }
   void Activate(ButtonId eButton) {
      switch(eButton) {
         case ButtonId::TableFocus:m_boFocusToggle=true;break;
         case ButtonId::Options:m_boOptions=true;break;
         case ButtonId::Accept:m_boConfirm=true;break;
         case ButtonId::Discard:m_boReject=true;break;
         case ButtonId::Previous:--m_iNavigation;break;
         case ButtonId::Next:++m_iNavigation;break;
         case ButtonId::AllPrintings:m_boAllPrintings=true;break;
         default:break;
      }
   }
   static SkColor Brightness(SkColor uColor,int iDelta) {
      auto const Adjust=[&](int iValue) -> U8CPU {
         return static_cast<U8CPU>(std::clamp(iValue+iDelta,0,255));
      };
      return SkColorSetARGB(SkColorGetA(uColor),
         Adjust(SkColorGetR(uColor)),Adjust(SkColorGetG(uColor)),
         Adjust(SkColorGetB(uColor)));
   }
   void DrawButton(SkCanvas* pCanvas,SkRect aRect,
      char const* szLabel,SkColor uColor,bool boEnabled,
      ButtonId eButton,float flSize=14.f) const {
      bool const boHover=boEnabled && m_eHovered==eButton;
      bool const boPressed=boEnabled && m_ePressed==eButton;
      if(boPressed)aRect.offset(0.f,2.f);
      SkColor const uFill=!boEnabled?0xff344753:
         Brightness(uColor,boPressed?-24:boHover?26:0);
      Fill(pCanvas,aRect,uFill);
      if(boHover || boPressed) {
         SkPaint aStroke;
         aStroke.setAntiAlias(true);
         aStroke.setStyle(SkPaint::kStroke_Style);
         aStroke.setStrokeWidth(boPressed?2.f:1.5f);
         aStroke.setColor(boPressed?0xffd9f9ff:0xff8ed5ee);
         pCanvas->drawRoundRect(aRect,10,10,aStroke);
      }
      Text(pCanvas,aRect.x()+11.f,aRect.y()+27.f,
         szLabel,flSize,boEnabled?0xfff5f7fa:0xff95a8b3);
   }
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
      SDL_SetWindowMinimumSize(m_pWindow,1200,760);
      m_pRenderer=SDL_CreateRenderer(m_pWindow,-1,SDL_RENDERER_SOFTWARE);
      m_pHandCursor=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_HAND);
      m_pArrowCursor=SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_ARROW);
      if(!m_pRenderer)throw std::runtime_error(SDL_GetError());
   }
   ~CameraDashboard() {
      if(m_pBackground)SDL_DestroyTexture(m_pBackground);
      if(m_pRaw)SDL_DestroyTexture(m_pRaw);
      if(m_pCard)SDL_DestroyTexture(m_pCard);
      if(m_pReference)SDL_DestroyTexture(m_pReference);
      if(m_pHandCursor)SDL_FreeCursor(m_pHandCursor);
      if(m_pArrowCursor)SDL_FreeCursor(m_pArrowCursor);
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
            if(aEvent.key.keysym.sym==SDLK_ESCAPE &&
               aEvent.key.repeat==0)m_boReject=true;
            if(aEvent.key.keysym.sym==SDLK_r)m_bFlip=!m_bFlip;
            if(aEvent.key.keysym.sym==SDLK_f)m_boFocusReset=true;
            if(aEvent.key.keysym.sym==SDLK_d &&
               aEvent.key.repeat==0)m_boSaveUndetected=true;
            if(aEvent.key.keysym.sym==SDLK_g &&
               aEvent.key.repeat==0)m_boFocusToggle=true;
            if(aEvent.key.keysym.sym==SDLK_o &&
               aEvent.key.repeat==0)m_boOptions=true;
            if(aEvent.key.keysym.sym==SDLK_p && m_bCardLocked)
               m_boAllPrintings=true;
            if(aEvent.key.keysym.sym==SDLK_LEFT)--m_iNavigation;
            if(aEvent.key.keysym.sym==SDLK_RIGHT)++m_iNavigation;
            if((aEvent.key.keysym.sym==SDLK_RETURN ||
               aEvent.key.keysym.sym==SDLK_KP_ENTER) &&
               aEvent.key.repeat==0)m_boConfirm=true;
            if(aEvent.key.keysym.sym==SDLK_PLUS ||
               aEvent.key.keysym.sym==SDLK_EQUALS ||
               aEvent.key.keysym.sym==SDLK_KP_PLUS)
               m_cardZoom=std::min(4.0f,m_cardZoom+0.5f);
            if(aEvent.key.keysym.sym==SDLK_MINUS ||
               aEvent.key.keysym.sym==SDLK_KP_MINUS)
               m_cardZoom=std::max(1.0f,m_cardZoom-0.5f);
         }
         if(aEvent.type==SDL_MOUSEWHEEL && m_bCardLocked &&
            aEvent.wheel.y!=0)
            m_iNavigation-=aEvent.wheel.y;
         if(aEvent.type==SDL_MOUSEMOTION)
            SetHover(MouseButton(aEvent.motion.x,aEvent.motion.y));
         if(aEvent.type==SDL_WINDOWEVENT &&
            aEvent.window.event==SDL_WINDOWEVENT_LEAVE) {
            SetHover(ButtonId::None);
            m_ePressed=ButtonId::None;
         }
         if(aEvent.type==SDL_MOUSEBUTTONDOWN &&
            aEvent.button.button==SDL_BUTTON_LEFT) {
            m_ePressed=MouseButton(aEvent.button.x,aEvent.button.y);
            SetHover(m_ePressed);
         }
         if(aEvent.type==SDL_MOUSEBUTTONUP &&
            aEvent.button.button==SDL_BUTTON_LEFT) {
            ButtonId const eReleased=MouseButton(
               aEvent.button.x,aEvent.button.y);
            if(m_ePressed!=ButtonId::None &&
               m_ePressed==eReleased)
               Activate(eReleased);
            m_ePressed=ButtonId::None;
            SetHover(eReleased);
         }
         if(aEvent.type==SDL_MOUSEBUTTONDOWN &&
            aEvent.button.button==SDL_BUTTON_RIGHT && m_bCardLocked)
            m_boReject=true;
      }
      return m_bRunning;
   }
   [[nodiscard]] bool Flip180() const noexcept{return m_bFlip;}
   [[nodiscard]] int ConsumeNavigation() noexcept {
      int const iDelta=m_iNavigation;
      m_iNavigation=0;
      return iDelta;
   }
   [[nodiscard]] bool ConsumeConfirmation() noexcept {
      bool const boConfirm=m_boConfirm;
      m_boConfirm=false;
      return boConfirm;
   }
   [[nodiscard]] bool ConsumeOptions() noexcept {
      bool const boOptions=m_boOptions;
      m_boOptions=false;
      return boOptions;
   }
   void SetSettingsNotice(std::string strNotice) {
      m_strSettingsNotice=std::move(strNotice);
   }
   [[nodiscard]] bool ConsumeAllPrintings() noexcept {
      bool const boRequested=m_boAllPrintings;
      m_boAllPrintings=false;
      return boRequested;
   }
   [[nodiscard]] bool ConsumeRejection() noexcept {
      bool const boReject=m_boReject;
      m_boReject=false;
      return boReject;
   }
   /**
   \brief Update the persistent list of explicitly accepted cards.
   \details The list is independent of transient input and DB reference
      images and remains visible after a scan is accepted or discarded.
   \param vecCards Cards already accepted during this application session.
   \returns Nothing.
   \throw std::bad_alloc If copying the list fails.
   */
   void SetCollectedCards(
      std::vector<deckkernel::camera::CollectedCard> const& vecCards) {
      // The session list is append-only: do not copy historical strings
      // on every one of the camera's ~20 video frames per second.
      if(m_vecCollected.size()!=vecCards.size())
         m_vecCollected=vecCards;
   }
   void SetTableAutoFocus(bool boEnabled) noexcept {
      m_boTableAutoFocus=boEnabled;
   }
   [[nodiscard]] bool ConsumeFocusToggle() noexcept {
      bool const boToggle=m_boFocusToggle;
      m_boFocusToggle=false;
      return boToggle;
   }
   /**
   \brief Consume a manual diagnostic snapshot request, even without a card.
   \details Pressing D saves the original full-resolution camera frame and
      classification statistics; no physical-card event is required.
   \returns True once per D key press.
   \throw None.
   */
   [[nodiscard]] bool ConsumeUndetectedSnapshot() noexcept {
      bool const boRequested=m_boSaveUndetected;
      m_boSaveUndetected=false;
      return boRequested;
   }
   [[nodiscard]] bool ConsumeFocusReset() noexcept {
      bool const boReset=m_boFocusReset;
      m_boFocusReset=false;
      return boReset;
   }
   void SetPhase(std::string strPhase) {m_strPhase=std::move(strPhase);}
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
      std::string strOracleId={},std::string strManaCost={},
      std::size_t uCandidateIndex=0,std::size_t uCandidateCount=0,
      bool boPrintingConfirmed=false) {
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
      m_uCandidateIndex=uCandidateIndex;
      m_uCandidateCount=uCandidateCount;
      m_boPrintingConfirmed=boPrintingConfirmed;
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
      // PHASE 05: the captured card and selected DB reference are immutable.
      // Reuse their SDL textures instead of converting the same RGB pixels
      // every camera frame. Live input still refreshes every frame.
      if(m_bCardLocked) {
         if(aCard.empty()) {
            Upload(m_pCard,aCard);
            m_pUploadedCardPixels=nullptr;
         }
         else if(aCard.data!=m_pUploadedCardPixels ||
            aCard.size()!=m_aUploadedCardSize) {
            Upload(m_pCard,aCard);
            m_pUploadedCardPixels=aCard.data;
            m_aUploadedCardSize=aCard.size();
         }
      }
      else {
         Upload(m_pCard,aRaw);
         m_pUploadedCardPixels=nullptr;
      }
      if(aReference.empty()) {
         Upload(m_pReference,aReference);
         m_pUploadedReferencePixels=nullptr;
      }
      else if(aReference.data!=m_pUploadedReferencePixels ||
         aReference.size()!=m_aUploadedReferenceSize) {
         Upload(m_pReference,aReference);
         m_pUploadedReferencePixels=aReference.data;
         m_aUploadedReferenceSize=aReference.size();
      }
      int iWidth{},iHeight{};
      SDL_GetRendererOutputSize(m_pRenderer,&iWidth,&iHeight);
      if(iWidth<400 || iHeight<400)return;
      auto const aSurface=SkSurfaces::Raster(
         SkImageInfo::MakeN32Premul(iWidth,iHeight));
      if(!aSurface)return;
      SkCanvas* pCanvas=aSurface->getCanvas();
      pCanvas->clear(0xff0a141d);
      float const flGap=18.f;
      float const flListWidth=std::max(230.f,iWidth*0.23f);
      float const flPaneWidth=
         (iWidth-32.f-2.f*flGap-flListWidth)/2.f;
      float const flPaneTop=58.f;
      // Reserve at least 290 pixels for the two-column metadata and
      // a separate fixed action bar. Status text must never enter buttons.
      float const flPaneHeight=std::max(190.f,
         std::min(iHeight*0.56f,iHeight-58.f-16.f-290.f-14.f));
      float const flRight=16.f+flPaneWidth+flGap;
      float const flListX=flRight+flPaneWidth+flGap;
      float const flBottom=flPaneTop+flPaneHeight+16.f;
      float const flFooterHeight=std::max(80.f,iHeight-flBottom-14.f);
      float const flDetailsWidth=flListX-16.f-flGap;
      Fill(pCanvas,SkRect::MakeXYWH(16.f,flPaneTop,
         flPaneWidth,flPaneHeight),0xff142633);
      Fill(pCanvas,SkRect::MakeXYWH(flRight,flPaneTop,
         flPaneWidth,flPaneHeight),0xff142633);
      Fill(pCanvas,SkRect::MakeXYWH(16.f,flBottom,
         flDetailsWidth,flFooterHeight),0xff172936);
      Fill(pCanvas,SkRect::MakeXYWH(flListX,flPaneTop,
         flListWidth,iHeight-flPaneTop-14.f),0xff142633);
      Text(pCanvas,25.f,35.f,"DECKKERNEL  /  IMAGE RECOGNITION",23.f,0xfff5f7fa);
      Text(pCanvas,static_cast<float>(iWidth)-465.f,35.f,
         m_strPhase.substr(0,18),15.f,0xff8ed5ee);
      DrawButton(pCanvas,SkRect::MakeXYWH(
         static_cast<float>(iWidth)-306.f,9.f,153.f,38.f),
         m_boTableAutoFocus?"Table AF: ON":"Table AF: OFF",
         m_boTableAutoFocus?0xff218a78:0xff344753,
         true,ButtonId::TableFocus);
      DrawButton(pCanvas,SkRect::MakeXYWH(
         static_cast<float>(iWidth)-145.f,9.f,128.f,38.f),
         "Options",0xff244454,true,ButtonId::Options,16.f);
      Text(pCanvas,30.f,84.f,
         m_bCardLocked?"INPUT / CAPTURED CARD":"INPUT / CAMERA",
         16.f,0xff8ed5ee);
      Text(pCanvas,flRight+14.f,84.f,"REFERENCE / DATABASE",16.f,0xff8ed5ee);
      Text(pCanvas,flListX+14.f,84.f,
         std::format("COLLECTED CARDS ({})",m_vecCollected.size()),
         15.f,0xff8ed5ee);
      auto const Clip=[&](std::string const& strValue,
         int iMaxPixels)->std::string {
         std::size_t const uMax=static_cast<std::size_t>(
            std::max(12,iMaxPixels/8));
         if(strValue.size()<=uMax)return strValue;
         return strValue.substr(0,uMax-3)+"...";
      };
      // Accepted cards are stable across subsequent scan cycles.
      // Display the newest entries if the column cannot fit every item.
      std::size_t const uVisible=static_cast<std::size_t>(
         std::max(1,static_cast<int>(
            (iHeight-flPaneTop-132.f)/29.f)));
      std::size_t const uStart=m_vecCollected.size()>uVisible?
         m_vecCollected.size()-uVisible:0;
      for(std::size_t uIndex=uStart;uIndex<m_vecCollected.size();
         ++uIndex) {
         auto const& aCard=m_vecCollected[uIndex];
         std::string const strSet=aCard.strSetName.empty()?
            aCard.strSet:aCard.strSetName;
         Text(pCanvas,flListX+14.f,
            flPaneTop+65.f+static_cast<float>(uIndex-uStart)*29.f,
            Clip(std::format("{}. {} ({})",uIndex+1,aCard.strName,
               strSet),static_cast<int>(flListWidth-28.f)),
            14.f,0xfff5f7fa);
      }
      Text(pCanvas,flListX+14.f,iHeight-34.f,
         "Enter: accept | Esc: discard | O: options",
         11.f,0xff9dddb9);
      if(!m_strSettingsNotice.empty())
         Text(pCanvas,flListX+14.f,iHeight-103.f,
            Clip(m_strSettingsNotice,
               static_cast<int>(flListWidth-24.f)),
            12.f,0xff9dddb9);
      if(m_bCardLocked) {
         Text(pCanvas,30.f,flPaneTop+flPaneHeight-18.f,
            "Captured: locked | "+m_strOrientation,14.f,0xff9dddb9);
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
      // DEMO PHASE 06: Oracle identity is shown on the left; the frozen
      // camera image above remains unchanged when printing candidates change.
      float const flLeftX=30.f;
      float const flRightX=flRight+14.f;
      int const iColumnWidth=static_cast<int>(flPaneWidth-30.f);
      Text(pCanvas,flLeftX,flBottom+28.f,
         "ORACLE CARD",16.f,0xff8ed5ee);
      Text(pCanvas,flLeftX,flBottom+58.f,
         Clip(m_strName,iColumnWidth),19.f,0xfff5f7fa);
      Text(pCanvas,flLeftX,flBottom+84.f,
         Clip("Type: "+m_strType,iColumnWidth),13.f,0xffc9d9e2);
      Text(pCanvas,flLeftX,flBottom+109.f,
         Clip("Oracle ID: "+m_strOracleId,iColumnWidth),12.f,0xff9dddb9);

      // DEMO PHASE 07/09/10: the right column always refers to the selected
      // printing, not to Oracle text or guessed properties of the camera image.
      std::string const strPrintingHeader=m_boPrintingConfirmed?
         "PRINTING / MANUALLY CONFIRMED":
         (m_uCandidateCount>1?
            std::format("PRINTING CANDIDATE {}/{}",
               m_uCandidateIndex+1,m_uCandidateCount):
            "PRINTING / IMAGE MATCH");
      Text(pCanvas,flRightX,flBottom+28.f,
         Clip(strPrintingHeader,iColumnWidth),16.f,0xff8ed5ee);
      Text(pCanvas,flRightX,flBottom+58.f,
         Clip(std::format("{} ({})  #{}",
            m_strSetName,m_strSet,m_strCollector),iColumnWidth),
         19.f,0xfff5f7fa);
      Text(pCanvas,flRightX,flBottom+84.f,
         Clip(std::format("{} | {} | Mana {}",
            m_strLanguage,m_strRarity,m_strManaCost),iColumnWidth),
         13.f,0xffc9d9e2);
      Text(pCanvas,flRightX,flBottom+109.f,
         Clip("Printing ID: "+m_strPrintingId,iColumnWidth),
         12.f,0xff9dddb9);

      Text(pCanvas,flLeftX,flBottom+146.f,
         Clip("Recognition: "+m_strStatus,
            static_cast<int>(flDetailsWidth-30.f)),
         15.f,0xffffcc79);
      Text(pCanvas,flLeftX,flBottom+171.f,
         Clip("Reference: "+m_strReferenceDiagnostic,
            static_cast<int>(flDetailsWidth-30.f)),
         13.f,0xff9dddb9);
      Text(pCanvas,flLeftX,std::min(flBottom+195.f,
         static_cast<float>(iHeight)-100.f),
         Clip("Next: "+m_strGuidance,
            static_cast<int>(flDetailsWidth-30.f)),
         13.f,0xff8ed5ee);
      // Explicit scan decisions and printing navigation. These controls
      // act only when a captured card is locked; the main loop validates
      // whether a candidate is ready before accepting it.
      // Always draw actions. Availability changes their appearance,
      // never their visibility or location on the dashboard.
      float const flActionsY=static_cast<float>(iHeight-77);
      bool const boCanBrowse=m_bCardLocked && m_uCandidateCount>1;
      bool const boCanList=m_bCardLocked && m_uCandidateCount>0 &&
         !m_strOracleId.empty();
      DrawButton(pCanvas,SkRect::MakeXYWH(
         28.f,flActionsY,117.f,43.f),
         "Accept",0xff218a78,m_bCardLocked,ButtonId::Accept,15.f);
      DrawButton(pCanvas,SkRect::MakeXYWH(
         153.f,flActionsY,117.f,43.f),
         "Discard",0xff85454a,m_bCardLocked,ButtonId::Discard,15.f);
      float const flNavWidth=std::min(140.f,(flPaneWidth-44.f)/3.f);
      float const flNavX=flRight+12.f;
      DrawButton(pCanvas,SkRect::MakeXYWH(
         flNavX,flActionsY,flNavWidth,43.f),
         "Previous",0xff244454,boCanBrowse,ButtonId::Previous);
      DrawButton(pCanvas,SkRect::MakeXYWH(
         flNavX+flNavWidth+8.f,flActionsY,flNavWidth,43.f),
         "Next",0xff244454,boCanBrowse,ButtonId::Next);
      DrawButton(pCanvas,SkRect::MakeXYWH(
         flNavX+2.f*(flNavWidth+8.f),flActionsY,
         flNavWidth,43.f),
         "All printings",0xff218a78,boCanList,
         ButtonId::AllPrintings,13.f);
      // Reuse the SDL background raster; actual card textures are rendered above it.
      SkPixmap aPixels;
      if(aSurface->peekPixels(&aPixels)) {
         if(!m_pBackground || m_iBackgroundWidth!=iWidth ||
            m_iBackgroundHeight!=iHeight) {
            if(m_pBackground)SDL_DestroyTexture(m_pBackground);
            m_pBackground=SDL_CreateTexture(m_pRenderer,
               SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,
               iWidth,iHeight);
            m_iBackgroundWidth=iWidth;
            m_iBackgroundHeight=iHeight;
         }
         if(m_pBackground) {
            SDL_UpdateTexture(m_pBackground,nullptr,aPixels.addr(),
               static_cast<int>(aPixels.rowBytes()));
            SDL_RenderCopy(m_pRenderer,m_pBackground,nullptr,nullptr);
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
