/* ************************************************************************
*   File: spells.h                                      Part of Bylins    *
*  Usage: header file: constants and fn prototypes for spell system       *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
************************************************************************ */

#ifndef _SPELLS_H_
#define _SPELLS_H_

#include "skills.h"
#include "structs.h"    // there was defined type "byte" if it had been missing
#include "classes/class_constants.h"

#include <optional>

struct ROOM_DATA;    // forward declaration to avoid inclusion of room.hpp and any dependencies of that header.

// *******************************
// * Spells type                 *
// *******************************

#define MTYPE_NEUTRAL        (1 << 0)
#define MTYPE_AGGRESSIVE    (1 << 1)

// *******************************
// * Spells class                *
// *******************************

#define STYPE_NEUTRAL    0
#define STYPE_AIR    1
#define STYPE_FIRE    2
#define STYPE_WATER    3
#define STYPE_EARTH    4
#define STYPE_LIGHT    6
#define STYPE_DARK    7
#define STYPE_MIND    8
#define STYPE_LIFE    9

#define MAG_DAMAGE            (1 << 0)
#define MAG_AFFECTS            (1 << 1)
#define MAG_UNAFFECTS        (1 << 2)
#define MAG_POINTS            (1 << 3)
#define MAG_ALTER_OBJS        (1 << 4)
#define MAG_GROUPS            (1 << 5)
#define MAG_MASSES            (1 << 6)
#define MAG_AREAS            (1 << 7)
#define MAG_SUMMONS            (1 << 8)
#define MAG_CREATIONS        (1 << 9)
#define MAG_MANUAL            (1 << 10)
#define MAG_WARCRY        (1 << 11)
#define MAG_NEED_CONTROL    (1 << 12) //Маг должен "контролировать" это заклинание
// А чего это тут дырка Ж)
#define NPC_DAMAGE_PC           (1 << 16)
#define NPC_DAMAGE_PC_MINHP     (1 << 17)
#define NPC_AFFECT_PC           (1 << 18)
#define NPC_AFFECT_PC_CASTER    (1 << 19)
#define NPC_AFFECT_NPC          (1 << 20)
#define NPC_UNAFFECT_NPC        (1 << 21)
#define NPC_UNAFFECT_NPC_CASTER (1 << 22)
#define NPC_DUMMY               (1 << 23)
#define MAG_ROOM            (1 << 24)
// Данный флаг используется для указания где
// чар может находиться чтобы аффекты от закла продолжали действовать
#define MAG_CASTER_INROOM       (1 << 25) // Аффект от этого спелла действует пока кастер в комнате //
#define MAG_CASTER_INWORLD      (1 << 26) // висит пока кастер в мире //
#define MAG_CASTER_ANYWHERE     (1 << 27) // висит пока не упадет сам //
#define MAG_CASTER_INWORLD_DELAY     (1 << 28) // висит пока кастер в мире, плюс таймер после ухода кастера//
#define NPC_CALCULATE           (0xff << 16)
// *** Extra attack bit flags //
#define EAF_PARRY       (1 << 0)
#define EAF_BLOCK       (1 << 1)
#define EAF_TOUCH       (1 << 2)
#define EAF_PROTECT     (1 << 3)
#define EAF_DEVIATE     (1 << 4)
#define EAF_MIGHTHIT    (1 << 5)
#define EAF_STUPOR      (1 << 6)
#define EAF_SLOW        (1 << 7)
#define EAF_PUNCTUAL    (1 << 8)
#define EAF_AWAKE       (1 << 9)
#define EAF_FIRST       (1 << 10)
#define EAF_SECOND      (1 << 11)
#define EAF_STAND       (1 << 13)
#define EAF_USEDRIGHT   (1 << 14)
#define EAF_USEDLEFT    (1 << 15)
#define EAF_MULTYPARRY  (1 << 16)
#define EAF_SLEEP       (1 << 17)
#define EAF_IRON_WIND   (1 << 18)
#define EAF_AUTOBLOCK   (1 << 19) // автоматический блок щитом в осторожном стиле
#define EAF_POISONED    (1 << 20) // отравление с пушек раз в раунд
#define EAF_FIRST_POISON (1 << 21) // отравление цели первый раз за бой

