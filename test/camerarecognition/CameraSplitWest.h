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
      // A reliably conventional frame always takes the existing standard path.
      if(InspectStandardCard(aCard).boRecognized)return std::nullopt;
      SplitWestLayout aResult;
      cv::rotate(aCard,aResult.aAligned,cv::ROTATE_90_CLOCKWISE);
      int const iWidth=aResult.aAligned.cols;
      int const iHeight=aResult.aAligned.rows;
      int const iMid=iWidth/2;
      int const iGap=std::max(3,iWidth/140);
      if(iMid<=iGap+80 || iHeight<180)return std::nullopt;
      aResult.arrFaces={{
         cv::Rect(iGap,0,iMid-2*iGap,iHeight),
         cv::Rect(iMid+iGap,0,iWidth-iMid-2*iGap,iHeight)
      }};
      // The type bar is not necessarily split into two separate boxes.
      // On several classic printings it spans the two face panels.
      // Measure one shared horizontal separator on the rotated source.
      cv::Mat aGray,aVertical;
      if(aResult.aAligned.channels()==4)
         cv::cvtColor(aResult.aAligned,aGray,cv::COLOR_BGRA2GRAY);
      else if(aResult.aAligned.channels()==3)
         cv::cvtColor(aResult.aAligned,aGray,cv::COLOR_BGR2GRAY);
      else
         aGray=aResult.aAligned;
      cv::Sobel(aGray,aVertical,CV_16S,0,1,3);
      int iSharedType=-1;
      double flBestSharedCoverage{};
      for(int iY=iHeight*38/100;iY<iHeight*79/100;++iY) {
         short const* pRow=aVertical.ptr<short>(iY);
         unsigned uEdges{};
         int const iXFirst=iWidth*10/100;
         int const iXLast=iWidth*90/100;
         for(int iX=iXFirst;iX<iXLast;++iX)
            uEdges+=std::abs(static_cast<int>(pRow[iX]))>=85;
         double const flCoverage=static_cast<double>(uEdges)/
            std::max(1,iXLast-iXFirst);
         if(flCoverage>flBestSharedCoverage) {
            flBestSharedCoverage=flCoverage;
            iSharedType=iY;
         }
      }
      bool const boSharedType=iSharedType>=0 &&
         flBestSharedCoverage>=0.40;
      for(int iFace=0;iFace<2;++iFace) {
         cv::Mat const aFace=aResult.aAligned(aResult.arrFaces[iFace]);
         FrameGeometry const aGeometry=DetectFrameGeometry(aFace);
         // Each half needs its own upper frame, but the lower type bar
         // may be a single shared physical line across both halves.
         if(!aGeometry.optTitle)return std::nullopt;
         cv::Rect const& aTitle=*aGeometry.optTitle;
         int const iTop=aTitle.br().y+
            std::max(2,aFace.rows/120);
         int const iTypeY=aGeometry.optType?
            aGeometry.optType->y:
            boSharedType?iSharedType:-1;
         if(iTypeY<0)return std::nullopt;
         int const iBottom=iTypeY-
            std::max(2,aFace.rows/120);
         if(iBottom-iTop<aFace.rows/5 ||
            iBottom-iTop>aFace.rows*65/100)
            return std::nullopt;
         int const iLeft=aTitle.x;
         int const iRight=aTitle.br().x;
         if(iRight-iLeft<aFace.cols*55/100)
            return std::nullopt;
         aResult.arrArtwork[iFace]=cv::Rect(
            aResult.arrFaces[iFace].x+iLeft,iTop,
            iRight-iLeft,iBottom-iTop);
      }
      return aResult;
   }
}
