// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include "openxlsx_session.h"
#include "openxlsx_cell_conversion.h"
#include "openxlsx_column_format.h"
#include "openxlsx_grid_style.h"
#include "../cpp_core/grid_backend_concepts.h"

#include <OpenXLSX.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace adecc::openxlsx_grid {

// Selection of an entire rectangular data region, not a worksheet-wide guess.
// Coordinates are 1-based as in Excel; dimensions are data rows / data columns.
// A caption row, if present, precedes the data within this rectangle.
struct GridRegion final {
   std::uint32_t first_row = 1;
   std::uint16_t first_column = 1;
   std::size_t data_rows = 0;
   std::size_t columns = 0;
   bool has_captions = false;
};

class OpenXLSXGridBackend {
public:
   using size_type = std::size_t;

   explicit OpenXLSXGridBackend(std::shared_ptr<WorkbookSession> session,
                               std::string sheet_name, GridRegion region)
      : session_(std::move(session)), sheet_name_(std::move(sheet_name)),
        region_(region), rows_(region.data_rows), columns_(region.columns),
        captions_(region.has_captions), alignments_(region.columns),
        number_formats_(region.columns), prepared_styles_(region.columns) {
      if(!session_) throw std::invalid_argument("OpenXLSX document is null");
      session_->require_open();
      if(sheet_name_.empty()) throw std::invalid_argument("Excel sheet name is empty");
      validate_(rows_, columns_, captions_);
      (void)sheet_();
      if(captions_) load_caption_alignments_();
   }

   [[nodiscard]] size_type rows() const noexcept { return rows_; }
   [[nodiscard]] size_type columns() const noexcept { return columns_; }
   [[nodiscard]] size_type current_row() const noexcept { return current_row_; }
   [[nodiscard]] size_type current_column() const noexcept { return current_column_; }

   // This is the grid's batch/update guard, NOT Excel's worksheet freeze panes.
   // There is no rendering to suspend in an XLSX document; preserve guard state.
   [[nodiscard]] bool freeze() noexcept {
      auto old = frozen_;
      frozen_ = true;
      return old;
   }
   void unfreeze(bool old) noexcept { frozen_ = old; }

   [[nodiscard]] bool focus_row(size_type row) {
      if(row >= rows_) return false;
      current_row_ = row;
      current_column_ = 0;
      sheet_().setSelected(true);  // Activates worksheet tab, not Excel active-cell XML.
      return true;
   }

   [[nodiscard]] bool focus_cell(size_type row, size_type column) {
      if(row >= rows_ || column >= columns_) return false;
      current_row_ = row;
      current_column_ = column;
      sheet_().setSelected(true);
      return true;
   }

   // Clears ONLY the selected rectangle. clear/reset(false) preserves captions.
   void reset(bool full = true) {
      auto sheet = sheet_();
      for(size_type row = 0; row < rows_; ++row)
         for(size_type col = 0; col < columns_; ++col)
            sheet.cell(excel_row_(row), excel_column_(col)).value().clear();

      rows_ = 0;
      current_row_ = current_column_ = 0;
      if(full) {
         if(captions_) {
            for(size_type col = 0; col < columns_; ++col)
               sheet.cell(region_.first_row, excel_column_(col)).value().clear();
         }
         columns_ = 0;
         captions_ = false;
         alignments_.clear();
         number_formats_.clear();
         prepared_styles_.clear();
         if(style_region_) {
            style_region_->rows = 0;
            style_region_->columns = 0;
            style_region_->captions = false;
            style_region_->header_applied = false;
         }
      }
   }

