/* ************************************************************************
*   File: mobact.cpp                                    Part of Bylins    *
*  Usage: Functions for generating intelligent (?) behavior in mobiles    *
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
#include "mobact.h"

#include "skills/backstab.h"
#include "skills/bash.h"
#include "skills/strangle.h"
#include "skills/chopoff.h"
#include "skills/disarm.h"
#include "skills/stupor.h"
#include "skills/throw.h"
#include "skills/mighthit.h"
#include "skills/protect.h"
#include "skills/track.h"

#include "abilities/abilities_rollsystem.h"
#include "action_targeting.h"
#include "act_movement.h"
#include "chars/world.characters.h"
#include "world_objects.h"
#include "handler.h"
#include "magic/magic.h"
#include "fightsystem/pk.h"
#include "random.h"
#include "house.h"
#include "fightsystem/fight.h"
#include "fightsystem/fight_hit.h"
#include "magic/magic_rooms.h"

// external structs
extern int no_specials;
extern int guild_poly(CHAR_DATA *, void *, int, char *);
extern guardian_type guardian_list;
extern struct ZoneData *zone_table;

int npc_scavenge(CHAR_DATA *ch);
int npc_loot(CHAR_DATA *ch);
int npc_move(CHAR_DATA *ch, int dir, int need_specials_check);
void npc_wield(CHAR_DATA *ch);
void npc_armor(CHAR_DATA *ch);
void npc_group(CHAR_DATA *ch);
void npc_groupbattle(CHAR_DATA *ch);
int npc_walk(CHAR_DATA *ch);
int npc_steal(CHAR_DATA *ch);
void npc_light(CHAR_DATA *ch);
extern void set_wait(CHAR_DATA *ch, int waittime, int victim_in_room);
bool guardian_attack(CHAR_DATA *ch, CHAR_DATA *vict);
void drop_obj_on_zreset(CHAR_DATA *ch, OBJ_DATA *obj, bool inv, bool zone_reset);

// local functions

#define MOB_AGGR_TO_ALIGN (MOB_AGGR_EVIL | MOB_AGGR_NEUTRAL | MOB_AGGR_GOOD)

int extra_aggressive(CHAR_DATA *ch, CHAR_DATA *victim) {
	int time_ok = FALSE, no_time = TRUE, month_ok = FALSE, no_month = TRUE, agro = FALSE;

	if (!IS_NPC(ch))
		return (FALSE);

	if (MOB_FLAGGED(ch, MOB_AGGRESSIVE))
		return (TRUE);

	if (MOB_FLAGGED(ch, MOB_GUARDIAN) && guardian_attack(ch, victim))
		return (TRUE);

	if (victim && MOB_FLAGGED(ch, MOB_AGGRMONO) && !IS_NPC(victim) && GET_RELIGION(victim) == RELIGION_MONO)
		agro = TRUE;

	if (victim && MOB_FLAGGED(ch, MOB_AGGRPOLY) && !IS_NPC(victim) && GET_RELIGION(victim) == RELIGION_POLY)
		agro = TRUE;

//Пока что убрал обработку флагов, тем более что персов кроме русичей и нет
//Поскольку расы и рода убраны из кода то так вот в лоб этот флаг не сделать,
//надо или по названию расы смотреть или еще что придумывать
	/*if (victim && MOB_FLAGGED(ch, MOB_AGGR_RUSICHI) && !IS_NPC(victim) && GET_KIN(victim) == KIN_RUSICHI)
		agro = TRUE;

	if (victim && MOB_FLAGGED(ch, MOB_AGGR_VIKINGI) && !IS_NPC(victim) && GET_KIN(victim) == KIN_VIKINGI)
		agro = TRUE;

	if (victim && MOB_FLAGGED(ch, MOB_AGGR_STEPNYAKI) && !IS_NPC(victim) && GET_KIN(victim) == KIN_STEPNYAKI)
		agro = TRUE; */

	if (MOB_FLAGGED(ch, MOB_AGGR_DAY)) {
		no_time = FALSE;
		if (weather_info.sunlight == SUN_RISE || weather_info.sunlight == SUN_LIGHT)
			time_ok = TRUE;
	}

	if (MOB_FLAGGED(ch, MOB_AGGR_NIGHT)) {
		no_time = FALSE;
		if (weather_info.sunlight == SUN_DARK || weather_info.sunlight == SUN_SET)
			time_ok = TRUE;
	}

	if (MOB_FLAGGED(ch, MOB_AGGR_WINTER)) {
		no_month = FALSE;
		if (weather_info.season == SEASON_WINTER)
			month_ok = TRUE;
	}

	if (MOB_FLAGGED(ch, MOB_AGGR_SPRING)) {
		no_month = FALSE;
		if (weather_info.season == SEASON_SPRING)
			month_ok = TRUE;
	}

	if (MOB_FLAGGED(ch, MOB_AGGR_SUMMER)) {
		no_month = FALSE;
		if (weather_info.season == SEASON_SUMMER)
			month_ok = TRUE;
	}

	if (MOB_FLAGGED(ch, MOB_AGGR_AUTUMN)) {
		no_month = FALSE;
		if (weather_info.season == SEASON_AUTUMN)
			month_ok = TRUE;
	}

	if (MOB_FLAGGED(ch, MOB_AGGR_FULLMOON)) {
		no_time = FALSE;
		if (weather_info.moon_day >= 12 && weather_info.moon_day <= 15 &&
			(weather_info.sunlight == SUN_DARK || weather_info.sunlight == SUN_SET))
			time_ok = TRUE;
	}
	if (agro || !no_time || !no_month)
		return ((no_time || time_ok) && (no_month || month_ok));
	else
		return (FALSE);
}

int attack_best(CHAR_DATA *ch, CHAR_DATA *victim) {
	OBJ_DATA *wielded = GET_EQ(ch, WEAR_WIELD);
	if (victim) {
		if (ch->get_skill(SKILL_STRANGLE) && !timed_by_skill(ch, SKILL_STRANGLE)) {
			go_strangle(ch, victim);
			return (TRUE);
		}
		if (ch->get_skill(SKILL_BACKSTAB) && !victim->get_fighting()) {
			go_backstab(ch, victim);
			return (TRUE);
		}
		if (ch->get_skill(SKILL_MIGHTHIT)) {
			go_mighthit(ch, victim);
			return (TRUE);
		}
		if (ch->get_skill(SKILL_STUPOR)) {
			go_stupor(ch, victim);
			return (TRUE);
		}
		if (ch->get_skill(SKILL_BASH)) {
			go_bash(ch, victim);
			return (TRUE);
		}
		if (ch->get_skill(SKILL_THROW)
			&& wielded
			&& GET_OBJ_TYPE(wielded) == OBJ_DATA::ITEM_WEAPON
			&& wielded->get_extra_flag(EExtraFlag::ITEM_THROWING)) {
			go_throw(ch, victim);
		}
		if (ch->get_skill(SKILL_DISARM)) {
			go_disarm(ch, victim);
		}
		if (ch->get_skill(SKILL_CHOPOFF)) {
			go_chopoff(ch, victim);
		}
		if (!ch->get_fighting()) {
			victim = try_protect(victim, ch);
			hit(ch, victim, ESkill::SKILL_UNDEF, FightSystem::MAIN_HAND);
		}
		return (TRUE);
	} else
		return (FALSE);
}

