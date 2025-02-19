/* ************************************************************************
*   File: magic.cpp                                     Part of Bylins    *
*  Usage: low-level functions for magic; spell template code              *
*                                                                         *
*  All rights reserved.  See license.doc for complete information.        *
*                                                                         *
*  Copyright (C) 1993, 94 by the Trustees of the Johns Hopkins University *
*  CircleMUD is based on DikuMUD, Copyright (C) 1990, 1991.               *
* 									  *
*  $Author$                                                        *
*  $Date$                                           *
*  $Revision$                                                      *
************************************************************************ */

#include "magic.h"

#include "action_targeting.h"
#include "affects/affect_handler.h"
#include "chars/world.characters.h"
#include "cmd/hire.h"
#include "corpse.h"
#include "fightsystem/fight.h"
#include "fightsystem/fight_hit.h"
#include "fightsystem/mobact.h"
#include "fightsystem/pk.h"
#include "handler.h"
#include "obj_prototypes.h"
#include "random.h"
#include "world_objects.h"

extern int what_sky;
extern DESCRIPTOR_DATA *descriptor_list;
extern struct spell_create_type spell_create[];
extern int interpolate(int min_value, int pulse);
extern int attack_best(CHAR_DATA *ch, CHAR_DATA *victim);

byte saving_throws(int class_num, int type, int level);    // class.cpp
byte extend_saving_throws(int class_num, int type, int level);
int check_charmee(CHAR_DATA *ch, CHAR_DATA *victim, int spellnum);
void cast_reaction(CHAR_DATA *victim, CHAR_DATA *caster, int spellnum);

bool material_component_processing(CHAR_DATA *caster, CHAR_DATA *victim, int spellnum);
bool material_component_processing(CHAR_DATA *caster, int vnum, int spellnum);

bool is_room_forbidden(ROOM_DATA *room) {
	for (const auto &af : room->affected) {
		if (af->type == SPELL_FORBIDDEN && (number(1, 100) <= af->modifier)) {
			return true;
		}
	}
	return false;
}

// * Saving throws are now in class.cpp as of bpl13.

/*
 * Negative apply_saving_throw[] values make saving throws better!
 * Then, so do negative modifiers.  Though people may be used to
 * the reverse of that. It's due to the code modifying the target
 * saving throw instead of the random number of the character as
 * in some other systems.
 */

int calc_anti_savings(CHAR_DATA *ch) {
	int modi = 0;

	if (WAITLESS(ch))
		modi = 350;
	else if (GET_GOD_FLAG(ch, GF_GODSLIKE))
		modi = 250;
	else if (GET_GOD_FLAG(ch, GF_GODSCURSE))
		modi = -250;
	else
		modi = GET_CAST_SUCCESS(ch);
	modi += MAX(0, MIN(20, (int) ((GET_REAL_WIS(ch) - 23) * 3 / 2)));
	if (!IS_NPC(ch)) {
		modi *= ch->get_cond_penalty(P_CAST);
	}
//  log("[EXT_APPLY] Name==%s modi==%d",GET_NAME(ch), modi);
	return modi;
}

int calculateSaving(CHAR_DATA *killer, CHAR_DATA *victim, int type, int ext_apply) {
	int temp_save_stat = 0, temp_awake_mod = 0;

	if (-GET_SAVE(victim, type) / 10 > number(1, 100)) {
		return 1;
	}

	// NPCs use warrior tables according to some book
	int save;
	int class_sav = GET_CLASS(victim);

	if (IS_NPC(victim)) {
		class_sav = CLASS_MOB;    // неизвестный класс моба
	} else {
		if (class_sav < 0 || class_sav >= NUM_PLAYER_CLASSES)
			class_sav = CLASS_WARRIOR;    // неизвестный класс игрока
	}

	// Базовые спасброски профессии/уровня
	save = extend_saving_throws(class_sav, type, GET_LEVEL(victim));

	switch (type) {
		case SAVING_REFLEX:      //3 реакция
			if ((save > 0) && can_use_feat(victim, DODGER_FEAT))
				save >>= 1;
			save -= dex_bonus(GET_REAL_DEX(victim));
			temp_save_stat = dex_bonus(GET_REAL_DEX(victim));
			if (victim->ahorse())
				save += 20;
			break;
		case SAVING_STABILITY:   //2  стойкость
			save += -GET_REAL_CON(victim);
			if (victim->ahorse())
				save -= 20;
			temp_save_stat = GET_REAL_CON(victim);
			break;
		case SAVING_WILL:        //1  воля
			save += -GET_REAL_WIS(victim);
			temp_save_stat = GET_REAL_WIS(victim);
			break;
		case SAVING_CRITICAL:   //0   здоровье
			save += -GET_REAL_CON(victim);
			temp_save_stat = GET_REAL_CON(victim);
			break;
	}

	// Ослабление магических атак
	if (type != SAVING_REFLEX) {
		if ((save > 0) &&
			(AFF_FLAGGED(victim, EAffectFlag::AFF_AIRAURA)
				|| AFF_FLAGGED(victim, EAffectFlag::AFF_FIREAURA)
				|| AFF_FLAGGED(victim, EAffectFlag::AFF_EARTHAURA)
				|| AFF_FLAGGED(victim, EAffectFlag::AFF_ICEAURA))) {
			save >>= 1;
		}
	}
	// Учет осторожного стиля
	if (PRF_FLAGGED(victim, PRF_AWAKE)) {
		if (can_use_feat(victim, IMPREGNABLE_FEAT)) {
			save -= MAX(0, victim->get_skill(SKILL_AWAKE) - 80) / 2;
			temp_awake_mod = MAX(0, victim->get_skill(SKILL_AWAKE) - 80) / 2;
		}
		temp_awake_mod += CalculateSkillAwakeModifier(killer, victim);
		save -= CalculateSkillAwakeModifier(killer, victim);
	}

	save += GET_SAVE(victim, type);    // одежда
	save += ext_apply;    // внешний модификатор

	if (IS_GOD(victim))
		save = -150;
	else if (GET_GOD_FLAG(victim, GF_GODSLIKE))
		save -= 50;
	else if (GET_GOD_FLAG(victim, GF_GODSCURSE))
		save += 50;
	if (IS_NPC(victim) && !IS_NPC(killer))
		log("SAVING: Caster==%s  Mob==%s vnum==%d Level==%d type==%d base_save==%d stat_bonus==%d awake_bonus==%d save_ext==%d cast_apply==%d result==%d new_random==%d",
			GET_NAME(killer),
			GET_NAME(victim),
			GET_MOB_VNUM(victim),
			GET_LEVEL(victim),
			type,
			extend_saving_throws(class_sav, type, GET_LEVEL(victim)),
			temp_save_stat,
			temp_awake_mod,
			GET_SAVE(victim, type),
			ext_apply,
			save,
			number(1, 200));
	// Throwing a 0 is always a failure.
	return save;
}

int general_savingthrow(CHAR_DATA *killer, CHAR_DATA *victim, int type, int ext_apply) {
	int save = calculateSaving(killer, victim, type, ext_apply);
	if (MAX(10, save) <= number(1, 200))
		return (true);

	// Oops, failed. Sorry.
	return (false);
}

int multi_cast_say(CHAR_DATA *ch) {
	if (!IS_NPC(ch))
		return 1;
	switch (GET_RACE(ch)) {
		case NPC_RACE_EVIL_SPIRIT:
		case NPC_RACE_GHOST:
		case NPC_RACE_HUMAN:
		case NPC_RACE_ZOMBIE:
		case NPC_RACE_SPIRIT: return 1;
	}
	return 0;
}

void show_spell_off(int aff, CHAR_DATA *ch) {
	if (!IS_NPC(ch) && PLR_FLAGGED(ch, PLR_WRITING))
		return;

	// TODO:" refactor and replace int aff by ESpell
	const std::string &msg = get_wear_off_text(static_cast<ESpell>(aff));
	if (!msg.empty()) {
		act(msg.c_str(), FALSE, ch, 0, 0, TO_CHAR | TO_SLEEP);
		send_to_char("\r\n", ch);
	}
}

// зависимость длительности закла от скила магии
float func_koef_duration(int spellnum, int percent) {
	switch (spellnum) {
		case SPELL_STRENGTH:
		case SPELL_DEXTERITY: return 1 + percent / 400.00;
			break;
		case SPELL_GROUP_BLINK:
		case SPELL_BLINK: return 1 + percent / 400.00;
			break;
		default: return 1;
			break;
	}
}

// зависимость модификации спелла от скила магии
float func_koef_modif(int spellnum, int percent) {
	switch (spellnum) {
		case SPELL_STRENGTH:
		case SPELL_DEXTERITY:
			if (percent > 100)
				return 1;
			return 0;
			break;
		case SPELL_MASS_SLOW:
		case SPELL_SLOW: {
			if (percent >= 80) {
				return (percent - 80) / 20.00 + 1.00;
			}
		}
			break;
		case SPELL_SONICWAVE:
			if (percent > 100) {
				return (percent - 80) / 20.00; // после 100% идет прибавка
			}
			return 1;
			break;
		case SPELL_FASCINATION:
		case SPELL_HYPNOTIC_PATTERN: 
			if (percent >= 80) {
				return (percent - 80) / 20.00 + 1.00;
			}
			return 1;
			break;
		default: return 1;
	}
	return 0;
}

int magic_skill_damage_calc(CHAR_DATA *ch, CHAR_DATA *victim, int spellnum, int dam) {
	if (IS_NPC(ch)) {
		dam += dam * ((GET_REAL_WIS(ch) - 22) * 5) / 100;
		return (dam);
	}

	const ESkill skill_number = get_magic_skill_number_by_spell(spellnum);

	if (skill_number > 0) {
		dam += dam
			* (1 + static_cast<double>(std::min(CalcSkillMinCap(ch, skill_number), ch->get_skill(skill_number))) / 500);
	}

	if (GET_REAL_WIS(ch) >= 23) {
		dam += dam * (1 + static_cast<double>((GET_REAL_WIS(ch) - 22)) / 200);
	}

	if (!IS_NPC(ch)) {
		dam = (IS_NPC(victim) ? MIN(dam, 6 * GET_MAX_HIT(ch)) : MIN(dam, 2 * GET_MAX_HIT(ch)));
	}

	return (dam);
}

