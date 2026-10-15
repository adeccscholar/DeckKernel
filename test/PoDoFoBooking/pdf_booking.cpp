// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#include "pdf_booking.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <functional>
#include <limits>
#include <ranges>
#include <regex>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace adecc::pdfbooking {
namespace {

std::string Trim(std::string_view text) {
   auto const start = text.find_first_not_of(" \t\r\n");
   if(start == std::string_view::npos) return {};
   auto const end = text.find_last_not_of(" \t\r\n");
   return std::string{text.substr(start, end - start + 1)};
}

bool Has(std::string const& text, std::string_view needle) {
   return text.find(needle) != std::string::npos;
}

bool IsHeading(std::string const& text) {
   return Has(text, "Beleg") && Has(text, "Buchungstext") &&
          Has(text, "Datum") && Has(text, "Betrag");
}

bool IsPageLabel(std::string const& text) {
   return text.starts_with("Seite ") || text.starts_with("Page ");
}

std::optional<ControlKind> ControlType(std::string const& text) {
   if(Has(text, "Gesamtsumme:") || Has(text, "Gesamtsumme :"))
      return ControlKind::Total;
   if(Has(text, "Übertrag:") || Has(text, "Übertrag :") ||
      Has(text, "Uebertrag:") || Has(text, "Uebertrag :"))
      return ControlKind::Carry;
   return std::nullopt;
}

std::string Location(VisualLine const& line) {
   return "page " + std::to_string(line.page) +
          ", Y=" + std::to_string(static_cast<int>(std::lround(line.y)));
}

} // namespace

std::string VisualLine::text() const {
   std::string result;
   for(auto const& part : parts) {
      auto clean = Trim(part.text);
      if(clean.empty()) continue;
      for(char& c : clean) {
         if(c == '\r' || c == '\n' || c == '\t') c = ' ';
      }
      if(!result.empty()) result += ' ';
      result += clean;
   }
   return result;
}

std::vector<VisualLine> MakeVisualLines(
   std::vector<TextFragment> fragments, double baseline_tolerance) {
   if(!std::isfinite(baseline_tolerance) || baseline_tolerance < 0.0)
      throw std::invalid_argument("Invalid PDF baseline tolerance");
   std::ranges::sort(fragments, [](auto const& a, auto const& b) {
      if(a.page != b.page) return a.page < b.page;
      if(a.y != b.y) return a.y > b.y; // PDF Y grows upwards.
      return a.x < b.x;
   });

   std::vector<VisualLine> lines;
   for(auto& fragment : fragments) {
      if(Trim(fragment.text).empty()) continue;
      if(lines.empty() || lines.back().page != fragment.page ||
         std::abs(lines.back().y - fragment.y) > baseline_tolerance) {
         lines.push_back(VisualLine{fragment.page, fragment.y, {}});
      }
      lines.back().parts.push_back(std::move(fragment));
   }
   for(auto& line : lines)
      std::ranges::sort(line.parts,
         [](auto const& a, auto const& b) { return a.x < b.x; });
   return lines;
}

std::optional<std::int64_t> ParseEuroCents(std::string_view value) {
   auto text = Trim(value);
   if(text.empty()) return std::nullopt;
   bool negative = false;
   if(text.front() == '-' || text.front() == '+') {
      negative = text.front() == '-';
      text.erase(0, 1);
   }
   if(text.empty()) return std::nullopt;
   auto const decimal = text.find(',');
   if(decimal == std::string::npos ||
      text.find(',', decimal + 1) != std::string::npos)
      return std::nullopt;
   auto const fraction = text.substr(decimal + 1);
   if(fraction.empty() || fraction.size() > 2 ||
      !std::ranges::all_of(fraction, [](unsigned char c) { return std::isdigit(c) != 0; }))
      return std::nullopt;

   auto const whole = text.substr(0, decimal);
   if(whole.empty()) return std::nullopt;
   std::string digits;
   auto const grouped = whole.find('.') != std::string::npos;
   std::size_t group_start = 0;
   while(group_start < whole.size()) {
      auto const separator = whole.find('.', group_start);
      auto const group_length =
         (separator == std::string::npos ? whole.size() : separator) - group_start;
      if(group_length == 0 ||
         (grouped && group_length > 3) ||
         (grouped && group_start != 0 && group_length != 3))
         return std::nullopt;
      for(std::size_t k = group_start; k < group_start + group_length; ++k) {
         if(!std::isdigit(static_cast<unsigned char>(whole[k]))) return std::nullopt;
         digits += whole[k];
      }
      if(separator == std::string::npos) break;
      group_start = separator + 1;
   }
   if(!grouped && digits.size() != whole.size()) return std::nullopt;

   std::int64_t euros = 0;
   for(char digit : digits) {
      auto const number = static_cast<int>(digit - '0');
      if(euros > (std::numeric_limits<std::int64_t>::max() / 100 - number) / 10)
         return std::nullopt;
      euros = euros * 10 + number;
   }
   std::int64_t cents = static_cast<int>(fraction[0] - '0') * 10;
   if(fraction.size() == 2) cents += fraction[1] - '0';
   auto const result = euros * 100 + cents;
   return negative ? -result : result;
}

