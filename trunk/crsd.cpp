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
	fd_set master_fd;				// master file descriptor list
	fd_set clients_fd;				// add fd when a client joins, delete fd when a client terminate connection
	int num_members;	
	int port_number;
	int chat_fd;
	int fdmax;						// maximum file descriptor member
} ChatRoom;

/* GLOBAL VARIABLES */

char* room_name;				// name of the chatroom to create, join, delete
vector<ChatRoom*> chat_rooms;	// contains the available chatrooms
int server_fd, client_fd;       // listen on server_fd, new connection on client_fd


/*print_room: for debugging purposes*/

void print_room(ChatRoom* new_chat){
	printf("name: %s \n",new_chat->name);
	//printf("thread_id: %d \n",new_chat->thread_id);
	printf("num_members: %d \n",new_chat->num_members);	
	printf("port_number: %d \n", new_chat->port_number);
}

/*get_in_addr: get sockaddr, IPv4 or IPv6*/
void *get_in_addr(struct sockaddr *sa){
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
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

/*generate_port_number: generates a port number for the new chat room. It checks that this port number is not in used by other
						chatrooms or by the server. 
*/
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
			if((room->port_number != port_num) && (port_num != atoi(PORT)) && (port_num > 3000) && (port_num < 65500))	// CHECK!!!!!!!!!!!!!!!
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


/*handle_chat_room: called by thread that handles the new chatroom
                    reads and writes messages from the users/clients in the chatroom*/
void* handle_chat_room(void* room){
	printf("Handle Chat Room\n");
	
	struct addrinfo hints, *servinfo, *p;  
	int yes=1;
	int rv;
    socklen_t sin_size;
	struct sockaddr_storage client_addr;			// connector's address information
	char s[INET6_ADDRSTRLEN];

	//int fdmax;										// maximum file descriptor member
		
	//int chat_fd, member_fd;							// listening and new client's fd
	int member_fd;
	int numbytes;
	char msg[256];									// buffer for client data
	
	ChatRoom* chat = (ChatRoom*) room;
	
	char port[10];									// buffer for port number
	sprintf(port, "%d", chat->port_number);

	if (send(client_fd, "Chat room was created succesfully" , strlen("Chat room was created succesfully"), 0) == -1)      // send message to client
		perror("send");

	close(client_fd);								// close the client_fd
	
	FD_ZERO(&(chat->clients_fd));						// clears the fd_sets
	FD_ZERO(&(chat->master_fd));
	
	// open socket on new port number
	memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;			// use my IP

    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {			// converts host name or IP address into an struct servinfo
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);                           //////////////////////FIXXXXXXXX
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((chat->chat_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(chat->chat_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {// allows reusabilitity of local addresses
            perror("setsockopt");
            exit(1);                           //////////////////////FIXXXXXXXX
        }

        if (bind(chat->chat_fd, p->ai_addr, p->ai_addrlen) == -1) {			// binds socket to a local socket address
            close(chat->chat_fd);
            perror("server: bind");
            continue;
		}
        break;
	}

    if (p == NULL)  {
        fprintf(stderr, "server: failed to bind\n");
        exit(1);                           //////////////////////FIXXXXXXXX
    }

    freeaddrinfo(servinfo);						// done with this structure
	
	if (listen(chat->chat_fd, BACKLOG) == -1) {		// new socket can be accepted, chat_fd listens for connections
        perror("listen");
		close(chat->chat_fd);
        exit(1);                           //////////////////////FIXXXXXXXX
    }

	// adds chat_fd to master_fd
	FD_SET(chat->chat_fd,&(chat->master_fd));
	chat->fdmax = chat->chat_fd;					// keeps track of the biggest fd

	//keep track of the biggest fd

    while(1) {							// main accept() loop
		chat->clients_fd = chat->master_fd;

		if(select(chat->fdmax+1, &(chat->clients_fd), NULL, NULL, NULL) == -1){
			perror("select");
			exit(4);                 ////////////////////
		}

		// go through the fd for data to read
		for(int i = 0; i <= chat->fdmax; i++){
			if(FD_ISSET(i, &(chat->clients_fd))) {		// can read from this fd
				sin_size = sizeof client_addr;

				member_fd = accept(chat->chat_fd, (struct sockaddr *)&client_addr, &sin_size);   // gets new socket with a new incoming connection
				if (member_fd == -1) {
					perror("accept");
				}
				else{
					FD_SET(member_fd,  &(chat->master_fd));    // adds to master set
					if(member_fd > chat->fdmax){						// new max
						chat->fdmax = member_fd;
					}
					chat->num_members += 1;					// one new client connected
					printf("Chat room %s: new connection from %s on socket %d\n", chat->name, 
						                                                          inet_ntop(client_addr.ss_family, 
																				            get_in_addr((struct sockaddr*)&client_addr),
																							s, 
																							INET6_ADDRSTRLEN),
																							member_fd);
				}
			}
			else{
				// data from client
				if((numbytes = recv(i, msg, sizeof msg, 0)) <= 0) {
					//error or connection closed by user
					if(numbytes == 0){		 // connection closed
						chat->num_members -= 1;
						printf("Chat room %s: socket %d disconnected\n", chat->name, i);
					}
					else{
						perror("recv");
					}
					close(i);					// closes the socket
					FD_CLR(i, &(chat->master_fd)); // removes from master_fd
				}
				else{
					// data from client
					for(int j = 0; j <= chat->fdmax; j++){

						if(FD_ISSET(j, &(chat->master_fd))){		// send to everyone in the chat room except the  chat_fd and the sender
							if(j != (chat->chat_fd) && j != i){      /////////////FIX!!!!!!!!!!

								if(send(j, msg, numbytes,0) == -1){
									perror("send");
								}
							}
						}
					}
				}
			}
		}
	}
}
	

/*create_room: creates a chatroom if it does not exist already*/
int create_room(){
	printf("Create Room\n");
	for(int i = 0; i < chat_rooms.size(); i++){				// checks if there is another chatroom with the same name
		if(strcmp(chat_rooms[i]->name, room_name) == 0){
			return 0;
			//break;
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
}

/*join_room: adds client to a chat room if it exists*/
int join_room(){
	for(int i = 0; i < chat_rooms.size(); i++){				// checks if there is another chatroom with the same name
		ChatRoom* room = chat_rooms[i];
		
		if(strcmp(room->name, room_name) == 0){				// room was found
			char port[10];									// buffer for port number
			sprintf(port, "%d", room->port_number);

			char members[10];								// buffer for number of members
			sprintf(port, "%d", room->num_members);

			if (send(client_fd, port , strlen(port), 0) == -1)      // sends the port number to the client
				perror("send");

			if (send(client_fd, members , strlen(members), 0) == -1)      // sends the port number to the client
				perror("send");
			return 1;
			break;
		}
	}
	//close(client_fd);
	return 0;												// no room with that name was found
}


/*delete_room: deletes a chat room if it exists*/
int delete_room(){
	vector<ChatRoom*>::iterator i;
	char out_message[100];
	for(i = chat_rooms.begin(); i < chat_rooms.end(); i++){				// checks if there is another chatroom with the same name
		ChatRoom* room = (*i);
		if(strcmp(room->name, room_name) == 0){     // room was found
			
			for(int j = 0; j <= room->fdmax; j++){
				fd_set* temp_set = &(room->master_fd);
				if(FD_ISSET(j, temp_set)){		// send to everyone in the chat room except chat_fd
					if(j != room->chat_fd){
						out_message = "Chat room is being deleted, shutting down connection";
						if(send(j, out_msg, strlen(out_msg),0) == -1){
							perror("send");
						}
						close(j);							// close the fd
					}
				}
			}
			close(room->chat_fd);							// closes the chat_fd
			pthread_kill(room->thread_id, SIG_TERM);		// terminates the thread   USE MASKING!!!!!!!!!!!
			free(room);										// frees allocated memory for chat room
			chat_rooms.erase(i);							// deletes its pointer
			return 1;
			break;
		}
	}
	//close(client_fd);	
	return 0;												// no room with that name was found
}


int main(void) {
	struct addrinfo hints, *servinfo, *p;  
	int yes=1;
	int rv;
    socklen_t sin_size;
	struct sockaddr_storage client_addr;			// connector's address information
	char s[INET6_ADDRSTRLEN];

	int numbytes;									// number of bytes obtained from client's request
	char request[MAX_REQUEST_SIZE];					// buffer for the request sent by the client
	char msg_out[100];
	
	chat_rooms = vector<ChatRoom*>();				// initializes the chatroom list

	memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;			// use my IP

    if ((rv = getaddrinfo(NULL, PORT, &hints, &servinfo)) != 0) {			// converts host name or IP address into an struct servinfo
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return -1;
    }

    // loop through all the results and bind to the first we can
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((server_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) {
            perror("server: socket");
            continue;
        }

        if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) { // allows reusabilitity of local addresses
            perror("setsockopt");
            return -1;;
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
        return -1;
    }

    freeaddrinfo(servinfo);						// done with this structure
	
	if (listen(server_fd, BACKLOG) == -1) {		// new socket can be accepted
        perror("listen");
        return -1;
    }

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
				msg_out = "Could not create chat room";
				if (send(client_fd, msg_out, strlen(msg_out), 0) == -1)
					perror("send");                 // CLOSE FD???!!!!!!!!!!!!!!!!!!!!~~~~~~~~~
			}
			close(client_fd);
			break;

		case 2:										// JOIN REQUEST
			printf("JOIN REQUEST\n");
			if(join_room()){
				if(send(client_fd, room_name, strlen(room_name), 0) == -1)
					perror("send");
			}
			else{
				msg_out = "Could not join chat room";
				if (send(client_fd, msg_out , strlen(msg_out), 0) == -1)
					perror("send");
			}
			close(client_fd);
			break;

		case 3:										// DELETE
			printf("DELETE REQUEST\n");
			if(delete_room()){
				if (send(client_fd, room_name, strlen(room_name), 0) == -1)
					perror("send");
			}
			else{
				msg_out = "Could not delete chat room";
				if (send(client_fd, msg_out, strlen(msg_out), 0) == -1)
					perror("send");
			}
			close(client_fd);
			break;

		case 4: 									// INVALID REQUEST
			msg_out = "Invalid Request. Connection closed.";
			if (send(client_fd, msg_out, strlen(msg_out), 0) == -1){
				perror("send");
				close(client_fd);
			}
			break;
		}
	}
	close(server_fd);
    return 0;
}
