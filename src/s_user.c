/*
 *   Unreal Internet Relay Chat Daemon, src/s_user.c
 *   Copyright (C) 1990 Jarkko Oikarinen and
 *                      University of Oulu, Computing Center
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

#ifndef lint
static char sccsid[] =
    "@(#)s_user.c	2.74 2/8/94 (C) 1988 University of Oulu, \
Computing Center and Jarkko Oikarinen";
#endif
#include "config.h"
#include "struct.h"
#include "common.h"
#include "sys.h"
#include "numeric.h"
#include "msg.h"
#include "channel.h"
#include "userload.h"
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

void send_umode_out PROTO((aClient *, aClient *, long));
void send_umode_out_nickv2 PROTO((aClient *, aClient *, long));
void send_umode PROTO((aClient *, aClient *, long, long, char *));
static is_silenced PROTO((aClient *, aClient *));
/* static  Link    *is_banned PROTO((aClient *, aChannel *)); */

int  sendanyways = 0;
int  dontspread = 0;
extern char *me_hash;
extern ircstats IRCstats;
extern char backupbuf[];
static char buf[BUFSIZE], buf2[BUFSIZE];
static int user_modes[] = { UMODE_OPER, 'o',
	UMODE_LOCOP, 'O',
	UMODE_INVISIBLE, 'i',
	UMODE_WALLOP, 'w',
	UMODE_FAILOP, 'g',
	UMODE_HELPOP, 'h',
	UMODE_SERVNOTICE, 's',
	UMODE_KILLS, 'k',
	UMODE_SERVICES, 'S',
	UMODE_SADMIN, 'a',
	UMODE_HIDEOPER, 'H',
	UMODE_ADMIN, 'A',
	UMODE_NETADMIN, 'N',
	UMODE_TECHADMIN, 'T',
	UMODE_CLIENT, 'c',
	UMODE_COADMIN, 'C',
	UMODE_FLOOD, 'f',
	UMODE_REGNICK, 'r',
	UMODE_HIDE, 'x',
	UMODE_EYES, 'e',
	UMODE_WHOIS, 'W',
	UMODE_KIX, 'q',
	UMODE_BOT, 'B',
	UMODE_FCLIENT, 'F',
	UMODE_HIDING, 'I',
	UMODE_SECURE, 'z',
	UMODE_DEAF, 'd',
	UMODE_VICTIM, 'v',
	UMODE_SETHOST, 't',
#ifdef STRIPBADWORDS
	UMODE_STRIPBADWORDS, 'G',
#endif
	UMODE_JUNK, 'j',
	0, 0
};

void iNAH_host(aClient *sptr, char *host)
{
	if (!sptr->user)
		return;
	if (sptr->user->virthost)
		MyFree(sptr->user->virthost);
	sptr->user->virthost = MyMalloc(strlen(host) + 1);
	ircsprintf(sptr->user->virthost, "%s", host);
	if (MyConnect(sptr))
		sendto_serv_butone_token(&me, sptr->name, MSG_SETHOST,
		    TOK_SETHOST, "%s", sptr->user->virthost);
	sptr->umodes |= UMODE_SETHOST;
}

long set_usermode(char *umode)
{
	int  newumode;
	int  what;
	char **p, *m;
	int  flag;
	int *s;

	newumode = 0;
	what = MODE_ADD;
	for (m = umode; *m; m++)
		switch (*m)
		{
		  case '+':
			  what = MODE_ADD;
			  break;
		  case '-':
			  what = MODE_DEL;
			  break;
		  case ' ':
		  case '\n':
		  case '\r':
		  case '\t':
			  break;
		  default:
			  for (s = user_modes; (flag = *s); s += 2)
				  if (*m == (char)(*(s + 1)))
				  {
					  if (what == MODE_ADD)
					  {
						  newumode |= flag;
					  }
					  else
					  {
						  newumode &= ~flag;
					  }
					  break;
				  }
		}

	return (newumode);
}

/*
** m_functions execute protocol messages on this server:
**
**	cptr	is always NON-NULL, pointing to a *LOCAL* client
**		structure (with an open socket connected!). This
**		identifies the physical socket where the message
**		originated (or which caused the m_function to be
**		executed--some m_functions may call others...).
**
**	sptr	is the source of the message, defined by the
**		prefix part of the message if present. If not
**		or prefix not found, then sptr==cptr.
**
**		(!IsServer(cptr)) => (cptr == sptr), because
**		prefixes are taken *only* from servers...
**
**		(IsServer(cptr))
**			(sptr == cptr) => the message didn't
**			have the prefix.
**
**			(sptr != cptr && IsServer(sptr) means
**			the prefix specified servername. (?)
**
**			(sptr != cptr && !IsServer(sptr) means
**			that message originated from a remote
**			user (not local).
**
**		combining
**
**		(!IsServer(sptr)) means that, sptr can safely
**		taken as defining the target structure of the
**		message in this server.
**
**	*Always* true (if 'parse' and others are working correct):
**
**	1)	sptr->from == cptr  (note: cptr->from == cptr)
**
**	2)	MyConnect(sptr) <=> sptr == cptr (e.g. sptr
**		*cannot* be a local connection, unless it's
**		actually cptr!). [MyConnect(x) should probably
**		be defined as (x == x->from) --msa ]
**
**	parc	number of variable parameter strings (if zero,
**		parv is allowed to be NULL)
**
**	parv	a NULL terminated list of parameter pointers,
**
**			parv[0], sender (prefix string), if not present
**				this points to an empty string.
**			parv[1]...parv[parc-1]
**				pointers to additional parameters
**			parv[parc] == NULL, *always*
**
**		note:	it is guaranteed that parv[0]..parv[parc-1] are all
**			non-NULL pointers.
*/

#ifndef NO_FDLIST
extern fdlist oper_fdlist;
#endif

/* Taken from xchat by Peter Zelezny
 * changed very slightly by codemastr
 */

unsigned char *StripColors(unsigned char *text) {
	int nc = 0, col = 0, i = 0, len = strlen(text);
	unsigned char *new_str = malloc(len + 2);

	while (len > 0) {
		if ((col && isdigit(*text) && nc < 2) || (col && *text == ',' && nc < 3)) {
			nc++;
			if (*text == ',')
				nc = 0;
		}
		else {
			if (col)
				col = 0;
			if (*text == '\003') {
				col = 1;
				nc = 0;
			}
			else {
				new_str[i] = *text;
				i++;
			}
		}
		text++;
		len--;
	}
	new_str[i] = 0;
	return new_str;
}


char umodestring[512];

void make_umodestr(void)
{

	int *s;
	char *m;

	m = umodestring;

	for (s = user_modes; *s; s += 2)
	{
		*m++ = (char)(*(s + 1));
	}

	*m = '\0';
}

/*
** next_client
**	Local function to find the next matching client. The search
**	can be continued from the specified client entry. Normal
**	usage loop is:
**
**	for (x = client; x = next_client(x,mask); x = x->next)
**		HandleMatchingClient;
**
*/
aClient *next_client(next, ch)
	aClient *next;		/* First client to check */
	char *ch;		/* search string (may include wilds) */
{
	aClient *tmp = next;

	next = find_client(ch, tmp);
	if (tmp && tmp->prev == next)
		return NULL;
	if (next != tmp)
		return next;
	for (; next; next = next->next)
	{
		if (!match(ch, next->name) || !match(next->name, ch))
			break;
	}
	return next;
}

/*
** hunt_server
**
**	Do the basic thing in delivering the message (command)
**	across the relays to the specific server (server) for
**	actions.
**
**	Note:	The command is a format string and *MUST* be
**		of prefixed style (e.g. ":%s COMMAND %s ...").
**		Command can have only max 8 parameters.
**
**	server	parv[server] is the parameter identifying the
**		target server.
**
**	*WARNING*
**		parv[server] is replaced with the pointer to the
**		real servername from the matched client (I'm lazy
**		now --msa).
**
**	returns: (see #defines)
*/
int  hunt_server(cptr, sptr, command, server, parc, parv)
	aClient *cptr, *sptr;
	char *command, *parv[];
	int  server, parc;
{
	aClient *acptr;

	/*
	   ** Assume it's me, if no server
	 */
	if (parc <= server || BadPtr(parv[server]) ||
	    match(me.name, parv[server]) == 0 ||
	    match(parv[server], me.name) == 0)
		return (HUNTED_ISME);
	/*
	   ** These are to pickup matches that would cause the following
	   ** message to go in the wrong direction while doing quick fast
	   ** non-matching lookups.
	 */
	if ((acptr = find_client(parv[server], NULL)))
		if (acptr->from == sptr->from && !MyConnect(acptr))
			acptr = NULL;
	if (!acptr && (acptr = find_server_quick(parv[server])))
		if (acptr->from == sptr->from && !MyConnect(acptr))
			acptr = NULL;
	if (!acptr)
		for (acptr = client, (void)collapse(parv[server]);
		    (acptr = next_client(acptr, parv[server]));
		    acptr = acptr->next)
		{
			if (acptr->from == sptr->from && !MyConnect(acptr))
				continue;
			/*
			 * Fix to prevent looping in case the parameter for
			 * some reason happens to match someone from the from
			 * link --jto
			 */
			if (IsRegistered(acptr) && (acptr != cptr))
				break;
		}
	if (acptr)
	{
		if (IsMe(acptr) || MyClient(acptr))
			return HUNTED_ISME;
		if (match(acptr->name, parv[server]))
			parv[server] = acptr->name;
		sendto_one(acptr, command, parv[0],
		    parv[1], parv[2], parv[3], parv[4],
		    parv[5], parv[6], parv[7], parv[8]);
		return (HUNTED_PASS);
	}
	sendto_one(sptr, err_str(ERR_NOSUCHSERVER), me.name,
	    parv[0], parv[server]);
	return (HUNTED_NOSUCH);
}


/*
** check_for_target_limit
**
** Return Values:
** True(1) == too many targets are addressed
** False(0) == ok to send message
**
*/
int  check_for_target_limit(aClient *sptr, void *target, const char *name)
{
#ifndef _WIN32			/* This is not windows compatible */
	u_char *p;
#ifndef __alpha
	u_int tmp = ((u_int)target & 0xffff00) >> 8;
#else
	u_int tmp = ((u_long)target & 0xffff00) >> 8;
#endif
	u_char hash = (tmp * tmp) >> 12;

	if (IsAnOper(sptr))
		return 0;
	if (sptr->targets[0] == hash)
		return 0;

	for (p = sptr->targets; p < &sptr->targets[MAXTARGETS - 1];)
		if (*++p == hash)
		{
			memmove(&sptr->targets[1], &sptr->targets[0],
			    p - sptr->targets);
			sptr->targets[0] = hash;
			return 0;
		}

	if (TStime() < sptr->nexttarget)
	{
		if (sptr->nexttarget - TStime() < TARGET_DELAY + 8)
		{
			sptr->nexttarget += 2;
			sendto_one(sptr, err_str(ERR_TARGETTOOFAST),
			    me.name, sptr->name, name, sptr->nexttarget - TStime());
		}
		return 1;
	}
	else
	{
		sptr->nexttarget += TARGET_DELAY;
		if (sptr->nexttarget < TStime() - (TARGET_DELAY * (MAXTARGETS - 1)))
			sptr->nexttarget =
			    TStime() - (TARGET_DELAY * (MAXTARGETS - 1));
	}
	memmove(&sptr->targets[1], &sptr->targets[0], MAXTARGETS - 1);
	sptr->targets[0] = hash;
#endif
	return 0;
}




/*
** 'do_nick_name' ensures that the given parameter (nick) is
** really a proper string for a nickname (note, the 'nick'
** may be modified in the process...)
**
**	RETURNS the length of the final NICKNAME (0, if
**	nickname is illegal)
**
**  Nickname characters are in range
**	'A'..'}', '_', '-', '0'..'9'
**  anything outside the above set will terminate nickname.
**  In addition, the first character cannot be '-'
**  or a Digit.
**
**  Note:
**	'~'-character should be allowed, but
**	a change should be global, some confusion would
**	result if only few servers allowed it...
*/
#if defined(CHINESE_NICK) || defined(JAPANESE_NICK)
/* Chinese Nick Verification Code - Added by RexHsu on 08/09/00 (beta2)
 * Now Support All GBK Words,Thanks to Mr.WebBar <climb@guomai.sh.cn>!
 * Special Char Bugs Fixed by RexHsu 09/01/00 I dont know whether it is
 * okay now?May I am right ;p
 * Thanks dilly for providing me Japanese code range!
 * Now I am meeting a nickname conflicting problem....
 *
 * GBK Libary Reference:
 * 1. GBK2312�Ǻ��ַ�����(A1A1----A9FE)
 * 2. GBK2312������(B0A1----F7FE)
 * 3. GBK���人����(8140----A0FE)
 * 4. GBK���人����(AA40----FEA0)
 * 5. GBK����Ǻ�����(A840----A9A0)
 * 6. ����ƽ����������(a4a1-a4f3) -->work correctly?maybe...
 * 7. ����Ƭ����������(a5a1-a5f7) -->work correctly?maybe...
 * 8. ���ı�����(xxxx-yyyy)
 */
int  isvalidChinese(const unsigned char c1, const unsigned char c2)
{
	const unsigned int GBK_S = 0xb0a1;
	const unsigned int GBK_E = 0xf7fe;
	const unsigned int GBK_2_S = 0x8140;
	const unsigned int GBK_2_E = 0xa0fe;
	const unsigned int GBK_3_S = 0xaa40;
	const unsigned int GBK_3_E = 0xfea0;
	const unsigned int JPN_PING_S = 0xa4a1;
	const unsigned int JPN_PING_E = 0xa4f3;
	const unsigned int JPN_PIAN_S = 0xa5a1;
	const unsigned int JPN_PIAN_E = 0xa5f7;
	unsigned int AWord = c1 * 256 + c2;
#if defined(CHINESE_NICK) && defined(JAPANESE_NICK)
	return (AWord >= GBK_S && AWord <= GBK_E || AWord >= GBK_2_S
	    && AWord <= GBK_2_E || AWord >= JPN_PING_S && AWord <= JPN_PING_E
	    || AWord >= JPN_PIAN_S && AWord <= JPN_PIAN_E) ? 1 : 0;
#endif
#if defined(CHINESE_NICK) && !defined(JAPANESE_NICK)
	return (AWord >= GBK_S && AWord <= GBK_E || AWord >= GBK_2_S
	    && AWord <= GBK_2_E ? 1 : 0);
#endif
#if !defined(CHINESE_NICK) && defined(JAPANESE_NICK)
	return (AWord >= JPN_PING_S && AWord <= JPN_PING_E
	    || AWord >= JPN_PIAN_S && AWord <= JPN_PIAN_E) ? 1 : 0;
#endif

}

/* Chinese Nick Supporting Code (Switch Mode) - Modified by RexHsu on 08/09/00 */
int  do_nick_name(char *pnick)
{
	unsigned char *ch;
	unsigned char *nick = pnick;
	int  firstChineseChar = 0;
	char lastChar;

	if (*nick == '-' || isdigit(*nick))	/* first character in [0..9-] */
		return 0;

	for (ch = nick; *ch && (ch - nick) < NICKLEN; ch++)
	{
		if ((!isvalid(*ch) && !((*ch) & 0x80)) || isspace(*ch)
		    || (*ch) == '@' || (*ch) == '!' || (*ch) == ':'
		    || (*ch) == ' ')
			break;
		if (firstChineseChar)
		{
			if (!isvalidChinese(lastChar, *ch))
				break;
			firstChineseChar = 0;
		}
		else if ((*ch) & 0x80)
			firstChineseChar = 1;
		lastChar = *ch;
	}

	if (firstChineseChar)
		ch--;

	*ch = '\0';

	return (ch - nick);
}


#else
int  do_nick_name(nick)
	char *nick;
{
	char *ch;

	if (*nick == '-' || isdigit(*nick))	/* first character in [0..9-] */
		return 0;

	for (ch = nick; *ch && (ch - nick) < NICKLEN; ch++)
		if (!isvalid(*ch) || isspace(*ch))
			break;

	*ch = '\0';

	return (ch - nick);
}
#endif

/*
** canonize
**
** reduce a string of duplicate list entries to contain only the unique
** items.  Unavoidably O(n^2).
*/
char *canonize(buffer)
	char *buffer;
{
	static char cbuf[BUFSIZ];
	char *s, *t, *cp = cbuf;
	int  l = 0;
	char *p = NULL, *p2;

	*cp = '\0';

	for (s = strtoken(&p, buffer, ","); s; s = strtoken(&p, NULL, ","))
	{
		if (l)
		{
			for (p2 = NULL, t = strtoken(&p2, cbuf, ","); t;
			    t = strtoken(&p2, NULL, ","))
				if (!mycmp(s, t))
					break;
				else if (p2)
					p2[-1] = ',';
		}
		else
			t = NULL;
		if (!t)
		{
			if (l)
				*(cp - 1) = ',';
			else
				l = 1;
			(void)strcpy(cp, s);
			if (p)
				cp += (p - s);
		}
		else if (p2)
			p2[-1] = ',';
	}
	return cbuf;
}

int  m_remgline(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
	if (!IsOper(sptr))
		return 0;

	sendto_one(sptr,
	    ":%s NOTICE %s :*** Please use /gline -mask instead of /Remgline",
	    me.name, sptr->name);
}

extern char cmodestring[512];

int  m_post(aClient *cptr, aClient *sptr, int parc, char *parv[])
{
	char *tkllayer[9] = {
		me.name,	/*0  server.name */
		"+",		/*1  +|- */
		"z",		/*2  G   */
		"*",		/*3  user */
		NULL,		/*4  host */
		NULL,
		NULL,		/*6  expire_at */
		NULL,		/*7  set_at */
		NULL		/*8  reason */
	};
	char hostip[128], mo[128], mo2[128];

	if (!MyClient(sptr))
		return 0;

	if (IsRegistered(sptr))
		return 0;

	strcpy(hostip, (char *)inetntoa((char *)&sptr->ip));

	sendto_one(sptr,
	    ":%s NOTICE AUTH :*** Proxy connection detected (bad!)", me.name);
	sendto_umode(UMODE_EYES, "Attempted WWW Proxy connect from client %s",
	    get_client_host(sptr));
	exit_client(cptr, sptr, &me, "HTTP proxy connection");

	tkllayer[4] = hostip;
	tkllayer[5] = me.name;
	ircsprintf(mo, "%li", iConf.socksbantime + TStime());
	ircsprintf(mo2, "%li", TStime());
	tkllayer[6] = mo;
	tkllayer[7] = mo2;
	tkllayer[8] = "HTTP Proxy";
	return m_tkl(&me, &me, 9, tkllayer);
}

/*
** register_user
**	This function is called when both NICK and USER messages
**	have been accepted for the client, in whatever order. Only
**	after this the USER message is propagated.
**
**	NICK's must be propagated at once when received, although
**	it would be better to delay them too until full info is
**	available. Doing it is not so simple though, would have
**	to implement the following:
**
**	1) user telnets in and gives only "NICK foobar" and waits
**	2) another user far away logs in normally with the nick
**	   "foobar" (quite legal, as this server didn't propagate
**	   it).
**	3) now this server gets nick "foobar" from outside, but
**	   has already the same defined locally. Current server
**	   would just issue "KILL foobar" to clean out dups. But,
**	   this is not fair. It should actually request another
**	   nick from local user or kill him/her...
*/
extern aTKline *tklines;
extern int badclass;

