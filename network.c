#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <unistd.h>
#include <fcntl.h>
#include "nyaxy.h"

#define BUFSIZE 65536

#define STATE_NEW    0
#define STATE_ACTIVE 1
#define STATE_CLOSE  2

struct pollpair {
	int state;
	int src_fd;
	int dest_fd;
	struct pollfd *src;
	struct pollfd *dest;
	size_t in_sz;
	size_t out_sz;
	char in[BUFSIZE];
	char out[BUFSIZE];
};

static struct pollfd *fds;
static struct pollpair *pairs;
static int fd_count = 0;
static int pair_count = 0;

static int net_init(int port);
static int open_port(int port);
static void handle_connection();
static void handle_new_pair(struct pollpair *pair);
static int handle_pair(struct pollpair *pair);
static void resize_fds();
static void close_new_pair(struct pollpair *pair);
static int handle_read(int fd, char *ptr, size_t *sz);
static int handle_write(int fd, char *ptr, size_t *sz);
static int is_pair_done(struct pollpair *pair);
void remove_pair(struct pollpair *pair);

const char BAD_ADDRESS[] = "Bad address. Expected format: <ip address>:<port>\\n\nE.g. 1.2.3.4:5678\\n\n";

int handle(int port) {
	int do_update;
	int i;

	if (net_init(port)) {
		return 1;
	}

	while (1) {
		poll(fds, fd_count, -1);

		for (i=pair_count; i--;) {
			if (handle_pair(pairs + i)) {
				do_update = 1;
			}
		}

		for (i=pair_count; i--;) {
			if (pairs[i].state == STATE_CLOSE) {
				remove_pair(pairs+i);
				do_update = 1;
			}
		}

		if (fds->revents & POLLIN) {
			handle_connection();
			do_update = 1;
		}

		if (do_update) {
			do_update = 0;
			resize_fds();
		}
	}
}

void handle_connection() {
	struct sockaddr_storage addr;
	struct pollpair *newest;
	struct pollfd *old;
	int fd;
	socklen_t len;

	len = sizeof(struct sockaddr_storage);
	fd = accept(fds->fd, (struct sockaddr *)&addr, &len);
	if (fd < 0) {
		fprintf(stderr, "Failed to accept connection.");
		return;
	}

	pair_count++;
	pairs = realloc(pairs, sizeof(struct pollpair) * pair_count);

	newest = pairs + pair_count - 1;
	newest->state = STATE_NEW;
	newest->src_fd = fd;
	newest->dest_fd = -1;
	newest->in_sz = 0;
	newest->out_sz = 0;
}

void remove_pair(struct pollpair *pair) {
	struct pollpair *last;

	if (pair->src_fd >= 0) {
		close(pair->src_fd);
	}

	if (pair->dest_fd >= 0) {
		close(pair->dest_fd);
	}

	last = pairs + pair_count - 1;

	if (pair != last) {
		memcpy(pair, last, sizeof(struct pollpair));
	}

	pairs = realloc(pairs, sizeof(struct pollpair) * --pair_count);
}

int handle_read(int fd, char *ptr, size_t *sz) {
	int x;

	if ((x = read(fd, ptr + *sz, BUFSIZE - *sz)) < 0) {
		perror("read()");
		return -1;
	}

	if (x) {
		*sz += x;
		return 1;
	} else {
		return 0;
	}
}

int handle_write(int fd, char *ptr, size_t *sz) {
	int x;

	if ((x = write(fd, ptr, *sz)) < 0) {
		perror("write()");
		return -1;
	}

	if (x == *sz) {
		*sz = 0;
		return 0;
	} else {
		*sz -= x;
		memcpy(ptr, ptr+x, *sz);
		return 1;
	}
}

int is_pair_done(struct pollpair *pair) {
	int in_left, out_left;

	if (pair->src_fd >= 0 && pair->dest_fd >= 0) {
		return 0;
	}

	in_left  = pair->dest_fd < 0 ? 0 : pair->in_sz;
	out_left = pair->src_fd  < 0 ? 0 : pair->out_sz;

	return !in_left && !out_left;
}

int handle_pair(struct pollpair *pair) {
	int bytes;
	int rv;
	int do_update = 0;

	if (pair->src_fd >= 0) {
		if (pair->in_sz < BUFSIZE && pair->src->revents & (POLLIN|POLLHUP)) {
			rv = handle_read(pair->src_fd, pair->in, &(pair->in_sz));
			if (rv < 0) {
				pair->state = STATE_CLOSE;
			}
			else if (!rv && pair->src->revents & POLLHUP) {
				close(pair->src_fd);
				pair->src_fd = -1;
				do_update = 1;
			}
		}

		if (pair->out_sz && pair->src->revents & POLLOUT) {
			rv = handle_write(pair->src_fd, pair->out, &(pair->out_sz));
			if (rv < 0) {
				pair->state = STATE_CLOSE;
			}
		}
	}

	if (pair->dest_fd >= 0) {
		if (pair->out_sz < BUFSIZE && pair->dest->revents & (POLLIN|POLLHUP)) {
			rv = handle_read(pair->dest_fd, pair->out, &(pair->out_sz));
			if (rv < 0) {
				pair->state = STATE_CLOSE;
			}
			else if (!rv && pair->dest->revents & POLLHUP) {
				close(pair->dest_fd);
				pair->dest_fd = -1;
				do_update = 1;
			}
		}

		if (pair->in_sz && pair->dest->revents & POLLOUT) {
			rv = handle_write(pair->dest_fd, pair->in, &(pair->in_sz));
			if (rv < 0) {
				pair->state = STATE_CLOSE;
			}
		}
	}

	if (pair->dest) {
		if (pair->in_sz) {
			pair->dest->events |= POLLOUT;
		} else {
			pair->dest->events &= ~POLLOUT;
		}
	}

	if (pair->src) {
		if (pair->out_sz) {
			pair->src->events |= POLLOUT;
		} else {
			pair->src->events &= ~POLLOUT;
		}
	}

	/* new connection */
	if (pair->src_fd >= 0 && pair->state == STATE_NEW) {
		handle_new_pair(pair);
		if (pair->state != STATE_NEW) {
			do_update = 1;
		}
	} else if (is_pair_done(pair)) {
		pair->state = STATE_CLOSE;
		do_update = 1;
	}
	
	return do_update;
}

