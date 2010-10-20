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


#undef max
#define max(x,y) ((x) > (y) ? (x) : (y))


#define DEBUG



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

char * argbuf[256];
char inbuf[BUFSIZ];
int  inbuf_len;

void str_rebuild(char *buf, size_t n);


int main(int argc, char *argv[])
{
	struct sigaction chld;
	struct sigaction winch;
	struct sigaction sigtrm;
	int STUPID;

#ifdef DEBUG
	log_f = fopen("dump", "w");
#endif

	memset(&chld, 0, sizeof(chld));
	memset(&winch, 0, sizeof(winch));
	memset(&sigtrm, 0, sizeof(sigtrm));
	chld.sa_handler = &sigchld_handler;
	winch.sa_handler = &sigwinch_handler;
	sigtrm.sa_handler = &sigterm_handler;

	term = getenv("TERM");

	master_controlling_tty = open("/dev/tty", O_RDWR | O_NOCTTY);
	ioctl(master_controlling_tty, TIOCGWINSZ, &term_size);  /* save terminal size */

	if(argc < 2) {
		fprintf(stderr, "Improper number of arguments.\n");
		exit(EXIT_FAILURE);
	}

	STUPID = open("/dev/null", O_RDWR);  /* setupterm needs a file descriptor but we don't want it to mess around with our real terminal */
	if(setupterm((char *)0, STUPID, (int *)0) == ERR) {
		fprintf(stderr, "could not get terminfo.\n");
		exit(EXIT_FAILURE);
	}
	close(STUPID);

	setup_escape_seqs();

	argbuf[0] = argv[1];
	inbuf_len = -1;

	if((pid = forkpty(&master_pty, NULL, NULL, &term_size)) == 0) {  /* if child */
		setenv("TERM", term, 1);
		execvp(argv[1], argbuf);
	} else {
		/* set up master pty side stuff */
		int ret;
		int standard_in = dup(STDIN_FILENO);

		tcgetattr(master_controlling_tty, &term_settings);
		tcgetattr(master_controlling_tty, &orig_settings);
		cfmakeraw(&term_settings);
		term_settings.c_cc[VMIN] = 1;
		term_settings.c_cc[VTIME] = 1;

		tcsetattr(master_controlling_tty, TCSANOW, &term_settings);

		sigaction(SIGCHLD, &chld, NULL);
		sigaction(SIGWINCH, &winch, NULL);
		sigaction(SIGTERM, &sigtrm, NULL);

		/* main input loop */
		while(1) {
			char in[BUFSIZ];

			int nfds=0;
			int r;

			fd_set rd, wr, er;
			FD_ZERO(&rd);
			FD_ZERO(&wr);
			FD_ZERO(&er);

			nfds = max(nfds, standard_in);
			FD_SET(standard_in, &rd);

			nfds = max(nfds, master_pty);
			FD_SET(master_pty, &rd);

			r = select(nfds+1, &rd, &wr, &er, NULL);

			if(r == -1)
				continue;

			if(FD_ISSET(standard_in, &rd)) {
				ret = read(standard_in, in, BUFSIZ*sizeof(char));

				/* rebuild string */
	//			strncat(inbuf, in, ret);
				str_rebuild(in, ret);

				/* tokenize string */
				if(inbuf_len != -1)
					argbuf[1] = inbuf;
				else
					argbuf[1] = 0;

				/* re-fork */
				close(master_pty);
				kill(pid, SIGKILL);

				if((pid = forkpty(&master_pty, NULL, NULL, &term_size)) == 0) {  /* if child */
					/* write(STDOUT_FILENO, E_KCLR, S_KCLR); */
					system("clear");

					setenv("TERM", term, 1);
					execvp(argv[1], argbuf);
				}

				/* free tokenized string */

			}
			if(FD_ISSET(master_pty, &rd)) {
				ret = read(master_pty, in, BUFSIZ*sizeof(char));
				write(STDOUT_FILENO, in, ret*sizeof(char));
			}
		}
	}

	return 0;
}

void str_rebuild(char *buf, size_t n)
{
	int i;

	for(i=0; i<n; i++) {
		if(buf[i] == 127) {
			if(inbuf_len != -1)
				inbuf[inbuf_len--] = '\0';
		}
		else
			inbuf[++inbuf_len] = buf[i];
	}

	return;
}

void sigchld_handler(int sig_num)
{
	return;  /* we don't care if the child dies */
}

void sigterm_handler(int sig_num)
{
	tcsetattr(master_controlling_tty, TCSANOW, &orig_settings);
	exit(EXIT_SUCCESS);
	return;  /* we don't care if the child dies */
}

void sigwinch_handler(int sig_num)
{
	ioctl(master_controlling_tty, TIOCGWINSZ, &term_size);  /* save new terminal size */
	ioctl(slave_pty, TIOCSWINSZ, &term_size);  /* set terminal size */

	kill(pid, SIGWINCH);  /* send resize signal to child */
}

void setup_escape_seqs()
{
	E_KCLR	= tigetstr("kent");	S_KCLR	= strlen(E_KCLR);

	return;
}
