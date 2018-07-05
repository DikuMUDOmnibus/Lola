/***************************************************************************
 * Mud Telopt Handler 1.3 by Igor van den Hoven.               06 Apr 2009 *
 ***************************************************************************/

#include "mud.h"
#include "telnet.h"

#define TELOPT_DEBUG 1

char iac_do_eor[]           = { IAC, DO,   TELOPT_EOR, 0 };

char iac_will_ttype[]       = { IAC, WILL, TELOPT_TTYPE, 0 };
char iac_sb_ttype_is[]      = { IAC, SB,   TELOPT_TTYPE, ENV_IS, 0 };

char iac_sb_naws[]          = { IAC, SB,   TELOPT_NAWS, 0 };

char iac_will_new_environ[] = { IAC, WILL, TELOPT_NEW_ENVIRON, 0 };
char iac_sb_new_environ[]   = { IAC, SB,   TELOPT_NEW_ENVIRON, ENV_IS, 0 };

char iac_do_mssp[]          = { IAC, DO,   TELOPT_MSSP, 0 };

char iac_do_msdp[]          = { IAC, DO,   TELOPT_MSDP, 0 };
char iac_sb_msdp[]          = { IAC, SB,   TELOPT_MSDP, 0 };

char iac_do_mccp[]          = { IAC, DO,   TELOPT_MCCP, 0 };
char iac_dont_mccp[]        = { IAC, DONT, TELOPT_MCCP, 0 };


struct telopt_type
{
	int      size;
	char   * code;
	int   (* func) (DESCRIPTOR_DATA *d, unsigned char *src, int srclen);
};

const struct telopt_type telopt_table [] =
{
	{ 3, iac_do_eor,           &process_do_eor},

	{ 3, iac_will_ttype,       &process_will_ttype},
	{ 4, iac_sb_ttype_is,      &process_sb_ttype_is},

	{ 3, iac_sb_naws,          &process_sb_naws},

	{ 3, iac_will_new_environ, &process_will_new_environ},
	{ 4, iac_sb_new_environ,   &process_sb_new_environ},

	{ 3, iac_do_mssp,          &process_do_mssp},

	{ 3, iac_do_msdp,          &process_do_msdp},
	{ 3, iac_sb_msdp,          &process_sb_msdp},

	{ 3, iac_do_mccp,          &process_do_mccp},
	{ 3, iac_dont_mccp,        &process_dont_mccp},

	{ 0, NULL,                 NULL}
};

/*
	Call this to announce support for telopts marked as such in tables.c
*/

void announce_support( DESCRIPTOR_DATA *d)
{
	int i;

	for (i = 0 ; i < 255 ; i++)
	{
		if (telnet_table[i].flags)
		{
			if (HAS_BIT(telnet_table[i].flags, ANNOUNCE_WILL))
			{
				descriptor_printf(d, "%c%c%c", IAC, WILL, i);
			}
			if (HAS_BIT(telnet_table[i].flags, ANNOUNCE_DO))
			{
				descriptor_printf(d, "%c%c%c", IAC, DO, i);
			}
		}
	}
}

/*
	Call this right before a copyover to reset the telnet state
*/

void unannounce_support( DESCRIPTOR_DATA *d)
{
	int i;

	for (i = 0 ; i < 255 ; i++)
	{
		if (telnet_table[i].flags)
		{
			if (HAS_BIT(telnet_table[i].flags, ANNOUNCE_WILL))
			{
				descriptor_printf(d, "%c%c%c", IAC, WONT, i);
			}
			if (HAS_BIT(telnet_table[i].flags, ANNOUNCE_DO))
			{
				descriptor_printf(d, "%c%c%c", IAC, DONT, i);
			}
		}
	}
}

/*
	This is the main routine that strips out and handles telopt negotiations.
	It also deals with \r and \0 so commands are separated by a single \n.
*/

