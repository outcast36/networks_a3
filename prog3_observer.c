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
#include <poll.h>
#include <stdbool.h>
#include <errno.h>

int main( int argc, char **argv) {
	struct hostent *ptrh; /* pointer to a host table entry */
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad; /* structure to hold an IP address */
	int sd; /* socket descriptor */
	int port; /* protocol port number */
	char *host; /* pointer to host name */
	int n; /* number of characters read */
	char buf[1000]; /* buffer for data from the server */

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

	char acceptStatus;
	char username[12];
	uint8_t nameLen;
	time_t startTime, curTime;
	recv(sd, &acceptStatus, sizeof(char), 0);
	if (acceptStatus == 'N'){
		printf("Server full, closing connection.\n");
		close(sd);
		exit(EXIT_SUCCESS);
	}
	time(&startTime);
	while (1) {
		while (1){
			printf("Please enter the username of a participant: ");
			// might need to not use fgets since since null byte is automatically appending and we dont want to send it to the server
			fgets(username, 12, stdin); // at most 11 characters read in
			nameLen = strlen(username)-1;
			if(username[nameLen] == '\n') {
				username[nameLen] = '\0';
				nameLen = strlen(username);
			}
			time(&curTime);
			// The allowed seconds is set to greater than 11 here to avoid a false connection timeout due to a small time sync issue.
			// If it gets sent at 11 seconds, the server should still have already closed the connection so it is inconsequential if it tries to send anyways.
			if (curTime - startTime >= 10){ 
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
		if (recv(sd, &acceptStatus, sizeof(char), 0) < 0){
			// This code might never run, as the second attemped send/recv done to a closed connection will instead exit the program.
			printf("Server timed out, closing connection.\n");
			close(sd);
			exit(EXIT_SUCCESS);
		}
		//Handle responses from username sending.
		if (acceptStatus == 'T') {
			printf("An observer is already connected to that username.\n");
			time(&startTime); // Reset start time as the status T indicates a timer reset.
		} else if (acceptStatus == 'N') {
			printf("No participant has that username, closing connection.\n");
			close(sd);
			exit(EXIT_SUCCESS);
		} else if (acceptStatus == 'Y') {
			printf("Succesfully connected to the participant!\n");
			break;
		}
	}

	char message[1032];
	struct pollfd inputs[2]; // one fd is the server socket, the second is stdin
	uint16_t messageLen;

	inputs[0].fd = sd;
	inputs[0].events = POLLIN; // listen for read events from server socket
	inputs[1].fd = STDIN_FILENO;
	inputs[1].events = POLLIN;

	while(1){
		n = poll(inputs,2,-1); // poll socket and stdin endlessly
		if (n<0) {
			fprintf(stderr,"Error polling file descriptors in observer: %d\n", errno);
			exit(EXIT_FAILURE);
		}
		else {
			if (inputs[0].revents & POLLIN) {
				bool disconnectFromServer = (recv(inputs[0].fd,&n,1,MSG_DONTWAIT|MSG_PEEK)==0);
				if (disconnectFromServer) break;
				else {
					recv(sd, &messageLen, sizeof(uint16_t), 0);
					// revert to host byte order 
					messageLen = ntohs(messageLen);
					recv(sd, &message, messageLen, 0);
					//If the last character in the message is not the newline character
					if(message[messageLen - 1] != '\n'){
						//Add a new line character to the end of the message.
						message[messageLen] = '\n';
						message[messageLen + 1] = '\0';
					}
					printf("%s", message);
				}
			}
			if (inputs[1].revents & POLLIN) {
				read(STDIN_FILENO,message,6);
				message[5]='\0';
				if (strcmp(message, "/quit")==0) break;
			}
		}
	}
	close(sd);
	exit(EXIT_SUCCESS);
}

