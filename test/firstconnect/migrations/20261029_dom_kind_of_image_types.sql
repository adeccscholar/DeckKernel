-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Add the six Scryfall image renditions as an independent domain table.
-- No existing relations, URL columns, foreign keys or records are modified.
-- Exact mixed-case table/constraint names require quoted identifiers.
-- Re-entrant: existing codes and their metadata are never overwritten.
--
-- Apply to the DeckKernel database after the deckkernel_test schema exists.
-- psql -X -v ON_ERROR_STOP=1 -d DeckKernel -f test/firstconnect/migrations/20261029_dom_kind_of_image_types.sql

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

CREATE TABLE IF NOT EXISTS deckkernel_test."dom_KindOfImage_Types" (
   id integer GENERATED ALWAYS AS IDENTITY,
   code text NOT NULL,
   description text,
   notice text,
   resolution text,

   CONSTRAINT "pk_dom_KindOfImage_Types" PRIMARY KEY (id),
   CONSTRAINT "uk_dom_KindOfImage_Types_code" UNIQUE (code)
);

-- Do not accept a preexisting, incompatible relation silently.
DO $verify$
DECLARE
   theTable regclass := to_regclass('deckkernel_test."dom_KindOfImage_Types"');
BEGIN
   IF theTable IS NULL THEN
      RAISE EXCEPTION 'Image rendition domain table is missing';
   END IF;

   IF (SELECT count(*)
       FROM pg_attribute AS a
       WHERE a.attrelid = theTable AND NOT a.attisdropped
         AND ((a.attname = 'id' AND a.atttypid = 'integer'::regtype)
           OR (a.attname IN ('code', 'description', 'notice', 'resolution')
               AND a.atttypid = 'text'::regtype))) <> 5 THEN
      RAISE EXCEPTION 'Unexpected image rendition domain column definition';
   END IF;

   IF NOT EXISTS (
      SELECT 1
      FROM pg_constraint AS c
      JOIN pg_attribute AS a ON a.attrelid = c.conrelid
      WHERE c.conrelid = theTable AND c.contype = 'p'
        AND c.conname = 'pk_dom_KindOfImage_Types'
        AND a.attname = 'id' AND c.conkey = ARRAY[a.attnum]::smallint[]
   ) OR NOT EXISTS (
      SELECT 1
      FROM pg_constraint AS c
      JOIN pg_attribute AS a ON a.attrelid = c.conrelid
      WHERE c.conrelid = theTable AND c.contype = 'u'
        AND c.conname = 'uk_dom_KindOfImage_Types_code'
        AND a.attname = 'code' AND c.conkey = ARRAY[a.attnum]::smallint[]
   ) THEN
      RAISE EXCEPTION 'Unexpected primary/unique constraint definitions';
   END IF;
END
$verify$;

COMMENT ON TABLE deckkernel_test."dom_KindOfImage_Types"
   IS 'Scryfall image URI variants; intended as a domain for a later card-face-to-image-URL relation.';
COMMENT ON COLUMN deckkernel_test."dom_KindOfImage_Types".resolution
   IS 'Nominal pixel dimensions as width x height; NULL for variable-sized crops. Not a guarantee for every image.';

INSERT INTO deckkernel_test."dom_KindOfImage_Types"
   (code, description, notice, resolution)
VALUES
   ('art_crop',
    'A rectangular crop containing primarily the card artwork, without the full card frame or rules text. Cropping and dimensions vary with card layout.',
    NULL, NULL),
   ('border_crop',
    'A JPEG of the full card with the rounded corners and most of the outer border cropped away, intended for rectangular displays.',
    NULL, '480x680'),
   ('large',
    'A large JPEG rendition of the full card, suitable for detailed on-screen viewing.',
    NULL, '672x936'),
   ('normal',
    'A medium-sized JPEG rendition of the full card, intended for normal on-screen card display.',
    NULL, '488x680'),
   ('png',
    'A high-resolution PNG rendition of the full card, with transparent rounded corners, suitable for high-quality display.',
    NULL, '745x1040'),
   ('small',
    'A small JPEG rendition of the full card, optimized for thumbnails and card lists.',
    NULL, '146x204')
ON CONFLICT ON CONSTRAINT "uk_dom_KindOfImage_Types_code" DO NOTHING;

-- Every defined image type must exist after insertion.
DO $verify_rows$
BEGIN
   IF (
      SELECT count(*)
      FROM deckkernel_test."dom_KindOfImage_Types"
      WHERE code IN ('art_crop', 'border_crop', 'large', 'normal', 'png', 'small')
   ) <> 6 THEN
      RAISE EXCEPTION 'Not all six image rendition codes exist';
   END IF;
END
$verify_rows$;

COMMIT;

SELECT id, code, resolution, description, notice
FROM deckkernel_test."dom_KindOfImage_Types"
ORDER BY code;
