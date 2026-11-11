// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file CameraTableFocus.h
\brief Pre-focus a fixed overhead camera on the physical tabletop.
\details Autofocus is initially allowed to settle. Its focus positions are
   evaluated from the central table region BEFORE any card contour or OCR
   is required. The best observed manual position is then applied and
   verified if the backend supports focus control. Inconclusive control
   falls back to the original continuous autofocus.
\note OpenCV does not provide a portable hardware autofocus ROI API.
   The local ROI scores actual frames; it does not command the camera
   driver to use this ROI for its internal autofocus algorithm.
*/
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include "CameraTrace.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <optional>
#include <print>
#include <string>

namespace deckkernel::camera {
   /**
   \brief Calibrate the table focus without depending on card recognition.
   \details The calibration is a separate camera phase, not a physical card
      or matching session. Never guess the camera-specific manual focus range.
   */
   class TablePlaneFocus final {
   public:
      enum class State {
         Continuous,
         Settling,
         Verifying,
         Locked,
         Fallback
      };
      struct Measurement {
         double flSharpness{};
         double flContrast{};
         bool boUsable{};
      };
   private:
      using clock_ty=std::chrono::steady_clock;
      State m_eState{State::Continuous};
      std::optional<int> m_optManualPosition;
      bool m_boTableMode{};
      clock_ty::time_point m_started{};
      cv::Mat m_previous;
      double m_flBestSharpness{};
      double m_flBestPosition{-1.0};
      double m_flLockedSharpness{};
      unsigned m_uStableSamples{};
      unsigned m_uVerifySamples{};
      std::string m_strStatus{"Continuous autofocus"};

      /**
      \brief Restore ordinary camera autofocus on an unsupported driver.
      \details This never changes the existing card recognition state.
      \param aCapture OpenCV camera whose focus control was attempted.
      \param svReason Human-readable diagnostic reason.
      \returns Nothing.
      \throw None.
      */
      void Fallback(cv::VideoCapture& aCapture,char const* szReason) {
         bool const boRestored=aCapture.set(cv::CAP_PROP_AUTOFOCUS,1.0);
         m_eState=State::Fallback;
         m_strStatus="Table focus unavailable - continuous autofocus";
         deckkernel::camera::Trace("[CAM-TABLE-FOCUS] fallback={} autofocus-restored={} "
            "autofocus={} focus={}",szReason,boRestored,
            aCapture.get(cv::CAP_PROP_AUTOFOCUS),
            aCapture.get(cv::CAP_PROP_FOCUS));
      }

      /**
      \brief Apply a manual focus position only when the backend confirms it.
      \details A successful set() request is insufficient without readback;
         OpenCV 5 returns a negative get() value for unsupported controls.
      \param aCapture Camera to configure.
      \param flPosition Device-specific focus value, never a guessed distance.
      \returns True when the focus control was applied and read back.
      \throw None.
      */
      [[nodiscard]] static bool SetManual(cv::VideoCapture& aCapture,
         double flPosition) {
         if(!std::isfinite(flPosition) || flPosition<0.0)return false;
         bool const boManual=aCapture.set(cv::CAP_PROP_AUTOFOCUS,0.0);
         double const flAuto=aCapture.get(cv::CAP_PROP_AUTOFOCUS);
         if(!boManual || flAuto<0.0 || flAuto>0.5)return false;
         if(!aCapture.set(cv::CAP_PROP_FOCUS,flPosition))return false;
         double const flReported=aCapture.get(cv::CAP_PROP_FOCUS);
         return std::isfinite(flReported) && flReported>=0.0 &&
            std::abs(flReported-flPosition)<=
               std::max(5.0,0.05*std::abs(flPosition));
      }

   public:
      /**
      \brief Construct a calibration controller.
      \details Table mode prefocuses before acquisition; an explicit focus
         value overrides any autofocus calibration.
      \param boTableMode Enable fixed table-plane pre-focus.
      \param optManualPosition Optional user-provided device focus value.
      \throw None.
      */
      explicit TablePlaneFocus(bool boTableMode,
         std::optional<int> optManualPosition={})
         : m_optManualPosition(optManualPosition),
           m_boTableMode(boTableMode) {}

      /**
      \brief Central tabletop area evaluated independently of any card contour.
      \details Approximately the middle 30% by 60% of the camera sensor.
      \param aSize Original camera frame dimensions.
      \returns The native-resolution calibration ROI.
      \throw None.
      */
      [[nodiscard]] static cv::Rect FocusRegion(cv::Size const& aSize) {
         return cv::Rect(aSize.width*35/100,aSize.height*20/100,
            std::max(1,aSize.width*30/100),
            std::max(1,aSize.height*60/100));
      }

