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
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <cstddef>
#include <optional>
#include <random>
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
      unsigned m_uFrameSerial{};
      double m_flInitialInside{};
      double m_flInitialContrast{};
      static constexpr unsigned cuMaxMissed=45;

      /**
      \brief Fit a projective transform to all RANSAC inliers using core.
      \details Normalized linear least squares estimates the eight independent
      homography coefficients, with h22 fixed to one. Hartley normalization
      avoids ill conditioning when the full camera frame is 4K.
      \param vecFrom Original tracked feature positions.
      \param vecTo Current feature positions.
      \param vecInliers Indices of previously accepted feature correspondences.
      \returns A 3x3 CV_64F homography or an empty matrix on failure.
      \throw cv::Exception If OpenCV matrix operations fail.
      */
      [[nodiscard]] static cv::Mat RefitHomography(
         std::vector<cv::Point2f> const& vecFrom,
         std::vector<cv::Point2f> const& vecTo,
         std::vector<std::size_t> const& vecInliers) {
         if(vecInliers.size()<4)return {};
         double flFromX{},flFromY{},flToX{},flToY{};
         for(std::size_t const uIndex:vecInliers) {
            flFromX+=vecFrom[uIndex].x;flFromY+=vecFrom[uIndex].y;
            flToX+=vecTo[uIndex].x;flToY+=vecTo[uIndex].y;
         }
         double const flN=static_cast<double>(vecInliers.size());
         flFromX/=flN;flFromY/=flN;flToX/=flN;flToY/=flN;
         double flFromDistance{},flToDistance{};
         for(std::size_t const uIndex:vecInliers) {
            flFromDistance+=std::hypot(
               vecFrom[uIndex].x-flFromX,vecFrom[uIndex].y-flFromY);
            flToDistance+=std::hypot(
               vecTo[uIndex].x-flToX,vecTo[uIndex].y-flToY);
         }
         if(flFromDistance<=1.0e-6 || flToDistance<=1.0e-6)return {};
         double const flSourceScale=std::sqrt(2.0)*flN/flFromDistance;
         double const flTargetScale=std::sqrt(2.0)*flN/flToDistance;
         int const iRows=static_cast<int>(vecInliers.size()*2);
         cv::Mat aSystem=cv::Mat::zeros(iRows,8,CV_64F);
         cv::Mat aRight(iRows,1,CV_64F);
         for(std::size_t uRow=0;uRow<vecInliers.size();++uRow) {
            std::size_t const uPoint=vecInliers[uRow];
            double const flX=(vecFrom[uPoint].x-flFromX)*flSourceScale;
            double const flY=(vecFrom[uPoint].y-flFromY)*flSourceScale;
            double const flU=(vecTo[uPoint].x-flToX)*flTargetScale;
            double const flV=(vecTo[uPoint].y-flToY)*flTargetScale;
            double* pFirst=aSystem.ptr<double>(static_cast<int>(2*uRow));
            double* pSecond=aSystem.ptr<double>(static_cast<int>(2*uRow+1));
            pFirst[0]=flX;pFirst[1]=flY;pFirst[2]=1.0;
            pFirst[6]=-flU*flX;pFirst[7]=-flU*flY;
            pSecond[3]=flX;pSecond[4]=flY;pSecond[5]=1.0;
            pSecond[6]=-flV*flX;pSecond[7]=-flV*flY;
            aRight.at<double>(static_cast<int>(2*uRow))=flU;
            aRight.at<double>(static_cast<int>(2*uRow+1))=flV;
         }
         cv::Mat aSolution;
         if(!cv::solve(aSystem,aRight,aSolution,cv::DECOMP_SVD))return {};
         cv::Matx33d const aNormalized(
            aSolution.at<double>(0),aSolution.at<double>(1),
            aSolution.at<double>(2),aSolution.at<double>(3),
            aSolution.at<double>(4),aSolution.at<double>(5),
            aSolution.at<double>(6),aSolution.at<double>(7),1.0);
         cv::Matx33d const aSourceTransform(
            flSourceScale,0.0,-flSourceScale*flFromX,
            0.0,flSourceScale,-flSourceScale*flFromY,0.0,0.0,1.0);
         cv::Matx33d const aTargetTransform(
            flTargetScale,0.0,-flTargetScale*flToX,
            0.0,flTargetScale,-flTargetScale*flToY,0.0,0.0,1.0);
         cv::Mat const aResult(
            aTargetTransform.inv()*aNormalized*aSourceTransform);
         return aResult;
      }

      /**
      \brief Robustly estimate a homography without the calib3d module.
      \details Deterministic RANSAC selects four nondegenerate optical-flow
      pairs per trial. imgproc provides the four-point perspective transform,
      core provides projection and final normalized least-squares refinement.
      This preserves the previous robust projective tracker in the locked
      OpenCV stack without linking optional calib3d.
      \param vecFrom Original image features.
      \param vecTo Corresponding optical-flow locations.
      \param uSeed Per-frame deterministic random seed.
      \returns Projective matrix and accepted inlier count, or empty/zero.
      \throw cv::Exception If an OpenCV transform fails.
      */
      [[nodiscard]] static std::pair<cv::Mat,int> EstimateHomographyRansac(
         std::vector<cv::Point2f> const& vecFrom,
         std::vector<cv::Point2f> const& vecTo,unsigned uSeed) {
         if(vecFrom.size()!=vecTo.size() || vecFrom.size()<10)return {{},0};
         std::minstd_rand aGenerator(0x5a31u+uSeed);
         std::uniform_int_distribution<std::size_t> aSampleIndex(
            0,vecFrom.size()-1);
         std::vector<std::size_t> vecBestInliers;
         cv::Mat aBest;
         double flBestError=std::numeric_limits<double>::infinity();
         constexpr double cflMaxSquareError=9.0;
         for(unsigned uTrial=0;uTrial<96;++uTrial) {
            std::array<std::size_t,4> arrIndices{};
            std::array<cv::Point2f,4> arrBefore{},arrAfter{};
            for(std::size_t uPart=0;uPart<4;++uPart) {
               std::size_t uCandidate{};
               do {
                  uCandidate=aSampleIndex(aGenerator);
               } while(std::find(arrIndices.begin(),
                  arrIndices.begin()+static_cast<std::ptrdiff_t>(uPart),
                  uCandidate)!=
                  arrIndices.begin()+static_cast<std::ptrdiff_t>(uPart));
               arrIndices[uPart]=uCandidate;
               arrBefore[uPart]=vecFrom[uCandidate];
               arrAfter[uPart]=vecTo[uCandidate];
            }
            // Four points must not collapse to a line in either camera frame.
            auto const Nondegenerate=[](
               std::array<cv::Point2f,4> const& arrPoints) {
               for(int iA=0;iA<4;++iA)
                  for(int iB=iA+1;iB<4;++iB)
                     for(int iC=iB+1;iC<4;++iC) {
                        cv::Point2f const aU=arrPoints[iB]-arrPoints[iA];
                        cv::Point2f const aV=arrPoints[iC]-arrPoints[iA];
                        if(std::abs(aU.x*aV.y-aU.y*aV.x)<30.f)
                           return false;
                     }
               return true;
            };
            if(!Nondegenerate(arrBefore) || !Nondegenerate(arrAfter))
               continue;
            cv::Mat const aModel=cv::getPerspectiveTransform(
               arrBefore.data(),arrAfter.data(),cv::DECOMP_LU);
            if(aModel.empty())continue;
            std::vector<cv::Point2f> vecProjected;
            cv::perspectiveTransform(vecFrom,vecProjected,aModel);
            std::vector<std::size_t> vecInliers;
            double flError{};
            for(std::size_t uIndex=0;uIndex<vecProjected.size();++uIndex) {
               double const flDx=static_cast<double>(
                  vecProjected[uIndex].x)-vecTo[uIndex].x;
               double const flDy=static_cast<double>(
                  vecProjected[uIndex].y)-vecTo[uIndex].y;
               double const flSquare=flDx*flDx+flDy*flDy;
               if(!std::isfinite(flSquare) ||
                  flSquare>cflMaxSquareError)continue;
               vecInliers.push_back(uIndex);
               flError+=flSquare;
            }
            if(vecInliers.size()>vecBestInliers.size() ||
               (vecInliers.size()==vecBestInliers.size() &&
                  flError<flBestError)) {
               vecBestInliers=std::move(vecInliers);
               flBestError=flError;
               aBest=aModel;
            }
            if(uTrial>=12 && vecBestInliers.size()==vecFrom.size())
               break;
         }
         if(vecBestInliers.size()<10 ||
            static_cast<double>(vecBestInliers.size())/vecFrom.size()<0.60)
            return {{},0};
         cv::Mat const aRefined=RefitHomography(
            vecFrom,vecTo,vecBestInliers);
         return {
            aRefined.empty()?aBest:aRefined,
            static_cast<int>(vecBestInliers.size())
         };
      }

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
         // Do not accept an optical lock supported only by the artwork zone.
         // Require feature coverage across multiple rows and columns of the
         // physical card. If absent, use the whole-card contrast fallback.
         std::array<bool,3> arrRows{},arrColumns{};
         unsigned uOccupied{};
         for(int iCell=0;iCell<9;++iCell) {
            if(arrCount[iCell]==0)continue;
            ++uOccupied;
            arrRows[iCell/3]=true;
            arrColumns[iCell%3]=true;
         }
         unsigned const uRows=static_cast<unsigned>(
            std::count(arrRows.begin(),arrRows.end(),true));
         unsigned const uColumns=static_cast<unsigned>(
            std::count(arrColumns.begin(),arrColumns.end(),true));
         if(uOccupied<4 || uRows<2 || uColumns<2)return {};
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
               // Projective RANSAC from core/imgproc: OpenCV 5.0.0 in the
               // pinned BuildEngine stack does not provide calib3d.
               auto const [aH,iInliers]=EstimateHomographyRansac(
                  vecBefore,vecAfter,++m_uFrameSerial);
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
         m_boActive=false;m_uMissed=0;m_uFrameSerial=0;
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
