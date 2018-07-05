/***************************************************************************
 * Mud Telopt Handler 1.4 by Igor van den Hoven.               10 Jun 2011 *
 ***************************************************************************/

#include "mud.h"
#include "telnet.h"

// Set table size and check for errors. Call once at startup.

void init_msdp_table(void)
{
	int index;

	for (index = 0 ; *msdp_table[index].name ; index++)
	{
		if (strcmp(msdp_table[index].name, msdp_table[index+1].name) > 0)
		{
			if (*msdp_table[index+1].name)
			{
				log_printf("init_msdp_table: Improperly sorted variable: %s.", msdp_table[index]);
			}
		}
	}
	mud->msdp_table_size = index;
}

// Binary search on the msdp_table.

int msdp_find(char *var)
{
	int val, bot, top, srt;

	bot = 0;
	top = mud->msdp_table_size - 1;
	val = top / 2;

	while (bot <= top)
	{
		srt = strcmp(var, msdp_table[val].name);

		if (srt < 0)
		{
			top = val - 1;
		}
		else if (srt > 0)
		{
			bot = val + 1;
		}
		else
		{
			return val;
		}
		val = bot + (top - bot) / 2;
	}
	return -1;
}

// Update a variable and queue it if it's being reported.

void msdp_update_var(DESCRIPTOR_DATA *d, char *var, char *fmt, ...)
{
	char buf[MAX_STRING_LENGTH];
	int index;
	va_list args;

	if (d->msdp_data == NULL)
	{
		return;
	}

	index = msdp_find(var);

	if (index == -1)
	{
		log_printf("msdp_update_var: Unknown variable: %s.", var);

		return;
	}

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);

	if (strcmp(d->msdp_data[index]->value, buf))
	{
		if (HAS_BIT(d->msdp_data[index]->flags, MSDP_FLAG_REPORTED))
		{
			SET_BIT(d->msdp_data[index]->flags, MSDP_FLAG_UPDATED);
			SET_BIT(d->comm_flags, COMM_FLAG_MSDPUPDATE);
		}
		RESTRING(d->msdp_data[index]->value, buf);
	}
}

// Update a variable and send it instantly.

void msdp_update_var_instant(DESCRIPTOR_DATA *d, char *var, char *fmt, ...)
{
	char buf[MAX_STRING_LENGTH];
	int index;
	va_list args;

	if (d->msdp_data == NULL)
	{
		return;
	}

	index = msdp_find(var);

	if (index == -1)
	{
		log_printf("msdp_update_var_instant: Unknown variable: %s.", var);

		return;
	}

	va_start(args, fmt);
	vsprintf(buf, fmt, args);
	va_end(args);

	if (HAS_BIT(d->msdp_data[index]->flags, MSDP_FLAG_REPORTED))
	{
		descriptor_printf(d, "%c%c%c%c%s%c%s%c%c", IAC, SB, TELOPT_MSDP, MSDP_VAR, msdp_table[index].name, MSDP_VAL, buf, IAC, SE);
	}

	if (strcmp(d->msdp_data[index]->value, buf))
	{
		RESTRING(d->msdp_data[index]->value, buf);
	}
}

// Send all reported variables that have been updated.

void msdp_send_update(DESCRIPTOR_DATA *d)
{
	char buf[MAX_STRING_LENGTH];
	int index, size;

	if (d->msdp_data == NULL)
	{
		return;
	}

	size = sprintf(buf, "%c%c%c", IAC, SB, TELOPT_MSDP);

	for (index = 0 ; index < mud->msdp_table_size ; index++)
	{
		if (HAS_BIT(d->msdp_data[index]->flags, MSDP_FLAG_UPDATED))
		{
			size += cat_sprintf(buf, "%c%s%c%s", MSDP_VAR, msdp_table[index].name, MSDP_VAL, d->msdp_data[index]->value);

			DEL_BIT(d->msdp_data[index]->flags, MSDP_FLAG_UPDATED);
		}

		if (size > MAX_STRING_LENGTH - MAX_INPUT_LENGTH)
		{
			break;
		}
	}

	cat_sprintf(buf, "%c%c", IAC, SE);

	descriptor_printf(d, "%s", buf);

	DEL_BIT(d->comm_flags, COMM_FLAG_MSDPUPDATE);
}


char *msdp_get_var(DESCRIPTOR_DATA *d, char *var)
{
	int index;

	if (d->msdp_data == NULL)
	{
		return NULL;
	}

	index = msdp_find(var);

	if (index == -1)
	{
		log_printf("msdp_get_var: Unknown variable: %s.", var);

		return NULL;
	}

	return d->msdp_data[index]->value;
}

