// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include "openxlsx_grid_style.h"

#include <OpenXLSX.hpp>
#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace adecc::openxlsx_grid {

// Shared lifetime: a selected grid keeps the open document alive even if the
// owning workbook facade is destroyed. Persist changes explicitly via save().
struct WorkbookSession final {
   OpenXLSX::XLDocument document;
   bool opened = false;

   // Backends register one region. Shared state tracks growing row counts even
   // after the GridModel goes out of scope. save() installs the Excel rules.
   struct StyledRegion {
      std::string sheet;
      std::uint32_t first_row = 1;
      std::uint16_t first_column = 1;
      std::size_t rows = 0;
      std::size_t columns = 0;
      bool captions = false;
      ExcelGridStyle style;
      bool header_applied = false;
      std::optional<std::size_t> conditional_format_index;
   };
   std::vector<std::shared_ptr<StyledRegion>> styled_regions;

   using AppearanceKey = std::pair<OpenXLSX::XLStyleIndex, ExcelCellStyle>;
   std::map<AppearanceKey, OpenXLSX::XLStyleIndex> appearance_styles;

   [[nodiscard]] static std::string excel_column(std::uint32_t column) {
      std::string name;
      while(column > 0) {
         auto const offset = (column - 1) % 26;
         name.insert(name.begin(), static_cast<char>('A' + offset));
         column = (column - 1) / 26;
      }
      return name;
   }

   [[nodiscard]] static OpenXLSX::XLColor xl_color(RGB const& rgb) {
      return OpenXLSX::XLColor{rgb.red, rgb.green, rgb.blue};
   }

   // Copy-on-write for fixed header styles. Never mutate shared cellXfs,
   // fonts or fills that might be used by other workbook cells.
   [[nodiscard]] OpenXLSX::XLStyleIndex appearance_style(
      OpenXLSX::XLStyleIndex base, ExcelCellStyle const& style) {
      if(style.empty()) return base;
      AppearanceKey const key{base, style};
      if(auto it = appearance_styles.find(key); it != appearance_styles.end())
         return it->second;

      auto& all = document.styles();
      auto& formats = all.cellFormats();
      auto const index = formats.create(formats.cellFormatByIndex(base));
      auto format = formats.cellFormatByIndex(index);
      if(style.bold || style.italic || style.foreground) {
         auto& fonts = all.fonts();
         auto const fontIndex = fonts.create(fonts.fontByIndex(format.fontIndex()));
         auto font = fonts.fontByIndex(fontIndex);
         if(style.bold) font.setBold(*style.bold);
         if(style.italic) font.setItalic(*style.italic);
         if(style.foreground) font.setFontColor(xl_color(*style.foreground));
         format.setFontIndex(fontIndex);
         format.setApplyFont(true);
      }
      if(style.background) {
         auto& fills = all.fills();
         auto const fillIndex = fills.create(fills.fillByIndex(format.fillIndex()));
         auto fill = fills.fillByIndex(fillIndex);
         fill.setFillType(OpenXLSX::XLPatternFill, true);
         fill.setPatternType(OpenXLSX::XLPatternSolid);
         fill.setColor(xl_color(*style.background));
         format.setFillIndex(fillIndex);
         format.setApplyFill(true);
      }
      appearance_styles.emplace(key, index);
      return index;
   }

   [[nodiscard]] OpenXLSX::XLStyleIndex differential_style(
      ExcelCellStyle const& style) {
      auto& formats = document.styles().diffCellFormats();
      auto const index = formats.create();
      auto format = formats.diffCellFormatByIndex(index);
      if(style.bold || style.italic || style.foreground) {
         auto font = format.font();
         if(style.bold) font.setBold(*style.bold);
         if(style.italic) font.setItalic(*style.italic);
         if(style.foreground) font.setFontColor(xl_color(*style.foreground));
      }
      if(style.background) {
         auto fill = format.fill();
         // Differential (conditional) fills use bgColor. In contrast, the
         // regular cell styles above use a solid fgColor for fixed headers.
         fill.setFillType(OpenXLSX::XLPatternFill, true);
         fill.setBackgroundColor(xl_color(*style.background));
      }
      return index;
   }

