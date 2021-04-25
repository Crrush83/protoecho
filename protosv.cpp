#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/epoll.h>
#include <pthread.h>
#include "UserInfo.pb.h"
#include <fstream>
#include <stdlib.h>
#include <cstdlib>
#include <typeinfo>
#include <iostream>
#include <string>
#include <sstream>
#include "minilogger.h"
#include <map>

#define MAX_EVENT_NUMBER 1000
#define BUFFER_SIZE 1024

#define MAXCLIENT 1000000
#define MAXREAD 10
#define MAX_MSG_ONE_TIME 100

using namespace std;
using namespace test;

typedef struct msg_proto_header
{
    uint8_t version;
    uint16_t command;
    uint32_t length;
} msg_header;
enum MSG_COMMOND
{
    LOGIN_REQUEST,
};

//TODO 数组：全局变量 fd有效 总长度 已接受数据长度 buf
//int done[MAXCLIENT] = {0};
//已经收到的长度
//char *bufs[MAXCLIENT] = {0};
msg_header msg_headers[MAXCLIENT];
char *msg[MAXCLIENT];
int msg_length[MAXCLIENT] = {0};
int have_msg_length[MAXCLIENT] = {0};
int need_msg_length[MAXCLIENT] = {0};
int header_exist[MAXCLIENT] = {0};
int data_exist[MAXCLIENT] = {0};
Logger<TextDecorator> svrlog("mylogfile.txt", "this is title!", true, true); //创建logger
std::map<int, int> fd_pt;
//std::map<>

int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

