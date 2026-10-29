-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Correct descriptions of the six real Scryfall image_uris fields discovered
-- in the current bulk export; older versions called them prospective formats.
-- Keep authored descriptions, all IDs, offline selection and cache data.
-- This script works whether the first offline migration was run before or
-- after its documentation was corrected.

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

-- Insert missing codes while preserving all existing keys.
INSERT INTO deckkernel_test.dom_card_face_image_types
   (code, description, notice, resolution, offline)
VALUES
   ('grid', 'A Scryfall WebP card image for compact grid and gallery views, supplied by image_uris.grid. The image dimensions may vary.', NULL, NULL, FALSE),
   ('display', 'A Scryfall WebP card image intended for detailed on-screen display, supplied by image_uris.display. The image dimensions may vary.', NULL, NULL, FALSE),
   ('art', 'A Scryfall WebP rendition focused on the card artwork, supplied by image_uris.art. This is distinct from the JPEG art_crop variant.', NULL, NULL, FALSE),
   ('jpeg', 'A Scryfall JPEG card image supplied by image_uris.jpeg. In the observed bulk export, it is the same image URL as image_uris.normal.', NULL, NULL, FALSE),
   ('thumb', 'A Scryfall WebP thumbnail card image for small previews and lists, supplied by image_uris.thumb. The image dimensions may vary.', NULL, NULL, FALSE),
   ('crop', 'A Scryfall WebP cropped card image supplied by image_uris.crop. The crop is distinct from the JPEG art_crop and border_crop variants.', NULL, NULL, FALSE)
ON CONFLICT (code) DO NOTHING;

-- Correct our own previously installed placeholder descriptions only.
-- Never overwrite subsequently edited, user-authored descriptions.
UPDATE deckkernel_test.dom_card_face_image_types AS t
SET description = v.new_description
FROM (VALUES
   ('grid', 'A compact image rendition intended for a tiled card grid. The dimensions and download source are not yet defined.', 'A Scryfall WebP card image for compact grid and gallery views, supplied by image_uris.grid. The image dimensions may vary.'),
   ('display', 'A general-purpose image rendition intended for on-screen display. The dimensions and download source are not yet defined.', 'A Scryfall WebP card image intended for detailed on-screen display, supplied by image_uris.display. The image dimensions may vary.'),
   ('art', 'An artwork-focused image rendition without an agreed crop, format or source. It is distinct from the documented art_crop URI variant.', 'A Scryfall WebP rendition focused on the card artwork, supplied by image_uris.art. This is distinct from the JPEG art_crop variant.'),
   ('jpeg', 'A JPEG-encoded image rendition. This code identifies an encoding rather than a specific image size or crop; its source is not yet defined.', 'A Scryfall JPEG card image supplied by image_uris.jpeg. In the observed bulk export, it is the same image URL as image_uris.normal.'),
   ('thumb', 'A thumbnail rendition for previews or compact lists. The dimensions and download source are not yet defined.', 'A Scryfall WebP thumbnail card image for small previews and lists, supplied by image_uris.thumb. The image dimensions may vary.'),
   ('crop', 'A cropped rendition with an as-yet-unspecified crop region, dimensions and download source. It is distinct from art_crop and border_crop.', 'A Scryfall WebP cropped card image supplied by image_uris.crop. The crop is distinct from the JPEG art_crop and border_crop variants.'),
   ('grid', 'A proposed compact tiled-grid rendition; source and size not yet defined.', 'A Scryfall WebP card image for compact grid and gallery views, supplied by image_uris.grid. The image dimensions may vary.'),
   ('display', 'A proposed on-screen rendition; source and size not yet defined.', 'A Scryfall WebP card image intended for detailed on-screen display, supplied by image_uris.display. The image dimensions may vary.'),
   ('art', 'A proposed artwork rendition distinct from art_crop; source not yet defined.', 'A Scryfall WebP rendition focused on the card artwork, supplied by image_uris.art. This is distinct from the JPEG art_crop variant.'),
   ('jpeg', 'A proposed JPEG-encoded rendition without a fixed crop or size.', 'A Scryfall JPEG card image supplied by image_uris.jpeg. In the observed bulk export, it is the same image URL as image_uris.normal.'),
   ('thumb', 'A proposed thumbnail rendition; source and size not yet defined.', 'A Scryfall WebP thumbnail card image for small previews and lists, supplied by image_uris.thumb. The image dimensions may vary.'),
   ('crop', 'A proposed unspecified crop rendition; source not yet defined.', 'A Scryfall WebP cropped card image supplied by image_uris.crop. The crop is distinct from the JPEG art_crop and border_crop variants.')
) AS v(code, old_description, new_description)
WHERE t.code = v.code AND t.description = v.old_description;

COMMIT;

SELECT code, offline, description
FROM deckkernel_test.dom_card_face_image_types
WHERE code IN ('grid','display','art','jpeg','thumb','crop')
ORDER BY code;