#define TYPE_UNDEFINED              -1
#define SPELL_RESERVED_DBC          0    // SKILL NUMBER ZERO -- RESERVED //

// PLAYER SPELLS TYPES //
#define   SPELL_KNOW   (1 << 0)
#define   SPELL_TEMP   (1 << 1)
#define   SPELL_POTION (1 << 2)
#define   SPELL_WAND   (1 << 3)
#define   SPELL_SCROLL (1 << 4)
#define   SPELL_ITEMS  (1 << 5)
#define   SPELL_RUNES  (1 << 6)

/// Flags for ingredient items (ITEM_INGREDIENT)
enum EIngredientFlag {
	ITEM_RUNES = 1 << 0,
	ITEM_CHECK_USES = 1 << 1,
	ITEM_CHECK_LAG = 1 << 2,
	ITEM_CHECK_LEVEL = 1 << 3,
	ITEM_DECAY_EMPTY = 1 << 4
};

template<>
EIngredientFlag ITEM_BY_NAME<EIngredientFlag>(const std::string &name);
template<>
const std::string &NAME_BY_ITEM<EIngredientFlag>(const EIngredientFlag item);

#define   MI_LAG1s       (1 << 0)
#define   MI_LAG2s       (1 << 1)
#define   MI_LAG4s       (1 << 2)
#define   MI_LAG8s       (1 << 3)
#define   MI_LAG16s      (1 << 4)
#define   MI_LAG32s      (1 << 5)
#define   MI_LAG64s      (1 << 6)
#define   MI_LAG128s     (1 << 7)
#define   MI_LEVEL1      (1 << 8)
#define   MI_LEVEL2      (1 << 9)
#define   MI_LEVEL4      (1 << 10)
#define   MI_LEVEL8      (1 << 11)
#define   MI_LEVEL16     (1 << 12)

// PLAYER SPELLS -- Numbered from 1 to SPELLS_COUNT //
enum ESpell : int {
	SPELL_NO_SPELL = 0,
	SPELL_ARMOR = 1,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_TELEPORT = 2,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_BLESS = 3,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_BLINDNESS = 4,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_BURNING_HANDS = 5,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CALL_LIGHTNING = 6,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CHARM = 7,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CHILL_TOUCH = 8,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CLONE = 9,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_COLOR_SPRAY = 10,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CONTROL_WEATHER = 11,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CREATE_FOOD = 12,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CREATE_WATER = 13,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CURE_BLIND = 14,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CURE_CRITIC = 15,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CURE_LIGHT = 16,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_CURSE = 17,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_DETECT_ALIGN = 18,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_DETECT_INVIS = 19,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_DETECT_MAGIC = 20,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_DETECT_POISON = 21,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_DISPEL_EVIL = 22,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_EARTHQUAKE = 23,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_ENCHANT_WEAPON = 24,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_ENERGY_DRAIN = 25,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_FIREBALL = 26,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_HARM = 27,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_HEAL = 28,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_INVISIBLE = 29,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_LIGHTNING_BOLT = 30,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_LOCATE_OBJECT = 31,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_MAGIC_MISSILE = 32,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_POISON = 33,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_PROT_FROM_EVIL = 34,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_REMOVE_CURSE = 35,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_SANCTUARY = 36,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_SHOCKING_GRASP = 37,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_SLEEP = 38,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_STRENGTH = 39,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_SUMMON = 40,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_PATRONAGE = 41,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_WORD_OF_RECALL = 42,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_REMOVE_POISON = 43,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_SENSE_LIFE = 44,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_ANIMATE_DEAD = 45,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_DISPEL_GOOD = 46,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_GROUP_ARMOR = 47,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_GROUP_HEAL = 48,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_GROUP_RECALL = 49,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_INFRAVISION = 50,    // Reserved Skill[] DO NOT CHANGE //
	SPELL_WATERWALK = 51,    // Reserved Skill[] DO NOT CHANGE //

