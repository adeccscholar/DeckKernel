// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/** \file main.cpp
    \brief First-stage camera card detector: present, stable, changed, absent.
    \details No Scryfall requests, no database access, no physical edition
             identification. Image evidence is optional and local.
*/
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#if defined(DECKKERNEL_CAMERA_HAS_SDL_SKIA)
#include "CameraDashboard.h"
#endif
#include "CameraOcrZones.h"
#include "CameraImageQuality.h"
#include "CameraCardSession.h"
#include "CameraCardLayout.h"
#include "CameraPhysicalTracker.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/geometry.hpp>
#include <opencv2/imgcodecs.hpp>
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
#include "CardImageMatcher.h"
#endif
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
#include <tesseract/baseapi.h>
#endif
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
#include <opencv2/videoio.hpp>
#include "CameraTableFocus.h"
#endif
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <charconv>
#include <cstdint>
#include <cwchar>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <opencv2/features2d.hpp>
#include <cctype>
#include <format>
#include <iostream>
#include <limits>
#include <optional>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
using hash_ty=std::uint64_t;
hash_ty DHash(cv::Mat const& source) {
   cv::Mat gray,small;
   if(source.channels()==3)cv::cvtColor(source,gray,cv::COLOR_BGR2GRAY);
   else if(source.channels()==4)cv::cvtColor(source,gray,cv::COLOR_BGRA2GRAY);
   else gray=source;
   cv::resize(gray,small,cv::Size(9,8),0,0,cv::INTER_AREA);
   hash_ty uResult{};
   for(int y=0;y<8;++y)
      for(int x=0;x<8;++x)
         uResult=(uResult<<1)|
            (small.at<unsigned char>(y,x)>small.at<unsigned char>(y,x+1)?1ULL:0ULL);
   return uResult;
}
struct FoundCard {
   cv::Mat rectified;
   hash_ty fingerprint{};
   std::array<cv::Point,4> corners{};
   bool boRotate180{};
   bool boLayoutRecognized{};
};
/**
\brief Estimate actual card width from four raw camera-frame corners.
\details Correctly excludes the scaling performed by perspective rectification.
\param arrCorners Four ordered physical-card corner positions.
\returns Source width in real camera pixels.
\throw None.
*/
[[nodiscard]] int NativeCardWidth(
   std::array<cv::Point,4> const& arrCorners) noexcept {
   double const flA=(cv::norm(arrCorners[0]-arrCorners[1])+
      cv::norm(arrCorners[2]-arrCorners[3]))/2.0;
   double const flB=(cv::norm(arrCorners[1]-arrCorners[2])+
      cv::norm(arrCorners[3]-arrCorners[0]))/2.0;
   return static_cast<int>(std::min(flA,flB));
}
struct CardDetectionDiagnostics {
   unsigned contours{};
   unsigned areaCandidates{};
   unsigned quadrilaterals{};
   unsigned rotatedCandidates{};
   unsigned aspectCandidates{};
   double selectedAreaPercent{};
   bool recoveredRectangle{};
   unsigned layoutChecked{};
   unsigned layoutRecognized{};
   unsigned rejectedPartialFrame{};
   bool selectedLayoutRecognized{};
   unsigned rejectedShape{};
   unsigned rejectedAspect{};
   unsigned selectedInnerPanels{};
   double selectedRatio{};
   bool inferredOuterFrame{};
};

/** Refine only interior, finite corners on the detection image.
    Border and out-of-frame points can arise from contour approximation:
    OpenCV cornerSubPix rejects them as initial points. Keep those corners
    unchanged; any failure of optional refinement must not lose the card.
*/
[[nodiscard]] std::array<cv::Point2f,4> RefineCardCorners(
   cv::Mat const& gray,std::array<cv::Point2f,4> const& points,
   double scale) {
   auto result=points;
   if(gray.empty()||gray.channels()!=1||gray.cols<16||gray.rows<16 ||
      !std::isfinite(scale)||scale<=0.0)return result;
   constexpr int border=6; // 5-pixel search window + one pixel clearance
   std::vector<cv::Point2f> valid;
   std::vector<unsigned> indices;
   for(unsigned i=0;i<4;++i) {
      cv::Point2f const p(static_cast<float>(points[i].x*scale),
         static_cast<float>(points[i].y*scale));
      if(!std::isfinite(p.x)||!std::isfinite(p.y)||
         p.x<border||p.y<border||
         p.x>=gray.cols-border||p.y>=gray.rows-border)continue;
      valid.push_back(p);
      indices.push_back(i);
   }
   if(valid.empty())return result;
   auto refined=valid;
   try {
      cv::cornerSubPix(gray,refined,cv::Size(5,5),cv::Size(-1,-1),
         cv::TermCriteria(cv::TermCriteria::COUNT|
            cv::TermCriteria::EPS,12,0.05));
   } catch(cv::Exception const&) {
      // Refinement is optional. Never abort card capture on a bad corner.
      return result;
   }
   for(std::size_t j=0;j<indices.size();++j) {
      auto const& p=refined[j];
      if(!std::isfinite(p.x)||!std::isfinite(p.y)||
         p.x<border||p.y<border||
         p.x>=gray.cols-border||p.y>=gray.rows-border||
         cv::norm(p-valid[j])>3.0)continue;
      result[indices[j]]=cv::Point2f(
         static_cast<float>(p.x/scale),static_cast<float>(p.y/scale));
   }
   return result;
}

std::optional<FoundCard> FindCard(cv::Mat const& frame,
   CardDetectionDiagnostics* diagnostics=nullptr,
   bool boRequireStandardLayout=false) {
   if(diagnostics)*diagnostics={};
   if(frame.empty()||frame.cols<64||frame.rows<64)return std::nullopt;

   // Keep contour work bounded even when the camera captures at 4K.
   // Perspective rectification always uses unmodified source pixels.
   double const scale=std::min(1.0,
      1600.0/std::max(frame.cols,frame.rows));
   cv::Mat image;
   if(scale<1.0)
      cv::resize(frame,image,cv::Size(),scale,scale,cv::INTER_AREA);
   else image=frame;
   cv::Mat gray,blur,edges;
   if(image.channels()==3)cv::cvtColor(image,gray,cv::COLOR_BGR2GRAY);
   else if(image.channels()==4)cv::cvtColor(image,gray,cv::COLOR_BGRA2GRAY);
   else gray=image;
   cv::GaussianBlur(gray,blur,cv::Size(5,5),0);

   double const imageArea=static_cast<double>(image.cols)*image.rows;
   double const minArea=std::max(350.0,imageArea*0.0005);
   double bestScore=-1.0,bestArea=0;
   bool recoveredBest=false;
   std::vector<cv::Point> best;
   bool boBestRotate180{};
   bool boBestLayoutRecognized{};
   unsigned uBestInnerPanels{};
   double flBestRatio{};
   CardDetectionDiagnostics stats;

   auto searchContours=[&](cv::Mat const& edgeMap) {
      // Small gaps in the printed border must not destroy the entire card
      // outline. The original raw Canny contours from the user's screenshot
      // did not approximate to any four-point polygon.
      cv::Mat closed;
      int const kernel=std::max(3,image.cols/320)|1;
      cv::morphologyEx(edgeMap,closed,cv::MORPH_CLOSE,
         cv::getStructuringElement(cv::MORPH_RECT,
            cv::Size(kernel,kernel)));
      std::vector<std::vector<cv::Point>> contours;
      cv::findContours(closed,contours,cv::RETR_LIST,
         cv::CHAIN_APPROX_SIMPLE);
      stats.contours+=static_cast<unsigned>(contours.size());
      struct InnerOutline {
         cv::Rect aBox;
         cv::Point2f aCenter;
         double flArea{};
      };
      // Precompute nested rectangular shapes once per Canny pass. Iterating
      // approxPolyDP over every contour for every card candidate would make
      // 4K camera acquisition unnecessarily quadratic and unstable.
      std::vector<InnerOutline> vecPossiblePanels;
      vecPossiblePanels.reserve(contours.size()/4);
      for(auto const& aCandidate:contours) {
         double const flArea=std::abs(cv::contourArea(aCandidate));
         if(flArea<minArea*0.3)continue;
         cv::Rect const aBox=cv::boundingRect(aCandidate);
         double const flAspect=static_cast<double>(
            std::max(aBox.width,aBox.height))/
            std::max(1,std::min(aBox.width,aBox.height));
         if(flAspect<1.3 || flAspect>4.0)continue;
         std::vector<cv::Point> vecCorners;
         cv::approxPolyDP(aCandidate,vecCorners,
            cv::arcLength(aCandidate,true)*0.025,true);
         if(vecCorners.size()!=4 || !cv::isContourConvex(vecCorners))
            continue;
         vecPossiblePanels.push_back(InnerOutline{
            aBox,cv::Point2f(aBox.x+aBox.width*0.5f,
               aBox.y+aBox.height*0.5f),flArea});
      }
      for(auto const& contour:contours) {
         double const area=std::abs(cv::contourArea(contour));
         // Exclude huge table/desk outlines, but retain small printed cards.
         // A sharply photographed card may occupy much more than 20% of
         // a 4K frame; rejecting large cards prevents focusing close enough.
         // Use card-like shape and boundary evidence, not an arbitrary 20%.
         if(area<minArea||area>imageArea*0.85)continue;
         ++stats.areaCandidates;
         auto const rectangle=cv::minAreaRect(contour);
         double const shortSide=std::min(
            rectangle.size.width,rectangle.size.height);
         double const longSide=std::max(
            rectangle.size.width,rectangle.size.height);
         if(shortSide<22.0)continue;
         double const rectangleArea=
            static_cast<double>(rectangle.size.width)*rectangle.size.height;
         if(rectangleArea<=0)continue;
         double const fill=std::min(1.0,area/rectangleArea);

         std::vector<cv::Point> corners;
         for(double epsilon:{0.018,0.026,0.038}) {
            cv::approxPolyDP(contour,corners,
               cv::arcLength(contour,true)*epsilon,true);
            if(corners.size()==4 && cv::isContourConvex(corners))
               break;
         }
         bool const recovered=corners.size()!=4||
            !cv::isContourConvex(corners);
         if(recovered) {
            // An axis-rotated minimum-area box cannot recover strong perspective.
            // Use it only for near-frontoparallel cards, never as an arbitrary
            // substitute for a strongly trapezoidal projection.
            if(longSide/shortSide<1.22 || longSide/shortSide>1.72)
               continue;
            // Recover the quadrilateral from the rotated minimum-area box.
            // Only accept contours filling most of that box, not arbitrary
            // elongated edges, hands or a tabletop boundary.
            // A mouse/oval fills about 78% of its bounding rectangle.
            // Card corners are nearly straight, including rounded corners.
            if(fill<0.86) {
               ++stats.rejectedShape;
               continue;
            }
            cv::Point2f points[4];
            rectangle.points(points);
            corners.clear();
            for(auto const& point:points)
               corners.emplace_back(cvRound(point.x),cvRound(point.y));
            ++stats.rotatedCandidates;
         } else {
            // Projective distortions may produce a trapezoidal silhouette.
            // Accept a convex quadrilateral with plausible short/long edges
            // even if its bounding rectangle differs from the card's ratio.
            std::array<double,4> arrLengths{};
            for(int iSide=0;iSide<4;++iSide)
               arrLengths[iSide]=cv::norm(
                  cv::Point2d(corners[iSide])-cv::Point2d(corners[(iSide+1)%4]));
            double const flMin=*std::min_element(arrLengths.begin(),arrLengths.end());
            double const flMax=*std::max_element(arrLengths.begin(),arrLengths.end());
            if(flMin<18.0 || flMax/flMin>3.5)continue;
            ++stats.quadrilaterals;
         }
         // PHASE 01: the perimeter of a physical card is supported by
         // straight, visible edges on all four sides. An oval mouse can
         // acquire a plausible minimum-area rectangle but it does not
         // actually contain four such supported sides.
         std::array<double,4> arrEdgeCoverage{};
         for(int iSide=0;iSide<4;++iSide) {
            cv::Point aFrom=corners[iSide];
            cv::Point aTo=corners[(iSide+1)%4];
            if(!cv::clipLine(closed.size(),aFrom,aTo)) {
               arrEdgeCoverage[iSide]=0.0;
               continue;
            }
            cv::LineIterator aLine(closed,aFrom,aTo,8,false);
            int const iStep=std::max(1,aLine.count/120);
            unsigned uSamples{},uPresent{};
            for(int iSample=0;iSample<aLine.count;++iSample,++aLine) {
               if(iSample%iStep!=0)continue;
               cv::Point const aPixel=aLine.pos();
               if(aPixel.x<2||aPixel.y<2||
                  aPixel.x>=closed.cols-2||aPixel.y>=closed.rows-2)
                  continue;
               ++uSamples;
               bool boEdge{};
               for(int iDy=-2;iDy<=2 && !boEdge;++iDy) {
                  unsigned char const* pRow=closed.ptr<unsigned char>(
                     aPixel.y+iDy);
                  for(int iDx=-2;iDx<=2;++iDx) {
                     if(pRow[aPixel.x+iDx]) {
                        boEdge=true;
                        break;
                     }
                  }
               }
               uPresent+=boEdge;
            }
            arrEdgeCoverage[iSide]=uSamples?
               static_cast<double>(uPresent)/uSamples:0.0;
         }
         unsigned uSupportedSides{};
         for(double const flCoverage:arrEdgeCoverage)
            uSupportedSides+=flCoverage>=0.45;
         // A closed four-corner contour needs evidence for at least three
         // sides, while allowing one weak edge under glare/occlusion.
         if(uSupportedSides<3) {
            ++stats.rejectedShape;
            continue;
         }
         // PHASE 01: compare the contour to the 63 x 88 mm physical
         // card. The original code only rewarded the ratio weakly, so an
         // artwork or type rectangle could still win by area. Distortion
         // widens the range, but a flat artwork panel must not be confused
         // with an unusually foreshortened whole card.
         double const ratio=longSide/shortSide;
         if(ratio<1.14 || ratio>1.90) {
            ++stats.rejectedAspect;
            continue;
         }
         if(recovered && (ratio<1.22 || ratio>1.64)) {
            ++stats.rejectedAspect;
            continue;
         }
         if(!recovered) {
            double const flPerimeter=cv::arcLength(contour,true);
            double const flQuadPerimeter=cv::arcLength(corners,true);
            if(flQuadPerimeter<=0.0 ||
               flPerimeter/flQuadPerimeter>1.15) {
               ++stats.rejectedShape;
               continue;
            }
            // A typical isolated artwork is wider than a physical card.
            // Preserve strongly skewed real cards: their opposing edge
            // lengths differ because of perspective projection.
            if(ratio>1.66) {
               std::array<double,4> arrSides{};
               for(int iSide=0;iSide<4;++iSide)
                  arrSides[iSide]=cv::norm(
                     corners[iSide]-corners[(iSide+1)%4]);
               double const flOppositeA=std::max(arrSides[0],arrSides[2])/
                  std::max(1.0,std::min(arrSides[0],arrSides[2]));
               double const flOppositeB=std::max(arrSides[1],arrSides[3])/
                  std::max(1.0,std::min(arrSides[1],arrSides[3]));
               if(std::max(flOppositeA,flOppositeB)<1.20) {
                  ++stats.rejectedAspect;
                  continue;
               }
            }
         }
         ++stats.aspectCandidates;
         // Favor the rectangular printed card over partial inner borders.
         double const ratioFitness=std::max(0.0,
            1.0-std::abs(ratio-1.40)/0.50);
         // A card is an outer, physical object. Tiny interior art boxes
         // may be rectangular too; penalize candidates significantly smaller
         // than the card expected at this camera distance, but do not impose
         // the old 4% minimum-area exclusion.
         // A second card-frame cue: compare the RGB intensity immediately
         // inside and outside the contour's minimum-area rectangle. Card
         // borders often separate distinctly from the desk; interior artwork
         // and fabric creases typically do not have this external boundary.
         cv::Rect const bounds=cv::boundingRect(contour);
         int const margin=std::max(4,image.cols/260);
         cv::Rect const expanded(bounds.x-margin,bounds.y-margin,
            bounds.width+2*margin,bounds.height+2*margin);
         double boundaryContrast=0.0;
         if(expanded.x>=0&&expanded.y>=0 &&
            expanded.br().x<gray.cols&&expanded.br().y<gray.rows) {
            cv::Scalar inside=cv::mean(gray(bounds));
            cv::Mat outsideMask(expanded.height,expanded.width,CV_8UC1,
               cv::Scalar(255));
            cv::rectangle(outsideMask,cv::Rect(margin,margin,
               bounds.width,bounds.height),cv::Scalar(0),-1);
            cv::Scalar outside=cv::mean(gray(expanded),outsideMask);
            boundaryContrast=std::abs(inside[0]-outside[0]);
         }
         // Geometric discovery is PHASE 01. The low-resolution standard
         // layout is an advisory RANKING cue, not a hard precondition.
         // Blur, glare, uncommon printing borders or a weak MJPG frame can
         // easily invalidate these provisional separator thresholds.
         // A physical contour may be tracked and refocused despite that.
         // A physical card encloses multiple rectangular elements, while
         // a mouse or isolated artwork does not. Count substantial, distinct
         // nested panels (art and rules) as additional evidence; do not
         // require them, because blur or unconventional printings hide them.
         unsigned uInnerPanels{};
         std::array<cv::Rect,3> arrPanels{};
         for(auto const& aInner:vecPossiblePanels) {
            if(aInner.flArea<area*0.075 ||
               aInner.flArea>area*0.70)continue;
            cv::Rect const& aInnerBox=aInner.aBox;
            if(cv::pointPolygonTest(corners,aInner.aCenter,true)<
               shortSide*0.06)continue;
            bool boDuplicate{};
            for(unsigned uPanel=0;uPanel<uInnerPanels;++uPanel) {
               cv::Rect const aCommon=aInnerBox&arrPanels[uPanel];
               double const flOverlap=static_cast<double>(aCommon.area())/
                  std::max(1,std::min(aInnerBox.area(),
                     arrPanels[uPanel].area()));
               if(flOverlap>0.75) {
                  boDuplicate=true;
                  break;
               }
            }
            if(boDuplicate)continue;
            arrPanels[uInnerPanels++]=aInnerBox;
            if(uInnerPanels==arrPanels.size())break;
         }
         double const flGeometryScore=area*fill*
            (0.65+0.35*ratioFitness)*
            (0.7+0.3*std::min(1.0,boundaryContrast/35.0))*
            (1.0+0.30*static_cast<double>(uInnerPanels));
         // Do not pre-prune a geometrically smaller whole card before
         // the full-frame structural evidence has been evaluated.
         if(!boRequireStandardLayout && flGeometryScore<=bestScore)
            continue;
         double flLayoutEvidence=0.0;
         bool boRotate180{};
         bool boLayoutRecognized{};
         if(boRequireStandardLayout) {
            // PHASE 01: a physical card must contain the expected outer
            // standard-card structure. Reject artwork-only rectangles before
            // allowing them to create a frozen physical-card session.
            std::array<cv::Point2f,4> arrCandidate{};
            cv::Point2f aCenter{};
            for(std::size_t uIndex=0;uIndex<4;++uIndex) {
               arrCandidate[uIndex]=cv::Point2f(
                  static_cast<float>(corners[uIndex].x),
                  static_cast<float>(corners[uIndex].y));
               aCenter+=arrCandidate[uIndex];
            }
            aCenter*=0.25f;
            std::sort(arrCandidate.begin(),arrCandidate.end(),
               [&](cv::Point2f const& aLeft,cv::Point2f const& aRight) {
                  return std::atan2(aLeft.y-aCenter.y,aLeft.x-aCenter.x) <
                     std::atan2(aRight.y-aCenter.y,aRight.x-aCenter.x);
               });
            auto const itCorner=std::min_element(
               arrCandidate.begin(),arrCandidate.end(),
               [](cv::Point2f const& aLeft,cv::Point2f const& aRight) {
                  return aLeft.x+aLeft.y<aRight.x+aRight.y;
               });
            std::rotate(arrCandidate.begin(),itCorner,arrCandidate.end());
            if(cv::norm(arrCandidate[0]-arrCandidate[1])>
               cv::norm(arrCandidate[1]-arrCandidate[2]))
               std::rotate(arrCandidate.begin(),
                  arrCandidate.begin()+1,arrCandidate.end());
            std::array<cv::Point2f,4> const arrTarget{{
               {0.f,0.f},{239.f,0.f},{239.f,335.f},{0.f,335.f}
            }};
            cv::Mat aCandidateImage;
            cv::warpPerspective(image,aCandidateImage,
               cv::getPerspectiveTransform(
                  arrCandidate.data(),arrTarget.data()),
               cv::Size(240,336));
            // PHASE 01: geometric contour alone is NOT an MTG card.
            // Require evidence for both the title/art and art/text frames.
            // This prevents a landscape artwork being stretched to portrait
            // and falsely accepted as a physical card.
            auto const aWholeFrame=
               deckkernel::camera::InspectWholeCardFrame(aCandidateImage);
            if(!aWholeFrame.boWholeCard) {
               ++stats.rejectedPartialFrame;
               continue;
            }
            ++stats.layoutChecked;
            auto const aLayout=
               deckkernel::camera::InspectStandardCard(aCandidateImage);
            boLayoutRecognized=true;
            ++stats.layoutRecognized;
            flLayoutEvidence=25.0*aWholeFrame.flScore;
            if(aWholeFrame.boOrientationReliable)
               boRotate180=aWholeFrame.boRotate180;
            else if(aLayout.boRecognized)
               boRotate180=aLayout.boRotate180;
            // Do not turn uncertain 0/180 orientation into a 90-degree
            // landscape-to-portrait reinterpretation of the card's artwork.
            // If layout is inconclusive, retain the geometrically valid
            // outer contour. The matcher still tests both rotations.
            // Never convert "unknown orientation" into "no physical card".

         }
         double const score=flGeometryScore*
            (boLayoutRecognized?
               1.0+0.01*std::clamp(flLayoutEvidence,0.0,100.0):1.0);
         if(score<=bestScore)continue;
         bestScore=score;
         bestArea=area;
         recoveredBest=recovered;
         uBestInnerPanels=uInnerPanels;
         flBestRatio=ratio;
         boBestRotate180=boRotate180;
         boBestLayoutRecognized=boLayoutRecognized;
         best=std::move(corners);
      }
   };
   cv::Canny(blur,edges,45,125);
   searchContours(edges);
   if(best.empty()) {
      cv::Mat enhanced;
      auto clahe=cv::createCLAHE(2.0,cv::Size(8,8));
      clahe->apply(blur,enhanced);
      cv::Canny(enhanced,edges,25,75);
      searchContours(edges);
   }
   stats.selectedAreaPercent=bestArea*100.0/imageArea;
   stats.recoveredRectangle=recoveredBest;
   stats.selectedLayoutRecognized=boBestLayoutRecognized;
   stats.selectedInnerPanels=uBestInnerPanels;
   stats.selectedRatio=flBestRatio;
   if(diagnostics)*diagnostics=stats;
   if(best.size()!=4)return std::nullopt;

   // Order four corners around the polygon centroid. Sum/difference extrema
   // can assign the same point to multiple corners under strong perspective.
   std::array<cv::Point2f,4> pts{};
   cv::Point2f aCenter{};
   for(auto const& aPoint:best) {
      aCenter+=cv::Point2f(static_cast<float>(aPoint.x/scale),
         static_cast<float>(aPoint.y/scale));
   }
   aCenter*=0.25f;
   for(std::size_t uIndex=0;uIndex<pts.size();++uIndex) {
      pts[uIndex]=cv::Point2f(static_cast<float>(best[uIndex].x/scale),
         static_cast<float>(best[uIndex].y/scale));
   }
   std::sort(pts.begin(),pts.end(),[&](cv::Point2f const& aLeft,
      cv::Point2f const& aRight) {
      return std::atan2(aLeft.y-aCenter.y,aLeft.x-aCenter.x) <
         std::atan2(aRight.y-aCenter.y,aRight.x-aCenter.x);
   });
   auto const itTopLeft=std::min_element(pts.begin(),pts.end(),
      [](cv::Point2f const& aLeft,cv::Point2f const& aRight) {
         return aLeft.x+aLeft.y<aRight.x+aRight.y;
      });
   std::rotate(pts.begin(),itTopLeft,pts.end());
   // Preserve portrait normalization independently of in-plane card rotation.
   if(cv::norm(pts[0]-pts[1])>cv::norm(pts[1]-pts[2]))
      std::rotate(pts.begin(),pts.begin()+1,pts.end());

   // Optional subpixel refinement must never invalidate the detected card.
   pts=RefineCardCorners(gray,pts,scale);

   std::array<cv::Point2f,4> const target{{
      {0,0},{479,0},{479,671},{0,671}
   }};
   cv::Mat rectified;
   cv::warpPerspective(frame,rectified,
      cv::getPerspectiveTransform(pts.data(),target.data()),
      cv::Size(480,672));
   std::array<cv::Point,4> quad{};
   for(unsigned i=0;i<4;++i)
      quad[i]=cv::Point(cvRound(pts[i].x),cvRound(pts[i].y));
   if(boBestRotate180)
      cv::rotate(rectified,rectified,cv::ROTATE_180);
   return FoundCard{rectified,DHash(rectified),quad,
      boBestRotate180,boBestLayoutRecognized};
}
/** High-resolution OCR warp only for an accepted card presentation.
    Corners are the ordered source-frame positions found by FindCard.
    Do not perform this full-resolution operation on every camera frame.
*/
[[nodiscard]] cv::Mat RectifyForOcr(cv::Mat const& source,
   std::array<cv::Point,4> const& corners,bool flip180) {
   if(source.empty())return {};
   std::array<cv::Point2f,4> pts{};
   for(std::size_t i=0;i<4;++i)
      pts[i]=cv::Point2f(static_cast<float>(corners[i].x),
         static_cast<float>(corners[i].y));
   double const width=std::max(cv::norm(pts[0]-pts[1]),
      cv::norm(pts[2]-pts[3]));
   int const outWidth=std::clamp(static_cast<int>(width),480,1120);
   int const outHeight=outWidth*7/5;
   std::array<cv::Point2f,4> const target{{
      {0,0},{static_cast<float>(outWidth-1),0},
      {static_cast<float>(outWidth-1),static_cast<float>(outHeight-1)},
      {0,static_cast<float>(outHeight-1)}
   }};
   cv::Mat result;
   cv::warpPerspective(source,result,
      cv::getPerspectiveTransform(pts.data(),target.data()),
      cv::Size(outWidth,outHeight));
   if(flip180)cv::rotate(result,result,cv::ROTATE_180);
   return result;
}
/**
\brief Preserve the physical margins outside the detected frame contour.
\details The canonical crop maps detected corners to the image boundary;
if those corners are on an interior frame, the collector-number strip
may be excluded. Here the four SOURCE points remain unchanged, but are
projected with an inset destination rectangle. Sampling the additional
6% border retrieves surrounding camera pixels, not artificial padding.
\param aSource Original source-frame pixels.
\param arrCorners The exact physical quadrilateral used for canonical crop.
\param boRotate180 Apply the same session orientation as the canonical crop.
\returns Independently rectified overscan view for OCR and display.
\throw cv::Exception If the transform fails.
\note Source pixels outside the camera sensor cannot be reconstructed.
*/
[[nodiscard]] cv::Mat RectifyWithMargin(cv::Mat const& aSource,
   std::array<cv::Point,4> const& arrCorners,bool boRotate180) {
   if(aSource.empty())return {};
   std::array<cv::Point2f,4> arrSource{};
   for(std::size_t uIndex=0;uIndex<4;++uIndex)
      arrSource[uIndex]=cv::Point2f(
         static_cast<float>(arrCorners[uIndex].x),
         static_cast<float>(arrCorners[uIndex].y));
   double const flWidth=std::max(
      cv::norm(arrSource[0]-arrSource[1]),
      cv::norm(arrSource[2]-arrSource[3]));
   int const iWidth=std::clamp(static_cast<int>(flWidth),480,1120);
   int const iHeight=iWidth*7/5;
   int const iMarginX=std::max(1,iWidth*6/100);
   int const iMarginY=std::max(1,iHeight*6/100);
   std::array<cv::Point2f,4> const arrTarget{{
      {static_cast<float>(iMarginX),static_cast<float>(iMarginY)},
      {static_cast<float>(iMarginX+iWidth-1),static_cast<float>(iMarginY)},
      {static_cast<float>(iMarginX+iWidth-1),
         static_cast<float>(iMarginY+iHeight-1)},
      {static_cast<float>(iMarginX),
         static_cast<float>(iMarginY+iHeight-1)}
   }};
   cv::Mat aOverscan;
   cv::warpPerspective(aSource,aOverscan,
      cv::getPerspectiveTransform(arrSource.data(),arrTarget.data()),
      cv::Size(iWidth+2*iMarginX,iHeight+2*iMarginY),
      cv::INTER_LINEAR,cv::BORDER_CONSTANT,cv::Scalar(0,0,0));
   if(boRotate180)cv::rotate(aOverscan,aOverscan,cv::ROTATE_180);
   return aOverscan;
}
// Manual 180-degree correction of an upside-down card. Use the same pixels
// for OCR, artwork matching, saved evidence and the diagnostic preview.
// This is NOT automatic orientation detection.
void FlipRectifiedCard(FoundCard& card) {
   cv::Mat rotated;
   cv::rotate(card.rectified,rotated,cv::ROTATE_180);
   card.rectified=std::move(rotated);
   card.fingerprint=DHash(card.rectified);
}
// A continuous card-presence session produces exactly one presentation event
// and, after sustained absence, exactly one removal event.
// dHash fluctuations must NOT produce either event on their own.
// Debounced presentation/removal events for a continuous camera stream.
// Physical removal and replacement are different transitions: a new detected
// card must not keep a previously recognized card active indefinitely.
/**
\brief Capture a single stable physical card and keep it locked until removal.
\details Changes in text, illustration, illumination or fingerprint cannot
start a second recognition. Only 45 consecutive absent observations release
the current card. A directly swapped card at the same location requires a
visible removal interval before it can become a new presentation.
*/
class StableCardTrigger {
   static constexpr unsigned stableFrames=4;
   static constexpr unsigned acquiringMissingTolerance=3;
   static constexpr unsigned removalFrames=45;
   std::optional<cv::Rect> m_previousBox;
   std::optional<cv::Rect> m_activeBox;
   cv::Mat m_bestFrame;
   double m_bestScore{-1.0};
   std::optional<unsigned> m_activeNumber;
   hash_ty m_activeFingerprint{};
   unsigned m_stable{},m_absent{},m_events{};
   unsigned m_acquireMissed{};

