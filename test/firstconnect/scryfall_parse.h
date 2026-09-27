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

namespace deckkernel::test {

struct ParsedBulkData {
   std::map<std::string, TScryfallSet::data_ty> mpSets;
   std::vector<TScryfallCard::data_ty> vecCards;
   };

ParsedBulkData ParseProcess(LoadedBulkData const& aLoaded);

} // namespace deckkernel::test
