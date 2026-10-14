// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

#include "adecc/openXlsxTools/openxlsx_workbook.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using adecc::openxlsx_grid::OpenXLSXWorkbook;
using adecc::openxlsx_grid::OpenXLSXGridBackend;
using adecc::openxlsx_grid::GridRegion;

static_assert(adecc::grid::grid_concept<OpenXLSXGridBackend>);
static_assert(adecc::grid::table_type<OpenXLSXGridBackend>);

void check(bool condition, char const* message) {
   if(!condition) throw std::runtime_error(message);
}

int main() {
   namespace fs = std::filesystem;
   auto const file = fs::current_path() / "openxlsx-grid-roundtrip.xlsx";
   auto const date = adecc::date_ty{std::chrono::year{2026}/10/14};

   fs::remove(file); // Ensure open_or_create really creates the workbook.
   {
      OpenXLSXWorkbook book(file);
      auto model = book.grid("Ledger", GridRegion{5, 3, 0, 0, false});
      check(model.rows() == 0 && model.columns() == 0, "New grid initially empty");
      std::vector<std::tuple<std::string, int, adecc::EAlignmentType>> captions{
         {"Text", 16, adecc::EAlignmentType::left},
         {"Amount", 16, adecc::EAlignmentType::right},
         {"Count", 16, adecc::EAlignmentType::right},
         {"Date", 16, adecc::EAlignmentType::center},
         {"Empty", 16, adecc::EAlignmentType::left}
      };
      model.set_captions(captions);
      check(model.rows() == 0 && model.columns() == 5, "Captions define only width");
      check(model.captions().at(0) == "Text", "Caption readback");
      model.appendRow(std::make_tuple(std::string("Hello"), 42.125, 12, date,
                                     std::optional<std::string>{}));
      check(model.rows() == 1 && model.columns() == 5, "Appended grid dimensions");
      check(model.get<std::string>(0, 0) == "Hello", "String conversion");
      check(std::abs(model.get<double>(0, 1) - 42.125) < 1e-10, "Floating conversion");
      check(model.get<int>(0, 2) == 12, "Integral conversion");
      check(model.get<adecc::date_ty>(0, 3) == date, "Date conversion");
      check(!model.get<std::optional<std::string>>(0, 4), "Empty optional");
      check(model.focus_cell(0, 3), "Logical cell positioning");
      check(model.current_row() == 0 && model.current_column() == 3, "Grid position");
      {
         auto guard = model.freeze_guard();
         model.set<double>(0, 1, 47.5);
      }
      check(model.get<double>(0, 1) == 47.5, "Guarded cell update");
      book.save();
   }
   {
      OpenXLSXWorkbook book(file); // This time open existing without overwriting.
      auto model = book.grid("Ledger", GridRegion{5, 3, 1, 5, true});
      check(model.rows() == 1 && model.columns() == 5, "Explicit existing dimensions");
      check(model.captions().at(1) == "Amount", "Caption roundtrip");
      check(model.get<std::string>(0, 0) == "Hello", "String roundtrip");
      check(model.get<double>(0, 1) == 47.5, "Numeric roundtrip");
      check(model.get<int>(0, 2) == 12, "Integer roundtrip");
      check(model.get<adecc::date_ty>(0, 3) == date, "Date roundtrip");
      check(!model.get<std::optional<int>>(0, 4), "Optional roundtrip");
      check(!model.focus_row(1), "Reject position outside selected rectangle");
      book.save();
   }
   // Verify that layout metadata has actually survived the XLSX roundtrip,
   // not merely the in-memory typed model.
   {
      OpenXLSX::XLDocument raw;
      raw.open(file.string());
      auto sheet = raw.workbook().worksheet("Ledger");
      check(std::abs(sheet.column(4).width() - 16.0F) < 0.1F,
            "Caption column width persisted");
      auto& formats = raw.styles().cellFormats();
      auto fmt = formats.cellFormatByIndex(sheet.cell(5, 4).cellFormat());
      check(fmt.applyAlignment() &&
            fmt.alignment().horizontal() == OpenXLSX::XLAlignRight,
            "Caption column alignment persisted");
      auto date_fmt = formats.cellFormatByIndex(sheet.cell(6, 6).cellFormat());
      check(date_fmt.numberFormatId() == 14, "Date number format persisted");
      raw.close();
   }
   fs::remove(file);
}
