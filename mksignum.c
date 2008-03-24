/* Yash: yet another shell */
/* mksignum.c: outputs string for 'signum.h' contents */
/* © 2007-2008 magicant */

/* This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.  */


#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE   600
#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <wchar.h>


typedef struct signal_T {
	int no;
	const char *name;
} signal_T;

const signal_T signals[] = {
	/* signals defined by POSIX.1-1990 */
	{ SIGHUP,  "HUP",  }, { SIGINT,  "INT",  }, { SIGQUIT, "QUIT", },
	{ SIGILL,  "ILL",  }, { SIGABRT, "ABRT", }, { SIGFPE,  "FPE",  },
	{ SIGKILL, "KILL", }, { SIGSEGV, "SEGV", }, { SIGPIPE, "PIPE", },
	{ SIGALRM, "ALRM", }, { SIGTERM, "TERM", }, { SIGUSR1, "USR1", },
	{ SIGUSR2, "USR2", }, { SIGCHLD, "CHLD", }, { SIGCONT, "CONT", },
	{ SIGSTOP, "STOP", }, { SIGTSTP, "TSTP", }, { SIGTTIN, "TTIN", },
	{ SIGTTOU, "TTOU", },
	/* signals defined by SUSv2 & POSIX.1-2001 (SUSv3) */
	{ SIGBUS,  "BUS",  }, { SIGPROF, "PROF", }, { SIGSYS,  "SYS",  },
	{ SIGTRAP, "TRAP", }, { SIGURG,  "URG",  }, { SIGXCPU, "XCPU", },
	{ SIGXFSZ, "XFSZ", },
#ifdef SIGPOLL
	{ SIGPOLL, "POLL", },
#endif
#ifdef SIGVTALRM
	{ SIGVTALRM, "VTALRM", },
#endif
	/* other signals */
#ifdef SIGIOT
	{ SIGIOT, "IOT", },
#endif
#ifdef SIGEMT
	{ SIGEMT, "EMT", },
#endif
#ifdef SIGSTKFLT
	{ SIGSTKFLT, "STKFLT", },
#endif
#ifdef SIGIO
	{ SIGIO, "IO", },
#endif
#ifdef SIGCLD
	{ SIGCLD, "CLD", },
#endif
#ifdef SIGPWR
	{ SIGPWR, "PWR", },
#endif
#ifdef SIGINFO
	{ SIGINFO, "INFO", },
#endif
#ifdef SIGLOST
	{ SIGLOST, "LOST", },
#endif
#ifdef SIGMSG
	{ SIGMSG, "MSG", },
#endif
#ifdef SIGWINCH
	{ SIGWINCH, "WINCH", },
#endif
#ifdef SIGDANGER
	{ SIGDANGER, "DANGER", },
#endif
#ifdef SIGMIGRATE
	{ SIGMIGRATE, "MIGRATE", },
#endif
#ifdef SIGPRE
	{ SIGPRE, "PRE", },
#endif
#ifdef SIGVIRT
	{ SIGVIRT, "VIRT", },
#endif
#ifdef SIGKAP
	{ SIGKAP, "KAP", },
#endif
#ifdef SIGGRANT
	{ SIGGRANT, "GRANT", },
#endif
#ifdef SIGRETRACT
	{ SIGRETRACT, "RETRACT", },
#endif
#ifdef SIGSOUND
	{ SIGSOUND, "SOUND", },
#endif
#ifdef SIGUNUSED
	{ SIGUNUSED, "UNUSED", },
#endif
	{ 0, NULL, },
};


int main(void)
{
	setlocale(LC_ALL, "");

	int min = 0;

	wprintf(L"/* signum.h: generated by mksignum */\n\n\n");
	wprintf(L"#include <stddef.h>\n\n");

	for (const signal_T *s = signals; s->no; s++)
		if (min < s->no)
			min = s->no;

	if (min < 100) {
		wprintf(L"/* injective function that returns an array index\n");
		wprintf(L" * corresponding to given signal number.\n");
		wprintf(L" * SIGNUM must not be a realtime signal number. */\n");
		wprintf(L"__attribute__((const))\n");
		wprintf(L"static inline size_t sigindex(int signum) {\n");
		wprintf(L"    return (size_t) signum;\n");
		wprintf(L"}\n\n");

		wprintf(L"/* max index returned by sigindex + 1 */\n");
		wprintf(L"#define MAXSIGIDX %d\n\n", min + 1);
	} else {
		sigset_t ss;
		size_t v;

		wprintf(L"/* injective function that returns an array index\n");
		wprintf(L" * corresponding to given signal number.\n");
		wprintf(L" * SIGNUM must not be a realtime signal number. */\n");
		wprintf(L"__attribute__((const))\n");
		wprintf(L"static inline size_t sigindex(int signum) {\n");
		wprintf(L"    switch (signum) {\n");
		wprintf(L"    case 0         : return 0;\n");

		sigemptyset(&ss);
		v = 1;
		for (const signal_T *s = signals; s->no; s++) {
			if (!sigismember(&ss, s->no)) {
				sigaddset(&ss, s->no);
				wprintf(L"    case SIG%-7s: return %zu;\n", s->name, v++);
			}
		}

		wprintf(L"    }\n");
		wprintf(L"}\n\n");

		wprintf(L"/* max index returned by sigindex + 1 */\n");
		wprintf(L"#define MAXSIGIDX %zu\n\n", v);
	}

	wprintf(L"/* number of realtime signals that can be handled by yash */\n");
	wprintf(L"#define RTSIZE %d\n\n",
#if defined SIGRTMIN && defined SIGRTMAX
			SIGRTMAX - SIGRTMIN + 10
#else
			0
#endif
		   );

	return 0;
}