int translate_telopts(DESCRIPTOR_DATA *d, char *src, int srclen, char *out)
{
	int cnt, skip;
	unsigned char *pti, *pto;

	pti = (unsigned char *) src;
	pto = (unsigned char *) out;

	if (d->teltop)
	{
		if (d->teltop + srclen + 1 < MAX_INPUT_LENGTH)
		{
			memcpy(d->telbuf + d->teltop, src, srclen);

			srclen += d->teltop;

			pti = (unsigned char *) d->telbuf;
		}
		d->teltop = 0;
	}

	while (srclen > 0)
	{
		switch (*pti)
		{
			case IAC:
				skip = 2;

				debug_telopts(d, pti, srclen);

				for (cnt = 0 ; telopt_table[cnt].code ; cnt++)
				{
					if (srclen < telopt_table[cnt].size)
					{
						if (!memcmp(pti, telopt_table[cnt].code, srclen))
						{
							skip = telopt_table[cnt].size;

							break;
						}
					}
					else
					{
						if (!memcmp(pti, telopt_table[cnt].code, telopt_table[cnt].size))
						{
							skip = telopt_table[cnt].func(d, pti, srclen);

							break;
						}
					}
				}

				if (telopt_table[cnt].code == NULL && srclen > 1)
				{
					switch (pti[1])
					{
						case WILL:
						case DO:
						case WONT:
						case DONT:
							skip = 3;
							break;

						case SB:
							skip = skip_sb(d, pti, srclen);
							break;

						case IAC:
							*pto++ = *pti++;
							srclen--;
							skip = 1;
							break;

						default:
							if (TELCMD_OK(pti[1]))
							{
								skip = 2;
							}
							else
							{
								skip = 1;
							}
							break;
					}
				}

				if (skip <= srclen)
				{
					pti += skip;
					srclen -= skip;
				}
				else
				{
					memcpy(d->telbuf, pti, srclen);
					d->teltop = srclen;

					*pto = 0;
					return strlen(out);
				}
				break;

			case '\r':
				if (srclen > 1 && pti[1] == '\0')
				{
					*pto++ = '\n';
				}
				pti++;
				srclen--;
				break;

			case '\0':
				pti++;
				srclen--;
				break;

			default:
				*pto++ = *pti++;
				srclen--;
				break;
		}
	}
	*pto = 0;

	return strlen(out);
}

void debug_telopts( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	if (srclen > 1 && TELOPT_DEBUG)
	{
		switch(src[1])
		{
			case IAC:
				log_printf("D%d@%s RCVD IAC IAC", d->descriptor, d->host);
				break;

			case DO:
			case DONT:
			case WILL:
			case WONT:
			case SB:
				if (srclen > 2)
				{
					if (src[1] == SB)
					{
						if (skip_sb(d, src, srclen) == srclen + 1)
						{
							log_printf("D%d@%s RCVD IAC SB %s ?", d->descriptor, d->host, TELOPT(src[2]));
						}
						else
						{
							log_printf("D%d@%s RCVD IAC SB %s IAC SE", d->descriptor, d->host, TELOPT(src[2]));
						}
					}
					else
					{
						log_printf("D%d@%s RCVD IAC %s %s", d->descriptor, d->host, TELCMD(src[1]), TELOPT(src[2]));
					}
				}
				else
				{
					log_printf("D%d@%s RCVD IAC %s ?", d->descriptor, d->host, TELCMD(src[1]));
				}
				break;

			default:
				if (TELCMD_OK(src[1]))
				{
					log_printf("D%d@%s RCVD IAC %s", d->descriptor, d->host, TELCMD(src[1]));
				}
				else
				{
					log_printf("D%d@%s RCVD IAC %d", d->descriptor, d->host, src[1]);
				}
				break;
		}
	}
	else
	{
		log_printf("D%d@%s RCVD IAC ?", d->descriptor, d->host);
	}
}

/*
	Send to client to have it disable local echo
*/

