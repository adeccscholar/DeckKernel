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
         std::array<std::string, 7> arrFields{
            Env("DECKKERNEL_PGHOST", "localhost"),
            Env("DECKKERNEL_PGPORT", "5432"),
            Env("DECKKERNEL_PGDATABASE", "DeckKernel"),
            Env("DECKKERNEL_PGUSER", "deckkernel_user"),
            Env("DECKKERNEL_PGPASSWORD", ""),
            Env("DECKKERNEL_PGSSLMODE", "prefer"),
            Env("DECKKERNEL_PG_GSSENCMODE", "disable")
         };
         char const* arrKeys[]{
            "host", "port", "dbname", "user", "password", "sslmode",
            "gssencmode", nullptr
         };
         char const* arrValues[]{
            arrFields[0].c_str(), arrFields[1].c_str(), arrFields[2].c_str(),
            arrFields[3].c_str(), arrFields[4].c_str(), arrFields[5].c_str(),
            arrFields[6].c_str(), nullptr
         };
         connection_ty aConnection(PQconnectdbParams(arrKeys, arrValues, 0));
         if(!aConnection || PQstatus(aConnection.get()) != CONNECTION_OK) {
            throw std::runtime_error("camera image matcher: PostgreSQL connection failed");
         }
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

      /** \brief Extract the provisional standard-card artwork region.
          \details Alternate layouts will need layout-specific regions.
          \param aCard Perspective-rectified full card.
          \returns A non-owning artwork ROI.
          \throw cv::Exception For an invalid image.
      */
      static cv::Mat Artwork(cv::Mat const& aCard) {
         int const iX = aCard.cols / 12;
         int const iY = aCard.rows * 19 / 100;
         return aCard(cv::Rect(iX, iY,
            aCard.cols - 2 * iX, aCard.rows * 33 / 100));
      }

      /** \brief Compute camera ORB descriptors once per candidate orientation.
          \details The stored reference descriptors are reused without image decoding.
          \param aImage Camera artwork or full-card view.
          \returns OpenCV ORB descriptor matrix.
          \throw cv::Exception If descriptor extraction fails.
      */
      static cv::Mat Descriptors(cv::Mat const& aImage) {
         cv::Ptr<cv::ORB> aOrb = cv::ORB::create(500);
         std::vector<cv::KeyPoint> vecKeypoints;
         cv::Mat aDescriptors;
         aOrb->detectAndCompute(aImage, cv::noArray(), vecKeypoints, aDescriptors);
         return aDescriptors;
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
         int iAccepted{};
         for(std::vector<cv::DMatch> const& vecPair : vecPairs) {
            if(vecPair.size() == 2 &&
               vecPair[0].distance < 0.75f * vecPair[1].distance) {
               ++iAccepted;
            }
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
         std::string const strLimit = std::to_string(m_uCandidates);
         char const* arrParams[]{strHash.c_str(), strLimit.c_str()};
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
         std::string const strHash = DHashBits(aCard);
         // PHASE 07: once artwork is known, expand candidate enumeration.
         std::string const strLimit = std::to_string(
            optArtwork?std::max(1000u,m_uCandidates):m_uCandidates);
         char const* arrParams[]{
            strHash.c_str(),
            optArtwork ? optArtwork->strAssetId.c_str() : nullptr,
            optArtwork ? optArtwork->strIllustrationId.c_str() : nullptr,
            strLimit.c_str()
         };
         result_ty aRows(PQexecParams(pConnection,
            "SELECT a.id::text, c.scryfall_id, cf.face_id::text, cf.name, "
            "s.code, c.collector_number, c.lang, v.variant, "
            "bit_count(f.perceptual_hash # $1::bit(64))::text, f.orb_descriptors, "
            "cf.type_line, cf.mana_cost, s.name, r.code, COALESCE(c.oracle_id,'') "
            "FROM deckkernel_test.scryfall_image_features f "
            "JOIN deckkernel_test.scryfall_image_assets a ON a.id=f.asset_id "
            "JOIN deckkernel_test.scryfall_card_image_variants v "
            "ON v.asset_id=a.id AND v.variant IN ('normal','border_crop','large') "
            "JOIN deckkernel_test.scryfall_cards c ON c.id=v.card_id "
            "JOIN deckkernel_test.scryfall_sets s ON s.id=c.set_id "
            "JOIN deckkernel_test.dom_rarities r ON r.id=c.rarity_id "
            "JOIN deckkernel_test.scryfall_card_faces cf "
            "ON cf.card_id=v.card_id AND cf.face_id=v.face_id "
            "WHERE f.algorithm='dhash64-orb' AND f.algorithm_version=1 "
            "AND f.perceptual_hash IS NOT NULL AND a.image_data IS NOT NULL "
            "AND ($2::bigint IS NULL OR EXISTS ("
            "  SELECT 1 FROM deckkernel_test.scryfall_card_image_variants va "
            "  JOIN deckkernel_test.scryfall_image_assets aa ON aa.id=va.asset_id "
            "  WHERE va.card_id=v.card_id AND va.face_id=v.face_id "
            "    AND va.variant='art_crop' "
            "    AND (va.asset_id=$2::bigint OR "
            "       ($3::text<>'' AND aa.illustration_id=$3::text)))) "
            "ORDER BY bit_count(f.perceptual_hash # $1::bit(64)), a.id "
            "LIMIT $4::integer",
            4, nullptr, arrParams, nullptr, nullptr, 0));
         if(!aRows || PQresultStatus(aRows.get()) != PGRES_TUPLES_OK) {
            throw std::runtime_error(std::format(
               "full-card feature lookup failed: {}", PQerrorMessage(pConnection)));
         }
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
         }
         std::stable_sort(vecMatches.begin(),vecMatches.end(),
            [](Ranked const& aLeft,Ranked const& aRight) {
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
         if(vecMatches.empty() || !AcceptFull(vecMatches.front())) {
            aResult.strStatus = boArtwork ?
               "ARTWORK_CANDIDATE; printing unresolved" :
               "UNKNOWN; no reliable artwork or full-card match";
            return aResult;
         }
         Ranked const& aBest = vecMatches.front();
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
         std::println("[CAM-PRINT] card #{} orientation={} variant={} "
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
         std::println("[CAM-INDEX] assets={} downloaded={} indexed={} missing-sha256={} empty-orb={}",
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
         std::println("[CAM-REFERENCE] card #{} asset={} size={}x{} bytes={} sha256={}",
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
         aResult.strStatus="PRINTING SELECTION - choose with arrows, Enter confirms";
         if(aTask.optSelection) {
            SetPrinting(aResult,*aTask.optSelection);
            LoadReference(pConnection,aResult);
         }
         return aResult;
      }

      CardImageMatchResult Match(PGconn* pConnection, Task const& aTask) {
         cv::Mat aCard = aTask.aCard;
         for(int iOrientation : {0, 180}) {
            Publish(CardImageMatchResult{
               aTask.uEvent,std::format("SEARCHING ARTWORK - orientation {} deg",iOrientation),
               "Comparing indexed illustrations",false});
            cv::Mat const aArt = Artwork(aCard);
            std::optional<Ranked> optArt = FindArtwork(pConnection, aArt);
            bool const boRecognized = optArt && AcceptArtwork(*optArt);
            std::println("[CAM-ART] card #{} orientation={} candidate-asset={} "
               "dhash={}/64 orb-good={} status={}",
               aTask.uEvent, iOrientation,
               optArt ? optArt->strAssetId : "",
               optArt ? optArt->iHash : 65,
               optArt ? optArt->iOrb : 0,
               boRecognized ? "ARTWORK_CANDIDATE" : "NO_RELIABLE_ARTWORK");
            if(boRecognized) {
               CardImageMatchResult aArtworkResult{
                  aTask.uEvent,"ARTWORK FOUND - checking concrete printing",
                  "Illustration matched; printing not yet identified",false};
               aArtworkResult.strAssetId=optArt->strAssetId;
               aArtworkResult.strName=ArtworkName(
                  pConnection,optArt->strAssetId);
               aArtworkResult.strReferenceLabel="DB artwork (art_crop)";
               aArtworkResult.iOrientation=iOrientation;
               LoadReference(pConnection,aArtworkResult);
               Publish(aArtworkResult);
               std::vector<Ranked> vecPrintings=
                  FindPrintings(pConnection,aCard,optArt);
               // PHASE 07: compare full-card and printing-specific regions.
               RefinePrintingImages(pConnection,aCard,vecPrintings);
               CardImageMatchResult aResult=SummarizePrintings(
                  vecPrintings,true,aTask.uEvent,iOrientation);
               if(aResult.strAssetId.empty()) {
                  aArtworkResult.boComplete=true;
                  aArtworkResult.strStatus=
                     "ARTWORK FOUND - printing unresolved";
                  aArtworkResult.strEdition=std::format(
                     "Illustration {} | possible reprints",
                     optArt->strIllustrationId);
                  return aArtworkResult;
               }
               LoadReference(pConnection,aResult);
               return aResult;
            }
            if(iOrientation == 0) {
               cv::rotate(aCard, aCard, cv::ROTATE_180);
            }
         }
         // Both artwork orientations failed: the full card becomes the fallback.
         std::optional<CardImageMatchResult> optBest;
         int iBestOrb = -1, iBestHash = 65;
         aCard = aTask.aCard;
         for(int iOrientation : {0, 180}) {
            Publish(CardImageMatchResult{
               aTask.uEvent,std::format("ARTWORK NOT FOUND - full card {} deg",iOrientation),
               "Searching complete-card index",false});
            std::vector<Ranked> vecMatches =
               FindPrintings(pConnection, aCard, std::nullopt);
            RefinePrintingImages(pConnection,aCard,vecMatches);
            CardImageMatchResult aResult = SummarizePrintings(
               vecMatches, false, aTask.uEvent, iOrientation);
            if(!vecMatches.empty() && AcceptFull(vecMatches.front()) &&
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
         return CardImageMatchResult{aTask.uEvent,DiagnoseIndex(pConnection),
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
            try {
               if(!aConnection || PQstatus(aConnection.get())!=CONNECTION_OK)
                  aConnection=Connect();
               if(aTask.optSelection)
                  Publish(DisplaySelection(aConnection.get(),aTask));
               else
                  Publish(Match(aConnection.get(),aTask));
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
      void Search(cv::Mat const& aCard, unsigned uEvent) {
         {
            std::lock_guard const aLock(m_mutex);
            m_pending = Task{uEvent, aCard.clone()};
         }
         m_signal.notify_one();
         std::println("[CAM-MATCH] card #{} queued: art 0/180, then full card",
            uEvent);
      }

      /**
      \brief Request a new candidate image for the active physical card.
      \details No global search, artwork search, or frozen-frame read happens.
      \param aCandidate DB candidate chosen with left/right arrow keys.
      \param uEvent Stable physical-card event ID.
      \returns Nothing.
      \throw std::bad_alloc If the pending task cannot be allocated.
      */
      void Select(PrintingCandidate const& aCandidate,unsigned uEvent) {
         {
            std::lock_guard const aLock(m_mutex);
            m_pending=Task{uEvent,{},aCandidate};
         }
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
