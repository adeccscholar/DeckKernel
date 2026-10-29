-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Independent finish domain for Scryfall printing variants.
-- Scryfall card property: finishes (nonfoil, foil, etched, and special glossy).
-- Existing card and price data is not modified. Printing-to-finish links
-- will be introduced in a separate migration.
--
-- Run on database DeckKernel after deckkernel_test has been provisioned:
-- psql -X -v ON_ERROR_STOP=1 -d DeckKernel -f test/firstconnect/migrations/20261029_dom_card_finish_types.sql

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

CREATE TABLE IF NOT EXISTS deckkernel_test.dom_card_finish_types (
   id integer GENERATED ALWAYS AS IDENTITY,
   code text NOT NULL,
   description text,
   notice text,

   CONSTRAINT pk_dom_card_finish_types PRIMARY KEY (id),
   CONSTRAINT uk_dom_card_finish_types_code UNIQUE (code)
);

-- Do not silently insert into a conflicting pre-existing table definition.
DO $verify$
DECLARE
   theTable regclass := to_regclass('deckkernel_test.dom_card_finish_types');
BEGIN
   IF theTable IS NULL
      OR (SELECT count(*)
          FROM pg_attribute a
          WHERE a.attrelid = theTable
            AND NOT a.attisdropped
            AND ((a.attname = 'id' AND a.atttypid = 'integer'::regtype)
              OR (a.attname IN ('code', 'description', 'notice')
                  AND a.atttypid = 'text'::regtype))) <> 4
      OR NOT EXISTS (
         SELECT 1 FROM pg_constraint c
         JOIN pg_attribute a ON a.attrelid = c.conrelid
         WHERE c.conrelid = theTable AND c.contype = 'p'
           AND c.conname = 'pk_dom_card_finish_types'
           AND a.attname = 'id'
           AND c.conkey = ARRAY[a.attnum]::smallint[])
      OR NOT EXISTS (
         SELECT 1 FROM pg_constraint c
         JOIN pg_attribute a ON a.attrelid = c.conrelid
         WHERE c.conrelid = theTable AND c.contype = 'u'
           AND c.conname = 'uk_dom_card_finish_types_code'
           AND a.attname = 'code'
           AND c.conkey = ARRAY[a.attnum]::smallint[])
   THEN
      RAISE EXCEPTION 'deckkernel_test.dom_card_finish_types has an incompatible definition';
   END IF;
END
$verify$;

COMMENT ON TABLE deckkernel_test.dom_card_finish_types IS
   'Card printing finish types; not the Scryfall price field or the promo treatment.';
COMMENT ON COLUMN deckkernel_test.dom_card_finish_types.description IS
   'Human-readable finish explanation; automatic future catalog synchronization must not overwrite authored text.';
COMMENT ON COLUMN deckkernel_test.dom_card_finish_types.notice IS
   'Optional editorial information, initially NULL.';

-- Add missing codes while preserving all existing IDs, descriptions and notes.
INSERT INTO deckkernel_test.dom_card_finish_types
   (code, description, notice)
VALUES
   ('nonfoil',
    'A printing without a foil or reflective metallic finish. Availability of this finish is a property of the specific card printing.',
    NULL),
   ('foil',
    'A printing with a traditional reflective foil finish. It is distinct from etched foil and does not imply a particular rarity.',
    NULL),
   ('etched',
    'A printing with an etched-foil treatment, distinct from the traditional foil finish. Availability is specific to the printing.',
    NULL),
   ('glossy',
    'A specialty printing with a glossy surface treatment. Scryfall distinguishes glossy from nonfoil, traditional foil, and etched foil.',
    NULL)
ON CONFLICT (code) DO NOTHING;

COMMIT;

SELECT id, code, description, notice
FROM deckkernel_test.dom_card_finish_types
ORDER BY code;
