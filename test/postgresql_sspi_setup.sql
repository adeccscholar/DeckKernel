-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Run this script as a PostgreSQL administrator while connected to database "DeckKernel".
-- PostgreSQL schemas are database-local. Running this script while connected to another
-- database would create the schema there instead of in DeckKernel.
-- It creates the dedicated login role used by the functional test and gives that role
-- ownership of the isolated test schema. Authentication itself is configured in
-- pg_hba.conf and pg_ident.conf, not in SQL.

DO $do$
BEGIN
   IF current_database() <> 'DeckKernel' THEN
      RAISE EXCEPTION
         'DeckKernel setup must run in database "DeckKernel"; current database is "%".',
         current_database();
   END IF;
END
$do$;

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

-- Persistent image cache survives resets and full recreation of deckkernel_test.
CREATE SCHEMA IF NOT EXISTS deckkernel_cache AUTHORIZATION deckkernel_user;
ALTER SCHEMA deckkernel_cache OWNER TO deckkernel_user;

GRANT USAGE, CREATE ON SCHEMA deckkernel_test TO deckkernel_user;
GRANT USAGE, CREATE ON SCHEMA deckkernel_cache TO deckkernel_user;

-- Cache provisioning is performed by the administrator ONCE; the FirstConnection
-- application deliberately has no CREATE privilege on database "DeckKernel".
-- Preserve the URL-keyed original image blobs across model-table rebuilds.
CREATE TABLE IF NOT EXISTS deckkernel_cache.scryfall_images (
   url text PRIMARY KEY,
   image_data bytea NOT NULL,
   sha256 text NOT NULL CHECK (length(sha256) = 64),
   media_type text NOT NULL,
   cached_at timestamptz NOT NULL DEFAULT now(),
   CHECK (octet_length(image_data) > 0)
);

-- A previous setup may have created this table as postgres. Correct ownership
-- without dropping or truncating existing cache entries.
ALTER TABLE deckkernel_cache.scryfall_images OWNER TO deckkernel_user;

GRANT SELECT, INSERT, UPDATE, DELETE
   ON TABLE deckkernel_cache.scryfall_images TO deckkernel_user;




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

-- Optional diagnostic: schema/table owners and access rights.
SELECT n.nspname AS schema_name,
       pg_get_userbyid(n.nspowner) AS schema_owner,
       has_schema_privilege('deckkernel_user', n.oid, 'USAGE') AS can_use
FROM pg_namespace n WHERE n.nspname = 'deckkernel_cache';

SELECT c.relname AS table_name,
       pg_get_userbyid(c.relowner) AS table_owner,
       has_table_privilege('deckkernel_user', c.oid, 'SELECT') AS can_read,
       has_table_privilege('deckkernel_user', c.oid, 'INSERT') AS can_insert,
       has_table_privilege('deckkernel_user', c.oid, 'UPDATE') AS can_update,
       has_table_privilege('deckkernel_user', c.oid, 'DELETE') AS can_delete
FROM pg_class c
JOIN pg_namespace n ON n.oid = c.relnamespace
WHERE n.nspname = 'deckkernel_cache' AND c.relname = 'scryfall_images';
