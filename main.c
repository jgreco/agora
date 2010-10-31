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
#include "vterm.h"

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
int vterm_pty;
int slave_pty;

int pid;

struct winsize term_size;

void sigchld_handler(int sig_num);
void sigwinch_handler(int sig_num);
void sigterm_handler(int sig_num);

WINDOW *term_win, *command_win;
vterm_t *vterm;
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
	int i, j;

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

	for(i=0; i<8; i++)
	for(j=0; j<8; j++)
		if(i!=7 || j!=0)
			init_pair(j*8+7-i, i, j);


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
	if((vterm = vterm_create_classic(COLS, LINES-1, 0)) == 0) {
		exit(EXIT_SUCCESS);
	} else {
		/* set up master pty side stuff */
		int standard_in = dup(STDIN_FILENO);

		vterm_set_colors(vterm,COLOR_WHITE,COLOR_BLACK);
		vterm_wnd_set(vterm, term_win);


		sigaction(SIGCHLD, &chld, NULL);
		sigaction(SIGWINCH, &winch, NULL);
		sigaction(SIGTERM, &sigtrm, NULL);
		sigaction(SIGINT, &sigint, NULL);
		sigaction(SIGHUP, &sighup, NULL);

		/* setup readline */
		rl_callback_handler_install ("", null);

		/* main input loop */
		while(1) {
			fd_set rd, wr, er;
			int nfds=0;
			int r;

			pid = vterm_get_pid(vterm);
			vterm_pty = vterm_get_pty_fd(vterm);

			/* setup select stuff */
			FD_ZERO(&rd);
			FD_ZERO(&wr);
			FD_ZERO(&er);

			nfds = max(nfds, standard_in);
			FD_SET(standard_in, &rd);

			nfds = max(nfds, vterm_pty);
			FD_SET(vterm_pty, &rd);

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
				close(vterm_pty);
				free(vterm);  /* is this ok? */
				kill(pid, SIGKILL);

				/* re-fork */
				if((vterm = vterm_create_classic(COLS, LINES-1, 0)) == 0) {
					execvp(argbuf[0], argbuf);
				}
				vterm_set_colors(vterm,COLOR_WHITE,COLOR_BLACK);
				vterm_wnd_set(vterm, term_win);

				/* free tokenized string */
				g_strfreev(argbuf);
			}

			/* read output from child proc */
			if(FD_ISSET(vterm_pty, &rd)) {
				if(vterm_read_pipe(vterm)) {
					vterm_wnd_update(vterm);
					touchwin(term_win);
					wrefresh(term_win);
					refresh();
				}
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
	endwin();
	refresh();

	wresize(term_win,LINES-1, COLS);
	wresize(command_win, 1, COLS);
	mvwin(command_win, LINES-1, 0);

	wrefresh(term_win);
	wrefresh(command_win);

	vterm_resize(vterm, LINES-1, COLS);

	return;
}
