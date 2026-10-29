-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Extend the existing card-face image type domain with an offline selection flag.
-- The six existing Scryfall image_uris keys are enabled initially. The six
-- reserved DeckKernel variants are disabled until their source/URL mapping
-- is implemented. No card faces, image URLs or cached image data are touched.
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

-- Additional, prospective DeckKernel image rendition codes. These are NOT
-- documented properties of Scryfall's standard Card.image_uris object.
-- Resolution is unspecified until an actual image source is integrated.
INSERT INTO deckkernel_test.dom_card_face_image_types
   (code, description, notice, resolution, offline)
VALUES
   ('grid',
    'A compact image rendition intended for a tiled card grid. The dimensions and download source are not yet defined.',
    NULL, NULL, FALSE),
   ('display',
    'A general-purpose image rendition intended for on-screen display. The dimensions and download source are not yet defined.',
    NULL, NULL, FALSE),
   ('art',
    'An artwork-focused image rendition without an agreed crop, format or source. It is distinct from the documented art_crop URI variant.',
    NULL, NULL, FALSE),
   ('jpeg',
    'A JPEG-encoded image rendition. This code identifies an encoding rather than a specific image size or crop; its source is not yet defined.',
    NULL, NULL, FALSE),
   ('thumb',
    'A thumbnail rendition for previews or compact lists. The dimensions and download source are not yet defined.',
    NULL, NULL, FALSE),
   ('crop',
    'A cropped rendition with an as-yet-unspecified crop region, dimensions and download source. It is distinct from art_crop and border_crop.',
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
