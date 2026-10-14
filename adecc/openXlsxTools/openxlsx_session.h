// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include <OpenXLSX.hpp>
#include <algorithm>
#include <cstdint>
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
