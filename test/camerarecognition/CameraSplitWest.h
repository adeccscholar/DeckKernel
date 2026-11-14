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
      for(int iFace=0;iFace<2;++iFace) {
         cv::Mat const aFace=aResult.aAligned(aResult.arrFaces[iFace]);
         FrameGeometry const aGeometry=DetectFrameGeometry(aFace);
         WholeCardFrameEvidence const aWhole=InspectWholeCardFrame(aFace);
         if(!aGeometry.optTitle || !aGeometry.optType ||
            !aWhole.boWholeCard)
            return std::nullopt;
         cv::Rect const& aTitle=*aGeometry.optTitle;
         cv::Rect const& aType=*aGeometry.optType;
         int const iTop=aTitle.br().y+
            std::max(2,aFace.rows/120);
         int const iBottom=aType.y-
            std::max(2,aFace.rows/120);
         if(iBottom-iTop<aFace.rows/5 ||
            iBottom-iTop>aFace.rows*65/100)
            return std::nullopt;
         int const iLeft=std::max(aTitle.x,aType.x);
         int const iRight=std::min(aTitle.br().x,aType.br().x);
         if(iRight-iLeft<aFace.cols*55/100)
            return std::nullopt;
         aResult.arrArtwork[iFace]=cv::Rect(
            aResult.arrFaces[iFace].x+iLeft,iTop,
            iRight-iLeft,iBottom-iTop);
      }
      return aResult;
   }
}
