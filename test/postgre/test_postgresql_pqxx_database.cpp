// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file test_postgresql_pqxx_database.cpp
\brief Integration test for the adecc PostgreSQL/libpqxx backend.
*/

#include "database.h"
#include "diagnostic_text.h"
#include "pqxx_database.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <format>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace std::chrono;

using physical_database_ty = adecc::db::postgres::postgres_database;
using database_ty = adecc::db::logical_database<
   physical_database_ty,
   adecc::db::postgres::query
   >;

using row_ty = std::tuple<
   long long,
   std::string,
   std::optional<std::string>,
   std::wstring,
   adecc::money_ty,
   double,
   adecc::timestamp_ty,
   adecc::date_ty,
   adecc::time_ty,
   bool,
   std::optional<long long>
   >;

struct test_config {
   std::string   strHost{ "localhost" };
   std::uint16_t uPort{ 5432 };
   std::string   strDatabase{ "DeckKernel" };
   std::string   strUser{ "deckkernel_user" };
   std::string   strPassword{};
   bool          boIntegrated{ true };
   };

[[noreturn]] void Fail(std::string const& strMessage) {
   throw std::runtime_error{ strMessage };
   }


void Require(bool const boCondition, std::string const& strMessage) {
   if (!boCondition) {
      Fail(strMessage);
      }
   }


std::string GetEnvironment(char const* const szName, std::string_view const svFallback = {}) {
   if (char const* const szValue = std::getenv(szName);
       szValue != nullptr && *szValue != '\0') {
      return std::string{ szValue };
      }

   return std::string{ svFallback };
   }


bool ParseBoolean(std::string_view const svValue, bool const boDefault) {
   if (svValue.empty()) {
      return boDefault;
      }

   if (svValue == "1" || svValue == "true" || svValue == "TRUE" ||
       svValue == "yes" || svValue == "YES" || svValue == "on" || svValue == "ON") {
      return true;
      }

   if (svValue == "0" || svValue == "false" || svValue == "FALSE" ||
       svValue == "no" || svValue == "NO" || svValue == "off" || svValue == "OFF") {
      return false;
      }

   Fail(std::format("invalid boolean value '{}'", svValue));
   }


std::uint16_t ParsePort(std::string_view const svValue) {
   unsigned int uPort{};
   auto const [pEnd, ec] = std::from_chars(
      svValue.data(),
      svValue.data() + svValue.size(),
      uPort
      );

   if (ec != std::errc{} ||
       pEnd != svValue.data() + svValue.size() ||
       uPort == 0 ||
       uPort > 65535) {
      Fail(std::format("invalid PostgreSQL port '{}'", svValue));
      }

   return static_cast<std::uint16_t>(uPort);
   }


void PrintUsage(char const* const szProgram) {
   std::cout
      << "Usage:\n"
      << "  " << szProgram
      << " --host <host> --database <database> --user <user> [--integrated]\n"
      << "  " << szProgram
      << " --host <host> --database <database> --user <user> "
         "--password-login --password <password>\n";
   }


test_config ParseArguments(int const iArgc, char* const* const argv) {
   test_config aConfig{
      .strHost = GetEnvironment("DECKKERNEL_PGHOST", "localhost"),
      .uPort = ParsePort(GetEnvironment("DECKKERNEL_PGPORT", "5432")),
      .strDatabase = GetEnvironment("DECKKERNEL_PGDATABASE", "DeckKernel"),
      .strUser = GetEnvironment("DECKKERNEL_PGUSER", "deckkernel_user"),
      .strPassword = GetEnvironment("DECKKERNEL_PGPASSWORD"),
      .boIntegrated = ParseBoolean(
         GetEnvironment("DECKKERNEL_PG_INTEGRATED"),
         true
         )
      };

   auto fnNeedValue = [&](int& iIndex, std::string_view const svOption) -> std::string {
      if (iIndex + 1 >= iArgc) {
         Fail(std::format("missing value for {}", svOption));
         }

      ++iIndex;
      return argv[iIndex];
      };

   for (int iIndex{ 1 }; iIndex < iArgc; ++iIndex) {
      std::string_view const svArg{ argv[iIndex] };

      if (svArg == "--help" || svArg == "-h") {
         PrintUsage(argv[0]);
         std::exit(0);
         }
      else if (svArg == "--host") {
         aConfig.strHost = fnNeedValue(iIndex, svArg);
         }
      else if (svArg == "--port") {
         aConfig.uPort = ParsePort(fnNeedValue(iIndex, svArg));
         }
      else if (svArg == "--database") {
         aConfig.strDatabase = fnNeedValue(iIndex, svArg);
         }
      else if (svArg == "--user") {
         aConfig.strUser = fnNeedValue(iIndex, svArg);
         }
      else if (svArg == "--password") {
         aConfig.strPassword = fnNeedValue(iIndex, svArg);
         }
      else if (svArg == "--integrated") {
         aConfig.boIntegrated = true;
         }
      else if (svArg == "--password-login") {
         aConfig.boIntegrated = false;
         }
      else {
         Fail(std::format("unknown argument '{}'", svArg));
         }
      }

   if (aConfig.strHost.empty()) {
      Fail("PostgreSQL host is missing");
      }

   if (aConfig.strDatabase.empty()) {
      Fail("PostgreSQL database is missing");
      }

   if (aConfig.strUser.empty()) {
      Fail("PostgreSQL user is missing");
      }

   if (!aConfig.boIntegrated && aConfig.strPassword.empty()) {
      Fail("password login requires --password or DECKKERNEL_PGPASSWORD");
      }

   return aConfig;
   }


