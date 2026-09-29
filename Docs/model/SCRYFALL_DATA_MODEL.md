# Scryfall data model for DeckKernel

[TOC|Scryfall data model]

This document explains the Scryfall data model from two perspectives:

1. the **card game**: what the objects mean to a player, collector or deck builder;
2. the **software model**: how those objects can be represented in DeckKernel and PostgreSQL.

The intended reader does not need prior knowledge of Magic: The Gathering.

Scryfall is JSON-oriented and usually documents semantic types instead of SQL storage
lengths. Where Scryfall publishes no contractual maximum, this document says
**no published maximum** instead of inventing a `varchar(n)` limit.

## 1. A short introduction to the game model

Magic: The Gathering is a trading-card game. A player builds a deck from individual
cards. A card has a game identity such as **Lightning Bolt**, but the same card can be
printed many times in different products, years, languages and visual treatments.

That distinction is essential for the database model:

- an **Oracle card** is the abstract game piece: the rules meaning of a card across
  reprints;
- a **printing** is one concrete edition of that card;
- a **set** is a published product or release that groups many printings;
- a **card face** represents one side or one functional part of a multi-face card;
- **legalities** say in which play formats a card may currently be used;
- **prices** are market observations and can change frequently;
- **images** and other URIs are delivery metadata rather than game identity.

Example:

```text
Lightning Bolt                         <- Oracle card / game identity
   |
   +-- Alpha printing                  <- one physical edition
   +-- Magic 2010 printing             <- another physical edition
   +-- promo printing                  <- another physical edition
```

A deck builder often cares primarily about the Oracle card. A collector, shop or
inventory system often cares about the exact printing.

## 2. Stability classes: master data and movement data

The Scryfall API is a current-state API, not an immutable historical ledger. Even
apparently stable fields can be corrected upstream. The categories below are therefore
expectations for DeckKernel, not guarantees.

| Class | Meaning | Typical update expectation | Examples |
| --- | --- | --- | --- |
| **Identity** | Technical identity that should remain stable | Very rarely changes | Scryfall UUIDs, `oracle_id`, `set_id` |
| **Stable master data** | Describes what a card or printing fundamentally is | Usually stable after publication; corrections possible | name, mana cost, set, collector number, release date |
| **Mutable master data** | Describes the current official interpretation or metadata | Can change when Scryfall or game rules are updated | Oracle text, keywords, external IDs, images |
| **State / movement data** | Current state that naturally changes over time | Expected to change repeatedly | legalities, prices, EDHREC rank, image status during previews |
| **Technical delivery data** | API/transport metadata | Can change without changing the card itself | API URIs, download URI, bulk-data timestamp |

For database design this suggests two different handling strategies:

- master data can normally be **upserted to the current authoritative value**;
- movement data should be stored with a **timestamp/history** if DeckKernel needs trends
  instead of only the latest state.

---

# 3. Entity overview

```text
BulkData
   |
   +--> Card printing
           |
           +--> Oracle card identity
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

The first-connect lesson intentionally stores only a small subset:

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

This is sufficient to demonstrate loading, parsing, normalization, PostgreSQL storage
and querying without pretending that the complete Scryfall model is already finished.

---

## 3.1 Bulk data, manifest and REST API: three access paths

Scryfall exposes the same subject area through different technical access patterns.
Understanding the difference is important before discussing a later DeckKernel data model.

### Bulk data

Bulk data is a **snapshot export**. It is intended for applications that need many or all
cards. Instead of issuing thousands of REST requests, the client first asks Scryfall which
bulk files currently exist and then downloads one large static file.

The first-connect lesson follows exactly this pattern:

```text
GET /bulk-data
      |
      v
bulk-data manifest / metadata
      |
      +--> type = default_cards
      +--> updated_at
      +--> download_uri
                 |
                 v
        large JSON/JSONL bulk file
                 |
                 v
              Parse
