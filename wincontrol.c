//控制端:通过服务器转发控制端的命令控制客户端(被控制端)
//将执行的命令发给服务器,接收服务器转发的被控制端执行命令之后的结果
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>      
#include <errno.h>
#include <winsock2.h>
#include <synchapi.h>

#pragma comment(lib, "Ws2_32.lib")

#define MSG_TYPE_LOGIN                          (0)
#define MSG_TYPE_LOGIN_RESULT                   (1)
#define MSG_TYPE_LOGIN_OUT                      (2)
#define MSG_TYPE_HEART                          (3)
#define MSG_TYPE_COMMD                          (4)
#define MSG_TYPE_COMMD_RESULT                   (5)
#define MSG_TYPE_COMMD_LIST_CLIENT              (6)
#define MSG_TYPE_COMMD_LIST_CLIENT_RESULT       (7)

#define SERVER_IP                           ("x.x.x.x") 
#define SERVER_PORT                         (8090)

#define LOGIN_TYPE_CLIENT                   (0)
#define LOGIN_TYPE_CONTROL                  (1)

#define LOGIN_STATE_SUCCESS                 (1)
#define LOGIN_STATE_FAILURE                 (0)


#define HEART_BEAT_INTERVAL                 (30 * 1000)

typedef struct msg {
      int type;
      int length;
}__MSG;
  
 
struct login {
    __MSG msg;
    char type;
    char user[32];
    char password[32];
};

struct login_result {
    __MSG msg;
    int code;
    char msgstr[256];
};

struct heartbeat {
    __MSG msg;
    long long time;
};

struct commd {
    __MSG msg;
    char commdstr[256];
};

struct commd_result {
    __MSG msg;
    char commdstr[256];
};

struct cmd_list_cli {
    __MSG msg;
};

struct cmd_list_cli_result {
    __MSG msg;
    char string[5][32];
};

int     print_login_result_info(struct login_result *login_result_packge);
void    print_client_list_info(struct cmd_list_cli_result *list_cli_packge);
void    print_command_result_info(struct commd_result *cmd_result_packge);
void    send_heartbeat(void *arg);
void    connect_server(struct sockaddr_in *server_addr, SOCKET sockfd);
void    send_login_packge(SOCKET sockfd);
void    send_command(SOCKET sockfd);
int     checkinputfmt(char *inputbuf);
int     checkipport(char *ipportbuf);
void    parse_input_from_control(char *inputbuf, char *commandstr, char *ipportstr);
int     cal_sizeofsendbuf(char *inputbuf);
int     islist(char *buf);
int     recv_full(SOCKET sockfd, char *buf, int len);
int     recv_tlv_buffer(SOCKET sockfd, char *buf);
int     send_tlv_buffer(SOCKET sockfd, char *buf);


int main()
{
    //socket
    int WSAret;
    WSADATA WSAdata;
    SOCKET connectfd = INVALID_SOCKET;
    struct sockaddr_in server_addr;
    HANDLE ht_thread;
    DWORD ht_threadid;
    char recvbuf[4096];
    __MSG *msg = NULL;
    int login_flag = 0, htthreadflag = 0;

    WSAret = WSAStartup(MAKEWORD(2,2), &WSAdata);
    if (WSAret != NO_ERROR) {
        wprintf(L"WSAStartup failed with error: %d\n", WSAret);
        return 1;
    }
    
    connectfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (connectfd == INVALID_SOCKET) {
        wprintf(L"socket failed with error: %ld\n", WSAGetLastError());
        WSACleanup();
        return 1;
    }
    
    //connect server
    connect_server(&server_addr, connectfd);

    //login
    send_login_packge(connectfd);
    
    //send command and recv result
    while(1) {
        
        memset(recvbuf, 0, 4096);
        
        if(recv_tlv_buffer(connectfd, (char *)recvbuf) == 0) {
            break;
        }
        
        msg = (__MSG *)recvbuf;
        
        if(login_flag == 0 && msg->type == MSG_TYPE_LOGIN_RESULT) {
            
            login_flag = print_login_result_info((struct login_result *)recvbuf);

            if(login_flag == 1 && htthreadflag == 0) {
                
                ht_thread = CreateThread(NULL, 0, (void *)send_heartbeat, &connectfd, 0, &ht_threadid);

                if(ht_thread == NULL) {
                    wprintf(L"heart beat thread create error:%d\n", GetLastError());
                    exit(1);
                }

                htthreadflag = 1;

                if(WaitForSingleObject(ht_thread, 0) == WAIT_FAILED) {
                    wprintf(L"WaitForSingleObject failed! error:%d\n", GetLastError());
                    exit(1);
                }
            }

            send_command(connectfd);
            
        } else if(msg->type == MSG_TYPE_COMMD_LIST_CLIENT_RESULT) {
            
            print_client_list_info((struct cmd_list_cli_result *)recvbuf);
            send_command(connectfd);
            
        } else if(msg->type == MSG_TYPE_COMMD_RESULT) {
            
            print_command_result_info((struct commd_result *)recvbuf);
            send_command(connectfd);
        }
        
    }

    //close
    if(closesocket(connectfd) == SOCKET_ERROR) {
        wprintf(L"close failed with error:%d\n", WSAGetLastError());
        WSACleanup();
    }
    WSACleanup();

    return 0;
}

