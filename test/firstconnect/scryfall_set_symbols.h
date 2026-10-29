// SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
// SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
#pragma once
/** \file scryfall_set_symbols.h
    \brief Non-destructive, resumable offline storage for Scryfall set SVGs.
    \details Import is separate from --all and from card artwork downloads.
             No set/card rows are deleted or recreated.
*/
namespace deckkernel::test {
struct SetSymbolImportOptions {
   bool probeOnly{};
   bool offlineOnly{};
};
void SyncSetSymbols(SetSymbolImportOptions options={});
}