   [[nodiscard]] static bool Near(cv::Rect const& aLeft,
      cv::Rect const& aRight,double flMotionRatio,double flAreaRatio) {
      if(aRight.area()==0)return false;
      cv::Point2f const aFirst(
         aLeft.x+aLeft.width*0.5f,aLeft.y+aLeft.height*0.5f);
      cv::Point2f const aSecond(
         aRight.x+aRight.width*0.5f,aRight.y+aRight.height*0.5f);
      double const flMotion=std::max(12.0,
         flMotionRatio*std::max(aRight.width,aRight.height));
      return cv::norm(aFirst-aSecond)<=flMotion &&
         std::abs(static_cast<double>(aLeft.area())-aRight.area())/
            aRight.area()<=flAreaRatio;
   }
public:
   enum class EventKind { Presented, Removed };
   struct Event {
      EventKind kind;
      unsigned number;
      hash_ty fingerprint;
      cv::Mat card;
   };

   /**
   \brief Release tracked geometry for an explicit next scan.
   \details Preserve the monotonically increasing event number so late
      asynchronous results cannot be attributed to a new physical card.
   \returns Nothing.
   \throw None.
   */
   void Reset() noexcept {
      m_previousBox.reset();m_activeBox.reset();m_bestFrame.release();
      m_bestScore=-1.0;m_activeNumber.reset();m_activeFingerprint=0;
      m_stable=0;m_absent=0;m_acquireMissed=0;
   }
   [[nodiscard]] unsigned StableCount() const noexcept{return m_stable;}
   [[nodiscard]] static constexpr unsigned RequiredFrames() noexcept{
      return stableFrames;
   }
   [[nodiscard]] static constexpr unsigned RemovalFrames() noexcept{
      return removalFrames;
   }
   [[nodiscard]] bool Active() const noexcept{
      return m_activeNumber.has_value();
   }
   [[nodiscard]] bool SameCardPosition(FoundCard const& aFound) const {
      return m_activeBox && Near(cv::boundingRect(aFound.corners),
         *m_activeBox,0.22,0.45);
   }

   /**
   \brief Accept the next card-presence observation.
   \details A locked card is never replaced based on changing image content.
   \param optFound Geometric card candidate, or none if absent.
   \returns A new-card or removal event, otherwise no event.
   \throw cv::Exception If an OpenCV quality operation fails.
   */
   std::optional<Event> Accept(std::optional<FoundCard> const& optFound,
      bool boTrustedTracking=false) {
      bool const boPresent=optFound.has_value();
      if(m_activeNumber) {
         if(boPresent && m_activeBox &&
            (boTrustedTracking ||
               Near(cv::boundingRect(optFound->corners),*m_activeBox,0.22,0.45))) {
            // Monitor the locked physical card, without ever reclassifying it.
            m_absent=0;
            m_activeBox=cv::boundingRect(optFound->corners);
            return std::nullopt;
         }
         if(++m_absent>=removalFrames) {
            Event aRemoval{EventKind::Removed,*m_activeNumber,
               m_activeFingerprint,{}};
            m_activeNumber.reset();
            m_activeBox.reset();
            m_previousBox.reset();
            m_stable=0;
            m_acquireMissed=0;
            m_bestFrame.release();
            m_bestScore=-1.0;
            m_absent=0;
            return aRemoval;
         }
         return std::nullopt;
      }
      if(!boPresent) {
         // PHASE 02: tolerate brief dropped contours while autofocus and
         // MJPEG exposure settle. A single missed frame is not a new card.
         if(m_previousBox &&
            ++m_acquireMissed<=acquiringMissingTolerance)
            return std::nullopt;
         m_previousBox.reset();
         m_stable=0;
         m_acquireMissed=0;
         m_bestFrame.release();
         m_bestScore=-1.0;
         return std::nullopt;
      }
      cv::Rect const aBox=cv::boundingRect(optFound->corners);
      if(m_previousBox && Near(aBox,*m_previousBox,0.16,0.35)) {
         ++m_stable;
      }
      else {
         m_stable=1;
         m_bestFrame.release();
         m_bestScore=-1.0;
      }
      m_acquireMissed=0;
      auto const aQuality=deckkernel_camera::quality::Measure(optFound->rectified);
      if(aQuality.score>m_bestScore) {
         m_bestScore=aQuality.score;
         m_bestFrame=optFound->rectified.clone();
      }
      m_previousBox=aBox;
      if(m_stable<stableFrames)return std::nullopt;
      m_activeNumber=++m_events;
      m_activeBox=aBox;
      m_activeFingerprint=optFound->fingerprint;
      m_absent=0;
      m_previousBox.reset();
      m_stable=0;
      m_acquireMissed=0;
      Event aEvent{EventKind::Presented,*m_activeNumber,
         m_activeFingerprint,m_bestFrame.empty()?
            optFound->rectified.clone():m_bestFrame.clone()};
      m_bestFrame.release();
      m_bestScore=-1.0;
      return aEvent;
   }
};

