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
#include <vector>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>

using namespace std;  

#define PORT "3500"				// the port users will be connecting to server

#define BACKLOG 10				// how many pending connections queue will hold

#define MAX_REQUEST_SIZE 100	// max number of bytes we can get at once for a request 


/* Struct holding data of each chat room */
typedef struct ChatRoom{
	char* name;
	pthread_t thread_id;
	fd_set clients_fd;			// add fd when a client joins, delete fd when a client terminate connection
	int num_members;	
	int port_number;
} ChatRoom;

/* GLOBAL VARIABLES */

char* room_name;				// name of the chatroom to create, join, delete
vector<ChatRoom*> chat_rooms;	// contains the available chatrooms
int server_fd, client_fd;       // listen on server_fd, new connection on client_fd


/*sigchld_handler*/
/*void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}*/
/*print_room: for debugging purposes*/

void print_room(ChatRoom* new_chat){
	printf("name: %s \n",new_chat->name);
	//printf("thread_id: %d \n",new_chat->thread_id);
	printf("num_members: %d \n",new_chat->num_members);	
	printf("port_number: %d \n", new_chat->port_number);
}

/*get_in_addr: get sockaddr, IPv4 or IPv6*/
void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*setup_connection: creates a socket on a port, binds and listens*/
int setup_connection(int socket_fd, char* port){
	struct addrinfo hints, *servinfo, *p;
       
    int yes=1;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;			// use my IP

    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {			// converts host name or IP address into an struct servinfo
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((socket_fd = socket(p->ai_family, p->ai_socktype, 
                p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &yes,		// allows reusabilitity of local addresses
                sizeof(int)) == -1) {
            perror("setsockopt");
            return -1;;
        }

        if (bind(socket_fd, p->ai_addr, p->ai_addrlen) == -1) {			// binds socket to a local socket address
            close(socket_fd);
            perror("server: bind");
            continue;
        }

        break;
    }

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        return -1;
    }

    freeaddrinfo(servinfo);						// done with this structure

    if (listen(socket_fd, BACKLOG) == -1) {		// new socket can be accepted
        perror("listen");
        return -1;
    }
	return 0;									// no errors and set up was sucessful
}

/* check_request: determines which request was obtained by the server */
int check_request(char* request){
	//printf("request: %s", request);
	
	if (strncmp("CREATE", request, 6) == 0){				// CREATE REQUEST
		room_name =  &(request[7]);								// skips white space copies the name of the room
		printf("CREATE REQUEST\n");
		return 1;
	}

	else {
		if (strncmp("JOIN", request, 4) == 0){				// JOIN REQUEST
			room_name = &(request[5]);								// copies the name of the room
			printf("JOIN REQUEST\n");
			return 2;
		}

		else{	
			if(strncmp("DELETE", request, 6) == 0){			// DELETE REQUEST
				room_name = &(request[7]);							// copies the name of the room
				printf("DELETE REQUEST\n");
				return 3;
			}
			else{											// INVALID REQUEST
				printf("INVALID REQUEST\n");
				return 4;
			}
		}
	}
}

int generate_port_number(){
	int used = 0;
	int port_num = -1;
	while(!used){
		srand (time(NULL));
		port_num = rand() % 21 + 7985;
		
		for(int i = 0; i < chat_rooms.size(); i++){				// checks if there is another chatroom with the same name
			ChatRoom* room = chat_rooms[i];
			printf("PORT NUMBER: %d", port_num);
			// port number is already in use by a chat room or the server itself
			if((room->port_number != port_num) && (port_num != atoi(PORT)) && (port_num > 3000) && (port_num < 65500))	
				used = 1;
		}
		// empty vector of end of the list
		if((port_num > 3000) && (port_num < 65500)){
			used = 1;  
		}
	}
	printf("PORT NUMBER: %d", port_num);
	return port_num;
}


/*handle_chat_room: reads and writes messages from the users/clients in the chatroom*/
void* handle_chat_room(void* room){
	printf("Handle Chat Room\n");
	int chat_fd;
	
	ChatRoom* chat = (ChatRoom*) room;
	
	char port[10];					// buffer for port number
	sprintf(port, "%d", chat->port_number);

	chat->port_number = generate_port_number();
	if (send(client_fd, port , strlen(port), 0) == -1)      // sends the port number to the client
		perror("send");

	close(client_fd);										// close the client_fd

	if(setup_connection(chat_fd, port) == -1)						// initializes the chat's socket
		perror("setting up socket for chat room");

}

