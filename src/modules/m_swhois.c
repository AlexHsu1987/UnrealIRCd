/*
 *   IRC - Internet Relay Chat, src/modules/m_swhois.c
 *   (C) 2001 The UnrealIRCd Team
 *
 *   SWHOIS command
 *
 *   See file AUTHORS in IRC package for additional names of
 *   the programmers.
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
#ifdef STRIPBADWORDS
#include "badwords.h"
#endif
#ifdef _WIN32
#include "version.h"
#endif

DLLFUNC int m_swhois(aClient *cptr, aClient *sptr, int parc, char *parv[]);

#define MSG_SWHOIS 	"SWHOIS"	
#define TOK_SWHOIS 	"BA"	

#ifndef STATIC_LINKING

#ifndef DYNAMIC_LINKING
ModuleInfo m_swhois_info
#else
#define m_swhois_info mod_header
ModuleInfo mod_header
#endif
  = {
  	2,
	"test",
	"$Id$",
	"command /swhois", 
	NULL,
	NULL 
    };

#ifdef DYNAMIC_LINKING
DLLFUNC void	mod_init(void)
#else
void    m_swhois_init(void)
#endif
{
	add_Command(MSG_SWHOIS, TOK_SWHOIS, m_swhois, MAXPARA);
}

#ifdef DYNAMIC_LINKING
DLLFUNC void	mod_load(void)
#else
void    m_swhois_load(void)
#endif
{
}

#ifdef DYNAMIC_LINKING
DLLFUNC void	mod_unload(void)
#else
void	m_swhois_unload(void)
#endif
{
	if (del_Command(MSG_SWHOIS, TOK_SWHOIS, m_swhois) < 0)
	{
		sendto_realops("Failed to delete commands when unloading %s",
				m_swhois_info.name);
	}
}
/*
 * m_swhois
 * parv[1] = nickname
 * parv[2] = new swhois
 *
*/

int m_swhois(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
        aClient *acptr;

        if (!IsServer(sptr) && !IsULine(sptr))
                return 0;
        if (parc < 3)
                return 0;
        acptr = find_person(parv[1], (aClient *)NULL);
        if (!acptr)
                return 0;

        if (acptr->user->swhois)
                MyFree(acptr->user->swhois);
        acptr->user->swhois = MyMalloc(strlen(parv[2]) + 1);
        ircsprintf(acptr->user->swhois, "%s", parv[2]);
        sendto_serv_butone_token(cptr, sptr->name,
           MSG_SWHOIS, TOK_SWHOIS, "%s :%s", parv[1], parv[2]);
        return 0;
}
