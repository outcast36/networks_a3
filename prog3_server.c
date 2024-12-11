#include <sys/types.h> 
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <poll.h>
#include <errno.h>
#include <time.h>
#include <stdbool.h>
#include <ctype.h>

#define QLEN 6 /* size of request queue */

// Global server state variables
int observerSD, participantSD;
int participants = 0, observers = 0;
int numSock = 0;
char yes='Y', no='N', invalid = 'I', timeReset = 'T';

typedef struct client client;
struct client {
	//struct pollfd* pfd; // use poll wrapper so mode and sd are contained
	char name[11]; // name should be "" if client is newly connected 
	int type; // 0 if participant, 1 if observer
	time_t startTime; // If the client is new, this is the time it started connection with the server. Gets set to -1 upon getting username.
	int head; // Queued message index.
};

int initializeSocket(struct protoent *ptrp, struct sockaddr_in *sad, struct sockaddr_in *cad, int portNumber) {
	int sd = -1, optval=1;
	if (0<portNumber && portNumber<65536) { /* test for illegal value */
		sad->sin_port = htons((u_short)portNumber);
	} else { /* print error message and exit */
		fprintf(stderr,"Error: Bad port number");
		exit(EXIT_FAILURE);
	}
	
	/* Create a socket */
	sd = socket(PF_INET, SOCK_STREAM, ptrp->p_proto);
	if (sd < 0) {
		fprintf(stderr, "Error: Socket creation failed\n");
		exit(EXIT_FAILURE);
	}

	/* Allow reuse of port - avoid "Bind failed" issues */
	if( setsockopt(sd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0 ) {
		fprintf(stderr, "Error Setting socket option failed\n");
		exit(EXIT_FAILURE);
	}

	/* Bind a local address to the socket */
	if (bind(sd, (struct sockaddr *)sad, sizeof(*sad)) < 0) {
		printf("%d\n", errno);
		fprintf(stderr,"Error: Bind failed\n");
		exit(EXIT_FAILURE);
	}

	/* Specify size of request queue */
	if (listen(sd, QLEN) < 0) {
		fprintf(stderr,"Error: Listen failed\n");
		exit(EXIT_FAILURE);
	}
	return sd;
}

client* newClient(struct pollfd* socks, int capacity, int sd_server) {
	struct sockaddr_in cad;
	int sd_client;
	socklen_t alen = sizeof(struct sockaddr_in);
	if ((sd_client = accept(sd_server,(struct sockaddr *) &cad, &alen)) < 0) {
		fprintf(stderr, "Error: Accept new client failed\n");
		printf("ERRNO: %d\n", errno);
		exit(EXIT_FAILURE);
	}
	if (capacity == 255) {
		send(sd_client, &no, sizeof(char), 0); 
		close(sd_client);
		return NULL;
	} 
	send(sd_client, &yes, sizeof(char), 0);
	socks[numSock].fd = sd_client;
	socks[numSock].fd = sd_client;
	socks[numSock].events = POLLIN | POLLOUT;
	client *c = (client*) malloc(sizeof(client)); // SCARY 
	numSock++;
	memset(c->name,'\0',11); 
	c->type = (sd_server != participantSD);
	c->startTime = time(NULL);
	return c;
}

// Prepend 14 characters to start of message
void messagePrepend(char* msg, char* name) {
	char public='>', private='-', at='@', space=' ';
	uint16_t msgLen = strlen(msg);
	uint8_t nameLen = strlen(name), spaces = 11 - nameLen;
	uint8_t colonIdx = spaces+1+nameLen;
	bool privateMessage = (msg[0]==at);
	for (int i=msgLen;i>=0;i--) msg[i+14] = msg[i];
	if (privateMessage) msg[0]=private;
	else msg[0]=public;
	for (int i=1;i<=spaces;i++) msg[i]=space;
	for (int i=0;i<nameLen;i++) msg[spaces+1+i] = name[i];
	msg[colonIdx] = ':';
	msg[colonIdx+1] = space;
	return;
}

// Linear search through clients array to find client matching key (name, type=<0,1>)
int findClientByName(client** clients, const char* name, int type) {
	for (int j=0;j<observers+participants;j++){
		if (clients[j]->type == type && strcmp(clients[j]->name, name) == 0){
			return j;
		}
	}
	return -1; // participant with $name or observer affiliated with $name not found
}

bool validNameChar(char c) {
	return isalnum(c) || c == '_';
}

// Assumes that char array name is null terminated, otherwise match string to:
// ^(a-zA-Z0-9_)\1{1-10}$
bool validateName(const char* name) {
	int n = strlen(name);
	if (n>10) return false;
	for (int i=0;i<n;i++) {
		if(!validNameChar(name[i])) return false;
	}
	return true;
}

int main(int argc, char **argv) {
	client *allClients[510];
	struct protoent *ptrp; /* pointer to a protocol table entry */
	struct sockaddr_in sad; /* structure to hold server's address */
	struct sockaddr_in pad, oad; /* structure to hold client's address */
	struct pollfd allSockets[512]; /* Array of all sockets */
	int MSG_Q_LEN = 1024; /* size of server message queue */
	char *msg_queue[MSG_Q_LEN]; /* queue to store messages*/
	char buf[1032]={0}; /* client message buffer */
	int sd_client; /* variable to handle new connections */
	int alen; /* length of address */
	int optval = 1; /* boolean value when we set socket option */
	int n = 0; /* store poll results */
	int head=0, tail=0;
	bool quit=false;
	uint8_t msg_len;
	
	if( argc != 3 ) {
		fprintf(stderr,"Error: Wrong number of arguments\n");
		fprintf(stderr,"usage:\n");
		fprintf(stderr,"./server participant_port observer_port\n");
		exit(EXIT_FAILURE);
	}

	memset((char *)&sad,0,sizeof(sad)); /* clear sockaddr structure */
	sad.sin_family = AF_INET; /* set family to Internet */
	sad.sin_addr.s_addr = INADDR_ANY; /* set the local IP address */
	int participantPort = atoi(argv[1]); /* convert argument to binary */
	int observerPort = atoi(argv[2]); /* convert argument to binary */

	/* Map TCP transport protocol name to protocol number */
	if ( ((long int)(ptrp = getprotobyname("tcp"))) == 0) {
		fprintf(stderr, "Error: Cannot map \"tcp\" to protocol number");
		exit(EXIT_FAILURE);
	}

	// Create two socket descriptors that are in the listening state so clients can connect
	participantSD = initializeSocket(ptrp, &sad, &pad, participantPort);
	observerSD = initializeSocket(ptrp, &sad, &oad, observerPort);
	
	memset((struct pollfd*) allSockets,0,sizeof(struct pollfd)*513);
	allSockets[numSock].fd = participantSD;
	allSockets[numSock++].events = POLLIN;
	allSockets[numSock].fd = observerSD;
	allSockets[numSock++].events = POLLIN;
	allSockets[numSock].fd = STDIN_FILENO;
	allSockets[numSock++].events = POLLIN;
	
	printf("Type '/quit' to cleanly exit the server. Cheers!\n");

	while (1) { // always have two listening sockets, CTRL-C/Z to kill server
		n = poll(allSockets, numSock, -1);
		switch (n) {
			case -1:
				fprintf(stderr, "Error: Polling sockets failed: %d\n", errno);
			default: 
				// check for activity on listening sockets
				if (allSockets[0].revents & POLLIN){
					allClients[observers+participants] = newClient(allSockets,participants,participantSD);
					participants++;
				} 
				if (allSockets[1].revents & POLLIN) {
					allClients[observers+participants] = newClient(allSockets,observers,observerSD);
					observers++;
				}
				if (allSockets[2].revents & POLLIN) {
					read(STDIN_FILENO,buf,6);
					buf[5]='\0';
					bool quit = (strcmp(buf, "/quit")==0);
					if (quit) {
						int size = observers+participants;
						for (int i=0;i<size;i++) {
							free(allClients[i]);
							close(allSockets[i+3].fd);
						}
						close(participantSD);
						close(observerSD);
						exit(EXIT_SUCCESS);
					}
				}
				// check all client sockets 
				for (int i=3;i<numSock;i++) {
					client *curClient = allClients[i-3];
					int disconnectFlags = (MSG_DONTWAIT | MSG_PEEK);
					bool disconnected = (recv(allSockets[i].fd,buf,1,disconnectFlags) == 0);
					bool nameExist = (strcmp(curClient->name, "") == 0);
					bool timeoutNaming = (time(NULL) - curClient->startTime >= 10);
					bool expireNewClient = (nameExist && timeoutNaming);
					bool isParticipant = (curClient->type==0);
					bool isObserver = !isParticipant;
					bool validName = false;
					bool takenName;
					bool takenParticipant;
					bool foundPar;
					bool affiliated;

					// Disconnect handler
					if (disconnected || expireNewClient) {	
						close(allSockets[i].fd);
						numSock--;
						if (i!=numSock) allSockets[i] = allSockets[numSock]; 
						if (isParticipant){ 
							if (!nameExist) { // send all observers a message saying "User ${username} has left"
								//generateMessage()
								char msg[32] = "User ";
								strcat(msg, curClient->name);
								strcat(msg, " has left");
								msg_queue[tail]=msg;
								tail = (tail+1)%MSG_Q_LEN;
							}
							int observerIdx = findClientByName(allClients, curClient->name,1);
							if (observerIdx>=0) {
								free(allClients[observerIdx]);
								close(allSockets[observerIdx+3].fd);
								allSockets[observerIdx+3] = allSockets[--numSock]; 
								observers--; // force observer DC
								allClients[observerIdx] = allClients[observers + participants];
							}
							participants--;
						} 
						else observers--;
						free(curClient);
						allClients[i - 3] = allClients[observers + participants];
						continue;
					}

					// Input Handler
					else if (allSockets[i].revents & POLLIN) {
						// New user username negotiation.
						if (nameExist) {// if read event is from new client trying to get username
							n = recv(allSockets[i].fd,&msg_len,sizeof(uint8_t),0);
							n = recv(allSockets[i].fd,buf,msg_len, 0); // NULL byte is being sent by client
							buf[msg_len]='\0';
							foundPar = (findClientByName(allClients, buf, 0)>=0); // search for participants with that name
							validName = validateName(buf);
							takenName = (validName && isParticipant && foundPar);
							takenParticipant = (findClientByName(allClients, buf, 1)>=0); // search for observer affiliated on that name
							affiliated = (isObserver && foundPar && takenParticipant);

							if (isObserver && !foundPar) { // no matching participant for observer -- DISCONNECT
								printf("No matching participant with that name. DISCONNECTING\n");
								send(allSockets[i].fd, &no, sizeof(char), 0); // send N
								close(allSockets[i].fd);
								allSockets[i] = allSockets[--numSock];
								observers--;
								free(curClient);
								allClients[i - 3] = allClients[observers + participants];
							} 

							else if (takenName || affiliated) { // reset client negotiation/affiliation timer
								printf("Name is already taken or affiliated with\n");
								send(allSockets[i].fd, &timeReset, sizeof(char), 0); // send T
								curClient->startTime = time(NULL);
							}

							else if (validName) { //Valid name gotten.
								strncpy(curClient->name, buf, msg_len);
								curClient->startTime = -1;	
								send(allSockets[i].fd, &yes, sizeof(char), 0); // send Y
								if (isObserver){ //New observer joining
									curClient->head = tail;
									allSockets[i].events = POLLOUT;
									char msg[32] = "A new observer has joined";
									msg_queue[tail]=msg;
									tail = (tail+1)%MSG_Q_LEN;
								} 
								else{ //New participant joining.
									allSockets[i].events = POLLIN;
									char msg[32] = "User ";
									strcat(msg, curClient->name);
									strcat(msg, " has joined");
									msg_queue[tail]=msg;
									tail = (tail+1)%MSG_Q_LEN;
								}
							}
							
							else if (isParticipant) { // Invalid name for participant, do not reset timer.
								send(allSockets[i].fd, &invalid, sizeof(char), 0); // send I		
							}										
						} 

						// else condition: (!nameExist == (strcmp(curClient->name, "") != 0)) 
						// curClient->name is a non-empty string, meaning a current user sent the message.
						// Adds to the msg_queue and increments the end of the queue.
						// The queue has a length declared in MSG_Q_LEN
						else { // receive the standard participant message, and add the prepend as well.
							uint16_t par_msg_len;
							recv(allSockets[i].fd, &par_msg_len, sizeof(uint16_t),0);
							par_msg_len = ntohs(par_msg_len);
							n = recv(allSockets[i].fd, buf, par_msg_len,0);
							if (n<0) {
								printf("FAILED RECV\n");
								exit(EXIT_FAILURE);
							}
							buf[par_msg_len] = '\0';
							messagePrepend(buf, curClient->name);
							msg_queue[tail] = buf;
							tail = (tail+1)%MSG_Q_LEN;
						}
					}

					// Output handler, send to available observers.
					// Each observer has a queue index that is incremented at the end of this function.
					// The observer recieves the msg at it's queue index.
					else if ((allSockets[i].revents & POLLOUT) && isObserver && (!nameExist) && (curClient->head!=tail)) {
						// send a uint16_t in network standard byte order indicating the length of the string
						uint16_t std_msg_len = htons(strlen(msg_queue[curClient->head]));

						//Private message handler, send only to sender and recipient.
						if(msg_queue[curClient->head][0] == '-'){
							printf("Private message\n");
							int start = 12 - strlen(curClient->name);
							char copiedName[10];

							//This copies the name of the sender to check if they're the same as the current client.
							strncpy(copiedName, (msg_queue[curClient->head]) + start, strlen(curClient->name));
							if((strcmp(copiedName, curClient->name) == 0) && (msg_queue[curClient->head][start - 1] == ' ')){ //If sender name equals the reciever name.
								printf("Found sender\n");
								int recvNameLen = 0;

								//Sees how many letters are in the reciever name.
								while (true){
									if (isspace(msg_queue[curClient->head][15 + recvNameLen]) || msg_queue[curClient->head][15+recvNameLen] == '\0'){
										break;
									}
									recvNameLen++;
								}
								
								//Checks if the recipient exists.
								if (recvNameLen > 10){

									// Username too long, send error to sender.
									printf("Private Message Username too long\n");
									char errorMessage[40] = "Warning:  Username entered is too long.";
									std_msg_len = htons(strlen(errorMessage));
									send(allSockets[i].fd, &std_msg_len, sizeof(uint16_t),0);
									std_msg_len = strlen(errorMessage);
									send(allSockets[i].fd, errorMessage, std_msg_len,0);
								} else {
									strncpy(copiedName, (msg_queue[curClient->head]) + 15, recvNameLen);
									if (findClientByName(allClients, copiedName, 0) == -1) {
										
										//Recipient doesn't exist, send error to sender.
										printf("Reciever doesn't exist\n");
										char errorMessage[40] = "Warning:  user ";
										strcat(errorMessage, copiedName);
										strcat(errorMessage, " doesn't exist...");
										std_msg_len = htons(strlen(errorMessage));
										send(allSockets[i].fd, &std_msg_len, sizeof(uint16_t),0);
										std_msg_len = strlen(errorMessage);
										send(allSockets[i].fd, errorMessage, std_msg_len,0);
									} else {

										//Recipient does exist, send message to sender.
										printf("Reciever exists\n");
										send(allSockets[i].fd, &std_msg_len, sizeof(uint16_t),0);
										std_msg_len = strlen(msg_queue[curClient->head]);
										send(allSockets[i].fd, msg_queue[curClient->head], std_msg_len,0);
									}
								}

							} else {

								//This copies the name of the recipient to check if they're the same as the current client.
								strncpy(copiedName, (msg_queue[curClient->head]) + 15, strlen(curClient->name));
								if((strcmp(copiedName, curClient->name) == 0) &&
										(isspace(msg_queue[curClient->head][15 + strlen(curClient->name)]) || (msg_queue[curClient->head][15+strlen(curClient->name)] == '\0'))){
									
									// Current client is the reciever, so send them the message.
									printf("Found reciever\n");
									
									send(allSockets[i].fd, &std_msg_len, sizeof(uint16_t),0);
									std_msg_len = strlen(msg_queue[curClient->head]);
									send(allSockets[i].fd, msg_queue[curClient->head], std_msg_len,0);
								}
								// If client doesn't go into either if statement, it wasn't the sender or recipient, and should just skip sending the message.
							}

						} else {
							//Public message, send to every observer.
							send(allSockets[i].fd, &std_msg_len, sizeof(uint16_t),0);
							std_msg_len = strlen(msg_queue[curClient->head]);
							send(allSockets[i].fd, msg_queue[curClient->head], std_msg_len,0);
						}

						// Advance through message queue in current observer; i.e. msg_queue.dequeue().
						curClient->head = (curClient->head+1)%MSG_Q_LEN;
					}
				}
				break;
		}
	}
}