void send_echo_off( DESCRIPTOR_DATA *d )
{
	SET_BIT(d->comm_flags, COMM_FLAG_PASSWORD);

	descriptor_printf(d, "%c%c%c", IAC, WILL, TELOPT_ECHO);
}

/*
	Send to client to have it enable local echo
*/

void send_echo_on( DESCRIPTOR_DATA *d )
{
	DEL_BIT(d->comm_flags, COMM_FLAG_PASSWORD);

	descriptor_printf(d, "%c%c%c", IAC, WONT, TELOPT_ECHO);
}

/*
	Send right after the prompt to mark it as such.
*/

void send_eor( DESCRIPTOR_DATA *d )
{
	if (HAS_BIT(d->comm_flags, COMM_FLAG_EOR))
	{
		descriptor_printf(d, "%c%c", IAC, EOR);
	}
}

/*
	End Of Record negotiation - not enabled by default in tables.c
*/

int process_do_eor( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	SET_BIT(d->comm_flags, COMM_FLAG_EOR);

	return 3;
}

/*
	Terminal Type negotiation - make sure d->terminal_type is initialized.
*/

int process_will_ttype( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	if (*d->terminal_type == 0)
	{
		// Request the first three terminal types to see if MTTS is supported, next reset to default.

		descriptor_printf(d, "%c%c%c%c%c%c", IAC, SB, TELOPT_TTYPE, ENV_SEND, IAC, SE);
		descriptor_printf(d, "%c%c%c%c%c%c", IAC, SB, TELOPT_TTYPE, ENV_SEND, IAC, SE);
		descriptor_printf(d, "%c%c%c%c%c%c", IAC, SB, TELOPT_TTYPE, ENV_SEND, IAC, SE);
		descriptor_printf(d, "%c%c%c", IAC, DONT, TELOPT_TTYPE);
	}
	return 3;
}

int process_sb_ttype_is( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	char val[MAX_INPUT_LENGTH];
	char *pto;
	int i;

	if (skip_sb(d, src, srclen) > srclen)
	{
		return srclen + 1;
	}

	pto = val;

	for (i = 4 ; i < srclen && src[i] != SE ; i++)
	{
		switch (src[i])
		{
			default:			
				*pto++ = src[i];
				break;

			case IAC:
				*pto = 0;

				if (*d->terminal_type == 0)
				{
					RESTRING(d->terminal_type, val);
				}
				else
				{
					if (sscanf(val, "MTTS %lld", &d->mtts) == 1)
					{
						if (HAS_BIT(d->mtts, MTTS_FLAG_256COLORS))
						{
							SET_BIT(d->comm_flags, COMM_FLAG_256COLORS);
						}
					}

					if (strcasestr(val, "-256color") || strcasecmp(val, "xterm"))
					{	
						SET_BIT(d->comm_flags, COMM_FLAG_256COLORS);
					}
				}
				break;
		}
	}
	return i + 1;
}

/*
	NAWS: Negotiate About Window Size
*/

int process_sb_naws( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	int i, j;

	d->cols = d->rows = 0;

	if (skip_sb(d, src, srclen) > srclen)
	{
		return srclen + 1;
	}

	for (i = 3, j = 0 ; i < srclen && j < 4 ; i++, j++)
	{
		switch (j)
		{
			case 0:
				d->cols += (src[i] == IAC) ? src[i++] * 256 : src[i] * 256;
				break;
			case 1:
				d->cols += (src[i] == IAC) ? src[i++] : src[i];
				break;
			case 2:
				d->rows += (src[i] == IAC) ? src[i++] * 256 : src[i] * 256;
				break;
			case 3:
				d->rows += (src[i] == IAC) ? src[i++] : src[i];
				break;
		}
	}

	return skip_sb(d, src, srclen);
}

/*
	NEW ENVIRON, used here to discover Windows telnet.
*/

