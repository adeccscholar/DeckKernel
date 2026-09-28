// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace deckkernel::docu {

struct ServerConfiguration {
   std::filesystem::path aRepositoryRoot;
   std::filesystem::path aRuntimeDirectory;
   std::string strBindAddress{ "127.0.0.1" };
   std::uint16_t uPort{ 8770U };
   };

class DocuServer final {
public:
   explicit DocuServer(ServerConfiguration aConfiguration);
   void Run();

private:
   ServerConfiguration aConfiguration;
   };

}
