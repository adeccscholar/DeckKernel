// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#include "pdf_booking.h"
#include <podofo/podofo.h>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>

using namespace adecc::pdfbooking;

int main(int argc, char** argv) {
   try {
      if(argc < 3) {
         std::cout << "Usage: adecc_podofo_booking --inspect|--extract file.pdf "
                      "[--csv output.csv] [--show-bookings] [--tolerance 2.5]\n";
         return 2;
      }
      std::string_view const mode = argv[1];
      if(mode != "--inspect" && mode != "--extract")
         throw std::invalid_argument("Expected --inspect or --extract");
      std::filesystem::path const file = argv[2];
      if(!std::filesystem::is_regular_file(file))
         throw std::runtime_error("Input PDF does not exist");
      std::filesystem::path output;
      bool show_bookings = false;
      double tolerance = 2.5;
      for(int i = 3; i < argc; ++i) {
         std::string_view flag = argv[i];
         if(flag == "--csv" && mode == "--extract" && i + 1 < argc)
            output = argv[++i];
         else if(flag == "--show-bookings" && mode == "--extract")
            show_bookings = true;
         else if(flag == "--tolerance" && i + 1 < argc)
            tolerance = std::stod(argv[++i]);
         else throw std::invalid_argument("Unrecognized command line argument");
      }
      PoDoFo::PdfMemDocument document;
      document.Load(file.string());
      std::vector<TextFragment> fragments;
      auto const& pages = document.GetPages();
      for(unsigned p = 0; p < pages.GetCount(); ++p) {
         std::vector<PoDoFo::PdfTextEntry> entries;
         pages.GetPageAt(p).ExtractTextTo(entries);
         std::cout << "[PAGE] " << p + 1 << " text entries: " << entries.size() << '\n';
         for(auto const& entry : entries)
            if(std::isfinite(entry.X) && std::isfinite(entry.Y))
               fragments.push_back({p + 1, entry.X, entry.Y, entry.Text});
      }
      if(fragments.empty()) {
         std::cerr << "[FAIL] No extractable text: scanned PDF or missing character mapping?\n";
         return 3;
      }
      auto lines = MakeVisualLines(std::move(fragments), tolerance);
      if(mode == "--inspect") {
         std::cout << "[PRIVATE] Text positions (page, Y, X, text):\n";
         for(auto const& line : lines)
            for(auto const& item : line.parts)
               std::cout << line.page << ';' << std::fixed << std::setprecision(2)
                         << item.y << ';' << item.x << ';' << item.text << '\n';
         return 0;
      }
      auto parsed = ParseBookings(lines);
      std::cout << "[SUMMARY] bookings=" << parsed.bookings.size()
                << " controls=" << parsed.controls.size()
                << " warnings=" << parsed.warnings.size() << '\n'
                << "[SUMMARY] extracted=" << FormatEuroCents(parsed.actual_cents) << " EUR\n";
      if(parsed.expected_cents)
         std::cout << "[SUMMARY] expected=" <<
            FormatEuroCents(*parsed.expected_cents) << " EUR\n";
      for(auto const& warning : parsed.warnings)
         std::cout << "[WARN] " << warning << '\n';
      if(show_bookings)
         for(auto const& b : parsed.bookings)
            std::cout << "[PRIVATE] " << b.page << ": " << b.voucher << " | "
                      << b.description << " | " << b.date << " | "
                      << FormatEuroCents(b.amount_cents) << '\n';
      if(!parsed.reconciled() || !parsed.warnings.empty()) {
         std::cerr << "[FAIL] Document does not reconcile or contains unclassified text.\n";
         return 4;
      }
      if(!output.empty()) {
         if(std::filesystem::exists(output))
            throw std::runtime_error("Output exists; refusing to overwrite");
         std::ofstream csv(output, std::ios::binary);
         if(!csv) throw std::runtime_error("Cannot create output CSV");
         csv << "Beleg;Buchungstext;Datum;BetragEUR\n";
         for(auto const& b : parsed.bookings)
            csv << EscapeCsv(b.voucher) << ';' << EscapeCsv(b.description)
                << ';' << EscapeCsv(b.date) << ';'
                << EscapeCsv(FormatEuroCents(b.amount_cents)) << '\n';
         if(!csv) throw std::runtime_error("Writing CSV failed");
         std::cout << "[FILE] " << output.string() << '\n';
      }
      std::cout << "[PASS] PDF amount reconciles with grand total\n";
      return 0;
   }
   catch(std::exception const& ex) {
      std::cerr << "[FAIL] " << ex.what() << '\n';
      return 1;
   }
}