#define KILL_FIGHTING   (1 << 0)
#define CHECK_HITS      (1 << 10)
#define SKIP_HIDING     (1 << 11)
#define SKIP_CAMOUFLAGE (1 << 12)
#define SKIP_SNEAKING   (1 << 13)
#define CHECK_OPPONENT  (1 << 14)
#define GUARD_ATTACK    (1 << 15)

int check_room_tracks(const room_rnum room, const long victim_id) {
	for (auto track = world[room]->track; track; track = track->next) {
		if (track->who == victim_id) {
			for (int i = 0; i < NUM_OF_DIRS; i++) {
				if (IS_SET(track->time_outgone[i], 7)) {
					return i;
				}
			}
		}
	}

	return BFS_ERROR;
}

int find_door(CHAR_DATA *ch, const bool track_method) {
	bool msg = false;

	for (const auto &vict : character_list) {
		if (CAN_SEE(ch, vict) && IN_ROOM(vict) != NOWHERE) {
			for (auto names = MEMORY(ch); names; names = names->next) {
				if (GET_IDNUM(vict) == names->id
					&& (!MOB_FLAGGED(ch, MOB_STAY_ZONE)
						|| world[ch->in_room]->zone_rn == world[IN_ROOM(vict)]->zone_rn)) {
					if (!msg) {
						msg = true;
						act("$n начал$g внимательно искать чьи-то следы.", FALSE, ch, 0, 0, TO_ROOM);
					}

					const auto door = track_method
									  ? check_room_tracks(ch->in_room, GET_IDNUM(vict))
									  : go_track(ch, vict.get(), SKILL_TRACK);

					if (BFS_ERROR != door) {
						return door;
					}
				}
			}
		}
	}

	return BFS_ERROR;
}

int npc_track(CHAR_DATA *ch) {
	const auto result = find_door(ch, GET_REAL_INT(ch) < number(15, 20));

	return result;
}

CHAR_DATA *selectRandomSkirmisherFromGroup(CHAR_DATA *leader) {
	ActionTargeting::FriendsRosterType roster{leader};
	auto isSkirmisher = [](CHAR_DATA *ch) { return PRF_FLAGGED(ch, PRF_SKIRMISHER); };
	int skirmishers = roster.count(isSkirmisher);
	if (skirmishers == 0 || skirmishers == roster.amount()) {
		return nullptr;
	}
	return roster.getRandomItem(isSkirmisher);
}

CHAR_DATA *selectVictimDependingOnGroupFormation(CHAR_DATA *assaulter, CHAR_DATA *initialVictim) {
	if ((initialVictim == nullptr) || !AFF_FLAGGED(initialVictim, EAffectFlag::AFF_GROUP)) {
		return initialVictim;
	}

	CHAR_DATA *leader = initialVictim;
	CHAR_DATA *newVictim = initialVictim;

	if (initialVictim->has_master()) {
		leader = initialVictim->get_master();
	}
	if (!assaulter->isInSameRoom(leader)) {
		return initialVictim;
	}

	newVictim = selectRandomSkirmisherFromGroup(leader);
	if (!newVictim) {
		return initialVictim;
	}

	AbilitySystem::AgainstRivalRollType abilityRoll;
	abilityRoll.initialize(leader, TACTICIAN_FEAT, assaulter);
	bool tacticianFail = !abilityRoll.isSuccess();
	abilityRoll.initialize(newVictim, SKIRMISHER_FEAT, assaulter);
	if (tacticianFail || !abilityRoll.isSuccess()) {
		return initialVictim;
	}

	act("Вы героически приняли удар $n1 на себя!", FALSE, assaulter, nullptr, newVictim, TO_VICT | TO_NO_BRIEF_SHIELDS);
	act("$n попытал$u ворваться в ваши ряды, но $N героически принял$G удар на себя!",
		FALSE,
		assaulter,
		nullptr,
		newVictim,
		TO_NOTVICT | TO_NO_BRIEF_SHIELDS);
	return newVictim;
}

