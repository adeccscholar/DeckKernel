// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\\file csv_booking_pipeline.cpp
\\brief Lesson: CSV source -> typed mapping policies -> SQL Server output sink.
\\details A deliberately small teaching example using the existing adecc types.
*/

#include "database.h"
#include "text_grid_wrapper.h"
#include "mssql_odbc_database.h"
#include "type_lists.h"
#include "type_transformations.h"
#include "file_as_tuple.h"
#include "system_data_persistent.h"
#include "policy_common.h"
#include "policies_hawkers_tray.h"
#include "sepa_policies_hawkers_tray.h"
#include <optional>
#include <print>
#include <cmath>
#include <algorithm>

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

// cpp_domain building blocks: each conversion/validation is a compile-time policy.
using IsoDateCase = adecc::CaseConversion<
   adecc::RegexStringPolicy<R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}$)">,
   adecc::ConvertToConversionPolicy<std::string, adecc::date_ty>>;
using EuropeanDateCase = adecc::CaseConversion<
   adecc::RegexStringPolicy<R"(^[0-9]{2}\.[0-9]{2}\.[0-9]{4}$)">,
   adecc::ConvertToConversionPolicy<std::string, adecc::date_ty, adecc::DateFmt::EUROPE>>;
using UsDateCase = adecc::CaseConversion<
   adecc::RegexStringPolicy<R"(^[0-9]{2}/[0-9]{2}/[0-9]{4}$)">,
   adecc::ConvertToConversionPolicy<std::string, adecc::date_ty, adecc::DateFmt::US>>;
using InvoiceDateConversion = adecc::ConversionChainPolicy<
   adecc::TrimStringConversionPolicy,
   adecc::CaseConversionPolicy<IsoDateCase, EuropeanDateCase, UsDateCase>>;
using InvoiceDate = adecc::ConvertedPolicyValue<"InvoiceDate",
   InvoiceDateConversion, adecc::ValidYearMonthDayPolicy>;

using EuroConversion = adecc::ConversionChainPolicy<
   adecc::TrimStringConversionPolicy,
   adecc::GermanDecimalToFloatConversionPolicy<double>>;
using EuroAmount = adecc::ConvertedPolicyValue<"EuroAmount",
   EuroConversion, adecc::PositiveNumberPolicy<double>>;


// SEPA domain types validate and normalize directly while reading the CSV.
using booking_iban_ty = adecc::Iban;
using booking_vat_ty = adecc::VatId_DE;

using mapping_ids_ty = std::map<std::string, long long>;

using domain_types = adecc::defined_type_list<long long, std::string>;
using account_types = adecc::defined_type_list<long long, std::string, std::string>;
using csv_file_types = adecc::defined_type_list<
   std::string, std::string, std::string, std::string,
   std::string, std::string, EuroAmount, InvoiceDate,
   booking_iban_ty, booking_vat_ty>;
using csv_types = adecc::defined_type_list<
   long long, std::string, std::string, std::string,
   std::string, std::string, std::string, EuroAmount, InvoiceDate,
   booking_iban_ty, booking_vat_ty>;
using booking_types = adecc::defined_type_list<
   long long, std::string, std::string, long long,
   long long, long long, long long, adecc::money_ty, adecc::date_ty,
   std::string, std::string>;

struct AccountMetaData {
   static constexpr std::string_view svTableName = "dbo.AdeccCsvAccount";
   static constexpr std::array<std::string_view, 3> arrAttributeNames{"id", "code", "name"};
   static constexpr std::array<std::size_t, 1> arrKeyIndices{0};
   static constexpr std::optional<std::size_t> optIdentityIndex{0};
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices{};
   };
struct BookingMetaData {
   static constexpr std::string_view svTableName = "dbo.AdeccCsvBooking";
   static constexpr std::array<std::string_view, 11> arrAttributeNames{
      "id", "reference", "name", "salutation_id", "kind_id",
      "debit_account_id", "credit_account_id", "amount_eur", "invoice_date", "iban", "vat_id_de"};
   static constexpr std::array<std::size_t, 1> arrKeyIndices{0};
   static constexpr std::optional<std::size_t> optIdentityIndex{0};
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices{};
   };
using TAccount = adecc::db::PersistentSystemData<account_types, AccountMetaData>;
using TBooking = adecc::db::PersistentSystemData<booking_types, BookingMetaData>;

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

// The adecc CSV source is a typed generator; no custom CSV parser.
auto ReadCsv(std::filesystem::path const& aPath) {
   return adecc::FromCsvFileTyped<false, true, csv_file_types>(aPath, ";", true);
   }

