-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Non-destructive domain tables for Scryfall legalities.
-- Format codes are taken from the provided 2026 Scryfall bulk-file object.
-- Four legality states are from Scryfall's Cards API contract.
-- No oracle-card relation is created and no existing cards are changed.
-- Existing IDs, descriptions and notices are never overwritten.

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

CREATE TABLE IF NOT EXISTS deckkernel_test.dom_format_types (
   id integer GENERATED ALWAYS AS IDENTITY,
   code text NOT NULL,
   description text,
   notice text,

   CONSTRAINT pk_dom_format_types PRIMARY KEY (id),
   CONSTRAINT uk_dom_format_types_code UNIQUE (code)
);

CREATE TABLE IF NOT EXISTS deckkernel_test.dom_legality_types (
   id integer GENERATED ALWAYS AS IDENTITY,
   code text NOT NULL,
   description text,
   notice text,

   CONSTRAINT pk_dom_legality_types PRIMARY KEY (id),
   CONSTRAINT uk_dom_legality_types_code UNIQUE (code)
);

-- Verify the required columns and constraint names on preexisting tables.
DO $verify$
DECLARE
   strTable text;
   theTable regclass;
BEGIN
   FOREACH strTable IN ARRAY ARRAY['dom_format_types','dom_legality_types']
   LOOP
      theTable := to_regclass(format('deckkernel_test.%I',strTable));
      IF theTable IS NULL OR
         (SELECT count(*) FROM pg_attribute a
          WHERE a.attrelid=theTable AND NOT a.attisdropped AND
           ((a.attname='id' AND a.atttypid='integer'::regtype)
            OR (a.attname IN ('code','description','notice')
                AND a.atttypid='text'::regtype))) <> 4 OR
         NOT EXISTS (
            SELECT 1 FROM pg_constraint c
            JOIN pg_attribute a ON a.attrelid=c.conrelid
            WHERE c.conrelid=theTable AND c.contype='p'
              AND c.conname='pk_' || strTable
              AND a.attname='id' AND c.conkey=ARRAY[a.attnum]::smallint[]) OR
         NOT EXISTS (
            SELECT 1 FROM pg_constraint c
            JOIN pg_attribute a ON a.attrelid=c.conrelid
            WHERE c.conrelid=theTable AND c.contype='u'
              AND c.conname='uk_' || strTable || '_code'
              AND a.attname='code' AND c.conkey=ARRAY[a.attnum]::smallint[])
      THEN
         RAISE EXCEPTION 'Incompatible domain table definition: %',strTable;
      END IF;
   END LOOP;
END
$verify$;

COMMENT ON TABLE deckkernel_test.dom_format_types IS
   'Scryfall legality format identifiers; individual card eligibility is time-sensitive.';
COMMENT ON TABLE deckkernel_test.dom_legality_types IS
   'Scryfall card legality states; banned and restricted differ from not_legal.';

-- Insert documented format identifiers only if absent.
INSERT INTO deckkernel_test.dom_format_types (code,description,notice)
VALUES
   ('standard', 'Standard: a rotating constructed format using the current Standard-legal card pool.', NULL),
   ('future', 'Future Standard: projected Standard legality based on upcoming releases and rotation.', NULL),
   ('historic', 'Historic: a non-rotating Magic Arena format including digital-only and rebalanced cards.', NULL),
   ('timeless', 'Timeless: a non-rotating Magic Arena format with an extensive card pool and its own restrictions.', NULL),
   ('gladiator', 'Gladiator: a 100-card singleton Magic Arena constructed format.', NULL),
   ('pioneer', 'Pioneer: a non-rotating constructed format with a card pool beginning with Return to Ravnica.', NULL),
   ('modern', 'Modern: a non-rotating constructed format whose eligible sets begin with Eighth Edition.', NULL),
   ('legacy', 'Legacy: an Eternal constructed format with its own banned-card list.', NULL),
   ('pauper', 'Pauper: a constructed format based on cards printed at common rarity, subject to a banned list.', NULL),
   ('vintage', 'Vintage: an Eternal format that distinguishes banned and restricted cards.', NULL),
   ('penny', 'Penny Dreadful: a community format with seasonally changing eligibility based on Magic Online prices.', NULL),
   ('commander', 'Commander: a singleton multiplayer format using a commander, color identity and a banned list.', NULL),
   ('oathbreaker', 'Oathbreaker: a singleton format using a planeswalker Oathbreaker and its signature spell.', NULL),
   ('standardbrawl', 'Standard Brawl: a singleton Brawl variant using the rotating Standard card pool.', NULL),
   ('brawl', 'Brawl: a commander-style singleton format tracked separately from Standard Brawl.', NULL),
   ('competitivebrawl', 'Competitive Brawl: a separately tracked competitive variant of Brawl.', NULL),
   ('alchemy', 'Alchemy: a rotating Magic Arena format using digital-only and rebalanced cards as well as eligible paper cards.', NULL),
   ('paupercommander', 'Pauper Commander: a Commander variant using common cards and an eligible uncommon creature commander.', NULL),
   ('duel', 'Duel Commander: a one-versus-one competitive Commander variant.', NULL),
   ('oldschool', 'Old School (93/94): a constructed format centered on the earliest Magic releases.', NULL),
   ('premodern', 'Premodern: a constructed format using an early historical card pool, from Fourth Edition through Scourge.', NULL),
   ('predh', 'PreDH: a Commander variant using a historically limited card pool predating modern Commander releases.', NULL),
   ('tlr', 'Tiny Leaders Reborn: a separately tracked singleton format with low mana-value restrictions.', NULL)
ON CONFLICT (code) DO NOTHING;

INSERT INTO deckkernel_test.dom_legality_types (code,description,notice)
VALUES
   ('legal', 'The card is permitted in the format under the usual deck-construction rules.', NULL),
   ('not_legal', 'The card does not qualify for the format''s eligible card pool; this is distinct from being banned.', NULL),
   ('banned', 'The card is explicitly prohibited in the format by its ban list or governing rules.', NULL),
   ('restricted', 'The card is permitted with a reduced copy limit under the format''s restricted list.', NULL)
ON CONFLICT (code) DO NOTHING;

COMMIT;

SELECT id,code,description,notice FROM deckkernel_test.dom_format_types ORDER BY code;
SELECT id,code,description,notice FROM deckkernel_test.dom_legality_types ORDER BY code;