```

The bulk file contains the actual Card objects. It is not a separate data model: the card
objects follow the same Scryfall object semantics that are also returned by the REST API.

### Manifest / metadata document

The response from the bulk-data endpoint acts as a **manifest** for available exports.
It describes files; it does not itself contain the complete card database.

For every export it tells the client, among other things:

- which export type it is;
- when that export was refreshed;
- where the current downloadable file is located;
- its content type/encoding and size information where provided.

This means the manifest is control and synchronization metadata. A client can compare
`updated_at` with the locally known state and decide whether the large payload needs to
be downloaded again.

The manifest URI and download URI are technical delivery information and must not be used
as business keys for cards.

### REST API

The REST API returns individual objects or paginated lists and is appropriate for targeted
queries, interactive searches and metadata that does not require a complete bulk reload.

Conceptually:

```text
Bulk data     -> efficient complete/current snapshot
REST API      -> targeted lookup, search and navigation
Catalogs      -> available value lists / vocabularies
Migrations    -> corrections to Scryfall object identities
```

Scryfall explicitly recommends bulk data instead of performing huge numbers of repetitive
API requests. Interactive or selective access can still use the REST interface.

---

# 4. BulkData

## Meaning in Scryfall

`BulkData` is not a Magic game entity. It is a delivery mechanism. Scryfall publishes
large downloadable snapshots so an application can synchronize many cards efficiently
instead of issuing one HTTP request per card.

For the lesson we use the `default_cards` export.

| Attribute | What it means | Domain / type | Change expectation | Suggested PostgreSQL type |
| --- | --- | --- | --- |
| `object` | Identifies the JSON object class | discriminator string | Technical; stable per object kind | `text` |
| `id` | Scryfall identifier of the bulk-data definition | UUID | Identity | `uuid` |
| `type` | Which export this is | domain-like string | Stable, but new types may be added | lookup/domain |
| `updated_at` | When Scryfall generated/refreshed this export | ISO date-time | **Changes on every refresh** | `timestamptz` |
| `uri` | API address of the metadata object | URI | Technical; may change | `text` |
| `name` | Human-readable export name | string | Rare changes | `text` |
| `description` | Explanation of the export | string | Rare changes | `text` |
| `download_uri` | Address of the actual bulk file | URI | **Expected to change with refreshes** | `text` |
| `content_type` | Media type of the payload | string | Technical | `text` |
| `content_encoding` | Compression/encoding information | string | Technical | `text` |
| `size` | Current payload size in bytes | integer | **Changes as data grows** | `bigint` |

Bulk-data metadata is therefore mostly **technical movement/delivery data**, not
long-lived card master data.

---

# 5. Set

## Meaning in the card game

A **set** is a release or product grouping. Examples are a yearly core release, an
expansion, a Commander product, a promotional series or a token set.

A set contains many card printings. A card such as Lightning Bolt may appear in several
different sets over its lifetime.

## Attribute explanation and stability

| Attribute | What it means to a non-player | Domain / type | Change expectation | Suggested PostgreSQL type |
| --- | --- | --- | --- | --- |
| `id` | Permanent Scryfall identity of the set | UUID | **Identity; expected stable** | `uuid` |
| `code` | Short set abbreviation printed/used by Magic systems | 3-5 character code | Stable after publication; correction is unusual | `text` |
| `mtgo_code` | Set code used by Magic Online | string, optional | Usually stable; can be added/corrected | `text` |
| `arena_code` | Set code used by MTG Arena | string, optional | Usually stable; can be added/corrected | `text` |
| `tcgplayer_id` | ID used by the external TCGplayer marketplace | integer, optional | Usually stable once assigned; may appear later | `bigint` |
| `name` | Full English product/set name | string | Stable master data; typo corrections possible | `text` |
| `set_type` | Product category, such as expansion or Commander | SetType domain | Stable for an existing set; domain itself can grow | lookup/domain |
| `released_at` | Date on which the set/product was released | ISO date, optional | Stable master data; preview data may be corrected | `date` |
| `block_code` | Historic grouping code for older release blocks | string, optional | Stable historic metadata | `text` |
| `block` | Human-readable historic block/group name | string, optional | Stable historic metadata | `text` |
| `parent_set_code` | Links token/promo subsets to a parent product | string, optional | Usually stable | `text` |
| `card_count` | Number of Scryfall card objects currently counted in the set | integer | **Can change**, especially during previews/corrections | `integer` |
| `printed_size` | Printed collector-number denominator when meaningful | integer, optional | Usually stable after completion | `integer` |
| `digital` | Set exists only in a digital Magic product | boolean | Stable master data | `boolean` |
| `foil_only` | Every printing in this set is foil | boolean | Usually stable | `boolean` |
| `nonfoil_only` | Every printing in this set is nonfoil | boolean | Usually stable | `boolean` |
| `scryfall_uri` | Human-facing Scryfall page | URI | Technical metadata | `text` |
| `uri` | API address | URI | Technical metadata | `text` |
| `icon_svg_uri` | Download location of the set symbol | URI | Technical; content/URI may change | `text` |
| `search_uri` | API query for cards in the set | URI | Technical metadata | `text` |

**Classification:** the set itself is classic **master data**. The only field that is
naturally somewhat dynamic is the current `card_count`, particularly before a set is
fully revealed.

---

# 6. Oracle card versus printing

This is the most important modelling decision.

## Oracle card

The Oracle card represents the official game identity. It answers questions such as:

- What is the card called?
- What does it currently do according to the official rules text?
- What is its mana cost and card type?
- What is its color identity?

The same Oracle card can have many printings.

`oracle_id` is the key Scryfall uses to connect those printings.

## Printing

A printing answers collector/product questions:

- In which set was this copy released?
- What collector number does it have?
- Who illustrated this edition?
- Is it foil, etched, promo or full-art?
- Which artwork and frame does it use?
- What is its current market price?

This means DeckKernel should avoid mixing Oracle-level and printing-level properties in
one conceptual entity when the model becomes more complete.

---

# 7. Card printing: identity and references

| Attribute | Meaning | Domain / type | Change expectation | Suggested DB type |
| --- | --- | --- | --- | --- |
| `id` | Scryfall identity of this exact printing | UUID | **Identity; expected stable** | `uuid` |
| `oracle_id` | Identity of the underlying abstract card across reprints | UUID, layout-dependent | **Identity; expected stable** | `uuid` |
| `lang` | Language of this exact printing | Language domain | Stable master data | lookup/domain |
| `layout` | Physical/functional card structure, e.g. normal or transform | Layout domain | Stable for a printing | lookup/domain |
| `prints_search_uri` | API query for all printings of this Oracle card | URI | Technical | `text` |
| `rulings_uri` | API location for rules rulings | URI | Technical; target contents can change | `text` |
| `scryfall_uri` | Human-facing Scryfall page for the printing | URI | Technical | `text` |
| `uri` | API address for the printing | URI | Technical | `text` |

Optional vendor/platform IDs include `arena_id`, `mtgo_id`, `mtgo_foil_id`,
`multiverse_ids[]`, `tcgplayer_id`, `tcgplayer_etched_id` and `cardmarket_id`.

These IDs are usually **stable master data once assigned**, but they can be added later
when a marketplace or digital platform publishes the corresponding object.

---

# 8. Gameplay / Oracle attributes

These fields describe how the card behaves in the game. For a person unfamiliar with
Magic, they are the closest equivalent to the specification of a game object.

| Attribute | Meaning in the game | Domain / type | Change expectation | Suggested DB type |
| --- | --- | --- | --- | --- |
| `name` | Official card name | string | Very stable; corrections/renames are exceptional | `text` |
| `type_line` | Classification such as Creature, Instant, Artifact, Land | string | Usually stable, but rules updates can change terminology | `text` |
| `mana_cost` | Resources required to play/cast the card, using symbols like `{2}{U}` | string, optional | Usually stable; functional errata is rare | `text` |
| `cmc` | Mana value: normalized numeric cost used by game rules | decimal | Usually stable; computed/rules interpretation can change | `numeric` |
| `oracle_text` | Current official rules text defining what the card does | string | **Mutable master data**; Oracle/rules updates can change it | `text` |
| `colors` | Actual colors of the card | Color domain array | Usually stable | child relation / array |
| `color_identity` | Commander deck-building color restriction | Color domain array | Usually stable, but Oracle changes can affect it | child relation / bit mask |
| `color_indicator` | Printed/rules color marker used on some special cards | Color domain array, optional | Usually stable | child relation / array |
| `keywords` | Rules keywords such as Flying or Trample | string array | **Can change with Oracle wording/rules maintenance** | child relation / array |
| `produced_mana` | Which mana colors/resources the card can create | Mana domain array, optional | Usually stable; derived from rules | child relation / array |
| `reserved` | Whether the card is on Wizards' Reserved List | boolean | Expected very stable | `boolean` |
| `power` | Creature combat attack value | string, optional | Usually stable; stored as text because `*` etc. exist | `text` |
| `toughness` | Creature combat durability value | string, optional | Usually stable | `text` |
| `loyalty` | Planeswalker loyalty value | string, optional | Usually stable; `X` can occur | `text` |
| `defense` | Battle-card defense value | string, optional | Usually stable | `text` |
| `hand_modifier` | Vanguard rule changing starting hand size | signed string, optional | Stable for that card | `text` |
| `life_modifier` | Vanguard rule changing starting life total | signed string, optional | Stable for that card | `text` |
| `edhrec_rank` | Popularity ranking from EDHREC | integer, optional | **Movement data; changes often** | `integer` |
| `penny_rank` | Popularity ranking for Penny Dreadful | integer, optional | **Movement data; changes often** | `integer` |
| `legalities` | Current permission status in each play format | format -> legality map | **State data; changes after bans/unbans/rotation** | child relation |

### Why combat values are strings

`power`, `toughness`, `loyalty` and similar fields are not safely modelled as integers.
Magic intentionally contains values such as `*`, `X` and other rule-dependent forms.

---

# 9. Printing attributes

These fields describe the concrete edition rather than the abstract game identity.

| Attribute | Meaning | Domain / type | Change expectation | Suggested DB type |
| --- | --- | --- | --- | --- |
| `set_id` | Which release/product contains this printing | UUID FK | **Stable master relationship** | `uuid` |
| `set` | Short set code copied onto the card object | string | Stable, denormalized | `text` |
| `set_name` | Full set name copied onto the card object | string | Stable, denormalized | `text` |
| `set_type` | Category of the containing set | SetType | Stable, denormalized | lookup/domain |
| `released_at` | Release date of this printing | ISO date | Stable after publication | `date` |
| `collector_number` | Printed catalogue number within the set | string | **Stable**; may contain letters or symbols such as `★` | `text` |
| `rarity` | Publishing rarity used by Magic products | Rarity domain | Stable for a printing | lookup/domain |
| `artist` | Illustrator name for this printing | string, optional | Stable once known; previews may fill it later | `text` |
| `artist_ids` | Stable Scryfall IDs of illustrators | UUID array, optional | Stable once assigned | child relation |
| `illustration_id` | Identity of the artwork across uses/reprints | UUID, optional | Stable once assigned | `uuid` |
| `booster` | Whether this printing can appear in booster products | boolean | Usually stable | `boolean` |
| `digital` | Printing exists only digitally | boolean | Stable | `boolean` |
| `finishes` | Available physical/digital treatments such as foil | Finish array | Usually stable; may be corrected | child relation |
| `frame` | Broad visual frame family | open string in maintained API types | Usually stable | `text` |
| `frame_effects` | Special visual treatments such as showcase/legendary crown | FrameEffect array | Usually stable; new values can be introduced | child relation |
| `full_art` | Artwork occupies an enlarged portion of the card | boolean | Stable | `boolean` |
| `games` | Platforms/products where the printing is available | Game array | Usually stable; availability can be corrected | child relation |
| `highres_image` | Whether Scryfall currently has high-resolution imagery | boolean | **Mutable technical state** | `boolean` |
| `image_status` | Current completeness/quality of image data | ImageStatus domain | **Mutable during previews** | lookup/domain |
| `oversized` | Printing uses oversized physical dimensions | boolean | Stable | `boolean` |
| `promo` | Printing is promotional rather than normal product distribution | boolean | Stable | `boolean` |
| `promo_types` | More precise promo categories | externally extensible string array | Usually stable; vocabulary can grow | child relation |
| `reprint` | Whether the card had appeared before this printing | boolean | Stable after release | `boolean` |
| `story_spotlight` | Printing/card marks an important story moment | boolean | Stable | `boolean` |
| `textless` | Physical printing intentionally omits text | boolean | Stable | `boolean` |
| `variation` | This object is a variation of another printing | boolean | Stable | `boolean` |
| `variation_of` | Printing ID of the base variation | UUID, optional | Stable relationship | `uuid` |
| `security_stamp` | Physical authenticity/security stamp style | SecurityStamp domain | Stable for printing | lookup/domain |
| `card_back_id` | Which card-back design is printed | UUID, conditional | Stable | `uuid` |
| `content_warning` | Scryfall flag advising caution in downstream display | boolean, optional | **Can change** | `boolean` |
| `attraction_lights` | Numbers printed on special Attraction cards | integers 1..6 | Stable | smallint array / child |

Localized fields such as `printed_name`, `printed_text` and `printed_type_line` are
properties of a particular localized printing and are therefore **printing master data**.

---

# 10. CardFace

## Meaning in the game

Not every Magic card is a simple rectangle with one functional face. Some cards:

- have two physical sides;
- contain two spells on one front face;
- transform from one form into another;
- have an Adventure spell plus a permanent;
- are reversible or otherwise multi-part.

Scryfall therefore stores face-specific values in `card_faces[]` for relevant layouts.

A face can have its own name, mana cost, rules text, colors, combat values, artwork and
localized text.

## Stability

Face data is mostly **stable or mutable master data**, following the same rules as the
corresponding Oracle/printing fields. Face rules text can change through Oracle updates;
face images can improve during preview season.

A practical relational key is:

```text
(card_id, face_index)
```

`face_index` preserves Scryfall's ordering of the faces.

---

# 11. Related cards / parts

Some cards create or depend on other objects: tokens, meld pieces, combo parts or other
closely related card objects. Scryfall exposes these through `all_parts[]`.

| Attribute | Meaning | Domain | Stability | Suggested type |
| --- | --- | --- | --- | --- |
| `id` | Scryfall ID of the related object | UUID | Identity | `uuid` |
| `component` | Kind of relationship/component | enum-like external vocabulary | Usually stable | lookup/domain |
| `name` | Human-readable related-card name | string | Stable master | `text` |
| `type_line` | Game classification of the related object | string | Mutable master | `text` |
| `uri` | API location | URI | Technical | `text` |

This is best modelled as a relationship entity, not as repeated text columns in the
main card table.

---

# 12. Legalities

## Meaning in the game

Magic is played in several **formats**. A format defines which sets/cards are allowed
and often has its own banned/restricted list.

A card can therefore be legal in Commander, banned in another format, and completely
outside the card pool of a third format.

Legalities are classic **state/movement data**. They can change when:

- a format rotates older sets out;
- organizers ban or unban a card;
- a new format is introduced;
- Scryfall corrects a status.

If DeckKernel wants historical analysis, a current-value table is insufficient; legality
changes should be timestamped.

A relational form could be:

```text
oracle_id | format | legality | valid_from | observed_at
```

---

# 13. Prices

## Meaning

Scryfall exposes current market-oriented prices for different currencies and finishes.
These values describe the market at a point in time; they are not properties of the game
rules.

Prices are the clearest example of **movement data** and may change every day.

| Attribute | Meaning | Domain | Change expectation | Suggested type |
| --- | --- | --- | --- | --- |
| `usd` | US-dollar nonfoil price | decimal string/null | Frequent | `numeric` nullable |
| `usd_foil` | US-dollar foil price | decimal string/null | Frequent | `numeric` nullable |
| `usd_etched` | US-dollar etched-foil price | decimal string/null | Frequent | `numeric` nullable |
| `eur` | Euro nonfoil price | decimal string/null | Frequent | `numeric` nullable |
| `eur_foil` | Euro foil price | decimal string/null | Frequent | `numeric` nullable |
| `tix` | Magic Online ticket price | decimal string/null | Frequent | `numeric` nullable |

Scryfall sends prices as strings. Conversion to a numeric type should therefore be an
explicit application-boundary decision.

For analysis, prefer a history entity such as:

```text
printing_id | observed_at | currency | finish | amount
```

rather than repeatedly overwriting one price column.

---

# 14. Images

Images are associated with a printing or card face. They are important for UI and
collection applications but are not part of the game's logical identity.

| Image key | Meaning | Typical rendition | Stability |
| --- | --- | --- | --- |
| `small` | Small card image | 146 x 204 JPG | URI/content can change |
| `normal` | Standard UI image | 488 x 680 JPG | URI/content can change |
| `large` | Larger image | 672 x 936 JPG | URI/content can change |
| `png` | High-quality transparent-corner image | 745 x 1040 PNG | URI/content can change |
| `art_crop` | Artwork without most card frame | variable JPG | URI/content can change |
| `border_crop` | Card image with border cropped | 480 x 680 JPG | URI/content can change |

`image_status` is especially volatile during preview season: a card may progress from
missing to placeholder to low-resolution and finally to a high-resolution scan.

Image URIs are therefore **technical mutable metadata**. If the product needs long-lived
image availability, download/cache according to Scryfall's usage rules rather than
assuming the URI is permanent.

---

# 15. Preview

Preview metadata describes the public reveal of a card before or around release.

| Attribute | Meaning | Domain | Stability |
| --- | --- | --- | --- | --- |
| `previewed_at` | Date on which the card was previewed | ISO date | Stable once known; corrections possible |
| `source_uri` | Link to article/video/source that revealed it | URI | Technical; link may age/change |
| `source` | Human-readable source name | string | Usually stable |

Preview information is **event metadata**: it records an occurrence rather than a core
game rule.

---

# 16. REST-accessible Scryfall structures

The bulk download is not the only way to access Scryfall data. The following structures
are available through REST-style endpoints and are useful for later lessons.

| Structure | What it provides | Typical use |
| --- | --- | --- |
| Cards | Individual card printings and paginated card searches | Interactive search, direct lookup, completing one missing card |
| Sets | Set/product metadata | Browse releases, resolve one set |
| Rulings | Published rules-manager notes connected to an Oracle card | Explain special interactions and historical/current rulings |
| Catalogs | Lists of currently known values such as card names or types | Autocomplete, vocabulary discovery, domain inspection |
| Migrations | Scryfall identity corrections such as merge/delete | Keep a local mirror consistent when Scryfall IDs are retired |
| Symbology | Magic symbol metadata | Render mana and game symbols |
| BulkData | Manifest-like metadata and current bulk download locations | Full synchronization |

## Cards via REST

Cards can be retrieved individually through Scryfall identifiers and several external
identifiers. Scryfall also exposes search, named-card lookup, autocomplete, random-card
selection and collection-style requests.

REST retrieval returns the same conceptual Card object described in this document. It is
therefore useful for targeted access, while the bulk file remains better for a complete
local mirror.

## Sets via REST

Sets are available as a list and as individual objects. This is useful when an application
needs current set metadata without downloading the card bulk export.

## Rulings

A ruling is a dated explanatory note about how a card works under the rules.

| Attribute | Meaning | Stability |
| --- | --- | --- |
| `oracle_id` | Oracle card to which the ruling belongs | Stable relationship |
| `source` | Publisher of the ruling, currently `wotc` or `scryfall` | Stable for that ruling |
| `published_at` | Date on which the ruling/note was published | Stable event date |
| `comment` | Human-readable explanation | Normally stable; corrections remain possible |

Rulings are not the card's Oracle text. They are additional explanations and should be
treated as their own entity/event-like data.

## Catalogs

Catalogs are Scryfall-provided lists of strings. They answer questions such as
"which values currently exist?" and are useful for UI selection, validation hints and
domain discovery.

A catalog contains:

| Attribute | Meaning |
| --- | --- |
| `uri` | API location of the catalog |
| `total_values` | Number of values currently listed |
| `data[]` | The actual strings |

Catalogs are especially relevant for DeckKernel because they can help discover external
domains without hard-coding them permanently into the program.

## Migrations

Scryfall can retire a card object ID because an object was merged or deleted. Migrations
describe those corrections.

| Attribute | Meaning |
| --- | --- |
| `id` | Migration event UUID |
| `performed_at` | Date of the correction |
| `migration_strategy` | `merge` or `delete` |
| `old_scryfall_id` | Retired card-object ID |
| `new_scryfall_id` | Replacement ID for a merge |
| `note` | Human-readable explanation |
| `metadata` | Additional human-oriented context |

This is important for a local mirror: even though Scryfall IDs are intended as stable
identifiers, Scryfall provides an explicit mechanism for exceptional identity corrections.

# 17. Domain tables

The following domains are externally owned by Scryfall/Magic. DeckKernel should not
assume that today's list is permanent. Lookup tables or tolerant string-backed domains
are often safer than rigid PostgreSQL enums.

## 17.1 SetType

| Value | Meaning for a non-player |
| --- | --- |
| `core` | Core/base Magic set |
| `expansion` | Normal major expansion release |
| `masters` | Reprint-focused Masters product |
| `alchemy` | Digital Arena/Alchemy-oriented set |
| `masterpiece` | Premium Masterpiece series |
| `arsenal` | Commander Arsenal product |
| `from_the_vault` | From the Vault premium collection |
| `spellbook` | Signature Spellbook product |
| `premium_deck` | Premium Deck Series |
| `duel_deck` | Preconstructed Duel Deck product |
| `draft_innovation` | Special draft-focused product |
| `treasure_chest` | Magic Online treasure-chest pool |
| `commander` | Commander preconstructed/product set |
| `planechase` | Planechase product |
| `archenemy` | Archenemy product |
| `vanguard` | Vanguard cards/product |
| `funny` | Un-set / intentionally humorous or nonstandard release |
| `starter` | Introductory/starter product |
| `box` | Gift-box/product collection |
| `promo` | Promotional-card-only set |
| `token` | Tokens and emblems |
| `memorabilia` | Gold-border, oversize, trophy or similar nonstandard objects |
| `minigame` | Minigame insert cards |

## 17.2 Color

| Value | Meaning |
| --- | --- |
| `W` | White |
| `U` | Blue |
| `B` | Black |
| `R` | Red |
| `G` | Green |
| `C` | Colorless token used by some API fields; colorless is not one of Magic's five colors |

## 17.3 Mana produced

The maintained API type currently uses the same symbolic values:

| Value | Meaning |
| --- | --- |
| `W` | White mana |
| `U` | Blue mana |
| `B` | Black mana |
| `R` | Red mana |
| `G` | Green mana |
| `C` | Colorless mana |

## 17.4 Rarity

| Value | Meaning |
| --- | --- |
| `common` | Common rarity |
| `uncommon` | Uncommon rarity |
| `rare` | Rare rarity |
| `mythic` | Mythic rare |
| `special` | Special/nonstandard rarity classification |
| `bonus` | Bonus-sheet/special bonus classification |

Rarity is a property of a **printing**, not necessarily of the abstract Oracle card.

## 17.5 Finish

| Value | Meaning |
| --- | --- |
| `nonfoil` | Normal nonfoil treatment |
| `foil` | Traditional foil treatment |
| `etched` | Etched-foil treatment |

## 17.6 Legality status

| Value | Meaning |
| --- | --- |
| `legal` | Card may currently be played in the format |
| `not_legal` | Card is outside the format/card pool |
| `restricted` | Card is legal only in a restricted quantity where the format supports this |
| `banned` | Card belongs to the format's card pool but is currently banned |

## 17.7 Format

| Value | Meaning / family |
| --- | --- |
| `standard` | Rotating premier constructed format |
| `future` | Future/preview legality view |
| `historic` | Arena non-rotating Historic format |
| `gladiator` | Arena singleton community format |
| `pioneer` | Non-rotating constructed format from newer-era sets |
| `explorer` | Arena format aligned toward Pioneer |
| `modern` | Non-rotating constructed format from the Modern card pool |
| `legacy` | Large eternal constructed format |
| `pauper` | Constructed format based mainly on cards printed at common |
| `vintage` | Broad eternal format with restricted list |
| `penny` | Penny Dreadful |
| `commander` | Multiplayer singleton Commander |
| `oathbreaker` | Planeswalker-led singleton format |
| `brawl` | Rotating Commander-like format |
| `alchemy` | Arena rotating digital format |
| `paupercommander` | Commander variant using Pauper-style restrictions |
| `duel` | Duel Commander |
| `oldschool` | Old School community format |
| `premodern` | Premodern community format |
| `predh` | Commander using pre-EDH-era card pool conventions |
| `timeless` | Arena high-power non-rotating format |
| `standardbrawl` | Standard-card-pool Brawl |

Formats are especially likely to change as a **domain**: new formats can be added and
existing formats can disappear or be renamed.

## 17.8 Language

| Code | Language |
| --- | --- |
| `en` | English |
| `es` | Spanish |
| `fr` | French |
| `de` | German |
| `it` | Italian |
| `pt` | Portuguese |
| `ja` | Japanese |
| `ko` | Korean |
| `ru` | Russian |
| `zhs` | Simplified Chinese |
| `zht` | Traditional Chinese |
| `he` | Hebrew |
| `la` | Latin |
| `grc` | Ancient Greek |
| `ar` | Arabic |
| `sa` | Sanskrit |
| `ph` | Phyrexian fictional language |

## 17.9 Layout

| Value | What it means physically/functionally |
| --- | --- |
| `normal` | Standard single-face card |
| `split` | Two spell halves printed on one face |
| `flip` | Older card that rotates 180 degrees to represent another state |
| `transform` | Double-faced card that transforms between front and back |
| `modal_dfc` | Double-faced card where either side can be played |
| `meld` | Cards that combine into a larger back face |
| `leveler` | Card using level-up layout |
| `class` | Class enchantment layout |
| `saga` | Saga chapter layout |
| `adventure` | Permanent plus Adventure spell component |
| `mutate` | Card using Mutate-era layout |
| `prototype` | Card with alternate Prototype cost/stat line |
| `battle` | Battle card layout |
| `planar` | Plane/Phenomenon style card |
| `scheme` | Archenemy Scheme card |
| `vanguard` | Vanguard avatar/rule-modifier card |
| `token` | Token object rather than a normal deck card |
| `double_faced_token` | Token with different faces on both sides |
| `emblem` | Rules emblem object |
| `augment` | Un-set Augment layout |
| `host` | Un-set Host layout |
| `art_series` | Collectible art card, usually not a playable game card |
| `reversible_card` | Two unrelated usable sides |
| `case` | Case enchantment layout |

## 17.10 Game / platform

| Value | Meaning |
| --- | --- |
| `paper` | Physical tabletop Magic |
| `mtgo` | Magic Online |
| `arena` | MTG Arena |
| `astral` | Historic MicroProse/Astral digital game content |
| `sega` | Historic Sega Dreamcast game content |

## 17.11 ImageStatus

| Value | Meaning | Typical stability |
| --- | --- | --- |
| `missing` | Scryfall has no image yet | Highly temporary |
| `placeholder` | Temporary generated placeholder | Temporary |
| `lowres` | Low-resolution preview image | Temporary |
| `highres_scan` | High-resolution image/scan available | Usually final state |

## 17.12 BorderColor

| Value | Meaning |
| --- | --- |
| `black` | Black physical border |
| `white` | White physical border |
| `borderless` | Borderless treatment |
| `silver` | Silver physical border |
| `gold` | Gold physical border |

This is a description of the physical printing, not a legality rule.

## 17.13 SecurityStamp

| Value | Meaning |
| --- | --- |
| `oval` | Oval security stamp |
| `triangle` | Triangle stamp |
| `acorn` | Acorn stamp used for particular nonstandard cards |
| `circle` | Circular stamp |
| `arena` | Arena-themed stamp |
| `heart` | Heart-shaped stamp |

## 17.14 FrameEffect

| Value | Meaning |
| --- | --- |
| `legendary` | Legendary crown treatment |
| `miracle` | Miracle frame treatment |
| `nyxtouched` | Nyx/star-field treatment |
| `draft` | Draft-matters frame treatment |
| `devoid` | Devoid frame treatment |
| `tombstone` | Odyssey tombstone marker |
| `colorshifted` | Colorshifted frame |
| `inverted` | Inverted/FNM-style frame |
| `sunmoondfc` | Sun/moon transform marks |
| `compasslanddfc` | Compass/land transform marks |
| `originpwdfc` | Origins/Planeswalker transform marks |
| `mooneldrazidfc` | Moon/Eldrazi transform marks |
| `waxingandwaningmoondfc` | Waxing/waning moon transform marks |
| `showcase` | Showcase treatment |
| `extendedart` | Extended-art treatment |
| `companion` | Companion frame |
| `etched` | Etched treatment marker |
| `snow` | Snow frame treatment |
| `lesson` | Lesson frame treatment |
| `shatteredglass` | Shattered Glass treatment |
| `convertdfc` | Convert double-faced marks |
| `fandfc` | Fan transform marks |
| `upsidedowndfc` | Upside-down transform marks |

`frame` itself is intentionally **not** listed as a closed domain here because the
maintained Scryfall API types currently model it as an open string.

---

# 18. Recommended storage and history strategy

| Data group | Example fields | Recommended handling |
| --- | --- | --- |
| Identity | `id`, `oracle_id`, `set_id` | Treat as immutable identifiers; detect unexpected changes |
| Stable set master | code, name, release date, set type | Upsert current value; optionally audit corrections |
| Stable printing master | collector number, rarity, set relation, artist, layout | Upsert current value; corrections are exceptional |
| Mutable Oracle master | Oracle text, type line, keywords, color identity | Upsert current value; keep history if rules evolution matters |
| Legal state | format legality | Timestamp/history if historical legality matters |
| Market movement | prices | Timestamped observations, never only a destructive overwrite if trends matter |
| Popularity movement | EDHREC/Penny rank | Timestamped observations if used analytically |
| Preview/image state | image status, high-res flag, preview metadata | Current state is enough for UI; history only if needed |
| Technical delivery | URIs, bulk update time | Replace with latest; normally no business history |

---

# 19. PostgreSQL and SQLite representation

The table below is a **representation comparison**, not the final DeckKernel schema.
The actual data modelling, key strategy and normalization will be decided later.

| Scryfall semantic kind | PostgreSQL representation | SQLite representation | Notes |
| --- | --- | --- | --- |
| UUID | `uuid` or `text` | `TEXT` | SQLite has no native UUID storage class |
| ISO date | `date` | `TEXT` in ISO-8601 form | SQLite date functions work with ISO text |
| ISO date-time | `timestamptz` | `TEXT` in ISO-8601 form | Preserve timezone/UTC information |
| open/free string | `text` | `TEXT` | No arbitrary length limit from Scryfall |
| URI | `text` | `TEXT` | Technical string |
| collector number | `text` | `TEXT` | Must remain text because non-digits occur |
| power/toughness/loyalty/defense | `text` | `TEXT` | Values can contain `*`, `X`, etc. |
| integer identifier from external systems | `bigint` where required | `INTEGER` | External ID, not a DeckKernel PK decision |
| currency/price text | `numeric` after explicit conversion | `NUMERIC` affinity or canonical decimal text | Exact-money strategy comes later |
| boolean | `boolean` | `INTEGER` 0/1 | SQLite has no separate Boolean storage class |
| externally owned vocabulary | lookup/domain/text | lookup table or `TEXT` + checks | Must tolerate future Scryfall values |
| repeated arrays | child relation or array | child relation or JSON text | Choice depends on later query needs |
| JSON metadata | `jsonb` if retained | `TEXT` containing JSON | Only when the raw structure should be preserved |

For the current evaluation, the important point is semantic compatibility, not the final
primary-key design. The later DeckKernel schema can use internal integer primary keys and
treat Scryfall UUIDs as unique external keys; that modelling decision is intentionally
deferred.

Do not introduce arbitrary length limits only because current sample data fits.

---

# 20. Candidate DeckKernel normalization

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

Without deciding the later physical key strategy yet, a useful semantic split would be:

```text
MASTER DATA
   sets
   oracle cards
   printings
   faces
   artists

