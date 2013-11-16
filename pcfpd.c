/*
 * Copyright (C) 2013 Alexander J. Iadicicco
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <signal.h>

#define DEFAULT_PORT 843
#define MAX_POLICY_LEN 65536

/* TODO: fflush in exit handler */

static FILE *log_f;

static const char *log_prefix(void)
{
	static char pfx[512];
	time_t now;
	struct tm *tmp;
	size_t sz;

	now = time(NULL);
	if (!(tmp = localtime(&now)))
		sz = snprintf(pfx, 512, "[----/--/-- --:--:-- +----] ");
	else
		sz = strftime(pfx, 512, "[%Y/%m/%d %H:%M:%S %z] ", tmp);

	return pfx;
}

static void log_line(const char *fmt, ...)
{
	char buf[4096];
	va_list va;

	va_start(va, fmt);
	vsnprintf(buf, 4096, fmt, va);
	va_end(va);

	fprintf(log_f, "%s%s\n", log_prefix(), buf);
	fflush(log_f);
}

static void log_open(const char *filename)
{
	if (log_f != NULL) {
		log_line("tried to open log again");
		return;
	}

	log_f = stdout;

	if (filename) {
		log_f = fopen(filename, "a");

		if (!log_f) {
			fprintf(stderr, "Could not open log file %s; using stdout\n",
			        filename);
			log_f = stdout;
		}
	}

	log_line("pcfpd started");
}

static void log_client(struct sockaddr_in *sa)
{
	char buf[256];

	if (!log_f)
		return;

	inet_ntop(AF_INET, &sa->sin_addr, buf, 256);

	log_line("%s", buf);
}

static void log_errno(const char *msg, int e)
{
	log_line("%s: %s", msg, strerror(e));
}

static char policy_data[MAX_POLICY_LEN];
static size_t policy_len;

static int read_policy(const char *file)
{
	int f;
	ssize_t sz;

	if ((f = open(file, O_RDONLY)) < 0) {
		perror("open");
		return -1;
	}

	policy_len = 0;

	while (policy_len < MAX_POLICY_LEN) {
		sz = read(f, policy_data + policy_len, 65536 - policy_len);
		if (sz < 0) {
			perror("read");
			return -1;
		}
		if (sz == 0)
			break;
		policy_len += sz;
	}

	return 0;
}

static void send_policy(int client)
{
	size_t sent = 0;
	ssize_t sz;

	while (sent < policy_len) {
		sz = write(client, policy_data + sent, policy_len - sent);
		if (sz < 0) {
			perror("write");
			return;
		}
		if (sz == 0) {
			fprintf(stderr, "Wrote 0 bytes?\n");
			return;
		}
		sent += sz;
	}
}

static int create_listener(unsigned short port)
{
	int listener, c;
	struct sockaddr_in addr;

	if ((listener = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		perror("socket");
		return -1;
	}

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);

	c = 1;
	if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &c, sizeof(c)) < 0)
		perror("warning, setsockopt");

	if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return -1;
	}

	if (listen(listener, 5) < 0) {
		perror("listen");
		return -1;
	}

	return listener;
}

static int running;

static void sigint_handler(int sig)
{
	log_line("caught SIGINT. stopping...");
	running = 0;
}

static void sighup_handler(int sig)
{
	log_line("caught SIGHUP. ignoring...");
}

static void sigterm_handler(int sig)
{
	log_line("caught SIGTERM. stopping...");
	running = 0;
}

static void sig_handler(int sig, void (*fn)(int))
{
	struct sigaction act;

	sigaction(sig, NULL, &act);
	act.sa_handler = fn;
	act.sa_flags &= ~SA_RESTART;
	sigaction(sig, &act, NULL);
}

static void usage(const char *argv0)
{
	fprintf(stderr, "\nUsage: %s [OPTIONS] -f POLICY\n", argv0);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, " -f POLICY   Use POLICY as policy file (required)\n");
	fprintf(stderr, " -p PORT     Listen on PORT (default %d)\n", DEFAULT_PORT);
	fprintf(stderr, " -d          Daemonize (fork to background)\n");
	fprintf(stderr, " -l FILE     Log requests to FILE (default stdout)\n");
}

int main(int argc, char *argv[])
{
	int c, listener;
	char *policy_file = NULL;
	char *log_file = NULL;
	unsigned short port = DEFAULT_PORT;
	int do_fork = 0;

	while ((c = getopt(argc, argv, "f:p:dl:")) != -1) switch (c) {
	case 'p':
		port = atoi(optarg);
		if (port == 0) {
			fprintf(stderr, "Invalid port %s\n", optarg);
			return 1;
		}
		break;

	case 'f':
		if (policy_file)
			free(policy_file);
		policy_file = strdup(optarg);
		break;

	case 'l':
		if (log_file)
			free(log_file);
		log_file = strdup(optarg);

	case 'd':
		do_fork = 1;
		break;

	default:
		usage(argv[0]);
		return 1;
	}

	log_open(log_file);

	sig_handler(SIGINT, sigint_handler);
	sig_handler(SIGHUP, sighup_handler);
	sig_handler(SIGTERM, sigterm_handler);
	sig_handler(SIGPIPE, SIG_IGN);

	if (!policy_file) {
		fprintf(stderr, "Missing required policy file argument -f\n");
		usage(argv[0]);
		return 1;
	}

	if (read_policy(policy_file) < 0) {
		fprintf(stderr, "Failed to read policy file\n");
		return 1;
	}

	if ((listener = create_listener(port)) < 0) {
		fprintf(stderr, "Failed to create listener\n");
		return 1;
	}

	if (do_fork) {
		pid_t pid = fork();

		if (pid < 0) {
			perror("fork");
			return 1;
		}

		if (pid != 0) {
			fprintf(stderr, "Forked with PID %d\n", pid);
			return 0;
		}

		close(0);
		close(1);
		close(2);
	}

	for (running = 1; running; ) {
		struct sockaddr_in sa;
		socklen_t salen;
		int client;
		client = accept(listener, (struct sockaddr*)&sa, &salen);
		if (client < 0) {
			int e = errno;
			log_errno("accept", e);
			if (e == EINTR || e == EAGAIN)
				continue;
			break;
		}
		log_client(&sa);
		send_policy(client);
		close(client);
	}
}