void connect_server(struct sockaddr_in *server_addr, SOCKET sockfd)
{
    server_addr->sin_family = AF_INET;
    server_addr->sin_addr.s_addr = inet_addr(SERVER_IP);
    server_addr->sin_port = htons(SERVER_PORT);
    
    if(connect(sockfd, (SOCKADDR *)server_addr, (int)sizeof(*server_addr)) == SOCKET_ERROR ) {
        wprintf(L"connect failed with error: %d\n", WSAGetLastError());
        closesocket(sockfd);
        WSACleanup();
        exit(-1);
    } else {
        wprintf(L"connect successfully\n");
    }
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
    SOCKET sockfd = *(SOCKET *)arg;
    char sendbuf[16];
    struct heartbeat *heartbeat_packge = (struct heartbeat *)sendbuf;
    
    memset(sendbuf, 0, 16);
    heartbeat_packge->msg.type = MSG_TYPE_HEART;
    heartbeat_packge->msg.length = sizeof(long long);
    heartbeat_packge->time = time(NULL);
    
    while(1) {

        Sleep(HEART_BEAT_INTERVAL);
        send_tlv_buffer(sockfd, (char *)sendbuf);
        
    }

    return ;
}


int islist(char *buf)
{
    return (strncmp(buf, "list", 4) == 0 || strncmp(buf, "LIST", 4) == 0);
}

void send_login_packge(SOCKET sockfd)
{
    struct login        login_packge;
    int i;
    
    login_packge.msg.type = MSG_TYPE_LOGIN;
    login_packge.msg.length = 65;
    login_packge.type = LOGIN_TYPE_CONTROL;
    
    for(i = 0; i < 32; i++) {
        login_packge.user[i] = 0xff;
    }

    for(i = 0; i < 32; i++) {
        login_packge.password[i] = 0xff;
    }
    
    send_tlv_buffer(sockfd, (char *)&login_packge);
    
    return ;
}

void send_command(SOCKET sockfd)
{
    char sendbuf[512];
    char inputbuf[256];
    __MSG *msg = (__MSG *)sendbuf;
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

        parse_input_from_control(inputbuf, cmdbuf,ipportbuf);
    
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



int recv_full(SOCKET sockfd, char *buf, int len)
{
    int hasrecv = 0;
    int ret;
    
    while(hasrecv < len) {
        
        if( (ret = recv(sockfd, buf, len, 0)) > 0) {
            hasrecv = hasrecv + ret;
        } else if(ret == 0) {
            wprintf(L"peer server is shutdown\n");
            break;
        } else {
            wprintf(L"recv failure with error: %d\n", WSAGetLastError());
            break;
        }
    }
    
    return hasrecv;
}

int recv_tlv_buffer(SOCKET sockfd, char *buf)
{
    int n = 0;
    int ret = 0;
    int recvheadflag = 0;
    char *pbuf = buf;
    int headlen = sizeof(__MSG);
    __MSG *msg = (__MSG *)buf;
    
    if((n = recv_full(sockfd, pbuf, headlen)) > 0) {//记得打括号
        if(n == headlen) {
            recvheadflag = 1;
            //wprintf(L"recv the MSG head:%d bytes\n", n);
            ret = 1;
        } else {
            ret = 0;
        }
    }
    if((recvheadflag) && (msg->length)) {
        if((n = recv_full(sockfd, &pbuf[headlen], msg->length)) > 0) {
            if(n == msg->length) {
                //wprintf(L"recv the MSG tail:%d bytes\n", n);
                ret = 1;
            } else {
                ret = 0;
            }
        }
    }

    return ret;
}

int send_full(SOCKET sockfd, char *buf, int len)
{
    int hassend = 0;
    int ret;

    while(hassend < len) {

        if( (ret = send(sockfd, buf, len, 0)) > 0 ) {
            hassend = hassend + ret;                      
        } else if (ret == 0) {
            wprintf(L"peer shutdown!\n");
            break;
        } else {
            wprintf(L"send fairlure with error :%d\n", WSAGetLastError());
            break;
        }
    }

    return hassend;
}

int send_tlv_buffer(SOCKET sockfd, char *buf)
{
    int ret = 0;
    int n = 0;
    int sendheadflag = 0;
    char *pbuf = buf;
    int headlen = sizeof(__MSG);
    __MSG *msg = (__MSG *)buf;
    
    if((n = send_full(sockfd, pbuf, headlen)) > 0) {
        if(n == headlen) {
            //wprintf(L"send the msg head\n");
            sendheadflag = 1;
            ret = 1;
        } else {
            ret = 0;
        }
    }
    
    if((sendheadflag)&& (msg->length)) {
        if((n = send_full(sockfd, &pbuf[headlen], msg->length)) > 0) {
            if(n == msg->length) {
                //wprintf(L"send the msg tail\n");
                ret = 1;
            } else {
                ret = 0;
            }
        }
    }
    
    return ret;
}


















