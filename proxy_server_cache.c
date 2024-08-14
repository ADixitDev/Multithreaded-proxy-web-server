#include "proxy_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/wait.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>
#include <time.h>
#define MAX_SIZE 200*(1<<20)     //size of the cache
#define MAX_ELEMENT_SIZE 10*(1<<20)     //max size of an element in cache
#define Maxclients 400   //max client can connnect conncurrently 
#define MAX_BYTES 4096    //max allowed size of request/response
// Lru cache Element
struct cache_element{
char *data ;
int len ;
char *url ; 
time_t lru_time_track;
struct cache_element *next ;
}

cache_element *find(char *url);
int add_cache_element(char *data , int size , char *url) ;
void remove_cache_element() ;

int port = 8080 ; // defult port i give .
// proxy socket id ek ho rahegi saari request issi socket se jayegi 
//and every client request make different thread and there id and wahi se return kar degi.

int proxy_socketId ; 
pthread_t tid[Maxclients] ;// array make to store thread id of clients
sem_t semaphore ; // //if client requests exceeds the max_clients this seamaphore puts the      //waiting threads to sleep and wakes them when traffic on queue decreases
pthread_mutex_t lock ; // lock is used for locking the cache

cache_element *head ; // pointer to the cache 
int cache_size ;// current size of cache 
// create the socket and then check the ki kahi humar local host mein map toh  nhi hai by 
//  if not we have to do bzero serevrAddr , set the family ,port & co&&ect the server with co&&cet()

