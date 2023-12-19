/**Name:Mohammad Belbesi**/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include "threadpool.h"

#define fail (-1)
#define RFC1123FMT "%a, %d %b %Y %H:%M:%S GMT"
#define error_400 "<HTML><HEAD><TITLE>400 Bad Request</TITLE></HEAD>\r\n<BODY><H4>400 Bad request</H4>\r\nBad Request.\r\n</BODY></HTML>\r\n"
#define error_501 "<HTML><HEAD><TITLE>501 Not supported</TITLE></HEAD>\r\n<BODY><H4>501 Not supported</H4>\r\nMethod is not supported.\r\n</BODY></HTML>\r\n"
#define error_404 "<HTML><HEAD><TITLE>404 Not Found</TITLE></HEAD>\r\n<BODY><H4>404 Not Found</H4>\r\nFile not found.\r\n</BODY></HTML>\r\n"
#define error_302 "<HTML><HEAD><TITLE>302 Found</TITLE></HEAD>\r\n<BODY><H4>302 Found</H4>\r\nDirectories must end with a slash.\r\n</BODY></HTML>\r\n"
#define error_403 "<HTML><HEAD><TITLE>403 Forbidden</TITLE></HEAD>\r\n<BODY><H4>403 Forbidden</H4>\r\nAccess denied.\r\n</BODY></HTML>\r\n"
#define error_500 "<HTML><HEAD><TITLE>500 Internal Server Error</TITLE></HEAD>\r\n<BODY><H4>500 Internal Server Error</H4>\r\nSome server side error.\r\n</BODY></HTML>\r\n"

#define Protocol_Message "HTTP/1.0 %d %s\r\n"
#define Server_Message "Server: webserver/1.0\r\n"
#define Connection_Message "Connection: close\r\n"
#define Content_Type_Message "Content-Type: %s\r\n"
#define Content_length_Message "Content-Length: %d\r\n"
#define Last_Modified_Message "Last-Modified: %s\r\n"
#define Table_first_Lines_Message "<HTML>\r\n<HEAD><TITLE>Index of %s</TITLE></HEAD>\r\n<BODY>\r\n<H4>Index of %s</H4>\r\n<table CELLSPACING=8>\r\n<tr><th>Name</th><th>Last Modified</th><th>Size</th></tr>\r\n"
#define Table_Row_Message "<tr>\r\n<td><A HREF=\"%s\">%s</A></td>\r\n<td>%s</td>\r\n<td>%s</td>\r\n</tr>\r\n"
#define Table_end_Message "</table>\r\n<HR>\r\n<ADDRESS>webserver/1.0</ADDRESS>\r\n</BODY></HTML>\r\n"

#define Internal_Server_Error 500
#define Forbidden 403
#define Not_Found 404