std::string MakeSuffix() {
   auto const iNow = duration_cast<microseconds>(
      system_clock::now().time_since_epoch()
      ).count();

   return std::to_string(static_cast<unsigned long long>(iNow));
   }


struct database_object_guard {
   database_ty* pDatabase{};
   std::string strTable{};

   ~database_object_guard() {
      if (pDatabase == nullptr || strTable.empty()) {
         return;
         }

      try {
         pDatabase->ExecuteCommand(std::format("DROP TABLE IF EXISTS {}", strTable));
         }
      catch (...) {
         }
      }
   };


adecc::date_ty MakeDate(int const iYear, unsigned const uMonth, unsigned const uDay) {
   adecc::date_ty const aDate{
      year{ iYear },
      month{ uMonth },
      day{ uDay }
      };

   Require(aDate.ok(), "test constructed an invalid date");
   return aDate;
   }


adecc::timestamp_ty MakeTimestamp(
   adecc::date_ty const& aDate,
   hours const aHours,
   minutes const aMinutes,
   seconds const aSeconds
) {
   return time_point_cast<system_clock::duration>(
      sys_days{ aDate } + aHours + aMinutes + aSeconds
      );
   }


adecc::time_ty MakeTime(
   hours const aHours,
   minutes const aMinutes,
   seconds const aSeconds
) {
   return adecc::time_ty{
      duration_cast<seconds>(aHours + aMinutes + aSeconds)
      };
   }


template <class... Args>
std::tuple<Args...> RequireOne(
   std::expected<std::tuple<Args...>, adecc::db::SingleRowError> const& aResult,
   std::string_view const svContext
) {
   if (!aResult) {
      switch (aResult.error()) {
      case adecc::db::SingleRowError::NotFound:
         Fail(std::format("{}: row not found", svContext));
      case adecc::db::SingleRowError::TooMany:
         Fail(std::format("{}: more than one row returned", svContext));
         }
      }

   return *aResult;
   }


void CheckRow(row_ty const& aActual, row_ty const& aExpected) {
   Require(std::get<0>(aActual) == std::get<0>(aExpected), "Id differs");
   Require(std::get<1>(aActual) == std::get<1>(aExpected), "Name differs");
   Require(std::get<2>(aActual) == std::get<2>(aExpected), "AlternateName differs");
   Require(std::get<3>(aActual) == std::get<3>(aExpected), "WideName differs");

   Require(
      std::abs(std::get<4>(aActual).Get() - std::get<4>(aExpected).Get()) < 0.000001,
      "Amount differs"
      );

   Require(
      std::abs(std::get<5>(aActual) - std::get<5>(aExpected)) < 0.000000001,
      "Ratio differs"
      );

   Require(std::get<6>(aActual) == std::get<6>(aExpected), "timestamp differs");
   Require(std::get<7>(aActual) == std::get<7>(aExpected), "date differs");

   Require(
      std::get<8>(aActual).to_duration() == std::get<8>(aExpected).to_duration(),
      "time differs"
      );

   Require(std::get<9>(aActual) == std::get<9>(aExpected), "bool differs");
   Require(std::get<10>(aActual) == std::get<10>(aExpected), "ParentId differs");
   }

} // namespace


