-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Keep the permanent PostgreSQL URL cache untouched.
-- Prefer small images for offline viewing, and disable expensive PNG downloads.
-- Existing image asset IDs and card/face/variant references remain intact.
-- Run after 20261030_dom_card_face_image_types_offline.sql.
-- Stop the image downloader before running this migration.
--
-- Execute: psql -X -v ON_ERROR_STOP=1 -d DeckKernel -f test/firstconnect/migrations/20261031_select_small_prune_model_png.sql
--
-- PostgreSQL MVCC: UPDATE makes the old bytea values reclaimable by VACUUM,
-- but may not immediately shrink the physical relation or TOAST files.
-- Ordinary VACUUM reclaims space for reuse without taking an exclusive
-- rewrite lock. VACUUM FULL is an optional, disruptive maintenance operation.

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '0';

-- Fail if the expected type rows are not installed.
DO $check$
BEGIN
   IF NOT EXISTS (SELECT 1 FROM deckkernel_test.dom_card_face_image_types WHERE code='small')
      OR NOT EXISTS (SELECT 1 FROM deckkernel_test.dom_card_face_image_types WHERE code='png')
   THEN
      RAISE EXCEPTION 'Missing image types: run image type migrations first';
   END IF;
END
$check$;

UPDATE deckkernel_test.dom_card_face_image_types
SET offline=CASE code WHEN 'small' THEN TRUE ELSE FALSE END
WHERE code IN ('small','png')
  AND offline IS DISTINCT FROM CASE code WHEN 'small' THEN TRUE ELSE FALSE END;

-- Clear only the model's PNG binary data and download bookkeeping.
-- Keep image asset rows, URLs, variant links, IDs and metadata.
-- Cache table deckkernel_cache.scryfall_images is never written here.
UPDATE deckkernel_test.scryfall_image_assets
SET image_data=NULL,
    sha256=NULL,
    media_type=NULL,
    downloaded_at=NULL
WHERE image_kind='png'
  AND (image_data IS NOT NULL OR sha256 IS NOT NULL
       OR media_type IS NOT NULL OR downloaded_at IS NOT NULL);

COMMIT;

SELECT t.code,t.offline,
       count(a.id) AS assets,
       count(a.id) FILTER (WHERE a.image_data IS NOT NULL) AS downloaded,
       count(a.id) FILTER (WHERE a.image_data IS NULL) AS missing
FROM deckkernel_test.dom_card_face_image_types t
LEFT JOIN deckkernel_test.scryfall_image_assets a ON a.image_kind=t.code
WHERE t.code IN ('png','small')
GROUP BY t.code,t.offline
ORDER BY t.code;

-- Run VACUUM separately AFTER this transaction (outside an explicit BEGIN):
-- VACUUM (ANALYZE) deckkernel_test.scryfall_image_assets;
