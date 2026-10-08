// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_database.cpp
\brief Lesson 3: PostgreSQL connection and transactional persistence.
*/

#include "scryfall_database.h"

#include "convert_integral.h"

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <print>
#include <ranges>
#include <string>
#include <string_view>

namespace deckkernel::test {
namespace {

std::string Environment(char const* const szName, std::string_view const svFallback) {
   if (char const* const szValue = std::getenv(szName); szValue != nullptr && *szValue != '\0') {
      return std::string{ szValue };
      }

   return std::string{ svFallback };
   }


bool EnvironmentFlag(char const* const szName, bool const boFallback) {
   std::string const strValue = Environment(szName, boFallback ? "true" : "false" );

   return strValue == "1" || strValue == "true" || strValue == "yes" || strValue == "on";
   }


adecc::db::postgres::postgres_credentials MakeCredentials() {
   adecc::db::postgres::postgres_credentials aCredentials;

   aCredentials.strHost = Environment("DECKKERNEL_PGHOST", "localhost");
   aCredentials.uPort = adecc::ConvertTo<std::uint16_t>( Environment("DECKKERNEL_PGPORT", "5432"));
   aCredentials.strDatabase        = Environment("DECKKERNEL_PGDATABASE", "DeckKernel");
   aCredentials.strUser            = Environment("DECKKERNEL_PGUSER", "deckkernel_user");
   aCredentials.strPassword        = Environment("DECKKERNEL_PGPASSWORD", "");
   aCredentials.boIntegrated       = EnvironmentFlag("DECKKERNEL_PG_INTEGRATED", true);
   aCredentials.strSslMode         = Environment("DECKKERNEL_PGSSLMODE", "prefer");
   aCredentials.strGssEncMode      = Environment("DECKKERNEL_PG_GSSENCMODE", "disable" );
   aCredentials.strGssLib          = Environment("DECKKERNEL_PG_GSSLIB", "");
   aCredentials.strKrbSrvName      = Environment("DECKKERNEL_PG_KRBSRVNAME", "postgres");
   aCredentials.strApplicationName = "DeckKernel-Scryfall-Test";

   return aCredentials;
   }


void EnsureSchema(postgres_database_ty const& aDatabase) {
   aDatabase.ExecuteCommand(
      "CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_sets ("
      "  id text PRIMARY KEY,"
      "  code text NOT NULL UNIQUE,"
      "  name text NOT NULL"
      "  )"
      );

   aDatabase.ExecuteCommand(
      "CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_cards ("
      "  id          text PRIMARY KEY,"
      "  oracle_id   text NULL,"
      "  name        text NOT NULL,"
      "  set_id      text NOT NULL REFERENCES deckkernel_test.scryfall_sets(id),"
      "  released_at date NOT NULL"
      "  )"
      );
   }

} // namespace


postgres_database_ty OpenDatabase() {
   postgres_database_ty aDatabase{
      MakeCredentials()
      };

   std::println("PostgreSQL connection:     {}", aDatabase.GetServer());
   std::println("PostgreSQL authentication: {}", aDatabase.Credentials().boIntegrated ? "integrated SSPI" : "password");

   return aDatabase;
   }


void StoreProcess(postgres_database_ty& aDatabase, ParsedBulkData const& aParsed) {
   EnsureSchema(aDatabase);

   auto aTransaction = aDatabase.Transaction();
   aDatabase.ExecuteCommand("DROP INDEX IF EXISTS deckkernel_test.ix_scryfall_cards_oracle_id" );
   aDatabase.ExecuteCommand("DROP INDEX IF EXISTS deckkernel_test.ix_scryfall_cards_released" );
   aDatabase.ExecuteCommand("TRUNCATE TABLE deckkernel_test.scryfall_cards, deckkernel_test.scryfall_sets");

   auto aSetSink = aDatabase.template MakeOutputSink<std::string, std::string, std::string>(
                                     TScryfallSet::CreateInsertSql(), TScryfallSet::CreateInsertOutputParameters()
                                     );

   auto rngSets = aParsed.mpSets | std::views::values;
   aSetSink = rngSets;

   auto aCardSink = aDatabase.template MakeOutputSink<std::string, std::optional<std::string>, std::string, 
                                                      std::string, adecc::date_ty>(
         TScryfallCard::CreateInsertSql(), TScryfallCard::CreateInsertOutputParameters());

   aCardSink = aParsed.vecCards;

   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_oracle_id ON deckkernel_test.scryfall_cards(oracle_id)");
   aDatabase.ExecuteCommand("CREATE INDEX ix_scryfall_cards_released ON deckkernel_test.scryfall_cards(released_at DESC)");
   aTransaction.Commit();

   std::println("Stored {} cards and {} sets through adecc output sinks.", aParsed.vecCards.size(), aParsed.mpSets.size());
   }

} // namespace deckkernel::test
