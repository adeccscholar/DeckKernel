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
   bool m_bRunning{true};
   bool m_bFlip{};
   std::string m_strName{"--"};
   std::string m_strRules{"--"};
   std::string m_strMana{"--"};
   std::string m_strStatus{"Waiting for card"};
   std::string m_strIdentity{"No verified identity"};
   std::string m_strOrientation{"Automatic orientation pending"};
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
   void Image(SDL_Texture* pTexture,SDL_Rect aArea,bool rotated=false) {
      if(!pTexture)return;
      int iW{},iH{};
      SDL_QueryTexture(pTexture,nullptr,nullptr,&iW,&iH);
      if(iW<=0||iH<=0)return;
      double const flScale=std::min(
         static_cast<double>(aArea.w)/iW,static_cast<double>(aArea.h)/iH);
      SDL_Rect const aDest{aArea.x+(aArea.w-static_cast<int>(iW*flScale))/2,
         aArea.y+(aArea.h-static_cast<int>(iH*flScale))/2,
         static_cast<int>(iW*flScale),static_cast<int>(iH*flScale)};
      SDL_RenderCopyEx(m_pRenderer,pTexture,nullptr,&aDest,
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
         }
      }
      return m_bRunning;
   }
   [[nodiscard]] bool Flip180() const noexcept{return m_bFlip;}
   void SetFields(std::string strName,std::string strRules,
      std::string strMana,std::string strStatus,
      std::string strIdentity="No verified identity",
      std::string strOrientation="Automatic orientation pending") {
      m_strName=std::move(strName);
      m_strRules=std::move(strRules);
      m_strMana=std::move(strMana);
      m_strStatus=std::move(strStatus);
      m_strIdentity=std::move(strIdentity);
      m_strOrientation=std::move(strOrientation);
   }
   void Show(cv::Mat const& aRaw,cv::Mat const& aCard,
      int iCamera,double flFps,double flBrightness) {
      Upload(m_pRaw,aRaw);
      Upload(m_pCard,aCard);
      int iWidth{},iHeight{};
      SDL_GetRendererOutputSize(m_pRenderer,&iWidth,&iHeight);
      if(iWidth<300||iHeight<300)return;
      auto const aSurface=SkSurfaces::Raster(SkImageInfo::MakeN32Premul(iWidth,iHeight));
      if(!aSurface)return;
      SkCanvas* pCanvas=aSurface->getCanvas();
      pCanvas->clear(0xff0a141d);
      Fill(pCanvas,SkRect::MakeXYWH(16,55,(iWidth-48)*0.56f,iHeight*0.57f),0xff142633);
      Fill(pCanvas,SkRect::MakeXYWH(32+(iWidth-48)*0.56f,55,
         (iWidth-48)*0.44f,iHeight*0.57f),0xff142633);
      float const flBottom=75.f+iHeight*0.57f;
      Fill(pCanvas,SkRect::MakeXYWH(16,flBottom,iWidth-32,
         iHeight-flBottom-16),0xff172936);
      Text(pCanvas,25,36,"DECKKERNEL  /  CAMERA RECOGNITION",24,0xfff5f7fa);
      Text(pCanvas,32,84,"LIVE CAMERA",17,0xff8ed5ee);
      Text(pCanvas,48+(iWidth-48)*0.56f,84,"RECTIFIED / ROTATED CARD",17,0xff8ed5ee);
      Text(pCanvas,32,flBottom+28,"CARD INFORMATION / OCR (UNVERIFIED)",19,0xfff5f7fa);
      Text(pCanvas,32,flBottom+55,"Name: "+m_strName,17,0xfff5f7fa);
      Text(pCanvas,32,flBottom+81,"Mana: "+m_strMana,15,0xffc9d9e2);
      // Wrap card text within the lower pane instead of truncating its first
      // line or allowing it to flow beyond the window.
      std::string strRemaining=m_strRules;
      float flLine=flBottom+108;
      int const maxChars=std::max(30,(iWidth-90)/9);
      for(int line=0;line<3 && !strRemaining.empty();++line) {
         auto const count=std::min(strRemaining.size(),
            static_cast<std::size_t>(maxChars));
         auto split=count;
         if(count<strRemaining.size()) {
            auto space=strRemaining.rfind(' ',count);
            if(space!=std::string::npos && space>count/2)
               split=space;
         }
         auto chunk=strRemaining.substr(0,split);
         if(line==0)chunk="Text: "+chunk;
         Text(pCanvas,32,flLine,chunk,15,0xffc9d9e2);
         flLine+=22;
         strRemaining.erase(0,split);
         while(!strRemaining.empty() && strRemaining.front()==' ')
            strRemaining.erase(0,1);
      }
      Text(pCanvas,32,flBottom+179,"Recognition: "+m_strStatus,16,0xffffcc79);
      Text(pCanvas,32,flBottom+204,"Identity: "+m_strIdentity,15,0xff9dddb9);
      Text(pCanvas,32,flBottom+229,"Orientation: "+m_strOrientation,15,0xffc9d9e2);
      Text(pCanvas,32,flBottom+257,
         std::format("Camera {} | {:.1f} fps | brightness {:.0f} | R: 180 deg | Esc: exit",
            iCamera,flFps,flBrightness),14,0xff8fa5b2);
      SkPixmap aPixels;
      if(aSurface->peekPixels(&aPixels)) {
         SDL_Texture* pBackground=SDL_CreateTexture(m_pRenderer,
            SDL_PIXELFORMAT_BGRA32,SDL_TEXTUREACCESS_STREAMING,iWidth,iHeight);
         if(pBackground) {
            SDL_UpdateTexture(pBackground,nullptr,aPixels.addr(),
               static_cast<int>(aPixels.rowBytes()));
            SDL_RenderCopy(m_pRenderer,pBackground,nullptr,nullptr);
            SDL_DestroyTexture(pBackground);
         }
      }
      Image(m_pRaw,SDL_Rect{30,98,
         static_cast<int>((iWidth-48)*0.56f)-28,static_cast<int>(iHeight*0.57f)-54},m_bFlip);
      Image(m_pCard,SDL_Rect{
         static_cast<int>(46+(iWidth-48)*0.56f),98,
         static_cast<int>((iWidth-48)*0.44f)-30,static_cast<int>(iHeight*0.57f)-54});
      SDL_RenderPresent(m_pRenderer);
   }
};
