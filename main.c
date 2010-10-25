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
#include <curses.h>
#include <term.h>

/* for g_shell_parse_argv */
#include <glib.h>

#include <readline/readline.h>


#undef max
#define max(x,y) ((x) > (y) ? (x) : (y))


/**/
#define DEBUG
/**/

#ifdef DEBUG
FILE *log_f;
#endif

int master_controlling_tty;
int master_pty;
int slave_pty;

int pid;

char *term = NULL;
struct termios term_settings;
struct termios orig_settings;
struct winsize term_size;

void setup_escape_seqs();

char *E_KCLR;	int S_KCLR;

void sigchld_handler(int sig_num);
void sigwinch_handler(int sig_num);
void sigterm_handler(int sig_num);

char buf[BUFSIZ];
char *inbuf;
int  inbuf_len;
char *slave_name;

void null();

int main(int argc, char *argv[])
{
	struct sigaction chld;
	struct sigaction winch;
	struct sigaction sigtrm;
	struct sigaction sigint;
	struct sigaction sighup;
	int STUPID;

#ifdef DEBUG
	log_f = fopen("dump", "w");
#endif

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

	term = getenv("TERM");

	master_controlling_tty = open("/dev/tty", O_RDWR | O_NOCTTY);
	ioctl(master_controlling_tty, TIOCGWINSZ, &term_size); /* save terminal size */

	if(argc < 2) {
		fprintf(stderr, "Improper number of arguments.\n");
		exit(EXIT_FAILURE);
	}

	/* setupterm needs a file descriptor but we don't want it to mess
	 * around with our real terminal */
	STUPID = open("/dev/null", O_RDWR);
	if(setupterm((char *)0, STUPID, (int *)0) == ERR) {
		fprintf(stderr, "could not get terminfo.\n");
		exit(EXIT_FAILURE);
	}
	close(STUPID);

	setup_escape_seqs();

	memcpy(buf, argv[1], strlen(argv[1]));
	inbuf = buf + 1 + strlen(argv[1]);
	(inbuf-1)[0] = ' ';

	if((pid = forkpty(&master_pty, slave_name, NULL, &term_size)) == 0) {  /* if child */
		setenv("TERM", term, 1);
		execvp(buf, "");
	} else {
		/* set up master pty side stuff */
		int ret;
		int master_pty_status = 1;
		int standard_in = dup(STDIN_FILENO);

		rl_callback_handler_install ("", null);

		sigaction(SIGCHLD, &chld, NULL);
		sigaction(SIGWINCH, &winch, NULL);
		sigaction(SIGTERM, &sigtrm, NULL);
		sigaction(SIGINT, &sigint, NULL);
		sigaction(SIGHUP, &sighup, NULL);

		/* main input loop */
		while(1) {
			int nfds=0;
			int r;

			fd_set rd, wr, er;
			FD_ZERO(&rd);
			FD_ZERO(&wr);
			FD_ZERO(&er);

			nfds = max(nfds, standard_in);
			FD_SET(standard_in, &rd);

			if(master_pty_status) {
				nfds = max(nfds, master_pty);
				FD_SET(master_pty, &rd);
			}

			r = select(nfds+1, &rd, &wr, &er, NULL);

			if(r == -1) {
				continue;
			}

			if(FD_ISSET(standard_in, &rd)) {
				char **argbuf;
				GError *err = NULL;

				rl_callback_read_char();

				/* rebuild string */
				strncpy(inbuf, rl_line_buffer, BUFSIZ - (1 + strlen(argv[1])));

				/* tokenize string */
				g_shell_parse_argv(buf, NULL, &argbuf, &err);
				if(err != NULL)
					continue;

				/* re-fork */
				close(master_pty);
				kill(pid, SIGKILL);

				if((pid = forkpty(&master_pty, slave_name, NULL, &term_size)) == 0) {  /* if child */
					/* clear screen */
					write(STDOUT_FILENO, E_KCLR, S_KCLR);

					setenv("TERM", term, 1);
					execvp(argbuf[0], argbuf);
				}

				/* free tokenized string */
				g_strfreev(argbuf);

				master_pty_status = 1;
			}
			if(FD_ISSET(master_pty, &rd)) {
				char in[BUFSIZ];
				ret = read(master_pty, in, BUFSIZ*sizeof(char));

				if(ret == -1) {
					master_pty_status = 0;
					continue;
				}

				write(STDOUT_FILENO, in, ret*sizeof(char));
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
	tcsetattr(master_controlling_tty, TCSANOW, &orig_settings);
	exit(EXIT_SUCCESS);
	return;
}

void sigwinch_handler(int sig_num)
{
	ioctl(master_controlling_tty, TIOCGWINSZ, &term_size);  /* save new terminal size */
	ioctl(slave_pty, TIOCSWINSZ, &term_size);  /* set terminal size */

	kill(pid, SIGWINCH);  /* send resize signal to child */
}

void setup_escape_seqs()
{
	E_KCLR	= tigetstr("clear");	S_KCLR	= strlen(E_KCLR);

	return;
}
