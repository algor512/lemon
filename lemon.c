#include <signal.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/timerfd.h>

#define FORCE_UPDATE_INTERVAL 30      /* seconds */
#define MIN_UPDATE_INTERVAL   500     /* milliseconds */
#define MAX_BLOCKS   1024
#define MAX_CMDS     1024
#define SECONDS(x) (1000 * (x))

#ifdef DEBUG
#define LOG(fmt, ...) fprintf(stderr, "%s:%d:%s " fmt "\n", __FILE__, __LINE__, __func__, ##__VA_ARGS__)
#else
#define LOG(fmt, ...)
#endif

enum {
	BLOCK_RAW,
	BLOCK_CONTINUOUS,
	BLOCK_SINGLE
};

struct block {
	int type;
	long update_time;
	char *cmd[4];
	char *str;
};

struct cmd {
	char *cmd[4];
	int pid;
	int block;
	int fd;
	FILE *ffd;
};

static struct block block[MAX_BLOCKS] = {0, };
static struct cmd cmd[MAX_CMDS] = {0, };
static int nblocks = 0;
static int ncmds = 0;
static char *buf = NULL;

static char **bar_cmd = NULL;
static int bar_pid = -1;
static int bar_fd = STDOUT_FILENO;


static inline void close_cmd(int i)
{
	if (cmd[i].ffd) {
		fclose(cmd[i].ffd);
	} else if (cmd[i].fd) {
		close(cmd[i].fd);
	}
}

static inline void make_cmd(char *out[3], char *cmd)
{
	out[0] = "/bin/sh";
	out[1] = "-c";
	out[2] = cmd;
	out[3] = NULL;
}

static void cleanup()
{
	if (buf) {
		free(buf);
		buf = NULL;
	}
	for (int i = 0; i < nblocks; ++i) {
		if (block[i].type != BLOCK_RAW && block[i].str) {
			free(block[i].str);
			block[i].str = NULL;
		}
	}
	for (int i = 0; i < ncmds; ++i) close_cmd(i);
	kill(0, SIGTERM);
	while (wait(NULL) > 0);
}

static void sighandler(int signum)
{
	switch (signum) {
	case SIGINT:
	case SIGTERM:
		cleanup();
		exit(0);
		break;
	}
}

static void err(const char *msg)
{
	cleanup();
	fprintf(stderr, "%s\n", msg);
	exit(1);
}

static void strip(char *str)
{
	int l = strlen(str);
	if (str[l - 1] == '\n')
		str[l - 1] = '\0';
}

static void run_command(char **cmd, int *pid, int *out_fd)
{
	int p[2];
	if (pipe(p) == -1) err("error in pipe()");

	*pid = fork();
	if (*pid < -1) err("error in fork()");
	if (*pid == 0) {
		dup2(p[1], STDOUT_FILENO);
		for (int i = STDERR_FILENO + 1; i < (int) sysconf(_SC_OPEN_MAX); i++) {
			close(i);
		}
		execvp(cmd[0], cmd);
	}
	close(p[1]);
	*out_fd = p[0];
}

static void restart_cmd(int i)
{
	LOG("(re)start cmd %d", i);
	if (cmd[i].pid > 0) {
		kill(cmd[i].pid, SIGKILL);
		waitpid(cmd[i].pid, NULL, 0); // todo: get return code
		close_cmd(i);
	}

	int pid, out_fd;
	run_command(cmd[i].cmd, &pid, &out_fd);

	LOG("new cmd %d pid=%d fd=%d", i, pid, out_fd);
	cmd[i].pid = pid;
	cmd[i].fd = out_fd;
	cmd[i].ffd = fdopen(out_fd, "r");
}

static void update_block(int i)
{
	int pid, out_fd;
	struct timespec ts;
	long current_time;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) err("error in clock_gettime()");
	current_time = 1000 * ts.tv_sec + ts.tv_nsec / 1000000;

	if (block[i].update_time != 0 && (current_time - block[i].update_time) <= MIN_UPDATE_INTERVAL) {
		fprintf(stderr, "block %d: too frequent updates\n", i);
		return;
	}

	run_command(block[i].cmd, &pid, &out_fd);
	LOG("update block %d, read fd=%d", i, out_fd);
	waitpid(pid, NULL, 0); // todo: get return code

	size_t ignore = 0;
	FILE *f = fdopen(out_fd, "r");
	if (getline(&block[i].str, &ignore, f) < 0) err("error in getline()");
	strip(block[i].str);
	fclose(f);
}