ParseResult ParseBookings(std::vector<VisualLine> const& lines) {
   // A text line is recognized only if a voucher, date and amount agree with
   // this first-layout hypothesis. Unknown lines are reported, not invented.
   static std::regex const row_pattern(
      R"(^\s*(\S+)\s+(.+?)\s+(\d{1,2}\.\d{1,2}\.\d{2,4})\s+([+-]?(?:\d{1,3}(?:\.\d{3})*|\d+),\d{1,2})\s*(?:EUR)?\s*$)");
   static std::regex const amount_pattern(
      R"(([+-]?(?:\d{1,3}(?:\.\d{3})*|\d+),\d{1,2})\s*(?:EUR)?\s*$)");

   ParseResult result;
   std::optional<double> voucher_x;
   for(auto const& line : lines) {
      auto const text = line.text();
      if(text.empty() || IsHeading(text) || IsPageLabel(text)) continue;

      if(auto kind = ControlType(text)) {
         std::smatch match;
         if(!std::regex_search(text, match, amount_pattern)) {
            result.warnings.push_back("Control amount not readable at " + Location(line));
            continue;
         }
         auto const amount = ParseEuroCents(match[1].str());
         if(!amount) {
            result.warnings.push_back("Control amount invalid at " + Location(line));
            continue;
         }
         result.controls.push_back(
            Control{*kind, line.page, result.bookings.size(), *amount});
         if(*kind == ControlKind::Total) {
            if(result.expected_cents)
               result.warnings.push_back("Multiple grand totals found at " + Location(line));
            result.expected_cents = *amount;
         }
         continue;
      }

      std::smatch match;
      if(std::regex_match(text, match, row_pattern)) {
         auto amount = ParseEuroCents(match[4].str());
         if(!amount) {
            result.warnings.push_back("Booking amount invalid at " + Location(line));
            continue;
         }
         result.bookings.push_back(Booking{
            line.page, match[1].str(), Trim(match[2].str()), match[3].str(), *amount
         });
         voucher_x = line.parts.front().x;
         continue;
      }

      // Continuation lines often have no voucher/date/amount. Require a
      // visible indent from the voucher position; do not silently consume
      // arbitrary footnotes, page headers, or other unrecognized content.
      bool const indented = voucher_x && !line.parts.empty() &&
                            line.parts.front().x > *voucher_x + 8.0;
      if(!result.bookings.empty() && indented && !result.expected_cents) {
         result.bookings.back().description += " " + text;
      }
      else {
         result.warnings.push_back("Unclassified text at " + Location(line) +
                                   " (use --inspect locally)");
      }
   }

   auto values = result.bookings | std::views::transform(
      [](Booking const& booking) { return booking.amount_cents; });
   result.actual_cents = std::ranges::fold_left(
      values, std::int64_t{0}, std::plus<>{});

   // A carry is a checkpoint only, not another booking. It can occur at
   // either end of a page. Do not presume it denotes a new charge.
   for(auto const& control : result.controls) {
      if(control.kind != ControlKind::Carry) continue;
      auto const prefix = result.bookings |
         std::views::take(control.after_bookings) |
         std::views::transform([](Booking const& b) { return b.amount_cents; });
      auto const subtotal = std::ranges::fold_left(
         prefix, std::int64_t{0}, std::plus<>{});
      if(subtotal != control.amount_cents)
         result.warnings.push_back(
            "Carry differs from recognized running sum on page " +
            std::to_string(control.page));
   }
   return result;
}

std::string FormatEuroCents(std::int64_t cents) {
   auto const negative = cents < 0;
   // Works for INT64_MIN without taking its absolute value as signed.
   std::uint64_t magnitude = negative
      ? static_cast<std::uint64_t>(-(cents + 1)) + 1
      : static_cast<std::uint64_t>(cents);
   return std::string(negative ? "-" : "") +
          std::to_string(magnitude / 100) + "," +
          (magnitude % 100 < 10 ? "0" : "") +
          std::to_string(magnitude % 100);
}

std::string EscapeCsv(std::string_view text) {
   // Mitigate spreadsheet formula injection in voucher/description fields.
   std::string guarded;
   if(!text.empty() && (text.front() == '=' || text.front() == '+' ||
      text.front() == '-' || text.front() == '@' || text.front() == '\t'))
      guarded += '\'';
   guarded += text;

   std::string quoted = "\"";
   for(char c : guarded) {
      if(c == '"') quoted += '"';
      if(c == '\r' || c == '\n') c = ' ';
      quoted += c;
   }
   quoted += '"';
   return quoted;
}

} // namespace adecc::pdfbooking
