/*
  retest.c - TRE regression test program

  Copyright (C) 2001-2003 Ville Laurikari <vl@iki.fi>.

  This program is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License version 2 (June
  1991) as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

*/

/*
   This is just a simple test application containing various hand-written
   tests for regression testing TRE.  Some of these tests are TRE
   specific, but most are applicable to any POSIX regexp implementation.
*/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif /* HAVE_CONFIG_H */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <locale.h>
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif /* HAVE_MALLOC_H */
#include <regex.h>

#include "xmalloc.h"

#define elementsof(x)   (sizeof(x)/sizeof(x[0]))

static int valid_reobj = 0;
static regex_t reobj;
static regmatch_t pmatch[32];
static char *regex_pattern;
static int cflags;

static int comp_tests = 0;
static int exec_tests = 0;
static int comp_errors = 0;
static int exec_errors = 0;

#ifndef REG_OK
#define REG_OK 0
#endif /* REG_OK */

#define END -2

void
test_exec(char *str, int eflags, ...)
{
  unsigned int i;
  int m;
  int rm_so, rm_eo;
  int fail = 0;
  char *data;
  int len;
  va_list ap;
  va_start(ap, eflags);

  exec_tests++;

  if (!valid_reobj)
    {
      exec_errors++;
      return;
    }

#ifdef MALLOC_DEBUGGING
  xmalloc_configure(1);
#endif /* !MALLOC_DEBUGGING */
  len = strlen(str);
  if (len == 0)
    {
      data = NULL;
    }
  else
    {
      data = xmalloc(len);
      if (data == NULL)
	{
	  fprintf(stderr, "Out of memory.\n");
	  exit(1);
	}
      memcpy(data, str, len);
    }
#ifdef MALLOC_DEBUGGING
  xmalloc_configure(0);
#endif /* !MALLOC_DEBUGGING */
  /* XXX - test the approximate matcher (with cost 0) and the
     backtracking matcher as well using special eflags. */
  m = regnexec(&reobj, data, len, elementsof(pmatch), pmatch, eflags);
  xfree(data);
  if (m != va_arg(ap, int))
    {
      printf("Exec error, regex: \"%s\", cflags %d, "
	     "string: \"%s\", eflags %d\n", regex_pattern, cflags,
	     str, eflags);
      printf("  got %smatch\n", m ? "no " : "");
      fail = 1;
    }

  if (!fail && m == 0)
    {
      for (i = 0; i < elementsof(pmatch); i++)
	{
	  rm_so = va_arg(ap, int);
	  if (rm_so == END)
	    break;
	  rm_eo = va_arg(ap, int);
	  if (pmatch[i].rm_so != rm_so
	      || pmatch[i].rm_eo != rm_eo)
	    {
	      printf("Exec error, regex: \"%s\", string: \"%s\"\n",
		     regex_pattern, str);
	      printf("  group %d: expected (%d, %d), got (%d, %d)\n",
		     i, rm_so, rm_eo, pmatch[i].rm_so, pmatch[i].rm_eo);
	      fail = 1;
	    }
	}

      va_end(ap);
      if (!(cflags & REG_NOSUB) && reobj.re_nsub != i - 1
	  && reobj.re_nsub <= elementsof(pmatch))
	{
	  printf("Comp error, regex: \"%s\"\n", regex_pattern);
	  printf("  re_nsub is %d, should be %d\n", reobj.re_nsub, i - 1);
	  fail = 1;
	}


      for (; i < elementsof(pmatch); i++)
	if (pmatch[i].rm_so != -1 || pmatch[i].rm_eo != -1)
	  {
	    if (!fail)
	      printf("Exec error, regex: \"%s\", string: \"%s\"\n",
		     regex_pattern, str);
	    printf("  group %d: expected (-1, -1), got (%d, %d)\n",
		   i, pmatch[i].rm_so, pmatch[i].rm_eo);
	    fail = 1;
	  }
    }

  if (fail)
    exec_errors++;
}

void
test_comp(char *re, int flags, int ret)
{
  int errcode = 0;
  char *data;
  int len;
  regex_pattern = re;
  cflags = flags;

  comp_tests++;

  if (valid_reobj)
    {
      regfree(&reobj);
      valid_reobj = 0;
    }


#ifdef MALLOC_DEBUGGING
  xmalloc_configure(1);
#endif /* MALLOC_DEBUGGING */
  len = strlen(re);
  if (len == 0)
    {
      data = NULL;
    }
  else
    {
      data = xmalloc(len);
      if (data == NULL)
	{
	  fprintf(stderr, "Out of memory.\n");
	  exit(1);
	}
      memcpy(data, re, len);
    }

#ifdef MALLOC_DEBUGGING
  {
    static int j = 0, k = 0;
    int i = 0;
    while (1)
      {
	if (j++ % 20 == 0)
	  {
	    printf(".");
	    if (++k % 79 == 0)
	      printf("\n");
	    fflush(stdout);
	  }
	xmalloc_configure(i);
	comp_tests++;
	errcode = regncomp(&reobj, data, len, flags);
	if (errcode != REG_ESPACE)
	  {
	    printf("*");
	    if (++k % 79 == 0)
	      printf("\n");
	    break;
	  }
#ifdef REGEX_DEBUG
	xmalloc_dump_leaks();
#endif /* REGEX_DEBUG */
	i++;
      }
  }
#else /* !MALLOC_DEBUGGING */
  errcode = regncomp(&reobj, data, len, flags);
#endif /* !MALLOC_DEBUGGING */

  if (errcode != ret)
    {
      printf("Comp error, regex: \"%s\"\n", regex_pattern);
      printf("  expected return code %d, got %d.\n",
	     ret, errcode);
      comp_errors++;
    }

  xfree(data);

  if (errcode == 0)
    valid_reobj = 1;
}


