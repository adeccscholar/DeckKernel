// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_parse.h
\brief Lesson boundary for parsing and relationally decomposing Scryfall bulk data.
*/

#pragma once

#include "scryfall_load.h"
#include "scryfall_model.h"

#include <map>
#include <string>
#include <vector>
#include <tuple>

namespace deckkernel::test {

struct StagedSet {
   std::string strUuid, strCode, strName;
   std::optional<std::string> optParentCode;
   };
struct StagedFace {
   int iFaceId{};
   std::string strName, strTypeLine, strManaCost;
   std::optional<std::string> optLayout, optLoyalty, optPower;
   std::optional<std::string> optIllustrationId, optImageNormalUrl, optImageArtCropUrl, optImagePngUrl;
   std::optional<std::string> optImageSmallUrl, optImageLargeUrl, optImageBorderCropUrl;
   std::vector<std::string> vecLandTypes, vecCreatureTypes;
   std::vector<std::string> vecSuperTypes, vecMainTypes, vecArtifactTypes;
   };
struct StagedCard {
   std::string strUuid, strName, strSetCode, strRarity;
   std::optional<std::string> optOracleId, optVariationOf, optIllustrationId;
   std::string strCollectorNumber, strLanguage;
   adecc::date_ty aReleasedAt;
   std::vector<StagedFace> vecFaces;
   };
struct ParsedBulkData {
   std::map<std::string, StagedSet> mpSets;
   std::vector<StagedCard> vecCards;
   std::map<std::string, int> mpLandTypes, mpCreatureTypes;
   std::map<std::string, int> mpSuperTypes, mpMainTypes, mpArtifactTypes;
   };

ParsedBulkData ParseProcess(LoadedBulkData const& aLoaded);

} // namespace deckkernel::test