   template<class... Hdr>
   void set_caption(std::vector<std::tuple<Hdr...>> const& values, bool clear = true) {
      static_assert(sizeof...(Hdr) >= 1, "Caption tuple must contain a title");
      // A header cannot be inserted over an existing non-header data region.
      if(!clear && !captions_ && rows_ != 0)
         throw std::logic_error("Cannot add captions over existing data rows");
      validate_(clear ? 0 : rows_, values.size(), true);
      if(clear) reset(true);

      captions_ = true;
      columns_ = values.size();
      alignments_.assign(columns_, std::nullopt);
      number_formats_.assign(columns_, std::nullopt);
      prepared_styles_.assign(columns_, std::nullopt);
      sync_style_region_();

      auto sheet = sheet_();
      for(size_type col = 0; col < values.size(); ++col) {
         auto cell = sheet.cell(region_.first_row, excel_column_(col));
         detail::ToCell(cell, std::get<0>(values[col]));
         if constexpr (sizeof...(Hdr) >= 2) {
            using Width = std::remove_cvref_t<decltype(std::get<1>(values[col]))>;
            if constexpr (std::is_arithmetic_v<Width>) {
               auto width = static_cast<float>(std::get<1>(values[col]));
               if(width <= 0) throw std::invalid_argument("Caption column width must be positive");
               sheet.column(excel_column_(col)).setWidth(width);
            }
         }
         if constexpr (sizeof...(Hdr) >= 3) {
            using Alignment = std::remove_cvref_t<decltype(std::get<2>(values[col]))>;
            if constexpr (std::same_as<Alignment, adecc::EAlignmentType>) {
               switch(std::get<2>(values[col])) {
                  case adecc::EAlignmentType::left:   alignments_[col] = OpenXLSX::XLAlignLeft; break;
                  case adecc::EAlignmentType::right:  alignments_[col] = OpenXLSX::XLAlignRight; break;
                  case adecc::EAlignmentType::center: alignments_[col] = OpenXLSX::XLAlignCenter; break;
                  default: break;
               }
            }
         }
         if constexpr (sizeof...(Hdr) >= 4) {
            using Extra = std::remove_cvref_t<decltype(std::get<3>(values[col]))>;
            if constexpr (std::same_as<Extra, ExcelColumnFormat>) {
               auto const& format = std::get<3>(values[col]).number_format;
               if(!format.empty())
                  number_formats_[col] = session_->number_format(format);
            }
         }

         // The header itself is a string, so only its alignment is applied.
         if(alignments_[col])
            cell.setCellFormat(session_->cell_style(
               cell.cellFormat(), std::nullopt, alignments_[col]));

         // Precompute the column style once. Data cells with the standard base
         // format reuse this ID directly, independently of their row count.
         if(number_formats_[col] || alignments_[col])
            prepared_styles_[col] = session_->cell_style(
               0, number_formats_[col], alignments_[col]);
      }
      // Also apply the column alignments to the previously selected data rows.
      for(size_type row = 0; row < rows_; ++row) {
         for(size_type col = 0; col < columns_; ++col) {
            if(!alignments_[col] && !number_formats_[col]) continue;
            auto cell = sheet.cell(excel_row_(row), excel_column_(col));
            style_cell_(cell, col);
         }
      }
   }

   // Optional Excel-only customization. May be called before or after
   // appending data; the final rectangular rule is installed at save().
   void set_style(ExcelGridStyle style) {
      if(!style_region_) {
         style_region_ = std::make_shared<WorkbookSession::StyledRegion>();
         style_region_->sheet = sheet_name_;
         style_region_->first_row = region_.first_row;
         style_region_->first_column = region_.first_column;
         session_->styled_regions.push_back(style_region_);
      }
      else if(style_region_->conditional_format_index) {
         throw std::logic_error("Cannot replace saved Excel grid style");
      }
      style_region_->style = std::move(style);
      style_region_->header_applied = false;
      sync_style_region_();
   }

   [[nodiscard]] std::vector<std::string> read_captions() const {
      if(!captions_) throw std::logic_error("Selected region has no caption row");
      std::vector<std::string> result;
      result.reserve(columns_);
      auto sheet = sheet_();
      for(size_type col = 0; col < columns_; ++col) {
         auto cell = sheet.findCell(region_.first_row, excel_column_(col));
         result.push_back(cell.empty() ? std::string{} : cell.value().getString());
      }
      return result;
   }

   size_type insert_row() {
      validate_(rows_ + 1, columns_, captions_);
      current_row_ = rows_++;
      current_column_ = 0;
      sync_style_region_();
      return current_row_;
   }

   template<class Ty>
   [[nodiscard]] Ty get(size_type row, size_type col) const {
      require_cell_(row, col);
      auto cell = sheet_().findCell(excel_row_(row), excel_column_(col));
      if constexpr (adecc::is_optional_v<Ty>) {
         using Value = typename Ty::value_type;
         if(cell.empty() || cell.value().type() == OpenXLSX::XLValueType::Empty)
            return std::nullopt;
         OpenXLSX::XLCellValue value = static_cast<OpenXLSX::XLCellValue>(cell.value());
         return detail::FromCell<Value>(std::move(value));
      }
      else {
         auto result = get<std::optional<Ty>>(row, col);
         if(!result) throw std::runtime_error("Excel grid cell is empty");
         return std::move(*result);
      }
   }

