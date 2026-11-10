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

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/features2d.hpp>
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
      };

      struct Ranked final {
         std::string strAssetId;
         std::string strIllustrationId;
         std::string strPrintingId;
         std::string strName;
         std::string strSet;
         std::string strCollector;
         std::string strLanguage;
         std::string strVariant;
         int iFace{};
         int iHash{65};
         int iOrb{};
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
         std::string const strLimit = std::to_string(m_uCandidates);
         char const* arrParams[]{
            strHash.c_str(),
            optArtwork ? optArtwork->strAssetId.c_str() : nullptr,
            optArtwork ? optArtwork->strIllustrationId.c_str() : nullptr,
            strLimit.c_str()
         };
         result_ty aRows(PQexecParams(pConnection,
            "SELECT a.id::text, c.scryfall_id, cf.face_id::text, cf.name, "
            "s.code, c.collector_number, c.lang, v.variant, "
            "bit_count(f.perceptual_hash # $1::bit(64))::text, f.orb_descriptors "
            "FROM deckkernel_test.scryfall_image_features f "
            "JOIN deckkernel_test.scryfall_image_assets a ON a.id=f.asset_id "
            "JOIN deckkernel_test.scryfall_card_image_variants v "
            "ON v.asset_id=a.id AND v.variant IN ('normal','border_crop','large') "
            "JOIN deckkernel_test.scryfall_cards c ON c.id=v.card_id "
            "JOIN deckkernel_test.scryfall_sets s ON s.id=c.set_id "
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

      /** \brief Compare full-card candidates without claiming a foil finish.
          \details Similar top-ranked physical printings are explicitly ambiguous.
          \param vecMatches Ranked candidates, including possible reprints.
          \param boArtwork True when an artwork candidate restricted the search.
          \param uEvent Camera presentation number.
          \param iOrientation Rotation applied to the worker's original card.
          \returns Result with conservative, unverified identity.
          \throw None.
      */
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
         bool const boAmbiguous = vecMatches.size() > 1 &&
            vecMatches[1].iOrb >= aBest.iOrb -
               std::max(5, aBest.iOrb / 8) &&
            vecMatches[1].iHash <= aBest.iHash + 4;
         aResult.strStatus = boAmbiguous ?
            "AMBIGUOUS; several printings resemble the card" :
            (boArtwork ? "ARTWORK_MATCH; printing candidate" :
               "FULL_CARD_FALLBACK; printing candidate");
         aResult.strEdition = std::format(
            "{} | {} #{} | {} | face={} | {} | ORB={} dHash={}/64 | UNVERIFIED",
            aBest.strName, aBest.strSet, aBest.strCollector,
            aBest.strLanguage, aBest.iFace, aBest.strPrintingId,
            aBest.iOrb, aBest.iHash);
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
      CardImageMatchResult Match(PGconn* pConnection, Task const& aTask) const {
         cv::Mat aCard = aTask.aCard;
         for(int iOrientation : {0, 180}) {
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
               CardImageMatchResult aResult = SummarizePrintings(
                  FindPrintings(pConnection, aCard, optArt), true,
                  aTask.uEvent, iOrientation);
               if(aResult.strEdition == "Edition not verified") {
                  std::string const strName = ArtworkName(
                     pConnection, optArt->strAssetId);
                  aResult.strEdition = std::format(
                     "{} | illustration={} | UNVERIFIED",
                     strName, optArt->strIllustrationId);
               }
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
            std::vector<Ranked> const vecMatches =
               FindPrintings(pConnection, aCard, std::nullopt);
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
         return optBest.value_or(CardImageMatchResult{
            aTask.uEvent, "UNKNOWN; no reliable artwork or full-card match",
            "Edition not verified"});
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
         try {
            connection_ty aConnection = Connect();
            for(;;) {
               Task aTask;
               {
                  std::unique_lock aLock(m_mutex);
                  m_signal.wait(aLock, [this] {
                     return m_stopping || m_pending.has_value();
                  });
                  if(m_stopping && !m_pending) {
                     break;
                  }
                  aTask = std::move(*m_pending);
                  m_pending.reset();
               }
               try {
                  Publish(Match(aConnection.get(), aTask));
               }
               catch(std::exception const& aException) {
                  std::println(std::cerr,
                     "[CAM-MATCH] card #{} error: {}",
                     aTask.uEvent, aException.what());
                  Publish(CardImageMatchResult{aTask.uEvent,
                     "MATCH_ERROR; see diagnostics", "Edition not verified"});
               }
            }
         }
         catch(std::exception const& aException) {
            std::println(std::cerr, "[CAM-MATCH] worker unavailable: {}",
               aException.what());
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