CURRENT / MOVEMENT STATE
   legalities
   prices
   rankings
   preview/image availability
```

The first-connect lesson intentionally does not implement the complete model. Its first
goal is to prove and teach the infrastructure path.

---

# 21. Upstream references and maintenance rule

This document was checked against the maintained `scryfall/api-types` definitions in
September 2026, including card fields, card faces, sets and the value-domain files for
set type, colors, mana types, rarity, finish, legality, format, language, layout, game,
image status, border color, security stamps and frame effects.

Scryfall owns these external domains. Before turning any current list into a hard
database constraint, verify the upstream definition again. New card mechanics and
product types are a normal part of Magic, so extensibility is a functional requirement,
not merely defensive programming.


---

# 22. Example data and REST calls

This section turns the abstract field descriptions into concrete examples. The JSON
fragments are intentionally shortened for teaching. They show the **shape and meaning**
of Scryfall responses, not a byte-for-byte snapshot of one specific response at a fixed
date. Fields that are irrelevant to the example are omitted.

For current endpoint details, always check the official Scryfall API documentation:

- Cards: https://scryfall.com/docs/api/cards
- Sets: https://scryfall.com/docs/api/sets
- Bulk data: https://scryfall.com/docs/api/bulk-data
- Rulings: https://scryfall.com/docs/api/rulings
- Catalogs: https://scryfall.com/docs/api/catalogs
- Migrations: https://scryfall.com/docs/api/migrations
- Lists / pagination: https://scryfall.com/docs/api/lists
- Card layouts: https://scryfall.com/docs/api/layouts
- Languages: https://scryfall.com/docs/api/languages

Scryfall's API usage FAQ is also relevant for clients:

https://scryfall.com/docs/faqs/i-m-having-trouble-accessing-the-scryfall-api-or-i-m-blocked-17

It explicitly recommends a meaningful `User-Agent`, an `Accept` header, HTTPS/TLS,
reasonable request rates and bulk data for large-scale synchronization.

## 22.1 Bulk-data manifest request

Request:

```http
GET https://api.scryfall.com/bulk-data
Accept: application/json;q=0.9,*/*;q=0.8
User-Agent: adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)
```

Shortened response shape:

```json
{
  "object": "list",
  "has_more": false,
  "data": [
    {
      "object": "bulk_data",
      "id": "<bulk-object-uuid>",
      "type": "default_cards",
      "updated_at": "2026-09-26T21:05:41.063+00:00",
      "uri": "https://api.scryfall.com/bulk-data/<uuid>",
      "name": "Default Cards",
      "description": "...",
      "download_uri": "https://data.scryfall.io/.../default-cards-....json",
      "content_type": "application/json",
      "content_encoding": "gzip",
      "size": 123456789
    }
  ]
}
```

Interpretation:

- `object = list` says that the outer response is a Scryfall list object.
- `data[]` contains the available bulk-data definitions.
- `type = default_cards` is the export used by the first-connect lesson.
- `updated_at` tells us whether the published snapshot changed.
- `download_uri` is the current static-file location.
- The file behind `download_uri` contains many Card objects.

The application should therefore treat the first response like a **manifest**:

```text
manifest says what exists
          |
          v
download_uri points to current snapshot
          |
          v
snapshot contains the actual card objects
```

The manifest is control data, not card master data.

## 22.2 Example object from the bulk file

A shortened printing object can look conceptually like this:

```json
{
  "object": "card",
  "id": "<printing-uuid>",
  "oracle_id": "<oracle-card-uuid>",
  "name": "Lightning Bolt",
  "lang": "en",
  "released_at": "2010-07-16",
  "layout": "normal",
  "mana_cost": "{R}",
  "cmc": 1.0,
  "type_line": "Instant",
  "oracle_text": "Lightning Bolt deals 3 damage to any target.",
  "colors": ["R"],
  "color_identity": ["R"],
  "set_id": "<set-uuid>",
  "set": "m11",
  "set_name": "Magic 2011",
  "collector_number": "149",
  "rarity": "common",
  "games": ["paper", "mtgo"],
  "finishes": ["nonfoil", "foil"],
  "legalities": {
    "modern": "legal",
    "legacy": "legal",
    "commander": "legal"
  },
  "prices": {
    "usd": "2.15",
    "usd_foil": "8.40",
    "eur": "1.80",
    "tix": "0.05"
  }
}
```

The important lesson is that this is a **printing object**:

- `id` identifies this exact edition;
- `oracle_id` identifies the abstract Lightning Bolt across editions;
- `set_id`, `set` and `set_name` say where this edition was published;
- game-rule fields describe what the card does;
- printing fields describe this concrete edition;
- legalities and prices are current state/movement data.

The first-connect parser deliberately takes only a small subset and normalizes the
repeated set information:

```text
input card object
   |
   +--> set_id + set + set_name
   |       |
   |       +--> one deduplicated Set row
   |
   +--> id + oracle_id + name + set_id + released_at
           |
           +--> one Card-printing row
```

## 22.3 Get one named card through REST

For an interactive application, a complete bulk download is unnecessary when the user
only asks for one card.

Example request:

```http
GET https://api.scryfall.com/cards/named?exact=Lightning%20Bolt
Accept: application/json;q=0.9,*/*;q=0.8
User-Agent: adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)
```

The response is one Card object with the same general shape as a card object in the bulk
file.

This is an important architectural point:

```text
bulk file Card object
        and
