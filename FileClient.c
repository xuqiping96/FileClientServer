#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define SERV_ADR "127.0.0.1"
#define PORT 10000
#define BUF_SIZE 1024
#define FILE_NAME_LEN 256
#define ALLOW_ACCEPT "allow\n"
#define FINISH "finish\n"

char file_path[FILE_NAME_LEN];
char file_name[FILE_NAME_LEN];
long file_size_in_byte;
struct stat file_info;

FILE *file_read_fp;
FILE *sock_read_fp;
FILE *sock_write_fp;


////////////////////////////////////////函数声明////////////////////////////////////////
/**
 * @brief 显示错误信息
 * 
 */
void error_handler(char *error_msg);

/**
 * @brief 初始化服务器地址信息
 * 
 */
void server_addr_init(struct sockaddr_in *serv_adr, char *addr, int port);

/**
 * @brief 向服务器发送文件名和文件大小控制消息，并接收服务器的回应
 * 
 * @return 如果服务器允许接收文件返回1，否则返回0
 */
int send_control_message();

/**
 * @brief 向服务器发送文件，收到服务器的接收成功消息后返回
 * 
 */
void send_file_to_server();

/**
 * @brief 服务器是否允许接收文件
 * 
 * @return 若允许返回1，否则返回0
 */
int is_allowed(char *control_message);

////////////////////////////////////////函数定义////////////////////////////////////////
void error_handler(char *error_msg)
{
    fprintf(stderr, "%s error.\n", error_msg);
    exit(EXIT_FAILURE);
}

void server_addr_init(struct sockaddr_in *serv_adr, char *addr, int port)
{
    int err;

    bzero(serv_adr, sizeof(*serv_adr));
    serv_adr->sin_family = AF_INET;
    serv_adr->sin_port = htons(port);
    err = inet_pton(AF_INET, addr, &(serv_adr->sin_addr));
    if(err != 1)
    {
        error_handler("inet_pton()");
    }
}

int is_allowed(char *control_message)
{
    if(strcmp(control_message, ALLOW_ACCEPT) == 0)
    {
        return 1;
    } else
    {
        return 0;
    }
}

int send_control_message()
{
    int err;
    char *ptr;
    char control_message[BUF_SIZE];
    int path_len;

    printf("Enter name: ");
    fgets(file_path, FILE_NAME_LEN, stdin);
    path_len = strlen(file_path);
    file_path[path_len - 1] = '\0';

    file_read_fp = fopen(file_path, "rb");
    if(file_read_fp == NULL)
    {
        error_handler("fopen()");
    }

    //获取文件信息
    err = stat(file_path, &file_info);
    if(err != 0)
    {
        error_handler("stat()");
    }

    file_size_in_byte = file_info.st_size;

    //提取文件路径中的文件名
    ptr = strtok(file_path, "/\n");
    while(ptr != NULL)
    {
        strcpy(file_name, ptr);
        ptr = strtok(NULL, "/\n");
    }

    snprintf(control_message, BUF_SIZE, "%s, %ld\n", file_name, file_size_in_byte);
    fputs(control_message, sock_write_fp);
    fflush(sock_write_fp);
    printf("Send control message to server, file name: %s, size: %ld Bytes.\n", file_name, file_size_in_byte);

    fgets(control_message, BUF_SIZE, sock_read_fp);
    
    return is_allowed(control_message);
}

void send_file_to_server()
{
    char file_data[BUF_SIZE];
    char control_message[BUF_SIZE];
    long nleft;
    long total_sent;
    long nsent;

    nleft = file_size_in_byte;
    total_sent = 0;
    printf("Send file message start.\n");
    while(nleft != 0)
    {
        if(nleft > BUF_SIZE)
        {
            nsent = BUF_SIZE;
        } else
        {
            nsent = nleft;
        }

        fread(file_data, sizeof(char), nsent, file_read_fp);
        fwrite(file_data, sizeof(char), nsent, sock_write_fp);
        fflush(sock_write_fp);

        nleft -= nsent;
        total_sent += nsent;
    }

    printf("Send file message end, file name: %s, size: %ld Bytes.\n", file_name, total_sent);
    fgets(control_message, BUF_SIZE, sock_read_fp);
    if(strcmp(control_message, FINISH) == 0)
    {
        printf("Receive control message from server, receive finish.\n");
    }
}

////////////////////////////////////////主函数入口////////////////////////////////////////
int main(int argc, char *argv[])
{
    int err;
    int clnt_sock;
    struct sockaddr_in serv_adr;

    //创建套接字
    clnt_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(clnt_sock == -1)
    {
        error_handler("socket()");
    }

    //初始化服务器地址信息
    server_addr_init(&serv_adr, SERV_ADR, PORT);

    //与服务器建立连接
    err = connect(clnt_sock, (struct sockaddr *)&serv_adr, sizeof(serv_adr));
    if(err == -1)
    {
        error_handler("connect()");
    }
    
    sock_read_fp = fdopen(clnt_sock, "rb");
    sock_write_fp = fdopen(clnt_sock, "wb");
    //向服务器发送控制消息并接收服务器的控制消息
    if(send_control_message())
    {
        printf("Receive control message from server, allow accept.\n");
        //向服务器发送文件
        send_file_to_server();
    } else
    {
        printf("Receive control message from server, not allow accept.\n");
    }

    fclose(file_read_fp);
    fclose(sock_read_fp);
    fclose(sock_write_fp);
    printf("Connection shut down\n");
    
    return 0;
}