static int register_user(cptr, sptr, nick, username, umode, virthost)
	aClient *cptr;
	aClient *sptr;
	char *nick, *username, *virthost, *umode;
{
	ConfigItem_ban *bconf;
	char *parv[3], *tmpstr, *encr;
#ifdef HOSTILENAME
	char stripuser[USERLEN + 1], *u1 = stripuser, *u2, olduser[USERLEN + 1],
	    userbad[USERLEN * 2 + 1], *ubad = userbad, noident = 0;
#endif
	int  xx;
	anUser *user = sptr->user;
	aClient *nsptr;
	int  i;
	char mo[256], mo2[256];
	char *tmpx;
	char *tkllayer[9] = {
		me.name,	/*0  server.name */
		"+",		/*1  +|- */
		"z",		/*2  G   */
		"*",		/*3  user */
		NULL,		/*4  host */
		NULL,
		NULL,		/*6  expire_at */
		NULL,		/*7  set_at */
		NULL		/*8  reason */
	};
	ConfigItem_tld *tlds;
	user->last = TStime();
	parv[0] = sptr->name;
	parv[1] = parv[2] = NULL;

	if (MyConnect(sptr))
	{
#ifdef SOCKSPORT
		if (sptr->flags & FLAGS_GOTSOCKS)
		{
			char hostip[128];
#ifndef INET6
			strcpy(hostip, (char *)inetntoa((char *)&sptr->ip));
#else
			strcpy(hostip,
			    (char *)inetntop(AF_INET6,
			    (char *)&sptr->ip, mydummy, MYDUMMY_SIZE));
#endif

			sendto_one(sptr,
			    ":%s NOTICE AUTH :*** Found open SOCKS server (bad)",
			    me.name);

			sendto_umode(UMODE_EYES,
			    "Open SOCKS server from client %s",
			    get_client_host(sptr));



			i =
			    exit_client(cptr, sptr, &me,
			    iConf.socksquitmessage);

			tkllayer[4] = hostip;
			tkllayer[5] = me.name;
			ircsprintf(mo, "%li", iConf.socksbantime + TStime());
			ircsprintf(mo2, "%li", TStime());
			tkllayer[6] = mo;
			tkllayer[7] = mo2;
			tkllayer[8] = iConf.socksbanmessage;
			m_tkl(&me, &me, 9, tkllayer);
			return i;
		}

#endif /* SOCKSPORT */

		if ((i = check_client(sptr)))
		{
			/* This had return i; before -McSkaf */
			if (i == -5)
				return FLUSH_BUFFER;

			sendto_umode(UMODE_OPER | UMODE_CLIENT,
			    "*** Notice -- %s from %s.",
			    i == -3 ? "Too many connections" :
			    "Unauthorized connection", get_client_host(sptr));
			ircstp->is_ref++;
			ircsprintf(mo, "This server is full.");
			return
			    exit_client(cptr, sptr, &me,
			    i ==
			    -3 ? mo :
			    "You are not authorized to connect to this server");
		}
		if (sptr->hostp)
		{
			/* No control-chars or ip-like dns replies... I cheat :)
			   -- OnyxDragon */
			for (tmpstr = sptr->sockhost; *tmpstr > ' ' &&
			    *tmpstr < 127; tmpstr++);
			if (*tmpstr || !*user->realhost
			    || isdigit(*(tmpstr - 1)))
				strncpyzt(sptr->sockhost,
				    (char *)inetntoa((char *)&sptr->ip), sizeof(sptr->sockhost));	/* Fix the sockhost for debug jic */
			strncpyzt(user->realhost, sptr->sockhost,
			    sizeof(sptr->sockhost));
		}
		else		/* Failsafe point, don't let the user define their
				   own hostname via the USER command --Cabal95 */
			strncpyzt(user->realhost, sptr->sockhost, HOSTLEN + 1);
		strncpyzt(user->realhost, user->realhost,
		    sizeof(user->realhost));
		/*
		 * I do not consider *, ~ or ! 'hostile' in usernames,
		 * as it is easy to differentiate them (Use \*, \? and \\)
		 * with the possible?
		 * exception of !. With mIRC etc. ident is easy to fake
		 * to contain @ though, so if that is found use non-ident
		 * username. -Donwulff
		 *
		 * I do, We only allow a-z A-Z 0-9 _ - and . now so the
		 * !strchr(sptr->username, '@') check is out of date. -Cabal95
		 *
		 * Moved the noident stuff here. -OnyxDragon
		 */
		if (!(sptr->flags & FLAGS_DOID))
			strncpyzt(user->username, username, USERLEN + 1);
		else if (sptr->flags & FLAGS_GOTID)
			strncpyzt(user->username, sptr->username, USERLEN + 1);
		else
		{
			/* because username may point to user->username */
			char temp[USERLEN + 1];

			strncpyzt(temp, username, USERLEN + 1);
#ifdef NO_IDENT_CHECKING
			strncpy(user->username, temp, USERLEN);
			user->username[USERLEN] = '\0';
#else
			*user->username = '~';
			(void)strncpy(&user->username[1], temp, USERLEN);
			user->username[USERLEN] = '\0';
#endif
#ifdef HOSTILENAME
			noident = 1;
#endif
		}
#ifdef HOSTILENAME
		/*
		 * Limit usernames to just 0-9 a-z A-Z _ - and .
		 * It strips the "bad" chars out, and if nothing is left
		 * changes the username to the first 8 characters of their
		 * nickname. After the MOTD is displayed it sends numeric
		 * 455 to the user telling them what(if anything) happened.
		 * -Cabal95
		 *
		 * Moved the noident thing to the right place - see above
		 * -OnyxDragon
		 */
		for (u2 = user->username + noident; *u2; u2++)
		{
			if (isallowed(*u2))
				*u1++ = *u2;
			else if (*u2 < 32)
			{
				/*
				 * Make sure they can read what control
				 * characters were in their username.
				 */
				*ubad++ = '^';
				*ubad++ = *u2 + '@';
			}
			else
				*ubad++ = *u2;
		}
		*u1 = '\0';
		*ubad = '\0';
		if (strlen(stripuser) != strlen(user->username + noident))
		{
			if (stripuser[0] == '\0')
			{
				strncpy(stripuser, cptr->name, 8);
				stripuser[8] = '\0';
			}

			strcpy(olduser, user->username + noident);
			strncpy(user->username + 1, stripuser, USERLEN - 1);
			user->username[0] = '~';
			user->username[USERLEN] = '\0';
		}
		else
			u1 = NULL;
#endif

#ifdef OLD
		if (!BadPtr(aconf->passwd) && !StrEq("ONE", aconf->passwd))
		{
/* I:line password encryption --codemastr */
#ifdef CRYPT_ILINE_PASSWORD
			if (sptr->passwd)
			{
				char salt[3];
				extern char *crypt();

				salt[0] = aconf->passwd[0];
				salt[1] = aconf->passwd[1];
				salt[3] = '\0';

				encr = crypt(sptr->passwd, salt);
			}
			else
				encr = "";
#else
			encr = sptr->passwd;
#endif
			if (!encr || !StrEq(encr, aconf->passwd))
			{
				ircstp->is_ref++;
				sendto_one(sptr, err_str(ERR_PASSWDMISMATCH),
				    me.name, parv[0]);
				return exit_client(cptr, sptr, &me,
				    "Bad Password");
			}
			/* .. Else password check was successful, clear the pass
			 * so it doesn't get sent to NickServ.
			 * - Wizzu
			 */
			else
			{
				MyFree(sptr->passwd);
				sptr->passwd = NULL;
			}
		}
#endif
		/*
		 * following block for the benefit of time-dependent K:-lines
		 */
		if ((bconf =
		    Find_ban(make_user_host(user->username, user->realhost),
		    CONF_BAN_USER)))
		{
			ircstp->is_ref++;
			sendto_one(cptr,
			    ":%s %d %s :*** You are not welcome on this server (%s)"
			    " Email %s for more information.",
			    me.name, ERR_YOUREBANNEDCREEP,
			    cptr->name, bconf->reason ? bconf->reason : "",
			    KLINE_ADDRESS);
			return exit_client(cptr, cptr, cptr, "You are banned");
		}
		if ((bconf = Find_ban(sptr->info, CONF_BAN_REALNAME)))
		{
			ircstp->is_ref++;
			sendto_one(cptr,
			    ":%s %d %s :*** Your GECOS (real name) is not allowed on this server (%s)"
			    " Please change it and reconnect",
			    me.name, ERR_YOUREBANNEDCREEP,
			    cptr->name, bconf->reason ? bconf->reason : "",
			    KLINE_ADDRESS);

			return exit_client(cptr, sptr, &me,
			    "Your GECOS (real name) is banned from this server");
		}
		tkl_check_expire(NULL);
		if ((xx = find_tkline_match(sptr, 0)) != -1)
		{
			ircstp->is_ref++;
			return xx;
		}
	}
	else
	{
		strncpyzt(user->username, username, USERLEN + 1);
	}
	SetClient(sptr);
	IRCstats.clients++;
	if (sptr->srvptr && sptr->srvptr->serv)
		sptr->srvptr->serv->users++;
	user->virthost =
	    (char *)make_virthost(user->realhost, user->virthost, 1);
	if (MyConnect(sptr))
	{
		IRCstats.unknown--;
		IRCstats.me_clients++;
		ircd_log(LOG_CLIENT, "Connect - %s!%s@%s", nick, user->username, user->realhost);
		sendto_one(sptr, rpl_str(RPL_WELCOME), me.name, nick,
		    ircnetwork, nick, user->username, user->realhost);
		/* This is a duplicate of the NOTICE but see below... */
		sendto_one(sptr, rpl_str(RPL_YOURHOST), me.name, nick,
		    me.name, version);
		sendto_one(sptr, rpl_str(RPL_CREATED), me.name, nick, creation);
		if (!(sptr->listener->umodes & LISTENER_JAVACLIENT))
#ifndef _WIN32
			sendto_one(sptr, rpl_str(RPL_MYINFO), me.name, parv[0],
			    me.name, version, umodestring, cmodestring);
#else
			sendto_one(sptr, rpl_str(RPL_MYINFO), me.name, parv[0],
			    me.name, "Unreal" VERSIONONLY, umodestring,
			    cmodestring);
#endif
		sendto_one(sptr, rpl_str(RPL_PROTOCTL), me.name, nick,
		    PROTOCTL_PARAMETERS);
#ifdef USE_SSL
		if (sptr->flags & FLAGS_SSL)
			if (sptr->ssl)
				sendto_one(sptr,
				    ":%s NOTICE %s :*** You are connected to %s with %s",
				    me.name, sptr->name, me.name,
				    ssl_get_cipher(sptr->ssl));
#endif
		(void)m_lusers(sptr, sptr, 1, parv);
		(void)m_motd(sptr, sptr, 1, parv);
#ifdef EXPERIMENTAL
		sendto_one(sptr,
		    ":%s NOTICE %s :*** \2NOTE:\2 This server (%s) is running experimental IRC server software. If you find any bugs or problems, please mail unreal-dev@lists.sourceforge.net about it",
		    me.name, sptr->name, me.name);
#endif
#ifdef HOSTILENAME
		/*
		 * Now send a numeric to the user telling them what, if
		 * anything, happened.
		 */
		if (u1)
			sendto_one(sptr, err_str(ERR_HOSTILENAME), me.name,
			    sptr->name, olduser, userbad, stripuser);
#endif
		nextping = TStime();
		sendto_connectnotice(nick, user, sptr);
		if (IsSecure(sptr))
			sptr->umodes |= UMODE_SECURE;
	}
	else if (IsServer(cptr))
	{
		aClient *acptr;

		if (!(acptr = (aClient *)find_server_quick(user->server)))
		{
			sendto_ops
			    ("Bad USER [%s] :%s USER %s %s : No such server",
			    cptr->name, nick, user->username, user->server);
			sendto_one(cptr, ":%s KILL %s :%s (No such server: %s)",
			    me.name, sptr->name, me.name, user->server);
			sptr->flags |= FLAGS_KILLED;
			return exit_client(sptr, sptr, &me,
			    "USER without prefix(2.8) or wrong prefix");
		}
		else if (acptr->from != sptr->from)
		{
			sendto_ops("Bad User [%s] :%s USER %s %s, != %s[%s]",
			    cptr->name, nick, user->username, user->server,
			    acptr->name, acptr->from->name);
			sendto_one(cptr, ":%s KILL %s :%s (%s != %s[%s])",
			    me.name, sptr->name, me.name, user->server,
			    acptr->from->name, acptr->from->sockhost);
			sptr->flags |= FLAGS_KILLED;
			return exit_client(sptr, sptr, &me,
			    "USER server wrong direction");
		}
		else
			sptr->flags |= (acptr->flags & FLAGS_TS8);
		/* *FINALL* this gets in ircd... -- Barubary */
		/* We change this a bit .. */
		if (IsULine(sptr->srvptr))
			sptr->flags |= FLAGS_ULINE;
	}
	if (sptr->umodes & UMODE_INVISIBLE)
	{
		IRCstats.invisible++;
	}

	if (virthost && umode)
	{
		tkllayer[0] = nick;
		tkllayer[1] = nick;
		tkllayer[2] = umode;
		dontspread = 1;
		m_mode(cptr, sptr, 3, tkllayer);
		dontspread = 0;
		if (virthost && *virthost != '*')
		{
			if (sptr->user->virthost)
				MyFree(sptr->user->virthost);
			sptr->user->virthost = MyMalloc(strlen(virthost) + 1);
			ircsprintf(sptr->user->virthost, virthost);
		}
	}

	hash_check_notify(sptr, RPL_LOGON);	/* Uglier hack */
	send_umode(NULL, sptr, 0, SEND_UMODES, buf);
	/* NICKv2 Servers ! */
	sendto_serv_butone_nickcmd(cptr, sptr, nick,
	    sptr->hopcount + 1, sptr->lastnick, user->username, user->realhost,
	    user->server, user->servicestamp, sptr->info,
	    (!buf || *buf == '\0' ? "+" : buf),
	    ((IsHidden(sptr)
	    && (sptr->umodes & UMODE_SETHOST)) ? sptr->user->virthost : "*"));

	/* Send password from sptr->passwd to NickServ for identification,
	 * if passwd given and if NickServ is online.
	 * - by taz, modified by Wizzu
	 */
	if (MyConnect(sptr))
	{
		if (sptr->passwd && (nsptr = find_person(NickServ, NULL)))
			sendto_one(nsptr, ":%s %s %s@%s :IDENTIFY %s",
			    sptr->name,
			    (IsToken(nsptr->from) ? TOK_PRIVATE : MSG_PRIVATE),
			    NickServ, SERVICES_NAME, sptr->passwd);
		/* Force the user to join the given chans -- codemastr */
		if (buf[0] != '\0' && buf[1] != '\0')
			sendto_one(cptr, ":%s MODE %s :%s", cptr->name,
			    cptr->name, buf);

		for (tlds = conf_tld; tlds; tlds = (ConfigItem_tld *) tlds->next) {
			if (!match(tlds->mask, cptr->user->realhost))
				break;
		}
		if (tlds && !BadPtr(tlds->channel)) {
			char *chans[3] = {
				sptr->name,
				tlds->channel,
				NULL
			};
			(void)m_join(sptr, sptr, 3, chans);
		}
		else if (!BadPtr(AUTO_JOIN_CHANS) && strcmp(AUTO_JOIN_CHANS, "0"))
		{
			char *chans[3] = {
				sptr->name,
				AUTO_JOIN_CHANS,
				NULL
			};
			(void)m_join(sptr, sptr, 3, chans);
		}
	}

	if (MyConnect(sptr) && !BadPtr(sptr->passwd))
	{
		MyFree(sptr->passwd);
		sptr->passwd = NULL;
	}
	return 0;
}

/*
** m_svsnick
**	parv[0] = sender
**	parv[1] = old nickname
**	parv[2] = new nickname
**	parv[3] = timestamp
*/
int  m_svsnick(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;

	if (!IsULine(sptr) || parc < 4 || (strlen(parv[2]) > NICKLEN))
		return -1;	/* This looks like an error anyway -Studded */

	if (!hunt_server(cptr, sptr, ":%s SVSNICK %s %s :%s", 1, parc,
	    parv) != HUNTED_ISME)
	{
		if ((acptr = find_person(parv[1], NULL)))
		{
			if (find_client(parv[2], NULL))	/* Collision */
				return exit_client(cptr, acptr, sptr,
				    "Nickname collision due to Services enforced "
				    "nickname change, your nick was overruled");
			if (do_nick_name(parv[2]) == 0)
				return 0;
			acptr->umodes &= ~UMODE_REGNICK;
			acptr->lastnick = TS2ts(parv[3]);
			sendto_common_channels(acptr, ":%s NICK :%s", parv[1],
			    parv[2]);
			if (IsPerson(acptr))
				add_history(acptr, 1);
			sendto_serv_butone_token(NULL, parv[1], MSG_NICK,
			    TOK_NICK, "%s :%i", parv[2], TS2ts(parv[3]));
			if (acptr->name[0])
			{
				(void)del_from_client_hash_table(acptr->name, acptr);
				if (IsPerson(acptr))
					hash_check_notify(acptr, RPL_LOGOFF);
			}
			if (MyClient(acptr))
			{
				RunHook2(HOOKTYPE_LOCAL_NICKCHANGE, acptr, parv[2]);
			}
			(void)strcpy(acptr->name, parv[2]);
			(void)add_to_client_hash_table(parv[2], acptr);
			if (IsPerson(acptr))
				hash_check_notify(acptr, RPL_LOGON);

		}
	}
	return 0;
}

