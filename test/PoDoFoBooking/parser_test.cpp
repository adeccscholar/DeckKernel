// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#include "pdf_booking.h"

#include <cassert>
#include <iostream>
#include <vector>

using namespace adecc::pdfbooking;

int main() {
   assert(ParseEuroCents("1.491,0") == 149100);
   assert(ParseEuroCents("-1.179,00") == -117900);
   assert(ParseEuroCents("0,09") == 9);
   assert(!ParseEuroCents("1,491.00"));
   assert(!ParseEuroCents("1.2,00"));
   assert(!ParseEuroCents("not money"));

   // Synthetic data only. Never commit real customer PDF material.
   std::vector<TextFragment> text{
      {1, 30, 750, "Beleg"},
      {1, 110, 750, "Buchungstext"},
      {1, 410, 750, "Datum"},
      {1, 520, 750, "Betrag in EU"},
      {1, 30, 700, "1001"},
      {1, 110, 700, "Büromaterial"},
      {1, 410, 700, "01.10.2026"},
      {1, 520, 700, "1.000,00"},
      {1, 110, 688, "und Zubehör"},
      {1, 110, 150, "Übertrag: 1.000,0"},
      {2, 30, 750, "Beleg"},
      {2, 110, 750, "Buchungstext"},
      {2, 410, 750, "Datum"},
      {2, 520, 750, "Betrag in EU"},
      {2, 30, 700, "1002"},
      {2, 110, 700, "Wartung"},
      {2, 410, 700, "02.10.2026"},
      {2, 520, 700, "491,00"},
      {2, 110, 150, "Gesamtsumme: 1.491,0"}
   };

   auto lines = MakeVisualLines(std::move(text));
   auto result = ParseBookings(lines);
   assert(result.bookings.size() == 2);
   assert(result.controls.size() == 2);
   assert(result.bookings[0].description == "Büromaterial und Zubehör");
   assert(result.bookings[1].description == "Wartung");
   assert(result.expected_cents == 149100);
   assert(result.actual_cents == 149100);
   assert(result.reconciled());
   assert(result.warnings.empty());
   assert(FormatEuroCents(result.actual_cents) == "1491,00");

   // Correctness is determined from the control total, not page count.
   result.expected_cents = 149200;
   assert(!result.reconciled());

   std::cout << "[PASS] synthetic two-page PDF booking parser\n";
}