#if defined(DECKKERNEL_CAMERA_HAS_OCR)
class CardOcr {
   tesseract::TessBaseAPI m_api;
   bool m_ready{};
   bool m_verbose{};
   bool m_lastAutoFlipped{};
   static std::string Clean(std::string text) {
      for(char& ch:text)if(ch=='\n'||ch=='\r'||ch=='\t')ch=' ';
      auto start=text.find_first_not_of(' ');
      if(start==std::string::npos)return {};
      auto end=text.find_last_not_of(' ');
      return text.substr(start,end-start+1);
   }
   struct OcrResult {std::string text;int confidence{};};
   OcrResult ReadRegion(cv::Mat const& image,cv::Rect region,int mode) {
      cv::Mat gray,scaled;
      gray=deckkernel_camera::quality::ForOcr(image(region));
      cv::resize(gray,scaled,cv::Size(),2.0,2.0,cv::INTER_CUBIC);
      m_api.SetPageSegMode(static_cast<tesseract::PageSegMode>(mode));
      m_api.SetImage(scaled.data,scaled.cols,scaled.rows,1,
         static_cast<int>(scaled.step));
      if(m_api.Recognize(nullptr)!=0)return {};
      int const confidence=m_api.MeanTextConf();
      std::unique_ptr<char[]> text(m_api.GetUTF8Text());
      std::string cleaned=text?Clean(text.get()):std::string{};
      unsigned letters=0;
      for(unsigned char ch:cleaned)if(std::isalnum(ch))++letters;
      if(confidence<45||letters<3)return {{},confidence};
      return {std::move(cleaned),confidence};
   }
public:
   explicit CardOcr(std::string const& tessdata,bool verbose=false):m_verbose(verbose) {
      if(m_api.Init(tessdata.c_str(),"eng",tesseract::OEM_LSTM_ONLY)!=0)
         throw std::runtime_error("Tesseract could not initialize eng.traineddata at "+tessdata);
      m_ready=true;
   }
   ~CardOcr(){if(m_ready)m_api.End();}
   bool Orient(cv::Mat& aCard) {
      if(aCard.empty())return false;
      cv::Mat reversed;
      cv::rotate(aCard,reversed,cv::ROTATE_180);
      auto const normal=deckkernel_camera::ocr::Detect(aCard);
      auto const flipped=deckkernel_camera::ocr::Detect(reversed);
      auto const normalName=ReadRegion(aCard,normal.name,tesseract::PSM_SINGLE_LINE);
      auto const normalType=ReadRegion(aCard,normal.type,tesseract::PSM_SINGLE_LINE);
      auto const reversedName=ReadRegion(reversed,flipped.name,tesseract::PSM_SINGLE_LINE);
      auto const reversedType=ReadRegion(reversed,flipped.type,tesseract::PSM_SINGLE_LINE);
      auto const normalText=ReadRegion(aCard,normal.rules,tesseract::PSM_AUTO);
      auto const reversedText=ReadRegion(reversed,flipped.rules,tesseract::PSM_AUTO);
      bool const normalTypeValid=deckkernel_camera::ocr::LooksLikeType(normalType.text);
      bool const reversedTypeValid=deckkernel_camera::ocr::LooksLikeType(reversedType.text);
      // Never invert a card solely because upside-down random OCR has a
      // superficially high confidence. Require a recognizable card type.
      int const normalScore=(normalName.text.empty()?0:normalName.confidence)+
         (normalTypeValid?100+normalType.confidence:0)+
         (normalText.text.empty()?0:normalText.confidence/2);
      int const reversedScore=(reversedName.text.empty()?0:reversedName.confidence)+
         (reversedTypeValid?100+reversedType.confidence:0)+
         (reversedText.text.empty()?0:reversedText.confidence/2);
      bool const flip=reversedTypeValid &&
         (!normalTypeValid || reversedScore>=normalScore+35) &&
         !reversedName.text.empty() &&
         reversedName.confidence>=50;
      if(m_verbose)
         std::println("[CAM-ROT] name={}/{} type-valid={}/{} "
            "score={}/{} chosen={}",
            normalName.confidence,reversedName.confidence,
            normalTypeValid,reversedTypeValid,normalScore,reversedScore,
            flip?180:0);
      if(flip)aCard=std::move(reversed);
      return flip;
   }
   std::string m_strLastName;
   std::string m_strLastRules;
   std::string m_strLastType;
   int m_typeConfidence{};
   int m_nameConfidence{};
   int m_rulesConfidence{};
   [[nodiscard]] bool LastAutoFlipped() const noexcept{return m_lastAutoFlipped;}
   [[nodiscard]] int NameConfidence() const noexcept{return m_nameConfidence;}
   [[nodiscard]] int RulesConfidence() const noexcept{return m_rulesConfidence;}
   [[nodiscard]] int TypeConfidence() const noexcept{return m_typeConfidence;}
   [[nodiscard]] std::string const& LastType() const noexcept{return m_strLastType;}
   [[nodiscard]] std::string const& LastName() const noexcept{return m_strLastName;}
   [[nodiscard]] std::string const& LastRules() const noexcept{return m_strLastRules;}
   /**
   \brief Recognize only the tiny printed set/collector-number region.
   \details Last automatic escalation after artwork and complete-card matching.
   Uses frozen pixels, never a continuously changing camera frame. No general
   card-name or rules OCR is involved.
   \param aBottom High-resolution lower 15 percent of the frozen card.
   \returns Raw recognized characters with OCR confidence for candidate
   validation. A low-confidence response must not identify a printing.
   \throw cv::Exception If image preprocessing fails.
   */
   [[nodiscard]] std::pair<std::string,int> ReadPrintingMargin(
      cv::Mat const& aBottom) {
      if(aBottom.empty())return {{},0};
      auto const aRead=ReadRegion(aBottom,
         cv::Rect(0,0,aBottom.cols,aBottom.rows),
         tesseract::PSM_SINGLE_LINE);
      return {aRead.text,aRead.confidence};
   }

   bool Read(cv::Mat const& card,unsigned eventNumber,unsigned attempt) {
      if(card.empty()||card.cols<100||card.rows<200)return false;
      auto zones=deckkernel_camera::ocr::Detect(card);
      auto const title=ReadRegion(card,zones.name,tesseract::PSM_SINGLE_LINE);
      // Standard frames may have decorative or broken border lines. Search
      // for the actual typeline text independently of border detection.
      // Search bands are broad; the selected type band is an OCR observation.
      OcrResult bestType;
      int bestTop=-1,bestBottom=-1;
      // Segment printed lines first, then evaluate each line as a possible
      // type. Repeated broad sliding windows mixed the type and rules text.
      cv::Rect const search(card.cols*57/1000,card.rows*44/100,
         card.cols*78/100,card.rows*32/100);
      cv::Mat searchGray;
      cv::cvtColor(card(search),searchGray,cv::COLOR_BGR2GRAY);
      m_api.SetPageSegMode(tesseract::PSM_AUTO);
      m_api.SetImage(searchGray.data,searchGray.cols,searchGray.rows,1,
         static_cast<int>(searchGray.step));
      if(m_api.Recognize(nullptr)==0) {
         std::unique_ptr<tesseract::ResultIterator> iterator(m_api.GetIterator());
         if(iterator) {
            do {
               int x0{},y0{},x1{},y1{};
               if(!iterator->BoundingBox(tesseract::RIL_TEXTLINE,&x0,&y0,&x1,&y1))
                  continue;
               std::unique_ptr<char[]> raw(iterator->GetUTF8Text(tesseract::RIL_TEXTLINE));
               std::string const text=raw?Clean(raw.get()):std::string{};
               if(!deckkernel_camera::ocr::LooksLikeType(text))continue;
               int const absTop=search.y+y0;
               int const absBottom=search.y+y1;
               int const confidence=static_cast<int>(
                  iterator->Confidence(tesseract::RIL_TEXTLINE));
               // Type is a single line just below the artwork. Prefer the
               // highest plausible line; longer rules are below it.
               if(bestTop<0 || absTop<bestTop ||
                  (absTop==bestTop && confidence>bestType.confidence)) {
                  bestTop=absTop;
                  bestBottom=absBottom;
                  bestType={text,confidence};
               }
            } while(iterator->Next(tesseract::RIL_TEXTLINE));
         }
      }
      if(bestTop>=0)
         deckkernel_camera::ocr::ApplyTypeTextLine(
            zones,card.size(),bestTop,bestBottom,
            static_cast<float>(bestType.confidence));
      // The rules are always read, independently of type confidence.
      auto const type=bestTop>=0?bestType:OcrResult{};
      auto const rules=ReadRegion(card,zones.rules,tesseract::PSM_AUTO);
      if(m_verbose)
         std::println("[CAM-ZONES] card #{} type={} confidence={} "
            "type-y={}..{} rules-y={}..{}",
            eventNumber,bestTop>=0?"detected":"unverified",
            type.confidence,zones.type.y,zones.type.br().y,
            zones.rules.y,zones.rules.br().y);
      m_strLastName=title.text;
      m_strLastType=bestTop>=0?type.text:"Type not yet recognized";
      m_strLastRules=rules.text;
      m_nameConfidence=title.confidence;
      m_typeConfidence=type.confidence;
      m_rulesConfidence=rules.confidence;
      if(m_verbose) {
         std::println("[CAM-OCR] card #{} attempt={} name=\"{}\" confidence={}",
            eventNumber,attempt,title.text,title.confidence);
         std::println("[CAM-OCR] card #{} attempt={} type=\"{}\" confidence={}",
            eventNumber,attempt,type.text,type.confidence);
         std::println("[CAM-OCR] card #{} attempt={} rules=\"{}\" confidence={}",
            eventNumber,attempt,rules.text,rules.confidence);
      }
      return !title.text.empty();
   }
   // OCR is performed in both orientations on accepted cards. The score
   // combines independent name/type/rules results; an upside-down fragment
   // must not win based solely on a plausible-looking name.
   bool ReadBest(cv::Mat& card,unsigned event,unsigned attempt,
      bool allowAutoFlip) {
      m_lastAutoFlipped=false;
      if(!allowAutoFlip)return Read(card,event,attempt);
      bool const original=Read(card,event,attempt);
      std::string name=m_strLastName,type=m_strLastType,rules=m_strLastRules;
      int const nameConf=m_nameConfidence,typeConf=m_typeConfidence,
         rulesConf=m_rulesConfidence;
      auto score=[](std::string const& n,int nc,
         std::string const& t,int tc,std::string const& r,int rc) {
         return (n.empty()?0:nc)+
            (deckkernel_camera::ocr::LooksLikeType(t)?120+tc:0)+
            (r.empty()?0:rc/2);
      };
      int const initial=score(name,nameConf,type,typeConf,rules,rulesConf);
      cv::Mat reversed;
      cv::rotate(card,reversed,cv::ROTATE_180);
      bool const alternative=Read(reversed,event,attempt);
      int const rotated=score(m_strLastName,m_nameConfidence,
         m_strLastType,m_typeConfidence,m_strLastRules,m_rulesConfidence);
      bool const accepted=alternative && rotated>=initial+25 &&
         deckkernel_camera::ocr::LooksLikeType(m_strLastType);
      if(accepted) {
         m_lastAutoFlipped=true;
         card=std::move(reversed);
         if(m_verbose)std::println("[CAM-ROT] selected 180 score {}>{}",
            rotated,initial);
         return alternative;
      }
      m_strLastName=std::move(name);
      m_strLastType=std::move(type);
      m_strLastRules=std::move(rules);
      m_nameConfidence=nameConf;
      m_typeConfidence=typeConf;
      m_rulesConfidence=rulesConf;
      if(m_verbose)std::println("[CAM-ROT] selected 0 score {} vs {}",
         initial,rotated);
      return original;
   }

};
#endif


// Coarse visual candidate detection only, NOT a definitive mana symbol reader.
// Hue distinguishes chromatic candidates; white/colorless and hybrid symbols
// require template verification against known Scryfall symbol graphics.
std::string ReportManaColorCandidates(cv::Mat const& card,
   unsigned event,bool verbose=false) {
   if(card.empty())return "No mana evidence";
   auto const region=deckkernel_camera::ocr::Detect(card).mana;
   cv::Mat roi=card(region),gray,blur;
   cv::cvtColor(roi,gray,cv::COLOR_BGR2GRAY);
   cv::GaussianBlur(gray,blur,cv::Size(5,5),1.3);
   std::vector<cv::Vec3f> circles;
   cv::HoughCircles(blur,circles,cv::HOUGH_GRADIENT,1.2,
      std::max(10,roi.cols/13),110,17,
      std::max(6,card.cols/85),std::max(12,card.cols/22));
   std::vector<cv::Vec3f> unique;
   for(auto const& candidate:circles) {
      bool duplicate=false;
      for(auto const& accepted:unique)
         if(cv::norm(cv::Point2f(candidate[0],candidate[1])-
            cv::Point2f(accepted[0],accepted[1]))<
            std::max(candidate[2],accepted[2])) {
            duplicate=true;break;
         }
      if(!duplicate)unique.push_back(candidate);
   }
   std::sort(unique.begin(),unique.end(),
      [](auto const& a,auto const& b){return a[0]<b[0];});
   if(verbose)
      std::println("[CAM-MANA] card #{} candidates={} status=UNVERIFIED",
         event,unique.size());
   return std::format("{} circular mana candidates (unverified)",
      unique.size());
}

class Processor {
   // PHASE 01/02: PhysicalCard owns the capture/release lifecycle.
   StableCardTrigger m_detector;
   deckkernel::camera::PhysicalCardTracker m_tracker;
   // PHASE 04/05: CardCapture owns quality and immutable image evidence.
   deckkernel::camera::CardCapture m_capture;
   // PHASE 06-10: CardIdentity owns Oracle identity and printing options.
   deckkernel::camera::CardIdentity m_identity;
   std::vector<deckkernel::camera::CollectedCard> m_vecCollected;
   bool m_boUserNavigated{};
   bool m_boArtworkRecognized{};
   unsigned m_uSelectionSerial{};
   deckkernel::camera::RecognitionPhase m_ePhase{
      deckkernel::camera::RecognitionPhase::Searching};
   bool m_save{};
   bool m_verbose{};
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
   std::unique_ptr<deckkernel::camera::CardImageMatcher> m_artwork;
#endif
   unsigned m_removedEvents{};
   unsigned m_liveFrames{},m_ocrAttempts{};
   double m_bestSharpness{};
   std::optional<unsigned> m_ocrCard;
   bool m_ocrAccepted{};
   bool m_autoFlipped{};
   bool m_manualOrientation{};
   bool m_boCapturedRotate180{};
   int m_iReferenceOrientation{};
   int m_bestNameConfidence{},m_bestTypeConfidence{},m_bestRulesConfidence{};
   std::string m_strName{"--"};
   std::string m_strRules{"--"};
   std::string m_strType{"--"};
   std::string m_strMana{"No mana evidence"};
   std::string m_strOrientation{"Automatic orientation pending"};
   cv::Mat m_displayCard;
   cv::Mat m_referenceCard;
   std::string m_strReferenceLabel{"No database match yet"};
   std::string m_strReferenceDiagnostic;
   std::string m_strSet;
   std::string m_strSetName;
   std::string m_strCollector;
   std::string m_strLanguage;
   std::string m_strRarity;
   std::string m_strPrintingId;
   std::string m_strOracleId;
   std::string m_strManaCost;
   std::string m_strStatus{"Waiting for card"};
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
   std::string m_strPrintingOcrData;
   std::future<std::pair<std::string,int>> m_printingOcr;
   unsigned m_uPrintingOcrEvent{};
#endif
   std::string m_strEdition{"Edition not verified"};
   bool m_recognitionReady{};
   bool m_cardLocked{};
   bool m_focusPending{};
   bool m_boFocusHintShown{};
   unsigned m_uFocusFrames{};
   double m_flCaptureScore{-1.0};
   double m_flCaptureSharpness{};
   std::chrono::steady_clock::time_point m_focusStarted{};
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
   std::unique_ptr<CardOcr> m_ocr;
#endif
   std::filesystem::path m_root;
public:
   explicit Processor(bool save,bool verbose=false):m_save(save),m_verbose(verbose),
      m_root(std::getenv("DECKKERNEL_CAMERA_EVIDENCE_ROOT")?
         std::getenv("DECKKERNEL_CAMERA_EVIDENCE_ROOT"):
         ".cache/camera-recognition") {}
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
   void EnableOcr(std::string const& data){m_ocr=std::make_unique<CardOcr>(data,m_verbose);}
   void EnablePrintingOcr(std::string const& data){
      m_strPrintingOcrData=data;
   }
#endif
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
   void EnableArtwork(unsigned limit){m_artwork=std::make_unique<deckkernel::camera::CardImageMatcher>(limit);}
#endif
   [[nodiscard]] unsigned RemovedEvents() const noexcept {return m_removedEvents;}
   [[nodiscard]] unsigned StableFrames() const noexcept{return m_detector.StableCount();}
   [[nodiscard]] unsigned RequiredStableFrames() const noexcept{return m_detector.RequiredFrames();}
   [[nodiscard]] std::string const& Name() const noexcept {return m_strName;}
   [[nodiscard]] std::string const& Rules() const noexcept {return m_strRules;}
   [[nodiscard]] std::string const& Type() const noexcept {return m_strType;}
   [[nodiscard]] std::string const& Status() const noexcept {return m_strStatus;}
   [[nodiscard]] std::string const& Edition() const noexcept {return m_strEdition;}
   [[nodiscard]] cv::Mat const& ReferenceCard() const noexcept {return m_referenceCard;}
   [[nodiscard]] std::string const& ReferenceLabel() const noexcept {return m_strReferenceLabel;}
   [[nodiscard]] std::string const& ReferenceDiagnostic() const noexcept {return m_strReferenceDiagnostic;}
   [[nodiscard]] std::string const& Set() const noexcept {return m_strSet;}
   [[nodiscard]] std::string const& SetName() const noexcept {return m_strSetName;}
   [[nodiscard]] std::string const& Collector() const noexcept {return m_strCollector;}
   [[nodiscard]] std::string const& Language() const noexcept {return m_strLanguage;}
   [[nodiscard]] std::string const& Rarity() const noexcept {return m_strRarity;}
   [[nodiscard]] std::string const& PrintingId() const noexcept {return m_strPrintingId;}
   [[nodiscard]] std::string const& OracleId() const noexcept {return m_strOracleId;}
   [[nodiscard]] std::string const& ManaCost() const noexcept {return m_strManaCost;}
   [[nodiscard]] bool CardLocked() const noexcept {return m_cardLocked;}
   [[nodiscard]] bool RecognitionPaused() const noexcept {
      return m_capture.Frozen();
   }
   [[nodiscard]] std::vector<deckkernel::camera::CollectedCard> const&
      CollectedCards() const noexcept {return m_vecCollected;}
   [[nodiscard]] deckkernel::camera::RecognitionPhase Phase() const noexcept {
      return m_ePhase;
   }
   void SetPhase(deckkernel::camera::RecognitionPhase ePhase) {
      if(m_ePhase==ePhase)return;
      std::println("[CAM-PHASE] {} -> {}",
         deckkernel::camera::PhaseName(m_ePhase),
         deckkernel::camera::PhaseName(ePhase));
      m_ePhase=ePhase;
   }
   [[nodiscard]] std::size_t CandidateCount() const noexcept {
      return m_identity.Count();
   }
   [[nodiscard]] std::size_t CandidateIndex() const noexcept {
      return m_identity.Index();
   }
   [[nodiscard]] bool SelectionConfirmed() const noexcept {
      return m_identity.Confirmed();
   }
   void ApplySelectedPrinting() {
      auto const* pCandidate=m_identity.Selected();
      if(!pCandidate)return;
      m_strPrintingId=pCandidate->strPrintingId;
      m_strSet=pCandidate->strSet;
      m_strSetName=pCandidate->strSetName;
      m_strCollector=pCandidate->strCollector;
      m_strLanguage=pCandidate->strLanguage;
      m_strRarity=pCandidate->strRarity;
      m_strOracleId=pCandidate->strOracleId;
      m_strManaCost=pCandidate->strManaCost;
      if(!pCandidate->strName.empty())m_strName=pCandidate->strName;
      if(!pCandidate->strTypeLine.empty())m_strType=pCandidate->strTypeLine;
   }
   void NavigateCandidate(int iDelta) {
      // PHASE 09: switch database candidates, never reopen the physical search.
      if(!m_ocrCard || !m_identity.Navigate(iDelta))return;
      m_boUserNavigated=true;
      ++m_uSelectionSerial;
      ApplySelectedPrinting();
      // Never show a previous printing image under the new candidate label.
      m_referenceCard.release();
      m_strReferenceLabel="Loading selected DB printing...";
      m_strReferenceDiagnostic.clear();
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
      if(m_artwork && m_identity.Selected())
         m_artwork->Select(*m_identity.Selected(),*m_ocrCard,
            m_uSelectionSerial);
#endif
      SetPhase(deckkernel::camera::RecognitionPhase::Selecting);
      m_strStatus=std::format("PRINTING SELECT {}/{} - press Enter",
         m_identity.Index()+1,m_identity.Count());
   }
   /**
   \brief Reset only the current scan while retaining collected cards.
   \details Camera focus calibration is restarted by the caller, and late
      asynchronous image results remain isolated by their event number.
   \returns Nothing.
   \throw None.
   */
   void ResetScan() {
      m_detector.Reset();
      m_tracker.Reset();
      m_capture.Clear();
      m_identity.Clear();
      m_ocrCard.reset();
      m_cardLocked=false;m_focusPending=false;m_recognitionReady=false;
      m_boFocusHintShown=false;m_boUserNavigated=false;
      m_boArtworkRecognized=false;
      m_uSelectionSerial=0;m_uFocusFrames=0;
      m_flCaptureScore=-1.0;m_flCaptureSharpness=0.0;
      m_bestNameConfidence=0;m_bestTypeConfidence=0;
      m_bestRulesConfidence=0;
      m_liveFrames=0;m_ocrAttempts=0;m_ocrAccepted=false;
      m_bestSharpness=0.0;m_autoFlipped=false;
      m_boCapturedRotate180=false;m_iReferenceOrientation=0;
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      m_uPrintingOcrEvent=0;
#endif
      m_strName="--";m_strType="--";m_strRules="--";
      m_strMana="No mana evidence";
      m_strOrientation="Waiting for card orientation";
      m_displayCard.release();m_referenceCard.release();
      m_strReferenceLabel="No database match yet";
      m_strReferenceDiagnostic.clear();
      m_strSet.clear();m_strSetName.clear();m_strCollector.clear();
      m_strLanguage.clear();m_strRarity.clear();m_strPrintingId.clear();
      m_strOracleId.clear();m_strManaCost.clear();
      m_strEdition="Edition not verified";
      SetPhase(deckkernel::camera::RecognitionPhase::Searching);
   }