int check_command_args(int argc, char* argv[], int* port, int* pool_size, int* max_number_of_request);
int dispatch_func(void* arg);
char *get_mime_type(char *name);
int long index_until_see_string(char *str1, char *str2);
void error_handle(int error_num, int fd, char *path);
void check_and_work_on_path(int fd, char *path);
int check_permissions(char* path);
void send_contents_of_directory(int fd, char*path);
void send_to_response_file(int fd, int file, char*path);
int main(int argc, char* argv[]) {
    int port=0,pool_size=0,max_requests=0,returns_result,i=0;
    returns_result=check_command_args(argc,argv,&port,&pool_size,&max_requests);
    if(returns_result!=EXIT_SUCCESS){
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(EXIT_SUCCESS);
    }

    ////////////////** server_pool create **////////////////
    threadpool *server_pool;
    server_pool=create_threadpool(pool_size);
    if(server_pool==NULL){
        printf("Usage: server <port> <pool-size> <max-number-of-request>\n");
        exit(EXIT_SUCCESS);
    }

    ////////////////** set the server on the internet line **////////////////
    int fd;/* socket descriptor */
    int *newfd;				/* returned by accept() */
    struct sockaddr_in srv;/* used by bind() */
    srv.sin_family = AF_INET;/* use the Internet addr family */
    srv.sin_addr.s_addr = htonl(INADDR_ANY);/* bind: a client may connect to any of my addresses */
    srv.sin_port = htons(port);/* bind socket ‘fd’ to port 80*/
    struct sockaddr_in cli;		/* used by accept() */
    unsigned int cli_len = sizeof(cli);	/* used by accept() */
    /** (1)create the socket **/
    if((fd = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket Error");
        destroy_threadpool(server_pool);
        exit(EXIT_FAILURE);
    }

    /** (2)bind the socket to a port **/
    if(bind(fd, (struct sockaddr*) &srv, sizeof(srv)) < 0) {

        perror("bind Error");
        destroy_threadpool(server_pool);
        close(fd);
        exit(EXIT_FAILURE);
    }
    /** (3) listen on the socket **/
    if(listen(fd, 5) < 0) {

        perror("listen Error");
        destroy_threadpool(server_pool);
        close(fd);
        exit(EXIT_FAILURE);
    }

    /** (4) accept blocks waiting for a connection
        accept returns a new socket (newFd) with the same properties as the original socket (fd) **/
    newfd= (int*)malloc(max_requests * sizeof (int));
    if(newfd == NULL) {
        fprintf(stderr,"newFd allocated memory is failed!\n");
        destroy_threadpool(server_pool);
        close(fd);
        exit(EXIT_FAILURE);
    }
    while(i < max_requests){
        newfd[i]=accept(fd, (struct sockaddr*) &cli,&cli_len);
        if(newfd < 0) {
            perror("accept Error");
            free(newfd);
            destroy_threadpool(server_pool);
            close(fd);
            exit(EXIT_FAILURE);
        }
        dispatch(server_pool,dispatch_func,(newfd+i));
        i++;
    }

    destroy_threadpool(server_pool);
    free(newfd);
    close(fd);

    return EXIT_SUCCESS;
}
///** this function to parse and check the user input arguments **///
/** return 1 if the user had input an incorrect args and 0 if it's in the right way**/
int check_command_args(int argc, char* argv[], int* port, int* pool_size, int* max_number_of_request){
    char* not_number_str;
    int base=10;

    if(argc!=4){
        return EXIT_FAILURE;//EXIT_FAILURE=1
    }

    (*port) = (int) strtol(argv[1],&not_number_str,base);//stroll convert the string to a long integer and return ptr that have characters that not a number
    if(strcmp(not_number_str,"")!=0 && (*port)<1024){
        return EXIT_FAILURE;
    }

    (*pool_size) = (int) strtol(argv[2],&not_number_str,base);
    if(strcmp(not_number_str,"")!=0 && (*pool_size)<=0){
        return EXIT_FAILURE;
    }

    (*max_number_of_request) = (int) strtol(argv[3],&not_number_str,base);
    if(strcmp(not_number_str,"")!=0 && (*max_number_of_request)<=0){
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

int dispatch_func(void* arg){
    int newFd=*((int*)arg);//pointer to the newFd that we send to the dispatch_func
    char buf[501];//buffer that has used by read
    memset(buf,0, sizeof (buf));//clear the buffer
    int long nbytes;//used by read
    /**read the request**/
    if((nbytes = read(newFd, buf, sizeof(buf))) < 0) {//1. Read request from socket
        /** Calling perror will give you the interpreted value of errno,
         * which is a thread-local error value written to by POSIX syscalls
         * (i.e., every thread has it's own value for errno). **/
        perror("client request read failed!");
        return fail;//we won't exit the program if one of the request failed //fail=-1
    }
    buf[nbytes]='\0';

    /** parse the request until reach /r/n that mean take only the first line
        Check input: The request first line should contain method, path and protocol. Here,
        you only have to check that there are 3 tokens and that the last one is one of the
        http versions, other checks on the method and the path will be checked later. In
        case the request is wrong, send 400 "Bad Request" respond, as in file 400.txt. **/

    char bufferCopy[501];//copy of the read buffer because strtok not safe and can change the original buffer
    memset(bufferCopy,0, sizeof (bufferCopy));//clear the new buffer
    strcpy(bufferCopy, buf);//copy the original buffer
    char* first_line=NULL;//save the first line request until see "\r\n"
    char* saved_string=NULL;
    const char delimiter[] = "\r\n";
    char*path=NULL;
    saved_string = strtok(bufferCopy, delimiter);
    if(strstr(buf,delimiter)==NULL){
        error_handle(400, newFd, path);
        close(newFd);//close the socket
        return EXIT_FAILURE;
    }
    int long line_length= (index_until_see_string((char*)buf,(char*)delimiter));
    first_line=(char*) malloc((line_length+1)*sizeof (char));
    if(first_line==NULL){
        fprintf(stderr,"first_line allocated memory is failed!\n");
        error_handle(500, newFd, path);
        close(newFd);
        return EXIT_FAILURE;
    }
    strncpy(first_line,saved_string,line_length);
    first_line[line_length]='\0';

    /***check the request and save the path and the protocol version*/
    int spaceCount = 0;
    int i;
    for (i = 0; first_line[i] != '\0'; i++) {
        if (first_line[i] == ' ') {
            spaceCount++;
        }
    }
    if(spaceCount!=2){
        error_handle(400, newFd, path);
        close(newFd);
        free(first_line);
        return EXIT_FAILURE;
    }

    char method[]="GET";
    char protocol_version[9]={0};
    protocol_version[8]='\0';
    char *token = strtok(first_line, " ");
    if(token==NULL){
        error_handle(400, newFd, path);
        close(newFd);
        free(first_line);
        return EXIT_FAILURE;
    }
    /** 3. check if it's GET method, if we get another method, return error
           message "501 not supported", as in file 501.txt **/
    if(strcmp(token,method)!=0){
        error_handle(501, newFd, path);
        close(newFd);
        free(first_line);
        return EXIT_FAILURE;
    }
    token = strtok(NULL, " ");
    if(token==NULL){
        error_handle(400, newFd, path);
        close(newFd);
        free(first_line);
        return EXIT_FAILURE;
    }
    int token_length = (int)strlen(token);
    path=(char *) malloc((token_length+1)*sizeof(char));
    if(path==NULL){
        fprintf(stderr,"protocol_version allocated memory is failed!\n");
        error_handle(500, newFd, path);
        close(newFd);
        free(first_line);
        return EXIT_FAILURE;
    }
    strcpy(path,token);
    path[token_length]='\0';
    token = strtok(NULL, " ");
    if(token==NULL){
        error_handle(400, newFd, path);
        close(newFd);
        free(first_line);
        free(path);
        return EXIT_FAILURE;
    }
    if(strcmp(token,"HTTP/1.1")!=0 && strcmp(token,"HTTP/1.0")!=0){
        error_handle(400, newFd, path);
        close(newFd);
        free(first_line);
        free(path);
        return EXIT_FAILURE;
    }
    strcpy(protocol_version,token);
    printf("%s\n\n", buf);///////////////////////////////////////////////////////////////////////////////////////
    /** check the legality of the path **/
    if(path[0]!='/' || strstr(path,"//")){
        error_handle(400, newFd, path);
        close(newFd);
        free(first_line);
        free(path);
        return EXIT_FAILURE;
    }
    /** deal with the path that have only / **/
    char *updated_path=NULL;
    if(path[0]=='/' && strlen(path)==1){
        char help_str[]="/.";
        updated_path=(char *) malloc(4 * sizeof (char));
        if(updated_path==NULL){
            fprintf(stderr,"updated path allocated memory is failed!\n");
            error_handle(500, newFd, path);
            close(newFd);
            free(first_line);
            free(path);
            return EXIT_FAILURE;
        }
        bzero(updated_path, 4 * sizeof (char));
        strncpy(updated_path, help_str, 2);
        strncat(updated_path, path, 2);
        updated_path[3]='\0';
        //printf("the path is: %s",updated_path);
        check_and_work_on_path(newFd, updated_path);
    }
    else{
        check_and_work_on_path(newFd, path);
    }

    free(first_line);
    if(updated_path!=NULL){
        free(updated_path);
    }
    free(path);
    close(newFd);

    return EXIT_SUCCESS;
}
/** "stat" is usually used when you want to get the detailed information about the file,
 * while "access" is used when you just need to check if the file can be accessed or not.**/
void check_and_work_on_path(int fd, char *path) {
    int file_found=0;
    char* index_html="index.html";
    int path_length= (int)strlen(path);
    /** The dir object is used in conjunction with other functions such as opendir(),
     * readdir(), and closedir() to read the contents of a directory **/
    DIR *dir;// DIR *dir is a pointer to a DIR
    struct dirent* entry;//structure that save and contain information about the file
    struct stat file_stat;
    /** The function returns 0 on success, and -1 on error. The variable "return_result" is being
     * used to store the return value of the stat() function. If the stat() function returns 0,
     * it means that it was successful in getting information about the file or directory
     * and the "file_state" variable now contains the information.**/
    int return_value,check_permissions_result;
    return_value= stat(path+1,&file_stat);//+1 means that it's skipping the first character of the path that is '\'
    if(return_value == Not_Found){
        error_handle(404, fd, path);
        return;
    }
    check_permissions_result= check_permissions(path);
    if(check_permissions_result==Not_Found){
        error_handle(404,fd,path);
        return;
    }
    if(check_permissions_result==Forbidden){
        error_handle(403,fd,path);
        return;
    }
    if(check_permissions_result==Internal_Server_Error){
        error_handle(500,fd,path);
        return;
    }
    if(S_ISDIR(file_stat.st_mode)){//If the path is a directory
        if(path[path_length-1]!='/') {//if it's not end with a '/', return a “302 Found”
            error_handle(302, fd, path);
            return;
        }
        dir= opendir(path+1);
        if(dir==NULL){//problem with openDir
            perror("openDir failed!\n");
            error_handle(500,fd,path);
            return;
        }
        while((entry = readdir(dir)) != NULL){//loop to found the index.html in the directory
            if(strcmp(entry->d_name, index_html) == 0){//if we found the index.html
                file_found = 1;
                break;
            }
        }

        if(file_found==1){
            char choosen_file[500];
            memset(choosen_file,'\0', sizeof (choosen_file));
            sprintf(choosen_file,"%sindex.html",path);//add the index.html to the path
            check_permissions_result= check_permissions(choosen_file);//check the permissions again after we add the index.html to the path
            if (check_permissions_result==Forbidden){
                closedir(dir);
                error_handle(403,fd,path);
                return;
            }
            if(stat(choosen_file+1,&file_stat)==fail){
                closedir(dir);
                error_handle(500,fd,path);
                return;
            }
            if((file_stat.st_mode & S_IROTH)!=S_IROTH){//if Read permission bit for other users is not allowed
                closedir(dir);
                error_handle(403, fd, path);
                return;
            }
            /** if had a read permissions and access all the conditions then**/
            // I need more control over file access modes and permissions so im using open over fopen
            int File = open(choosen_file+1,O_RDONLY, 0666);
            if(File<0){
                closedir(dir);
                error_handle(500,fd,path);
                return;
            }
            send_to_response_file(fd, File, choosen_file + 1);
            close(File);
            closedir(dir);
            return;
        }
        else{
            if(stat(path+1,&file_stat)==fail){
                closedir(dir);
                error_handle(500,fd,path);
                return;
            }
            if((file_stat.st_mode & S_IROTH)!=S_IROTH){//if Read permission bit for other users is not allowed
                closedir(dir);
                error_handle(403, fd, path);
                return;
            }
            send_contents_of_directory(fd,path+1);
            closedir(dir);
            return;
        }
    }
    else{
        if(S_ISREG(file_stat.st_mode) == 0){//This checks if the macro S_ISREG applied to fs.st_mode returns a 0,
            error_handle(403,fd,path);// indicating that the file is not a regular file.
            return;
        }
        if((file_stat.st_mode & S_IROTH)==S_IROTH){//if Read permission bit for other users is allowed
            int File = open(path+1,O_RDONLY,0666);
            if(File<0){
                error_handle(500,fd,path);
                return;
            }
            send_to_response_file(fd, File, path + 1);
            close(File);
        }
        else{
            error_handle(403,fd,path);
            return;
        }
    }

}
void send_contents_of_directory(int fd, char*path){
    /** The dir object is used in conjunction with other functions such as opendir(),
     * readdir(), and closedir() to read the contents of a directory **/

    DIR *dir;// DIR *dir is a pointer to a DIR
    struct dirent* entry;//structure that save and contain information about the file
    dir= opendir(path);
    if(dir == NULL){
        perror("open directory problem");
        error_handle(500, fd, path);
        return;
    }
    struct stat file_stat;
    if(stat(path,&file_stat)==fail){
        perror("The path does not exist or the user does not have permission to access the file\n");
        error_handle(500,fd,path);
        return;
    }

/** deal with time in (now) and last modified time in my file**/

    time_t now;
    char timebuf[128];
    memset(timebuf,'\0',sizeof (timebuf));
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));//timebuf holds the correct format of the current time.
    ///////////////////////////////////////////////////////////////
    char last_modified_timebuf[128];
    memset(last_modified_timebuf,'\0',sizeof (last_modified_timebuf));
    strftime(last_modified_timebuf, sizeof(last_modified_timebuf), RFC1123FMT, gmtime((const time_t*)&file_stat.st_mtim));

/** end of the time dealing process**/


    char response[1000];
    memset(response,'\0',sizeof (response));

    char version_status_phrase[50];
    memset(version_status_phrase,'\0',sizeof (version_status_phrase));

    char content_type[150];
    memset(content_type,'\0', sizeof (content_type));

    char content_length[150];
    memset(content_length,'\0', sizeof (content_length));

    char last_modified[150];
    memset(last_modified,'\0', sizeof (last_modified));
    sprintf(last_modified, Last_Modified_Message, last_modified_timebuf);

    char selected_file_name [600];
    memset(selected_file_name,'\0', sizeof (selected_file_name));

    char selected_modified_date [128];
    memset(selected_modified_date,'\0', sizeof (selected_modified_date));

    char selected_length [16];
    memset(selected_length,'\0', sizeof (selected_length));
    int row_length = strlen(Table_Row_Message) + sizeof (response);
    char* table_row = (char*) calloc(row_length, sizeof(char));
    if(table_row == NULL){
        fprintf(stderr,"table row allocated memory failed!\n");
        error_handle(500, fd, path);
        return;
    }

    char *table = NULL;
    int size = 0;
    int path_length = (int)strlen(path);
    while((entry = readdir(dir)) != NULL){
        strcpy(selected_file_name, entry->d_name);
        if(strcmp(".", entry->d_name) != 0 && strcmp("..", entry->d_name) != 0){
            int entry_length = (int)strlen(entry->d_name);
            char* updated_path=(char*)calloc(path_length+entry_length+1,sizeof(char));
            if(updated_path==NULL){
                fprintf(stderr,"table allocated memory failed!\n");
                error_handle(500, fd, path);
                return;
            }
            sprintf(updated_path, "%s%s", path, entry->d_name);
            stat(updated_path, &file_stat);
            free(updated_path);
        }
        else{
            stat(entry->d_name, &file_stat);
        }

        strftime(selected_modified_date, sizeof(timebuf), RFC1123FMT, gmtime((const time_t*)&file_stat.st_mtim));
        if(entry->d_type!=DT_REG){
            strcpy(selected_length, "");
        }
        else{
            sprintf(selected_length,"%d", (int)file_stat.st_size);
        }
        sprintf(table_row, Table_Row_Message, selected_file_name, selected_file_name, selected_modified_date, selected_length);

        if(table == NULL){
            int table_row_length = (int)strlen(table_row);
            size += table_row_length;
            table=(char*)calloc((table_row_length + 1),sizeof(char));
            if(table == NULL){
                fprintf(stderr,"table allocated memory failed!\n");
                error_handle(500, fd, path);
                return;
            }
            else{
                memset(table, 0, table_row_length);
                strcat(table, table_row);
            }

        }
        else{
            int table_len = (int)strlen(table);
            int table_row_length = (int)strlen(table_row);
            int counter = table_len;
            size += table_row_length;
            table = (char *)realloc(table, sizeof(char) * (table_row_length + table_len + 1));
            if(table == NULL){
                fprintf(stderr,"table reallocated memory failed!\n");
                error_handle(500, fd, path);
                return;
            }
            while(table_len <= (counter + table_row_length)){
                table[table_len] = '\0';
                table_len++;
            }
            strcat(table, table_row);

        }
        memset(selected_file_name, '\0', sizeof(selected_file_name));
        memset(selected_modified_date, '\0', sizeof(selected_modified_date));
        memset(selected_length, '\0', sizeof(selected_length));
        memset(table_row, '\0', row_length);
    }
    char* file_type = "text/html";
    int line_length = (int)strlen(path - 1) * 2 + (int)strlen(Table_first_Lines_Message);
    char* first_line = (char*) malloc(sizeof(char) * (line_length + 1));
    if(first_line == NULL){
        fprintf(stderr,"first_line reallocated memory failed!\n");
        error_handle(500, fd, path);
        return;
    }
    memset(first_line,0,line_length);
    sprintf(first_line,Table_first_Lines_Message,(path-1),(path-1));
    sprintf(content_length,Content_length_Message,(int)(strlen(table)+strlen(first_line)+strlen(Table_end_Message)));
    sprintf(version_status_phrase,Protocol_Message,200,"OK");
    sprintf(content_type,Content_Type_Message,file_type);
    sprintf(response,"%s%s%s%s\r\n%s%s%s%s\r\n", version_status_phrase,
            Server_Message,
            "Date: ",
            timebuf,
            content_type,
            content_length,
            last_modified,
            Connection_Message);
    printf("\n");
    printf("%s\n",response);
    write(fd,response,strlen(response));
    write(fd,first_line,strlen(first_line));
    write(fd,table, strlen(table));
    write(fd,Table_end_Message,strlen(Table_end_Message));

    free (first_line);
    free(table);
    free(table_row);
    closedir(dir);
}

void send_to_response_file(int fd, int file, char*path){
    struct stat file_stat;
    if(stat(path,&file_stat)==fail){
        perror("The path does not exist or the user does not have permission to access the file");
        error_handle(500,fd,path);
        return;
    }
    int file_size=(int)file_stat.st_size;


    /** deal with time in (now) and last modified time in my file**/
    time_t now;
    char timebuf[128];
    memset(timebuf,'\0',sizeof (timebuf));
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));//timebuf holds the correct format of the current time.
    ///////////////////////////////////////////////////////////////
    char last_modified_timebuf[128];
    memset(last_modified_timebuf,'\0',sizeof (last_modified_timebuf));
    strftime(last_modified_timebuf, sizeof(last_modified_timebuf), RFC1123FMT, gmtime((const time_t*)&file_stat.st_mtim));
    /** end of the time dealing process**/

    char version_status_phrase[50];
    memset(version_status_phrase,'\0',sizeof (version_status_phrase));

    char response[1000];
    memset(response,'\0',sizeof (response));

    char content_type[150];
    memset(content_type,'\0', sizeof (content_type));

    char content_length[150];
    memset(content_length,'\0', sizeof (content_length));

    char last_modified[150];
    memset(last_modified,'\0', sizeof (last_modified));

    char* file_type=NULL;
    file_type= get_mime_type(path);//save the file type

    sprintf(version_status_phrase, Protocol_Message, 200, "OK");
    sprintf(content_type, Content_Type_Message, file_type);
    sprintf(content_length, Content_length_Message, file_size);
    sprintf(last_modified, Last_Modified_Message, last_modified_timebuf);
    if(file_type==NULL){
        sprintf(response,"%s%s%s%s\r\n%s%s%s\r\n", version_status_phrase, Server_Message, "Date: ", timebuf, content_length, last_modified, Connection_Message);
        printf("\n");
        printf("%s\n",response);
    }
    else{
        sprintf(response,"%s%s%s%s\r\n%s%s%s%s\r\n", version_status_phrase, Server_Message, "Date: ", timebuf, content_type, content_length, last_modified, Connection_Message);
        printf("\n");
        printf("%s\n",response);
    }
    write(fd,response, strlen(response));
    /*int buffer_size = 16777216; // Initial buffer size of 16KB
    unsigned char *buffer = (unsigned char*) malloc(buffer_size);
    if (buffer == NULL) {
        fprintf(stderr,"Error allocating memory for file reading buffer\n");
        error_handle(500,fd,path);
        return;
    }*/
    int buffer_size = 1677721; // Initial buffer size of 16KB
    unsigned int nbytes,counter=0;
    unsigned char buffer[buffer_size];
    memset(buffer, '\0' , buffer_size);//clear the buf
    while(1){
        nbytes=read(file , buffer, buffer_size);
        counter+=nbytes;
        if(nbytes>0){
            write(fd, buffer, nbytes);
        }
        /*if(nbytes == buffer_size){
            buffer_size *= 2;
            buffer = (unsigned char *) realloc(buf, buffer_size);
            if (buffer == NULL) {
                fprintf(stderr,"Error reallocating memory for file reading buffer\n");
                error_handle(500,fd,path);
                return;
            }
        }*/
        else{
            if(counter!=file_size){//if there are a problem with read
                perror("can't read!\n");
            }
            break;//exit the loop
        }
        memset(buffer, '\0', buffer_size);//clear the buf
    }
    //free(buffer);
}

