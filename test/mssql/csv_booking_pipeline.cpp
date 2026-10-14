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
#include "unused_workbook_path.h"
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
#include <numeric>
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

// The domain is described once by type lists. SQL yields tuple generators,
// Transform maps them, and a GridModel / WriteGridModel consumes the range.
// No intermediate DTOs or duplicated display fields are needed.
using account_source_ty = adecc::defined_type_list<
   std::string, int, adecc::money_ty, adecc::money_ty, adecc::money_ty>;
using account_grid_ty = adecc::defined_type_list<
   std::string, std::string, adecc::money_ty, adecc::money_ty, adecc::money_ty>;

using person_database_ty = adecc::defined_type_list<
   std::string, int, std::string, std::string, adecc::date_ty, double>;
using person_domain_ty = adecc::defined_type_list<
   std::string, int, adecc::Iban, adecc::VatId_DE, InvoiceDate, double>;
using person_display_ty = adecc::defined_type_list<
   std::string, std::string, std::string, std::string, std::string, std::string>;
using person_excel_ty = adecc::defined_type_list<
   std::string, std::string, std::string, std::string, adecc::date_ty, double>;

using captions_ty =
   std::vector<std::tuple<std::string, int, adecc::EAlignmentType>>;
using excel_captions_ty = std::vector<std::tuple<std::string, int,
   adecc::EAlignmentType, adecc::openxlsx_grid::ExcelColumnFormat>>;

// The exact same sink algorithm is used by the text and Excel grid models.
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

excel_captions_ty AccountExcelCaptions() {
   using Align = adecc::EAlignmentType;
   return {
      {"Konto", 10, Align::left, {}},
      {"Kontoname", 29, Align::left, {}},
      {"Soll EUR", 16, Align::right, {"#,##0.00 \"EUR\""}},
      {"Haben EUR", 16, Align::right, {"#,##0.00 \"EUR\""}},
      {"Saldo EUR", 16, Align::right, {"#,##0.00 \"EUR\""}}
   };
}

excel_captions_ty PersonExcelCaptions() {
   using Align = adecc::EAlignmentType;
   return {
      {"Name", 24, Align::left, {}},
      {"Anrede", 12, Align::left, {}},
      {"IBAN", 34, Align::left, {}},
      {"USt-IdNr.", 15, Align::left, {}},
      {"Rechnungsdatum", 19, Align::center, {"dd.mm.yyyy"}},
      {"Betrag EUR", 17, Align::right, {"#,##0.00 \"EUR\""}}
   };
}

using totals_ty = std::tuple<long long, adecc::money_ty, adecc::money_ty>;

totals_ty ReadAndValidateTotals(database_ty& db) {
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

   return {std::get<0>(*totals), std::get<1>(*totals),
           std::get<0>(*balance)};
}

void PrintTotals(totals_ty const& totals) {
   std::println("Bookings: {}, debit total = credit total = {:.2f} EUR",
      std::get<0>(totals), std::get<1>(totals).Get());
   std::println("[PASS] debit/credit control: difference = {:.2f} EUR",
      std::get<2>(totals).Get());
}

template <class Grid>
   requires (adecc::grid::write_grid_model_type<Grid> ||
             adecc::grid::grid_model_type<Grid>)