   /**
   \brief Accept the selected printing and start a fresh scan.
   \details Confirmation explicitly records a user choice, not an
      additional verified image match. The list survives ResetScan().
   \returns True if a concrete printing was added to the list.
   \throw std::bad_alloc If adding the list entry fails.
   */
   [[nodiscard]] bool ConfirmCandidate() {
      if(!m_capture.Frozen() || !m_recognitionReady ||
         !m_boArtworkRecognized ||
         !m_identity.Selected() || !m_identity.Confirm()) {
         if(m_capture.Frozen())
            m_strStatus="ARTWORK/PRINTING NOT READY - wait or discard";
         return false;
      }
      ApplySelectedPrinting();
      auto const& aSelected=*m_identity.Selected();
      m_vecCollected.push_back(deckkernel::camera::CollectedCard{
         aSelected.strName,aSelected.strSet,aSelected.strSetName,
         aSelected.strCollector,aSelected.strPrintingId,
         aSelected.strOracleId});
      std::println("[CAM-COLLECT] added #{} name={} set={} collector={} "
         "printing={}",m_vecCollected.size(),aSelected.strName,
         aSelected.strSet,aSelected.strCollector,aSelected.strPrintingId);
      std::string const strName=aSelected.strName;
      ResetScan();
      m_strStatus=std::format("ADDED {} - focus table for next card",strName);
      return true;
   }

   /**
   \brief Discard the active recognition and return to tabletop focusing.
   \details Does not modify previously accepted cards.
   \returns True when a frozen or tracked scan was discarded.
   \throw None.
   */
   [[nodiscard]] bool DiscardScan() {
      std::println("[CAM-COLLECT] discarded event={} collected={}",
         m_ocrCard.value_or(0),m_vecCollected.size());
      ResetScan();
      m_strStatus="SCAN DISCARDED - focus table for next card";
      return true;
   }
   [[nodiscard]] bool Focusing() const noexcept {return m_focusPending;}
   [[nodiscard]] bool TrackingActive() const noexcept {
      return m_tracker.Active();
   }
   [[nodiscard]] bool IdentificationReady() const noexcept {return m_recognitionReady;}
   [[nodiscard]] std::string Guidance() const {
      if(!m_cardLocked)return "Waiting for a card in the camera";
      if(m_focusPending)return m_boFocusHintShown?
         "Move card closer or adjust focus; physical card remains locked":
         "Autofocus settling; selecting the sharpest frame";
      if(m_identity.Count()>0 && m_recognitionReady &&
         m_boArtworkRecognized)
         return std::format(
            "Left/right or wheel: printing {}/{} | Enter/click: add | Esc/right-click: discard",
            m_identity.Index()+1,m_identity.Count());
      if(!m_recognitionReady)
         return "Image search active; Esc/right-click discards this scan";
      return "Artwork or printing not established; Esc/right-click to rescan";
   }
   /**
   \brief Normalize printed set codes and collector numbers for OCR comparison.
   \details Only ASCII alphanumeric candidates are compared. Original
   Scryfall data remains unchanged in the identity model.
   \param svValue OCR or DB source string.
   \returns Uppercase alphanumeric comparison text.
   \throw std::bad_alloc If allocation fails.
   */
   [[nodiscard]] static std::string NormalizePrintingText(
      std::string_view svValue) {
      std::string strResult;
      for(unsigned char const ch:svValue) {
         if(ch>='a'&&ch<='z')strResult.push_back(static_cast<char>(ch-'a'+'A'));
         else if((ch>='A'&&ch<='Z')||(ch>='0'&&ch<='9'))
            strResult.push_back(static_cast<char>(ch));
      }
      return strResult;
   }

   /**
   \brief Escalate ambiguous printings to frozen lower-margin OCR.
   \details Tesseract runs in its own thread; the camera and physical tracker
   remain responsive and unchanged.
   \returns Nothing.
   \throw std::system_error If OCR worker creation fails.
   */
   void StartPrintingOcr() {
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(m_strPrintingOcrData.empty() || m_identity.Count()<2 ||
         m_capture.Bottom().empty() || m_printingOcr.valid())return;
      // PHASE 08: use the reference-confirmed orientation of the frozen
      // display copy. CardCapture itself remains immutable throughout.
      if(m_displayCard.empty())return;
      int const iTop=m_displayCard.rows*86/100;
      int const iBottom=m_displayCard.rows*99/100;
      cv::Mat const aMargin=m_displayCard(cv::Rect(
         0,iTop,m_displayCard.cols,iBottom-iTop)).clone();
      std::string const strData=m_strPrintingOcrData;
      m_uPrintingOcrEvent=m_ocrCard.value_or(0);
      // PHASE 08: OCR is a last-resort, read-only consumer of frozen evidence.
      SetPhase(deckkernel::camera::RecognitionPhase::PrintingOcr);
      m_strStatus="PRINTING AMBIGUOUS - reading frozen lower margin";
      m_printingOcr=std::async(std::launch::async,
         [aMargin,strData]() -> std::pair<std::string,int> {
            CardOcr aOcr(strData);
            return aOcr.ReadPrintingMargin(aMargin);
         });
#endif
   }

   /**
   \brief Compare a completed lower-margin OCR result to known DB candidates.
   \details Require both set code and collector number and sufficient OCR
   confidence. OCR does not invent or eliminate database printings.
   \returns Nothing.
   \throw std::runtime_error If the asynchronous OCR worker failed.
   */
   void PollPrintingOcr() {
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(!m_printingOcr.valid() ||
         m_printingOcr.wait_for(std::chrono::seconds(0))!=
            std::future_status::ready)return;
      std::pair<std::string,int> aRead;
      try {
         aRead=m_printingOcr.get();
      }
      catch(std::exception const& aException) {
         if(m_ocrCard && *m_ocrCard==m_uPrintingOcrEvent &&
            !m_identity.Confirmed()) {
            SetPhase(deckkernel::camera::RecognitionPhase::Selecting);
            m_strStatus=std::format("PRINTING OCR unavailable: {}",
               aException.what());
         }
         else if(m_ocrCard && !m_identity.Confirmed() &&
            !m_boUserNavigated && m_identity.Count()>1)
            StartPrintingOcr();
         return;
      }
      auto const& [strRecognized,iConfidence]=aRead;
      if(!m_ocrCard || *m_ocrCard!=m_uPrintingOcrEvent ||
         m_identity.Confirmed() || m_boUserNavigated) {
         // A discarded scan may still finish OCR after the new event has
         // started. Drain that future, then start the NEW card's OCR if
         // it was delayed by the previous valid future.
         if(m_ocrCard && !m_identity.Confirmed() &&
            !m_boUserNavigated && m_identity.Count()>1)
            StartPrintingOcr();
         return;
      }
      std::string const strNormalized=NormalizePrintingText(strRecognized);
      std::optional<std::size_t> optMatch;
      bool boMultiple{};
      if(iConfidence>=65) {
         auto const& vecCandidates=m_identity.Candidates();
         for(std::size_t uIndex=0;uIndex<vecCandidates.size();++uIndex) {
            auto const& aCandidate=vecCandidates[uIndex];
            std::string const strSet=NormalizePrintingText(aCandidate.strSet);
            std::string const strNumber=NormalizePrintingText(
               aCandidate.strCollector);
            if(strSet.size()<3||strNumber.empty()||
               strNormalized.find(strSet)==std::string::npos ||
               strNormalized.find(strNumber)==std::string::npos)continue;
            if(optMatch)boMultiple=true;
            else optMatch=uIndex;
         }
      }
      if(optMatch && !boMultiple) {
         // OCR corroboration is evidence for one candidate, not verification.
         static_cast<void>(m_identity.SelectAt(*optMatch));
         ApplySelectedPrinting();
         m_referenceCard.release();
         m_strReferenceLabel="Loading OCR-supported DB printing...";
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
         if(m_artwork && m_identity.Selected())
            m_artwork->Select(*m_identity.Selected(),*m_ocrCard,
               ++m_uSelectionSerial);
#endif
         m_strStatus=std::format(
            "PRINTING OCR SUPPORTS {}/{} - Enter to confirm",
            *optMatch+1,m_identity.Count());
      }
      else {
         m_strStatus=std::format(
            "PRINTING OCR inconclusive (confidence {}) - select with arrows",
            iConfidence);
      }
      SetPhase(deckkernel::camera::RecognitionPhase::Selecting);
      std::println("[CAM-PRINT-OCR] card={} confidence={} text='{}' "
         "unique_candidate={}",
         *m_ocrCard,iConfidence,strRecognized,
         optMatch && !boMultiple?std::to_string(*optMatch+1):"none");
#endif
   }

   void PollRecognition() {
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
      if(!m_artwork)return;
      auto result=m_artwork->Poll();
      if(!result || !m_ocrCard || result->uEvent!=*m_ocrCard)return;
      // Discard late DB-image responses from an older arrow-key position.
      if(result->boCandidateSelection &&
         result->uSelectionSerial!=m_uSelectionSerial)return;
      // PHASE 06: the image matcher may contradict the frame-layout
      // orientation. Correct only the presentation copy, preserving
      // CardCapture's immutable pixel evidence and its tracking geometry.
      if(!result->boCandidateSelection && !result->strAssetId.empty() &&
         m_capture.Frozen() &&
         result->iOrientation!=m_iReferenceOrientation) {
         m_iReferenceOrientation=result->iOrientation;
         m_displayCard=m_capture.Overscan().clone();
         if(m_iReferenceOrientation==180)
            cv::rotate(m_displayCard,m_displayCard,cv::ROTATE_180);
         m_strOrientation=std::format(
            "Frame: {} deg | reference: {} deg",
            m_boCapturedRotate180?180:0,m_iReferenceOrientation);
         std::println("[CAM-ORIENTATION] card={} matcher={} deg "
            "corrected frozen display copy",
            result->uEvent,m_iReferenceOrientation);
      }
      // PHASE 06/07: accept only events for the active physical card.
      // PHASE 09: loading a new reference never restarts recognition.
      if(m_identity.Confirmed()) {
         // Enter may occur before an asynchronous reference download ends.
         // Accept the image for the confirmed selection, but never replace
         // its confirmed status or return to automatic recognition.
         if(result->boCandidateSelection) {
            if(!result->aReferenceImage.empty())
               m_referenceCard=std::move(result->aReferenceImage);
            if(!result->strReferenceLabel.empty())
               m_strReferenceLabel=result->strReferenceLabel;
            if(!result->strReferenceDiagnostic.empty())
               m_strReferenceDiagnostic=result->strReferenceDiagnostic;
         }
         return;
      }
      if(!result->boCandidateSelection && result->boArtworkRecognized)
         m_boArtworkRecognized=true;
      m_strStatus=result->strStatus;
      m_strEdition=result->strEdition;
      if(!result->boCandidateSelection)
         m_recognitionReady=result->boComplete;
      if(!result->vecCandidates.empty()) {
         m_identity.SetCandidates(std::move(result->vecCandidates),
            result->boCandidatesTruncated);
         SetPhase(m_identity.Count()>1 || m_identity.Truncated()?
            deckkernel::camera::RecognitionPhase::Selecting:
            deckkernel::camera::RecognitionPhase::Printing);
         // PHASE 08 occurs only when complete-card comparison left alternatives.
         StartPrintingOcr();
      }
      else if(!result->boComplete)
         SetPhase(result->strStatus.find("ARTWORK")!=std::string::npos?
            deckkernel::camera::RecognitionPhase::Artwork:
            deckkernel::camera::RecognitionPhase::Printing);
      else if(m_identity.Count()==0 && !result->boCandidateSelection)
         SetPhase(deckkernel::camera::RecognitionPhase::Unresolved);
      if(!result->strName.empty())m_strName=result->strName;
      if(!result->strTypeLine.empty())m_strType=result->strTypeLine;
      if(!result->strManaCost.empty())m_strManaCost=result->strManaCost;
      m_strSet=result->strSet;
      m_strSetName=result->strSetName;
      m_strCollector=result->strCollector;
      m_strLanguage=result->strLanguage;
      m_strRarity=result->strRarity;
      m_strPrintingId=result->strPrintingId;
      m_strOracleId=result->strOracleId;
      if(!result->strReferenceLabel.empty())
         m_strReferenceLabel=result->strReferenceLabel;
      if(!result->strReferenceDiagnostic.empty())
         m_strReferenceDiagnostic=result->strReferenceDiagnostic;
      if(!result->aReferenceImage.empty())
         m_referenceCard=std::move(result->aReferenceImage);
      std::println("[CAM-MATCH] card #{} status={} edition={}",
         result->uEvent,m_strStatus,m_strEdition);
#endif
   }
   [[nodiscard]] std::string const& Mana() const noexcept {return m_strMana;}
   [[nodiscard]] std::string const& Orientation() const noexcept{return m_strOrientation;}
   [[nodiscard]] cv::Mat const& DisplayCard() const noexcept{return m_displayCard;}
   void ClearDisplayCard() {m_displayCard.release();}
   void MergeOcr() {
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(!m_ocr)return;
      auto merge=[](std::string& current,int& best,
         std::string const& candidate,int confidence) {
         if(candidate.empty() || confidence<45)return;
         if(current=="--" || confidence>best+3 ||
            (confidence>=best && candidate.size()>current.size() &&
               candidate.size()<=current.size()+20)) {
            current=candidate;
            best=confidence;
         }
      };
      merge(m_strName,m_bestNameConfidence,
         m_ocr->LastName(),m_ocr->NameConfidence());
      if(deckkernel_camera::ocr::LooksLikeType(m_ocr->LastType()))
         merge(m_strType,m_bestTypeConfidence,
            m_ocr->LastType(),m_ocr->TypeConfidence());
      merge(m_strRules,m_bestRulesConfidence,
         m_ocr->LastRules(),m_ocr->RulesConfidence());
      // OCR is supplemental metadata, never replace the recognition lifecycle status.
#endif
   }

   void ReevaluateOcrAfterRotation(bool manualFlip) noexcept {
      // PHASE 05 invariant: R must never change a frozen card or the active
      // physical object's captured evidence. The matcher tests both rotations.
      if(m_cardLocked)return;
      m_manualOrientation=manualFlip;
      // Changing the orientation must be allowed to rerun OCR even when a
      // previous, wrong-side-up title passed the provisional confidence test.
      m_ocrAccepted=false;
      m_ocrAttempts=0;
      m_liveFrames=90;
      m_bestSharpness=0;
      // Keep the active presentation event: a manual rotation must cause
      // another OCR pass without requiring the user to remove the card.
      m_autoFlipped=false;
      if(!m_displayCard.empty())
         cv::rotate(m_displayCard,m_displayCard,cv::ROTATE_180);
      m_strOrientation=manualFlip?"Manual 180 deg":"Manual 0 deg";
   }
   /**
   \brief Accumulate the sharpest full-resolution perspective-rectified frame.
   \details The autofocus device may respond slowly; image quality is measured
   on the actual card, and matching starts only after settling or timeout.
   \param pCandidate Detected card geometry or nullptr on a missing observation.
   \param aSource Original camera frame, if available.
   \returns Nothing.
   \throw cv::Exception If perspective transformation fails.
   */
   void EvaluateFocus(FoundCard const* pCandidate,
      cv::Mat const& aSource) {
      if(!m_focusPending)return;
      if(pCandidate) {
         cv::Mat aImage=aSource.empty()?pCandidate->rectified.clone():
            RectifyForOcr(aSource,pCandidate->corners,
               m_boCapturedRotate180 != m_manualOrientation);
         // PHASE 04: the card was already acquired as a physical object.
         // An uncertain internal layout must not prevent quality sampling.
         // The tracker owns the four outer corners, not artwork detection.
         // Check that we are still tracking approximately the same full-card
         // footprint before offering a new registered frame.
         cv::Mat const aExtended=aSource.empty()?cv::Mat{}:
            RectifyWithMargin(aSource,pCandidate->corners,
               m_boCapturedRotate180 != m_manualOrientation);
         if(m_capture.Offer(aImage,
            NativeCardWidth(pCandidate->corners),aExtended)) {
            m_displayCard=m_capture.Overscan();
            if(m_boFocusHintShown)
               m_strStatus="FOCUSING - received an improved card frame";
         }
         m_flCaptureScore=m_capture.Score();
         m_flCaptureSharpness=m_capture.Sharpness();
         m_uFocusFrames=m_capture.Samples();
      }
      auto const flSeconds=std::chrono::duration<double>(
         std::chrono::steady_clock::now()-m_focusStarted).count();
      // Complete after enough valid observations or a bounded wall-clock timeout.
      if(!m_capture.Ready()) {
         // PHASE 04: retain the SAME tracked card while focus improves.
         // The image is not frozen, and no Scryfall search starts on blur.
         if(m_capture.NeedsBetterFocusHint() && !m_boFocusHintShown) {
            m_boFocusHintShown=true;
            auto const& aQuality=m_capture.Quality();
            m_strStatus=std::format(
               "FOCUS INSUFFICIENT - native width {}px; "
               "detail {:.0f}; move card closer or adjust camera focus",
               aQuality.sourceWidth,aQuality.detailSharpness);
            std::println("[CAM-FOCUS] card={} waiting for sharp image "
               "native={}x{} detail={:.1f} blur={:.1f} clipped={:.1f}%",
               *m_ocrCard,aQuality.sourceWidth,aQuality.sourceHeight,
               aQuality.detailSharpness,aQuality.sharpness,
               aQuality.clippedPercent);
         }
         return;
      }
      m_focusPending=false;
      // PHASE 05: freeze once. No recognition stage may mutate these pixels.
      if(!m_capture.Freeze()) {
         SetPhase(deckkernel::camera::RecognitionPhase::Unresolved);
         m_strStatus="CAPTURE FAILED - no usable card image";
         return;
      }
      m_displayCard=m_capture.Overscan().clone();
      SetPhase(deckkernel::camera::RecognitionPhase::Frozen);
      if(m_save) {
         std::filesystem::create_directories(m_root);
         auto const aPath=m_root/std::format(
            "event-{:06}-frozen-search-input.png",*m_ocrCard);
         if(!cv::imwrite(aPath.string(),m_capture.Image()))
            std::println(std::cerr,
               "[CAM-EVIDENCE] could not save frozen image {}",
               aPath.string());
         else
            std::println("[CAM-EVIDENCE] frozen-search-input={}",
               aPath.string());
         auto const aOverscanPath=m_root/std::format(
            "event-{:06}-frozen-overscan.png",*m_ocrCard);
         if(!cv::imwrite(aOverscanPath.string(),m_capture.Overscan()))
            std::println(std::cerr,
               "[CAM-EVIDENCE] could not save overscan {}",
               aOverscanPath.string());
         else
            std::println("[CAM-EVIDENCE] frozen-overscan={}",
               aOverscanPath.string());
      }
      auto const& aQuality=m_capture.Quality();
      std::println("[CAM-QUALITY] card={} source={}x{} rectified={}x{} "
         "sharpness={:.1f} printed-detail={:.1f} brightness={:.1f} "
         "clipped={:.1f}% usable={}",
         *m_ocrCard,aQuality.sourceWidth,aQuality.sourceHeight,
         m_displayCard.cols,m_displayCard.rows,
         aQuality.sharpness,aQuality.detailSharpness,aQuality.brightness,
         aQuality.clippedPercent,m_capture.UsableForMatching());
      if(!m_capture.UsableForMatching()) {
         // PHASE 05: expose low confidence, but do NOT block image matching.
         // Earlier code froze a real, readable card then skipped the entire
         // artwork/printing pipeline because of an uncalibrated threshold.
         std::println("[CAM-QUALITY] card={} below provisional threshold; "
            "continuing visual search with low-confidence capture",
            *m_ocrCard);
         m_strEdition="Low-quality capture; match results need caution";
      }
      m_strStatus=std::format(
         "CARD CAPTURED - focus selected (sharpness {:.1f}, {} frames)",
         m_flCaptureSharpness,m_uFocusFrames);
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
      if(m_artwork) {
         m_strStatus=m_capture.UsableForMatching()?
            "FROZEN CARD - searching indexed artwork":
            "LOW-QUALITY FROZEN CARD - trying indexed artwork";
         SetPhase(deckkernel::camera::RecognitionPhase::Artwork);
         // PHASE 06: keep the frozen full card intact. The matcher owns
         // artwork isolation and, when --save-events is active, exports
         // both measured and fallback artwork crops for debugging.
         m_artwork->Search(m_capture.Image(),*m_ocrCard,
            m_save?m_root:std::filesystem::path{});
      }
      else {
         m_strStatus="SHARP FRAME SELECTED - image matcher disabled";
         m_recognitionReady=true;
      }
#else
      m_strStatus="SHARP FRAME SELECTED - no image matcher compiled";
      m_recognitionReady=true;
#endif
      std::println("[CAM-FOCUS] card #{} frames={} seconds={:.2f} "
         "sharpness={:.2f} score={:.2f}",
         *m_ocrCard,m_uFocusFrames,flSeconds,
         m_flCaptureSharpness,m_flCaptureScore);
   }

