// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include <OpenXLSX.hpp>
#include <stdexcept>

namespace adecc::openxlsx_grid {

// Shared lifetime: a selected grid keeps the open document alive even if the
// owning workbook facade is destroyed. Persist changes explicitly via save().
struct WorkbookSession final {
   OpenXLSX::XLDocument document;
   bool opened = false;

   ~WorkbookSession() noexcept {
      if(opened) {
         try { document.close(); }
         catch(...) { /* A destructor must not throw. Unsaved changes are not committed. */ }
      }
   }

   void require_open() const {
      if(!opened) throw std::logic_error("OpenXLSX workbook is not open");
   }
};

} // namespace adecc::openxlsx_grid