CHAR_DATA *find_best_stupidmob_victim(CHAR_DATA *ch, int extmode) {
	CHAR_DATA *victim, *use_light = nullptr, *min_hp = nullptr, *min_lvl = nullptr, *caster = nullptr, *best = nullptr;
	int kill_this, extra_aggr = 0;

	victim = ch->get_fighting();

	for (const auto vict : world[ch->in_room]->people) {
		if ((IS_NPC(vict) && !IS_SET(extmode, CHECK_OPPONENT) && !IS_CHARMICE(vict))
			|| (IS_CHARMICE(vict) && !vict->get_fighting()) // чармиса агрим только если он уже с кем-то сражается
			|| PRF_FLAGGED(vict, PRF_NOHASSLE)
			|| !MAY_SEE(ch, ch, vict)
			|| (IS_SET(extmode, CHECK_OPPONENT) && ch != vict->get_fighting())
			|| (!may_kill_here(ch, vict, NoArgument) && !IS_SET(extmode, GUARD_ATTACK)))//старжники агрят в мирках
		{
			continue;
		}

		kill_this = FALSE;

		// Mobile too damage //обработка флага ТРУС
		if (IS_SET(extmode, CHECK_HITS)
			&& MOB_FLAGGED(ch, MOB_WIMPY)
			&& AWAKE(vict)
			&& GET_HIT(ch) * 2 < GET_REAL_MAX_HIT(ch)) {
			continue;
		}

		// Mobile helpers... //ассист
		if (IS_SET(extmode, KILL_FIGHTING)
			&& vict->get_fighting()
			&& vict->get_fighting() != ch
			&& IS_NPC(vict->get_fighting())
			&& !AFF_FLAGGED(vict->get_fighting(), EAffectFlag::AFF_CHARM)
			&& SAME_ALIGN(ch, vict->get_fighting())) {
			kill_this = TRUE;
		} else {
			// ... but no aggressive for this char
			if (!(extra_aggr = extra_aggressive(ch, vict))
				&& !IS_SET(extmode, GUARD_ATTACK)) {
				continue;
			}
		}

		// skip sneaking, hiding and camouflaging pc
		if (IS_SET(extmode, SKIP_SNEAKING)) {
			skip_sneaking(vict, ch);
			if ((EXTRA_FLAGGED(vict, EXTRA_FAILSNEAK))) {
				AFF_FLAGS(vict).unset(EAffectFlag::AFF_SNEAK);
			}
			if (AFF_FLAGGED(vict, EAffectFlag::AFF_SNEAK))
				continue;
		}

		if (IS_SET(extmode, SKIP_HIDING)) {
			skip_hiding(vict, ch);
			if (EXTRA_FLAGGED(vict, EXTRA_FAILHIDE)) {
				AFF_FLAGS(vict).unset(EAffectFlag::AFF_HIDE);
			}
		}

		if (IS_SET(extmode, SKIP_CAMOUFLAGE)) {
			skip_camouflage(vict, ch);
			if (EXTRA_FLAGGED(vict, EXTRA_FAILCAMOUFLAGE)) {
				AFF_FLAGS(vict).unset(EAffectFlag::AFF_CAMOUFLAGE);
			}
		}

		if (!CAN_SEE(ch, vict))
			continue;

		// Mobile aggresive
		if (!kill_this && extra_aggr) {
			if (can_use_feat(vict, SILVER_TONGUED_FEAT)) {
				const int number1 = number(1, GET_LEVEL(vict) * GET_REAL_CHA(vict));
				const int range = ((GET_LEVEL(ch) > 30)
								   ? (GET_LEVEL(ch) * 2 * GET_REAL_INT(ch) + GET_REAL_INT(ch) * 20)
								   : (GET_LEVEL(ch) * GET_REAL_INT(ch)));
				const int number2 = number(1, range);
				const bool do_continue = number1 > number2;
				if (do_continue) {
					continue;
				}
			}
			kill_this = TRUE;
		}

		if (!kill_this)
			continue;

		// define victim if not defined
		if (!victim)
			victim = vict;

		if (IS_DEFAULTDARK(ch->in_room)
			&& ((GET_EQ(vict, OBJ_DATA::ITEM_LIGHT)
				&& GET_OBJ_VAL(GET_EQ(vict, OBJ_DATA::ITEM_LIGHT), 2))
				|| (!AFF_FLAGGED(vict, EAffectFlag::AFF_HOLYDARK)
					&& (AFF_FLAGGED(vict, EAffectFlag::AFF_SINGLELIGHT)
						|| AFF_FLAGGED(vict, EAffectFlag::AFF_HOLYLIGHT))))
			&& (!use_light
				|| GET_REAL_CHA(use_light) > GET_REAL_CHA(vict))) {
			use_light = vict;
		}

		if (!min_hp
			|| GET_HIT(vict) + GET_REAL_CHA(vict) * 10 < GET_HIT(min_hp) + GET_REAL_CHA(min_hp) * 10) {
			min_hp = vict;
		}

		if (!min_lvl
			|| GET_LEVEL(vict) + number(1, GET_REAL_CHA(vict))
				< GET_LEVEL(min_lvl) + number(1, GET_REAL_CHA(min_lvl))) {
			min_lvl = vict;
		}

		if (IS_CASTER(vict)
			&& (!caster
				|| GET_CASTER(caster) * GET_REAL_CHA(vict) < GET_CASTER(vict) * GET_REAL_CHA(caster))) {
			caster = vict;
		}
	}

	if (GET_REAL_INT(ch) < 5 + number(1, 6))
		best = victim;
	else if (GET_REAL_INT(ch) < 10 + number(1, 6))
		best = use_light ? use_light : victim;
	else if (GET_REAL_INT(ch) < 15 + number(1, 6))
		best = min_lvl ? min_lvl : (use_light ? use_light : victim);
	else if (GET_REAL_INT(ch) < 25 + number(1, 6))
		best = caster ? caster : (min_lvl ? min_lvl : (use_light ? use_light : victim));
	else
		best = min_hp ? min_hp : (caster ? caster : (min_lvl ? min_lvl : (use_light ? use_light : victim)));

	if (best && !ch->get_fighting() && MOB_FLAGGED(ch, MOB_AGGRMONO) &&
		!IS_NPC(best) && GET_RELIGION(best) == RELIGION_MONO) {
		act("$n закричал$g: 'Умри, христианская собака!' и набросил$u на вас.", FALSE, ch, nullptr, best, TO_VICT);
		act("$n закричал$g: 'Умри, христианская собака!' и набросил$u на $N3.", FALSE, ch, nullptr, best, TO_NOTVICT);
	}

	if (best && !ch->get_fighting() && MOB_FLAGGED(ch, MOB_AGGRPOLY) &&
		!IS_NPC(best) && GET_RELIGION(best) == RELIGION_POLY) {
		act("$n закричал$g: 'Умри, грязный язычник!' и набросил$u на вас.", FALSE, ch, nullptr, best, TO_VICT);
		act("$n закричал$g: 'Умри, грязный язычник!' и набросил$u на $N3.", FALSE, ch, nullptr, best, TO_NOTVICT);
	}

	return selectVictimDependingOnGroupFormation(ch, best);
}
// TODO invert and rename for clarity: -> isStrayCharmice(), to return true if a charmice, and master is absent =II
bool find_master_charmice(CHAR_DATA *charmice) {
	// проверяем на спелл чарма, ищем хозяина и сравниваем румы
	if (!IS_CHARMICE(charmice) || !charmice->has_master()) {
		return true;
	}

	if (charmice->in_room == charmice->get_master()->in_room) {
		return true;
	}

	return false;
}

