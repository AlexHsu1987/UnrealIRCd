/*
 *   Unreal Internet Relay Chat Daemon, src/modules/m_oper.c
 *   (C) 2000-2001 Carsten V. Munk and the UnrealIRCd Team
 *   Moved to modules by Fish (Justin Hammond)
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 1, or (at your option)
 *   any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include "config.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include <time.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#endif
#include <fcntl.h>
#include "h.h"
#include "proto.h"
#include "inet.h"
#ifdef STRIPBADWORDS
#include "badwords.h"
#endif
#ifdef _WIN32
#include "version.h"
#endif

DLLFUNC int m_oper(aClient *cptr, aClient *sptr, int parc, char *parv[]);


/* Place includes here */
#define MSG_OPER        "OPER"  /* OPER */
#define TOK_OPER        ";"     /* 59 */

typedef struct oper_oflag_ {
	unsigned long oflag;
	long* umode;	/* you just HAD to make them variables */
	char** host;
	char* announce;
} oper_oflag_t;

static oper_oflag_t oper_oflags[] = {
	{ OFLAG_NETADMIN,	&UMODE_NETADMIN,	&netadmin_host,
		"is now a network administrator (N)" },
	{ OFLAG_SADMIN,		&UMODE_SADMIN,		&sadmin_host,
		"is now a services administrator (a)" },
	{ OFLAG_ADMIN,		&UMODE_ADMIN,		&admin_host,
		"is now a server admin (A)" },
	{ OFLAG_COADMIN,	&UMODE_COADMIN,		&coadmin_host,
		"is now a co administrator (C)" },
	{ OFLAG_ISGLOBAL,	&UMODE_OPER,		&oper_host,
		"is now an operator (O)" },
	{ OFLAG_HELPOP,		&UMODE_HELPOP,		0 ,
		0 },
	{ OFLAG_GLOBOP,		&UMODE_FAILOP,		0 ,
		0 },
	{ OFLAG_WALLOP,		&UMODE_WALLOP,	0 ,
		0 },
	{ OFLAG_WHOIS,		&UMODE_WHOIS,	0 , 		
		0 },
	{ 0,			0,	0 ,
		0 },
};

#ifndef DYNAMIC_LINKING
ModuleHeader m_oper_Header
#else
#define m_oper_Header Mod_Header
ModuleHeader Mod_Header
#endif
  = {
	"oper",	/* Name of module */
	"$Id$", /* Version */
	"command /oper", /* Short description of module */
	"3.2-b8-1",
	NULL 
    };


/* The purpose of these ifdefs, are that we can "static" link the ircd if we
 * want to
*/

/* This is called on module init, before Server Ready */
#ifdef DYNAMIC_LINKING
DLLFUNC int	Mod_Init(ModuleInfo *modinfo)
#else
int    m_oper_Init(ModuleInfo *modinfo)
#endif
{
	/*
	 * We call our add_Command crap here
	*/
	add_Command(MSG_OPER, TOK_OPER, m_oper, MAXPARA);
	return MOD_SUCCESS;
}

/* Is first run when server is 100% ready */
#ifdef DYNAMIC_LINKING
DLLFUNC int	Mod_Load(int module_load)
#else
int    m_oper_Load(int module_load)
#endif
{
	return MOD_SUCCESS;
}


/* Called when module is unloaded */
#ifdef DYNAMIC_LINKING
DLLFUNC int	Mod_Unload(int module_unload)
#else
int	m_oper_Unload(int module_unload)
#endif
{
	if (del_Command(MSG_OPER, TOK_OPER, m_oper) < 0)
	{
		sendto_realops("Failed to delete commands when unloading %s",
				m_oper_Header.name);
	}
	return MOD_SUCCESS;
}


/*
** m_oper
**	parv[0] = sender prefix
**	parv[1] = oper name
**	parv[2] = oper password
*/

extern int  SVSNOOP;