int main(int const iArgc, char* const* const argv) {
   try {
      test_config const aConfig = ParseArguments(iArgc, argv);

      adecc::db::postgres::postgres_credentials const aCredentials{
         .strHost = aConfig.strHost,
         .uPort = aConfig.uPort,
         .strDatabase = aConfig.strDatabase,
         .strUser = aConfig.strUser,
         .strPassword = aConfig.strPassword,
         .boIntegrated = aConfig.boIntegrated,
         .strSslMode = "prefer",
         .strGssEncMode = "disable",
         .strKrbSrvName = "postgres",
         .strGssLib = {},
         .strApplicationName = "DeckKernel-PostgreSQL-Backend-Test",
         .iConnectTimeout = 30
         };

      std::cout << "[TEST] connect to "
                << aConfig.strHost
                << ':'
                << aConfig.uPort
                << " / "
                << aConfig.strDatabase
                << " as "
                << aConfig.strUser
                << " using "
                << (aConfig.boIntegrated ? "integrated authentication" : "password authentication")
                << '\n';

      database_ty theDatabase{ aCredentials };
      Require(theDatabase.Connected(), "database reports not connected after construction");
      std::cout << "[PASS] connection\n";

      std::string const strTable = "adecc_pqxx_data_" + MakeSuffix();

      database_object_guard aCleanup{
         .pDatabase = &theDatabase,
         .strTable = strTable
         };

      theDatabase.ExecuteCommand(std::format(
         "CREATE TEMP TABLE {} ("
         "Id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,"
         "Name varchar(200) NOT NULL UNIQUE,"
         "AlternateName varchar(200) NULL,"
         "WideName text NOT NULL,"
         "Amount numeric(18,2) NOT NULL,"
         "Ratio numeric(18,6) NOT NULL,"
         "CreatedAt timestamp(0) NOT NULL,"
         "BusinessDate date NOT NULL,"
         "BusinessTime time(0) NOT NULL,"
         "Active boolean NOT NULL,"
         "ParentId bigint NULL"
         ") ON COMMIT PRESERVE ROWS",
         strTable
         ));
      std::cout << "[PASS] temporary test table created\n";

      std::string const strInsert = std::format(
         "INSERT INTO {} "
         "(Name, AlternateName, WideName, Amount, Ratio, CreatedAt, "
         " BusinessDate, BusinessTime, Active, ParentId) "
         "VALUES "
         "(:Name, :AlternateName, :WideName, :Amount, :Ratio, :CreatedAt, "
         " :BusinessDate, :BusinessTime, :Active, :ParentId)",
         strTable
         );

      auto theOutput = theDatabase.MakeOutputRange<
         long long,
         std::string,
         std::optional<std::string>,
         std::wstring,
         adecc::money_ty,
         double,
         adecc::timestamp_ty,
         adecc::date_ty,
         adecc::time_ty,
         bool,
         std::optional<long long>
         >(
            strInsert,
            {
               { "Id", adecc::db_output_param_role::identity },
               { "Name", adecc::db_output_param_role::needed_value },
               { "AlternateName", adecc::db_output_param_role::needed_value },
               { "WideName", adecc::db_output_param_role::needed_value },
               { "Amount", adecc::db_output_param_role::needed_value },
               { "Ratio", adecc::db_output_param_role::needed_value },
               { "CreatedAt", adecc::db_output_param_role::needed_value },
               { "BusinessDate", adecc::db_output_param_role::needed_value },
               { "BusinessTime", adecc::db_output_param_role::needed_value },
               { "Active", adecc::db_output_param_role::needed_value },
               { "ParentId", adecc::db_output_param_role::needed_value }
            }
         );

      auto fnInsertOne = [&](row_ty const& aInput) -> row_ty {
         std::array<row_ty, 1> arrInput{ aInput };
         std::optional<row_ty> optOutput;

         for (auto const& aOutput : theOutput(arrInput)) {
            Require(!optOutput.has_value(), "single-row output range returned more than one row");
            optOutput = aOutput;
            }

         Require(optOutput.has_value(), "single-row output range returned no row");
         return *optOutput;
         };

      adecc::date_ty const aDate1 = MakeDate(2026, 10, 8);
      adecc::timestamp_ty const aTimestamp1 = MakeTimestamp(
         aDate1,
         hours{ 1 },
         minutes{ 2 },
         seconds{ 3 }
         );
      adecc::time_ty const aTime1 = MakeTime(
         hours{ 14 },
         minutes{ 15 },
         seconds{ 16 }
         );

      row_ty aInput1{
         0LL,
         "Muenchen",
         std::nullopt,
         L"M\u00FCnchen \u2013 \u6771\u4EAC",
         adecc::money_ty{ 1234.56 },
         12.125,
         aTimestamp1,
         aDate1,
         aTime1,
         true,
         std::nullopt
         };

      row_ty aInserted1 = fnInsertOne(aInput1);
      long long const iId1 = std::get<0>(aInserted1);
      Require(iId1 > 0, "identity value was not returned");
      std::get<0>(aInput1) = iId1;
      CheckRow(aInserted1, aInput1);
      std::cout << "[PASS] identity and first insert, Id=" << iId1 << '\n';

      auto const aRead1 = RequireOne(
         theDatabase.ExecuteOne<
            long long,
            std::string,
            std::optional<std::string>,
            std::wstring,
            adecc::money_ty,
            double,
            adecc::timestamp_ty,
            adecc::date_ty,
            adecc::time_ty,
            bool,
            std::optional<long long>
            >(
               std::format(
                  "SELECT Id, Name, AlternateName, WideName, Amount, Ratio, CreatedAt, "
                  "BusinessDate, BusinessTime, Active, ParentId "
                  "FROM {} WHERE Id = :Id",
                  strTable
                  ),
               database_ty::Params(database_ty::Param("Id", iId1))
               ),
         "read first inserted row"
         );

      CheckRow(aRead1, aInput1);
      std::cout << "[PASS] narrow, wide, NULL, numeric, timestamp, date, time and bool roundtrip\n";

      std::wstring const strWideValue{
         L"\u00C4\u00D6\u00DC \u20AC \u6771\u4EAC"
         };

      auto const [strWideResult] = RequireOne(
         theDatabase.ExecuteOne<std::wstring>(
            "SELECT CAST(:Value AS text) AS Value",
            database_ty::Params(database_ty::Param("Value", strWideValue))
            ),
         "wstring parameter roundtrip"
         );
      Require(strWideResult == strWideValue, "std::wstring parameter differs");

      std::wstring_view const svWideValue{ strWideValue };
      auto const [strWideViewResult] = RequireOne(
         theDatabase.ExecuteOne<std::wstring>(
            "SELECT CAST(:Value AS text) AS Value",
            database_ty::Params(database_ty::Param("Value", svWideValue))
            ),
         "wstring_view parameter roundtrip"
         );
      Require(strWideViewResult == strWideValue, "std::wstring_view parameter differs");

      wchar_t const* const szWideValue = L"M\u00FCnchen";
      auto const [strWidePointerResult] = RequireOne(
         theDatabase.ExecuteOne<std::wstring>(
            "SELECT CAST(:Value AS text) AS Value",
            database_ty::Params(database_ty::Param("Value", szWideValue))
            ),
         "wchar_t pointer parameter roundtrip"
         );
      Require(
         strWidePointerResult == std::wstring{ szWideValue },
         "wchar_t const* parameter differs"
         );
      std::cout << "[PASS] wstring, wstring_view and wchar_t const* parameter roundtrip\n";

      adecc::date_ty const aDate2 = MakeDate(2026, 10, 9);
      row_ty aInput2{
         0LL,
         "Child",
         std::optional<std::string>{ "Alternate" },
         L"Kind \u20AC",
         adecc::money_ty{ 42.10 },
         0.125,
         MakeTimestamp(aDate2, hours{ 7 }, minutes{ 8 }, seconds{ 9 }),
         aDate2,
         MakeTime(hours{ 17 }, minutes{ 18 }, seconds{ 19 }),
         false,
         iId1
         };

      row_ty aInserted2 = fnInsertOne(aInput2);
      long long const iId2 = std::get<0>(aInserted2);
      Require(iId2 > iId1, "second identity value is not greater than first value");
      std::get<0>(aInput2) = iId2;
      CheckRow(aInserted2, aInput2);

      std::vector<std::tuple<long long, std::string>> vecRepeatedParameterRows;

      for (auto const& aRow : theDatabase.Execute<long long, std::string>(
              std::format(
                 "SELECT Id, Name FROM {} "
                 "WHERE Id = :Id OR ParentId = :Id ORDER BY Id",
                 strTable
                 ),
              database_ty::Params(database_ty::Param("Id", iId1)))) {
         vecRepeatedParameterRows.emplace_back(aRow);
         }

      Require(
         vecRepeatedParameterRows.size() == 2,
         "repeated logical parameter query did not return two rows"
         );
      Require(
         std::get<0>(vecRepeatedParameterRows[0]) == iId1,
         "repeated parameter query returned wrong first row"
         );
      Require(
         std::get<0>(vecRepeatedParameterRows[1]) == iId2,
         "repeated parameter query returned wrong second row"
         );
      std::cout << "[PASS] repeated :Id parameter supplied once and reused by PostgreSQL\n";

      std::string const strRollbackName = "Rollback_" + MakeSuffix();

      {
         auto theTransaction = theDatabase.Transaction();

         row_ty aRollbackRow{
            0LL,
            strRollbackName,
            std::nullopt,
            L"Rollback",
            adecc::money_ty{ 1.00 },
            1.0,
            aTimestamp1,
            aDate1,
            aTime1,
            true,
            std::nullopt
            };

         [[maybe_unused]] row_ty const aInsertedRollback = fnInsertOne(aRollbackRow);
         }

      auto const [iRollbackCount] = RequireOne(
         theDatabase.ExecuteOne<long long>(
            std::format(
               "SELECT COUNT(*) FROM {} WHERE Name = :Name",
               strTable
               ),
            database_ty::Params(database_ty::Param("Name", strRollbackName))
            ),
         "check automatic rollback"
         );
      Require(iRollbackCount == 0, "TransactionScope destructor did not roll back");
      std::cout << "[PASS] transaction automatic rollback\n";

      std::string const strCommitName = "Commit_" + MakeSuffix();

      {
         auto theTransaction = theDatabase.Transaction();

         row_ty aCommitRow{
            0LL,
            strCommitName,
            std::nullopt,
            L"Commit",
            adecc::money_ty{ 2.00 },
            2.0,
            aTimestamp1,
            aDate1,
            aTime1,
            true,
            std::nullopt
            };

         [[maybe_unused]] row_ty const aInsertedCommit = fnInsertOne(aCommitRow);
         theTransaction.CommitAndClose();
         }

      auto const [iCommitCount] = RequireOne(
         theDatabase.ExecuteOne<long long>(
            std::format(
               "SELECT COUNT(*) FROM {} WHERE Name = :Name",
               strTable
               ),
            database_ty::Params(database_ty::Param("Name", strCommitName))
            ),
         "check committed transaction"
         );
      Require(iCommitCount == 1, "committed transaction is not visible");
      std::cout << "[PASS] transaction commit\n";

      bool boDiagnosticException{ false };

      try {
         row_ty aDuplicate{
            0LL,
            std::get<1>(aInput1),
            std::nullopt,
            L"Duplicate",
            adecc::money_ty{ 9.99 },
            9.0,
            aTimestamp1,
            aDate1,
            aTime1,
            true,
            std::nullopt
            };

         [[maybe_unused]] row_ty const aShouldFail = fnInsertOne(aDuplicate);
         }
      catch (std::exception const& ex) {
         boDiagnosticException = true;
         std::string const strWhat{ ex.what() };

         Require(!strWhat.empty(), "constraint error lost PostgreSQL diagnostics");
         std::cout << "[PASS] PostgreSQL constraint diagnostics retained\n";
         adecc::diagnostic::WriteUtf8(std::cout, "[DIAGNOSTIC SAMPLE]\n");
         adecc::diagnostic::WriteUtf8(std::cout, strWhat);
         adecc::diagnostic::WriteUtf8(std::cout, "\n[END DIAGNOSTIC SAMPLE]\n");
         }

      Require(
         boDiagnosticException,
         "duplicate UNIQUE(Name) insert unexpectedly succeeded"
         );

      std::cout << "\nALL POSTGRESQL LIBPQXX TESTS PASSED\n";
      return 0;
      }
   catch (std::exception const& ex) {
      adecc::diagnostic::WriteUtf8(std::cerr, "\nTEST FAILED\n");
      adecc::diagnostic::WriteUtf8(std::cerr, ex.what());
      adecc::diagnostic::WriteUtf8(std::cerr, "\n");
      return 1;
      }
   catch (...) {
      std::cerr << "\nTEST FAILED\nunknown exception\n";
      return 2;
      }
   }