int
main(int argc, char **argv)
{

  /*
   * Basic tests with pure regular expressions
   */

  /* Basic string matching. */
  test_comp("foobar", REG_EXTENDED, 0);
  test_exec("foobar", 0, REG_OK, 0, 6, END);
  test_exec("xxxfoobarzapzot", 0, REG_OK, 3, 9, END);
  test_comp("foobar", REG_EXTENDED | REG_NOSUB, 0);
  test_exec("foobar", 0, REG_OK, END);
  test_comp("aaaa", REG_EXTENDED, 0);
  test_exec("xxaaaaaaaaaaaaaaaaa", 0, REG_OK, 2, 6, END);

  /* Test zero length match. */
  test_comp("(a*)", REG_EXTENDED, 0);
  test_exec("", 0, REG_OK, 0, 0, 0, 0, END);

  test_comp("(a*)*", REG_EXTENDED, 0);
  test_exec("", 0, REG_OK, 0, 0, 0, 0, END);

  test_comp("((a*)*)*", REG_EXTENDED, 0);
  test_exec("", 0, REG_OK, 0, 0, 0, 0, 0, 0, END);
  test_comp("(a*bcd)*", REG_EXTENDED, 0);
  test_exec("aaaaaaaaaaaabcxbcxbcxaabcxaabcx", 0, REG_OK, 0, 0, -1, -1, END);
  test_exec("aaaaaaaaaaaabcxbcxbcxaabcxaabc", 0, REG_OK, 0, 0, -1, -1, END);
  test_exec("aaaaaaaaaaaabcxbcdbcxaabcxaabc", 0, REG_OK, 0, 0, -1, -1, END);
  test_exec("aaaaaaaaaaaabcdbcdbcxaabcxaabc", 0, REG_OK, 0, 18, 15, 18, END);

  test_comp("(a*)+", REG_EXTENDED, 0);
  test_exec("-", 0, REG_OK, 0, 0, 0, 0, END);

  test_comp("((a*)*b)*b", REG_EXTENDED, 0);
  test_exec("aaaaaaaaaaaaaaaaaaaaaaaaab", 0, REG_OK,
	    25, 26, -1, -1, -1, -1, END);

  test_comp("", 0, 0);
  test_exec("", 0, REG_OK, 0, 0, END);
  test_exec("foo", 0, REG_OK, 0, 0, END);

  /* Test for submatch addressing which requires arbitrary lookahead. */
  test_comp("(a*)aaaaaa", REG_EXTENDED, 0);
  test_exec("aaaaaaaaaaaaaaax", 0, REG_OK, 0, 15, 0, 9, END);

  /* Test leftmost and longest matching and some tricky submatches. */
  test_comp("(a*)(a*)", REG_EXTENDED, 0);
  test_exec("aaaa", 0, REG_OK, 0, 4, 0, 4, 4, 4, END);
  test_comp("(abcd|abc)(d?)", REG_EXTENDED, 0);
  test_exec("abcd", 0, REG_OK, 0, 4, 0, 4, 4, 4, END);
  test_comp("(abc|abcd)(d?)", REG_EXTENDED, 0);
  test_exec("abcd", 0, REG_OK, 0, 4, 0, 4, 4, 4, END);
  test_comp("(abc|abcd)(d?)e", REG_EXTENDED, 0);
  test_exec("abcde", 0, REG_OK, 0, 5, 0, 4, 4, 4, END);
  test_comp("(abcd|abc)(d?)e", REG_EXTENDED, 0);
  test_exec("abcde", 0, REG_OK, 0, 5, 0, 4, 4, 4, END);
  test_comp("a(bc|bcd)(d?)", REG_EXTENDED, 0);
  test_exec("abcd", 0, REG_OK, 0, 4, 1, 4, 4, 4, END);
  test_comp("a(bcd|bc)(d?)", REG_EXTENDED, 0);
  test_exec("abcd", 0, REG_OK, 0, 4, 1, 4, 4, 4, END);
  test_comp("a*(a?bc|bcd)(d?)", REG_EXTENDED, 0);
  test_exec("aaabcd", 0, REG_OK, 0, 6, 3, 6, 6, 6, END);
  test_comp("a*(bcd|a?bc)(d?)", REG_EXTENDED, 0);
  test_exec("aaabcd", 0, REG_OK, 0, 6, 3, 6, 6, 6, END);
  test_comp("(a|(a*b*))*", REG_EXTENDED, 0);
  test_exec("", 0, REG_OK, 0, 0, 0, 0, 0, 0, END);
  test_exec("a", 0, REG_OK, 0, 1, 0, 1, -1, -1, END);
  test_exec("aa", 0, REG_OK, 0, 2, 0, 2, 0, 2, END);
  test_exec("aaa", 0, REG_OK, 0, 3, 0, 3, 0, 3, END);
  test_exec("bbb", 0, REG_OK, 0, 3, 0, 3, 0, 3, END);
  test_exec("aaabbb", 0, REG_OK, 0, 6, 0, 6, 0, 6, END);
  test_exec("bbbaaa", 0, REG_OK, 0, 6, 3, 6, 3, 6, END);
  test_comp("((a*b*)|a)*", REG_EXTENDED, 0);
  test_exec("", 0, REG_OK, 0, 0, 0, 0, 0, 0, END);
  test_exec("a", 0, REG_OK, 0, 1, 0, 1, 0, 1, END);
  test_exec("aa", 0, REG_OK, 0, 2, 0, 2, 0, 2, END);
  test_exec("aaa", 0, REG_OK, 0, 3, 0, 3, 0, 3, END);
  test_exec("bbb", 0, REG_OK, 0, 3, 0, 3, 0, 3, END);
  test_exec("aaabbb", 0, REG_OK, 0, 6, 0, 6, 0, 6, END);
  test_exec("bbbaaa", 0, REG_OK, 0, 6, 3, 6, 3, 6, END);
  test_comp("a.*(.*b.*(.*c.*).*d.*).*e.*(.*f.*).*g", REG_EXTENDED, 0);
  test_exec("aabbccddeeffgg", 0, REG_OK, 0, 14, 3, 9, 5, 7, 11, 13, END);
  test_comp("(wee|week)(night|knights)s*", REG_EXTENDED, 0);
  test_exec("weeknights", 0, REG_OK, 0, 10, 0, 3, 3, 10, END);
  test_exec("weeknightss", 0, REG_OK, 0, 11, 0, 3, 3, 10, END);
  test_comp("a*", REG_EXTENDED, 0);
  test_exec("aaaaaaaaaa", 0, REG_OK, 0, 10, END);
  test_comp("aa*", REG_EXTENDED, 0);
  test_exec("aaaaaaaaaa", 0, REG_OK, 0, 10, END);
  test_comp("aaa*", REG_EXTENDED, 0);
  test_exec("aaaaaaaaaa", 0, REG_OK, 0, 10, END);
  test_comp("aaaa*", REG_EXTENDED, 0);
  test_exec("aaaaaaaaaa", 0, REG_OK, 0, 10, END);

  /* Test clearing old submatch data with nesting parentheses
     and iteration. */
  test_comp("((a)|(b))*c", REG_EXTENDED, 0);
  test_exec("aaabc", 0, REG_OK, 0, 5, 3, 4, -1, -1, 3, 4, END);
  test_exec("aaaac", 0, REG_OK, 0, 5, 3, 4, 3, 4, -1, -1, END);
  test_comp("foo((bar)*)*zot", REG_EXTENDED, 0);
  test_exec("foozot", 0, REG_OK, 0, 6, 3, 3, -1, -1, END);
  test_exec("foobarzot", 0, REG_OK, 0, 9, 3, 6, 3, 6, END);
  test_exec("foobarbarzot", 0, REG_OK, 0, 12, 3, 9, 6, 9, END);
  test_comp("foo((zup)*|(bar)*|(zap)*)*zot", REG_EXTENDED, 0);
  test_exec("foobarzapzot", 0, REG_OK,
	    0, 12, 6, 9, -1, -1, -1, -1, 6, 9, END);
  test_exec("foobarbarzapzot", 0, REG_OK,
	    0, 15, 9, 12, -1, -1, -1, -1, 9, 12, END);
  test_exec("foozupzot", 0, REG_OK,
	    0, 9, 3, 6, 3, 6, -1, -1, -1, -1, END);
  test_exec("foobarzot", 0, REG_OK,
	    0, 9, 3, 6, -1, -1, 3, 6, -1, -1, END);
  test_exec("foozapzot", 0, REG_OK,
	    0, 9, 3, 6, -1, -1, -1, -1, 3, 6, END);
  test_exec("foozot", 0, REG_OK,
	    0, 6, 3, 3, -1, -1, -1, -1, -1, -1, END);



  /* Test case where, e.g., Perl and Python regexp functions, and many
     other backtracking matchers, fail to produce the longest match.
     It is not exactly a bug since Perl does not claim to find the
     longest match, but a confusing feature and, in my opinion, a bad
     design choice because the union operator is traditionally defined
     to be commutative (with respect to the language denoted by the RE). */
  test_comp("(a|ab)(blip)?", REG_EXTENDED, 0);
  test_exec("ablip", 0, REG_OK, 0, 5, 0, 1, 1, 5, END);
  test_exec("ab", 0, REG_OK, 0, 2, 0, 2, -1, -1, END);
  test_comp("(ab|a)(blip)?", REG_EXTENDED, 0);
  test_exec("ablip", 0, REG_OK, 0, 5, 0, 1, 1, 5, END);
  test_exec("ab", 0, REG_OK, 0, 2, 0, 2, -1, -1, END);

  /* Test more submatch addressing. */
  test_comp("((a|b)*)a(a|b)*", REG_EXTENDED, 0);
  test_exec("aaaaabaaaba", 0, REG_OK, 0, 11, 0, 10, 9, 10, -1, -1, END);
  test_exec("aaaaabaaab", 0, REG_OK, 0, 10, 0, 8, 7, 8, 9, 10, END);
  test_exec("caa", 0, REG_OK, 1, 3, 1, 2, 1, 2, -1, -1, END);
  test_comp("((a|aba)*)(ababbaba)((a|b)*)", REG_EXTENDED, 0);
  test_exec("aabaababbabaaababbab", 0, REG_OK,
	    0, 20, 0, 4, 1, 4, 4, 12, 12, 20, 19, 20, END);
  test_exec("aaaaababbaba", 0, REG_OK,
	    0, 12, 0, 4, 3, 4, 4, 12, 12, 12, -1, -1, END);
  test_comp("((a|aba|abb|bba|bab)*)(ababbababbabbbabbbbbbabbaba)((a|b)*)",
	    REG_EXTENDED, 0);
  test_exec("aabaabbbbabababaababbababbabbbabbbbbbabbabababbababababbabababa",
	    0, REG_OK, 0, 63, 0, 16, 13, 16, 16, 43, 43, 63, 62, 63, END);

  /* Test for empty subexpressions. */
  test_comp("", 0, 0);
  test_exec("", 0, REG_OK, 0, 0, END);
  test_exec("foo", 0, REG_OK, 0, 0, END);
  test_comp("(a|)", REG_EXTENDED, 0);
  test_exec("a", 0, REG_OK, 0, 1, 0, 1, END);
  test_exec("b", 0, REG_OK, 0, 0, 0, 0, END);
  test_exec("", 0, REG_OK, 0, 0, 0, 0, END);
  test_comp("a|", REG_EXTENDED, 0);
  test_exec("a", 0, REG_OK, 0, 1, END);
  test_exec("b", 0, REG_OK, 0, 0, END);
  test_exec("", 0, REG_OK, 0, 0, END);
  test_comp("|a", REG_EXTENDED, 0);
  test_exec("a", 0, REG_OK, 0, 1, END);
  test_exec("b", 0, REG_OK, 0, 0, END);
  test_exec("", 0, REG_OK, 0, 0, END);

  /* Miscellaneous tests. */
  test_comp("(a*)b(c*)", REG_EXTENDED, 0);
  test_exec("abc", 0, REG_OK, 0, 3, 0, 1, 2, 3, END);
  test_exec("***abc***", 0, REG_OK, 3, 6, 3, 4, 5, 6, END);
  test_comp("(a)", REG_EXTENDED, 0);
  test_exec("a", 0, REG_OK, 0, 1, 0, 1, END);
  test_comp("((a))", REG_EXTENDED, 0);
  test_exec("a", 0, REG_OK, 0, 1, 0, 1, 0, 1, END);
  test_comp("(((a)))", REG_EXTENDED, 0);
  test_exec("a", 0, REG_OK, 0, 1, 0, 1, 0, 1, 0, 1, END);
  test_comp("((((((((((((((((((((a))))))))))))))))))))", REG_EXTENDED, 0);
  test_exec("a", 0, REG_OK, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
	    0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1,
	    0, 1, 0, 1, 0, 1, END);

  test_comp("ksntoeaiksntoeaikstneoaiksnteoaiksntoeaiskntoeaiskntoekainstoei"
	    "askntoeakisntoeksaitnokesantiksoentaikosentaiksoentaiksnoeaiskn"
	    "teoaksintoekasitnoeksaitkosetniaksoetnaisknoetakistoeksintokesa"
	    "nitksoentaisknoetaisknoetiaksotneaikstoekasitoeskatioksentaikso"
	    "enatiksoetnaiksonateiksoteaeskanotisknetaiskntoeasknitoskenatis"
	    "konetaisknoteai", 0, 0);

  test_comp("((aab)|(aac)|(aa*))c", REG_EXTENDED, 0);
  test_exec("aabc", 0, REG_OK, 0, 4, 0, 3,  0,  3, -1, -1, -1, -1, END);
  test_exec("aacc", 0, REG_OK, 0, 4, 0, 3, -1, -1,  0,  3, -1, -1, END);
  test_exec("aaac", 0, REG_OK, 0, 4, 0, 3, -1, -1, -1, -1,  0,  3, END);

  test_comp("^(([^!]+!)?([^!]+)|.+!([^!]+!)([^!]+))$",
	    REG_EXTENDED, 0);
  test_exec("foo!bar!bas", 0, REG_OK,
	    0, 11, 0, 11, -1, -1, -1, -1, 4, 8, 8, 11, END);
  test_comp("^([^!]+!)?([^!]+)$|^.+!([^!]+!)([^!]+)$",
	    REG_EXTENDED, 0);
  test_exec("foo!bar!bas", 0, REG_OK,
	    0, 11, -1, -1, -1, -1, 4, 8, 8, 11, END);
  test_comp("^(([^!]+!)?([^!]+)|.+!([^!]+!)([^!]+))$",
	    REG_EXTENDED, 0);
  test_exec("foo!bar!bas", 0, REG_OK,
	    0, 11, 0, 11, -1, -1, -1, -1, 4, 8, 8, 11, END);

  test_comp("M[ou]'?am+[ae]r .*([AEae]l[- ])?[GKQ]h?[aeu]+([dtz][dhz]?)+af[iy]",
	    REG_EXTENDED, 0);
  test_exec("Muammar Quathafi", 0, REG_OK, 0, 16, -1, -1, 11, 13, END);

  test_comp("(Ab|cD)*", REG_EXTENDED | REG_ICASE, 0);
  test_exec("aBcD", 0, REG_OK, 0, 4, 2, 4, END);


  /*
   * Many of the following tests were mostly inspired by (or copied from) the
   * libhackerlab posix test suite by Tom Lord.
   */

  test_comp("a", 0, 0);
  test_exec("a", 0, REG_OK, 0, 1, END);
  test_comp("\\.", 0, 0);
  test_exec(".", 0, REG_OK, 0, 1, END);
  test_comp("\\[", 0, 0);
  test_exec("[", 0, REG_OK, 0, 1, END);
  test_comp("\\\\", 0, 0);
  test_exec("\\", 0, REG_OK, 0, 1, END);
  test_comp("\\*", 0, 0);
  test_exec("*", 0, REG_OK, 0, 1, END);
  test_comp("\\^", 0, 0);
  test_exec("^", 0, REG_OK, 0, 1, END);
  test_comp("\\$", 0, 0);
  test_exec("$", 0, REG_OK, 0, 1, END);

  test_comp("\\", 0, REG_EESCAPE);

  test_comp("x\\.", 0, 0);
  test_exec("x.", 0, REG_OK, 0, 2, END);
  test_comp("x\\[", 0, 0);
  test_exec("x[", 0, REG_OK, 0, 2, END);
  test_comp("x\\\\", 0, 0);
  test_exec("x\\", 0, REG_OK, 0, 2, END);
  test_comp("x\\*", 0, 0);
  test_exec("x*", 0, REG_OK, 0, 2, END);
  test_comp("x\\^", 0, 0);
  test_exec("x^", 0, REG_OK, 0, 2, END);
  test_comp("x\\$", 0, 0);
  test_exec("x$", 0, REG_OK, 0, 2, END);

  test_comp("x\\", 0, REG_EESCAPE);

  test_comp(".", 0, 0);
  test_exec("a", 0, REG_OK, 0, 1, END);
  test_exec("\n", 0, REG_OK, 0, 1, END);



  /*
   * Test bracket expressions.
   */

  test_comp("[", 0, REG_EBRACK);
  test_comp("[]", 0, REG_EBRACK);
  test_comp("[^]", 0, REG_EBRACK);

  test_comp("[]x]", 0, 0);
  test_exec("]", 0, REG_OK, 0, 1, END);
  test_exec("x", 0, REG_OK, 0, 1, END);

  test_comp("[.]", 0, 0);
  test_exec(".", 0, REG_OK, 0, 1, END);
  test_exec("a", 0, REG_NOMATCH, END);

  test_comp("[*]", 0, 0);
  test_exec("*", 0, REG_OK, 0, 1, END);

  test_comp("[[]", 0, 0);
  test_exec("[", 0, REG_OK, 0, 1, END);

  test_comp("[\\]", 0, 0);
  test_exec("\\", 0, REG_OK, 0, 1, END);

  test_comp("[-x]", 0, 0);
  test_exec("-", 0, REG_OK, 0, 1, END);
  test_exec("x", 0, REG_OK, 0, 1, END);
  test_comp("[x-]", 0, 0);
  test_exec("-", 0, REG_OK, 0, 1, END);
  test_exec("x", 0, REG_OK, 0, 1, END);
  test_comp("[-]", 0, 0);
  test_exec("-", 0, REG_OK, 0, 1, END);

  test_comp("[abc]", 0, 0);
  test_exec("a", 0, REG_OK, 0, 1, END);
  test_exec("b", 0, REG_OK, 0, 1, END);
  test_exec("c", 0, REG_OK, 0, 1, END);
  test_exec("d", 0, REG_NOMATCH, END);
  test_exec("xa", 0, REG_OK, 1, 2, END);
  test_exec("xb", 0, REG_OK, 1, 2, END);
  test_exec("xc", 0, REG_OK, 1, 2, END);
  test_exec("xd", 0, REG_NOMATCH, END);
  test_comp("x[abc]", 0, 0);
  test_exec("xa", 0, REG_OK, 0, 2, END);
  test_exec("xb", 0, REG_OK, 0, 2, END);
  test_exec("xc", 0, REG_OK, 0, 2, END);
  test_exec("xd", 0, REG_NOMATCH, END);
  test_comp("[^abc]", 0, 0);
  test_exec("a", 0, REG_NOMATCH, END);
  test_exec("b", 0, REG_NOMATCH, END);
  test_exec("c", 0, REG_NOMATCH, END);
  test_exec("d", 0, REG_OK, 0, 1, END);
  test_exec("xa", 0, REG_OK, 0, 1, END);
  test_exec("xb", 0, REG_OK, 0, 1, END);
  test_exec("xc", 0, REG_OK, 0, 1, END);
  test_exec("xd", 0, REG_OK, 0, 1, END);
  test_comp("x[^abc]", 0, 0);
  test_exec("xa", 0, REG_NOMATCH, END);
  test_exec("xb", 0, REG_NOMATCH, END);
  test_exec("xc", 0, REG_NOMATCH, END);
  test_exec("xd", 0, REG_OK, 0, 2, END);

  test_comp("[()+?*\\]+", REG_EXTENDED, 0);
  test_exec("x\\*?+()x", 0, REG_OK, 1, 7, END);

  /* Standard character classes. */
  test_comp("[[:alnum:]]+", REG_EXTENDED, 0);
  test_exec("%abc123890XYZ=", 0, REG_OK, 1, 13, END);
  test_comp("[[:cntrl:]]+", REG_EXTENDED, 0);
  test_exec("%\n\t\015\f ", 0, REG_OK, 1, 5, END);
  test_comp("[[:lower:]]+", REG_EXTENDED, 0);
  test_exec("AbcdE", 0, REG_OK, 1, 4, END);
  test_comp("[[:lower:]]+", REG_EXTENDED | REG_ICASE, 0);
  test_exec("AbcdE", 0, REG_OK, 0, 5, END);
  test_comp("[[:space:]]+", REG_EXTENDED, 0);
  test_exec("x \t\f\nx", 0, REG_OK, 1, 5, END);
  test_comp("[[:alpha:]]+", REG_EXTENDED, 0);
  test_exec("%abC123890xyz=", 0, REG_OK, 1, 4, END);
  test_comp("[[:digit:]]+", REG_EXTENDED, 0);
  test_exec("%abC123890xyz=", 0, REG_OK, 4, 10, END);
  test_comp("[^[:digit:]]+", REG_EXTENDED, 0);
  test_exec("%abC123890xyz=", 0, REG_OK, 0, 4, END);
  test_comp("[[:print:]]+", REG_EXTENDED, 0);
  test_exec("\n %abC12\f", 0, REG_OK, 1, 8, END);
  test_comp("[[:upper:]]+", REG_EXTENDED, 0);
  test_exec("\n aBCDEFGHIJKLMNOPQRSTUVWXYz", 0, REG_OK, 3, 27, END);
  test_comp("[[:upper:]]+", REG_EXTENDED | REG_ICASE, 0);
  test_exec("\n aBCDEFGHIJKLMNOPQRSTUVWXYz", 0, REG_OK, 2, 28, END);
#ifdef HAVE_ISBLANK
  test_comp("[[:blank:]]+", REG_EXTENDED, 0);
  test_exec("\na \t b", 0, REG_OK, 2, 5, END);
#endif /* HAVE_ISBLANK */
  test_comp("[[:graph:]]+", REG_EXTENDED, 0);
  test_exec("\n %abC12\f", 0, REG_OK, 2, 8, END);
  test_comp("[[:punct:]]+", REG_EXTENDED, 0);
  test_exec("a~!@#$%^&*()_+=-`[]{};':\"|\\,./?>< ",
	    0, REG_OK, 1, 33, END);
  test_comp("[[:xdigit:]]+", REG_EXTENDED, 0);
  test_exec("-0123456789ABCDEFabcdef", 0, REG_OK, 1, 23, END);
  test_comp("[[:bogus-character-class-name:]", REG_EXTENDED, REG_ECTYPE);


  /* Range expressions (assuming that the C locale is being used). */
  test_comp("[a-z]+", REG_EXTENDED, 0);
  test_exec("ABCabcxyzABC", 0, REG_OK, 3, 9, END);
  test_comp("[z-a]+", REG_EXTENDED, REG_ERANGE);
  test_comp("[a-b-c]", 0, REG_ERANGE);
  test_comp("[a-a]+", REG_EXTENDED, 0);
  test_exec("zaaaaab", 0, REG_OK, 1, 6, END);
  test_comp("[--Z]+", REG_EXTENDED, 0);
  test_exec("!ABC-./XYZ~", 0, REG_OK, 1, 10, END);
  test_comp("[*--]", 0, 0);
  test_exec("-", 0, REG_OK, 0, 1, END);
  test_exec("*", 0, REG_OK, 0, 1, END);
  test_comp("[*--Z]+", REG_EXTENDED, 0);
  test_exec("!+*,---ABC", 0, REG_OK, 1, 7, END);
  test_comp("[a-]+", REG_EXTENDED, 0);
  test_exec("xa-a--a-ay", 0, REG_OK, 1, 9, END);

  /* REG_ICASE and character sets. */
  test_comp("[a-c]*", REG_ICASE | REG_EXTENDED, 0);
  test_exec("cABbage", 0, REG_OK, 0, 5, END);
  test_comp("[^a-c]*", REG_ICASE | REG_EXTENDED, 0);
  test_exec("tObAcCo", 0, REG_OK, 0, 2, END);
  test_comp("[A-C]*", REG_ICASE | REG_EXTENDED, 0);
  test_exec("cABbage", 0, REG_OK, 0, 5, END);
  test_comp("[^A-C]*", REG_ICASE | REG_EXTENDED, 0);
  test_exec("tObAcCo", 0, REG_OK, 0, 2, END);

  /* Complex character sets. */
  test_comp("[[:digit:]a-z#$%]+", REG_EXTENDED, 0);
  test_exec("__abc#lmn012$x%yz789*", 0, REG_OK, 2, 20, END);
  test_comp("[[:digit:]a-z#$%]+", REG_ICASE | REG_EXTENDED, 0);
  test_exec("__abcLMN012x%#$yz789*", 0, REG_OK, 2, 20, END);
  test_comp("[^[:digit:]a-z#$%]+", REG_EXTENDED, 0);
  test_exec("abc#lmn012$x%yz789--@*,abc", 0, REG_OK, 18, 23, END);
  test_comp("[^[:digit:]a-z#$%]+", REG_ICASE | REG_EXTENDED, 0);
  test_exec("abc#lmn012$x%yz789--@*,abc", 0, REG_OK, 18, 23, END);
  test_comp("[^[:digit:]#$%[:xdigit:]]+", REG_ICASE | REG_EXTENDED, 0);
  test_exec("abc#lmn012$x%yz789--@*,abc", 0, REG_OK, 4, 7, END);
  test_comp("[^-]+", REG_EXTENDED, 0);
  test_exec("---afd*(&,ml---", 0, REG_OK, 3, 12, END);
  test_comp("[^--Z]+", REG_EXTENDED, 0);
  test_exec("---AFD*(&,ml---", 0, REG_OK, 6, 12, END);
  test_comp("[^--Z]+", REG_ICASE | REG_EXTENDED, 0);
  test_exec("---AFD*(&,ml---", 0, REG_OK, 6, 10, END);

  /* Unsupported things (equivalence classes and multicharacter collating
     elements) */
  test_comp("[[.foo.]]", 0, REG_ECOLLATE);
  test_comp("[[=foo=]]", 0, REG_ECOLLATE);
  test_comp("[[..]]", 0, REG_ECOLLATE);
  test_comp("[[==]]", 0, REG_ECOLLATE);
  test_comp("[[.]]", 0, REG_ECOLLATE);
  test_comp("[[=]]", 0, REG_ECOLLATE);
  test_comp("[[.]", 0, REG_ECOLLATE);
  test_comp("[[=]", 0, REG_ECOLLATE);
  test_comp("[[.", 0, REG_ECOLLATE);
  test_comp("[[=", 0, REG_ECOLLATE);


  /* Miscellaneous tests. */
  test_comp("abc\\(\\(de\\)\\(fg\\)\\)hi", 0, 0);
  test_exec("xabcdefghiy", 0, REG_OK, 1, 10, 4, 8, 4, 6, 6, 8, END);

  test_comp("abc*def", 0, 0);
  test_exec("xabdefy", 0, REG_OK, 1, 6, END);
  test_exec("xabcdefy", 0, REG_OK, 1, 7, END);
  test_exec("xabcccccccdefy", 0, REG_OK, 1, 13, END);

  test_comp("abc\\(def\\)*ghi", 0, 0);
  test_exec("xabcghiy", 0, REG_OK, 1, 7, -1, -1, END);
  test_exec("xabcdefghi", 0, REG_OK, 1, 10, 4, 7, END);
  test_exec("xabcdefdefdefghi", 0, REG_OK, 1, 16, 10, 13, END);

  test_comp("a?", REG_EXTENDED, REG_OK);
  test_exec("aaaaa", 0, REG_OK, 0, 1, END);
  test_exec("xaaaaa", 0, REG_OK, 0, 0, END);
  test_comp("a+", REG_EXTENDED, REG_OK);
  test_exec("aaaaa", 0, REG_OK, 0, 5, END);
  test_exec("xaaaaa", 0, REG_OK, 1, 6, END);


  /*
   * Test anchors and their behaviour with the REG_NEWLINE compilation
   * flag and the REG_NOTBOL, REG_NOTEOL execution flags.
   */

  /* Normally, `^' matches the empty string at beginning of input.
     If REG_NOTBOL is used, `^' won't match the zero length string. */
  test_comp("^abc", 0, 0);
  test_exec("abcdef", 0, REG_OK, 0, 3, END);
  test_exec("abcdef", REG_NOTBOL, REG_NOMATCH, END);
  test_exec("xyzabcdef", 0, REG_NOMATCH, END);
  test_exec("xyzabcdef", REG_NOTBOL, REG_NOMATCH, END);
  test_exec("\nabcdef", 0, REG_NOMATCH, END);
  test_exec("\nabcdef", REG_NOTBOL, REG_NOMATCH, END);

  /* Normally, `$' matches the empty string at end of input.
     If REG_NOTEOL is used, `$' won't match the zero length string. */
  test_comp("abc$", 0, 0);
  test_exec("defabc", 0, REG_OK, 3, 6, END);
  test_exec("defabc", REG_NOTEOL, REG_NOMATCH, END);
  test_exec("defabcxyz", 0, REG_NOMATCH, END);
  test_exec("defabcxyz", REG_NOTEOL, REG_NOMATCH, END);
  test_exec("defabc\n", 0, REG_NOMATCH, END);
  test_exec("defabc\n", REG_NOTEOL, REG_NOMATCH, END);

  test_comp("^abc$", 0, 0);
  test_exec("abc", 0, REG_OK, 0, 3, END);
  test_exec("abc", REG_NOTBOL, REG_NOMATCH, END);
  test_exec("abc", REG_NOTEOL, REG_NOMATCH, END);
  test_exec("abc", REG_NOTBOL | REG_NOTEOL, REG_NOMATCH, END);
  test_exec("\nabc\n", 0, REG_NOMATCH, END);
  test_exec("defabc\n", 0, REG_NOMATCH, END);
  test_exec("\nabcdef", 0, REG_NOMATCH, END);
  test_exec("abcdef", 0, REG_NOMATCH, END);
  test_exec("defabc", 0, REG_NOMATCH, END);
  test_exec("abc\ndef", 0, REG_NOMATCH, END);
  test_exec("def\nabc", 0, REG_NOMATCH, END);

  /* If REG_NEWLINE is used, `^' matches the empty string immediately after
     a newline, regardless of whether execution flags contain REG_NOTBOL.
     Similarly, if REG_NEWLINE is used, `$' matches the empty string
     immediately before a newline, regardless of execution flags. */
  test_comp("^abc", REG_NEWLINE, 0);
  test_exec("abcdef", 0, REG_OK, 0, 3, END);
  test_exec("abcdef", REG_NOTBOL, REG_NOMATCH, END);
  test_exec("xyzabcdef", 0, REG_NOMATCH, END);
  test_exec("xyzabcdef", REG_NOTBOL, REG_NOMATCH, END);
  test_exec("\nabcdef", 0, REG_OK, 1, 4, END);
  test_exec("\nabcdef", REG_NOTBOL, 0, 1, 4, END);
  test_comp("abc$", REG_NEWLINE, 0);
  test_exec("defabc", 0, REG_OK, 3, 6, END);
  test_exec("defabc", REG_NOTEOL, REG_NOMATCH, END);
  test_exec("defabcxyz", 0, REG_NOMATCH, END);
  test_exec("defabcxyz", REG_NOTEOL, REG_NOMATCH, END);
  test_exec("defabc\n", 0, REG_OK, 3, 6, END);
  test_exec("defabc\n", REG_NOTEOL, 0, 3, 6, END);
  test_comp("^abc$", REG_NEWLINE, 0);
  test_exec("abc", 0, REG_OK, 0, 3, END);
  test_exec("abc", REG_NOTBOL, REG_NOMATCH, END);
  test_exec("abc", REG_NOTEOL, REG_NOMATCH, END);
  test_exec("abc", REG_NOTBOL | REG_NOTEOL, REG_NOMATCH, END);
  test_exec("\nabc\n", 0, REG_OK, 1, 4, END);
  test_exec("defabc\n", 0, REG_NOMATCH, END);
  test_exec("\nabcdef", 0, REG_NOMATCH, END);
  test_exec("abcdef", 0, REG_NOMATCH, END);
  test_exec("abcdef", REG_NOTBOL, REG_NOMATCH, END);
  test_exec("defabc", 0, REG_NOMATCH, END);
  test_exec("defabc", REG_NOTEOL, REG_NOMATCH, END);
  test_exec("abc\ndef", 0, REG_OK, 0, 3, END);
  test_exec("abc\ndef", REG_NOTBOL, REG_NOMATCH, END);
  test_exec("abc\ndef", REG_NOTEOL, 0, 0, 3, END);
  test_exec("abc\ndef", REG_NOTBOL | REG_NOTEOL, REG_NOMATCH, END);
  test_exec("def\nabc", 0, REG_OK, 4, 7, END);
  test_exec("def\nabc", REG_NOTBOL, 0, 4, 7, END);
  test_exec("def\nabc", REG_NOTEOL, REG_NOMATCH, END);
  test_exec("def\nabc", REG_NOTBOL | REG_NOTEOL, REG_NOMATCH, END);

  /* With BRE syntax, `^' has a special meaning only at the beginning of the
     RE or the beginning of a parenthesized subexpression. */
  test_comp("a\\{0,1\\}^bc", 0, 0);
  test_exec("bc", 0, REG_NOMATCH, END);
  test_exec("^bc", 0, REG_OK, 0, 3, END);
  test_exec("abc", 0, REG_NOMATCH, END);
  test_exec("a^bc", 0, REG_OK, 0, 4, END);
  test_comp("a\\{0,1\\}\\(^bc\\)", 0, 0);
  test_exec("bc", 0, REG_OK, 0, 2, 0, 2, END);
  test_exec("^bc", 0, REG_NOMATCH, END);
  test_exec("abc", 0, REG_NOMATCH, END);
  test_exec("a^bc", 0, REG_NOMATCH, END);
  test_comp("(^a", 0, 0);
  test_exec("(^a", 0, REG_OK, 0, 3, END);

  /* With BRE syntax, `$' has a special meaning only at the end of the
     RE or the end of a parenthesized subexpression. */
  test_comp("ab$c\\{0,1\\}", 0, 0);
  test_exec("ab", 0, REG_NOMATCH, END);
  test_exec("ab$", 0, REG_OK, 0, 3, END);
  test_exec("abc", 0, REG_NOMATCH, END);
  test_exec("ab$c", 0, REG_OK, 0, 4, END);
  test_comp("\\(ab$\\)c\\{0,1\\}", 0, 0);
  test_exec("ab", 0, REG_OK, 0, 2, 0, 2, END);
  test_exec("ab$", 0, REG_NOMATCH, END);
  test_exec("abc", 0, REG_NOMATCH, END);
  test_exec("ab$c", 0, REG_NOMATCH, END);
  test_comp("a$)", 0, 0);
  test_exec("a$)", 0, REG_OK, 0, 3, END);

  /* Miscellaneous tests for `^' and `$'. */
  test_comp("foo^$", REG_EXTENDED, 0);
  test_exec("foo", 0, REG_NOMATCH, END);
  test_comp("x$\n^y", REG_EXTENDED | REG_NEWLINE, 0);
  test_exec("foo\nybarx\nyes\n", 0, REG_OK, 8, 11, END);
  test_comp("^$", 0, 0);
  test_exec("x", 0, REG_NOMATCH, END);
  test_exec("", 0, REG_OK, 0, 0, END);
  test_exec("\n", 0, REG_NOMATCH, 0, 0, END);
  test_comp("^$", REG_NEWLINE, 0);
  test_exec("x", 0, REG_NOMATCH, END);
  test_exec("", 0, REG_OK, 0, 0, END);
  test_exec("\n", 0, REG_OK, 0, 0, END);

  /* REG_NEWLINE causes `.' not to match newlines. */
  test_comp(".*", 0, 0);
  test_exec("ab\ncd", 0, REG_OK, 0, 5, END);
  test_comp(".*", REG_NEWLINE, 0);
  test_exec("ab\ncd", 0, REG_OK, 0, 2, END);

  /*
   * Tests for nonstandard syntax extensions.
   */

  /* Zero width assertions. */
  test_comp("\\<x", REG_EXTENDED, 0);
  test_exec("aax xaa", 0, REG_OK, 4, 5, END);
  test_exec("xaa", 0, REG_OK, 0, 1, END);
  test_comp("x\\>", REG_EXTENDED, 0);
  test_exec("axx xaa", 0, REG_OK, 2, 3, END);
  test_exec("aax", 0, REG_OK, 2, 3, END);
  test_comp("\\bx", REG_EXTENDED, 0);
  test_exec("axx xaa", 0, REG_OK, 4, 5, END);
  test_exec("aax", 0, REG_NOMATCH, END);
  test_exec("xax", 0, REG_OK, 0, 1, END);
  test_comp("x\\b", REG_EXTENDED, 0);
  test_exec("axx xaa", 0, REG_OK, 2, 3, END);
  test_exec("aax", 0, REG_OK, 2, 3, END);
  test_exec("xaa", 0, REG_NOMATCH);
  test_comp("\\Bx", REG_EXTENDED, 0);
  test_exec("aax xxa", 0, REG_OK, 2, 3, END);
  test_comp("\\Bx\\b", REG_EXTENDED, 0);
  test_exec("aax xxx", 0, REG_OK, 2, 3, END);

  /* Shorthands for character classes. */
  test_comp("\\w+", REG_EXTENDED, 0);
  test_exec(",.(a23_Nt-�o)", 0, REG_OK, 3, 9, END);
  test_comp("\\D", REG_EXTENDED, 0);

  /* Quoted special characters. */
  test_comp("\\t", REG_EXTENDED, 0);
  test_comp("\\e", REG_EXTENDED, 0);

  /*
   * Test bounded repetitions.
   */

  test_comp("a{0,0}", REG_EXTENDED, REG_OK);
  test_exec("aaa", 0, REG_OK, 0, 0, END);
  test_comp("a{0,1}", REG_EXTENDED, REG_OK);
  test_exec("aaa", 0, REG_OK, 0, 1, END);
  test_comp("a{1,1}", REG_EXTENDED, REG_OK);
  test_exec("aaa", 0, REG_OK, 0, 1, END);
  test_comp("a{1,3}", REG_EXTENDED, REG_OK);
  test_exec("xaaaaa", 0, REG_OK, 1, 4, END);
  test_comp("a{0,3}", REG_EXTENDED, REG_OK);
  test_exec("aaaaa", 0, REG_OK, 0, 3, END);
  test_comp("a{0,}", REG_EXTENDED, REG_OK);
  test_exec("", 0, REG_OK, 0, 0, END);
  test_exec("a", 0, REG_OK, 0, 1, END);
  test_exec("aa", 0, REG_OK, 0, 2, END);
  test_exec("aaa", 0, REG_OK, 0, 3, END);
  test_comp("a{1,}", REG_EXTENDED, REG_OK);
  test_exec("", 0, REG_NOMATCH, END);
  test_exec("a", 0, REG_OK, 0, 1, END);
  test_exec("aa", 0, REG_OK, 0, 2, END);
  test_exec("aaa", 0, REG_OK, 0, 3, END);
  test_comp("a{2,}", REG_EXTENDED, REG_OK);
  test_exec("", 0, REG_NOMATCH, END);
  test_exec("a", 0, REG_NOMATCH, 0, 1, END);
  test_exec("aa", 0, REG_OK, 0, 2, END);
  test_exec("aaa", 0, REG_OK, 0, 3, END);
  test_comp("a{3,}", REG_EXTENDED, REG_OK);
  test_exec("", 0, REG_NOMATCH, END);
  test_exec("a", 0, REG_NOMATCH, END);
  test_exec("aa", 0, REG_NOMATCH, END);
  test_exec("aaa", 0, REG_OK, 0, 3, END);
  test_exec("aaaa", 0, REG_OK, 0, 4, END);
  test_exec("aaaaa", 0, REG_OK, 0, 5, END);
  test_exec("aaaaaa", 0, REG_OK, 0, 6, END);
  test_exec("aaaaaaa", 0, REG_OK, 0, 7, END);

  test_comp("a{5,10}", REG_EXTENDED, REG_OK);
  test_comp("a{6,6}", REG_EXTENDED, REG_OK);
  test_exec("aaaaaaaaaaaa", 0, REG_OK, 0, 6, END);
  test_exec("xxaaaaaaaaaaaa", 0, REG_OK, 2, 8, END);
  test_exec("xxaaaaa", 0, REG_NOMATCH, END);
  test_comp("a{5,6}", REG_EXTENDED, REG_OK);
  test_exec("aaaaaaaaaaaa", 0, REG_OK, 0, 6, END);
  test_exec("xxaaaaaaaaaaaa", 0, REG_OK, 2, 8, END);
  test_exec("xxaaaaa", 0, REG_OK, 2, 7, END);
  test_exec("xxaaaa", 0, REG_NOMATCH, END);

  /* Trickier ones... */
  test_comp("([ab]{5,10})*b", REG_EXTENDED, REG_OK);
  test_exec("bbbbbabaaaaab", 0, REG_OK, 0, 13, 5, 12, END);
  test_exec("bbbbbbaaaaab", 0, REG_OK, 0, 12, 5, 11, END);
  test_exec("bbbbbbaaaab", 0, REG_OK, 0, 11, 0, 10, END);
  test_exec("bbbbbbaaab", 0, REG_OK, 0, 10, 0, 9, END);
  test_exec("bbbbbbaab", 0, REG_OK, 0, 9, 0, 8, END);
  test_exec("bbbbbbab", 0, REG_OK, 0, 8, 0, 7, END);

  test_comp("([ab]*)(ab[ab]{5,10})ba", REG_EXTENDED, REG_OK);
  test_exec("abbabbbabaabbbbbbbbbbbbbabaaaabab", 0, REG_OK,
	    0, 10, 0, 0, 0, 8, END);
  test_exec("abbabbbabaabbbbbbbbbbbbabaaaaabab", 0, REG_OK,
	    0, 32, 0, 23, 23, 30, END);
  test_exec("abbabbbabaabbbbbbbbbbbbabaaaabab", 0, REG_OK,
	    0, 24, 0, 10, 10, 22, END);
  test_exec("abbabbbabaabbbbbbbbbbbba", 0, REG_OK,
	    0, 24, 0, 10, 10, 22, END);

  /* Test repeating something that has submatches inside. */
  test_comp("(a){0,5}", REG_EXTENDED, 0);
  test_exec("", 0, REG_OK, 0, 0, -1, -1, END);
  test_exec("a", 0, REG_OK, 0, 1, 0, 1, END);
  test_exec("aa", 0, REG_OK, 0, 2, 1, 2, END);
  test_exec("aaa", 0, REG_OK, 0, 3, 2, 3, END);
  test_exec("aaaa", 0, REG_OK, 0, 4, 3, 4, END);
  test_exec("aaaaa", 0, REG_OK, 0, 5, 4, 5, END);
  test_exec("aaaaaa", 0, REG_OK, 0, 5, 4, 5, END);

  test_comp("(a){2,3}", REG_EXTENDED, 0);
  test_exec("", 0, REG_NOMATCH, END);
  test_exec("a", 0, REG_NOMATCH, END);
  test_exec("aa", 0, REG_OK, 0, 2, 1, 2, END);
  test_exec("aaa", 0, REG_OK, 0, 3, 2, 3, END);
  test_exec("aaaa", 0, REG_OK, 0, 3, 2, 3, END);

  test_comp("\\(a\\)\\{4\\}", 0, 0);
  test_exec("aaaa", 0, REG_OK, 0, 4, 3, 4, END);

  test_comp("\\(a*\\)\\{2\\}", 0, 0);
  test_exec("a", 0, REG_OK, 0, 1, 1, 1, END);

  test_comp("((..)|(.)){2}", REG_EXTENDED, 0);
  test_exec("aa", 0, REG_OK, 0, 2, 1, 2, -1, -1, 1, 2, END);


  /*
   * Back referencing tests.
   */
  test_comp("([a-z]*) \\1", REG_EXTENDED, 0);
  test_exec("foobar foobar", 0, REG_OK, 0, 13, 0, 6, END);

  /* Searching for a leftmost longest square (repeated string) */
  test_comp("(.*)\\1", REG_EXTENDED, 0);
  test_exec("foobarfoobar", 0, REG_OK, 0, 12, 0, 6, END);

  test_comp("a(b)*c\\1", REG_EXTENDED, 0);
  test_exec("acb", 0, REG_OK, 0, 2, -1, -1, END);
  test_exec("abbcbbb", 0, REG_OK, 0, 5, 2, 3, END);
  test_exec("abbdbd", 0, REG_NOMATCH, END);

  test_comp("([a-c]*)\\1", REG_EXTENDED, 0);
  test_exec("abcacdef", 0, REG_OK, 0, 0, 0, 0, END);
  test_exec("abcabcabcd", 0, REG_OK, 0, 6, 0, 3, END);

  test_comp("\\(a*\\)*\\(x\\)\\(\\1\\)", 0, 0);
  test_exec("x", 0, REG_OK, 0, 1, 0, 0, 0, 1, 1, 1, END);
#if 0
  /* This test fails currently. */
  test_exec("ax", 0, REG_OK, 0, 2, 1, 1, 1, 2, 2, 2, END);
#endif

  test_comp("(a)\\1{1,2}", REG_EXTENDED, 0);
  test_exec("aabc", 0, REG_OK, 0, 2, 0, 1, END);


  /*
   * Test minimal repetitions (non-greedy repetitions)
   */

  /* Basic .*/
  test_comp(".*?", REG_EXTENDED, 0);
  test_exec("abcd", 0, REG_OK, 0, 0, END);
  test_comp(".+?", REG_EXTENDED, 0);
  test_exec("abcd", 0, REG_OK, 0, 1, END);
  test_comp(".??", REG_EXTENDED, 0);
  test_exec("abcd", 0, REG_OK, 0, 0, END);
  test_comp(".{2,5}?", REG_EXTENDED, 0);
  test_exec("abcd", 0, REG_OK, 0, 2, END);

  /* More complicated. */
  test_comp("<b>(.*?)</b>", REG_EXTENDED, 0);
  test_exec("<b>text1</b><b>text2</b>", 0, REG_OK, 0, 12, 3, 8, END);
  test_comp("a(.*?)(foo|bar|zap)", REG_EXTENDED, 0);
  test_exec("hubba wooga-booga zabar gafoo wazap", 0, REG_OK,
	    4, 23, 5, 20, 20, 23, END);


  /*
   * Error reporting tests.
   */

  test_comp("\\", REG_EXTENDED, REG_EESCAPE);
  test_comp("(", REG_EXTENDED, REG_EPAREN);
  test_comp(")", REG_EXTENDED, REG_OK);
  test_exec(")", 0, REG_OK, 0, 1, END);
  test_comp("a{1", REG_EXTENDED, REG_EBRACE);
  test_comp("a{1,x}", REG_EXTENDED, REG_BADBR);
  test_comp("a{1x}", REG_EXTENDED, REG_BADBR);
  test_comp("a{1,0}", REG_EXTENDED, REG_BADBR);
  test_comp("a{x}", REG_EXTENDED, REG_BADBR);
  test_comp("a{}", REG_EXTENDED, REG_BADBR);

  test_comp("\\", 0, REG_EESCAPE);
  test_comp("\\(", 0, REG_EPAREN);
  test_comp("\\)", 0, REG_EPAREN);
  test_comp("a\\{1", 0, REG_EBRACE);
  test_comp("a\\{1,x\\}", 0, REG_BADBR);
  test_comp("a\\{1x\\}", 0, REG_BADBR);
  test_comp("a\\{1,0\\}", 0, REG_BADBR);
  test_comp("a\\{x\\}", 0, REG_BADBR);
  test_comp("a\\{\\}", 0, REG_BADBR);




  /*
   * Internationalization tests.
   */

  /* This same test with the correct locale is below. */
  test_comp("��+", REG_EXTENDED, 0);
  test_exec("���ξޤϡ�����������������", 0, REG_OK, 10, 13, END);

#ifndef WIN32
  if (setlocale(LC_CTYPE, "fi_FI.ISO-8859-1") != NULL)
    {
      printf("\nTesting LC_CTYPE fi_FI.ISO-8859-1\n");
      test_comp("aBCdeFghiJKlmnoPQRstuvWXyZ���", REG_ICASE, 0);
      test_exec("abCDefGhiJKlmNoPqRStuVwXyz���", 0, REG_OK, 0, 29, END);
    }

#ifdef TRE_MULTIBYTE
  if (setlocale(LC_CTYPE, "ja_JP.eucjp") != NULL)
    {
      printf("\nTesting LC_CTYPE ja_JP.eucjp\n");
      /* I tried to make a test where implementations not aware of multibyte
         character sets will fail.  I have no idea what the japanese text here
         means, I took it from http://www.ipsec.co.jp/. */
      test_comp("��+", REG_EXTENDED, 0);
      test_exec("���ξޤϡ�����������������", 0, REG_OK, 10, 12, END);
    }
#endif /* TRE_MULTIBYTE */
#endif /* WIN32 */

  regfree(&reobj);

  printf("\n");
  if (comp_errors || exec_errors)
    printf("%d out of %d tests FAILED!\n",
	   comp_errors + exec_errors, comp_tests + exec_tests);
  else
    printf("All %d tests passed.\n", comp_tests + exec_tests);




#ifdef MALLOC_DEBUGGING
  xmalloc_dump_leaks();
#endif /* MALLOC_DEBUGGING */

  return comp_errors || exec_errors;
}


/* EOF */