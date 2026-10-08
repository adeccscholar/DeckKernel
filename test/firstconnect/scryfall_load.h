// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_load.h
\brief Lesson boundary for resolving and downloading Scryfall bulk data.
\details
The load lesson owns only transport and local-cache concerns. It deliberately knows
nothing about JSON card decomposition or PostgreSQL.
*/

#pragma once

#include <filesystem>
#include <string>

namespace deckkernel::test {

struct BulkDescriptor {
   std::string           strDownloadUri;
   std::string           strUpdatedAt;
   bool                  boGzipJsonLines { false };
   };

struct LoadedBulkData {
   BulkDescriptor        aDescriptor;
   std::filesystem::path aPath;
   bool                  boDownloaded { false };
   };

LoadedBulkData LoadProcess();

} // namespace deckkernel::test