int sendErrorMessage(int socket, int status_code)
{
	char str[1024];
	char currentTime[50];
	time_t now = time(0);

	struct tm data = *gmtime(&now);
	strftime(currentTime,sizeof(currentTime),"%a, %d %b %Y %H:%M:%S %Z", &data);

	switch(status_code)
	{
		case 400: snprintf(str, sizeof(str), "HTTP/1.1 400 Bad Request\r\nContent-Length: 95\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\n<BODY><H1>400 Bad Rqeuest</H1>\n</BODY></HTML>", currentTime);
				  printf("400 Bad Request\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 403: snprintf(str, sizeof(str), "HTTP/1.1 403 Forbidden\r\nContent-Length: 112\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\n<BODY><H1>403 Forbidden</H1><br>Permission Denied\n</BODY></HTML>", currentTime);
				  printf("403 Forbidden\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 404: snprintf(str, sizeof(str), "HTTP/1.1 404 Not Found\r\nContent-Length: 91\r\nContent-Type: text/html\r\nConnection: keep-alive\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\n<BODY><H1>404 Not Found</H1>\n</BODY></HTML>", currentTime);
				  printf("404 Not Found\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 500: snprintf(str, sizeof(str), "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 115\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\n<BODY><H1>500 Internal Server Error</H1>\n</BODY></HTML>", currentTime);
				  //printf("500 Internal Server Error\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 501: snprintf(str, sizeof(str), "HTTP/1.1 501 Not Implemented\r\nContent-Length: 103\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>404 Not Implemented</TITLE></HEAD>\n<BODY><H1>501 Not Implemented</H1>\n</BODY></HTML>", currentTime);
				  printf("501 Not Implemented\n");
				  send(socket, str, strlen(str), 0);
				  break;

		case 505: snprintf(str, sizeof(str), "HTTP/1.1 505 HTTP Version Not Supported\r\nContent-Length: 125\r\nConnection: keep-alive\r\nContent-Type: text/html\r\nDate: %s\r\nServer: VaibhavN/14785\r\n\r\n<HTML><HEAD><TITLE>505 HTTP Version Not Supported</TITLE></HEAD>\n<BODY><H1>505 HTTP Version Not Supported</H1>\n</BODY></HTML>", currentTime);
				  printf("505 HTTP Version Not Supported\n");
				  send(socket, str, strlen(str), 0);
				  break;

		default:  return -1;

	}
	return 1;
}
int ConnectWith_RemoteServer (int host_addr , int port_num){
  int remoteSocket = socket(AF_INET , SOCK_STREAM , 0) ;
  if(remoteSocket<0){
    printf("Error in Creating Socket.\n");
		return -1;
  }

  // check the host in local host ki  map toh nhi hai by gethostbyname()

     struct hostent *host = gethostbyname(host_addr) ;

     if(host==NULL){
        fprintf(stderr, "No such host exists.\n");	
		return -1;
     }

     // inserts ip address and port number of host in struct `server_addr`
	struct sockaddr_in server_addr;

	bzero((char*)&server_addr, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_num);

	bcopy((char *)host->h_addr,(char *)&server_addr.sin_addr.s_addr,host->h_length);

	// Connect to Remote server ----------------------------------------------------

	if( connect(remoteSocket, (struct sockaddr*)&server_addr, (socklen_t)sizeof(server_addr)) < 0 )
	{
		fprintf(stderr, "Error in connecting !\n"); 
		return -1;
	}
	// free(host_addr);
	return remoteSocket;
    
}
// tempReq is store the hhtp request

// We concatentation  the string and See the parser to set and get key value if got Null , 
// Then do unparse if got <0 then we have to do connect with remotServer then after that
// we send the data from client to remote server .
// and remote server will recv and then for loop to temp store the buff which is http request 
// outerwhile loop (send to client from remoteServer , and  recieve at remoteServer) 
//  
int handle_request(int clientSocketId , ParsedRequest * request ,  char *tempReq){
  //"GET /index.html HTTP/1.1\r\n"
  // we try to string concatenation 
  char *buf = (char*)malloc(sizeof(char)*MAX_BYTES);
  strcpy(buf , "GET") ;
  strcat(buf , request->path) ; 
  strcat(buf , " ") ; 
  strcat(buf , request->method) ; 
  strcat(buf , "\r\n") ;

  // Then we try to set and get the key value pair 
  size_t len = strlen(buf);

	if (ParsedHeader_set(request, "Connection", "close") < 0){
		printf("set header key not work\n");
	}

	if(ParsedHeader_get(request, "Host") == NULL)
	{
		if(ParsedHeader_set(request, "Host", request->host) < 0){
			printf("Set \"Host\" header key not working\n");
		}
	}

	if (ParsedRequest_unparse_headers(request, buf + len, (size_t)MAX_BYTES - len) < 0) {
		printf("unparse failed\n");
		//return -1;				// If this happens Still try to send request without header
	}

    int server_port = 80 // by default 80 pe hi chalta 

    if(request->port!=NULL){
     server_port = atoi(request->port) ;
    }

    // Now we have to go to remote server with the ip address and host ;

    int remote_socketID = ConnectWith_RemoteServer(request->host , server_port );

    if(remote_socketID < 0) // sorry kuch nhi ho sakta :p
		return -1;

        // Now we have  send bytes to remote server from client as a response and add in cache .
     
     int bytes_send = send(remote_socketID , buf , strlen(buf) , 0) ;
     bzero(buf , Max_BYTES);
     // recieve  at remote_server 
     bytes_send = recv(remote_socketID , buf , Max_BYTES-1 , 0) ; 

     // create temp_buffer  in heap 
     char *temp_buffer  = (char*)malloc(sizeof(char)*MAX_BYTES); //temp -> buffer
     int temp_buffer_size = MAX_BYTES;
	 int temp_buffer_index = 0 ;

     while(bytes_send>0){
        
        bytes_send = send (clientSocketId  ,buf , size_t(buf) ) ;

        for(int i = 0 ; i<temp_buffer_size/sizeof(char) ; i++){
            // we temp store the http request 
            temp_buffer[temp_buffer_index] = buf[i] ; 
            temp_buffer_index ++ ; 
        }
        temp_buffer_index+=Max_BYTES ; 
        temp_buffer = (char*)realloc(temp_buffer , temp_buffer_size) ;
       
		if(bytes_send < 0)
		{
			perror("Error in sending data to client socket.\n");
			break;
		}
		bzero(buf, MAX_BYTES);

        bytes_send = recv (remote_socketID , buf , Max_BYTES , 0) ;

     }

    temp_buffer[temp_buffer_index]='\0';
	free(buf);
	add_cache_element(temp_buffer, strlen(temp_buffer), tempReq);
	printf("Done\n");
	free(temp_buffer);
	
	
 	close(remote_socketID);
	return 0;

}

void thread_fn(void * socketNew){
  sem_wait(&semaphore) ; 
  int p ; 
  // semaphore value will copy in p 
  sem_getvalue ( &semaphore , &p) ;
  // just copy it is good practice 
  int *t = socketNew ; 
  // its a clientconnected socket fd 
  int socket = *t ;  
  int byte_send_by_client , len ;  //  // Bytes Transferred


// calloc create the memory allocation of MaxBytes and in each element in maxbytes size is char
   char * buffer  = (char *) calloc( MAX_BYTES , sizeof(char)) ; 
 // before using bzero we have to delete bcz of garbage value 
 bzero(buffer , MAX_BYTES) ; 
 // client send the length of message of bytes and store in buffer 
 //isko message passing ka one of the way bolte hai
 byte_send_by_client = recv(proxy_socketId , buffer, MAX_BYTES , 0 ) ;

 while( byte_send_by_client>0){
  len = strlen (buffer) ;

  if(strstr(buffer , "\r\n\r\n")== NULL){
    byte_send_by_client = recv(proxy_socketId , buffer+len ,MAX_BYTES-len , 0) ; 
  }
  else {
    break ;
  }
  int checkHTTPversion(char *msg)
{
	int version = -1;

	if(strncmp(msg, "HTTP/1.1", 8) == 0)
	{
		version = 1;
	}
	else if(strncmp(msg, "HTTP/1.0", 8) == 0)			
	{
		version = 1;										// Handling this similar to version 1.1
	}
	else
		version = -1;

	return version;
}


 }
 // just copy the request for good practice 
 // tempReq and buffer both stores the http request by client 
 char *tempReq = (char *)malloc(sizeof(char)*Max_BYTES+1) ; 

 for(int i = 0 ; i<Max_BYTES , i++){
    tempReq[i] = buffer[i] ;
 }

 // Now time come to check that request in our cache or not 

 struct *cache_element  temp = find(tempReq) ;

 if(temp != NULL){
    int size_of_element = temp->len/ sizeof(char) ; 
   char request[Max_BYTES] ; 
   int pos = 0 ; 
   while (pos<size_of_element){
    bzero(request , Max_BYTES) ; 

    for(int i = 0 ; i<Max_BYTES ; i++){
        request[i] = temp->data[pos] ; 
        pos++ ;

    }
    send (socket , response , Max_BYTES , 0) ;
   }

   printf("Data retrived from the Cache\n\n");
		printf("%s\n\n",response);

    
 }
 // Now i have to parse the request bcz cache is miss
	else if(bytes_send_client > 0)
	{
		len = strlen(buffer); 
		//Parsing the request
		ParsedRequest* request = ParsedRequest_create();
		
        //ParsedRequest_parse returns 0 on success and -1 on failure.On success it stores parsed request in
        // the request
		if (ParsedRequest_parse(request, buffer, len) < 0) 
		{
		   	printf("Parsing failed\n");
		}
		else
		{	
			bzero(buffer, MAX_BYTES);
			if(!strcmp(request->method,"GET"))							
			{
                // They have limitation 
                //1.They will only valid http version 1
                //2.Only Get http method will supported 
				if( request->host && request->path && (checkHTTPversion(request->version) == 1) )
				{
					bytes_send_client = handle_request(socket, request, tempReq);		// Handle GET request
					if(bytes_send_client == -1)
					{	
						sendErrorMessage(socket, 500);
					}

				}
				else
					sendErrorMessage(socket, 500);			// 500 Internal Error

			}
            else
            {
                printf("This code doesn't support any method other than GET\n");
            }
    
		}
        //freeing up the request pointer
		ParsedRequest_destroy(request);

	}

	else if( bytes_send_client < 0)
	{
		perror("Error in receiving from client.\n");
	}
	else if(bytes_send_client == 0)
	{
		printf("Client disconnected!\n");
	}

	shutdown(socket, SHUT_RDWR);
	close(socket);
	free(buffer);
	sem_post(&seamaphore);	
	
	sem_getvalue(&seamaphore,&p);
	printf("Semaphore post value:%d\n",p);
	free(tempReq);
	return NULL;

}

int main(int argc , char *argv[]){

    int client_socketId , client_len ; 
    struct sockaddr_in server_addr , client_addr;

    sem_init (&semaphore , 0 , Maxclients) ; //Intializing semaphore and lock
    pthread_mutex_init(&lock , NULL) ; // intializing lock for cache
  // Null because in c is a Garbage value 

    if(argc==2){ //  checking whether two arguments are recieved or not
    port_number = atoi(argv[1]) ; 
    }
    else
	{
		printf("Too few arguments\n");
		exit(1);
	}

	printf("Setting Proxy Server Port : %d\n",port_number);

    //creating the proxy socket->  AF_INET -> IpV4 , sock_stream for TCP byte stream
	proxy_socketId = socket(AF_INET, SOCK_STREAM, 0);

	if( proxy_socketId < 0)
	{
		perror("Failed to create socket.\n");
		exit(1);
	}

     int reuse = 1;
   if (setsockopt(proxy_socketId, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) 
        perror("setsockopt(SO_REUSEADDR) failed\n");

	bzero((char*)&server_addr, sizeof(server_addr));  
	server_addr.sin_family = AF_INET;
	server_addr.sin_port = htons(port_number); // Assigning port to the Proxy
	server_addr.sin_addr.s_addr = INADDR_ANY; // Any available adress assigned

    // bind 
    if( bind(proxy_socketId, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0 )
	{
		perror("Port is not free\n");
		exit(1);
	}
	printf("Binding on port: %d\n",port_number);

    // Proxy socket listening to the requests
    // socket_id , it is a queue after that reaching Maxclients kernel rejecting
	int listen_status = listen(proxy_socketId, Maxclients);

	if(listen_status < 0 )
	{
		perror("Error while Listening !\n");
		exit(1);
	}
    
// infinte loop for accept the connect and estalblish the connection
   int i = 0 ; 
   int Connected_socketId[Maxclients] ;
    while (1){
    
   bzero((char*)&client_addr, sizeof(client_addr));			// Clears struct client_addr
		client_len = sizeof(client_addr); 

        // Accepting the connections
		client_socketId = accept(proxy_socketId, (struct sockaddr*)&client_addr,(socklen_t*)&client_len);	// Accepts connection
		if(client_socketId < 0)
		{
			fprintf(stderr, "Error in Accepting connection !\n");
			exit(1);
		}
		else{
			Connected_socketId[i] = client_socketId; // Storing accepted client into array
		}

		// Getting IP address and port number of client
		struct sockaddr_in* client_pt = (struct sockaddr_in*)&client_addr;
		struct in_addr ip_addr = client_pt->sin_addr;
		char str[INET_ADDRSTRLEN];										// INET_ADDRSTRLEN: Default ip address size
		inet_ntop( AF_INET, &ip_addr, str, INET_ADDRSTRLEN );
		printf("Client is connected with port number: %d and ip address: %s \n",ntohs(client_addr.sin_port), str);
		//printf("Socket values of index %d in main function is %d\n",i, client_socketId);
		pthread_create(&tid[i],NULL,thread_fn, (void*)&Connected_socketId[i]); // Creating a thread for each client accepted
		i++;
    

    }
   return 0 ;
}
cache_element* find(char * url){

    // Checks for url in the cache 
    //if found returns pointer to the respective cache element 
    //or else returns NULL
    cache_element * site = NULL ;
 int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
    if (head!= NULL ){
      site = head ;

        while (site!=NULL)
        {
            if(!strcmp(site->url,url)){
				printf("LRU Time Track Before : %ld", site->lru_time_track);
                printf("\nurl found\n");
				// Updating the time_track 
				site->lru_time_track = time(NULL);
				printf("LRU Time Track After : %ld", site->lru_time_track);
				break;
            }
            site=site->next;
        }       
    }
	else {
    printf("\nurl not found\n");
	}
	//sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
    return site;
}
    
void remove_cache_element(){
    // If cache is not empty searches for the node which has the least lru_time_track and deletes it
    cache_element * p ;  	// Cache_element Pointer (Prev. Pointer)
	cache_element * q ;		// Cache_element Pointer (Next Pointer)
	cache_element * temp;	// Cache element to remove
    //sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Remove Cache Lock Acquired %d\n",temp_lock_val); 
	if( head != NULL) { // Cache != empty
		for (q = head, p = head, temp =head ; q -> next != NULL; 
			q = q -> next) { // Iterate through entire cache and search for oldest time track
			if(( (q -> next) -> lru_time_track) < (temp -> lru_time_track)) {
				temp = q -> next;
				p = q;
			}
		}
		if(temp == head) { 
			head = head -> next; /*Handle the base case*/
		} else {
			p->next = temp->next;	
		}
		cache_size = cache_size - (temp -> len) - sizeof(cache_element) - 
		strlen(temp -> url) - 1;     //updating the cache size
		free(temp->data);     		
		free(temp->url); // Free the removed element 
		free(temp);
	} 
	//sem_post(&cache_lock);
    temp_lock_val = pthread_mutex_unlock(&lock);
	printf("Remove Cache Lock Unlocked %d\n",temp_lock_val); 
}
int add_cache_element(char* data,int buffer_size,char* url){
    // Adds element to the cache
	// sem_wait(&cache_lock);
    int temp_lock_val = pthread_mutex_lock(&lock);
	printf("Add Cache Lock Acquired %d\n", temp_lock_val);
    int element_size=buffer_size+1+strlen(url)+sizeof(cache_element); // Size of the new element which will be added to the cache
    if(element_size>MAX_ELEMENT_SIZE){
		//sem_post(&cache_lock);
        // If element size is greater than MAX_ELEMENT_SIZE we don't add the element to the cache
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		// free(data);
		// printf("--\n");
		// free(url);
        return 0;
    }
    else
    {   while(cache_size+element_size>MAX_SIZE){
            // We keep removing elements from cache until we get enough space to add the element
            remove_cache_element();
        }
        cache_element* element = (cache_element*) malloc(sizeof(cache_element)); // Allocating memory for the new cache element
        element->data= (char*)malloc(buffer_size+1); // Allocating memory for the response to be stored in the cache element
		strcpy(element->data,data); 
        element -> url = (char*)malloc(1+( strlen( url )*sizeof(char)  )); // Allocating memory for the request to be stored in the cache element (as a key)
		strcpy( element -> url, url );
		element->lru_time_track=time(NULL);    // Updating the time_track
        element->next=head; 
        element->len=size;
        head=element;
        cache_size+=element_size;
        temp_lock_val = pthread_mutex_unlock(&lock);
		printf("Add Cache Lock Unlocked %d\n", temp_lock_val);
		//sem_post(&cache_lock);
		// free(data);
		// printf("--\n");
		// free(url);
        return 1;
    }
    return 0;
}




