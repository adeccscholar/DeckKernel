// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include "openxlsx_grid_backend.h"
#include "../cpp_core/grid_wrapper.h"

#include <filesystem>
#include <memory>
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
   }

   void open(std::filesystem::path const& path) {
      reinitialize_();
      session_->document.open(path.string());
      session_->opened = true;
   }

   void save() {
      session_->require_open();
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
