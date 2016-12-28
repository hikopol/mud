#include "bonus.command.parser.hpp"

#include "interpreter.h"
#include "utils.h"

#include <sstream>

namespace Bonus
{
	const char *USAGE_MESSAGE = "��������� �������:\r\n����� <�������|�������|��������> [���������|����|����] [�����]";

	class StringStreamFinalizer
	{
	public:
		StringStreamFinalizer(std::string& destination) : m_destination(destination) {}
		~StringStreamFinalizer() { m_destination = m_stream.str(); }

		template <typename Operand>
		StringStreamFinalizer& operator<<(const Operand& operand);

	private:
		std::stringstream m_stream;
		std::string& m_destination;
	};

	template <typename Operand>
	StringStreamFinalizer& StringStreamFinalizer::operator<<(const Operand& operand)
	{
		m_stream << operand;
		return *this;
	}

	ArgumentsParser::ArgumentsParser(const char* arguments, EBonusType type, int duration):
		m_result(ER_ERROR),
		m_bonus_time(duration),
		m_bonus_multiplier(1),
		m_bonus_type(type)
	{
		parse_arguments(arguments);
	}

	void ArgumentsParser::parse()
	{
		std::stringstream out;
		out << "*** ����������� ";

		StringStreamFinalizer error_message(m_error_message);
		if (!isname(m_first_argument, "������� ������� ��������"))
		{
			error_message << USAGE_MESSAGE << "\r\n";
			return;
		}

		StringStreamFinalizer broadcast_message(m_broadcast_message);
		if (is_abbrev(m_first_argument.c_str(), "��������"))
		{
			broadcast_message << "����� ��� �������.\r\n";
			m_result = ER_STOP;
			return;
		}

		if (m_first_argument.empty())
		{
			error_message << USAGE_MESSAGE << "\r\n";
			return;
		}

		if (!m_second_argument.empty()
			&& !isname(m_second_argument, "��������� ���� ����"))
		{
			error_message << "��� ������ ����� ���� &W���������&n, &W����&n ��� &W����&n.\r\n";
			return;
		}

		if (!m_arguments_remainder.empty())
		{
			m_bonus_time = atol(m_arguments_remainder.c_str());
		}

		if (m_bonus_time < 1
			|| m_bonus_time > 60)
		{
			error_message << "��������� ��������� ��������: �� 1 �� 60 ������� �����.\r\n";
			return;
		}

		if (is_abbrev(m_first_argument.c_str(), "�������"))
		{
			out << "������� �����";
			m_bonus_multiplier = 2;
		}
		else if (is_abbrev(m_first_argument.c_str(), "�������"))
		{
			out << "������� �����";
			m_bonus_multiplier = 3;
		}
		else
		{
			// logic error.
		}

		if (is_abbrev(m_second_argument.c_str(), "���������"))
		{
			out << " ���������� �����";
			m_bonus_type = BONUS_WEAPON_EXP;
		}
		else if (is_abbrev(m_second_argument.c_str(), "����"))
		{
			out << " �����";
			m_bonus_type = BONUS_EXP;
		}
		else if (is_abbrev(m_second_argument.c_str(), "����"))
		{
			out << " ������������ �����";
			m_bonus_type = BONUS_DAMAGE;
		}
		else
		{
			// logic error.
		}

		out << " �� " << m_bonus_time << " �����. ***";
		m_result = ER_START;
		broadcast_message << "&W" << out.str() << "&n\r\n";
	}

	void ArgumentsParser::parse_arguments(const char* argument)
	{
		char buffer[MAX_STRING_LENGTH];
		argument = one_argument(argument, buffer);
		m_first_argument = buffer;
		argument = one_argument(argument, buffer);
		m_second_argument = buffer;
		m_arguments_remainder = argument;
	}

}

// vim: ts=4 sw=4 tw=0 noet syntax=cpp :
