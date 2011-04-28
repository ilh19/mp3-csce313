#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <list>

using namespace std;  

#define PORT "3500"				// the port users will be connecting to

#define BACKLOG 10				// how many pending connections queue will hold

#define MAX_REQUEST_SIZE 100	// max number of bytes we can get at once for a request 

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

// Struct holding data of each chat room
typedef struct ChatRoom{
	char* name;
	int process_id;
	fd_set clients_fd;			// add fd when a client joins, delete fd when a client terminate connection
} ChatRoom;

int check_request(char* request, char* room_name){
	char* temp_name;
	printf("request: %s", request);
	
	if (strncmp("CREATE", request, 6) == 0){				// CREATE REQUEST
		temp_name = &(request[7]);							// skips white space
		printf("Room name: %s\n", temp_name);
		room_name = temp_name;								// copies the name of the room
		printf("CREATE REQUEST: room name: %s\n", room_name);
		return 1;
	}

	else {
		if (strncmp("JOIN", request, 4) == 0){					// JOIN REQUEST
			temp_name = &(request[5]);
			room_name = temp_name;								// copies the name of the room
			printf("JOIN REQUEST\n");
			return 2;
		}

		else{	
			if(strncmp("DELETE", request, 6) == 0){				// DELETE REQUEST
				temp_name = &(request[7]);
				room_name = temp_name;							  // copies the name of the room
				printf("DELETE REQUEST\n");
				return 3;
			}
			else{										// INVALID REQUEST
				printf("INVALID REQUEST\n");
				return 4;
			}
		}
	}
}

int main(void)
{
    int server_fd, client_fd;                // listen on server_fd, new connection on client_fd

    struct addrinfo hints, *servinfo, *p;
    struct sockaddr_storage client_addr;      // connector's address information
    socklen_t sin_size;

    struct sigaction sa;
    int yes=1;
    char s[INET6_ADDRSTRLEN];		
    int rv, numbytes;						// number of bytes obtained from client's request
	char request[MAX_REQUEST_SIZE];			// buffer for the request sent by the client
	//char* temp_request;					// stores the input in tokens
	char* room_name;						// name of the chatroom to create, join, delete

	list<ChatRoom> chat_room_list;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE; // use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {			// converts host name or IP address into an struct servinfo
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((server_fd = socket(p->ai_family, p->ai_socktype,
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes,		// allows reusabilitity of local addresses
                sizeof(int)) == -1) {
            perror("setsockopt");
            exit(1);
        }

        if (bind(server_fd, p->ai_addr, p->ai_addrlen) == -1) {			// binds socket to a local socket address
            close(server_fd);
            perror("server: bind");
            continue;
        }

        break;
    }

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return 2;
    }

    freeaddrinfo(servinfo);						// done with this structure

    if (listen(server_fd, BACKLOG) == -1) {		// new socket can be accepted
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler;			// takes care of dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }

    printf("Server: waiting for connections...\n");

    while(1) {  // main accept() loop
        sin_size = sizeof client_addr;
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &sin_size);   // gets new socket with a new incoming connection
        if (client_fd == -1) {
            perror("accept");
            continue;
        }

        inet_ntop(client_addr.ss_family,					// converts numeric addresses into a text string
            get_in_addr((struct sockaddr *)&client_addr),
            s, sizeof s);
        printf("server: got connection from %s\n", s);

		if ((numbytes = recv(client_fd, request, MAX_REQUEST_SIZE-1, 0)) == -1) {  // receives request
			perror("recv");
			exit(1);
		}
		request[numbytes] = '\0';

		printf("Request: %s\n", request);
		
		int type_request = check_request(request, room_name);

		printf("type_request: %d\n", type_request);

		switch(type_request){
		case 1:										// CREATE REQUEST
			printf("CREATE REQUEST\n");
			printf("room_name length: %d\n", strlen(room_name));
			if (send(client_fd, room_name, strlen(room_name), 0) == -1)
                perror("send");
			break;

		case 2:										// JOIN REQUEST
			printf("JOIN REQUEST\n");
			if (send(client_fd, room_name, strlen(room_name), 0) == -1)
				perror("send");
			break;

		case 3:										// DELETE
			printf("DELETE REQUEST\n");
			if (send(client_fd, room_name, strlen(room_name), 0) == -1)
				perror("send");
			break;

		case 4: 									// INVALID REQUEST
			char* error_msg = "Invalid Request. Connection closed.";
			if (send(client_fd, error_msg, strlen(error_msg), 0) == -1){
				perror("send");
				close(client_fd);
			}
			break;
		}
		

	   if (!fork()) {				// child process to handle the chat room if the chatroom does not exist already
            close(server_fd);		// child does not need the listener
            if (send(client_fd, "Hello, world!", 13, 0) == -1)
                perror("send");
            close(client_fd);
            exit(0);
        }
        close(client_fd);			// parent does not need this
	}
    return 0;
}