void handle_new_pair(struct pollpair *pair) {
	static char host[BUFSIZE];
	static char port[BUFSIZE];
	static char buf[BUFSIZE];
	struct addrinfo hints = { 0 };
	struct addrinfo *addr;
	char *ptr, *div, *end, *stop, *a, *b;
	int x, y, z, remaining, repeat, err, fd;

	remaining = BUFSIZE - pair->in_sz;

	if ((end = memchr(pair->in, '\r', pair->in_sz)) == NULL) {
		return;
	}

	if ((div = memchr(pair->in, ':', pair->in_sz)) == NULL || div > end) {
		fprintf(stderr, "Missing :\n");
		pair->state = STATE_CLOSE;
		return;
	}

	a = host;
	b = buf;
	ptr = pair->in;
	stop = div;

	for (repeat=2; repeat--;) {
		while (ptr < stop) {
			if (*ptr < 0x7f) {
				*(a++) = *(ptr++);
			} else {
				*(b++) = *(ptr++);
			}
		}
		*(a++) = '\0';
		a    = port;
		ptr  = div+1;
		stop = end;
	}

	/* TODO: check port is number */

	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	err = getaddrinfo(host, port, &hints, &addr);
	if (err) {
		fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(err));
		hints.ai_family = AF_INET6;
		err = getaddrinfo(host, port, &hints, &addr);
		if (err) {
			fprintf(stderr, "getaddrinfo(): %s\n", gai_strerror(err));
			pair->state = STATE_CLOSE;
			return;
		}
	}

	fd = socket(addr->ai_family, addr->ai_socktype, addr->ai_protocol);
	if (fd < 0) {
		perror("socket()");
		freeaddrinfo(addr);
		pair->state = STATE_CLOSE;
		return;
	}

	err = connect(fd, addr->ai_addr, addr->ai_addrlen);
	freeaddrinfo(addr);

	if (err || fcntl(fd, F_SETFD, O_NDELAY|O_NONBLOCK) < 0) {
		perror("connect() / fcntl()");
		close(fd);
		pair->state = STATE_CLOSE;
		return;
	}

	if (b != buf) {
		x = b - buf;
		memcpy(pair->in, buf, x);
	} else {
		x = 0;
	}

	if (*(end++) == '\n') {
		end++;
	}
	y = end + 1 - pair->in;
	z = pair->in_sz - y;

	if (z) {
		memcpy(pair->in + x, end + 1, z);
		x += z;
	}

	pair->in_sz   = x;
	pair->state   = STATE_ACTIVE;
	pair->dest_fd = fd;
}

void resize_fds()
{
	struct pollfd *cur;
	int i, x;

	x = 1;

	for (i=pair_count; i--;) {
		if (pairs[i].src_fd >= 0) {
			x++;
		}
		if (pairs[i].dest_fd >= 0) {
			x++;
		}
	}

	fd_count = x;
	fds = realloc(fds, sizeof(*fds) * x);
	cur = fds + x - 1;

	for (i=pair_count; i--;) {
		if (pairs[i].src_fd >= 0) {
			cur->fd = pairs[i].src_fd;
			cur->events = POLLIN | POLLOUT | POLLHUP;
			pairs[i].src = cur;
			cur--;
		} else {
			pairs[i].src = NULL;
		}

		if (pairs[i].dest_fd >= 0) {
			cur->fd = pairs[i].dest_fd;
			cur->events = POLLIN | POLLOUT | POLLHUP;
			pairs[i].dest = cur;
			cur--;
		} else {
			pairs[i].dest = NULL;
		}
	}
}

int net_init(int port) {
	int fd;

	if ((fd = open_port(port)) < 0) {
		return 1;
	}

	fds = malloc(sizeof(struct pollfd));
	pairs = NULL;

	fds->fd = fd;
	fds->events = POLLIN;

	fd_count = 1;

	return 0;
}

int open_port(int port) {
	struct sockaddr_in addr;
	int fd;
	int value;

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) {
		perror("socket()");
		return -1;
	}

	value = 1;
	if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &value, sizeof(value)) < 0) {
		perror("setsockopt()");
		return -1;
	}

	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		perror("bind()");
		return -1;
	}

	if (listen(fd, 32) < 0) {
		perror("listen()");
		return -1;
	}

	return fd;
}

