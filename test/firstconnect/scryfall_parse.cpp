// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_parse.cpp
\brief Lesson 2: stream, parse and normalize Scryfall bulk data.
\details
The Scryfall bulk representation is an external transport shape. Each card object repeats
set data. This source decomposes that representation into one deduplicated set relation
and one card-printing relation before PostgreSQL is involved.
*/

#include "scryfall_parse.h"

#include "convert_integral.h"

#include <nlohmann/json.hpp>
#include <zlib.h>

#include <array>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <algorithm>
#include <optional>
#include <ranges>
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace deckkernel::test {
namespace {

using json_ty = nlohmann::json;

void ParseCatalog(std::string const& strJson, std::map<std::string, int>& mpCodes) {
   json_ty const aCatalog = json_ty::parse(strJson);
   for (json_ty const& aCode : aCatalog.at("data")) {
      mpCodes.try_emplace(aCode.get<std::string>(), 0);
      }
   }

// Multiword subtypes are matched against the catalog, preserving source order.
std::vector<std::string> FindSubtypes(std::string const& strTypeLine,
   std::map<std::string, int> const& mpTypes, std::string_view const svMainType) {
   std::vector<std::pair<std::size_t, std::string>> vecMatches;
   std::size_t uStart = 0;
   while (uStart < strTypeLine.size()) {
      std::size_t const uEnd = strTypeLine.find(" // ", uStart);
      std::string_view const svFace{strTypeLine.data() + uStart,
         (uEnd == std::string::npos ? strTypeLine.size() : uEnd) - uStart};
      // Scryfall JSON is UTF-8. Use byte spelling to avoid Windows execution-charset conversion.
      constexpr std::string_view svEmDash{"\xE2\x80\x94", 3};
      std::size_t const uDash = svFace.find(svEmDash);
      if (uDash != std::string_view::npos) {
         std::string_view const svMain = svFace.substr(0, uDash);
         auto const fnWord = [](std::string_view haystack, std::string_view needle) {
            std::size_t const pos = haystack.find(needle);
            return pos != std::string_view::npos &&
               (pos == 0 || haystack[pos - 1] == ' ') &&
               (pos + needle.size() == haystack.size() || haystack[pos + needle.size()] == ' ');
            };
         if (fnWord(svMain, svMainType)) {
            std::string_view const svSubtypes = svFace.substr(uDash + svEmDash.size());
            for (auto const& [strCode, iUnused] : mpTypes) {
               std::size_t uPos = svSubtypes.find(strCode);
               while (uPos != std::string_view::npos) {
                  std::size_t const uAfter = uPos + strCode.size();
                  if ((uPos == 0 || svSubtypes[uPos - 1] == ' ') &&
                      (uAfter == svSubtypes.size() || svSubtypes[uAfter] == ' ')) {
                     vecMatches.emplace_back(uStart + uDash + uPos, strCode);
                     break;
                     }
                  uPos = svSubtypes.find(strCode, uPos + 1);
                  }
               }
            }
         }
      if (uEnd == std::string::npos) { break; }
      uStart = uEnd + 4;
      }
   std::ranges::sort(vecMatches, {}, &std::pair<std::size_t, std::string>::first);
   std::vector<std::string> vecTypes;
   vecTypes.reserve(vecMatches.size());
   for (auto const& [uPos, strCode] : vecMatches) {
      if (std::ranges::find(vecTypes, strCode) == vecTypes.end()) {
         vecTypes.emplace_back(strCode);
         }
      }
   return vecTypes;
   }

// Match ordered whole-word catalog entries in the main-type part.
std::vector<std::string> FindMainTypes(std::string_view svTypeLine,
   std::map<std::string, int> const& mpTypes) {
   constexpr std::string_view svDash{"\xE2\x80\x94", 3};
   auto const uDash = svTypeLine.find(svDash);
   if (uDash != std::string_view::npos) { svTypeLine = svTypeLine.substr(0, uDash); }
   std::vector<std::pair<std::size_t, std::string>> vecMatches;
   for (auto const& [strCode, iUnused] : mpTypes) {
      auto uPos = svTypeLine.find(strCode);
      while (uPos != std::string_view::npos) {
         auto const uEnd = uPos + strCode.size();
         if ((uPos == 0 || svTypeLine[uPos - 1] == ' ') &&
             (uEnd == svTypeLine.size() || svTypeLine[uEnd] == ' ')) {
            vecMatches.emplace_back(uPos, strCode);
            break;
            }
         uPos = svTypeLine.find(strCode, uPos + 1);
         }
      }
   std::ranges::sort(vecMatches, {}, &std::pair<std::size_t, std::string>::first);
   std::vector<std::string> vecResult;
   vecResult.reserve(vecMatches.size());
   for (auto const& [uPos, strCode] : vecMatches) { vecResult.emplace_back(strCode); }
   return vecResult;
   }

std::optional<std::string> OptionalText(json_ty const& aObject, char const* const szField) {
   auto const it = aObject.find(szField);
   if (it == aObject.end() || it->is_null()) { return std::nullopt; }
   return it->get<std::string>();
   }

struct GzipDeleter {
   void operator()(std::remove_pointer_t<gzFile>* pFile) const noexcept {
      if (pFile != nullptr) {
         (void)gzclose(pFile);
         }
      }
   };

using gzip_handle_ty = std::unique_ptr<std::remove_pointer_t<gzFile>, GzipDeleter>;


/**
\brief Decomposes one Scryfall card object into two relational rows.
\param aCard Parsed Scryfall card object.
\param aParsed Destination for set and card rows.
\details
The bulk file repeats set_id, set code and set_name on every printing. We normalize this
before the database boundary: one set tuple is deduplicated by set_id and every printing
keeps set_id as the foreign-key value.
*/
void AppendCard(json_ty const& aCard, ParsedBulkData& aParsed) {
   StagedCard aRow{
      .strUuid = aCard.at("id").get<std::string>(),
      .strName = aCard.at("name").get<std::string>(),
      .strSetCode = aCard.at("set").get<std::string>(),
      .strRarity = aCard.at("rarity").get<std::string>(),
      .optOracleId = OptionalText(aCard, "oracle_id"),
      .optVariationOf = OptionalText(aCard, "variation_of"),
      .optIllustrationId = OptionalText(aCard, "illustration_id"),
      .strCollectorNumber = aCard.at("collector_number").get<std::string>(),
      .strLanguage = aCard.at("lang").get<std::string>(),
      .aReleasedAt = adecc::ConvertTo<adecc::date_ty>(aCard.at("released_at").get<std::string>())
      };
   auto fnAppendFace = [&](int iFaceId, json_ty const& aSource,
      std::string strName, std::string strLine) {
      StagedFace aFace{
         .iFaceId = iFaceId,
         .strName = std::move(strName),
         .strTypeLine = std::move(strLine),
         .strManaCost = aSource.value("mana_cost", std::string{}),
         .optLayout = OptionalText(aSource, "layout"),
         .optLoyalty = OptionalText(aSource, "loyalty"),
         .optPower = OptionalText(aSource, "power")
         };
      aFace.optIllustrationId = OptionalText(aSource, "illustration_id");
      if (!aFace.optIllustrationId) { aFace.optIllustrationId = OptionalText(aCard, "illustration_id"); }
      if (auto const it = aSource.find("image_uris"); it != aSource.end() && it->is_object()) {
         aFace.optImageNormalUrl = OptionalText(*it, "normal");
         aFace.optImageArtCropUrl = OptionalText(*it, "art_crop");
         aFace.optImagePngUrl = OptionalText(*it, "png");
         }
      // A face normally has no layout field of its own; retain the parent layout if supplied.
      if (!aFace.optLayout) { aFace.optLayout = OptionalText(aCard, "layout"); }
      aFace.vecLandTypes = FindSubtypes(aFace.strTypeLine, aParsed.mpLandTypes, "Land");
      aFace.vecCreatureTypes = FindSubtypes(aFace.strTypeLine, aParsed.mpCreatureTypes, "Creature");
      aFace.vecArtifactTypes = FindSubtypes(aFace.strTypeLine, aParsed.mpArtifactTypes, "Artifact");
      aFace.vecSuperTypes = FindMainTypes(aFace.strTypeLine, aParsed.mpSuperTypes);
      aFace.vecMainTypes = FindMainTypes(aFace.strTypeLine, aParsed.mpMainTypes);
      aRow.vecFaces.emplace_back(std::move(aFace));
      };
   fnAppendFace(0, aCard, aRow.strName, aCard.value("type_line", std::string{}));
   if (auto it = aCard.find("card_faces"); it != aCard.end() && it->is_array()) {
      int iFaceId = 1;
      for (json_ty const& aFace : *it) {
         fnAppendFace(iFaceId++, aFace, aFace.value("name", aRow.strName),
            aFace.value("type_line", std::string{}));
         }
      }
   aParsed.vecCards.emplace_back(std::move(aRow));
   }


/**
\brief Reads gzip-compressed JSONL incrementally with zlib.
\param aPath Local .jsonl.gz file.
\param aParsed Destination for normalized tuples.
\details
This is one gzip stream, not a multi-entry archive, so zlib is sufficient and libarchive
is deliberately not involved. The RAII handle guarantees gzclose. A 64 KiB buffer is
read blockwise; strPending keeps a JSON record that crosses a block boundary.
*/
void ReadGzipJsonLines(std::filesystem::path const& aPath, ParsedBulkData& aParsed) {
   gzip_handle_ty upFile{ gzopen(aPath.string().c_str(), "rb") };

   if (!upFile) {
      throw std::runtime_error { std::format("cannot open gzip bulk-data file '{}'", aPath.string()) };
      }

   std::array<char, 64U * 1024U> arrBuffer{};
   std::string strPending;

   while (true) {
      int const iRead = gzread(upFile.get(), arrBuffer.data(), static_cast<unsigned int>(arrBuffer.size()) );

      if (iRead < 0) {
         int iError{};
         char const* const szError = gzerror(upFile.get(), &iError);
         (void)iError;

         throw std::runtime_error{
            std::format(
               "gzip read failed: {}",
               szError == nullptr ? "unknown error" : szError
               )
            };
         }

      if (iRead == 0) {
         break;
         }

      strPending.append(arrBuffer.data(), static_cast<std::size_t>(iRead));

      std::size_t uStart{};

      while (true) {
         std::size_t const uNewLine = strPending.find('\n', uStart);

         if (uNewLine == std::string::npos) {
            strPending.erase(0, uStart);
            break;
            }

         std::string_view const svLine{
            strPending.data() + uStart,
            uNewLine - uStart
            };

         if (!svLine.empty()) {
            AppendCard(json_ty::parse(svLine), aParsed);
            }

         uStart = uNewLine + 1;
         }
      }

   if (!strPending.empty()) {
      AppendCard(json_ty::parse(strPending), aParsed);
      }
   }


void ReadPlainBulk(
   std::filesystem::path const& aPath,
   ParsedBulkData& aParsed
) {
   std::ifstream isFile{ aPath, std::ios::binary };

   if (!isFile) {
      throw std::runtime_error{
         std::format("cannot open bulk-data file '{}'", aPath.string())
         };
      }

   char chFirst{};

   while (isFile.get(chFirst)) {
      if (!std::isspace(static_cast<unsigned char>(chFirst))) {
         break;
         }
      }

   isFile.clear();
   isFile.seekg(0);

   if (chFirst == '[') {
      json_ty const aCards = json_ty::parse(isFile);

      for (json_ty const& aCard : aCards) {
         AppendCard(aCard, aParsed);
         }

      return;
      }

   std::string strLine;

   while (std::getline(isFile, strLine)) {
      if (!strLine.empty()) {
         AppendCard(json_ty::parse(strLine), aParsed);
         }
      }
   }

} // namespace


ParsedBulkData ParseProcess(LoadedBulkData const& aLoaded) {
   ParsedBulkData aParsed;
   ParseCatalog(aLoaded.strLandCatalog, aParsed.mpLandTypes);
   ParseCatalog(aLoaded.strCreatureCatalog, aParsed.mpCreatureTypes);
   ParseCatalog(aLoaded.strSuperCatalog, aParsed.mpSuperTypes);
   ParseCatalog(aLoaded.strMainTypeCatalog, aParsed.mpMainTypes);
   ParseCatalog(aLoaded.strArtifactCatalog, aParsed.mpArtifactTypes);
   // Validate the actual catalog and UTF-8 parser before reading the full bulk file.
   constexpr std::string_view svEmDash{"\xE2\x80\x94", 3};
   std::string const strDash{svEmDash};
   auto fnCheck = [&](std::string const& strLine, std::string_view svMain,
      std::map<std::string, int> const& mpCatalog, std::string_view svExpected) {
      auto const vecFound = FindSubtypes(strLine, mpCatalog, svMain);
      if (std::ranges::find(vecFound, std::string{svExpected}) == vecFound.end()) {
         throw std::runtime_error(std::format(
            "Subtype parser preflight failed: type_line='{}', category='{}', "
            "expected='{}', catalog_size={}, matches={}",
            strLine, svMain, svExpected, mpCatalog.size(), vecFound.size()));
         }
      };
   fnCheck("Basic Land " + strDash + " Swamp", "Land", aParsed.mpLandTypes, "Swamp");
   fnCheck("Legendary Creature " + strDash + " Vedalken Artificer",
      "Creature", aParsed.mpCreatureTypes, "Vedalken");
   fnCheck("Legendary Creature " + strDash + " Vedalken Artificer",
      "Creature", aParsed.mpCreatureTypes, "Artificer");
   if (aParsed.mpLandTypes.contains("Power-Plant")) {
      fnCheck("Land " + strDash + " Urza's Power-Plant",
         "Land", aParsed.mpLandTypes, "Power-Plant");
      }
   std::println("[PARSE] UTF-8 subtype preflight passed (land={}, creature={})",
      aParsed.mpLandTypes.size(), aParsed.mpCreatureTypes.size());
   json_ty const aSets = json_ty::parse(aLoaded.strSetsCatalog);
   for (json_ty const& aSet : aSets.at("data")) {
      StagedSet aRow{
         .strUuid = aSet.at("id").get<std::string>(),
         .strCode = aSet.at("code").get<std::string>(),
         .strName = aSet.at("name").get<std::string>(),
         .optParentCode = OptionalText(aSet, "parent_set_code")
         };
      if (!aParsed.mpSets.emplace(aRow.strCode, std::move(aRow)).second) {
         throw std::runtime_error("duplicate set code in Scryfall catalog");
         }
      }

   if (aLoaded.aDescriptor.boGzipJsonLines) {
      ReadGzipJsonLines(aLoaded.aPath, aParsed);
      }
   else {
      ReadPlainBulk(aLoaded.aPath, aParsed);
      }

   std::size_t uLandMatches = 0, uCreatureMatches = 0;
   for (StagedCard const& aCard : aParsed.vecCards) {
      for (StagedFace const& aFace : aCard.vecFaces) {
         uLandMatches += aFace.vecLandTypes.size();
         uCreatureMatches += aFace.vecCreatureTypes.size();
         }
      }
   std::println("[PARSE] Subtype matches: land={}, creature={}, cards={}",
      uLandMatches, uCreatureMatches, aParsed.vecCards.size());
   if (uLandMatches == 0 || uCreatureMatches == 0) {
      throw std::runtime_error(std::format(
         "Subtype parsing found land={} creature={} across {} cards; check catalog and type_line",
         uLandMatches, uCreatureMatches, aParsed.vecCards.size()));
      }
   std::println(
      "Parsed {} cards from {} sets.",
      aParsed.vecCards.size(),
      aParsed.mpSets.size()
      );

   return aParsed;
   }

} // namespace deckkernel::test
