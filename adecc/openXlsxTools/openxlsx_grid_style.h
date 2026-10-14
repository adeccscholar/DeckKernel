// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include <compare>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace adecc::openxlsx_grid {

// All properties are optional: an unspecified property leaves existing
// font/fill settings untouched. These are appearance rules, not cell values.
struct RGB {
   std::uint8_t red = 0;
   std::uint8_t green = 0;
   std::uint8_t blue = 0;
   auto operator<=>(RGB const&) const = default;
};

struct ExcelCellStyle {
   std::optional<RGB> background;
   std::optional<RGB> foreground;
   std::optional<bool> bold;
   std::optional<bool> italic;

   [[nodiscard]] bool empty() const noexcept {
      return !background && !foreground && !bold && !italic;
   }
   auto operator<=>(ExcelCellStyle const&) const = default;
};

// A portable value object. No OpenXLSX handles, row data, or backend calls.
// Header is fixed; row_pattern is evaluated dynamically by Excel using
// conditional formatting. A one-element pattern formats all rows alike.
class ExcelGridStyle {
public:
   ExcelGridStyle& header(ExcelCellStyle value) {
      header_ = std::move(value);
      return *this;
   }

   ExcelGridStyle& row_pattern(std::vector<ExcelCellStyle> values) {
      row_pattern_ = std::move(values);
      return *this;
   }

   [[nodiscard]] ExcelCellStyle const& header_style() const noexcept {
      return header_;
   }

   [[nodiscard]] std::vector<ExcelCellStyle> const& row_styles() const noexcept {
      return row_pattern_;
   }

private:
   ExcelCellStyle header_;
   std::vector<ExcelCellStyle> row_pattern_;
};

} // namespace adecc::openxlsx_grid
