// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file CameraFrameGeometry.h
\brief Optional geometry-derived card frame and printing-margin regions.
\details Finds broad title and type-bar outlines from image contours and
   a dark bottom printing strip from the observed pixel distribution.
   Geometric acceptance is deliberately conservative. If either frame
   shape is ambiguous, downstream OCR keeps its existing fallback zones.
   Percentages bound the search, but never determine accepted boundaries.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

namespace deckkernel::camera {
   struct FrameGeometry {
      std::optional<cv::Rect> optTitle;
      std::optional<cv::Rect> optType;
      std::optional<cv::Rect> optPrintingStrip;
      double flTitleConfidence{};
      double flTypeConfidence{};
   };

   /**
   \brief Detect the physical title and type-bar outlines and black set strip.
   \details Image contours, hull-to-contour area and rectangular coverage
      distinguish outlined horizontal bars from individual printed lines.
      The bottom strip is independently detected from a dark connected
      horizontal run; it need not share a contour with either title bar.
   \param aCard Perspective-rectified, upright card image.
   \returns Optional measured rectangles in original image coordinates.
   \throw cv::Exception On invalid OpenCV image operations.
   \note These results are geometric observations, not OCR evidence.
   */
   [[nodiscard]] inline FrameGeometry DetectFrameGeometry(
      cv::Mat const& aCard) {
      FrameGeometry aResult;
      if(aCard.empty() || aCard.cols<100 || aCard.rows<200)
         return aResult;
      cv::Mat aSmall,aGray;
      cv::resize(aCard,aSmall,cv::Size(320,448),
         0.0,0.0,cv::INTER_AREA);
      if(aSmall.channels()==4)
         cv::cvtColor(aSmall,aGray,cv::COLOR_BGRA2GRAY);
      else if(aSmall.channels()==3)
         cv::cvtColor(aSmall,aGray,cv::COLOR_BGR2GRAY);
      else
         aGray=aSmall;
      cv::Mat aSmooth,aEdges;
      cv::GaussianBlur(aGray,aSmooth,cv::Size(3,3),0.75);
      cv::Canny(aSmooth,aEdges,45,125);
      cv::morphologyEx(aEdges,aEdges,cv::MORPH_CLOSE,
         cv::getStructuringElement(cv::MORPH_RECT,cv::Size(5,3)));
      std::vector<std::vector<cv::Point>> vecContours;
      cv::findContours(aEdges,vecContours,cv::RETR_LIST,
         cv::CHAIN_APPROX_SIMPLE);
      struct Candidate {
         cv::Rect aBox{};
         double flConfidence{};
      };
      std::optional<Candidate> optTitle,optType;
      for(auto const& vecContour:vecContours) {
         if(vecContour.size()<5)continue;
         cv::Rect const aBox=cv::boundingRect(vecContour);
         double const flWidth=static_cast<double>(aBox.width)/aSmall.cols;
         double const flHeight=static_cast<double>(aBox.height)/aSmall.rows;
         if(flWidth<0.64 || flWidth>0.99 ||
            flHeight<0.032 || flHeight>0.16 ||
            aBox.x<aSmall.cols*0.015 ||
            aBox.br().x>aSmall.cols*0.985)
            continue;
         double const flArea=std::abs(cv::contourArea(vecContour));
         double const flCoverage=flArea/std::max(1,aBox.area());
         if(flCoverage<0.46 || flCoverage>1.05)continue;
         std::vector<cv::Point> vecHull;
         cv::convexHull(vecContour,vecHull);
         double const flHull=std::abs(cv::contourArea(vecHull));
         double const flConcavity=flHull>0.0?
            std::max(0.0,1.0-flArea/flHull):0.0;
         // Arc/corner variation adds evidence but is not mandatory:
         // many printed cards have straight, beveled frame corners.
         double const flScore=flWidth+0.35*flCoverage+
            0.10*std::min(0.15,flConcavity)/0.15;
         double const flY=static_cast<double>(aBox.y)/aSmall.rows;
         if(flY>=0.018 && flY<0.20 &&
            static_cast<double>(aBox.br().y)/aSmall.rows<0.28) {
            if(!optTitle || flScore>optTitle->flConfidence)
               optTitle=Candidate{aBox,flScore};
         }
         else if(flY>=0.39 && flY<0.75 &&
            static_cast<double>(aBox.br().y)/aSmall.rows<0.82) {
            if(!optType || flScore>optType->flConfidence)
               optType=Candidate{aBox,flScore};
         }
      }
      auto const ToSource=[&](cv::Rect const& aBox) {
         int const iLeft=aBox.x*aCard.cols/aSmall.cols;
         int const iTop=aBox.y*aCard.rows/aSmall.rows;
         int const iRight=(aBox.br().x*aCard.cols+aSmall.cols-1)/
            aSmall.cols;
         int const iBottom=(aBox.br().y*aCard.rows+aSmall.rows-1)/
            aSmall.rows;
         return cv::Rect(iLeft,iTop,std::max(1,iRight-iLeft),
            std::max(1,iBottom-iTop)) &
            cv::Rect(0,0,aCard.cols,aCard.rows);
      };
      if(optTitle) {
         aResult.optTitle=ToSource(optTitle->aBox);
         aResult.flTitleConfidence=optTitle->flConfidence;
      }
      if(optType) {
         aResult.optType=ToSource(optType->aBox);
         aResult.flTypeConfidence=optType->flConfidence;
      }
      // Detect the dark collector/set line separately, across the card's
      // lower edge. Its observed top/bottom rows determine the OCR crop.
      cv::Mat aDark;
      cv::threshold(aGray,aDark,75,255,cv::THRESH_BINARY_INV);
      int const iLeft=aSmall.cols*12/100;
      int const iRight=aSmall.cols*88/100;
      int iBestFirst=-1,iBestLast=-1;
      int iFirst=-1;
      auto const TestRun=[&](int iLast) {
         if(iFirst<0)return;
         int const iMinLength=std::max(3,aSmall.rows/140);
         if(iLast-iFirst>=iMinLength &&
            (iBestFirst<0 || iLast-iFirst>iBestLast-iBestFirst)) {
            iBestFirst=iFirst;
            iBestLast=iLast;
         }
      };
      for(int iY=aSmall.rows*82/100;iY<aSmall.rows*995/1000;++iY) {
         int const iCount=cv::countNonZero(
            aDark(cv::Rect(iLeft,iY,iRight-iLeft,1)));
         bool const boDark=
            iCount>=(iRight-iLeft)*74/100;
         if(boDark) {
            if(iFirst<0)iFirst=iY;
         }
         else {
            TestRun(iY);
            iFirst=-1;
         }
      }
      TestRun(aSmall.rows*995/1000);
      if(iBestFirst>=0) {
         int const iTop=std::max(0,
            (iBestFirst-2)*aCard.rows/aSmall.rows);
         int const iBottom=std::min(aCard.rows,
            (iBestLast+2)*aCard.rows/aSmall.rows);
         if(iBottom-iTop>=3)
            aResult.optPrintingStrip=cv::Rect(
               aCard.cols*4/100,iTop,aCard.cols*92/100,
               iBottom-iTop);
      }
      return aResult;
   }
}