// пока тестово
CHAR_DATA *find_best_mob_victim(CHAR_DATA *ch, int extmode) {
	CHAR_DATA *currentVictim, *caster = nullptr, *best = nullptr;
	CHAR_DATA *druid = nullptr, *cler = nullptr, *charmmage = nullptr;
	int extra_aggr = 0;
	bool kill_this;

	int mobINT = GET_REAL_INT(ch);
	if (mobINT < INT_STUPID_MOD) {
		return find_best_stupidmob_victim(ch, extmode);
	}

	currentVictim = ch->get_fighting();
	if (currentVictim && !IS_NPC(currentVictim)) {
		if (IS_CASTER(currentVictim)) {
			return currentVictim;
		}
	}

	// проходим по всем чарам в комнате
	for (const auto vict : world[ch->in_room]->people) {
		if ((IS_NPC(vict) && !IS_CHARMICE(vict))
			|| (IS_CHARMICE(vict) && !vict->get_fighting()
				&& find_master_charmice(vict)) // чармиса агрим только если нет хозяина в руме.
			|| PRF_FLAGGED(vict, PRF_NOHASSLE)
			|| !MAY_SEE(ch, ch, vict) // если не видим цель,
			|| (IS_SET(extmode, CHECK_OPPONENT) && ch != vict->get_fighting())
			|| (!may_kill_here(ch, vict, NoArgument) && !IS_SET(extmode, GUARD_ATTACK)))//старжники агрят в мирках
		{
			continue;
		}

		kill_this = FALSE;
		// Mobile too damage //обработка флага ТРУС
		if (IS_SET(extmode, CHECK_HITS)
			&& MOB_FLAGGED(ch, MOB_WIMPY)
			&& AWAKE(vict) && GET_HIT(ch) * 2 < GET_REAL_MAX_HIT(ch)) {
			continue;
		}

		// Mobile helpers... //ассист
		if ((vict->get_fighting())
			&& (vict->get_fighting() != ch)
			&& (IS_NPC(vict->get_fighting()))
			&& (!AFF_FLAGGED(vict->get_fighting(), EAffectFlag::AFF_CHARM))) {
			kill_this = TRUE;
		} else {
			// ... but no aggressive for this char
			if (!(extra_aggr = extra_aggressive(ch, vict))
				&& !IS_SET(extmode, GUARD_ATTACK)) {
				continue;
			}
		}
		if (IS_SET(extmode, SKIP_SNEAKING)) {
			skip_sneaking(vict, ch);
			if (EXTRA_FLAGGED(vict, EXTRA_FAILSNEAK)) {
				AFF_FLAGS(vict).unset(EAffectFlag::AFF_SNEAK);
			}

			if (AFF_FLAGGED(vict, EAffectFlag::AFF_SNEAK)) {
				continue;
			}
		}

		if (IS_SET(extmode, SKIP_HIDING)) {
			skip_hiding(vict, ch);
			if (EXTRA_FLAGGED(vict, EXTRA_FAILHIDE)) {
				AFF_FLAGS(vict).unset(EAffectFlag::AFF_HIDE);
			}
		}

		if (IS_SET(extmode, SKIP_CAMOUFLAGE)) {
			skip_camouflage(vict, ch);
			if (EXTRA_FLAGGED(vict, EXTRA_FAILCAMOUFLAGE)) {
				AFF_FLAGS(vict).unset(EAffectFlag::AFF_CAMOUFLAGE);
			}
		}
		if (!CAN_SEE(ch, vict))
			continue;

		if (!kill_this && extra_aggr) {
			if (can_use_feat(vict, SILVER_TONGUED_FEAT)
				&& number(1, GET_LEVEL(vict) * GET_REAL_CHA(vict)) > number(1, GET_LEVEL(ch) * GET_REAL_INT(ch))) {
				continue;
			}
			kill_this = TRUE;
		}

		if (!kill_this)
			continue;
		// волхв
		if (GET_CLASS(vict) == CLASS_DRUID) {
			druid = vict;
			caster = vict;
			continue;
		}
		// лекарь
		if (GET_CLASS(vict) == CLASS_CLERIC) {
			cler = vict;
			caster = vict;
			continue;
		}
		// кудес
		if (GET_CLASS(vict) == CLASS_CHARMMAGE) {
			charmmage = vict;
			caster = vict;
			continue;
		}

		if (GET_HIT(vict) <= CHARACTER_HP_FOR_MOB_PRIORITY_ATTACK) {
			return vict;
		}
		if (IS_CASTER(vict)) {
			caster = vict;
			continue;
		}
		best = vict;
	}

	if (!best) {
		best = currentVictim;
	}

	if (mobINT < INT_MIDDLE_AI) {
		int rand = number(0, 2);
		if (caster) {
			best = caster;
		}
		if ((rand == 0) && (druid)) {
			best = druid;
		}
		if ((rand == 1) && (cler)) {
			best = cler;
		}
		if ((rand == 2) && (charmmage)) {
			best = charmmage;
		}
		return selectVictimDependingOnGroupFormation(ch, best);
	}

	if (mobINT < INT_HIGH_AI) {
		int rand = number(0, 1);
		if (caster)
			best = caster;
		if (charmmage)
			best = charmmage;
		if ((rand == 0) && (druid))
			best = druid;
		if ((rand == 1) && (cler))
			best = cler;

		return selectVictimDependingOnGroupFormation(ch, best);
	}

	//  и если >= 40 инты
	if (caster)
		best = caster;
	if (charmmage)
		best = charmmage;
	if (cler)
		best = cler;
	if (druid)
		best = druid;

	return selectVictimDependingOnGroupFormation(ch, best);
}

int perform_best_mob_attack(CHAR_DATA *ch, int extmode) {
	CHAR_DATA *best;
	int clone_number = 0;
	best = find_best_mob_victim(ch, extmode);

	if (best) {
		// если у игрока стоит олц на зону, в ней его не агрят
		if (best->player_specials->saved.olc_zone == GET_MOB_VNUM(ch) / 100) {
			send_to_char(best, "&GАгромоб, атака остановлена.\r\n");
			return (FALSE);
		}
		if (GET_POS(ch) < POS_FIGHTING && GET_POS(ch) > POS_SLEEPING) {
			act("$n вскочил$g.", FALSE, ch, nullptr, nullptr, TO_ROOM);
			GET_POS(ch) = POS_STANDING;
		}

		if (IS_SET(extmode, KILL_FIGHTING) && best->get_fighting()) {
			if (MOB_FLAGGED(best->get_fighting(), MOB_NOHELPS))
				return (FALSE);
			act("$n вступил$g в битву на стороне $N1.", FALSE, ch, nullptr, best->get_fighting(), TO_ROOM);
		}

		if (IS_SET(extmode, GUARD_ATTACK)) {
			act("'$N - за грехи свои ты заслуживаешь смерти!', сурово проговорил$g $n.",
				FALSE,
				ch,
				nullptr,
				best,
				TO_ROOM);
			act("'Как страж этого города, я намерен$g привести приговор в исполнение немедленно. Защищайся!'",
				FALSE,
				ch,
				nullptr,
				best,
				TO_ROOM);
		}

		if (!IS_NPC(best)) {
			struct follow_type *f;
			// поиск клонов и отработка атаки в клона персонажа
			for (f = best->followers; f; f = f->next)
				if (MOB_FLAGGED(f->follower, MOB_CLONE))
					clone_number++;
			for (f = best->followers; f; f = f->next)
				if (IS_NPC(f->follower) && MOB_FLAGGED(f->follower, MOB_CLONE)
					&& IN_ROOM(f->follower) == IN_ROOM(best)) {
					if (number(0, clone_number) == 1)
						break;
					if ((GET_REAL_INT(ch) < 20) && number(0, clone_number))
						break;
					if (GET_REAL_INT(ch) >= 30)
						break;
					if ((GET_REAL_INT(ch) >= 20)
						&& number(1, 10 + VPOSI((35 - GET_REAL_INT(ch)), 0, 15) * clone_number) <= 10)
						break;
					best = f->follower;
					break;
				}
		}
		if (!attack_best(ch, best) && !ch->get_fighting())
			hit(ch, best, ESkill::SKILL_UNDEF, FightSystem::MAIN_HAND);
		return (TRUE);
	}
	return (FALSE);
}

int perform_best_horde_attack(CHAR_DATA *ch, int extmode) {
	if (perform_best_mob_attack(ch, extmode)) {
		return (TRUE);
	}

	for (const auto vict : world[ch->in_room]->people) {
		if (!IS_NPC(vict) || !MAY_SEE(ch, ch, vict) || MOB_FLAGGED(vict, MOB_PROTECT)) {
			continue;
		}

		if (!SAME_ALIGN(ch, vict)) {
			if (GET_POS(ch) < POS_FIGHTING && GET_POS(ch) > POS_SLEEPING) {
				act("$n вскочил$g.", FALSE, ch, nullptr, nullptr, TO_ROOM);
				GET_POS(ch) = POS_STANDING;
			}

			if (!attack_best(ch, vict) && !ch->get_fighting()) {
				hit(ch, vict, ESkill::SKILL_UNDEF, FightSystem::MAIN_HAND);
			}
			return (TRUE);
		}
	}
	return (FALSE);
}

