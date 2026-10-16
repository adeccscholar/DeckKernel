// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

#include "adecc/openXlsxTools/openxlsx_workbook.h"
#include "adecc/openXlsxTools/unused_workbook_path.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>

#ifdef _WIN32
#include <Windows.h>
#endif
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

void check(bool condition, std::string const& message) {
   if(!condition) throw std::runtime_error(message);
}

// Place the example beside the executable, regardless of the calling directory.
// Both the CTest binary and the installed apps/<configuration> binary therefore
// keep independently inspectable XLSX files.
std::filesystem::path ExecutableDirectory(char const* argv0) {
#ifdef _WIN32
   // Resolve the actual module location even when invoked through PATH or a shortcut.
   std::wstring buffer(32768, L'\0');
   auto const count = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
   if(count == 0 || count >= buffer.size())
      throw std::runtime_error("Cannot determine executable directory");
   buffer.resize(count);
   return std::filesystem::path{buffer}.parent_path();
#else
   return std::filesystem::absolute(argv0).parent_path();
#endif
}

int main(int argc, char** argv) {
   namespace fs = std::filesystem;
   auto const file = adecc::openxlsx_grid::UnusedWorkbookPath(ExecutableDirectory(argc > 0 ? argv[0] : ""), "OpenXlsxGrid_Example");
   auto const date = adecc::date_ty{std::chrono::year{2026}/10/16};

   std::cout << "[OpenXlsxGrid] Example workbook: " << file.string() << '\n';
   std::cout << "[OpenXlsxGrid] Creating Sheet 'Ledger' at C5, five columns.\n";
   // No prior workbook is modified or deleted, even if it is open in Excel.
   std::cout << "[OpenXlsxGrid] Earlier workbooks remain untouched.\n";
   {
      OpenXLSXWorkbook book(file);
      auto model = book.grid("Ledger", GridRegion{5, 3, 0, 0, false});
      check(model.rows() == 0 && model.columns() == 0, "New grid initially empty");
      std::vector<std::tuple<std::string, int, adecc::EAlignmentType, adecc::openxlsx_grid::ExcelColumnFormat>> captions {
         {"Text", 16, adecc::EAlignmentType::left, adecc::openxlsx_grid::ExcelColumnFormat{}},
         {"Amount", 16, adecc::EAlignmentType::right, adecc::openxlsx_grid::ExcelColumnFormat{"#,##0.00"}},
         {"Count", 16, adecc::EAlignmentType::right, adecc::openxlsx_grid::ExcelColumnFormat{}},
         {"Date", 16, adecc::EAlignmentType::center, adecc::openxlsx_grid::ExcelColumnFormat{"dd.mm.yyyy"}},
         {"Empty", 16, adecc::EAlignmentType::left, adecc::openxlsx_grid::ExcelColumnFormat{}}
      };
      model.set_captions(captions);
      adecc::openxlsx_grid::ExcelGridStyle style;
      style.header({
         .background = adecc::openxlsx_grid::RGB{34, 79, 119},
         .foreground = adecc::openxlsx_grid::RGB{255, 255, 255},
         .bold = true
      }).row_pattern({
         {.background = adecc::openxlsx_grid::RGB{255, 255, 255}},
         {.background = adecc::openxlsx_grid::RGB{225, 236, 247},
          .italic = true}
      });
      model.set_style(style);
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
      std::cout << "[PASS] Workbook created with captions in C5:G5 and data in C6:G6.\n";
      std::cout << "[PASS] Typed write, empty optional, positioning and freeze guard.\n";
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
      std::cout << "[PASS] Reopened workbook: captions, values and optional cells.\n";
   }
   {
      OpenXLSXWorkbook book(file);
      check(book.sheet_exists("Ledger"), "Existing worksheet lookup");
      check(book.worksheet_rows("Ledger") == 6, "Last used worksheet row");
      auto ledger = book.grid_existing("Ledger", 5, 3);
      check(ledger.rows() == 1 && ledger.columns() == 5, "Offset C5 discovers one data row and five columns");
      check(ledger.captions().at(1) == "Amount", "Offset caption is correctly read");
      check(ledger.get<double>(0, 1) == 47.5, "Offset amount is correctly read");
      auto amount = book.grid_existing("Ledger", GridRegion{6, 4, 1, 1, false});
      check(amount.get<double>(0, 0) == 47.5, "Explicit off-origin selection is supported");
      // Remove sheet is deliberately tested with a disposable second sheet.
      book.add_sheet("Temporary");
      check(book.sheet_exists("Temporary"), "Create supplementary sheet");
      book.remove_sheet("Temporary");
      check(!book.sheet_exists("Temporary"), "Delete supplementary sheet");
      book.save();
   }
   // Verify that layout metadata has actually survived the XLSX roundtrip,
   // not merely the in-memory typed model.
   {
      OpenXLSX::XLDocument raw;
      raw.open(file.string());
      auto sheet = raw.workbook().worksheet("Ledger");
      check(std::abs(sheet.column(4).width() - 16.0F) < 0.1F, "Caption column width persisted");
      auto& formats = raw.styles().cellFormats();
      auto fmt = formats.cellFormatByIndex(sheet.cell(5, 4).cellFormat());
      check(fmt.applyAlignment() && fmt.alignment().horizontal() == OpenXLSX::XLAlignRight, "Caption column alignment persisted");
      auto date_fmt = formats.cellFormatByIndex(sheet.cell(6, 6).cellFormat());
      auto amount_fmt = formats.cellFormatByIndex(sheet.cell(6, 4).cellFormat());
      check(date_fmt.applyNumberFormat(), "Date format enabled");
      check(amount_fmt.applyNumberFormat(), "Amount format enabled");
      check(date_fmt.numberFormatId() != amount_fmt.numberFormatId(),"Date and amount use different Excel formats");
      auto& numbers = raw.styles().numberFormats();
      check(numbers.numberFormatById(date_fmt.numberFormatId()).formatCode() == "dd.mm.yyyy", "Caption date number format persisted");
      check(numbers.numberFormatById(amount_fmt.numberFormatId()).formatCode() == "#,##0.00", "Caption amount number format persisted");
      auto cf = sheet.conditionalFormats();
      check(cf.count() == 1, "One dynamic row-format region persisted");
      auto ruleRegion = cf.conditionalFormatByIndex(0);
      check(ruleRegion.sqref() == "C6:G6", "Conditional format covers data, not header");
      auto rules = ruleRegion.cfRules();
      check(rules.count() == 2, "Both pattern positions have Excel rules");
      check(rules.cfRuleByIndex(1).formula() == "MOD(ROW()-6,2)=1", "Alternating row formula persisted");
      auto headerStyle = formats.cellFormatByIndex(sheet.cell(5, 3).cellFormat());
      check(headerStyle.applyFont() && headerStyle.applyFill(), "Fixed header font and fill persisted");
      raw.close();
      std::cout << "[PASS] Excel formatting: column width, alignment, date number format.\n";
   }
   check(fs::exists(file) && fs::file_size(file) > 0, "XLSX example file missing or empty");
   std::cout << "[PASS] XLSX roundtrip completed successfully.\n";
   std::cout << "[FILE] " << file.string() << '\n';
   std::cout << "[INFO] Open the file in Excel to inspect Ledger!C5:G6.\n";
}
