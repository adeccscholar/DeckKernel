-- Additive migration: preserve the original layout text and all card-face rows.
BEGIN;
SET LOCAL lock_timeout='5s';
SET LOCAL statement_timeout='120s';

ALTER TABLE deckkernel_test.scryfall_card_faces
   ADD COLUMN IF NOT EXISTS layout_id integer;

-- Collect all preexisting, nonempty layout values without changing their text.
INSERT INTO deckkernel_test.dom_layout_types (code,description,notice)
SELECT DISTINCT f.layout,NULL::text,NULL::text
FROM deckkernel_test.scryfall_card_faces AS f
WHERE f.layout IS NOT NULL AND f.layout <> ''
ON CONFLICT (code) DO NOTHING;

-- Existing IDs remain stable. A previously populated, contradictory layout_id
-- is an error, not something to overwrite.
DO $validate$
BEGIN
   IF EXISTS (
      SELECT 1 FROM deckkernel_test.scryfall_card_faces AS f
      JOIN deckkernel_test.dom_layout_types AS d ON d.code=f.layout
      WHERE f.layout_id IS NOT NULL AND f.layout_id <> d.id
   ) THEN
      RAISE EXCEPTION 'Existing layout_id conflicts with legacy layout text';
   END IF;
END
$validate$;

UPDATE deckkernel_test.scryfall_card_faces AS f
SET layout_id=d.id
FROM deckkernel_test.dom_layout_types AS d
WHERE f.layout=d.code AND f.layout_id IS NULL;

DO $verify$
BEGIN
   IF EXISTS (
      SELECT 1 FROM deckkernel_test.scryfall_card_faces AS f
      WHERE f.layout IS NOT NULL AND f.layout <> '' AND f.layout_id IS NULL
   ) THEN
      RAISE EXCEPTION 'Incomplete card-face layout backfill';
   END IF;
END
$verify$;

DO $fk$
BEGIN
   IF NOT EXISTS (
      SELECT 1 FROM pg_constraint
      WHERE conrelid='deckkernel_test.scryfall_card_faces'::regclass
      AND conname='fk_scryfall_card_faces_layout_id'
   ) THEN
      ALTER TABLE deckkernel_test.scryfall_card_faces
      ADD CONSTRAINT fk_scryfall_card_faces_layout_id
         FOREIGN KEY (layout_id) REFERENCES deckkernel_test.dom_layout_types(id);
   END IF;
END
$fk$;

CREATE INDEX IF NOT EXISTS ix_scryfall_card_faces_layout_id
ON deckkernel_test.scryfall_card_faces(layout_id);

COMMIT;

SELECT count(*) AS faces,
       count(layout_id) AS linked_faces,
       count(*) FILTER (WHERE layout IS NOT NULL AND layout <> '' AND layout_id IS NULL)
          AS unresolved_legacy_layouts
FROM deckkernel_test.scryfall_card_faces;
