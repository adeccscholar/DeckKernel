-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Run this script as a PostgreSQL administrator while connected to database "DeckKernel".
-- It creates the dedicated login role used by the functional test and gives that role
-- ownership of the isolated test schema. Authentication itself is configured in
-- pg_hba.conf and pg_ident.conf, not in SQL.

DO $do$
BEGIN
   IF NOT EXISTS (
      SELECT 1
      FROM pg_catalog.pg_roles
      WHERE rolname = 'deckkernel_user'
   ) THEN
      CREATE ROLE deckkernel_user LOGIN;
   ELSE
      ALTER ROLE deckkernel_user LOGIN;
   END IF;
END
$do$;

GRANT CONNECT ON DATABASE "DeckKernel" TO deckkernel_user;

CREATE SCHEMA IF NOT EXISTS deckkernel_test AUTHORIZATION deckkernel_user;
ALTER SCHEMA deckkernel_test OWNER TO deckkernel_user;

GRANT USAGE, CREATE ON SCHEMA deckkernel_test TO deckkernel_user;


-- Existing test tables may have been created earlier by an administrator.
-- Transfer them to the functional-test role so CREATE INDEX, TRUNCATE and
-- subsequent schema-local DDL run with the same ownership model as fresh tables.
DO $do$
BEGIN
   IF to_regclass('deckkernel_test.scryfall_cards') IS NOT NULL THEN
      ALTER TABLE deckkernel_test.scryfall_cards OWNER TO deckkernel_user;
   END IF;

   IF to_regclass('deckkernel_test.scryfall_sets') IS NOT NULL THEN
      ALTER TABLE deckkernel_test.scryfall_sets OWNER TO deckkernel_user;
   END IF;
END
$do$;
