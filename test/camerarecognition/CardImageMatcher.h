// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
/**
\file CardImageMatcher.h
\brief Match rectified camera cards against the existing Scryfall image feature index.
\details Artwork is searched at 0 and then 180 degrees. A reliable artwork
candidate restricts a subsequent full-card search to related printings.
If neither orientation yields artwork, a full-card search is attempted at
both orientations. Results are suggestions until physically validated.
*/
#pragma once

#include "CameraCardSession.h"
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <libpq-fe.h>
#include <algorithm>
#include <array>
#include <atomic>
#include "CameraTrace.h"
#include <chrono>
#include <filesystem>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <print>
#include <stdexcept>
#include <system_error>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace deckkernel::camera {
   /** \brief Asynchronous, explicitly unverified recognition outcome.
       \details The event ID is used to discard replies for cards already removed.
       \note A visual match cannot establish foil or etched finishes.
   */
   struct CardImageMatchResult final {
      unsigned uEvent{};
      std::string strStatus;
      std::string strEdition{"Edition not verified"};
      bool boComplete{true};
      // A visual candidate cannot authenticate a particular physical printing.
      std::string strAssetId;
      std::string strName;
      std::string strTypeLine;
      std::string strManaCost;
      std::string strSet;
      std::string strSetName;
      std::string strCollector;
      std::string strLanguage;
      std::string strRarity;
      std::string strPrintingId;
      std::string strOracleId;
      std::string strReferenceLabel;
      std::string strReferenceDiagnostic;
      cv::Mat aReferenceImage;
      int iOrientation{};
      std::vector<PrintingCandidate> vecCandidates;
      bool boCandidateSelection{};
      bool boCandidatesTruncated{};
      // Explicit evidence survives when an intermediate status update is
      // overwritten by a newer worker result before the next UI poll.
      bool boArtworkRecognized{};
      bool boOcrNameRecognized{};
      unsigned uSelectionSerial{};
   };

   /** \brief Image-index consumer running outside the capture thread.
       \details Reuses the existing version-1 dHash64 and ORB bytea features.
       \note The matching thresholds are provisional, not accuracy-calibrated.
   */
   class CardImageMatcher final {
      using hash_ty = std::uint64_t;

      struct Task final {
         unsigned uEvent{};
         cv::Mat aCard;
         std::optional<PrintingCandidate> optSelection;
         unsigned uSelectionSerial{};
         std::filesystem::path aEvidenceDirectory;
         std::optional<std::string> optOcrName;
      };

      struct Ranked final {
         std::string strAssetId;
         std::string strIllustrationId;
         std::string strPrintingId;
         std::string strName;
         std::string strSet;
         std::string strCollector;
         std::string strLanguage;
         std::string strTypeLine;
         std::string strManaCost;
         std::string strSetName;
         std::string strRarity;
         std::string strOracleId;
         std::string strVariant;
         int iFace{};
         int iHash{65};
         int iOrb{};
         int iRegionDistance{128};
         int iArtworkMatches{-1};
      };

      struct PgRelease final {
         void operator()(PGconn* pConn) const noexcept {
            if(pConn) PQfinish(pConn);
         }
      };
      struct RowsRelease final {
         void operator()(PGresult* pRows) const noexcept {
            if(pRows) PQclear(pRows);
         }
      };
      using connection_ty = std::unique_ptr<PGconn, PgRelease>;
      using result_ty = std::unique_ptr<PGresult, RowsRelease>;

      // Provisional acceptance criteria. Ground-truth recordings must calibrate them.
      static constexpr int ciArtMinOrb = 12;
      static constexpr int ciArtMaxHash = 30;
      static constexpr int ciFullMinOrb = 14;
      static constexpr int ciFullMaxHash = 24;
      static constexpr int ciStrongOrb = 25;

      std::mutex m_mutex;
      std::condition_variable m_signal;
      std::optional<Task> m_pending;
      std::optional<CardImageMatchResult> m_result;
      bool m_stopping{};
      std::atomic<unsigned> m_uActiveEvent{0};
      unsigned m_uCandidates{};
      std::thread m_worker;

      /** \brief Read an optional PostgreSQL connection parameter.
          \details The supplied fallback is used only for an absent or empty variable.
          \param szName Name of the environment variable.
          \param szFallback Default string.
          \returns Effective connection parameter.
          \throw None.
      */
      static std::string Env(char const* szName, char const* szFallback) {
         if(char const* szValue = std::getenv(szName); szValue && *szValue) {
            return szValue;
         }
         return szFallback;
      }

      /** \brief Connect to the existing DeckKernel test database.
          \details The connection is owned exclusively by the image worker.
          \returns RAII PostgreSQL connection.
          \throw std::runtime_error If PostgreSQL cannot be reached.
      */
      static connection_ty Connect() {
         std::array<std::string, 8> arrFields{
            Env("DECKKERNEL_PGHOST", "localhost"),
            Env("DECKKERNEL_PGPORT", "5432"),
            Env("DECKKERNEL_PGDATABASE", "DeckKernel"),
            Env("DECKKERNEL_PGUSER", "deckkernel_user"),
            Env("DECKKERNEL_PGPASSWORD", ""),
            Env("DECKKERNEL_PGSSLMODE", "prefer"),
            Env("DECKKERNEL_PG_GSSENCMODE", "disable"),
            Env("DECKKERNEL_PG_CONNECT_TIMEOUT", "5")
         };
         char const* arrKeys[]{
            "host", "port", "dbname", "user", "password", "sslmode",
            "gssencmode", "connect_timeout", nullptr
         };
         char const* arrValues[]{
            arrFields[0].c_str(), arrFields[1].c_str(), arrFields[2].c_str(),
            arrFields[3].c_str(), arrFields[4].c_str(), arrFields[5].c_str(),
            arrFields[6].c_str(), arrFields[7].c_str(), nullptr
         };
         auto const aStart=std::chrono::steady_clock::now();
         connection_ty aConnection(PQconnectdbParams(arrKeys, arrValues, 0));
         double const flElapsed=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-aStart).count();
         deckkernel::camera::Trace("[CAM-DB] connect elapsed={:.2f}s status={}",
            flElapsed,aConnection && PQstatus(aConnection.get())==CONNECTION_OK?
               "ready":"failed");
         if(!aConnection || PQstatus(aConnection.get()) != CONNECTION_OK) {
            throw std::runtime_error("camera image matcher: PostgreSQL connection failed");
         }
         // A very large dHash shortlist can otherwise keep the UI at
         // ARTWORK indefinitely. PostgreSQL cancels the individual query
         // and the worker publishes the actual database error.
         result_ty aTimeout(PQexec(aConnection.get(),
            "SET statement_timeout = '12000ms'"));
         if(!aTimeout || PQresultStatus(aTimeout.get())!=PGRES_COMMAND_OK)
            throw std::runtime_error(std::format(
               "cannot set camera query timeout: {}",
               PQerrorMessage(aConnection.get())));
         return aConnection;
      }

      /** \brief Compute the same dHash64 as scantest.
          \details Preserve bit order for PostgreSQL bit(64) XOR distance.
          \param aImage Decoded camera crop.
          \returns Hash in PostgreSQL binary literal order.
          \throw cv::Exception When OpenCV cannot transform the input.
      */
      static std::string DHashBits(cv::Mat const& aImage) {
         cv::Mat aGray, aSmall;
         if(aImage.channels() == 4) {
            cv::cvtColor(aImage, aGray, cv::COLOR_BGRA2GRAY);
         }
         else if(aImage.channels() == 3) {
            cv::cvtColor(aImage, aGray, cv::COLOR_BGR2GRAY);
         }
         else {
            aGray = aImage;
         }
         cv::resize(aGray, aSmall, cv::Size(9, 8), 0, 0, cv::INTER_AREA);
         std::string strBits;
         strBits.reserve(64);
         for(int iY = 0; iY < 8; ++iY) {
            for(int iX = 0; iX < 8; ++iX) {
               strBits.push_back(aSmall.at<unsigned char>(iY, iX) >
                  aSmall.at<unsigned char>(iY, iX + 1) ? '1' : '0');
            }
         }
         return strBits;
      }

      /**
      \brief Artwork window and measured horizontal frame boundaries.
      \details A view is retained within its owning frozen-card cv::Mat.
      */
      struct ArtworkRegion {
         cv::Mat aImage;
         cv::Rect aRect;
         bool boDetected{};
         double flTopCoverage{};
         double flBottomCoverage{};
      };

      /**
      \brief Isolate the illustration from the complete frozen card.
      \details Detect the two long horizontal separator lines bounding the
      art: title/artwork and artwork/type. Standard-frame variants change
      their exact vertical location, so the earlier fixed crops starting
      at 19/22 percent missed the TOP of real illustrations. A second
      conservative fallback window is always evaluated and logged.
      \param aCard Perspective-rectified full card, already portrait.
      \param iVariant Zero for measured bounds, one for fallback window.
      \returns Artwork ROI plus source pixel rectangle and line evidence.
      \throw cv::Exception If image processing fails.
      \note This is image geometry only. OCR remains disabled at this stage.
      */
      [[nodiscard]] static ArtworkRegion Artwork(cv::Mat const& aCard,
         int iVariant=0) {
         ArtworkRegion aRegion;
         if(aCard.empty())return aRegion;
         cv::Rect aWindow(
            aCard.cols*(iVariant==0?7:9)/100,
            aCard.rows*(iVariant==0?10:14)/100,
            aCard.cols*(iVariant==0?86:82)/100,
            aCard.rows*(iVariant==0?46:40)/100);
         if(iVariant==0) {
            cv::Mat aSmall,aGray,aVertical;
            cv::resize(aCard,aSmall,cv::Size(240,336),
               0.0,0.0,cv::INTER_AREA);
            if(aSmall.channels()==4)
               cv::cvtColor(aSmall,aGray,cv::COLOR_BGRA2GRAY);
            else if(aSmall.channels()==3)
               cv::cvtColor(aSmall,aGray,cv::COLOR_BGR2GRAY);
            else
               aGray=aSmall;
            cv::Sobel(aGray,aVertical,CV_16S,0,1,3);
            struct Separator {
               double flCoverage{};
               int iRow{};
            };
            auto const FindSeparator=[&](int iFirst,int iLast) {
               Separator aBest;
               int const iX1=aSmall.cols*15/100;
               int const iX2=aSmall.cols*85/100;
               for(int iY=aSmall.rows*iFirst/100;
                  iY<aSmall.rows*iLast/100;++iY) {
                  short const* pRow=aVertical.ptr<short>(iY);
                  unsigned uCount{};
                  for(int iX=iX1;iX<iX2;++iX)
                     uCount+=std::abs(static_cast<int>(pRow[iX]))>=85;
                  double const flCoverage=
                     static_cast<double>(uCount)/(iX2-iX1);
                  if(flCoverage>aBest.flCoverage)
                     aBest={flCoverage,iY};
               }
               return aBest;
            };
            Separator const aTop=FindSeparator(7,25);
            Separator const aBottom=FindSeparator(43,68);
            aRegion.flTopCoverage=aTop.flCoverage;
            aRegion.flBottomCoverage=aBottom.flCoverage;
            int const iGap=aBottom.iRow-aTop.iRow;
            if(aTop.flCoverage>=0.28 && aBottom.flCoverage>=0.28 &&
               iGap>=aSmall.rows*27/100 &&
               iGap<=aSmall.rows*55/100) {
               // Exclude two rows of artwork-border pixels. Derive both
               // positions from the same actual frozen camera image.
               int const iTop=static_cast<int>(
                  (aTop.iRow+2)*aCard.rows/static_cast<double>(aSmall.rows));
               int const iBottom=static_cast<int>(
                  (aBottom.iRow-2)*aCard.rows/static_cast<double>(aSmall.rows));
               aWindow.y=iTop;
               aWindow.height=iBottom-iTop;
               aRegion.boDetected=true;
            }
         }
         aWindow &=cv::Rect(0,0,aCard.cols,aCard.rows);
         if(aWindow.width<30||aWindow.height<30)return aRegion;
         aRegion.aRect=aWindow;
         aRegion.aImage=aCard(aWindow);
         return aRegion;
      }

      /**
      \brief Compute query descriptors with a low-contrast fallback.
      \details The database's version-1 ORB descriptors are intentionally
      unchanged. A 500-pixel camera card can have fewer than ten usable
      raw keypoints, making the existing minimum of twelve good matches
      impossible. Only a weak query receives local CLAHE preprocessing.
      No claim is made that extra CLAHE keypoints imply correct matches.
      \param aImage Artwork or complete-card image from frozen capture.
      \returns Original ORB descriptors or contrast-recovered descriptors.
      \throw cv::Exception If OpenCV preprocessing or ORB extraction fails.
      \note The fallback threshold requires calibration with DB references.
      */
      static cv::Mat Descriptors(cv::Mat const& aImage) {
         cv::Ptr<cv::ORB> const aOrb=cv::ORB::create(500);
         std::vector<cv::KeyPoint> vecRaw;
         cv::Mat aRaw;
         aOrb->detectAndCompute(aImage,cv::noArray(),vecRaw,aRaw);
         if(aRaw.rows>=40) {
            deckkernel::camera::Trace("[CAM-ORB-QUERY] raw={} enhanced=skipped source={}x{}",
               aRaw.rows,aImage.cols,aImage.rows);
            return aRaw;
         }
         cv::Mat aGray;
         if(aImage.channels()==4)
            cv::cvtColor(aImage,aGray,cv::COLOR_BGRA2GRAY);
         else if(aImage.channels()==3)
            cv::cvtColor(aImage,aGray,cv::COLOR_BGR2GRAY);
         else
            aGray=aImage;
         cv::Mat aEnhanced;
         cv::Ptr<cv::CLAHE> const aClahe=cv::createCLAHE(
            2.0,cv::Size(8,8));
         aClahe->apply(aGray,aEnhanced);
         cv::Mat aImproved;
         std::vector<cv::KeyPoint> vecImproved;
         aOrb->detectAndCompute(aEnhanced,cv::noArray(),
            vecImproved,aImproved);
         deckkernel::camera::Trace("[CAM-ORB-QUERY] raw={} enhanced={} source={}x{} "
            "mode={}",aRaw.rows,aImproved.rows,
            aImage.cols,aImage.rows,
            aImproved.rows>aRaw.rows?"clahe":"raw");
         return aImproved.rows>aRaw.rows?std::move(aImproved):std::move(aRaw);
      }

      /** \brief Count ratio-filtered ORB matches against persisted descriptors.
          \details Each ORB row consists of 32 bytes in scantest version 1.
          \param aQuery Query descriptors.
          \param pRows Result rows containing the BYTEA column.
          \param iRow Row to score.
          \param iColumn Index of the persisted descriptor column.
          \returns Number of accepted ORB matches.
          \throw cv::Exception If the OpenCV matcher fails.
      */
      static int OrbMatches(cv::Mat const& aQuery, PGresult* pRows,
         int iRow, int iColumn) {
         if(aQuery.empty() || PQgetisnull(pRows, iRow, iColumn)) {
            return 0;
         }
         std::size_t uSize{};
         std::unique_ptr<unsigned char, decltype(&PQfreemem)> upBytes(
            PQunescapeBytea(reinterpret_cast<unsigned char const*>(
               PQgetvalue(pRows, iRow, iColumn)), &uSize), &PQfreemem);
         if(!upBytes || uSize < 64 || uSize % 32 != 0 ||
            uSize / 32 > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            return 0;
         }
         cv::Mat const aReference(static_cast<int>(uSize / 32),
            32, CV_8UC1, upBytes.get());
         cv::BFMatcher aMatcher(cv::NORM_HAMMING, false);
         std::vector<std::vector<cv::DMatch>> vecPairs;
         aMatcher.knnMatch(aQuery, aReference, vecPairs, 2);
         // Each reference descriptor may support at most one camera point.
         // Without this guard 400 CLAHE-enhanced camera points can all match
         // the SAME DB descriptor, manufacturing apparent evidence.
         std::vector<cv::DMatch> vecAccepted;
         for(std::vector<cv::DMatch> const& vecPair:vecPairs) {
            if(vecPair.size()==2 &&
               vecPair[0].distance<0.75f*vecPair[1].distance)
               vecAccepted.push_back(vecPair[0]);
         }
         std::sort(vecAccepted.begin(),vecAccepted.end(),
            [](cv::DMatch const& aLeft,cv::DMatch const& aRight) {
               return aLeft.distance<aRight.distance;
            });
         std::vector<bool> vecUsed(
            static_cast<std::size_t>(aReference.rows),false);
         int iAccepted{};
         for(cv::DMatch const& aMatch:vecAccepted) {
            std::size_t const uIndex=
               static_cast<std::size_t>(aMatch.trainIdx);
            if(uIndex>=vecUsed.size() || vecUsed[uIndex])continue;
            vecUsed[uIndex]=true;
            ++iAccepted;
         }
         return iAccepted;
      }

      /** \brief Rank candidate matches by ORB and then dHash distance.
          \details A smaller perceptual hash is only a tiebreaker here.
          \param aLeft First candidate.
          \param aRight Second candidate.
          \returns True if first candidate outranks the second.
          \throw None.
      */
      static bool Better(Ranked const& aLeft, Ranked const& aRight) noexcept {
         return aLeft.iOrb != aRight.iOrb ?
            aLeft.iOrb > aRight.iOrb : aLeft.iHash < aRight.iHash;
      }

      /** \brief Apply provisional artwork acceptance criteria.
          \details Reject nearest-neighbour matches without discriminative evidence.
          \param aMatch Best artwork candidate.
          \returns True when artwork comparison has adequate evidence.
          \throw None.
      */
      static bool AcceptArtwork(Ranked const& aMatch) noexcept {
         return aMatch.iOrb >= ciArtMinOrb &&
            (aMatch.iHash <= ciArtMaxHash || aMatch.iOrb >= ciStrongOrb);
      }

      /** \brief Apply provisional full-card acceptance criteria.
          \details A match is not yet a verified physical printing.
          \param aMatch Best full-card candidate.
          \returns True when the full-card score passes the tentative floor.
          \throw None.
      */
      static bool AcceptFull(Ranked const& aMatch) noexcept {
         return aMatch.iOrb >= ciFullMinOrb &&
            (aMatch.iHash <= ciFullMaxHash || aMatch.iOrb >= ciStrongOrb);
      }

      /** \brief Search only indexed artwork variants.
          \details EXISTS prevents multiple printings sharing one image from
          consuming multiple shortlist positions.
          \param pConnection Dedicated worker database connection.
          \param aArt Cropped camera illustration.
          \returns Best candidate if the feature catalog contains one.
          \throw std::runtime_error If the query fails.
      */
      std::optional<Ranked> FindArtwork(PGconn* pConnection,
         cv::Mat const& aArt) const {
         std::string const strHash = DHashBits(aArt);
         // With blur and a slightly inaccurate image window dHash can rank
         // the true artwork outside the old first-100 shortlist. ORB is the
         // actual second-stage discriminator on a wider candidate set.
         std::string const strLimit = std::to_string(
            std::max(400u,m_uCandidates));
         char const* arrParams[]{strHash.c_str(), strLimit.c_str()};
         auto const aQueryStart=std::chrono::steady_clock::now();
         result_ty aRows(PQexecParams(pConnection,
            "SELECT a.id::text, COALESCE(a.illustration_id, ''), "
            "bit_count(f.perceptual_hash # $1::bit(64))::text, f.orb_descriptors "
            "FROM deckkernel_test.scryfall_image_features f "
            "JOIN deckkernel_test.scryfall_image_assets a ON a.id=f.asset_id "
            "WHERE f.algorithm='dhash64-orb' AND f.algorithm_version=1 "
            "AND f.perceptual_hash IS NOT NULL AND a.image_data IS NOT NULL "
            "AND EXISTS (SELECT 1 FROM deckkernel_test.scryfall_card_image_variants v "
            "            WHERE v.asset_id=a.id AND v.variant='art_crop') "
            "ORDER BY bit_count(f.perceptual_hash # $1::bit(64)), a.id "
            "LIMIT $2::integer",
            2, nullptr, arrParams, nullptr, nullptr, 0));
         if(!aRows || PQresultStatus(aRows.get()) != PGRES_TUPLES_OK) {
            throw std::runtime_error(std::format(
               "artwork feature lookup failed: {}", PQerrorMessage(pConnection)));
         }
         double const flDbSeconds=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-aQueryStart).count();
         cv::Mat const aQuery = Descriptors(aArt);
         std::optional<Ranked> optBest;
         for(int iRow = 0; iRow < PQntuples(aRows.get()); ++iRow) {
            Ranked aMatch;
            aMatch.strAssetId = PQgetvalue(aRows.get(), iRow, 0);
            aMatch.strIllustrationId = PQgetvalue(aRows.get(), iRow, 1);
            aMatch.iHash = std::stoi(PQgetvalue(aRows.get(), iRow, 2));
            aMatch.iOrb = OrbMatches(aQuery, aRows.get(), iRow, 3);
            if(!optBest || Better(aMatch, *optBest)) {
               optBest = std::move(aMatch);
            }
         }
         double const flTotalSeconds=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-aQueryStart).count();
         deckkernel::camera::Trace("[CAM-ART-QUERY] shortlisted={} sql={:.2f}s "
            "orb={:.2f}s best-orb={} best-hash={}",
            PQntuples(aRows.get()),flDbSeconds,
            flTotalSeconds-flDbSeconds,
            optBest?optBest->iOrb:0,optBest?optBest->iHash:65);
         return optBest;
      }

      /** \brief Resolve a shared art image to a display name.
          \details This does not establish the printing.
          \param pConnection Worker database connection.
          \param strAssetId Scryfall image asset identifier.
          \returns One face name linked to the artwork.
          \throw std::runtime_error If PostgreSQL cannot execute the query.
      */
      static std::string ArtworkName(PGconn* pConnection,
         std::string const& strAssetId) {
         char const* arrParams[]{strAssetId.c_str()};
         result_ty aRows(PQexecParams(pConnection,
            "SELECT cf.name "
            "FROM deckkernel_test.scryfall_card_image_variants v "
            "JOIN deckkernel_test.scryfall_card_faces cf "
            "ON cf.card_id=v.card_id AND cf.face_id=v.face_id "
            "WHERE v.asset_id=$1::bigint AND v.variant='art_crop' "
            "ORDER BY v.card_id,v.face_id LIMIT 1",
            1, nullptr, arrParams, nullptr, nullptr, 0));
         if(!aRows || PQresultStatus(aRows.get()) != PGRES_TUPLES_OK) {
            throw std::runtime_error("artwork face-name lookup failed");
         }
         return PQntuples(aRows.get()) ? PQgetvalue(aRows.get(), 0, 0) : "";
      }

      /** \brief Search full-card variants for the concrete Scryfall printing.
          \details If an artwork candidate exists, only related art assets
          and illustration IDs are eligible. Otherwise the full catalog is used.
          \param pConnection Worker database connection.
          \param aCard Rectified camera card in the selected orientation.
          \param optArtwork Optional art-based restriction.
          \returns Full-card candidates ranked by ORB and dHash, deduplicated
          by printing and face.
          \throw std::runtime_error On an invalid database result.
      */
      std::vector<Ranked> FindPrintings(PGconn* pConnection,
         cv::Mat const& aCard, std::optional<Ranked> const& optArtwork) const {
         std::string const strHash=DHashBits(aCard);
         std::string const strLimit=std::to_string(
            optArtwork?std::max(1000u,m_uCandidates):m_uCandidates);
         // PHASE 07: if the artwork is identified, begin with its printing
         // faces, NEVER with a global dHash sort over 2.7M indexed assets.
         // The existing FirstConnection model already indexes
         // scryfall_image_assets(illustration_id) and the variant asset ID.
         // A MATERIALIZED CTE forces the small relational candidate set to
         // be established before perceptual similarity is evaluated.
         std::string strSql;
         if(optArtwork) {
            strSql=
               "WITH art_faces AS MATERIALIZED ("
               " SELECT va.card_id,va.face_id "
               " FROM deckkernel_test.scryfall_card_image_variants va "
               " WHERE va.asset_id=$2::bigint AND va.variant='art_crop' "
               " UNION "
               " SELECT va.card_id,va.face_id "
               " FROM deckkernel_test.scryfall_image_assets aa "
               " JOIN deckkernel_test.scryfall_card_image_variants va "
               " ON va.asset_id=aa.id AND va.variant='art_crop' "
               " WHERE $3::text<>'' AND aa.illustration_id=$3::text "
               ") ";
         }
         strSql+=
            "SELECT a.id::text, c.scryfall_id, cf.face_id::text, cf.name, "
            "s.code, c.collector_number, c.lang, v.variant, "
            "bit_count(f.perceptual_hash # $1::bit(64))::text, "
            "f.orb_descriptors, cf.type_line, cf.mana_cost, s.name, "
            "r.code, COALESCE(c.oracle_id,'') "
            "FROM ";
         if(optArtwork) {
            strSql+=
               "art_faces af "
               "JOIN deckkernel_test.scryfall_card_image_variants v "
               "ON v.card_id=af.card_id AND v.face_id=af.face_id "
               "AND v.variant IN ('normal','border_crop','large') ";
         }
         else {
            strSql+=
               "deckkernel_test.scryfall_card_image_variants v ";
         }
         strSql+=
            "JOIN deckkernel_test.scryfall_image_assets a "
            "ON a.id=v.asset_id AND a.image_data IS NOT NULL "
            "JOIN deckkernel_test.scryfall_image_features f "
            "ON f.asset_id=a.id AND f.algorithm='dhash64-orb' "
            "AND f.algorithm_version=1 AND f.perceptual_hash IS NOT NULL "
            "JOIN deckkernel_test.scryfall_cards c ON c.id=v.card_id "
            "JOIN deckkernel_test.scryfall_sets s ON s.id=c.set_id "
            "JOIN deckkernel_test.dom_rarities r ON r.id=c.rarity_id "
            "JOIN deckkernel_test.scryfall_card_faces cf "
            "ON cf.card_id=v.card_id AND cf.face_id=v.face_id ";
         if(!optArtwork)
            strSql+="WHERE v.variant IN ('normal','border_crop','large') ";
         strSql+=
            optArtwork?
               "ORDER BY bit_count(f.perceptual_hash # $1::bit(64)), "
               "a.id LIMIT $4::integer":
               "ORDER BY bit_count(f.perceptual_hash # $1::bit(64)), "
               "a.id LIMIT $2::integer";
         char const* arrParams[]{
            strHash.c_str(),
            optArtwork?optArtwork->strAssetId.c_str():strLimit.c_str(),
            optArtwork?optArtwork->strIllustrationId.c_str():nullptr,
            optArtwork?strLimit.c_str():nullptr
         };
         auto const aQueryStart=std::chrono::steady_clock::now();
         result_ty aRows(PQexecParams(pConnection,
            strSql.c_str(),optArtwork?4:2,nullptr,
            arrParams,nullptr,nullptr,0));
         if(!aRows || PQresultStatus(aRows.get()) != PGRES_TUPLES_OK) {
            throw std::runtime_error(std::format(
               "full-card feature lookup failed: {}", PQerrorMessage(pConnection)));
         }
         double const flDbSeconds=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-aQueryStart).count();
         cv::Mat const aQuery = Descriptors(aCard);
         std::vector<Ranked> vecMatches;
         for(int iRow = 0; iRow < PQntuples(aRows.get()); ++iRow) {
            Ranked aMatch;
            aMatch.strAssetId = PQgetvalue(aRows.get(), iRow, 0);
            aMatch.strPrintingId = PQgetvalue(aRows.get(), iRow, 1);
            aMatch.iFace = std::stoi(PQgetvalue(aRows.get(), iRow, 2));
            aMatch.strName = PQgetvalue(aRows.get(), iRow, 3);
            aMatch.strSet = PQgetvalue(aRows.get(), iRow, 4);
            aMatch.strCollector = PQgetvalue(aRows.get(), iRow, 5);
            aMatch.strLanguage = PQgetvalue(aRows.get(), iRow, 6);
            aMatch.strVariant = PQgetvalue(aRows.get(), iRow, 7);
            aMatch.iHash = std::stoi(PQgetvalue(aRows.get(), iRow, 8));
            aMatch.iOrb = OrbMatches(aQuery, aRows.get(), iRow, 9);
            aMatch.strTypeLine = PQgetvalue(aRows.get(), iRow, 10);
            aMatch.strManaCost = PQgetvalue(aRows.get(), iRow, 11);
            aMatch.strSetName = PQgetvalue(aRows.get(), iRow, 12);
            aMatch.strRarity = PQgetvalue(aRows.get(), iRow, 13);
            aMatch.strOracleId = PQgetvalue(aRows.get(), iRow, 14);
            auto const it = std::find_if(vecMatches.begin(), vecMatches.end(),
               [&](Ranked const& aExisting) {
                  return aExisting.strPrintingId == aMatch.strPrintingId &&
                     aExisting.iFace == aMatch.iFace;
               });
            if(it == vecMatches.end()) {
               vecMatches.push_back(std::move(aMatch));
            }
            else if(Better(aMatch, *it)) {
               *it = std::move(aMatch);
            }
         }
         std::sort(vecMatches.begin(), vecMatches.end(), Better);
         double const flTotalSeconds=std::chrono::duration<double>(
            std::chrono::steady_clock::now()-aQueryStart).count();
         deckkernel::camera::Trace("[CAM-PRINT-QUERY] shortlist={} distinct={} "
            "sql={:.2f}s orb={:.2f}s restricted={}",
            PQntuples(aRows.get()),vecMatches.size(),flDbSeconds,
            flTotalSeconds-flDbSeconds,optArtwork.has_value());
         return vecMatches;
      }

      /**
      \brief Convert one ranked image variant into a distinct printing candidate.
      \details The database ID, card face and printing ID preserve printing
      identity independently from the selected JPEG variant.
      \param aMatch Ranked matching image.
      \returns Candidate with original Scryfall metadata.
      \throw std::bad_alloc If strings cannot be copied.
      */
      static PrintingCandidate Candidate(Ranked const& aMatch) {
         PrintingCandidate aCandidate;
         aCandidate.strAssetId=aMatch.strAssetId;
         aCandidate.strPrintingId=aMatch.strPrintingId;
         aCandidate.strOracleId=aMatch.strOracleId;
         aCandidate.strName=aMatch.strName;
         aCandidate.strTypeLine=aMatch.strTypeLine;
         aCandidate.strManaCost=aMatch.strManaCost;
         aCandidate.strSet=aMatch.strSet;
         aCandidate.strSetName=aMatch.strSetName;
         aCandidate.strCollector=aMatch.strCollector;
         aCandidate.strLanguage=aMatch.strLanguage;
         aCandidate.strRarity=aMatch.strRarity;
         aCandidate.strVariant=aMatch.strVariant;
         aCandidate.iFace=aMatch.iFace;
         aCandidate.iHash=aMatch.iHash;
         aCandidate.iOrb=aMatch.iOrb;
         aCandidate.iRegionDistance=aMatch.iRegionDistance;
         return aCandidate;
      }

      /**
      \brief Populate the selected DB candidate into a presentation event.
      \details Does not trigger a fresh image-recognition run.
      \param aResult Result object to update.
      \param aCandidate Selected printing.
      \returns Nothing.
      \throw std::bad_alloc If copying metadata fails.
      */
      static void SetPrinting(CardImageMatchResult& aResult,
         PrintingCandidate const& aCandidate) {
         aResult.strAssetId=aCandidate.strAssetId;
         aResult.strName=aCandidate.strName;
         aResult.strTypeLine=aCandidate.strTypeLine;
         aResult.strManaCost=aCandidate.strManaCost;
         aResult.strSet=aCandidate.strSet;
         aResult.strSetName=aCandidate.strSetName;
         aResult.strCollector=aCandidate.strCollector;
         aResult.strLanguage=aCandidate.strLanguage;
         aResult.strRarity=aCandidate.strRarity;
         aResult.strPrintingId=aCandidate.strPrintingId;
         aResult.strOracleId=aCandidate.strOracleId;
         aResult.strReferenceLabel=std::format(
            "DB {} | {} #{} (UNVERIFIED)",
            aCandidate.strVariant,aCandidate.strSet,
            aCandidate.strCollector);
         aResult.strEdition=std::format("{} #{} [{}] | ORB={} dHash={}/64",
            aCandidate.strSet,aCandidate.strCollector,
            aCandidate.strLanguage,aCandidate.iOrb,aCandidate.iHash);
      }

      /** \brief Compare full-card candidates without claiming a foil finish.
          \details Similar top-ranked physical printings are explicitly ambiguous.
          \param vecMatches Ranked candidates, including possible reprints.
          \param boArtwork True when an artwork candidate restricted the search.
          \param uEvent Camera presentation number.
          \param iOrientation Rotation applied to the worker's original card.
          \returns Result with conservative, unverified identity.
          \throw None.
      */
      /**
      \brief Independently corroborate the illustration of a full-card match.
      \details Text and similar MTG frame borders can create false global
      ORB matches. Here only isolated artwork pixels are compared.
      \param aCamera Frozen camera card.
      \param aReference Decoded DB printing image.
      \returns Unique ratio-tested ORB artwork correspondences.
      \throw cv::Exception If ORB matching fails.
      \note Failure is inconclusive, not evidence for another identity.
      */
      [[nodiscard]] static int FullCardArtworkMatches(
         cv::Mat const& aCamera,cv::Mat const& aReference) {
         if(aCamera.empty() || aReference.empty())return 0;
         ArtworkRegion const aCameraArt=Artwork(aCamera,0);
         ArtworkRegion const aReferenceArt=Artwork(aReference,0);
         if(aCameraArt.aImage.empty() || aReferenceArt.aImage.empty())
            return 0;
         cv::Mat const aQuery=Descriptors(aCameraArt.aImage);
         cv::Mat const aRef=Descriptors(aReferenceArt.aImage);
         if(aQuery.rows<2 || aRef.rows<2)return 0;
         cv::BFMatcher aMatcher(cv::NORM_HAMMING,false);
         std::vector<std::vector<cv::DMatch>> vecPairs;
         aMatcher.knnMatch(aQuery,aRef,vecPairs,2);
         std::vector<cv::DMatch> vecAccepted;
         for(auto const& vecPair:vecPairs) {
            if(vecPair.size()==2 &&
               vecPair[0].distance<0.75f*vecPair[1].distance)
               vecAccepted.push_back(vecPair[0]);
         }
         std::sort(vecAccepted.begin(),vecAccepted.end(),
            [](cv::DMatch const& aLeft,cv::DMatch const& aRight) {
               return aLeft.distance<aRight.distance;
            });
         std::vector<bool> vecUsed(static_cast<std::size_t>(aRef.rows),false);
         int iMatches{};
         for(cv::DMatch const& aMatch:vecAccepted) {
            std::size_t const uIndex=
               static_cast<std::size_t>(aMatch.trainIdx);
            if(uIndex>=vecUsed.size()||vecUsed[uIndex])continue;
            vecUsed[uIndex]=true;
            ++iMatches;
         }
         return iMatches;
      }

      /**
      \brief Compare the frozen card's printed margin and set-symbol region.
      \details Two local 64-bit hashes supply evidence not found in pure
      artwork similarity. They cannot prove a unique physical printing.
      \param aCamera Frozen full-resolution input card.
      \param aReference Original normal/large Scryfall card image.
      \returns Hamming distance in the two regions, 0..128.
      \throw cv::Exception If OpenCV cannot process the regions.
      */
      [[nodiscard]] static int PrintingRegionDistance(
         cv::Mat const& aCamera,cv::Mat const& aReference) {
         if(aCamera.empty()||aReference.empty())return 128;
         auto Crop=[](cv::Mat const& aImage,int iX,int iY,
            int iW,int iH)->cv::Mat {
            int const iLeft=aImage.cols*iX/100;
            int const iTop=aImage.rows*iY/100;
            return aImage(cv::Rect(iLeft,iTop,
               std::max(1,aImage.cols*iW/100),
               std::max(1,aImage.rows*iH/100)));
         };
         int iDistance{};
         for(auto const& arrRegion:std::array<std::array<int,4>,2>{{
            {{4,86,92,12}},{{82,50,15,11}}
         }}) {
            std::string const strA=DHashBits(Crop(aCamera,
               arrRegion[0],arrRegion[1],arrRegion[2],arrRegion[3]));
            std::string const strB=DHashBits(Crop(aReference,
               arrRegion[0],arrRegion[1],arrRegion[2],arrRegion[3]));
            for(std::size_t uIndex=0;uIndex<64;++uIndex)
               iDistance+=strA[uIndex]!=strB[uIndex];
         }
         return iDistance;
      }

      /**
      \brief Refine leading full-card candidates with printing-specific detail.
      \details Only twelve references are decoded on the async database thread.
      The weighting is provisional and must be calibrated against test cards.
      \param pConnection Database connection.
      \param aCard Frozen camera card.
      \param vecMatches ORB/dHash-ranked references to refine.
      \returns Nothing.
      \throw std::runtime_error If PostgreSQL fails.
      */
      static void RefinePrintingImages(PGconn* pConnection,
         cv::Mat const& aCard,std::vector<Ranked>& vecMatches) {
         std::size_t const uCount=std::min<std::size_t>(12,vecMatches.size());
         for(std::size_t uIndex=0;uIndex<uCount;++uIndex) {
            Ranked& aMatch=vecMatches[uIndex];
            if(aMatch.strVariant!="normal" && aMatch.strVariant!="large")
               continue;
            CardImageMatchResult aReference;
            aReference.strAssetId=aMatch.strAssetId;
            LoadReference(pConnection,aReference);
            if(aReference.aReferenceImage.empty())continue;
            aMatch.iRegionDistance=PrintingRegionDistance(
               aCard,aReference.aReferenceImage);
            aMatch.iArtworkMatches=FullCardArtworkMatches(
               aCard,aReference.aReferenceImage);
            deckkernel::camera::Trace("[CAM-FULL-ART] asset={} name={} "
               "full-orb={} art-orb={} hash={}",
               aMatch.strAssetId,aMatch.strName,aMatch.iOrb,
               aMatch.iArtworkMatches,aMatch.iHash);
         }
         std::stable_sort(vecMatches.begin(),vecMatches.end(),
            [](Ranked const& aLeft,Ranked const& aRight) {
               // A decoded full-card reference with artwork corroboration
               // ranks above a candidate based only on common text/frame.
               // Unknown artwork scores (-1) remain unverified.
               bool const boLeftVerified=aLeft.iArtworkMatches>=12;
               bool const boRightVerified=aRight.iArtworkMatches>=12;
               if(boLeftVerified!=boRightVerified)
                  return boLeftVerified;
               int const iLeft=4*aLeft.iOrb-aLeft.iHash-
                  (aLeft.iRegionDistance==128?64:aLeft.iRegionDistance);
               int const iRight=4*aRight.iOrb-aRight.iHash-
                  (aRight.iRegionDistance==128?64:aRight.iRegionDistance);
               return iLeft>iRight;
            });
      }

      static CardImageMatchResult SummarizePrintings(
         std::vector<Ranked> const& vecMatches, bool boArtwork,
         unsigned uEvent, int iOrientation) {
         CardImageMatchResult aResult{uEvent};
         if(vecMatches.empty()) {
            aResult.strStatus=boArtwork?
               "ARTWORK FOUND - no indexed full-card variants for this face":
               "UNKNOWN - no reliable full-card candidates";
            return aResult;
         }
         Ranked const& aBest=vecMatches.front();
         bool const boVisualPrintingMatch=AcceptFull(aBest);
         if(!boArtwork &&
            (!boVisualPrintingMatch || aBest.iArtworkMatches<12)) {
            // Without a separately matching illustration, similarities
            // between frames and printed text are not a card identity.
            aResult.strStatus=
               "FULL-CARD UNRESOLVED - artwork uncorroborated";
            deckkernel::camera::Trace("[CAM-FULL-REJECT] card={} asset={} "
               "full-orb={} art-orb={} required-art-orb=12",
               uEvent,aBest.strAssetId,aBest.iOrb,
               aBest.iArtworkMatches);
            return aResult;
         }
         // An accepted artwork already identifies the illustration and
         // restricts its linked printing faces. Weak FULL-CARD similarity
         // must not erase those database candidates. Expose them for manual
         // confirmation, but never claim the printing was recognized.
         if(boArtwork && !boVisualPrintingMatch) {
            for(Ranked const& aMatch:vecMatches) {
               aResult.vecCandidates.push_back(Candidate(aMatch));
               if(aResult.vecCandidates.size()>=32) {
                  aResult.boCandidatesTruncated=
                     vecMatches.size()>aResult.vecCandidates.size();
                  break;
               }
            }
            SetPrinting(aResult,aResult.vecCandidates.front());
            aResult.strStatus=std::format(
               "ARTWORK FOUND - printing unverified, {} DB candidates",
               aResult.vecCandidates.size());
            aResult.strEdition=
               "Full-card comparison inconclusive; select printing manually";
            aResult.iOrientation=iOrientation;
            deckkernel::camera::Trace("[CAM-PRINT] card={} artwork-linked={} "
               "full-card-verified=false best-orb={} best-dhash={}",
               uEvent,vecMatches.size(),aBest.iOrb,aBest.iHash);
            return aResult;
         }
         // Phase PRINTING: retain distinct alternatives. A large score gap
         // narrows choices, but optical similarity is not physical proof.
         for(Ranked const& aMatch:vecMatches) {
            if(!AcceptFull(aMatch))continue;
            bool const boRelevant=aMatch.iOrb>=aBest.iOrb-
               std::max(8,aBest.iOrb/4) &&
               aMatch.iHash<=aBest.iHash+10;
            if(!boRelevant)continue;
            aResult.vecCandidates.push_back(Candidate(aMatch));
            if(aResult.vecCandidates.size()>=32) {
               aResult.boCandidatesTruncated=true;
               break;
            }
         }
         if(aResult.vecCandidates.empty())
            aResult.vecCandidates.push_back(Candidate(aBest));
         bool const boAmbiguous=aResult.vecCandidates.size()>1 ||
            aResult.boCandidatesTruncated;
         aResult.strStatus = boAmbiguous ?
            "AMBIGUOUS; several printings resemble the card" :
            (boArtwork ? "ARTWORK_MATCH; printing candidate" :
               "FULL_CARD_FALLBACK; printing candidate");
         SetPrinting(aResult,aResult.vecCandidates.front());
         aResult.iOrientation = iOrientation;
         deckkernel::camera::Trace("[CAM-PRINT] card #{} orientation={} variant={} "
            "scryfall_id={} set={} number={} lang={} face={} "
            "dhash={}/64 orb-good={} candidates={} status={}",
            uEvent, iOrientation, aBest.strVariant, aBest.strPrintingId,
            aBest.strSet, aBest.strCollector, aBest.strLanguage,
            aBest.iFace, aBest.iHash, aBest.iOrb, vecMatches.size(),
            boAmbiguous ? "AMBIGUOUS" : "PRINTING_CANDIDATE_UNVERIFIED");
         return aResult;
      }

      /** \brief Execute the ordered artwork and printing stages.
          \details Artwork is tested before expensive full-card fallback.
          \param pConnection Dedicated worker connection.
          \param aTask One stable-card event.
          \returns An explicitly unverified result.
          \throw std::runtime_error On database errors.
      */
      /**
      \brief Inspect source and feature availability when a search fails.
      \details SHA-256 is a download-integrity field, not a recognition signal.
      \param pConnection Worker PostgreSQL connection.
      \returns Explicit diagnosis, or no message if matching features exist.
      \throw std::runtime_error If the diagnostic query fails.
      */
      static std::string DiagnoseIndex(PGconn* pConnection) {
         result_ty aRows(PQexec(pConnection,
            "SELECT count(*)::text, "
            "count(*) FILTER (WHERE a.image_data IS NOT NULL)::text, "
            "count(*) FILTER (WHERE a.image_data IS NOT NULL "
            " AND (a.sha256 IS NULL OR a.sha256=''))::text, "
            "count(*) FILTER (WHERE f.asset_id IS NOT NULL "
            " AND f.algorithm='dhash64-orb' AND f.algorithm_version=1 "
            " AND f.perceptual_hash IS NOT NULL)::text, "
            "count(*) FILTER (WHERE f.asset_id IS NOT NULL "
            " AND (f.orb_descriptors IS NULL OR octet_length(f.orb_descriptors)=0))::text "
            "FROM deckkernel_test.scryfall_image_assets a "
            "LEFT JOIN deckkernel_test.scryfall_image_features f ON f.asset_id=a.id"));
         if(!aRows || PQresultStatus(aRows.get())!=PGRES_TUPLES_OK ||
            PQntuples(aRows.get())!=1) {
            throw std::runtime_error(std::format(
               "image index diagnostic unavailable: {}",PQerrorMessage(pConnection)));
         }
         auto const uTotal=std::stoull(PQgetvalue(aRows.get(),0,0));
         auto const uDownloaded=std::stoull(PQgetvalue(aRows.get(),0,1));
         auto const uMissingSha=std::stoull(PQgetvalue(aRows.get(),0,2));
         auto const uIndexed=std::stoull(PQgetvalue(aRows.get(),0,3));
         auto const uMissingOrb=std::stoull(PQgetvalue(aRows.get(),0,4));
         deckkernel::camera::Trace("[CAM-INDEX] assets={} downloaded={} indexed={} missing-sha256={} empty-orb={}",
            uTotal,uDownloaded,uIndexed,uMissingSha,uMissingOrb);
         if(uTotal==0)return "NO IMAGE ASSETS in database";
         if(uDownloaded==0)return "NO IMAGE DATA downloaded";
         if(uIndexed==0)return "NO FEATURE INDEX: run scantest --all";
         if(uMissingOrb>=uIndexed)return "ORB DESCRIPTORS EMPTY: recheck feature indexing";
         if(uMissingSha!=0)return std::format(
            "NO MATCH; {} downloaded assets missing SHA-256 (integrity metadata)",
            uMissingSha);
         if(uDownloaded>uIndexed)return std::format(
            "NO MATCH; indexed {} of {} downloaded assets",uIndexed,uDownloaded);
         return "NO VISUAL MATCH: index available, candidate evidence too weak";
      }

      /**
      \brief Read one original Scryfall reference image from the existing DB.
      \details Decodes only the winner. SHA-256 is integrity metadata and
      is not used as a matching threshold.
      \param pConnection Worker database connection.
      \param aResult Candidate receiving a decoded reference image.
      \returns Nothing.
      \throw std::runtime_error When the PostgreSQL image query fails.
      */
      static void LoadReference(PGconn* pConnection,
         CardImageMatchResult& aResult) {
         if(aResult.strAssetId.empty())return;
         char const* arrParams[]{aResult.strAssetId.c_str()};
         result_ty aRows(PQexecParams(pConnection,
            "SELECT image_data, COALESCE(sha256,'') "
            "FROM deckkernel_test.scryfall_image_assets "
            "WHERE id=$1::bigint",
            1,nullptr,arrParams,nullptr,nullptr,0));
         if(!aRows || PQresultStatus(aRows.get())!=PGRES_TUPLES_OK) {
            throw std::runtime_error(std::format(
               "reference image query failed: {}",PQerrorMessage(pConnection)));
         }
         if(PQntuples(aRows.get())!=1 || PQgetisnull(aRows.get(),0,0)) {
            aResult.strReferenceDiagnostic="Database image bytes missing";
            return;
         }
         std::size_t uSize{};
         std::unique_ptr<unsigned char,decltype(&PQfreemem)> upBytes(
            PQunescapeBytea(reinterpret_cast<unsigned char const*>(
               PQgetvalue(aRows.get(),0,0)),&uSize),&PQfreemem);
         if(!upBytes || uSize==0) {
            aResult.strReferenceDiagnostic="Database image could not be read";
            return;
         }
         constexpr std::size_t cuMaxImageBytes=16*1024*1024;
         if(uSize>cuMaxImageBytes ||
            uSize>static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            aResult.strReferenceDiagnostic=std::format(
               "Reference image too large ({} bytes)",uSize);
            return;
         }
         cv::Mat const aEncoded(1,static_cast<int>(uSize),
            CV_8UC1,upBytes.get());
         aResult.aReferenceImage=cv::imdecode(aEncoded,cv::IMREAD_COLOR);
         if(aResult.aReferenceImage.empty()) {
            aResult.strReferenceDiagnostic="OpenCV could not decode DB reference image";
            return;
         }
         std::string const strSha256=PQgetvalue(aRows.get(),0,1);
         aResult.strReferenceDiagnostic=strSha256.size()==64?
            "Original DB image loaded; SHA-256 metadata present":
            "Original DB image loaded; SHA-256 metadata missing/invalid";
         deckkernel::camera::Trace("[CAM-REFERENCE] card #{} asset={} size={}x{} bytes={} sha256={}",
            aResult.uEvent,aResult.strAssetId,aResult.aReferenceImage.cols,
            aResult.aReferenceImage.rows,uSize,
            strSha256.size()==64?"present":"missing-or-invalid");
      }

      /**
      \brief Resolve a selection into one display image, without recognizing.
      \details The physical card and its frozen capture are not accessed.
      \param pConnection Dedicated database connection.
      \param aTask Selection request for a particular presentation.
      \returns Candidate metadata and reference image.
      \throw std::runtime_error If a database read fails.
      */
      CardImageMatchResult DisplaySelection(PGconn* pConnection,
         Task const& aTask) {
         CardImageMatchResult aResult{aTask.uEvent};
         aResult.boCandidateSelection=true;
         aResult.uSelectionSerial=aTask.uSelectionSerial;
         aResult.strStatus="PRINTING SELECTION - choose with arrows, Enter confirms";
         if(aTask.optSelection) {
            SetPrinting(aResult,*aTask.optSelection);
            LoadReference(pConnection,aResult);
         }
         return aResult;
      }

      /**
      \brief Recover printing candidates using an OCR card title as last resort.
      \details Exact case-insensitive name equality is required. OCR cannot
         establish a printing: every returned printing stays unverified.
         The query runs only after artwork recognition failed.
      \param pConnection Worker-owned PostgreSQL connection.
      \param aTask Frozen-card event containing a name-OCR observation.
      \returns Candidate printings with no claim of visual verification.
      \throw std::runtime_error On SQL errors.
      */
      CardImageMatchResult MatchOcrName(PGconn* pConnection,
         Task const& aTask) {
         CardImageMatchResult aResult{aTask.uEvent};
         aResult.boComplete=true;
         aResult.strStatus="OCR NAME NOT FOUND - discard or retry";
         if(!aTask.optOcrName || aTask.optOcrName->size()<3)
            return aResult;
         char const* arrParams[]{aTask.optOcrName->c_str()};
         result_ty aRows(PQexecParams(pConnection,
            "SELECT a.id::text,c.scryfall_id,cf.face_id::text,cf.name,"
            "s.code,c.collector_number,c.lang,v.variant,"
            "cf.type_line,cf.mana_cost,s.name,r.code,"
            "COALESCE(c.oracle_id,'') "
            "FROM deckkernel_test.scryfall_card_faces cf "
            "JOIN deckkernel_test.scryfall_cards c ON c.id=cf.card_id "
            "JOIN deckkernel_test.scryfall_sets s ON s.id=c.set_id "
            "JOIN deckkernel_test.dom_rarities r ON r.id=c.rarity_id "
            "JOIN deckkernel_test.scryfall_card_image_variants v "
            "ON v.card_id=cf.card_id AND v.face_id=cf.face_id "
            "AND v.variant='normal' "
            "JOIN deckkernel_test.scryfall_image_assets a "
            "ON a.id=v.asset_id AND a.image_data IS NOT NULL "
            "WHERE lower(cf.name)=lower($1::text) "
            "ORDER BY s.code,c.collector_number LIMIT 32",
            1,nullptr,arrParams,nullptr,nullptr,0));
         if(!aRows || PQresultStatus(aRows.get())!=PGRES_TUPLES_OK)
            throw std::runtime_error(std::format(
               "OCR name lookup failed: {}",PQerrorMessage(pConnection)));
         for(int iRow=0;iRow<PQntuples(aRows.get());++iRow) {
            PrintingCandidate aCandidate;
            aCandidate.strAssetId=PQgetvalue(aRows.get(),iRow,0);
            aCandidate.strPrintingId=PQgetvalue(aRows.get(),iRow,1);
            aCandidate.iFace=std::stoi(PQgetvalue(aRows.get(),iRow,2));
            aCandidate.strName=PQgetvalue(aRows.get(),iRow,3);
            aCandidate.strSet=PQgetvalue(aRows.get(),iRow,4);
            aCandidate.strCollector=PQgetvalue(aRows.get(),iRow,5);
            aCandidate.strLanguage=PQgetvalue(aRows.get(),iRow,6);
            aCandidate.strVariant=PQgetvalue(aRows.get(),iRow,7);
            aCandidate.strTypeLine=PQgetvalue(aRows.get(),iRow,8);
            aCandidate.strManaCost=PQgetvalue(aRows.get(),iRow,9);
            aCandidate.strSetName=PQgetvalue(aRows.get(),iRow,10);
            aCandidate.strRarity=PQgetvalue(aRows.get(),iRow,11);
            aCandidate.strOracleId=PQgetvalue(aRows.get(),iRow,12);
            aResult.vecCandidates.push_back(std::move(aCandidate));
         }
         if(aResult.vecCandidates.empty())return aResult;
         aResult.boOcrNameRecognized=true;
         aResult.boCandidatesTruncated=PQntuples(aRows.get())==32;
         SetPrinting(aResult,aResult.vecCandidates.front());
         aResult.strStatus=std::format(
            "OCR TITLE MATCH - {} possible printings (UNVERIFIED)",
            aResult.vecCandidates.size());
         aResult.strEdition="OCR title only; printing requires manual selection";
         LoadReference(pConnection,aResult);
         return aResult;
      }

      [[nodiscard]] bool Cancelled(unsigned uEvent) const noexcept {
         return m_uActiveEvent.load(std::memory_order_relaxed)!=uEvent;
      }

      CardImageMatchResult Match(PGconn* pConnection, Task const& aTask) {
         // PHASE 06: compare BOTH orientations and two card-art windows.
         // Never stop at the first barely acceptable visual match: it may
         // be the wrong 180-degree orientation and poison the whole session.
         struct ArtworkChoice {
            Ranked aRanked;
            int iOrientation{};
            int iVariant{};
         };
         std::optional<ArtworkChoice> optArtworkChoice;
         for(int iOrientation : {0,180}) {
            if(Cancelled(aTask.uEvent))return CardImageMatchResult{aTask.uEvent};
            cv::Mat aOriented=aTask.aCard;
            if(iOrientation==180)
               cv::rotate(aTask.aCard,aOriented,cv::ROTATE_180);
            for(int iVariant=0;iVariant<2;++iVariant) {
               if(Cancelled(aTask.uEvent))return CardImageMatchResult{aTask.uEvent};
               // Normal mode evaluates the measured crop first. A strong
               // illustration match need not pay for three other SQL scans.
               if(optArtworkChoice && !deckkernel::camera::TraceEnabled() &&
                  optArtworkChoice->aRanked.iOrb>=25 &&
                  optArtworkChoice->aRanked.iHash<=20)break;
               ArtworkRegion const aRegion=Artwork(aOriented,iVariant);
               if(aRegion.aImage.empty())continue;
               if(!aTask.aEvidenceDirectory.empty()) {
                  std::filesystem::create_directories(
                     aTask.aEvidenceDirectory);
                  std::filesystem::path const aImagePath=
                     aTask.aEvidenceDirectory/std::format(
                        "event-{:06}-artwork-{}-window-{}.png",
                        aTask.uEvent,iOrientation,iVariant+1);
                  if(!cv::imwrite(aImagePath.string(),aRegion.aImage))
                     std::println(std::cerr,
                        "[CAM-ART] cannot save {}",aImagePath.string());
               }
               deckkernel::camera::Trace("[CAM-ART-CROP] card={} orientation={} window={} "
                  "origin={},{} size={}x{} measured={} separators={:.2f}/{:.2f}",
                  aTask.uEvent,iOrientation,iVariant+1,
                  aRegion.aRect.x,aRegion.aRect.y,
                  aRegion.aRect.width,aRegion.aRect.height,
                  aRegion.boDetected,aRegion.flTopCoverage,
                  aRegion.flBottomCoverage);
               Publish(CardImageMatchResult{
                  aTask.uEvent,std::format(
                     "SEARCHING ARTWORK - {} deg, window {}",
                     iOrientation,iVariant+1),
                  "Comparing indexed illustrations",false});
               std::optional<Ranked> optCandidate=FindArtwork(
                  pConnection,aRegion.aImage);
               if(Cancelled(aTask.uEvent))
                  return CardImageMatchResult{aTask.uEvent};
               bool const boAccepted=optCandidate &&
                  AcceptArtwork(*optCandidate);
               deckkernel::camera::Trace("[CAM-ART] card={} orientation={} window={} "
                  "asset={} hash={}/64 orb={} accepted={}",
                  aTask.uEvent,iOrientation,iVariant+1,
                  optCandidate?optCandidate->strAssetId:"",
                  optCandidate?optCandidate->iHash:65,
                  optCandidate?optCandidate->iOrb:0,boAccepted);
               if(boAccepted && (!optArtworkChoice ||
                  Better(*optCandidate,optArtworkChoice->aRanked))) {
                  optArtworkChoice=ArtworkChoice{
                     std::move(*optCandidate),iOrientation,iVariant};
               }
            }
         }
         if(Cancelled(aTask.uEvent))return CardImageMatchResult{aTask.uEvent};
         if(optArtworkChoice) {
            int const iOrientation=optArtworkChoice->iOrientation;
            Ranked const& aArt=optArtworkChoice->aRanked;
            cv::Mat aOriented=aTask.aCard;
            if(iOrientation==180)
               cv::rotate(aTask.aCard,aOriented,cv::ROTATE_180);
            CardImageMatchResult aArtworkResult{
               aTask.uEvent,"ARTWORK FOUND - checking concrete printing",
               "Illustration matched; printing not yet identified",false};
            aArtworkResult.boArtworkRecognized=true;
            aArtworkResult.strAssetId=aArt.strAssetId;
            aArtworkResult.strName=ArtworkName(
               pConnection,aArt.strAssetId);
            aArtworkResult.strReferenceLabel="DB artwork (art_crop)";
            aArtworkResult.iOrientation=iOrientation;
            LoadReference(pConnection,aArtworkResult);
            Publish(aArtworkResult);
            std::vector<Ranked> vecPrintings=FindPrintings(
               pConnection,aOriented,std::optional<Ranked>{aArt});
            if(Cancelled(aTask.uEvent))return CardImageMatchResult{aTask.uEvent};
            RefinePrintingImages(pConnection,aOriented,vecPrintings);
            CardImageMatchResult aResult=SummarizePrintings(
               vecPrintings,true,aTask.uEvent,iOrientation);
            aResult.boArtworkRecognized=true;
            if(aResult.strAssetId.empty()) {
               aArtworkResult.boComplete=true;
               aArtworkResult.strStatus="ARTWORK FOUND - printing unresolved";
               aArtworkResult.strEdition=std::format(
                  "Illustration {} | possible reprints",
                  aArt.strIllustrationId);
               return aArtworkResult;
            }
            LoadReference(pConnection,aResult);
            return aResult;
         }
         // In normal live scanning, a global full-card search over millions
         // of images is expensive and produces frame/text false positives.
         // Return quickly and let frozen-image OCR be the last resort.
         // --verbose retains the legacy global fallback for diagnosis.
         if(!deckkernel::camera::TraceEnabled())
            return CardImageMatchResult{aTask.uEvent,
               "ARTWORK NOT FOUND - OCR fallback", "Visual evidence insufficient"};
         // Both artwork orientations failed: the full card becomes the fallback.
         cv::Mat aCard=aTask.aCard;
         std::optional<CardImageMatchResult> optBest;
         int iBestOrb = -1, iBestHash = 65;
         aCard = aTask.aCard;
         for(int iOrientation : {0, 180}) {
            if(Cancelled(aTask.uEvent))return CardImageMatchResult{aTask.uEvent};
            Publish(CardImageMatchResult{
               aTask.uEvent,std::format("ARTWORK NOT FOUND - full card {} deg",iOrientation),
               "Searching complete-card index",false});
            std::vector<Ranked> vecMatches =
               FindPrintings(pConnection, aCard, std::nullopt);
            if(Cancelled(aTask.uEvent))return CardImageMatchResult{aTask.uEvent};
            RefinePrintingImages(pConnection,aCard,vecMatches);
            CardImageMatchResult aResult = SummarizePrintings(
               vecMatches, false, aTask.uEvent, iOrientation);
            if(!vecMatches.empty() && AcceptFull(vecMatches.front()) &&
               vecMatches.front().iArtworkMatches>=12 &&
               (vecMatches.front().iOrb > iBestOrb ||
               (vecMatches.front().iOrb == iBestOrb &&
                  vecMatches.front().iHash < iBestHash))) {
               iBestOrb = vecMatches.front().iOrb;
               iBestHash = vecMatches.front().iHash;
               optBest = std::move(aResult);
            }
            if(iOrientation == 0) {
               cv::rotate(aCard, aCard, cv::ROTATE_180);
            }
         }
         if(optBest) {
            LoadReference(pConnection,*optBest);
            return std::move(*optBest);
         }
         return CardImageMatchResult{aTask.uEvent,
            deckkernel::camera::TraceEnabled()?DiagnoseIndex(pConnection):
               "NO VISUAL MATCH - OCR fallback",
            "Edition not verified"};
      }

      /** \brief Publish a worker result without touching capture-thread state.
          \details The latest completed event replaces older results.
          \param aResult Completed result.
          \returns Nothing.
          \throw None.
      */
      void Publish(CardImageMatchResult aResult) {
         std::lock_guard const aLock(m_mutex);
         if(aResult.uEvent!=m_uActiveEvent.load(
            std::memory_order_relaxed))return;
         m_result = std::move(aResult);
      }

      /** \brief Process the bounded asynchronous work queue.
          \details Only this thread accesses PostgreSQL and OpenCV ORB matching.
          \returns Nothing.
          \throw None.
      */
      void Run() noexcept {
         connection_ty aConnection;
         for(;;) {
            Task aTask;
            {
               std::unique_lock aLock(m_mutex);
               m_signal.wait(aLock,[this] {
                  return m_stopping || m_pending.has_value();
               });
               if(m_stopping && !m_pending)break;
               aTask=std::move(*m_pending);
               m_pending.reset();
            }
            if(Cancelled(aTask.uEvent))continue;
            try {
               auto const aStart=std::chrono::steady_clock::now();
               deckkernel::camera::Trace("[CAM-MATCH] card #{} worker started selection={}",
                  aTask.uEvent,aTask.optSelection.has_value());
               if(!aConnection || PQstatus(aConnection.get())!=CONNECTION_OK) {
                  Publish(CardImageMatchResult{aTask.uEvent,
                     "CONNECTING TO POSTGRESQL - image index",
                     "Waiting for database connection",false});
                  aConnection=Connect();
               }
               deckkernel::camera::Trace("[CAM-MATCH] card #{} database ready",
                  aTask.uEvent);
               if(Cancelled(aTask.uEvent))continue;
               if(aTask.optSelection)
                  Publish(DisplaySelection(aConnection.get(),aTask));
               else if(aTask.optOcrName)
                  Publish(MatchOcrName(aConnection.get(),aTask));
               else
                  Publish(Match(aConnection.get(),aTask));
               deckkernel::camera::Trace("[CAM-MATCH] card #{} worker finished elapsed={:.2f}s",
                  aTask.uEvent,std::chrono::duration<double>(
                     std::chrono::steady_clock::now()-aStart).count());
            }
            catch(std::exception const& aException) {
               std::println(std::cerr,"[CAM-MATCH] card #{} failed: {}",
                  aTask.uEvent,aException.what());
               aConnection.reset();
               Publish(CardImageMatchResult{aTask.uEvent,
                  std::format("RECOGNITION ERROR: {}",aException.what()),
                  "Edition not verified"});
            }
         }
      }

   public:
      /** \brief Start the offline image matching worker.
          \details No images are fetched, written or reindexed.
          \param uCandidates Maximum shortlisted references per recognition stage.
          \throw std::system_error If the thread cannot be started.
      */
      explicit CardImageMatcher(unsigned uCandidates)
         : m_uCandidates(uCandidates), m_worker([this] { Run(); }) {
      }

      /** \brief Join the worker and release its PostgreSQL connection.
          \details Pending work is completed before the worker exits.
          \throw None.
      */
      ~CardImageMatcher() {
         {
            std::lock_guard const aLock(m_mutex);
            m_stopping = true;
         }
         m_signal.notify_one();
         if(m_worker.joinable()) {
            m_worker.join();
         }
      }

      CardImageMatcher(CardImageMatcher const&) = delete;
      CardImageMatcher& operator=(CardImageMatcher const&) = delete;

      /** \brief Submit one rectified, stable card without blocking the camera.
          \details Replaces older pending events, retaining bounded memory.
          \param aCard Current rectified camera card.
          \param uEvent Presentation identifier.
          \returns Nothing.
          \throw std::bad_alloc If image cloning fails.
      */
      void Search(cv::Mat const& aCard,unsigned uEvent,
         std::filesystem::path const& aEvidenceDirectory={}) {
         {
            std::lock_guard const aLock(m_mutex);
            m_uActiveEvent.store(uEvent,std::memory_order_relaxed);
            m_pending=Task{uEvent,aCard.clone(),{},0,aEvidenceDirectory};
         }
         m_signal.notify_one();
         deckkernel::camera::Trace("[CAM-MATCH] card #{} queued: art 0/180, then full card; "
            "capture={}x{}",uEvent,aCard.cols,aCard.rows);
      }

      /**
      \brief Request a new candidate image for the active physical card.
      \details No global search, artwork search, or frozen-frame read happens.
      \param aCandidate DB candidate chosen with left/right arrow keys.
      \param uEvent Stable physical-card event ID.
      \returns Nothing.
      \throw std::bad_alloc If the pending task cannot be allocated.
      */
      void Select(PrintingCandidate const& aCandidate,unsigned uEvent,
         unsigned uSelectionSerial) {
         {
            std::lock_guard const aLock(m_mutex);
            if(m_uActiveEvent.load(std::memory_order_relaxed)!=uEvent)return;
            m_pending=Task{uEvent,{},aCandidate,uSelectionSerial};
         }
         m_signal.notify_one();
      }

      /**
      \brief Invalidate a discarded physical-card event.
      \details Discards pending work and stops the running worker between
         database and ORB phases. An SQL statement already executing may
         complete first; stale results are not published.
      \returns Nothing.
      \throw None.
      */
      void Cancel() noexcept {
         std::lock_guard const aLock(m_mutex);
         m_uActiveEvent.store(0,std::memory_order_relaxed);
         m_pending.reset();
         m_result.reset();
      }

      /**
      \brief Search candidate printings by the final title-OCR observation.
      \details Reuses the existing worker and current frozen event. No
         artwork rescan or global full-card match is launched.
      \param strName OCR text with adequate confidence.
      \param uEvent Frozen card event.
      \returns Nothing.
      \throw std::bad_alloc If queueing the OCR result fails.
      */
      void SearchName(std::string const& strName,unsigned uEvent) {
         std::lock_guard const aLock(m_mutex);
         if(m_uActiveEvent.load(std::memory_order_relaxed)!=uEvent)return;
         m_pending=Task{uEvent,{}, {},0,{},strName};
         m_signal.notify_one();
      }

      /** \brief Receive the latest completed asynchronous result.
          \details Intended for the capture thread; never blocks on PostgreSQL.
          \returns One result or std::nullopt if no new match is ready.
          \throw None.
      */
      std::optional<CardImageMatchResult> Poll() {
         std::lock_guard const aLock(m_mutex);
         std::optional<CardImageMatchResult> optResult = std::move(m_result);
         m_result.reset();
         return optResult;
      }
   };
}