int check_permissions(char* path){//check the permissions on all the path files
    int path_length= (int)strlen(path);
    /**The stat() function fills a struct stat object with information about a file or directory,
     * including the file type and permissions.**/
    struct stat file_stat;
    char *copy1,*copy2;
    copy1=(char*) malloc((path_length+1)*sizeof (char));
    copy2=(char*) malloc((path_length+1)*sizeof (char));
    if(copy1==NULL){
        fprintf(stderr,"path copy1 allocated memory is failed!\n");
        return Internal_Server_Error;//case of a failure after connection with a client return 500 message
    }
    if(copy2==NULL){
        fprintf(stderr,"path copy2 allocated memory is failed!\n");
        free(copy1);
        return Internal_Server_Error;
    }
    bzero(copy1,(path_length+1)*sizeof (char));
    bzero(copy2,(path_length+1)*sizeof (char));
    /**The +1 in path + 1 is used to move the pointer to the next character in the string
     * path. In this case, it effectively strips the leading / from the path**/
    sprintf(copy1, "%s", path  + 1);
    char *token = strtok(copy1, "/");
    while(token!=NULL){

        strcat(copy2, token);

        strcat(copy2, "/");

        token = strtok(NULL, "/");

        if(token==NULL){//if there are no /
            break;
        }

        /**The stat() function returns 0 on success, and -1 on failure. So,
         * this line of code is checking if the return value of the stat function is -1,
         * which means that the function has failed.**/
        if(stat(copy2, &file_stat) == fail){//the function can fail if the file does not exist
            perror("stat fail to find the file\n");
            free(copy2);
            free(copy1);
            return Not_Found;
        }
        if((file_stat.st_mode & S_IXOTH)!=S_IXOTH){//if there are no permissions
            free(copy2);
            free(copy1);
            return Forbidden;
        }
    }
    free(copy2);
    free(copy1);
    return EXIT_SUCCESS;
}

