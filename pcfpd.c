#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define DEFAULT_PORT 843
#define MAX_POLICY_LEN 65536

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

	if (bind(listener, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		perror("bind");
		return -1;
	}

	c = 1;
	if (setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &c, sizeof(c)) < 0)
		perror("warning, setsockopt");

	if (listen(listener, 5) < 0) {
		perror("listen");
		return -1;
	}

	return listener;
}

static void usage(const char *argv0)
{
	fprintf(stderr, "Usage: %s -f POLICY [-p PORT]\n", argv0);
	fprintf(stderr, "Default port is %d\n", DEFAULT_PORT);
}

int main(int argc, char *argv[])
{
	int c, listener;
	char *policy_file = NULL;
	unsigned short port = DEFAULT_PORT;

	while ((c = getopt(argc, argv, "p:f:")) != -1) switch (c) {
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

	default:
		usage(argv[0]);
		return 1;
	}

	if (!policy_file) {
		fprintf(stderr, "Missing required policy file argument -f\n");
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

	for (;;) {
		int client = accept(listener, NULL, NULL);
		if (client < 0) {
			perror("accept");
			break;
		}
		send_policy(client);
		close(client);
	}
}
