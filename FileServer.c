#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/epoll.h>

#define SERV_ADR "127.0.0.1"
#define PORT 10000
#define BUF_SIZE 1024
#define FILE_NAME_LEN 256
#define EPOLL_SIZE 30
#define MAX_FILE (2 * EPOLL_SIZE + 3)
#define ALLOW_ACCEPT "allow\n"
#define NOT_ALLOW_ACCEPT "not allow\n"
#define FINISH "finish\n"
#define FILE_PATH "./ServerFiles/"

typedef struct
{
    char file_name[FILE_NAME_LEN];
    long nleft;
    long nsent;
    FILE *file_write_fp;
    FILE *sock_read_fp;
    FILE *sock_write_fp;
} ClntInfo;


int listen_sock, conn_sock;
struct sockaddr_in serv_adr;
struct sockaddr_in clnt_adr;
socklen_t clnt_adr_sz;

ClntInfo clnts_info[MAX_FILE];
struct epoll_event *ep_events;
struct epoll_event event;
int epfd, event_cnt;

////////////////////////////////////////函数声明////////////////////////////////////////
/**
 * @brief 显示错误信息
 * 
 */
void error_handler(char *error_msg);

/**
 * @brief 初始化服务器地址并绑定套接字
 * 
 */
void server_addr_init(struct sockaddr_in *serv_adr, char *addr, int port);

/**
 * 初始化客户端epoll监听集和客户端信息数组
 * 
 */
void clnt_set_init();

/**
 * @brief 向epoll监听集和客户端信息数组添加新的客户端
 * 
 */
void add_clnt_sock(int clnt_sock, char *file_name, long file_size_in_byte, FILE *read_fp, FILE *write_fp);

/**
 * @brief 接收客户端控制消息获得文件名和文件大小，判断是否允许接收文件。若允许，则直接添加客户端，否则断开连接
 * 
 * @return 若允许接收则返回1，否则返回0
 */
int receive_control_message(int clnt_sock);

/**
 * @brief 接收客户端发来的文件数据，并写到服务器端文件中，每次最多接收BUF_SIZE个字节
 * 
 */
void receive_file_from_client(int clnt_sock);

/**
 * @brief 从监听集和客户端信息数组移除客户端并关闭连接
 * 
 */
void remove_clnt_sock(int clnt_sock);

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

    err = bind(listen_sock, (struct sockaddr *)serv_adr, sizeof(*serv_adr));
    if(err == -1)
    {
        error_handler("bind()");
    }
}

void clnt_set_init()
{
    epfd = epoll_create(EPOLL_SIZE);
    ep_events = (struct epoll_event *)malloc(sizeof(struct epoll_event) * EPOLL_SIZE);

    event.events = EPOLLIN;
    event.data.fd = listen_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listen_sock, &event);
}

void add_clnt_sock(int clnt_sock, char *file_name, long file_size_in_byte, FILE *read_fp, FILE *write_fp)
{
    char file_path[FILE_NAME_LEN];
    event.events = EPOLLIN;
    event.data.fd = clnt_sock;
    epoll_ctl(epfd, EPOLL_CTL_ADD, clnt_sock, &event);

    strcpy(file_path, FILE_PATH);
    strcat(file_path, file_name);

    strcpy(clnts_info[clnt_sock].file_name, file_name);
    clnts_info[clnt_sock].nleft = file_size_in_byte;
    clnts_info[clnt_sock].nsent = 0;
    clnts_info[clnt_sock].file_write_fp = fopen(file_path, "wb");
    clnts_info[clnt_sock].sock_read_fp = read_fp;
    clnts_info[clnt_sock].sock_write_fp = write_fp;
}

