// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file CameraSplitWest.h
\brief Separate conservative layout detection for classic west-facing split cards.
\details The physical outer card remains upright. Both printed face tops
   point west; rotating the entire card 90 degrees clockwise yields two
   portrait-oriented faces arranged left and right. Detection requires
   independent title and type/frame evidence for each face. It does not
   change the validated standard-card geometry or artwork matching.
*/
#include "CameraFrameGeometry.h"
#include "CameraCardLayout.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <optional>
#include <utility>

namespace deckkernel::camera {
   struct SplitWestLayout {
      cv::Mat aAligned;
      std::array<cv::Rect,2> arrFaces{};
      std::array<cv::Rect,2> arrArtwork{};
      std::array<cv::Rect,2> arrTitles{};
      cv::Rect aSharedTitle{};
      bool boSharedTitle{};
   };

   /**
   \brief Recognize two independently framed west-facing faces.
   \details Rejects a standard portrait card and does not reclassify
      borderless or ambiguous cards without structural evidence. The
      artwork rectangles are derived from the detected title/type bands,
      not absolute card-height percentages.
   \param aCard Full perspective-corrected physical card in portrait form.
   \returns Aligned split card and face/artwork bounds, or no classification.
   \throw cv::Exception If OpenCV image operations fail.
   */
   [[nodiscard]] inline std::optional<SplitWestLayout> DetectSplitWest(
      cv::Mat const& aCard) {
      if(aCard.empty() || aCard.cols<180 || aCard.rows<280 ||
         aCard.rows<=aCard.cols)return std::nullopt;

      SplitWestLayout aResult;
      // This rotation is local to the two printed faces. The physical
      // portrait image is never rotated in the acquisition/tracker state.
      cv::rotate(aCard,aResult.aAligned,cv::ROTATE_90_CLOCKWISE);
      int const iWidth=aResult.aAligned.cols;
      int const iHeight=aResult.aAligned.rows;
      int const iMid=iWidth/2;
      if(iMid<150 || iHeight<180)return std::nullopt;
      int const iSideMargin=std::max(2,iWidth/150);
      aResult.arrFaces={{
         cv::Rect(iSideMargin,0,iMid-2*iSideMargin,iHeight),
         cv::Rect(iMid+iSideMargin,0,
            iWidth-iMid-2*iSideMargin,iHeight)
      }};

      // Detect the actual printed horizontal bars. No fixed percentage
      // window is used for the title or the illustration boundaries:
      // the first independent line is the outer header boundary, the
      // second is the title/art line, and the next *paired* distant line
      // delimits the artwork. The two independent halves corroborate it.
      static constexpr int ciSampleWidth=240;
      static constexpr int ciSampleHeight=336;
      std::array<std::array<double,ciSampleHeight>,2> arrCoverage{};
      std::array<int,2> arrOuterTop{},arrTitleBottom{};
      for(int iFace=0;iFace<2;++iFace) {
         cv::Mat const aFace=aResult.aAligned(aResult.arrFaces[iFace]);
         cv::Mat aSmall,aGray,aGradient;
         cv::resize(aFace,aSmall,cv::Size(ciSampleWidth,ciSampleHeight),
            0.0,0.0,cv::INTER_AREA);
         if(aSmall.channels()==4)
            cv::cvtColor(aSmall,aGray,cv::COLOR_BGRA2GRAY);
         else if(aSmall.channels()==3)
            cv::cvtColor(aSmall,aGray,cv::COLOR_BGR2GRAY);
         else
            aGray=aSmall;
         cv::Sobel(aGray,aGradient,CV_16S,0,1,3);
         for(int iY=0;iY<ciSampleHeight;++iY) {
            short const* pRow=aGradient.ptr<short>(iY);
            unsigned uEdges{};
            for(int iX=ciSampleWidth/10;
               iX<ciSampleWidth-ciSampleWidth/10;++iX)
               uEdges+=std::abs(static_cast<int>(pRow[iX]))>=85;
            arrCoverage[iFace][iY]=static_cast<double>(uEdges)/
               (ciSampleWidth-2*(ciSampleWidth/10));
         }
         int iLastBand=-ciSampleWidth;
         std::array<int,2> arrFirstTwo{-1,-1};
         int iBands{};
         for(int iY=ciSampleWidth/30;
            iY<ciSampleHeight-ciSampleWidth/30;++iY) {
            if(arrCoverage[iFace][iY]<0.65 ||
               iY-iLastBand<ciSampleWidth/12)continue;
            arrFirstTwo[iBands++]=iY;
            iLastBand=iY;
            if(iBands==2)break;
         }
         if(iBands!=2)return std::nullopt;
         arrOuterTop[iFace]=arrFirstTwo[0];
         arrTitleBottom[iFace]=arrFirstTwo[1];
      }
      if(std::abs(arrTitleBottom[0]-arrTitleBottom[1])>
         ciSampleWidth/15)return std::nullopt;

      int iSharedBottom=-1;
      int const iStart=std::max(arrTitleBottom[0],arrTitleBottom[1])+
         ciSampleWidth/2;
      for(int iY=iStart;iY<ciSampleHeight-2;++iY) {
         std::array<double,2> arrStrength{};
         for(int iFace=0;iFace<2;++iFace)
            for(int iOffset=-1;iOffset<=1;++iOffset)
               arrStrength[iFace]=std::max(arrStrength[iFace],
                  arrCoverage[iFace][iY+iOffset]);
         if(arrStrength[0]+arrStrength[1]>=0.95 &&
            std::max(arrStrength[0],arrStrength[1])>=0.65 &&
            std::min(arrStrength[0],arrStrength[1])>=0.25) {
            iSharedBottom=iY;
            break;
         }
      }
      if(iSharedBottom<0 ||
         iSharedBottom-iStart>ciSampleWidth/3)return std::nullopt;

      // Independently measured vertical separation between two faces.
      // Its evidence is sampled within the observed artwork height.
      cv::Mat aGray,aGradient;
      if(aResult.aAligned.channels()==4)
         cv::cvtColor(aResult.aAligned,aGray,cv::COLOR_BGRA2GRAY);
      else if(aResult.aAligned.channels()==3)
         cv::cvtColor(aResult.aAligned,aGray,cv::COLOR_BGR2GRAY);
      else
         aGray=aResult.aAligned;
      double const flScale=static_cast<double>(ciSampleHeight)/iHeight;
      cv::Mat aSmall;
      cv::resize(aGray,aSmall,cv::Size(),flScale,flScale,
         cv::INTER_AREA);
      cv::Sobel(aSmall,aGradient,CV_16S,1,0,3);
      int const iCenter=aGradient.cols/2;
      int const iHalfWidth=std::max(2,aGradient.cols/50);
      int const iArtStart=std::max(arrTitleBottom[0],
         arrTitleBottom[1]);
      double flDivider{};
      for(int iX=std::max(0,iCenter-iHalfWidth);
         iX<std::min(aGradient.cols,iCenter+iHalfWidth);++iX) {
         unsigned uEdges{};
         for(int iY=iArtStart;iY<iSharedBottom;++iY)
            uEdges+=std::abs(
               static_cast<int>(aGradient.at<short>(iY,iX)))>=85;
         flDivider=std::max(flDivider,
            static_cast<double>(uEdges)/
               std::max(1,iSharedBottom-iArtStart));
      }
      // Some Room cards have a softly printed central artwork divider.
      // The paired title/art lines already establish both face layouts.
      if(flDivider<0.11)return std::nullopt;

      // A single title across both faces has no comparable central divider
      // in its title band. Detect that separately from the artwork divider.
      double flTitleDivider{};
      for(int iX=std::max(0,iCenter-iHalfWidth);
         iX<std::min(aGradient.cols,iCenter+iHalfWidth);++iX) {
         unsigned uEdges{};
         int const iTop=std::max(arrOuterTop[0],arrOuterTop[1]);
         int const iBottom=std::min(arrTitleBottom[0],arrTitleBottom[1]);
         for(int iY=iTop;iY<iBottom;++iY)
            uEdges+=std::abs(
               static_cast<int>(aGradient.at<short>(iY,iX)))>=85;
         flTitleDivider=std::max(flTitleDivider,
            static_cast<double>(uEdges)/std::max(1,iBottom-iTop));
      }
      aResult.boSharedTitle=flTitleDivider<flDivider*0.6;
      for(int iFace=0;iFace<2;++iFace) {
         cv::Rect const& aFace=aResult.arrFaces[iFace];
         int const iOuter=arrOuterTop[iFace]*aFace.height/ciSampleHeight;
         int const iTitle=arrTitleBottom[iFace]*aFace.height/ciSampleHeight;
         int const iBottom=iSharedBottom*aFace.height/ciSampleHeight;
         int const iInset=std::max(3,aFace.width/35);
         int const iLineInset=std::max(2,aFace.height/90);
         int const iTop=iTitle+iLineInset;
         int const iArtBottom=iBottom-iLineInset;
         if(iArtBottom<=iTop || iTitle<=iOuter ||
            iArtBottom-iTop<aFace.width/2 ||
            aFace.width<=2*iInset)return std::nullopt;
         aResult.arrArtwork[iFace]=cv::Rect(
            aFace.x+iInset,iTop,
            aFace.width-2*iInset,iArtBottom-iTop);
         aResult.arrTitles[iFace]=cv::Rect(
            aFace.x+iInset,iOuter+iLineInset,
            aFace.width-2*iInset,
            iTitle-iOuter-2*iLineInset);
         if(aResult.arrTitles[iFace].height<=0)
            return std::nullopt;
      }
      if(aResult.boSharedTitle) {
         int const iTop=std::max(aResult.arrTitles[0].y,
            aResult.arrTitles[1].y);
         int const iBottom=std::min(
            aResult.arrTitles[0].br().y,
            aResult.arrTitles[1].br().y);
         if(iBottom<=iTop)return std::nullopt;
         aResult.aSharedTitle=cv::Rect(
            aResult.arrTitles[0].x,iTop,
            aResult.arrTitles[1].br().x-aResult.arrTitles[0].x,
            iBottom-iTop);
      }
      return aResult;
   }

}