      /**
      \brief Evaluate optical detail in the expected tabletop region.
      \details A Laplacian sharpness score is meaningful for comparisons
         of the SAME stationary table scene, not across unrelated pictures.
      \param aFrame Original frame without artificial sharpening.
      \returns Contrast and sharpness, with a low-texture quality guard.
      \throw cv::Exception If OpenCV preprocessing fails.
      */
      [[nodiscard]] static Measurement Measure(cv::Mat const& aFrame) {
         if(aFrame.empty() || aFrame.cols<100 || aFrame.rows<100)return {};
         cv::Rect const aArea=FocusRegion(aFrame.size())&
            cv::Rect(0,0,aFrame.cols,aFrame.rows);
         cv::Mat aSmall,aGray;
         cv::resize(aFrame(aArea),aSmall,cv::Size(320,240),
            0.0,0.0,cv::INTER_AREA);
         if(aSmall.channels()==4)
            cv::cvtColor(aSmall,aGray,cv::COLOR_BGRA2GRAY);
         else if(aSmall.channels()==3)
            cv::cvtColor(aSmall,aGray,cv::COLOR_BGR2GRAY);
         else
            aGray=aSmall;
         cv::Scalar aMean,aDeviation;
         cv::meanStdDev(aGray,aMean,aDeviation);
         cv::Mat aLap;
         cv::Laplacian(aGray,aLap,CV_32F);
         cv::Scalar aLapMean,aLapDeviation;
         cv::meanStdDev(aLap,aLapMean,aLapDeviation);
         Measurement aResult;
         aResult.flSharpness=aLapDeviation[0]*aLapDeviation[0];
         aResult.flContrast=aDeviation[0];
         aResult.boUsable=aResult.flContrast>=9.0 &&
            aResult.flSharpness>=12.0 && aMean[0]>=30.0 &&
            aMean[0]<=235.0;
         return aResult;
      }

      /**
      \brief Start table-plane prefocus before any card search.
      \details Camera autofocus searches normally during the short settling
         interval; a previous hardware setting is not assumed to persist.
      \param aCapture Open and configured video capture.
      \returns Nothing.
      \throw None.
      */
      void Begin(cv::VideoCapture& aCapture) {
         m_previous.release();
         m_flBestSharpness=0.0;
         m_flBestPosition=-1.0;
         m_flLockedSharpness=0.0;
         m_uStableSamples=0;
         m_uVerifySamples=0;
         m_started=clock_ty::now();
         if(m_optManualPosition) {
            double const flPosition=static_cast<double>(
               *m_optManualPosition);
            if(SetManual(aCapture,flPosition)) {
               m_eState=State::Locked;
               m_strStatus=std::format(
                  "Manual tabletop focus locked at {}",flPosition);
               deckkernel::camera::Trace("[CAM-TABLE-FOCUS] manual position={} "
                  "reported={}",flPosition,
                  aCapture.get(cv::CAP_PROP_FOCUS));
            }
            else Fallback(aCapture,"manual preset unsupported");
            return;
         }
         if(!m_boTableMode) {
            bool const boAccepted=
               aCapture.set(cv::CAP_PROP_AUTOFOCUS,1.0);
            m_eState=State::Continuous;
            m_strStatus="Continuous autofocus";
            deckkernel::camera::Trace("[CAM-TABLE-FOCUS] mode=continuous accepted={} "
               "reported={}",boAccepted,
               aCapture.get(cv::CAP_PROP_AUTOFOCUS));
            return;
         }
         bool const boAccepted=
            aCapture.set(cv::CAP_PROP_AUTOFOCUS,1.0);
         double const flReported=aCapture.get(cv::CAP_PROP_AUTOFOCUS);
         if(!boAccepted || flReported<0.0 || flReported<0.5) {
            Fallback(aCapture,"autofocus control unsupported");
            return;
         }
         double const flFocus=aCapture.get(cv::CAP_PROP_FOCUS);
         if(!std::isfinite(flFocus) || flFocus<0.0) {
            Fallback(aCapture,"manual focus position unreadable");
            return;
         }
         m_eState=State::Settling;
         m_strStatus="Calibrating focus at the tabletop";
         deckkernel::camera::Trace("[CAM-TABLE-FOCUS] settle start position={} "
            "ROI=center 30%x60% (software scoring)",flFocus);
      }