   /**
   \brief Determine the next observation without rediscovering inner art.
   \details PHASE 01 uses global card contours only when there is no active
   physical card. PHASE 03 uses only optical flow from the existing object.
   A lost track is reported as absent; it never selects a new rectangle
   inside the old card during the same presentation.
   \param aFrame Original camera pixels.
   \returns A card observation or none when tracking is temporarily lost.
   \throw cv::Exception If the OpenCV tracker fails.
   */
   [[nodiscard]] std::optional<FoundCard> Observe(cv::Mat const& aFrame,
      CardDetectionDiagnostics* pDiagnostics=nullptr) {
      if(pDiagnostics)*pDiagnostics={};
      if(!m_tracker.Active()) {
         auto optCard=FindCard(aFrame,pDiagnostics,true);
         // Layout is only advisory; acquire a geometrically credible card
         // even when the internal frame is blurred or nonstandard.
         if(!optCard && !m_cardLocked)
            m_strStatus="SEARCHING - waiting for a stable outer card contour";
         return optCard;
      }
      auto optCorners=m_tracker.Track(aFrame);
      if(!optCorners)return std::nullopt;
      FoundCard aCard;
      for(std::size_t uIndex=0;uIndex<4;++uIndex)
         aCard.corners[uIndex]=cv::Point(
            cvRound((*optCorners)[uIndex].x),cvRound((*optCorners)[uIndex].y));
      aCard.rectified=RectifyForOcr(aFrame,aCard.corners,false);
      aCard.fingerprint=DHash(aCard.rectified);
      aCard.boRotate180=m_boCapturedRotate180;
      return aCard;
   }

   // Offline replay and synthetic contour tests retain independent
   // detections. The live camera explicitly calls Observe(frame) before
   // Process(found,frame), enabling physical optical-flow tracking.
   bool Process(cv::Mat const& frame) {
      return Process(FindCard(frame));
   }
   bool Process(std::optional<FoundCard> const& found,
      cv::Mat const& sourceFrame={},bool boTrustedTracking=false) {
      auto event=m_detector.Accept(found,boTrustedTracking);
      if(m_focusPending && m_ocrCard)
         EvaluateFocus(found &&
            (boTrustedTracking || m_detector.SameCardPosition(*found))?
            &*found:nullptr,sourceFrame);
      // The sharpest accepted card is captured once. Do not continuously
      // rewrite OCR text while the viewer is inspecting the captured card.
      if(found && m_ocrCard && !m_cardLocked && !m_recognitionReady) {
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
         if(m_ocr && m_ocrAttempts<4) {
            ++m_liveFrames;
            if(m_liveFrames>=30) {
               cv::Mat gray;
               cv::cvtColor(found->rectified,gray,cv::COLOR_BGR2GRAY);
               cv::Mat lap;
               cv::Laplacian(gray,lap,CV_64F);
               cv::Scalar mean,dev;
               cv::meanStdDev(lap,mean,dev);
               double const sharpness=dev[0]*dev[0];
               if(sharpness>m_bestSharpness*1.10 || m_liveFrames>=90) {
                  m_liveFrames=0;
                  m_bestSharpness=std::max(m_bestSharpness,sharpness);
                  ++m_ocrAttempts;
                  cv::Mat aCandidate;
                  if(!sourceFrame.empty())
                     aCandidate=RectifyForOcr(sourceFrame,found->corners,
                        m_manualOrientation);
                  else aCandidate=found->rectified.clone();
                  if(m_autoFlipped)
                     cv::rotate(aCandidate,aCandidate,cv::ROTATE_180);
                  m_ocrAccepted=m_ocr->Read(aCandidate,
                     *m_ocrCard,m_ocrAttempts);
                  MergeOcr();
               }
            }
         }
#endif
      }
      if(!event) {
         // PHASE 01/02: maintain ACQUIRING through a few missed frames
         // instead of flashing SEARCHING/ACQUIRING with every blur event.
         if(!m_cardLocked)SetPhase(m_detector.StableCount()>0?
            deckkernel::camera::RecognitionPhase::Acquiring:
            deckkernel::camera::RecognitionPhase::Searching);
         return false;
      }
      if(event->kind==StableCardTrigger::EventKind::Removed) {
         ++m_removedEvents;
         m_ocrCard.reset();m_ocrAccepted=false;m_ocrAttempts=0;
         m_bestNameConfidence=0;m_bestTypeConfidence=0;
         m_bestRulesConfidence=0;
         m_liveFrames=0;m_bestSharpness=0;m_autoFlipped=false;
         m_strStatus="Card removed - waiting for next card";
         m_strEdition="Edition not verified";
         m_recognitionReady=false;
         m_cardLocked=false;
         m_focusPending=false;
         m_boFocusHintShown=false;
         m_uFocusFrames=0;
         m_flCaptureScore=-1.0;
         m_capture.Clear();
         m_identity.Clear();
         m_boUserNavigated=false;
         m_boArtworkRecognized=false;
         m_uSelectionSerial=0;
         m_tracker.Reset();
         m_boCapturedRotate180=false;
         m_iReferenceOrientation=0;
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
         m_uPrintingOcrEvent=0;
#endif
         SetPhase(deckkernel::camera::RecognitionPhase::Searching);
         m_strName="--";m_strType="--";m_strRules="--";
         m_strMana="No mana evidence";
         m_strOrientation="Waiting for card orientation";
         m_displayCard.release();
         m_referenceCard.release();
         m_strReferenceLabel="No database match yet";
         m_strReferenceDiagnostic.clear();
         m_strSet.clear();m_strSetName.clear();m_strCollector.clear();
         m_strLanguage.clear();m_strRarity.clear();m_strPrintingId.clear();
         m_strOracleId.clear();m_strManaCost.clear();
         std::println("[CAM] CARD-NO-LONGER-DETECTED #{} fingerprint={:016x}; "
            "ready-for-next-card",
            event->number,event->fingerprint);
         return false;
      }
      std::println("[CAM] NEW-STABLE-CARD #{} fingerprint={:016x}; edition=UNKNOWN",
         event->number,event->fingerprint);
      m_iReferenceOrientation=0;
      if(found) {
         m_boCapturedRotate180=found->boRotate180;
         m_strOrientation=found->boLayoutRecognized?
            std::format("Standard frame: auto {} deg",
               m_boCapturedRotate180?180:0):
            "Orientation uncertain - reference search will test both";
         std::println("[CAM-ORIENTATION] card={} layout={} initial={} deg",
            event->number,found->boLayoutRecognized?"standard":"uncertain",
            m_boCapturedRotate180?180:0);
      }
      if(!sourceFrame.empty() && found)
         event->card=RectifyForOcr(sourceFrame,found->corners,
            m_boCapturedRotate180 != m_manualOrientation);
      m_ocrCard=event->number;
      // PHASE 03: lock the outer physical card before examining its artwork.
      if(!sourceFrame.empty() && found) {
         std::array<cv::Point2f,4> arrCorners{};
         for(std::size_t uIndex=0;uIndex<4;++uIndex)
            arrCorners[uIndex]=cv::Point2f(
               static_cast<float>(found->corners[uIndex].x),
               static_cast<float>(found->corners[uIndex].y));
         if(!m_tracker.Begin(sourceFrame,arrCorners))
            std::println("[CAM-TRACK] insufficient full-card optical-flow features");
      }
      m_strStatus="CARD CAPTURED - preparing identification";
      m_strEdition="Edition not verified";
      m_recognitionReady=false;
      m_cardLocked=true;
      m_focusPending=true;
      m_boFocusHintShown=false;
      m_uFocusFrames=0;
      m_flCaptureScore=-1.0;
      m_flCaptureSharpness=0.0;
      m_focusStarted=std::chrono::steady_clock::now();
      m_identity.Clear();
      m_boUserNavigated=false;
      m_boArtworkRecognized=false;
      m_uSelectionSerial=0;
      SetPhase(deckkernel::camera::RecognitionPhase::Tracking);
      SetPhase(deckkernel::camera::RecognitionPhase::Focusing);
      m_strName="--";m_strType="--";m_strRules="--";
      m_referenceCard.release();
      m_strReferenceLabel="Searching Scryfall references...";
      m_strReferenceDiagnostic.clear();
      m_strSet.clear();m_strSetName.clear();m_strCollector.clear();
      m_strLanguage.clear();m_strRarity.clear();m_strPrintingId.clear();
      m_strOracleId.clear();m_strManaCost.clear();
      m_bestNameConfidence=0;m_bestTypeConfidence=0;m_bestRulesConfidence=0;
      m_liveFrames=0;m_bestSharpness=0;m_ocrAttempts=0;m_ocrAccepted=false;
      m_autoFlipped=false;
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(m_ocr) {
         m_ocrAttempts=1;
         m_ocrAccepted=m_ocr->ReadBest(event->card,event->number,
            m_ocrAttempts,!m_manualOrientation);
         m_autoFlipped=m_ocr->LastAutoFlipped();
         m_strOrientation=m_manualOrientation?"Manual orientation":
            (m_autoFlipped?"Auto 180 deg (both tested)":"Auto 0 deg (both tested)");
         MergeOcr();
      }
#endif
      // Mana/OCR diagnostics remain opt-in and are never needed for matching.
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(m_ocr)m_strMana=ReportManaColorCandidates(event->card,event->number,m_verbose);
#endif
      // PHASE 04: initially collect the normalized card, then sharper frames.
      cv::Mat const aInitialOverscan=
         (!sourceFrame.empty() && found)?
            RectifyWithMargin(sourceFrame,found->corners,
               m_boCapturedRotate180 != m_manualOrientation):cv::Mat{};
      m_capture.Begin(event->card,
         found?NativeCardWidth(found->corners):event->card.cols,
         aInitialOverscan);
      m_displayCard=m_capture.Overscan();
      m_flCaptureScore=m_capture.Score();
      m_flCaptureSharpness=m_capture.Sharpness();
      m_uFocusFrames=m_capture.Samples();
      if(m_save) {
         auto const zones=deckkernel_camera::ocr::Detect(event->card);
         if(!deckkernel_camera::ocr::Valid(zones,event->card.size()))
            throw std::runtime_error("invalid/non-disjoint OCR zones");
         std::filesystem::create_directories(m_root);
         auto saveZone=[&](char const* label,cv::Rect region) {
            auto const file=m_root/std::format("event-{:06}-{}.png",event->number,label);
            if(!cv::imwrite(file.string(),event->card(region)))
               throw std::runtime_error("cannot save OCR zone");
         };
         saveZone("name",zones.name);
         saveZone("mana",zones.mana);
         saveZone("type",zones.type);
         saveZone("set-symbol",zones.setSymbol);
         saveZone("rules",zones.rules);
         auto const file=m_root/std::format("event-{:06}-zones.png",event->number);
         cv::imwrite(file.string(),deckkernel_camera::ocr::Diagram(event->card,zones));
      }
      m_strStatus="CARD CAPTURED - waiting for autofocus / sharper input";
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
      if(!m_artwork)
         m_strEdition="Image matcher disabled";
#endif
      if(m_save){
         std::filesystem::create_directories(m_root);
         auto const name=m_root/std::format("event-{:06}.png",event->number);
         if(!cv::imwrite(name.string(),event->card))
            throw std::runtime_error("could not save rectified camera evidence");
      }
      return true;
   }
};

#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
class CameraWindow {
   HWND m_hwnd{};
   cv::Mat m_pixels;
   cv::Mat m_rectifiedPixels;
   bool m_flip180{};
   bool m_focusReset{};
   bool m_boConfirm{};
   bool m_boReject{};
   int m_iNavigation{};
   static void PaintImage(HDC dc,cv::Mat const& image,RECT const& area) {
      if(image.empty())return;
      int const availableWidth=area.right-area.left;
      int const availableHeight=area.bottom-area.top;
      if(availableWidth<=0||availableHeight<=0)return;
      double const scale=std::min(
         static_cast<double>(availableWidth)/image.cols,
         static_cast<double>(availableHeight)/image.rows);
      int const width=std::max(1,static_cast<int>(image.cols*scale));
      int const height=std::max(1,static_cast<int>(image.rows*scale));
      BITMAPINFO info{};
      info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
      info.bmiHeader.biWidth=image.cols;
      info.bmiHeader.biHeight=-image.rows; // top-down BGRA bitmap
      info.bmiHeader.biPlanes=1;
      info.bmiHeader.biBitCount=32;
      info.bmiHeader.biCompression=BI_RGB;
      SetStretchBltMode(dc,HALFTONE);
      StretchDIBits(dc,area.left+(availableWidth-width)/2,
         area.top+(availableHeight-height)/2,width,height,
         0,0,image.cols,image.rows,image.data,&info,DIB_RGB_COLORS,SRCCOPY);
   }
   static cv::Mat ToDisplayPixels(cv::Mat const& image) {
      if(image.empty())return {};
      cv::Mat pixels;
      if(image.channels()==3)
         cv::cvtColor(image,pixels,cv::COLOR_BGR2BGRA);
      else if(image.channels()==4)
         pixels=image.clone();
      else
         cv::cvtColor(image,pixels,cv::COLOR_GRAY2BGRA);
      return pixels;
   }
   static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
      if(msg==WM_NCCREATE) {
         auto* cs=reinterpret_cast<CREATESTRUCTW*>(lp);
         SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
         return TRUE;
      }
      auto* self=reinterpret_cast<CameraWindow*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
      if(msg==WM_ERASEBKGND)return 1;
      if(msg==WM_KEYDOWN && self && wp==static_cast<WPARAM>('R')) {
         self->m_flip180=!self->m_flip180;
         std::println("[CAM] manual card orientation={} degrees (R toggles)",
            self->m_flip180?180:0);
         InvalidateRect(hwnd,nullptr,FALSE);
         return 0;
      }
      if(msg==WM_KEYDOWN && self && wp==static_cast<WPARAM>('F')) {
         self->m_focusReset=true;
         return 0;
      }
      if(msg==WM_KEYDOWN && self) {
         if(wp==VK_LEFT)--self->m_iNavigation;
         if(wp==VK_RIGHT)++self->m_iNavigation;
         if(wp==VK_RETURN)self->m_boConfirm=true;
         if(wp==VK_ESCAPE)self->m_boReject=true;
         if(wp==VK_LEFT || wp==VK_RIGHT || wp==VK_RETURN ||
            wp==VK_ESCAPE)return 0;
      }
      if(msg==WM_MOUSEWHEEL && self) {
         self->m_iNavigation-=
            GET_WHEEL_DELTA_WPARAM(wp)/WHEEL_DELTA;
         return 0;
      }
      if(msg==WM_LBUTTONDOWN && self) {
         self->m_boConfirm=true;
         return 0;
      }
      if(msg==WM_RBUTTONDOWN && self) {
         self->m_boReject=true;
         return 0;
      }
      if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}
      if(msg==WM_DESTROY){if(self)self->m_hwnd=nullptr;return 0;}
      if(msg==WM_PAINT) {
         PAINTSTRUCT ps{};
         HDC dc=BeginPaint(hwnd,&ps);
         RECT rc{};GetClientRect(hwnd,&rc);
         int const clientWidth=rc.right-rc.left;
         int const clientHeight=rc.bottom-rc.top;
         HDC buffer=CreateCompatibleDC(dc);
         HBITMAP bitmap=(buffer && clientWidth>0 && clientHeight>0)?
            CreateCompatibleBitmap(dc,clientWidth,clientHeight):nullptr;
         if(bitmap) {
            HGDIOBJ oldBitmap=SelectObject(buffer,bitmap);
            FillRect(buffer,&rc,reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
            int const divider=clientWidth*57/100;
            RECT const left{12,42,divider-10,clientHeight-12};
            RECT const right{divider+10,42,clientWidth-12,clientHeight-12};
            SetBkMode(buffer,TRANSPARENT);
            SetTextColor(buffer,RGB(232,237,242));
            wchar_t const* titleLeft=L"CAMERA - DETECTED CONTOUR";
            TextOutW(buffer,12,13,titleLeft,static_cast<int>(wcslen(titleLeft)));
            wchar_t const* titleRight=self&&self->m_flip180?
               L"RECTIFIED CARD - ROTATED 180 (R)":
               L"RECTIFIED CARD - ROTATION 0 (R)";
            TextOutW(buffer,divider+10,13,titleRight,
               static_cast<int>(wcslen(titleRight)));
            if(self) {
               PaintImage(buffer,self->m_pixels,left);
               PaintImage(buffer,self->m_rectifiedPixels,right);
               if(self->m_rectifiedPixels.empty()) {
                  wchar_t const* waiting=L"NO CARD CONTOUR";
                  TextOutW(buffer,divider+22,clientHeight/2,waiting,
                     static_cast<int>(wcslen(waiting)));
               }
            }
            RECT separator{divider,12,divider+1,clientHeight-12};
            FillRect(buffer,&separator,
               reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));
            BitBlt(dc,0,0,clientWidth,clientHeight,buffer,0,0,SRCCOPY);
            SelectObject(buffer,oldBitmap);
            DeleteObject(bitmap);
         }
         if(buffer)DeleteDC(buffer);
         EndPaint(hwnd,&ps);
         return 0;
      }
      return DefWindowProcW(hwnd,msg,wp,lp);
   }