void FillAccountGrid(database_ty& db, id_names_ty const& accountNames,
                     Grid& grid) {
   // Execute() is a single-pass generator. Its lifetime and transformations
   // intentionally remain inside this function until the sink has consumed it.
   auto sqlRows = account_source_ty::invoke([&]<typename... Types>() {
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
   auto rows = sqlRows | std::views::transform(
      [&](account_source_ty::type_list const& row) {
         return adecc::Transform<account_source_ty, account_grid_ty>(
            row,
            adecc::BindMapping<1, account_source_ty, account_grid_ty>(
               accountNames));
      });
   FillGrid(grid, rows);
}

std::map<int, std::string> SalutationNames(mapping_ids_ty const& salutations) {
   std::map<int, std::string> names;
   for(auto const& [name, id] : salutations)
      names.emplace(static_cast<int>(id), name);
   return names;
}

template <class Grid>
   requires (adecc::grid::write_grid_model_type<Grid> ||
             adecc::grid::grid_model_type<Grid>)
void FillPersonGrid(database_ty& db, std::map<int, std::string> const& names,
                    Grid& grid, bool sampleOnly) {
   // The console retains its pedagogical WHERE selection; Excel exports all.
   std::string sql =
      "SELECT name AS PersonName, CAST(salutation_id AS int) AS SalutationId, "
      "iban AS Iban, vat_id_de AS VatId, invoice_date AS InvoiceDate, "
      "amount_eur FROM dbo.AdeccCsvBooking";
   if(sampleOnly)
      sql += " WHERE kind_id = :kind_id AND salutation_id = :salutation_id";
   sql += " ORDER BY id";

   auto sqlRows = person_database_ty::invoke([&]<typename... Types>() {
      if(sampleOnly)
         return db.template Execute<Types...>(
            sql, {{"kind_id", 4, true}, {"salutation_id", 3, true}});
      return db.template Execute<Types...>(sql);
   });

   auto domainRows = sqlRows | std::views::transform(
      [](person_database_ty::type_list const& row) {
         return adecc::Transform<person_database_ty, person_domain_ty>(row);
      });

   // Only the presentation projection differs. No alternate storage structure
   // is introduced: both are direct typed ranges into the same grid sink.
   if constexpr (adecc::grid::write_grid_model_type<Grid>) {
      auto display = domainRows | std::views::transform(
         [&](person_domain_ty::type_list const& row) {
            return adecc::Transform<person_domain_ty, person_display_ty>(
               row,
               adecc::BindMapping<1, person_domain_ty, person_display_ty>(names),
               adecc::BindMapping<2, person_domain_ty, person_display_ty>(
                  [](adecc::Iban const& value) { return value.Value(); }),
               adecc::BindMapping<5, person_domain_ty, person_display_ty>(
                  [](double value) { return adecc::ConvertTo<std::string>(value); }));
         });
      FillGrid(grid, display);
   }
   else {
      auto excel = domainRows | std::views::transform(
         [&](person_domain_ty::type_list const& row) {
            return adecc::Transform<person_domain_ty, person_excel_ty>(
               row,
               adecc::BindMapping<1, person_domain_ty, person_excel_ty>(names),
               adecc::BindMapping<2, person_domain_ty, person_excel_ty>(
                  [](adecc::Iban const& value) { return value.Value(); }));
         });
      FillGrid(grid, excel);
   }
}

void PrintReport(database_ty& db, id_names_ty const& accounts,
                 std::map<int, std::string> const& salutations,
                 totals_ty const& totals) {
   PrintTotals(totals);
   using Backend = adecc::grid::OStreamGridBackend<>;
   {
      adecc::grid::WriteGridModel<Backend> grid{
         Backend{std::cout, Backend::TableSeparators()}, AccountCaptions()
      };
      FillAccountGrid(db, accounts, grid);
   }
   std::println("Sample bookings and domain-formatted person data:");
   {
      adecc::grid::WriteGridModel<Backend> grid{
         Backend{std::cout, Backend::TableSeparators()}, PersonCaptions()
      };
      FillPersonGrid(db, salutations, grid, true);
   }
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

double SumExportedPersonAmounts(std::filesystem::path const& file,
                                std::size_t expected_rows) {
   namespace xlsx = adecc::openxlsx_grid;
   xlsx::OpenXLSXWorkbook workbook{file};
   auto grid = workbook.grid_existing("Personen");
   if(grid.rows() != expected_rows || grid.columns() != 6)
      throw std::runtime_error("Person table dimensions do not match export");

   // GridModel's column selection is zero-based. The sixth column is amount.
   // No additional DTO, vector or conversion to a formatted string.
   auto amounts = grid.makeRowView<double>({5})
      | std::views::transform([](auto const& row) {
           return std::get<0>(row);
        });
   return std::ranges::fold_left(amounts, 0.0, std::plus<>{});
}

std::filesystem::path ExportExcel(database_ty& db,
                                  id_names_ty const& accounts,
                                  std::map<int, std::string> const& salutations,
                                  totals_ty const& totals,
                                  std::size_t imported,
                                  std::filesystem::path const& directory) {
   namespace xlsx = adecc::openxlsx_grid;
   auto const path = xlsx::UnusedWorkbookPath(directory, "CsvBookingReport");
   xlsx::OpenXLSXWorkbook book{path};
   {
      auto grid = book.grid("Summenübersicht", xlsx::GridRegion{1, 1, 0, 0, false});
      grid.set_captions(AccountExcelCaptions());
      xlsx::ExcelGridStyle style;
      style.header({
         .background = xlsx::RGB{34, 79, 119},
         .foreground = xlsx::RGB{255, 255, 255},
         .bold = true
      }).row_pattern({
         {.background = xlsx::RGB{255, 255, 255}},
         {.background = xlsx::RGB{235, 242, 249}}
      });
      grid.set_style(style);
      FillAccountGrid(db, accounts, grid);
   }
   {
      auto grid = book.grid("Summenübersicht", xlsx::GridRegion{1, 7, 0, 0, false});
      using Align = adecc::EAlignmentType;
      grid.set_captions(excel_captions_ty{
         {"Kennzahl", 26, Align::left, {}},
         {"Wert", 19, Align::right, {"#,##0.00"}}
      });
      // Only three scalar aggregates, not another domain model.
      std::vector<std::tuple<std::string, double>> metrics{
         {"Buchungen", static_cast<double>(std::get<0>(totals))},
         {"Soll/Haben EUR", std::get<1>(totals).Get()},
         {"Differenz EUR", std::get<2>(totals).Get()}
      };
      FillGrid(grid, metrics);
   }
   {
      auto grid = book.grid("Personen", xlsx::GridRegion{1, 1, 0, 0, false});
      grid.set_captions(PersonExcelCaptions());
      xlsx::ExcelGridStyle style;
      style.header({
         .background = xlsx::RGB{34, 79, 119},
         .foreground = xlsx::RGB{255, 255, 255},
         .bold = true
      }).row_pattern({
         {.background = xlsx::RGB{255, 255, 255}},
         {.background = xlsx::RGB{235, 242, 249}},
         {.background = xlsx::RGB{235, 242, 249}}
      });
      grid.set_style(style);
      FillPersonGrid(db, salutations, grid, false);
      if(grid.rows() != imported)
         throw std::runtime_error("Unfiltered Excel person selection is incomplete");
   }
   book.save();
   book.close();

   // Inspect persisted values and styles, not just the in-memory grid.
   OpenXLSX::XLDocument check;
   check.open(path.string());
   auto summarySheet = check.workbook().worksheet("Summenübersicht");
   auto peopleSheet = check.workbook().worksheet("Personen");
   if(summarySheet.findCell(1, 1).empty() || peopleSheet.findCell(1, 1).empty())
      throw std::runtime_error("Excel output is missing worksheet headings");
   if(imported > 0) {
      if(peopleSheet.findCell(static_cast<std::uint32_t>(imported + 1), 1).empty())
         throw std::runtime_error("Excel output is missing the last person");
      auto conditional = peopleSheet.conditionalFormats();
      if(conditional.count() != 1 ||
         conditional.conditionalFormatByIndex(0).cfRules().count() != 3)
         throw std::runtime_error("Dynamic person row formatting was not persisted");
      if(conditional.conditionalFormatByIndex(0).sqref() !=
         "A2:F" + std::to_string(imported + 1))
         throw std::runtime_error("Conditional format has incorrect data range");
      auto& styles = check.styles().cellFormats();
      auto const dateFmt = styles.cellFormatByIndex(peopleSheet.cell(2, 5).cellFormat());
      auto const amountFmt = styles.cellFormatByIndex(peopleSheet.cell(2, 6).cellFormat());
      if(!dateFmt.applyNumberFormat() || !amountFmt.applyNumberFormat() ||
         dateFmt.numberFormatId() == amountFmt.numberFormatId())
         throw std::runtime_error("Excel date and amount formats are not independent");
      auto& formats = check.styles().numberFormats();
      if(formats.numberFormatById(dateFmt.numberFormatId()).formatCode() != "dd.mm.yyyy" ||
         formats.numberFormatById(amountFmt.numberFormatId()).formatCode() != "#,##0.00 \"EUR\"")
         throw std::runtime_error("Excel caption formats were not persisted");
   }
   check.close();
   // A separate read of the persisted workbook, using the existing Grid Range
   // selector. The person table contains each booking amount once.
   auto const sum = SumExportedPersonAmounts(path, imported);
   if(std::abs(sum - std::get<1>(totals).Get()) > 0.01)
      throw std::runtime_error("Excel amount fold does not match SQL total");
   std::println("[PASS] Excel person amount fold = {:.2f} EUR", sum);
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

int main(int argc, char** argv) {
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

      auto const directory = ExecutableDirectory(argc > 0 ? argv[0] : "");
      auto const input = argc > 1
         ? std::filesystem::path{argv[1]}
         : directory / "bookings.csv";
      auto const count = ImportCsvBookings(db, input, salutations, kinds, accounts);
      auto const totals = ReadAndValidateTotals(db);
      if(std::get<0>(totals) != static_cast<long long>(count))
         throw std::runtime_error("Booking count mismatch");

      auto const salutationNames = SalutationNames(salutations);
      PrintReport(db, accountNames, salutationNames, totals);
      auto const report = ExportExcel(
         db, accountNames, salutationNames, totals, count, directory);
      transaction.CommitAndClose();

      std::cout << "[PASS] " << count << " bookings imported\n";
      std::cout << "[PASS] Excel contains all " << count << " persons (no WHERE)\n";
      std::cout << "[FILE] " << report.string() << '\n';
      return 0;
   }
   catch(std::exception const& error) {
      std::cerr << "[FAIL] " << error.what() << '\n';
      return 1;
   }
}