void process_msdp_varval( DESCRIPTOR_DATA *d, char *var, char *val )
{
	int var_index, val_index;

	if (d->msdp_data == NULL)
	{
		return;
	}

	var_index = msdp_find(var);

	if (var_index == -1)
	{
		return;
	}

	if (HAS_BIT(msdp_table[var_index].flags, MSDP_FLAG_CONFIGURABLE))
	{
		RESTRING(d->msdp_data[var_index]->value, val);

		if (msdp_table[var_index].fun)
		{
			msdp_table[var_index].fun(d, var_index);
		}
		return;
	}

	// Commands only take variables as arguments.

	if (HAS_BIT(msdp_table[var_index].flags, MSDP_FLAG_COMMAND))
	{
		val_index = msdp_find(val);

		if (val_index == -1)
		{
			return;
		}

		if (msdp_table[var_index].fun)
		{
			msdp_table[var_index].fun(d, val_index);
		}
		return;
	}
} 

void msdp_command_list(DESCRIPTOR_DATA *d, int index)
{
	int flag;
	char buf[MAX_STRING_LENGTH];

	if (!HAS_BIT(msdp_table[index].flags, MSDP_FLAG_LIST))
	{
		return;
	}

	flag = msdp_table[index].flags;

	sprintf(buf, "%c%c%c%c%s%c%c", IAC, SB, TELOPT_MSDP, MSDP_VAR, msdp_table[index].name, MSDP_VAL, MSDP_ARRAY_OPEN);

	for (index = 0 ; index < mud->msdp_table_size ; index++)
	{
		if (flag != MSDP_FLAG_LIST)
		{
			if (HAS_BIT(d->msdp_data[index]->flags, flag) && !HAS_BIT(d->msdp_data[index]->flags, MSDP_FLAG_LIST))
			{
				cat_sprintf(buf, "%c%s", MSDP_VAL, msdp_table[index].name);
			}
		}
		else
		{
			if (HAS_BIT(d->msdp_data[index]->flags, MSDP_FLAG_LIST))
			{
				cat_sprintf(buf, "%c%s", MSDP_VAL, msdp_table[index].name);
			}
		}
	}

	cat_sprintf(buf, "%c%c%c", MSDP_ARRAY_CLOSE, IAC, SE);

	descriptor_printf(d, "%s", buf);
}

void msdp_command_report(DESCRIPTOR_DATA *d, int index)
{
	if (!HAS_BIT(msdp_table[index].flags, MSDP_FLAG_REPORTABLE))
	{
		return;
	}

	SET_BIT(d->msdp_data[index]->flags, MSDP_FLAG_REPORTED);

	if (HAS_BIT(msdp_table[index].flags, MSDP_FLAG_SENDABLE))
	{
		SET_BIT(d->msdp_data[index]->flags, MSDP_FLAG_UPDATED);
	}
}

void msdp_command_reset(DESCRIPTOR_DATA *d, int index)
{
	int flag;

	if (!HAS_BIT(msdp_table[index].flags, MSDP_FLAG_LIST))
	{
		return;
	}

	flag = msdp_table[index].flags &= ~MSDP_FLAG_LIST;

	for (index = 0 ; index < mud->msdp_table_size ; index++)
	{
		if (HAS_BIT(d->msdp_data[index]->flags, flag))
		{
			d->msdp_data[index]->flags = msdp_table[index].flags;
		}
	}
}

void msdp_command_send(DESCRIPTOR_DATA *d, int index)
{
	if (HAS_BIT(d->msdp_data[index]->flags, MSDP_FLAG_SENDABLE))
	{
		SET_BIT(d->msdp_data[index]->flags, MSDP_FLAG_UPDATED);	
		SET_BIT(d->comm_flags, COMM_FLAG_MSDPUPDATE);
	}
}

void msdp_command_unreport(DESCRIPTOR_DATA *d, int index)
{
	if (!HAS_BIT(msdp_table[index].flags, MSDP_FLAG_REPORTABLE))
	{
		return;
	}

	DEL_BIT(d->msdp_data[index]->flags, MSDP_FLAG_REPORTED);
}

// Comment out if you don't want Arachnos Intermud support