	// Insert new spells here, up to SPELLS_COUNT //
	SPELL_CURE_SERIOUS = 52,
	SPELL_GROUP_STRENGTH = 53,
	SPELL_HOLD = 54,
	SPELL_POWER_HOLD = 55,
	SPELL_MASS_HOLD = 56,
	SPELL_FLY = 57,
	SPELL_BROKEN_CHAINS = 58,
	SPELL_NOFLEE = 59,
	SPELL_CREATE_LIGHT = 60,
	SPELL_DARKNESS = 61,
	SPELL_STONESKIN = 62,
	SPELL_CLOUDLY = 63,
	SPELL_SILENCE = 64,
	SPELL_LIGHT = 65,
	SPELL_CHAIN_LIGHTNING = 66,
	SPELL_FIREBLAST = 67,
	SPELL_IMPLOSION = 68,
	SPELL_WEAKNESS = 69,
	SPELL_GROUP_INVISIBLE = 70,
	SPELL_SHADOW_CLOAK = 71,
	SPELL_ACID = 72,
	SPELL_REPAIR = 73,
	SPELL_ENLARGE = 74,
	SPELL_FEAR = 75,
	SPELL_SACRIFICE = 76,
	SPELL_WEB = 77,
	SPELL_BLINK = 78,
	SPELL_REMOVE_HOLD = 79,
	SPELL_CAMOUFLAGE = 80,
	SPELL_POWER_BLINDNESS = 81,
	SPELL_MASS_BLINDNESS = 82,
	SPELL_POWER_SILENCE = 83,
	SPELL_EXTRA_HITS = 84,
	SPELL_RESSURECTION = 85,
	SPELL_MAGICSHIELD = 86,
	SPELL_FORBIDDEN = 87,
	SPELL_MASS_SILENCE = 88,
	SPELL_REMOVE_SILENCE = 89,
	SPELL_DAMAGE_LIGHT = 90,
	SPELL_DAMAGE_SERIOUS = 91,
	SPELL_DAMAGE_CRITIC = 92,
	SPELL_MASS_CURSE = 93,
	SPELL_ARMAGEDDON = 94,
	SPELL_GROUP_FLY = 95,
	SPELL_GROUP_BLESS = 96,
	SPELL_REFRESH = 97,
	SPELL_STUNNING = 98,
	SPELL_HIDE = 99,
	SPELL_SNEAK = 100,
	SPELL_DRUNKED = 101,
	SPELL_ABSTINENT = 102,
	SPELL_FULL = 103,
	SPELL_CONE_OF_COLD = 104,
	SPELL_BATTLE = 105,
	SPELL_HAEMORRAGIA = 106,
	SPELL_COURAGE = 107,
	SPELL_WATERBREATH = 108,
	SPELL_SLOW = 109,
	SPELL_HASTE = 110,
	SPELL_MASS_SLOW = 111,
	SPELL_GROUP_HASTE = 112,
	SPELL_SHIELD = 113,
	SPELL_PLAQUE = 114,
	SPELL_CURE_PLAQUE = 115,
	SPELL_AWARNESS = 116,
	SPELL_RELIGION = 117,
	SPELL_AIR_SHIELD = 118,
	SPELL_PORTAL = 119,
	SPELL_DISPELL_MAGIC = 120,
	SPELL_SUMMON_KEEPER = 121,
	SPELL_FAST_REGENERATION = 122,
	SPELL_CREATE_WEAPON = 123,
	SPELL_FIRE_SHIELD = 124,
	SPELL_RELOCATE = 125,
	SPELL_SUMMON_FIREKEEPER = 126,
	SPELL_ICE_SHIELD = 127,
	SPELL_ICESTORM = 128,
	SPELL_ENLESS = 129,
	SPELL_SHINEFLASH = 130,
	SPELL_MADNESS = 131,
	SPELL_GROUP_MAGICGLASS = 132,
	SPELL_CLOUD_OF_ARROWS = 133,
	SPELL_VACUUM = 134,
	SPELL_METEORSTORM = 135,
	SPELL_STONEHAND = 136,
	SPELL_MINDLESS = 137,
	SPELL_PRISMATICAURA = 138,
	SPELL_EVILESS = 139,
	SPELL_AIR_AURA = 140,
	SPELL_FIRE_AURA = 141,
	SPELL_ICE_AURA = 142,
	SPELL_SHOCK = 143,
	SPELL_MAGICGLASS = 144,
	SPELL_GROUP_SANCTUARY = 145,
	SPELL_GROUP_PRISMATICAURA = 146,
	SPELL_DEAFNESS = 147,
	SPELL_POWER_DEAFNESS = 148,
	SPELL_REMOVE_DEAFNESS = 149,
	SPELL_MASS_DEAFNESS = 150,
	SPELL_DUSTSTORM = 151,
	SPELL_EARTHFALL = 152,
	SPELL_SONICWAVE = 153,
	SPELL_HOLYSTRIKE = 154,
	SPELL_ANGEL = 155,
	SPELL_MASS_FEAR = 156,    // Added by Niker //
	SPELL_FASCINATION = 157,
	SPELL_CRYING = 158,
	SPELL_OBLIVION = 159,    // Забвение. Dalim //
	SPELL_BURDEN_OF_TIME = 160,    // Бремя времени. Dalim //
	SPELL_GROUP_REFRESH = 161,
	SPELL_PEACEFUL = 162,    // Усмирение. dzMUDiST //
	SPELL_MAGICBATTLE = 163,
	SPELL_BERSERK = 164,
	SPELL_STONEBONES = 165,
	SPELL_ROOM_LIGHT = 166,    // Закл освящения комнаты //
	SPELL_POISONED_FOG = 167,    // Закл отравленного тумана //
	SPELL_THUNDERSTORM = 168,    // Закл отравленного тумана //
	SPELL_LIGHT_WALK = 169,
	SPELL_FAILURE = 170,
	SPELL_CLANPRAY = 171,
	SPELL_GLITTERDUST = 172,
	SPELL_SCREAM = 173,
	SPELL_CATS_GRACE = 174,
	SPELL_BULL_BODY = 175,
	SPELL_SNAKE_WISDOM = 176,
	SPELL_GIMMICKRY = 177,
	SPELL_WC_OF_CHALLENGE = 178,
	SPELL_WC_OF_MENACE = 179,
	SPELL_WC_OF_RAGE = 180,
	SPELL_WC_OF_MADNESS = 181,
	SPELL_WC_OF_THUNDER = 182,
	SPELL_WC_OF_DEFENSE = 183,
	SPELL_WC_OF_BATTLE = 184,
	SPELL_WC_OF_POWER = 185,
	SPELL_WC_OF_BLESS = 186,
	SPELL_WC_OF_COURAGE = 187,
	SPELL_RUNE_LABEL = 188,
	SPELL_ACONITUM_POISON = 189,
	SPELL_SCOPOLIA_POISON = 190,
	SPELL_BELENA_POISON = 191,
	SPELL_DATURA_POISON = 192,
	SPELL_TIMER_REPAIR = 193,
	SPELL_LACKY = 194,
	SPELL_BANDAGE = 195,
	SPELL_NO_BANDAGE = 196,
	SPELL_CAPABLE = 197,
	SPELL_STRANGLE = 198,
	SPELL_RECALL_SPELLS = 199,
	SPELL_HYPNOTIC_PATTERN = 200,
	SPELL_SOLOBONUS = 201,
	SPELL_VAMPIRE = 202,
	SPELLS_RESTORATION = 203,    //очистка магических афектов
	SPELL_AURA_DEATH = 204,    // аура смерти верховного лича
	SPELL_RECOVERY = 205,    // исцеление нежити
	SPELL_MASS_RECOVERY = 206,    // масовое исцеление нежити
	SPELL_AURA_EVIL = 207,    // аура зла для всей нежити в группе
	SPELL_MENTAL_SHADOW = 208,    // ментальная тень
	SPELL_EVARDS_BLACK_TENTACLES = 209, //навьи руки
	SPELL_WHIRLWIND = 210, //вихрь
	SPELL_INDRIKS_TEETH = 211, //зубы индрика
	SPELL_MELFS_ACID_ARROW = 212, //кислотная стрела
	SPELL_THUNDERSTONE = 213, //громовой камень
	SPELL_CLOD = 214,  // глыба
	SPELL_EXPEDIENT = 215, //боевой приъем
	SPELL_SIGHT_OF_DARKNESS = 216, // зрение тьмы
	SPELL_GENERAL_SINCERITY = 217, // общая искреность
	SPELL_MAGICAL_GAZE = 218, // магический взор
	SPELL_ALL_SEEING_EYE = 219, // всевидящее око
	SPELL_EYE_OF_GODS = 220, // око богов
	SPELL_BREATHING_AT_DEPTH = 221, // дыхание на глыбине
	SPELL_GENERAL_RECOVERY = 222, // общее востановление
	SPELL_COMMON_MEAL = 223, // общая трапеза
	SPELL_STONE_WALL = 224, // каменная стена
	SPELL_SNAKE_EYES = 225, // глаза змея
	SPELL_EARTH_AURA = 226, // земной поклон
	SPELL_GROUP_PROT_FROM_EVIL = 227, // групповая защита от тьмы
	SPELL_ARROWS_FIRE = 228,  // стрелы охотника
	SPELL_ARROWS_WATER = 229, // стрелы охотника
	SPELL_ARROWS_EARTH = 230, // стрелы охотника
	SPELL_ARROWS_AIR = 231,   // стрелы охотника
	SPELL_ARROWS_DEATH = 232, // стрелы охотника
	SPELL_PALADINE_INSPIRATION = 233, //воодушевить при крите
	SPELL_DEXTERITY = 234, //ловкость
	SPELL_GROUP_BLINK = 235, // групповая мигалка
	SPELL_GROUP_CLOUDLY = 236, // группповое затуманивание
	SPELL_GROUP_AWARNESS = 237, // групповая внимательность
	SPELL_WC_EXPERIENSE = 238, // опыт группы
	SPELL_WC_LUCK = 239, // удача группы
	SPELL_WC_PHYSDAMAGE = 240, // + дам
	SPELL_MASS_FAILURE = 241, // взор Велеса (массовая недоля)
	SPELL_MASS_NOFLEE = 242, // западня (массовое сковывание)
	SPELL_FIRE_BREATH = 243,       // магическое дыхание
	SPELL_GAS_BREATH = 244,        // магическое дыхание
	SPELL_FROST_BREATH = 245,      // магическое дыхание
	SPELL_ACID_BREATH = 246,       // магическое дыхание
	SPELL_LIGHTNING_BREATH = 247,  // магическое дыхание
	SPELL_IDENTIFY = 351,
	SPELL_FULL_IDENTIFY =352,
	SPELL_QUEST = 353,
	SPELLS_COUNT = SPELL_QUEST //last
};