int process_will_new_environ( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	descriptor_printf(d, "%c%c%c%c%c%s%c%c", IAC, SB, TELOPT_NEW_ENVIRON, ENV_SEND, ENV_VAR, "SYSTEMTYPE", IAC, SE);
	descriptor_printf(d, "%c%c%c%c%c%s%c%c", IAC, SB, TELOPT_NEW_ENVIRON, ENV_SEND, ENV_VAR, "IPADDRESS", IAC, SE);

	return 3;
}

int process_sb_new_environ( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	char var[MAX_INPUT_LENGTH], val[MAX_INPUT_LENGTH];
	char *pto;
	int i;

	if (skip_sb(d, src, srclen) > srclen)
	{
		return srclen + 1;
	}

	var[0] = val[0] = 0;

	i = 4;

	while (i < srclen && src[i] != SE)
	{
		switch (src[i])
		{
			case ENV_VAR:
			case ENV_USR:
				i++;
				pto = var;

				while (i < srclen && src[i] >= 32 && src[i] != IAC)
				{
					*pto++ = src[i++];
				}
				*pto = 0;
				break;

			case ENV_VAL:
				i++;
				pto = val;

				while (i < srclen && src[i] >= 32 && src[i] != IAC)
				{
					*pto++ = src[i++];
				}
				*pto = 0;

				// Detect Windows telnet and enable remote echo.

				if (!strcasecmp(var, "SYSTEMTYPE") && !strcasecmp(val, "WIN32"))
				{
					if (!strcasecmp(d->terminal_type, "ANSI"))
					{
						SET_BIT(d->comm_flags, COMM_FLAG_REMOTEECHO);

						RESTRING(d->terminal_type, "WINDOWS TELNET");
					}
				}
				if (!strcasecmp(var, "IPADDRESS"))
				{
					log_printf("D%d@%s NEW-ENVIRON IPADDRESS: %s", d->descriptor, d->host, val);
//					descriptor_printf(d, "D%d@%s NEW-ENVIRON IPADDRESS: %s\n\r", d->descriptor, d->host, val);
				}
				break;

			default:
				i++;
				break;
		}
	}
	return i + 1;
}

/*
	MSDP: Mud Server Status Protocol

	http://tintin.sourceforge.net/msdp
*/

int process_do_msdp( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	int index;

	if (d->msdp_data)
	{
		return 3;
	}

	d->msdp_data = (struct msdp_data **) calloc(mud->msdp_table_size, sizeof(struct msdp_data *));

	for (index = 0 ; index < mud->msdp_table_size ; index++)
	{
		d->msdp_data[index] = (struct msdp_data *) calloc(1, sizeof(struct msdp_data *));

		d->msdp_data[index]->flags = msdp_table[index].flags;
		d->msdp_data[index]->value = strdup("");
	}

	// Easiest to handle variable initialization here.

	msdp_update_var(d, "SPECIFICATION", "%s", "http://tintin.sourceforge.net/msdp");

	return 3;
}

int process_sb_msdp( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	char var[MAX_INPUT_LENGTH], val[MAX_INPUT_LENGTH];
	char *pto;
	int i, nest;

	if (skip_sb(d, src, srclen) > srclen)
	{
		return srclen + 1;
	}

	var[0] = val[0] = 0;

	i = 3;
	nest = 0;

	while (i < srclen && src[i] != SE)
	{
		switch (src[i])
		{
			case MSDP_VAR:
				i++;
				pto = var;

				while (i < srclen && src[i] != MSDP_VAL && src[i] != IAC)
				{
					*pto++ = src[i++];
				}
				*pto = 0;

				break;

			case MSDP_VAL:
				i++;
				pto = val;

				while (i < srclen && src[i] != IAC)
				{
					if (src[i] == MSDP_TABLE_OPEN || src[i] == MSDP_ARRAY_OPEN)
					{
						nest++;
					}
					else if (src[i] == MSDP_TABLE_CLOSE || src[i] == MSDP_ARRAY_CLOSE)
					{
						nest--;
					}
					else if (nest == 0 && (src[i] == MSDP_VAR || src[i] == MSDP_VAL))
					{
						break;
					}
					*pto++ = src[i++];
				}
				*pto = 0;

				if (nest == 0)
				{
					process_msdp_varval(d, var, val);
				}
				break;

			default:
				i++;
				break;
		}
	}
	return i + 1;
}

