// SPDX-FileCopyrightText: 2021 - 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file db_test.h
\brief Integration tests for typed database output ranges, sinks, generated identities, and updates.

\details
Exercises the logical database abstraction with real tables and typed tuples. The scenarios verify
generated-value write-back, range-based inserts, sink-based updates, standard algorithms, final result
validation, and transaction handling across supported database dialects.

\par Architectural background
This header follows the architecture described by Volker Hillmann in
*Rethinking C++ (C++ neu denken)*, especially:
- "Tests as Architectural Proof".
- "Database as a Relational Source and Sink".
- "Database as Source, Transformation, and Sink".
- "Migration as an Architecture Test".

\see ../ARCHITECTURE.md#tests-as-architectural-proof

\version 1.0
\date 26.09.2026
\author Volker Hillmann (adecc Systemhaus GmbH)

\copyright Copyright © 2021 - 2026 adecc Systemhaus GmbH

\licenseblock{LicenseRef-PolyForm-Noncommercial-1.0.0}
This file is licensed under the PolyForm Noncommercial License 1.0.0.
Use, modification, and distribution are permitted only as defined by that license.
The complete and controlling terms are available at
https://polyformproject.org/licenses/noncommercial/1.0.0/.
Any use not permitted by that license requires separate permission or a separate
license from adecc Systemhaus GmbH.
\endlicenseblock

*/

#pragma once

#include "database.h"
#include "system_data_persistent.h"
#include "database_test_dialects.h"

#include <algorithm>
#include <numeric>
#include <exception>
#include <iostream>
#include <optional>
#include <print>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>
#include <array>

using test_row_ty = std::tuple<int, std::string, std::string, int>;


/*!
\brief Drops and recreates the test table
\details
   The table contains an identity primary key and three data columns.

\param db Logical database
\param os Output stream for test messages
*/
template <adecc::db::logical_database_type app_db>
void CreateTestTable(app_db& db, std::ostream& os,
                     adecc::test::database_dialect const aDialect = adecc::test::database_dialect::sql_server) {
   db.ExecuteCommand(std::string{ adecc::test::SimpleTestTableDdl(aDialect) });
   std::println(os, "Table TestDbNeu was recreated for {}.", adecc::test::DialectName(aDialect));
   }


/*!
\brief Inserts test data through OutputRange
\details
   The identity column \c Id is not bound by the INSERT. After each INSERT
   it is obtained from the backend and written into the output tuple.

\param db Logical database
\param os Output stream for test messages
\returns Inserted rows including the generated identity
\throw std::runtime_error if a test condition fails
*/
template <adecc::db::logical_database_type app_db>
std::vector<test_row_ty> InsertRows(app_db& db, std::ostream& os) {
   std::vector<test_row_ty> vecInput {
	  { 0, "Alpha", "Kommentar Alpha", 10 },
	  { 0, "Beta",  "Kommentar Beta",  20 },
	  { 0, "Gamma", "Kommentar Gamma", 30 }
	  };

   auto aRange = db.template MakeOutputRange<int, std::string, std::string, int>(
	  "insert into TestDbNeu "
	  "(Name, Kommentar, Wert) "
	  "values "
	  "(:Name, :Kommentar, :Wert)",
	  {
		 { "Id",        adecc::db_output_param_role::identity },
		 { "Name",      adecc::db_output_param_role::needed_value },
		 { "Kommentar", adecc::db_output_param_role::needed_value },
		 { "Wert",      adecc::db_output_param_role::needed_value }
	  }
   );

   std::vector<test_row_ty> vecInserted;

   for (auto const& tupRow : aRange(vecInput)) {
	  vecInserted.emplace_back(tupRow);
	  }

   if (vecInserted.size() != vecInput.size()) {
	  throw std::runtime_error {
		 "InsertRows: number of inserted rows does not match"
		 };
	  }

   for (std::size_t uIndex{}; uIndex < vecInserted.size(); ++uIndex) {
	  test_row_ty const& tupInput = vecInput[uIndex];
	  test_row_ty const& tupOutput = vecInserted[uIndex];

	  if (std::get<0>(tupOutput) <= 0) {
		 throw std::runtime_error {
			"InsertRows: generated identity was not written back"
			};
		 }

	  if (std::get<1>(tupOutput) != std::get<1>(tupInput)) {
		 throw std::runtime_error {
			"InsertRows: Name changed unexpectedly"
			};
		 }

	  if (std::get<2>(tupOutput) != std::get<2>(tupInput)) {
		 throw std::runtime_error {
			"InsertRows: comment column changed unexpectedly"
			};
		 }

	  if (std::get<3>(tupOutput) != std::get<3>(tupInput)) {
		 throw std::runtime_error {
			"InsertRows: value column changed unexpectedly"
			};
		 }

	  std::println(os, "Inserted: Id={}, Name={}, Comment={}, Value={}",
		 std::get<0>(tupOutput), std::get<1>(tupOutput), std::get<2>(tupOutput),
		 std::get<3>(tupOutput)
	    );
	  }

   return vecInserted;
   }