REST Card object
        |
        +--> same conceptual Scryfall entity
```

The access path differs; the data semantics do not.

A REST lookup is useful for:

- one card requested by a user;
- filling a missing local object;
- validating a specific Scryfall ID;
- interactive UI operations.

Bulk data is useful when most or all cards are required.

## 22.4 Search cards through REST

Example:

```http
GET https://api.scryfall.com/cards/search?q=type%3Adragon
```

Shortened list response:

```json
{
  "object": "list",
  "total_cards": 1234,
  "has_more": true,
  "next_page": "https://api.scryfall.com/cards/search?...&page=2",
  "data": [
    {
      "object": "card",
      "id": "<uuid>",
      "name": "Example Dragon",
      "...": "..."
    }
  ]
}
```

Meaning:

- `data[]` is the current page of Card objects;
- `has_more` tells the client whether another page exists;
- `next_page` is the REST URI for the next page;
- `total_cards` is the number of cards across all result pages.

A client should follow `next_page` instead of constructing undocumented pagination
logic itself.

See the official list documentation:

https://scryfall.com/docs/api/lists

## 22.5 Set REST example

Request one set by code:

```http
GET https://api.scryfall.com/sets/m11
```

Shortened response:

```json
{
  "object": "set",
  "id": "<set-uuid>",
  "code": "m11",
  "name": "Magic 2011",
  "set_type": "core",
  "released_at": "2010-07-16",
  "card_count": 249,
  "digital": false,
  "foil_only": false,
  "nonfoil_only": false,
  "search_uri": "https://api.scryfall.com/cards/search?...",
  "scryfall_uri": "https://scryfall.com/sets/m11"
}
```

For a viewer unfamiliar with Magic:

- `Magic 2011` is the product/release;
- `m11` is its compact code;
- `core` describes the kind of product;
- `card_count` describes Scryfall's current known object count for that set;
- the set object does not contain all card objects inline;
- `search_uri` points to a REST query that can enumerate cards in the set.

This is a classic master-data object with a few fields, such as current counts and URIs,
that can still change.

## 22.6 Rulings REST example

Cards can have additional explanatory rules notes.

Conceptual request:

```http
GET https://api.scryfall.com/cards/<scryfall-card-id>/rulings
```

Shortened response:

```json
{
  "object": "list",
  "has_more": false,
  "data": [
    {
      "object": "ruling",
      "oracle_id": "<oracle-card-uuid>",
      "source": "wotc",
      "published_at": "2024-01-12",
      "comment": "Example explanatory ruling text."
    }
  ]
}
```

A ruling is not a new printing and not a replacement for Oracle text. It is an additional
dated explanation of a card's rules behavior.

This makes rulings naturally suitable for a separate entity:

```text
Oracle card 1
     |
     +--> 0..n rulings
