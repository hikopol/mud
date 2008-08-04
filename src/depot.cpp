// $RCSfile$     $Date$     $Revision$
// Copyright (c) 2007 Krodo
// Part of Bylins http://www.mud.ru

#include "depot.hpp"
#include <map>
#include <list>
#include <sstream>
#include <cmath>
#include <bitset>
#include <boost/lexical_cast.hpp>
#include <boost/bind.hpp>

#include "db.h"
#include "handler.h"
#include "utils.h"
#include "comm.h"
#include "auction.h"
#include "exchange.h"
#include "interpreter.h"
#include "screen.h"
#include "char.hpp"

extern SPECIAL(bank);
extern void write_one_object(char **data, OBJ_DATA * object, int location);
extern int can_take_obj(CHAR_DATA * ch, OBJ_DATA * obj);
extern void write_one_object(char **data, OBJ_DATA * object, int location);
extern OBJ_DATA *read_one_object_new(char **data, int *error);

namespace Depot {

// ���� ������������� �������
const int PERS_CHEST_VNUM = 331;
// ���� (��������������� ��� ������������� � renumber_obj_rnum)
int PERS_CHEST_RNUM = -1;
// ������������ ���-�� ������ � ������������ ��������� (������� * 2)
const unsigned int MAX_PERS_SLOTS = 25;

/**
* ��� ������������ ������ ������ � ���������.
*/
class OfflineNode
{
	public:
	OfflineNode() : vnum(0), timer(0), rent_cost(0), uid(0) {};
	int vnum; // ����
	int timer; // ������
	int rent_cost; // ���� ����� � ����
	int uid; // ���������� ���
};

typedef std::list<OBJ_DATA *> ObjListType; // ���, ������
typedef std::list<OfflineNode> TimerListType; // ������ �����, ������������ � ��������

class CharNode
{
	public:
	CharNode() : ch(0), money(0), money_spend(0), buffer_cost(0), need_save(0), cost_per_day(0) {};

// ������
	// ������ � ������������ ��������� ������
	ObjListType pers_online;
	// ��� (������ ����� � ���� ������� �������)
	CHAR_DATA *ch;
// �������
	// ������ � ���������� �������
	TimerListType offline_list;
	// ����� ����+���� ������� (���� ��� ����� ������ ��� ����)
	long money;
	// ������� ���� ��������� �� ����� ���� � �����
	long money_spend;
// ����� ����
	// ������ ��� ������� ������ �����
	double buffer_cost;
	// ��� ����, ����� �� ������ ���� ����� ���� ��� ���������
	std::string name;
	// � ��������� ���������� ������ � ��� ����� ��������� (������� �������� �� � ����)
	// �����/�������� ������, �������� �� �������� �������, ���� ���� � ����
	bool need_save;
	// ����� ��������� ����� ����� � ���� (�������� ������������ ��������� ������),
	// ����� �� ����� ������� � ����������� � ������� �����������
	int cost_per_day;

	void save_online_objs();
	void update_online_item();
	void update_offline_item();
	void reset();
	bool removal_period_cost();

	void take_item(CHAR_DATA *vict, char *arg, int howmany);
	void remove_item(ObjListType::iterator &obj_it, ObjListType &cont, CHAR_DATA *vict);
	bool obj_from_obj_list(char *name, CHAR_DATA *vict);
	void load_online_objs(int file_type, bool reload = 0);
	void online_to_offline(ObjListType &cont);