template<>
ESpell ITEM_BY_NAME<ESpell>(const std::string &name);
template<>
const std::string &NAME_BY_ITEM<ESpell>(const ESpell spell);

#define MAX_SLOT 13

/*
 *  NON-PLAYER AND OBJECT SPELLS AND SKILLS
 *  The practice levels for the spells and skills below are _not_ recorded
 *  in the playerfile; therefore, the intended use is for spells and skills
 *  associated with objects (such as SPELL_IDENTIFY used with scrolls of
 *  identify) or non-players (such as NPC-only spells).
 */

// NEW NPC/OBJECT SPELLS can be inserted here up to 299


// WEAPON ATTACK TYPES

#define TYPE_HIT                     400
/*
#define TYPE_STING                   401
#define TYPE_WHIP                    402
#define TYPE_SLASH                   403
#define TYPE_BITE                    404
#define TYPE_BLUDGEON                405
#define TYPE_CRUSH                   406
#define TYPE_POUND                   407
#define TYPE_CLAW                    408
#define TYPE_MAUL                    409
#define TYPE_THRASH                  410
#define TYPE_PIERCE                  411
#define TYPE_BLAST		             412
#define TYPE_PUNCH		             413
#define TYPE_STAB		             414
#define TYPE_PICK                    415
*/
#define TYPE_MAGIC                   420
// new attack types can be added here - up to TYPE_SUFFERING
#define TYPE_TUNNERLDEATH            496
#define TYPE_WATERDEATH              497
#define TYPE_ROOMDEATH               498
#define TYPE_SUFFERING               499