int mag_damage(int level, CHAR_DATA *ch, CHAR_DATA *victim, int spellnum, int savetype) {
	int dam = 0, rand = 0, count = 1, modi = 0, ndice = 0, sdice = 0, adice = 0, no_savings = FALSE;
	OBJ_DATA *obj = nullptr;

	if (victim == nullptr || IN_ROOM(victim) == NOWHERE || ch == nullptr)
		return (0);

	if (!pk_agro_action(ch, victim))
		return (0);

	log("[MAG DAMAGE] %s damage %s (%d)", GET_NAME(ch), GET_NAME(victim), spellnum);
	// Magic glass
	if ((spellnum != SPELL_LIGHTNING_BREATH && spellnum != SPELL_FIRE_BREATH && spellnum != SPELL_GAS_BREATH
		&& spellnum != SPELL_ACID_BREATH)
		|| ch == victim) {
		if (!IS_SET(SpINFO.routines, MAG_WARCRY)) {
			if (ch != victim && spellnum <= SPELLS_COUNT &&
				((AFF_FLAGGED(victim, EAffectFlag::AFF_MAGICGLASS) && number(1, 100) < (GET_LEVEL(victim) / 3)))) {
				act("Магическое зеркало $N1 отразило вашу магию!", FALSE, ch, 0, victim, TO_CHAR);
				act("Магическое зеркало $N1 отразило магию $n1!", FALSE, ch, 0, victim, TO_NOTVICT);
				act("Ваше магическое зеркало отразило поражение $n1!", FALSE, ch, 0, victim, TO_VICT);
				log("[MAG DAMAGE] Зеркало - полное отражение: %s damage %s (%d)",
					GET_NAME(ch),
					GET_NAME(victim),
					spellnum);
				return (mag_damage(level, ch, ch, spellnum, savetype));
			}
		} else {
			if (ch != victim && spellnum <= SPELLS_COUNT && IS_GOD(victim)
				&& (IS_NPC(ch) || GET_LEVEL(victim) > GET_LEVEL(ch))) {
				act("Звуковой барьер $N1 отразил ваш крик!", FALSE, ch, 0, victim, TO_CHAR);
				act("Звуковой барьер $N1 отразил крик $n1!", FALSE, ch, 0, victim, TO_NOTVICT);
				act("Ваш звуковой барьер отразил крик $n1!", FALSE, ch, 0, victim, TO_VICT);
				return (mag_damage(level, ch, ch, spellnum, savetype));
			}
		}

		if (!IS_SET(SpINFO.routines, MAG_WARCRY) && AFF_FLAGGED(victim, EAffectFlag::AFF_SHADOW_CLOAK)
			&& spellnum <= SPELLS_COUNT && number(1, 100) < 21) {
			act("Густая тень вокруг $N1 жадно поглотила вашу магию.", FALSE, ch, 0, victim, TO_CHAR);
			act("Густая тень вокруг $N1 жадно поглотила магию $n1.", FALSE, ch, 0, victim, TO_NOTVICT);
			act("Густая тень вокруг вас поглотила магию $n1.", FALSE, ch, 0, victim, TO_VICT);
			log("[MAG DAMAGE] Мантия  - поглощение урона: %s damage %s (%d)", GET_NAME(ch), GET_NAME(victim), spellnum);
			return (0);
		} 
		// Блочим маг дамагу от директ спелов для Витязей : шанс (скил/20 + вес.щита/2) ~ 30% при 200 скила и 40вес щита (Кудояр)
		if (!IS_SET(SpINFO.routines, MAG_WARCRY) && !IS_SET(SpINFO.routines, MAG_MASSES) && !IS_SET(SpINFO.routines, MAG_AREAS) 
			&& (victim->get_skill(SKILL_BLOCK) > 100) && GET_EQ(victim, WEAR_SHIELD) && can_use_feat(victim, MAGICAL_SHIELD_FEAT)
			&& ( number(1, 100) < ((victim->get_skill(SKILL_BLOCK))/20 + GET_OBJ_WEIGHT(GET_EQ(victim, WEAR_SHIELD))/2)))
			{
				act("Ловким движением $N0 отразил щитом вашу магию.", FALSE, ch, 0, victim, TO_CHAR);
				act("Ловким движением $N0 отразил щитом магию $n1.", FALSE, ch, 0, victim, TO_NOTVICT);
				act("Вы отразили своим щитом магию $n1.", FALSE, ch, 0, victim, TO_VICT);
				return (0);
			}
	}

	// позиции до начала атаки для расчета модификаторов в damage()
	// в принципе могут меняться какими-то заклами, но пока по дефолту нет
	int ch_start_pos = GET_POS(ch);
	int victim_start_pos = GET_POS(victim);

	if (ch != victim) {
		modi = calc_anti_savings(ch);
		if (can_use_feat(ch, RELATED_TO_MAGIC_FEAT) && !IS_NPC(victim)) {
			modi -= 80; //бонуса на непись нету
		}
		if (can_use_feat(ch, MAGICAL_INSTINCT_FEAT) && !IS_NPC(victim)) {
			modi -= 30; //бонуса на непись нету
		}
	}

	if (!IS_NPC(ch) && (GET_LEVEL(ch) > 10))
		modi += (GET_LEVEL(ch) - 10);
//  if (!IS_NPC(ch) && !IS_NPC(victim))
//     modi = 0;
	if (PRF_FLAGGED(ch, PRF_AWAKE) && !IS_NPC(victim))
		modi = modi - 50;
	// вводим переменную-модификатор владения школы магии	
	const int ms_mod = func_koef_modif(spellnum, ch->get_skill(get_magic_skill_number_by_spell(spellnum))); // к кубикам от % владения магии 
	switch (spellnum) {
		// ******** ДЛЯ ВСЕХ МАГОВ ********
		// магическая стрела - для всех с 1го левела 1го круга(8 слотов)
		// *** мин 15 макс 45 (360)
		// нейтрал
		case SPELL_MAGIC_MISSILE: modi += 300;//hotelos by postavit "no_saving = THRUE" no ono po idiotski propisano
			ndice = 2;
			sdice = 4;
			adice = 10;
			// если есть фит магическая стрела, то стрелок на 30 уровне будет 6
			if (can_use_feat(ch, MAGICARROWS_FEAT))
				count = (level + 9) / 5;
			else
				count = (level + 9) / 10;
			break;
			// ледяное прикосновение - для всех с 7го левела 3го круга(7 слотов)
			// *** мин 29.5 макс 55.5  (390)
			// нейтрал
		case SPELL_CHILL_TOUCH: savetype = SAVING_REFLEX;
			ndice = 15;
			sdice = 2;
			adice = level;
			break;
			// кислота - для всех с 18го левела 5го круга (6 слотов)
			// *** мин 48 макс 70 (420)
			// нейтрал
		case SPELL_ACID: savetype = SAVING_REFLEX;
			obj = nullptr;
			if (IS_NPC(victim)) {
				rand = number(1, 50);
				if (rand <= WEAR_BOTHS) {
					obj = GET_EQ(victim, rand);
				} else {
					for (rand -= WEAR_BOTHS, obj = victim->carrying; rand && obj;
						 rand--, obj = obj->get_next_content());
				}
			}
			if (obj) {
				ndice = 6;
				sdice = 10;
				adice = level;
				act("Кислота покрыла $o3.", FALSE, victim, obj, 0, TO_CHAR);
				alterate_object(obj, number(level * 2, level * 4), 100);
			} else {
				ndice = 6;
				sdice = 15;
				adice = (level - 18) * 2;
			}
			break;

			// землетрясение чернокнижники 22 уровень 7 круг (4)
			// *** мин 48 макс 60 (240)
			// нейтрал
		case SPELL_EARTHQUAKE: savetype = SAVING_REFLEX;
			ndice = 6;
			sdice = 15;
			adice = (level - 22) * 2;
			// если наездник, то считаем не сейвисы, а SKILL_HORSE
			if (ch->ahorse()) {
//		    5% шанс успеха,
				rand = number(1, 100);
				if (rand > 95)
					break;
				// провал - 5% шанс или скилл наездника vs скилл магии кастера на кубике d6
				if (rand < 5 || (CalcCurrentSkill(victim, SKILL_HORSE, nullptr) * number(1, 6))
					< GET_SKILL(ch, SKILL_EARTH_MAGIC) * number(1, 6)) {//фейл
					ch->drop_from_horse();
					break;
				}
			}
			if (GET_POS(victim) > POS_SITTING && !WAITLESS(victim) && (number(1, 999) > GET_AR(victim) * 10) &&
				(GET_MOB_HOLD(victim) || !general_savingthrow(ch, victim, SAVING_REFLEX, CALC_SUCCESS(modi, 30)))) {
				if (IS_HORSE(ch))
					ch->drop_from_horse();
				act("$n3 повалило на землю.", FALSE, victim, 0, 0, TO_ROOM | TO_ARENA_LISTEN);
				act("Вас повалило на землю.", FALSE, victim, 0, 0, TO_CHAR);
				GET_POS(victim) = POS_SITTING;
				update_pos(victim);
				WAIT_STATE(victim, 2 * PULSE_VIOLENCE);
			}
			break;

		case SPELL_SONICWAVE: savetype = SAVING_STABILITY;
			ndice = 5 + ms_mod;
			sdice = 8;
			adice = level/3 + 2*ms_mod;
			if (GET_POS(victim) > POS_SITTING &&
				!WAITLESS(victim) && (number(1, 999) > GET_AR(victim) * 10) &&
				(GET_MOB_HOLD(victim) || !general_savingthrow(ch, victim, SAVING_STABILITY, CALC_SUCCESS(modi, 60)))) {
				act("$n3 повалило на землю.", FALSE, victim, 0, 0, TO_ROOM | TO_ARENA_LISTEN);
				act("Вас повалило на землю.", FALSE, victim, 0, 0, TO_CHAR);
				GET_POS(victim) = POS_SITTING;
				update_pos(victim);
				WAIT_STATE(victim, 2 * PULSE_VIOLENCE);
			}
			break;

			// ********** ДЛЯ ФРАГЕРОВ **********
			// горящие руки - с 1го левела 1го круга (8 слотов)
			// *** мин 21 мах 30 (240)
			// ОГОНЬ
		case SPELL_BURNING_HANDS: savetype = SAVING_REFLEX;
			ndice = 8;
			sdice = 3;
			adice = (level + 2) / 3;
			break;

			// обжигающая хватка - с 4го левела 2го круга (8 слотов)
			// *** мин 36 макс 45 (360)
			// ОГОНЬ
		case SPELL_SHOCKING_GRASP: savetype = SAVING_REFLEX;
			ndice = 10;
			sdice = 6;
			adice = (level + 2) / 3;
			break;

			// молния - с 7го левела 3го круга (7 слотов)
			// *** мин 18 - макс 45 (315)
			// ВОЗДУХ
		case SPELL_LIGHTNING_BOLT: savetype = SAVING_REFLEX;
			ndice = 3;
			sdice = 5;
			count = (level + 5) / 6;
			break;

			// яркий блик - с 7го 3го круга (7 слотов)
			// *** мин 33 - макс 40 (280)
			// ОГОНЬ
		case SPELL_SHINEFLASH: ndice = 10;
			sdice = 5;
			adice = (level + 2) / 3;
			break;

			// шаровая молния - с 10го левела 4го круга (6 слотов)
			// *** мин 35 макс 55 (330)
			// ВОЗДУХ
		case SPELL_CALL_LIGHTNING: savetype = SAVING_REFLEX;
			ndice = 7 + ch->get_remort();
			sdice = 6;
			adice = level;
			break;

			// ледяные стрелы - уровень 14 круг 5 (6 слотов)
			// *** мин 44 макс 60 (360)
			// ОГОНЬ
		case SPELL_COLOR_SPRAY: savetype = SAVING_STABILITY;
			ndice = 6;
			sdice = 5;
			adice = level;
			break;

			// ледяной ветер - уровень 14 круг 5 (6 слотов)
			// *** мин 44 макс 60 (360)
			// ВОДА
		case SPELL_CONE_OF_COLD: savetype = SAVING_STABILITY;
			ndice = 10;
			sdice = 5;
			adice = level;
			break;

			// Огненный шар - уровень 25 круг 7 (4 слотов)
			// *** мин 66 макс 80 (400)
			// ОГОНЬ
		case SPELL_FIREBALL: savetype = SAVING_REFLEX;
			ndice = 10;
			sdice = 21;
			adice = (level - 25) * 5;
			break;

			// Огненный поток - уровень 18 круг 6 (5 слотов)
			// ***  мин 38 макс 50 (250)
			// ОГОНЬ, ареа
		case SPELL_FIREBLAST: savetype = SAVING_STABILITY;
			ndice = 10 + ch->get_remort();
			sdice = 3;
			adice = level;
			break;

			// метеоритный шторм - уровень 22 круг 7 (4 слота)
			// *** мин 66 макс 80  (240)
			// нейтрал, ареа
/*	case SPELL_METEORSTORM:
		savetype = SAVING_REFLEX;
		ndice = 11+ch->get_remort();
		sdice = 11;
		adice = (level - 22) * 3;
		break;*/

			// цепь молний - уровень 22 круг 7 (4 слота)
			// *** мин 76 макс 100 (400)
			// ВОЗДУХ, ареа
		case SPELL_CHAIN_LIGHTNING: savetype = SAVING_STABILITY;
			ndice = 2 + ch->get_remort();
			sdice = 4;
			adice = (level + ch->get_remort()) * 2;
			break;

			// гнев богов - уровень 26 круг 8 (2 слота)
			// *** мин 226 макс 250 (500)
			// ВОДА
		case SPELL_IMPLOSION: savetype = SAVING_WILL;
			ndice = 10;
			sdice = 13;
			adice = level * 6;
			break;

			// ледяной шторм - 26 левела 8й круг (2)
			// *** мин 55 макс 75 (150)
			// ВОДА, ареа
		case SPELL_ICESTORM: savetype = SAVING_STABILITY;
			ndice = 5;
			sdice = 10;
			adice = (level - 26) * 5;
			break;

			// суд богов - уровень 28 круг 9 (1 слот)
			// *** мин 188 макс 200 (200)
			// ВОЗДУХ, ареа
		case SPELL_ARMAGEDDON: savetype = SAVING_WILL;
			//в современных реалиях колдуны имеют 12+ мортов
			if (!(IS_NPC(ch))) {
				ndice = 10 + ((ch->get_remort() / 3) - 4);
				sdice = level / 9;
				adice = level * (number(4, 6));
			} else {
				ndice = 12;
				sdice = 3;
				adice = level * 6;
			}
			break;

			// ******* ХАЙЛЕВЕЛ СУПЕРДАМАДЖ МАГИЯ ******
			// каменное проклятие - круг 28 уровень 9 (1)
			// для всех
		case SPELL_STUNNING:
			if (ch == victim ||
				((number(1, 999) > GET_AR(victim) * 10) &&
					!general_savingthrow(ch, victim, SAVING_CRITICAL, CALC_SUCCESS(modi, GET_REAL_WIS(ch))))) {
				savetype = SAVING_STABILITY;
				ndice = GET_REAL_WIS(ch) / 5;
				sdice = GET_REAL_WIS(ch);
				adice = 5 + (GET_REAL_WIS(ch) - 20) / 6;
				int choice_stunning = 750;
				if (can_use_feat(ch, DARKDEAL_FEAT))
					choice_stunning -= GET_REMORT(ch) * 15;
				if (number(1, 999) > choice_stunning) {
					act("Ваше каменное проклятие отшибло сознание у $N1.", FALSE, ch, 0, victim, TO_CHAR);
					act("Каменное проклятие $n1 отшибло сознание у $N1.", FALSE, ch, 0, victim, TO_NOTVICT);
					act("У вас отшибло сознание, вам очень плохо...", FALSE, ch, 0, victim, TO_VICT);
					GET_POS(victim) = POS_STUNNED;
					WAIT_STATE(victim, adice * PULSE_VIOLENCE);
				}
			} else {
				ndice = GET_REAL_WIS(ch) / 7;
				sdice = GET_REAL_WIS(ch);
				adice = level;
			}
			break;

			// круг пустоты - круг 28 уровень 9 (1)
			// для всех
		case SPELL_VACUUM: savetype = SAVING_STABILITY;
			ndice = MAX(1, (GET_REAL_WIS(ch) - 10) / 2);
			sdice = MAX(1, GET_REAL_WIS(ch) - 10);
			//	    adice = MAX(1, 2 + 30 - GET_LEVEL(ch) + (GET_REAL_WIS(ch) - 29)) / 7;
			//	    Ну явно кривота была. Отбалансил на свой вкус. В 50 мудры на 25м леве лаг на 3 на 30 лаг на 4 а не наоборот
			//чтобы не обижать колдунов
			adice = 4 + MAX(1, GET_LEVEL(ch) + 1 + (GET_REAL_WIS(ch) - 29)) / 7;
			if (ch == victim ||
				(!general_savingthrow(ch, victim, SAVING_CRITICAL, CALC_SUCCESS(modi, GET_REAL_WIS(ch))) &&
					(number(1, 999) > GET_AR(victim) * 10) &&
					number(0, 1000) <= 500)) {
				GET_POS(victim) = POS_STUNNED;
				WAIT_STATE(victim, adice * PULSE_VIOLENCE);
			}
			break;

			// ********* СПЕЦИФИЧНАЯ ДЛЯ КЛЕРИКОВ МАГИЯ **********
		case SPELL_DAMAGE_LIGHT: savetype = SAVING_CRITICAL;
			ndice = 4;
			sdice = 3;
			adice = (level + 2) / 3;
			break;
		case SPELL_DAMAGE_SERIOUS: savetype = SAVING_CRITICAL;
			ndice = 10;
			sdice = 3;
			adice = (level + 1) / 2;
			break;
		case SPELL_DAMAGE_CRITIC: savetype = SAVING_CRITICAL;
			ndice = 15;
			sdice = 4;
			adice = (level + 1) / 2;
			break;
		case SPELL_DISPEL_EVIL: ndice = 4;
			sdice = 4;
			adice = level;
			if (ch != victim && IS_EVIL(ch) && !WAITLESS(ch) && GET_HIT(ch) > 1) {
				send_to_char("Ваша магия обратилась против вас.", ch);
				GET_HIT(ch) = 1;
			}
			if (!IS_EVIL(victim)) {
				if (victim != ch)
					act("Боги защитили $N3 от вашей магии.", FALSE, ch, 0, victim, TO_CHAR);
				return (0);
			};
			break;
		case SPELL_DISPEL_GOOD: ndice = 4;
			sdice = 4;
			adice = level;
			if (ch != victim && IS_GOOD(ch) && !WAITLESS(ch) && GET_HIT(ch) > 1) {
				send_to_char("Ваша магия обратилась против вас.", ch);
				GET_HIT(ch) = 1;
			}
			if (!IS_GOOD(victim)) {
				if (victim != ch)
					act("Боги защитили $N3 от вашей магии.", FALSE, ch, 0, victim, TO_CHAR);
				return (0);
			};
			break;
		case SPELL_HARM: savetype = SAVING_CRITICAL;
			ndice = 7;
			sdice = level;
			adice = level * GET_REMORT(ch) / 4;
			//adice = (level + 4) / 5;
			break;

		case SPELL_FIRE_BREATH:
		case SPELL_FROST_BREATH:
		case SPELL_ACID_BREATH:
		case SPELL_LIGHTNING_BREATH:
		case SPELL_GAS_BREATH: savetype = SAVING_STABILITY;
			if (!IS_NPC(ch))
				return (0);
			ndice = ch->mob_specials.damnodice;
			sdice = ch->mob_specials.damsizedice;
			adice = GetRealDamroll(ch) + str_bonus(GET_REAL_STR(ch), STR_TO_DAM);
			break;

		case SPELL_SACRIFICE:
			if (WAITLESS(victim))
				break;
			ndice = 8;
			sdice = 8;
			adice = level;
			break;

		case SPELL_DUSTSTORM: savetype = SAVING_STABILITY;
			ndice = 5;
			sdice = 6;
			adice = level + ch->get_remort() * 3;
			if (GET_POS(victim) > POS_SITTING &&
				!WAITLESS(victim) && (number(1, 999) > GET_AR(victim) * 10) &&
				(!general_savingthrow(ch, victim, SAVING_REFLEX, CALC_SUCCESS(modi, 30)))) {
				act("$n3 повалило на землю.", FALSE, victim, 0, 0, TO_ROOM | TO_ARENA_LISTEN);
				act("Вас повалило на землю.", FALSE, victim, 0, 0, TO_CHAR);
				GET_POS(victim) = POS_SITTING;
				update_pos(victim);
				WAIT_STATE(victim, 2 * PULSE_VIOLENCE);
			}
			break;

		case SPELL_EARTHFALL: savetype = SAVING_REFLEX;
			ndice = 8;
			sdice = 8;
			adice = level * 2;
			break;

		case SPELL_SHOCK: savetype = SAVING_REFLEX;
			ndice = 6;
			sdice = level / 2;
			adice = (level + GET_REMORT(ch)) * 2;
			break;

		case SPELL_SCREAM: savetype = SAVING_STABILITY;
			ndice = 10;
			sdice = (level + GET_REMORT(ch)) / 5;
			adice = level + GET_REMORT(ch) * 2;
			break;

		case SPELL_WHIRLWIND: savetype = SAVING_REFLEX;
			if (!(IS_NPC(ch))) {
				ndice = 10 + ((ch->get_remort() / 3) - 4);
				sdice = 18 + (3 - (30 - level) / 3);
				adice = (level + ch->get_remort() - 25) * (number(1, 4));
			} else {
				ndice = 10;
				sdice = 21;
				adice = (level - 5) * (number(2, 4));
			}
			break;

		case SPELL_INDRIKS_TEETH: ndice = 3 + ch->get_remort();
			sdice = 4;
			adice = level + ch->get_remort() + 1;
			break;

		case SPELL_MELFS_ACID_ARROW: savetype = SAVING_REFLEX;
			ndice = 10 + ch->get_remort() / 3;
			sdice = 20;
			adice = level + ch->get_remort() - 25;
			break;

		case SPELL_THUNDERSTONE: savetype = SAVING_REFLEX;
			ndice = 3 + ch->get_remort();
			sdice = 6;
			adice = 1 + level + ch->get_remort();
			break;

		case SPELL_CLOD: savetype = SAVING_REFLEX;
			ndice = 10 + ch->get_remort() / 3;
			sdice = 20;
			adice = (level + ch->get_remort() - 25) * (number(1, 4));
			break;

		case SPELL_HOLYSTRIKE:
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_EVILESS)) {
				dam = -1;
				no_savings = TRUE;
				// смерть или диспелл :)
				if (general_savingthrow(ch, victim, SAVING_WILL, modi)) {
					act("Черное облако вокруг вас нейтрализовало действие тумана, растворившись в нем.",
						FALSE, victim, 0, 0, TO_CHAR);
					act("Черное облако вокруг $n1 нейтрализовало действие тумана.",
						FALSE, victim, 0, 0, TO_ROOM | TO_ARENA_LISTEN);
					affect_from_char(victim, SPELL_EVILESS);
				} else {
					dam = MAX(1, GET_HIT(victim) + 1);
					if (IS_NPC(victim))
						dam += 99;    // чтобы насмерть
				}
			} else {
				ndice = 10;
				sdice = 8;
				adice = level * 5;
			}
			break;

		case SPELL_WC_OF_RAGE: ndice = (level + 3) / 4;
			sdice = 6;
			adice = dice(GET_REMORT(ch) / 2, 8);
			break;

		case SPELL_WC_OF_THUNDER: {
			ndice = GET_REMORT(ch) + (level + 2) / 3;
			sdice = 5;
			if (GET_POS(victim) > POS_SITTING &&
				!WAITLESS(victim) &&
				(GET_MOB_HOLD(victim) || !general_savingthrow(ch, victim, SAVING_STABILITY, GET_REAL_CON(ch)))) {
				act("$n3 повалило на землю.", FALSE, victim, 0, 0, TO_ROOM | TO_ARENA_LISTEN);
				act("Вас повалило на землю.", FALSE, victim, 0, 0, TO_CHAR);
				GET_POS(victim) = POS_SITTING;
				update_pos(victim);
				WAIT_STATE(victim, 2 * PULSE_VIOLENCE);
			}
			break;
		}

		case SPELL_ARROWS_FIRE:
		case SPELL_ARROWS_WATER:
		case SPELL_ARROWS_EARTH:
		case SPELL_ARROWS_AIR:
		case SPELL_ARROWS_DEATH:
			if (!(IS_NPC(ch))) {
				act("Ваша магическая стрела поразила $N1.", FALSE, ch, 0, victim, TO_CHAR);
				act("Магическая стрела $n1 поразила $N1.", FALSE, ch, 0, victim, TO_NOTVICT);
				act("Магическая стрела настигла вас.", FALSE, ch, 0, victim, TO_VICT);
				ndice = 3 + ch->get_remort();
				sdice = 4;
				adice = level + ch->get_remort() + 1;
			} else {
				ndice = 20;
				sdice = 4;
				adice = level * 3;
			}

			break;

	}            // switch(spellnum)

	if (!dam && !no_savings) {
		double koeff = 1;
		if (IS_NPC(victim)) {
			if (NPC_FLAGGED(victim, NPC_FIRECREATURE)) {
				if (IS_SET(SpINFO.spell_class, STYPE_FIRE))
					koeff /= 2;
				if (IS_SET(SpINFO.spell_class, STYPE_WATER))
					koeff *= 2;
			}
			if (NPC_FLAGGED(victim, NPC_AIRCREATURE)) {
				if (IS_SET(SpINFO.spell_class, STYPE_EARTH))
					koeff *= 2;
				if (IS_SET(SpINFO.spell_class, STYPE_AIR))
					koeff /= 2;
			}
			if (NPC_FLAGGED(victim, NPC_WATERCREATURE)) {
				if (IS_SET(SpINFO.spell_class, STYPE_FIRE))
					koeff *= 2;
				if (IS_SET(SpINFO.spell_class, STYPE_WATER))
					koeff /= 2;
			}
			if (NPC_FLAGGED(victim, NPC_EARTHCREATURE)) {
				if (IS_SET(SpINFO.spell_class, STYPE_EARTH))
					koeff /= 2;
				if (IS_SET(SpINFO.spell_class, STYPE_AIR))
					koeff *= 2;
			}
		}
		dam = dice(ndice, sdice) + adice;
		dam = complex_spell_modifier(ch, spellnum, GAPPLY_SPELL_EFFECT, dam);

		// колдуны в 2 раза сильнее фрагают по мобам
		if (can_use_feat(ch, POWER_MAGIC_FEAT) && IS_NPC(victim)) {
			dam += (int) dam * 0.5;
		}

		if (AFF_FLAGGED(ch, EAffectFlag::AFF_DATURA_POISON))
			dam -= dam * GET_POISON(ch) / 100;

		if (!IS_SET(SpINFO.routines, MAG_WARCRY)) {
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi))
				koeff /= 2;
		}

		if (dam > 0) {
			koeff *= 1000;
			dam = (int) MMAX(1.0, (dam * MMAX(300.0, MMIN(koeff, 2500.0)) / 1000.0));
		}
		//вместо старого учета мудры добавлена обработка с учетом скиллов
		//после коэффициента - так как в самой функции стоит планка по дамагу, пусть и относительная
		dam = magic_skill_damage_calc(ch, victim, spellnum, dam);
	}

	//Голодный кастер меньше дамажит!
	if (!IS_NPC(ch))
		dam *= ch->get_cond_penalty(P_DAMROLL);

	if (number(1, 100) <= GET_MR(victim))
		dam = 0;

	for (; count > 0 && rand >= 0; count--) {
		if (ch->in_room != NOWHERE
			&& IN_ROOM(victim) != NOWHERE
			&& GET_POS(ch) > POS_STUNNED
			&& GET_POS(victim) > POS_DEAD) {
			// инит полей для дамага
			Damage dmg(SpellDmg(spellnum), dam, FightSystem::MAGE_DMG);
			dmg.ch_start_pos = ch_start_pos;
			dmg.victim_start_pos = victim_start_pos;
			// колдуны игнорят поглощение у мобов
			if (can_use_feat(ch, POWER_MAGIC_FEAT) && IS_NPC(victim)) {
				dmg.flags.set(FightSystem::IGNORE_ABSORBE);
			}
			// отражение магии в кастующего
			if (ch == victim) {
				dmg.flags.set(FightSystem::MAGIC_REFLECT);
			}
			if (count <= 1) {
				dmg.flags.reset(FightSystem::NO_FLEE_DMG);
			} else {
				dmg.flags.set(FightSystem::NO_FLEE_DMG);
			}
			rand = dmg.process(ch, victim);
		}
	}
	return rand;
}

int pc_duration(CHAR_DATA *ch, int cnst, int level, int level_divisor, int min, int max) {
	int result = 0;
	if (IS_NPC(ch)) {
		result = cnst;
		if (level > 0 && level_divisor > 0)
			level = level / level_divisor;
		else
			level = 0;
		if (min > 0)
			level = MIN(level, min);
		if (max > 0)
			level = MAX(level, max);
		return (level + result);
	}
	result = cnst * SECS_PER_MUD_HOUR;
	if (level > 0 && level_divisor > 0)
		level = level * SECS_PER_MUD_HOUR / level_divisor;
	else
		level = 0;
	if (min > 0)
		level = MIN(level, min * SECS_PER_MUD_HOUR);
	if (max > 0)
		level = MAX(level, max * SECS_PER_MUD_HOUR);
	result = (level + result) / SECS_PER_PLAYER_AFFECT;
	return (result);
}

bool material_component_processing(CHAR_DATA *caster, CHAR_DATA *victim, int spellnum) {
	int vnum = 0;
	const char *missing = nullptr, *use = nullptr, *exhausted = nullptr;
	switch (spellnum) {
		case SPELL_FASCINATION: vnum = 3000;
			use = "Вы взяли череп летучей мыши в левую руку.\r\n";
			missing = "Батюшки светы! А помаду-то я дома забыл$g.\r\n";
			exhausted = "$o рассыпался в ваших руках от неловкого движения.\r\n";
			break;
		case SPELL_HYPNOTIC_PATTERN: vnum = 3006;
			use = "Вы разожгли палочку заморских благовоний.\r\n";
			missing = "Вы начали суматошно искать свои благовония, но тщетно.\r\n";
			exhausted = "$o дотлели и рассыпались пеплом.\r\n";
			break;
		case SPELL_ENCHANT_WEAPON: vnum = 1930;
			use = "Вы подготовили дополнительные компоненты для зачарования.\r\n";
			missing = "Вы были уверены что положили его в этот карман.\r\n";
			exhausted = "$o вспыхнул голубоватым светом, когда его вставили в предмет.\r\n";
			break;

		default: log("WARNING: wrong spellnum %d in %s:%d", spellnum, __FILE__, __LINE__);
			return false;
	}
	OBJ_DATA *tobj = get_obj_in_list_vnum(vnum, caster->carrying);
	if (!tobj) {
		act(missing, FALSE, victim, 0, caster, TO_CHAR);
		return (TRUE);
	}
	tobj->dec_val(2);
	act(use, FALSE, caster, tobj, 0, TO_CHAR);
	if (GET_OBJ_VAL(tobj, 2) < 1) {
		act(exhausted, FALSE, caster, tobj, 0, TO_CHAR);
		obj_from_char(tobj);
		extract_obj(tobj);
	}
	return (FALSE);
}

bool material_component_processing(CHAR_DATA *caster, int /*vnum*/, int spellnum) {
	const char *missing = nullptr, *use = nullptr, *exhausted = nullptr;
	switch (spellnum) {
		case SPELL_ENCHANT_WEAPON: use = "Вы подготовили дополнительные компоненты для зачарования.\r\n";
			missing = "Вы были уверены что положили его в этот карман.\r\n";
			exhausted = "$o вспыхнул голубоватым светом, когда его вставили в предмет.\r\n";
			break;

		default: log("WARNING: wrong spellnum %d in %s:%d", spellnum, __FILE__, __LINE__);
			return false;
	}
	OBJ_DATA *tobj = GET_EQ(caster, WEAR_HOLD);
	if (!tobj) {
		act(missing, FALSE, caster, 0, caster, TO_CHAR);
		return (TRUE);
	}
	tobj->dec_val(2);
	act(use, FALSE, caster, tobj, 0, TO_CHAR);
	if (GET_OBJ_VAL(tobj, 2) < 1) {
		act(exhausted, FALSE, caster, tobj, 0, TO_CHAR);
		obj_from_char(tobj);
		extract_obj(tobj);
	}
	return (FALSE);
}

