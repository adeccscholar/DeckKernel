-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Execute ONLY the DO statement below as a single query in pgAdmin Query Tool.
-- Enable Auto-commit. Do not use BEGIN, an existing open transaction, or
-- pgAdmin's server-side cursor mode. Stop the image importer first.
-- This script changes only resettable model payloads, never permanent cache.
--
-- One transaction per 10 PNG assets. All image asset IDs, URLs, and links stay.

DO $cleanup$
DECLARE
   iLastId bigint := 0;
   iChanged integer;
   iCleared bigint := 0;
BEGIN
   IF current_database() <> 'DeckKernel' THEN
      RAISE EXCEPTION 'Expected database DeckKernel';
   END IF;

   IF NOT EXISTS (
      SELECT 1
      FROM deckkernel_test.dom_card_face_image_types
      WHERE code = 'png' AND offline = FALSE
   ) THEN
      RAISE EXCEPTION 'Set png.offline = FALSE before cleanup';
   END IF;

   LOOP
      PERFORM set_config('lock_timeout', '3s', true);

      WITH selected AS MATERIALIZED (
         SELECT id
         FROM deckkernel_test.scryfall_image_assets
         WHERE id > iLastId
           AND image_kind = 'png'
           AND (image_data IS NOT NULL OR sha256 IS NOT NULL
             OR media_type IS NOT NULL OR downloaded_at IS NOT NULL)
         ORDER BY id
         LIMIT 10
         FOR UPDATE
      ),
      cleared AS (
         UPDATE deckkernel_test.scryfall_image_assets AS a
         SET image_data = NULL,
             sha256 = NULL,
             media_type = NULL,
             downloaded_at = NULL
         FROM selected
         WHERE a.id = selected.id
         RETURNING a.id
      )
      SELECT count(*)::integer, COALESCE(max(id), iLastId)
      INTO iChanged, iLastId
      FROM cleared;

      EXIT WHEN iChanged = 0;

      iCleared := iCleared + iChanged;
      COMMIT;

      IF iCleared % 1000 = 0 THEN
         RAISE NOTICE 'Cleared % PNG model assets, last id %',
            iCleared, iLastId;
      END IF;
   END LOOP;

   RAISE NOTICE 'PNG model cleanup completed: cleared % rows', iCleared;
END
$cleanup$;
