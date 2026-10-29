-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Data-preserving domain-table rename.
-- Run in DeckKernel database as the owner of deckkernel_test tables.
-- This migration neither deletes rows nor changes IDs or foreign keys.
-- Use with psql -v ON_ERROR_STOP=1 -d DeckKernel -f <file>.
--
BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

DO $migration$
DECLARE
   aMapping record;
   strSource text;
   strDestination text;
BEGIN
   FOR aMapping IN
      SELECT * FROM (VALUES
         ('artifact_types', 'dom_artifact_types'),
         ('card_types', 'dom_card_types'),
         ('creatures_types', 'dom_creature_types'),
         ('land_types', 'dom_land_types'),
         ('rarities', 'dom_rarities'),
         ('supertypes', 'dom_supertypes')
      ) AS mappings(source_name, destination_name)
   LOOP
      strSource := aMapping.source_name;
      strDestination := aMapping.destination_name;
      -- Tolerate databases where the historical creatures_types typo
      -- has already been manually corrected.
      IF strSource = 'creatures_types'
         AND to_regclass('deckkernel_test.creatures_types') IS NULL
         AND to_regclass('deckkernel_test.creature_types') IS NOT NULL
      THEN
         strSource := 'creature_types';
      END IF;

      IF to_regclass(format('%I.%I', 'deckkernel_test', strSource)) IS NOT NULL
         AND to_regclass(format('%I.%I', 'deckkernel_test', strDestination)) IS NOT NULL
      THEN
         RAISE EXCEPTION 'Both %.% and %.% exist: manual reconciliation required',
            'deckkernel_test', strSource, 'deckkernel_test', strDestination;
      ELSIF to_regclass(format('%I.%I', 'deckkernel_test', strSource)) IS NOT NULL
      THEN
         EXECUTE format('ALTER TABLE %I.%I RENAME TO %I',
            'deckkernel_test', strSource, strDestination);
      ELSIF to_regclass(format('%I.%I', 'deckkernel_test', strDestination)) IS NULL
      THEN
         RAISE EXCEPTION 'Neither source %.% nor destination %.% exists',
            'deckkernel_test', strSource, 'deckkernel_test', strDestination;
      END IF;
   END LOOP;
END
$migration$;

ALTER TABLE deckkernel_test.dom_artifact_types
   ADD COLUMN IF NOT EXISTS description text,
   ADD COLUMN IF NOT EXISTS notice text;
ALTER TABLE deckkernel_test.dom_card_types
   ADD COLUMN IF NOT EXISTS description text,
   ADD COLUMN IF NOT EXISTS notice text;
ALTER TABLE deckkernel_test.dom_creature_types
   ADD COLUMN IF NOT EXISTS description text,
   ADD COLUMN IF NOT EXISTS notice text;
ALTER TABLE deckkernel_test.dom_land_types
   ADD COLUMN IF NOT EXISTS description text,
   ADD COLUMN IF NOT EXISTS notice text;
ALTER TABLE deckkernel_test.dom_rarities
   ADD COLUMN IF NOT EXISTS description text,
   ADD COLUMN IF NOT EXISTS notice text;
ALTER TABLE deckkernel_test.dom_supertypes
   ADD COLUMN IF NOT EXISTS description text,
   ADD COLUMN IF NOT EXISTS notice text;

COMMIT;

SELECT t.table_name, t.column_name, t.data_type, t.is_nullable
FROM information_schema.columns AS t
WHERE t.table_schema = 'deckkernel_test'
  AND t.table_name IN (
     'dom_artifact_types','dom_card_types','dom_creature_types',
     'dom_land_types','dom_rarities','dom_supertypes')
  AND t.column_name IN ('id','code','description','notice')
ORDER BY t.table_name, t.ordinal_position;
