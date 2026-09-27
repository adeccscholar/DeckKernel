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
void AppendCard(
   json_ty const& aCard,
   ParsedBulkData& aParsed
) {
   std::string const strId = aCard.at("id").get<std::string>();
   std::string const strSetId = aCard.at("set_id").get<std::string>();
   std::string const strSetCode = aCard.at("set").get<std::string>();
   std::string const strSetName = aCard.at("set_name").get<std::string>();
   std::string const strName = aCard.at("name").get<std::string>();
   std::string const strReleasedAt = aCard.at("released_at").get<std::string>();

   std::optional<std::string> optOracleId;

   if (auto const it = aCard.find("oracle_id");
       it != aCard.end() && !it->is_null()) {
      optOracleId = it->get<std::string>();
      }

   aParsed.mpSets.try_emplace(
      strSetId,
      TScryfallSet::data_ty{
         strSetId,
         strSetCode,
         strSetName
         }
      );

   aParsed.vecCards.emplace_back(
      strId,
      std::move(optOracleId),
      strName,
      strSetId,
      adecc::ConvertTo<adecc::date_ty>(strReleasedAt)
      );
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
void ReadGzipJsonLines(
   std::filesystem::path const& aPath,
   ParsedBulkData& aParsed
) {
   gzip_handle_ty upFile{ gzopen(aPath.string().c_str(), "rb") };

   if (!upFile) {
      throw std::runtime_error{
         std::format("cannot open gzip bulk-data file '{}'", aPath.string())
         };
      }

   std::array<char, 64U * 1024U> arrBuffer{};
   std::string strPending;

   while (true) {
      int const iRead = gzread(
         upFile.get(),
         arrBuffer.data(),
         static_cast<unsigned int>(arrBuffer.size())
         );

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
