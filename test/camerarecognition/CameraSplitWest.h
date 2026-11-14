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

      // A horizontal artwork edge is not the type line. In the old code,
      // the unrestricted maximum from 38..79% often selected the RULES
      // box instead of the first art/type boundary, producing mixed text
      // and artwork crops. Require two distinct bands independently in
      // each face: title/art at 8..20%, artwork/type at 49..65%.
      struct Bands {
         int iTop{};
         int iBottom{};
         double flTop{};
         double flBottom{};
      };
      std::array<Bands,2> arrBands{};
      std::array<std::array<double,336>,2> arrCoverage{};
      for(int iFace=0;iFace<2;++iFace) {
         cv::Mat const aFace=aResult.aAligned(aResult.arrFaces[iFace]);
         cv::Mat aSmall,aGray,aGradient;
         cv::resize(aFace,aSmall,cv::Size(240,336),
            0.0,0.0,cv::INTER_AREA);
         if(aSmall.channels()==4)
            cv::cvtColor(aSmall,aGray,cv::COLOR_BGRA2GRAY);
         else if(aSmall.channels()==3)
            cv::cvtColor(aSmall,aGray,cv::COLOR_BGR2GRAY);
         else
            aGray=aSmall;
         cv::Sobel(aGray,aGradient,CV_16S,0,1,3);
         for(int iY=0;iY<336;++iY) {
            short const* pRow=aGradient.ptr<short>(iY);
            unsigned uCount{};
            for(int iX=24;iX<216;++iX)
               uCount+=std::abs(static_cast<int>(pRow[iX]))>=85;
            arrCoverage[iFace][iY]=
               static_cast<double>(uCount)/192.0;
         }
         for(int iY=27;iY<68;++iY)
            if(arrCoverage[iFace][iY]>arrBands[iFace].flTop) {
               arrBands[iFace].iTop=iY;
               arrBands[iFace].flTop=arrCoverage[iFace][iY];
            }
         if(arrBands[iFace].flTop<0.70)return std::nullopt;
      }
      if(std::abs(arrBands[0].iTop-arrBands[1].iTop)>15)
         return std::nullopt;

      // Room cards frequently have a dark/soft separator on one face.
      // The former 0.70 requirement per half discarded whole cards while
      // still accepting internal rectangles. Both faces MUST confirm the
      // same boundary, but the stronger side can corroborate a weaker one.
      int iSharedBottom=-1;
      for(int iY=165;iY<220;++iY) {
         std::array<double,2> arrStrength{};
         for(int iFace=0;iFace<2;++iFace)
            for(int iOffset=0;iOffset<3;++iOffset)
               arrStrength[iFace]=std::max(arrStrength[iFace],
                  arrCoverage[iFace][iY+iOffset]);
         if(arrStrength[0]+arrStrength[1]>=1.10 &&
            std::max(arrStrength[0],arrStrength[1])>=0.75 &&
            std::min(arrStrength[0],arrStrength[1])>=0.30) {
            iSharedBottom=iY+1;
            arrBands[0].flBottom=arrStrength[0];
            arrBands[1].flBottom=arrStrength[1];
            break;
         }
      }
      if(iSharedBottom<0)return std::nullopt;
      for(int iFace=0;iFace<2;++iFace) {
         arrBands[iFace].iBottom=iSharedBottom;
         int const iHeight=iSharedBottom-arrBands[iFace].iTop;
         if(iHeight<108 || iHeight>190)return std::nullopt;
      }

      // A conventional single-face card may also have continuous
      // horizontal title/type separators. Require independent evidence
      // of the vertical division into two printed faces.
      cv::Mat aGray,aGradient;
      if(aResult.aAligned.channels()==4)
         cv::cvtColor(aResult.aAligned,aGray,cv::COLOR_BGRA2GRAY);
      else if(aResult.aAligned.channels()==3)
         cv::cvtColor(aResult.aAligned,aGray,cv::COLOR_BGR2GRAY);
      else
         aGray=aResult.aAligned;
      cv::resize(aGray,aGray,cv::Size(672,480),0,0,cv::INTER_AREA);
      cv::Sobel(aGray,aGradient,CV_16S,1,0,3);
      double flDivider{};
      for(int iX=672*46/100;iX<672*54/100;++iX) {
         unsigned uCount{};
         for(int iY=480*15/100;iY<480*65/100;++iY)
            uCount+=std::abs(
               static_cast<int>(aGradient.at<short>(iY,iX)))>=85;
         flDivider=std::max(flDivider,
            static_cast<double>(uCount)/(480*50/100));
      }
      if(flDivider<0.16)return std::nullopt;

      for(int iFace=0;iFace<2;++iFace) {
         cv::Rect const& aFace=aResult.arrFaces[iFace];
         // Exclude the title/type bars and the outer/central card frame.
         // Both coordinates come from detected separator rows, never
         // arbitrary whole-card artwork percentages.
         int const iTop=
            arrBands[iFace].iTop*aFace.height/336+
               std::max(3,aFace.height/100);
         int const iBottom=
            arrBands[iFace].iBottom*aFace.height/336-
               std::max(3,aFace.height/100);
         int const iMargin=std::max(3,aFace.width*7/100);
         if(iBottom-iTop<aFace.height*30/100 ||
            iBottom-iTop>aFace.height*57/100 ||
            aFace.width<=2*iMargin)return std::nullopt;
         aResult.arrArtwork[iFace]=cv::Rect(
            aFace.x+iMargin,iTop,
            aFace.width-2*iMargin,iBottom-iTop);
      }
      return aResult;
   }

}
