// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>
#include <system_error>

namespace adecc::openxlsx_grid {

// Preserve earlier XLSX files, including those still open in Excel.
[[nodiscard]] inline std::filesystem::path UnusedWorkbookPath(
   std::filesystem::path const& directory, std::string const& stem) {
   namespace fs = std::filesystem;
   for(unsigned int n = 0; n < 10000; ++n) {
      std::string const name = stem + (n ? "_" + std::to_string(n) : "") + ".xlsx";
      fs::path const candidate = directory / name;
      std::error_code ec;
      bool const exists = fs::exists(candidate, ec);
      if(ec)
         throw fs::filesystem_error("Cannot inspect Excel output", candidate, ec);
      if(!exists) return candidate;
   }
   throw std::runtime_error("No unused Excel filename available (0-9999)");
}

} // namespace adecc::openxlsx_grid