      /**
      \brief Sample frames while pre-focusing the tabletop.
      \details The best observed focus position is frozen manually only
         after stable textured images; the result is verified on subsequent
         original frames. Poor results restore continuous autofocus.
      \param aCapture Camera with device focus controls.
      \param aFrame Unmodified camera frame.
      \returns True while camera acquisition should wait for calibration.
      \throw cv::Exception On OpenCV image processing errors.
      */
      [[nodiscard]] bool Observe(cv::VideoCapture& aCapture,
         cv::Mat const& aFrame) {
         if(!Calibrating() || aFrame.empty())return false;
         cv::Rect const aArea=FocusRegion(aFrame.size());
         cv::Mat aSmall,aGray;
         cv::resize(aFrame(aArea),aSmall,cv::Size(120,90),
            0.0,0.0,cv::INTER_AREA);
         if(aSmall.channels()==4)
            cv::cvtColor(aSmall,aGray,cv::COLOR_BGRA2GRAY);
         else if(aSmall.channels()==3)
            cv::cvtColor(aSmall,aGray,cv::COLOR_BGR2GRAY);
         else
            aGray=aSmall;
         bool boStable=true;
         if(!m_previous.empty()) {
            cv::Mat aDelta;
            cv::absdiff(aGray,m_previous,aDelta);
            boStable=cv::mean(aDelta)[0]<8.0;
         }
         m_previous=aGray.clone();
         Measurement const aMetric=Measure(aFrame);
         double const flElapsed=std::chrono::duration<double>(
            clock_ty::now()-m_started).count();
         if(m_eState==State::Settling) {
            double const flFocus=aCapture.get(cv::CAP_PROP_FOCUS);
            if(boStable && aMetric.boUsable && flFocus>=0.0 &&
               std::isfinite(flFocus)) {
               ++m_uStableSamples;
               if(aMetric.flSharpness>m_flBestSharpness) {
                  m_flBestSharpness=aMetric.flSharpness;
                  m_flBestPosition=flFocus;
               }
            }
            if(flElapsed<2.0 ||
               (m_uStableSamples<8 && flElapsed<4.0))
               return true;
            if(m_uStableSamples<8 || m_flBestPosition<0.0) {
               Fallback(aCapture,"no stable textured tabletop frames");
               return false;
            }
            if(!SetManual(aCapture,m_flBestPosition)) {
               Fallback(aCapture,"focus lock not supported/read back");
               return false;
            }
            m_eState=State::Verifying;
            m_started=clock_ty::now();
            m_uVerifySamples=0;
            m_flLockedSharpness=0.0;
            m_strStatus="Verifying locked tabletop focus";
            deckkernel::camera::Trace("[CAM-TABLE-FOCUS] selected position={} "
               "best-roi-sharpness={:.1f} stable-samples={}",
               m_flBestPosition,m_flBestSharpness,m_uStableSamples);
            return true;
         }
         if(boStable && aMetric.boUsable) {
            m_flLockedSharpness=std::max(
               m_flLockedSharpness,aMetric.flSharpness);
            ++m_uVerifySamples;
         }
         if(flElapsed<0.6 ||
            (m_uVerifySamples<8 && flElapsed<1.2))return true;
         if(m_uVerifySamples<3 ||
            m_flLockedSharpness<m_flBestSharpness*0.55) {
            Fallback(aCapture,"manual lock lost tabletop sharpness");
            return false;
         }
         m_eState=State::Locked;
         m_strStatus=std::format("Tabletop focus locked at {:.0f}",
            aCapture.get(cv::CAP_PROP_FOCUS));
         deckkernel::camera::Trace("[CAM-TABLE-FOCUS] locked position={} "
            "before={:.1f} after={:.1f} verified-frames={}",
            aCapture.get(cv::CAP_PROP_FOCUS),m_flBestSharpness,
            m_flLockedSharpness,m_uVerifySamples);
         return false;
      }

      /**
      \brief Whether image recognition must wait for table calibration.
      \details Used independently of the existing card-capture phases.
      \returns True while the hardware focus is settling or being verified.
      \throw None.
      */
      [[nodiscard]] bool Calibrating() const noexcept {
         return m_eState==State::Settling ||
            m_eState==State::Verifying;
      }

      /**
      \brief Current camera focus diagnostic for display.
      \details Distinguishes verified hardware lock from autofocus fallback.
      \returns Persistent camera focus status.
      \throw None.
      */
      [[nodiscard]] std::string const& Status() const noexcept {
         return m_strStatus;
      }
   };
} // namespace deckkernel::camera