/*
** m_nick
**	parv[0] = sender prefix
**	parv[1] = nickname
**  if from new client  -taz
**	parv[2] = nick password
**  if from server:
**      parv[2] = hopcount
**      parv[3] = timestamp
**      parv[4] = username
**      parv[5] = hostname
**      parv[6] = servername
**  if NICK version 1:
**      parv[7] = servicestamp
**	parv[8] = info
**  if NICK version 2:
**	parv[7] = servicestamp
**      parv[8] = umodes
**	parv[9] = virthost, * if none
**	parv[10] = info
*/
int  m_nick(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	ConfigItem_ban *aconf;
	aClient *acptr, *serv;
	aClient *acptrs;
	char nick[NICKLEN + 2], *s;
	Membership *mp;
	time_t lastnick = (time_t) 0;
	int  differ = 1;

	/*
	 * If the user didn't specify a nickname, complain
	 */
	if (parc < 2)
	{
		sendto_one(sptr, err_str(ERR_NONICKNAMEGIVEN),
		    me.name, parv[0]);
		return 0;
	}

	if (MyConnect(sptr) && (s = (char *)index(parv[1], '~')))
		*s = '\0';

	strncpyzt(nick, parv[1], NICKLEN + 1);
	/*
	 * if do_nick_name() returns a null name OR if the server sent a nick
	 * name and do_nick_name() changed it in some way (due to rules of nick
	 * creation) then reject it. If from a server and we reject it,
	 * and KILL it. -avalon 4/4/92
	 */
	if (do_nick_name(nick) == 0 ||
	    (IsServer(cptr) && strcmp(nick, parv[1])))
	{
		sendto_one(sptr, err_str(ERR_ERRONEUSNICKNAME),
		    me.name, parv[0], parv[1], "Illegal characters");

		if (IsServer(cptr))
		{
			ircstp->is_kill++;
			sendto_failops("Bad Nick: %s From: %s %s",
			    parv[1], parv[0], get_client_name(cptr, FALSE));
			sendto_one(cptr, ":%s KILL %s :%s (%s <- %s[%s])",
			    me.name, parv[1], me.name, parv[1],
			    nick, cptr->name);
			if (sptr != cptr)
			{	/* bad nick change */
				sendto_serv_butone(cptr,
				    ":%s KILL %s :%s (%s <- %s!%s@%s)",
				    me.name, parv[0], me.name,
				    get_client_name(cptr, FALSE),
				    parv[0],
				    sptr->user ? sptr->username : "",
				    sptr->user ? sptr->user->server :
				    cptr->name);
				sptr->flags |= FLAGS_KILLED;
				return exit_client(cptr, sptr, &me, "BadNick");
			}
		}
		return 0;
	}

	/*
	   ** Protocol 4 doesn't send the server as prefix, so it is possible
	   ** the server doesn't exist (a lagged net.burst), in which case
	   ** we simply need to ignore the NICK. Also when we got that server
	   ** name (again) but from another direction. --Run
	 */
	/*
	   ** We should really only deal with this for msgs from servers.
	   ** -- Aeto
	 */
	if (IsServer(cptr) &&
	    (parc > 7
	    && (!(serv = (aClient *)find_server_b64_or_real(parv[6], NULL))
	    || serv->from != cptr->from)))
	{
		sendto_realops("Cannot find server %s (%s)", parv[6],
		    backupbuf);
		return 0;
	}
	/*
	   ** Check against nick name collisions.
	   **
	   ** Put this 'if' here so that the nesting goes nicely on the screen :)
	   ** We check against server name list before determining if the nickname
	   ** is present in the nicklist (due to the way the below for loop is
	   ** constructed). -avalon
	 */
	/* I managed to fuck this up i guess --stskeeps */
	if ((acptr = find_server(nick, NULL)))
	{
		if (MyConnect(sptr))
		{
#ifdef GUEST
			if (IsUnknown(sptr))
			{
				m_guest(cptr, sptr, parc, parv);
				return 0;
			}
#endif
			sendto_one(sptr, err_str(ERR_NICKNAMEINUSE), me.name,
			    BadPtr(parv[0]) ? "*" : parv[0], nick);
			return 0;	/* NICK message ignored */
		}
	}

	/*
	   ** Check for a Q-lined nickname. If we find it, and it's our
	   ** client, just reject it. -Lefler
	   ** Allow opers to use Q-lined nicknames. -Russell
	 */
	if (!stricmp("ircd", nick))
	{
		sendto_one(sptr, err_str(ERR_ERRONEUSNICKNAME), me.name,
		    BadPtr(parv[0]) ? "*" : parv[0], nick,
		    "Reserved for internal IRCd purposes");
		return 0;
	}
	if (!stricmp("irc", nick))
	{
		sendto_one(sptr, err_str(ERR_ERRONEUSNICKNAME), me.name,
		    BadPtr(parv[0]) ? "*" : parv[0], nick,
		    "Reserved for internal IRCd purposes");
		return 0;
	}
	if (!IsULine(sptr) && ((aconf = Find_ban(nick, CONF_BAN_NICK))
#ifdef OLD
	    || (asqline = find_sqline_match(nick))
#endif
	    ))
	{
		if (IsServer(sptr))
		{
			acptrs =
			    (aClient *)find_server_b64_or_real(sptr->user ==
			    NULL ? (char *)parv[6] : (char *)sptr->user->
			    server);
			sendto_realops("Q:lined nick %s from %s on %s", nick,
			    (*sptr->name != 0
			    && !IsServer(sptr) ? sptr->name : "<unregistered>"),
			    acptrs ? acptrs->name : "unknown server");
		}
		else
		{
			sendto_realops("Q:lined nick %s from %s on %s",
			    nick,
			    *sptr->name ? sptr->name : "<unregistered>",
			    me.name);
		}

		if ((!IsServer(cptr)) && (!IsOper(cptr)))
		{

			if (aconf)
				sendto_one(sptr, err_str(ERR_ERRONEUSNICKNAME),
				    me.name, BadPtr(parv[0]) ? "*" : parv[0],
				    nick,
				    BadPtr(aconf->reason) ? "reason unspecified"
				    : aconf->reason);
#ifdef OLD
			else if (asqline)
				sendto_one(sptr, err_str(ERR_ERRONEUSNICKNAME),
				    me.name, BadPtr(parv[0]) ? "*" : parv[0],
				    nick,
				    BadPtr(asqline->reason) ?
				    "reason unspecified" : asqline->reason);
#endif
			sendto_realops("Forbidding Q-lined nick %s from %s.",
			    nick, get_client_name(cptr, FALSE));
			return 0;	/* NICK message ignored */
		}
	}
	/*
	   ** acptr already has result from previous find_server()
	 */
	if (acptr)
	{
		/*
		   ** We have a nickname trying to use the same name as
		   ** a server. Send out a nick collision KILL to remove
		   ** the nickname. As long as only a KILL is sent out,
		   ** there is no danger of the server being disconnected.
		   ** Ultimate way to jupiter a nick ? >;-). -avalon
		 */
		sendto_failops("Nick collision on %s(%s <- %s)",
		    sptr->name, acptr->from->name,
		    get_client_name(cptr, FALSE));
		ircstp->is_kill++;
		sendto_one(cptr, ":%s KILL %s :%s (%s <- %s)",
		    me.name, sptr->name, me.name, acptr->from->name,
		    /* NOTE: Cannot use get_client_name
		       ** twice here, it returns static
		       ** string pointer--the other info
		       ** would be lost
		     */
		    get_client_name(cptr, FALSE));
		sptr->flags |= FLAGS_KILLED;
		return exit_client(cptr, sptr, &me, "Nick/Server collision");
	}

	if (MyClient(cptr) && !IsOper(cptr))
		cptr->since += 3;	/* Nick-flood prot. -Donwulff */

	if (!(acptr = find_client(nick, NULL)))
		goto nickkilldone;	/* No collisions, all clear... */
	/*
	   ** If the older one is "non-person", the new entry is just
	   ** allowed to overwrite it. Just silently drop non-person,
	   ** and proceed with the nick. This should take care of the
	   ** "dormant nick" way of generating collisions...
	 */
	/* Moved before Lost User Field to fix some bugs... -- Barubary */
	if (IsUnknown(acptr) && MyConnect(acptr))
	{
		/* This may help - copying code below */
		if (acptr == cptr)
			return 0;
		acptr->flags |= FLAGS_KILLED;
		exit_client(NULL, acptr, &me, "Overridden");
		goto nickkilldone;
	}
	/* A sanity check in the user field... */
	if (acptr->user == NULL)
	{
		/* This is a Bad Thing */
		sendto_failops("Lost user field for %s in change from %s",
		    acptr->name, get_client_name(cptr, FALSE));
		ircstp->is_kill++;
		sendto_one(acptr, ":%s KILL %s :%s (Lost user field!)",
		    me.name, acptr->name, me.name);
		acptr->flags |= FLAGS_KILLED;
		/* Here's the previous versions' desynch.  If the old one is
		   messed up, trash the old one and accept the new one.
		   Remember - at this point there is a new nick coming in!
		   Handle appropriately. -- Barubary */
		exit_client(NULL, acptr, &me, "Lost user field");
		goto nickkilldone;
	}
	/*
	   ** If acptr == sptr, then we have a client doing a nick
	   ** change between *equivalent* nicknames as far as server
	   ** is concerned (user is changing the case of his/her
	   ** nickname or somesuch)
	 */
	if (acptr == sptr)
		if (strcmp(acptr->name, nick) != 0)
			/*
			   ** Allows change of case in his/her nick
			 */
			goto nickkilldone;	/* -- go and process change */
		else
			/*
			   ** This is just ':old NICK old' type thing.
			   ** Just forget the whole thing here. There is
			   ** no point forwarding it to anywhere,
			   ** especially since servers prior to this
			   ** version would treat it as nick collision.
			 */
			return 0;	/* NICK Message ignored */
	/*
	   ** Note: From this point forward it can be assumed that
	   ** acptr != sptr (point to different client structures).
	 */
	/*
	   ** Decide, we really have a nick collision and deal with it
	 */
	if (!IsServer(cptr))
	{
		/*
		   ** NICK is coming from local client connection. Just
		   ** send error reply and ignore the command.
		 */
#ifdef GUEST
		if (IsUnknown(sptr))
		{
			m_guest(cptr, sptr, parc, parv);
			return 0;
		}
#endif
		sendto_one(sptr, err_str(ERR_NICKNAMEINUSE),
		    /* parv[0] is empty when connecting */
		    me.name, BadPtr(parv[0]) ? "*" : parv[0], nick);
		return 0;	/* NICK message ignored */
	}
	/*
	   ** NICK was coming from a server connection.
	   ** This means we have a race condition (two users signing on
	   ** at the same time), or two net fragments reconnecting with
	   ** the same nick.
	   ** The latter can happen because two different users connected
	   ** or because one and the same user switched server during a
	   ** net break.
	   ** If we have the old protocol (no TimeStamp and no user@host)
	   ** or if the TimeStamps are equal, we kill both (or only 'new'
	   ** if it was a "NICK new"). Otherwise we kill the youngest
	   ** when user@host differ, or the oldest when they are the same.
	   ** --Run
	   **
	 */
	if (IsServer(sptr))
	{
		/*
		   ** A new NICK being introduced by a neighbouring
		   ** server (e.g. message type "NICK new" received)
		 */
		if (parc > 3)
		{
			lastnick = TS2ts(parv[3]);
			if (parc > 5)
				differ = (mycmp(acptr->user->username, parv[4])
				    || mycmp(acptr->user->realhost, parv[5]));
		}
		sendto_failops("Nick collision on %s (%s %d <- %s %d)",
		    acptr->name, acptr->from->name, acptr->lastnick,
		    get_client_name(cptr, FALSE), lastnick);
		/*
		   **    I'm putting the KILL handling here just to make it easier
		   ** to read, it's hard to follow it the way it used to be.
		   ** Basically, this is what it will do.  It will kill both
		   ** users if no timestamp is given, or they are equal.  It will
		   ** kill the user on our side if the other server is "correct"
		   ** (user@host differ and their user is older, or user@host are
		   ** the same and their user is younger), otherwise just kill the
		   ** user an reintroduce our correct user.
		   **    The old code just sat there and "hoped" the other server
		   ** would kill their user.  Not anymore.
		   **                                               -- binary
		 */
		if (!(parc > 3) || (acptr->lastnick == lastnick))
		{
			ircstp->is_kill++;
			sendto_serv_butone(NULL,
			    ":%s KILL %s :%s (Nick Collision)",
			    me.name, acptr->name, me.name);
			acptr->flags |= FLAGS_KILLED;
			(void)exit_client(NULL, acptr, &me,
			    "Nick collision with no timestamp/equal timestamps");
			return 0;	/* We killed both users, now stop the process. */
		}

		if ((differ && (acptr->lastnick > lastnick)) ||
		    (!differ && (acptr->lastnick < lastnick)) || acptr->from == cptr)	/* we missed a QUIT somewhere ? */
		{
			ircstp->is_kill++;
			sendto_serv_butone(cptr,
			    ":%s KILL %s :%s (Nick Collision)",
			    me.name, acptr->name, me.name);
			acptr->flags |= FLAGS_KILLED;
			(void)exit_client(NULL, acptr, &me, "Nick collision");
			goto nickkilldone;	/* OK, we got rid of the "wrong" user,
						   ** now we're going to add the user the
						   ** other server introduced.
						 */
		}

		if ((differ && (acptr->lastnick < lastnick)) ||
		    (!differ && (acptr->lastnick > lastnick)))
		{
			/*
			 * Introduce our "correct" user to the other server
			 */

			sendto_one(cptr, ":%s KILL %s :%s (Nick Collision)",
			    me.name, parv[1], me.name);
			sendto_one(cptr, "NICK %s %d %d %s %s %s :%s",
			    acptr->name, acptr->hopcount + 1, acptr->lastnick,
			    acptr->user->username, acptr->user->realhost,
			    acptr->user->server, acptr->info);
			send_umode(cptr, acptr, 0, SEND_UMODES, buf);
			if (IsHidden(acptr))
			{
				sendto_one(cptr, ":%s SETHOST %s", acptr->name,
				    acptr->user->virthost);
			}
			if (acptr->user->away)
				sendto_one(cptr, ":%s AWAY :%s", acptr->name,
				    acptr->user->away);
			send_user_joins(cptr, acptr);
			return 0;	/* Ignore the NICK */
		}
		return 0;
	}
	else
	{
		/*
		   ** A NICK change has collided (e.g. message type ":old NICK new").
		 */
		if (parc > 2)
			lastnick = TS2ts(parv[2]);
		differ = (mycmp(acptr->user->username, sptr->user->username) ||
		    mycmp(acptr->user->realhost, sptr->user->realhost));
		sendto_failops
		    ("Nick change collision from %s to %s (%s %d <- %s %d)",
		    sptr->name, acptr->name, acptr->from->name, acptr->lastnick,
		    sptr->from->name, lastnick);
		if (!(parc > 2) || lastnick == acptr->lastnick)
		{
			ircstp->is_kill += 2;
			sendto_serv_butone(NULL,	/* First kill the new nick. */
			    ":%s KILL %s :%s (Self Collision)",
			    me.name, acptr->name, me.name);
			sendto_serv_butone(cptr,	/* Tell my servers to kill the old */
			    ":%s KILL %s :%s (Self Collision)",
			    me.name, sptr->name, me.name);
			sptr->flags |= FLAGS_KILLED;
			acptr->flags |= FLAGS_KILLED;
			(void)exit_client(NULL, sptr, &me, "Self Collision");
			(void)exit_client(NULL, acptr, &me, "Self Collision");
			return 0;	/* Now that I killed them both, ignore the NICK */
		}
		if ((differ && (acptr->lastnick > lastnick)) ||
		    (!differ && (acptr->lastnick < lastnick)))
		{
			/* sptr (their user) won, let's kill acptr (our user) */
			ircstp->is_kill++;
			sendto_serv_butone(cptr,
			    ":%s KILL %s :%s (Nick collision: %s <- %s)",
			    me.name, acptr->name, me.name,
			    acptr->from->name, sptr->from->name);
			acptr->flags |= FLAGS_KILLED;
			(void)exit_client(NULL, acptr, &me, "Nick collision");
			goto nickkilldone;	/* their user won, introduce new nick */
		}
		if ((differ && (acptr->lastnick < lastnick)) ||
		    (!differ && (acptr->lastnick > lastnick)))
		{
			/* acptr (our user) won, let's kill sptr (their user),
			   ** and reintroduce our "correct" user
			 */
			ircstp->is_kill++;
			/* Kill the user trying to change their nick. */
			sendto_serv_butone(cptr,
			    ":%s KILL %s :%s (Nick collision: %s <- %s)",
			    me.name, sptr->name, me.name,
			    sptr->from->name, acptr->from->name);
			sptr->flags |= FLAGS_KILLED;
			(void)exit_client(NULL, sptr, &me, "Nick collision");
			/*
			 * Introduce our "correct" user to the other server
			 */
			/* Kill their user. */
			sendto_one(cptr, ":%s KILL %s :%s (Nick Collision)",
			    me.name, parv[1], me.name);
			sendto_one(cptr, "NICK %s %d %d %s %s %s :%s",
			    acptr->name, acptr->hopcount + 1, acptr->lastnick,
			    acptr->user->username, acptr->user->realhost,
			    acptr->user->server, acptr->info);
			send_umode(cptr, acptr, 0, SEND_UMODES, buf);
			if (acptr->user->away)
				sendto_one(cptr, ":%s AWAY :%s", acptr->name,
				    acptr->user->away);
			if (IsHidden(acptr))
			{
				sendto_one(cptr, ":%s SETHOST %s", acptr->name,
				    acptr->user->virthost);
			}

			send_user_joins(cptr, acptr);
			return 0;	/* their user lost, ignore the NICK */
		}

	}
	return 0;		/* just in case */
      nickkilldone:
	if (IsServer(sptr))
	{
		/* A server introducing a new client, change source */

		sptr = make_client(cptr, serv);
		add_client_to_list(sptr);
		if (parc > 2)
			sptr->hopcount = TS2ts(parv[2]);
		if (parc > 3)
			sptr->lastnick = TS2ts(parv[3]);
		else		/* Little bit better, as long as not all upgraded */
			sptr->lastnick = TStime();
		if (sptr->lastnick < 0)
		{
			sendto_realops
			    ("Negative timestamp recieved from %s, resetting to TStime (%s)",
			    cptr->name, backupbuf);
			sptr->lastnick = TStime();
		}
	}
	else if (sptr->name[0] && IsPerson(sptr))
	{
		/*
		   ** If the client belongs to me, then check to see
		   ** if client is currently on any channels where it
		   ** is currently banned.  If so, do not allow the nick
		   ** change to occur.
		   ** Also set 'lastnick' to current time, if changed.
		 */
		if (MyClient(sptr))
		{
			for (mp = cptr->user->channel; mp; mp = mp->next)
			{
				if (is_banned(cptr, &me, mp->chptr))
				{
					sendto_one(cptr,
					    err_str(ERR_BANNICKCHANGE),
					    me.name, parv[0],
					    mp->chptr->chname);
					return 0;
				}
				if (!IsOper(cptr) && !IsULine(cptr)
				    && mp->chptr->mode.mode & MODE_NONICKCHANGE
				    && !is_chanownprotop(cptr, mp->chptr))
				{
					sendto_one(cptr,
					    err_str(ERR_NONICKCHANGE),
					    me.name, parv[0],
					    mp->chptr->chname);
					return 0;
				}
			}
			RunHook2(HOOKTYPE_LOCAL_NICKCHANGE, sptr, nick);
		}
		/*
		 * Client just changing his/her nick. If he/she is
		 * on a channel, send note of change to all clients
		 * on that channel. Propagate notice to other servers.
		 */
		if (mycmp(parv[0], nick) ||
		    /* Next line can be removed when all upgraded  --Run */
		    !MyClient(sptr) && parc > 2
		    && TS2ts(parv[2]) < sptr->lastnick)
			sptr->lastnick = (MyClient(sptr)
			    || parc < 3) ? TStime() : TS2ts(parv[2]);
		if (sptr->lastnick < 0)
		{
			sendto_realops("Negative timestamp (%s)", backupbuf);
			sptr->lastnick = TStime();
		}
		add_history(sptr, 1);
		sendto_common_channels(sptr, ":%s NICK :%s", parv[0], nick);
		sendto_serv_butone_token(cptr, parv[0], MSG_NICK, TOK_NICK,
		    "%s %d", nick, sptr->lastnick);
		acptr->umodes &= ~UMODE_REGNICK;
	}
	else if (!sptr->name[0])
	{
#ifdef NOSPOOF
		/*
		 * Client setting NICK the first time.
		 *
		 * Generate a random string for them to pong with.
		 */
#ifndef _WIN32
		sptr->nospoof =
		    1 + (int)(9000000.0 * random() / (RAND_MAX + 80000000.0));
#else
		sptr->nospoof =
		    1 + (int)(9000000.0 * rand() / (RAND_MAX + 80000000.0));
#endif
		/*
		 * If on the odd chance it comes out zero, make it something
		 * non-zero.
		 */
		if (sptr->nospoof == 0)
			sptr->nospoof = 0xa123b789;
		sendto_one(sptr, ":%s NOTICE %s :*** If you are having problems"
		    " connecting due to ping timeouts, please"
		    " type /quote pong %X or /raw pong %X now.",
		    me.name, nick, sptr->nospoof, sptr->nospoof);
		sendto_one(sptr, "PING :%X", sptr->nospoof);
#endif /* NOSPOOF */

#ifdef CONTACT_EMAIL
		sendto_one(sptr,
		    ":%s NOTICE %s :*** If you need assistance with a"
		    " connection problem, please email " CONTACT_EMAIL
		    " with the name and version of the client you are"
		    " using, and the server you tried to connect to: %s",
		    me.name, nick, me.name);
#endif /* CONTACT_EMAIL */
#ifdef CONTACT_URL
		sendto_one(sptr,
		    ":%s NOTICE %s :*** If you need assistance with"
		    " connecting to this server, %s, please refer to: "
		    CONTACT_URL, me.name, nick, me.name);
#endif /* CONTACT_URL */

		/* Copy password to the passwd field if it's given after NICK
		 * - originally by taz, modified by Wizzu
		 */
		if ((parc > 2) && (strlen(parv[2]) < sizeof(sptr->passwd)))
		{
			if (sptr->passwd)
				MyFree(sptr->passwd);
			sptr->passwd = MyMalloc(strlen(parv[2]) + 1);
			(void)strcpy(sptr->passwd, parv[2]);
		}
		/* This had to be copied here to avoid problems.. */
		(void)strcpy(sptr->name, nick);
		if (sptr->user && IsNotSpoof(sptr))
		{
			/*
			   ** USER already received, now we have NICK.
			   ** *NOTE* For servers "NICK" *must* precede the
			   ** user message (giving USER before NICK is possible
			   ** only for local client connection!). register_user
			   ** may reject the client and call exit_client for it
			   ** --must test this and exit m_nick too!!!
			 */
			sptr->lastnick = TStime();	/* Always local client */
			if (register_user(cptr, sptr, nick,
			    sptr->user->username, NULL, NULL) == FLUSH_BUFFER)
				return FLUSH_BUFFER;
		}
	}
	/*
	 *  Finally set new nick name.
	 */
	if (sptr->name[0])
	{
		(void)del_from_client_hash_table(sptr->name, sptr);
		if (IsPerson(sptr))
			hash_check_notify(sptr, RPL_LOGOFF);
	}
	(void)strcpy(sptr->name, nick);
	(void)add_to_client_hash_table(nick, sptr);
	if (IsServer(cptr) && parc > 7)
	{
		parv[3] = nick;
		m_user(cptr, sptr, parc - 3, &parv[3]);
		if (GotNetInfo(cptr))
			sendto_umode(UMODE_FCLIENT,
			    "*** Notice -- Client connecting at %s: %s (%s@%s)",
			    sptr->user->server, sptr->name,
			    sptr->user->username, sptr->user->realhost);
	}
	else if (IsPerson(sptr))
		hash_check_notify(sptr, RPL_LOGON);

	return 0;
}