   void apply_grid_styles() {
      require_open();
      for(auto const& item : styled_regions) {
         auto& region = *item;
         if(region.columns == 0) continue;
         auto sheet = document.workbook().worksheet(region.sheet);
         if(region.captions && !region.header_applied &&
            !region.style.header_style().empty()) {
            for(std::size_t col = 0; col < region.columns; ++col) {
               auto cell = sheet.cell(region.first_row,
                  static_cast<std::uint16_t>(region.first_column + col));
               cell.setCellFormat(appearance_style(
                  cell.cellFormat(), region.style.header_style()));
            }
            region.header_applied = true;
         }

         auto const& pattern = region.style.row_styles();
         if(pattern.empty() || region.rows == 0) continue;
         auto const data_first = std::uint32_t(region.first_row) +
                                 (region.captions ? 1u : 0u);
         auto const data_last = data_first + static_cast<std::uint32_t>(region.rows) - 1u;
         auto const last_col = std::uint32_t(region.first_column) +
                               static_cast<std::uint32_t>(region.columns) - 1u;
         std::string const address =
            excel_column(region.first_column) + std::to_string(data_first) +
            ":" + excel_column(last_col) + std::to_string(data_last);

         auto conditional = sheet.conditionalFormats();
         if(region.conditional_format_index) {
            conditional.conditionalFormatByIndex(*region.conditional_format_index)
               .setSqref(address); // Extend existing rule when new rows were appended.
            continue;
         }

         // A conditional-format range is anchored to the data only, never the
         // header row; ROW() is reevaluated by Excel when rows are sorted.
         auto const index = conditional.create();
         auto cf = conditional.conditionalFormatByIndex(index);
         cf.setSqref(address);
         auto rules = cf.cfRules();
         for(std::size_t slot = 0; slot < pattern.size(); ++slot) {
            if(pattern[slot].empty()) continue;
            auto const dxf = differential_style(pattern[slot]);
            auto const ruleIndex = rules.create();
            auto rule = rules.cfRuleByIndex(ruleIndex);
            rule.setType(OpenXLSX::XLCfType::Expression);
            rule.setDxfId(dxf);
            rule.setFormula(
               "MOD(ROW()-" + std::to_string(data_first) + "," +
               std::to_string(pattern.size()) + ")=" + std::to_string(slot));
         }
         region.conditional_format_index = index;
      }
   }


   using number_format_id = std::uint32_t;
   using alignment_type = OpenXLSX::XLAlignmentStyle;
   using cell_style_id = OpenXLSX::XLStyleIndex;
   using StyleKey = std::tuple<cell_style_id, std::optional<number_format_id>,
                               std::optional<alignment_type>>;

   // Each workbook owns its numFmt IDs and cellXfs indices. A grid must not
   // keep these caches across opening a different document.
   std::map<std::string, number_format_id> number_formats;
   std::map<StyleKey, cell_style_id> styles;

   void clear_style_caches() {
      number_formats.clear();
      styles.clear();
      appearance_styles.clear();
      styled_regions.clear();
   }

   [[nodiscard]] number_format_id number_format(std::string const& code) {
      require_open();
      if(code.empty()) throw std::invalid_argument("Empty Excel number format");
      if(auto it = number_formats.find(code); it != number_formats.end())
         return it->second;

      auto& formats = document.styles().numberFormats();
      number_format_id max_id = 163; // OOXML custom format IDs start at 164.
      for(std::size_t i = 0; i < formats.count(); ++i) {
         auto existing = formats.numberFormatByIndex(i);
         auto const id = existing.numberFormatId();
         if(existing.formatCode() == code) {
            number_formats.emplace(code, id);
            return id;
         }
         max_id = std::max(max_id, id);
      }
      if(max_id == std::numeric_limits<number_format_id>::max())
         throw std::overflow_error("Excel number format ID space exhausted");

      auto const index = formats.create();
      auto format = formats.numberFormatByIndex(index);
      auto const id = max_id + 1;
      format.setNumberFormatId(id);
      format.setFormatCode(code);
      number_formats.emplace(code, id);
      return id;
   }

   [[nodiscard]] cell_style_id cell_style(
      cell_style_id base,
      std::optional<number_format_id> number_format_id,
      std::optional<alignment_type> alignment) {
      require_open();
      if(!number_format_id && !alignment) return base;

      auto& formats = document.styles().cellFormats();
      auto original = formats.cellFormatByIndex(base);
      // Returning an existing style prevents repeated edits from creating
      // another copy of the same style (without modifying its shared XML).
      bool const same_number =
         !number_format_id ||
         (original.applyNumberFormat() &&
          original.numberFormatId() == *number_format_id);
      bool const same_alignment =
         !alignment ||
         (original.applyAlignment() &&
          original.alignment().horizontal() == *alignment);
      if(same_number && same_alignment) return base;

      StyleKey const key{base, number_format_id, alignment};
      if(auto it = styles.find(key); it != styles.end()) return it->second;

      // cellFormatByIndex returns a wrapper referring to the XML style node.
      // Create the clone FIRST, then modify that newly created node only.
      auto const id = formats.create(original);
      auto format = formats.cellFormatByIndex(id);
      if(number_format_id) {
         format.setNumberFormatId(*number_format_id);
         format.setApplyNumberFormat(true);
      }
      if(alignment) {
         format.alignment(OpenXLSX::XLCreateIfMissing).setHorizontal(*alignment);
         format.setApplyAlignment(true);
      }
      styles.emplace(key, id);
      return id;
   }

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
