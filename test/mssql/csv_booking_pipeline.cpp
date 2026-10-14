// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\\file csv_booking_pipeline.cpp
\\brief Lesson: CSV source -> typed mapping policies -> SQL Server output sink.
\\details A deliberately small teaching example using the existing adecc types.
*/

#include "database.h"
#include "text_grid_wrapper.h"
#include "grid_wrapper.h"
#include "openxlsx_workbook.h"
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
#include <cstdint>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#ifdef _WIN32
#include <Windows.h>
#endif
#include <map>
#include <ranges>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using database_ty = adecc::db::logical_database<adecc::db::mssql::mssql_database, adecc::db::mssql::query>;

// cpp_domain building blocks: each conversion/validation is a compile-time policy.
using IsoDateCase = adecc::CaseConversion< adecc::RegexStringPolicy<R"(^[0-9]{4}-[0-9]{2}-[0-9]{2}$)">,
                                           adecc::ConvertToConversionPolicy<std::string, adecc::date_ty>>;
using EuDateCase = adecc::CaseConversion<  adecc::RegexStringPolicy<R"(^[0-9]{2}\.[0-9]{2}\.[0-9]{4}$)">,
                                           adecc::ConvertToConversionPolicy<std::string, adecc::date_ty, adecc::DateFmt::EUROPE>>;
using UsDateCase = adecc::CaseConversion<  adecc::RegexStringPolicy<R"(^[0-9]{2}/[0-9]{2}/[0-9]{4}$)">,
                                           adecc::ConvertToConversionPolicy<std::string, adecc::date_ty, adecc::DateFmt::US>>;
using InvoiceDateConversion = adecc::ConversionChainPolicy< adecc::TrimStringConversionPolicy,
                                                            adecc::CaseConversionPolicy<IsoDateCase, EuDateCase, UsDateCase>>;
using InvoiceDate = adecc::ConvertedPolicyValue<"InvoiceDate", InvoiceDateConversion, adecc::ValidYearMonthDayPolicy,
                                                adecc::ConvertToStringWithArgumentsPolicy<adecc::date_ty, adecc::DateFmt::EUROPE>>;


using EuroConversion = adecc::ConversionChainPolicy<adecc::TrimStringConversionPolicy, adecc::GermanDecimalToFloatConversionPolicy<double>>;
using EuroAmount = adecc::ConvertedPolicyValue<"EuroAmount", EuroConversion, adecc::PositiveNumberPolicy<double>>;


using mapping_ids_ty = std::map<std::string, long long>;
using id_names_ty = std::map<int, std::string>;

using domain_types = adecc::defined_type_list<long long, std::string>;
using account_types = adecc::defined_type_list<long long, std::string, std::string>;
using csv_file_types = adecc::defined_type_list<std::string, std::string, std::string, std::string, std::string, std::string, 
                                                EuroAmount, InvoiceDate, adecc::Iban, adecc::VatId_DE>;
using csv_types = adecc::defined_type_list<long long, std::string, std::string, std::string, std::string, std::string, std::string, 
                                           EuroAmount, InvoiceDate, adecc::Iban, adecc::VatId_DE>;

using booking_types = adecc::defined_type_list<long long, std::string, std::string, long long, long long, long long, long long, 
                                               adecc::money_ty, adecc::date_ty, std::string, std::string>;

struct AccountMetaData {
   static constexpr std::string_view svTableName = "dbo.AdeccCsvAccount";
   static constexpr std::array<std::string_view, 3> arrAttributeNames {
	   "id", "code", "name"
	   };
	   
   static constexpr std::array<std::size_t, 1> arrKeyIndices { 0 };
   static constexpr std::optional<std::size_t> optIdentityIndex { 0 };
   static constexpr std::array<std::size_t, 0> arrReadOnlyIndices {};
   };
   
