#define _GNU_SOURCE
#include <stdio.h>
#include <termios.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <sys/select.h>
#include <signal.h>
#include <string.h>
#include <ctype.h>
#include <pty.h>

/* for terminfo usage only.  curses input/output is never used */
#include <ncurses.h>
#include <term.h>
#include "madtty.h"

/* for g_shell_parse_argv */
#include <glib.h>

#include <readline/readline.h>


#undef max
#define max(x,y) ((x) > (y) ? (x) : (y))


/*
#define DEBUG
*/

#ifdef DEBUG
FILE *log_f;
#endif

int master_controlling_tty;
int madtty_pty;

madtty_t *rt;
int madtty_ready;

int pid;

struct winsize term_size;

void sigchld_handler(int sig_num);
void sigwinch_handler(int sig_num);
void sigterm_handler(int sig_num);

WINDOW *term_win, *command_win;
int screen_h, screen_w;


void null();

int main(int argc, char *argv[])
{
	struct sigaction chld;
	struct sigaction winch;
	struct sigaction sigtrm;
	struct sigaction sigint;
	struct sigaction sighup;
	char buf[BUFSIZ];
	int cursor_offset;
	char *inbuf;

	memset(&chld, 0, sizeof(chld));
	memset(&winch, 0, sizeof(winch));
	memset(&sigtrm, 0, sizeof(sigtrm));
	memset(&sigint, 0, sizeof(sigint));
	memset(&sighup, 0, sizeof(sighup));
	chld.sa_handler = &sigchld_handler;
	winch.sa_handler = &sigwinch_handler;
	sigtrm.sa_handler = &sigterm_handler;
	sigint.sa_handler = &sigterm_handler;
	sighup.sa_handler = &sigterm_handler;

	/* ncurses setup */
	initscr();
	noecho();
	start_color();
	raw();
	nodelay(stdscr, TRUE);
	curs_set(0);

	keypad(stdscr, TRUE);

	madtty_init_vt100_graphics();
	madtty_init_colors();
	getmaxyx(stdscr, screen_h, screen_w);

	rt = madtty_create(screen_h-1, screen_w);

	term_win = newwin(LINES-1, COLS, 0, 0);
	command_win = newwin(1, COLS, LINES-1, 0);
	wrefresh(term_win);
	wrefresh(command_win);

#ifdef DEBUG
	log_f = fopen("dump", "w");
#endif

	if(argc < 2) {
		fprintf(stderr, "Improper number of arguments.\n");
		exit(EXIT_FAILURE);
	}

	/* set up string to be parsed into child's argv */
	memcpy(buf, argv[1], strlen(argv[1]));
	inbuf = buf + 1 + strlen(argv[1]);
	cursor_offset = 1 + strlen(argv[1]);
	(inbuf-1)[0] = ' ';

	/* start off by forking a child with no arguments */
	if((pid = madtty_forkpty_classic(rt, &madtty_pty)) == 0) {
		exit(EXIT_SUCCESS);
	} else {
		/* set up master pty side stuff */
		int standard_in = dup(STDIN_FILENO);
		madtty_ready = 1;



		sigaction(SIGCHLD, &chld, NULL);
		sigaction(SIGWINCH, &winch, NULL);
		sigaction(SIGTERM, &sigtrm, NULL);
		sigaction(SIGINT, &sigint, NULL);
		sigaction(SIGHUP, &sighup, NULL);

		/* setup readline */
		rl_callback_handler_install ("", null);
		rl_variable_bind("editing-mode", "vi");

		/* main input loop */
		while(1) {
			fd_set rd, wr, er;
			int nfds=0;
			int r;

			/* setup select stuff */
			FD_ZERO(&rd);
			FD_ZERO(&wr);
			FD_ZERO(&er);

			nfds = max(nfds, standard_in);
			FD_SET(standard_in, &rd);

			if(madtty_ready) {
				nfds = max(nfds, madtty_pty);
				FD_SET(madtty_pty, &rd);
			}

			r = select(nfds+1, &rd, &wr, &er, NULL);

			if(r == -1) {
				/* select errored, lets try again... */
				continue;
			}

			/* read user input */
			if(FD_ISSET(standard_in, &rd)) {
				char **argbuf;
				GError *err = NULL;

				rl_callback_read_char();

				/* rebuild string */
				strncpy(inbuf, rl_line_buffer, BUFSIZ - (1 + strlen(argv[1])));

				werase(command_win);
				mvwprintw(command_win, 0, 0, "%s", buf);
				mvwchgat(command_win, 0, rl_point + cursor_offset, 1, A_REVERSE, 0, NULL);
				wrefresh(command_win);

				/* tokenize string */
				g_shell_parse_argv(buf, NULL, &argbuf, &err);
				if(err != NULL)
					continue;

				/* kill old child, if it's still around */
				kill(pid, SIGKILL);

				/* re-fork */
				if((pid = madtty_forkpty_classic(rt, &madtty_pty)) == 0) {
					execvp(argbuf[0], argbuf);
				}

				madtty_ready = 1;

				/* free tokenized string */
				g_strfreev(argbuf);
			}

			/* read output from child proc */
			if(FD_ISSET(madtty_pty, &rd)) {
				if(madtty_process(rt) == -1)
					madtty_ready = 0;
				madtty_draw(rt, term_win, 0, 0);
				wrefresh(term_win);
			}
		}
	}

	return 0;
}

void null()
{
}

void sigchld_handler(int sig_num)
{
	return;  /* we don't care if the child dies */
}

void sigterm_handler(int sig_num)
{
	endwin();
	rl_callback_handler_remove();
	exit(EXIT_SUCCESS);
	return;
}

void sigwinch_handler(int sig_num)
{
	int fd, cols = -1, rows = -1;
	struct winsize w;

	if((fd = open("/dev/tty", O_RDONLY)) != -1) {
		if(ioctl(fd, TIOCGWINSZ, &w) != -1) {
			rows = w.ws_row;
			cols = w.ws_col;
		}
		close(fd);
	}

	if(rows <= 0) {
		rows = atoi(getenv("LINES") ? "0" : "24");
	}
	if(cols <= 0) {
		cols = atoi(getenv("COLUMNS") ? "0" : "80");
	}

	madtty_resize(rt, rows-1, cols);

	wresize(term_win,rows-1, cols);
	wresize(command_win, 1, cols);
	mvwin(command_win, rows-1, 0);

	wrefresh(term_win);
	wrefresh(command_win);


	return;
}
