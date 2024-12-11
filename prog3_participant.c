/* demo_client.c - code for example client program that uses TCP */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <ctype.h>

int main( int argc, char **argv) {
	struct hostent *ptrh; /* pointer to a host table entry */
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad; /* structure to hold an IP address */
	char message[1002]; /* buffer for message input */
	char username[16];
	time_t startTime, curTime;
	char *host; /* pointer to host name */
	int sd; /* socket descriptor */
	int port; /* protocol port number */
	int n; /* number of characters read */
	uint16_t messageLen;
	char acceptStatus;
	uint8_t nameLen;

	memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
	sad.sin_family = AF_INET; /* set family to Internet */

	if( argc != 3 ) {
		fprintf(stderr,"Error: Wrong number of arguments\n");
		fprintf(stderr,"usage:\n");
		fprintf(stderr,"./client server_address server_port\n");
		exit(EXIT_FAILURE);
	}

	port = atoi(argv[2]); /* convert to binary */
	if (port > 0) /* test for legal value */
		sad.sin_port = htons((u_short)port);
	else {
		fprintf(stderr,"Error: bad port number %s\n",argv[2]);
		exit(EXIT_FAILURE);
	}

	host = argv[1]; /* if host argument specified */

	/* Convert host name to equivalent IP address and copy to sad. */
	ptrh = gethostbyname(host);
	if ( ptrh == NULL ) {
		fprintf(stderr,"Error: Invalid host: %s\n", host);
		exit(EXIT_FAILURE);
	}

	memcpy(&sad.sin_addr, ptrh->h_addr, ptrh->h_length);

	/* Map TCP transport protocol name to protocol number. */
	if ( ((long int)(ptrp = getprotobyname("tcp"))) == 0) {
		fprintf(stderr, "Error: Cannot map \"tcp\" to protocol number");
		exit(EXIT_FAILURE);
	}

	/* Create a socket. */
	sd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
	if (sd < 0) {
		fprintf(stderr, "Error: Socket creation failed\n");
		exit(EXIT_FAILURE);
	}

	/* Connect the socket to the specified server. */
	if (connect(sd, (struct sockaddr *)&sad, sizeof(sad)) < 0) {
		fprintf(stderr,"connect failed\n");
		exit(EXIT_FAILURE);
	}

	recv(sd, &acceptStatus, sizeof(char), 0);
	if (acceptStatus == 'N'){
		printf("Server full, closing connection.\n");
		close(sd);
		exit(EXIT_SUCCESS);
	}
	time(&startTime);
	while (1) {
		while (1){
			printf("Please enter a username: ");
			fgets(username, sizeof(username), stdin);
			nameLen = strlen(username)-1;
			if(username[nameLen] == '\n') {
				username[nameLen] = '\0';
				nameLen = strlen(username);
			}
			time(&curTime);
			// The allowed seconds is set to greater than 11 here to avoid a false connection timeout due to a small time sync issue.
			// If it gets sent at 11 seconds, the server should still have already closed the connection so it is inconsequential if it tries to send anyways.
			if (curTime - startTime > 11){ 
				printf("Server timed out, closing connection.\n");
				close(sd);
				exit(EXIT_SUCCESS);
			}
			if (nameLen > 10){
				printf("Username is too large, please ensure that the username is 10 characters or under.\n");
			} else if (nameLen < 1){
				printf("Username cannot be empty.\n");
			} else {
				break;
			}
		}
		send(sd, &nameLen, sizeof(uint8_t), 0);
		send(sd, &username, nameLen, 0); // If server time outs, program should stop here.
		if (recv(sd, &acceptStatus, sizeof(char), 0) == 0){
			// This code might never run, as I think that after the server times out, the second attemped send/recv done to the server will instead exit the program.
			printf("Server timed out, closing connection.\n");
			close(sd);
			exit(EXIT_SUCCESS);
		}
		//Handle responses from username sending.
		if (acceptStatus == 'T') {
			printf("Username already taken.\n");
			time(&startTime); // Reset start time as the status T indicates a timer reset.
		} else if (acceptStatus == 'I') {
			printf("Username invalid, please ensure that only letters, numbers, and/or underscores are used.\n");
		} else if (acceptStatus == 'Y') {
			printf("Succesfully connected to server!\n");
			break;
		}
	}

	while(1){
		memset(message,0,1002);
		printf("Enter message: ");
		fgets(message, 1002, stdin); // enforce 1000 characters in to save room for \n and \0
		messageLen = strlen(message);
		if(message[messageLen-1] == '\n') message[messageLen-1] = '\0';
		messageLen = strlen(message);
		// compare to 1001 to account for 1000 chars (valid) + fgets newline
		if (strcmp(message, "/quit")==0 || messageLen > 1000) { 
			close(sd);
			exit(EXIT_SUCCESS);
		}

		// Checks to make sure that a non whitespace character exists in the message, skipping the message if not.
		for (int i = 0; i < messageLen; i++){
			if (!isspace(message[i])){
				if (message[messageLen-1]=='\n') message[messageLen-1] = '\0'; 
				messageLen = htons(messageLen);
				send(sd, &messageLen, sizeof(uint16_t), 0);
				messageLen = strlen(message);
				send(sd, &message, messageLen, 0);
				break;
			}
		}

	}
}

