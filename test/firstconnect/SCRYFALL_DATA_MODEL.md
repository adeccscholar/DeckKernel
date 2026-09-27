# Scryfall data model for DeckKernel

This document is a development-oriented reference for the Scryfall entities, attributes
and domains relevant to DeckKernel. It separates the external Scryfall contract from the
small relational subset used by the first-connect lesson.

Scryfall is JSON-oriented and generally documents semantic types rather than SQL storage
lengths. Where Scryfall publishes no contractual maximum, this document says
**no published maximum** instead of inventing a `varchar(n)` limit.

## Entity overview

```text
BulkData
   |
   +--> Card printing
           |
           +--> Set
           +--> 0..n CardFace
           +--> 0..n RelatedCard
           +--> Legalities
           +--> Prices
           +--> ImageUris
           +--> PurchaseUris
           +--> RelatedUris
           +--> Preview
```

The most important distinction is:

- **Oracle identity**: the conceptual Magic card, normally identified by `oracle_id`.
- **Printing**: one concrete card object/edition, identified by Scryfall `id`.
- **Set**: the release/product grouping, identified by `set_id`.
- **Card face**: a layout-dependent subobject for split, transform, MDFC and other
  multi-face layouts.

The first-connect lesson currently persists only:

```text
Set:
   id
   code
   name

Card printing:
   id
   oracle_id
   name
   set_id
   released_at
```

---

# 1. BulkData

Scryfall bulk exports are the preferred source for large local synchronization jobs.

| Attribute | Domain | Nullable | Suggested PostgreSQL type | Constraint / note |
| --- | --- | ---: | --- | --- |
| `object` | literal/object discriminator | no | `text` | Bulk-data object type |
| `id` | UUID | no | `uuid` | Stable bulk object identifier |
| `type` | enum-like string | no | lookup/domain | Examples include `default_cards` |
| `updated_at` | ISO date-time | no | `timestamptz` | Update timestamp |
| `uri` | URI | no | `text` | API object URI |
| `name` | string | no | `text` | No published maximum |
| `description` | string | no | `text` | No published maximum |
| `download_uri` | URI | no | `text` | Download target |
| `content_type` | MIME-like string | no | `text` | Payload type |
| `content_encoding` | string | no | `text` | Encoding/compression |
| `size` | integer bytes | no | `bigint` | Payload size |

For the first lesson, only `type`, `updated_at` and the download URI are used.

---

# 2. Set

A set is shared by many card printings.

| Attribute | Domain | Nullable | Suggested PostgreSQL type | Constraint / note |
| --- | --- | ---: | --- | --- |
| `id` | UUID | no | `uuid` | Stable Scryfall set ID |
| `code` | string | no | `text` | Documented as unique 3-5 letter set code |
| `mtgo_code` | string | yes | `text` | MTGO-specific code |
| `arena_code` | string | yes | `text` | Arena-specific code |
| `tcgplayer_id` | integer | yes | `bigint` | External provider ID |
| `name` | string | no | `text` | English set name |
| `set_type` | closed value domain | no | lookup/domain | See SetType below |
| `released_at` | ISO date | yes | `date` | Release / first-print date |
| `block_code` | string | yes | `text` | Historical block code |
| `block` | string | yes | `text` | Block/group name |
| `parent_set_code` | string | yes | `text` | Parent for promo/token sets |
| `card_count` | integer | no | `integer` | Non-negative |
| `printed_size` | integer | yes | `integer` | Non-negative |
| `digital` | boolean | no | `boolean` | Digital-only |
| `foil_only` | boolean | no | `boolean` | Only foil cards |
| `nonfoil_only` | boolean | no | `boolean` | Only nonfoil cards |
| `scryfall_uri` | URI | no | `text` | Human-facing page |
| `uri` | URI | no | `text` | API object |
| `icon_svg_uri` | URI | no | `text` | Set icon |
| `search_uri` | URI | no | `text` | API search for cards in set |

## SetType domain

Current maintained API types include:

`core`, `expansion`, `masters`, `alchemy`, `masterpiece`, `arsenal`,
`from_the_vault`, `spellbook`, `premium_deck`, `duel_deck`,
`draft_innovation`, `treasure_chest`, `commander`, `planechase`,
`archenemy`, `vanguard`, `funny`, `starter`, `box`, `promo`, `token`,
`memorabilia`, `minigame`.

For DeckKernel, a lookup table or flexible domain is safer than a hard PostgreSQL enum,
because Scryfall owns and can extend this vocabulary.

---

# 3. Card printing

A Scryfall Card object is a **printing**.

## 3.1 Identity and references

| Attribute | Domain | Nullable | Suggested DB type | Note |
| --- | --- | ---: | --- | --- |
| `id` | UUID | no | `uuid` | Printing primary key |
| `oracle_id` | UUID | layout-dependent | `uuid` | Shared across reprints; absent at root for reversible cards |
| `lang` | language code | no | lookup/domain | See Language below |
| `layout` | closed value domain | no | lookup/domain | See Layout below |
| `prints_search_uri` | URI | no | `text` | API URI |
| `rulings_uri` | URI | no | `text` | API URI |
| `scryfall_uri` | URI | no | `text` | Human page |
| `uri` | URI | no | `text` | API object |