int mag_affects(int level, CHAR_DATA *ch, CHAR_DATA *victim, int spellnum, int savetype) {
	bool accum_affect = FALSE, accum_duration = FALSE, success = TRUE;
	bool update_spell = FALSE;
	const char *to_vict = nullptr, *to_room = nullptr;
	int i, modi = 0;
	int rnd = 0;
	int decline_mod = 0;
	if (victim == nullptr
		|| IN_ROOM(victim) == NOWHERE
		|| ch == nullptr) {
		return 0;
	}

	// Calculate PKILL's affects
	//   1) "NPC affect PC spellflag"  for victim
	//   2) "NPC affect NPC spellflag" if victim cann't pkill FICHTING(victim)
	if (ch != victim)    //send_to_char("Start\r\n",ch);
	{
		//send_to_char("Start\r\n",victim);
		if (IS_SET(SpINFO.routines, NPC_AFFECT_PC))    //send_to_char("1\r\n",ch);
		{
			//send_to_char("1\r\n",victim);
			if (!pk_agro_action(ch, victim))
				return 0;
		} else if (IS_SET(SpINFO.routines, NPC_AFFECT_NPC) && victim->get_fighting())    //send_to_char("2\r\n",ch);
		{
			//send_to_char("2\r\n",victim);
			if (!pk_agro_action(ch, victim->get_fighting()))
				return 0;
		}
		//send_to_char("Stop\r\n",ch);
		//send_to_char("Stop\r\n",victim);
	}
	// Magic glass
	if (!IS_SET(SpINFO.routines, MAG_WARCRY)) {
		if (ch != victim
			&& SpINFO.violent
			&& ((!IS_GOD(ch)
				&& AFF_FLAGGED(victim, EAffectFlag::AFF_MAGICGLASS)
				&& (ch->in_room == IN_ROOM(victim)) //зеркало сработает только если оба в одной комнате
				&& number(1, 100) < (GET_LEVEL(victim) / 3))
				|| (IS_GOD(victim)
					&& (IS_NPC(ch)
						|| GET_LEVEL(victim) > (GET_LEVEL(ch)))))) {
			act("Магическое зеркало $N1 отразило вашу магию!", FALSE, ch, 0, victim, TO_CHAR);
			act("Магическое зеркало $N1 отразило магию $n1!", FALSE, ch, 0, victim, TO_NOTVICT);
			act("Ваше магическое зеркало отразило поражение $n1!", FALSE, ch, 0, victim, TO_VICT);
			mag_affects(level, ch, ch, spellnum, savetype);
			return 0;
		}
	} else {
		if (ch != victim && SpINFO.violent && IS_GOD(victim)
			&& (IS_NPC(ch) || GET_LEVEL(victim) > (GET_LEVEL(ch) + GET_REMORT(ch) / 2))) {
			act("Звуковой барьер $N1 отразил ваш крик!", FALSE, ch, 0, victim, TO_CHAR);
			act("Звуковой барьер $N1 отразил крик $n1!", FALSE, ch, 0, victim, TO_NOTVICT);
			act("Ваш звуковой барьер отразил крик $n1!", FALSE, ch, 0, victim, TO_VICT);
			mag_affects(level, ch, ch, spellnum, savetype);
			return 0;
		}
	}
	//  блочим директ аффекты вредных спелов для Витязей  шанс = (скил/20 + вес.щита/2)  (Кудояр)
	if (ch != victim && SpINFO.violent && !IS_SET(SpINFO.routines, MAG_WARCRY) && !IS_SET(SpINFO.routines, MAG_MASSES) && !IS_SET(SpINFO.routines, MAG_AREAS) 
	&& (victim->get_skill(SKILL_BLOCK) > 100) && GET_EQ(victim, WEAR_SHIELD) && can_use_feat(victim, MAGICAL_SHIELD_FEAT)
			&& ( number(1, 100) < ((victim->get_skill(SKILL_BLOCK))/20 + GET_OBJ_WEIGHT(GET_EQ(victim, WEAR_SHIELD))/2)))
	{
		act("Ваши чары повисли на щите $N1, и затем развеялись.", FALSE, ch, 0, victim, TO_CHAR);
		act("Щит $N1 поглотил злые чары $n1.", FALSE, ch, 0, victim, TO_NOTVICT);
		act("Ваш щит уберег вас от злых чар $n1.", FALSE, ch, 0, victim, TO_VICT);
		return (0);
	}
	
		
	if (!IS_SET(SpINFO.routines, MAG_WARCRY) && ch != victim && SpINFO.violent
		&& number(1, 999) <= GET_AR(victim) * 10) {
		send_to_char(NOEFFECT, ch);
		return 0;
	}

	AFFECT_DATA<EApplyLocation> af[MAX_SPELL_AFFECTS];
	for (i = 0; i < MAX_SPELL_AFFECTS; i++) {
		af[i].type = spellnum;
		af[i].bitvector = 0;
		af[i].modifier = 0;
		af[i].battleflag = 0;
		af[i].location = APPLY_NONE;
	}

	// decrease modi for failing, increese fo success
	if (ch != victim) {
		modi = calc_anti_savings(ch);
		if (can_use_feat(ch, RELATED_TO_MAGIC_FEAT) && !IS_NPC(victim)) {
			modi -= 80; //бонуса на непись нету
		}
		if (can_use_feat(ch, MAGICAL_INSTINCT_FEAT) && !IS_NPC(victim)) {
			modi -= 30; //бонуса на непись нету
		}

	}

	if (PRF_FLAGGED(ch, PRF_AWAKE) && !IS_NPC(victim)) {
		modi = modi - 50;
	}

//  log("[MAG Affect] Modifier value for %s (caster %s) = %d(spell %d)",
//      GET_NAME(victim), GET_NAME(ch), modi, spellnum);

	const int koef_duration = func_koef_duration(spellnum, ch->get_skill(get_magic_skill_number_by_spell(spellnum)));
	const int koef_modifier = func_koef_modif(spellnum, ch->get_skill(get_magic_skill_number_by_spell(spellnum)));

	switch (spellnum) {
		case SPELL_CHILL_TOUCH: savetype = SAVING_STABILITY;
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			af[0].location = APPLY_STR;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 2, level, 4, 6, 0)) * koef_duration;
			af[0].modifier = -1 - GET_REMORT(ch) / 2;
			af[0].battleflag = AF_BATTLEDEC;
			accum_duration = TRUE;
			to_room = "Боевой пыл $n1 несколько остыл.";
			to_vict = "Вы почувствовали себя слабее!";
			break;

		case SPELL_ENERGY_DRAIN:
		case SPELL_WEAKNESS: savetype = SAVING_WILL;
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			if (affected_by_spell(victim, SPELL_STRENGTH)) {
				affect_from_char(victim, SPELL_STRENGTH);
				success = FALSE;
				break;
			}
			if (affected_by_spell(victim, SPELL_DEXTERITY)) {
				affect_from_char(victim, SPELL_DEXTERITY);
				success = FALSE;
				break;
			}
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 4, level, 5, 4, 0)) * koef_duration;
			af[0].location = APPLY_STR;
			if (spellnum == SPELL_WEAKNESS)
				af[0].modifier = -1 * ((level / 6 + GET_REMORT(ch) / 2));
			else
				af[0].modifier = -2 * ((level / 6 + GET_REMORT(ch) / 2));
			if (IS_NPC(ch) && level >= (LVL_IMMORT))
				af[0].modifier += (LVL_IMMORT - level - 1);    //1 str per mob level above 30
			af[0].battleflag = AF_BATTLEDEC;
			accum_duration = TRUE;
			to_room = "$n стал$g немного слабее.";
			to_vict = "Вы почувствовали себя слабее!";
			spellnum = SPELL_WEAKNESS;
			break;
		case SPELL_STONE_WALL:
		case SPELL_STONESKIN: af[0].location = APPLY_ABSORBE;
			af[0].modifier = (level * 2 + 1) / 3;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "Кожа $n1 покрылась каменными пластинами.";
			to_vict = "Вы стали менее чувствительны к ударам.";
			spellnum = SPELL_STONESKIN;
			break;

		case SPELL_GENERAL_RECOVERY:
		case SPELL_FAST_REGENERATION: af[0].location = APPLY_HITREG;
			af[0].modifier = 50 + GET_REMORT(ch);
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[1].location = APPLY_MOVEREG;
			af[1].modifier = 50 + GET_REMORT(ch);
			af[1].duration = af[0].duration;
			accum_duration = TRUE;
			to_room = "$n расцвел$g на ваших глазах.";
			to_vict = "Вас наполнила живительная сила.";
			spellnum = SPELL_FAST_REGENERATION;
			break;

		case SPELL_AIR_SHIELD:
			if (affected_by_spell(victim, SPELL_ICE_SHIELD))
				affect_from_char(victim, SPELL_ICE_SHIELD);
			if (affected_by_spell(victim, SPELL_FIRE_SHIELD))
				affect_from_char(victim, SPELL_FIRE_SHIELD);
			af[0].bitvector = to_underlying(EAffectFlag::AFF_AIRSHIELD);
			af[0].battleflag = AF_BATTLEDEC;
			if (IS_NPC(victim) || victim == ch)
				af[0].duration = pc_duration(victim, 10 + GET_REMORT(ch), 0, 0, 0, 0) * koef_duration;
			else
				af[0].duration = pc_duration(victim, 4 + GET_REMORT(ch), 0, 0, 0, 0) * koef_duration;
			to_room = "$n3 окутал воздушный щит.";
			to_vict = "Вас окутал воздушный щит.";
			break;

		case SPELL_FIRE_SHIELD:
			if (affected_by_spell(victim, SPELL_ICE_SHIELD))
				affect_from_char(victim, SPELL_ICE_SHIELD);
			if (affected_by_spell(victim, SPELL_AIR_SHIELD))
				affect_from_char(victim, SPELL_AIR_SHIELD);
			af[0].bitvector = to_underlying(EAffectFlag::AFF_FIRESHIELD);
			af[0].battleflag = AF_BATTLEDEC;
			if (IS_NPC(victim) || victim == ch)
				af[0].duration = pc_duration(victim, 10 + GET_REMORT(ch), 0, 0, 0, 0) * koef_duration;
			else
				af[0].duration = pc_duration(victim, 4 + GET_REMORT(ch), 0, 0, 0, 0) * koef_duration;
			to_room = "$n3 окутал огненный щит.";
			to_vict = "Вас окутал огненный щит.";
			break;

		case SPELL_ICE_SHIELD:
			if (affected_by_spell(victim, SPELL_FIRE_SHIELD))
				affect_from_char(victim, SPELL_FIRE_SHIELD);
			if (affected_by_spell(victim, SPELL_AIR_SHIELD))
				affect_from_char(victim, SPELL_AIR_SHIELD);
			af[0].bitvector = to_underlying(EAffectFlag::AFF_ICESHIELD);
			af[0].battleflag = AF_BATTLEDEC;
			if (IS_NPC(victim) || victim == ch)
				af[0].duration = pc_duration(victim, 10 + GET_REMORT(ch), 0, 0, 0, 0) * koef_duration;
			else
				af[0].duration = pc_duration(victim, 4 + GET_REMORT(ch), 0, 0, 0, 0) * koef_duration;
			to_room = "$n3 окутал ледяной щит.";
			to_vict = "Вас окутал ледяной щит.";
			break;

		case SPELL_AIR_AURA: af[0].location = APPLY_RESIST_AIR;
			af[0].modifier = level;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_AIRAURA);
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n3 окружила воздушная аура.";
			to_vict = "Вас окружила воздушная аура.";
			break;

		case SPELL_EARTH_AURA: af[0].location = APPLY_RESIST_EARTH;
			af[0].modifier = level;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_EARTHAURA);
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n глубоко поклонил$u земле.";
			to_vict = "Глубокий поклон тебе, матушка земля.";
			break;

		case SPELL_FIRE_AURA: af[0].location = APPLY_RESIST_WATER;
			af[0].modifier = level;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_FIREAURA);
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n3 окружила огненная аура.";
			to_vict = "Вас окружила огненная аура.";
			break;

		case SPELL_ICE_AURA: af[0].location = APPLY_RESIST_FIRE;
			af[0].modifier = level;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_ICEAURA);
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n3 окружила ледяная аура.";
			to_vict = "Вас окружила ледяная аура.";
			break;

		case SPELL_GROUP_CLOUDLY:
		case SPELL_CLOUDLY: af[0].location = APPLY_AC;
			af[0].modifier = -20;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "Очертания $n1 расплылись и стали менее отчетливыми.";
			to_vict = "Ваше тело стало прозрачным, как туман.";
			spellnum = SPELL_CLOUDLY;
			break;

		case SPELL_GROUP_ARMOR:
		case SPELL_ARMOR: af[0].location = APPLY_AC;
			af[0].modifier = -20;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[1].location = APPLY_SAVING_REFLEX;
			af[1].modifier = -5;
			af[1].duration = af[0].duration;
			af[2].location = APPLY_SAVING_STABILITY;
			af[2].modifier = -5;
			af[2].duration = af[0].duration;
			accum_duration = TRUE;
			to_room = "Вокруг $n1 вспыхнул белый щит и тут же погас.";
			to_vict = "Вы почувствовали вокруг себя невидимую защиту.";
			spellnum = SPELL_ARMOR;
			break;

		case SPELL_FASCINATION:
			if (material_component_processing(ch, victim, spellnum)) {
				success = FALSE;
				break;
			}
			af[0].location = APPLY_CHA;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT*GET_REMORT(ch), 1, 0, 0) * koef_duration;
			if (ch == victim)
				af[0].modifier = (level + 9) / 10 + 0.7*koef_modifier;
			else
				af[0].modifier = (level + 14) / 15 + 0.7*koef_modifier;
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_room = "$n0 достал$g из маленькой сумочки какие-то вонючие порошки и отвернул$u, бормоча под нос \r\n\"..так это на ресницы надо, кажется... Эх, только бы не перепутать...\" \r\n";
			to_vict = "Вы попытались вспомнить уроки старой цыганки, что учила вас людям головы морочить.\r\nХотя вы ее не очень то слушали.\r\n";
			spellnum = SPELL_FASCINATION;
			break;


		case SPELL_GROUP_BLESS:
		case SPELL_BLESS: af[0].location = APPLY_SAVING_STABILITY;
			af[0].modifier = -5 - GET_REMORT(ch) / 3;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_BLESS);
			af[1].location = APPLY_SAVING_WILL;
			af[1].modifier = -5 - GET_REMORT(ch) / 4;
			af[1].duration = af[0].duration;
			af[1].bitvector = to_underlying(EAffectFlag::AFF_BLESS);
			to_room = "$n осветил$u на миг неземным светом.";
			to_vict = "Боги одарили вас своей улыбкой.";
			spellnum = SPELL_BLESS;
			break;

		case SPELL_CALL_LIGHTNING:
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
			}
			af[0].location = APPLY_HITROLL;
			af[0].modifier = -dice(1 + level / 8 + ch->get_remort() / 4, 4);
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 2, level + 7, 8, 0, 0)) * koef_duration;
			af[1].location = APPLY_CAST_SUCCESS;
			af[1].modifier = -dice(1 + level / 4 + ch->get_remort() / 2, 4);
			af[1].duration = af[0].duration;
			spellnum = SPELL_MAGICBATTLE;
			to_room = "$n зашатал$u, пытаясь прийти в себя от взрыва шаровой молнии.";
			to_vict = "Взрыв шаровой молнии $N1 отдался в вашей голове громким звоном.";
			break;

		case SPELL_CONE_OF_COLD:
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
			}
			af[0].location = APPLY_DEX;
			af[0].modifier = -dice(int(MAX(1, ((level - 14) / 7))), 3);
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 9, 0, 0, 0, 0)) * koef_duration;
			to_vict = "Вы покрылись серебристым инеем.";
			to_room = "$n покрыл$u красивым серебристым инеем.";
			break;
		case SPELL_GROUP_AWARNESS:
		case SPELL_AWARNESS:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_AWARNESS);
			af[1].location = APPLY_SAVING_REFLEX;
			af[1].modifier = -1 - GET_REMORT(ch) / 4;
			af[1].duration = af[0].duration;
			af[1].bitvector = to_underlying(EAffectFlag::AFF_AWARNESS);
			to_room = "$n начал$g внимательно осматриваться по сторонам.";
			to_vict = "Вы стали более внимательны к окружающему.";
			spellnum = SPELL_AWARNESS;
			break;

		case SPELL_SHIELD: af[0].duration = pc_duration(victim, 4, 0, 0, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_SHIELD);
			af[0].location = APPLY_SAVING_STABILITY;
			af[0].modifier = -10;
			af[0].battleflag = AF_BATTLEDEC;
			af[1].duration = af[0].duration;
			af[1].bitvector = to_underlying(EAffectFlag::AFF_SHIELD);
			af[1].location = APPLY_SAVING_WILL;
			af[1].modifier = -10;
			af[1].battleflag = AF_BATTLEDEC;
			af[2].duration = af[0].duration;
			af[2].bitvector = to_underlying(EAffectFlag::AFF_SHIELD);
			af[2].location = APPLY_SAVING_REFLEX;
			af[2].modifier = -10;
			af[2].battleflag = AF_BATTLEDEC;

			to_room = "$n покрыл$u сверкающим коконом.";
			to_vict = "Вас покрыл голубой кокон.";
			break;

		case SPELL_GROUP_HASTE:
		case SPELL_HASTE:
			if (affected_by_spell(victim, SPELL_SLOW)) {
				affect_from_char(victim, SPELL_SLOW);
				success = FALSE;
				break;
			}
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_HASTE);
			af[0].location = APPLY_SAVING_REFLEX;
			af[0].modifier = -1 - GET_REMORT(ch) / 5;
			to_vict = "Вы начали двигаться быстрее.";
			to_room = "$n начал$g двигаться заметно быстрее.";
			spellnum = SPELL_HASTE;
			break;

		case SPELL_SHADOW_CLOAK: af[0].bitvector = to_underlying(EAffectFlag::AFF_SHADOW_CLOAK);
			af[0].location = APPLY_SAVING_STABILITY;
			af[0].modifier = -(GET_LEVEL(ch) / 3 + GET_REMORT(ch)) / 4;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n скрыл$u в густой тени.";
			to_vict = "Густые тени окутали вас.";
			break;

		case SPELL_ENLARGE:
			if (affected_by_spell(victim, SPELL_ENLESS)) {
				affect_from_char(victim, SPELL_ENLESS);
				success = FALSE;
				break;
			}
			af[0].location = APPLY_SIZE;
			af[0].modifier = 5 + level / 2 + GET_REMORT(ch) / 3;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n начал$g расти, как на дрожжах.";
			to_vict = "Вы стали крупнее.";
			break;

		case SPELL_ENLESS:
			if (affected_by_spell(victim, SPELL_ENLARGE)) {
				affect_from_char(victim, SPELL_ENLARGE);
				success = FALSE;
				break;
			}
			af[0].location = APPLY_SIZE;
			af[0].modifier = -(5 + level / 3);
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n скукожил$u.";
			to_vict = "Вы стали мельче.";
			break;

		case SPELL_MAGICGLASS:
		case SPELL_GROUP_MAGICGLASS: af[0].bitvector = to_underlying(EAffectFlag::AFF_MAGICGLASS);
			af[0].duration = pc_duration(victim, 10, GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n3 покрыла зеркальная пелена.";
			to_vict = "Вас покрыло зеркало магии.";
			spellnum = SPELL_MAGICGLASS;
			break;

		case SPELL_CLOUD_OF_ARROWS: af[0].duration = pc_duration(victim, 10, GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_CLOUD_OF_ARROWS);
			af[0].location = APPLY_HITROLL;
			af[0].modifier = level / 6;
			accum_duration = TRUE;
			to_room = "$n3 окружило облако летающих огненных стрел.";
			to_vict = "Вас окружило облако летающих огненных стрел.";
			break;

		case SPELL_STONEHAND: af[0].bitvector = to_underlying(EAffectFlag::AFF_STONEHAND);
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "Руки $n1 задубели.";
			to_vict = "Ваши руки задубели.";
			break;

		case SPELL_GROUP_PRISMATICAURA:
		case SPELL_PRISMATICAURA:
			if (!IS_NPC(ch) && !same_group(ch, victim)) {
				send_to_char("Только на себя или одногруппника!\r\n", ch);
				return 0;
			}
			if (affected_by_spell(victim, SPELL_SANCTUARY)) {
				affect_from_char(victim, SPELL_SANCTUARY);
				success = FALSE;
				break;
			}
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_SANCTUARY)) {
				success = FALSE;
				break;
			}
			af[0].bitvector = to_underlying(EAffectFlag::AFF_PRISMATICAURA);
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			accum_duration = TRUE;
			to_room = "$n3 покрыла призматическая аура.";
			to_vict = "Вас покрыла призматическая аура.";
			spellnum = SPELL_PRISMATICAURA;
			break;

		case SPELL_MINDLESS:
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			af[0].location = APPLY_MANAREG;
			af[0].modifier = -50;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim,
																	0,
																	GET_REAL_WIS(ch) + GET_REAL_INT(ch),
																	10,
																	0,
																	0))
				* koef_duration;
			af[1].location = APPLY_CAST_SUCCESS;
			af[1].modifier = -50;
			af[1].duration = af[0].duration;
			af[2].location = APPLY_HITROLL;
			af[2].modifier = -5;
			af[2].duration = af[0].duration;

			to_room = "$n0 стал$g слаб$g на голову!";
			to_vict = "Ваш разум помутился!";
			break;

		case SPELL_DUSTSTORM:
		case SPELL_SHINEFLASH:
		case SPELL_MASS_BLINDNESS:
		case SPELL_POWER_BLINDNESS:
		case SPELL_BLINDNESS: savetype = SAVING_STABILITY;
			if (MOB_FLAGGED(victim, MOB_NOBLIND) ||
				WAITLESS(victim) ||
				((ch != victim) &&
					!GET_GOD_FLAG(victim, GF_GODSCURSE) && general_savingthrow(ch, victim, savetype, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			switch (spellnum) {
				case SPELL_DUSTSTORM:
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 3, level, 6, 0, 0)) * koef_duration;
					break;
				case SPELL_SHINEFLASH:
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 2, level + 7, 8, 0, 0))
						* koef_duration;
					break;
				case SPELL_MASS_BLINDNESS:
				case SPELL_BLINDNESS:
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 2, level, 8, 0, 0)) * koef_duration;
					break;
				case SPELL_POWER_BLINDNESS:
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 3, level, 6, 0, 0)) * koef_duration;
					break;
			}
			af[0].bitvector = to_underlying(EAffectFlag::AFF_BLIND);
			af[0].battleflag = AF_BATTLEDEC;
			to_room = "$n0 ослеп$q!";
			to_vict = "Вы ослепли!";
			spellnum = SPELL_BLINDNESS;
			break;

		case SPELL_MADNESS: savetype = SAVING_WILL;
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 3, 0, 0, 0, 0)) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_NOFLEE);
			af[1].location = APPLY_MADNESS;
			af[1].duration = af[0].duration;
			af[1].modifier = level;
			to_room = "Теперь $n не сможет сбежать из боя!";
			to_vict = "Вас обуяло безумие!";
			break;

		case SPELL_WEB: savetype = SAVING_REFLEX;
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_BROKEN_CHAINS)
				|| (ch != victim && general_savingthrow(ch, victim, savetype, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			af[0].location = APPLY_HITROLL;
			af[0].modifier = -2 - GET_REMORT(ch) / 5;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 3, level, 6, 0, 0)) * koef_duration;
			af[0].battleflag = AF_BATTLEDEC;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_NOFLEE);
			af[1].location = APPLY_AC;
			af[1].modifier = 20;
			af[1].duration = af[0].duration;
			af[1].battleflag = AF_BATTLEDEC;
			af[1].bitvector = to_underlying(EAffectFlag::AFF_NOFLEE);
			to_room = "$n3 покрыла невидимая паутина, сковывая $s движения!";
			to_vict = "Вас покрыла невидимая паутина!";
			break;

		case SPELL_MASS_CURSE:
		case SPELL_CURSE: savetype = SAVING_WILL;
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			// если есть фит порча
			if (can_use_feat(ch, DECLINE_FEAT))
				decline_mod += GET_REMORT(ch);
			af[0].location = APPLY_INITIATIVE;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 1, level, 2, 0, 0)) * koef_duration;
			af[0].modifier = -(5 + decline_mod);
			af[0].bitvector = to_underlying(EAffectFlag::AFF_CURSE);

			af[1].location = APPLY_HITROLL;
			af[1].duration = af[0].duration;
			af[1].modifier = -(level / 6 + decline_mod + GET_REMORT(ch) / 5);
			af[1].bitvector = to_underlying(EAffectFlag::AFF_CURSE);

			if (level >= 20) {
				af[2].location = APPLY_CAST_SUCCESS;
				af[2].duration = af[0].duration;
				af[2].modifier = -(level / 3 + GET_REMORT(ch));
				if (IS_NPC(ch) && level >= (LVL_IMMORT))
					af[2].modifier += (LVL_IMMORT - level - 1);    //1 cast per mob level above 30
				af[2].bitvector = to_underlying(EAffectFlag::AFF_CURSE);
			}
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_room = "Красное сияние вспыхнуло над $n4 и тут же погасло!";
			to_vict = "Боги сурово поглядели на вас.";
			spellnum = SPELL_CURSE;
			break;

		case SPELL_MASS_SLOW:
		case SPELL_SLOW: savetype = SAVING_STABILITY;
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_BROKEN_CHAINS)
				|| (ch != victim && general_savingthrow(ch, victim, savetype, modi * number(1, koef_modifier / 2)))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			if (affected_by_spell(victim, SPELL_HASTE)) {
				affect_from_char(victim, SPELL_HASTE);
				success = FALSE;
				break;
			}

			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 9, 0, 0, 0, 0)) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_SLOW);
			af[1].duration =
				calculate_resistance_coeff(victim, get_resist_type(spellnum), pc_duration(victim, 9, 0, 0, 0, 0))
					* koef_duration;
			af[1].location = APPLY_DEX;
			af[1].modifier = -koef_modifier;
			to_room = "Движения $n1 заметно замедлились.";
			to_vict = "Ваши движения заметно замедлились.";
			spellnum = SPELL_SLOW;
			break;

		case SPELL_GENERAL_SINCERITY:
		case SPELL_DETECT_ALIGN:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_DETECT_ALIGN);
			accum_duration = TRUE;
			to_vict = "Ваши глаза приобрели зеленый оттенок.";
			to_room = "Глаза $n1 приобрели зеленый оттенок.";
			spellnum = SPELL_DETECT_ALIGN;
			break;

		case SPELL_ALL_SEEING_EYE:
		case SPELL_DETECT_INVIS:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_DETECT_INVIS);
			accum_duration = TRUE;
			to_vict = "Ваши глаза приобрели золотистый оттенок.";
			to_room = "Глаза $n1 приобрели золотистый оттенок.";
			spellnum = SPELL_DETECT_INVIS;
			break;

		case SPELL_MAGICAL_GAZE:
		case SPELL_DETECT_MAGIC:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_DETECT_MAGIC);
			accum_duration = TRUE;
			to_vict = "Ваши глаза приобрели желтый оттенок.";
			to_room = "Глаза $n1 приобрели желтый оттенок.";
			spellnum = SPELL_DETECT_MAGIC;
			break;

		case SPELL_SIGHT_OF_DARKNESS:
		case SPELL_INFRAVISION:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_INFRAVISION);
			accum_duration = TRUE;
			to_vict = "Ваши глаза приобрели красный оттенок.";
			to_room = "Глаза $n1 приобрели красный оттенок.";
			spellnum = SPELL_INFRAVISION;
			break;

		case SPELL_SNAKE_EYES:
		case SPELL_DETECT_POISON:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_DETECT_POISON);
			accum_duration = TRUE;
			to_vict = "Ваши глаза приобрели карий оттенок.";
			to_room = "Глаза $n1 приобрели карий оттенок.";
			spellnum = SPELL_DETECT_POISON;
			break;

		case SPELL_GROUP_INVISIBLE:
		case SPELL_INVISIBLE:
			if (!victim)
				victim = ch;
			if (affected_by_spell(victim, SPELL_GLITTERDUST)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].modifier = -40;
			af[0].location = APPLY_AC;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_INVISIBLE);
			accum_duration = TRUE;
			to_vict = "Вы стали невидимы для окружающих.";
			to_room = "$n медленно растворил$u в пустоте.";
			spellnum = SPELL_INVISIBLE;
			break;

		case SPELL_PLAQUE: savetype = SAVING_STABILITY;
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			af[0].location = APPLY_HITREG;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 0, level, 2, 0, 0)) * koef_duration;
			af[0].modifier = -95;
			af[1].location = APPLY_MANAREG;
			af[1].duration = af[0].duration;
			af[1].modifier = -95;
			af[2].location = APPLY_MOVEREG;
			af[2].duration = af[0].duration;
			af[2].modifier = -95;
			af[3].location = APPLY_PLAQUE;
			af[3].duration = af[0].duration;
			af[3].modifier = level;
			af[4].location = APPLY_WIS;
			af[4].duration = af[0].duration;
			af[4].modifier = -GET_REMORT(ch) / 5;
			af[5].location = APPLY_INT;
			af[5].duration = af[0].duration;
			af[5].modifier = -GET_REMORT(ch) / 5;
			af[6].location = APPLY_DEX;
			af[6].duration = af[0].duration;
			af[6].modifier = -GET_REMORT(ch) / 5;
			af[7].location = APPLY_STR;
			af[7].duration = af[0].duration;
			af[7].modifier = -GET_REMORT(ch) / 5;
			to_vict = "Вас скрутило в жестокой лихорадке.";
			to_room = "$n3 скрутило в жестокой лихорадке.";
			break;

		case SPELL_POISON: savetype = SAVING_CRITICAL;
			if (ch != victim && (AFF_FLAGGED(victim, EAffectFlag::AFF_SHIELD) ||
				general_savingthrow(ch, victim, savetype, modi - GET_REAL_CON(victim) / 2))) {
				if (ch->in_room
					== IN_ROOM(victim)) // Добавлено чтобы яд нанесенный SPELL_POISONED_FOG не спамил чару постоянно
					send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			af[0].location = APPLY_STR;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 0, level, 1, 0, 0)) * koef_duration;
			af[0].modifier = -2;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_POISON);
			af[0].battleflag = AF_SAME_TIME;

			af[1].location = APPLY_POISON;
			af[1].duration = af[0].duration;
			af[1].modifier = level + GET_REMORT(ch) / 2;
			af[1].bitvector = to_underlying(EAffectFlag::AFF_POISON);
			af[1].battleflag = AF_SAME_TIME;

			to_vict = "Вы почувствовали себя отравленным.";
			to_room = "$n позеленел$g от действия яда.";

			break;

		case SPELL_PROT_FROM_EVIL:
		case SPELL_GROUP_PROT_FROM_EVIL:
			if (!IS_NPC(ch) && !same_group(ch, victim)) {
				send_to_char("Только на себя или одногруппника!\r\n", ch);
				return 0;
			}
			af[0].location = APPLY_RESIST_DARK;
			if (spellnum == SPELL_PROT_FROM_EVIL) {
				affect_from_char(ch, SPELL_GROUP_PROT_FROM_EVIL);
				af[0].modifier = 5;
			} else {
				affect_from_char(ch, SPELL_PROT_FROM_EVIL);
				af[0].modifier = level;
			}
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_PROTECT_EVIL);
			accum_duration = TRUE;
			to_vict = "Вы подавили в себе страх к тьме.";
			to_room = "$n подавил$g в себе страх к тьме.";
			break;

		case SPELL_GROUP_SANCTUARY:
		case SPELL_SANCTUARY:
			if (!IS_NPC(ch) && !same_group(ch, victim)) {
				send_to_char("Только на себя или одногруппника!\r\n", ch);
				return 0;
			}
			if (affected_by_spell(victim, SPELL_PRISMATICAURA)) {
				affect_from_char(victim, SPELL_PRISMATICAURA);
				success = FALSE;
				break;
			}
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_PRISMATICAURA)) {
				success = FALSE;
				break;
			}

			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_SANCTUARY);
			to_vict = "Белая аура мгновенно окружила вас.";
			to_room = "Белая аура покрыла $n3 с головы до пят.";
			spellnum = SPELL_SANCTUARY;
			break;

		case SPELL_SLEEP: savetype = SAVING_WILL;
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_HOLD) || MOB_FLAGGED(victim, MOB_NOSLEEP)
				|| (ch != victim && general_savingthrow(ch, victim, SAVING_WILL, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			};

			if (victim->get_fighting())
				stop_fighting(victim, FALSE);
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 1, level, 6, 1, 6)) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_SLEEP);
			af[0].battleflag = AF_BATTLEDEC;
			if (GET_POS(victim) > POS_SLEEPING && success) {
				// add by Pereplut
				if (victim->ahorse())
					victim->drop_from_horse();
				send_to_char("Вы слишком устали... Спать... Спа...\r\n", victim);
				act("$n прилег$q подремать.", TRUE, victim, 0, 0, TO_ROOM | TO_ARENA_LISTEN);

				GET_POS(victim) = POS_SLEEPING;
			}
			break;

		case SPELL_GROUP_STRENGTH:
		case SPELL_STRENGTH:
			if (affected_by_spell(victim, SPELL_WEAKNESS)) {
				affect_from_char(victim, SPELL_WEAKNESS);
				success = FALSE;
				break;
			}
			af[0].location = APPLY_STR;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			if (ch == victim)
				af[0].modifier = (level + 9) / 10 + koef_modifier + GET_REMORT(ch) / 5;
			else
				af[0].modifier = (level + 14) / 15 + koef_modifier + GET_REMORT(ch) / 5;
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_vict = "Вы почувствовали себя сильнее.";
			to_room = "Мышцы $n1 налились силой.";
			spellnum = SPELL_STRENGTH;
			break;

		case SPELL_DEXTERITY:
			if (affected_by_spell(victim, SPELL_WEAKNESS)) {
				affect_from_char(victim, SPELL_WEAKNESS);
				success = FALSE;
				break;
			}
			af[0].location = APPLY_DEX;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			if (ch == victim)
				af[0].modifier = (level + 9) / 10 + koef_modifier + GET_REMORT(ch) / 5;
			else
				af[0].modifier = (level + 14) / 15 + koef_modifier + GET_REMORT(ch) / 5;
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_vict = "Вы почувствовали себя более шустрым.";
			to_room = "$n0 будет двигаться более шустро.";
			spellnum = SPELL_DEXTERITY;
			break;

		case SPELL_PATRONAGE: af[0].location = APPLY_HIT;
			af[0].duration = pc_duration(victim, 3, level, 10, 0, 0) * koef_duration;
			af[0].modifier = GET_LEVEL(ch) * 2 + GET_REMORT(ch);
			if (GET_ALIGNMENT(victim) >= 0) {
				to_vict = "Исходящий с небес свет на мгновение озарил вас.";
				to_room = "Исходящий с небес свет на мгновение озарил $n3.";
			} else {
				to_vict = "Вас окутало клубящееся облако Тьмы.";
				to_room = "Клубящееся темное облако на мгновение окутало $n3.";
			}
			break;

		case SPELL_EYE_OF_GODS:
		case SPELL_SENSE_LIFE: to_vict = "Вы способны разглядеть даже микроба.";
			to_room = "$n0 начал$g замечать любые движения.";
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_SENSE_LIFE);
			accum_duration = TRUE;
			spellnum = SPELL_SENSE_LIFE;
			break;

		case SPELL_WATERWALK:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_WATERWALK);
			accum_duration = TRUE;
			to_vict = "На рыбалку вы можете отправляться без лодки.";
			break;

		case SPELL_BREATHING_AT_DEPTH:
		case SPELL_WATERBREATH:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_WATERBREATH);
			accum_duration = TRUE;
			to_vict = "У вас выросли жабры.";
			to_room = "У $n1 выросли жабры.";
			spellnum = SPELL_WATERBREATH;
			break;

		case SPELL_HOLYSTRIKE:
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_EVILESS)) {
				// все решится в дамадже части спелла
				success = FALSE;
				break;
			}
			// тут break не нужен

			// fall through
		case SPELL_MASS_HOLD:
		case SPELL_POWER_HOLD:
		case SPELL_HOLD:
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_SLEEP)
				|| MOB_FLAGGED(victim, MOB_NOHOLD) || AFF_FLAGGED(victim, EAffectFlag::AFF_BROKEN_CHAINS)
				|| (ch != victim && general_savingthrow(ch, victim, SAVING_WILL, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														spellnum == SPELL_POWER_HOLD ? pc_duration(victim,
																								   2,
																								   level + 7,
																								   8,
																								   2,
																								   5)
																					 : pc_duration(victim,
																								   1,
																								   level + 9,
																								   10,
																								   1,
																								   3)) * koef_duration;

			af[0].bitvector = to_underlying(EAffectFlag::AFF_HOLD);
			af[0].battleflag = AF_BATTLEDEC;
			to_room = "$n0 замер$q на месте!";
			to_vict = "Вы замерли на месте, не в силах пошевельнуться.";
			spellnum = SPELL_HOLD;
			break;

		case SPELL_WC_OF_RAGE:
		case SPELL_SONICWAVE:
		case SPELL_MASS_DEAFNESS:
		case SPELL_POWER_DEAFNESS:
		case SPELL_DEAFNESS:
			switch (spellnum) {
				case SPELL_WC_OF_RAGE: savetype = SAVING_WILL;
					modi = GET_REAL_CON(ch);
					break;
				case SPELL_SONICWAVE:
				case SPELL_MASS_DEAFNESS:
				case SPELL_POWER_DEAFNESS:
				case SPELL_DEAFNESS: savetype = SAVING_STABILITY;
					break;
			}
			if (  //MOB_FLAGGED(victim, MOB_NODEAFNESS) ||
				(ch != victim && general_savingthrow(ch, victim, savetype, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			switch (spellnum) {
				case SPELL_WC_OF_RAGE:
				case SPELL_POWER_DEAFNESS:
				case SPELL_SONICWAVE:
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 2, level + 3, 4, 6, 0))
						* koef_duration;
					break;
				case SPELL_MASS_DEAFNESS:
				case SPELL_DEAFNESS:
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 2, level + 7, 8, 3, 0))
						* koef_duration;
					break;
			}
			af[0].bitvector = to_underlying(EAffectFlag::AFF_DEAFNESS);
			af[0].battleflag = AF_BATTLEDEC;
			to_room = "$n0 оглох$q!";
			to_vict = "Вы оглохли.";
			spellnum = SPELL_DEAFNESS;
			break;

		case SPELL_MASS_SILENCE:
		case SPELL_POWER_SILENCE:
		case SPELL_SILENCE: savetype = SAVING_WILL;
			if (MOB_FLAGGED(victim, MOB_NOSIELENCE) ||
				(ch != victim && general_savingthrow(ch, victim, savetype, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														spellnum == SPELL_POWER_SILENCE ? pc_duration(victim,
																									  2,
																									  level + 3,
																									  4,
																									  6,
																									  0)
																						: pc_duration(victim,
																									  2,
																									  level + 7,
																									  8,
																									  3,
																									  0))
				* koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_SILENCE);
			af[0].battleflag = AF_BATTLEDEC;
			to_room = "$n0 прикусил$g язык!";
			to_vict = "Вы не в состоянии вымолвить ни слова.";
			spellnum = SPELL_SILENCE;
			break;

		case SPELL_GROUP_FLY:
		case SPELL_FLY:
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_FLY);
			to_room = "$n0 медленно поднял$u в воздух.";
			to_vict = "Вы медленно поднялись в воздух.";
			spellnum = SPELL_FLY;
			break;

		case SPELL_BROKEN_CHAINS: af[0].duration = pc_duration(victim, 10, GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_BROKEN_CHAINS);
			af[0].battleflag = AF_BATTLEDEC;
			to_room = "Ярко-синий ореол вспыхнул вокруг $n1 и тут же угас.";
			to_vict = "Волна ярко-синего света омыла вас с головы до ног.";
			break;
		case SPELL_GROUP_BLINK:
		case SPELL_BLINK: af[0].location = APPLY_SPELL_BLINK;
			af[0].modifier = 5 + GET_REMORT(ch) * 2 / 3.0;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			to_room = "$n начал$g мигать.";
			to_vict = "Вы начали мигать.";
			spellnum = SPELL_BLINK;
			break;

		case SPELL_MAGICSHIELD: af[0].location = APPLY_AC;
			af[0].modifier = -GET_LEVEL(ch) * 10 / 6;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[1].location = APPLY_SAVING_REFLEX;
			af[1].modifier = -GET_LEVEL(ch) / 5;
			af[1].duration = af[0].duration;
			af[2].location = APPLY_SAVING_STABILITY;
			af[2].modifier = -GET_LEVEL(ch) / 5;
			af[2].duration = af[0].duration;
			accum_duration = TRUE;
			to_room = "Сверкающий щит вспыхнул вокруг $n1 и угас.";
			to_vict = "Сверкающий щит вспыхнул вокруг вас и угас.";
			break;

		case SPELL_NOFLEE: // "приковать противника"
		case SPELL_INDRIKS_TEETH:
		case SPELL_MASS_NOFLEE: af[0].battleflag = AF_BATTLEDEC;
			savetype = SAVING_WILL;
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_BROKEN_CHAINS)
				|| (ch != victim && general_savingthrow(ch, victim, savetype, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 3, level, 4, 4, 0)) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_NOTELEPORT);
			to_room = "$n0 теперь прикован$a к $N2.";
			to_vict = "Вы не сможете покинуть $N3.";
			break;

		case SPELL_LIGHT:
			if (!IS_NPC(ch) && !same_group(ch, victim)) {
				send_to_char("Только на себя или одногруппника!\r\n", ch);
				return 0;
			}
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_HOLYLIGHT);
			to_room = "$n0 начал$g светиться ярким светом.";
			to_vict = "Вы засветились, освещая комнату.";
			break;

		case SPELL_DARKNESS:
			if (!IS_NPC(ch) && !same_group(ch, victim)) {
				send_to_char("Только на себя или одногруппника!\r\n", ch);
				return 0;
			}
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_HOLYDARK);
			to_room = "$n0 погрузил$g комнату во мрак.";
			to_vict = "Вы погрузили комнату в непроглядную тьму.";
			break;
		case SPELL_VAMPIRE: af[0].duration = pc_duration(victim, 10, GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].location = APPLY_DAMROLL;
			af[0].modifier = 0;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_VAMPIRE);
			to_room = "Зрачки $n3 приобрели красный оттенок.";
			to_vict = "Ваши зрачки приобрели красный оттенок.";
			break;

		case SPELL_EVILESS:
			if (!IS_NPC(victim) || victim->get_master() != ch || !MOB_FLAGGED(victim, MOB_CORPSE)) {
				//тихо уходим, т.к. заклинание массовое
				break;
			}
			af[0].duration = pc_duration(victim, 10, GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].location = APPLY_DAMROLL;
			af[0].modifier = 15 + (GET_REMORT(ch) > 8 ? (GET_REMORT(ch) - 8) : 0);
			af[0].bitvector = to_underlying(EAffectFlag::AFF_EVILESS);
			af[1].duration = af[0].duration;
			af[1].location = APPLY_HITROLL;
			af[1].modifier = 7 + (GET_REMORT(ch) > 8 ? (GET_REMORT(ch) - 8) : 0);;
			af[1].bitvector = to_underlying(EAffectFlag::AFF_EVILESS);
			af[2].duration = af[0].duration;
			af[2].location = APPLY_HIT;
			af[2].bitvector = to_underlying(EAffectFlag::AFF_EVILESS);

			// иначе, при рекасте, модификатор суммируется с текущим аффектом.
			if (!AFF_FLAGGED(victim, EAffectFlag::AFF_EVILESS)) {
				af[2].modifier = GET_REAL_MAX_HIT(victim);
				// не очень красивый способ передать сигнал на лечение в mag_points
				AFFECT_DATA<EApplyLocation> tmpaf;
				tmpaf.type = SPELL_EVILESS;
				tmpaf.duration = 1;
				tmpaf.modifier = 0;
				tmpaf.location = EApplyLocation::APPLY_NONE;
				tmpaf.battleflag = 0;
				tmpaf.bitvector = to_underlying(EAffectFlag::AFF_EVILESS);
				affect_to_char(ch, tmpaf);
			}
			to_vict = "Черное облако покрыло вас.";
			to_room = "Черное облако покрыло $n3 с головы до пят.";
			break;

		case SPELL_WC_OF_THUNDER:
		case SPELL_ICESTORM:
		case SPELL_EARTHFALL:
		case SPELL_SHOCK: {
			switch (spellnum) {
				case SPELL_WC_OF_THUNDER: savetype = SAVING_WILL;
					modi = GET_REAL_CON(ch) * 3 / 2;
					break;
				case SPELL_ICESTORM: savetype = SAVING_REFLEX;
					modi = CALC_SUCCESS(modi, 30);
					break;
				case SPELL_EARTHFALL: savetype = SAVING_REFLEX;
					modi = CALC_SUCCESS(modi, 95);
					break;
				case SPELL_SHOCK: savetype = SAVING_REFLEX;
					if (GET_CLASS(ch) == CLASS_CLERIC) {
						modi = CALC_SUCCESS(modi, 75);
					} else {
						modi = CALC_SUCCESS(modi, 25);
					}
					break;
			}
			if (WAITLESS(victim) || (!WAITLESS(ch) && general_savingthrow(ch, victim, savetype, modi))) {
				success = FALSE;
				break;
			}
			switch (spellnum) {
				case SPELL_WC_OF_THUNDER: af[0].type = SPELL_DEAFNESS;
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 2, level + 3, 4, 6, 0))
						* koef_duration;
					af[0].duration = complex_spell_modifier(ch, SPELL_DEAFNESS, GAPPLY_SPELL_EFFECT, af[0].duration);
					af[0].bitvector = to_underlying(EAffectFlag::AFF_DEAFNESS);
					af[0].battleflag = AF_BATTLEDEC;
					to_room = "$n0 оглох$q!";
					to_vict = "Вы оглохли.";

					if ((IS_NPC(victim)
						&& AFF_FLAGGED(victim, static_cast<EAffectFlag>(af[0].bitvector)))
						|| (ch != victim
							&& affected_by_spell(victim, SPELL_DEAFNESS))) {
						if (ch->in_room == IN_ROOM(victim))
							send_to_char(NOEFFECT, ch);
					} else {
						affect_join(victim, af[0], accum_duration, FALSE, accum_affect, FALSE);
						act(to_vict, FALSE, victim, 0, ch, TO_CHAR);
						act(to_room, TRUE, victim, 0, ch, TO_ROOM | TO_ARENA_LISTEN);
					}
					break;

				case SPELL_ICESTORM:
				case SPELL_EARTHFALL: WAIT_STATE(victim, 2 * PULSE_VIOLENCE);
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 2, 0, 0, 0, 0)) * koef_duration;
					af[0].bitvector = to_underlying(EAffectFlag::AFF_MAGICSTOPFIGHT);
					af[0].battleflag = AF_BATTLEDEC | AF_PULSEDEC;
					to_room = "$n3 оглушило.";
					to_vict = "Вас оглушило.";
					spellnum = SPELL_MAGICBATTLE;
					break;

				case SPELL_SHOCK: WAIT_STATE(victim, 2 * PULSE_VIOLENCE);
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 2, 0, 0, 0, 0)) * koef_duration;
					af[0].bitvector = to_underlying(EAffectFlag::AFF_MAGICSTOPFIGHT);
					af[0].battleflag = AF_BATTLEDEC | AF_PULSEDEC;
					to_room = "$n3 оглушило.";
					to_vict = "Вас оглушило.";
					spellnum = SPELL_MAGICBATTLE;
					mag_affects(level, ch, victim, SPELL_BLINDNESS, SAVING_STABILITY);
					break;
			}
			break;
		}

