-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
-- Scanner extension: applied by FirstConnection EnsureSchema, not as ad-hoc migration.
-- All Cards: full printing set, all languages. Illustration and image data are separate.
--
-- Main printing columns (included in CREATE TABLE for a fresh schema):
--   collector_number text NOT NULL, lang text NOT NULL, illustration_id text
-- Card face columns:
--   illustration_id text, image_url_normal text,
--   image_url_art_crop text, image_url_png text
--
-- Persistent image tables survive FirstConnection's intentional card-table rebuilds.
CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_image_assets (
   id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
   image_url text NOT NULL UNIQUE,
   image_kind text NOT NULL DEFAULT 'art_crop',
   illustration_id text,
   sha256 text,
   media_type text,
   image_data bytea,
   downloaded_at timestamptz,
   CHECK (sha256 IS NULL OR length(sha256)=64)
);
CREATE INDEX IF NOT EXISTS ix_scryfall_image_assets_illustration
   ON deckkernel_test.scryfall_image_assets(illustration_id);
CREATE TABLE IF NOT EXISTS deckkernel_test.scryfall_image_features (
   asset_id bigint PRIMARY KEY REFERENCES deckkernel_test.scryfall_image_assets(id) ON DELETE CASCADE,
   algorithm text NOT NULL,
   algorithm_version integer NOT NULL,
   perceptual_hash bit(64),
   orb_descriptors bytea,
   orb_keypoint_count integer NOT NULL DEFAULT 0,
   calculated_at timestamptz NOT NULL DEFAULT now()
);
