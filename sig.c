/* Yash: yet another shell */
/* sig.c: signal handling */
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


#include "common.h"
#include <assert.h>
#include <errno.h>
#include <stdbool.h>
#include <signal.h>
#include "option.h"
#include "util.h"
#include "sig.h"
#include "signum.h"


/* このシェルのシグナルの扱いについて:
 * このシェルは、どんなときでも SIGCHLD, SIGHUP, SIGQUIT を捕捉する。
 * ジョブ制御が有効ならば、SIGTTOU と SIGTSTP も捕捉する。
 * 対話的シェルでは、SIGINT と SIGTERM も捕捉する。
 * それ以外のシグナルは、トラップに従って捕捉する。
 * シグナルのブロックは、wait_for_sigchld で SIGCHLD/SIGHUP をブロックする
 * 以外は行わない。 */


static void sig_handler(int signum);

/* シグナルの情報を管理する構造体 */
typedef struct signal_T {
    int no;
    const char *name;
} signal_T;

/* シグナル情報の配列 */
static const signal_T signals[] = {
    /* POSIX.1-1990 で定義されたシグナル */
    { SIGHUP,  "HUP",  }, { SIGINT,  "INT",  }, { SIGQUIT, "QUIT", },
    { SIGILL,  "ILL",  }, { SIGABRT, "ABRT", }, { SIGFPE,  "FPE",  },
    { SIGKILL, "KILL", }, { SIGSEGV, "SEGV", }, { SIGPIPE, "PIPE", },
    { SIGALRM, "ALRM", }, { SIGTERM, "TERM", }, { SIGUSR1, "USR1", },
    { SIGUSR2, "USR2", }, { SIGCHLD, "CHLD", }, { SIGCONT, "CONT", },
    { SIGSTOP, "STOP", }, { SIGTSTP, "TSTP", }, { SIGTTIN, "TTIN", },
    { SIGTTOU, "TTOU", },
    /* SUSv2 & POSIX.1-2001 (SUSv3) で定義されたシグナル */
    { SIGBUS,  "BUS",  }, { SIGPROF, "PROF", }, { SIGSYS,  "SYS",  },
    { SIGTRAP, "TRAP", }, { SIGURG,  "URG",  }, { SIGXCPU, "XCPU", },
    { SIGXFSZ, "XFSZ", },
#ifdef SIGPOLL
    { SIGPOLL, "POLL", },
#endif
#ifdef SIGVTALRM
    { SIGVTALRM, "VTALRM", },
#endif
    /* 他のシグナル */
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
    /* 番号 0 のシグナルは存在しない (C99 7.14)
     * よってこれを配列の終わりの目印とする */
};

/* 各シグナルについて、受け取った後まだ処理していないことを示すフラグ */
static volatile sig_atomic_t signal_received[MAXSIGIDX];
/* 各シグナルについて、受信時に実行するトラップコマンド */
static char *trap_command[MAXSIGIDX];
/* ↑ これらの配列の添字は sigindex で求める。
 * 配列の最初の要素は EXIT トラップに対応する。 */

/* リアルタイムシグナルの signal_received と trap_command */
static volatile sig_atomic_t rtsignal_received[RTSIZE];
static char *rttrap_command[RTSIZE];

/* SIGCHLD を受信すると、signal_received[sigindex(SIGCHLD)] だけでなく
 * この変数も true になる。
 * signal_received[...] は主にトラップを実行するためのフラグとして使うが、
 * sigchld_received は子プロセスの変化の通知を受け取るために使う。 */
static volatile sig_atomic_t sigchld_received;

/* SIGCHLD, HUP, QUIT, TTOU, TSTP, TERM, INT のアクションの初期値。
 * これはシェルが起動時にシグナルハンドラを設定する前の状態を保存しておく
 * ものである。これらのシグナルに対して一度でもトラップが設定されると、
 * そのシグナルの初期値は SIG_DFL に変更される。 */
static struct sigaction
    initsigchldaction, initsighupaction, initsigquitaction,
    initsigttouaction, initsigtstpaction,
    initsigtermaction, initsigintaction;

/* シグナルモジュールを初期化したかどうか
 * これが true のとき、initsig{chld,hup,quit}action の値が有効である */
static bool initialized = false;
/* ジョブ制御用のシグナルの初期化をしたかどうか
 * これが true のとき、initsig{ttou,tstp}action の値が有効である */
static bool job_initialized = false;
/* 対話的動作用のシグナルの初期化をしたかどうか
 * これが true のとき、initsig{int,term}action の値が有効である */
static bool interactive_initialized = false;

/* シグナルモジュールを初期化する */
void init_signal(void)
{
    struct sigaction action;
    sigset_t ss;

    if (!initialized) {
	initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGCHLD, &action, &initsigchldaction) < 0)
	    xerror(0, errno, "sigaction(SIGCHLD)");
	if (sigaction(SIGHUP, &action, &initsighupaction) < 0)
	    xerror(0, errno, "sigaction(SIGHUP)");
	if (sigaction(SIGQUIT, &action, &initsigquitaction) < 0)
	    xerror(0, errno, "sigaction(SIGQUIT)");

	sigemptyset(&ss);
	if (sigprocmask(SIG_SETMASK, &ss, NULL) < 0)
	    xerror(0, errno, "sigprocmask(SETMASK, nothing)");
    }
}

