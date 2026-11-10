// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file CameraPhysicalTracker.h
\brief Follow a whole physical card by tracked image features, not new contours.
\details The initial four corners are owned by a physical presentation. Later
frames are handled by sparse optical flow and a RANSAC homography constrained
to preserve an entire quadrilateral. A painting inside the card never becomes
a new physical card while tracking remains active.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/video/tracking.hpp>
#include <opencv2/calib3d.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <optional>
#include <utility>
#include <vector>

namespace deckkernel::camera {
   /**
   \brief Sparse optical-flow tracker for a single physical card.
   \details Supports short tracking loss without global redetection. It does
   not interpret a printing's artwork descriptor as physical-card identity.
   */
   class PhysicalCardTracker final {
      cv::Mat m_aPreviousGray;
      std::vector<cv::Point2f> m_vecPoints;
      std::array<cv::Point2f,4> m_arrCorners{};
      cv::Size m_aFrameSize{};
      bool m_boActive{};
      unsigned m_uMissed{};
      double m_flInitialInside{};
      double m_flInitialContrast{};
      static constexpr unsigned cuMaxMissed=45;

      /**
      \brief Calculate strong, spatially distributed points within the whole card.
      \details Frame, title, type bar, text box and art are all eligible.
      \param aGray Source frame, 8-bit grayscale.
      \param arrCorners Four ordered card corners.
      \returns Feature coordinates distributed over a three-by-three cell grid.
      \throw cv::Exception If OpenCV feature detection fails.
      */
      static std::vector<cv::Point2f> Features(cv::Mat const& aGray,
         std::array<cv::Point2f,4> const& arrCorners) {
         std::vector<cv::Point> vecPolygon;
         for(auto const& aPoint:arrCorners)
            vecPolygon.emplace_back(cvRound(aPoint.x),cvRound(aPoint.y));
         cv::Rect const aBounds=cv::boundingRect(vecPolygon) &
            cv::Rect(0,0,aGray.cols,aGray.rows);
         if(aBounds.width<16||aBounds.height<16)return {};
         // Work only on the known physical-card ROI, not the entire 4K scene.
         cv::Mat aMask(aBounds.size(),CV_8UC1,cv::Scalar(0));
         for(cv::Point& aPoint:vecPolygon)aPoint-=aBounds.tl();
         cv::fillConvexPoly(aMask,vecPolygon,cv::Scalar(255));
         std::array<cv::Point2f,4> const arrCanonical{{
            {0.f,0.f},{3.f,0.f},{3.f,3.f},{0.f,3.f}
         }};
         cv::Mat const aH=cv::getPerspectiveTransform(
            arrCorners.data(),arrCanonical.data());
         std::vector<cv::Point2f> vecAll;
         cv::goodFeaturesToTrack(aGray(aBounds),vecAll,
            300,0.008,5.0,aMask,3,false);
         for(cv::Point2f& aPoint:vecAll) {
            aPoint.x+=static_cast<float>(aBounds.x);
            aPoint.y+=static_cast<float>(aBounds.y);
         }
         std::vector<cv::Point2f> vecCanonical;
         cv::perspectiveTransform(vecAll,vecCanonical,aH);
         std::array<unsigned,9> arrCount{};
         std::vector<cv::Point2f> vecSelected;
         for(std::size_t uIndex=0;uIndex<vecAll.size();++uIndex) {
            cv::Point2f const& aPoint=vecCanonical[uIndex];
            if(!std::isfinite(aPoint.x)||!std::isfinite(aPoint.y) ||
               aPoint.x<0.f||aPoint.y<0.f||aPoint.x>=3.f||aPoint.y>=3.f)
               continue;
            int const iCellY=std::clamp(static_cast<int>(aPoint.y),0,2);
            int const iCellX=std::clamp(static_cast<int>(aPoint.x),0,2);
            unsigned& uCount=arrCount[3*iCellY+iCellX];
            if(uCount++>=24)continue;
            vecSelected.push_back(vecAll[uIndex]);
         }
         return vecSelected;
      }

      /**
      \brief Validate a new projected quadrilateral.
      \details Reject implausible area changes and point inversions.
      \param arrCandidate Proposed corners.
      \returns True for finite, convex, plausible card geometry.
      \throw None.
      */
      [[nodiscard]] bool Plausible(
         std::array<cv::Point2f,4> const& arrCandidate) const {
         std::vector<cv::Point2f> vecOld(
            m_arrCorners.begin(),m_arrCorners.end());
         std::vector<cv::Point2f> vecNew(
            arrCandidate.begin(),arrCandidate.end());
         double const flOld=cv::contourArea(vecOld);
         double const flNew=cv::contourArea(vecNew);
         if(!std::isfinite(flNew)||std::abs(flNew)<100.0 ||
            std::abs(flOld)<100.0 || flOld*flNew<=0.0)return false;
         double const flRatio=std::abs(flNew/flOld);
         if(flRatio<0.65 || flRatio>1.55)return false;
         for(auto const& aPoint:arrCandidate) {
            if(!std::isfinite(aPoint.x)||!std::isfinite(aPoint.y) ||
               aPoint.x<0.f||aPoint.y<0.f||
               aPoint.x>=m_aFrameSize.width ||
               aPoint.y>=m_aFrameSize.height)return false;
         }
         return true;
      }

