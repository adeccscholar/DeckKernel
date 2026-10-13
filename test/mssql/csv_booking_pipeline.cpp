// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\\file csv_booking_pipeline.cpp
\\brief Lesson: CSV source -> typed mapping policies -> SQL Server output sink.
\\details A deliberately small teaching example using the existing adecc types.
*/

#include "database.h"
#include "mssql_odbc_database.h"
#include "type_lists.h"
#include "type_transformations.h"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using database_ty = adecc::db::logical_database<
   adecc::db::mssql::mssql_database, adecc::db::mssql::query>;

using domain_types = adecc::defined_type_list<long long, std::string>;
using csv_types = adecc::defined_type_list<
   long long, std::string, std::string, std::string, std::string, long long>;
using booking_types = adecc::defined_type_list<
   long long, std::string, std::string, long long, long long, long long>;

std::string Environment(char const* szName, std::string_view svDefault = {}) {
   if (char const* szValue = std::getenv(szName); szValue && *szValue) {
      return szValue;
      }
   return std::string{svDefault};
   }

database_ty Connect() {
   return database_ty{adecc::db::mssql::mssql_credentials{
      .strServer = Environment("ADECC_MSSQL_SERVER", "localhost"),
      .strDatabase = Environment("ADECC_MSSQL_DATABASE", "Person_Test"),
      .boIntegrated = true,
      .boMARS = true,
      .iTimeout = 30
      }};
   }

// This example uses a deliberately simple semicolon CSV (without quoted fields).
std::vector<csv_types::type_list> ReadCsv(std::filesystem::path const& aPath) {
   std::ifstream aFile{aPath};
   if (!aFile) { throw std::runtime_error("Cannot open CSV: " + aPath.string()); }

   std::vector<csv_types::type_list> vecRows;
   std::string strLine;
   if (!std::getline(aFile, strLine) ||
       strLine != "reference;name;salutation;kind;amount_cents") {
      throw std::runtime_error("Invalid CSV header");
      }
   while (std::getline(aFile, strLine)) {
      if (strLine.empty()) { continue; }
      std::array<std::string, 5> arrFields;
      std::size_t uBegin = 0;
      for (std::size_t i = 0; i < arrFields.size(); ++i) {
         auto const uEnd = strLine.find(';', uBegin);
         if (i + 1 != arrFields.size() && uEnd == std::string::npos) {
            throw std::runtime_error("Missing CSV column: " + strLine);
            }
         if (i + 1 == arrFields.size() && uEnd != std::string::npos) {
            throw std::runtime_error("Extra CSV column: " + strLine);
            }
         arrFields[i] = strLine.substr(uBegin, uEnd == std::string::npos
            ? std::string::npos : uEnd - uBegin);
         uBegin = uEnd == std::string::npos ? strLine.size() : uEnd + 1;
         }
      std::size_t uParsed = 0;
      long long const iCents = std::stoll(arrFields[4], &uParsed);
      if (uParsed != arrFields[4].size()) {
         throw std::runtime_error("Invalid amount in CSV: " + strLine);
         }
      vecRows.emplace_back(0LL, std::move(arrFields[0]), std::move(arrFields[1]),
         std::move(arrFields[2]), std::move(arrFields[3]), iCents);
      }
   return vecRows;
   }

void CreateDemoTables(database_ty& aDb) {
   // Dedicated disposable teaching tables; never touch production tables.
   aDb.ExecuteCommand("DROP TABLE IF EXISTS dbo.AdeccCsvBooking");
   aDb.ExecuteCommand("DROP TABLE IF EXISTS dbo.AdeccCsvSalutation");
   aDb.ExecuteCommand("DROP TABLE IF EXISTS dbo.AdeccCsvKind");
   aDb.ExecuteCommand(
      "CREATE TABLE dbo.AdeccCsvSalutation ("
      "id bigint IDENTITY(1,1) PRIMARY KEY, code nvarchar(40) NOT NULL UNIQUE)");
   aDb.ExecuteCommand(
      "CREATE TABLE dbo.AdeccCsvKind ("
      "id bigint IDENTITY(1,1) PRIMARY KEY, code nvarchar(40) NOT NULL UNIQUE)");
   aDb.ExecuteCommand(
      "CREATE TABLE dbo.AdeccCsvBooking ("
      "id bigint IDENTITY(1,1) PRIMARY KEY,"
      "reference nvarchar(40) NOT NULL UNIQUE,"
      "name nvarchar(200) NOT NULL,"
      "salutation_id bigint NOT NULL REFERENCES dbo.AdeccCsvSalutation(id),"
      "kind_id bigint NOT NULL REFERENCES dbo.AdeccCsvKind(id),"
      "amount_cents bigint NOT NULL)");
   }

