// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
#include "scryfall_database.h"

namespace deckkernel::test {
   /** \brief Download and persist Scryfall artwork images into PostgreSQL bytea.
       \details Image acquisition is resumable and separate from printing import.
       \param aDatabase Established DeckKernel database context.
       \throw std::runtime_error for transport or storage errors.
   */
   void ImportImages(postgres_database_ty const& aDatabase);
}