void addfd(int epollfd, int fd, bool enable_et)
{
    epoll_event event;
    event.data.fd = fd;
    event.events = EPOLLIN;
    if (enable_et)
    {
        event.events |= EPOLLET;
    }
    //新加的
    //event.events |= EPOLLONESHOT;
    //
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

void get_header(int fd)
{
    int read_size = 0;
    int read_header_length = 0;
    for (;;)
    {
        read_size = recv(fd, &msg_headers[fd] + read_header_length, sizeof(msg_header), 0);
        if (read_size < 0)
        {
            if (errno == EAGAIN)
                continue;
        }
        else if (read_size == 0)
        {
            if (read_header_length == sizeof(msg_header))
            {
                msg_length[fd] = msg_headers[fd].length;
                break;
            }
            else
                return;
        }
        else
        {
            read_header_length += read_size;
            if (read_header_length == sizeof(msg_header))
            {
                msg_length[fd] = msg_headers[fd].length;
                break;
            }
            else
                continue;
        }
    }
}
//清理工作：header存在位 have长度 need长度已经==0 msg_length置0 释放msg空间
void done(int fd)
{
    test::UserInfo myprotobuf;
    //总长度
    int len = msg_length[fd];
    //  printf("%s", msg[fd]);
    myprotobuf.ParseFromArray(msg[fd], msg_length[fd]);
    printf("[parse][sockfd:%d]", fd);

    string name = myprotobuf.name();
    std::stringstream ss;
    std::string age;
    ss << myprotobuf.age();
    ss >> age;
    string ret = "name:" + name + "\t" + "age:" + age;

    char *r = (char *)malloc(len);
    r = (char *)ret.c_str();

    //打日志
    char pt[20];
    char logtxt[20 + len];
    //printf("%d",fd_pt[fd]);
    sprintf(logtxt, "[port : %d][fd : %d] %s", fd_pt[fd],fd,r);
    svrlog.Log(logtxt);
    int size_h = len;
    //int size_h = std::strlen(r);
    int size_n = htonl(size_h);

    printf("%s\n", r);
    send(fd, &size_n, sizeof(size_n), 0);
    send(fd, r, size_h, 0);
    printf("[send back] OK\n");
    msg_length[fd] = 0;
    have_msg_length[fd] = 0;
    header_exist[fd] = 0;
    data_exist[fd] = 0;
}
void get(int fd)
{
    int read_size = 0;
    int res = 0;
    for (;;)
    {
        read_size = recv(fd, msg[fd] + have_msg_length[fd], MAX_MSG_ONE_TIME, 0);
        if (read_size < 0)
        {
            if (errno == EAGAIN)
                continue;
        }
        else if (read_size == 0)
            break;
        else
        {
            //printf("recieve %d\n", read_size);
            have_msg_length[fd] += read_size;
            need_msg_length[fd] -= read_size;
            break;
        }
    }
    if (need_msg_length[fd] == 0)
    {
        // printf("%d", have_msg_length[fd]);
        done(fd);
    }
}
void set_and_get(int fd)
{
    data_exist[fd] = 1;
    msg[fd] = (char *)malloc(msg_length[fd]);
    need_msg_length[fd] = msg_length[fd];
    get(fd);
}

void recv_proto(int fd)
{
    if (header_exist[fd] == 0)
    {
        get_header(fd);
        header_exist[fd] = 1;
    }
    if (msg_headers[fd].command == LOGIN_REQUEST && msg_length[fd] > 0)
    {
        //第一次
        if (!data_exist[fd])
            set_and_get(fd);
        //非第一次
        else
            get(fd);
        //检查是否已经完成
        //在get中调用done（）
    }
}
//水平触发的读取函数
void lt(epoll_event *events, int number, int epollfd, int listenfd)
{
    char buf[BUFFER_SIZE];
    for (int i = 0; i < number; i++)
    {
        int sockfd = events[i].data.fd;
        if (sockfd == listenfd)
        {
            struct sockaddr_in client_address;
            socklen_t client_addrlength = sizeof(client_address);
            int connfd = accept(listenfd, (struct sockaddr *)&client_address, &client_addrlength);
            addfd(epollfd, connfd, false);

            //获取ip
            char ip[100];
            inet_ntop(AF_INET, &client_address.sin_addr.s_addr, ip, sizeof(ip));
            //获取port
            int port_cli = htons(client_address.sin_port);
            char port1[100];
            sprintf(port1, "%d", htons(client_address.sin_port));
            char log[100];
            sprintf(log, "client[%s:%s] is accepted!", ip, port1);
            svrlog.Log(log);
            //fd_pt.insert(sockfd, (int)htons(client_address.sin_port));
            fd_pt.insert(std::pair<int,int>(sockfd, htons(client_address.sin_port)));
           // printf("%d",htons(client_address.sin_port));
            //printf("%d",fd_pt[sockfd]);
            char login[40];
            sprintf(login,"新连接到来：分配描述符%d", connfd);
            printf("%s\n",login);
            svrlog.Log(login);


            
        }
        else if (events[i].events & EPOLLIN)
        {

            recv_proto(sockfd);
        }
        else
        {
            printf("something else happened \n");
        }
    }
}
int main(int argc, char *argv[])
{
    if (argc <= 2)
    {
        printf("usage: %s ip_address port_number\n", basename(argv[0]));
        return 1;
    }
    const char *ip = argv[1];
    int port = atoi(argv[2]);

    int ret = 0;
    struct sockaddr_in address;
    bzero(&address, sizeof(address));
    address.sin_family = AF_INET;
    inet_pton(AF_INET, ip, &address.sin_addr);
    address.sin_port = htons(port);

    int listenfd = socket(PF_INET, SOCK_STREAM, 0);
    assert(listenfd >= 0);

    ret = bind(listenfd, (struct sockaddr *)&address, sizeof(address));
    assert(ret != -1);

    ret = listen(listenfd, 5);
    assert(ret != -1);
    //Logger<TextDecorator> svrlog("svrCreate.log","this is title!",true,true);//创建logger
    svrlog.Log("create svr OK");

    epoll_event events[MAX_EVENT_NUMBER];
    int epollfd = epoll_create(5);
    assert(epollfd != -1);
    addfd(epollfd, listenfd, true);

    while (1)
    {
        int ret = epoll_wait(epollfd, events, MAX_EVENT_NUMBER, -1);
        if (ret < 0)
        {
            svrlog.Log("epoll failure");
            printf("epoll failure\n");
            break;
        }

        lt(events, ret, epollfd, listenfd);
    }

    close(listenfd);
    return 0;
}
