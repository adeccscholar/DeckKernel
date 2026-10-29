-- SPDX-FileCopyrightText: 2026 adecc Systemhaus GmbH
-- SPDX-License-Identifier: LicenseRef-PolyForm-Noncommercial-1.0.0
-- Optional editorial descriptions, keyed strictly by code.
-- Never replace an existing description; never modify notice or code.
BEGIN;

UPDATE deckkernel_test.dom_card_types AS d
SET description = v.description
FROM (VALUES
 ('Artifact', 'A card type for objects such as devices, tools, and constructs. An artifact permanent remains on the battlefield until an effect or rule moves it.'),
 ('Battle', 'A permanent card type representing a conflict. Battles have defense counters and rules governing protection and defeat; their subtype determines additional mechanics.'),
 ('Conspiracy', 'A card type used in special draft formats. Conspiracy cards start in the command zone and are not included in the main deck.'),
 ('Creature', 'A permanent card type that can normally attack and block and has power and toughness. Creature subtypes do not themselves grant abilities.'),
 ('Dungeon', 'A nontraditional card type used with venture mechanics. Dungeons exist outside the deck and track progress through rooms.'),
 ('Enchantment', 'A permanent card type representing a sustained magical effect. Its individual rules text and any subtypes specify its behavior.'),
 ('Instant', 'A spell type that can normally be cast whenever its player has priority, subject to applicable restrictions.'),
 ('Kindred', 'A card type that allows noncreature cards to have creature subtypes; historically called Tribal.'),
 ('Land', 'A permanent card type normally played as a special action rather than cast as a spell. Playing a land follows the land-play rules.'),
 ('Phenomenon', 'A nontraditional Planechase card type that produces a one-time event when encountered.'),
 ('Plane', 'A nontraditional Planechase card type representing a location with abilities relevant to planar play.'),
 ('Planeswalker', 'A permanent card type representing an ally with loyalty counters and loyalty abilities.'),
 ('Scheme', 'A nontraditional Archenemy card type whose effects are used in Archenemy games.'),
 ('Sorcery', 'A spell type normally cast only during its controller''s main phase while the stack is empty.'),
 ('Vanguard', 'A nontraditional card type used in the Vanguard variant to modify a player''s starting conditions and abilities.')
) AS v(code,description)
WHERE d.code=v.code AND d.description IS NULL;

UPDATE deckkernel_test.dom_supertypes AS d
SET description=v.description
FROM (VALUES
 ('Basic', 'A supertype most commonly seen on lands. It matters to deck construction and effects that refer to basic lands; it does not by itself supply a mana ability.'),
 ('Legendary', 'A supertype identifying permanents subject to the legend rule and referenced by many card effects.'),
 ('Snow', 'A supertype identifying a snow object. Snow is not a color and does not inherently generate snow mana.'),
 ('World', 'A supertype for permanents subject to the world rule.'),
 ('Ongoing', 'A supertype for certain scheme cards that remain active until their abandonment condition is met.')
) AS v(code,description)
WHERE d.code=v.code AND d.description IS NULL;

UPDATE deckkernel_test.dom_land_types AS d
SET description=v.description
FROM (VALUES
 ('Plains','A basic land type that intrinsically grants the ability to tap for white mana.'),
 ('Island','A basic land type that intrinsically grants the ability to tap for blue mana.'),
 ('Swamp','A basic land type that intrinsically grants the ability to tap for black mana.'),
 ('Mountain','A basic land type that intrinsically grants the ability to tap for red mana.'),
 ('Forest','A basic land type that intrinsically grants the ability to tap for green mana.'),
 ('Desert','A land subtype used by cards and effects referencing Deserts; it supplies no intrinsic mana ability.'),
 ('Cave','A land subtype used by cards and effects referencing Caves; it supplies no intrinsic mana ability.'),
 ('Gate','A land subtype used by cards and effects referencing Gates; it supplies no intrinsic mana ability.'),
 ('Lair','A land subtype used by cards and effects referencing Lairs; it supplies no intrinsic mana ability.'),
 ('Locus','A land subtype used by cards and effects referencing Loci; it supplies no intrinsic mana ability.'),
 ('Mine','A land subtype appearing on Urza lands; it supplies no intrinsic mana ability.'),
 ('Power-Plant','A land subtype appearing on Urza lands; it supplies no intrinsic mana ability.'),
 ('Tower','A land subtype appearing on Urza lands; it supplies no intrinsic mana ability.'),
 ('Sphere','A land subtype used by cards and effects referencing Spheres; it supplies no intrinsic mana ability.'),
 ('Urza''s','A land subtype appearing on some lands associated with Urza; it supplies no intrinsic mana ability.'),
 ('Town','A land subtype used by cards and effects referencing Towns; it supplies no intrinsic mana ability.')
) AS v(code,description)
WHERE d.code=v.code AND d.description IS NULL;

