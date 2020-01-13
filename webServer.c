#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <signal.h>
#include <libgen.h>

#define MAX_BUFFER 2024

char *conc(const char *s_str1, const char *s_str2);
char *read_file(const char *s_filename);
size_t get_filesize(const char *s_filename);
char *get_extension(char *s_filename);
char *get_filename(char *s_reqfull);
void sighandler();

int main(int argc, char *argv[]){

    if(argc != 3){
        printf("Usage: %s <html_file> <port_number>", basename(argv[0]));
        exit(1);
    }

    char *s_header = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=UTF-8\r\n\r\n";
    char *s_header_image = "HTTP/1.1 200 OK\r\nContent-Length: ";

    char *s_html = read_file(argv[1]);
    char *s_header_html = conc(s_header, s_html);
    char webpage[strlen(s_header_html)];
    strcpy(webpage, s_header_html);

    s_html = read_file("error/404.html");
    s_header_html = conc(s_header, s_html);
    char error_404[strlen(s_header_html)];
    strcpy(error_404, s_header_html);

    const int i_port = atoi(argv[2]);

    struct sockaddr_in serv_addr, cli_addr;
    int fd_serv, fd_cli;
    char s_req[MAX_BUFFER];
    int fd_file;
    int set_opt = 1;
    socklen_t sin_len = sizeof(serv_addr);    

    fd_serv = socket(AF_INET, SOCK_STREAM, 0);
    if(fd_serv == -1){
        perror("[ERROR]socket");
        exit(1);
    }

    if(setsockopt(fd_serv, SOL_SOCKET, SO_REUSEADDR, &set_opt, sizeof(int)) == -1){
        perror("[ERROR]set socket options");
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(i_port);

    if(bind(fd_serv, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) == -1){
        perror("[ERROR]bind");
        close(fd_serv);
        exit(1);
    }

    if(listen(fd_serv, 50) == -1){
        perror("[ERROR]listen");
        close(fd_serv);
        exit(1);
    }

    while(1){
        fd_cli = accept(fd_serv, (struct sockaddr *) &cli_addr, &sin_len);
        if(fd_cli == -1){
            perror("[ERROR]: Connection failed");
            continue;
        }

        printf("[INFO]: Client connected...\n");

        switch (fork()){
            case -1:{
                perror("[ERROR]fork");
                exit(1);
            } break;
            case 0:{ // Child
                close(fd_serv);
                memset(s_req, 0, MAX_BUFFER);
                read(fd_cli, s_req, MAX_BUFFER-1);

                char *copy = strdup(s_req);

                const char *s_filename = get_filename(copy);
             
                //printf("%s", s_req);

                if(strcmp(s_filename, "") == 0){
                    write(fd_cli, webpage, sizeof(webpage)-1);
                } else {
                    if((fd_file = open(s_filename, O_RDONLY)) == -1){
                        perror("[ERROR]send request file");
                        write(fd_cli, error_404, sizeof(error_404)-1);                        
                        close(fd_file);
                    } else {
                        size_t filesize = get_filesize(s_filename);

                        char s_size[32];
                        snprintf(s_size, 32, "%lu", filesize);   

                        copy = strdup(s_filename);
                        char *ext = get_extension(copy);  

                        if(strcmp(ext, "html") == 0 || strcmp(ext, "htm") == 0){
                            char *s_html = read_file(s_filename);
                            char *s_header_html = conc(s_header, s_html);
                            char webpage[strlen(s_header_html)];
                            strcpy(webpage, s_header_html);
                            write(fd_cli, webpage, sizeof(webpage)-1);            
                        }

                        if(strcmp(ext, "jpg") == 0 || strcmp(ext, "png") == 0 || strcmp(ext, "jpeg") == 0 || strcmp(ext, "gif") == 0){
                            char *s_image = conc(s_header_image, s_size);
                            s_image = conc(s_image, "\r\n");
                            s_image = conc(s_image, "Connection: keep-alive\r\n\r\n");
                            char image[strlen(s_image)];
                            strcpy(image, s_image);
                            write(fd_cli, image, sizeof(image));
                            sendfile(fd_cli, fd_file, NULL, filesize);
                        }
                        
                        if(strcmp(ext, "css") == 0){
                            sendfile(fd_cli, fd_file, NULL, filesize);                            
                        }

                        close(fd_file);
                    }
                }                

                close(fd_cli);

                printf("[INFO]: Closing...\n");
                exit(0);
            } break;
            default:{ // Parent
                close(fd_cli);
                signal(SIGCHLD, sighandler);
            } break;
        }
    }
    return 0;
}

char *conc(const char *s_str1, const char *s_str2){
    char *s_conc = malloc(strlen(s_str1) + strlen(s_str2) + 1);
    strcpy(s_conc, s_str1);
    strcat(s_conc, s_str2);
    return s_conc;
}

char *read_file(const char *s_filename){  
    FILE *f_file = fopen(s_filename, "rb");
    if(f_file == NULL){
        perror("[ERROR]Cannot open file");
        exit(1);
    }
    fseek(f_file, 0, SEEK_END);
    long l_filesize = ftell(f_file);
    fseek(f_file, 0, SEEK_SET);
    char *s_string = malloc(l_filesize + 1);
    fread(s_string, l_filesize, 1, f_file);
    fclose(f_file);
    s_string[l_filesize] = 0;
    return s_string;
}

size_t get_filesize(const char *s_filename) {
    struct stat st;
    if(stat(s_filename, &st) != 0) {
        return 0;
    }
    return st.st_size;   
}

char *get_filename(char *s_reqfull){
    char *s_tokreq;
    s_tokreq = strtok(s_reqfull, "/");
    s_tokreq = strtok(NULL, "HTTP");
    s_tokreq[strlen(s_tokreq) - 1] = '\0';
    return s_tokreq;
}

char *get_extension(char *s_filename){
    char *s_ext;
    s_ext = strtok(s_filename, ".");
    s_ext = strtok(NULL, "");
    return s_ext;
}

void sighandler(){
    while(waitpid(-1, NULL, WNOHANG) > 0);
}
