-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
--
-- Add the Scryfall card price types as a separate, non-destructive value table.
-- Fields and descriptions are based on Scryfall's API:
-- https://scryfall.com/docs/api/cards
-- https://github.com/scryfall/api-types/blob/main/src/objects/Card/values/Prices.ts
--
-- ID, code, currency, description, notices.
-- Currency contains USD, EUR or TIX (MTGO event tickets, NOT ISO 4217).
-- The values represent Scryfall prices fields, not unique trading exchanges.
-- A missing price in the API is NULL and is not a zero price.
-- All current card/printing data is preserved; no price-history model is created.

BEGIN;
SET LOCAL lock_timeout = '5s';
SET LOCAL statement_timeout = '60s';

CREATE TABLE IF NOT EXISTS deckkernel_test.dom_price_types (
   id integer GENERATED ALWAYS AS IDENTITY,
   code text NOT NULL,
   currency text NOT NULL,
   description text,
   notices text,

   CONSTRAINT pk_dom_price_types PRIMARY KEY (id),
   CONSTRAINT uk_dom_price_types_code UNIQUE (code)
);

-- Detect incompatible prior installations and fail without changing data.
DO $validate$
DECLARE
   theTable regclass := to_regclass('deckkernel_test.dom_price_types');
BEGIN
   IF theTable IS NULL THEN
      RAISE EXCEPTION 'deckkernel_test.dom_price_types does not exist';
   END IF;

   IF (
      SELECT count(*)
      FROM pg_attribute AS a
      WHERE a.attrelid = theTable AND NOT a.attisdropped
        AND ((a.attname = 'id' AND a.atttypid = 'integer'::regtype)
          OR (a.attname IN ('code','currency','description','notices')
             AND a.atttypid = 'text'::regtype))
   ) <> 5 THEN
      RAISE EXCEPTION 'dom_price_types has incompatible column definitions';
   END IF;

   IF NOT EXISTS (
      SELECT 1 FROM pg_constraint AS c
      JOIN pg_attribute AS a ON a.attrelid = c.conrelid
      WHERE c.conrelid = theTable AND c.contype = 'p'
        AND c.conname = 'pk_dom_price_types'
        AND a.attname = 'id'
        AND c.conkey = ARRAY[a.attnum]::smallint[]
   ) OR NOT EXISTS (
      SELECT 1 FROM pg_constraint AS c
      JOIN pg_attribute AS a ON a.attrelid = c.conrelid
      WHERE c.conrelid = theTable AND c.contype = 'u'
        AND c.conname = 'uk_dom_price_types_code'
        AND a.attname = 'code'
        AND c.conkey = ARRAY[a.attnum]::smallint[]
   ) THEN
      RAISE EXCEPTION 'dom_price_types constraints do not match pk_/uk_ naming';
   END IF;
END
$validate$;

COMMENT ON TABLE deckkernel_test.dom_price_types IS
   'Types of price quotations supplied by Scryfall for individual card printings.';
COMMENT ON COLUMN deckkernel_test.dom_price_types.currency IS
   'USD, EUR or MTGO ticket unit TIX; TIX is not an ISO 4217 currency.';
COMMENT ON COLUMN deckkernel_test.dom_price_types.notices IS
   'Optional editorial notes; automatic catalog synchronization never overwrites existing notes.';

-- Add the six currently supported Scryfall fields. Preserve existing IDs,
-- currencies, descriptions, and notes when a code already exists.
INSERT INTO deckkernel_test.dom_price_types
   (code, currency, description, notices)
VALUES
   ('usd', 'USD', 'Indicative market price for a nonfoil paper printing in US dollars, reported by Scryfall from US-market sources such as TCGplayer.', NULL),
   ('usd_foil', 'USD', 'Indicative market price for a traditional foil paper printing in US dollars, reported by Scryfall from US-market sources such as TCGplayer.', NULL),
   ('usd_etched', 'USD', 'Indicative market price for an etched-foil paper printing in US dollars, reported by Scryfall from US-market sources such as TCGplayer.', NULL),
   ('eur', 'EUR', 'Indicative market price for a nonfoil paper printing in euros, reported by Scryfall from European-market sources such as Cardmarket.', NULL),
   ('eur_foil', 'EUR', 'Indicative market price for a foil paper printing in euros, reported by Scryfall from European-market sources such as Cardmarket.', NULL),
   ('tix', 'TIX', 'Indicative Magic Online digital card price denominated in MTGO event tickets (tix), not in a fiat currency; commonly associated with Cardhoarder.', NULL)
ON CONFLICT (code) DO NOTHING;

COMMIT;

SELECT id, code, currency, description, notices
FROM deckkernel_test.dom_price_types
ORDER BY code;