UPDATE deckkernel_test.dom_artifact_types AS d
SET description=v.description
FROM (VALUES
 ('Clue','An artifact subtype often associated with investigating. Individual Clues may have abilities beyond the commonly granted sacrifice-and-draw ability.'),
 ('Contraption','An artifact subtype used with the assemble and crank mechanics in Unstable.'),
 ('Equipment','An artifact subtype generally attached to a creature using equip or other attachment effects. Equipment does not intrinsically grant a particular bonus.'),
 ('Food','An artifact subtype often associated with gaining life by sacrificing the artifact. Its exact abilities are determined by the object and applicable effects.'),
 ('Fortification','An artifact subtype that can attach to a land using fortify or other permitted effects.'),
 ('Gold','An artifact subtype associated with mana-producing token artifacts; the specific ability is determined by the token or card.'),
 ('Incubator','An artifact subtype used for incubator tokens that can transform into creatures.'),
 ('Junk','An artifact subtype often used for disposable token artifacts; its abilities depend on the effect creating it.'),
 ('Map','An artifact subtype often used for token artifacts that explore; the token or card defines its abilities.'),
 ('Powerstone','An artifact subtype associated with special mana-producing tokens. Its mana restrictions come from the token ability, not merely from the subtype.'),
 ('Treasure','An artifact subtype commonly used for sacrifice-to-produce-mana tokens. The individual object specifies its mana ability.'),
 ('Vehicle','An artifact subtype often associated with crew or other means to become an artifact creature.'),
 ('Blood','An artifact subtype associated with discard-and-draw token abilities; the object defines its actual ability.'),
 ('Bobblehead','An artifact subtype appearing on collectible artifacts with related effects.'),
 ('Attraction','An artifact subtype used with the Attraction mechanic and its separate deck.'),
 ('Spacecraft','An artifact subtype associated with space-themed mechanics; its rules text determines its abilities.')
) AS v(code,description)
WHERE d.code=v.code AND d.description IS NULL;

UPDATE deckkernel_test.dom_rarities AS d
SET description=v.description
FROM (VALUES
 ('common','A common-rarity printing in a product using rarity classifications.'),
 ('uncommon','An uncommon-rarity printing, typically distributed less frequently than common.'),
 ('rare','A rare-rarity printing, typically distributed less frequently than uncommon.'),
 ('mythic','A mythic-rare printing, typically distributed less frequently than rare in products using mythic rarities.'),
 ('special','A Scryfall rarity classification for printings outside the normal common-to-mythic distribution.'),
 ('bonus','A Scryfall rarity classification used for bonus printings outside the usual product rarity allocation.')
) AS v(code,description)
WHERE d.code=v.code AND d.description IS NULL;

-- All existing creature codes get a meaningful rules-neutral description.
-- This does not claim they have abilities, statistics, or characteristics.
UPDATE deckkernel_test.dom_creature_types
SET description='A recognized Magic creature subtype (' || code || '). This subtype may be referenced by card effects and does not by itself grant abilities or characteristics.'
WHERE description IS NULL;

COMMIT;

-- Review remaining codes in the other five domains before extending coverage.
SELECT 'artifact' AS domain, code FROM deckkernel_test.dom_artifact_types WHERE description IS NULL
UNION ALL SELECT 'card', code FROM deckkernel_test.dom_card_types WHERE description IS NULL
UNION ALL SELECT 'land', code FROM deckkernel_test.dom_land_types WHERE description IS NULL
UNION ALL SELECT 'rarity', code FROM deckkernel_test.dom_rarities WHERE description IS NULL
UNION ALL SELECT 'supertype', code FROM deckkernel_test.dom_supertypes WHERE description IS NULL
ORDER BY domain, code;