void CreateDemoTables(database_ty& aDb) {
   // Disposable teaching tables, created in dependency order.
   std::vector<std::string> const vecCommands{
      "DROP TABLE IF EXISTS dbo.AdeccCsvBooking",
      "DROP TABLE IF EXISTS dbo.AdeccCsvSalutation",
      "DROP TABLE IF EXISTS dbo.AdeccCsvKind",
      "DROP TABLE IF EXISTS dbo.AdeccCsvAccount",
      "CREATE TABLE dbo.AdeccCsvSalutation ("
         "id bigint IDENTITY(1,1) PRIMARY KEY, code nvarchar(40) NOT NULL UNIQUE)",
      "CREATE TABLE dbo.AdeccCsvKind ("
         "id bigint IDENTITY(1,1) PRIMARY KEY, code nvarchar(40) NOT NULL UNIQUE)",
      "CREATE TABLE dbo.AdeccCsvAccount ("
         "id bigint IDENTITY(1,1) PRIMARY KEY,"
         "code nvarchar(20) NOT NULL UNIQUE,"
         "name nvarchar(100) NOT NULL)",
      "CREATE TABLE dbo.AdeccCsvBooking ("
         "id bigint IDENTITY(1,1) PRIMARY KEY,"
         "reference nvarchar(40) NOT NULL UNIQUE,"
         "name nvarchar(200) NOT NULL,"
         "salutation_id bigint NOT NULL REFERENCES dbo.AdeccCsvSalutation(id),"
         "kind_id bigint NOT NULL REFERENCES dbo.AdeccCsvKind(id),"
         "debit_account_id bigint NOT NULL REFERENCES dbo.AdeccCsvAccount(id),"
         "credit_account_id bigint NOT NULL REFERENCES dbo.AdeccCsvAccount(id),"
         "amount_eur decimal(18,2) NOT NULL CHECK(amount_eur > 0),"
         "invoice_date date NOT NULL,"
         "iban nvarchar(34) NOT NULL,"
         "vat_id_de nvarchar(11) NOT NULL,"
         "CONSTRAINT CK_AdeccCsvDistinctAccounts CHECK(debit_account_id <> credit_account_id))"
      };
   std::for_each(vecCommands.begin(), vecCommands.end(),
      [&](std::string const& strSql) { aDb.ExecuteCommand(strSql); });
   }

mapping_ids_ty StoreDomain(
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
   mapping_ids_ty mpIds;
   for (auto const& tplRow : aOutput(aSource)) {
      mpIds.emplace(std::get<1>(tplRow), std::get<0>(tplRow));
      }
   if (mpIds.size() != vecCodes.size()) {
      throw std::runtime_error("Domain contains duplicate entries");
      }
   return mpIds;
   }

mapping_ids_ty StoreAccounts(database_ty& aDb) {
   std::vector<account_types::type_list> vecAccounts{
      {0LL, "1000", "Bank"},
      {0LL, "1200", "Forderungen"},
      {0LL, "1400", "Vorsteuer"},
      {0LL, "1600", "Verbindlichkeiten"},
      {0LL, "1776", "Umsatzsteuer"},
      {0LL, "4000", "Umsatzerloese"},
      {0LL, "4900", "Sonstige Aufwendungen"},
      {0LL, "1300", "Sonstige Forderungen"},
      {0LL, "1800", "Kasse"},
      {0LL, "3000", "Eigenkapital"},
      {0LL, "4200", "Nebenerloese"},
      {0LL, "5000", "Wareneinkauf"},
      {0LL, "6300", "Bankgebuehren"}
      };
   auto aOutput = account_types::invoke([&]<typename... types_ty>() {
      return aDb.template MakeOutputRange<types_ty...>(
         TAccount::CreateInsertSql(), TAccount::CreateInsertOutputParameters());
      });
   mapping_ids_ty mpIds;
   for (auto const& tplAccount : aOutput(vecAccounts)) {
      mpIds.emplace(std::get<1>(tplAccount), std::get<0>(tplAccount));
      }
   if (mpIds.size() != vecAccounts.size()) {
      throw std::runtime_error("Account IDs incomplete");
      }
   return mpIds;
   }

