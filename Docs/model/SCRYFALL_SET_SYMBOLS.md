# Offline Scryfall Set Symbols

The Scryfall [Set object](https://scryfall.com/docs/api/sets) has an
\`icon_svg_uri\` referencing the set's official vector icon. For example,
\`https://svgs.scryfall.io/sets/mir.svg\` (the server may append a query string).
This is a separate asset from the card's \`art_crop\` and full card image.

## Import

The existing \`deckkernel_scryfall_postgres_test\` executable offers:

- \`--set-symbol-probe\`: download the first missing set SVG;
- \`--set-symbols\`: resumably download missing set SVGs;
- \`--set-symbols-offline\`: report coverage without any HTTP calls.

These modes preserve existing cards, set IDs, card images and domains.
They use the existing DeckKernel PG environment variables, the Scryfall
set catalog and per-request delay. The import is **not** part of the live
camera path. Existing symbols are left untouched, and additional
unknown set codes are not invented in the database.

The table \`deckkernel_test.scryfall_set_symbols\` has one entry per
\`scryfall_sets.id\`, plus \`icon_svg_uri\`, SHA-256, original SVG bytes
and \`downloaded_at\`. Distinct set IDs may contain byte-identical SVGs;
a future precomputed template index may deduplicate them by SHA-256.

## Runtime matching design

1. Preserve full-card and artwork matching for top-N candidates.
2. Read the *already-detected* set-symbol region of the rectified card.
3. Obtain candidate set IDs through the printing IDs in that shortlist.
4. Rasterize the cached SVGs in a strict offline SVG renderer once, at
   multiple small template sizes (for example 48/96 px), and cache those
   templates. Do not render SVGs for each camera frame.
5. Compare the photographed symbol with only those candidate templates,
   allowing color/contrast variation, rotation and perspective errors.
6. Use symbol similarity to re-rank candidate printings. Absence, poor
   visibility or uncertain symbol recognition must not veto an otherwise
   valid card or artwork match.

Treat variants of the same set icon as a shared set-level cue, not a
unique fingerprint of a printing. Foil/etched details require separate
evidence. Preserve SVG originals for audit and future rasterizers.

## Safety and verification

SVG is untrusted input. Never run embedded scripts or external network
references; render through a locked-down SVG backend. The import checks
the approved HTTPS hostname, restricts payload size, rejects several
active-content constructs, and keeps the original data byte-for-byte.
Its lightweight validation is **not** a substitute for a fully hardened
SVG/XML parser before rasterization.

The importer has not been compiled or executed under bcc64x in this
repository update. Run its probe first and verify that a \`mir\` row is
stored before downloading every icon.