/*
** m_message (used in m_private() and m_notice())
** the general function to deliver MSG's between users/channels
**
**	parv[0] = sender prefix
**	parv[1] = receiver list
**	parv[2] = message text
**
** massive cleanup
** rev argv 6/91
**
*/
#define PREFIX_HALFOP	0x1
#define PREFIX_VOICE	0x2
#define PREFIX_OP	0x4

static int m_message(cptr, sptr, parc, parv, notice)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
	int  notice;
{
	aClient *acptr;
	char *s;
	aChannel *chptr;
	char *nick, *server, *p, *cmd, *ctcp, *p2, *pc, *text;
	int  cansend = 0;
	int  prefix = 0;

	/*
	 * Reasons why someone can't send to a channel
	 */
	static char *err_cantsend[] = {
		"You need voice (+v)",
		"No external channel messages",
		"Colour is not permitted in this channel",
		"You are banned",
		"CTCPs are not permitted in this channel",
		NULL
	};

	if (IsHandshake(sptr))
		return 0;

	if (notice)
	{
	}

	sptr->flags &= ~FLAGS_TS8;
	cmd = notice ? MSG_NOTICE : MSG_PRIVATE;
	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NORECIPIENT),
		    me.name, parv[0], cmd);
		return -1;
	}

	if (parc < 3 || *parv[2] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (WEBTV_SUPPORT == 1)
	{
		if (*parv[2] != '\1')
		{
			cmd = MSG_PRIVATE;
		}
	}
	if (MyConnect(sptr))
		parv[1] = canonize(parv[1]);

	for (p = NULL, nick = strtoken(&p, parv[1], ","); nick;
	    nick = strtoken(&p, NULL, ","))
	{
		/*
		   ** nickname addressed?
		 */
		if (!strcasecmp(nick, "ircd") && MyClient(sptr))
		{
			parse(sptr, parv[2], (parv[2] + strlen(parv[2])));
			continue;
		}
		if (!strcasecmp(nick, "irc") && MyClient(sptr))
		{
			if (webtv_parse(sptr, parv[2]) == -2)
			{
				parse(sptr, parv[2],
				    (parv[2] + strlen(parv[2])));
			}
			continue;
		}
		if (*nick != '#' && (acptr = find_person(nick, NULL)))
		{
			/* F:Line stuff by _Jozeph_ added by Stskeeps with comments */
			if (*parv[2] == 1 && MyClient(sptr) && !IsOper(sptr))
				/* somekinda ctcp thing
				   and i don't want to waste cpu on what others already checked..
				   (should this be checked ??) --Sts
				 */
			{
				ctcp = &parv[2][1];
				/* Most likely a DCC send .. */
				if (!myncmp(ctcp, "DCC SEND ", 9))
				{
					ConfigItem_deny_dcc *fl;
					char *end, file[BUFSIZE];
					int  size_string = 0;

					if (sptr->flags & FLAGS_DCCBLOCK)
					{
						sendto_one(sptr, ":%s NOTICE %s :*** You are blocked from sending files as you have tried to send a forbidden file - reconnect to regain ability to send",
							me.name, sptr->name);
						continue;
					}
					ctcp = &parv[2][10];
					end = (char *)index(ctcp, ' ');

					/* check if it was fake.. just pass it along then .. */
					if (!end || (end < ctcp))
						goto dcc_was_ok;

					size_string = (int)(end - ctcp);

					if (!size_string
					    || (size_string > (BUFSIZE - 1)))
						goto dcc_was_ok;

					strncpy(file, ctcp, size_string);
					file[size_string] = '\0';

					if ((fl =
					    (ConfigItem_deny_dcc *)
					    dcc_isforbidden(cptr, sptr, acptr,
					    file)))
					{
						sendto_one(sptr,
						    ":%s %d %s :*** Cannot DCC SEND file %s to %s (%s)",
						    me.name, RPL_TEXT,
						    sptr->name, file,
						    acptr->name,
						    fl->reason ? fl->reason :
						    "Possible infected virus file");
						sendto_one(sptr, ":%s NOTICE %s :*** You have been blocked from sending files, reconnect to regain permission to send files",
							me.name, sptr->name);

						sendto_umode(UMODE_VICTIM,
						    "%s tried to send forbidden file %s (%s) to %s (is blocked now)",
						    sptr->name, file,
						    fl->reason, acptr->name);
						sptr->flags |= FLAGS_DCCBLOCK;
						continue;
					}
					/* if it went here it was a legal send .. */
				}
			}
		      dcc_was_ok:

			if (MyClient(sptr)
			    && check_for_target_limit(sptr, acptr, acptr->name))
				continue;

			if (!is_silenced(sptr, acptr))
			{
				if (!notice && MyConnect(sptr) &&
				    acptr->user && acptr->user->away)
					sendto_one(sptr, rpl_str(RPL_AWAY),
					    me.name, parv[0], acptr->name,
					    acptr->user->away);
#ifdef STRIPBADWORDS
				if (!(IsULine(acptr) || IsULine(sptr)) &&
				    IsFilteringWords(acptr))
					sendto_message_one(acptr, sptr,
					    parv[0], cmd, nick,
					    stripbadwords_message(parv[2]));
				else
#endif
					sendto_message_one(acptr,
					    sptr, parv[0], cmd, nick, parv[2]);
			}
			continue;
		}

		p2 = (char *)strchr(nick, '#');
		prefix = 0;
		if (p2 && (chptr = find_channel(p2, NullChn)))
		{
			if (p2 != nick)
				for (pc = nick; pc != p2; pc++)
				{
					switch (*pc)
					{
					  case '+':
						  prefix |= PREFIX_VOICE;
						  break;
					  case '%':
						  prefix |= PREFIX_HALFOP;
						  break;
					  case '@':
						  prefix |= PREFIX_OP;
						  break;
					  default:
						  break;	/* ignore it :P */
					}
				}
			cansend =
			    !IsULine(sptr) ? can_send(sptr, chptr, parv[2]) : 0;
			if (!cansend)
			{
				/*if (chptr->mode.mode & MODE_FLOODLIMIT) */
				/* When we do it this way it appears to work? */
				if (chptr->mode.per)
					if (check_for_chan_flood(cptr, sptr,
					    chptr) == 1)
						continue;

				sendanyways = (parv[2][0] == '`' ? 1 : 0);
				text =
				    (chptr->mode.mode & MODE_STRIP ?
				    (char *)StripColors(parv[2]) : parv[2]);
#ifdef STRIPBADWORDS
				text =
				    (char *)(chptr->
				    mode.mode & MODE_STRIPBADWORDS ? (char
				    *)stripbadwords_channel(text) : text);
#endif
				sendto_channelprefix_butone_tok(cptr,
				    sptr, chptr,
				    prefix,
				    notice ? MSG_NOTICE : MSG_PRIVATE,
				    notice ? TOK_NOTICE : TOK_PRIVATE,
				    nick, text);
				sendanyways = 0;
				continue;
			}
			else if (!notice)
				sendto_one(sptr, err_str(ERR_CANNOTSENDTOCHAN),
				    me.name, parv[0], parv[0],
				    err_cantsend[cansend - 1], p2);
			continue;
		}
		else if (p2)
		{
			sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name,
			    parv[0], p2);
			continue;
		}


		/*
		   ** the following two cases allow masks in NOTICEs
		   ** (for OPERs only)
		   **
		   ** Armin, 8Jun90 (gruner@informatik.tu-muenchen.de)
		 */
		if ((*nick == '$' || *nick == '#') && (IsAnOper(sptr)
		    || IsULine(sptr)))
		{
			if (IsULine(sptr))
				goto itsokay;
			if (!(s = (char *)rindex(nick, '.')))
			{
				sendto_one(sptr, err_str(ERR_NOTOPLEVEL),
				    me.name, parv[0], nick);
				continue;
			}
			while (*++s)
				if (*s == '.' || *s == '*' || *s == '?')
					break;
			if (*s == '*' || *s == '?')
			{
				sendto_one(sptr, err_str(ERR_WILDTOPLEVEL),
				    me.name, parv[0], nick);
				continue;
			}
		      itsokay:
			sendto_match_butone(IsServer(cptr) ? cptr : NULL,
			    sptr, nick + 1,
			    (*nick == '#') ? MATCH_HOST :
			    MATCH_SERVER,
			    ":%s %s %s :%s", parv[0], cmd, nick, parv[2]);
			continue;
		}

		/*
		   ** user[%host]@server addressed?
		 */
		server = index(nick, '@');
		if (server)	/* meaning there is a @ */
		{
			/* There is always a \0 if its a string */
			if (*(server + 1) != '\0')
			{
				acptr = find_server_quick(server + 1);
				if (acptr)
				{
					/*
					   ** Not destined for a user on me :-(
					 */
					if (!IsMe(acptr))
					{
						sendto_one(acptr,
						    ":%s %s %s :%s", parv[0],
						    cmd, nick, parv[2]);
						continue;
					}

					/* Find the nick@server using hash. */
					acptr =
					    find_nickserv(nick,
					    (aClient *)NULL);
					if (acptr)
					{
						sendto_prefix_one(acptr, sptr,
						    ":%s %s %s :%s",
						    parv[0], cmd,
						    acptr->name, parv[2]);
						continue;
					}
				}
				if (server
				    && strncasecmp(server + 1, SERVICES_NAME,
				    strlen(SERVICES_NAME)) == 0)
					sendto_one(sptr,
					    err_str(ERR_SERVICESDOWN), me.name,
					    parv[0], nick);
				else
					sendto_one(sptr,
					    err_str(ERR_NOSUCHNICK), me.name,
					    parv[0], nick);

				continue;
			}

		}
		/* nothing, nada, not anything found */
		sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name, parv[0],
		    nick);
		continue;
	}
	return 0;
}

/*
** m_private
**	parv[0] = sender prefix
**	parv[1] = receiver list
**	parv[2] = message text
*/

int  m_private(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	return m_message(cptr, sptr, parc, parv, 0);
}


/*
 * Built in services aliasing for ChanServ, Memoserv, NickServ and
 * OperServ. This not only is an alias, but is also a security measure,
 * because PRIVMSG's arent sent to 'ChanServ' they are now sent to
 * 'ChanServ@Services.Network.Org' so nobody can snoop /cs commands :)
 */

int  m_chanserv(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (SERVICES_NAME && (acptr = find_person(ChanServ, NULL)))
		sendto_one(acptr, ":%s PRIVMSG %s@%s :%s", parv[0],
		    ChanServ, SERVICES_NAME, parv[1]);
	else
		sendto_one(sptr, err_str(ERR_SERVICESDOWN), me.name,
		    parv[0], ChanServ);
}

int  m_memoserv(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;

	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (SERVICES_NAME && (acptr = find_person(MemoServ, NULL)))
		sendto_one(acptr, ":%s PRIVMSG %s@%s :%s", parv[0],
		    MemoServ, SERVICES_NAME, parv[1]);
	else
		sendto_one(sptr, err_str(ERR_SERVICESDOWN), me.name,
		    parv[0], MemoServ);
}

int  m_nickserv(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (SERVICES_NAME && (acptr = find_person(NickServ, NULL)))
		sendto_one(acptr, ":%s PRIVMSG %s@%s :%s", parv[0],
		    NickServ, SERVICES_NAME, parv[1]);
	else
		sendto_one(sptr, err_str(ERR_SERVICESDOWN), me.name,
		    parv[0], NickServ);
}

int  m_botserv(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (SERVICES_NAME && (acptr = find_person(BotServ, NULL)))
		sendto_one(acptr, ":%s PRIVMSG %s@%s :%s", parv[0],
		    BotServ, SERVICES_NAME, parv[1]);
	else
		sendto_one(sptr, err_str(ERR_SERVICESDOWN), me.name,
		    parv[0], BotServ);
}

int  m_infoserv(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (SERVICES_NAME && (acptr = find_person(InfoServ, NULL)))
	{
		sendto_one(acptr, ":%s PRIVMSG %s@%s :%s", parv[0],
		    InfoServ, SERVICES_NAME, parv[1]);
	}
	else
		sendto_one(sptr, err_str(ERR_SERVICESDOWN), me.name,
		    parv[0], InfoServ);
}
int  m_operserv(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}
	if (SERVICES_NAME && (acptr = find_person(OperServ, NULL)))
		sendto_one(acptr, ":%s PRIVMSG %s@%s :%s", parv[0],
		    OperServ, SERVICES_NAME, parv[1]);
	else
		sendto_one(sptr, err_str(ERR_SERVICESDOWN), me.name,
		    parv[0], OperServ);
}

int  m_helpserv(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (SERVICES_NAME && (acptr = find_person(HelpServ, NULL)))
		sendto_one(acptr, ":%s PRIVMSG %s@%s :%s", parv[0],
		    HelpServ, SERVICES_NAME, parv[1]);
	else
		sendto_one(sptr, err_str(ERR_SERVICESDOWN), me.name,
		    parv[0], HelpServ);
}

int  m_statserv(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{

	aClient *acptr;



	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (STATS_SERVER && (acptr = find_person(StatServ, NULL)))
#ifndef STATSERV_ABSOLUTE
		if (!strncmp(acptr->srvptr->name, STATS_SERVER,
		    strlen(STATS_SERVER)))
		{
			sendto_one(acptr, ":%s PRIVMSG %s :%s", parv[0],
			    StatServ, parv[1]);
		}
		else
		{
			sendto_one(sptr,
			    ":%s NOTICE %s :*** Couldn't send to %s due to my setting of StatServ is not equal to what server it is on",
			    me.name, sptr->name, StatServ);
			return;
		}
#else
		sendto_one(acptr, ":%s PRIVMSG %s@%s :%s", parv[0],
		    StatServ, STATS_SERVER, parv[1]);
#endif
	else
		sendto_one(sptr, err_str(ERR_SERVICESDOWN), me.name,
		    parv[0], StatServ);
}


/*
 * Automatic NickServ/ChanServ direction for the identify command
 * -taz
 */
int  m_identify(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if (*parv[1])
	{
		if ((*parv[1] == '#') && ((char *)index(parv[1], ' ')))
		{
			if (SERVICES_NAME
			    && (acptr = find_person(ChanServ, NULL)))
				sendto_one(acptr,
				    ":%s PRIVMSG %s@%s :IDENTIFY %s", parv[0],
				    ChanServ, SERVICES_NAME, parv[1]);
			else
				sendto_one(sptr, err_str(ERR_SERVICESDOWN),
				    me.name, parv[0], ChanServ);
		}
		else
		{
			if (SERVICES_NAME
			    && (acptr = find_person(NickServ, NULL)))
				sendto_one(acptr,
				    ":%s PRIVMSG %s@%s :IDENTIFY %s", parv[0],
				    NickServ, SERVICES_NAME, parv[1]);
			else
				sendto_one(sptr, err_str(ERR_SERVICESDOWN),
				    me.name, parv[0], NickServ);
		}
	}
}

/*
 * Automatic NickServ/ChanServ parsing. If the second word of parv[1]
 * starts with a '#' this message goes to ChanServ. If it starts with
 * anything else, it goes to NickServ. If there is no second word in
 * parv[1], the message defaultly goes to NickServ. If parv[1] == 'help'
 * the user in instructed to /cs, /ns or /ms HELP for the help they need.
 * -taz
 */

int  m_services(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	char *tmps;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOTEXTTOSEND), me.name, parv[0]);
		return -1;
	}

	if ((strlen(parv[1]) >= 4) && (!strncmp(parv[1], "help", 4)))
	{
		sendto_one(sptr,
		    ":Services!Services@%s NOTICE %s :For ChanServ help use: /chanserv help",
		    SERVICES_NAME, sptr->name);
		sendto_one(sptr,
		    ":Services!Services@%s NOTICE %s :For NickServ help use: /nickserv help",
		    SERVICES_NAME, sptr->name);
		sendto_one(sptr,
		    ":Services!Services@%s NOTICE %s :For MemoServ help use: /memoserv help",
		    SERVICES_NAME, sptr->name);
		sendto_one(sptr,
		    ":Services!Services@%s NOTICE %s :For HelpServ help use: /helpserv help",
		    SERVICES_NAME, sptr->name);

		return;
	}

	if ((tmps = (char *)index(parv[1], ' ')))
	{
		tmps++;
		if (*tmps == '#')
			return m_chanserv(cptr, sptr, parc, parv);
		else
			return m_nickserv(cptr, sptr, parc, parv);
	}

	return m_nickserv(cptr, sptr, parc, parv);

}

/*
** m_notice
**	parv[0] = sender prefix
**	parv[1] = receiver list
**	parv[2] = notice text
*/

int  m_notice(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	return m_message(cptr, sptr, parc, parv, 1);
}

int  channelwho = 0;
int  operwho = 0;
static void do_who(sptr, acptr, repchan)
	aClient *sptr, *acptr;
	aChannel *repchan;
{
	char status[8];
	int  i = 0;

	/* auditoriums only show @'s */
	if (repchan && (repchan->mode.mode & MODE_AUDITORIUM) &&
	    !is_chan_op(acptr, repchan))
		return;

	if (acptr->user->away)
		status[i++] = 'G';
	else
		status[i++] = 'H';
	if (IsARegNick(acptr))
		status[i++] = 'r';

	/* Check for +H here too -- codemastr */
	if (IsAnOper(acptr) && (!IsHideOper(acptr) || sptr == acptr
	    || IsAnOper(sptr)))
		status[i++] = '*';
	else if (IsInvisible(acptr) && sptr != acptr && IsAnOper(sptr))
		status[i++] = '%';
	if (repchan && is_chan_op(acptr, repchan))
		status[i++] = '@';
	else if (repchan && has_voice(acptr, repchan))
		status[i++] = '+';
	status[i] = '\0';
	if (IsWhois(acptr) && channelwho == 0 && sptr != acptr && !operwho)
	{
		sendto_one(acptr,
		    ":%s NOTICE %s :*** %s either did a /who or a specific /who on you",
		    me.name, acptr->name, sptr->name);
	}
	if (IsHiding(acptr) && sptr != acptr && !IsNetAdmin(sptr)
	    && !IsTechAdmin(sptr))
		repchan = NULL;
	sendto_one(sptr, rpl_str(RPL_WHOREPLY), me.name, sptr->name,
	    (repchan) ? (repchan->chname) : "*", acptr->user->username,
	    IsHidden(acptr) ? acptr->user->virthost : acptr->user->realhost,
	    acptr->user->server, acptr->name, status, acptr->hopcount,
	    acptr->info);

}


