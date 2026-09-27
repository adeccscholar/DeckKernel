// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

/**
\file scryfall_database.h
\brief Lesson boundary for connecting to PostgreSQL and storing parsed Scryfall data.
*/

#pragma once

#include "scryfall_parse.h"

#include "database.h"
#include "pqxx_database.h"

namespace deckkernel::test {

using postgres_database_ty = adecc::db::logical_database<
   adecc::db::postgres::postgres_database,
   adecc::db::postgres::fw_query
   >;

postgres_database_ty OpenDatabase();

void StoreProcess(
   postgres_database_ty& aDatabase,
   ParsedBulkData const& aParsed
);

} // namespace deckkernel::test