      /**
      \brief Calculate whole-card contrast relative to its immediate surround.
      \details This is a local presence fallback for texture-poor cards, not
      a new contour/artwork recognizer. It cannot determine a printing.
      \param aGray Full camera frame converted to grayscale.
      \returns Card interior mean and signed interior/background contrast.
      \throw cv::Exception If the OpenCV masking operation fails.
      */
      [[nodiscard]] std::pair<double,double> Contrast(
         cv::Mat const& aGray) const {
         std::vector<cv::Point> vecPolygon;
         for(auto const& aPoint:m_arrCorners)
            vecPolygon.emplace_back(cvRound(aPoint.x),cvRound(aPoint.y));
         cv::Rect const aBox=cv::boundingRect(vecPolygon);
         int const iMargin=std::max(12,std::min(aBox.width,aBox.height)/8);
         cv::Rect const aRegion=cv::Rect(
            aBox.x-iMargin,aBox.y-iMargin,
            aBox.width+2*iMargin,aBox.height+2*iMargin) &
            cv::Rect(0,0,aGray.cols,aGray.rows);
         if(aRegion.width<20||aRegion.height<20)return {0.0,0.0};
         for(cv::Point& aPoint:vecPolygon)aPoint-=aRegion.tl();
         cv::Mat aInside(aRegion.size(),CV_8UC1,cv::Scalar(0));
         cv::fillConvexPoly(aInside,vecPolygon,cv::Scalar(255));
         cv::Mat aOutside;
         cv::bitwise_not(aInside,aOutside);
         double const flInside=cv::mean(aGray(aRegion),aInside)[0];
         double const flOutside=cv::mean(aGray(aRegion),aOutside)[0];
         return {flInside,flInside-flOutside};
      }

      /**
      \brief Validate stationary presence using whole-card border contrast.
      \details Used only when the card has too few optical-flow features.
      \param aGray Current grayscale camera frame.
      \returns True when the known outer card is still visually present.
      \throw cv::Exception If image masking fails.
      */
      [[nodiscard]] bool ContrastPresent(cv::Mat const& aGray) const {
         if(std::abs(m_flInitialContrast)<18.0)return false;
         auto const [flInside,flContrast]=Contrast(aGray);
         return flContrast*m_flInitialContrast>0.0 &&
            std::abs(flContrast)>=std::abs(m_flInitialContrast)*0.55 &&
            std::abs(flInside-m_flInitialInside)<=35.0;
      }

   public:
      /**
      \brief Begin tracking a physical card identified by its outer boundary.
      \details No further global contour search is needed on successful frames.
      \param aFrame First camera frame.
      \param arrCorners Four card vertices in clockwise winding order.
      \returns Whether sufficient spatial features are available to lock.
      \throw cv::Exception If initialization fails.
      */
      [[nodiscard]] bool Begin(cv::Mat const& aFrame,
         std::array<cv::Point2f,4> const& arrCorners) {
         Reset();
         if(aFrame.empty())return false;
         if(aFrame.channels()==1)m_aPreviousGray=aFrame.clone();
         else cv::cvtColor(aFrame,m_aPreviousGray,
            aFrame.channels()==4?cv::COLOR_BGRA2GRAY:cv::COLOR_BGR2GRAY);
         m_aFrameSize=aFrame.size();
         m_arrCorners=arrCorners;
         m_vecPoints=Features(m_aPreviousGray,m_arrCorners);
         auto const [flInside,flContrast]=Contrast(m_aPreviousGray);
         m_flInitialInside=flInside;
         m_flInitialContrast=flContrast;
         // The physical-object lock persists even without sufficient feature
         // points. In that case Track reports missing observations; it must
         // never fall back to global artwork/contour search during this card.
         m_boActive=true;
         return m_vecPoints.size()>=12;
      }