/*create_room: creates a chatroom if it does not exist already*/
int create_room(){
	printf("Create Room\n");
	for(int i = 0; i < chat_rooms.size(); i++){				// checks if there is another chatroom with the same name
		if(strcmp(chat_rooms[i]->name, room_name) == 0){
			return 0;
			break;
		}
	}
	//printf("Set Thread\n");
	// set up to create a new thread to handle the chat room;
	pthread_t thread_id;
	pthread_attr_t attributes;
	pthread_attr_init(&attributes);
	pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
	
	// create a new room struct
	ChatRoom* new_chat = (ChatRoom*)malloc(sizeof(ChatRoom));
	
	new_chat->name = room_name;
	new_chat->num_members = 0; 
	new_chat->port_number = generate_port_number();      // obtains a random port number
	
	chat_rooms.push_back(new_chat);  

	//print_room(new_chat);
	
	printf("Creating a pthread\n");	
	//creates a thread to handle this room
	if (pthread_create(&(new_chat->thread_id), NULL, handle_chat_room, (void *)new_chat)){ //if not 0, error ocurred
         perror("ERROR CREATING THREAD\n");
         return 0;
     }
	return 1;
}

/*join_room: adds client to a chat room if it exists*/
int join_room(int client_fd){
	for(int i = 0; i < chat_rooms.size(); i++){				// checks if there is another chatroom with the same name
		ChatRoom* room = chat_rooms[i];
		
		if(strcmp(room->name, room_name) == 0){     // room was found
			room->num_members += 1;
			FD_SET(client_fd, &room->clients_fd);			// adds new client_fd to the chatroom
			return 1;
			break;
		}
	}
	close(client_fd);
	return 0;												// no room with that name was found
}

/*delete_room: deletes a chat room if it exists*/
int delete_room(){
	vector<ChatRoom*>::iterator i;
	for(i = chat_rooms.begin(); i < chat_rooms.end(); i++){				// checks if there is another chatroom with the same name
		ChatRoom* room = (*i);
		if(strcmp(room->name, room_name) == 0){     // room was found
			//FD_SET(client_fd, &room.clients_fd);			
			// close all fd and send a message to all of them saying connection is being closed
			// terminate the process or thread
			chat_rooms.erase(i);
			return 1;
			break;
		}
	}
	close(client_fd);	////////OJOOOOO!!
	////////FREE MEMORY of chatroom struct!!
	return 0;												// no room with that name was found
}

int main(void)
{
    socklen_t sin_size;
	struct sockaddr_storage client_addr;			// connector's address information
	char s[INET6_ADDRSTRLEN];

	int numbytes;									// number of bytes obtained from client's request
	char request[MAX_REQUEST_SIZE];					// buffer for the request sent by the client
	
	chat_rooms = vector<ChatRoom*>();				// initializes the chatroom list
/*
    sa.sa_handler = sigchld_handler;			// takes care of dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }*/
	if(setup_connection(server_fd, PORT) == -1)		// initializes the server's socket
		perror("setting up socket for server");

    printf("Server: waiting for connections from client...\n");

    while(1) {							// main accept() loop
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
		request[numbytes] = '\0';			// add termination char 
		
		printf("Request: %s\n", request);
		
		int type_request = check_request(request);		// gets the type of request: CREATE, JOIN, DELETE

		printf("type_request: %d\n", type_request);

		switch(type_request){
		case 1:										// CREATE REQUEST
			printf("CREATE REQUEST\n");
			if(create_room()){						// room was created succesfully
				if (send(client_fd, room_name, strlen(room_name), 0) == -1)
					perror("send");
			}
			else{									// room was not created
				if (send(client_fd, "Could not create chat room", strlen("Could not create chat room"), 0) == -1)
					perror("send");                 // CLOSE FD???!!!!!!!!!!!!!!!!!!!!~~~~~~~~~
			}
			break;

		case 2:										// JOIN REQUEST
			printf("JOIN REQUEST\n");
			if(join_room(client_fd)){
				if (send(client_fd, room_name, strlen(room_name), 0) == -1)
					perror("send");
			}
			else{
				if (send(client_fd, "Could not join chat room", strlen("Could not join chat room"), 0) == -1)
					perror("send");
			}
			break;

		case 3:										// DELETE
			printf("DELETE REQUEST\n");
			if(delete_room()){
				if (send(client_fd, room_name, strlen(room_name), 0) == -1)
					perror("send");
			}
			else{
				if (send(client_fd, "Could not delete chat room", strlen("Could not join chat room"), 0) == -1)
					perror("send");
			}
			break;

		case 4: 									// INVALID REQUEST
			char* error_msg = "Invalid Request. Connection closed.";
			if (send(client_fd, error_msg, strlen(error_msg), 0) == -1){
				perror("send");
				close(client_fd);
			}
			break;
		}
	}
    return 0;
}
