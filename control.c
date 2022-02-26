//控制端:通过服务器转发控制端的命令控制客户端(被控制端)
//将执行的命令发给服务器,接收服务器转发的被控制端执行命令之后的结果
#include <stdio.h>
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
#include <errno.h>

#define MSG_TYPE_LOGIN                          (0)
#define MSG_TYPE_LOGIN_RESULT                   (1)
#define MSG_TYPE_LOGIN_OUT                      (2)
#define MSG_TYPE_HEART                          (3)
#define MSG_TYPE_COMMD                          (4)
#define MSG_TYPE_COMMD_RESULT                   (5)
#define MSG_TYPE_COMMD_LIST_CLIENT              (6)
#define MSG_TYPE_COMMD_LIST_CLIENT_RESULT       (7)

#define SERVER_IP                           "139.196.120.117"
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
}MSG;
  
 
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

struct cmd_list_cli {
    MSG msg;
};

struct cmd_list_cli_result {
    MSG msg;
    char string[0][32];
};

int     print_login_result_info(struct login_result *login_result_packge);
void    print_client_list_info(struct cmd_list_cli_result *list_cli_packge);
void    print_command_result_info(struct commd_result *cmd_result_packge);
void    send_heartbeat(void *arg);
void    send_login_packge(int sockfd);
void    send_command(int sockfd);
int     checkinputfmt(char *inputbuf);
int     checkipport(char *ipportbuf);
void    parse_input_from_control(char *inputbuf, char *commandstr, char *ipportstr);
int     cal_sizeofsendbuf(char *inputbuf);
int     islist(char *buf);
int     recv_full(int sockfd, char *buf, int len);
void    recv_tlv_buffer(int sockfd, char *buf);
void    send_tlv_buffer(int sockfd, char *buf);