//Заклинание плач. Далим.
		case SPELL_CRYING: {
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_CRYING)
				|| (ch != victim && general_savingthrow(ch, victim, savetype, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			af[0].location = APPLY_HIT;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 4, 0, 0, 0, 0)) * koef_duration;
			af[0].modifier =
				-1 * MAX(1,
						 (MIN(29, GET_LEVEL(ch)) - MIN(24, GET_LEVEL(victim)) +
							 GET_REMORT(ch) / 3) * GET_MAX_HIT(victim) / 100);
			af[0].bitvector = to_underlying(EAffectFlag::AFF_CRYING);
			if (IS_NPC(victim)) {
				af[1].location = APPLY_LIKES;
				af[1].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
															pc_duration(victim, 5, 0, 0, 0, 0));
				af[1].modifier = -1 * MAX(1, ((level + 9) / 2 + 9 - GET_LEVEL(victim) / 2));
				af[1].bitvector = to_underlying(EAffectFlag::AFF_CRYING);
				af[1].battleflag = AF_BATTLEDEC;
				to_room = "$n0 издал$g протяжный стон.";
				break;
			}
			af[1].location = APPLY_CAST_SUCCESS;
			af[1].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 5, 0, 0, 0, 0));
			af[1].modifier = -1 * MAX(1, (level / 3 + GET_REMORT(ch) / 3 - GET_LEVEL(victim) / 10));
			af[1].bitvector = to_underlying(EAffectFlag::AFF_CRYING);
			af[1].battleflag = AF_BATTLEDEC;
			af[2].location = APPLY_MORALE;
			af[2].duration = af[1].duration;
			af[2].modifier = -1 * MAX(1, (level / 3 + GET_REMORT(ch) / 5 - GET_LEVEL(victim) / 5));
			af[2].bitvector = to_underlying(EAffectFlag::AFF_CRYING);
			af[2].battleflag = AF_BATTLEDEC;
			to_room = "$n0 издал$g протяжный стон.";
			to_vict = "Вы впали в уныние.";
			break;
		}
			//Заклинания Забвение, Бремя времени. Далим.
		case SPELL_OBLIVION:
		case SPELL_BURDEN_OF_TIME: {
			if (WAITLESS(victim)
				|| general_savingthrow(ch, victim, SAVING_REFLEX,
									   CALC_SUCCESS(modi, (spellnum == SPELL_OBLIVION ? 40 : 90)))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			WAIT_STATE(victim, (level / 10 + 1) * PULSE_VIOLENCE);
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 3, 0, 0, 0, 0)) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_SLOW);
			af[0].battleflag = AF_BATTLEDEC;
			to_room = "Облако забвения окружило $n3.";
			to_vict = "Ваш разум помутился.";
			spellnum = SPELL_OBLIVION;
			break;
		}

		case SPELL_PEACEFUL: {
			if (AFF_FLAGGED(victim, EAffectFlag::AFF_PEACEFUL)
				|| (IS_NPC(victim) && !AFF_FLAGGED(victim, EAffectFlag::AFF_CHARM)) ||
				(ch != victim && general_savingthrow(ch, victim, savetype, modi))) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			if (victim->get_fighting()) {
				stop_fighting(victim, TRUE);
				change_fighting(victim, TRUE);
				WAIT_STATE(victim, 2 * PULSE_VIOLENCE);
			}
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 2, 0, 0, 0, 0)) * koef_duration;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_PEACEFUL);
			to_room = "Взгляд $n1 потускнел, а сам он успокоился.";
			to_vict = "Ваша душа очистилась от зла и странно успокоилась.";
			break;
		}

		case SPELL_STONEBONES: {
			if (GET_MOB_VNUM(victim) < MOB_SKELETON || GET_MOB_VNUM(victim) > LAST_NECRO_MOB) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
			}
			af[0].location = APPLY_ARMOUR;
			af[0].duration = pc_duration(victim, 100, level, 1, 0, 0) * koef_duration;
			af[0].modifier = level + 10 + GET_REMORT(ch) / 2;
			af[1].location = APPLY_SAVING_STABILITY;
			af[1].duration = af[0].duration;
			af[1].modifier = level + 10 + GET_REMORT(ch) / 2;
			accum_duration = TRUE;
			to_vict = " ";
			to_room = "Кости $n1 обрели твердость кремня.";
			break;
		}

		case SPELL_FAILURE:
		case SPELL_MASS_FAILURE: {
			savetype = SAVING_WILL;
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			af[0].location = APPLY_MORALE;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 2, level, 2, 0, 0)) * koef_duration;
			af[0].modifier = -5 - (GET_LEVEL(ch) + GET_REMORT(ch)) / 2;
			af[1].location = static_cast<EApplyLocation>(number(1, 6));
			af[1].duration = af[0].duration;
			af[1].modifier = -(GET_LEVEL(ch) + GET_REMORT(ch) * 3) / 15;
			to_room = "Тяжелое бурое облако сгустилось над $n4.";
			to_vict = "Тяжелые тучи сгустились над вами, и вы почувствовали, что удача покинула вас.";
			break;
		}

		case SPELL_GLITTERDUST: {
			savetype = SAVING_REFLEX;
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi + 50)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}

			if (affected_by_spell(victim, SPELL_INVISIBLE)) {
				affect_from_char(victim, SPELL_INVISIBLE);
			}
			if (affected_by_spell(victim, SPELL_CAMOUFLAGE)) {
				affect_from_char(victim, SPELL_CAMOUFLAGE);
			}
			if (affected_by_spell(victim, SPELL_HIDE)) {
				affect_from_char(victim, SPELL_HIDE);
			}
			af[0].location = APPLY_SAVING_REFLEX;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 4, 0, 0, 0, 0)) * koef_duration;
			af[0].modifier = (GET_LEVEL(ch) + GET_REMORT(ch)) / 3;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_GLITTERDUST);
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_room = "Облако ярко блестящей пыли накрыло $n3.";
			to_vict = "Липкая блестящая пыль покрыла вас с головы до пят.";
			break;
		}

		case SPELL_SCREAM: {
			savetype = SAVING_STABILITY;
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			af[0].bitvector = to_underlying(EAffectFlag::AFF_AFFRIGHT);
			af[0].location = APPLY_SAVING_WILL;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 2, level, 2, 0, 0)) * koef_duration;
			af[0].modifier = (2 * GET_LEVEL(ch) + GET_REMORT(ch)) / 4;

			af[1].bitvector = to_underlying(EAffectFlag::AFF_AFFRIGHT);
			af[1].location = APPLY_MORALE;
			af[1].duration = af[0].duration;
			af[1].modifier = -(GET_LEVEL(ch) + GET_REMORT(ch)) / 6;

			to_room = "$n0 побледнел$g и задрожал$g от страха.";
			to_vict = "Страх сжал ваше сердце ледяными когтями.";
			break;
		}

		case SPELL_CATS_GRACE: {
			af[0].location = APPLY_DEX;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			if (ch == victim)
				af[0].modifier = (level + 5) / 10;
			else
				af[0].modifier = (level + 10) / 15;
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_vict = "Ваши движения обрели невиданную ловкость.";
			to_room = "Движения $n1 обрели невиданную ловкость.";
			break;
		}

		case SPELL_BULL_BODY: {
			af[0].location = APPLY_CON;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0);
			if (ch == victim)
				af[0].modifier = (level + 5) / 10;
			else
				af[0].modifier = (level + 10) / 15;
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_vict = "Ваше тело налилось звериной мощью.";
			to_room = "Плечи $n1 раздались вширь, а тело налилось звериной мощью.";
			break;
		}

		case SPELL_SNAKE_WISDOM: {
			af[0].location = APPLY_WIS;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].modifier = (level + 6) / 15;
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_vict = "Шелест змеиной чешуи коснулся вашего сознания, и вы стали мудрее.";
			to_room = "$n спокойно и мудро посмотрел$g вокруг.";
			break;
		}

		case SPELL_GIMMICKRY: {
			af[0].location = APPLY_INT;
			af[0].duration = pc_duration(victim, 20, SECS_PER_PLAYER_AFFECT * GET_REMORT(ch), 1, 0, 0) * koef_duration;
			af[0].modifier = (level + 6) / 15;
			accum_duration = TRUE;
			accum_affect = TRUE;
			to_vict = "Вы почувствовали, что для вашего ума более нет преград.";
			to_room = "$n хитро прищурил$u и поглядел$g по сторонам.";
			break;
		}

		case SPELL_WC_OF_MENACE: {
			savetype = SAVING_WILL;
			modi = GET_REAL_CON(ch);
			if (ch != victim && general_savingthrow(ch, victim, savetype, modi)) {
				send_to_char(NOEFFECT, ch);
				success = FALSE;
				break;
			}
			af[0].location = APPLY_MORALE;
			af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
														pc_duration(victim, 2, level + 3, 4, 6, 0)) * koef_duration;
			af[0].modifier = -dice((7 + level) / 8, 3);
			to_vict = "Похоже, сегодня не ваш день.";
			to_room = "Удача покинула $n3.";
			break;
		}

		case SPELL_WC_OF_MADNESS: {
			savetype = SAVING_STABILITY;
			modi = GET_REAL_CON(ch) * 3 / 2;
			if (ch == victim || !general_savingthrow(ch, victim, savetype, modi)) {
				af[0].location = APPLY_INT;
				af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
															pc_duration(victim, 2, level + 3, 4, 6, 0)) * koef_duration;
				af[0].modifier = -dice((7 + level) / 8, 2);
				to_vict = "Вы потеряли рассудок.";
				to_room = "$n0 потерял$g рассудок.";

				savetype = SAVING_STABILITY;
				modi = GET_REAL_CON(ch) * 2;
				if (ch == victim || !general_savingthrow(ch, victim, savetype, modi)) {
					af[1].location = APPLY_CAST_SUCCESS;
					af[1].duration = af[0].duration;
					af[1].modifier = -(dice((2 + level) / 3, 4) + dice(GET_REMORT(ch) / 2, 5));

					af[2].location = APPLY_MANAREG;
					af[2].duration = af[1].duration;
					af[2].modifier = af[1].modifier;
					to_vict = "Вы обезумели.";
					to_room = "$n0 обезумел$g.";
				}
			} else {
				savetype = SAVING_STABILITY;
				modi = GET_REAL_CON(ch) * 2;
				if (!general_savingthrow(ch, victim, savetype, modi)) {
					af[0].location = APPLY_CAST_SUCCESS;
					af[0].duration = calculate_resistance_coeff(victim, get_resist_type(spellnum),
																pc_duration(victim, 2, level + 3, 4, 6, 0))
						* koef_duration;
					af[0].modifier = -(dice((2 + level) / 3, 4) + dice(GET_REMORT(ch) / 2, 5));

					af[1].location = APPLY_MANAREG;
					af[1].duration = af[0].duration;
					af[1].modifier = af[0].modifier;
					to_vict = "Вас охватила паника.";
					to_room = "$n0 начал$g сеять панику.";
				} else {
					send_to_char(NOEFFECT, ch);
					success = FALSE;
				}
			}
			update_spell = TRUE;
			break;
		}

		case SPELL_WC_LUCK: {
			af[0].location = APPLY_MORALE;
			af[0].modifier = MAX(1, ch->get_skill(SKILL_WARCRY) / 20.0);
			af[0].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			to_room = nullptr;
			break;
		}

		case SPELL_WC_EXPERIENSE: {
			af[0].location = APPLY_PERCENT_EXP;
			af[0].modifier = MAX(1, ch->get_skill(SKILL_WARCRY) / 20.0);
			af[0].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			to_room = nullptr;
			break;
		}

		case SPELL_WC_PHYSDAMAGE: {
			af[0].location = APPLY_PERCENT_DAM;
			af[0].modifier = MAX(1, ch->get_skill(SKILL_WARCRY) / 20.0);
			af[0].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			to_room = nullptr;
			break;
		}

		case SPELL_WC_OF_BATTLE: {
			af[0].location = APPLY_AC;
			af[0].modifier = -(10 + MIN(20, 2 * GET_REMORT(ch)));
			af[0].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			to_room = nullptr;
			break;
		}

		case SPELL_WC_OF_DEFENSE: {
			af[0].location = APPLY_SAVING_CRITICAL;
			af[0].modifier -= ch->get_skill(SKILL_WARCRY) / 10.0;
			af[0].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			af[1].location = APPLY_SAVING_REFLEX;
			af[1].modifier -= ch->get_skill(SKILL_WARCRY) / 10;
			af[1].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			af[2].location = APPLY_SAVING_STABILITY;
			af[2].modifier -= ch->get_skill(SKILL_WARCRY) / 10;
			af[2].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			af[3].location = APPLY_SAVING_WILL;
			af[3].modifier -= ch->get_skill(SKILL_WARCRY) / 10;
			af[3].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			//to_vict = nullptr;
			to_room = nullptr;
			break;
		}

		case SPELL_WC_OF_POWER: {
			af[0].location = APPLY_HIT;
			af[0].modifier = MIN(200, (4 * ch->get_con() + ch->get_skill(SKILL_WARCRY)) / 2);
			af[0].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			to_vict = nullptr;
			to_room = nullptr;
			break;
		}

		case SPELL_WC_OF_BLESS: {
			af[0].location = APPLY_SAVING_STABILITY;
			af[0].modifier = -(4 * ch->get_con() + ch->get_skill(SKILL_WARCRY)) / 24;
			af[0].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			af[1].location = APPLY_SAVING_WILL;
			af[1].modifier = af[0].modifier;
			af[1].duration = af[0].duration;
			to_vict = nullptr;
			to_room = nullptr;
			break;
		}

		case SPELL_WC_OF_COURAGE: {
			af[0].location = APPLY_HITROLL;
			af[0].modifier = (44 + ch->get_skill(SKILL_WARCRY)) / 45;
			af[0].duration = pc_duration(victim, 2, ch->get_skill(SKILL_WARCRY), 20, 10, 0) * koef_duration;
			af[1].location = APPLY_DAMROLL;
			af[1].modifier = (29 + ch->get_skill(SKILL_WARCRY)) / 30;
			af[1].duration = af[0].duration;
			to_vict = nullptr;
			to_room = nullptr;
			break;
		}

		case SPELL_ACONITUM_POISON: af[0].location = APPLY_ACONITUM_POISON;
			af[0].duration = 7;
			af[0].modifier = level;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_POISON);
			af[0].battleflag = AF_SAME_TIME;
			to_vict = "Вы почувствовали себя отравленным.";
			to_room = "$n позеленел$g от действия яда.";
			break;

		case SPELL_SCOPOLIA_POISON: af[0].location = APPLY_SCOPOLIA_POISON;
			af[0].duration = 7;
			af[0].modifier = 5;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_POISON) | to_underlying(EAffectFlag::AFF_SCOPOLIA_POISON);
			af[0].battleflag = AF_SAME_TIME;
			to_vict = "Вы почувствовали себя отравленным.";
			to_room = "$n позеленел$g от действия яда.";
			break;

		case SPELL_BELENA_POISON: af[0].location = APPLY_BELENA_POISON;
			af[0].duration = 7;
			af[0].modifier = 5;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_POISON);
			af[0].battleflag = AF_SAME_TIME;
			to_vict = "Вы почувствовали себя отравленным.";
			to_room = "$n позеленел$g от действия яда.";
			break;

		case SPELL_DATURA_POISON: af[0].location = APPLY_DATURA_POISON;
			af[0].duration = 7;
			af[0].modifier = 5;
			af[0].bitvector = to_underlying(EAffectFlag::AFF_POISON);
			af[0].battleflag = AF_SAME_TIME;
			to_vict = "Вы почувствовали себя отравленным.";
			to_room = "$n позеленел$g от действия яда.";
			break;

		case SPELL_LACKY: af[0].duration = pc_duration(victim, 6, 0, 0, 0, 0);
			af[0].bitvector = to_underlying(EAffectFlag::AFF_LACKY);
			//Polud пробный обработчик аффектов
			af[0].handler.reset(new LackyAffectHandler());
			af[0].type = SPELL_LACKY;
			af[0].location = APPLY_HITROLL;
			af[0].modifier = 0;
			to_room = "$n вдохновенно выпятил$g грудь.";
			to_vict = "Вы почувствовали вдохновение.";
			break;

		case SPELL_ARROWS_FIRE:
		case SPELL_ARROWS_WATER:
		case SPELL_ARROWS_EARTH:
		case SPELL_ARROWS_AIR:
		case SPELL_ARROWS_DEATH: {
			//Додати обработчик
			break;
		}
		case SPELL_PALADINE_INSPIRATION:
			/*
         * групповой спелл, развешивающий рандомные аффекты, к сожалению
         * не может быть применен по принципа "сгенерили рандом - и применили"
         * поэтому на каждого члена группы применяется свой аффект, а кастер еще и полечить может
         * */

			if (ch == victim)
				rnd = number(1, 4);
			else
				rnd = number(1, 3);
			af[0].type = SPELL_PALADINE_INSPIRATION;
			af[0].battleflag = AF_BATTLEDEC | AF_PULSEDEC;
			switch (rnd) {
				case 1:af[0].location = APPLY_PERCENT_DAM;
					af[0].duration = pc_duration(victim, 5, 0, 0, 0, 0);
					af[0].modifier = GET_REMORT(ch) / 5 * 2 + GET_REMORT(ch);
					break;
				case 2:af[0].location = APPLY_CAST_SUCCESS;
					af[0].duration = pc_duration(victim, 3, 0, 0, 0, 0);
					af[0].modifier = GET_REMORT(ch) / 5 * 2 + GET_REMORT(ch);
					break;
				case 3:af[0].location = APPLY_MANAREG;
					af[0].duration = pc_duration(victim, 10, 0, 0, 0, 0);
					af[0].modifier = GET_REMORT(ch) / 5 * 2 + GET_REMORT(ch) * 5;
					break;
				case 4:call_magic(ch, ch, nullptr, nullptr, SPELL_GROUP_HEAL, GET_LEVEL(ch));
					break;
				default:break;
			}
	}


	//проверка на обкаст мобов, имеющих от рождения встроенный аффкект
	//чтобы этот аффект не очистился, при спадении спелла
	if (IS_NPC(victim) && success) {
		for (i = 0; i < MAX_SPELL_AFFECTS && success; ++i) {
			if (AFF_FLAGGED(&mob_proto[victim->get_rnum()], static_cast<EAffectFlag>(af[i].bitvector))) {
				if (ch->in_room == IN_ROOM(victim)) {
					send_to_char(NOEFFECT, ch);
				}
				success = FALSE;
			}
		}
	}
	// позитивные аффекты - продлеваем, если они уже на цели
	if (!SpINFO.violent && affected_by_spell(victim, spellnum) && success) {
		update_spell = TRUE;
	}
	// вот такой оригинальный способ запретить рекасты негативных аффектов - через флаг апдейта
	if ((ch != victim) && affected_by_spell(victim, spellnum) && success && (!update_spell)) {
		if (ch->in_room == IN_ROOM(victim))
			send_to_char(NOEFFECT, ch);
		success = FALSE;
	}

	for (i = 0; success && i < MAX_SPELL_AFFECTS; i++) {
		af[i].type = spellnum;
		if (af[i].bitvector || af[i].location != APPLY_NONE) {
			af[i].duration = complex_spell_modifier(ch, spellnum, GAPPLY_SPELL_EFFECT, af[i].duration);
			if (update_spell)
				affect_join_fspell(victim, af[i]);
			else
				affect_join(victim, af[i], accum_duration, FALSE, accum_affect, FALSE);
		}
		// тут мы ездим по циклу 16 раз, хотя аффектов 1-3...
//		ch->send_to_TC(true, true, true, "Applied affect type %i\r\n", af[i].type);
	}

	if (success) {
		// вот некрасиво же тут это делать...
		if (spellnum == SPELL_POISON)
			victim->Poisoner = GET_ID(ch);
		if (to_vict != nullptr)
			act(to_vict, FALSE, victim, 0, ch, TO_CHAR);
		if (to_room != nullptr)
			act(to_room, TRUE, victim, 0, ch, TO_ROOM | TO_ARENA_LISTEN);
		return 1;
	}
	return 0;
}


