// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include "openxlsx_grid_backend.h"
#include "../cpp_core/grid_wrapper.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace adecc::openxlsx_grid {

// Workbook/document RAII. A GridModel owns a session lease and therefore
// keeps the document open. save() is an explicit commit, not a destructor side effect.
class OpenXLSXWorkbook final {
public:
   OpenXLSXWorkbook() : session_(std::make_shared<WorkbookSession>()) {}

   explicit OpenXLSXWorkbook(std::filesystem::path const& path)
      : OpenXLSXWorkbook() {
      open_or_create(path);
   }

   OpenXLSXWorkbook(OpenXLSXWorkbook const&) = delete;
   OpenXLSXWorkbook& operator=(OpenXLSXWorkbook const&) = delete;

   void open_or_create(std::filesystem::path const& path) {
      if(std::filesystem::exists(path)) open(path);
      else create(path, false);
   }

   void create(std::filesystem::path const& path, bool overwrite = false) {
      reinitialize_();
      session_->document.create(path.string(), overwrite);
      session_->opened = true;
      session_->clear_style_caches();
   }

   void open(std::filesystem::path const& path) {
      reinitialize_();
      session_->document.open(path.string());
      session_->opened = true;
      session_->clear_style_caches();
   }

   void save() {
      session_->require_open();
      session_->apply_grid_styles();
      session_->document.save();
   }

   void close() {
      if(session_.use_count() > 1)
         throw std::logic_error("Cannot close workbook with live grid views");
      if(session_->opened) {
         session_->document.close();
         session_->opened = false;
      }
   }

   [[nodiscard]] bool sheet_exists(std::string const& sheet_name) const {
      session_->require_open();
      return session_->document.workbook().worksheetExists(sheet_name);
   }

   [[nodiscard]] std::vector<std::string> sheet_names() const {
      session_->require_open();
      return session_->document.workbook().worksheetNames();
   }

   void add_sheet(std::string const& sheet_name) {
      session_->require_open();
      if(sheet_name.empty() || sheet_exists(sheet_name))
         throw std::invalid_argument("Excel worksheet name is empty or already exists");
      session_->document.workbook().addWorksheet(sheet_name);
   }

   // XLWorkbook::deleteSheet refuses to remove the last worksheet.
   // Prevent dangling GridModels by disallowing removal with active views.
   void remove_sheet(std::string const& sheet_name) {
      session_->require_open();
      if(session_.use_count() > 1)
         throw std::logic_error("Cannot delete a sheet while grid views are alive");
      if(!sheet_exists(sheet_name))
         throw std::out_of_range("Excel worksheet does not exist");
      session_->document.workbook().deleteSheet(sheet_name);
      session_->styled_regions.erase(
         std::remove_if(session_->styled_regions.begin(), session_->styled_regions.end(),
            [&](auto const& region) { return region->sheet == sheet_name; }),
         session_->styled_regions.end());
   }

   // The last occupied worksheet row number, not a count of business records.
   [[nodiscard]] std::size_t worksheet_rows(std::string const& sheet_name) const {
      session_->require_open();
      if(!sheet_exists(sheet_name))
         throw std::out_of_range("Excel worksheet does not exist");
      return session_->document.workbook().worksheet(sheet_name).rowCount();
   }

   // Read an existing table with the same 1-based origin as grid().
   // Zero dimensions discover consecutive columns on the first row and
   // consecutive data rows until the first completely empty row.
   [[nodiscard]] adecc::grid::GridModel<OpenXLSXGridBackend>
   grid_existing(std::string name, GridRegion region) {
      session_->require_open();
      if(!sheet_exists(name))
         throw std::out_of_range("Excel worksheet does not exist");
      if(region.first_row == 0 || region.first_row > 1048576 ||
         region.first_column == 0 || region.first_column > 16384)
         throw std::out_of_range("Excel grid origin outside sheet limits");
      auto sheet = session_->document.workbook().worksheet(name);
      auto occupied = [&](std::uint32_t row, std::uint16_t column) {
         auto cell = sheet.findCell(row, column);
         return !cell.empty() &&
                cell.value().type() != OpenXLSX::XLValueType::Empty;
      };
      if(region.columns == 0) {
         for(std::uint32_t col = region.first_column; col <= 16384; ++col) {
            if(!occupied(region.first_row, static_cast<std::uint16_t>(col))) break;
            ++region.columns;
         }
         if(region.columns == 0)
            throw std::logic_error("No Excel table at requested origin");
      }
      if(std::uint64_t(region.first_column) + region.columns > 16385)
         throw std::out_of_range("Excel grid extends beyond column XFD");
      auto const first_data = std::uint64_t(region.first_row) +
                              (region.has_captions ? 1u : 0u);
      if(region.data_rows == 0 && first_data <= 1048576) {
         auto const last = sheet.rowCount();
         for(std::uint64_t row = first_data; row <= last; ++row) {
            bool found = false;
            for(std::size_t col = 0; col < region.columns; ++col) {
               if(occupied(static_cast<std::uint32_t>(row),
                           static_cast<std::uint16_t>(region.first_column + col))) {
                  found = true;
                  break;
               }
            }
            if(!found) break;
            ++region.data_rows;
         }
      }
      return grid(std::move(name), region);
   }

   // Existing source-compatible call, with A1 as its default origin.
   [[nodiscard]] adecc::grid::GridModel<OpenXLSXGridBackend>
   grid_existing(std::string name, bool has_captions = true) {
      return grid_existing(std::move(name), GridRegion{1, 1, 0, 0, has_captions});
   }

   // Offset and optional explicit dimensions, in the same order as grid().
   [[nodiscard]] adecc::grid::GridModel<OpenXLSXGridBackend>
   grid_existing(std::string name, std::uint32_t first_row,
                 std::uint16_t first_column, std::size_t rows = 0,
                 std::size_t columns = 0, bool has_captions = true) {
      return grid_existing(std::move(name),
                           GridRegion{first_row, first_column,
                                      rows, columns, has_captions});
   }

   // The complete selected rectangle must be supplied when reading existing data.
   // On a new grid: {first_row,first_column,0,0,false}; captions then set columns.
   [[nodiscard]] adecc::grid::GridModel<OpenXLSXGridBackend>
   grid(std::string sheet_name, GridRegion region) {
      session_->require_open();
      if(sheet_name.empty()) throw std::invalid_argument("Excel sheet name is empty");
      auto workbook = session_->document.workbook();
      if(!workbook.worksheetExists(sheet_name)) workbook.addWorksheet(sheet_name);
      return adecc::grid::GridModel<OpenXLSXGridBackend>{
         OpenXLSXGridBackend{session_, std::move(sheet_name), region}
      };
   }

   [[nodiscard]] adecc::grid::GridModel<OpenXLSXGridBackend>
   grid(std::string sheet_name = "Sheet1",
        std::uint32_t first_row = 1,
        std::uint16_t first_column = 1,
        std::size_t rows = 0,
        std::size_t columns = 0,
        bool has_captions = false) {
      return grid(std::move(sheet_name), GridRegion{first_row, first_column, rows, columns, has_captions});
   }

private:
   void reinitialize_() {
      if(session_.use_count() > 1)
         throw std::logic_error("Cannot reopen workbook with live grid views");
      close();
   }

   std::shared_ptr<WorkbookSession> session_;
};

} // namespace adecc::openxlsx_grid