int receive_control_message(int clnt_sock)
{
    struct statvfs fs_info;
    char control_message[BUF_SIZE];
    char file_name[FILE_NAME_LEN];
    long file_size_in_byte;
    long free_size;
    char *ptr;
    FILE *sock_read_fp = fdopen(clnt_sock, "rb");
    FILE *sock_write_fp = fdopen(clnt_sock, "wb");

    //接收客户端控制消息并解析
    fgets(control_message, BUF_SIZE, sock_read_fp);
    ptr = strtok(control_message, ", \n");
    strcpy(file_name, ptr);
    ptr = strtok(NULL, ", \n");
    file_size_in_byte = atol(ptr);
    printf("Receive control message from client, file name: %s, size: %ld Bytes.\n", file_name, file_size_in_byte);

    if(statvfs(".", &fs_info) == -1)
    {
        error_handler("statvfs()");
    }
    free_size = fs_info.f_bsize * fs_info.f_bfree;
    if(free_size > file_size_in_byte)
    {
        add_clnt_sock(clnt_sock, file_name, file_size_in_byte, sock_read_fp, sock_write_fp);
        fputs(ALLOW_ACCEPT, sock_write_fp);
        fflush(sock_write_fp);
        printf("Send control message to client, allow accept.\n");

        return 1;
    } else
    {
        fputs(NOT_ALLOW_ACCEPT, sock_write_fp);
        fflush(sock_write_fp);
        printf("Send control message to client, not allow accept.\n");

        return 0;
    }
}

void receive_file_from_client(int clnt_sock)
{
    char file_data[BUF_SIZE];
    long n_to_write;

    if(clnts_info[clnt_sock].nleft > BUF_SIZE)
    {
        n_to_write = BUF_SIZE;

    } else
    {
        n_to_write = clnts_info[clnt_sock].nleft;

    }
    fread(file_data, sizeof(char), n_to_write, clnts_info[clnt_sock].sock_read_fp);
    fwrite(file_data, sizeof(char), n_to_write, clnts_info[clnt_sock].file_write_fp);
    fflush(clnts_info[clnt_sock].file_write_fp);

    clnts_info[clnt_sock].nleft -= n_to_write;
    clnts_info[clnt_sock].nsent += n_to_write;

    if(clnts_info[clnt_sock].nleft == 0)
    {
        printf("Receive file message end, file name: %s, size: %ld Bytes.\n", clnts_info[clnt_sock].file_name, clnts_info[clnt_sock].nsent);
        fputs(FINISH, clnts_info[clnt_sock].sock_write_fp);
        fflush(clnts_info[clnt_sock].sock_write_fp);
        printf("Send control message to client, receive finish.\n");

        remove_clnt_sock(clnt_sock);
    }
}

void remove_clnt_sock(int clnt_sock)
{
    printf("Closing down connection ...\n");
    fclose(clnts_info[clnt_sock].file_write_fp);
    fclose(clnts_info[clnt_sock].sock_read_fp);
    fclose(clnts_info[clnt_sock].sock_write_fp);

    epoll_ctl(epfd, EPOLL_CTL_DEL, clnt_sock, NULL);
}

////////////////////////////////////////主函数入口////////////////////////////////////////
int main(int argc, char argv[])
{
    int err;

    //创建套接字
    listen_sock = socket(AF_INET, SOCK_STREAM, 0);
    if(listen_sock == -1)
    {
        error_handler("socket()");
    }

    //初始化服务器地址信息并与套接字绑定
    server_addr_init(&serv_adr, SERV_ADR, PORT);

    //开启套接字监听新连接
    err = listen(listen_sock, 10);
    if(err == -1)
    {
        error_handler("listen()");
    }

    //初始化客户端数组
    clnt_set_init();

    //接受新连接，并接收文件
    clnt_adr_sz = sizeof(clnt_adr);
    printf("Listening for connection ...\n");
    while(1)
    {
        event_cnt = epoll_wait(epfd, ep_events, EPOLL_SIZE, -1);

        if(event_cnt == -1)
        {
            error_handler("epoll_wait()");
        }

        for(int i = 0; i < event_cnt; i++)
        {
            if(ep_events[i].data.fd == listen_sock)
            {
                conn_sock = accept(listen_sock, (struct sockaddr *)&clnt_adr, &clnt_adr_sz);
                printf("New client accepted\n");
                printf("Connection successful\n");
                printf("Listening for input ...\n");
                printf("Listening for connection ...\n");
                //接收控制消息，判断是否允许接收文件，若允许则直接添加客户端
                if(receive_control_message(conn_sock))
                {
                    printf("Receive file message start\n");
                    receive_file_from_client(conn_sock);
                } else
                {
                    printf("Closing down connection ...\n");
                    close(conn_sock);
                }
            } else
            {
                receive_file_from_client(ep_events[i].data.fd);
            }
        }
    }





    return 0;
}