static void update_blocks()
{
	for (int i = 0; i < nblocks; ++i) {
		if (block[i].type == BLOCK_SINGLE) {
			update_block(i);
		}
	}
}

static void print_status()
{
	/* (re)start bar if needed */
	int ws;
	if (bar_cmd != NULL && (bar_pid < 0 || (ws = waitpid(bar_pid, NULL, WNOHANG)) != 0)) {
		int p[2];
		if (pipe(p) == -1) err("error in pipe()");

		bar_pid = fork();
		if (bar_pid < -1) err("error in fork()");
		if (bar_pid == 0) {
			dup2(p[0], STDIN_FILENO);
			for (int i = STDERR_FILENO + 1; i < (int) sysconf(_SC_OPEN_MAX); i++) {
				close(i);
			}
			execvp(bar_cmd[0], bar_cmd);
		}
		close(p[0]);
		bar_fd = p[1];
		LOG("(re)start bar, fd = %d", bar_fd);
	}

	for (int i = 0; i < nblocks; ++i) {
		if (block[i].str) {
			write(bar_fd, block[i].str, strlen(block[i].str));
		}
	}
	write(bar_fd, "\n", 1);
	fsync(bar_fd);
}

int main(int argc, char *argv[])
{
	setpgid(0, 0);
	signal(SIGCHLD, SIG_IGN);
	signal(SIGTERM, sighandler);

	int pos = 1;
	while (pos < argc) {
		if (strcmp(argv[pos], "-r") == 0) {
			++pos;
			if (pos >= argc) err("invalid command line arguments");
			block[nblocks].type = BLOCK_RAW;
			block[nblocks].str = argv[pos];
			++nblocks;
			++pos;
		} else if (strcmp(argv[pos], "-c") == 0) {
			++pos;
			if (pos >= argc) err("invalid command line arguments");
			block[nblocks].type = BLOCK_CONTINUOUS;
			make_cmd(cmd[ncmds].cmd, argv[pos]);
			cmd[ncmds].block = nblocks;
			++nblocks;
			++ncmds;
			++pos;
		} else if (strcmp(argv[pos], "-s") == 0) {
			++pos;
			if (pos >= argc) err("invalid command line arguments");
			block[nblocks].type = BLOCK_SINGLE;
			make_cmd(block[nblocks].cmd, argv[pos]);
			++pos;
			for (; pos < argc && argv[pos][0] != '-'; ++pos) {
				make_cmd(cmd[ncmds].cmd, argv[pos]);
				cmd[ncmds].block = nblocks;
				++ncmds;
			}
			++nblocks;
		} else if (strcmp(argv[pos], "--") == 0) {
			bar_cmd = &argv[pos + 1];
			break;
		} else {
			err("invalid command line arguments");
		}
	}

	update_blocks();
	print_status();

	struct pollfd polls[MAX_CMDS + 1];
	for (int i = 0; i < ncmds; ++i) {
		restart_cmd(i);
		polls[i].fd = cmd[i].fd;
		polls[i].events = POLLIN;
	}

	int timer_fd = timerfd_create(CLOCK_MONOTONIC, 0);
	struct itimerspec timer_spec = {{ FORCE_UPDATE_INTERVAL, 0 }, { FORCE_UPDATE_INTERVAL, 0 }};
	timerfd_settime(timer_fd, 0, &timer_spec, NULL);

	polls[ncmds].fd = timer_fd;
	polls[ncmds].events = POLLIN;

	int ret;
	size_t ignore;
	char ignore_buf[8];
	while (1) {
		ret = poll(polls, ncmds + 1, -1);
		if (ret <= 0) err("error in poll()");

		for (int i = 0; i < ncmds; ++i) {
			if (polls[i].revents & (POLLERR | POLLHUP | POLLNVAL)) {
				/* try to restart cmd[i] */
				restart_cmd(i);
				polls[i].fd = cmd[i].fd;
			}
			if (polls[i].revents & POLLIN) {
				if (block[cmd[i].block].type == BLOCK_SINGLE) {
					if (getline(&buf, &ignore, cmd[i].ffd) < 0) err("error in getline()");
					update_block(cmd[i].block);
				} else {
					if (getline(&block[cmd[i].block].str, &ignore, cmd[i].ffd) < 0) err("error in getline()");
					strip(block[cmd[i].block].str);
				}
			}
		}

		/* timer */
		if (polls[ncmds].revents & (POLLERR | POLLHUP | POLLNVAL)) err("some errors have been recieved from timer");
		if (polls[ncmds].revents & POLLIN) {
			read(timer_fd, ignore_buf, 8);
			update_blocks();
		}
		print_status();
	}

	return 0;
}
