// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace adecc::pdfbooking {

// Kept free of PoDoFo types: the parser is independently testable without
// shipping customer files or linking any PDF libraries.
struct TextFragment {
   std::size_t page = 0;
   double x = 0.0;
   double y = 0.0;
   std::string text;
};

struct VisualLine {
   std::size_t page = 0;
   double y = 0.0;
   std::vector<TextFragment> parts;

   [[nodiscard]] std::string text() const;
};

struct Booking {
   std::size_t page = 0;
   std::string voucher;
   std::string description;
   std::string date;  // Preserve the PDF spelling before any domain validation.
   std::int64_t amount_cents = 0; // Exact signed cents, never double.
};

enum class ControlKind { Carry, Total };

struct Control {
   ControlKind kind = ControlKind::Carry;
   std::size_t page = 0;
   std::size_t after_bookings = 0;
   std::int64_t amount_cents = 0;
};

struct ParseResult {
   std::vector<Booking> bookings;
   std::vector<Control> controls;
   std::vector<std::string> warnings; // Locations only: no private raw text.
   std::optional<std::int64_t> expected_cents;
   std::int64_t actual_cents = 0;

   [[nodiscard]] bool reconciled() const noexcept {
      return expected_cents && *expected_cents == actual_cents &&
             !bookings.empty();
   }
};

[[nodiscard]] std::vector<VisualLine> MakeVisualLines(
   std::vector<TextFragment> fragments, double baseline_tolerance = 2.5);

[[nodiscard]] std::optional<std::int64_t> ParseEuroCents(
   std::string_view value);

[[nodiscard]] ParseResult ParseBookings(
   std::vector<VisualLine> const& lines);

[[nodiscard]] std::string FormatEuroCents(std::int64_t cents);
[[nodiscard]] std::string EscapeCsv(std::string_view value);

} // namespace adecc::pdfbooking
