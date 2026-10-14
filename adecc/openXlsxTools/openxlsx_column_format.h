// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include <string>

namespace adecc::openxlsx_grid {

// Optional fourth element of a variadic grid caption. The cpp_core grid and
// text backends continue to use only title, width and alignment. XLSX backends
// interpret this Excel-specific presentation description.
struct ExcelColumnFormat {
   std::string number_format;  // e.g. "dd.mm.yyyy" or "#,##0.00 \"EUR\""
};

} // namespace adecc::openxlsx_grid