/*
** m_who
**	parv[0] = sender prefix
**	parv[1] = nickname mask list
**	parv[2] = additional selection flag, only 'o' for now.
*/
int  m_who(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	char *mask = parc > 1 ? parv[1] : NULL;
	Membership *mp;
	Member *ms;
	aChannel *chptr;
	aChannel *mychannel;
	char *channame = NULL, *s;
	int  oper = parc > 2 ? (*parv[2] == 'o') : 0;	/* Show OPERS only */
	int  member;


	if (!BadPtr(mask))
	{
		if ((s = (char *)index(mask, ',')))
		{
			parv[1] = ++s;
			(void)m_who(cptr, sptr, parc, parv);
		}
		clean_channelname(mask);
	}
	channelwho = 0;
	operwho = 0;
	mychannel = NullChn;
	if (oper)
	{
#ifdef HELP_WHO
		sendto_umode(UMODE_HELPOP,
		    "*** HelpOp -- from %s: [Did a /who 0 o]", parv[0]);
		sendto_serv_butone(&me, ":%s HELP :[Did a /who 0 o]", parv[0]);
#endif
		operwho = 1;
	}
	if (sptr->user)
		if ((mp = sptr->user->channel))
			mychannel = mp->chptr;

	/* Allow use of m_who without registering */


	/*
	   **  Following code is some ugly hacking to preserve the
	   **  functions of the old implementation. (Also, people
	   **  will complain when they try to use masks like "12tes*"
	   **  and get people on channel 12 ;) --msa
	 */
	if (!mask || *mask == '\0')
		mask = NULL;
	else if (mask[1] == '\0' && mask[0] == '*')
	{
		mask = NULL;
		if (mychannel)
			channame = mychannel->chname;
	}
	else if (mask[1] == '\0' && mask[0] == '0')	/* "WHO 0" for irc.el */
		mask = NULL;
	else
		channame = mask;
	(void)collapse(mask);
	/* Don't allow masks for non opers */
	if (!IsOper(sptr) && mask && (strchr(mask, '*') || strchr(mask, '?')))
	{

		sendto_one(sptr, rpl_str(RPL_ENDOFWHO), me.name, parv[0],
		    BadPtr(mask) ? "*" : mask);
	}
	if (IsChannelName(channame))
	{
		/*
		 * List all users on a given channel
		 */
		chptr = find_channel(channame, NULL);
		if (chptr)
		{
			member = IsMember(sptr, chptr) || IsOper(sptr);
			if (member || !SecretChannel(chptr))
				for (ms = chptr->members; ms; ms = ms->next)
				{
					acptr = ms->cptr;
					if (IsHiding(acptr))
						continue;
					if (oper && (!IsAnOper(acptr)))
						continue;
					if ((acptr != sptr
					    && IsInvisible(acptr)
					    && !member) && !IsOper(sptr))
						continue;
					channelwho = 1;
					do_who(sptr, acptr, chptr);
				}
		}
	}
	else
		for (acptr = client; acptr; acptr = acptr->next)
		{
			aChannel *ch2ptr = NULL;
			int  showsecret, showperson, isinvis;

			if (!IsPerson(acptr))
				continue;
			if (oper && (!IsAnOper(acptr) || (IsHideOper(acptr)
			    && sptr != acptr && !IsAnOper(sptr))))
				continue;
			showperson = 0;
			showsecret = 0;
			/*
			 * Show user if they are on the same channel, or not
			 * invisible and on a non secret channel (if any).
			 * Do this before brute force match on all relevant fields
			 * since these are less cpu intensive (I hope :-) and should
			 * provide better/more shortcuts - avalon
			 */
			isinvis = acptr != sptr && IsInvisible(acptr)
			    && !IsAnOper(sptr);
			for (mp = acptr->user->channel; mp; mp = mp->next)
			{
				chptr = mp->chptr;
				member = IsMember(sptr, chptr) || IsOper(sptr);
				if (isinvis && !member)
					continue;
				if (IsAnOper(sptr))
					showperson = 1;
				if (member || (!isinvis &&
				    ShowChannel(sptr, chptr)))
				{
					ch2ptr = chptr;
					showperson = 1;
					break;
				}
				if (HiddenChannel(chptr)
				    && !SecretChannel(chptr) && !isinvis)
					showperson = 1;
			}
			if (!acptr->user->channel && !isinvis)
				showperson = 1;
			/*
			   ** This is brute force solution, not efficient...? ;(
			   ** Show entry, if no mask or any of the fields match
			   ** the mask. --msa
			 */
			if (showperson &&
			    (!mask ||
			    match(mask, acptr->name) == 0 ||
			    match(mask, acptr->user->username) == 0 ||
			    (!IsAnOper(sptr)
			    || match(mask, acptr->user->realhost)) == 0
			    || (!IsHidden(acptr)
			    || match(mask, acptr->user->virthost)) == 0
			    || (IsHidden(acptr)
			    || match(mask, acptr->user->realhost)) == 0
			    || match(mask, acptr->user->server) == 0
			    || match(mask, acptr->info) == 0))
				do_who(sptr, acptr, ch2ptr);
		}
	sendto_one(sptr, rpl_str(RPL_ENDOFWHO), me.name, parv[0],
	    BadPtr(mask) ? "*" : mask);
	return 0;
}

/*
** get_mode_str
** by vmlinuz
** returns an ascii string of modes
*/
char *get_mode_str(aClient *acptr)
{
	int  flag;
	int *s;
	char *m;

	m = buf;
	*m++ = '+';
	for (s = user_modes; (flag = *s) && (m - buf < BUFSIZE - 4); s += 2)
		if ((acptr->umodes & flag))
			*m++ = (char)(*(s + 1));
	*m = '\0';
	return buf;
}

char *get_modestr(long modes)
{
	int  flag;
	int *s;
	char *m;

	m = buf;
	*m++ = '+';
	for (s = user_modes; (flag = *s) && (m - buf < BUFSIZE - 4); s += 2)
		if ((modes & flag))
			*m++ = (char)(*(s + 1));
	*m = '\0';
	return buf;
}

/*
** m_whois
**	parv[0] = sender prefix
**	parv[1] = nickname masklist
*/
int  m_whois(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	static anUser UnknownUser = {
		NULL,		/* nextu */
		NULL,		/* channel */
		NULL,		/* invited */
		NULL,		/* silence */
		NULL,		/* away */
		0,		/* last */
		0,		/* servicestamp */
		1,		/* refcount */
		0,		/* joined */
		"<Unknown>",	/* username */
		"<Unknown>",	/* host */
		"<Unknown>"	/* server */
	};
	Membership *lp;
	anUser *user;
	aClient *acptr, *a2cptr;
	aChannel *chptr;
	char *nick, *tmp, *name;
	char *p = NULL;
	int  found, len, mlen;



	if (parc < 2)
	{
		sendto_one(sptr, err_str(ERR_NONICKNAMEGIVEN),
		    me.name, parv[0]);
		return 0;
	}

	if (parc > 2)
	{
		if (hunt_server(cptr, sptr, ":%s WHOIS %s :%s", 1, parc,
		    parv) != HUNTED_ISME)
			return 0;
		parv[1] = parv[2];
	}

	for (tmp = parv[1]; (nick = strtoken(&p, tmp, ",")); tmp = NULL)
	{
		int  invis, showchannel, member, wilds;

		found = 0;
		/* We do not support "WHOIS *" */
		wilds = (index(nick, '?') || index(nick, '*'));
		if (wilds)
			continue;

		if (acptr = find_client(nick, NULL))
		{
			if (IsServer(acptr))
				continue;
			/*
			 * I'm always last :-) and acptr->next == NULL!!
			 */
			if (IsMe(acptr))
				break;
			/*
			 * 'Rules' established for sending a WHOIS reply:
			 * - only send replies about common or public channels
			 *   the target user(s) are on;
			 */

			user = acptr->user ? acptr->user : &UnknownUser;
			name = (!*acptr->name) ? "?" : acptr->name;

			invis = acptr != sptr && IsInvisible(acptr);
			member = (user->channel) ? 1 : 0;

			a2cptr = find_server_quick(user->server);

			if (!IsPerson(acptr))
				continue;

			if (IsWhois(acptr) && (sptr != acptr))
			{
				sendto_one(acptr,
				    ":%s NOTICE %s :*** %s (%s@%s) did a /whois on you.",
				    me.name, acptr->name, sptr->name,
				    sptr->user->username,
				    IsHidden(acptr) ? sptr->
				    user->virthost : sptr->user->realhost);
			}

			sendto_one(sptr, rpl_str(RPL_WHOISUSER), me.name,
			    parv[0], name,
			    user->username,
			    IsHidden(acptr) ? user->virthost : user->realhost,
			    acptr->info);

			if (IsEyes(sptr) && IsOper(sptr))
			{
				/* send the target user's modes */
				sendto_one(sptr, rpl_str(RPL_WHOISMODES),
				    me.name, parv[0], name,
				    get_mode_str(acptr));
			}
			if (IsHidden(acptr) && ((acptr == sptr) || IsAnOper(sptr))) 
			{
				sendto_one(sptr, rpl_str(RPL_WHOISHOST),
				    me.name, parv[0], acptr->name,
				    user->realhost);
			}

			if (IsARegNick(acptr))
				sendto_one(sptr, rpl_str(RPL_WHOISREGNICK), me.name, parv[0], name);
			
			found = 1;
			mlen = strlen(me.name) + strlen(parv[0]) + 6 + strlen(name);
			for (len = 0, *buf = '\0', lp = user->channel; lp; lp = lp->next)
			{
				chptr = lp->chptr;
				showchannel = 0;
				if (ShowChannel(sptr, chptr))
					showchannel = 1;
#ifndef SHOW_SECRET
				if (IsAnOper(sptr) && !SecretChannel(chptr))
#else
				if (IsAnOper(sptr))
#endif
					showchannel = 1;
				if (IsServices(acptr) && !(IsNetAdmin(sptr) || IsTechAdmin(sptr)))
					showchannel = 0;
				if (acptr == sptr)
					showchannel = 1;

				if (showchannel)
				{
					if (len + strlen(chptr->chname) > (size_t)BUFSIZE - 4 - mlen)
					{
						sendto_one(sptr,
						    ":%s %d %s %s :%s",
						    me.name,
						    RPL_WHOISCHANNELS,
						    parv[0], name, buf);
						*buf = '\0';
						len = 0;
					}
#ifdef SHOW_SECRET
					if (!(acptr == sptr) && IsAnOper(sptr)
#else
					if (!(acptr == sptr)
					    && (IsNetAdmin(sptr)
					    || IsTechAdmin(sptr))
#endif
					    && SecretChannel(chptr))
						*(buf + len++) = '~';
					if (is_chanowner(acptr, chptr))
						*(buf + len++) = '*';
					else if (is_chanprot(acptr, chptr))
						*(buf + len++) = '^';
					else if (is_chan_op(acptr, chptr))
						*(buf + len++) = '@';
					else if (is_half_op(acptr, chptr))
						*(buf + len++) = '%';
					else if (has_voice(acptr, chptr))
						*(buf + len++) = '+';
					if (len)
						*(buf + len) = '\0';
					(void)strcpy(buf + len, chptr->chname);
					len += strlen(chptr->chname);
					(void)strcat(buf + len, " ");
					len++;
				}
			}

			if (buf[0] != '\0')
				sendto_one(sptr, rpl_str(RPL_WHOISCHANNELS), me.name, parv[0], name, buf);

			sendto_one(sptr, rpl_str(RPL_WHOISSERVER),
			    me.name, parv[0], name, user->server,
			    a2cptr ? a2cptr->info : "*Not On This Net*");

			if (user->away)
				sendto_one(sptr, rpl_str(RPL_AWAY), me.name,
				    parv[0], name, user->away);
			/* makesure they aren't +H (we'll also check
			   before we display a helpop or IRCD Coder msg)
			   -- codemastr */
			if ((IsAnOper(acptr) || IsServices(acptr))
			    && (!IsHideOper(acptr) || sptr == acptr
			    || IsAnOper(sptr)))
			{
				buf[0] = '\0';
				if (IsNetAdmin(acptr))
					strcat(buf, "a Network Administrator");
				else if (IsTechAdmin(acptr))
					strcat(buf,
					    "a Technical Administrator");
				else if (IsSAdmin(acptr))
					strcat(buf, "a Services Operator");
				else if (IsAdmin(acptr) && !IsCoAdmin(acptr))
					strcat(buf, "a Server Administrator");
				else if (IsCoAdmin(acptr))
					strcat(buf, "a Co Administrator");
				else if (IsServices(acptr))
					strcat(buf, "a Network Service");
				else if (IsOper(acptr))
					strcat(buf, "an IRC Operator");

				else
					strcat(buf, "a Local IRC Operator");
				if (buf[0])
					sendto_one(sptr,
					    rpl_str(RPL_WHOISOPERATOR), me.name,
					    parv[0], name, buf);
			}

			if (IsHelpOp(acptr) && (!IsHideOper(acptr)
			    || sptr == acptr || IsAnOper(sptr)))
				if (!user->away)
					sendto_one(sptr,
					    rpl_str(RPL_WHOISHELPOP), me.name,
					    parv[0], name);

			if (acptr->umodes & UMODE_BOT)
			{
				sendto_one(sptr, rpl_str(RPL_WHOISBOT),
				    me.name, parv[0], name, ircnetwork);
			}
			if (acptr->umodes & UMODE_SECURE)
			{
				sendto_one(sptr, ":%s %d %s %s :%s", me.name,
				    RPL_WHOISSPECIAL,
				    parv[0], name,
				    "is a Secure Connection");
			}
			if (user->swhois && !IsHideOper(acptr))
			{
				if (*user->swhois != '\0')
					sendto_one(sptr, ":%s %d %s %s :%s",
					    me.name, RPL_WHOISSPECIAL, parv[0],
					    name, acptr->user->swhois);
			}
			/*
			 * Fix /whois to not show idle times of
			 * global opers to anyone except another
			 * global oper or services.
			 * -CodeM/Barubary
			 */
			if (MyConnect(acptr))
				sendto_one(sptr, rpl_str(RPL_WHOISIDLE),
				    me.name, parv[0], name,
				    TStime() - user->last, acptr->firsttime);
		}
		if (!found)
			sendto_one(sptr, err_str(ERR_NOSUCHNICK),
			    me.name, parv[0], nick);
		if (p)
			p[-1] = ',';
	}
	sendto_one(sptr, rpl_str(RPL_ENDOFWHOIS), me.name, parv[0], parv[1]);

	return 0;
}

/*
** m_user
**	parv[0] = sender prefix
**	parv[1] = username (login name, account)
**	parv[2] = client host name (used only from other servers)
**	parv[3] = server host name (used only from other servers)
**	parv[4] = users real name info
*/
int  m_user(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
#define	UFLAGS	(UMODE_INVISIBLE|UMODE_WALLOP|UMODE_SERVNOTICE)
	char *username, *host, *server, *realname, *umodex = NULL, *virthost =
	    NULL;
	u_int32_t sstamp = 0;
	anUser *user;
	aClient *acptr;

	if (IsServer(cptr) && !IsUnknown(sptr))
		return 0;

	if (MyClient(sptr) && (sptr->listener->umodes & LISTENER_SERVERSONLY))
	{
		return exit_client(cptr, sptr, sptr,
		    "This port is for servers only");
	}

	if (parc > 2 && (username = (char *)index(parv[1], '@')))
		*username = '\0';
	if (parc < 5 || *parv[1] == '\0' || *parv[2] == '\0' ||
	    *parv[3] == '\0' || *parv[4] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "USER");
		if (IsServer(cptr))
			sendto_ops("bad USER param count for %s from %s",
			    parv[0], get_client_name(cptr, FALSE));
		else
			return 0;
	}


	/* Copy parameters into better documenting variables */

	username = (parc < 2 || BadPtr(parv[1])) ? "<bad-boy>" : parv[1];
	host = (parc < 3 || BadPtr(parv[2])) ? "<nohost>" : parv[2];
	server = (parc < 4 || BadPtr(parv[3])) ? "<noserver>" : parv[3];

	/* This we can remove as soon as all servers have upgraded. */

	if (parc == 6 && IsServer(cptr))
	{
		if (isdigit(*parv[4]))
			sstamp = atol(parv[4]);
		realname = (BadPtr(parv[5])) ? "<bad-realname>" : parv[5];
		umodex = NULL;
	}
	else if (parc == 8 && IsServer(cptr))
	{
		if (isdigit(*parv[4]))
			sstamp = atol(parv[4]);
		realname = (BadPtr(parv[7])) ? "<bad-realname>" : parv[7];
		umodex = parv[5];
		virthost = parv[6];
	}
	else
	{
		realname = (BadPtr(parv[4])) ? "<bad-realname>" : parv[4];
	}
	user = make_user(sptr);

	if (!MyConnect(sptr))
	{
		if (sptr->srvptr == NULL)
			sendto_ops("WARNING, User %s introduced as being "
			    "on non-existant server %s.", sptr->name, server);
		if (SupportNS(cptr))
		{
			acptr = (aClient *)find_server_b64_or_real(server);
			if (acptr)
				user->server = find_or_add(acptr->name);
			else
				user->server = find_or_add(server);
		}
		else
			user->server = find_or_add(server);
		strncpyzt(user->realhost, host, sizeof(user->realhost));
		goto user_finish;
	}

	if (!IsUnknown(sptr))
	{
		sendto_one(sptr, err_str(ERR_ALREADYREGISTRED),
		    me.name, parv[0]);
		return 0;
	}

	if (!IsServer(cptr))
	{
		sptr->umodes |= CONN_MODES;
	}

	strncpyzt(user->realhost, host, sizeof(user->realhost));
	user->server = me_hash;
      user_finish:
	user->servicestamp = sstamp;
	strncpyzt(sptr->info, realname, sizeof(sptr->info));
	if (sptr->name[0] && (IsServer(cptr) ? 1 : IsNotSpoof(sptr)))
		/* NICK and no-spoof already received, now we have USER... */
	{
		int  xx;

		xx =
		    register_user(cptr, sptr, sptr->name, username, umodex,
		    virthost);
		return xx;
	}
	else
		strncpyzt(sptr->user->username, username, USERLEN + 1);

	return 0;
}

/*
** m_quit
**	parv[0] = sender prefix
**	parv[1] = comment
*/
int  m_quit(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	char *ocomment = (parc > 1 && parv[1]) ? parv[1] : parv[0];
	static char comment[TOPICLEN];

	if (!IsServer(cptr))
	{
		ircsprintf(comment, "%s ",
		    BadPtr(prefix_quit) ? "Quit:" : prefix_quit);

#ifdef CENSOR_QUIT
		ocomment = (char *)stripbadwords_channel(ocomment);
#endif
		strncpy(comment + strlen(comment), ocomment, TOPICLEN - 7);
		comment[TOPICLEN] = '\0';
		return exit_client(cptr, sptr, sptr, comment);
	}
	else
	{
		return exit_client(cptr, sptr, sptr, ocomment);
	}
}


/*
** m_kill
**	parv[0] = sender prefix
**	parv[1] = kill victim(s) - comma separated list
**	parv[2] = kill path
*/
int  m_kill(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	static anUser UnknownUser = {
		NULL,		/* nextu */
		NULL,		/* channel */
		NULL,		/* invited */
		NULL,		/* silence */
		NULL,		/* away */
		0,		/* last */
		0,		/* servicestamp */
		1,		/* refcount */
		0,		/* joined */
		"<Unknown>",	/* username */
		"<Unknown>",	/* host */
		"<Unknown>"	/* server */
	};
	aClient *acptr;
	anUser *auser;
	char inpath[HOSTLEN * 2 + USERLEN + 5];
	char *oinpath = get_client_name(cptr, FALSE);
	char *user, *path, *killer, *nick, *p, *s;
	int  chasing = 0, kcount = 0;



	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "KILL");
		return 0;
	}

	user = parv[1];
	path = parv[2];		/* Either defined or NULL (parc >= 2!!) */

	strcpy(inpath, oinpath);

#ifndef ROXnet
	if (IsServer(cptr) && (s = (char *)index(inpath, '.')) != NULL)
		*s = '\0';	/* Truncate at first "." */
