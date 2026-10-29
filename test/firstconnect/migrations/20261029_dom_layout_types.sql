-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Initialize DeckKernel's layout domain from the Scryfall layout documentation:
-- https://scryfall.com/docs/api/layouts
-- The documented 24 codes are described, unknown codes are stored with
-- description and notice NULL. Existing rows and their IDs are immutable.
-- Applied independently of FirstConnection and safe to rerun.
--
-- This migration neither drops nor changes existing card or card-face data.

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

CREATE TABLE IF NOT EXISTS deckkernel_test.dom_layout_types (
   id integer GENERATED ALWAYS AS IDENTITY,
   code text NOT NULL,
   description text,
   notice text,

   CONSTRAINT pk_dom_layout_types PRIMARY KEY (id),
   CONSTRAINT uk_dom_layout_types_code UNIQUE (code)
);

-- Refuse to continue if an existing relation does not conform to the
-- required domain schema or named key constraints.
DO $check$
DECLARE
   theTable regclass := to_regclass('deckkernel_test.dom_layout_types');
BEGIN
   IF theTable IS NULL OR
      (SELECT count(*)
         FROM pg_attribute AS a
         WHERE a.attrelid = theTable
           AND NOT a.attisdropped
           AND ((a.attname = 'id' AND a.atttypid = 'integer'::regtype)
             OR (a.attname IN ('code','description','notice')
                 AND a.atttypid = 'text'::regtype))) <> 4
      OR NOT EXISTS (
         SELECT 1 FROM pg_constraint AS c
         JOIN pg_attribute AS a ON a.attrelid=c.conrelid
         WHERE c.conrelid=theTable AND c.contype='p'
           AND c.conname='pk_dom_layout_types'
           AND a.attname='id' AND c.conkey=ARRAY[a.attnum]::smallint[])
      OR NOT EXISTS (
         SELECT 1 FROM pg_constraint AS c
         JOIN pg_attribute AS a ON a.attrelid=c.conrelid
         WHERE c.conrelid=theTable AND c.contype='u'
           AND c.conname='uk_dom_layout_types_code'
           AND a.attname='code' AND c.conkey=ARRAY[a.attnum]::smallint[])
   THEN
      RAISE EXCEPTION 'deckkernel_test.dom_layout_types has an incompatible definition';
   END IF;
END
$check$;

COMMENT ON TABLE deckkernel_test.dom_layout_types IS
   'Scryfall card layout codes, documented at https://scryfall.com/docs/api/layouts';
COMMENT ON COLUMN deckkernel_test.dom_layout_types.description IS
   'Explanation of the documented layout; NULL for an as-yet-unknown code.';
COMMENT ON COLUMN deckkernel_test.dom_layout_types.notice IS
   'Optional editorial notes; not modified by automatic synchronization.';

-- Seed documented layouts, but never overwrite existing descriptions or IDs.
INSERT INTO deckkernel_test.dom_layout_types (code, description, notice)
VALUES
   ('normal', 'A standard Magic card layout with a single card face.', NULL),
   ('split', 'A card whose front is divided into two spell halves, each with its own characteristics.', NULL),
   ('flip', 'A single-sided card with two vertically opposed orientations that can be flipped.', NULL),
   ('transform', 'A double-faced card that can transform between its front and back faces.', NULL),
   ('modal_dfc', 'A modal double-faced card whose front or back face can be played, subject to applicable rules.', NULL),
   ('meld', 'A card used in a meld pair; the reverse side depicts part of the combined melded permanent.', NULL),
   ('leveler', 'A card with level up and a level-based text box showing different characteristics at different levels.', NULL),
   ('class', 'A Class enchantment with levels that can be unlocked to gain additional abilities.', NULL),
   ('case', 'A Case enchantment that can be solved by fulfilling a stated condition, unlocking an additional effect.', NULL),
   ('saga', 'A Saga enchantment with chapter abilities tied to lore counters.', NULL),
   ('adventure', 'A card combining an Adventure spell with another spell or permanent card face.', NULL),
   ('mutate', 'A card with the mutate mechanic and its distinctive alternate casting presentation.', NULL),
   ('prototype', 'A card showing both a normal cost and a prototype cost with alternate power and toughness.', NULL),
   ('battle', 'A Battle card layout with defense and rules associated with defending and defeating it.', NULL),
   ('planar', 'A Plane or Phenomenon card used in the Planechase multiplayer variant.', NULL),
   ('scheme', 'A Scheme card used by the archenemy in the Archenemy multiplayer variant.', NULL),
   ('vanguard', 'A Vanguard avatar card used to modify a player''s starting conditions or abilities.', NULL),
   ('token', 'A printed representation of a token object, rather than a conventional deck card.', NULL),
   ('double_faced_token', 'A double-faced token card presenting a token representation on each side.', NULL),
   ('emblem', 'A printed representation of an emblem created by a card effect; an emblem is not a card or permanent.', NULL),
   ('augment', 'An Augment card intended to combine with a Host creature in the Unstable environment.', NULL),
   ('host', 'A Host creature card that can combine with an Augment card in the Unstable environment.', NULL),
   ('art_series', 'A collectible art-series card displaying artwork, rather than a regular playable card.', NULL),
   ('reversible_card', 'A reversible double-faced card with independently identifiable, unrelated front and back faces.', NULL)
ON CONFLICT (code) DO NOTHING;

-- Preserve layout codes already present on cards before this migration.
-- Card faces may not exist yet on a fresh installation.
DO $backfill$
BEGIN
   IF to_regclass('deckkernel_test.scryfall_card_faces') IS NOT NULL THEN
      EXECUTE $sql$
         INSERT INTO deckkernel_test.dom_layout_types (code, description, notice)
         SELECT DISTINCT btrim(f.layout), NULL::text, NULL::text
         FROM deckkernel_test.scryfall_card_faces AS f
         WHERE f.layout IS NOT NULL AND btrim(f.layout) <> ''
         ON CONFLICT (code) DO NOTHING
      $sql$;
   END IF;
END
$backfill$;

COMMIT;

SELECT id, code, description, notice
FROM deckkernel_test.dom_layout_types
ORDER BY code;