	int get_cost_per_day() { return cost_per_day; };
	void reset_cost_per_day() { cost_per_day = 0; } ;
	void add_cost_per_day(OBJ_DATA *obj);
	void add_cost_per_day(int amount);
};

typedef std::map<long, CharNode> DepotListType; // ��� ����, ����
DepotListType depot_list; // ������ ������ ��������

/**
* �������� ����� ������������� ��������� (������ ���������� ����).
*/
void remove_pers_file(std::string name)
{
	char filename[MAX_STRING_LENGTH];
	if (get_filename(name.c_str(), filename, PERS_DEPOT_FILE))
		remove(filename);
}

/**
* �������� ������ � ������ ������, ���� ��� ����.
* �������� ������� ������ � ���� ����� �� �����.
*/
void remove_char_entry(long uid, CharNode &node)
{
	// ���� ��� ��� ���-�� ������, ���� ���������� � ���� ��� �����
	if (node.money_spend || node.buffer_cost)
	{
		CHAR_DATA t_victim;
		CHAR_DATA *victim = &t_victim;
		if (load_char(node.name.c_str(), victim) > -1 && GET_UNIQUE(victim) == uid)
		{
			int total_pay = node.money_spend + static_cast<int>(node.buffer_cost);
			add_bank_gold(victim, -total_pay);
			if (get_bank_gold(victim) < 0)
			{
				add_gold(victim, get_bank_gold(victim));
				set_bank_gold(victim, 0);
				if (get_gold(victim) < 0)
					set_gold(victim, 0);
			}
			save_char(victim, NOWHERE);
		}
	}
	node.reset();
	remove_pers_file(node.name);
}

/**
* ������������� ������ ��������. ���� ����� � ������� ����������� �� ���������.
*/
void init_depot()
{
	PERS_CHEST_RNUM = real_object(PERS_CHEST_VNUM);

	const char *depot_file = LIB_DEPOT"depot.db";
	std::ifstream file(depot_file);
	if (!file.is_open())
	{
		log("���������: error open file: %s! (%s %s %d)", depot_file, __FILE__, __func__, __LINE__);
		return;
	}

	std::string buffer;
	while (file >> buffer)
	{
		if (buffer != "<Node>")
		{
			log("���������: ������ ������ <Node>");
			break;
		}
		CharNode tmp_node;
		long uid;
		// ����� ����
		if (!(file >> uid >> tmp_node.money >> tmp_node.money_spend >> tmp_node.buffer_cost))
		{
			log("���������: ������ ������ uid(%ld), money(%ld), money_spend(%ld), buffer_cost(%f).",
			uid, tmp_node.money, tmp_node.money_spend, tmp_node.buffer_cost);
			break;
		}
		// ������ ���������
		file >> buffer;
		if (buffer != "<Objects>")
		{
			log("���������: ������ ������ <Objects>.");
			break;
		}
		while (file >> buffer)
		{
			if (buffer == "</Objects>") break;

			OfflineNode tmp_obj;
			try
			{
				tmp_obj.vnum = boost::lexical_cast<int>(buffer);
			}
			catch(boost::bad_lexical_cast &)
			{
				log("���������: ������ ������ vnum (%s)", buffer.c_str());
				break;
			}
			if (!(file >> tmp_obj.timer >> tmp_obj.rent_cost >> tmp_obj.uid))
			{
				log("���������: ������ ������ timer(%d) rent_cost(%d) uid(%d) (obj vnum: %d).",
					tmp_obj.timer, tmp_obj.rent_cost, tmp_obj.uid, tmp_obj.vnum);
				break;
			}
			// ��������� ������������� ��������� �������� � ���� ��� � ���� � ���� �� ������
			int rnum = real_object(tmp_obj.vnum);
			if (rnum >= 0)
			{
				obj_index[rnum].stored++;
				tmp_node.add_cost_per_day(tmp_obj.rent_cost);
				tmp_node.offline_list.push_back(tmp_obj);
			}
		}
		if (buffer != "</Objects>")
		{
			log("���������: ������ ������ </Objects>.");
			break;
		}
		// �������� ���������� ������������
		file >> buffer;
		if (buffer != "</Node>")
		{
			log("���������: ������ ������ </Node>.");
			break;
		}

		// �������� ������ �� ������ �������� �������� ������ ����� � ��� ����� �� �������� �����,
		// ������ ����� �� ��������, ��� ���� ��� ����� ������ ��� ��� ������ � ������ ���� � ����
		// � �������� ������ �� ����� remove_char_entry - ��� ����� �����

		// ��������� ���� �� ��� ����� ��� ������
		tmp_node.name = GetNameByUnique(uid);
		if (tmp_node.name.empty())
		{
			log("���������: UID %ld - ��������� �� ����������.", uid);
			remove_char_entry(uid, tmp_node);
			continue;
		}
		// ������ ��������� ��������� ������ �����
		if (tmp_node.offline_list.empty() && tmp_node.pers_online.empty())
		{
			log("���������: UID %ld - ������ ���������.", uid);
			remove_char_entry(uid, tmp_node);
			continue;
		}

		name_convert(tmp_node.name);
		depot_list[uid] = tmp_node;
	}
}

/**
* �������� ����� �������� � ����� �������� ����� ����� �������� � ������ ���, ������ ��� ����� ����.
*/
void load_chests()
{
	for (CHAR_DATA *ch = character_list; ch; ch = ch->next)
	{
		if (ch->nr > 0 && ch->nr <= top_of_mobt && mob_index[ch->nr].func == bank)
		{
			OBJ_DATA *pers_chest = read_object(PERS_CHEST_VNUM, VIRTUAL);
			if (!pers_chest) return;
			obj_to_room(pers_chest, ch->in_room);
		}
	}
}

/**
* ������� ����������� ���� ����� ������, �� �����, ������ ��� ����� ��� ������.
*/
int get_object_low_rent(OBJ_DATA *obj)
{
	int rent = GET_OBJ_RENT(obj) > GET_OBJ_RENTEQ(obj) ? GET_OBJ_RENTEQ(obj) : GET_OBJ_RENT(obj);
	return rent;
}

/**
* ���������� ���� ����� � ��������� �� ���������� � ������������.
*/
void CharNode::add_cost_per_day(int amount)
{
	if (amount < 0 || amount > 50000)
	{
		log("���������: ���������� ��������� ����� %i", amount);
		return;
	}
	int over = std::numeric_limits<int>::max() - cost_per_day;
	if (amount > over)
		cost_per_day = std::numeric_limits<int>::max();
	else
		cost_per_day += amount;
}

/**
* ������ add_cost_per_day(int amount) ��� �������� *OBJ_DATA.
*/
void CharNode::add_cost_per_day(OBJ_DATA *obj)
{
	add_cost_per_day(get_object_low_rent(obj));
}

/**
* ��� �������� � save_timedata(), ������ ������ � ����������� ������.
*/
void write_objlist_timedata(const ObjListType &cont, std::ofstream &file)
{
	for (ObjListType::const_iterator obj_it = cont.begin(); obj_it != cont.end(); ++obj_it)
		file << GET_OBJ_VNUM(*obj_it) << " " << GET_OBJ_TIMER(*obj_it) << " "
			<< get_object_low_rent(*obj_it) << " " << GET_OBJ_UID(*obj_it) << "\n";
}

/**
* ���������� ���� ������� � ����� ���� � ����-����� ������ (� ��� ����� �������� �� ������ ������).
*/
void save_timedata()
{
	const char *depot_file = LIB_DEPOT"depot.backup";
	std::ofstream file(depot_file);
	if (!file.is_open())
	{
		log("���������: error open file: %s! (%s %s %d)", depot_file, __FILE__, __func__, __LINE__);
		return;
	}

	for (DepotListType::const_iterator it = depot_list.begin(); it != depot_list.end(); ++it)
	{
		file << "<Node>\n" << it->first << " ";
		// ��� ������ - ����� ����� � ��� �����, ����� ����� �� ����������� ����� ����
		if (it->second.ch)
			file << get_bank_gold(it->second.ch) + get_gold(it->second.ch) << " 0 ";
		else
			file << it->second.money << " " << it->second.money_spend << " ";
		file << it->second.buffer_cost << "\n";

		// ������ ����� ������ �� ������ - �� � �����, ����� �� ���������
		file << "<Objects>\n";
		write_objlist_timedata(it->second.pers_online, file);
		for (TimerListType::const_iterator obj_it = it->second.offline_list.begin();
				obj_it != it->second.offline_list.end(); ++obj_it)
		{
			file << obj_it->vnum << " " << obj_it->timer << " " << obj_it->rent_cost
				<< " " << obj_it->uid << "\n";
		}
		file << "</Objects>\n</Node>\n";
	}
	file.close();
	std::string buffer("cp "LIB_DEPOT"depot.backup "LIB_DEPOT"depot.db");
	system(buffer.c_str());
}

/**
* ������ ����� �� ������ (������ ������ ������).
*/
void write_obj_file(const std::string &name, int file_type, const ObjListType &cont)
{
	char filename[MAX_STRING_LENGTH];
	if (!get_filename(name.c_str(), filename, file_type))
	{
		log("���������: �� ������� ������������� ��� ����� (name: %s, filename: %s) (%s %s %d).",
			name.c_str(), filename, __FILE__, __func__, __LINE__);
		return;
	}
	// ��� ������ ������ ������ ������� ����, ����� �� ������� �������� � ����
	if (cont.empty())
	{
		remove(filename);
		return;
	}

	std::ofstream file(filename);
	if (!file.is_open())
	{
		log("���������: error open file: %s! (%s %s %d)", filename, __FILE__, __func__, __LINE__);
		return;
	}
	file << "* Items file\n";
	for (ObjListType::const_iterator obj_it = cont.begin(); obj_it != cont.end(); ++obj_it)
	{
		char databuf[MAX_STRING_LENGTH];
		char *data = databuf;
		write_one_object(&data, *obj_it, 0);
		file << databuf;
	}
	file << "\n$\n$\n";
	file.close();
}

/**
* ���������� ������������� ��������� ����������� ������ � ����. ��������� ����� �����.
*/
void CharNode::save_online_objs()
{
	if (need_save)
	{
		write_obj_file(name, PERS_DEPOT_FILE, pers_online);
		need_save = false;
	}
}

/**
* ���������� ���� ������ ����� ���������� ������� ��������� � �����.
*/
void save_all_online_objs()
{
	for (DepotListType::iterator it = depot_list.begin(); it != depot_list.end(); ++it)
		it->second.save_online_objs();
}

/**
* ������ �������� � ������ ������� � ����������� � �����, ���� ��� ������ � �������� ����� �����.
*/
void CharNode::update_online_item()
{
	for (ObjListType::iterator obj_it = pers_online.begin(); obj_it != pers_online.end(); )
	{
		GET_OBJ_TIMER(*obj_it)--;
		if (GET_OBJ_TIMER(*obj_it) <= 0)
		{
			if (ch)
				send_to_char(ch, "[������������ ���������]: %s'%s ��������%s � ����'%s\r\n",
					CCIRED(ch, C_NRM), (*obj_it)->short_description,
					GET_OBJ_SUF_2((*obj_it)), CCNRM(ch, C_NRM));
			// �������� ����� �� cost_per_day ����� �� ����, ������ ��� ��� ��� ��������
			extract_obj(*obj_it);
			pers_online.erase(obj_it++);
			need_save = true;
		}
		else
			++obj_it;
	}
}

/**
* ������ ����� �� ������ � �������� � ��������� ����� ��� �������� �����.
* \return true - ����� ������� ������ � ������ ��������
*/
bool CharNode::removal_period_cost()
{
	double i;
	buffer_cost += (static_cast<double>(cost_per_day) / SECS_PER_MUD_DAY);
	modf(buffer_cost, &i);
	if (i)
	{
		int diff = static_cast<int> (i);
		money -= diff;
		money_spend += diff;
		buffer_cost -= i;
	}
	return (money < 0) ? true : false;
}

/**
* ������ �������� �� ���� �������, �������� ��� ������� �������� (� �����������).
*/
void update_timers()
{
	for (DepotListType::iterator it = depot_list.begin(); it != depot_list.end(); )
	{
		class CharNode & node = it->second;
		// �������� ����� � ������������� �� �� ���� ���������� ������� ��������
		node.reset_cost_per_day();
		// ����������/����������� ������
		if (node.ch)
			node.update_online_item();
		else
			node.update_offline_item();
		// ������ ����� � ���� �����, ���� ����� ��� �� �������
		if (node.get_cost_per_day() && node.removal_period_cost())
		{
			log("���������: UID %ld (%s) - ������ ������� ��-�� �������� ����� �� �����.",
				it->first, node.name.c_str());
			remove_char_entry(it->first, it->second);
			depot_list.erase(it++);
		}
		else
			++it;
	}
}

/**
* ������ �������� � ������� ������� � �������� ����� �����.
*/
void CharNode::update_offline_item()
{
	for (TimerListType::iterator obj_it = offline_list.begin(); obj_it != offline_list.end(); )
	{
		--(obj_it->timer);
		if (obj_it->timer <= 0)
		{
			// ������ ������ � ����
			int rnum = real_object(obj_it->vnum);
			if (rnum >= 0)
			obj_index[rnum].stored--;
			// �������� ����� �� cost_per_day ����� �� ����, ������ ��� ��� ��� ��������
			offline_list.erase(obj_it++);
		}
		else
		{
			add_cost_per_day(obj_it->rent_cost);
			++obj_it;
		}
	}
}

/**
* �������� ���� ������� ����� � ����� ����� � �������. ���� ������ ��������� �� ���������.
* ���� ��� ���������� ����� ������ ����� �� ����������, ������ ��� �� ������ ����� (������).
*/
void CharNode::reset()
{
	for (ObjListType::iterator obj_it = pers_online.begin(); obj_it != pers_online.end(); ++obj_it)
		extract_obj(*obj_it);
	pers_online.clear();

	// ��� ����� ������� ���������� ��������� ���� � ���� � ����� ��� �������� ��� �����
	for (TimerListType::iterator obj = offline_list.begin(); obj != offline_list.end(); ++obj)
	{
		int rnum = real_object(obj->vnum);
		if (rnum >= 0)
			obj_index[rnum].stored--;
	}
	offline_list.clear();

	reset_cost_per_day();
	buffer_cost = 0;
	money = 0;
	money_spend = 0;
}

/**
* ��������, �������� �� obj ������������ ����������.
* \return 0 - �� ��������, 1- ��������.
*/
bool is_depot(OBJ_DATA *obj)
{
	if (obj->item_number == PERS_CHEST_RNUM)
		return true;
	else
		return false;
}

/**
* ���������� ���������� �������� ��� ������� ���������.
*/
void print_obj(std::stringstream &out, OBJ_DATA *obj, int count)
{
	out << obj->short_description;
	if (count > 1)
		out << " [" << count << "]";
	out << " [" << get_object_low_rent(obj) << " "
		<< desc_count(get_object_low_rent(obj), WHAT_MONEYa) << "]\r\n";
}

/**
* ������ ���-�� ������ ��� ������ � ������������ ��������� � ������ ����� ����.
*/
unsigned int get_max_pers_slots(CHAR_DATA *ch)
{
	if (GET_CLASS(ch) == CLASS_DRUID)
		return MAX_PERS_SLOTS * 2;
	else
		return MAX_PERS_SLOTS;
}

/**
* ��� ���������� show_depot - ����� ������ ��������� ���������.
* ���������� �� ������� � ����������� ���������� ���������.
* � ������ ������ ch � money ���� ���������, ����� ����� ���� ����� �������� � ����� ���������.
*/
std::string print_obj_list(CHAR_DATA * ch, ObjListType &cont, const std::string &chest_name, int money)
{
	int rent_per_day = 0;
	std::stringstream out;

	cont.sort(
		boost::bind(std::less<char *>(),
			boost::bind(&obj_data::name, _1),
			boost::bind(&obj_data::name, _2)));

	ObjListType::const_iterator prev_obj_it = cont.end();
	int count = 0;
	bool found = 0;
	for(ObjListType::const_iterator obj_it = cont.begin(); obj_it != cont.end(); ++obj_it)
	{
		if (prev_obj_it == cont.end())
		{
			prev_obj_it = obj_it;
			count = 1;
		}
		else if (!equal_obj(*obj_it, *prev_obj_it))
		{
			print_obj(out, *prev_obj_it, count);
			prev_obj_it = obj_it;
			count = 1;
		}
		else
			count++;
		rent_per_day += get_object_low_rent(*obj_it);
		found = true;
	}
	if (prev_obj_it != cont.end() && count)
		print_obj(out, *prev_obj_it, count);
	if (!found)
		out << "� ������ ������ ��������� ��������� �����.\r\n";

	std::stringstream head;
	int expired = rent_per_day ? (money / rent_per_day) : 0;
	head << CCWHT(ch, C_NRM) << chest_name
		<< "�����: " << cont.size()
		<< ", �������� ����: " << get_max_pers_slots(ch) - cont.size()
		<< ", ����� � ����: " << rent_per_day << " " << desc_count(rent_per_day, WHAT_MONEYa);
	if (rent_per_day)
		head << ", ����� �� " << expired << " " << desc_count(expired, WHAT_DAY);
	else
		head << ", ����� �� ����� ����� ����";
	head << CCNRM(ch, C_NRM) << ".\r\n";

	return (head.str() + out.str());
}

/**
* ����� ��������� ��������� ��� �������� ������, ����� �� ��������.
* ������� ��������� �� ������� ��������, ��� ���� � �����-������� �� �� ����� (��� �������).
* \param ch - ����������� � ����������� �� ������� ���� ������, ����� ����� �������.
*/
DepotListType::iterator create_depot(long uid, CHAR_DATA *ch = 0)
{
	DepotListType::iterator it = depot_list.find(uid);
	if (it == depot_list.end())
	{
		CharNode tmp_node;
		// � ������ ���������� ���� (���� ������ ��� �������) ch ��������� �������,
		// � ��� ��������� ����� ���
		tmp_node.ch = ch;
		tmp_node.name = GetNameByUnique(uid);
		if (!tmp_node.name.empty())
		{
			depot_list[uid] = tmp_node;
			it = depot_list.find(uid);
		}
	}
	return it;
}

/**
* ������� ������������ ��������� ������ ��������� ����������.
*/
void show_depot(CHAR_DATA * ch, OBJ_DATA * obj)
{
	if (IS_NPC(ch)) return;
	if (IS_IMMORTAL(ch))
	{
		send_to_char("� ��� ��������� ����������...\r\n" , ch);
		return;
	}
	if (RENTABLE(ch))
	{
		send_to_char(ch, "%s��������� ���������� � ����� � ������� ����������.%s\r\n",
			CCIRED(ch, C_NRM), CCNRM(ch, C_NRM));
		return;
	}

	DepotListType::iterator it = create_depot(GET_UNIQUE(ch), ch);
	if (it == depot_list.end())
	{
		send_to_char("��������, ������� �����...\r\n" , ch);
		log("���������: UID %d, name: %s - ��������� ������������ ��� ���������.", GET_UNIQUE(ch), GET_NAME(ch));
		return;
	}

	std::string out;
	out = print_obj_list(ch, it->second.pers_online, "���� ������������ ���������:\r\n", (get_gold(ch) + get_bank_gold(ch)));
	page_string(ch->desc, out, 1);
}

/**
* � ������ ������������ ����� �� ����� - ������ ������� �����, ��������� ���������� ���� �� ����.
* �� ����� ��� �������� ������������� ��� ������, �.�. �������� ���� � ���� ����� ���.
*/
void put_gold_chest(CHAR_DATA *ch, OBJ_DATA *obj)
{
	if (GET_OBJ_TYPE(obj) != ITEM_MONEY) return;

	long gold = GET_OBJ_VAL(obj, 0);
	if ((get_bank_gold(ch) + gold) < 0)
	{
		long over = std::numeric_limits<long>::max() - get_bank_gold(ch);
		add_bank_gold(ch, over);
		gold -= over;
		add_gold(ch, gold);
		obj_from_char(obj);
		extract_obj(obj);
		send_to_char(ch, "�� ������� ������� ������ %ld %s.\r\n",
			over, desc_count(over, WHAT_MONEYu));
	}
	else
	{
		add_bank_gold(ch, gold);
		obj_from_char(obj);
		extract_obj(obj);
		send_to_char(ch, "�� ������� %ld %s.\r\n", gold, desc_count(gold, WHAT_MONEYu));
	}
}

/**
* �������� ����������� �������� ������ � ���������.
* FIXME � ������� ��������.
*/
bool can_put_chest(CHAR_DATA *ch, OBJ_DATA *obj)
{
	// depot_log("can_put_chest: %s, %s", GET_NAME(ch), GET_OBJ_PNAME(obj, 0));
	if (OBJ_FLAGGED(obj, ITEM_ZONEDECAY)
		|| OBJ_FLAGGED(obj, ITEM_REPOP_DECAY)
		|| OBJ_FLAGGED(obj, ITEM_NOSELL)
		|| OBJ_FLAGGED(obj, ITEM_DECAY)
		|| OBJ_FLAGGED(obj, ITEM_NORENT)
		|| GET_OBJ_TYPE(obj) == ITEM_KEY
		|| GET_OBJ_RENT(obj) < 0
		|| GET_OBJ_RNUM(obj) <= NOTHING)
	{
		send_to_char(ch, "��������� ���� �������� �������� %s � ���������.\r\n", OBJ_PAD(obj, 3));
		return 0;
	}
	else if (GET_OBJ_TYPE(obj) == ITEM_CONTAINER && obj->contains)
	{
		send_to_char(ch, "� %s ���-�� �����.\r\n", OBJ_PAD(obj, 5));
		return 0;
	}
	return 1;
}

/**
* ������ ������ � ��������� (����� �������� �����), ������ ��������� �� ���� � �����.
*/
bool put_depot(CHAR_DATA *ch, OBJ_DATA *obj)
{
	if (IS_NPC(ch)) return 0;
	if (IS_IMMORTAL(ch))
	{
		send_to_char("� ��� ��������� ����������...\r\n" , ch);
		return 0;
	}
	if (RENTABLE(ch))
	{
		send_to_char(ch, "%s��������� ���������� � ����� � ������� ����������.%s\r\n",
			CCIRED(ch, C_NRM), CCNRM(ch, C_NRM));
		return 0;
	}

	if (GET_OBJ_TYPE(obj) == ITEM_MONEY)
	{
		put_gold_chest(ch, obj);
		return 1;
	}
	// � ������ ������ ���� �� �� ����� ������ �����-�� ���������� ������ - ��� �� ������ �������
	if (!can_put_chest(ch, obj)) return 1;

	DepotListType::iterator it = create_depot(GET_UNIQUE(ch), ch);
	if (it == depot_list.end())
	{
		send_to_char("��������, ������� �����...\r\n" , ch);
		log("���������: UID %d, name: %s - ��������� ������������ ��� ���������.", GET_UNIQUE(ch), GET_NAME(ch));
		return 0;
	}

	if (it->second.pers_online.size() >= get_max_pers_slots(ch))
	{
		send_to_char("� ����� ��������� ������ �� �������� ����� :(.\r\n" , ch);
		return 0;
	}
	if (!get_bank_gold(ch) && !get_gold(ch))
	{
		send_to_char(ch,
			"� ��� ���� ������ ��� �����, ��� �� ����������� �������������� �� �������� �����?\r\n",
			OBJ_PAD(obj, 5));
		return 0;
	}

	it->second.pers_online.push_front(obj);
	it->second.need_save = true;

	act("�� �������� $o3 � ������������ ���������.", FALSE, ch, obj, 0, TO_CHAR);
	act("$n �������$g $o3 � ������������ ���������.", TRUE, ch, obj, 0, TO_ROOM);

	obj_from_char(obj);
	check_auction(NULL, obj);
	OBJ_DATA *temp;
	REMOVE_FROM_LIST(obj, object_list, next);

	return 1;
}

/**
* ������ ����-�� �� ������������� ���������.
*/
void take_depot(CHAR_DATA *vict, char *arg, int howmany)
{
	if (IS_NPC(vict)) return;
	if (IS_IMMORTAL(vict))
	{
		send_to_char("� ��� ��������� ����������...\r\n" , vict);
		return;
	}
	if (RENTABLE(vict))
	{
		send_to_char(vict, "%s��������� ���������� � ����� � ������� ����������.%s\r\n",
			CCIRED(vict, C_NRM), CCNRM(vict, C_NRM));
		return;
	}

	DepotListType::iterator it = depot_list.find(GET_UNIQUE(vict));
	if (it == depot_list.end())
	{
		send_to_char("� ������ ������ ���� ��������� ��������� �����.\r\n", vict);
		return;
	}

	it->second.take_item(vict, arg, howmany);
}

/**
* ����� ������ �� ���������.
*/
void CharNode::remove_item(ObjListType::iterator &obj_it, ObjListType &cont, CHAR_DATA *vict)
{
	(*obj_it)->next = object_list;
	object_list = *obj_it;
	obj_to_char(*obj_it, vict);
	act("�� ����� $o3 �� ������������� ���������.", FALSE, vict, *obj_it, 0, TO_CHAR);
	act("$n ����$g $o3 �� ������������� ���������.", TRUE, vict, *obj_it, 0, TO_ROOM);
	cont.erase(obj_it++);
	need_save = true;
}

/**
* ����� ������ � ���������� (�� ������� �������), ������� �� ��� ��.
*/
bool CharNode::obj_from_obj_list(char *name, CHAR_DATA *vict)
{
	char tmpname[MAX_INPUT_LENGTH];
	char *tmp = tmpname;
	strcpy(tmp, name);

	ObjListType &cont = pers_online;

	int j = 0, number;
	if (!(number = get_number(&tmp))) return false;

	for (ObjListType::iterator obj_it = cont.begin(); obj_it != cont.end() && (j <= number); ++obj_it)
	{
		if (isname(tmp, (*obj_it)->name) && ++j == number)
		{
			remove_item(obj_it, cont, vict);
			return true;
		}
	}
	return false;
}

/**
* ����� ������ � ��������� � �� ������.
*/
void CharNode::take_item(CHAR_DATA *vict, char *arg, int howmany)
{
	ObjListType &cont = pers_online;

	int obj_dotmode = find_all_dots(arg);
	if (obj_dotmode == FIND_INDIV)
	{
		bool result = obj_from_obj_list(arg, vict);
		if (!result)
		{
			send_to_char(vict, "�� �� ������ '%s' � ���������.\r\n", arg);
			return;
		}
		while (result && --howmany)
			result = obj_from_obj_list(arg, vict);
	}
	else
	{
		if (obj_dotmode == FIND_ALLDOT && !*arg)
		{
			send_to_char("����� ��� \"���\" ?\r\n", vict);
			return;
		}
		bool found = 0;
		for (ObjListType::iterator obj_list_it = cont.begin(); obj_list_it != cont.end(); )
		{
			if (obj_dotmode == FIND_ALL || isname(arg, (*obj_list_it)->name))
			{
				// ����� ������ ���� ����� ������� �� �������� ����.��� ������
				if (!can_take_obj(vict, *obj_list_it)) return;
				found = 1;
				remove_item(obj_list_it, cont, vict);
			}
			else
				++obj_list_it;
		}

		if (!found)
		{
			send_to_char(vict, "�� �� ������ ������ �������� �� '%s' � ���������.\r\n", arg);
			return;
		}
	}
}

/**
* ����� �� ���� � ����, �� � ������ ������������� ��������� ������, ��� ������ ����� ������.
* ��� ������� ������� �� ����, ��� ���������� ������������� ����� � ������� ������.
*/
int get_total_cost_per_day(CHAR_DATA *ch)
{
	DepotListType::iterator it = depot_list.find(GET_UNIQUE(ch));
	if (it != depot_list.end())
	{
		int cost = 0;
		if (!it->second.pers_online.empty())
			for (ObjListType::const_iterator obj_it = it->second.pers_online.begin();
				 obj_it != it->second.pers_online.end(); ++obj_it)
			{
				cost += get_object_low_rent(*obj_it);
			}
		cost += it->second.get_cost_per_day();
		return cost;
	}
	return 0;
}


/**
* ����� ���� � show stats.
* TODO: �������� ����� ��������, ����� ��� ������� ������������� ������������ ��������.
*/
void show_stats(CHAR_DATA *ch)
{
	std::stringstream out;
	int pers_count = 0, offline_count = 0;
	for (DepotListType::iterator it = depot_list.begin(); it != depot_list.end(); ++it)
	{
		pers_count += it->second.pers_online.size();
		offline_count += it->second.offline_list.size();
	}

	out << "  ��������: " << depot_list.size() << ", "
		<< "� ������������: " << pers_count << ", "
		<< "� ��������: " << offline_count << "\r\n";
	send_to_char(out.str().c_str(), ch);
}

/**
* ���� ������ ������ � ������ ������. FIXME: ����-���� ������ ��������� � ���� ����.
* \param reload - ��� ������� ������ ����, �� ��������� ����
* (���������� ��������� �� � ������� ������� ��� ����� ��� ���).
*/
void CharNode::load_online_objs(int file_type, bool reload)
{
	if (!reload && offline_list.empty()) return;

	char filename[MAX_STRING_LENGTH];
	if (!get_filename(name.c_str(), filename, file_type))
	{
		log("���������: �� ������� ������������� ��� ����� (name: %s, filename: %s) (%s %s %d).",
			name.c_str(), filename, __FILE__, __func__, __LINE__);
		return;
	}

	FILE *fl = fopen(filename, "r+b");
	if (!fl) return;

	fseek(fl, 0L, SEEK_END);
	int fsize = ftell(fl);
	if (!fsize)
	{
		fclose(fl);
		log("���������: ������ ���� ��������� (%s).", filename);
		return;
	}
	char *databuf = new char [fsize + 1];

	fseek(fl, 0L, SEEK_SET);
	if (!fread(databuf, fsize, 1, fl) || ferror(fl) || !databuf)
	{
		fclose(fl);
		log("���������: ������ ������ ����� ��������� (%s).", filename);
		return;
	}
	fclose(fl);

	char *data = databuf;
	*(data + fsize) = '\0';
	int error = 0;
	OBJ_DATA *obj;

	for (fsize = 0; *data && *data != '$'; fsize++)
	{
		if (!(obj = read_one_object_new(&data, &error)))
		{
			if (error)
				log("���������: ������ ������ �������� (%s, error: %d).", filename, error);
			continue;
		}
		if (!reload)
		{
			// ������ �������� �� ������� �������� � ��������� ������������ ������
			TimerListType::iterator obj_it = std::find_if(offline_list.begin(), offline_list.end(),
				boost::bind(std::equal_to<long>(),
				 boost::bind(&OfflineNode::uid, _1), GET_OBJ_UID(obj)));
			if (obj_it != offline_list.end() && obj_it->vnum == GET_OBJ_VNUM(obj))
			{
				GET_OBJ_TIMER(obj) = obj_it->timer;
				// ���� ��������� ���� � ���� �� ������, ���� � ���� ������ � ����
				// ������������� � read_one_object_new ����� read_object
				int rnum = real_object(GET_OBJ_VNUM(obj));
				if (rnum >= 0)
					obj_index[rnum].stored--;
			}
			else
			{
				extract_obj(obj);
				continue;
			}
		}
		// ��� ������� �� ������ �� �������, � ������ ���, ��� ����,
		// ���� � ���� � ��� ������������� ��� ������ ������, � �� ������ �� � �� ����

		pers_online.push_front(obj);
		// ������� �� �� ����������� �����, � ������� ��� ���������� ��� �� ������ ������ �� �����
		OBJ_DATA *temp;
		REMOVE_FROM_LIST(obj, object_list, next);
	}
	delete [] databuf;
	offline_list.clear();
	reset_cost_per_day();
}

/**
* ���� ���� � ���� - ������ �� ���������, ���� ������, ���������� � ���-�� ������ �����, ������� ������� � ������.
*/
void enter_char(CHAR_DATA *ch)
{
	DepotListType::iterator it = depot_list.find(GET_UNIQUE(ch));
	if (it != depot_list.end())
	{
		// ������� �����, ���� ���-�� ���� ��������� �� �����
		if (it->second.money_spend > 0)
		{
			add_bank_gold(ch, -(it->second.money_spend));
			if (get_bank_gold(ch) < 0)
			{
				add_gold(ch, get_bank_gold(ch));
				set_bank_gold(ch, 0);
				// ���� �������, ��� ����� �� ������, ������ ��� ������ �������� ��� ������ ��
				// ������ � ���������, � ��������� ��� � �� ��� �������� ��� ���-�� �����
				// ������� �� ������ ������, ���� ���, ��� �������� �������� ��� �����
				if (get_gold(ch) < 0)
				{
					set_gold(ch, 0);
					it->second.reset();
					// ���� ������� ����� ��� ������ �� ������ �����,
					// ���� ���� �� ����� ������� ����������� �� ���� ����
				}
			}

			send_to_char(ch, "%s���������: �� ����� ������ ���������� �������� %d %s.%s\r\n\r\n",
				CCWHT(ch, C_NRM), it->second.money_spend, desc_count(it->second.money_spend,
				WHAT_MONEYa), CCNRM(ch, C_NRM));
		}
		// ������ ���������, ��������� ��� ��� ����� ��� ������ ���
		it->second.load_online_objs(PERS_DEPOT_FILE);
		// ��������� ����������� ����� � ������������ ch ��� ������ ����� ������
		it->second.money = 0;
		it->second.money_spend = 0;
		it->second.offline_list.clear();
		it->second.ch = ch;
	}
}

/**
* ������� ������ ������ � �������.
*/
void CharNode::online_to_offline(ObjListType &cont)
{
	for (ObjListType::const_iterator obj_it = cont.begin(); obj_it != cont.end(); ++obj_it)
	{
		OfflineNode tmp_obj;
		tmp_obj.vnum = GET_OBJ_VNUM(*obj_it);
		tmp_obj.timer = GET_OBJ_TIMER(*obj_it);
		tmp_obj.rent_cost = get_object_low_rent(*obj_it);
		tmp_obj.uid = GET_OBJ_UID(*obj_it);
		offline_list.push_back(tmp_obj);
		extract_obj(*obj_it);
		// ������� ������������ ��������� � ����� �����
		add_cost_per_day(tmp_obj.rent_cost);
		// �� ����.� ���� � ���� ��� ������ � �����
		int rnum = real_object(tmp_obj.vnum);
		if (rnum >= 0)
			obj_index[rnum].stored++;
	}
	cont.clear();
}

/**
* �������� ������ ������ � ���������� � ������ ���������� ����� ����� ���.
*/
void renumber_obj_rnum(int rnum)
{
	for (DepotListType::iterator it = depot_list.begin(); it != depot_list.end(); ++it)
		for (ObjListType::iterator obj_it = it->second.pers_online.begin(); obj_it != it->second.pers_online.end(); ++obj_it)
			if (GET_OBJ_RNUM(*obj_it) >= rnum)
				GET_OBJ_RNUM(*obj_it)++;
}

/**
* ����� ���� �� ���� - ������� �������� � ������� (���� � �����, ����-����� ��� ��, '�����').
*/
void exit_char(CHAR_DATA *ch)
{
	DepotListType::iterator it = depot_list.find(GET_UNIQUE(ch));
	if (it != depot_list.end())
	{
		// ��� ����� ������� ���� ������ �� ����, �.�. ���� ������� ����� � ��� �� �����
		// � ����� � ������� ����� ������ ������ ����� ������ � ��� ���������
		it->second.save_online_objs();

		it->second.online_to_offline(it->second.pers_online);
		it->second.ch = 0;
		it->second.money = get_bank_gold(ch) + get_gold(ch);
		it->second.money_spend = 0;
	}
}

/**
* ������ ������ ���� �� ������ ������.
*/
void reload_char(long uid, CHAR_DATA *ch)
{
	DepotListType::iterator it = create_depot(uid);
	if (it == depot_list.end())
	{
		log("���������: UID %ld - ��������� ������������ ��� ���������.", uid);
		return;
	}
	// ������� �������� ���, ��� ����
	it->second.reset();

	// ����� ���� ���� �������� ��� �����, ����� ��� ����� ��� ��������� (����� ��������� � exit_char)
	CHAR_DATA *vict = 0, *t_vict = 0;
	DESCRIPTOR_DATA *d = DescByUID(uid);
	if (d)
		vict = d->character; // ��� ������
	else
	{
		// ��� �������������� �������
		t_vict = new CHAR_DATA; // TODO: ���������� �� ����
		if (load_char(it->second.name.c_str(), t_vict) < 0)
		{
			// ������ �� ���������� �������� ����� �������� � do_reboot
			send_to_char(ch, "������������ ��� ��������� (%s).\r\n", it->second.name.c_str());
			delete t_vict;
			t_vict = 0;
		}
		vict = t_vict;
	}
	if (vict)
	{
		// ������ ��� ����� �����: ������ ���. ��������� ��� ������� ������� ���� ��� ����� ����,
		// ������� �� ������ ������ ��� ������� ������ ������, ����� ���� ���������� ��� � �������
		it->second.load_online_objs(PERS_DEPOT_FILE, 1);
		// ���� ���� ������� � ��������
		if (!d) exit_char(vict);
	}
	if (t_vict) delete t_vict;

	sprintf(buf, "Depot: %s reload items for %s.", GET_NAME(ch), it->second.name.c_str());
	mudlog(buf, DEF, MAX(LVL_IMMORT, GET_INVIS_LEV(ch)), SYSLOG, TRUE);
	imm_log(buf);
}

/**
* ���� ������ � ������������ ����������.
* \param count - ���������� ���-�� ��������� ������������ ������ ����� ������� �� ��������� ���-������.
*/
int print_spell_locate_object(CHAR_DATA *ch, int count, std::string name)
{
	for (DepotListType::iterator it = depot_list.begin(); it != depot_list.end(); ++it)
	{
		for (ObjListType::iterator obj_it = it->second.pers_online.begin(); obj_it != it->second.pers_online.end(); ++obj_it)
		{
			if (number(1, 100) > (40 + MAX((GET_REAL_INT(ch) - 25) * 2, 0)))
				continue;
			if (!isname(name.c_str(), (*obj_it)->name) || OBJ_FLAGGED((*obj_it), ITEM_NOLOCATE))
				continue;

			sprintf(buf, "%s ��������� � ����-�� � ������������ ���������.\r\n", (*obj_it)->short_description);
			CAP(buf);
			send_to_char(buf, ch);

			if (--count <= 0)
				return count;
		}
	}
	return count;
}

} // namespace Depot
