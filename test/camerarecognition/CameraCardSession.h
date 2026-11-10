// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/**
\file CameraCardSession.h
\brief Explicit physical-card, capture, and card-identity models.
\details Each phase owns only its own evidence. The camera never reuses
a printing's similarity score to decide whether a physical object remains
present. An immutable capture is shared by all image recognition stages.
*/
#include "CameraImageQuality.h"
#include <opencv2/core.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace deckkernel::camera {
   /**
   \brief Named stages for logs, demonstrations, and the camera dashboard.
   \details State changes are made only by the owning session controller.
   */
   enum class RecognitionPhase {
      Searching,
      Acquiring,
      Tracking,
      Focusing,
      Frozen,
      Artwork,
      Printing,
      PrintingOcr,
      Selecting,
      Confirmed,
      Unresolved
   };

   /**
   \brief Return the stable English name for a recognition phase.
   \details Names are intentionally independent of diagnostic messages.
   \param ePhase Current state.
   \returns Name for the on-screen demonstration.
   \throw None.
   */
   [[nodiscard]] inline char const* PhaseName(RecognitionPhase ePhase) noexcept {
      switch(ePhase) {
         case RecognitionPhase::Searching: return "01 SEARCHING";
         case RecognitionPhase::Acquiring: return "02 ACQUIRING";
         case RecognitionPhase::Tracking: return "03 TRACKING";
         case RecognitionPhase::Focusing: return "04 FOCUSING";
         case RecognitionPhase::Frozen: return "05 FROZEN";
         case RecognitionPhase::Artwork: return "06 ARTWORK";
         case RecognitionPhase::Printing: return "07 PRINTING";
         case RecognitionPhase::PrintingOcr: return "08 PRINTING OCR";
         case RecognitionPhase::Selecting: return "09 SELECT PRINTING";
         case RecognitionPhase::Confirmed: return "10 CONFIRMED";
         case RecognitionPhase::Unresolved: return "UNRESOLVED";
      }
      return "UNKNOWN";
   }

   /**
   \brief The immutable image evidence produced by a single capture session.
   \details Focus and quality selection may update the best frame until Freeze().
   After Freeze(), later tracking frames cannot change the image used by
   artwork matching, full-card matching, or optional printing OCR.
   */
   class CardCapture final {
      cv::Mat m_aBest;
      cv::Mat m_aFrozen;
      cv::Mat m_aBottom;
      double m_flBestScore{-1.0};
      double m_flSharpness{};
      unsigned m_uSamples{};
      bool m_boFrozen{};
      std::chrono::steady_clock::time_point m_started{};
   public:
      /**
      \brief Start a new capture of a physical card.
      \details Replaces all evidence from the preceding physical session.
      \param aInitial First rectified card image.
      \returns Nothing.
      \throw cv::Exception If image processing fails.
      */
      void Begin(cv::Mat const& aInitial) {
         Clear();
         m_started=std::chrono::steady_clock::now();
         Offer(aInitial);
      }

      /**
      \brief Offer an additional registered frame for quality selection.
      \details The score penalizes clipped highlights, darkness, and blur.
      \param aRectified Full-resolution image of the same physical card.
      \returns True if this image became the current best sample.
      \throw cv::Exception If image processing fails.
      */
      [[nodiscard]] bool Offer(cv::Mat const& aRectified) {
         if(m_boFrozen || aRectified.empty())return false;
         auto const aMetrics=deckkernel_camera::quality::Measure(aRectified);
         ++m_uSamples;
         if(aMetrics.score<=m_flBestScore)return false;
         m_flBestScore=aMetrics.score;
         m_flSharpness=aMetrics.sharpness;
         m_aBest=aRectified.clone();
         return true;
      }

      /**
      \brief Check whether focus settling has completed.
      \details The time bound ensures progress on cameras with unstable focus.
      \returns True after 18 samples or 2 seconds.
      \throw None.
      */
      [[nodiscard]] bool Ready() const noexcept {
         return m_uSamples>=18 ||
            std::chrono::duration<double>(
               std::chrono::steady_clock::now()-m_started).count()>=2.0;
      }

      /**
      \brief Freeze the selected capture and its high-resolution lower margin.
      \details No future Offer() may modify the frozen image.
      \returns True if at least one image is available.
      \throw cv::Exception For an invalid OpenCV image operation.
      */
      [[nodiscard]] bool Freeze() {
         if(m_boFrozen)return true;
         if(m_aBest.empty())return false;
         m_aFrozen=std::move(m_aBest);
         // Final OCR escalation uses original frozen pixels, never upscaled
         // synthetic details. The crop is independent of artwork extraction.
         int const iTop=m_aFrozen.rows*85/100;
         m_aBottom=m_aFrozen(cv::Rect(0,iTop,
            m_aFrozen.cols,m_aFrozen.rows-iTop)).clone();
         m_boFrozen=true;
         return true;
      }

      /**
      \brief Clear one physical-card capture.
      \details All prior card pixels are discarded.
      \returns Nothing.
      \throw None.
      */
      void Clear() noexcept {
         m_aBest.release();m_aFrozen.release();m_aBottom.release();
         m_flBestScore=-1.0;m_flSharpness=0.0;m_uSamples=0;
         m_boFrozen=false;
      }

      [[nodiscard]] bool Frozen() const noexcept{return m_boFrozen;}
      [[nodiscard]] cv::Mat const& Image() const noexcept{
         return m_boFrozen?m_aFrozen:m_aBest;
      }
      [[nodiscard]] cv::Mat const& Bottom() const noexcept{return m_aBottom;}
      [[nodiscard]] double Sharpness() const noexcept{return m_flSharpness;}
      [[nodiscard]] double Score() const noexcept{return m_flBestScore;}
      [[nodiscard]] unsigned Samples() const noexcept{return m_uSamples;}
   };

   /**
   \brief Candidate for a particular Scryfall printing and card face.
   \details Printings are distinct by Scryfall ID and face number, not by
   each indexed image size or format.
   */
   struct PrintingCandidate {
      std::string strAssetId;
      std::string strPrintingId;
      std::string strOracleId;
      std::string strName;
      std::string strTypeLine;
      std::string strManaCost;
      std::string strSet;
      std::string strSetName;
      std::string strCollector;
      std::string strLanguage;
      std::string strRarity;
      std::string strVariant;
      int iFace{};
      int iHash{65};
      int iOrb{};
      int iRegionDistance{64};
      bool boOcrCorroborated{};
   };

   /**
   \brief Oracle identity and the ordered candidate printing set.
   \details Selection is provisional until Confirm(). A manual confirmation
   records a user's selection, not additional optical evidence.
   */
   class CardIdentity final {
      std::string m_strOracleId;
      std::string m_strOracleName;
      std::vector<PrintingCandidate> m_vecCandidates;
      std::size_t m_uSelected{};
      bool m_boConfirmed{};
      bool m_boManual{};
      bool m_boTruncated{};
   public:
      /**
      \brief Clear identity separately from physical tracking.
      \details Does not mutate a frozen capture.
      \returns Nothing.
      \throw None.
      */
      void Clear() {
         m_strOracleId.clear();m_strOracleName.clear();
         m_vecCandidates.clear();m_uSelected=0;
         m_boConfirmed=false;m_boManual=false;m_boTruncated=false;
      }

      /**
      \brief Replace all candidates after one completed recognition.
      \details Resets any prior manual selection.
      \param vecCandidates Ordered candidate printings.
      \param boTruncated Whether the query could not enumerate all candidates.
      \returns Nothing.
      \throw None.
      */
      void SetCandidates(std::vector<PrintingCandidate> vecCandidates,
         bool boTruncated=false) {
         m_vecCandidates=std::move(vecCandidates);
         m_uSelected=0;m_boConfirmed=false;m_boManual=false;
         m_boTruncated=boTruncated;
         if(!m_vecCandidates.empty()) {
            m_strOracleId=m_vecCandidates[0].strOracleId;
            m_strOracleName=m_vecCandidates[0].strName;
         }
      }

      /**
      \brief Browse candidate printings without rescanning the physical card.
      \details Navigation wraps at either end.
      \param iDelta Negative for left, positive for right.
      \returns True if selection changed.
      \throw None.
      */
      [[nodiscard]] bool Navigate(int iDelta) noexcept {
         if(m_boConfirmed || m_vecCandidates.size()<2)return false;
         std::size_t const uCount=m_vecCandidates.size();
         m_uSelected=iDelta<0?(m_uSelected+uCount-1)%uCount:
            (m_uSelected+1)%uCount;
         return true;
      }

      /**
      \brief Confirm a human-selected printing in the current session.
      \details This does not change the certainty of the image classifier.
      \returns True if a candidate could be confirmed.
      \throw None.
      */
      [[nodiscard]] bool Confirm() noexcept {
         if(m_vecCandidates.empty() || m_boConfirmed)return false;
         m_boManual=true;m_boConfirmed=true;
         return true;
      }

      /**
      \brief Select a printing from independent OCR evidence.
      \details This does not confirm it or erase remaining candidates.
      \param uIndex Candidate index.
      \returns True if index is valid and the selection is not confirmed.
      \throw None.
      */
      [[nodiscard]] bool SelectAt(std::size_t uIndex) noexcept {
         if(m_boConfirmed || uIndex>=m_vecCandidates.size())return false;
         m_uSelected=uIndex;
         return true;
      }
      [[nodiscard]] std::vector<PrintingCandidate> const& Candidates()
         const noexcept {return m_vecCandidates;}

      [[nodiscard]] PrintingCandidate const* Selected() const noexcept {
         return m_uSelected<m_vecCandidates.size()?
            &m_vecCandidates[m_uSelected]:nullptr;
      }
      [[nodiscard]] std::size_t Count() const noexcept{
         return m_vecCandidates.size();
      }
      [[nodiscard]] std::size_t Index() const noexcept{return m_uSelected;}
      [[nodiscard]] bool Confirmed() const noexcept{return m_boConfirmed;}
      [[nodiscard]] bool Manual() const noexcept{return m_boManual;}
      [[nodiscard]] bool Truncated() const noexcept{return m_boTruncated;}
      [[nodiscard]] std::string const& OracleId() const noexcept{
         return m_strOracleId;
      }
      [[nodiscard]] std::string const& OracleName() const noexcept{
         return m_strOracleName;
      }
   };
}
