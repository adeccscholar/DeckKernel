-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Remove PNG binary payloads from the RESETTABLE MODEL in small transactions.
-- Never modify deckkernel_cache.scryfall_images, asset IDs, URLs, or links.
-- Run using psql with AUTOCOMMIT on, not inside a pgAdmin transaction block.
-- Stop concurrent image imports before running this script.
--
-- Prerequisite: 20261031_select_small_prune_model_png.sql
-- psql -X -v ON_ERROR_STOP=1 -d DeckKernel -f test/firstconnect/migrations/20261031_prune_png_model_assets_batched.sql

CREATE OR REPLACE PROCEDURE deckkernel_test.prune_png_model_assets(
   IN iBatchSize integer DEFAULT 10)
LANGUAGE plpgsql
AS $prune$
DECLARE
   iChanged integer;
   iTotal bigint := 0;
   iBatches bigint := 0;
BEGIN
   IF current_database() <> 'DeckKernel' THEN
      RAISE EXCEPTION 'Wrong database; DeckKernel is required';
   END IF;
   IF iBatchSize NOT BETWEEN 1 AND 100 THEN
      RAISE EXCEPTION 'Batch size must be between 1 and 100';
   END IF;
   IF NOT EXISTS (
      SELECT 1 FROM deckkernel_test.dom_card_face_image_types
      WHERE code = 'png' AND offline = FALSE
   ) THEN
      RAISE EXCEPTION 'PNG must be offline-disabled before cleanup';
   END IF;

   LOOP
      -- A plain ordered selection ensures locked rows are not silently skipped.
      -- Any lock timeout aborts the CALL; already committed batches survive.
      WITH batch AS MATERIALIZED (
         SELECT id
         FROM deckkernel_test.scryfall_image_assets
         WHERE image_kind = 'png'
           AND (image_data IS NOT NULL OR sha256 IS NOT NULL
              OR media_type IS NOT NULL OR downloaded_at IS NOT NULL)
         ORDER BY id
         LIMIT iBatchSize
         FOR UPDATE
      )
      UPDATE deckkernel_test.scryfall_image_assets AS a
      SET image_data = NULL,
          sha256 = NULL,
          media_type = NULL,
          downloaded_at = NULL
      FROM batch
      WHERE a.id = batch.id;

      GET DIAGNOSTICS iChanged = ROW_COUNT;
      iTotal := iTotal + iChanged;
      iBatches := iBatches + 1;

      COMMIT;

      IF iBatches % 100 = 0 OR iChanged = 0 THEN
         RAISE NOTICE 'PNG model cleanup: batches=%, cleared=%, last_batch=%',
            iBatches, iTotal, iChanged;
      END IF;
      EXIT WHEN iChanged = 0;
   END LOOP;
END
$prune$;

-- CALL performs its own commits; never wrap this statement in BEGIN.
SET lock_timeout = '3s';
SET statement_timeout = '0';
CALL deckkernel_test.prune_png_model_assets(10);
RESET lock_timeout;
RESET statement_timeout;

SELECT count(*) AS png_assets,
       count(*) FILTER (WHERE image_data IS NOT NULL) AS png_payloads_remaining
FROM deckkernel_test.scryfall_image_assets
WHERE image_kind = 'png';

-- Optional, separately and outside a transaction:
-- VACUUM (ANALYZE) deckkernel_test.scryfall_image_assets;