#endif

	if (!IsPrivileged(cptr))
	{
		sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
		return 0;
	}
	if (IsAnOper(cptr))
	{
		if (BadPtr(path))
		{
			sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
			    me.name, parv[0], "KILL");
			return 0;
		}
		if (strlen(path) > (size_t)TOPICLEN)
			path[TOPICLEN] = '\0';
	}

	if (MyClient(sptr))
		user = canonize(user);

	for (p = NULL, nick = strtoken(&p, user, ","); nick;
	    nick = strtoken(&p, NULL, ","))
	{

		chasing = 0;

		if (!(acptr = find_client(nick, NULL)))
		{
			/*
			   ** If the user has recently changed nick, we automaticly
			   ** rewrite the KILL for this new nickname--this keeps
			   ** servers in synch when nick change and kill collide
			 */
			if (!(acptr =
			    get_history(nick, (long)KILLCHASETIMELIMIT)))
			{
				sendto_one(sptr, err_str(ERR_NOSUCHNICK),
				    me.name, parv[0], nick);
				continue;
			}
			sendto_one(sptr,
			    ":%s NOTICE %s :*** KILL changed from %s to %s",
			    me.name, parv[0], nick, acptr->name);
			chasing = 1;
		}
		if ((!MyConnect(acptr) && MyClient(cptr) && !OPCanGKill(cptr))
		    || (MyConnect(acptr) && MyClient(cptr)
		    && !OPCanLKill(cptr)))
		{
			sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name,
			    parv[0]);
			continue;
		}
		if (IsServer(acptr) || IsMe(acptr))
		{
			sendto_one(sptr, err_str(ERR_CANTKILLSERVER),
			    me.name, parv[0]);
			continue;
		}

		if (IsServices(acptr) && !(IsNetAdmin(sptr) || IsTechAdmin(sptr)
		    || IsULine(sptr)))
		{
			sendto_one(sptr, err_str(ERR_KILLDENY), me.name,
			    parv[0], parv[1]);
			return 0;
		}
	      aftermath:

		/* From here on, the kill is probably going to be successful. */

		kcount++;

		if (!IsServer(sptr) && (kcount > MAXKILLS))
		{
			sendto_one(sptr,
			    ":%s NOTICE %s :*** Too many targets, kill list was truncated. Maximum is %d.",
			    me.name, parv[0], MAXKILLS);
			break;
		}
		if (!IsServer(cptr))
		{
			/*
			   ** The kill originates from this server, initialize path.
			   ** (In which case the 'path' may contain user suplied
			   ** explanation ...or some nasty comment, sigh... >;-)
			   **
			   **   ...!operhost!oper
			   **   ...!operhost!oper (comment)
			 */
			strcpy(inpath,
			    IsHidden(cptr) ? cptr->user->virthost : cptr->user->
			    realhost);
			if (kcount < 2)	/* Only check the path the first time
					   around, or it gets appended to itself. */
				if (!BadPtr(path))
				{
					(void)ircsprintf(buf, "%s%s (%s)",
					    cptr->name,
					    IsOper(sptr) ? "" : "(L)", path);
					path = buf;
				}
				else
					path = cptr->name;
		}
		else if (BadPtr(path))
			path = "*no-path*";	/* Bogus server sending??? */
		/*
		   ** Notify all *local* opers about the KILL (this includes the one
		   ** originating the kill, if from this server--the special numeric
		   ** reply message is not generated anymore).
		   **
		   ** Note: "acptr->name" is used instead of "user" because we may
		   **    have changed the target because of the nickname change.
		 */

		auser = acptr->user ? acptr->user : &UnknownUser;

		if (index(parv[0], '.'))
			sendto_umode(UMODE_KILLS,
			    "*** Notice -- Received KILL message for %s!%s@%s from %s Path: %s!%s",
			    acptr->name, auser->username,
			    IsHidden(acptr) ? auser->virthost : auser->realhost,
			    parv[0], inpath, path);
		else
			sendto_umode(UMODE_KILLS,
			    "*** Notice -- Received KILL message for %s!%s@%s from %s Path: %s!%s",
			    acptr->name, auser->username,
			    IsHidden(acptr) ? auser->virthost : auser->realhost,
			    parv[0], inpath, path);
#if defined(USE_SYSLOG) && defined(SYSLOG_KILL)
		if (IsOper(sptr))
			syslog(LOG_DEBUG, "KILL From %s For %s Path %s!%s",
			    parv[0], acptr->name, inpath, path);
#endif
		/*
		 * By otherguy
		*/
                ircd_log
                    (LOG_KILL, "KILL (%s) by  %s(%s!%s)",
                           make_nick_user_host
                     (acptr->name, acptr->user->username, (IsHidden(acptr) ? acptr->user->virthost : acptr->user->realhost)),
                            parv[0],
                            inpath,
                            path);
		/*
		   ** And pass on the message to other servers. Note, that if KILL
		   ** was changed, the message has to be sent to all links, also
		   ** back.
		   ** Suicide kills are NOT passed on --SRB
		 */
		if (!MyConnect(acptr) || !MyConnect(sptr) || !IsAnOper(sptr))
		{
			sendto_serv_butone(cptr, ":%s KILL %s :%s!%s",
			    parv[0], acptr->name, inpath, path);
			if (chasing && IsServer(cptr))
				sendto_one(cptr, ":%s KILL %s :%s!%s",
				    me.name, acptr->name, inpath, path);
			acptr->flags |= FLAGS_KILLED;
		}

		/*
		   ** Tell the victim she/he has been zapped, but *only* if
		   ** the victim is on current server--no sense in sending the
		   ** notification chasing the above kill, it won't get far
		   ** anyway (as this user don't exist there any more either)
		 */
		if (MyConnect(acptr))
			sendto_prefix_one(acptr, sptr, ":%s KILL %s :%s!%s",
			    parv[0], acptr->name, inpath, path);
		/*
		   ** Set FLAGS_KILLED. This prevents exit_one_client from sending
		   ** the unnecessary QUIT for this. (This flag should never be
		   ** set in any other place)
		 */
		if (MyConnect(acptr) && MyConnect(sptr) && IsAnOper(sptr))

			(void)ircsprintf(buf2, "[%s] Local kill by %s (%s)",
			    me.name, sptr->name,
			    BadPtr(parv[2]) ? sptr->name : parv[2]);
		else
		{
			if ((killer = index(path, ' ')))
			{
				while (*killer && *killer != '!')
					killer--;
				if (!*killer)
					killer = path;
				else
					killer++;
			}
			else
				killer = path;
			(void)ircsprintf(buf2, "Killed (%s)", killer);
		}

		if (exit_client(cptr, acptr, sptr, buf2) == FLUSH_BUFFER)
			return FLUSH_BUFFER;
	}
	return 0;
}

/***********************************************************************
 * m_away() - Added 14 Dec 1988 by jto.
 *            Not currently really working, I don't like this
 *            call at all...
 *
 *            ...trying to make it work. I don't like it either,
 *	      but perhaps it's worth the load it causes to net.
 *	      This requires flooding of the whole net like NICK,
 *	      USER, MODE, etc messages...  --msa
 ***********************************************************************/

/*
** m_away
**	parv[0] = sender prefix
**	parv[1] = away message
*/
int  m_away(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	char *away, *awy2 = parv[1];


	away = sptr->user->away;
	if (parc < 2 || !*awy2)
	{
		/* Marking as not away */

		if (away)
		{
			MyFree(away);
			sptr->user->away = NULL;
		}
		/* hope this works XX */
		sendto_serv_butone_token(cptr, parv[0], MSG_AWAY, TOK_AWAY, "");
		if (MyConnect(sptr))
			sendto_one(sptr, rpl_str(RPL_UNAWAY), me.name, parv[0]);
		return 0;
	}

	/* Marking as away */

	if (strlen(awy2) > (size_t)TOPICLEN)
		awy2[TOPICLEN] = '\0';

	if (away)
		if (strcmp(away, parv[1]) == 0)
		{
			return 0;
		}
	sendto_serv_butone_token(cptr, parv[0], MSG_AWAY, TOK_AWAY, ":%s",
	    awy2);

	if (away)
		away = (char *)MyRealloc(away, strlen(awy2) + 1);
	else
		away = (char *)MyMalloc(strlen(awy2) + 1);

	sptr->user->away = away;
	(void)strcpy(away, awy2);
	if (MyConnect(sptr))
		sendto_one(sptr, rpl_str(RPL_NOWAWAY), me.name, parv[0]);
	return 0;
}

/*
** m_ping
**	parv[0] = sender prefix
**	parv[1] = origin
**	parv[2] = destination
*/
int  m_ping(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	char *origin, *destination;


	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOORIGIN), me.name, parv[0]);
		return 0;
	}
	origin = parv[1];
	destination = parv[2];	/* Will get NULL or pointer (parc >= 2!!) */

	acptr = find_client(origin, NULL);
	if (!acptr)
		acptr = find_server_quick(origin);
	if (acptr && acptr != sptr)
		origin = cptr->name;
	if (!BadPtr(destination) && mycmp(destination, me.name) != 0)
	{
		if ((acptr = find_server_quick(destination)))
			sendto_one(acptr, ":%s PING %s :%s", parv[0],
			    origin, destination);
		else
		{
			sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
			    me.name, parv[0], destination);
			return 0;
		}
	}
	else
		sendto_one(sptr, ":%s %s %s :%s", me.name,
		    IsToken(sptr) ? TOK_PONG : MSG_PONG,
		    (destination) ? destination : me.name, origin);
	return 0;
}

#ifdef NOSPOOF
/*
** m_nospoof - allows clients to respond to no spoofing patch
**	parv[0] = prefix
**	parv[1] = code
*/
int  m_nospoof(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	unsigned long result;

	if (IsNotSpoof(cptr))
		return 0;
	if (IsRegistered(cptr))
		return 0;
	if (!*sptr->name)
		return 0;
	if (BadPtr(parv[1]))
		goto temp;
	result = strtoul(parv[1], NULL, 16);
	/* Accept code in second parameter (ircserv) */
	if (result != sptr->nospoof)
	{
		if (BadPtr(parv[2]))
			goto temp;
		result = strtoul(parv[2], NULL, 16);
		if (result != sptr->nospoof)
			goto temp;
	}
	sptr->nospoof = 0;
	if (sptr->user && sptr->name[0])
		return register_user(cptr, sptr, sptr->name,
		    sptr->user->username, NULL, NULL);
	return 0;
      temp:
	/* Homer compatibility */
	sendto_one(cptr, ":%X!nospoof@%s PRIVMSG %s :%cVERSION%c",
	    cptr->nospoof, me.name, cptr->name, (char)1, (char)1);
	return 0;
}
#endif /* NOSPOOF */

/*
** m_pong
**	parv[0] = sender prefix
**	parv[1] = origin
**	parv[2] = destination
*/
int  m_pong(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	char *origin, *destination;

#ifdef NOSPOOF
	if (!IsRegistered(cptr))
		return m_nospoof(cptr, sptr, parc, parv);
#endif

	if (parc < 2 || *parv[1] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NOORIGIN), me.name, parv[0]);
		return 0;
	}

	origin = parv[1];
	destination = parv[2];
	cptr->flags &= ~FLAGS_PINGSENT;
	sptr->flags &= ~FLAGS_PINGSENT;

	if (!BadPtr(destination) && mycmp(destination, me.name) != 0)
	{
		if ((acptr = find_client(destination, NULL)) ||
		    (acptr = find_server_quick(destination)))
		{
			if (!IsServer(cptr) && !IsServer(acptr))
			{
				sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
				    me.name, parv[0], destination);
				return 0;
			}
			else
				sendto_one(acptr, ":%s PONG %s %s",
				    parv[0], origin, destination);
		}
		else
		{
			sendto_one(sptr, err_str(ERR_NOSUCHSERVER),
			    me.name, parv[0], destination);
			return 0;
		}
	}
#ifdef	DEBUGMODE
	else
		Debug((DEBUG_NOTICE, "PONG: %s %s", origin,
		    destination ? destination : "*"));
#endif
	return 0;
}

/*
** m_mkpasswd
**	parv[0] = sender prefix
**	parv[1] = password to encrypt
*/
#ifndef _WIN32
int  m_mkpasswd(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	static char saltChars[] =
	    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789./";
	char salt[3];
	extern char *crypt();
	int  useable = 0;

	if (!IsAnOper(sptr))
		return -1;
	if (parc > 1)
	{
		if (strlen(parv[1]) >= 1)
			useable = 1;
	}

	if (useable == 0)
	{
		sendto_one(sptr,
		    ":%s NOTICE %s :*** Encryption's MUST be atleast 1 character in length",
		    me.name, parv[0]);
		return 0;
	}
	srandom(TStime());
#ifndef _WIN32
	salt[0] = saltChars[random() % 64];
	salt[1] = saltChars[random() % 64];
	salt[2] = 0;
#else
	salt[0] = saltChars[rand() % 64];
	salt[1] = saltChars[rand() % 64];
	salt[2] = 0;
#endif
	if ((strchr(saltChars, salt[0]) == NULL)
	    || (strchr(saltChars, salt[1]) == NULL))
	{
		sendto_one(sptr, ":%s NOTICE %s :*** Illegal salt %s", me.name,
		    parv[0], salt);
		return 0;
	}


	sendto_one(sptr, ":%s NOTICE %s :*** Encryption for [%s] is %s",
	    me.name, parv[0], parv[1], crypt(parv[1], salt));
	return 0;
}

#else
int  m_mkpasswd(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	sendto_one(sptr,
	    ":%s NOTICE %s :*** Encryption is disabled on UnrealIRCD-win32",
	    me.name, parv[0]);
	return 0;
}

#endif

/*
** m_oper
**	parv[0] = sender prefix
**	parv[1] = oper name
**	parv[2] = oper password
*/
int  SVSNOOP;

int  m_oper(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	ConfigItem_oper *aconf;
	ConfigItem_oper_from *oper_from;
	char *name, *password, *encr, *nuhhost;
#ifdef CRYPT_OPER_PASSWORD
	char salt[3];
	extern char *crypt();
#endif /* CRYPT_OPER_PASSWORD */


	name = parc > 1 ? parv[1] : NULL;
	password = parc > 2 ? parv[2] : NULL;

	if (!IsServer(cptr) && (BadPtr(name) || BadPtr(password)))
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "OPER");
		return 0;
	}

	if (SVSNOOP)
	{
		sendto_one(sptr,
		    ":%s NOTICE %s :*** This server is in NOOP mode, you cannot /oper",
		    me.name, sptr->name);
		return 0;
	}

	/* if message arrived from server, trust it, and set to oper */

	if ((IsServer(cptr) || IsMe(cptr)) && !IsOper(sptr))
	{
		sptr->umodes |= UMODE_OPER;
		sendto_serv_butone(cptr, ":%s MODE %s :+o", parv[0], parv[0]);
		if (IsMe(cptr))
		{
			sendto_one(sptr, rpl_str(RPL_YOUREOPER),
			    me.name, parv[0]);
		}
		return 0;
	}
	else if (IsOper(sptr))
	{
		if (MyConnect(sptr))
			sendto_one(sptr, rpl_str(RPL_YOUREOPER),
			    me.name, parv[0]);
		return 0;
	}

	if (!(aconf = Find_oper(name)))
	{
		sendto_one(sptr, err_str(ERR_NOOPERHOST), me.name, parv[0]);
		sendto_realops
		    ("Failed OPER attempt by %s (%s@%s) [unknown oper]",
		    parv[0], sptr->user->username, sptr->sockhost);
		sptr->since += 7;
		return 0;
	}
	nuhhost = make_user_host(sptr->user->username, sptr->user->realhost);
	for (oper_from = (ConfigItem_oper_from *) aconf->from;
	    oper_from; oper_from = (ConfigItem_oper_from *) oper_from->next)
		if (!match(oper_from->name, nuhhost))
			break;
	if (!oper_from)
	{
		sendto_one(sptr, err_str(ERR_NOOPERHOST), me.name, parv[0]);
		sendto_realops
		    ("Failed OPER attempt by %s (%s@%s) [host doesnt match]",
		    parv[0], sptr->user->username, sptr->sockhost);
		sptr->since += 7;
		return 0;
	}

#ifdef CRYPT_OPER_PASSWORD
	/* use first two chars of the password they send in as salt */

	/* passwd may be NULL. Head it off at the pass... */
	salt[0] = '\0';
	if (password && aconf->password && aconf->password[0]
	    && aconf->password[1])
	{
		salt[0] = aconf->password[0];
		salt[1] = aconf->password[1];
		salt[2] = '\0';
		encr = crypt(password, salt);
	}
	else
	{
		encr = "";
	}
#else /* CRYPT_OPER_PASSWORD */
	encr = password;
#endif /* CRYPT_OPER_PASSWORD */
	if (StrEq(encr, aconf->password))
	{
		int  old = (sptr->umodes & ALL_UMODES);
		char *s;

		/* Put in the right class */
		if (sptr->class)
			sptr->class->clients--;

		sptr->class = aconf->class;
		sptr->class->clients++;

		if (aconf->swhois) {
			if (sptr->user->swhois)
				MyFree(sptr->user->swhois);
			sptr->user->swhois = MyMalloc(strlen(aconf->swhois) +1);
			strcpy(sptr->user->swhois, aconf->swhois);
			sendto_serv_butone_token(cptr, sptr->name,
				MSG_SWHOIS, TOK_SWHOIS, "%s :%s", sptr->name, aconf->swhois);
		}
		if ((aconf->oflags & OFLAG_HELPOP))
		{
			sptr->umodes |= UMODE_HELPOP;
		}

		if (!(aconf->oflags & OFLAG_ISGLOBAL))
		{
			SetLocOp(sptr);
		}
		else if (aconf->oflags & OFLAG_NETADMIN)
		{
			if (aconf->oflags & OFLAG_SADMIN)
			{
				sptr->umodes |=
				    (UMODE_NETADMIN | UMODE_ADMIN |
				    UMODE_SADMIN);
				SetNetAdmin(sptr);
				SetSAdmin(sptr);
				SetAdmin(sptr);

				SetOper(sptr);
			}
			else
			{
				sptr->umodes |= (UMODE_NETADMIN | UMODE_ADMIN);
				SetNetAdmin(sptr);
				SetAdmin(sptr);
				SetOper(sptr);

			}
		}
		else if (aconf->oflags & OFLAG_COADMIN)
		{
			if (aconf->oflags & OFLAG_SADMIN)
			{
				sptr->umodes |=
				    (UMODE_COADMIN | UMODE_ADMIN |
				    UMODE_SADMIN);
				SetCoAdmin(sptr);
				SetSAdmin(sptr);
				SetAdmin(sptr);
				SetOper(sptr);

			}
			else
			{
				sptr->umodes |= (UMODE_COADMIN | UMODE_ADMIN);
				SetCoAdmin(sptr);
				SetAdmin(sptr);
				SetOper(sptr);

			}
		}
		else if (aconf->oflags & OFLAG_TECHADMIN)
		{
			if (aconf->oflags & OFLAG_SADMIN)
			{
				sptr->umodes |=
				    (UMODE_TECHADMIN | UMODE_ADMIN |
				    UMODE_SADMIN);
				SetTechAdmin(sptr);
				SetSAdmin(sptr);
				SetAdmin(sptr);
				SetOper(sptr);

			}
			else
			{
				sptr->umodes |= (UMODE_TECHADMIN | UMODE_ADMIN);
				SetTechAdmin(sptr);
				SetAdmin(sptr);
				SetOper(sptr);

			}
		}
		else
		    if (aconf->oflags & OFLAG_ADMIN
		    && aconf->oflags & OFLAG_SADMIN)
		{
			sptr->umodes |= (UMODE_ADMIN | UMODE_SADMIN);
			SetAdmin(sptr);
			SetSAdmin(sptr);
			SetOper(sptr);

		}
		else if (aconf->oflags & OFLAG_SADMIN)
		{
			sptr->umodes |= (UMODE_SADMIN);
			SetSAdmin(sptr);
			SetOper(sptr);

		}
		else if (aconf->oflags & OFLAG_ADMIN)
		{
			sptr->umodes |= (UMODE_ADMIN);
			SetAdmin(sptr);
			SetOper(sptr);

		}
		else
		{
			if (aconf->oflags & OFLAG_SADMIN)
			{
				sptr->umodes |= (UMODE_OPER | UMODE_SADMIN);
				SetSAdmin(sptr);
				SetOper(sptr);

			}
			else
			{
				sptr->umodes |= (UMODE_OPER);
				SetOper(sptr);

			}
		}

		if (aconf->oflags & OFLAG_EYES)
		{
			sptr->umodes |= (UMODE_EYES);
			SetEyes(sptr);
		}

		if (aconf->oflags & OFLAG_WHOIS)
		{
			sptr->umodes |= (UMODE_WHOIS);
		}

		if (aconf->oflags & OFLAG_HIDE)
		{
			sptr->umodes |= (UMODE_HIDE);
		}

		sptr->oflag = aconf->oflags;

		sptr->umodes |=
		    (UMODE_SERVNOTICE | UMODE_WALLOP | UMODE_FAILOP |
		    UMODE_FLOOD | UMODE_CLIENT | UMODE_KILLS);
		send_umode_out(cptr, sptr, old);
#ifndef NO_FDLIST
		addto_fdlist(sptr->slot, &oper_fdlist);
#endif
		sendto_one(sptr, rpl_str(RPL_YOUREOPER), me.name, parv[0]);

		if (!(aconf->oflags & OFLAG_ISGLOBAL))
		{
			sendto_ops("%s (%s@%s) is now a local operator (o)",
			    parv[0], sptr->user->username,
			    IsHidden(sptr) ? sptr->user->virthost : sptr->
			    user->realhost);
			if (iNAH == 1 && (sptr->oflag & OFLAG_HIDE))
				iNAH_host(sptr, locop_host);
			sptr->umodes &= ~UMODE_OPER;
		}
/*        	else
        if ((aconf->oflags & OFLAG_AGENT))
        {
			sendto_ops("%s (%s@%s) is now an IRCd Agent (S)", parv[0],
				sptr->user->username, IsHidden(sptr) ? sptr->user->virthost : sptr->user->realhost);
        }*/
		else if (aconf->oflags & OFLAG_NETADMIN)
		{
			sendto_ops
			    ("%s (%s@%s) is now a network administrator (N)",
			    parv[0], sptr->user->username,
			    IsHidden(cptr) ? sptr->user->virthost : sptr->
			    user->realhost);
			if (MyClient(sptr))
			{
				sendto_serv_butone(&me,
				    ":%s GLOBOPS :%s (%s@%s) is now a network administrator (N)",
				    me.name, parv[0], sptr->user->username,
				    IsHidden(sptr) ? sptr->
				    user->virthost : sptr->user->realhost);
			}

			if (iNAH == 1 && (sptr->oflag & OFLAG_HIDE))
				iNAH_host(sptr, netadmin_host);
		}
		else if (aconf->oflags & OFLAG_COADMIN)
		{
			sendto_ops("%s (%s@%s) is now a co administrator (C)",
			    parv[0], sptr->user->username,
			    IsHidden(sptr) ? sptr->user->virthost : sptr->
			    user->realhost);
			if (MyClient(sptr))
			{
				/*      sendto_serv_butone(&me, ":%s GLOBOPS :%s (%s@%s) is now a co administrator (C)", me.name, parv[0],
				   sptr->user->username, IsHidden(sptr) ? sptr->user->virthost : sptr->user->realhost);
				 */
			}
			if (iNAH == 1 && (sptr->oflag & OFLAG_HIDE))
				iNAH_host(sptr, coadmin_host);
		}
		else if (aconf->oflags & OFLAG_TECHADMIN)
		{
			sendto_ops
			    ("%s (%s@%s) is now a technical administrator (T)",
			    parv[0], sptr->user->username,
			    IsHidden(sptr) ? sptr->user->virthost : sptr->
			    user->realhost);
			if (MyClient(sptr))
			{
				sendto_serv_butone(&me,
				    ":%s GLOBOPS :%s (%s@%s) is now a technical administrator (T)",
				    me.name, parv[0], sptr->user->username,
				    IsHidden(sptr) ? sptr->
				    user->virthost : sptr->user->realhost);
			}
			if (iNAH == 1 && (sptr->oflag & OFLAG_HIDE))
				iNAH_host(sptr, techadmin_host);
		}
		else if (aconf->oflags & OFLAG_SADMIN)
		{
			sendto_ops("%s (%s@%s) is now a services admin (a)",
			    parv[0], sptr->user->username,
			    IsHidden(sptr) ? sptr->user->virthost : sptr->
			    user->realhost);
			if (MyClient(sptr))
			{
				sendto_serv_butone(&me,
				    ":%s GLOBOPS :%s (%s@%s) is now a services administrator (a)",
				    me.name, parv[0], sptr->user->username,
				    IsHidden(sptr) ? sptr->
				    user->virthost : sptr->user->realhost);
			}
			if (iNAH == 1 && (sptr->oflag & OFLAG_HIDE))
				iNAH_host(sptr, sadmin_host);
		}
		else if (aconf->oflags & OFLAG_ADMIN)
		{
			sendto_ops("%s (%s@%s) is now a server admin (A)",
			    parv[0], sptr->user->username,
			    IsHidden(sptr) ? sptr->user->virthost : sptr->
			    user->realhost);
			if (iNAH == 1 && (sptr->oflag & OFLAG_HIDE))
				iNAH_host(sptr, admin_host);
		}
		else
		{
			sendto_ops("%s (%s@%s) is now an operator (O)", parv[0],
			    sptr->user->username,
			    IsHidden(sptr) ? sptr->user->virthost : sptr->
			    user->realhost);
			if (iNAH == 1 && (sptr->oflag & OFLAG_HIDE))
				iNAH_host(sptr, oper_host);
		}

		if (IsOper(sptr))
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

#if !defined(CRYPT_OPER_PASSWORD) && (defined(FNAME_OPERLOG) ||\
    (defined(USE_SYSLOG) && defined(SYSLOG_OPER)))
		encr = "";
