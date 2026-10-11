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
#include <print>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace deckkernel::test {
namespace {

using json_ty = nlohmann::json;

// The catalog gives codes, not IDs. Assign stable IDs in catalog order for this fresh-load test.
void ParseCatalog(std::string const& strJson, std::map<std::string, int>& mpCodes) {
   json_ty const aCatalog = json_ty::parse(strJson);
   for (json_ty const& aCode : aCatalog.at("data")) {
      std::string const strCode = aCode.get<std::string>();
      mpCodes.try_emplace(strCode, static_cast<int>(mpCodes.size() + 1));
      }
   }

std::optional<std::string> FindSubtype(std::string const& strTypeLine,
   std::map<std::string, int> const& mpTypes) {
   std::size_t const uSep = strTypeLine.find(" — ");
   if (uSep == std::string::npos) { return std::nullopt; }
   std::string const strSubtypes = strTypeLine.substr(uSep + std::string_view(" — ").size());
   std::optional<std::string> optFound;
   for (auto const& [strCode, iId] : mpTypes) {
      std::size_t uOffset = strSubtypes.find(strCode);
      while (uOffset != std::string::npos) {
         std::size_t const uEnd = uOffset + strCode.size();
         if ((uOffset == 0 || strSubtypes[uOffset - 1] == ' ') &&
             (uEnd == strSubtypes.size() || strSubtypes[uEnd] == ' ')) {
            if (!optFound || strCode.size() > optFound->size()) { optFound = strCode; }
            break;
            }
         uOffset = strSubtypes.find(strCode, uOffset + 1);
         }
      }
   return optFound;
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
      .optOracleId = aCard.value("oracle_id", std::optional<std::string>{}),
      .optVariationOf = aCard.value("variation_of", std::optional<std::string>{}),
      .aReleasedAt = adecc::ConvertTo<adecc::date_ty>(aCard.at("released_at").get<std::string>()),
      .optLandType = FindSubtype(aCard.value("type_line", ""), aParsed.mpLandTypes),
      .optCreatureType = FindSubtype(aCard.value("type_line", ""), aParsed.mpCreatureTypes)
      };
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
   json_ty const aSets = json_ty::parse(aLoaded.strSetsCatalog);
   for (json_ty const& aSet : aSets.at("data")) {
      StagedSet aRow{
         .strUuid = aSet.at("id").get<std::string>(),
         .strCode = aSet.at("code").get<std::string>(),
         .strName = aSet.at("name").get<std::string>(),
         .optParentCode = aSet.value("parent_set_code", std::optional<std::string>{})
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

   std::println(
      "Parsed {} cards from {} sets.",
      aParsed.vecCards.size(),
      aParsed.mpSets.size()
      );

   return aParsed;
   }

} // namespace deckkernel::test
