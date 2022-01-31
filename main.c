#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "nyaxy.h"

static void print_syntax(const char *arg);

int main(int argc, char **argv) {
	long port;
	char *end;

	if (argc != 2) {
		print_syntax(argv[0]);
		return 1;
	}

	port = strtol(argv[1], &end, 10);

	if (!*argv[1] || *end || port < 1 || port > 65535) {
		print_syntax(argv[0]);
		return 1;
	}

	return handle(port);
}

void print_syntax(const char *arg) {
	fprintf(stderr, "Syntax: %s <port>\n", arg);
}