   template<class Ty>
   void set(size_type row, size_type col, Ty const& value) {
      require_cell_(row, col);
      auto cell = sheet_().cell(excel_row_(row), excel_column_(col));
      if constexpr (adecc::is_optional_v<Ty>) {
         if(value) {
            detail::ToCell(cell, *value);
            style_cell_<typename Ty::value_type>(cell, col);
         }
         else cell.value().clear();
      }
      else {
         detail::ToCell(cell, value);
         style_cell_<Ty>(cell, col);
      }
      current_row_ = row;
      current_column_ = col;
   }

private:
   void sync_style_region_() {
      if(!style_region_) return;
      style_region_->rows = rows_;
      style_region_->columns = columns_;
      style_region_->captions = captions_;
   }

   void load_caption_alignments_() {
      auto sheet = sheet_();
      auto& formats = session_->document.styles().cellFormats();
      for(size_type col = 0; col < columns_; ++col) {
         auto cell = sheet.findCell(region_.first_row, excel_column_(col));
         if(cell.empty()) continue;
         auto fmt = formats.cellFormatByIndex(cell.cellFormat());
         if(fmt.applyAlignment()) {
            auto const alignment = fmt.alignment().horizontal();
            if(alignment == OpenXLSX::XLAlignLeft || alignment == OpenXLSX::XLAlignCenter ||
               alignment == OpenXLSX::XLAlignRight)
               alignments_[col] = alignment;
         }
      }
   }

   [[nodiscard]] OpenXLSX::XLWorksheet sheet_() const {
      session_->require_open();
      return session_->document.workbook().worksheet(sheet_name_);
   }

   void validate_(size_type rows, size_type cols, bool with_caption) const {
      if(region_.first_row == 0 || region_.first_column == 0 ||
         region_.first_row > 1048576 || region_.first_column > 16384)
         throw std::out_of_range("Excel origin outside sheet limits");
      if(rows != 0 && cols == 0)
         throw std::invalid_argument("Grid containing data rows must have columns");
      if(std::uint64_t(region_.first_column) + cols > 16385ULL ||
         std::uint64_t(region_.first_row) + rows + (with_caption ? 1 : 0) > 1048577ULL)
         throw std::out_of_range("Selected rectangle exceeds Excel limits");
   }

   [[nodiscard]] std::uint32_t excel_row_(size_type row) const {
      return static_cast<std::uint32_t>(std::uint64_t(region_.first_row) + (captions_ ? 1 : 0) + row);
   }
   [[nodiscard]] std::uint16_t excel_column_(size_type col) const {
      return static_cast<std::uint16_t>(std::uint64_t(region_.first_column) + col);
   }
   void require_cell_(size_type row, size_type col) const {
      if(row >= rows_ || col >= columns_)
         throw std::out_of_range("Cell outside selected Excel grid region");
   }

   template<class Ty = void>
   void style_cell_(OpenXLSX::XLCellAssignable& cell, size_type col) {
      // The explicit fourth caption value has priority over automatic
      // C++ type formatting. A plain floating point value uses General (0),
      // never a date format inherited from another column.
      std::optional<std::uint32_t> number_format = number_formats_[col];
      if(!number_format) {
         using Value = std::remove_cvref_t<Ty>;
         if constexpr (std::same_as<Value, adecc::date_ty>) number_format = 14;
         else if constexpr (std::same_as<Value, adecc::time_ty>) number_format = 21;
         else if constexpr (std::same_as<Value, adecc::timestamp_ty>) number_format = 22;
         else if constexpr (std::same_as<Value, adecc::money_ty>) number_format = 2;
         else if constexpr (std::is_integral_v<Value> || std::is_floating_point_v<Value>)
            number_format = 0;
      }

      if(!number_format && !alignments_[col]) return;
      auto const base = cell.cellFormat();
      if(base == 0 && number_format == number_formats_[col] &&
         prepared_styles_[col]) {
         cell.setCellFormat(*prepared_styles_[col]);
      }
      else {
         cell.setCellFormat(session_->cell_style(
            base, number_format, alignments_[col]));
      }
   }

   std::shared_ptr<WorkbookSession> session_;
   std::shared_ptr<WorkbookSession::StyledRegion> style_region_;
   std::string sheet_name_;
   GridRegion region_;
   size_type rows_ = 0;
   size_type columns_ = 0;
   size_type current_row_ = 0;
   size_type current_column_ = 0;
   bool captions_ = false;
   bool frozen_ = false;
   std::vector<std::optional<OpenXLSX::XLAlignmentStyle>> alignments_;
   std::vector<std::optional<std::uint32_t>> number_formats_;
   std::vector<std::optional<OpenXLSX::XLStyleIndex>> prepared_styles_;
};

static_assert(adecc::grid::grid_concept<OpenXLSXGridBackend>,
              "OpenXLSX backend does not satisfy grid_concept");

} // namespace adecc::openxlsx_grid