/*
 *  Every spell which summons/gates/conjours a mob comes through here.
 *
 *  None of these spells are currently implemented in Circle 3.0; these
 *  were taken as examples from the JediMUD code.  Summons can be used
 *  for spells like clone, ariel servant, etc.
 *
 * 10/15/97 (gg) - Implemented Animate Dead and Clone.
 */

// * These use act(), don't put the \r\n.
const char *mag_summon_msgs[] =
	{
		"\r\n",
		"$n сделал$g несколько изящних пассов - вы почувствовали странное дуновение!",
		"$n поднял$g труп!",
		"$N появил$U из клубов голубого дыма!",
		"$N появил$U из клубов зеленого дыма!",
		"$N появил$U из клубов красного дыма!",
		"$n сделал$g несколько изящных пассов - вас обдало порывом холодного ветра.",
		"$n сделал$g несколько изящных пассов, от чего ваши волосы встали дыбом.",
		"$n сделал$g несколько изящных пассов, обдав вас нестерпимым жаром.",
		"$n сделал$g несколько изящных пассов, вызвав у вас приступ тошноты.",
		"$n раздвоил$u!",
		"$n оживил$g труп!",
		"$n призвал$g защитника!",
		"Огненный хранитель появился из вихря пламени!"
	};

// * Keep the \r\n because these use send_to_char.
const char *mag_summon_fail_msgs[] =
	{
		"\r\n",
		"Нет такого существа в мире.\r\n",
		"Жаль, сорвалось...\r\n",
		"Ничего.\r\n",
		"Черт! Ничего не вышло.\r\n",
		"Вы не смогли сделать этого!\r\n",
		"Ваша магия провалилась.\r\n",
		"У вас нет подходящего трупа!\r\n"
	};

