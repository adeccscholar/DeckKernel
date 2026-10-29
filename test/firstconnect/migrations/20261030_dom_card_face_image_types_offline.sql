-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Extend the existing card-face image type domain with an offline selection flag.
-- All twelve codes occur in the observed 2026 Scryfall image_uris object.
-- Six already-imported variants are enabled initially. Six additional keys
-- (five WebP renditions and the JPEG normal alias) remain disabled until the
-- URL-storage and download pipeline supports them.
-- No card faces, image URLs or cached image data are touched.
--
-- This migration is re-entrant. Values already assigned to offline are left
-- unchanged on reruns so later manual selections are not reset.
--
-- Prerequisite: 20261029_dom_card_face_image_types.sql
-- Execute: psql -X -v ON_ERROR_STOP=1 -d DeckKernel -f test/firstconnect/migrations/20261030_dom_card_face_image_types_offline.sql

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

ALTER TABLE deckkernel_test.dom_card_face_image_types
   ADD COLUMN IF NOT EXISTS offline boolean;

-- Preserve existing catalog rows, IDs, descriptions, notices and resolutions.
-- The legacy six variants are created with offline=TRUE only if absent.
INSERT INTO deckkernel_test.dom_card_face_image_types
   (code, description, notice, resolution, offline)
VALUES
   ('art_crop',
    'A rectangular crop containing primarily the card artwork, without the full card frame or rules text. Cropping and dimensions vary with card layout.',
    NULL, NULL, TRUE),
   ('border_crop',
    'A JPEG of the full card with the rounded corners and most of the outer border cropped away, intended for rectangular displays.',
    NULL, '480x680', TRUE),
   ('large',
    'A large JPEG rendition of the full card, suitable for detailed on-screen viewing.',
    NULL, '672x936', TRUE),
   ('normal',
    'A medium-sized JPEG rendition of the full card, intended for normal on-screen card display.',
    NULL, '488x680', TRUE),
   ('png',
    'A high-resolution PNG rendition of the full card, with transparent rounded corners, suitable for high-quality display.',
    NULL, '745x1040', TRUE),
   ('small',
    'A small JPEG rendition of the full card, optimized for thumbnails and card lists.',
    NULL, '146x204', TRUE)
ON CONFLICT (code) DO NOTHING;

-- Additional Scryfall image_uris keys observed in the latest bulk export.
-- thumb, grid, display, art, crop are WebP; jpeg is a JPEG alias of normal
-- in the observed sample. Nominal dimensions are not fixed here.
-- They remain offline=FALSE until the importer is explicitly extended.
INSERT INTO deckkernel_test.dom_card_face_image_types
   (code, description, notice, resolution, offline)
VALUES
   ('grid',
    'A Scryfall WebP card image for compact grid and gallery views, supplied by image_uris.grid. The image dimensions may vary.',
    NULL, NULL, FALSE),
   ('display',
    'A Scryfall WebP card image intended for detailed on-screen display, supplied by image_uris.display. The image dimensions may vary.',
    NULL, NULL, FALSE),
   ('art',
    'A Scryfall WebP rendition focused on the card artwork, supplied by image_uris.art. This is distinct from the JPEG art_crop variant.',
    NULL, NULL, FALSE),
   ('jpeg',
    'A Scryfall JPEG card image supplied by image_uris.jpeg. In the observed bulk export, it is the same image URL as image_uris.normal.',
    NULL, NULL, FALSE),
   ('thumb',
    'A Scryfall WebP thumbnail card image for small previews and lists, supplied by image_uris.thumb. The image dimensions may vary.',
    NULL, NULL, FALSE),
   ('crop',
    'A Scryfall WebP cropped card image supplied by image_uris.crop. The crop is distinct from the JPEG art_crop and border_crop variants.',
    NULL, NULL, FALSE)
ON CONFLICT (code) DO NOTHING;

-- The preexisting six rows had no offline column. Set TRUE only where the
-- new column is still NULL; retain any subsequent user choices on reruns.
UPDATE deckkernel_test.dom_card_face_image_types
SET offline = TRUE
WHERE code IN ('art_crop', 'border_crop', 'large', 'normal', 'png', 'small')
  AND offline IS NULL;

-- Unknown and future codes are disabled unless explicitly enabled later.
UPDATE deckkernel_test.dom_card_face_image_types
SET offline = FALSE
WHERE offline IS NULL;

ALTER TABLE deckkernel_test.dom_card_face_image_types
   ALTER COLUMN offline SET DEFAULT FALSE,
   ALTER COLUMN offline SET NOT NULL;

COMMENT ON COLUMN deckkernel_test.dom_card_face_image_types.offline IS
   'Select this image variant for future offline acquisition; TRUE does not prove an image is already cached.';

-- Fail atomically if any expected catalog value is missing.
DO $validate$
BEGIN
   IF (
      SELECT count(*)
      FROM deckkernel_test.dom_card_face_image_types
      WHERE code IN (
         'art_crop','border_crop','large','normal','png','small',
         'grid','display','art','jpeg','thumb','crop')
   ) <> 12 THEN
      RAISE EXCEPTION 'Expected 12 image type codes after migration';
   END IF;
END
$validate$;

COMMIT;

SELECT id, code, resolution, offline, description, notice
FROM deckkernel_test.dom_card_face_image_types
ORDER BY code;