      /**
      \brief Advance the same physical card without searching for new outlines.
      \details Tracking failures count toward removal; occasional lost frames
      never cause an unrelated inner artwork rectangle to be accepted.
      \param aFrame Next full camera frame.
      \returns Four corners when tracked, or no corners when temporarily lost.
      \throw cv::Exception If optical flow fails.
      */
      [[nodiscard]] std::optional<std::array<cv::Point2f,4>>
         Track(cv::Mat const& aFrame) {
         if(!m_boActive || aFrame.empty() ||
            aFrame.size()!=m_aFrameSize)return std::nullopt;
         cv::Mat aCurrent;
         if(aFrame.channels()==1)aCurrent=aFrame;
         else cv::cvtColor(aFrame,aCurrent,
            aFrame.channels()==4?cv::COLOR_BGRA2GRAY:cv::COLOR_BGR2GRAY);
         if(m_vecPoints.size()<12)
            m_vecPoints=Features(m_aPreviousGray,m_arrCorners);
         std::vector<cv::Point2f> vecNext;
         std::vector<unsigned char> vecStatus;
         std::vector<float> vecError;
         if(m_vecPoints.size()>=12) {
            // Keep LK pyramids bounded to the last known object and a
            // 20-percent motion margin instead of processing the full frame.
            std::vector<cv::Point2f> vecPolygon(
               m_arrCorners.begin(),m_arrCorners.end());
            cv::Rect const aBox=cv::boundingRect(vecPolygon);
            int const iMargin=std::max(25,
               std::max(aBox.width,aBox.height)/5);
            cv::Rect const aRegion=cv::Rect(
               aBox.x-iMargin,aBox.y-iMargin,
               aBox.width+2*iMargin,aBox.height+2*iMargin) &
               cv::Rect(0,0,aCurrent.cols,aCurrent.rows);
            if(aRegion.width<=40||aRegion.height<=40) {
               ++m_uMissed;
               return std::nullopt;
            }
            std::vector<cv::Point2f> vecLocal=m_vecPoints;
            for(cv::Point2f& aPoint:vecLocal) {
               aPoint.x-=static_cast<float>(aRegion.x);
               aPoint.y-=static_cast<float>(aRegion.y);
            }
            cv::calcOpticalFlowPyrLK(
               m_aPreviousGray(aRegion),aCurrent(aRegion),
               vecLocal,vecNext,vecStatus,vecError,
               cv::Size(21,21),3);
            for(cv::Point2f& aPoint:vecNext) {
               aPoint.x+=static_cast<float>(aRegion.x);
               aPoint.y+=static_cast<float>(aRegion.y);
            }
            std::vector<cv::Point2f> vecBefore,vecAfter;
            for(std::size_t uIndex=0;uIndex<vecStatus.size();++uIndex) {
               if(vecStatus[uIndex] && vecError[uIndex]<30.f) {
                  vecBefore.push_back(m_vecPoints[uIndex]);
                  vecAfter.push_back(vecNext[uIndex]);
               }
            }
            if(vecAfter.size()>=12) {
               cv::Mat aInliers;
               cv::Mat const aH=cv::findHomography(vecBefore,vecAfter,
                  cv::RANSAC,3.0,aInliers);
               int const iInliers=aInliers.empty()?0:cv::countNonZero(aInliers);
               if(!aH.empty()&&iInliers>=10 &&
                  static_cast<double>(iInliers)/vecAfter.size()>=0.60) {
                  std::vector<cv::Point2f> vecOld(
                     m_arrCorners.begin(),m_arrCorners.end()),vecNew;
                  cv::perspectiveTransform(vecOld,vecNew,aH);
                  std::array<cv::Point2f,4> arrNew{};
                  if(vecNew.size()==4)std::copy(vecNew.begin(),vecNew.end(),
                     arrNew.begin());
                  if(vecNew.size()==4 && Plausible(arrNew)) {
                     m_arrCorners=arrNew;
                     m_aPreviousGray=aCurrent.clone();
                     m_vecPoints=Features(m_aPreviousGray,m_arrCorners);
                     m_uMissed=0;
                     return m_arrCorners;
                  }
               }
            }
         }
         // Texture-poor card fallback: check the SAME full-card silhouette
         // and its background contrast, never search for a new inner contour.
         if(m_vecPoints.size()<12 && ContrastPresent(aCurrent)) {
            m_aPreviousGray=aCurrent.clone();
            m_uMissed=0;
            return m_arrCorners;
         }
         ++m_uMissed;
         // Leave the preceding reference frame untouched on a failed update.
         return std::nullopt;
      }

      /**
      \brief Release all image features for the current physical card.
      \details Subsequent frames may then be globally detected anew.
      \returns Nothing.
      \throw None.
      */
      void Reset() noexcept {
         m_boActive=false;m_uMissed=0;
         m_vecPoints.clear();m_aPreviousGray.release();
         m_aFrameSize={};
         m_flInitialInside=0.0;
         m_flInitialContrast=0.0;
      }

      [[nodiscard]] bool Active() const noexcept{return m_boActive;}
      [[nodiscard]] bool Lost() const noexcept{
         return m_boActive && m_uMissed>=cuMaxMissed;
      }
      [[nodiscard]] unsigned Missed() const noexcept{return m_uMissed;}
   };
}