/*
	MSSP: Mud Server Status Protocol

	http://tintin.sourceforge.net/mssp

	Uncomment and update as needed
*/


int process_do_mssp( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	descriptor_printf(d, "%c%c%c", IAC, SB, TELOPT_MSSP);

	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "NAME",              MSSP_VAL, "Lowlands");
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "PLAYERS",           MSSP_VAL, mud->total_plr);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "UPTIME",            MSSP_VAL, mud->boot_time);

	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "CRAWL DELAY",       MSSP_VAL, 11);
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "CODEBASE",          MSSP_VAL, "Lola 1.4");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "CONTACT",           MSSP_VAL, "");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "CREATED",           MSSP_VAL, "2007");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "HOSTNAME",          MSSP_VAL, "lolamud.net");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "ICON",              MSSP_VAL, "");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "IP",                MSSP_VAL, "217.19.28.128");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "LANGUAGE",          MSSP_VAL, "English");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "LOCATION",          MSSP_VAL, "Netherlands");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "MINIMUM AGE",       MSSP_VAL, "0");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "PORT",              MSSP_VAL, "6969");

	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "FAMILY",            MSSP_VAL, "Emud");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "FAMILY",            MSSP_VAL, "MrMud");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "FAMILY",            MSSP_VAL, "Merc");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "FAMILY",            MSSP_VAL, "DikuMUD");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "WEBSITE",           MSSP_VAL, "");

	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "GENRE",             MSSP_VAL, "Fantasy");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "STATUS",            MSSP_VAL, "LIVE");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "SUBGENRE",          MSSP_VAL, "Medieval Fantasy");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "GAMEPLAY",          MSSP_VAL, "Hack and Slash");
	descriptor_printf(d, "%c%s%c%s", MSSP_VAR, "GAMESYSTEM",        MSSP_VAL, "Custom");

	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "AREAS",             MSSP_VAL, mud->top_area);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "HELPFILES",         MSSP_VAL, mud->top_help);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "MOBILES",           MSSP_VAL, mud->top_mob_index);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "OBJECTS",           MSSP_VAL, mud->top_obj_index);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "ROOMS",             MSSP_VAL, mud->top_room);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "RESETS",            MSSP_VAL, mud->top_reset);

	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "CLASSES",           MSSP_VAL, MAX_CLASS);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "LEVELS",            MSSP_VAL, MAX_LEVEL);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "RACES",             MSSP_VAL, MAX_RACE);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "SKILLS",            MSSP_VAL, MAX_SKILL);

	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "ANSI",              MSSP_VAL, 1);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "MCCP",              MSSP_VAL, 1);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "MCP",               MSSP_VAL, 0);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "MSP",               MSSP_VAL, 0);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "MXP",               MSSP_VAL, 0);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "PUEBLO",            MSSP_VAL, 0);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "VT100",             MSSP_VAL, 1);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "XTERM 256 COLORS",  MSSP_VAL, 1);

	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "PAY TO PLAY",       MSSP_VAL, 0);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "PAY FOR PERKS",     MSSP_VAL, 0);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "HIRING BUILDERS",   MSSP_VAL, 0);
	descriptor_printf(d, "%c%s%c%d", MSSP_VAR, "HIRING CODERS",     MSSP_VAL, 0);

	descriptor_printf(d, "%c%c", IAC, SE);

	return 3;
}


/*
	MCCP: Mud Client Compression Protocol
*/

void *zlib_alloc( void *opaque, unsigned int items, unsigned int size )
{
	return calloc(items, size);
}


void zlib_free( void *opaque, void *address ) 
{
	free(address);
}