struct BookingMetaData {
   static constexpr std::string_view svTableName = "dbo.AdeccCsvBooking";
   static constexpr std::array<std::string_view, 11> arrAttributeNames {
      "id", "reference", "name", "salutation_id", "kind_id", "debit_account_id", "credit_account_id", 
	  "amount_eur", "invoice_date", "iban", "vat_id_de"
	  };
	  
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


void CreateDemoTables(database_ty& aDb) {
   // Disposable teaching tables, created in dependency order.
   std::vector<std::string> const vecCommands {
      "DROP TABLE IF EXISTS dbo.AdeccCsvBooking",
      "DROP TABLE IF EXISTS dbo.AdeccCsvSalutation",
      "DROP TABLE IF EXISTS dbo.AdeccCsvKind",
      "DROP TABLE IF EXISTS dbo.AdeccCsvAccount",
	  
      "CREATE TABLE dbo.AdeccCsvSalutation ("
         "id bigint IDENTITY(1,1) PRIMARY KEY, "
		 "code nvarchar(40) NOT NULL UNIQUE"
		 ")",
		 
      "CREATE TABLE dbo.AdeccCsvKind ("
         "id bigint IDENTITY(1,1) PRIMARY KEY, "
		 "code nvarchar(40) NOT NULL UNIQUE"
		 ")",
		 
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
         "CONSTRAINT CK_AdeccCsvDistinctAccounts CHECK(debit_account_id <> credit_account_id)"
		 ")"
      };
	  
   std::for_each(vecCommands.begin(), vecCommands.end(), [&aDb](std::string const& strSql) { aDb.ExecuteCommand(strSql); });
   }

mapping_ids_ty StoreDomain(database_ty& aDb, std::string_view svTable, std::vector<std::string> const& vecCodes) {
   auto aOutput = domain_types::invoke([&]<typename... types_ty>() {
      return aDb.template MakeOutputRange<types_ty...>(
         std::format("INSERT INTO {} (code) VALUES (:code)", svTable),
         {  {"id",   adecc::db_output_param_role::identity},
            {"code", adecc::db_output_param_role::needed_value}
         });
      });
	  
   auto aSource = vecCodes | std::views::transform([](std::string const& strCode) {
                                 return domain_types::type_list {0LL, strCode };
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

mapping_ids_ty StoreAccounts(database_ty& aDb, id_names_ty& mpAccountNames) {
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
      mpAccountNames.emplace(static_cast<int>(std::get<0>(tplAccount)), std::get<2>(tplAccount));
      }
   if (mpIds.size() != vecAccounts.size()) {
      throw std::runtime_error("Account IDs incomplete");
      }
   return mpIds;
   }

// Query results are materialized before either grid sink consumes them.
using account_source_ty =
   adecc::defined_type_list<std::string, int, adecc::money_ty,
                            adecc::money_ty, adecc::money_ty>;
using person_database_ty =
   adecc::defined_type_list<std::string, int, std::string, std::string,
                            adecc::date_ty, double>;
using person_domain_ty =
   adecc::defined_type_list<std::string, int, adecc::Iban, adecc::VatId_DE,
                            InvoiceDate, double>;
using person_display_ty =
   adecc::defined_type_list<std::string, std::string, std::string,
                            std::string, std::string, std::string>;
using captions_ty =
   std::vector<std::tuple<std::string, int, adecc::EAlignmentType>>;

struct AccountRow {
   std::string code;
   std::string name;
   double debit = 0;
   double credit = 0;
   double balance = 0;
};

struct SummaryData {
   long long booking_count = 0;
   double total_eur = 0;
   double difference_eur = 0;
   std::vector<AccountRow> accounts;
};

struct PersonRow {
   std::string name;
   std::string salutation;
   std::string iban;
   std::string vat_id;
   adecc::date_ty invoice_date{};
   double amount_eur = 0;
   std::string display_date;
   std::string display_amount;
};

// One source-to-sink algorithm for text and XLSX. Both GridModel (random
// access) and WriteGridModel (sequential) implement append-range via cpp_core.
// Each caller supplies already configured captions and a projected row range.
template <class Grid, std::ranges::input_range Rows>
   requires (adecc::grid::write_grid_model_type<Grid> ||
             adecc::grid::grid_model_type<Grid>)
void FillGrid(Grid& grid, Rows&& rows) {
   grid += std::forward<Rows>(rows);
}

captions_ty AccountCaptions() {
   using Align = adecc::EAlignmentType;
   return {
      {"Konto", 10, Align::left},
      {"Kontoname", 29, Align::left},
      {"Soll EUR", 16, Align::right},
      {"Haben EUR", 16, Align::right},
      {"Saldo EUR", 16, Align::right}
   };
}

captions_ty PersonCaptions() {
   using Align = adecc::EAlignmentType;
   return {
      {"Name", 24, Align::left},
      {"Anrede", 12, Align::left},
      {"IBAN", 34, Align::left},
      {"USt-IdNr.", 15, Align::left},
      {"Rechnungsdatum", 19, Align::center},
      {"Betrag EUR", 17, Align::right}
   };
}

SummaryData ReadSummary(database_ty& db, id_names_ty const& accountNames) {
   auto totals = db.ExecuteOne<long long, adecc::money_ty>(
      "SELECT COUNT_BIG(*) AS BookingCount, "
      "COALESCE(SUM(amount_eur), CONVERT(decimal(18,2),0)) AS TotalAmount "
      "FROM dbo.AdeccCsvBooking");
   if(!totals) throw std::runtime_error("Cannot read booking totals");

   auto balance = db.ExecuteOne<adecc::money_ty>(
      "SELECT COALESCE(SUM(amount),CONVERT(decimal(18,2),0)) AS Balance "
      "FROM (SELECT amount_eur AS amount FROM dbo.AdeccCsvBooking "
      "UNION ALL SELECT -amount_eur FROM dbo.AdeccCsvBooking) AS entries");
   if(!balance || std::abs(std::get<0>(*balance).Get()) > 0.001)
      throw std::runtime_error("Unbalanced bookings");

   SummaryData result{
      .booking_count = std::get<0>(*totals),
      .total_eur = std::get<1>(*totals).Get(),
      .difference_eur = std::get<0>(*balance).Get()
   };
   auto records = account_source_ty::invoke([&]<typename... Types>() {
      return db.template Execute<Types...>(
         "SELECT a.code AS AccountCode, CAST(a.id AS int) AS AccountId, "
         "SUM(IIF(b.debit_account_id=a.id, b.amount_eur, 0)) AS DebitAmount, "
         "SUM(IIF(b.credit_account_id=a.id, b.amount_eur, 0)) AS CreditAmount, "
         "SUM(IIF(b.debit_account_id=a.id, b.amount_eur, 0) - "
         "IIF(b.credit_account_id=a.id, b.amount_eur, 0)) AS BalanceAmount "
         "FROM dbo.AdeccCsvAccount a LEFT JOIN dbo.AdeccCsvBooking b "
         "ON b.debit_account_id=a.id OR b.credit_account_id=a.id "
         "GROUP BY a.id, a.code ORDER BY a.code");
   });
   for(auto const& record : records) {
      result.accounts.push_back(AccountRow{
         .code = std::get<0>(record),
         .name = accountNames.at(std::get<1>(record)),
         .debit = std::get<2>(record).Get(),
         .credit = std::get<3>(record).Get(),
         .balance = std::get<4>(record).Get()
      });
   }
   return result;
}

std::vector<PersonRow> ReadPeople(database_ty& db,
                                  mapping_ids_ty const& salutations,
                                  bool const sampleOnly) {
   std::string sql =
      "SELECT name AS PersonName, CAST(salutation_id AS int) AS SalutationId, "
      "iban AS Iban, vat_id_de AS VatId, invoice_date AS InvoiceDate, "
      "amount_eur FROM dbo.AdeccCsvBooking";
   // Preserve the historical console sample, but export all rows to Excel.
   if(sampleOnly)
      sql += " WHERE kind_id = :kind_id AND salutation_id = :salutation_id";
   sql += " ORDER BY id";

   std::map<int, std::string> names;
   for(auto const& [name, id] : salutations)
      names.emplace(static_cast<int>(id), name);

   std::vector<PersonRow> result;
   auto consume = [&](auto&& rows) {
      for(auto const& record : rows) {
         auto domain = adecc::Transform<person_database_ty, person_domain_ty>(record);
         auto display = adecc::Transform<person_domain_ty, person_display_ty>(
            domain,
            adecc::BindMapping<1, person_domain_ty, person_display_ty>(names),
            adecc::BindMapping<2, person_domain_ty, person_display_ty>(
               [](adecc::Iban const& value) { return value.Value(); }),
            adecc::BindMapping<5, person_domain_ty, person_display_ty>(
               [](double const& value) {
                  return adecc::ConvertTo<std::string>(value);
               }));
         result.push_back(PersonRow{
            .name = std::get<0>(display),
            .salutation = std::get<1>(display),
            .iban = std::get<2>(display),
            .vat_id = std::get<3>(display),
            .invoice_date = std::get<4>(record),
            .amount_eur = std::get<5>(record),
            .display_date = std::get<4>(display),
            .display_amount = std::get<5>(display)
         });
      }
   };

   if(sampleOnly) {
      auto rows = person_database_ty::invoke([&]<typename... Types>() {
         return db.template Execute<Types...>(
            sql, {{"kind_id", 4, true}, {"salutation_id", 3, true}});
      });
      consume(rows);
   }
   else {
      auto rows = person_database_ty::invoke([&]<typename... Types>() {
         return db.template Execute<Types...>(sql);
      });
      consume(rows);
   }
   return result;
}

void PrintSummary(SummaryData const& data) {
   std::println("Bookings: {}, debit total = credit total = {:.2f} EUR",
                data.booking_count, data.total_eur);
   std::println("[PASS] debit/credit control: difference = {:.2f} EUR",
                data.difference_eur);

   using Backend = adecc::grid::OStreamGridBackend<>;
   adecc::grid::WriteGridModel<Backend> grid{
      Backend{std::cout, Backend::TableSeparators()}, AccountCaptions()
   };
   auto rows = data.accounts | std::views::transform([](AccountRow const& record) {
      return std::tuple{
         record.code, record.name,
         adecc::ConvertTo<std::string>(record.debit),
         adecc::ConvertTo<std::string>(record.credit),
         adecc::ConvertTo<std::string>(record.balance)
      };
   });
   FillGrid(grid, rows);
}

void PrintPeople(std::vector<PersonRow> const& people) {
   std::println("Sample bookings and domain-formatted person data:");
   using Backend = adecc::grid::OStreamGridBackend<>;
   adecc::grid::WriteGridModel<Backend> grid{
      Backend{std::cout, Backend::TableSeparators()}, PersonCaptions()
   };
   auto rows = people | std::views::transform([](PersonRow const& record) {
      return std::tuple{
         record.name, record.salutation, record.iban, record.vat_id,
         record.display_date, record.display_amount
      };
   });
   FillGrid(grid, rows);
}

std::filesystem::path ExecutableDirectory(char const* argv0) {
#ifdef _WIN32
   std::wstring path(32768, L'\0');
   DWORD const count = ::GetModuleFileNameW(
      nullptr, path.data(), static_cast<DWORD>(path.size()));
   if(count == 0 || count >= path.size())
      throw std::runtime_error("Cannot determine executable directory");
   path.resize(count);
   return std::filesystem::path{path}.parent_path();
#else
   return std::filesystem::absolute(argv0).parent_path();
#endif
}

std::filesystem::path ExportExcel(SummaryData const& data,
                                  std::vector<PersonRow> const& people,
                                  std::filesystem::path const& directory) {
   namespace xlsx = adecc::openxlsx_grid;
   auto const path = directory / "CsvBookingReport.xlsx";
   if(std::filesystem::exists(path))
      std::filesystem::remove(path);

   xlsx::OpenXLSXWorkbook book{path};
   {
      auto grid = book.grid("Summenübersicht", xlsx::GridRegion{1, 1, 0, 0, false});
      grid.set_captions(AccountCaptions());
      auto rows = data.accounts | std::views::transform([](AccountRow const& record) {
         return std::tuple{
            record.code, record.name, record.debit, record.credit, record.balance
         };
      });
      FillGrid(grid, rows);
   }
   {
      auto grid = book.grid("Summenübersicht", xlsx::GridRegion{1, 7, 0, 0, false});
      using Align = adecc::EAlignmentType;
      grid.set_captions(captions_ty{
         {"Kennzahl", 26, Align::left}, {"Wert", 19, Align::right}
      });
      std::vector<std::tuple<std::string, double>> metrics{
         {"Buchungen", static_cast<double>(data.booking_count)},
         {"Soll/Haben EUR", data.total_eur},
         {"Differenz EUR", data.difference_eur}
      };
      FillGrid(grid, metrics);
   }
   {
      auto grid = book.grid("Personen", xlsx::GridRegion{1, 1, 0, 0, false});
      grid.set_captions(PersonCaptions());
      auto rows = people | std::views::transform([](PersonRow const& record) {
         // Native Excel numeric and calendar types rather than display strings.
         return std::tuple{
            record.name, record.salutation, record.iban, record.vat_id,
            record.invoice_date, record.amount_eur
         };
      });
      FillGrid(grid, rows);
      if(grid.rows() != people.size())
         throw std::runtime_error("Excel person export row count mismatch");
   }
   book.save();
   book.close();

   // Confirm the persisted document contains both sheets and the last record.
   OpenXLSX::XLDocument check;
   check.open(path.string());
   auto summarySheet = check.workbook().worksheet("Summenübersicht");
   auto peopleSheet = check.workbook().worksheet("Personen");
   if(summarySheet.findCell(1, 1).empty() ||
      peopleSheet.findCell(1, 1).empty())
      throw std::runtime_error("Excel export is missing worksheet headings");
   if(!people.empty() &&
      peopleSheet.findCell(
         static_cast<std::uint32_t>(people.size() + 1), 1).empty())
      throw std::runtime_error("Excel export is missing the last person");
   check.close();
   return path;
}

std::size_t ImportCsvBookings(database_ty& aDb, std::filesystem::path const& aPath, mapping_ids_ty const& mpSalutations,
                              mapping_ids_ty const& mpKinds, mapping_ids_ty const& mpAccounts) {
   auto aSource = adecc::FromCsvFileTyped<false, true, csv_file_types>(aPath, ";", true)
                   | std::views::transform([](auto&& tplRow) { 
				          return std::tuple_cat(std::tuple{0LL}, std::move(tplRow));
						  })
                   | std::views::transform([&](auto&& tplRow) {
                          return adecc::Transform<csv_types, booking_types>(std::move(tplRow),
                                              adecc::BindMapping<3, csv_types, booking_types>(mpSalutations),
                                              adecc::BindMapping<4, csv_types, booking_types>(mpKinds),
                                              adecc::BindMapping<5, csv_types, booking_types>(mpAccounts),
                                              adecc::BindMapping<6, csv_types, booking_types>(mpAccounts),
                                              adecc::BindMapping<9, csv_types, booking_types>(
                                                        [](adecc::Iban const& aIban) { return aIban.Value(); }));
                          });
		 
   auto aSink = booking_types::invoke([&]<typename... types_ty>() {
         return aDb.template MakeOutputSink<types_ty...>(TBooking::CreateInsertSql(), TBooking::CreateInsertOutputParameters());
         });
		 
   aSink(aSource);
   
   auto const aCount = aDb.ExecuteOne<long long>("SELECT COUNT_BIG(*) AS BookingCount FROM dbo.AdeccCsvBooking");
   if (!aCount) {
      throw std::runtime_error("Cannot read imported booking count");
      }
   return static_cast<std::size_t>(std::get<0>(*aCount));
   }

} // namespace

int main(int iArgc, char** argv) {
   try {
      auto db = Connect();
      auto transaction = db.Transaction();
      CreateDemoTables(db);
      auto const salutations = StoreDomain(
         db, "dbo.AdeccCsvSalutation", {"Herr", "Frau", "Firma", "Divers"});
      auto const kinds = StoreDomain(
         db, "dbo.AdeccCsvKind", {"Rechnung", "Gutschrift", "Storno", "Lastschrift"});
      id_names_ty accountNames;
      auto const accounts = StoreAccounts(db, accountNames);

      auto const directory = ExecutableDirectory(iArgc > 0 ? argv[0] : "");
      auto const input = iArgc > 1
         ? std::filesystem::path{argv[1]}
         : directory / "bookings.csv";
      auto const count = ImportCsvBookings(
         db, input, salutations, kinds, accounts);
      auto const summary = ReadSummary(db, accountNames);
      auto const textPeople = ReadPeople(db, salutations, true);
      auto const excelPeople = ReadPeople(db, salutations, false);
      if(excelPeople.size() != count ||
         summary.booking_count != static_cast<long long>(count))
         throw std::runtime_error("Unfiltered Excel person selection is incomplete");

      transaction.CommitAndClose();
      PrintSummary(summary);
      PrintPeople(textPeople);
      auto const reportPath = ExportExcel(summary, excelPeople, directory);
      std::cout << "[PASS] " << count << " bookings imported\n";
      std::cout << "[PASS] XLSX: " << summary.accounts.size()
                << " accounts, " << excelPeople.size()
                << " persons (without WHERE)\n";
      std::cout << "[FILE] " << reportPath.string() << '\n';
      return 0;
   }
   catch(std::exception const& error) {
      std::cerr << "[FAIL] " << error.what() << '\n';
      return 1;
   }
}