int perform_mob_switch(CHAR_DATA *ch) {
	CHAR_DATA *best;
	best = find_best_mob_victim(ch, SKIP_HIDING | SKIP_CAMOUFLAGE | SKIP_SNEAKING | CHECK_OPPONENT);

	if (!best)
		return FALSE;

	best = try_protect(best, ch);
	if (best == ch->get_fighting())
		return FALSE;

	// переключаюсь на best
	stop_fighting(ch, FALSE);
	set_fighting(ch, best);
	set_wait(ch, 2, FALSE);

	if (ch->get_skill(SKILL_MIGHTHIT)
		&& check_mighthit_weapon(ch)) {
		SET_AF_BATTLE(ch, EAF_MIGHTHIT);
	} else if (ch->get_skill(SKILL_STUPOR)) {
		SET_AF_BATTLE(ch, SKILL_STUPOR);
	}

	return TRUE;
}

void do_aggressive_mob(CHAR_DATA *ch, int check_sneak) {
	if (!ch || ch->in_room == NOWHERE || !IS_NPC(ch) || !MAY_ATTACK(ch) || AFF_FLAGGED(ch, EAffectFlag::AFF_BLIND)) {
		return;
	}

	int mode = check_sneak ? SKIP_SNEAKING : 0;

	// ****************  Horde
	if (MOB_FLAGGED(ch, MOB_HORDE)) {
		perform_best_horde_attack(ch, mode | SKIP_HIDING | SKIP_CAMOUFLAGE);
		return;
	}

	// ****************  Aggressive Mobs
	if (extra_aggressive(ch, nullptr)) {
		const auto &room = world[ch->in_room];
		for (auto affect_it = room->affected.begin(); affect_it != room->affected.end(); ++affect_it) {
			if (affect_it->get()->type == SPELL_RUNE_LABEL && (affect_it != room->affected.end())) {
				act("$n шаркнул$g несколько раз по светящимся рунам, полностью их уничтожив.",
					FALSE,
					ch,
					nullptr,
					nullptr,
					TO_ROOM | TO_ARENA_LISTEN);
				RoomSpells::removeAffectFromRoom(world[ch->in_room], affect_it);
				break;
			}
		}
		perform_best_mob_attack(ch, mode | SKIP_HIDING | SKIP_CAMOUFLAGE | CHECK_HITS);
		return;
	}
	//Polud стражники
	if (MOB_FLAGGED(ch, MOB_GUARDIAN)) {
		perform_best_mob_attack(ch, SKIP_HIDING | SKIP_CAMOUFLAGE | SKIP_SNEAKING | GUARD_ATTACK);
		return;
	}

	// *****************  Mob Memory
	if (MOB_FLAGGED(ch, MOB_MEMORY) && MEMORY(ch)) {
		CHAR_DATA *victim = nullptr;
		// Find memory in room
		const auto people_copy = world[ch->in_room]->people;
		for (auto vict_i = people_copy.begin(); vict_i != people_copy.end() && !victim; ++vict_i) {
			const auto vict = *vict_i;

			if (IS_NPC(vict)
				|| PRF_FLAGGED(vict, PRF_NOHASSLE)) {
				continue;
			}
			for (memory_rec *names = MEMORY(ch); names && !victim; names = names->next) {
				if (names->id == GET_IDNUM(vict)) {
					if (!MAY_SEE(ch, ch, vict) || !may_kill_here(ch, vict, NoArgument)) {
						continue;
					}
					if (check_sneak) {
						skip_sneaking(vict, ch);
						if (EXTRA_FLAGGED(vict, EXTRA_FAILSNEAK)) {
							AFF_FLAGS(vict).unset(EAffectFlag::AFF_SNEAK);
						}
						if (AFF_FLAGGED(vict, EAffectFlag::AFF_SNEAK))
							continue;
					}
					skip_hiding(vict, ch);
					if (EXTRA_FLAGGED(vict, EXTRA_FAILHIDE)) {
						AFF_FLAGS(vict).unset(EAffectFlag::AFF_HIDE);
					}
					skip_camouflage(vict, ch);
					if (EXTRA_FLAGGED(vict, EXTRA_FAILCAMOUFLAGE)) {
						AFF_FLAGS(vict).unset(EAffectFlag::AFF_CAMOUFLAGE);
					}
					if (CAN_SEE(ch, vict)) {
						victim = vict;
					}
				}
			}
		}

		// Is memory found ?
		if (victim && !CHECK_WAIT(ch)) {
			if (GET_POS(ch) < POS_FIGHTING && GET_POS(ch) > POS_SLEEPING) {
				act("$n вскочил$g.", FALSE, ch, nullptr, nullptr, TO_ROOM);
				GET_POS(ch) = POS_STANDING;
			}
			if (GET_RACE(ch) != NPC_RACE_HUMAN) {
				act("$n вспомнил$g $N3.", FALSE, ch, nullptr, victim, TO_ROOM);
			} else {
				act("'$N - ты пытал$U убить меня ! Попал$U ! Умри !!!', воскликнул$g $n.",
					FALSE, ch, nullptr, victim, TO_ROOM);
			}
			if (!attack_best(ch, victim)) {
				hit(ch, victim, ESkill::SKILL_UNDEF, FightSystem::MAIN_HAND);
			}
			return;
		}
	}

	// ****************  Helper Mobs
	if (MOB_FLAGGED(ch, MOB_HELPER)) {
		perform_best_mob_attack(ch, mode | KILL_FIGHTING | CHECK_HITS);
		return;
	}
}

/**
* Примечание: сам ch после этой функции уже может быть спуржен
* в результате агра на себя кого-то в комнате и начале атаки
* например с глуша.
*/
void do_aggressive_room(CHAR_DATA *ch, int check_sneak) {
	if (!ch || ch->in_room == NOWHERE) {
		return;
	}

	const auto people =
		world[ch->in_room]->people;    // сделать копию people, т. к. оно может измениться в теле цикла и итераторы будут испорчены
	for (const auto &vict: people) {
		// здесь не надо преварително запоминать next_in_room, потому что как раз
		// он то и может быть спуржен по ходу do_aggressive_mob, а вот атакующий нет
		do_aggressive_mob(vict, check_sneak);
	}
}

/**
 * Проверка на наличие в комнате мобов с таким же спешиалом, что и входящий.
 * \param ch - входящий моб
 * \return true - можно войти, false - нельзя
 */
bool allow_enter(ROOM_DATA *room, CHAR_DATA *ch) {
	if (!IS_NPC(ch) || !GET_MOB_SPEC(ch)) {
		return true;
	}

	for (const auto vict : room->people) {
		if (IS_NPC(vict)
			&& GET_MOB_SPEC(vict) == GET_MOB_SPEC(ch)) {
			return false;
		}
	}

	return true;
}