int start_compress( DESCRIPTOR_DATA *d )
{
	char start_mccp[] = { IAC, SB, TELOPT_MCCP, IAC, SE, 0 };
	z_stream *stream;

	if (d->mccp)
	{
		return TRUE;
	}

	stream = calloc(1, sizeof(z_stream));

	stream->next_in	    = NULL;
	stream->avail_in    = 0;

	stream->next_out    = mud->mccp_buf;
	stream->avail_out   = COMPRESS_BUF_SIZE;

	stream->data_type   = Z_ASCII;
	stream->zalloc      = zlib_alloc;
	stream->zfree       = zlib_free;
	stream->opaque      = Z_NULL;

	/*
		12, 5 = 32K of memory, more than enough
	*/

	if (deflateInit2(stream, Z_BEST_COMPRESSION, Z_DEFLATED, 12, 5, Z_DEFAULT_STRATEGY) != Z_OK)
	{
		log_printf("start_compress: failed deflateInit2 D%d@%s", d->descriptor, d->host);
		free(stream);

		return FALSE;
	}

	write_to_descriptor(d, start_mccp, 0);

	/*
		The above call must send all pending output to the descriptor, since from now on we'll be compressing.
	*/

	d->mccp = stream;

	return TRUE;
}


void end_compress( DESCRIPTOR_DATA *d )
{
	if (d->mccp == NULL)
	{
		return;
	}

	d->mccp->next_in	= NULL;
	d->mccp->avail_in	= 0;

	d->mccp->next_out	= mud->mccp_buf;
	d->mccp->avail_out	= COMPRESS_BUF_SIZE;

	if (deflate(d->mccp, Z_FINISH) != Z_STREAM_END)
	{
		log_printf("end_compress: failed to deflate D%d@%s", d->descriptor, d->host);
	}

	if (!HAS_BIT(d->comm_flags, COMM_FLAG_DISCONNECT))
	{
		process_compressed(d);
	}

	if (deflateEnd(d->mccp) != Z_OK)
	{
		log_printf("end_compress: failed to deflateEnd D%d@%s", d->descriptor, d->host);
	}

	free(d->mccp);

	d->mccp = NULL;

	return;
}


void write_compressed( DESCRIPTOR_DATA *d )
{
	d->mccp->next_in    = (unsigned char *) d->outbuf;
	d->mccp->avail_in   = d->outtop;

	d->mccp->next_out   = (unsigned char *) mud->mccp_buf;
	d->mccp->avail_out  = COMPRESS_BUF_SIZE;

	d->outtop           = 0;

	if (deflate(d->mccp, Z_SYNC_FLUSH) != Z_OK)
	{
		return;
	}

	process_compressed(d);

	return;
}


void process_compressed( DESCRIPTOR_DATA *d )
{
	int length;

	length = COMPRESS_BUF_SIZE - d->mccp->avail_out;

	if (write(d->descriptor, mud->mccp_buf, length) < 1)
	{
		log_printf("process_compressed D%d@%s", d->descriptor, d->host);
		SET_BIT(d->comm_flags, COMM_FLAG_DISCONNECT);

		return;
	}

	return;
}


int process_do_mccp( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	start_compress(d);

	return 3;
}


int process_dont_mccp( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	end_compress(d);

	return 3;
}

/*
	Returns the length of a telnet subnegotiation, return srclen + 1 for incomplete state.
*/

int skip_sb( DESCRIPTOR_DATA *d, unsigned char *src, int srclen )
{
	int i;

	for (i = 1 ; i < srclen ; i++)
	{
		if (src[i] == SE && src[i-1] == IAC)
		{
			return i + 1;
		}
	}

	return srclen + 1;
}


		
/*
	Utility function
*/

void descriptor_printf( DESCRIPTOR_DATA *d, char *fmt, ... )
{
	char buf[MAX_STRING_LENGTH];
	int size;
	va_list args;

	va_start(args, fmt);

	size = vsprintf(buf, fmt, args);

	va_end(args);

	write_to_descriptor(d, buf, size);
}