#endif
#if defined(USE_SYSLOG) && defined(SYSLOG_OPER)
		syslog(LOG_INFO, "OPER (%s) (%s) by (%s!%s@%s)",
		    name, encr, parv[0], sptr->user->username, sptr->sockhost);
#endif
		ircd_log(LOG_OPER, "OPER (%s) by (%s!%s@%s)", name, parv[0], sptr->user->username,
			sptr->sockhost);

	}
	else
	{
		sendto_one(sptr, err_str(ERR_PASSWDMISMATCH), me.name, parv[0]);
#ifdef  FAILOPER_WARN
		sendto_one(sptr,
		    ":%s NOTICE %s :*** Your attempt has been logged.", me.name,
		    sptr->name);
#endif
		sendto_realops
		    ("Failed OPER attempt by %s (%s@%s) using UID %s [NOPASSWORD]",
		    parv[0], sptr->user->username, sptr->sockhost, name);
		sendto_serv_butone(&me,
		    ":%s GLOBOPS :Failed OPER attempt by %s (%s@%s) using UID %s [---]",
		    me.name, parv[0], sptr->user->username, sptr->sockhost,
		    name);
		sptr->since += 7;
#ifdef FNAME_OPERLOG
		{
			int  logfile;

			/*
			 * This conditional makes the logfile active only after
			 * it's been created - thus logging can be turned off by
			 * removing the file.
			 *
			 * stop NFS hangs...most systems should be able to open a
			 * file in 3 seconds. -avalon (curtesy of wumpus)
			 */
			if (IsPerson(sptr) &&
			    (logfile =
			    open(FNAME_OPERLOG, O_WRONLY | O_APPEND)) != -1)
			{

				(void)ircsprintf(buf,
				    "%s FAILED OPER (%s) (%s) by (%s!%s@%s)\n PASSWORD %s",
				    myctime(TStime()), name, encr, parv[0],
				    sptr->user->username, sptr->sockhost,
				    password);
				(void)write(logfile, buf, strlen(buf));
				(void)close(logfile);
			}
			/* Modification by pjg */
		}
#endif
	}
	return 0;
}

/***************************************************************************
 * m_pass() - Added Sat, 4 March 1989
 ***************************************************************************/

/*
** m_pass
**	parv[0] = sender prefix
**	parv[1] = password
*/
int  m_pass(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	char *password = parc > 1 ? parv[1] : NULL;
	int  PassLen = 0;
	if (BadPtr(password))
	{
		sendto_one(cptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "PASS");
		return 0;
	}
	if (!MyConnect(sptr) || (!IsUnknown(cptr) && !IsHandshake(cptr)))
	{
		sendto_one(cptr, err_str(ERR_ALREADYREGISTRED),
		    me.name, parv[0]);
		return 0;
	}
	PassLen = strlen(password);
	if (cptr->passwd)
		MyFree(cptr->passwd);
	if (PassLen > (PASSWDLEN))
		PassLen = PASSWDLEN;
	cptr->passwd = MyMalloc(PassLen + 1);
	strncpyzt(cptr->passwd, password, PassLen + 1);
	return 0;
}

/*
 * m_userhost added by Darren Reed 13/8/91 to aid clients and reduce
 * the need for complicated requests like WHOIS. It returns user/host
 * information only (no spurious AWAY labels or channels).
 * Re-written by Dianora 1999
 */
int  m_userhost(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{

	char *p;		/* scratch end pointer */
	char *cn;		/* current name */
	struct Client *acptr;
	char response[5][NICKLEN * 2 + CHANNELLEN + USERLEN + HOSTLEN + 30];
	int  i;			/* loop counter */

	if (parc < 2)
	{
		sendto_one(sptr, rpl_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "USERHOST");
		return 0;
	}

	/* The idea is to build up the response string out of pieces
	 * none of this strlen() nonsense.
	 * 5 * (NICKLEN*2+CHANNELLEN+USERLEN+HOSTLEN+30) is still << sizeof(buf)
	 * and our ircsprintf() truncates it to fit anyway. There is
	 * no danger of an overflow here. -Dianora
	 */
	response[0][0] = response[1][0] = response[2][0] =
	    response[3][0] = response[4][0] = '\0';

	cn = parv[1];

	for (i = 0; (i < 5) && cn; i++)
	{
		if ((p = strchr(cn, ' ')))
			*p = '\0';

		if ((acptr = find_person(cn, NULL)))
		{
			ircsprintf(response[i], "%s%s=%c%s@%s",
			    acptr->name,
			    IsAnOper(acptr) ? "*" : "",
			    (acptr->user->away) ? '-' : '+',
			    acptr->user->username,
			    ((acptr != sptr) && !IsOper(sptr)
			    && IsHidden(acptr) ? acptr->user->virthost :
			    acptr->user->realhost));
		}
		if (p)
			p++;
		cn = p;
	}

	ircsprintf(buf, "%s%s %s %s %s %s",
	    rpl_str(RPL_USERHOST),
	    response[0], response[1], response[2], response[3], response[4]);
	sendto_one(sptr, buf, me.name, parv[0]);

	return 0;
}

/*
 * m_ison added by Darren Reed 13/8/91 to act as an efficent user indicator
 * with respect to cpu/bandwidth used. Implemented for NOTIFY feature in
 * clients. Designed to reduce number of whois requests. Can process
 * nicknames in batches as long as the maximum buffer length.
 *
 * format:
 * ISON :nicklist
 */

int  m_ison(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	char namebuf[USERLEN + HOSTLEN + 4];
	aClient *acptr;
	char *s, **pav = parv, *user;
	int  len;
	char *p = NULL;


	if (parc < 2)
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "ISON");
		return 0;
	}

	(void)ircsprintf(buf, rpl_str(RPL_ISON), me.name, *parv);
	len = strlen(buf);
#ifndef NO_FDLIST
	cptr->priority += 30;	/* this keeps it from moving to 'busy' list */
#endif
	for (s = strtoken(&p, *++pav, " "); s; s = strtoken(&p, NULL, " "))
	{
		if (user = index(s, '!'))
			*user++ = '\0';
		if ((acptr = find_person(s, NULL)))
		{
			if (user)
			{
				strcpy(namebuf, acptr->user->username);
				strcat(namebuf, "@");
				strcat(namebuf, acptr->user->realhost);
				if (match(user, namebuf))
					continue;
				*--user = '!';
			}

			(void)strncat(buf, s, sizeof(buf) - len);
			len += strlen(s);
			(void)strncat(buf, " ", sizeof(buf) - len);
			len++;
		}
	}
	sendto_one(sptr, "%s", buf);
	return 0;
}


/*
 * m_umode() added 15/10/91 By Darren Reed.
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 */
int  m_umode(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	int  flag;
	int *s;
	char **p, *m;
	aClient *acptr;
	int  what, setflags;
	short rpterror = 0;

	what = MODE_ADD;

	if (parc < 2)
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS),
		    me.name, parv[0], "MODE");
		return 0;
	}

	if (!(acptr = find_person(parv[1], NULL)))
	{
		if (MyConnect(sptr))
			sendto_one(sptr, err_str(ERR_NOSUCHNICK),
			    me.name, parv[0], parv[1]);
		return 0;
	}
	if (acptr != sptr)
		return 0;

	if (parc < 3)
	{
/*
 * we use get_mode_str() now
		m = buf;
		*m++ = '+';
		for (s = user_modes; (flag = *s) && (m - buf < BUFSIZE - 4);
		     s += 2)
			if ((sptr->umodes & flag))
				*m++ = (char)(*(s+1));
		*m = '\0';
*/
		sendto_one(sptr, rpl_str(RPL_UMODEIS),
		    me.name, parv[0], get_mode_str(sptr));
		return 0;
	}

	/* find flags already set for user */
	setflags = 0;
	for (s = user_modes; (flag = *s); s += 2)
		if ((sptr->umodes & flag))
			setflags |= flag;

	/*
	 * parse mode change string(s)
	 */
	for (p = &parv[2]; p && *p; p++)
		for (m = *p; *m; m++)
			switch (*m)
			{
			  case '+':
				  what = MODE_ADD;
				  break;
			  case '-':
				  what = MODE_DEL;
				  break;

				  /* we may not get these,
				   * but they shouldnt be in default
				   */
			  case ' ':
			  case '\n':
			  case '\r':
			  case '\t':
				  break;
			  case 'r':
			  case 't':
				  if (MyClient(sptr))
					  break;
				  /* since we now use chatops define in unrealircd.conf, we have
				   * to disallow it here */
			  case 'b':
				  if (ALLOW_CHATOPS == 0 && what == MODE_ADD
				      && MyClient(sptr))
					  break;
				  goto def;
			  case 'I':
				  if (NO_OPER_HIDING == 1 && what == MODE_ADD
				      && MyClient(sptr))
					  break;
				  goto def;
			  case 'B':
				  if (what == MODE_ADD && MyClient(sptr))
					  (void)m_botmotd(sptr, sptr, 1, parv);
			  default:
				def:
				  for (s = user_modes; (flag = *s); s += 2)
					  if (*m == (char)(*(s + 1)))
					  {
						  if (what == MODE_ADD)
						  {
							  sptr->umodes |= flag;
						  }
						  else
						  {
							  sptr->umodes &= ~flag;
						  }

						  break;
					  }

				  if (flag == 0 && MyConnect(sptr) && !rpterror)
				  {
					  sendto_one(sptr,
					      err_str(ERR_UMODEUNKNOWNFLAG),
					      me.name, parv[0]);
					  rpterror = 1;
				  }
				  break;
			}
	/*
	 * stop users making themselves operators too easily
	 */

	if (!(setflags & UMODE_OPER) && IsOper(sptr) && !IsServer(cptr))
		ClearOper(sptr);
	if (!(setflags & UMODE_LOCOP) && IsLocOp(sptr) && !IsServer(cptr))
		sptr->umodes &= ~UMODE_LOCOP;
	/*
	 *  Let only operators set HelpOp
	 * Helpops get all /quote help <mess> globals -Donwulff
	 */
	if (MyClient(sptr) && IsHelpOp(sptr) && !OPCanHelpOp(sptr))
		ClearHelpOp(sptr);
	/*
	 * Let only operators set FloodF, ClientF; also
	 * remove those flags if they've gone -o/-O.
	 *  FloodF sends notices about possible flooding -Cabal95
	 *  ClientF sends notices about clients connecting or exiting
	 *  Admin is for server admins
	 */
	if (!IsAnOper(sptr) && !IsServer(cptr))
	{
		if (IsWhois(sptr))
			sptr->umodes &= ~UMODE_WHOIS;
		if (IsClientF(sptr))
			ClearClientF(sptr);
		if (IsFloodF(sptr))
			ClearFloodF(sptr);
		if (IsAdmin(sptr))
			ClearAdmin(sptr);
		if (IsSAdmin(sptr))
			ClearSAdmin(sptr);
		if (IsNetAdmin(sptr))
			ClearNetAdmin(sptr);
		if (IsHideOper(sptr))
			ClearHideOper(sptr);
		if (IsCoAdmin(sptr))
			ClearCoAdmin(sptr);
		if (IsTechAdmin(sptr))
			ClearTechAdmin(sptr);
		if (IsEyes(sptr))
			ClearEyes(sptr);
	}

	/*
	 * New oper access flags - Only let them set certian usermodes on
	 * themselves IF they have access to set that specific mode in their
	 * O:Line.
	 */
	if (MyClient(sptr) && IsAnOper(sptr))
	{
		if (IsClientF(sptr) && !OPCanUModeC(sptr))
			ClearClientF(sptr);
		if (IsFloodF(sptr) && !OPCanUModeF(sptr))
			ClearFloodF(sptr);
		if (IsAdmin(sptr) && !OPIsAdmin(sptr))
			ClearAdmin(sptr);
		if (IsSAdmin(sptr) && !OPIsSAdmin(sptr))
			ClearSAdmin(sptr);
		if (IsNetAdmin(sptr) && !OPIsNetAdmin(sptr))
			ClearNetAdmin(sptr);
		if (IsCoAdmin(sptr) && !OPIsCoAdmin(sptr))
			ClearCoAdmin(sptr);
		if (IsTechAdmin(sptr) && !OPIsTechAdmin(sptr))
			ClearTechAdmin(sptr);
		if ((sptr->umodes & UMODE_HIDING)
		    && !(sptr->oflag & OFLAG_INVISIBLE))
			sptr->umodes &= ~UMODE_HIDING;
		if (MyClient(sptr) && (sptr->umodes & UMODE_SECURE)
		    && !IsSecure(sptr))
			sptr->umodes &= ~UMODE_SECURE;
		if (IsTechAdmin(sptr) && IsNetAdmin(sptr))
			ClearTechAdmin(sptr);
	}

	/*
	 * For Services Protection...
	 */
	if (!IsServer(cptr) && !IsULine(sptr))
	{
		if (IsServices(sptr))
			ClearServices(sptr);
/*        	if (IsDeaf(sptr))
        		sptr->umodes &= ~UMODE_DEAF;
*/
	}
	if ((setflags & UMODE_HIDE) && !IsHidden(sptr))
		sptr->umodes &= ~UMODE_SETHOST;

	if (IsHidden(sptr) && !(setflags & UMODE_HIDE))
	{
		sptr->user->virthost =
		    (char *)make_virthost(sptr->user->realhost,
		    sptr->user->virthost, 1);
	}

	/*
	   This is to remooove the kix bug.. and to protect some stuffie
	   -techie
	 */
	if (MyConnect(sptr))
	{
		if ((sptr->umodes & (UMODE_KIX)) && !(IsNetAdmin(sptr)
		    || IsTechAdmin(sptr)))
			sptr->umodes &= ~UMODE_KIX;
		if ((sptr->umodes & (UMODE_FCLIENT)) && !IsOper(sptr))
			sptr->umodes &= ~UMODE_FCLIENT;

		/* Agents
		   if ((sptr->umodes & (UMODE_AGENT)) && !(sptr->oflag & OFLAG_AGENT))
		   sptr->umodes &= ~UMODE_AGENT; */
		if ((sptr->umodes & UMODE_HIDING) && !IsAnOper(sptr))
			sptr->umodes &= ~UMODE_HIDING;


		if ((sptr->umodes & UMODE_HIDING)
		    && !(sptr->oflag & OFLAG_INVISIBLE))
			sptr->umodes &= ~UMODE_HIDING;
		if (MyClient(sptr) && (sptr->umodes & UMODE_SECURE)
		    && !IsSecure(sptr))
			sptr->umodes &= ~UMODE_SECURE;

		if ((sptr->umodes & (UMODE_HIDING))
		    && !(setflags & UMODE_HIDING))
		{
			sendto_umode(UMODE_ADMIN,
			    "[+I] Activated total invisibility mode on %s",
			    sptr->name);
			sendto_serv_butone(cptr,
			    ":%s SMO A :[+I] Activated total invisibility mode on %s",
			    me.name, sptr->name);
			sendto_channels_inviso_part(sptr);
		}
		if ((sptr->umodes & UMODE_JUNK) && !IsOper(sptr))
			sptr->umodes &= ~UMODE_JUNK;

		if (!(sptr->umodes & (UMODE_HIDING)))
		{
			if (setflags & UMODE_HIDING)
			{
				sendto_umode(UMODE_ADMIN,
				    "[+I] De-activated total invisibility mode on %s",
				    sptr->name);
				sendto_serv_butone(cptr,
				    ":%s SMO A :[+I] De-activated total invisibility mode on %s",
				    me.name, sptr->name);
				sendto_channels_inviso_join(sptr);

			}
		}
	}
	/*
	 * If I understand what this code is doing correctly...
	 *   If the user WAS an operator and has now set themselves -o/-O
	 *   then remove their access, d'oh!
	 * In order to allow opers to do stuff like go +o, +h, -o and
	 * remain +h, I moved this code below those checks. It should be
	 * O.K. The above code just does normal access flag checks. This
	 * only changes the operflag access level.  -Cabal95
	 */
	if ((setflags & (UMODE_OPER | UMODE_LOCOP)) && !IsAnOper(sptr) &&
	    MyConnect(sptr))
	{
#ifndef NO_FDLIST
		delfrom_fdlist(sptr->slot, &oper_fdlist);
#endif
		sptr->oflag = 0;
	}
	if (!(setflags & UMODE_OPER) && IsOper(sptr))
		IRCstats.operators++;
	if ((setflags & UMODE_OPER) && !IsOper(sptr))
	{
		IRCstats.operators--;
#ifndef NO_FDLIST
		if (MyConnect(sptr))
			delfrom_fdlist(sptr->slot, &oper_fdlist);
#endif
	}
	if (!(setflags & UMODE_INVISIBLE) && IsInvisible(sptr))
	{
		IRCstats.invisible++;
	}
	if ((setflags & UMODE_INVISIBLE) && !IsInvisible(sptr))
	{
		IRCstats.invisible--;
	}
	/*
	 * compare new flags with old flags and send string which
	 * will cause servers to update correctly.
	 */
	if (dontspread == 0)
		send_umode_out(cptr, sptr, setflags);

	return 0;
}