Optional external IDs include:

`arena_id`, `mtgo_id`, `mtgo_foil_id`, `multiverse_ids[]`, `tcgplayer_id`,
`tcgplayer_etched_id`, `cardmarket_id`.

A repeated value such as `multiverse_ids[]` is better represented by a child relation
if it will be queried.

## 3.2 Gameplay / Oracle attributes

| Attribute | Domain | Nullable / conditional | Suggested DB type | Note |
| --- | --- | ---: | --- | --- |
| `name` | string | no | `text` | Multi-face root names can contain ` // ` |
| `type_line` | string | no | `text` | No published maximum |
| `mana_cost` | string | yes/layout-dependent | `text` | Symbol notation; empty string differs from absent cost |
| `cmc` | decimal | usually no | `numeric` | Fractional values can exist |
| `oracle_text` | string | face/layout-dependent | `text` | Rules text |
| `colors` | color array | layout-dependent | child relation / array | Usually W/U/B/R/G |
| `color_identity` | color array | no for ordinary cards | child relation / bit mask | Commander identity |
| `color_indicator` | color array | yes | child relation / array | Face-level when present |
| `keywords` | string array | no | child relation / array | Open vocabulary |
| `produced_mana` | mana/color array | yes | child relation / array | Mana produced |
| `reserved` | boolean | no | `boolean` | Reserved List flag |
| `power` | string | yes | `text` | Not numeric; `*` and similar values exist |
| `toughness` | string | yes | `text` | Not numeric |
| `loyalty` | string | yes | `text` | Can be `X` |
| `defense` | string | yes | `text` | Keep textual |
| `hand_modifier` | signed string | Vanguard only | `text` | Example `-1` |
| `life_modifier` | signed string | Vanguard only | `text` | Example `+2` |
| `edhrec_rank` | integer | yes | `integer` | Ranking only |
| `penny_rank` | integer | yes | `integer` | Ranking only |
| `legalities` | object/map | no | child relation | See Legalities |

## 3.3 Printing attributes

| Attribute | Domain | Nullable | Suggested DB type | Note |
| --- | --- | ---: | --- | --- |
| `set_id` | UUID | no | `uuid` FK | References Set |
| `set` | set code | no | `text` | Denormalized convenience value |
| `set_name` | string | no | `text` | Denormalized convenience value |
| `set_type` | SetType | no | lookup/domain | Denormalized from set |
| `released_at` | ISO date | no | `date` | Printing release |
| `collector_number` | string | no | `text` | May contain letters or `★`; never use integer |
| `rarity` | closed value domain | no | lookup/domain | See Rarity |
| `artist` | string | yes | `text` | May be absent for previews |
| `artist_ids` | UUID array | yes | child relation | Multiple artists possible |
| `illustration_id` | UUID | yes | `uuid` | Artwork identity |
| `booster` | boolean | no | `boolean` | Found in boosters |
| `digital` | boolean | no | `boolean` | Digital-only |
| `finishes` | array of enum values | no | child relation | nonfoil/foil/etched |
| `frame` | enum-like string | no | lookup/domain | Frame family |
| `frame_effects` | string array | yes | child relation | Multiple effects |
| `full_art` | boolean | no | `boolean` | Full-art |
| `games` | enum array | no | child relation | paper/mtgo/arena/etc. |
| `highres_image` | boolean | no | `boolean` | Image quality |
| `image_status` | enum | no | lookup/domain | See Image status |
| `oversized` | boolean | no | `boolean` | Oversized card |
| `promo` | boolean | no | `boolean` | Promotional printing |
| `promo_types` | string array | yes | child relation | Extensible vocabulary |
| `reprint` | boolean | no | `boolean` | Reprint flag |
| `story_spotlight` | boolean | no | `boolean` | Story spotlight |
| `textless` | boolean | no | `boolean` | Printed without rules text |
| `variation` | boolean | no | `boolean` | Variation flag |
| `variation_of` | UUID | yes | `uuid` self FK | Parent printing |
| `security_stamp` | enum-like string | yes | lookup/domain | Stamp type |
| `card_back_id` | UUID | conditional | `uuid` | Card-back design |
| `content_warning` | boolean | yes | `boolean` | Downstream-use warning |
| `attraction_lights` | integer array | yes | smallint array / child | Values documented as 1..6 |

Localized/flavor fields are optional strings:

`flavor_name`, `flavor_text`, `watermark`, `printed_name`, `printed_text`,
`printed_type_line`.

Scryfall publishes no useful fixed SQL lengths for these; use `text`.

---

# 4. CardFace

Multi-face layouts move several values from the root object into `card_faces[]`.

Typical face attributes:

`name`, `oracle_id` for reversible cards, `mana_cost`, `type_line`,
`oracle_text`, `colors`, `color_indicator`, `power`, `toughness`,
`loyalty`, `defense`, `hand_modifier`, `life_modifier`, `image_uris`,
`artist`, `artist_id`, `illustration_id`, `flavor_name`, `flavor_text`,
`watermark`, `printed_name`, `printed_text`, `printed_type_line`.