/* do_job_control と is_interactive_now に従ってシグナルを設定する。 */
void set_signals(void)
{
    struct sigaction action;

    if (do_job_control && !job_initialized) {
	job_initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGTTOU, &action, &initsigttouaction) < 0)
	    xerror(0, errno, "sigaction(SIGTTOU)");
	if (sigaction(SIGTSTP, &action, &initsigtstpaction) < 0)
	    xerror(0, errno, "sigaction(SIGTSTP)");
    }
    if (is_interactive_now && !interactive_initialized) {
	interactive_initialized = true;

	sigemptyset(&action.sa_mask);
	action.sa_flags = 0;
	action.sa_handler = sig_handler;
	if (sigaction(SIGINT, &action, &initsigintaction) < 0)
	    xerror(0, errno, "sigaction(SIGINT)");
	if (sigaction(SIGTERM, &action, &initsigtermaction) < 0)
	    xerror(0, errno, "sigaction(SIGTERM)");
    }
}

/* シグナルハンドラを初期設定に戻す。
 * qiignore: true なら、SIGQUIT と SIGINT はブロックしたままにする。 */
void reset_signals(void)
{
    if (initialized) {
	if (sigaction(SIGCHLD, &initsigchldaction, NULL) < 0)
	    xerror(0, errno, "sigaction(SIGCHLD)");
	if (sigaction(SIGHUP, &initsighupaction, NULL) < 0)
	    xerror(0, errno, "sigaction(SIGHUP)");
	if (sigaction(SIGQUIT, &initsigquitaction, NULL) < 0)
	    xerror(0, errno, "sigaction(SIGQUIT)");
    }
    if (job_initialized) {
	if (sigaction(SIGTTOU, &initsigttouaction, NULL) < 0)
	    xerror(0, errno, "sigaction(SIGTTOU)");
	if (sigaction(SIGTSTP, &initsigtstpaction, NULL) < 0)
	    xerror(0, errno, "sigaction(SIGTSTP)");
    }
    if (interactive_initialized) {
	if (sigaction(SIGINT, &initsigintaction, NULL) < 0)
	    xerror(0, errno, "sigaction(SIGINT)");
	if (sigaction(SIGTERM, &initsigtermaction, NULL) < 0)
	    xerror(0, errno, "sigaction(SIGTERM)");
    }
}

/* SIGQUIT と SIGINT をブロックする */
void block_sigquit_and_sigint(void)
{
    sigset_t ss;
    sigemptyset(&ss);
    sigaddset(&ss, SIGQUIT);
    sigaddset(&ss, SIGINT);
    if (sigprocmask(SIG_SETMASK, &ss, NULL) < 0)
	xerror(0, errno, "sigprocmask(SETMASK, QUIT|INT)");
}

/* 汎用のシグナルハンドラ */
void sig_handler(int signum)
{
#if defined SIGRTMIN && defined SIGRTMAX
    if (SIGRTMIN <= signum && signum <= SIGRTMAX) {
	size_t index = signum - SIGRTMIN;
	if (index < RTSIZE)
	    rtsignal_received[index] = true;
    } else
#endif
    {
	size_t index = sigindex(signum);
	assert(index < MAXSIGIDX);
	signal_received[index] = true;

	if (signum == SIGCHLD)
	    sigchld_received = true;
    }
}

/* wait_for_sigchld を呼ぶ前にこの関数を呼んで、
 * SIGCHLD と SIGHUP をブロックする。 */
void block_sigchld_and_sighup(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    sigaddset(&ss, SIGCHLD);
    sigaddset(&ss, SIGHUP);
    if (sigprocmask(SIG_BLOCK, &ss, NULL) < 0)
	xerror(0, errno, "sigprocmask(BLOCK, CHLD|HUP)");
}

/* wait_for_sigchld を呼んだ後にこの関数を呼んで、
 * SIGCHLD と SIGHUP のブロックを解除する。 */
void unblock_sigchld_and_sighup(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    sigaddset(&ss, SIGCHLD);
    sigaddset(&ss, SIGHUP);
    if (sigprocmask(SIG_UNBLOCK, &ss, NULL) < 0)
	xerror(0, errno, "sigprocmask(UNBLOCK, CHLD|HUP)");
}

/* SIGCHLD を受信するまで待機する。
 * この関数は SIGCHLD と SIGHUP をブロックした状態で呼び出すこと。
 * 既にシグナルを受信済みの場合、待機せずにすぐ返る。
 * SIGHUP にトラップを設定していない場合に SIGHUP を受信すると、ただちに
 * シェルを終了する。 */
void wait_for_sigchld(void)
{
    sigset_t ss;

    sigemptyset(&ss);
    for (;;) {
	if (signal_received[sigindex(SIGHUP)])
	    (void) 0; // TODO sig.c: SIGHUP 受信時にシェルを終了する
	if (sigchld_received)
	    break;
	if (sigsuspend(&ss) < 0 && errno != EINTR) {
	    xerror(0, errno, "sigsuspend");
	    break;
	}
    }

    sigchld_received = false;
}

/* トラップが設定されたシグナルを受信していたら、対応するコマンドを実行する。 */
void handle_traps(void)
{
    (void) trap_command;
    (void) rttrap_command;
    // TODO sig.c: handle_traps: 未実装
}

/* SIG_IGN 以外に設定したトラップを全て解除する */
void clear_traps(void)
{
    // TODO sig.c: clear_traps: 未実装
}


/* vim: set ts=8 sts=4 sw=4 noet: */
