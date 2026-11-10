// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file CameraCardLayout.h
\brief Conservative whole-card layout evidence for ordinary modern card faces.
\details Distinguishes the outer standard card from an internal artwork
rectangle before the physical-card tracker is allowed to lock. The typical
name/artwork/type/text layout is also used to choose 0 or 180 degrees.
These are structural image cues, not OCR and not card identification.
Nonstandard layouts must use a separately validated layout recognizer.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace deckkernel::camera {
   /**
   \brief Image-layout evidence for a conventionally framed card.
   \details Weak or contradictory evidence is rejected instead of guessing.
   */
   struct StandardCardLayout {
      bool boRecognized{};
      bool boRotate180{};
      double flEvidence{};
      double flUprightScore{};
      double flInvertedScore{};
      double flUpperSeparator{};
      double flLowerSeparator{};
   };

   /**
   \brief Evidence that a rectangle contains the entire standard card frame.
   \details In contrast to an artwork-only rectangle, a standard card has
   a title-to-art boundary near its top AND an art-to-text boundary below.
   \note Scores are structural evidence, not card identity or OCR.
   */
   struct WholeCardFrameEvidence {
      bool boWholeCard{};
      bool boRotate180{};
      bool boOrientationReliable{};
      double flTitleBoundary{};
      double flTextBoundary{};
      double flScore{};
   };

   /**
   \brief Reject artwork-only rectangles before a physical-card lock.
   \details Works in both 0 and 180 degree views of a rectified portrait
   candidate. Two geographically separated, nearly horizontal frame
   separators must be visible. A landscape artwork rotated into portrait
   by minAreaRect geometry does not acquire these missing text/name bands.
   \param aRectified A geometrically normalized physical-card candidate.
   \returns Whole-card structural evidence and optional orientation.
   \throw cv::Exception If OpenCV processing fails.
   \note This classifier is deliberately restricted to framed standard cards.
   Unusual layouts require their own detection path rather than weakening
   physical-card validation globally.
   */
   [[nodiscard]] inline WholeCardFrameEvidence InspectWholeCardFrame(
      cv::Mat const& aRectified) {
      WholeCardFrameEvidence aEvidence;
      if(aRectified.empty() || aRectified.cols<80 ||
         aRectified.rows<110)return aEvidence;
      cv::Mat aSample,aGray;
      cv::resize(aRectified,aSample,cv::Size(240,336),
         0.0,0.0,cv::INTER_AREA);
      if(aSample.channels()==4)
         cv::cvtColor(aSample,aGray,cv::COLOR_BGRA2GRAY);
      else if(aSample.channels()==3)
         cv::cvtColor(aSample,aGray,cv::COLOR_BGR2GRAY);
      else
         aGray=aSample;

      struct Direction {
         double flTitle{};
         double flText{};
         double flScore{};
         bool boValid{};
      };
      auto const Evaluate=[&](cv::Mat const& aDirection)->Direction {
         cv::Mat aVertical;
         cv::Sobel(aDirection,aVertical,CV_16S,0,1,3);
         // Ignore left/right frame borders and require an edge extending
         // across much of the central card width, not just art texture.
         int const iLeft=aDirection.cols*17/100;
         int const iRight=aDirection.cols*83/100;
         auto const BandCoverage=[&](int iFirst,int iLast)->double {
            double flMaximum{};
            int const iStart=aDirection.rows*iFirst/100;
            int const iEnd=aDirection.rows*iLast/100;
            for(int iY=iStart;iY<iEnd;++iY) {
               short const* pRow=aVertical.ptr<short>(iY);
               unsigned uEdges{};
               for(int iX=iLeft;iX<iRight;++iX)
                  uEdges+=std::abs(static_cast<int>(pRow[iX]))>=85;
               flMaximum=std::max(flMaximum,
                  static_cast<double>(uEdges)/(iRight-iLeft));
            }
            return flMaximum;
         };
         // Printed title/art boundary is commonly in the upper 6..20%.
         // The distinct art/type or type/rules boundary appears lower down.
         double const flTitle=BandCoverage(6,20);
         double const flText=BandCoverage(45,70);
         bool const boValid=flTitle>=0.32 && flText>=0.32;
         return Direction{flTitle,flText,
            std::min(flTitle,flText)+0.15*(flTitle+flText),boValid};
      };
      Direction const aNormal=Evaluate(aGray);
      cv::Mat aInverted;
      cv::rotate(aGray,aInverted,cv::ROTATE_180);
      Direction const aReverse=Evaluate(aInverted);
      aEvidence.boWholeCard=aNormal.boValid || aReverse.boValid;
      if(!aEvidence.boWholeCard)return aEvidence;
      bool const boReverse=aReverse.boValid && (!aNormal.boValid ||
         aReverse.flScore>aNormal.flScore+0.08);
      Direction const& aSelected=boReverse?aReverse:aNormal;
      aEvidence.boRotate180=boReverse;
      aEvidence.boOrientationReliable=
         (aNormal.boValid!=aReverse.boValid) ||
         std::abs(aNormal.flScore-aReverse.flScore)>=0.08;
      aEvidence.flTitleBoundary=aSelected.flTitle;
      aEvidence.flTextBoundary=aSelected.flText;
      aEvidence.flScore=aSelected.flScore;
      return aEvidence;
   }

   /**
   \brief Evaluate a standard frame and identify its correct 180-degree orientation.
   \details The illustration of a standard card occupies the upper middle,
   with a title bar above and a less colorful text panel below. Its top
   and bottom boundaries are usually horizontal. Whole-artwork crops lack
   those frame separators and should not be accepted as physical cards.
   The decision is deliberately conservative; it does not cover adventure,
   split, full-art, borderless, or nonstandard frame designs.
   \param aRectified Card-shaped image after four-corner perspective correction.
   \returns Layout recognition and the stronger orientation.
   \throw cv::Exception If an OpenCV image operation fails.
   */
   [[nodiscard]] inline StandardCardLayout InspectStandardCard(
      cv::Mat const& aRectified) {
      StandardCardLayout aResult;
      if(aRectified.empty() || aRectified.cols<80 ||
         aRectified.rows<110)return aResult;
      cv::Mat aSample,aColor,aGray,aSmooth;
      cv::resize(aRectified,aSample,cv::Size(240,336),
         0.0,0.0,cv::INTER_AREA);
      if(aSample.channels()==4)
         cv::cvtColor(aSample,aColor,cv::COLOR_BGRA2BGR);
      else if(aSample.channels()==1)
         cv::cvtColor(aSample,aColor,cv::COLOR_GRAY2BGR);
      else
         aColor=aSample;
      cv::cvtColor(aColor,aGray,cv::COLOR_BGR2GRAY);
      // Smooth printed letters but retain the larger artwork textures.
      cv::GaussianBlur(aColor,aSmooth,cv::Size(7,7),1.8);

      auto const Band=[&](int iX,int iY,int iWidth,int iHeight) {
         return cv::Rect(aSmooth.cols*iX/100,aSmooth.rows*iY/100,
            std::max(1,aSmooth.cols*iWidth/100),
            std::max(1,aSmooth.rows*iHeight/100));
      };
      auto const Variation=[&](cv::Rect const& aRegion) {
         cv::Scalar aMean,aDeviation;
         cv::meanStdDev(aSmooth(aRegion),aMean,aDeviation);
         return std::sqrt((aDeviation[0]*aDeviation[0]+
            aDeviation[1]*aDeviation[1]+
            aDeviation[2]*aDeviation[2])/3.0);
      };
      cv::Mat aVerticalGradient;
      cv::Sobel(aGray,aVerticalGradient,CV_16S,0,1,3);
      // A frame separator should extend across much of the card width,
      // whereas texture in an illustration generally does not form one
      // consistent horizontal edge across that whole width.
      auto const Separator=[&](int iMinY,int iMaxY) {
         int const iLeft=aGray.cols*12/100;
         int const iRight=aGray.cols*88/100;
         double flCoverage{};
         for(int iY=aGray.rows*iMinY/100;
            iY<aGray.rows*iMaxY/100;++iY) {
            int iPixels{};
            short const* pRow=aVerticalGradient.ptr<short>(iY);
            for(int iX=iLeft;iX<iRight;++iX)
               iPixels+=std::abs(static_cast<int>(pRow[iX]))>=85;
            flCoverage=std::max(flCoverage,
               static_cast<double>(iPixels)/(iRight-iLeft));
         }
         return flCoverage;
      };

      struct Direction {
         bool boValid{};
         double flScore{};
         double flTopSeparator{};
         double flBottomSeparator{};
      };
      auto const Evaluate=[&](bool boRotated) {
         // Symmetric sampling avoids an additional image rotation and lets
         // the same detector evaluate both possible physical orientations.
         auto const Project=[&](int iY,int iHeight) {
            return boRotated?100-iY-iHeight:iY;
         };
         double const flArt=Variation(Band(14,Project(25,24),72,24));
         double const flRules=Variation(Band(14,Project(66,16),72,16));
         double const flName=Variation(Band(14,Project(6,9),72,9));
         double const flFrame=Variation(Band(14,Project(90,6),72,6));
         double const flUpper=Separator(
            boRotated?73:16,boRotated?84:27);
         double const flLower=Separator(
            boRotated?34:48,boRotated?52:66);
         double const flTextPenalty=
            0.60*flRules+0.25*flName+0.15*flFrame;
         double const flScore=flArt-flTextPenalty+
            20.0*(flUpper+flLower);
         bool const boStructured=flArt>=12.0 &&
            flArt>flRules*1.10 &&
            flArt>flName*1.05 &&
            flUpper>=0.30 && flLower>=0.30 &&
            flUpper+flLower>=0.75;
         return Direction{boStructured,flScore,flUpper,flLower};
      };
      Direction const aUpright=Evaluate(false);
      Direction const aInverted=Evaluate(true);
      aResult.flUprightScore=aUpright.flScore;
      aResult.flInvertedScore=aInverted.flScore;
      bool const boRotated=
         aInverted.boValid && (!aUpright.boValid ||
            aInverted.flScore>aUpright.flScore+4.0);
      Direction const& aWinner=boRotated?aInverted:aUpright;
      // A near-tie between two valid interpretations is not proof of
      // orientation and is intentionally left unresolved.
      bool const boAmbiguous=aUpright.boValid && aInverted.boValid &&
         std::abs(aUpright.flScore-aInverted.flScore)<4.0;
      aResult.boRecognized=aWinner.boValid && !boAmbiguous;
      aResult.boRotate180=boRotated;
      aResult.flEvidence=aWinner.flScore;
      aResult.flUpperSeparator=aWinner.flTopSeparator;
      aResult.flLowerSeparator=aWinner.flBottomSeparator;
      return aResult;
   }
}