/*!
\brief Updates selected rows by directly invoking the sink with a range
\details
   The complete tuple range is used, while the SQL statement references only
   \c Id and \c Value. The \c Name and \c Comment fields are marked as
   \c may_be_missing and therefore may be absent from the SQL statement.

\param db Logical database
\param vecInserted Inserted rows including generated identities
\param os Output stream for test messages
\throw std::runtime_error if a test condition fails
*/
template <adecc::db::logical_database_type app_db>
void UpdateRowsByCall(
   app_db& db,
   std::vector<test_row_ty> const& vecInserted,
   std::ostringstream& os
) {
   if (vecInserted.size() < 3) {
	  throw std::runtime_error {
		 "UpdateRowsByCall: too few inserted rows"
		 };
	  }

   std::vector<test_row_ty> vecUpdate {
	  {
		 std::get<0>(vecInserted[0]),
		 std::get<1>(vecInserted[0]),
		 std::get<2>(vecInserted[0]),
		 110
	  },
	  {
		 std::get<0>(vecInserted[2]),
		 std::get<1>(vecInserted[2]),
		 std::get<2>(vecInserted[2]),
		 330
	  }
	  };

   auto aSink = db.template MakeOutputSink<int, std::string, std::string, int>(
	  "update TestDbNeu "
	  "set Wert = :Wert "
	  "where Id = :Id",
	  {
		 { "Id",        adecc::db_output_param_role::key },
		 { "Name",      adecc::db_output_param_role::may_be_missing },
		 { "Kommentar", adecc::db_output_param_role::may_be_missing },
		 { "Wert",      adecc::db_output_param_role::needed_value }
	  }
   );

   aSink(vecUpdate);

   for (auto const& tupRow : vecUpdate) {
	  std::println(
		 os,
		 "Updated by call: Id={}, Value={}",
		 std::get<0>(tupRow),
		 std::get<3>(tupRow)
	  );
	  }
   }


/*!
\brief Updates one row through \c OutputSink::operator=
\details
   This function tests assignment of a single tuple value to the sink.

\param db Logical database
\param tupInserted Inserted row including generated identity
\param os Output stream for test messages
*/
template <adecc::db::logical_database_type app_db>
void UpdateOneRowByAssignment(app_db& db, test_row_ty const& tupInserted,
                              std::ostream& out) {
   test_row_ty tupUpdate { std::get<0>(tupInserted),
	                        std::get<1>(tupInserted),
	                        std::get<2>(tupInserted),
	                        111
	                      };

   auto aSink = db.template MakeOutputSink<int, std::string, std::string, int>(
	  "update TestDbNeu "
	  "set Wert = :Wert "
	  "where Id = :Id",
	  {
		 { "Id",        adecc::db_output_param_role::key },
		 { "Name",      adecc::db_output_param_role::may_be_missing },
		 { "Kommentar", adecc::db_output_param_role::may_be_missing },
		 { "Wert",      adecc::db_output_param_role::needed_value }
	  }
   );

   aSink = tupUpdate;

   std::println(out, "Updated by tuple assignment: Id={}, Value={}",
	             std::get<0>(tupUpdate), std::get<3>(tupUpdate));
   }


/*!
\brief Updates multiple rows through \c OutputSink::operator=
\details
   This function tests assignment of a complete range to the sink.

\param db Logical database
\param vecInserted Inserted rows including generated identities
\param os Output stream for test messages
\throw std::runtime_error if a test condition fails
*/
template <adecc::db::logical_database_type app_db>
void UpdateRowsByRangeAssignment(app_db& db, std::vector<test_row_ty> const& vecInserted,
                                 std::ostream& out) {
   if (vecInserted.size() < 3) {
	  throw std::runtime_error { 
		   "UpdateRowsByRangeAssignment: The number of inserted records is too low."
		 };
	  }

   std::vector<test_row_ty> vecUpdate {	 
		{
		     std::get<0>(vecInserted[0]),
		     std::get<1>(vecInserted[0]),
		     std::get<2>(vecInserted[0]),
		     112
	     },
	     {
		     std::get<0>(vecInserted[1]),
		     std::get<1>(vecInserted[1]),
		     std::get<2>(vecInserted[1]),
		     222
	     }
	   };

   auto aSink = db.template MakeOutputSink<int, std::string, std::string, int>(
	  "update TestDbNeu "
	  "set Wert = :Wert "
	  "where Id = :Id",
	  {
		 { "Id",        adecc::db_output_param_role::key },
		 { "Name",      adecc::db_output_param_role::may_be_missing },
		 { "Kommentar", adecc::db_output_param_role::may_be_missing },
		 { "Wert",      adecc::db_output_param_role::needed_value }
	  }
   );

   aSink = vecUpdate;

   for (auto const& tupRow : vecUpdate) {
	  std::println(out, "Updated by range assignment: Id={}, Value={}",
		            std::get<0>(tupRow), std::get<3>(tupRow));
	  }
   }