#define TAR_IGNORE      (1 << 0)
#define TAR_CHAR_ROOM   (1 << 1)
#define TAR_CHAR_WORLD  (1 << 2) // не ищет мобов при касте чарами (призвать/переместиться/переход)
#define TAR_FIGHT_SELF  (1 << 3)
#define TAR_FIGHT_VICT  (1 << 4)
#define TAR_SELF_ONLY   (1 << 5)    // Only a check, use with i.e. TAR_CHAR_ROOM //
#define TAR_NOT_SELF    (1 << 6)    // Only a check, use with i.e. TAR_CHAR_ROOM //
#define TAR_OBJ_INV     (1 << 7)
#define TAR_OBJ_ROOM    (1 << 8)
#define TAR_OBJ_WORLD   (1 << 9)
#define TAR_OBJ_EQUIP    (1 << 10)
#define TAR_ROOM_THIS    (1 << 11) // Цель комната в которой сидит чар//
#define TAR_ROOM_DIR    (1 << 12) // Цель комната в каком-то направлении от чара//
#define TAR_ROOM_WORLD    (1 << 13) // Цель какая-то комната в мире//

struct attack_hit_type {
	const char *singular;
	const char *plural;
};

#define MANUAL_SPELL(spellname)    spellname(level, caster, cvict, ovict);