int mag_summons(int level, CHAR_DATA *ch, OBJ_DATA *obj, int spellnum, int savetype) {
	CHAR_DATA *tmp_mob, *mob = nullptr;
	OBJ_DATA *tobj, *next_obj;
	struct follow_type *k;
	int pfail = 0, msg = 0, fmsg = 0, handle_corpse = FALSE, keeper = FALSE, cha_num = 0, modifier = 0;
	mob_vnum mob_num;

	if (ch == nullptr) {
		return 0;
	}

	switch (spellnum) {
		case SPELL_CLONE: msg = 10;
			fmsg = number(3, 5);    // Random fail message.
			mob_num = MOB_DOUBLE;
			pfail =
				50 - GET_CAST_SUCCESS(ch) - GET_REMORT(ch) * 5;    // 50% failure, should be based on something later.
			keeper = TRUE;
			break;

		case SPELL_SUMMON_KEEPER: msg = 12;
			fmsg = number(2, 6);
			mob_num = MOB_KEEPER;
			if (ch->get_fighting())
				pfail = 50 - GET_CAST_SUCCESS(ch) - GET_REMORT(ch);
			else
				pfail = 0;
			keeper = TRUE;
			break;

		case SPELL_SUMMON_FIREKEEPER: msg = 13;
			fmsg = number(2, 6);
			mob_num = MOB_FIREKEEPER;
			if (ch->get_fighting())
				pfail = 50 - GET_CAST_SUCCESS(ch) - GET_REMORT(ch);
			else
				pfail = 0;
			keeper = TRUE;
			break;

		case SPELL_ANIMATE_DEAD:
			if (obj == nullptr || !IS_CORPSE(obj)) {
				act(mag_summon_fail_msgs[7], FALSE, ch, 0, 0, TO_CHAR);
				return 0;
			}
			mob_num = GET_OBJ_VAL(obj, 2);
			if (mob_num <= 0)
				mob_num = MOB_SKELETON;
			else {
				const int real_mob_num = real_mobile(mob_num);
				tmp_mob = (mob_proto + real_mob_num);
				tmp_mob->set_normal_morph();
				pfail = 10 + tmp_mob->get_con() * 2
					- number(1, GET_LEVEL(ch)) - GET_CAST_SUCCESS(ch) - GET_REMORT(ch) * 5;

				int corpse_mob_level = GET_LEVEL(mob_proto + real_mob_num);
				if (corpse_mob_level <= 5) {
					mob_num = MOB_SKELETON;
				} else if (corpse_mob_level <= 10) {
					mob_num = MOB_ZOMBIE;
				} else if (corpse_mob_level <= 15) {
					mob_num = MOB_BONEDOG;
				} else if (corpse_mob_level <= 20) {
					mob_num = MOB_BONEDRAGON;
				} else if (corpse_mob_level <= 25) {
					mob_num = MOB_BONESPIRIT;
				} else if (corpse_mob_level <= 34) {
					mob_num = MOB_NECROTANK;
				} else {
					int rnd = number(1, 100);
					mob_num = MOB_NECRODAMAGER;
					if (rnd > 50) {
						mob_num = MOB_NECROBREATHER;
					}
				}

				// MOB_NECROCASTER disabled, cant cast

				if (GET_LEVEL(ch) + GET_REMORT(ch) + 4 < 15 && mob_num > MOB_ZOMBIE) {
					mob_num = MOB_ZOMBIE;
				} else if (GET_LEVEL(ch) + GET_REMORT(ch) + 4 < 25 && mob_num > MOB_BONEDOG) {
					mob_num = MOB_BONEDOG;
				} else if (GET_LEVEL(ch) + GET_REMORT(ch) + 4 < 32 && mob_num > MOB_BONEDRAGON) {
					mob_num = MOB_BONEDRAGON;
				}
			}

			handle_corpse = TRUE;
			msg = number(1, 9);
			fmsg = number(2, 6);
			break;

		case SPELL_RESSURECTION:
			if (obj == nullptr || !IS_CORPSE(obj)) {
				act(mag_summon_fail_msgs[7], FALSE, ch, 0, 0, TO_CHAR);
				return 0;
			}
			if ((mob_num = GET_OBJ_VAL(obj, 2)) <= 0) {
				send_to_char("Вы не можете поднять труп этого монстра!\r\n", ch);
				return 0;
			}

			handle_corpse = TRUE;
			msg = 11;
			fmsg = number(2, 6);

			tmp_mob = mob_proto + real_mobile(mob_num);
			tmp_mob->set_normal_morph();

			pfail = 10 + tmp_mob->get_con() * 2
				- number(1, GET_LEVEL(ch)) - GET_CAST_SUCCESS(ch) - GET_REMORT(ch) * 5;
			break;

		default: return 0;
	}

	if (AFF_FLAGGED(ch, EAffectFlag::AFF_CHARM)) {
		send_to_char("Вы слишком зависимы, чтобы искать себе последователей!\r\n", ch);
		return 0;
	}
	// при перке помощь тьмы гораздо меньше шанс фейла
	if (!IS_IMMORTAL(ch) && number(0, 101) < pfail && savetype) {
		if (can_use_feat(ch, HELPDARK_FEAT)) {
			if (number(0, 3) == 0) {
				send_to_char(mag_summon_fail_msgs[fmsg], ch);
				if (handle_corpse)
					extract_obj(obj);
				return 0;
			}
		} else {
			send_to_char(mag_summon_fail_msgs[fmsg], ch);
			if (handle_corpse)
				extract_obj(obj);
			return 0;
		}
	}

	if (!(mob = read_mobile(-mob_num, VIRTUAL))) {
		send_to_char("Вы точно не помните, как создать данного монстра.\r\n", ch);
		return 0;
	}
	// очищаем умения у оживляемого моба
	if (spellnum == SPELL_RESSURECTION) {
		clear_char_skills(mob);
		// Меняем именование.
		sprintf(buf2, "умертвие %s %s", GET_PAD(mob, 1), GET_NAME(mob));
		mob->set_pc_name(buf2);
		sprintf(buf2, "умертвие %s", GET_PAD(mob, 1));
		mob->set_npc_name(buf2);
		mob->player_data.long_descr = "";
		sprintf(buf2, "умертвие %s", GET_PAD(mob, 1));
		mob->player_data.PNames[0] = std::string(buf2);
		sprintf(buf2, "умертвию %s", GET_PAD(mob, 1));
		mob->player_data.PNames[2] = std::string(buf2);
		sprintf(buf2, "умертвие %s", GET_PAD(mob, 1));
		mob->player_data.PNames[3] = std::string(buf2);
		sprintf(buf2, "умертвием %s", GET_PAD(mob, 1));
		mob->player_data.PNames[4] = std::string(buf2);
		sprintf(buf2, "умертвии %s", GET_PAD(mob, 1));
		mob->player_data.PNames[5] = std::string(buf2);
		sprintf(buf2, "умертвия %s", GET_PAD(mob, 1));
		mob->player_data.PNames[1] = std::string(buf2);
		mob->set_sex(ESex::SEX_NEUTRAL);
		MOB_FLAGS(mob).set(MOB_RESURRECTED);    // added by Pereplut
		// если есть фит ярость тьмы, то прибавляем к хп и дамролам
		if (can_use_feat(ch, FURYDARK_FEAT)) {
			GET_DR(mob) = GET_DR(mob) + GET_DR(mob) * 0.20;
			GET_MAX_HIT(mob) = GET_MAX_HIT(mob) + GET_MAX_HIT(mob) * 0.20;
			GET_HIT(mob) = GET_MAX_HIT(mob);
			GET_HR(mob) = GET_HR(mob) + GET_HR(mob) * 0.20;
		}
	}
	
	if (!IS_IMMORTAL(ch) && (AFF_FLAGGED(mob, EAffectFlag::AFF_SANCTUARY) || MOB_FLAGGED(mob, MOB_PROTECT))) {
		send_to_char("Оживляемый был освящен Богами и противится этому!\r\n", ch);
		extract_char(mob, FALSE);
		return 0;
	}
	if (!IS_IMMORTAL(ch)
		&& (GET_MOB_SPEC(mob) || MOB_FLAGGED(mob, MOB_NORESURRECTION) || MOB_FLAGGED(mob, MOB_AREA_ATTACK))) {
		send_to_char("Вы не можете обрести власть над этим созданием!\r\n", ch);
		extract_char(mob, FALSE);
		return 0;
	}
	if (!IS_IMMORTAL(ch) && AFF_FLAGGED(mob, EAffectFlag::AFF_SHIELD)) {
		send_to_char("Боги защищают это существо даже после смерти.\r\n", ch);
		extract_char(mob, FALSE);
		return 0;
	}
	if (MOB_FLAGGED(mob, MOB_MOUNTING)) {
		MOB_FLAGS(mob).unset(MOB_MOUNTING);
	}
	if (IS_HORSE(mob)) {
		send_to_char("Это был боевой скакун, а не хухры-мухры.\r\n", ch);
		extract_char(mob, FALSE);
		return 0;
	}

	if (spellnum == SPELL_ANIMATE_DEAD && mob_num >= MOB_NECRODAMAGER && mob_num <= LAST_NECRO_MOB) {
		// add 10% mob health by remort
		mob->set_max_hit(mob->get_max_hit() * (1.0 + ch->get_remort() / 10.0));
		mob->set_hit(mob->get_max_hit());
		int player_charms_value = get_player_charms(ch, spellnum);
		int mob_cahrms_value = get_reformed_charmice_hp(ch, mob, spellnum);
		int damnodice = 1;
		mob->mob_specials.damnodice = damnodice;
		// look for count dice to maximize damage on player_charms_value. max 255.
		while (player_charms_value > mob_cahrms_value && damnodice <= 255) {
			damnodice++;
			mob->mob_specials.damnodice = damnodice;
			mob_cahrms_value = get_reformed_charmice_hp(ch, mob, spellnum);
		}
		damnodice--;

		mob->mob_specials.damnodice = damnodice; // get prew damnodice for match with player_charms_value
		if (damnodice == 255) {
			// if damnodice == 255 mob damage not maximized. damsize too small
			send_to_room("Темные искры пробежали по земле... И исчезли...", ch->in_room, 0);
		} else {
			// mob damage maximazed.
			send_to_room("Темные искры пробежали по земле. Кажется сама СМЕРТЬ наполняет это тело силой!",
						 ch->in_room,
						 0);
		}
	}

	if (!check_charmee(ch, mob, spellnum)) {
		extract_char(mob, FALSE);
		if (handle_corpse)
			extract_obj(obj);
		return 0;
	}

	mob->set_exp(0);
	IS_CARRYING_W(mob) = 0;
	IS_CARRYING_N(mob) = 0;
	mob->set_gold(0);
	GET_GOLD_NoDs(mob) = 0;
	GET_GOLD_SiDs(mob) = 0;
	const auto days_from_full_moon =
		(weather_info.moon_day < 14) ? (14 - weather_info.moon_day) : (weather_info.moon_day - 14);
	const auto duration = pc_duration(mob, GET_REAL_WIS(ch) + number(0, days_from_full_moon), 0, 0, 0, 0);
	AFFECT_DATA<EApplyLocation> af;
	af.type = SPELL_CHARM;
	af.duration = duration;
	af.modifier = 0;
	af.location = EApplyLocation::APPLY_NONE;
	af.bitvector = to_underlying(EAffectFlag::AFF_CHARM);
	af.battleflag = 0;
	affect_to_char(mob, af);
	if (keeper) {
		af.bitvector = to_underlying(EAffectFlag::AFF_HELPER);
		affect_to_char(mob, af);
		mob->set_skill(SKILL_RESCUE, 100);
	}

	MOB_FLAGS(mob).set(MOB_CORPSE);
	if (spellnum == SPELL_CLONE) {
		sprintf(buf2, "двойник %s %s", GET_PAD(ch, 1), GET_NAME(ch));
		mob->set_pc_name(buf2);
		sprintf(buf2, "двойник %s", GET_PAD(ch, 1));
		mob->set_npc_name(buf2);
		mob->player_data.long_descr = "";
		sprintf(buf2, "двойник %s", GET_PAD(ch, 1));
		mob->player_data.PNames[0] = std::string(buf2);
		sprintf(buf2, "двойника %s", GET_PAD(ch, 1));
		mob->player_data.PNames[1] = std::string(buf2);
		sprintf(buf2, "двойнику %s", GET_PAD(ch, 1));
		mob->player_data.PNames[2] = std::string(buf2);
		sprintf(buf2, "двойника %s", GET_PAD(ch, 1));
		mob->player_data.PNames[3] = std::string(buf2);
		sprintf(buf2, "двойником %s", GET_PAD(ch, 1));
		mob->player_data.PNames[4] = std::string(buf2);
		sprintf(buf2, "двойнике %s", GET_PAD(ch, 1));
		mob->player_data.PNames[5] = std::string(buf2);

		mob->set_str(ch->get_str());
		mob->set_dex(ch->get_dex());
		mob->set_con(ch->get_con());
		mob->set_wis(ch->get_wis());
		mob->set_int(ch->get_int());
		mob->set_cha(ch->get_cha());

		mob->set_level(ch->get_level());
		GET_HR(mob) = -20;
		GET_AC(mob) = GET_AC(ch);
		GET_DR(mob) = GET_DR(ch);

		GET_MAX_HIT(mob) = GET_MAX_HIT(ch);
		GET_HIT(mob) = GET_MAX_HIT(ch);
		mob->mob_specials.damnodice = 0;
		mob->mob_specials.damsizedice = 0;
		mob->set_gold(0);
		GET_GOLD_NoDs(mob) = 0;
		GET_GOLD_SiDs(mob) = 0;
		mob->set_exp(0);

		GET_POS(mob) = POS_STANDING;
		GET_DEFAULT_POS(mob) = POS_STANDING;
		mob->set_sex(ESex::SEX_MALE);

		mob->set_class(ch->get_class());
		GET_WEIGHT(mob) = GET_WEIGHT(ch);
		GET_HEIGHT(mob) = GET_HEIGHT(ch);
		GET_SIZE(mob) = GET_SIZE(ch);
		MOB_FLAGS(mob).set(MOB_CLONE);
		MOB_FLAGS(mob).unset(MOB_MOUNTING);
	}
	act(mag_summon_msgs[msg], FALSE, ch, 0, mob, TO_ROOM | TO_ARENA_LISTEN);

	char_to_room(mob, ch->in_room);
	ch->add_follower(mob); 
	
	if (spellnum == SPELL_CLONE) {
		// клоны теперь кастятся все вместе // ужасно некрасиво сделано
		for (k = ch->followers; k; k = k->next) {
			if (AFF_FLAGGED(k->follower, EAffectFlag::AFF_CHARM)
				&& k->follower->get_master() == ch) {
				cha_num++;
			}
		}
		cha_num = MAX(1, (GET_LEVEL(ch) + 4) / 5 - 2) - cha_num;
		if (cha_num < 1)
			return 0;
		mag_summons(level, ch, obj, spellnum, 0);
	}
	if (spellnum == SPELL_ANIMATE_DEAD) {
		MOB_FLAGS(mob).set(MOB_RESURRECTED);
		if (mob_num == MOB_SKELETON && can_use_feat(ch, LOYALASSIST_FEAT))
			mob->set_skill(SKILL_RESCUE, 100);

		if (mob_num == MOB_BONESPIRIT && can_use_feat(ch, HAUNTINGSPIRIT_FEAT))
			mob->set_skill(SKILL_RESCUE, 120);

		// даем всем поднятым, ну наверное не будет чернок 75+ мудры вызывать зомби в щите.
		float eff_wis = get_effective_wis(ch, spellnum);
		if (eff_wis >= 65) {
			// пока не даем, если надо включите
			//af.bitvector = to_underlying(EAffectFlag::AFF_MAGICGLASS);
			//affect_to_char(mob, af);
		}
		if (eff_wis >= 75) {
			AFFECT_DATA<EApplyLocation> af;
			af.type = SPELL_NO_SPELL;
			af.duration = duration * (1 + GET_REMORT(ch));
			af.modifier = 0;
			af.location = EApplyLocation::APPLY_NONE;
			af.bitvector = to_underlying(EAffectFlag::AFF_ICESHIELD);
			af.battleflag = 0;
			affect_to_char(mob, af);
		}

	}

	if (spellnum == SPELL_SUMMON_KEEPER) {
		// Svent TODO: не забыть перенести это в ability
		mob->set_level(ch->get_level());
		int rating = (ch->get_skill(SKILL_LIGHT_MAGIC) + GET_REAL_CHA(ch)) / 2;
		GET_MAX_HIT(mob) = GET_HIT(mob) = 50 + dice(10, 10) + rating * 6;
		mob->set_skill(SKILL_PUNCH, 10 + rating * 1.5);
		mob->set_skill(SKILL_RESCUE, 50 + rating);
		mob->set_str(3 + rating / 5);
		mob->set_dex(10 + rating / 5);
		mob->set_con(10 + rating / 5);
		GET_HR(mob) = rating / 2 - 4;
		GET_AC(mob) = 100 - rating * 2.65;
	}

	if (spellnum == SPELL_SUMMON_FIREKEEPER) {
		AFFECT_DATA<EApplyLocation> af;
		af.type = SPELL_CHARM;
		af.duration = duration;
		af.modifier = 0;
		af.location = EApplyLocation::APPLY_NONE;
		af.battleflag = 0;
		if (get_effective_cha(ch) >= 30) {
			af.bitvector = to_underlying(EAffectFlag::AFF_FIRESHIELD);
			affect_to_char(mob, af);
		} else {
			af.bitvector = to_underlying(EAffectFlag::AFF_FIREAURA);
			affect_to_char(mob, af);
		}

		modifier = VPOSI((int) get_effective_cha(ch) - 20, 0, 30);

		GET_DR(mob) = 10 + modifier * 3 / 2;
		GET_NDD(mob) = 1;
		GET_SDD(mob) = modifier / 5 + 1;
		mob->mob_specials.ExtraAttack = 0;

		GET_MAX_HIT(mob) = GET_HIT(mob) = 300 + number(modifier * 12, modifier * 16);
		mob->set_skill(SKILL_AWAKE, 50 + modifier * 2);
		PRF_FLAGS(mob).set(PRF_AWAKE);
	}
	MOB_FLAGS(mob).set(MOB_NOTRAIN);
	// А надо ли это вообще делать???
	if (handle_corpse) {
		for (tobj = obj->get_contains(); tobj;) {
			next_obj = tobj->get_next_content();
			obj_from_obj(tobj);
			obj_to_room(tobj, ch->in_room);
			if (!obj_decay(tobj) && tobj->get_in_room() != NOWHERE) {
				act("На земле остал$U лежать $o.", FALSE, ch, tobj, 0, TO_ROOM | TO_ARENA_LISTEN);
			}
			tobj = next_obj;
		}
		extract_obj(obj);
	}
	return 1;
}

