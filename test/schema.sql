-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0

BEGIN;

CREATE SCHEMA IF NOT EXISTS deckkernel_test;

CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_sets (
   id   text PRIMARY KEY,
   code text NOT NULL UNIQUE,
   name text NOT NULL
);

CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_cards (
   id          text PRIMARY KEY,
   oracle_id   text NULL,
   name        text NOT NULL,
   set_id      text NOT NULL
               REFERENCES deckkernel_test.scryfall_sets(id),
   released_at date NOT NULL
);

CREATE INDEX IF NOT EXISTS ix_scryfall_cards_oracle_id
   ON deckkernel_test.scryfall_cards(oracle_id);

CREATE INDEX IF NOT EXISTS ix_scryfall_cards_released
   ON deckkernel_test.scryfall_cards(released_at DESC);

COMMIT;
