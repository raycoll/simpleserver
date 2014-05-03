#include <stdio.h>
#include <sys/types.h>   // definitions of a number of data types used in socket.h and netinet/in.h
#include <sys/socket.h>  // definitions of structures needed for sockets, e.g. sockaddr
#include <netinet/in.h>  // constants and structures needed for internet domain addresses, e.g. sockaddr_in
#include <stdlib.h>
#include <strings.h>
#include <sys/wait.h> /* for the waitpid() system call */
#include <signal.h> /* signal name macros, and the kill() prototype */
#include <sys/stat.h>
#include <string.h>
const int BUF_SIZE = 4096;
const int FILE_BUF_SIZE = 4096;  

void sigchld_handler(int s)
{
    while(waitpid(-1, NULL, WNOHANG) > 0);
}

void handle_request(int sock); 
void send_response(int sock, char* request_uri);
void send_http_error(int sock,char* status_code);

void error(char *msg)
{
    perror(msg);
    exit(1);
}

int main(int argc, char *argv[])
{
    int sockfd, newsockfd, portno, pid;

    if (argc != 2)
    {
        printf("Please provide a port number to run the server!\n");
        error("No port number provided");
    }

    //get the port number from command line arg 
    portno = atoi(argv[1]);

    socklen_t clilen;
    struct sockaddr_in serv_addr, cli_addr;
    struct sigaction sa;          

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) 
        error("ERROR opening socket");
    bzero((char *) &serv_addr, sizeof(serv_addr));

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(portno);

    if (bind(sockfd, (struct sockaddr *) &serv_addr,
        sizeof(serv_addr)) < 0) 
        error("ERROR on binding");

    listen(sockfd,5);

    clilen = sizeof(cli_addr);

    /****** Kill Zombie Processes ******/
    sa.sa_handler = sigchld_handler; // reap all dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if (sigaction(SIGCHLD, &sa, NULL) == -1) {
     perror("sigaction");
     exit(1);
    }
    /*********************************/

    while (1) {
        newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);

        if (newsockfd < 0) 
            error("ERROR on accept");

        pid = fork(); //create a new process
        if (pid < 0)
            error("ERROR on fork");

        if (pid == 0)  { // fork() returns a value of 0 to the child process
            close(sockfd);
            handle_request(newsockfd);
            exit(0);
        }
        else //returns the process ID of the child process to the parent
            close(newsockfd); // parent doesn't need this 
        } /* end of while */
        return 0; 
}

/******** handle_request() *********************
 There is a separate instance of this function 
 for each connection.  It handles all communication
 once a connnection has been established.
 *****************************************/
void handle_request(int sock)
{
    int n;
    char buffer[BUF_SIZE];
    bzero(buffer,BUF_SIZE-1);
    n = read(sock,buffer,BUF_SIZE-1);  // GET / HTTP/1.1
                                // GET /example.html HTTP/1.1
    if (n < 0) 
        error("ERROR reading from socket");
    else buffer[n] = '\0';
    
    char method[64];
    char request_uri[n];
    char http_version[32];
    sscanf(buffer, "%s %s %s", method, request_uri, http_version);

    //#1 for the project: dump message to console   
    printf("Here is the client's HTTP request message: \n%s\n",buffer);

    //#2 for the project: send HTTP response to the client
    send_response(sock,request_uri);
}

/******** send_response(int sock, char* request_uri) *********
 Sends an HTTP response back to the client at socket sock. 
 Request uri specifies the file requested by the client 
 CURRENT SUPPORTED FILE TYPES: HTML,JPEG,GIF
 *************************************************************/
void send_response(int sock, char* request_uri)
{

    //set the content-type
    char* extension = memchr(request_uri,'.',strlen(request_uri));
    char content_type[64]; 
    if (extension)
    {
        if (!strcmp(extension,".jpeg") || !strcmp(extension,".jpg"))
            strncpy(content_type,"image/jpeg",31);
        else if (!strcmp(extension,".gif"))
            strncpy(content_type,"image/gif",31);
        else if (!strcmp(extension,".html"))
            strncpy(content_type,"text/html",31);
        else
        {
            send_http_error(sock,"400");
            error("Unsupported file type requested!\n");
        }
    }
    else
    {
        send_http_error(sock,"400");
        error("File must have an extension!\n");
    }

    //open the file
    FILE* f;
    if (!(f = fopen(request_uri+1,"r")))
    {
        send_http_error(sock,"404");
        error("Failed to open file! 404 sent to client\n");
    }

    //get the file size
    struct stat s; 
    stat(request_uri+1, &s);
    size_t f_size = s.st_size;

    //convert file size to string
    char f_size_str[32];
    snprintf(f_size_str,31,"%d",f_size);

    //create the response header from previously parsed info and write it to the socket
    char header[BUF_SIZE];
    snprintf(header,BUF_SIZE-1,"HTTP/1.1 200 OK\r\nContent-Type: \
        %s\r\nContent-Length: %s\r\n\r\n", content_type, f_size_str);
    
    if(write(sock,header,strlen(header)) < 0)
        error("Writing HTTP header failed!\n");

    //write file to socket(incrementally to accomodate big files)
    char* buf[FILE_BUF_SIZE];
    size_t amt; 
    while(1)
    {
        amt = fread(buf,sizeof(char),FILE_BUF_SIZE,f);
        if(write(sock,buf,amt) < 0)
            error("Writing HTTP body to client failed!\n");

        //the entire file has been read if the last fread() did not 
        //fill the buffer. 
        if (amt < FILE_BUF_SIZE)
          break; 
    }

}

void send_http_error(int sock, char* status_code)
{
    char status_msg[32]; 
    if (!strcmp(status_code,"404"))
        strcpy(status_msg,"Not Found");
    else if (!strcmp(status_code,"400"))
        strcpy(status_msg,"Bad Request");
    else
        status_msg[0]='\0'; 

    char errmsg[BUF_SIZE];
    snprintf(errmsg,BUF_SIZE,"HTTP/1.1 %s %s\r\n", status_code, status_msg);
    
    if(write(sock,errmsg,strlen(errmsg)) < 0 )
    	error("Writing HTTP Error response header failed!\n"); 
}
