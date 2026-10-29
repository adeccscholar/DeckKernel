// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_postgres_test.cpp
\brief First DeckKernel lesson composing load, parse, database and evaluation.
\details
main() intentionally reads like the complete technical story:
Load -> Parse -> Connect -> Store -> Evaluate.
The evaluation stays here so the example visibly closes the data-flow loop.
*/

#include "scryfall_database.h"
#include "scryfall_images.h"
#include "scryfall_load.h"
#include "scryfall_parse.h"

#include "text_grid_wrapper.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <print>
#include <ranges>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace deckkernel::test {

template <typename fn_ty>
using process_result_ty = std::invoke_result_t<fn_ty>;

template <typename fn_ty>
process_result_ty<fn_ty> RunTimedProcess(std::string_view const svName, fn_ty&& fnProcess) {
   auto const aStart = std::chrono::steady_clock::now();

   if constexpr (std::is_void_v<process_result_ty<fn_ty>>) {
      std::invoke(std::forward<fn_ty>(fnProcess));

      double const flSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - aStart).count();

      std::println("[TIME] {:<10}: {:.3f} s", svName, flSeconds);
      }
   else {
      process_result_ty<fn_ty> aResult = std::invoke(std::forward<fn_ty>(fnProcess));

      double const flSeconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - aStart).count();

      std::println("[TIME] {:<10}: {:.3f} s", svName, flSeconds);
      return aResult;
      }
   }


void ShowLatestCards(postgres_database_ty const& aDatabase) {
   using backend_ty = adecc::grid::OStreamGridBackend<adecc::AnsiStreamPolicy>;
   using grid_ty = adecc::grid::WriteGridModel<backend_ty>;

   adecc::vecCaptions<adecc::AnsiStreamPolicy> const vecCaptions {
      { "Name", 54, adecc::EAlignmentType::left },
      { "Set", 40, adecc::EAlignmentType::left }
      };

   backend_ty aBackend { std::cout,  backend_ty::TableSeparators()  };

   grid_ty aGrid { std::move(aBackend), vecCaptions };

   auto rngLatest = aDatabase.template Execute<std::string, std::string>(
      "SELECT c.name AS card_name, s.name AS set_name "
      "FROM deckkernel_test.scryfall_cards c "
      "JOIN deckkernel_test.scryfall_sets s ON s.id = c.set_id "
      "ORDER BY c.released_at DESC, c.name, c.id "
      "LIMIT 20"
      );

   aGrid = rngLatest;
   }


void ShowLatestCardsThroughSink(postgres_database_ty const& aDatabase) {
   using backend_ty = adecc::grid::OStreamGridBackend<adecc::AnsiStreamPolicy>;
   using grid_ty = adecc::grid::WriteGridModel<backend_ty>;

   adecc::vecCaptions<adecc::AnsiStreamPolicy> const vecCaptions {
      { "Name", 54, adecc::EAlignmentType::left },
      { "Set", 40, adecc::EAlignmentType::left }
      };

   backend_ty aBackend{
      std::cout,
      backend_ty::CompactTableSeparators()
      };

   grid_ty aGrid{
      std::move(aBackend),
      vecCaptions
      };

   auto rngLatest = aDatabase.template Execute<std::string, std::string>(
      "SELECT c.name AS card_name, s.name AS set_name "
      "FROM deckkernel_test.scryfall_cards c "
      "JOIN deckkernel_test.scryfall_sets s ON s.id = c.set_id "
      "ORDER BY c.released_at DESC, c.name, c.id "
      "LIMIT 20"
      );

   std::ranges::copy(
      rngLatest,
      aGrid.template sink<std::string, std::string>()
      );
   }


void EvaluateProcess(postgres_database_ty const& aDatabase) {
   std::println("\nNewest 20 cards: direct range assignment to text grid");
   ShowLatestCards(aDatabase);

   std::println("\nNewest 20 cards: std::ranges::copy to text-grid sink");
   ShowLatestCardsThroughSink(aDatabase);
   }

} // namespace deckkernel::test