public:
   explicit CameraWindow(int index) {
      HINSTANCE instance=GetModuleHandleW(nullptr);
      WNDCLASSW cls{};
      cls.hInstance=instance;
      cls.lpfnWndProc=Proc;
      cls.lpszClassName=L"DeckKernelLiveCamera";
      cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
      if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)
         throw std::runtime_error("camera preview class registration failed");
      m_hwnd=CreateWindowExW(0,cls.lpszClassName,L"DeckKernel - camera + rectified card",
         WS_OVERLAPPEDWINDOW|WS_VISIBLE,CW_USEDEFAULT,CW_USEDEFAULT,
         1450,850,nullptr,nullptr,instance,this);
      if(!m_hwnd)throw std::runtime_error("camera preview creation failed");
      ShowWindow(m_hwnd,SW_SHOW);
      std::println("[CAM] preview: left=camera, right=actual rectified OCR/artwork "
         "input; press R to toggle 0/180 degrees");
   }
   [[nodiscard]] bool Flip180() const noexcept {return m_flip180;}
   [[nodiscard]] bool ConsumeFocusReset() noexcept {
      bool const boReset=m_focusReset;
      m_focusReset=false;
      return boReset;
   }
   [[nodiscard]] int ConsumeNavigation() noexcept {
      int const iDelta=m_iNavigation;
      m_iNavigation=0;
      return iDelta;
   }
   [[nodiscard]] bool ConsumeConfirmation() noexcept {
      bool const boResult=m_boConfirm;
      m_boConfirm=false;
      return boResult;
   }
   [[nodiscard]] bool ConsumeRejection() noexcept {
      bool const boResult=m_boReject;
      m_boReject=false;
      return boResult;
   }
   bool Messages() {
      MSG message{};
      while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
         TranslateMessage(&message);
         DispatchMessageW(&message);
      }
      return m_hwnd!=nullptr;
   }
   void Show(cv::Mat const& frame,cv::Mat const& rectified,
      int index,double fps,double brightness) {
      m_pixels=ToDisplayPixels(frame);
      m_rectifiedPixels=ToDisplayPixels(rectified);
      auto title=std::format(L"DeckKernel - camera {} - {}x{} - {:.1f} fps - brightness {:.0f}",
         index,frame.cols,frame.rows,fps,brightness);
      SetWindowTextW(m_hwnd,title.c_str());
      InvalidateRect(m_hwnd,nullptr,FALSE);
   }
};
#endif

void SelfTest() {
   // PHASE 10 regression: an explicit scan reset must free the physical
   // lock without reusing its event number. A delayed result from scan 1
   // must never become a result for scan 2.
   StableCardTrigger aScanTrigger;
   FoundCard aTestCard;
   aTestCard.rectified=cv::Mat(700,500,CV_8UC3,
      cv::Scalar(160,165,170));
   aTestCard.corners={cv::Point(100,100),cv::Point(600,100),
      cv::Point(600,800),cv::Point(100,800)};
   aTestCard.fingerprint=0x12345678;
   std::optional<StableCardTrigger::Event> optFirst;
   for(int iFrame=0;iFrame<4;++iFrame)
      optFirst=aScanTrigger.Accept(
         std::optional<FoundCard>{aTestCard});
   if(!optFirst || optFirst->number!=1 || !aScanTrigger.Active())
      throw std::runtime_error("initial explicit scan regression");
   aScanTrigger.Reset();
   if(aScanTrigger.Active() || aScanTrigger.StableCount()!=0)
      throw std::runtime_error("explicit scan reset retained old card");
   std::optional<StableCardTrigger::Event> optSecond;
   for(int iFrame=0;iFrame<4;++iFrame)
      optSecond=aScanTrigger.Accept(
         std::optional<FoundCard>{aTestCard});
   if(!optSecond || optSecond->number!=2)
      throw std::runtime_error("scan reset reused an asynchronous event ID");
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
   // PHASE 00: calibrated focus measurements never require a card contour.
   cv::Mat aFocusTexture(720,1280,CV_8UC3,cv::Scalar(105,110,120));
   cv::Rect const aFocusArea=
      deckkernel::camera::TablePlaneFocus::FocusRegion(
         aFocusTexture.size());
   if(aFocusArea.width!=384 || aFocusArea.height!=432 ||
      aFocusArea.x!=448 || aFocusArea.y!=144)
      throw std::runtime_error("table focus ROI regression");
   for(int iGrid=0;iGrid<18;++iGrid) {
      cv::line(aFocusTexture,
         cv::Point(aFocusArea.x+12+iGrid*18,aFocusArea.y+10),
         cv::Point(aFocusArea.x+12+iGrid*18,aFocusArea.y+400),
         cv::Scalar(20,25,30),2);
   }
   auto const aSharp=
      deckkernel::camera::TablePlaneFocus::Measure(aFocusTexture);
   cv::Mat aFocusBlur;
   cv::GaussianBlur(aFocusTexture,aFocusBlur,
      cv::Size(31,31),8.0);
   auto const aBlurredFocusMetrics=
      deckkernel::camera::TablePlaneFocus::Measure(aFocusBlur);
   if(!aSharp.boUsable ||
      aSharp.flSharpness<=aBlurredFocusMetrics.flSharpness)
      throw std::runtime_error("table focus optical-score regression");
#endif
   // REGRESSION: a printed collector strip just OUTSIDE the detected
   // inner frame must be visible in the source-pixel overscan, while the
   // canonical artwork/full-card image remains geometrically unchanged.
   cv::Mat aMarginSource(950,950,CV_8UC3,cv::Scalar(25,30,36));
   cv::rectangle(aMarginSource,cv::Rect(300,220,300,420),
      cv::Scalar(225,235,245),-1);
   cv::rectangle(aMarginSource,cv::Rect(305,633,290,6),
      cv::Scalar(0,0,255),-1);
   std::array<cv::Point,4> const arrInnerCorners{{
      {300,230},{600,230},{600,630},{300,630}
   }};
   cv::Mat const aCanonical=RectifyForOcr(
      aMarginSource,arrInnerCorners,false);
   cv::Mat const aOverscan=RectifyWithMargin(
      aMarginSource,arrInnerCorners,false);
   cv::Mat aCanonicalRed,aOverscanRed;
   cv::inRange(aCanonical,cv::Scalar(0,0,200),
      cv::Scalar(45,45,255),aCanonicalRed);
   cv::inRange(aOverscan,cv::Scalar(0,0,200),
      cv::Scalar(45,45,255),aOverscanRed);
   if(aOverscan.cols<=aCanonical.cols ||
      aOverscan.rows<=aCanonical.rows ||
      cv::countNonZero(aCanonicalRed)!=0 ||
      cv::countNonZero(aOverscanRed)<15)
      throw std::runtime_error(
         "overscan did not retain collector strip outside inner card frame");

   // DEMO TEST 04/05: the capture becomes immutable after Freeze().
   deckkernel::camera::CardCapture aCapture;
   cv::Mat aInitial(672,480,CV_8UC3,cv::Scalar(75,115,155));
   aCapture.Begin(aInitial);
   if(!aCapture.Freeze() || aCapture.Image().empty() ||
      aCapture.Bottom().empty())
      throw std::runtime_error("frozen capture could not be established");
   cv::Mat const aSnapshot=aCapture.Image().clone();
   cv::Mat aChanged(672,480,CV_8UC3,cv::Scalar(190,220,240));
   if(aCapture.Offer(aChanged) ||
      cv::norm(aSnapshot,aCapture.Image(),cv::NORM_INF)!=0)
      throw std::runtime_error("frozen capture was modified");

   // DEMO TEST 09/10: switching printings never mutates card pixels.
   deckkernel::camera::CardIdentity aIdentity;
   std::vector<deckkernel::camera::PrintingCandidate> vecPrintings(3);
   for(unsigned uIndex=0;uIndex<vecPrintings.size();++uIndex) {
      vecPrintings[uIndex].strOracleId="oracle-demo";
      vecPrintings[uIndex].strName="Demo Card";
      vecPrintings[uIndex].strPrintingId=std::format("printing-{}",uIndex);
   }
   aIdentity.SetCandidates(std::move(vecPrintings));
   if(aIdentity.Count()!=3 || !aIdentity.Navigate(-1) ||
      aIdentity.Index()!=2 || !aIdentity.Navigate(1) ||
      aIdentity.Index()!=0 || !aIdentity.Confirm() ||
      aIdentity.Navigate(1) || !aIdentity.Confirmed())
      throw std::runtime_error("printing navigation/confirmation lifecycle failed");
   aIdentity.Clear();
   if(aIdentity.Count()!=0 || aIdentity.Confirmed())
      throw std::runtime_error("printing identity not cleared");

   // DEMO TEST 03: follow a textured physical card without new contours.
   cv::Mat aTexture(720,960,CV_8UC3,cv::Scalar(20,25,30));
   cv::rectangle(aTexture,cv::Rect(260,150,290,400),
      cv::Scalar(210,220,230),-1);
   for(int iY=0;iY<8;++iY) {
      for(int iX=0;iX<7;++iX) {
         cv::circle(aTexture,cv::Point(280+iX*37,175+iY*48),5,
            cv::Scalar((iX*37+iY*17)%220,
               (iX*53+iY*27)%220,(iX*29+iY*41)%220),-1);
      }
   }
   deckkernel::camera::PhysicalCardTracker aTracker;
   std::array<cv::Point2f,4> const arrCard{{
      {260.f,150.f},{550.f,150.f},{550.f,550.f},{260.f,550.f}
   }};
   if(!aTracker.Begin(aTexture,arrCard))
      throw std::runtime_error("textured card did not establish optical-flow lock");
   if(!aTracker.Track(aTexture))
      throw std::runtime_error("stationary physical card lost during optical flow");
   aTracker.Reset();
   if(aTracker.Active())
      throw std::runtime_error("physical tracker did not release its lock");

   // DEMO TEST 03: a texture-poor physical card remains locked by its
   // whole-card/background contrast, but disappears when it is removed.
   cv::Mat aPlainFrame(720,960,CV_8UC3,cv::Scalar(20,25,30));
   cv::rectangle(aPlainFrame,cv::Rect(260,150,290,400),
      cv::Scalar(235,235,235),-1);
   deckkernel::camera::PhysicalCardTracker aPlainTracker;
   static_cast<void>(aPlainTracker.Begin(aPlainFrame,arrCard));
   if(!aPlainTracker.Track(aPlainFrame))
      throw std::runtime_error("texture-poor card did not retain its physical lock");
   cv::Mat aEmptyFrame(720,960,CV_8UC3,cv::Scalar(20,25,30));
   if(aPlainTracker.Track(aEmptyFrame))
      throw std::runtime_error("removed texture-poor card remained falsely present");

   Processor processor(false);
   cv::Mat const brightCard(672,480,CV_8UC3,cv::Scalar(255,255,255));
   cv::Mat const dimCard(672,480,CV_8UC3,cv::Scalar(15,15,15));
   auto const brightQuality=deckkernel_camera::quality::Measure(brightCard);
   auto const dimQuality=deckkernel_camera::quality::Measure(dimCard);
   if(!brightQuality.GlareRisk() || !dimQuality.LowLight())
      throw std::runtime_error("camera lighting diagnostics failed");
   // DEMO TEST 04: source-pixel quality must not be falsified by the
   // minimum 480-pixel perspective output width. Blurry, low-resolution
   // input must remain unsuitable even after image enlargement.
   cv::Mat aSharpSource(1050,750,CV_8UC3,cv::Scalar(220,225,230));
   for(int iLine=0;iLine<12;++iLine) {
      cv::putText(aSharpSource,std::format("TEST CARD PRINTING {}",iLine),
         cv::Point(50,80+iLine*75),cv::FONT_HERSHEY_SIMPLEX,1.1,
         cv::Scalar(25,25,25),2,cv::LINE_AA);
   }
   cv::Mat aBlurred;
   cv::GaussianBlur(aSharpSource,aBlurred,cv::Size(25,25),6.0);
   auto const aSharpQuality=deckkernel_camera::quality::Measure(
      aSharpSource,750);
   auto const aBlurQuality=deckkernel_camera::quality::Measure(
      aBlurred,750);
   auto const aUpscaledLowResolution=
      deckkernel_camera::quality::Measure(aSharpSource,180);
   // A visually acquired card always enters matching after the bounded
   // focus interval. Printed-detail quality is a warning, not a veto.
   deckkernel::camera::CardCapture aWeakCapture;
   cv::Mat aWeakFrame(672,480,CV_8UC3,cv::Scalar(160,160,160));
   aWeakCapture.Begin(aWeakFrame,180);
   if(aWeakCapture.UsableForMatching())
      throw std::runtime_error("synthetic weak capture unexpectedly passed gate");
   // Do not delay the real time-dependent Ready() check in a self-test:
   // readiness uses elapsed time rather than input image sharpness.

   if(!(aSharpQuality.detailSharpness>aBlurQuality.detailSharpness) ||
      aUpscaledLowResolution.UsableForMatching() ||
      aUpscaledLowResolution.sourceWidth!=180)
      throw std::runtime_error(
         "source-resolution/detail-sharpness quality regression");
   std::println("[CAM-QUALITY-TEST] detail sharp={:.1f} blurred={:.1f} "
      "native-small={} rejected={}",
      aSharpQuality.detailSharpness,aBlurQuality.detailSharpness,
      aUpscaledLowResolution.sourceWidth,
      !aUpscaledLowResolution.UsableForMatching());
   auto const enhanced=deckkernel_camera::quality::ForOcr(dimCard);
   if(enhanced.empty() || enhanced.channels()!=1 ||
      enhanced.size()!=dimCard.size())
      throw std::runtime_error("OCR contrast preprocessing failed");
   cv::Mat zoneCard(672,480,CV_8UC3,cv::Scalar(255,255,255));
   auto drawSeparators=[&](int y1,int y2) {
      zoneCard.setTo(cv::Scalar(255,255,255));
      cv::line(zoneCard,cv::Point(34,y1),cv::Point(446,y1),
         cv::Scalar(20,20,20),3);
      cv::line(zoneCard,cv::Point(34,y2),cv::Point(446,y2),
         cv::Scalar(20,20,20),3);
   };
   drawSeparators(368,412);
   auto const firstZones=deckkernel_camera::ocr::Detect(zoneCard);
   drawSeparators(291,337);
   auto const zones=deckkernel_camera::ocr::Detect(zoneCard);
   if(!firstZones.typeSeparated || !zones.typeSeparated ||
      std::abs(zones.rules.y-firstZones.rules.y)<40)
      throw std::runtime_error("dynamic OCR type/rules boundary not detected");
   if(!deckkernel_camera::ocr::Valid(zones,zoneCard.size()) ||
      zones.name.br().x>zones.mana.x ||
      zones.type.br().x>zones.setSymbol.x ||
      zones.rules.y<zones.type.br().y)
      throw std::runtime_error("OCR text/symbol zones overlap");
   cv::Mat blank(720,1280,CV_8UC3,cv::Scalar(20,20,20));
   cv::Mat card=blank.clone();
   cv::rectangle(card,cv::Rect(475,175,250,350),cv::Scalar(240,240,240),-1);
   // The manual 180-degree correction must be lossless and preserve the hash
   // on two successive toggles. Both panels and downstream consumers use it.
   auto sample=FindCard(card);
   if(!sample)throw std::runtime_error("synthetic card contour missing");
   cv::Mat const highRes=RectifyForOcr(card,sample->corners,false);
   if(highRes.empty() || highRes.cols<480 || highRes.rows<672)
      throw std::runtime_error("high-resolution card extraction failed");
   cv::Mat upscaled;
   cv::resize(card,upscaled,cv::Size(3840,2160));
   auto full=FindCard(upscaled);
   if(!full || full->rectified.size()!=cv::Size(480,672))
      throw std::runtime_error("4K camera detection must remain at fixed resolution");
   cv::Mat const fullOcr=RectifyForOcr(upscaled,full->corners,false);
   if(fullOcr.cols<=480 || fullOcr.rows<=672)
      throw std::runtime_error("4K accepted card did not preserve source details");
   // Physically sized cards can cover much less than 4% of the 4K frame.
   // Both printed sizes must be detected, including a modest tilt.
   for(int cardWidth: {150,220,330}) {
      cv::Mat scene(2160,3840,CV_8UC3,cv::Scalar(28,28,28));
      int const cardHeight=cardWidth*7/5;
      cv::Rect const cardBox(1500,850,cardWidth,cardHeight);
      cv::rectangle(scene,cardBox,cv::Scalar(240,240,240),-1);
      cv::rectangle(scene,cardBox,cv::Scalar(45,45,45),3);
      CardDetectionDiagnostics stats;
      auto const detected=FindCard(scene,&stats);
      if(!detected || stats.selectedAreaPercent>=4.0 ||
         stats.selectedAreaPercent<=0.0)
         throw std::runtime_error("small 4K printed card detection failed");
   }
   // DEMO TEST 01: a genuine standard card contains artwork, title, a
   // type separator and printed text. Its artwork is not a physical card.
   cv::Mat aFramedScene(960,1500,CV_8UC3,cv::Scalar(26,31,39));
   cv::Rect const aOuter(550,125,320,448);
   cv::rectangle(aFramedScene,aOuter,cv::Scalar(218,224,231),-1);
   cv::rectangle(aFramedScene,aOuter,cv::Scalar(12,16,21),5);
   cv::rectangle(aFramedScene,
      cv::Rect(571,150,278,38),cv::Scalar(221,235,246),-1);
   cv::line(aFramedScene,cv::Point(566,208),cv::Point(855,208),
      cv::Scalar(16,17,19),5);
   cv::Rect const aInnerArt(574,215,272,155);
   cv::rectangle(aFramedScene,aInnerArt,cv::Scalar(48,90,143),-1);
   for(int i=0;i<25;++i) {
      cv::circle(aFramedScene,
         cv::Point(588+(i*41)%245,225+(i*37)%132),
         7+(i%6),cv::Scalar(
            (i*43+17)%240,(i*63+33)%240,(i*31+61)%240),-1);
   }
   cv::rectangle(aFramedScene,aInnerArt,cv::Scalar(12,21,28),4);
   cv::line(aFramedScene,cv::Point(565,382),cv::Point(855,382),
      cv::Scalar(11,14,17),5);
   cv::rectangle(aFramedScene,cv::Rect(575,401,268,125),
      cv::Scalar(229,227,216),-1);
   for(int i=0;i<6;++i)
      cv::line(aFramedScene,cv::Point(592,416+i*16),
         cv::Point(820-(i%3)*30,416+i*16),
         cv::Scalar(68,72,79),2);
   cv::line(aFramedScene,cv::Point(572,539),cv::Point(846,539),
      cv::Scalar(24,25,28),3);
   // REGRESSION: an unrecognized internal frame must not block a
   // geometrically valid physical card. This is important for out-of-focus
   // and unusual printing layouts in a real 4K MJPEG stream.
   cv::Mat aUnusualScene(900,1300,CV_8UC3,cv::Scalar(27,29,33));
   cv::rectangle(aUnusualScene,cv::Rect(400,120,300,420),
      cv::Scalar(228,233,238),-1);
   cv::rectangle(aUnusualScene,cv::Rect(400,120,300,420),
      cv::Scalar(16,18,21),5);
   // REGRESSION: an oval mouse and isolated rectangular art/text panels
   // are not physical cards. A card-sized outer rectangle is a separate
   // geometric object with different side proportions and straight edges.
   cv::Mat aFalseObjects(900,1300,CV_8UC3,cv::Scalar(28,30,32));
   cv::ellipse(aFalseObjects,cv::Point(390,390),cv::Size(103,145),
      0.0,0.0,360.0,cv::Scalar(220,225,230),-1);
   cv::rectangle(aFalseObjects,cv::Rect(800,250,300,165),
      cv::Scalar(210,220,235),-1);
   cv::rectangle(aFalseObjects,cv::Rect(800,480,300,100),
      cv::Scalar(215,225,235),-1);
   CardDetectionDiagnostics aFalseStats;
   if(FindCard(aFalseObjects,&aFalseStats,true))
      throw std::runtime_error(
         "mouse, artwork, or rules panel was mistaken for a physical card");
   if(aFalseStats.rejectedShape==0 ||
      aFalseStats.rejectedAspect==0)
      throw std::runtime_error(
         "false-object geometry rejection was not exercised");

   // PHASE 01 regression: an unstructured rectangle is not sufficient
   // evidence for a complete standard card. It must not be locked in the
   // live detection path merely because it has a card-like aspect ratio.
   // Artwork can itself form a portrait-looking crop when a rotated
   // minimum-area rectangle is warped. Color/texture alone is not a card.
   cv::Mat aArtworkOnlyScene(900,1300,CV_8UC3,cv::Scalar(25,29,35));
   cv::Rect const aArtworkOnly(430,135,300,420);
   cv::rectangle(aArtworkOnlyScene,aArtworkOnly,
      cv::Scalar(24,75,132),-1);
   for(int iMark=0;iMark<28;++iMark) {
      cv::circle(aArtworkOnlyScene,
         cv::Point(445+(iMark*47)%270,155+(iMark*67)%370),
         8+(iMark%6),cv::Scalar((iMark*31)%210,
            (iMark*53)%240,(iMark*67)%225),-1);
   }
   cv::rectangle(aArtworkOnlyScene,aArtworkOnly,
      cv::Scalar(12,15,22),4);
   if(FindCard(aArtworkOnlyScene,nullptr,true))
      throw std::runtime_error(
         "portrait artwork without title/type frame acquired as physical card");

   auto const aUnusual=FindCard(aUnusualScene,nullptr,true);
   if(aUnusual)
      throw std::runtime_error(
         "unstructured card-shaped rectangle incorrectly acquired as a whole card");

   auto const aStandard=FindCard(aFramedScene,nullptr,true);
   if(!aStandard)
      throw std::runtime_error(
         "standard-card structural validation rejected whole outer card");
   cv::Rect const aDetectedBox=cv::boundingRect(aStandard->corners);
   if(aDetectedBox.area()<aOuter.area()*0.70)
      throw std::runtime_error(
         "artwork was incorrectly accepted as the physical card");
   if(aStandard->boRotate180)
      throw std::runtime_error("upright standard card was rotated 180 degrees");

   // DEMO TEST 01: the same physical card, actually upside-down, must
   // retain the outer contour and correct the 180-degree orientation.
   cv::Mat aReversedScene;
   cv::rotate(aFramedScene,aReversedScene,cv::ROTATE_180);
   auto const aReversed=FindCard(aReversedScene,nullptr,true);
   if(!aReversed || !aReversed->boRotate180)
      throw std::runtime_error(
         "upside-down standard card orientation was not recovered");
   if(cv::norm(aStandard->rectified,aReversed->rectified,
      cv::NORM_L2)/aStandard->rectified.total()>8.0)
      throw std::runtime_error(
         "rotation correction does not preserve the standard card layout");

   // Projective skew: unequal top/bottom widths and oblique vertical edges.
   // Four real corners, not a rotated bounding rectangle, must be restored.
   cv::Mat skewed(1000,1500,CV_8UC3,cv::Scalar(28,28,28));
   std::vector<cv::Point> const vecSkewCorners{
      {660,240},{825,265},{875,785},{545,725}};
   cv::fillConvexPoly(skewed,vecSkewCorners,cv::Scalar(240,240,240));
   cv::polylines(skewed,vecSkewCorners,true,cv::Scalar(35,35,35),3);
   CardDetectionDiagnostics skewStats;
   auto optSkew=FindCard(skewed,&skewStats);
   if(!optSkew || skewStats.quadrilaterals==0 ||
      optSkew->rectified.size()!=cv::Size(480,672)) {
      throw std::runtime_error(
         "projective card skew did not yield a rectified quadrilateral");
   }
   // The central rectified card must remain distinct from the background.
   if(cv::mean(optSkew->rectified(
      cv::Rect(100,130,280,370)))[0]<175.0) {
      throw std::runtime_error("perspective warp failed to preserve card pixels");
   }
      // A rotated print with a broken border must still yield a card target.
   cv::Mat rotatedScene(900,1600,CV_8UC3,cv::Scalar(38,38,38));
   cv::RotatedRect testRect(cv::Point2f(800.f,450.f),
      cv::Size2f(178.f,250.f),24.f);
   cv::Point2f testPoints[4];
   testRect.points(testPoints);
   std::vector<cv::Point> polygon;
   for(auto const& p:testPoints)
      polygon.emplace_back(cvRound(p.x),cvRound(p.y));
   cv::fillConvexPoly(rotatedScene,polygon,cv::Scalar(224,224,224));
   cv::polylines(rotatedScene,polygon,true,cv::Scalar(24,24,24),3);
   CardDetectionDiagnostics rotatedStats;
   auto const rotatedCard=FindCard(rotatedScene,&rotatedStats);
   if(!rotatedCard || rotatedStats.aspectCandidates==0)
      throw std::runtime_error("rotated printed card not detected");
   // Out-of-frame and border-adjacent corners are valid contour estimates
   // but illegal cornerSubPix starting points. They must survive unchanged.
   cv::Mat cornerGray(100,120,CV_8UC1,cv::Scalar(96));
   std::array<cv::Point2f,4> const edgePoints{{
      {-1.f,35.f},{119.f,60.f},{60.f,99.f},{0.f,0.f}
   }};
   auto const edgeResult=RefineCardCorners(cornerGray,edgePoints,1.0);
   for(std::size_t i=0;i<edgePoints.size();++i)
      if(edgeResult[i]!=edgePoints[i])
         throw std::runtime_error("border corner must remain unchanged");
   std::array<cv::Point2f,4> const mixedPoints{{
      {2.f,3.f},{50.f,40.f},{119.f,99.f},{20.f,0.f}
   }};
   auto const mixedResult=RefineCardCorners(cornerGray,mixedPoints,1.0);
   if(mixedResult[0]!=mixedPoints[0] ||
      mixedResult[2]!=mixedPoints[2] ||
      mixedResult[3]!=mixedPoints[3])
      throw std::runtime_error("invalid corner changed during subpixel refinement");
   cv::Mat const before=sample->rectified.clone();
   hash_ty const hashBefore=sample->fingerprint;
   FlipRectifiedCard(*sample);
   FlipRectifiedCard(*sample);
   if(cv::norm(before,sample->rectified,cv::NORM_INF)!=0 ||
      sample->fingerprint!=hashBefore)
      throw std::runtime_error("180-degree preview orientation roundtrip failed");
   // A blank view before the first card must never emit a removal.
   for(int i=0;i<60;++i)processor.Process(blank);
   if(processor.RemovedEvents()!=0)
      throw std::runtime_error("empty startup scene emitted a removal");
   unsigned events{};
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=1)throw std::runtime_error("first stable card must emit one event");
   // Hash fluctuations with uninterrupted presence are NOT a new card.
   cv::Mat fluctuating=card.clone();
   cv::rectangle(fluctuating,cv::Rect(495,205,140,140),
      cv::Scalar(70,80,90),-1);
   for(int i=0;i<30;++i)
      if(processor.Process(i%2?card:fluctuating))++events;
   if(events!=1)throw std::runtime_error("hash jitter caused duplicate card event");
   // Short contour dropouts must not emit a removal or a duplicate.
   for(int i=0;i<12;++i)processor.Process(blank);
   if(processor.RemovedEvents()!=0)
      throw std::runtime_error("short contour dropout emitted a removal");
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=1)throw std::runtime_error("brief contour loss caused a duplicate");
   // Removal is debounced, but no longer depends on a 45-frame gap.
   for(unsigned i=0;i<StableCardTrigger::RemovalFrames()-1;++i)
      processor.Process(blank);
   if(processor.RemovedEvents()!=0)
      throw std::runtime_error("card removed before debounce");
   processor.Process(blank);
   if(processor.RemovedEvents()!=1)
      throw std::runtime_error("missing removal after debounce");
   for(int i=0;i<60;++i)processor.Process(blank);
   if(processor.RemovedEvents()!=1)
      throw std::runtime_error("repeated removal while scene remained empty");
   for(int i=0;i<12;++i)if(processor.Process(card))++events;
   if(events!=2)throw std::runtime_error("reinserted card must emit second event");
   for(unsigned i=0;i<StableCardTrigger::RemovalFrames();++i)
      processor.Process(blank);
   if(processor.RemovedEvents()!=2)
      throw std::runtime_error("second recognized card did not emit removal");
   // Once captured, image-content changes must not restart a search.
   StableCardTrigger swap;
   auto a=FindCard(card);
   if(!a)throw std::runtime_error("locking sample A missing");
   unsigned presents=0,removes=0;
   for(int i=0;i<6;++i) {
      auto event=swap.Accept(a);
      if(event && event->kind==StableCardTrigger::EventKind::Presented)++presents;
   }
   auto b=*a;
   cv::rectangle(b.rectified,cv::Rect(90,130,300,290),
      cv::Scalar(12,24,210),-1);
   b.fingerprint=DHash(b.rectified);
   for(int i=0;i<30;++i) {
      auto event=swap.Accept(b);
      if(event)throw std::runtime_error("locked card retriggered by artwork change");
   }
   for(unsigned i=0;i<StableCardTrigger::RemovalFrames();++i) {
      auto event=swap.Accept(std::nullopt);
      if(event && event->kind==StableCardTrigger::EventKind::Removed)++removes;
   }
   if(presents!=1 || removes!=1)
      throw std::runtime_error("locked card removal lifecycle failed");
   for(int i=0;i<6;++i) {
      auto event=swap.Accept(b);
      if(event && event->kind==StableCardTrigger::EventKind::Presented)++presents;
   }
   if(presents!=2)
      throw std::runtime_error("new card not accepted after confirmed removal");
   // A wrong contour elsewhere must not keep an already-presented card
   // alive indefinitely. The right panel must be cleared on removal.
   Processor unrelated(false);
   for(int i=0;i<6;++i)unrelated.Process(card);
   if(unrelated.DisplayCard().empty())
      throw std::runtime_error("presented card preview not populated");
   cv::Mat different=blank.clone();
   cv::rectangle(different,cv::Rect(80,105,260,364),
      cv::Scalar(235,235,235),-1);
   cv::rectangle(different,cv::Rect(100,160,210,220),
      cv::Scalar(45,50,195),-1);
   for(unsigned i=0;i<StableCardTrigger::RemovalFrames();++i)
      unrelated.Process(different);
   if(unrelated.RemovedEvents()!=1 || !unrelated.DisplayCard().empty())
      throw std::runtime_error("unrelated contour retained previous card");
   std::println("SMOKE|RESULT|PASS|stable presentation, one removal per card, "
      "jitter suppression, short dropout and reentry");
}