/*!
\brief Updates multiple rows through \c std::ranges::copy
\details
   This function tests the sink output iterator.

\param db Logical database
\param vecInserted Inserted rows including generated identities
\param os Output stream for test messages
\throw std::runtime_error if a test condition fails
*/
template <adecc::db::logical_database_type app_db>
void UpdateRowsByCopy(app_db& db, std::vector<test_row_ty> const& vecInserted,
                      std::ostream& out) {
   if (vecInserted.size() < 3) {
	  throw std::runtime_error {
		 "UpdateRowsByCopy: The number of inserted records is too low."
		 };
	  }

   std::vector<test_row_ty> vecUpdate {
	    {
		   std::get<0>(vecInserted[1]),
		   std::get<1>(vecInserted[1]),
		   std::get<2>(vecInserted[1]),
		   223
	    },
	    {
		   std::get<0>(vecInserted[2]),
		   std::get<1>(vecInserted[2]),
		   std::get<2>(vecInserted[2]),
		   333
	    }
	  };

   auto aSink = db.template MakeOutputSink<int, std::string, std::string, int>(
	  "update TestDbNeu "
	  "set Wert = :Wert "
	  "where Id = :Id",
	    {
		   { "Id",        adecc::db_output_param_role::key },
		   { "Name",      adecc::db_output_param_role::may_be_missing },
		   { "Kommentar", adecc::db_output_param_role::may_be_missing },
		   { "Wert",      adecc::db_output_param_role::needed_value }
	    }
     );

   std::ranges::copy(vecUpdate, aSink.OutputIterator());

   for (auto const& tupRow : vecUpdate) {
	  std::println(out, "Updated by std::ranges::copy: Id={}, Value={}",
		            std::get<0>(tupRow), std::get<3>(tupRow));
	  }
   }


/*!
\brief Reads the test table for diagnostic output
\details
   The function writes all rows ordered by \c Id to the output stream.

\param db Logical database
\param os Output stream for test messages
*/
template <adecc::db::logical_database_type app_db>
void PrintRows(app_db& db, std::ostream& out) {
   auto rngRows = db.template Execute<int, std::string, std::optional<std::string>, int>(
	  "select Id, Name, Kommentar, Wert "
	  "from TestDbNeu "
	  "order by Id"
   );

   std::println(out, "");
   std::println(out, "Current rows:");

   for (auto const& [iId, strName, optKommentar, iWert] : rngRows) {
	  std::println(out, "Id={}, Name={}, Comment={}, Value={}",
		            iId, strName, optKommentar.value_or("<NULL>"), iWert);
	  }
   }


/*!
\brief Verifies the final result of the INSERT and UPDATE operations
\details
   Exactly three rows are expected. After all update variants, the
   final values must be 112, 223, and 333.

\param db Logical database
\param os Output stream for test messages
\throw std::runtime_error if a test condition fails
*/
template <adecc::db::logical_database_type app_db>
void VerifyRows(app_db& db, std::ostream& out) {
   auto rngRows = db.template Execute<int, std::string, std::optional<std::string>, int>(
	  "select Id, Name, Kommentar, Wert "
	  "from TestDbNeu "
	  "order by Id"
	  );

   std::vector<std::tuple<int, std::string, std::optional<std::string>, int>> vecRows;

   for (auto const& tupRow : rngRows) {
	  vecRows.emplace_back(tupRow);
	  }

   if (vecRows.size() != 3) {
	  throw std::runtime_error {
		 "VerifyRows: Exactly three records were not found"
		 };
	  }

   if (std::get<3>(vecRows[0]) != 112) {
	  throw std::runtime_error {
		 "VerifyRows: The first line does not have the expected value of 112"
		 };
	  }

   if (std::get<3>(vecRows[1]) != 223) {
	  throw std::runtime_error {
		 "VerifyRows: The second row does not contain the expected value of 223"
		 };
	  }

   if (std::get<3>(vecRows[2]) != 333) {
	  throw std::runtime_error {
		 "VerifyRows: The third row does not have the expected value of 333"
		 };
	  }

   std::println(out, "VerifyRows: all checks passed.");
   }


/*!
\brief Runs the complete ExecuteCommand, OutputRange, and OutputSink test
\details
   The test creates a real \c TestDbNeu table, inserts multiple rows
   with identity write-back, and then updates selected rows
   through several sink usage forms.

\param db Logical database
\returns Text log of the test
*/
template <adecc::db::logical_database_type app_db>
std::string RunOutputTests(app_db& db,
                           adecc::test::database_dialect const aDialect = adecc::test::database_dialect::sql_server) {
   std::ostringstream os;

   auto aTransaction = db.Transaction();

   CreateTestTable(db, os, aDialect);

   std::vector<test_row_ty> vecInserted = InsertRows(db, os);

   UpdateRowsByCall(db, vecInserted, os);
   UpdateOneRowByAssignment(db, vecInserted[0], os);
   UpdateRowsByRangeAssignment(db, vecInserted, os);
   UpdateRowsByCopy(db, vecInserted, os);

   VerifyRows(db, os);

   PrintRows(db, os);

   aTransaction.CommitAndClose();

   std::println(os, "");
   std::println(os, "Output tests completed successfully.");

   return os.str();
   }