int main()
{
    //socket
    int sockfd;
    int login_flag = 0;
    int htthreadflag = 0;
    pthread_t ht_thread = 1;
    struct sockaddr_in serveraddr;
    MSG *msg = NULL; 
    char recvbuf[4096];
    
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    
    if(sockfd <= 0) {
        printf("socket failure!\n");
        exit(-1);
    }
    
    //connect server
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serveraddr.sin_port = htons(SERVER_PORT);
    
    if( connect(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0) {
        perror("connect failure:");
        close(sockfd);
        exit(-1);
    } else {
        printf("connect successfully!\n");
    }

    //login
    send_login_packge(sockfd);
    
    //send command and recv result
    while(1) {
        
        memset(recvbuf, 0, 4096);
        
        recv_tlv_buffer(sockfd, (char *)recvbuf);
        
        msg = (MSG *)recvbuf;
        
        if(login_flag == 0 && msg->type == MSG_TYPE_LOGIN_RESULT) {
            //(struct login_result *)buf;
            
            login_flag = print_login_result_info((struct login_result *)recvbuf);

            if(login_flag == 1 && htthreadflag == 0) {
                if(pthread_create(&ht_thread, NULL, (void *)send_heartbeat, &sockfd) != 0) {
                    printf("ht_thread create failure\n");
                }
                pthread_detach(ht_thread);\
                htthreadflag = 1;
            }

            send_command(sockfd);
            
        } else if(msg->type == MSG_TYPE_COMMD_LIST_CLIENT_RESULT) {
            //(struct cmd_list_cli_result *)recvbuf;
            
            print_client_list_info((struct cmd_list_cli_result *)recvbuf);
            send_command(sockfd);
            
        } else if(msg->type == MSG_TYPE_COMMD_RESULT) {
            //(struct commd_result *)recvbuf;
            
            print_command_result_info((struct commd_result *)recvbuf);
            send_command(sockfd);
        }
        
    }

    //close
    return 0;
}


int print_login_result_info(struct login_result *login_result_packge)
{
    int flag = 0;
    if(login_result_packge->code == LOGIN_STATE_SUCCESS) {
        flag = 1;
        printf("login result info:%s\n", login_result_packge->msgstr);
    }    
    
    return flag;
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


int islist(char *buf)
{
    return (strncmp(buf, "list", 4) == 0 || strncmp(buf, "LIST", 4) == 0);
}

void send_login_packge(int sockfd)
{
    struct login        login_packge;
    int i;
    
    login_packge.msg.type = MSG_TYPE_LOGIN;
    login_packge.msg.length = 65;
    login_packge.type = LOGIN_TYPE_CONTROL;
    
    for(i = 1; i < 65; i++) {
        login_packge.msg.value[i] = 0xff;
    }
    
    send_tlv_buffer(sockfd, (char *)&login_packge);
    
    return ;
}

void send_command(int sockfd)
{
    char sendbuf[512];
    char inputbuf[256];
    MSG *msg = (MSG *)sendbuf;
    struct commd *command = (struct commd *)sendbuf;
    
    
    memset(sendbuf, 0, 512);
    
    
    while(1) {
        
        printf("input your command:\n");
        memset(inputbuf, 0, 256);
        fgets(inputbuf, 256, stdin);
        //scanf("%s", inputbuf);//"%s"format有问题
    
        if(checkinputfmt(inputbuf)) {    
        
            if(islist(inputbuf)) {
            
                msg->type= MSG_TYPE_COMMD_LIST_CLIENT;
                msg->length = 0;
            
                send_tlv_buffer(sockfd, (char *)sendbuf);
            
            } else {
            
                msg->type = MSG_TYPE_COMMD;
                msg->length = cal_sizeofsendbuf(inputbuf);
                strncpy(command->commdstr, inputbuf, 256); 
            
                send_tlv_buffer(sockfd, (char *)sendbuf);
            }
            
            break;
        }
    }
    
    return ;
}

int checkinputfmt(char *inputbuf)
{
    int ret = 0;
    char cmdbuf[256 - 32];
    char ipportbuf[32];
    
    sscanf(inputbuf, "%s\n", cmdbuf);
    
    if(!islist(cmdbuf)) {

        parse_input_from_control(inputbuf, cmdbuf, ipportbuf);

        printf("you input cmd: %s, ipport: %s\n", cmdbuf, ipportbuf);
        
        if(checkipport(ipportbuf)) {
            ret = 1;
        }
    } else {
        ret = 1;
    }
    
    return ret;
}

void parse_input_from_control(char *inputbuf, char *commandstr, char *ipportstr)
{
    int i = 0;

    for(i = strlen(inputbuf) - 1; inputbuf[i] != ' '; i--) {
        if(inputbuf[i] == '\n') {
            inputbuf[i] = '\0';
        }
    }

    strncpy(ipportstr, &inputbuf[i + 1], strlen(&inputbuf[i + 1]) + 1);

    inputbuf[i] = '\0';
    strncpy(commandstr, inputbuf, strlen(inputbuf) + 1);

    sprintf(inputbuf, "%s %s", commandstr, ipportstr);

    return ;
}

int checkipport(char *ipportbuf) 
{
    int ret = 0;
    unsigned int num[4];
    
    if(4 == sscanf(ipportbuf, "%d.%d.%d.%d", num, &num[1], &num[2], &num[3])) {
        ret = 1;
    }
    
    return ret;
}

int cal_sizeofsendbuf(char *inputbuf)
{
    int ret = 0;
    
    int i = 0;
    
    while(inputbuf[i] != '\0') {
        i++;
    }
    
    ret = i + 1;
    
    return ret;
}


void print_client_list_info(struct cmd_list_cli_result *list_cli_packge)
{
    int i = 0;
    
    int n = list_cli_packge->msg.length / 32;
    
    for(i = 0; i < n; i++) {
        printf("%s\n", list_cli_packge->string[i]);
    }
    
}

void print_command_result_info(struct commd_result *cmd_result_packge)
{
    printf("%s\n", cmd_result_packge->commdstr);
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
    if(msg->length > 0) {
        if((n = recv_full(sockfd, &pbuf[headlen], msg->length)) > 0) {
            if(n == msg->length) {
                printf("recv the msg tail:%d bytes\n", n);
            }
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
            //printf("send the msg head\n");
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
                //printf("send the msg tail\n");
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


