#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
/** \brief Translate a Windows camera's FOURCC to readable characters. */
std::string FourccName(double code) {
   if(!std::isfinite(code)||code<=0)return "unspecified";
   std::uint32_t const number=static_cast<std::uint32_t>(code);
   std::string out;
   for(unsigned shift=0;shift<32;shift+=8) {
      char const ch=static_cast<char>((number>>shift)&0xff);
      if(ch<32||ch>126)return std::format("0x{:08x}",number);
      out.push_back(ch);
   }
   return out;
}

int Api(std::string_view backend) {
   if(backend=="any")return cv::CAP_ANY;
   if(backend=="dshow")return cv::CAP_DSHOW;
   if(backend=="msmf")return cv::CAP_MSMF;
   throw std::runtime_error("backend must be any, dshow or msmf");
}

int CameraFourcc(std::string_view value) {
   if(value=="mjpg")return cv::VideoWriter::fourcc('M','J','P','G');
   if(value=="yuy2")return cv::VideoWriter::fourcc('Y','U','Y','2');
   throw std::runtime_error("FOURCC must be auto, mjpg or yuy2");
}

void ConfigureCapture(cv::VideoCapture& capture,int width,int height,int fps,
   std::string_view fourcc) {
   // Drivers may restart their capture graph when these properties change.
   // The requested mode is not necessarily the mode ultimately negotiated.
   if(fourcc!="auto") {
      bool const result=capture.set(cv::CAP_PROP_FOURCC,CameraFourcc(fourcc));
      std::println("[CAM] FOURCC request={} accepted={}",fourcc,result);
   }
   bool const w=capture.set(cv::CAP_PROP_FRAME_WIDTH,width);
   bool const h=capture.set(cv::CAP_PROP_FRAME_HEIGHT,height);
   bool const f=capture.set(cv::CAP_PROP_FPS,fps);
   std::println("[CAM] mode request={}x{} @{}fps accepted={}/{}/{}",
      width,height,fps,w,h,f);
   std::println("[CAM] negotiated={}x{} @{}fps FOURCC={}",
      capture.get(cv::CAP_PROP_FRAME_WIDTH),
      capture.get(cv::CAP_PROP_FRAME_HEIGHT),
      capture.get(cv::CAP_PROP_FPS),
      FourccName(capture.get(cv::CAP_PROP_FOURCC)));
}

struct FrameSignal {
   double brightness{};
   double maximum{};
   double minimum{};
   bool nonblack{};
};

/** \brief Measure real pixel values, not merely successful VideoCapture.read(). */
FrameSignal MeasureFrame(cv::Mat const& frame) {
   if(frame.empty())return {};
   cv::Mat thumbnail;
   cv::resize(frame,thumbnail,cv::Size(80,60),0,0,cv::INTER_AREA);
   cv::Mat const oneChannel=thumbnail.reshape(1);
   FrameSignal signal;
   cv::minMaxLoc(oneChannel,&signal.minimum,&signal.maximum);
   cv::Scalar const mean=cv::mean(thumbnail);
   if(frame.channels()>=3)
      signal.brightness=(mean[0]+mean[1]+mean[2])/3.0;
   else signal.brightness=mean[0];
   signal.nonblack=signal.maximum>5.0;
   return signal;
}

struct ProbeProfile {
   char const* name{};
   int backend{};
   int width{},height{},fps{};
   char const* fourcc{};
};

/** \brief Try several normal camera delivery modes without touching the DB.
    \returns True if any mode yielded nonblack image pixels.
*/
bool ProbeCamera(int index) {
   std::array<ProbeProfile,4> const modes{{
      {"DirectShow MJPG",cv::CAP_DSHOW,640,480,30,"mjpg"},
      {"DirectShow YUY2",cv::CAP_DSHOW,640,480,30,"yuy2"},
      {"MediaFoundation auto",cv::CAP_MSMF,640,480,30,"auto"},
      {"MediaFoundation MJPG",cv::CAP_MSMF,640,480,30,"mjpg"}
   }};
   unsigned successful=0;
   for(auto const& mode:modes) {
      std::println("[CAM-PROBE] START index={} mode={}",index,mode.name);
      try {
         cv::VideoCapture capture;
         if(!capture.open(index,mode.backend)||!capture.isOpened()) {
            std::println("[CAM-PROBE] UNAVAILABLE {}",mode.name);
            continue;
         }
         ConfigureCapture(capture,mode.width,mode.height,mode.fps,mode.fourcc);
         unsigned received=0,bright=0,black=0,changed=0,empty=0;
         cv::Mat previous,frame;
         double maximum=0;
         auto const beginning=std::chrono::steady_clock::now();
         for(unsigned n=0;n<45;++n) {
            if(!capture.read(frame)||frame.empty()){
               ++empty;
            } else {
               ++received;
               auto const signal=MeasureFrame(frame);
               maximum=std::max(maximum,signal.maximum);
               if(signal.nonblack)++bright;else ++black;
               cv::Mat small;
               cv::resize(frame,small,cv::Size(80,60),0,0,cv::INTER_AREA);
               if(!previous.empty()&&cv::norm(previous,small,cv::NORM_L1)>50)
                  ++changed;
               previous=std::move(small);
            }
            if(std::chrono::duration<double>(
               std::chrono::steady_clock::now()-beginning).count()>=5.0)
               break;
         }
         double const seconds=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-beginning).count();
         std::println("[CAM-PROBE] RESULT {} backend={} frames={} empty={} "
            "nonblack={} black={} changed={} maxPixel={:.0f} elapsed={:.1f}s",
            mode.name,capture.getBackendName(),received,empty,bright,black,
            changed,maximum,seconds);
         if(bright>0)++successful;
         capture.release();
      } catch(cv::Exception const& ex) {
         std::println(std::cerr,
            "[CAM-PROBE] BACKEND ERROR {}: {}",mode.name,ex.what());
      }
   }
   std::println("SMOKE|CHECK|camera-pixels|{}|camera={} valid_modes={}/4",
      successful?"PASS":"FAIL",index,successful);
   if(!successful)
      std::println("[CAM-PROBE] No usable image data. Check the camera in the "
         "Windows Camera application before changing the card recognition algorithm.");
   return successful>0;
}
#endif

