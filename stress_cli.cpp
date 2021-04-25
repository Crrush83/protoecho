#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include "UserInfo.pb.h"
#include "minilogger.h"
#define MAX_CMD_STR 100
using namespace test;
Logger<TextDecorator> clilog("stresscli.txt", "clilog", true, true); //创建logger
typedef struct msg_proto_header
{
    uint8_t version;
    uint16_t command;
    uint32_t length;
} header;
enum MSG_COMMOND
{
    LOGIN_REQUEST,
};

void make_proto_message(char **msgbufp, header *hp)
{
    //模仿写
   // sleep(0.5);
    test::UserInfo msg;
    msg.set_name("zxt");
    msg.set_age(18);
    msg.set_stat(IDLE);

    header myheader;
    myheader.version = 2;
    myheader.command = LOGIN_REQUEST;
    myheader.length = sizeof(msg);
    *msgbufp = (char *)malloc(sizeof(msg));
    msg.SerializeToArray(*msgbufp, 1024);
    *hp = myheader;
}

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epoll_fd, int fd)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLOUT | EPOLLET | EPOLLERR;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

bool send_nbytes(int sockfd, const char *buffer, int len)
{
    int bytes_write = 0;
    int sent = 0;
    printf("write out %d bytes to socket %d\n", len, sockfd);
    while (1)
    {
        bytes_write = send(sockfd, buffer + sent, len - sent, 0);
        if (bytes_write == -1)
        {
            if (errno == EAGAIN)
                continue;
            else
                return false;
        }
        else if (bytes_write == 0)
        {
            continue;
        }
        else
        {
            sent += bytes_write;
        }
        if (sent == len)
            return true;
        else
            continue;
    }
}

bool recv_nbytes(int sockfd, char *buffer, int len)
{
    int bytes_read = 0;
    int read = 0;
    memset(buffer, '\0', len);
    for (;;)
    {
        bytes_read = recv(sockfd, buffer + read, len - read, 0);
        if (bytes_read == -1)
        {
            if (errno == EAGAIN)
                continue;
            else
                return false;
        }
        else if (bytes_read == 0)
        {
            continue;
        }
        else
        {
            read += bytes_read;
            printf("read in %d bytes from socket %d with content: %s\n", bytes_read, sockfd, buffer);
        }
        if (read == len)
        {
            return true;
        }
    }
}
bool read_length(int sockfd, int *length_hp)
{
    int length_n;
    if (!recv_nbytes(sockfd, (char *)&length_n, sizeof(length_n)))
        return false;
    *length_hp = ntohl(length_n);
    return true;
    ;
}
bool write_length(int sockfd, int *length_hp)
{
    int length_n;
    length_n = htonl(*length_hp);
    if (!send_nbytes(sockfd, (char *)&length_n, sizeof(length_n)))
        return false;
    return true;
}
bool send_header(int sockfd, const header *hp)
{
    int size = sizeof(*hp);
    if (!send_nbytes(sockfd, (char *)hp, size))
    {
        return false;
    }
    return true;
}

void start_conn(int epoll_fd, int num, const char *ip, int port)
{
    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    for (int i = 0; i < num; ++i)
    {
        //sleep(0.01);
        int sockfd = socket(PF_INET, SOCK_STREAM, 0);
        printf("create 1 sock\n");
        clilog.Log("create 1 sock");
        if (sockfd < 0)
        {
            continue;
        }

        if (connect(sockfd, (struct sockaddr *)&address, sizeof(address)) == 0)
        {
            
            char log[30];
            sprintf(log,"build connection %d",i);
            clilog.Log(log);
            printf("%s\n", log);
            addfd(epoll_fd, sockfd);
        }
        else{
             printf("%s\n", strerror(errno));
        }
    }
}

void close_conn(int epoll_fd, int sockfd)
{
    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, sockfd, 0);
    close(sockfd);
}

int main(int argc, char *argv[])
{
    assert(argc == 4);
    int epoll_fd = epoll_create(100);
    start_conn(epoll_fd, atoi(argv[3]), argv[1], atoi(argv[2]));
    epoll_event events[10000];
    char buffer[2048];
    while (1)
    {
        int fds = epoll_wait(epoll_fd, events, 10000, 2000);
        for (int i = 0; i < fds; i++)
        {
            int sockfd = events[i].data.fd;
            if (events[i].events & EPOLLIN)
            {
                int length_h;
                read_length(sockfd, &length_h);
                if (!recv_nbytes(sockfd, buffer, length_h))
                {
                    printf("read data error\n");
                    clilog.Log("read data error");
                    close_conn(epoll_fd, sockfd);
                }

                struct epoll_event event;
                event.events = EPOLLOUT | EPOLLET | EPOLLERR;
                event.data.fd = sockfd;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sockfd, &event);
            }
            else if (events[i].events & EPOLLOUT)
            {
                char *buf;
                header myheader;
                make_proto_message(&buf, &myheader);
                if (!send_header(sockfd, &myheader))
                {
                    clilog.Log("send header error");
                    printf("send header error\n");
                }
                if (!send_nbytes(sockfd, buf, myheader.length))
                {
                    clilog.Log("send data error");
                    printf("send data error\n");
                    close_conn(epoll_fd, sockfd);
                }
                struct epoll_event event;
                event.events = EPOLLIN | EPOLLET | EPOLLERR;
                event.data.fd = sockfd;
                epoll_ctl(epoll_fd, EPOLL_CTL_MOD, sockfd, &event);
            }
            else if (events[i].events & EPOLLERR)
            {
                close_conn(epoll_fd, sockfd);
                printf("hi\n");
            }
        }
    }
}