A practical relational key is:

```text
(card_id, face_index)
```

where `face_index` preserves Scryfall's face order.

---

# 5. Related cards / parts

`all_parts[]` describes linked objects such as tokens or meld pieces.

Typical useful fields:

| Attribute | Domain | Suggested type |
| --- | --- | --- |
| `id` | UUID | `uuid` |
| `component` | enum-like string | lookup/domain |
| `name` | string | `text` |
| `type_line` | string | `text` |
| `uri` | URI | `text` |

This is naturally a relationship entity rather than repeated embedded data.

---

# 6. Legalities

Each format maps to one of:

`legal`, `not_legal`, `restricted`, `banned`.

A query-friendly relational model is:

```text
oracle_id | format | legality
```

Avoid one fixed SQL column per format if DeckKernel should tolerate new formats.

---

# 7. Prices

Scryfall price fields are strings or null:

| Attribute | Domain | Suggested type |
| --- | --- | --- |
| `usd` | decimal string/null | `numeric` nullable |
| `usd_foil` | decimal string/null | `numeric` nullable |
| `usd_etched` | decimal string/null | `numeric` nullable |
| `eur` | decimal string/null | `numeric` nullable |
| `eur_foil` | decimal string/null | `numeric` nullable |
| `tix` | decimal string/null | `numeric` nullable |

These are supplied as strings, so conversion should happen explicitly at the application
boundary. If price history matters, model prices as timestamped observations.

---

# 8. ImageUris

Current image keys and typical renditions:

| Key | Typical rendition |
| --- | --- |
| `small` | 146 x 204 JPG |
| `normal` | 488 x 680 JPG |
| `large` | 672 x 936 JPG |
| `png` | 745 x 1040 PNG |
| `art_crop` | variable JPG crop |
| `border_crop` | 480 x 680 JPG |

Store URIs as `text`. For multi-face cards, imagery can be face-specific.

---

# 9. Preview

Optional preview fields:

| Attribute | Domain | Suggested type |
| --- | --- | --- |
| `previewed_at` | ISO date | `date` |
| `source_uri` | URI | `text` |
| `source` | string | `text` |

---

# 10. Closed and bounded domains

## Colors

`W`, `U`, `B`, `R`, `G`; API types also expose `C` for colorless in some
contexts.

## Rarity

`common`, `uncommon`, `rare`, `special`, `mythic`, `bonus`.

## Finish

`nonfoil`, `foil`, `etched`.

## Legality

`legal`, `not_legal`, `restricted`, `banned`.

## Image status

`missing`, `placeholder`, `lowres`, `highres_scan`.

## Language

Current maintained codes include:

`en`, `es`, `fr`, `de`, `it`, `pt`, `ja`, `ko`, `ru`, `zhs`,
`zht`, `he`, `la`, `grc`, `ar`, `sa`, `ph`.

## Layout

Current maintained layout values include:

`normal`, `split`, `flip`, `transform`, `modal_dfc`, `meld`, `leveler`,
`class`, `saga`, `adventure`, `mutate`, `prototype`, `battle`, `planar`,
`scheme`, `vanguard`, `token`, `double_faced_token`, `emblem`, `augment`,
`host`, `art_series`, `reversible_card`, `case`.

These domains are externally owned and can grow. DeckKernel should have an unknown/new
value strategy rather than assuming today's list is permanent.

---

# 11. Length and constraint policy

| Scryfall semantic kind | Recommended PostgreSQL storage |
| --- | --- |
| UUID | `uuid` |
| ISO date | `date` |
| ISO date-time | `timestamptz` |
| open/free string | `text` |
| URI | `text` |
| collector number | `text` |
| power/toughness/loyalty/defense | `text` |
| currency string | explicit conversion to `numeric` if arithmetic is required |
| boolean | `boolean` |
| externally owned closed vocabulary | lookup/domain with extension strategy |
| repeated arrays | child relation when queried |

Do not introduce arbitrary length limits purely because current sample data fits. If a
DeckKernel UI or domain rule later imposes a length constraint, document it as a
DeckKernel rule, not as a Scryfall restriction.

---

# 12. Candidate DeckKernel normalization

A fuller model can evolve toward:

```text
scryfall_sets
scryfall_oracle_cards
scryfall_printings
scryfall_card_faces
scryfall_card_colors
scryfall_color_identity
scryfall_keywords
scryfall_legalities
scryfall_finishes
scryfall_games
scryfall_prices
scryfall_images
scryfall_related_cards
scryfall_artists
```

The first-connect lesson intentionally does not implement this complete schema. Its
purpose is to prove the infrastructure and teach the data flow first.

---

# 13. Upstream references

Checked against the maintained Scryfall API type definitions and Scryfall API
documentation in September 2026, especially:

- Card object/layout definitions.
- Card field definitions.
- Card-face definitions.
- Set object and SetType definitions.
- Value domains for rarity, legality, layouts, colors, finishes, games, image status,
  languages and image sizes.
- Bulk-data API.

Before turning any external value list into a hard database constraint, verify the
current Scryfall contract again.
