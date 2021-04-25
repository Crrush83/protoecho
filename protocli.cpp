#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include "UserInfo.pb.h"
#define MAX_CMD_STR 100
using namespace test;
//字符串处理函数，先读一遍再复制

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

int make_proto_message(int fd)
{

    test::UserInfo msg;
    msg.set_name("zxt");
    msg.set_age(18);
    msg.set_stat(IDLE);

    header myheader;
    myheader.version = 2;
    myheader.command = LOGIN_REQUEST;
    myheader.length = sizeof(msg);
    char buff[1024];
    msg.SerializeToArray(buff, 1024);

    int res;
    res = send(fd, &myheader, sizeof(struct msg_proto_header), 0);
    // printf("%d\n", res);

    res = send(fd, buff, sizeof(msg), 0);
    //   printf("%d\n", res);
    return 0;
}
int recv_str(int fd)
{
    int read_size;
    int length_n;
    int length_h;
    int len = 0;
    char *buf;
    for (;;)
    {
        read_size = read(fd, &length_n, 4);
        //rdcnt = read(sockfd, &length_n, sizeof(length_n));
        ///   printf("just read %d\n",rdcnt);
        // length_h = ntohl(length_n);
        len += read_size;
        //      printf("have read %d\n",len);
        //printf("length is (n): %d\n",length_n);
        ///  printf("length is (h): %d\n",ntohl(length_n));
        /// printf("sizeof(length_h): %d\n",sizeof(length_h));
        if (len == sizeof(length_h))
            break;
    }
    //   printf("recieving..\n");
    //记得把字节序调整回来（和前面对应）
    length_h = ntohl(length_n);
    len = 0;
    buf = (char *)malloc(length_h);
    for (;;)
    {
        read_size = read(fd, &*(buf + len), length_h - len);
        len += read_size;
        //  printf("have recieve %d byteS\n",len);
        //
        //strcpy(buf, str(buf,bu));
        if (len == length_h)
        {
            printf("[recv]%s\n", buf);
            break;
        }
    }
    //   printf("and now length is: %d\n",len);
}

int main(int argc, char *argv[])
{
    int res;
    // TODO 定义服务器Socket地址srv_addr；
    struct sockaddr_in server_addr;
    // TODO 定义Socket连接描述符connfd；
    int connfd;
    // 基于argc简单判断命令行指令输入是否正确；

    if (argc != 3)
    {
        printf("Usage:%s <IP> <PORT>\n", argv[0]);
        return 0;
    }

    // TODO 初始化服务器Socket地址srv_addr，其中会用到argv[1]、argv[2]
    /* IP地址转换推荐使用inet_pton()；端口地址转换推荐使用atoi(); */
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));

    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) == 0)
    {
        perror("inet_pton\n");
        exit(1);
    }

    // TODO 获取Socket连接描述符: connfd = socket(x,x,x);
    connfd = socket(AF_INET, SOCK_STREAM, 0);
    do
    {
        // TODO 连接服务器，结果存于res: int res = connect(x,x,x);
        res = connect(connfd, (struct sockaddr *)&server_addr, sizeof(server_addr));
        // 以下代码紧跟connnect()；
        if (res == 0)
        {
            // TODO 连接成功，按题设要求打印服务器端地址server[ip:port]3
            char ip[100];
            inet_ntop(AF_INET, &server_addr.sin_addr.s_addr, ip, sizeof(ip));
            int port = ntohs(server_addr.sin_port);
            printf("connect OK\n", ip, port);
            // TODO 执行业务处理函数echo_rqt();若echo_rqt()返回0，跳出循环准备释放资源，终结程序；
            while (make_proto_message(connfd) == 0)
            {
                printf("[cli]send OK\n");
                recv_str(connfd);
                sleep(0.1);
            }

            printf("send error\n");
        }
        else if (res == -1 && errno == EINTR)
        {
            continue; // 若connect因系统信号中断而失败，则再次执行connect；
        }

    } while (1);

    // TODO 关闭connfd
    close(connfd);
    printf("[cli] connfd is closed!\n");
    printf("[cli] client is going to exit!\n");
    return 0;
}
