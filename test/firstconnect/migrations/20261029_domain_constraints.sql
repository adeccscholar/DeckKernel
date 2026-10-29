-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Rename domain-table constraints in place, preserving all rows, identity
-- values, foreign keys, and the constraint-backed indexes.
--
-- Naming:
--   Primary key : pk_<table_name>
--   Code unique : uk_<table_name>_code
--
-- Re-entrant. Fails atomically when a required key/constraint is missing,
-- has unexpected columns or cannot be renamed.
-- This migration operates exclusively on the six dom_ tables.

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

DO $migration$
DECLARE
   strTable text;
   theTable regclass;
   iIdAttnum smallint;
   iCodeAttnum smallint;
   strExisting text;
   strExpected text;
BEGIN
   FOREACH strTable IN ARRAY ARRAY[
      'dom_artifact_types',
      'dom_card_types',
      'dom_creature_types',
      'dom_land_types',
      'dom_rarities',
      'dom_supertypes'
   ]
   LOOP
      theTable := to_regclass(format('%I.%I', 'deckkernel_test', strTable));
      IF theTable IS NULL THEN
         RAISE EXCEPTION 'Domain table deckkernel_test.% is missing', strTable;
      END IF;

      -- Lock each table while inspecting and renaming its constraints.
      EXECUTE format(
         'LOCK TABLE %I.%I IN ACCESS EXCLUSIVE MODE',
         'deckkernel_test', strTable);

      SELECT a.attnum INTO iIdAttnum
      FROM pg_attribute AS a
      WHERE a.attrelid = theTable
        AND a.attname = 'id'
        AND NOT a.attisdropped;
      IF iIdAttnum IS NULL THEN
         RAISE EXCEPTION 'Missing id column in %', theTable;
      END IF;

      SELECT a.attnum INTO iCodeAttnum
      FROM pg_attribute AS a
      WHERE a.attrelid = theTable
        AND a.attname = 'code'
        AND NOT a.attisdropped;
      IF iCodeAttnum IS NULL THEN
         RAISE EXCEPTION 'Missing code column in %', theTable;
      END IF;

      -- Do not guess the old constraint name: it may retain the pre-rename
      -- table name. Identify the existing primary key by its id column.
      SELECT c.conname INTO strExisting
      FROM pg_constraint AS c
      WHERE c.conrelid = theTable
        AND c.contype = 'p'
        AND c.conkey = ARRAY[iIdAttnum]::smallint[];
      IF strExisting IS NULL THEN
         RAISE EXCEPTION 'Expected primary key on %.id is missing', theTable;
      END IF;

      strExpected := 'pk_' || strTable;
      IF strExisting <> strExpected THEN
         EXECUTE format(
            'ALTER TABLE %I.%I RENAME CONSTRAINT %I TO %I',
            'deckkernel_test', strTable, strExisting, strExpected);
      END IF;

      -- Likewise, preserve and rename the unique constraint on code.
      SELECT c.conname INTO strExisting
      FROM pg_constraint AS c
      WHERE c.conrelid = theTable
        AND c.contype = 'u'
        AND c.conkey = ARRAY[iCodeAttnum]::smallint[];
      IF strExisting IS NULL THEN
         RAISE EXCEPTION 'Expected unique constraint on %.code is missing', theTable;
      END IF;

      strExpected := 'uk_' || strTable || '_code';
      IF strExisting <> strExpected THEN
         EXECUTE format(
            'ALTER TABLE %I.%I RENAME CONSTRAINT %I TO %I',
            'deckkernel_test', strTable, strExisting, strExpected);
      END IF;
   END LOOP;
END
$migration$;

-- Abort the entire migration if one of the expected names is absent.
DO $verify$
DECLARE
   strTable text;
   theTable regclass;
BEGIN
   FOREACH strTable IN ARRAY ARRAY[
      'dom_artifact_types',
      'dom_card_types',
      'dom_creature_types',
      'dom_land_types',
      'dom_rarities',
      'dom_supertypes'
   ]
   LOOP
      theTable := to_regclass(format('%I.%I', 'deckkernel_test', strTable));
      IF NOT EXISTS (
         SELECT 1 FROM pg_constraint AS c
         WHERE c.conrelid = theTable
           AND c.contype = 'p'
           AND c.conname = 'pk_' || strTable
      ) OR NOT EXISTS (
         SELECT 1 FROM pg_constraint AS c
         WHERE c.conrelid = theTable
           AND c.contype = 'u'
           AND c.conname = 'uk_' || strTable || '_code'
      ) THEN
         RAISE EXCEPTION 'Domain constraints not verified for %', theTable;
      END IF;
   END LOOP;
END
$verify$;

COMMIT;

SELECT cls.relname AS table_name, c.conname AS constraint_name,
       CASE c.contype WHEN 'p' THEN 'PRIMARY KEY' WHEN 'u' THEN 'UNIQUE' END AS constraint_type
FROM pg_constraint AS c
JOIN pg_class AS cls ON cls.oid = c.conrelid
JOIN pg_namespace AS ns ON ns.oid = cls.relnamespace
WHERE ns.nspname = 'deckkernel_test'
  AND cls.relname IN (
     'dom_artifact_types', 'dom_card_types', 'dom_creature_types',
     'dom_land_types', 'dom_rarities', 'dom_supertypes')
  AND c.contype IN ('p','u')
ORDER BY cls.relname, c.contype, c.conname;
