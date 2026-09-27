# Scryfall data model for DeckKernel

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

# 4. BulkData

## Meaning in Scryfall

`BulkData` is not a Magic game entity. It is a delivery mechanism. Scryfall publishes
large downloadable snapshots so an application can synchronize many cards efficiently
instead of issuing one HTTP request per card.

For the lesson we use the `default_cards` export.

| Attribute | What it means | Domain / type | Change expectation | Suggested PostgreSQL type |
| --- | --- | --- | --- | --- |
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

# 16. Domain tables

The following domains are externally owned by Scryfall/Magic. DeckKernel should not
assume that today's list is permanent. Lookup tables or tolerant string-backed domains
are often safer than rigid PostgreSQL enums.

## 16.1 SetType

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

## 16.2 Color

| Value | Meaning |
| --- | --- |
| `W` | White |
| `U` | Blue |
| `B` | Black |
| `R` | Red |
| `G` | Green |
| `C` | Colorless token used by some API fields; colorless is not one of Magic's five colors |

## 16.3 Mana produced

The maintained API type currently uses the same symbolic values:

| Value | Meaning |
| --- | --- |
| `W` | White mana |
| `U` | Blue mana |
| `B` | Black mana |
| `R` | Red mana |
| `G` | Green mana |
| `C` | Colorless mana |

## 16.4 Rarity

| Value | Meaning |
| --- | --- |
| `common` | Common rarity |
| `uncommon` | Uncommon rarity |
| `rare` | Rare rarity |
| `mythic` | Mythic rare |
| `special` | Special/nonstandard rarity classification |
| `bonus` | Bonus-sheet/special bonus classification |

Rarity is a property of a **printing**, not necessarily of the abstract Oracle card.

## 16.5 Finish

| Value | Meaning |
| --- | --- |
| `nonfoil` | Normal nonfoil treatment |
| `foil` | Traditional foil treatment |
| `etched` | Etched-foil treatment |

## 16.6 Legality status

| Value | Meaning |
| --- | --- |
| `legal` | Card may currently be played in the format |
| `not_legal` | Card is outside the format/card pool |
| `restricted` | Card is legal only in a restricted quantity where the format supports this |
| `banned` | Card belongs to the format's card pool but is currently banned |

## 16.7 Format

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

## 16.8 Language

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

## 16.9 Layout

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

## 16.10 Game / platform

| Value | Meaning |
| --- | --- |
| `paper` | Physical tabletop Magic |
| `mtgo` | Magic Online |
| `arena` | MTG Arena |
| `astral` | Historic MicroProse/Astral digital game content |
| `sega` | Historic Sega Dreamcast game content |

## 16.11 ImageStatus

| Value | Meaning | Typical stability |
| --- | --- | --- |
| `missing` | Scryfall has no image yet | Highly temporary |
| `placeholder` | Temporary generated placeholder | Temporary |
| `lowres` | Low-resolution preview image | Temporary |
| `highres_scan` | High-resolution image/scan available | Usually final state |

## 16.12 BorderColor

| Value | Meaning |
| --- | --- |
| `black` | Black physical border |
| `white` | White physical border |
| `borderless` | Borderless treatment |
| `silver` | Silver physical border |
| `gold` | Gold physical border |

This is a description of the physical printing, not a legality rule.

## 16.13 SecurityStamp

| Value | Meaning |
| --- | --- |
| `oval` | Oval security stamp |
| `triangle` | Triangle stamp |
| `acorn` | Acorn stamp used for particular nonstandard cards |
| `circle` | Circular stamp |
| `arena` | Arena-themed stamp |
| `heart` | Heart-shaped stamp |

## 16.14 FrameEffect

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

# 17. Recommended storage and history strategy

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

# 18. Length and constraint policy

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
| externally owned vocabulary | lookup/domain with extension strategy |
| repeated arrays | child relation when queried relationally |

Do not introduce arbitrary length limits only because current sample data fits.

---

# 19. Candidate DeckKernel normalization

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

A useful architectural split would be:

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

# 20. Upstream references and maintenance rule

This document was checked against the maintained `scryfall/api-types` definitions in
September 2026, including card fields, card faces, sets and the value-domain files for
set type, colors, mana types, rarity, finish, legality, format, language, layout, game,
image status, border color, security stamps and frame effects.

Scryfall owns these external domains. Before turning any current list into a hard
database constraint, verify the upstream definition again. New card mechanics and
product types are a normal part of Magic, so extensibility is a functional requirement,
not merely defensive programming.