namespace {
OBJ_DATA *create_charmice_box(CHAR_DATA *ch) {
	const auto obj = world_objects.create_blank();

	obj->set_aliases("узелок вещами");
	const std::string descr = std::string("узелок с вещами ") + ch->get_pad(1);
	obj->set_short_description(descr);
	obj->set_description("Туго набитый узел лежит тут.");
	obj->set_ex_description(descr.c_str(), "Кто-то сильно торопился, когда набивал этот узелок.");
	obj->set_PName(0, "узелок");
	obj->set_PName(1, "узелка");
	obj->set_PName(2, "узелку");
	obj->set_PName(3, "узелок");
	obj->set_PName(4, "узелком");
	obj->set_PName(5, "узелке");
	obj->set_sex(ESex::SEX_MALE);
	obj->set_type(OBJ_DATA::ITEM_CONTAINER);
	obj->set_wear_flags(to_underlying(EWearFlag::ITEM_WEAR_TAKE));
	obj->set_weight(1);
	obj->set_cost(1);
	obj->set_rent_off(1);
	obj->set_rent_on(1);
	obj->set_timer(9999);

	obj->set_extra_flag(EExtraFlag::ITEM_NOSELL);
	obj->set_extra_flag(EExtraFlag::ITEM_NOLOCATE);
	obj->set_extra_flag(EExtraFlag::ITEM_NODECAY);
	obj->set_extra_flag(EExtraFlag::ITEM_SWIMMING);
	obj->set_extra_flag(EExtraFlag::ITEM_FLYING);

	return obj.get();
}

void extract_charmice(CHAR_DATA *ch) {
	std::vector<OBJ_DATA *> objects;
	for (int i = 0; i < NUM_WEARS; ++i) {
		if (GET_EQ(ch, i)) {
			OBJ_DATA *obj = unequip_char(ch, i);
			if (obj) {
				remove_otrigger(obj, ch);
				objects.push_back(obj);
			}
		}
	}

	while (ch->carrying) {
		OBJ_DATA *obj = ch->carrying;
		obj_from_char(obj);
		objects.push_back(obj);
	}

	if (!objects.empty()) {
		OBJ_DATA *charmice_box = create_charmice_box(ch);
		for (auto it = objects.begin(); it != objects.end(); ++it) {
			obj_to_obj(*it, charmice_box);
		}
		drop_obj_on_zreset(ch, charmice_box, true, false);
	}

	extract_char(ch, FALSE);
}
}