void PrintReport(database_ty& aDb) {
   auto aTotals = aDb.ExecuteOne<long long, adecc::money_ty>(
      "SELECT COUNT_BIG(*) AS BookingCount, "
      "COALESCE(SUM(amount_eur), CONVERT(decimal(18,2),0)) AS TotalAmount "
      "FROM dbo.AdeccCsvBooking");
   if (!aTotals) { throw std::runtime_error("Cannot read booking totals"); }
   std::println("Bookings: {}, debit total = credit total = {:.2f} EUR",
      std::get<0>(*aTotals), std::get<1>(*aTotals).Get());
   auto aBalance = aDb.ExecuteOne<adecc::money_ty>(
      "SELECT COALESCE(SUM(amount),CONVERT(decimal(18,2),0)) AS Balance FROM ("
      "SELECT amount_eur AS amount FROM dbo.AdeccCsvBooking "
      "UNION ALL SELECT -amount_eur FROM dbo.AdeccCsvBooking) AS entries");
   if (!aBalance || std::abs(std::get<0>(*aBalance).Get()) > 0.001) {
      throw std::runtime_error("Unbalanced bookings");
      }
   std::println("[PASS] debit/credit control: difference = 0 EUR");
   using backend_ty = adecc::grid::OStreamGridBackend<>;
   using grid_ty = adecc::grid::WriteGridModel<backend_ty>;
   using align_ty = adecc::EAlignmentType;
   grid_ty aGrid{
      backend_ty{std::cout, backend_ty::TableSeparators()},
      std::vector<std::tuple<std::string, int, align_ty>>{
         {"Konto", 8, align_ty::left},
         {"Soll EUR", 14, align_ty::right},
         {"Haben EUR", 14, align_ty::right},
         {"Saldo EUR", 14, align_ty::right}
         }
      };
   for (std::string const strCode : {"1000", "1200", "1300", "1400", "1600", "1776", "1800",
      "3000", "4000", "4200", "4900", "5000", "6300"}) {
      auto aAccount = aDb.ExecuteOne<adecc::money_ty, adecc::money_ty>(
         "SELECT COALESCE(SUM(CASE WHEN b.debit_account_id=a.id "
         "THEN b.amount_eur ELSE CONVERT(decimal(18,2),0) END), CONVERT(decimal(18,2),0)) AS DebitAmount, "
         "COALESCE(SUM(CASE WHEN b.credit_account_id=a.id "
         "THEN b.amount_eur ELSE CONVERT(decimal(18,2),0) END), CONVERT(decimal(18,2),0)) AS CreditAmount "
         "FROM dbo.AdeccCsvAccount a "
         "LEFT JOIN dbo.AdeccCsvBooking b "
         "ON b.debit_account_id=a.id OR b.credit_account_id=a.id "
         "WHERE a.code=:Code",
         database_ty::Params(database_ty::Param("Code", strCode)));
      if (!aAccount) { throw std::runtime_error("Cannot evaluate account " + strCode); }
      auto const& [aDebit, aCredit] = *aAccount;
      aGrid += std::tuple{
         strCode,
         std::format("{:.2f}", aDebit.Get()),
         std::format("{:.2f}", aCredit.Get()),
         std::format("{:.2f}", aDebit.Get() - aCredit.Get())
         };
      }
   }

std::size_t ImportCsvBookings(database_ty& aDb, std::filesystem::path const& aPath,
   mapping_ids_ty const& mpSalutations,
   mapping_ids_ty const& mpKinds,
   mapping_ids_ty const& mpAccounts) {
   auto aSource = ReadCsv(aPath)
      | std::views::transform([](csv_file_types::type_list const& tplRow) {
         return std::tuple_cat(std::tuple{0LL}, tplRow);
         })
      | std::views::transform([&](csv_types::type_list const& tplRow) {
         return adecc::Transform<csv_types, booking_types>(tplRow,
            adecc::BindMapping<3, csv_types, booking_types>(mpSalutations),
            adecc::BindMapping<4, csv_types, booking_types>(mpKinds),
            adecc::BindMapping<5, csv_types, booking_types>(mpAccounts),
            adecc::BindMapping<6, csv_types, booking_types>(mpAccounts));
         });
   auto aSink = booking_types::invoke([&]<typename... types_ty>() {
      return aDb.template MakeOutputSink<types_ty...>(
         TBooking::CreateInsertSql(), TBooking::CreateInsertOutputParameters());
      });
   aSink(aSource);
   auto const aCount = aDb.ExecuteOne<long long>(
      "SELECT COUNT_BIG(*) AS BookingCount FROM dbo.AdeccCsvBooking");
   if (!aCount) {
      throw std::runtime_error("Cannot read imported booking count");
      }
   return static_cast<std::size_t>(std::get<0>(*aCount));
   }

} // namespace

int main(int iArgc, char** argv) {
   try {
      // Source -> Transformation (lookup policies) -> Sink
      auto aDb = Connect();
      auto aTransaction = aDb.Transaction();
      CreateDemoTables(aDb);
      auto mpSalutations = StoreDomain(aDb, "dbo.AdeccCsvSalutation",
         {"Herr", "Frau", "Firma", "Divers"});
      auto mpKinds = StoreDomain(aDb, "dbo.AdeccCsvKind",
         {"Rechnung", "Gutschrift", "Storno", "Lastschrift"});
      auto mpAccounts = StoreAccounts(aDb);
      auto const aCsvPath = iArgc > 1 ? std::filesystem::path{argv[1]}
         : std::filesystem::absolute(argv[0]).parent_path() / "bookings.csv";
      auto uCount = ImportCsvBookings(aDb, aCsvPath,
         mpSalutations, mpKinds, mpAccounts);
      PrintReport(aDb);
      aTransaction.CommitAndClose();
      std::cout << "[PASS] " << uCount << " bookings imported\n";
      return 0;
      }
   catch (std::exception const& aError) {
      std::cerr << "[FAIL] " << aError.what() << '\n';
      return 1;
      }
   }