void spell_create_water(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_recall(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_teleport(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_summon(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_relocate(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_portal(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_locate_object(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_charm(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_information(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_identify(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_full_identify(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_enchant_weapon(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_control_weather(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_create_weapon(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_energydrain(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_fear(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_sacrifice(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_forbidden(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_holystrike(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void skill_identify(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_angel(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_vampire(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);
void spell_mental_shadow(int level, CHAR_DATA *ch, CHAR_DATA *victim, OBJ_DATA *obj);

// возращает текст выводимый при спадении скила
std::string get_wear_off_text(ESpell spell);

// текст заклинания для игроков, произнесенные заклинателями разных религий
struct CastPhraseList {
	// заклинание произнесенное язычником
	std::string text_for_heathen;

	// заклинание произнесенное христианиным
	std::string text_for_christian;
};

// возвращает фразу для заклинания согласно религии
// std::nullopt если соответсвующего заклинания в таблице нет
std::optional<CastPhraseList> get_cast_phrase(int spell);

// basic magic calling functions

int fix_name_and_find_spell_num(char *name);

bool catch_bloody_corpse(OBJ_DATA *l);

// other prototypes //
void init_spell_levels(void);
const char *skill_name(int num);
const char *spell_name(int num);
int calculateSaving(CHAR_DATA *killer, CHAR_DATA *victim, int type, int ext_apply);
int general_savingthrow(CHAR_DATA *killer, CHAR_DATA *victim, int type, int ext_apply);
bool can_get_spell(CHAR_DATA *ch, int spellnum);
int min_spell_lvl_with_req(CHAR_DATA *ch, int spellnum, int req_lvl);
bool can_get_spell_with_req(CHAR_DATA *ch, int spellnum, int req_lvl);
ESkill get_magic_skill_number_by_spell(int spellnum);
int check_recipe_values(CHAR_DATA *ch, int spellnum, int spelltype, int showrecipe);
int check_recipe_items(CHAR_DATA *ch, int spellnum, int spelltype, int extract, const CHAR_DATA *targ = NULL);

//Polud статистика использования заклинаний
typedef std::map<int, int> SpellCountType;

namespace SpellUsage {
extern bool isActive;
extern time_t start;
void AddSpellStat(int charClass, int spellNum);
void save();
void clear();
};
//-Polud

#define CALC_SUCCESS(modi, perc)         ((modi)-100+(perc))

const int HOURS_PER_WARCRY = 4;
const int HOURS_PER_TURN_UNDEAD = 8;

#endif

// vim: ts=4 sw=4 tw=0 noet syntax=cpp :