void mobile_activity(int activity_level, int missed_pulses) {
	int door, max, was_in = -1, activity_lev, i, ch_activity;
	int std_lev = activity_level % PULSE_MOBILE;

	character_list.foreach_on_copy([&](const CHAR_DATA::shared_ptr &ch) {
		if (!IS_MOB(ch)
			|| !ch->in_used_zone()) {
			return;
		}

		i = missed_pulses;
		while (i--) {
			pulse_affect_update(ch.get());
		}

		ch->wait_dec(missed_pulses);
		ch->decreaseSkillsCooldowns(missed_pulses);

		if (GET_PUNCTUAL_WAIT(ch) > 0)
			GET_PUNCTUAL_WAIT(ch) -= missed_pulses;
		else
			GET_PUNCTUAL_WAIT(ch) = 0;

		if (GET_PUNCTUAL_WAIT(ch) < 0)
			GET_PUNCTUAL_WAIT(ch) = 0;

		if (ch->mob_specials.speed <= 0) {
			activity_lev = std_lev;
		} else {
			activity_lev = activity_level % (ch->mob_specials.speed RL_SEC);
		}

		ch_activity = GET_ACTIVITY(ch);

// на случай вызова mobile_activity() не каждый пульс
		// TODO: by WorM а где-то используется это mob_specials.speed ???
		if (ch_activity - activity_lev < missed_pulses && ch_activity - activity_lev >= 0) {
			ch_activity = activity_lev;
		}
		if (ch_activity != activity_lev
			|| (was_in = ch->in_room) == NOWHERE
			|| GET_ROOM_VNUM(ch->in_room) % 100 == 99) {
			return;
		}

		// Examine call for special procedure
		if (MOB_FLAGGED(ch, MOB_SPEC) && !no_specials) {
			if (mob_index[GET_MOB_RNUM(ch)].func == nullptr) {
				log("SYSERR: %s (#%d): Attempting to call non-existing mob function.",
					GET_NAME(ch), GET_MOB_VNUM(ch));
				MOB_FLAGS(ch).unset(MOB_SPEC);
			} else {
				buf2[0] = '\0';
				if ((mob_index[GET_MOB_RNUM(ch)].func)(ch.get(), ch.get(), 0, buf2)) {
					return;    // go to next char
				}
			}
		}
		// Extract free horses
		if (AFF_FLAGGED(ch, EAffectFlag::AFF_HORSE)
			&& MOB_FLAGGED(ch, MOB_MOUNTING)
			&& !ch->has_master()) // если скакун, под седлом но нет хозяина
		{
			act("Возникший как из-под земли цыган ловко вскочил на $n3 и унесся прочь.",
				FALSE,
				ch.get(),
				nullptr,
				nullptr,
				TO_ROOM);
			extract_char(ch.get(), FALSE);

			return;
		}

		// Extract uncharmed mobs
		if (EXTRACT_TIMER(ch) > 0) {
			if (ch->has_master()) {
				EXTRACT_TIMER(ch) = 0;
			} else {
				EXTRACT_TIMER(ch)--;
				if (!EXTRACT_TIMER(ch)) {
					extract_charmice(ch.get());

					return;
				}
			}
		}

		// If the mob has no specproc, do the default actions
		if (ch->get_fighting() ||
			GET_POS(ch) <= POS_STUNNED ||
			GET_WAIT(ch) > 0 ||
			AFF_FLAGGED(ch, EAffectFlag::AFF_CHARM) ||
			AFF_FLAGGED(ch, EAffectFlag::AFF_HOLD) || AFF_FLAGGED(ch, EAffectFlag::AFF_MAGICSTOPFIGHT) ||
			AFF_FLAGGED(ch, EAffectFlag::AFF_STOPFIGHT) || AFF_FLAGGED(ch, EAffectFlag::AFF_SLEEP)) {
			return;
		}

		if (IS_HORSE(ch)) {
			if (GET_POS(ch) < POS_FIGHTING) {
				GET_POS(ch) = POS_STANDING;
			}

			return;
		}

		if (GET_POS(ch) == POS_SLEEPING && GET_DEFAULT_POS(ch) > POS_SLEEPING) {
			GET_POS(ch) = GET_DEFAULT_POS(ch);
			act("$n проснул$u.", FALSE, ch.get(), 0, 0, TO_ROOM);
		}

		if (!AWAKE(ch)) {
			return;
		}

		max = FALSE;
		bool found = false;
		for (const auto vict : world[ch->in_room]->people) {
			if (ch.get() == vict) {
				continue;
			}

			if (vict->get_fighting() == ch.get()) {
				return;        // Mob is under attack
			}

			if (!IS_NPC(vict)
				&& CAN_SEE(ch, vict)) {
				max = TRUE;
			}
		}

		// Mob attemp rest if it is not an angel
		if (!max && !MOB_FLAGGED(ch, MOB_NOREST) &&
			GET_HIT(ch) < GET_REAL_MAX_HIT(ch) && !MOB_FLAGGED(ch, MOB_ANGEL) && !MOB_FLAGGED(ch, MOB_GHOST)
			&& GET_POS(ch) > POS_RESTING) {
			act("$n присел$g отдохнуть.", FALSE, ch.get(), 0, 0, TO_ROOM);
			GET_POS(ch) = POS_RESTING;
		}

		// Mob return to default pos if full rested or if it is an angel
		if ((GET_HIT(ch) >= GET_REAL_MAX_HIT(ch)
			&& GET_POS(ch) != GET_DEFAULT_POS(ch))
			|| ((MOB_FLAGGED(ch, MOB_ANGEL)
				|| MOB_FLAGGED(ch, MOB_GHOST))
				&& GET_POS(ch) != GET_DEFAULT_POS(ch))) {
			switch (GET_DEFAULT_POS(ch)) {
				case POS_STANDING: act("$n поднял$u.", FALSE, ch.get(), 0, 0, TO_ROOM);
					GET_POS(ch) = POS_STANDING;
					break;
				case POS_SITTING: act("$n сел$g.", FALSE, ch.get(), 0, 0, TO_ROOM);
					GET_POS(ch) = POS_SITTING;
					break;
				case POS_RESTING: act("$n присел$g отдохнуть.", FALSE, ch.get(), 0, 0, TO_ROOM);
					GET_POS(ch) = POS_RESTING;
					break;
				case POS_SLEEPING: act("$n уснул$g.", FALSE, ch.get(), 0, 0, TO_ROOM);
					GET_POS(ch) = POS_SLEEPING;
					break;
			}
		}
		// continue, if the mob is an angel
		// если моб ментальная тень или ангел он не должен проявлять активность
		if ((MOB_FLAGGED(ch, MOB_ANGEL))
			|| (MOB_FLAGGED(ch, MOB_GHOST))) {
			return;
		}

		// look at room before moving
		do_aggressive_mob(ch.get(), FALSE);

		// if mob attack something
		if (ch->get_fighting()
			|| GET_WAIT(ch) > 0) {
			return;
		}

		// Scavenger (picking up objects)
		// От одного до трех предметов за раз
		i = number(1, 3);
		while (i) {
			npc_scavenge(ch.get());
			i--;
		}

		if (EXTRACT_TIMER(ch) == 0) {
			//чармисы, собирающиеся уходить - не лутят! (Купала)
			//Niker: LootCR// Start
			//Не уверен, что рассмотрены все случаи, когда нужно снимать флаги с моба
			//Реализация для лута и воровства
			int grab_stuff = FALSE;
			// Looting the corpses

			grab_stuff += npc_loot(ch.get());
			grab_stuff += npc_steal(ch.get());

			if (grab_stuff) {
				MOB_FLAGS(ch).unset(MOB_LIKE_DAY);    //Взял из make_horse
				MOB_FLAGS(ch).unset(MOB_LIKE_NIGHT);
				MOB_FLAGS(ch).unset(MOB_LIKE_FULLMOON);
				MOB_FLAGS(ch).unset(MOB_LIKE_WINTER);
				MOB_FLAGS(ch).unset(MOB_LIKE_SPRING);
				MOB_FLAGS(ch).unset(MOB_LIKE_SUMMER);
				MOB_FLAGS(ch).unset(MOB_LIKE_AUTUMN);
			}
			//Niker: LootCR// End
		}
		npc_wield(ch.get());
		npc_armor(ch.get());

		if (GET_POS(ch) == POS_STANDING && NPC_FLAGGED(ch, NPC_INVIS)) {
			ch->set_affect(EAffectFlag::AFF_INVISIBLE);
		}

		if (GET_POS(ch) == POS_STANDING && NPC_FLAGGED(ch, NPC_MOVEFLY)) {
			ch->set_affect(EAffectFlag::AFF_FLY);
		}

		if (GET_POS(ch) == POS_STANDING && NPC_FLAGGED(ch, NPC_SNEAK)) {
			if (CalcCurrentSkill(ch.get(), SKILL_SNEAK, 0) >= number(0, 100)) {
				ch->set_affect(EAffectFlag::AFF_SNEAK);
			} else {
				ch->remove_affect(EAffectFlag::AFF_SNEAK);
			}
			affect_total(ch.get());
		}

		if (GET_POS(ch) == POS_STANDING && NPC_FLAGGED(ch, NPC_CAMOUFLAGE)) {
			if (CalcCurrentSkill(ch.get(), SKILL_CAMOUFLAGE, 0) >= number(0, 100)) {
				ch->set_affect(EAffectFlag::AFF_CAMOUFLAGE);
			} else {
				ch->remove_affect(EAffectFlag::AFF_CAMOUFLAGE);
			}

			affect_total(ch.get());
		}

		door = BFS_ERROR;

		// Helpers go to some dest
		if (MOB_FLAGGED(ch, MOB_HELPER)
			&& !MOB_FLAGGED(ch, MOB_SENTINEL)
			&& !AFF_FLAGGED(ch, EAffectFlag::AFF_BLIND)
			&& !ch->has_master()
			&& GET_POS(ch) == POS_STANDING) {
			for (found = FALSE, door = 0; door < NUM_OF_DIRS; door++) {
				ROOM_DATA::exit_data_ptr rdata = EXIT(ch, door);
				if (!rdata
					|| rdata->to_room() == NOWHERE
					|| !legal_dir(ch.get(), door, TRUE, FALSE)
					|| (is_room_forbidden(world[rdata->to_room()])
						&& !MOB_FLAGGED(ch, MOB_IGNORE_FORBIDDEN))
					|| IS_DARK(rdata->to_room())
					|| (MOB_FLAGGED(ch, MOB_STAY_ZONE)
						&& world[ch->in_room]->zone_rn != world[rdata->to_room()]->zone_rn)) {
					continue;
				}

				const auto room = world[rdata->to_room()];
				for (auto first : room->people) {
					if (IS_NPC(first)
						&& !AFF_FLAGGED(first, EAffectFlag::AFF_CHARM)
						&& !IS_HORSE(first)
						&& CAN_SEE(ch, first)
						&& first->get_fighting()
						&& SAME_ALIGN(ch, first)) {
						found = TRUE;
						break;
					}
				}

				if (found) {
					break;
				}
			}

			if (!found) {
				door = BFS_ERROR;
			}
		}

		if (GET_DEST(ch) != NOWHERE
			&& GET_POS(ch) > POS_FIGHTING
			&& door == BFS_ERROR) {
			npc_group(ch.get());
			door = npc_walk(ch.get());
		}

		if (MEMORY(ch) && door == BFS_ERROR && GET_POS(ch) > POS_FIGHTING && ch->get_skill(SKILL_TRACK))
			door = npc_track(ch.get());

		if (door == BFS_ALREADY_THERE) {
			do_aggressive_mob(ch.get(), FALSE);
			return;
		}

		if (door == BFS_ERROR) {
			door = number(0, 18);
		}

		// Mob Movement
		if (!MOB_FLAGGED(ch, MOB_SENTINEL)
			&& GET_POS(ch) == POS_STANDING
			&& (door >= 0 && door < NUM_OF_DIRS)
			&& EXIT(ch, door)
			&& EXIT(ch, door)->to_room() != NOWHERE
			&& legal_dir(ch.get(), door, TRUE, FALSE)
			&& (!is_room_forbidden(world[EXIT(ch, door)->to_room()]) || MOB_FLAGGED(ch, MOB_IGNORE_FORBIDDEN))
			&& (!MOB_FLAGGED(ch, MOB_STAY_ZONE)
				|| world[EXIT(ch, door)->to_room()]->zone_rn == world[ch->in_room]->zone_rn)
			&& allow_enter(world[EXIT(ch, door)->to_room()], ch.get())) {
			// После хода нпц уже может не быть, т.к. ушел в дт, я не знаю почему
			// оно не валится на муд.ру, но на цигвине у меня падало стабильно,
			// т.к. в ch уже местами мусор после фри-чара // Krodo
			if (npc_move(ch.get(), door, 1)) {
				npc_group(ch.get());
				npc_groupbattle(ch.get());
			} else {
				return;
			}
		}

		npc_light(ch.get());

		// *****************  Mob Memory
		if (MOB_FLAGGED(ch, MOB_MEMORY)
			&& MEMORY(ch)
			&& GET_POS(ch) > POS_SLEEPING
			&& !AFF_FLAGGED(ch, EAffectFlag::AFF_BLIND)
			&& !ch->get_fighting()) {
			// Find memory in world
			for (auto names = MEMORY(ch); names && (GET_SPELL_MEM(ch, SPELL_SUMMON) > 0
				|| GET_SPELL_MEM(ch, SPELL_RELOCATE) > 0); names = names->next) {
				for (const auto &vict : character_list) {
					if (names->id == GET_IDNUM(vict)
						&& CAN_SEE(ch, vict) && !PRF_FLAGGED(vict, PRF_NOHASSLE)) {
						if (GET_SPELL_MEM(ch, SPELL_SUMMON) > 0) {
							cast_spell(ch.get(), vict.get(), 0, 0, SPELL_SUMMON, SPELL_SUMMON);

							break;
						} else if (GET_SPELL_MEM(ch, SPELL_RELOCATE) > 0) {
							cast_spell(ch.get(), vict.get(), 0, 0, SPELL_RELOCATE, SPELL_RELOCATE);

							break;
						}
					}
				}
			}
		}

		// Add new mobile actions here

		if (was_in != ch->in_room) {
			do_aggressive_room(ch.get(), FALSE);
		}
	});            // end for()
}

