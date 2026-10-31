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
-- This short transaction updates two catalog flags only.
-- The large image cleanup is performed in independently committed batches.

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '30s';

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

-- The model image payloads are intentionally NOT changed here.
-- Use 20261031_prune_png_model_assets_batched.sql separately.

COMMIT;

SELECT code, offline
FROM deckkernel_test.dom_card_face_image_types
WHERE code IN ('png','small')
ORDER BY code;

-- Next: execute 20261031_prune_png_model_assets_batched.sql with psql
-- autocommit enabled, from a separate connection after previous rollback.