/*
    m_umode2 added by Stskeeps
    parv[0] - sender
    parv[1] - modes to change

    Small wrapper to bandwidth save
*/

int  m_umode2(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	char *xparv[4] = {
		parv[0],
		parv[0],
		parv[1],
		NULL
	};

	m_umode(cptr, sptr, 3, xparv);
}

/*
 * m_svs2mode() added by Potvin
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 * parv[3] - Service Stamp (if mode == d)
 */
int  m_svs2mode(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	int  flag;
	int *s;
	char **p, *m;
	aClient *acptr;
	int  what, setflags;

	if (!IsULine(sptr))
		return 0;

	what = MODE_ADD;

	if (parc < 3)
		return 0;

	if (!(acptr = find_person(parv[1], NULL)))
		return 0;

	setflags = 0;
	for (s = user_modes; (flag = *s); s += 2)
		if (acptr->umodes & flag)
			setflags |= flag;
	/*
	 * parse mode change string(s)
	 */
	for (p = &parv[2]; p && *p; p++)
		for (m = *p; *m; m++)
			switch (*m)
			{
			  case '+':
				  what = MODE_ADD;
				  break;
			  case '-':
				  what = MODE_DEL;
				  break;
				  /* we may not get these,
				   * but they shouldnt be in default
				   */
			  case ' ':
			  case '\n':
			  case '\r':
			  case '\t':
				  break;
			  case 'l':
				  if (parv[3] && isdigit(*parv[3]))
					  max_global_count = atoi(parv[3]);
				  break;
			  case 'd':
				  if (parv[3] && (isdigit(*parv[3])
				      || (*parv[3] == '!')))
					  acptr->user->servicestamp =
					      TS2ts(parv[3]);
				  break;
			  case 'i':
				  if (what == MODE_ADD
				      && !(acptr->umodes & UMODE_INVISIBLE))
				  {
					  IRCstats.invisible++;
				  }
				  if (what == MODE_DEL
				      && (acptr->umodes & UMODE_INVISIBLE))
				  {
					  IRCstats.invisible--;
				  }
				  goto setmodey;
			  case 'o':
				  if (what == MODE_ADD
				      && !(acptr->umodes & UMODE_OPER))
					  IRCstats.operators++;
				  if (what == MODE_DEL
				      && (acptr->umodes & UMODE_OPER))
					  IRCstats.operators--;
			  default:
				setmodey:
				  for (s = user_modes; (flag = *s); s += 2)
					  if (*m == (char)(*(s + 1)))
					  {
						  if (what == MODE_ADD)
							  acptr->umodes |= flag;
						  else
							  acptr->umodes &=
							      ~flag;
						  break;
					  }
				  break;
			}

	if (parc > 3)
		sendto_serv_butone_token(cptr, parv[0], MSG_SVS2MODE,
		    TOK_SVS2MODE, "%s %s %s", parv[1], parv[2], parv[3]);
	else
		sendto_serv_butone_token(cptr, parv[0], MSG_SVS2MODE,
		    TOK_SVS2MODE, "%s %s", parv[1], parv[2]);

	send_umode(NULL, acptr, setflags, ALL_UMODES, buf);
	if (MyClient(acptr) && buf[0] && buf[1])
		sendto_one(acptr, ":%s MODE %s :%s", parv[0], parv[1], buf);
	return 0;
}

/*
 * m_svsmode() added by taz
 * parv[0] - sender
 * parv[1] - username to change mode for
 * parv[2] - modes to change
 * parv[3] - Service Stamp (if mode == d)
 */
int  m_svsmode(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	int  flag;
	int *s;
	char **p, *m;
	aClient *acptr;
	int  what, setflags;

	if (!IsULine(sptr))
		return 0;

	what = MODE_ADD;

	if (parc < 3)
		return 0;

	if (!(acptr = find_person(parv[1], NULL)))
		return 0;

	setflags = 0;
	for (s = user_modes; (flag = *s); s += 2)
		if (acptr->umodes & flag)
			setflags |= flag;
	/*
	 * parse mode change string(s)
	 */
	for (p = &parv[2]; p && *p; p++)
		for (m = *p; *m; m++)
			switch (*m)
			{
			  case '+':
				  what = MODE_ADD;
				  break;
			  case '-':
				  what = MODE_DEL;
				  break;
				  /* we may not get these,
				   * but they shouldnt be in default
				   */
			  case ' ':
			  case '\n':
			  case '\r':
			  case '\t':
				  break;
			  case 'i':
				  if (what == MODE_ADD
				      && !(acptr->umodes & UMODE_INVISIBLE))
				  {
					  IRCstats.invisible++;
				  }
				  if (what == MODE_DEL
				      && (acptr->umodes & UMODE_INVISIBLE))
				  {

					  IRCstats.invisible--;
				  }
				  goto setmodex;
			  case 'o':
				  if (what == MODE_ADD
				      && !(acptr->umodes & UMODE_OPER))
					  IRCstats.operators++;
				  if (what == MODE_DEL
				      && (acptr->umodes & UMODE_OPER))
					  IRCstats.operators--;
				  goto setmodex;
			  case 'd':
				  if (parv[3] && isdigit(*parv[3]))
				  {
					  acptr->user->servicestamp =
					      atol(parv[3]);
					  break;
				  }
			  default:
				setmodex:
				  for (s = user_modes; (flag = *s); s += 2)
					  if (*m == (char)(*(s + 1)))
					  {
						  if (what == MODE_ADD)
							  acptr->umodes |= flag;
						  else
							  acptr->umodes &=
							      ~flag;
						  break;
					  }
				  break;
			}


	if (parc > 3)
		sendto_serv_butone_token(cptr, parv[0], MSG_SVSMODE,
		    TOK_SVSMODE, "%s %s %s", parv[1], parv[2], parv[3]);
	else
		sendto_serv_butone_token(cptr, parv[0], MSG_SVSMODE,
		    TOK_SVSMODE, "%s %s", parv[1], parv[2]);

	return 0;
}

/*
 * send the MODE string for user (user) to connection cptr
 * -avalon
 */
void send_umode(cptr, sptr, old, sendmask, umode_buf)
	aClient *cptr, *sptr;
	long old, sendmask;
	char *umode_buf;
{
	int *s, flag;
	char *m;
	int  what = MODE_NULL;

	/*
	 * build a string in umode_buf to represent the change in the user's
	 * mode between the new (sptr->flag) and 'old'.
	 */
	m = umode_buf;
	*m = '\0';
	for (s = user_modes; (flag = *s); s += 2)
	{
		if (MyClient(sptr) && !(flag & sendmask))
			continue;
		if ((flag & old) && !(sptr->umodes & flag))
		{
			if (what == MODE_DEL)
				*m++ = *(s + 1);
			else
			{
				what = MODE_DEL;
				*m++ = '-';
				*m++ = *(s + 1);
			}
		}
		else if (!(flag & old) && (sptr->umodes & flag))
		{
			if (what == MODE_ADD)
				*m++ = *(s + 1);
			else
			{
				what = MODE_ADD;
				*m++ = '+';
				*m++ = *(s + 1);
			}
		}
	}
	*m = '\0';
	if (*umode_buf && cptr)
		sendto_one(cptr, ":%s %s %s :%s", sptr->name,
		    (IsToken(cptr) ? TOK_MODE : MSG_MODE),
		    sptr->name, umode_buf);
}

/*
 * added Sat Jul 25 07:30:42 EST 1992
 */
void send_umode_out(cptr, sptr, old)
	aClient *cptr, *sptr;
	long old;
{
	int  i;
	aClient *acptr;

	send_umode(NULL, sptr, old, SEND_UMODES, buf);

	for (i = LastSlot; i >= 0; i--)
		if ((acptr = local[i]) && IsServer(acptr) &&
		    (acptr != cptr) && (acptr != sptr) && *buf)
			if (!SupportUMODE2(acptr))
			{
				sendto_one(acptr, ":%s MODE %s :%s",
				    sptr->name, sptr->name, buf);
			}
			else
			{
				sendto_one(acptr, ":%s %s %s",
				    sptr->name,
				    (IsToken(acptr) ? TOK_UMODE2 : MSG_UMODE2),
				    buf);
			}
	if (cptr && MyClient(cptr))
		send_umode(cptr, sptr, old, ALL_UMODES, buf);

}

void send_umode_out_nickv2(cptr, sptr, old)
	aClient *cptr, *sptr;
	long old;
{
	int  i;
	aClient *acptr;

	send_umode(NULL, sptr, old, SEND_UMODES, buf);

	for (i = LastSlot; i >= 0; i--)
		if ((acptr = local[i]) && IsServer(acptr)
		    && !SupportNICKv2(acptr) && (acptr != cptr)
		    && (acptr != sptr) && *buf)
			sendto_one(acptr, ":%s MODE %s :%s", sptr->name,
			    sptr->name, buf);

	if (cptr && MyClient(cptr))
		send_umode(cptr, sptr, old, ALL_UMODES, buf);

}


/***********************************************************************
 * m_silence() - Added 19 May 1994 by Run.
 *
 ***********************************************************************/

/*
 * is_silenced : Does the actual check wether sptr is allowed
 *               to send a message to acptr.
 *               Both must be registered persons.
 * If sptr is silenced by acptr, his message should not be propagated,
 * but more over, if this is detected on a server not local to sptr
 * the SILENCE mask is sent upstream.
 */
static int is_silenced(sptr, acptr)
	aClient *sptr;
	aClient *acptr;
{
	Link *lp;
	anUser *user;
	static char sender[HOSTLEN + NICKLEN + USERLEN + 5];

	if (!(acptr->user) || !(lp = acptr->user->silence) ||
	    !(user = sptr->user)) return 0;
	ircsprintf(sender, "%s!%s@%s", sptr->name, user->username,
	    user->realhost);
	for (; lp; lp = lp->next)
	{
		if (!match(lp->value.cp, sender))
		{
			if (!MyConnect(sptr))
			{
				sendto_one(sptr->from, ":%s SILENCE %s :%s",
				    acptr->name, sptr->name, lp->value.cp);
				lp->flags = 1;
			}
			return 1;
		}
	}
	return 0;
}

int  del_silence(sptr, mask)
	aClient *sptr;
	char *mask;
{
	Link **lp;
	Link *tmp;

	for (lp = &(sptr->user->silence); *lp; lp = &((*lp)->next))
		if (mycmp(mask, (*lp)->value.cp) == 0)
		{
			tmp = *lp;
			*lp = tmp->next;
			MyFree(tmp->value.cp);
			free_link(tmp);
			return 0;
		}
	return -1;
}

static int add_silence(sptr, mask)
	aClient *sptr;
	char *mask;
{
	Link *lp;
	int  cnt = 0, len = 0;

	for (lp = sptr->user->silence; lp; lp = lp->next)
	{
		len += strlen(lp->value.cp);
		if (MyClient(sptr))
			if ((len > MAXSILELENGTH) || (++cnt >= MAXSILES))
			{
				sendto_one(sptr, err_str(ERR_SILELISTFULL),
				    me.name, sptr->name, mask);
				return -1;
			}
			else
			{
				if (!match(lp->value.cp, mask))
					return -1;
			}
		else if (!mycmp(lp->value.cp, mask))
			return -1;
	}
	lp = make_link();
	bzero((char *)lp, sizeof(Link));
	lp->next = sptr->user->silence;
	lp->value.cp = (char *)MyMalloc(strlen(mask) + 1);
	(void)strcpy(lp->value.cp, mask);
	sptr->user->silence = lp;
	return 0;
}

/*
** m_silence
**	parv[0] = sender prefix
** From local client:
**	parv[1] = mask (NULL sends the list)
** From remote client:
**	parv[1] = nick that must be silenced
**      parv[2] = mask
*/

int  m_silence(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	Link *lp;
	aClient *acptr;
	char c, *cp;


	if (MyClient(sptr))
	{
		acptr = sptr;
		if (parc < 2 || *parv[1] == '\0'
		    || (acptr = find_person(parv[1], NULL)))
		{
			if (!(acptr->user))
				return 0;
			for (lp = acptr->user->silence; lp; lp = lp->next)
				sendto_one(sptr, rpl_str(RPL_SILELIST), me.name,
				    sptr->name, acptr->name, lp->value.cp);
			sendto_one(sptr, rpl_str(RPL_ENDOFSILELIST), me.name,
			    acptr->name);
			return 0;
		}
		cp = parv[1];
		c = *cp;
		if (c == '-' || c == '+')
			cp++;
		else if (!(index(cp, '@') || index(cp, '.') ||
		    index(cp, '!') || index(cp, '*')))
		{
			sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name,
			    parv[0], parv[1]);
			return -1;
		}
		else
			c = '+';
		cp = pretty_mask(cp);
		if ((c == '-' && !del_silence(sptr, cp)) ||
		    (c != '-' && !add_silence(sptr, cp)))
		{
			sendto_prefix_one(sptr, sptr, ":%s SILENCE %c%s",
			    parv[0], c, cp);
			if (c == '-')
				sendto_serv_butone(NULL, ":%s SILENCE * -%s",
				    sptr->name, cp);
		}
	}
	else if (parc < 3 || *parv[2] == '\0')
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS), me.name, parv[0],
		    "SILENCE");
		return -1;
	}
	else if ((c = *parv[2]) == '-' || (acptr = find_person(parv[1], NULL)))
	{
		if (c == '-')
		{
			if (!del_silence(sptr, parv[2] + 1))
				sendto_serv_butone(cptr, ":%s SILENCE %s :%s",
				    parv[0], parv[1], parv[2]);
		}
		else
		{
			(void)add_silence(sptr, parv[2]);
			if (!MyClient(acptr))
				sendto_one(acptr, ":%s SILENCE %s :%s",
				    parv[0], parv[1], parv[2]);
		}
	}
	else
	{
		sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name, parv[0],
		    parv[1]);
		return -1;
	}
	return 0;
}

/* m_svsjoin() - Lamego - Wed Jul 21 20:04:48 1999
   Copied off PTlink IRCd (C) PTlink coders team.
	parv[0] - sender
	parv[1] - nick to make join
	parv[2] - channel(s) to join
*/
int  m_svsjoin(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	if (!IsULine(sptr))
		return 0;

	if (parc != 3 || !(acptr = find_person(parv[1], NULL)))
		return 0;

	if (MyClient(acptr))
	{
		parv[0] = parv[1];
		parv[1] = parv[2];
		(void)m_join(acptr, acptr, 2, parv);
	}
	else
		sendto_one(acptr, ":%s SVSJOIN %s %s", parv[0],
		    parv[1], parv[2]);

	return 0;
}

/* m_sajoin() - Lamego - Wed Jul 21 20:04:48 1999
   Copied off PTlink IRCd (C) PTlink coders team.
   Coded for Sadmin by Stskeeps
   also Modified by NiQuiL (niquil@programmer.net)
	parv[0] - sender
	parv[1] - nick to make join
	parv[2] - channel(s) to join
*/
int  m_sajoin(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	if (!IsSAdmin(sptr) && !IsULine(sptr))
	{
	 sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
	 return 0;
	}

	if (parc != 3)
	{
	 sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS), me.name, parv[0], "SAJOIN");
	 return 0;
	}

	if (!(acptr = find_person(parv[1], NULL)))
	{
		sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name, parv[0], parv[1]);
		return 0;
	}

	sendto_realops("%s used SAJOIN to make %s join %s", sptr->name, parv[1],
	    parv[2]);

	if (MyClient(acptr))
	{
		parv[0] = parv[1];
		parv[1] = parv[2];
		sendto_one(acptr,
		    ":%s NOTICE %s :*** You were forced to join %s", me.name,
		    acptr->name, parv[2]);
		(void)m_join(acptr, acptr, 2, parv);
	}
	else
		sendto_one(acptr, ":%s SAJOIN %s %s", parv[0],
		    parv[1], parv[2]);

	return 0;
}
/* m_svspart() - Lamego - Wed Jul 21 20:04:48 1999
   Copied off PTlink IRCd (C) PTlink coders team.
  Modified for PART by Stskeeps
	parv[0] - sender
	parv[1] - nick to make part
	parv[2] - channel(s) to part
*/
int  m_svspart(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	if (!IsULine(sptr))
		return 0;

	if (parc != 3 || !(acptr = find_person(parv[1], NULL))) return 0;

	if (MyClient(acptr))
	{
		parv[0] = parv[1];
		parv[1] = parv[2];
		(void)m_part(acptr, acptr, 2, parv);
	}
	else
		sendto_one(acptr, ":%s SVSPART %s %s", parv[0],
		    parv[1], parv[2]);

	return 0;
}

/* m_sapart() - Lamego - Wed Jul 21 20:04:48 1999
   Copied off PTlink IRCd (C) PTlink coders team.
   Coded for Sadmin by Stskeeps
   also Modified by NiQuiL (niquil@programmer.net)
	parv[0] - sender
	parv[1] - nick to make part
	parv[2] - channel(s) to part
*/
int  m_sapart(cptr, sptr, parc, parv)
	aClient *cptr, *sptr;
	int  parc;
	char *parv[];
{
	aClient *acptr;
	if (!IsSAdmin(sptr) && !IsULine(sptr))
	{
		sendto_one(sptr, err_str(ERR_NOPRIVILEGES), me.name, parv[0]);
		return 0;
	}

	if (parc != 3)
	{
		sendto_one(sptr, err_str(ERR_NEEDMOREPARAMS), me.name, parv[0], "SAPART");
		return 0;
	}

	if (!(acptr = find_person(parv[1], NULL)))
	{
		sendto_one(sptr, err_str(ERR_NOSUCHNICK), me.name, parv[0], parv[1]);
		return 0;
	}

	sendto_realops("%s used SAPART to make %s part %s", sptr->name, parv[1],
	    parv[2]);

	if (MyClient(acptr))
	{
		parv[0] = parv[1];
		parv[1] = parv[2];
		sendto_one(acptr,
		    ":%s NOTICE %s :*** You were forced to part %s", me.name,
		    acptr->name, parv[2]);
		(void)m_part(acptr, acptr, 2, parv);
	}
	else
		sendto_one(acptr, ":%s SAPART %s %s", parv[0],
		    parv[1], parv[2]);

	return 0;
}
/* These just waste space
int m_noshortn(cptr, sptr, parc, parv)
aClient	*cptr, *sptr;
int	parc;
char	*parv[];
{	sendto_one(sptr, "NOTICE %s :*** Please use /nickserv for that command",sptr->name);
}
int m_noshortc(cptr, sptr, parc, parv)
aClient	*cptr, *sptr;
int	parc;
char	*parv[];
{	sendto_one(sptr, "NOTICE %s :*** Please use /chanserv for that command",sptr->name);
}
int m_noshortm(cptr, sptr, parc, parv)
aClient	*cptr, *sptr;
int	parc;
char	*parv[];
{	sendto_one(sptr, "NOTICE %s :*** Please use /memoserv for that command",sptr->name);
}
int m_noshorto(cptr, sptr, parc, parv)
aClient	*cptr, *sptr;
int	parc;
char	*parv[];
{	sendto_one(sptr, "NOTICE %s :*** Please use /operserv for that command",sptr->name);
}
int m_noshorth(cptr, sptr, parc, parv)
aClient *cptr, *sptr;
int     parc;
char    *parv[];
{       sendto_one(sptr, "NOTICE %s :*** Please use /helpserv for that command",sptr->name);
}
*/