int mag_points(int level, CHAR_DATA *ch, CHAR_DATA *victim, int spellnum, int) {
	int hit = 0; //если выставить больше нуля, то лечит
	int move = 0; //если выставить больше нуля, то реген хп
	bool extraHealing = false; //если true, то лечит сверх макс.хп

	if (victim == nullptr) {
		log("MAG_POINTS: Ошибка! Не указана цель, спелл spellnum: %d!\r\n", spellnum);
		return 0;
	}

	switch (spellnum) {
		case SPELL_CURE_LIGHT: hit = dice(6, 3) + (level + 2) / 3;
			send_to_char("Вы почувствовали себя немножко лучше.\r\n", victim);
			break;
		case SPELL_CURE_SERIOUS: hit = dice(25, 2) + (level + 2) / 3;
			send_to_char("Вы почувствовали себя намного лучше.\r\n", victim);
			break;
		case SPELL_CURE_CRITIC: hit = dice(45, 2) + level;
			send_to_char("Вы почувствовали себя значительно лучше.\r\n", victim);
			break;
		case SPELL_HEAL:
		case SPELL_GROUP_HEAL: hit = GET_REAL_MAX_HIT(victim) - GET_HIT(victim);
			send_to_char("Вы почувствовали себя здоровым.\r\n", victim);
			break;
		case SPELL_PATRONAGE: hit = (GET_LEVEL(victim) + GET_REMORT(victim)) * 2;
			break;
		case SPELL_WC_OF_POWER: hit = MIN(200, (4 * ch->get_con() + ch->get_skill(SKILL_WARCRY)) / 2);
			send_to_char("По вашему телу начала струиться живительная сила.\r\n", victim);
			break;
		case SPELL_EXTRA_HITS: extraHealing = true;
			hit = dice(10, level / 3) + level;
			send_to_char("По вашему телу начала струиться живительная сила.\r\n", victim);
			break;
		case SPELL_EVILESS:
			//лечим только умертвия-чармисы
			if (!IS_NPC(victim) || victim->get_master() != ch || !MOB_FLAGGED(victim, MOB_CORPSE))
				return 1;
			//при рекасте - не лечим
			if (AFF_FLAGGED(ch, EAffectFlag::AFF_EVILESS)) {
				hit = GET_REAL_MAX_HIT(victim) - GET_HIT(victim);
				affect_from_char(ch, SPELL_EVILESS); //сбрасываем аффект с хозяина
			}
			break;
		case SPELL_REFRESH:
		case SPELL_GROUP_REFRESH: move = GET_REAL_MAX_MOVE(victim) - GET_MOVE(victim);
			send_to_char("Вы почувствовали себя полным сил.\r\n", victim);
			break;
		case SPELL_FULL:
		case SPELL_COMMON_MEAL: {
			if (GET_COND(victim, THIRST) > 0)
				GET_COND(victim, THIRST) = 0;
			if (GET_COND(victim, FULL) > 0)
				GET_COND(victim, FULL) = 0;
			send_to_char("Вы полностью насытились.\r\n", victim);
		}
			break;
		default: log("MAG_POINTS: Ошибка! Передан не определенный лечащий спелл spellnum: %d!\r\n", spellnum);
			return 0;
			break;
	}

	hit = complex_spell_modifier(ch, spellnum, GAPPLY_SPELL_EFFECT, hit);

	if (hit && victim->get_fighting() && ch != victim) {
		if (!pk_agro_action(ch, victim->get_fighting()))
			return 0;
	}
	// лечение
	if (GET_HIT(victim) < MAX_HITS && hit != 0) {
		// просто лечим
		if (!extraHealing && GET_HIT(victim) < GET_REAL_MAX_HIT(victim))
			GET_HIT(victim) = MIN(GET_HIT(victim) + hit, GET_REAL_MAX_HIT(victim));
		// добавляем фикс.хиты сверх максимума
		if (extraHealing) {
			// если макс.хп отрицательные - доводим до 1
			if (GET_REAL_MAX_HIT(victim) <= 0)
				GET_HIT(victim) = MAX(GET_HIT(victim), MIN(GET_HIT(victim) + hit, 1));
			else
				// лимит в треть от макс.хп сверху
				GET_HIT(victim) = MAX(GET_HIT(victim),
									  MIN(GET_HIT(victim) + hit,
										  GET_REAL_MAX_HIT(victim) + GET_REAL_MAX_HIT(victim) * 33 / 100));
		}
	}
	if (move != 0 && GET_MOVE(victim) < GET_REAL_MAX_MOVE(victim))
		GET_MOVE(victim) = MIN(GET_MOVE(victim) + move, GET_REAL_MAX_MOVE(victim));
	update_pos(victim);

	return 1;
}

inline bool NODISPELL(const AFFECT_DATA<EApplyLocation>::shared_ptr &affect) {
	return !affect
		|| !spell_info[affect->type].name
		|| *spell_info[affect->type].name == '!'
		|| affect->bitvector == to_underlying(EAffectFlag::AFF_CHARM)
		|| affect->type == SPELL_CHARM
		|| affect->type == SPELL_QUEST
		|| affect->type == SPELL_PATRONAGE
		|| affect->type == SPELL_SOLOBONUS
		|| affect->type == SPELL_EVILESS;
}

int mag_unaffects(int/* level*/, CHAR_DATA *ch, CHAR_DATA *victim, int spellnum, int/* type*/) {
	int spell = 0, remove = 0;
	const char *to_vict = nullptr, *to_room = nullptr;

	if (victim == nullptr) {
		return 0;
	}

	switch (spellnum) {
		case SPELL_CURE_BLIND: spell = SPELL_BLINDNESS;
			to_vict = "К вам вернулась способность видеть.";
			to_room = "$n прозрел$g.";
			break;
		case SPELL_REMOVE_POISON: spell = SPELL_POISON;
			to_vict = "Тепло заполнило ваше тело.";
			to_room = "$n выглядит лучше.";
			break;
		case SPELL_CURE_PLAQUE: spell = SPELL_PLAQUE;
			to_vict = "Лихорадка прекратилась.";
			break;
		case SPELL_REMOVE_CURSE: spell = SPELL_CURSE;
			to_vict = "Боги вернули вам свое покровительство.";
			break;
		case SPELL_REMOVE_HOLD: spell = SPELL_HOLD;
			to_vict = "К вам вернулась способность двигаться.";
			break;
		case SPELL_REMOVE_SILENCE: spell = SPELL_SILENCE;
			to_vict = "К вам вернулась способность разговаривать.";
			break;
		case SPELL_REMOVE_DEAFNESS: spell = SPELL_DEAFNESS;
			to_vict = "К вам вернулась способность слышать.";
			break;
		case SPELL_DISPELL_MAGIC:
			if (!IS_NPC(ch)
				&& !same_group(ch, victim)) {
				send_to_char("Только на себя или одногруппника!\r\n", ch);

				return 0;
			}

			{
				const auto affects_count = victim->affected.size();
				if (0 == affects_count) {
					send_to_char(NOEFFECT, ch);
					return 0;
				}

				spell = 1;
				const auto rspell = number(1, static_cast<int>(affects_count));
				auto affect_i = victim->affected.begin();
				while (spell < rspell) {
					++affect_i;
					++spell;
				}

				if (NODISPELL(*affect_i)) {
					send_to_char(NOEFFECT, ch);

					return 0;
				}

				spell = (*affect_i)->type;
			}

			remove = TRUE;
			break;

		default: log("SYSERR: unknown spellnum %d passed to mag_unaffects.", spellnum);
			return 0;
	}

	if (spellnum == SPELL_REMOVE_POISON && !affected_by_spell(victim, spell)) {
		if (affected_by_spell(victim, SPELL_ACONITUM_POISON))
			spell = SPELL_ACONITUM_POISON;
		else if (affected_by_spell(victim, SPELL_SCOPOLIA_POISON))
			spell = SPELL_SCOPOLIA_POISON;
		else if (affected_by_spell(victim, SPELL_BELENA_POISON))
			spell = SPELL_BELENA_POISON;
		else if (affected_by_spell(victim, SPELL_DATURA_POISON))
			spell = SPELL_DATURA_POISON;
	}

	if (!affected_by_spell(victim, spell)) {
		if (spellnum != SPELL_HEAL)    // 'cure blindness' message.
			send_to_char(NOEFFECT, ch);
		return 0;
	}
	spellnum = spell;
	if (ch != victim && !remove) {
		if (IS_SET(SpINFO.routines, NPC_AFFECT_NPC)) {
			if (!pk_agro_action(ch, victim))
				return 0;
		} else if (IS_SET(SpINFO.routines, NPC_AFFECT_PC) && victim->get_fighting()) {
			if (!pk_agro_action(ch, victim->get_fighting()))
				return 0;
		}
	}
//Polud затычка для закла !удалить яд!. По хорошему нужно его переделать с параметром - тип удаляемого яда
	if (spell == SPELL_POISON) {
		affect_from_char(victim, SPELL_ACONITUM_POISON);
		affect_from_char(victim, SPELL_DATURA_POISON);
		affect_from_char(victim, SPELL_SCOPOLIA_POISON);
		affect_from_char(victim, SPELL_BELENA_POISON);
	}
	affect_from_char(victim, spell);
	if (to_vict != nullptr)
		act(to_vict, FALSE, victim, 0, ch, TO_CHAR);
	if (to_room != nullptr)
		act(to_room, TRUE, victim, 0, ch, TO_ROOM | TO_ARENA_LISTEN);

	return 1;
}

int mag_alter_objs(int/* level*/, CHAR_DATA *ch, OBJ_DATA *obj, int spellnum, int/* savetype*/) {
	const char *to_char = nullptr;

	if (obj == nullptr) {
		return 0;
	}

	if (obj->get_extra_flag(EExtraFlag::ITEM_NOALTER)) {
		act("$o устойчив$A к вашей магии.", TRUE, ch, obj, 0, TO_CHAR);
		return 0;
	}

	switch (spellnum) {
		case SPELL_BLESS:
			if (!obj->get_extra_flag(EExtraFlag::ITEM_BLESS)
				&& (GET_OBJ_WEIGHT(obj) <= 5 * GET_LEVEL(ch))) {
				obj->set_extra_flag(EExtraFlag::ITEM_BLESS);
				if (obj->get_extra_flag(EExtraFlag::ITEM_NODROP)) {
					obj->unset_extraflag(EExtraFlag::ITEM_NODROP);
					if (GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_WEAPON) {
						obj->inc_val(2);
					}
				}
				obj->add_maximum(MAX(GET_OBJ_MAX(obj) >> 2, 1));
				obj->set_current_durability(GET_OBJ_MAX(obj));
				to_char = "$o вспыхнул$G голубым светом и тут же погас$Q.";
				obj->add_timed_spell(SPELL_BLESS, -1);
			}
			break;

		case SPELL_CURSE:
			if (!obj->get_extra_flag(EExtraFlag::ITEM_NODROP)) {
				obj->set_extra_flag(EExtraFlag::ITEM_NODROP);
				if (GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_WEAPON) {
					if (GET_OBJ_VAL(obj, 2) > 0) {
						obj->dec_val(2);
					}
				} else if (ObjSystem::is_armor_type(obj)) {
					if (GET_OBJ_VAL(obj, 0) > 0) {
						obj->dec_val(0);
					}
					if (GET_OBJ_VAL(obj, 1) > 0) {
						obj->dec_val(1);
					}
				}
				to_char = "$o вспыхнул$G красным светом и тут же погас$Q.";
			}
			break;

		case SPELL_INVISIBLE:
			if (!obj->get_extra_flag(EExtraFlag::ITEM_NOINVIS)
				&& !obj->get_extra_flag(EExtraFlag::ITEM_INVISIBLE)) {
				obj->set_extra_flag(EExtraFlag::ITEM_INVISIBLE);
				to_char = "$o растворил$U в пустоте.";
			}
			break;

		case SPELL_POISON:
			if (!GET_OBJ_VAL(obj, 3)
				&& (GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_DRINKCON
					|| GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_FOUNTAIN
					|| GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_FOOD)) {
				obj->set_val(3, 1);
				to_char = "$o отравлен$G.";
			}
			break;

		case SPELL_REMOVE_CURSE:
			if (obj->get_extra_flag(EExtraFlag::ITEM_NODROP)) {
				obj->unset_extraflag(EExtraFlag::ITEM_NODROP);
				if (GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_WEAPON) {
					obj->inc_val(2);
				}
				to_char = "$o вспыхнул$G розовым светом и тут же погас$Q.";
			}
			break;

		case SPELL_ENCHANT_WEAPON: {
			if (ch == nullptr || obj == nullptr) {
				return 0;
			}

			// Either already enchanted or not a weapon.
			if (GET_OBJ_TYPE(obj) != OBJ_DATA::ITEM_WEAPON) {
				to_char = "Еще раз ударьтесь головой об стену, авось зрение вернется...";
				break;
			} else if (OBJ_FLAGGED(obj, EExtraFlag::ITEM_MAGIC)) {
				to_char = "Вам не под силу зачаровать магическую вещь.";
				break;
			}

			if (OBJ_FLAGGED(obj, EExtraFlag::ITEM_SETSTUFF)) {
				send_to_char(ch, "Сетовый предмет не может быть заколдован.\r\n");
				break;
			}

			auto reagobj = GET_EQ(ch, WEAR_HOLD);
			if (reagobj
				&& (get_obj_in_list_vnum(GlobalDrop::MAGIC1_ENCHANT_VNUM, reagobj)
					|| get_obj_in_list_vnum(GlobalDrop::MAGIC2_ENCHANT_VNUM, reagobj)
					|| get_obj_in_list_vnum(GlobalDrop::MAGIC3_ENCHANT_VNUM, reagobj))) {
				// у нас имеется доп символ для зачарования
				obj->set_enchant(ch->get_skill(SKILL_LIGHT_MAGIC), reagobj);
				material_component_processing(ch, reagobj->get_rnum(), spellnum); //может неправильный вызов
			} else {
				obj->set_enchant(ch->get_skill(SKILL_LIGHT_MAGIC));
			}
			if (GET_RELIGION(ch) == RELIGION_MONO) {
				to_char = "$o вспыхнул$G на миг голубым светом и тут же потух$Q.";
			} else if (GET_RELIGION(ch) == RELIGION_POLY) {
				to_char = "$o вспыхнул$G на миг красным светом и тут же потух$Q.";
			} else {
				to_char = "$o вспыхнул$G на миг желтым светом и тут же потух$Q.";
			}
			break;
		}
		case SPELL_REMOVE_POISON:
			if (GET_OBJ_RNUM(obj) < 0) {
				to_char = "Ничего не случилось.";
				char buf[100];
				sprintf(buf,
						"неизвестный прототип объекта : %s (VNUM=%d)",
						GET_OBJ_PNAME(obj, 0).c_str(),
						obj->get_vnum());
				mudlog(buf, BRF, LVL_BUILDER, SYSLOG, 1);
				break;
			}
			if (obj_proto[GET_OBJ_RNUM(obj)]->get_val(3) > 1 && GET_OBJ_VAL(obj, 3) == 1) {
				to_char = "Содержимое $o1 протухло и не поддается магии.";
				break;
			}
			if ((GET_OBJ_VAL(obj, 3) == 1)
				&& ((GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_DRINKCON)
					|| GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_FOUNTAIN
					|| GET_OBJ_TYPE(obj) == OBJ_DATA::ITEM_FOOD)) {
				obj->set_val(3, 0);
				to_char = "$o стал$G вполне пригодным к применению.";
			}
			break;

		case SPELL_FLY: obj->add_timed_spell(SPELL_FLY, -1);
			obj->set_extra_flag(EExtraFlag::ITEM_FLYING);
			to_char = "$o вспыхнул$G зеленоватым светом и тут же погас$Q.";
			break;

		case SPELL_ACID: alterate_object(obj, number(GET_LEVEL(ch) * 2, GET_LEVEL(ch) * 4), 100);
			break;

		case SPELL_REPAIR: obj->set_current_durability(GET_OBJ_MAX(obj));
			to_char = "Вы полностью восстановили $o3.";
			break;

		case SPELL_TIMER_REPAIR:
			if (GET_OBJ_RNUM(obj) != NOTHING) {
				obj->set_current_durability(GET_OBJ_MAX(obj));
				obj->set_timer(obj_proto.at(GET_OBJ_RNUM(obj))->get_timer());
				to_char = "Вы полностью восстановили $o3.";
				log("%s used magic repair", GET_NAME(ch));
			} else {
				return 0;
			}
			break;

		case SPELLS_RESTORATION: {
			if (OBJ_FLAGGED(obj, EExtraFlag::ITEM_MAGIC)
				&& (GET_OBJ_RNUM(obj) != NOTHING)) {
				if (obj_proto.at(GET_OBJ_RNUM(obj))->get_extra_flag(EExtraFlag::ITEM_MAGIC)) {
					return 0;
				}
				obj->unset_enchant();
			} else {
				return 0;
			}
			to_char = "$o осветил$U на миг внутренним светом и тут же потух$Q.";
		}
			break;

		case SPELL_LIGHT: obj->add_timed_spell(SPELL_LIGHT, -1);
			obj->set_extra_flag(EExtraFlag::ITEM_GLOW);
			to_char = "$o засветил$U ровным зеленоватым светом.";
			break;

		case SPELL_DARKNESS:
			if (obj->timed_spell().check_spell(SPELL_LIGHT)) {
				obj->del_timed_spell(SPELL_LIGHT, true);
				return 1;
			}
			break;
	} // switch

	if (to_char == nullptr) {
		send_to_char(NOEFFECT, ch);
	} else {
		act(to_char, TRUE, ch, obj, 0, TO_CHAR);
	}

	return 1;
}

int mag_creations(int/* level*/, CHAR_DATA *ch, int spellnum) {
	obj_vnum z;

	if (ch == nullptr) {
		return 0;
	}
	// level = MAX(MIN(level, LVL_IMPL), 1); - Hm, not used.

	switch (spellnum) {
		case SPELL_CREATE_FOOD: z = START_BREAD;
			break;

		case SPELL_CREATE_LIGHT: z = CREATE_LIGHT;
			break;

		default: send_to_char("Spell unimplemented, it would seem.\r\n", ch);
			return 0;
			break;
	}

	const auto tobj = world_objects.create_from_prototype_by_vnum(z);
	if (!tobj) {
		send_to_char("Что-то не видно образа для создания.\r\n", ch);
		log("SYSERR: spell_creations, spell %d, obj %d: obj not found", spellnum, z);
		return 0;
	}

	act("$n создал$g $o3.", FALSE, ch, tobj.get(), 0, TO_ROOM | TO_ARENA_LISTEN);
	act("Вы создали $o3.", FALSE, ch, tobj.get(), 0, TO_CHAR);
	load_otrigger(tobj.get());

	if (IS_CARRYING_N(ch) >= CAN_CARRY_N(ch)) {
		send_to_char("Вы не сможете унести столько предметов.\r\n", ch);
		obj_to_room(tobj.get(), ch->in_room);
		obj_decay(tobj.get());
	} else if (IS_CARRYING_W(ch) + GET_OBJ_WEIGHT(tobj) > CAN_CARRY_W(ch)) {
		send_to_char("Вы не сможете унести такой вес.\r\n", ch);
		obj_to_room(tobj.get(), ch->in_room);
		obj_decay(tobj.get());
	} else {
		obj_to_char(tobj.get(), ch);
	}

	return 1;
}

int mag_manual(int level, CHAR_DATA *caster, CHAR_DATA *cvict, OBJ_DATA *ovict, int spellnum, int/* savetype*/) {
	switch (spellnum) {
		case SPELL_GROUP_RECALL:
		case SPELL_WORD_OF_RECALL: MANUAL_SPELL(spell_recall);
			break;
		case SPELL_TELEPORT: MANUAL_SPELL(spell_teleport);
			break;
		case SPELL_CONTROL_WEATHER: MANUAL_SPELL(spell_control_weather);
			break;
		case SPELL_CREATE_WATER: MANUAL_SPELL(spell_create_water);
			break;
		case SPELL_LOCATE_OBJECT: MANUAL_SPELL(spell_locate_object);
			break;
		case SPELL_SUMMON: MANUAL_SPELL(spell_summon);
			break;
		case SPELL_PORTAL: MANUAL_SPELL(spell_portal);
			break;
		case SPELL_CREATE_WEAPON: MANUAL_SPELL(spell_create_weapon);
			break;
		case SPELL_RELOCATE: MANUAL_SPELL(spell_relocate);
			break;
		case SPELL_CHARM: MANUAL_SPELL(spell_charm);
			break;
		case SPELL_ENERGY_DRAIN: MANUAL_SPELL(spell_energydrain);
			break;
		case SPELL_MASS_FEAR:
		case SPELL_FEAR: MANUAL_SPELL(spell_fear);
			break;
		case SPELL_SACRIFICE: MANUAL_SPELL(spell_sacrifice);
			break;
		case SPELL_IDENTIFY: MANUAL_SPELL(spell_identify);
			break;
		case SPELL_FULL_IDENTIFY: MANUAL_SPELL(spell_full_identify);
			break;
		case SPELL_HOLYSTRIKE: MANUAL_SPELL(spell_holystrike);
			break;
		case SPELL_ANGEL: MANUAL_SPELL(spell_angel);
			break;
		case SPELL_VAMPIRE: MANUAL_SPELL(spell_vampire);
			break;
		case SPELL_MENTAL_SHADOW: MANUAL_SPELL(spell_mental_shadow);
			break;
		default: return 0;
			break;
	}
	return 1;
}

//******************************************************************************
//******************************************************************************
//******************************************************************************

int check_mobile_list(CHAR_DATA *ch) {
	for (const auto &vict : character_list) {
		if (vict.get() == ch) {
			return (TRUE);
		}
	}

	return (FALSE);
}