void error_handle(int error_num, int fd, char *path) {

    time_t now;
    char timebuf[128];
    now = time(NULL);
    strftime(timebuf, sizeof(timebuf), RFC1123FMT, gmtime(&now));//timebuf holds the correct format of the current time.
    char version_status_phrase[50];
    memset(version_status_phrase,'\0',sizeof (version_status_phrase));
    char body[500];
    memset(body,'\0',sizeof (body));
    char response[1000];
    memset(response,'\0',sizeof (response));
    int content_Length;
    char content_type[50];
    memset(content_type,'\0', sizeof (content_type));
    if(error_num==400){
        sprintf(version_status_phrase,Protocol_Message,error_num,"Bad Request");
        content_Length= strlen(error_400);
        sprintf(body,"%s", error_400);
    }
    if(error_num==501){
        sprintf(version_status_phrase,Protocol_Message,error_num,"Not supported");
        content_Length= strlen(error_501);
        sprintf(body,"%s", error_501);
    }
    if(error_num==404){
        sprintf(version_status_phrase,Protocol_Message,error_num,"Not Found");
        content_Length= strlen(error_404);
        sprintf(body,"%s", error_404);
    }
    if(error_num==302){
        sprintf(version_status_phrase,Protocol_Message,error_num,"Found");
        content_Length= strlen(error_302);
        sprintf(body,"%s", error_302);
    }
    if(error_num==403){
        sprintf(version_status_phrase,Protocol_Message,error_num,"Forbidden");
        content_Length= strlen(error_403);
        sprintf(body,"%s", error_403);
    }
    if(error_num==500){
        sprintf(version_status_phrase,Protocol_Message,error_num,"Internal Server Error");
        content_Length= strlen(error_500);
        sprintf(body,"%s", error_500);
    }

    sprintf(content_type, Content_Type_Message, "text/html");

    if(error_num == 302){//if the error message was 302 add the path to the message
        sprintf(response, "%s%sDate: %s\r\n%sLocation: %s/\r\nContent-Length: %d\r\n%s\r\n\r\n%s",
                version_status_phrase,
                Server_Message,
                timebuf,
                content_type,
                path,
                content_Length,
                Connection_Message,
                body);
    }
    else{
        sprintf(response, "%s%sDate: %s\r\n%sContent-Length: %d\r\n%s\r\n\r\n%s",
                version_status_phrase,
                Server_Message,
                timebuf,
                content_type,
                content_Length,
                Connection_Message,
                body);
    }
    printf("%s\n",response);
    if(write(fd, response, strlen(response) )<0){//if write failed
        perror("write response error message failed!");
    }
}

int long index_until_see_string(char *str1, char *str2){
    int long counter=0;
    char *target_pos=strstr(str1,str2);
    if(target_pos!=NULL){
        counter= target_pos-str1;
    }
    return counter;
}
char *get_mime_type(char *name){

    char *ext = strrchr(name, '.');
    if (!ext) return NULL;
    if (strcmp(ext, ".html") == 0 || strcmp(ext, ".htm") == 0) return "text/html";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".au") == 0) return "audio/basic";
    if (strcmp(ext, ".wav") == 0) return "audio/wav";
    if (strcmp(ext, ".avi") == 0) return "video/x-msvideo";
    if (strcmp(ext, ".mpeg") == 0 || strcmp(ext, ".mpg") == 0) return "video/mpeg";
    if (strcmp(ext, ".mp3") == 0) return "audio/mpeg";
    return NULL;
}