std::map<std::string, long long> StoreDomain(
   database_ty& aDb, std::string_view svTable, std::vector<std::string> const& vecCodes) {
   auto aOutput = domain_types::invoke([&]<typename... types_ty>() {
      return aDb.template MakeOutputRange<types_ty...>(
         "INSERT INTO " + std::string{svTable} + " (code) VALUES (:code)",
         {
            {"id", adecc::db_output_param_role::identity},
            {"code", adecc::db_output_param_role::needed_value}
         });
      });
   auto aSource = vecCodes | std::views::transform([](std::string const& strCode) {
      return domain_types::type_list{0LL, strCode};
      });
   std::map<std::string, long long> mpIds;
   for (auto const& tplRow : aOutput(aSource)) {
      mpIds.emplace(std::get<1>(tplRow), std::get<0>(tplRow));
      }
   if (mpIds.size() != vecCodes.size()) {
      throw std::runtime_error("Domain contains duplicate entries");
      }
   return mpIds;
   }

void StoreBookings(database_ty& aDb, std::vector<csv_types::type_list> const& vecSource,
   std::map<std::string, long long> const& mpSalutations,
   std::map<std::string, long long> const& mpKinds) {
   auto aSource = vecSource | std::views::transform([&](auto const& tplRow) {
      return adecc::Transform<csv_types, booking_types>(tplRow,
         adecc::BindMapping<3, csv_types, booking_types>(mpSalutations),
         adecc::BindMapping<4, csv_types, booking_types>(mpKinds));
      });
   auto aSink = booking_types::invoke([&]<typename... types_ty>() {
      return aDb.template MakeOutputSink<types_ty...>(
         "INSERT INTO dbo.AdeccCsvBooking "
         "(reference, name, salutation_id, kind_id, amount_cents) "
         "VALUES (:reference, :name, :salutation_id, :kind_id, :amount_cents)",
         {
            {"id", adecc::db_output_param_role::identity},
            {"reference", adecc::db_output_param_role::needed_value},
            {"name", adecc::db_output_param_role::needed_value},
            {"salutation_id", adecc::db_output_param_role::needed_value},
            {"kind_id", adecc::db_output_param_role::needed_value},
            {"amount_cents", adecc::db_output_param_role::needed_value}
         });
      });
   aSink(aSource);
   }

} // namespace

int main(int iArgc, char** argv) {
   try {
      // Source -> Transformation (lookup policies) -> Sink
      auto aDb = Connect();
      auto aTransaction = aDb.Transaction();
      CreateDemoTables(aDb);
      auto mpSalutations = StoreDomain(aDb, "dbo.AdeccCsvSalutation",
         {"Herr", "Frau", "Firma"});
      auto mpKinds = StoreDomain(aDb, "dbo.AdeccCsvKind",
         {"Rechnung", "Gutschrift", "Storno"});
      auto vecBookings = ReadCsv(iArgc > 1 ? argv[1] : "bookings.csv");
      StoreBookings(aDb, vecBookings, mpSalutations, mpKinds);
      aTransaction.CommitAndClose();
      std::cout << "[PASS] " << vecBookings.size() << " bookings imported\n";
      return 0;
      }
   catch (std::exception const& aError) {
      std::cerr << "[FAIL] " << aError.what() << '\n';
      return 1;
      }
   }