DLLFUNC int  m_oper(aClient *cptr, aClient *sptr, int parc, char *parv[]) {
	ConfigItem_oper *aconf;
	ConfigItem_oper_from *oper_from;
	char *name, *password, nuhhost[NICKLEN+USERLEN+HOSTLEN+6], nuhhost2[NICKLEN+USERLEN+HOSTLEN+6];
	char* host = 0;
	int i = 0, j = 0;
	char* announce = 0;

	if (parc < 3) {
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "OPER");
		return 0;
	}

	if (SVSNOOP) {
		sendto_one(sptr,
		    ":%s %s %s :*** This server is in NOOP mode, you cannot /oper",
		    me.name, IsWebTV(sptr) ? "PRIVMSG" : "NOTICE", sptr->name);
		return 0;
	}

	if (IsOper(sptr)) {
		sendto_one(sptr, rpl_str(RPL_YOUREOPER),
		    me.name, parv[0]);
		return 0;
	}

	name = parc > 1 ? parv[1] : NULL;
	password = parc > 2 ? parv[2] : NULL;

	if (!(aconf = Find_oper(name))) {
		sendto_one(sptr, err_str(ERR_NOOPERHOST), me.name, parv[0]);
		sendto_realops
		    ("Failed OPER attempt by %s (%s@%s) [unknown oper]",
		    parv[0], sptr->user->username, sptr->sockhost);
		sptr->since += 7;
		return 0;
	}
	strlcpy(nuhhost, make_user_host(sptr->user->username, sptr->user->realhost), sizeof(nuhhost));
	strlcpy(nuhhost2, make_user_host(sptr->user->username, Inet_ia2p(&sptr->ip)), sizeof(nuhhost2));
	for (oper_from = (ConfigItem_oper_from *) aconf->from;
	    oper_from; oper_from = (ConfigItem_oper_from *) oper_from->next)
		if (!match(oper_from->name, nuhhost) || !match(oper_from->name, nuhhost2))
			break;
	if (!oper_from)	{
		sendto_one(sptr, err_str(ERR_NOOPERHOST), me.name, parv[0]);
		sendto_realops
		    ("Failed OPER attempt by %s (%s@%s) [host doesnt match]",
		    parv[0], sptr->user->username, sptr->sockhost);
		sptr->since += 7;
		return 0;
	}

	i = Auth_Check(cptr, aconf->auth, password);
	if (i > 1)
	{
		int  old = (sptr->umodes & ALL_UMODES);

		/* Put in the right class */
		if (sptr->class)
			sptr->class->clients--;

		sptr->class = aconf->class;
		sptr->class->clients++;
		sptr->oflag = 0;
		if (aconf->swhois) {
			if (sptr->user->swhois)
				MyFree(sptr->user->swhois);
			sptr->user->swhois = MyMalloc(strlen(aconf->swhois) +1);
			strcpy(sptr->user->swhois, aconf->swhois);
			sendto_serv_butone_token(cptr, sptr->name,
				MSG_SWHOIS, TOK_SWHOIS, "%s :%s", sptr->name, aconf->swhois);
		}

/* new oper code */

		sptr->umodes |= OPER_MODES;

/* handle oflags that trigger umodes */
		
		while(oper_oflags[j].umode) {
			if(aconf->oflags & oper_oflags[j].oflag) {	/* we match this oflag */
				if (!announce && oper_oflags[j].announce) { /* we haven't matched an oper_type yet */
					host = *oper_oflags[j].host;	/* set the iNAH host */
					announce = oper_oflags[j].announce; /* set the announcement */
				}
				sptr->umodes |= 
					*oper_oflags[j].umode; /* add the umode for this oflag */
			}
			j++;
		}

		sptr->oflag = aconf->oflags;
		if ((aconf->oflags & OFLAG_HIDE) && iNAH && !BadPtr(host)) {
			iNAH_host(sptr, host);
			SetHidden(sptr);
		}

		if (!IsOper(sptr))
		{
			sptr->umodes |= UMODE_LOCOP;
			if ((aconf->oflags & OFLAG_HIDE) && iNAH && !BadPtr(locop_host)) {
				iNAH_host(sptr, locop_host);
				SetHidden(sptr);
			}
			sendto_ops("%s (%s@%s) is now a local operator (o)",
			    parv[0], sptr->user->username,
			    IsHidden(sptr) ? sptr->user->virthost : sptr->user->realhost);
				
		}


		if (announce != NULL) {
			sendto_ops
			    ("%s (%s@%s) [%s] %s",
			    parv[0], sptr->user->username,
			    IsHidden(sptr) ? sptr->user->virthost : sptr->
			    user->realhost, parv[1], announce);
				sendto_serv_butone(&me,
				    ":%s GLOBOPS :%s (%s@%s) [%s] %s",
				    me.name, parv[0], sptr->user->username,
				    IsHidden(sptr) ? sptr->
				    user->virthost : sptr->user->realhost, parv[1], announce);

		} 
		if (!aconf->snomask)
			set_snomask(sptr, SNO_DEFOPER);
		else
			set_snomask(sptr, aconf->snomask);
		send_umode_out(cptr, sptr, old);
		sendto_one(sptr, rpl_str(RPL_SNOMASK),
			me.name, parv[0], get_sno_str(sptr));

#ifndef NO_FDLIST
		addto_fdlist(sptr->slot, &oper_fdlist);
#endif
		sendto_one(sptr, rpl_str(RPL_YOUREOPER), me.name, parv[0]);
		if (IsInvisible(sptr) && !(old & UMODE_INVISIBLE))
			IRCstats.invisible++;
		if (IsOper(sptr) && !IsHideOper(sptr))
			IRCstats.operators++;

		if (SHOWOPERMOTD == 1)
			m_opermotd(cptr, sptr, parc, parv);
		if (!BadPtr(OPER_AUTO_JOIN_CHANS)
		    && strcmp(OPER_AUTO_JOIN_CHANS, "0"))
		{
			char *chans[3] = {
				sptr->name,
				OPER_AUTO_JOIN_CHANS,
				NULL
			};
			(void)m_join(cptr, sptr, 3, chans);
		}
		ircd_log(LOG_OPER, "OPER (%s) by (%s!%s@%s)", name, parv[0], sptr->user->username,
			sptr->sockhost);

	}
	if (i == -1)
	{
		sendto_one(sptr, err_str(ERR_PASSWDMISMATCH), me.name, parv[0]);
		if (FAILOPER_WARN)
			sendto_one(sptr,
			    ":%s %s %s :*** Your attempt has been logged.", me.name,
			    IsWebTV(sptr) ? "PRIVMSG" : "NOTICE", sptr->name);
		sendto_realops
		    ("Failed OPER attempt by %s (%s@%s) using UID %s [FAILEDAUTH]",
		    parv[0], sptr->user->username, sptr->sockhost, name);
		sendto_serv_butone(&me,
		    ":%s GLOBOPS :Failed OPER attempt by %s (%s@%s) using UID %s [---]",
		    me.name, parv[0], sptr->user->username, sptr->sockhost,
		    name);
		sptr->since += 7;
	}
	/* Belay that order, number One. (-2) */
	return 0;
}