void msdp_configure_arachnos(DESCRIPTOR_DATA *d, int index)
{
	char var[MAX_INPUT_LENGTH], val[MAX_INPUT_LENGTH];
	char mud_name[MAX_INPUT_LENGTH], mud_host[MAX_INPUT_LENGTH], mud_port[MAX_INPUT_LENGTH];
	char msg_user[MAX_INPUT_LENGTH], msg_time[MAX_INPUT_LENGTH], msg_body[MAX_INPUT_LENGTH];
	char mud_players[MAX_INPUT_LENGTH], mud_uptime[MAX_INPUT_LENGTH], mud_update[MAX_INPUT_LENGTH];
	char *pti, *pto;

	struct tm timeval_tm;
	time_t timeval_t;

	var[0] = val[0] = mud_name[0] = mud_host[0] = mud_port[0] = msg_user[0] = msg_time[0] = msg_body[0] = mud_players[0] = mud_uptime[0] = mud_update[0] = 0;

	pti = d->msdp_data[index]->value;

	while (*pti)
	{
		switch (*pti)
		{
			case MSDP_VAR:
				pti++;
				pto = var;

				while (*pti > MSDP_ARRAY_CLOSE)
				{
					*pto++ = *pti++;
				}
				*pto = 0;
				break;

			case MSDP_VAL:
				pti++;
				pto = val;

				while (*pti > MSDP_ARRAY_CLOSE)
				{
					*pto++ = *pti++;
				}
				*pto = 0;

				if (!strcmp(var, "MUD_NAME"))
				{
					strcpy(mud_name, val);
				}
				else if (!strcmp(var, "MUD_HOST"))
				{
					strcpy(mud_host, val);
				}
				else if (!strcmp(var, "MUD_PORT"))
				{
					strcpy(mud_port, val);
				}
				else if (!strcmp(var, "MSG_USER"))
				{
					strcpy(msg_user, val);
				}
				else if (!strcmp(var, "MSG_TIME"))
				{
					timeval_t = (time_t) atoll(val);
					timeval_tm = *localtime(&timeval_t);
					
					strftime(msg_time, 20, "%T %D", &timeval_tm);
				}
				else if (!strcmp(var, "MSG_BODY"))
				{
					strcpy(msg_body, val);
				}
				else if (!strcmp(var, "MUD_UPTIME"))
				{
					timeval_t = (time_t) atoll(val);
					timeval_tm = *localtime(&timeval_t);
					
					strftime(mud_uptime, 20, "%T %D", &timeval_tm);
				}
				else if (!strcmp(var, "MUD_UPDATE"))
				{
					timeval_t = (time_t) atoll(val);
					timeval_tm = *localtime(&timeval_t);
					
					strftime(mud_update, 20, "%T %D", &timeval_tm);
				}
				else if (!strcmp(var, "MUD_PLAYERS"))
				{
					strcpy(mud_players, val);
				}
				break;

			default:
				pti++;
				break;
		}
	}

	if (*mud_name && *mud_host && *mud_port)
	{
		if (!strcmp(msdp_table[index].name, "ARACHNOS_DEVEL"))
		{
			if (*msg_user && *msg_time && *msg_body)
			{
				arachnos_devel("%s %s@%s:%s devtalks: %s", msg_time, msg_user, mud_host, mud_port, msg_body);
			}
		}
		else if (!strcmp(msdp_table[index].name, "ARACHNOS_MUDLIST"))
		{
			if (*mud_uptime && *mud_update && *mud_players)
			{
				arachnos_mudlist("%18.18s %14.14s %5.5s %17.17s %17.17s %4.4s", mud_name, mud_host, mud_port, mud_update, mud_uptime, mud_players);
			}
		}
	}
}

struct msdp_type msdp_table[] =
{
	{    "ALIGNMENT",                     MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "ARACHNOS_DEVEL",            MSDP_FLAG_CONFIGURABLE|MSDP_FLAG_REPORTABLE,    msdp_configure_arachnos },
	{    "ARACHNOS_MUDLIST",                               MSDP_FLAG_CONFIGURABLE,    msdp_configure_arachnos },
	{    "COMMANDS",                             MSDP_FLAG_COMMAND|MSDP_FLAG_LIST,    NULL },
	{    "CONFIGURABLE_VARIABLES",          MSDP_FLAG_CONFIGURABLE|MSDP_FLAG_LIST,    NULL },
	{    "EXPERIENCE",                    MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "EXPERIENCE_MAX",                MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "HEALTH",                        MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "HEALTH_MAX",                    MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "LEVEL",                         MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "LIST",                                                MSDP_FLAG_COMMAND,    msdp_command_list },
	{    "LISTS",                                                  MSDP_FLAG_LIST,    NULL },
	{    "MANA",                          MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "MANA_MAX",                      MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "MONEY",                         MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "MOVEMENT",                      MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "MOVEMENT_MAX",                  MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
        {    "REPORT",                                              MSDP_FLAG_COMMAND,    msdp_command_report },
	{    "REPORTABLE_VARIABLES",              MSDP_FLAG_REPORTABLE|MSDP_FLAG_LIST,    NULL },
	{    "REPORTED_VARIABLES",                  MSDP_FLAG_REPORTED|MSDP_FLAG_LIST,    NULL },
	{    "RESET",                                               MSDP_FLAG_COMMAND,    msdp_command_reset },
	{    "ROOM",                                             MSDP_FLAG_REPORTABLE,    NULL },
	{    "ROOM_EXITS",                    MSDP_FLAG_SENDABLE|MSDP_FLAG_REPORTABLE,    NULL },
	{    "SEND",                                                MSDP_FLAG_COMMAND,    msdp_command_send },
	{    "SENDABLE_VARIABLES",                  MSDP_FLAG_SENDABLE|MSDP_FLAG_LIST,    NULL },
	{    "SPECIFICATION",                                      MSDP_FLAG_SENDABLE,    NULL },
	{    "UNREPORT",                                            MSDP_FLAG_COMMAND,    msdp_command_unreport },

	{    "",                                                                    0,    NULL } // End of table marker
};