void cast_reaction(CHAR_DATA *victim, CHAR_DATA *caster, int spellnum) {
	if (caster == victim)
		return;

	if (!check_mobile_list(victim) || !SpINFO.violent)
		return;

	if (AFF_FLAGGED(victim, EAffectFlag::AFF_CHARM) ||
		AFF_FLAGGED(victim, EAffectFlag::AFF_SLEEP) ||
		AFF_FLAGGED(victim, EAffectFlag::AFF_BLIND) ||
		AFF_FLAGGED(victim, EAffectFlag::AFF_STOPFIGHT) ||
		AFF_FLAGGED(victim, EAffectFlag::AFF_MAGICSTOPFIGHT) || AFF_FLAGGED(victim, EAffectFlag::AFF_HOLD)
		|| IS_HORSE(victim))
		return;

	if (IS_NPC(caster)
		&& GET_MOB_RNUM(caster) == real_mobile(DG_CASTER_PROXY))
		return;

	if (CAN_SEE(victim, caster) && MAY_ATTACK(victim) && IN_ROOM(victim) == IN_ROOM(caster)) {
		if (IS_NPC(victim))
			attack_best(victim, caster);
		else
			hit(victim, caster, ESkill::SKILL_UNDEF, FightSystem::MAIN_HAND);
	} else if (CAN_SEE(victim, caster) && !IS_NPC(caster) && IS_NPC(victim) && MOB_FLAGGED(victim, MOB_MEMORY)) {
		mobRemember(victim, caster);
	}

	if (caster->purged()) {
		return;
	}
	if (!CAN_SEE(victim, caster) && (GET_REAL_INT(victim) > 25 || GET_REAL_INT(victim) > number(10, 25))) {
		if (!AFF_FLAGGED(victim, EAffectFlag::AFF_DETECT_INVIS)
			&& GET_SPELL_MEM(victim, SPELL_DETECT_INVIS) > 0)
			cast_spell(victim, victim, 0, 0, SPELL_DETECT_INVIS, SPELL_DETECT_INVIS);
		else if (!AFF_FLAGGED(victim, EAffectFlag::AFF_SENSE_LIFE)
			&& GET_SPELL_MEM(victim, SPELL_SENSE_LIFE) > 0)
			cast_spell(victim, victim, 0, 0, SPELL_SENSE_LIFE, SPELL_SENSE_LIFE);
		else if (!AFF_FLAGGED(victim, EAffectFlag::AFF_INFRAVISION)
			&& GET_SPELL_MEM(victim, SPELL_LIGHT) > 0)
			cast_spell(victim, victim, 0, 0, SPELL_LIGHT, SPELL_LIGHT);
	}
}

// Применение заклинания к одной цели
//---------------------------------------------------------
int mag_single_target(int level, CHAR_DATA *caster, CHAR_DATA *cvict, OBJ_DATA *ovict, int spellnum, int savetype) {
	/*
	if (IS_SET(SpINFO.routines, 0)){
			send_to_char(NOEFFECT, caster);
			return (-1);
	}
	*/

	//туповато конечно, но подобные проверки тут как счупалцьа перепутаны
	//и чтоб сделать по человечески надо треть пары модулей перелопатить
	if (cvict && (caster != cvict))
		if (IS_GOD(cvict) || (((GET_LEVEL(cvict) / 2) > (GET_LEVEL(caster) + (GET_REMORT(caster) / 2)))
			&& !IS_NPC(caster))) // при разнице уровня более чем в 2 раза закл фэйл
		{
			send_to_char(NOEFFECT, caster);
			return (-1);
		}

	if (IS_SET(SpINFO.routines, MAG_WARCRY) && cvict && IS_UNDEAD(cvict))
		return 1;

	if (IS_SET(SpINFO.routines, MAG_DAMAGE))
		if (mag_damage(level, caster, cvict, spellnum, savetype) == -1)
			return (-1);    // Successful and target died, don't cast again.

	if (IS_SET(SpINFO.routines, MAG_AFFECTS))
		mag_affects(level, caster, cvict, spellnum, savetype);

	if (IS_SET(SpINFO.routines, MAG_UNAFFECTS))
		mag_unaffects(level, caster, cvict, spellnum, savetype);

	if (IS_SET(SpINFO.routines, MAG_POINTS))
		mag_points(level, caster, cvict, spellnum, savetype);

	if (IS_SET(SpINFO.routines, MAG_ALTER_OBJS))
		mag_alter_objs(level, caster, ovict, spellnum, savetype);

	if (IS_SET(SpINFO.routines, MAG_SUMMONS))
		mag_summons(level, caster, ovict, spellnum, 1);    // savetype =1 -- ВРЕМЕННО, показатель что фэйлить надо

	if (IS_SET(SpINFO.routines, MAG_CREATIONS))
		mag_creations(level, caster, spellnum);

	if (IS_SET(SpINFO.routines, MAG_MANUAL))
		mag_manual(level, caster, cvict, ovict, spellnum, savetype);

	cast_reaction(cvict, caster, spellnum);
	return 1;
}

typedef struct {
	int spell;
	const char *to_char;
	const char *to_room;
	const char *to_vict;
	float castSuccessPercentDecay;
	int skillDivisor;
	int diceSize;
	int minTargetsAmount;
	int maxTargetsAmount;
	int freeTargets;
	int castLevelDecay;
} spl_message;

// Svent TODO Перенести эту порнографию в спеллпарсер
const spl_message mag_messages[] =
	{
		{SPELL_PALADINE_INSPIRATION, // групповой
		 "Ваш точный удар воодушевил и придал новых сил!",
		 "Точный удар $n1 воодушевил и придал новых сил!",
		 nullptr,
		 0.0, 20, 2, 1, 20, 3, 0},
		{SPELL_EVILESS,
		 "Вы запросили помощи у Чернобога. Долг перед темными силами стал чуточку больше..",
		 "Внезапно появившееся чёрное облако скрыло $n3 на мгновение от вашего взгляда.",
		 nullptr,
		 0.0, 20, 2, 1, 20, 3, 0},
		{SPELL_MASS_BLINDNESS,
		 "У вас над головой возникла яркая вспышка, которая ослепила все живое.",
		 "Вдруг над головой $n1 возникла яркая вспышка.",
		 "Вы невольно взглянули на вспышку света, вызванную $n4, и ваши глаза заслезились.",
		 0.05, 20, 2, 5, 20, 3, 2},
		{SPELL_MASS_HOLD,
		 "Вы сжали зубы от боли, когда из вашего тела вырвалось множество невидимых каменных лучей.",
		 nullptr,
		 "В вас попал каменный луч, исходящий от $n1.",
		 0.05, 20, 2, 5, 20, 3, 2},
		{SPELL_MASS_CURSE,
		 "Медленно оглянувшись, вы прошептали древние слова.",
		 nullptr,
		 "$n злобно посмотрел$g на вас и начал$g шептать древние слова.",
		 0.05, 20, 3, 5, 20, 3, 2},
		{SPELL_MASS_SILENCE,
		 "Поведя вокруг грозным взглядом, вы заставили всех замолчать.",
		 nullptr,
		 "Вы встретились взглядом с $n4, и у вас появилось ощущение, что горлу чего-то не хватает.",
		 0.05, 20, 2, 5, 20, 3, 2},
		{SPELL_DEAFNESS,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.05, 10, 3, 5, 20, 3, 2},
		{SPELL_MASS_DEAFNESS,
		 "Вы нахмурились, склонив голову, и громкий хлопок сотряс воздух.",
		 "Как только $n0 склонил$g голову, раздался оглушающий хлопок.",
		 nullptr,
		 0.05, 10, 3, 5, 20, 3, 2},
		{SPELL_MASS_SLOW,
		 "Положив ладони на землю, вы вызвали цепкие корни,\r\nопутавшие существ, стоящих рядом с вами.",
		 nullptr,
		 "$n вызвал$g цепкие корни, опутавшие ваши ноги.",
		 0.05, 10, 3, 5, 20, 3, 2},
		{SPELL_ARMAGEDDON,
		 "Вы сплели руки в замысловатом жесте, и все потускнело!",
		 "$n сплел$g руки в замысловатом жесте, и все потускнело!",
		 nullptr,
		 0.05, 25, 2, 5, 20, 3, 2},
		{SPELL_EARTHQUAKE,
		 "Вы опустили руки, и земля начала дрожать вокруг вас!",
		 "$n опустил$g руки, и земля задрожала!",
		 nullptr,
		 0.05, 25, 2, 5, 20, 5, 2},
		{SPELL_THUNDERSTONE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.05, 25, 2, 3, 15, 3, 4},
		{SPELL_CONE_OF_COLD,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.05, 40, 2, 2, 5, 5, 4},
		{SPELL_ACID,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.05, 20, 2, 3, 8, 4, 4},
		{SPELL_LIGHTNING_BOLT,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.05, 15, 3, 3, 6, 4, 4},
		{SPELL_CALL_LIGHTNING,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.05, 15, 3, 3, 5, 4, 4},
		{SPELL_WHIRLWIND,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.05, 40, 1, 1, 3, 3, 4},
		{SPELL_DAMAGE_SERIOUS,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.05, 20, 3, 1, 6, 6, 4},
		{SPELL_FIREBLAST,
		 "Вы вызвали потоки подземного пламени!",
		 "$n0 вызвал$g потоки пламени из глубин земли!",
		 nullptr,
		 0.05, 20, 2, 5, 20, 3, 3},
		{SPELL_ICESTORM,
		 "Вы воздели руки к небу, и тысячи мелких льдинок хлынули вниз!",
		 "$n воздел$g руки к небу, и тысячи мелких льдинок хлынули вниз!",
		 nullptr,
		 0.05, 30, 2, 5, 20, 3, 4},
		{SPELL_DUSTSTORM,
		 "Вы взмахнули руками и вызвали огромное пылевое облако,\r\nскрывшее все вокруг.",
		 "Вас поглотила пылевая буря, вызванная $n4.",
		 nullptr,
		 0.05, 15, 2, 5, 20, 3, 2},
		{SPELL_MASS_FEAR,
		 "Вы оглядели комнату устрашающим взглядом, заставив всех содрогнуться.",
		 "$n0 оглядел$g комнату устрашающим взглядом.",
		 nullptr,
		 0.05, 15, 2, 5, 20, 3, 3},
		{SPELL_GLITTERDUST,
		 "Вы слегка прищелкнули пальцами, и вокруг сгустилось облако блестящей пыли.",
		 "$n0 сотворил$g облако блестящей пыли, медленно осевшее на землю.",
		 nullptr,
		 0.05, 15, 3, 5, 20, 5, 3},
		{SPELL_SONICWAVE,
		 "Вы оттолкнули от себя воздух руками, и он плотным кольцом стремительно двинулся во все стороны!",
		 "$n махнул$g руками, и огромное кольцо сжатого воздуха распостранилось во все стороны!",
		 nullptr,
		 0.05, 20, 2, 5, 20, 3, 3},
		{SPELL_CHAIN_LIGHTNING,
		 "Вы подняли руки к небу и оно осветилось яркими вспышками!",
		 "$n поднял$g руки к небу и оно осветилось яркими вспышками!",
		 nullptr,
		 0.05, 10, 3, 1, 8, 1, 5},
		{SPELL_EARTHFALL,
		 "Вы высоко подбросили комок земли и он, увеличиваясь на глазах, обрушился вниз.",
		 "$n высоко подбросил$g комок земли, который, увеличиваясь на глазах, стал падать вниз.",
		 nullptr,
		 0.05, 20, 2, 1, 3, 1, 8},
		{SPELL_SHOCK,
		 "Яркая вспышка слетела с кончиков ваших пальцев и с оглушительным грохотом взорвалась в воздухе.",
		 "Выпущенная $n1 яркая вспышка с оглушительным грохотом взорвалась в воздухе.",
		 nullptr,
		 0.05, 35, 2, 1, 4, 2, 8},
		{SPELL_BURDEN_OF_TIME,
		 "Вы скрестили руки на груди, вызвав яркую вспышку синего света.",
		 "$n0 скрестил$g руки на груди, вызвав яркую вспышку синего света.",
		 nullptr,
		 0.05, 20, 2, 5, 20, 3, 8},
		{SPELL_FAILURE,
		 "Вы простерли руки над головой, вызвав череду раскатов грома.",
		 "$n0 вызвал$g череду раскатов грома, заставивших все вокруг содрогнуться.",
		 nullptr,
		 0.05, 15, 2, 1, 5, 3, 8},
		{SPELL_SCREAM,
		 "Вы испустили кошмарный вопль, едва не разорвавший вам горло.",
		 "$n0 испустил$g кошмарный вопль, отдавшийся в вашей душе замогильным холодом.",
		 nullptr,
		 0.05, 40, 3, 1, 8, 3, 5},
		{SPELL_BURNING_HANDS,
		 "С ваших ладоней сорвался поток жаркого пламени.",
		 "$n0 испустил$g поток жаркого багрового пламени!",
		 nullptr,
		 0.05, 20, 2, 5, 20, 3, 7},
		{SPELL_COLOR_SPRAY,
		 "Из ваших рук вылетел сноп ледяных стрел.",
		 "$n0 метнул$g во врагов сноп ледяных стрел.",
		 nullptr,
		 0.05, 30, 2, 1, 5, 3, 7},
		{SPELL_WC_OF_CHALLENGE,
		 nullptr,
		 "Вы не стерпели насмешки, и бросились на $n1!",
		 nullptr,
		 0.01, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_MENACE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.01, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_RAGE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.01, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_MADNESS,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.01, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_THUNDER,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.01, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_DEFENSE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.01, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_HEAL,
		 "Вы подняли голову вверх и ощутили яркий свет, ласково бегущий по вашему телу.\r\n",
		 nullptr,
		 nullptr,
		 0.01, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_ARMOR,
		 "Вы создали защитную сферу, которая окутала вас и пространство рядом с вами.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_RECALL,
		 "Вы выкрикнули заклинание и хлопнули в ладоши.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_STRENGTH,
		 "Вы призвали мощь Вселенной.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_BLESS,
		 "Прикрыв глаза, вы прошептали таинственную молитву.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_HASTE,
		 "Разведя руки в стороны, вы ощутили всю мощь стихии ветра.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_FLY,
		 "Ваше заклинание вызвало белое облако, которое разделилось, подхватывая вас и товарищей.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_INVISIBLE,
		 "Вы вызвали прозрачный туман, поглотивший все дружественное вам.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_MAGICGLASS,
		 "Вы произнесли несколько резких слов, и все вокруг засеребрилось.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_SANCTUARY,
		 "Вы подняли руки к небу и произнесли священную молитву.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_PRISMATICAURA,
		 "Силы духа, призванные вами, окутали вас и окружающих голубоватым сиянием.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_FIRE_AURA,
		 "Силы огня пришли к вам на помощь и защитили вас.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_AIR_AURA,
		 "Силы воздуха пришли к вам на помощь и защитили вас.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_ICE_AURA,
		 "Силы холода пришли к вам на помощь и защитили вас.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_REFRESH,
		 "Ваша магия наполнила воздух зеленоватым сиянием.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_DEFENSE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_BATTLE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_POWER,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_BLESS,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_OF_COURAGE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_SIGHT_OF_DARKNESS,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GENERAL_SINCERITY,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_MAGICAL_GAZE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_ALL_SEEING_EYE,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_EYE_OF_GODS,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_BREATHING_AT_DEPTH,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GENERAL_RECOVERY,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_COMMON_MEAL,
		 "Вы услышали гомон невидимых лакеев, готовящих трапезу.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_STONE_WALL,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_SNAKE_EYES,
		 nullptr,
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_EARTH_AURA,
		 "Земля одарила вас своей защитой.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_PROT_FROM_EVIL,
		 "Сила света подавила в вас страх к тьме.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_BLINK,
		 "Очертания вас и соратников замерцали в такт биения сердца, став прозрачней.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_CLOUDLY,
		 "Пелена тумана окутала вас и окружающих, скрыв очертания.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_GROUP_AWARNESS,
		 "Произнесенные слова обострили ваши чувства и внимательность ваших соратников.\r\n",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_EXPERIENSE,
		 "Вы приготовились к обретению нового опыта.",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_LUCK,
		 "Вы ощутили, что вам улыбнулась удача.",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_WC_PHYSDAMAGE,
		 "Боевой клич придал вам сил!",
		 nullptr,
		 nullptr,
		 0.0, 20, 2, 5, 20, 3, 0},
		{SPELL_MASS_FAILURE,
		 "Вняв вашему призыву, Змей Велес коснулся недобрым взглядом ваших супостатов.\r\n",
		 nullptr,
		 "$n провыл$g несколько странно звучащих слов и от тяжелого взгляда из-за края мира у вас подкосились ноги.",
		 0.03, 25, 2, 3, 15, 4, 6},
		{SPELL_MASS_NOFLEE,
		 "Вы соткали магические тенета, опутавшие ваших врагов.\r\n",
		 nullptr,
		 "$n что-то прошептал$g, странно скрючив пальцы, и взлетевшие откуда ни возьмись ловчие сети опутали вас",
		 0.03, 25, 2, 3, 15, 5, 6},
		{-1, nullptr, nullptr, nullptr, 0.01, 1, 1, 1, 1, 1, 0}
	};

int findIndexOfSpellMsg(int spellNumber) {
	int i = 0;
	for (; mag_messages[i].spell != -1; ++i) {
		if (mag_messages[i].spell == spellNumber) {
			return i;
		}
	}
	return i;
}

int trySendCastMessages(CHAR_DATA *ch, CHAR_DATA *victim, ROOM_DATA *room, int spellnum) {
	int msgIndex = findIndexOfSpellMsg(spellnum);
	if (mag_messages[msgIndex].spell < 0) {
		sprintf(buf, "ERROR: Нет сообщений в mag_messages для заклинания с номером %d.", spellnum);
		mudlog(buf, BRF, LVL_BUILDER, SYSLOG, TRUE);
		return msgIndex;
	}
	if (room && world[ch->in_room] == room) {
		if (multi_cast_say(ch)) {
			if (mag_messages[msgIndex].to_char != nullptr) {
				// вот тут надо воткнуть проверку на группу.
				act(mag_messages[msgIndex].to_char, FALSE, ch, 0, victim, TO_CHAR);
			}
			if (mag_messages[msgIndex].to_room != nullptr) {
				act(mag_messages[msgIndex].to_room, FALSE, ch, 0, victim, TO_ROOM | TO_ARENA_LISTEN);
			}
		}
	}
	return msgIndex;
};

int calculateAmountTargetsOfSpell(const CHAR_DATA *ch, const int &msgIndex, const int &spellnum) {
	int amount = ch->get_skill(get_magic_skill_number_by_spell(spellnum));
	amount = dice(amount / mag_messages[msgIndex].skillDivisor, mag_messages[msgIndex].diceSize);
	return mag_messages[msgIndex].minTargetsAmount + MIN(amount, mag_messages[msgIndex].maxTargetsAmount);
}

int callMagicToArea(CHAR_DATA *ch, CHAR_DATA *victim, ROOM_DATA *room, int spellnum, int level) {
	if (ch == nullptr || IN_ROOM(ch) == NOWHERE) {
		return 0;
	}

	ActionTargeting::FoesRosterType
		roster{ch, victim, [](CHAR_DATA *, CHAR_DATA *target) { return !IS_HORSE(target); }};
	int msgIndex = trySendCastMessages(ch, victim, room, spellnum);
	int targetsAmount = calculateAmountTargetsOfSpell(ch, msgIndex, spellnum);
	int targetsCounter = 1;
	float castDecay = 0.0;
	int levelDecay = 0;
	if (can_use_feat(ch, MULTI_CAST_FEAT)) {
		castDecay = mag_messages[msgIndex].castSuccessPercentDecay * 0.6;
		levelDecay = MAX(MIN(1, mag_messages[msgIndex].castLevelDecay), mag_messages[msgIndex].castLevelDecay - 1);
	} else {
		castDecay = mag_messages[msgIndex].castSuccessPercentDecay;
		levelDecay = mag_messages[msgIndex].castLevelDecay;
	}
	const int CASTER_CAST_SUCCESS = GET_CAST_SUCCESS(ch);

	for (const auto &target : roster) {
		if (mag_messages[msgIndex].to_vict != nullptr && target->desc) {
			act(mag_messages[msgIndex].to_vict, FALSE, ch, 0, target, TO_VICT);
		}
		mag_single_target(level, ch, target, nullptr, spellnum, SAVING_STABILITY);
		if (ch->purged()) {
			return 1;
		}
		if (!IS_NPC(ch)) {
			++targetsCounter;
			if (targetsCounter > mag_messages[msgIndex].freeTargets) {
				int tax = CASTER_CAST_SUCCESS * castDecay * (targetsCounter - mag_messages[msgIndex].freeTargets);
				GET_CAST_SUCCESS(ch) = MAX(-200, CASTER_CAST_SUCCESS - tax);
				level = MAX(1, level - levelDecay);
				if (PRF_FLAGGED(ch, PRF_TESTER)) {
					send_to_char(ch,
								 "&GМакс. целей: %d, Каст: %d, Уровень заклинания: %d.&n\r\n",
								 targetsAmount,
								 GET_CAST_SUCCESS(ch),
								 level);
				}
			};
		};
		if (targetsCounter >= targetsAmount) {
			break;
		}
	}

	GET_CAST_SUCCESS(ch) = CASTER_CAST_SUCCESS;
	return 1;
}

// Применение заклинания к группе в комнате
//---------------------------------------------------------
int callMagicToGroup(int level, CHAR_DATA *ch, int spellnum) {
	if (ch == nullptr) {
		return 0;
	}

	trySendCastMessages(ch, nullptr, world[IN_ROOM(ch)], spellnum);

	ActionTargeting::FriendsRosterType roster{ch, ch};
	roster.flip();
	for (const auto target : roster) {
		mag_single_target(level, ch, target, nullptr, spellnum, SAVING_STABILITY);
	}
	return 1;
}

// vim: ts=4 sw=4 tw=0 noet syntax=cpp :