// Mob Memory Routines
// 11.07.2002 - у зачармленных мобов не работает механизм памяти на время чарма

// make ch remember victim
void mobRemember(CHAR_DATA *ch, CHAR_DATA *victim) {
	struct timed_type timed{};
	memory_rec *tmp;
	bool present = FALSE;

	if (!IS_NPC(ch) ||
		IS_NPC(victim) ||
		PRF_FLAGGED(victim, PRF_NOHASSLE) ||
		!MOB_FLAGGED(ch, MOB_MEMORY) ||
		AFF_FLAGGED(ch, EAffectFlag::AFF_CHARM))
		return;

	for (tmp = MEMORY(ch); tmp && !present; tmp = tmp->next)
		if (tmp->id == GET_IDNUM(victim)) {
			if (tmp->time > 0)
				tmp->time = time(nullptr) + MOB_MEM_KOEFF * GET_REAL_INT(ch);
			present = TRUE;
		}

	if (!present) {
		CREATE(tmp, 1);
		tmp->next = MEMORY(ch);
		tmp->id = GET_IDNUM(victim);
		tmp->time = time(nullptr) + MOB_MEM_KOEFF * GET_REAL_INT(ch);
		MEMORY(ch) = tmp;
	}

	if (!timed_by_skill(victim, SKILL_HIDETRACK)) {
		timed.skill = SKILL_HIDETRACK;
		timed.time = ch->get_skill(SKILL_TRACK) ? 6 : 3;
		timed_to_char(victim, &timed);
	}
}

// make ch forget victim
void mobForget(CHAR_DATA *ch, CHAR_DATA *victim) {
	memory_rec *curr, *prev = nullptr;

	// Момент спорный, но думаю, что так правильнее
	if (AFF_FLAGGED(ch, EAffectFlag::AFF_CHARM))
		return;

	if (!(curr = MEMORY(ch)))
		return;

	while (curr && curr->id != GET_IDNUM(victim)) {
		prev = curr;
		curr = curr->next;
	}

	if (!curr)
		return;        // person wasn't there at all.

	if (curr == MEMORY(ch))
		MEMORY(ch) = curr->next;
	else
		prev->next = curr->next;

	free(curr);
}

// erase ch's memory
// Можно заметить, что функция вызывается только при extract char/mob
// Удаляется все подряд
void clearMemory(CHAR_DATA *ch) {
	memory_rec *curr, *next;

	curr = MEMORY(ch);

	while (curr) {
		next = curr->next;
		free(curr);
		curr = next;
	}
	MEMORY(ch) = nullptr;
}
//Polud Функция проверяет, является ли моб ch стражником (описан в файле guards.xml)
//и должен ли он сагрить на эту жертву vict
bool guardian_attack(CHAR_DATA *ch, CHAR_DATA *vict) {
	struct mob_guardian tmp_guard;
	int num_wars_vict = 0;

	if (!IS_NPC(ch) || !vict || !MOB_FLAGGED(ch, MOB_GUARDIAN))
		return false;
//на всякий случай проверим, а вдруг моб да подевался куда из списка...
	auto it = guardian_list.find(GET_MOB_VNUM(ch));
	if (it == guardian_list.end())
		return false;

	tmp_guard = guardian_list[GET_MOB_VNUM(ch)];

	if ((tmp_guard.agro_all_agressors && AGRESSOR(vict)) ||
		(tmp_guard.agro_killers && PLR_FLAGGED(vict, PLR_KILLER)))
		return true;

	if (CLAN(vict)) {
		num_wars_vict = Clan::GetClanWars(vict);
		int clan_town_vnum = CLAN(vict)->GetOutRent() / 100; //Polud подскажите мне другой способ определить vnum зоны
		int mob_town_vnum = GET_MOB_VNUM(ch) / 100;          //по vnum комнаты, не перебирая все комнаты и зоны мира
		if (num_wars_vict && num_wars_vict > tmp_guard.max_wars_allow && clan_town_vnum != mob_town_vnum)
			return true;
	}
	if (AGRESSOR(vict))
		for (auto iter = tmp_guard.agro_argressors_in_zones.begin(); iter != tmp_guard.agro_argressors_in_zones.end();
			 iter++) {
			if (*iter == AGRESSOR(vict) / 100) return true;
		}

	return false;
}

// vim: ts=4 sw=4 tw=0 noet syntax=cpp :