```

Official documentation:

https://scryfall.com/docs/api/rulings

## 22.7 Catalog REST example

Catalogs are useful for discovering the values Scryfall currently knows.

A catalog response has the form:

```json
{
  "object": "catalog",
  "uri": "https://api.scryfall.com/catalog/...",
  "total_values": 3,
  "data": [
    "Example value A",
    "Example value B",
    "Example value C"
  ]
}
```

This is particularly useful for lessons about domains: instead of assuming that every
external vocabulary is fixed forever, a client can inspect Scryfall-maintained catalogs.

Official documentation:

https://scryfall.com/docs/api/catalogs

## 22.8 Migration REST example

Migrations describe exceptional corrections to Scryfall object identities.

Shortened example:

```json
{
  "object": "migration",
  "id": "<migration-uuid>",
  "performed_at": "2026-01-10",
  "migration_strategy": "merge",
  "old_scryfall_id": "<retired-card-uuid>",
  "new_scryfall_id": "<replacement-card-uuid>",
  "note": "Objects represented the same printing and were merged."
}
```

Meaning:

```text
old Scryfall ID
      |
      | merge
      v
new Scryfall ID
```

or, for a delete:

```text
old Scryfall ID
      |
      X  no replacement
```

For our later data model this is the reason to treat Scryfall IDs as **external unique
keys**, not as the conceptual internal identity of DeckKernel itself.

Official documentation:

https://scryfall.com/docs/api/migrations

## 22.9 Multi-face card example

A double-faced card differs fundamentally from a simple card. Important game fields move
to `card_faces[]`.

Shortened example:

```json
{
  "object": "card",
  "id": "<printing-uuid>",
  "oracle_id": "<oracle-uuid>",
  "layout": "transform",
  "name": "Front Name // Back Name",
  "card_faces": [
    {
      "name": "Front Name",
      "mana_cost": "{1}{G}",
      "type_line": "Creature — Example",
      "oracle_text": "Example front-side rules.",
      "power": "2",
      "toughness": "2"
    },
    {
      "name": "Back Name",
      "mana_cost": "",
      "type_line": "Creature — Example",
      "oracle_text": "Example back-side rules.",
      "power": "4",
      "toughness": "4"
    }
  ]
}
```

A viewer should therefore not assume that `name`, `mana_cost`, `oracle_text`,
`power` or images always live at the root object.

The `layout` field determines which representation is valid.

Official layout documentation:

https://scryfall.com/docs/api/layouts

## 22.10 SQLite and PostgreSQL example representation

The following is still **not** the final DeckKernel schema. It only demonstrates how the
same externally supplied values can be represented in both databases.

Scryfall input:

```json
{
  "id": "4f0d...",
  "oracle_id": "a8b1...",
  "name": "Lightning Bolt",
  "released_at": "2010-07-16",
  "digital": false
}
```

Possible PostgreSQL representation:

```text
scryfall_id   uuid/text     -> 4f0d...
oracle_id     uuid/text     -> a8b1...
name          text          -> Lightning Bolt
released_at   date          -> 2010-07-16
digital       boolean       -> false
```

Possible SQLite representation:

```text
scryfall_id   TEXT          -> 4f0d...
oracle_id     TEXT          -> a8b1...
name          TEXT          -> Lightning Bolt
released_at   TEXT          -> 2010-07-16
digital       INTEGER       -> 0
```

The later DeckKernel model may use its own integer primary keys and keep the Scryfall UUIDs
as unique external identifiers. That decision remains outside the scope of this first
structure assessment.

## 22.11 Recommended REST versus bulk choice

| Requirement | Prefer | Reason |
| --- | --- | --- |
| Initial complete local mirror | Bulk data | One large transfer, avoids many API calls |
| Regular full refresh | Bulk manifest + bulk file | Compare `updated_at`, download only when needed |
| Show one card interactively | REST | Small targeted request |
| Search by user query | REST search | Server performs Scryfall search semantics |
| Retrieve one set | REST | No need for full card snapshot |
| Read rulings for one card | REST | Separate object family |
| Discover current vocabularies | Catalog REST endpoints | Domain discovery |
| Repair retired Scryfall IDs | Migration REST endpoints | Explicit identity-correction feed |
| Offline analysis over all cards | Bulk data | Local processing is efficient and API-friendly |



---

# 23. Domain sources: Catalog API, documented domains and observed values

This section answers a different question from the field descriptions above:

> Can DeckKernel obtain the allowed values directly from Scryfall, or would we merely be
> observing values that happen to occur in the current data?

That distinction matters. A value observed in bulk data proves only that the value is
currently used. It does **not** prove that the set of observed values is the complete
allowed domain.

For domain handling we therefore distinguish five source classes:

| Source class | Meaning |
| --- | --- |
| **Catalog API** | Scryfall exposes a dedicated REST endpoint returning the current value list |
| **Documented domain** | Scryfall documentation / maintained API types define the values, but there is no dedicated Catalog endpoint |
| **Observed from Bulk** | Values can be collected with DISTINCT/grouping from the current bulk snapshot |
| **Observed from REST objects** | Values can be collected from current REST responses but are not separately delivered |
| **Open string** | No closed domain should be assumed |

## 23.1 Catalog response shape

Every Catalog endpoint returns the same basic structure:

```http
GET https://api.scryfall.com/catalog/creature-types
Accept: application/json;q=0.9,*/*;q=0.8
User-Agent: adecc-DeckKernel/0.1 (+https://github.com/adeccscholar/DeckKernel)
```

Conceptual response:

```json
{
  "object": "catalog",
  "uri": "https://api.scryfall.com/catalog/creature-types",
  "total_values": 3,
  "data": [
    "Dragon",
    "Elf",
    "Wizard"
  ]
}
```

The real `data[]` list is much larger. Catalog responses contain **values only**; they
do not provide a stable numeric ID, description, localized label or version number for
each value.

That has an architectural consequence for DeckKernel:

```text
Scryfall Catalog
      |
      | value string only
      v
local domain value
      |
      +--> optional internal integer key
      +--> Scryfall value string as UNIQUE external value
      +--> local description / translation / ordering
      +--> active / first_seen / last_seen metadata
```

The exact later schema is intentionally not decided here, but an extension mechanism will
be necessary if DeckKernel wants descriptions, translations or stable internal IDs.

## 23.2 Catalog endpoints currently available

The following Catalog endpoints are exposed by Scryfall clients that track the documented
Catalog API:

| Catalog | REST endpoint | Meaning | Expected growth |
| --- | --- | --- | --- |
| Card names | `/catalog/card-names` | English non-token card names known to Scryfall | **High**; every new set can add names |
| Artist names | `/catalog/artist-names` | Artist names used on Scryfall printings | **High** |
| Word bank | `/catalog/word-bank` | English words occurring on Magic cards | **High** |
| Creature types | `/catalog/creature-types` | Creature subtype vocabulary such as Dragon or Elf | **Medium/High**; new types appear |
| Planeswalker types | `/catalog/planeswalker-types` | Planeswalker subtype vocabulary | **Low/Medium** |
| Land types | `/catalog/land-types` | Land subtype vocabulary | **Low/Medium** |
| Artifact types | `/catalog/artifact-types` | Artifact subtype vocabulary | **Medium** |
| Enchantment types | `/catalog/enchantment-types` | Enchantment subtype vocabulary | **Medium** |
| Spell types | `/catalog/spell-types` | Instant/sorcery spell subtype vocabulary | **Medium** |
| Powers | `/catalog/powers` | Power values currently used on cards | **Medium** |
| Toughnesses | `/catalog/toughnesses` | Toughness values currently used on cards | **Medium** |
| Loyalties | `/catalog/loyalties` | Loyalty values currently used on Planeswalkers | **Medium** |
| Watermarks | `/catalog/watermarks` | Printed watermark names | **Medium** |
| Keyword abilities | `/catalog/keyword-abilities` | Rules keyword abilities such as Flying | **High over long time** |
| Keyword actions | `/catalog/keyword-actions` | Rules keyword actions such as Destroy/Exile-style named actions | **Medium/High** |
| Ability words | `/catalog/ability-words` | Italicized ability-word vocabulary | **Medium** |

Official Catalog documentation:

https://scryfall.com/docs/api/catalogs

### Practical use

These endpoints are the strongest source available for those vocabularies because the
value list is delivered separately by Scryfall rather than inferred from card rows.

For large catalogs such as card names, artist names, word bank or creature types, this
document deliberately does not duplicate all live values. They should be fetched from the
Catalog endpoint when needed.

## 23.3 Catalog values that are useful as examples

The values below are examples of the live catalog categories, not a complete hard-coded
domain.

### Creature types

Typical values include:

`Angel`, `Beast`, `Bird`, `Cat`, `Cleric`, `Dragon`, `Elf`, `Goblin`,
`Human`, `Knight`, `Merfolk`, `Soldier`, `Vampire`, `Warrior`, `Wizard`,
`Zombie`.

The authoritative current list should come from:

```http
GET https://api.scryfall.com/catalog/creature-types
```

### Powers and toughnesses

These catalogs demonstrate why combat values are strings rather than integers.

Typical values can include ordinary numbers as well as symbolic values such as:

`0`, `1`, `2`, `3`, `4`, `5`, `*`.

The exact current list must be retrieved from:

```http
GET https://api.scryfall.com/catalog/powers
GET https://api.scryfall.com/catalog/toughnesses
```

### Keyword abilities

Typical examples include:

`Flying`, `First strike`, `Double strike`, `Trample`, `Vigilance`,
`Deathtouch`, `Lifelink`, `Menace`, `Haste`, `Ward`.

The current complete list comes from:

```http
GET https://api.scryfall.com/catalog/keyword-abilities
```

### Watermarks

Watermarks are visual faction/product marks printed behind rules text on some cards.
The current values should be retrieved from:

```http
GET https://api.scryfall.com/catalog/watermarks
```

## 23.4 Domains without a dedicated Catalog endpoint

The following important domains are **not** represented by the Catalog endpoint list
above. Their complete domain therefore comes from Scryfall documentation / maintained API
types, while current usage can additionally be observed from bulk or REST objects.

| Domain | Separate Catalog? | Primary source | Can be observed in Bulk? | Extension expectation |
| --- | ---: | --- | ---: | --- |
| SetType | no | Documented domain / API type | yes | **Medium**; new product categories are possible |
| Color | no | Documented Magic/Scryfall domain | yes | **Very low** |
| Mana produced | no | Documented domain | yes | **Very low** |
| Rarity | no | Documented domain | yes | **Low/Medium** |
| Finish | no | Documented domain | yes | **Medium**; new treatments can appear |
| Legality status | no | Documented domain | yes | **Very low** |
| Format | no dedicated Catalog | Documented API type | yes through `legalities` keys | **High** over time |
| Language | no dedicated Catalog | Documented language list | yes | **Low/Medium** |
| Layout | no dedicated Catalog | Documented layout domain | yes | **Medium/High** as new mechanics appear |
| Game/platform | no dedicated Catalog | Documented domain | yes | **Low/Medium** |
| ImageStatus | no | Documented domain | yes | **Low** |
| BorderColor | no | Documented domain | yes | **Low** |
| SecurityStamp | no | Documented domain | yes | **Medium** |
| FrameEffect | no | Documented domain | yes | **High**; new visual treatments appear |
| Frame | no closed domain | Open string in maintained API types | yes | **High / open** |
| PromoType | no dedicated Catalog | Externally maintained string vocabulary | yes | **High / open** |

### Important interpretation rule

For these domains, a DISTINCT scan of the bulk file gives:

> values currently present in this snapshot

It does **not** give:

> every value Scryfall may legally return

Therefore the bulk file must not be treated as the domain definition.

## 23.5 Existing documented domain values

For completeness, the currently documented values already listed earlier in this document
can be summarized by source:

| Domain | Current values in this document | Source kind |
| --- | --- | --- |
| Color | W, U, B, R, G, C where applicable | Documented domain |
| Rarity | common, uncommon, rare, mythic, special, bonus | Documented domain |
| Finish | nonfoil, foil, etched | Documented domain |
| Legality | legal, not_legal, restricted, banned | Documented domain |
| Language | en, es, fr, de, it, pt, ja, ko, ru, zhs, zht, he, la, grc, ar, sa, ph | Documented domain |
| Game | paper, mtgo, arena, astral, sega | Documented domain |
| ImageStatus | missing, placeholder, lowres, highres_scan | Documented domain |
| BorderColor | black, white, borderless, silver, gold | Documented domain |
| SecurityStamp | oval, triangle, acorn, circle, arena, heart | Documented domain |
| SetType | values listed in section 17.1 | Documented domain |
| Layout | values listed in section 17.9 | Documented domain |
| FrameEffect | values listed in section 17.14 | Documented domain |

Those lists should be considered snapshots of the documented domain as of this review, not
permanent DeckKernel enums.

## 23.6 Domains that should explicitly support extension

The following groups are particularly likely to grow and should not be modelled as code
that fails on an unknown value:

| Domain/group | Why growth is likely |
| --- | --- |
| Card names | Every new card adds values |
| Artist names | New artists enter the game |
| Creature/other card subtypes | New mechanics and creature concepts add types |
| Keyword abilities/actions | New rules mechanics are introduced regularly |
| Ability words | New mechanics can introduce new ability words |
| SetType | New product categories can appear |
| Layout | New physical/gameplay card structures are introduced |
| Format | New organized/community/digital formats appear |
| Finish | New printing treatments are introduced |
| FrameEffect | New showcase/visual treatments appear frequently |
| PromoType | Marketing/product categories change regularly |
| Watermarks | New factions/products may add marks |

By contrast, values such as the five Magic colors or legality states are much more stable,
although a tolerant parser is still preferable for external data.

## 23.7 Proposed extension mechanism concept

No final DeckKernel schema is defined here, but the data source strongly suggests the
following capabilities for later design:

```text
external value
   |
   +-- source             Catalog / documented / observed
   +-- external text      exact Scryfall value
   +-- internal ID        optional integer key
   +-- display text       our explanation
   +-- translation        optional
   +-- first_seen         optional
   +-- last_seen          optional
   +-- active             optional
   +-- unknown/new flag   optional
```

A synchronization process could then:

1. fetch Catalog-backed domains directly from Scryfall;
2. compare them with locally known values;
3. insert previously unseen values without breaking the import;
4. flag new values for review and documentation;
5. keep local descriptions/translations separate from Scryfall's raw value.

For non-Catalog domains, the same extension mechanism can use the documented list as the
baseline and report unknown values discovered in REST/Bulk data.

This lets DeckKernel remain strict enough to notice changes without becoming brittle when
Scryfall adds a new value.