int main(int argc,char* argv[]) {
   using namespace deckkernel::test;

   try {
      if(argc>2) throw std::runtime_error("Expected one mode: --domains-only, --image-probe, --images-only, --images-offline or --all");
      std::string_view const svMode=argc==2?std::string_view{argv[1]}:std::string_view{"--images-only"};
      if(svMode=="--help" || svMode=="-h") {
         std::println("Usage: deckkernel_scryfall_postgres_test.exe [--all | --domains-only | --images-only | --image-probe | --images-offline]");
         std::println("--domains-only: synchronize domain codes only; preserve existing IDs and descriptions");
         std::println("--image-probe: one missing image, HTTPS/TLS check, no card reload");
         std::println("--images-only (default): restore caches and fetch all missing images, no card reload");
         std::println("--images-offline: restore all from caches, no network and no card reload");
         std::println("--all: fresh model initialization and import; refuses to reset an existing model");
         return 0;
         }
      if(svMode!="--all" && svMode!="--domains-only" && svMode!="--images-only" &&
         svMode!="--image-probe" && svMode!="--images-offline")
         throw std::runtime_error("Unknown test mode; use --help");

      if(svMode=="--domains-only") {
         std::println("[MODE] --domains-only: parse catalogs and update only missing domain codes");
         LoadedBulkData aLoaded=RunTimedProcess("Load", []() { return LoadProcess(); });
         aLoaded.strSetsCatalog=RunTimedProcess("Read CardSets", []() { return ReadCardSets(); });
         ParsedBulkData const aParsed=RunTimedProcess("Parse", [&]() { return ParseProcess(aLoaded); });
         postgres_database_ty aDatabase=RunTimedProcess("Connect", []() { return OpenDatabase(); });
         RunTimedProcess("Domains", [&]() { UpdateDomainValues(aDatabase,aParsed); });
         std::println("[MODE] domains synchronized; card, set and image records unchanged");
         return 0;
         }

      if(svMode!="--all") {
         std::println("[MODE] {}: reuse existing model; Load/Parse/Store are SKIPPED",svMode);
         postgres_database_ty aDatabase=RunTimedProcess("Connect",[](){return OpenDatabase();});
         ImageImportOptions aOptions;
         aOptions.bAllowNetwork=(svMode!="--images-offline");
         aOptions.bContinuous=(svMode=="--images-only" || svMode=="--images-offline");
         if(svMode=="--image-probe") aOptions.uMaxMissing=1;
         RunTimedProcess("Images",[&](){ImportImages(aDatabase,aOptions);});
         std::println("[MODE] image stage completed; card and set tables unchanged");
         return 0;
         }

      std::println("[MODE] --all: fresh Load/Parse/Store; existing models are protected");
      LoadedBulkData aLoaded = RunTimedProcess(
         "Load",
         []() {
            return LoadProcess();
            }
         );

      aLoaded.strSetsCatalog = RunTimedProcess("Read CardSets", []() { return ReadCardSets(); });

      ParsedBulkData const aParsed = RunTimedProcess(
         "Parse",
         [&aLoaded]() {
            return ParseProcess(aLoaded);
            }
         );

      postgres_database_ty aDatabase = RunTimedProcess(
         "Connect",
         []() {
            return OpenDatabase();
            }
         );

      RunTimedProcess(
         "Store",
         [&aDatabase, &aParsed]() {
            StoreProcess(aDatabase, aParsed);
            }
         );

      RunTimedProcess(
         "Images",
         [&aDatabase]() {
            ImportImages(aDatabase);
            }
         );

      RunTimedProcess(
         "Evaluate",
         [&aDatabase]() {
            EvaluateProcess(aDatabase);
            }
         );

      std::println("\nFunctional test completed successfully.");
      return 0;
      }
   catch (std::exception const& ex) {
      std::println(std::cerr, "Functional test failed: {}", ex.what());
      return 1;
      }
   }