int CameraIndex(std::string_view str) {
   int n{};
   auto const r=std::from_chars(str.data(),str.data()+str.size(),n);
   if(r.ec!=std::errc{}||r.ptr!=str.data()+str.size()||n<0)
      throw std::runtime_error("invalid camera index");
   return n;
}
void Usage() {
   std::println("Usage: deckkernel_camera_recognition_test --self-test");
   std::println("   or: --sequence=folder [--save-events]");
   std::println("   or: --image=file.png [--ocr] [--save-events] (original phone photo)");
   std::println("   or: --frozen-image=event-NNNNNN-frozen-search-input.png [--save-events] (direct DB matcher replay)");
   std::println("   or: --camera=0 [--backend=any|dshow|msmf] [--width=3840 --height=2160 --fps=30 --fourcc=mjpg|auto|yuy2] [--save-events]");
   std::println("   or: --probe-camera=0          test 4 Windows camera delivery modes");
   std::println("   optional: --ocr --tessdata=PATH_TO_TESSDATA (eng.traineddata required)");
   std::println("   camera: indexed artwork/printing recognition enabled by default");
   std::println("   optional: --artwork [--artwork-limit=100] or --no-artwork");
   std::println("   camera focus: --table-focus (default), --continuous-autofocus or --focus-position=N");
   std::println("   press F to re-calibrate the focus at the tabletop before searching");
   std::println("   optional: --verbose (periodic camera and OCR console diagnostics)");
}
}
int main(int argc,char** argv) {
   try{
      bool selfTest=false,save=false,verbose=false;
      bool ocr=false,artwork=false,noArtwork=false;
      bool tableFocus=true;
      std::optional<int> manualFocusPosition;
      unsigned artworkLimit=100;
      std::string tessdata;
      std::string backend="any";
      std::string fourcc="mjpg";
      int width=3840,height=2160,fpsRequested=30;
      std::optional<int> probeCamera;
      std::string folder;
      std::string imageFile;
      std::string frozenImageFile;
      std::optional<int> camera;
      for(int i=1;i<argc;++i){
         std::string_view arg(argv[i]);
         if(arg=="--help"){Usage();return 0;}
         if(arg=="--self-test")selfTest=true;
         else if(arg=="--verbose")verbose=true;
         else if(arg=="--save-events")save=true;
         else if(arg=="--ocr")ocr=true;
         else if(arg=="--artwork")artwork=true;
         else if(arg=="--no-artwork")noArtwork=true;
         else if(arg=="--table-focus")tableFocus=true;
         else if(arg=="--continuous-autofocus")tableFocus=false;
         else if(arg.starts_with("--focus-position="))
            manualFocusPosition=CameraIndex(arg.substr(17));
         else if(arg.starts_with("--artwork-limit="))artworkLimit=static_cast<unsigned>(CameraIndex(arg.substr(16)));
         else if(arg.starts_with("--tessdata="))tessdata=arg.substr(11);
         else if(arg.starts_with("--backend="))backend=arg.substr(10);
         else if(arg.starts_with("--fourcc="))fourcc=arg.substr(9);
         else if(arg.starts_with("--width="))width=CameraIndex(arg.substr(8));
         else if(arg.starts_with("--height="))height=CameraIndex(arg.substr(9));
         else if(arg.starts_with("--fps="))fpsRequested=CameraIndex(arg.substr(6));
         else if(arg.starts_with("--probe-camera="))probeCamera=CameraIndex(arg.substr(15));
         else if(arg.starts_with("--sequence="))folder=arg.substr(11);
         else if(arg.starts_with("--image="))imageFile=arg.substr(8);
         else if(arg.starts_with("--frozen-image="))
            frozenImageFile=arg.substr(15);
         else if(arg.starts_with("--camera="))camera=CameraIndex(arg.substr(9));
         else throw std::runtime_error("unknown argument (use --help)");
      }
      if(selfTest){SelfTest();return 0;}
      if(probeCamera) {
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
         return ProbeCamera(*probeCamera)?0:1;
#else
         throw std::runtime_error("OpenCV videoio is unavailable");
#endif
      }
      // Live recognition is expected by default; --no-artwork is an explicit opt-out.
      if(camera && !noArtwork)artwork=true;
      if(noArtwork)artwork=false;
      if(artworkLimit<1||artworkLimit>1000)
         throw std::runtime_error("--artwork-limit must be 1..1000");
      if(width<160||height<120||width>3840||height>2160||
         fpsRequested<1||fpsRequested>120)
         throw std::runtime_error("invalid camera width, height or fps");
      unsigned const sources=static_cast<unsigned>(!folder.empty())+
         static_cast<unsigned>(camera.has_value())+
         static_cast<unsigned>(!imageFile.empty())+
         static_cast<unsigned>(!frozenImageFile.empty());
      if(sources!=1)
         throw std::runtime_error(
            "select exactly one of --image, --frozen-image, --sequence or --camera");
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if((ocr || artwork) && tessdata.empty()) {
         if(auto const* env=std::getenv("TESSDATA_PREFIX");env&&*env)tessdata=env;
         else tessdata=(std::filesystem::path(argv[0]).parent_path()/"tessdata").string();
      }
#else
      if(ocr)throw std::runtime_error("OCR was not enabled at CMake configure time");
#endif
      if(!frozenImageFile.empty()) {
#if !defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
         throw std::runtime_error(
            "offline frozen-image matcher was disabled at CMake configuration");
#else
         // REPLAY: process the EXACT previously frozen matcher input.
         // No camera, contour detection, tracking or new capture is involved.
         cv::Mat const aFrozen=cv::imread(
            frozenImageFile,cv::IMREAD_COLOR);
         if(aFrozen.empty())
            throw std::runtime_error(
               "cannot decode frozen matcher input: "+frozenImageFile);
         deckkernel::camera::CardImageMatcher aMatcher(artworkLimit);
         std::filesystem::path const aEvidenceDirectory=save?
            std::filesystem::path(".cache/camera-recognition/replay"):
            std::filesystem::path{};
         std::println("[CAM-REPLAY] frozen={} dimensions={}x{}",
            frozenImageFile,aFrozen.cols,aFrozen.rows);
         aMatcher.Search(aFrozen,1,aEvidenceDirectory);
         auto const aStart=std::chrono::steady_clock::now();
         while(std::chrono::steady_clock::now()-aStart<
            std::chrono::seconds(100)) {
            if(auto optResult=aMatcher.Poll()) {
               std::println("[CAM-REPLAY] status={} complete={} "
                  "asset={} name={} set={} collector={} candidates={}",
                  optResult->strStatus,optResult->boComplete,
                  optResult->strAssetId,optResult->strName,
                  optResult->strSet,optResult->strCollector,
                  optResult->vecCandidates.size());
               if(!optResult->boComplete)continue;
               if(save && !optResult->aReferenceImage.empty()) {
                  std::filesystem::create_directories(
                     aEvidenceDirectory);
                  cv::imwrite((aEvidenceDirectory/
                     "matched-reference.png").string(),
                     optResult->aReferenceImage);
               }
               return optResult->strAssetId.empty()?2:0;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
         }
         std::println(std::cerr,
            "[CAM-REPLAY] no terminal result within 100 seconds");
         return 3;
#endif
      }
      if(!imageFile.empty()) {
#if !defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
         if(artwork)throw std::runtime_error("artwork matching disabled at CMake configuration");
#endif
         cv::Mat const image=cv::imread(imageFile,cv::IMREAD_COLOR);
         if(image.empty())
            throw std::runtime_error("cannot decode original image: "+imageFile);
         std::println("[CAM] still image={} size={}x{}",imageFile,image.cols,image.rows);
         auto found=FindCard(image);
         if(found) {
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
            if(artwork)deckkernel::camera::CardImageMatcher(artworkLimit).Search(found->rectified,1);
#endif
            std::println("[CAM] card contour rectified to {}x{}",
               found->rectified.cols,found->rectified.rows);
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
            if(ocr) {
               CardOcr stillOcr(tessdata);
               static_cast<void>(stillOcr.Orient(found->rectified));
               static_cast<void>(stillOcr.Read(found->rectified,1,1));
            }
#endif
            if(save) {
               std::filesystem::create_directories(".cache/camera-recognition");
               if(!cv::imwrite(".cache/camera-recognition/still-card.png",found->rectified))
                  throw std::runtime_error("cannot save rectified still image");
            }
         } else {
            std::println("[CAM] NO CARD CONTOUR: preserving original image for OCR");
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
            if(ocr) {
               // Original photo may already be tightly cropped to the card.
               CardOcr stillOcr(tessdata);
               static_cast<void>(stillOcr.Read(image,1,1));
            }
#endif
         }
         return 0;
      }
      Processor processor(save,verbose);
#if defined(DECKKERNEL_CAMERA_HAS_ARTWORK)
      if(artwork)processor.EnableArtwork(artworkLimit);
#else
      if(artwork)throw std::runtime_error("artwork matching disabled at CMake configuration");
#endif
#if defined(DECKKERNEL_CAMERA_HAS_OCR)
      if(ocr)processor.EnableOcr(tessdata);
      if(artwork)processor.EnablePrintingOcr(tessdata);
#endif
      if(!folder.empty()){
         std::vector<std::filesystem::path> frames;
         for(auto const& e:std::filesystem::directory_iterator(folder)){
            if(!e.is_regular_file())continue;
            auto ext=e.path().extension().string();
            if(ext==".jpg"||ext==".png"||ext==".jpeg"||ext==".webp")
               frames.push_back(e.path());
         }
         std::sort(frames.begin(),frames.end());
         for(auto const& path:frames){
            auto frame=cv::imread(path.string(),cv::IMREAD_COLOR);
            if(frame.empty())throw std::runtime_error(
               "cannot read camera frame: "+path.string());
            processor.Process(frame);
         }
         std::println("[CAM] replay finished; frames={}",frames.size());
         return 0;
      }
#if defined(DECKKERNEL_CAMERA_HAS_VIDEOIO)
      int api=Api(backend);
      std::println("[CAM] opening camera={} backend-request={}",*camera,backend);
      cv::VideoCapture capture(*camera,api);
      if(!capture.isOpened())
         throw std::runtime_error("cannot open camera: try another index/backend or Windows camera privacy settings");
      std::println("[CAM] opened camera={} backend={}",*camera,capture.getBackendName());
      ConfigureCapture(capture,width,height,fpsRequested,fourcc);
      // DirectShow exposes these values only when the driver implements
      // the camera properties. Reporting them does not mutate working
      // exposure or HDR configuration.
      std::println("[CAM-LIGHT] auto-exposure={} exposure={} gain={} "
         "brightness={} contrast={} saturation={} white-balance={}",
         capture.get(cv::CAP_PROP_AUTO_EXPOSURE),
         capture.get(cv::CAP_PROP_EXPOSURE),
         capture.get(cv::CAP_PROP_GAIN),
         capture.get(cv::CAP_PROP_BRIGHTNESS),
         capture.get(cv::CAP_PROP_CONTRAST),
         capture.get(cv::CAP_PROP_SATURATION),
         capture.get(cv::CAP_PROP_WB_TEMPERATURE));
      // PHASE 00: pre-focus on the expected horizontal table plane BEFORE
      // physical card recognition. Keep the previous continuous AF as a
      // fallback for cameras which cannot report or lock manual focus.
      deckkernel::camera::TablePlaneFocus aTableFocus(
         tableFocus,manualFocusPosition);
      aTableFocus.Begin(capture);
#if defined(DECKKERNEL_CAMERA_HAS_SDL_SKIA)
      CameraDashboard preview;
#else
      CameraWindow preview(*camera);
#endif
      cv::Mat frame;
      unsigned long long count=0;
      unsigned failures=0,blackFrames=0;
      auto interval=std::chrono::steady_clock::now();
      double fps=0.0;
      bool appliedFlip=false;
      auto const beginning=interval;
      while(preview.Messages()){
         if(!capture.read(frame)||frame.empty()){
            if(++failures==1 || failures%30==0)
               std::println(std::cerr,"[CAM] no frame returned, consecutive failures={}",failures);
            if(failures>=90)throw std::runtime_error("camera opened but produced no frames");
            continue;
         }
         failures=0;
         ++count;
         if(preview.ConsumeFocusReset()) {
            // Recalibrate only between physical sessions: never change
            // optical settings while examining a frozen card.
            if(!processor.CardLocked()) {
               std::println("[CAM-TABLE-FOCUS] recalibration requested by F");
               aTableFocus.Begin(capture);
            }
            else std::println("[CAM-TABLE-FOCUS] F ignored: card locked");
         }
         FrameSignal const signal=MeasureFrame(frame);
         // Pre-focus independently of contours: a blurred table card must
         // never be required to pass layout recognition first.
         bool const boFocusCalibrating=aTableFocus.Observe(capture,frame);
         // One contour detection/rectification per frame. Use precisely the
         // same pixels for the side panel, the OCR and the artwork worker.
         CardDetectionDiagnostics detection;
         // PHASE 05+: stop ALL contour detection and optical tracking once
         // the card is frozen. The camera remains open only for rendering
         // and for a later calibration after an explicit user decision.
         bool const boRecognitionPaused=processor.RecognitionPaused();
         auto found=(signal.nonblack && !boFocusCalibrating &&
            !boRecognitionPaused)?
            processor.Observe(frame,&detection):
            std::optional<FoundCard>{};
         bool const flip=preview.Flip180();
         if(flip!=appliedFlip) {
            processor.ReevaluateOcrAfterRotation(flip);
            appliedFlip=flip;
         }
         if(found&&flip)FlipRectifiedCard(*found);
         if(signal.nonblack) {
            blackFrames=0;
            if(!boFocusCalibrating && !boRecognitionPaused)
               processor.Process(found,frame,processor.TrackingActive());
         } else {
            ++blackFrames;
         }
         // PHASE 06-09: asynchronous matching and OCR continue while
         // the physical camera detector is paused on its frozen snapshot.
         processor.PollRecognition();
         processor.PollPrintingOcr();
         // PHASE 09: only the selected DB reference changes. A mouse wheel
         // or arrow cannot start a second physical capture.
         int const iBrowse=preview.ConsumeNavigation();
         if(iBrowse!=0 && processor.RecognitionPaused()) {
            int const iStep=iBrowse<0?-1:1;
            for(int iCount=0;iCount<std::abs(iBrowse);++iCount)
               processor.NavigateCandidate(iStep);
         }
         // PHASE 10: a completed scan ends only by a deliberate decision.
         // Accepted cards survive ResetScan() in the collected list.
         bool const boReject=preview.ConsumeRejection();
         bool const boAccept=preview.ConsumeConfirmation();
         bool boRestartFocus{};
         if(boReject)
            boRestartFocus=processor.DiscardScan();
         else if(boAccept)
            boRestartFocus=processor.ConfirmCandidate();
         if(boRestartFocus) {
            std::println("[CAM-SCAN] next scan: pre-focus tabletop");
            aTableFocus.Begin(capture);
         }
         // Visual diagnostic: mark the four corners that pass card geometry checks.
         cv::Mat annotated=frame.clone();
         if(boFocusCalibrating) {
            cv::rectangle(annotated,
               deckkernel::camera::TablePlaneFocus::FocusRegion(frame.size()),
               cv::Scalar(0,180,250),3,cv::LINE_AA);
            cv::putText(annotated,"TABLE FOCUS CALIBRATION",cv::Point(12,26),
               cv::FONT_HERSHEY_SIMPLEX,0.7,
               cv::Scalar(0,180,250),2,cv::LINE_AA);
         } else if(found && !processor.CardLocked()) {
            for(unsigned i=0;i<4;++i)
               cv::line(annotated,found->corners[i],found->corners[(i+1)%4],
                  cv::Scalar(0,220,0),2,cv::LINE_AA);
            cv::putText(annotated,"CARD CONTOUR",cv::Point(12,26),
               cv::FONT_HERSHEY_SIMPLEX,0.7,cv::Scalar(0,220,0),2,cv::LINE_AA);
         } else if(processor.CardLocked()) {
            cv::putText(annotated,"CARD CAPTURED",cv::Point(12,26),
               cv::FONT_HERSHEY_SIMPLEX,0.7,cv::Scalar(0,220,0),2,cv::LINE_AA);
         } else {
            cv::putText(annotated,"NO CARD CONTOUR",cv::Point(12,26),
               cv::FONT_HERSHEY_SIMPLEX,0.7,cv::Scalar(0,170,240),2,cv::LINE_AA);
         }
#if defined(DECKKERNEL_CAMERA_HAS_SDL_SKIA)
         std::string qualityStatus=boFocusCalibrating?
            aTableFocus.Status():processor.CardLocked()?
            "Captured frame retained; no further card search until removal":
            (found?"Card boundary found; stabilizing capture":
               aTableFocus.Status()+" | Looking for a card boundary");
         preview.SetPhase(boFocusCalibrating?
            "00 TABLE FOCUS":deckkernel::camera::PhaseName(processor.Phase()));
         preview.SetCollectedCards(processor.CollectedCards());
         preview.SetFields(processor.Name(),processor.Type(),processor.Rules(),
            processor.Mana(),processor.Status(),processor.Edition(),
            flip?"Manual 180 deg / "+processor.Orientation():processor.Orientation(),
            qualityStatus,processor.CardLocked(),processor.Guidance(),
            processor.ReferenceLabel(),processor.ReferenceDiagnostic(),
            processor.Set(),processor.SetName(),processor.Collector(),
            processor.Language(),processor.Rarity(),processor.PrintingId(),
            processor.OracleId(),processor.ManaCost(),
            processor.CandidateIndex(),processor.CandidateCount(),
            processor.SelectionConfirmed());
#endif
         // Only a confirmed card belongs in the right panel. Do not show
         // a transient candidate or a different contour after removal.
#if defined(DECKKERNEL_CAMERA_HAS_SDL_SKIA)
         preview.Show(annotated,processor.DisplayCard(),
            processor.ReferenceCard(),*camera,fps,signal.brightness);
#else
         preview.Show(annotated,processor.DisplayCard(),
            *camera,fps,signal.brightness);
#endif
         auto now=std::chrono::steady_clock::now();
         double const seconds=std::chrono::duration<double>(now-interval).count();
         if(seconds>=2.0){
            fps=count/seconds;
            if(verbose) {
               std::println("[CAM] backend={} frames={} size={}x{} fps={:.1f} "
                  "mean={:.2f} min={:.0f} max={:.0f} black-frames={}",
                  capture.getBackendName(),count,frame.cols,frame.rows,fps,
                  signal.brightness,signal.minimum,signal.maximum,blackFrames);
               if(!processor.CardLocked())
                  std::println("[CAM-DETECT] contours={} candidates={} "
                     "quadrilaterals={} rotated={} plausible={} "
                     "shape-rejected={} aspect-rejected={} full-frame-rejected={} "
                     "layout={}/{} chosen-ratio={:.3f} nested-panels={} area={:.2f}%",
                     detection.contours,detection.areaCandidates,
                     detection.quadrilaterals,detection.rotatedCandidates,
                     detection.aspectCandidates,detection.rejectedShape,
                     detection.rejectedAspect,detection.rejectedPartialFrame,
                     detection.layoutRecognized,
                     detection.layoutChecked,detection.selectedRatio,
                     detection.selectedInnerPanels,
                     detection.selectedAreaPercent);
            }
            count=0;interval=now;
         }
         // Do not keep silently treating all-zero placeholder frames as
         // successful camera acquisition or running contour recognition.
         if(blackFrames>=5 &&
            std::chrono::duration<double>(now-beginning).count()>=5.0)
            throw std::runtime_error("VIDEO SIGNAL ABSENT: camera device opened, "
               "but returned at least five nearly black frames. "
               "Run --probe-camera=N and check the device in Windows Camera.");
      }
      std::println("[CAM] preview closed");
      return 0;
#else
      throw std::runtime_error("OpenCV videoio not present in installed BuildEngine stack; use --sequence");
#endif
   }
   catch(std::exception const& e){
      std::println(std::cerr,"[CAM] ERROR: {}",e.what());return 1;
   }
}
