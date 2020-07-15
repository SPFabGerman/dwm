#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <string.h>

#include "sockdef.h"

int main(int argc, char ** argv) {
	struct sockaddr_un addr;
	int sock, i, returnValue;
	int used = 0;
	char inBuf[MAXBUFF_SOCKET];
	char outBuf[MAXBUFF_SOCKET];

	if (argc <= 1) {
		fprintf(stderr, "No function provided.\n");
		return -1;
	}

	if ((sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		fprintf(stderr, "Could not create socket.\n");
		return -1;
	}
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, socket_path, sizeof(addr.sun_path) - 1);
	if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
		fprintf(stderr, "Could not connect to socket.\n");
		return -1;
	}
	
	strncpy(inBuf, argv[1], MAXBUFF_SOCKET);
	for (i = 2; i < argc; i++) {
		int l = strlen(argv[i]);
		if (used + l + 1 > MAXBUFF_SOCKET) {
			fprintf(stderr, "Input to long.\n");
			return -1;
		}
		strcat(inBuf, " ");
		strncat(inBuf, argv[i], MAXBUFF_SOCKET - used);
		used += l + 1;
	}

	send(sock, inBuf, MAXBUFF_SOCKET, 0);

	recv(sock, &returnValue, sizeof(returnValue), 0);
	recv(sock, outBuf, MAXBUFF_SOCKET, 0);

	close(sock);

	printf("%s\n", outBuf);
	
	return returnValue;
}

