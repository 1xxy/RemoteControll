//服务器：用来管理客户端(被控制端), 或接受控制端的命令并把命令转发给客户端，
//        再把执行后结果返回给控制端。
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

#define SERVER_PORT                         (8090)

#define LOGIN_TYPE_CLIENT                   (0)
#define LOGIN_TYPE_CONTROL                  (1)

#define LOGIN_STATE_SUCCESS                 (1)
#define LOGIN_STATE_FAILURE                 (0)

#define RESET_THREAD_CREATED	            (1)
#define RESET_THREAD_UNCREATED	            (0)

#define RESET_TIME_INTERVAL		            (3)

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

struct List_cli {
    int fd;
    int status;
    long origin_time;
    struct List_ctrol *pointtoctr;
    struct List_cli *next;    
    char string[32];    
    //pthread_rwlock_t rwlock;    
};

struct List_ctrol {
    char string[32];
    int fd;
    struct List_ctrol *next;
}; 

typedef struct arg_type {
    int confd;
    struct sockaddr_in ClientorControlAddr;
    struct login login_packge;
}arg_type;

static struct List_cli *cli_list_head = NULL;
static struct List_ctrol *control_list_head = NULL;

pthread_rwlock_t rwlock_cli = PTHREAD_RWLOCK_INITIALIZER;
pthread_rwlock_t rwlock_ctr = PTHREAD_RWLOCK_INITIALIZER;

void                deal_opposite_request(void *args);
int                 isClient(struct login *CliorCtro_Login);
int                 isControl(struct login *CliorCtro_Login);
int                 check_user_pwd(struct login *CliorCtro_Login);
void                pushtoList_cli(struct List_cli **H, int fd, struct sockaddr_in *cliorcontroladdr);
void                pushtoList_control(struct List_ctrol **H, int fd, struct sockaddr_in *cliorcontroladdr);
void                makelist_client_packge(struct cmd_list_cli_result *buf);
struct List_cli*    locateclient(char *ipportstr);
void                make_login_result_packge(struct login_result *login_result_packge);
void                do_transpondctr(int confd, struct List_cli *pointtocli, struct List_ctrol *pointtoctr, char *commandstr, char *sendbuf);
void                send_noclient(int confd, char *sendbuf);
void                do_transpondcli(struct List_cli *pointtocurrent_cli, int confd/*, char *recvbuf*/);
struct List_cli*    locatecurrent_cli(int confd);
struct List_ctrol*  locatecurrent_control(int confd);
int                 check_login(int confd, char *recvbuf);
void                do_control_request(int confd, char *buffer);
void                send_client_list(int confd, char *sendbuf);
void                parse_cmd_from_control(char *recvbuf, char *ipportstr, char *commandstr);
void                parse_input_from_control(char *inputbuf, char *commandstr, char *ipportstr);
void                makecommand_packge(struct commd *command_packge, char *commandstr);
void                make_command_result_busy(struct commd_result *commd_result_packge);
void                reset(void *arg);
int                 recv_full(int sockfd, char *buf, int len);
int                 send_full(int sockfd, char *buf, int len);
int                 recv_tlv_buffer(int sockfd, char *buf);
void                send_tlv_buffer(int sockfd, char *buf);
void                send_login_result(int sockfd, char *sendbuf);
void                clear_resource_cli(struct List_cli *pointtocurrent_cli);
void                clear_resource_ctr(struct List_ctrol *pointtocurrent_ctr);
//void                send_client_shutdown(int control_fd);
struct List_cli*    locateprecli(struct List_cli *pointtocurrent_cli);
struct List_ctrol*  locateprectr(struct List_ctrol *pointtocurrent_ctr); 


int main(int argc, char *argv[])
{
    
    int                 sockfd, confd;
    struct sockaddr_in  serveraddr;
    struct sockaddr_in  ClientorControlAddr;
    char                recvbuf[128];
    arg_type            *pargs = NULL;
    
    socklen_t cliaddrlen = sizeof(ClientorControlAddr);
    
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    
    if( sockfd < 0 ) {
        printf("creat socket fd error!\n");
        exit(-1);
    }
    
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(SERVER_PORT);
    
    
    if( bind(sockfd, (struct sockaddr *)&serveraddr, sizeof(serveraddr)) < 0 ) {
        printf("bind error!\n");
        exit(-1);
    }
    
    listen(sockfd, 10);

    while(1) {
        
        if( (confd = accept(sockfd, (struct sockaddr *)&ClientorControlAddr, &cliaddrlen)) < 0 ) {
            perror("accept error!\n");
            continue;
        } else {
            
            printf("\n accept from host %s \n\n", inet_ntoa(ClientorControlAddr.sin_addr));
            
            pthread_t new_thread;
            
            if(check_login(confd, recvbuf)) {
                
                printf("login from :%s\n", inet_ntoa(ClientorControlAddr.sin_addr));
                
                pargs = (arg_type *)malloc(sizeof(arg_type));

                if(pargs == NULL) {
                    printf("heap have no room!\n");
                    exit(0);
                }

                pargs->confd = confd;
                pargs->ClientorControlAddr = ClientorControlAddr;
                pargs->login_packge = *(struct login *)recvbuf;
            
                if(pthread_create(&new_thread, NULL, (void*)&deal_opposite_request, pargs) != 0) {
                
                    printf("pthread_create error!\n");
                    break;
                }
            
                pthread_detach(new_thread);
               
                
            } else {
                close(confd);
            }
        }
    }
    
    
    close(sockfd);
    
    return 0;
}

int check_login(int confd, char *recvbuf)
{
    int i = 0;
    
    memset(recvbuf, 0, 128);
    (void)recv_tlv_buffer(confd, (char *)recvbuf);
    
    if(check_user_pwd((struct login *)recvbuf)) {
        printf("password check successfully\n");
        i = 1;
    } else {
        printf("deny illeagl login\n");
        i = 0;
    }
    
    return i;
}

void deal_opposite_request(void *args)
{
    arg_type            *arg = (arg_type *)args;
    
    int                 confd = arg->confd;
    struct sockaddr_in  ClientorControlAddr = arg->ClientorControlAddr;
    struct login        login_packge = arg->login_packge;
    
    //login
    char                            portbuf[6]; 
    char                            buffer[4096];
    //transpond
    struct List_cli                 *pointtocli = NULL;
    struct List_cli                 *pointtocurrent_cli = NULL;


    
    
    if(isClient(&login_packge)) {
          
        send_login_result(confd, buffer);
            
        pthread_rwlock_wrlock(&rwlock_cli);
        pushtoList_cli(&cli_list_head, confd, &ClientorControlAddr);
        pthread_rwlock_unlock(&rwlock_cli);
            
        memset(portbuf, 0, sizeof(portbuf));
        sprintf(portbuf, "%d", ntohs(ClientorControlAddr.sin_port));
            
        printf("Client--%s:%s login successfully\n", inet_ntoa(ClientorControlAddr.sin_addr), portbuf);
            
        pthread_rwlock_rdlock(&rwlock_cli);
        pointtocurrent_cli = locatecurrent_cli(confd);
        pthread_rwlock_unlock(&rwlock_cli);
        
        if(pointtocurrent_cli == NULL) {
            exit(-1);
        }
        
        do_transpondcli(pointtocurrent_cli, confd/*, buffer*/);
        
        clear_resource_cli(pointtocurrent_cli);

        if(arg != NULL) {
            free(arg);
            arg = NULL;
        }
        
    } else {
        
        if(isControl(&login_packge)) {
            
            send_login_result(confd, buffer);
            
            pthread_rwlock_wrlock(&rwlock_ctr);
            pushtoList_control(&control_list_head, confd, &ClientorControlAddr);
            pthread_rwlock_unlock(&rwlock_ctr);
                
            memset(portbuf, 0, sizeof(portbuf));
            sprintf(portbuf, "%d", ntohs(ClientorControlAddr.sin_port));
                
            printf("Control--%s:%s login successfully\n", inet_ntoa(ClientorControlAddr.sin_addr), portbuf);
                                                           
                
            do_control_request(confd, buffer);

            if(arg != NULL) {
                free(arg);
                arg = NULL;
            }

        } else {
            printf("can not identify opposite terminal!\n");
            close(confd);
        }
    
    }
    
    return ;
    
}


void clear_resource_cli(struct List_cli *pointtocurrent_cli)
{   
    struct List_cli *pretocurrentcli = NULL;
    
    pretocurrentcli = locateprecli(pointtocurrent_cli);
    
    if(pretocurrentcli == NULL) {
        printf("client list is empty!\n");
        return ;
    }
    pthread_rwlock_wrlock(&rwlock_cli);
    pretocurrentcli->next = pointtocurrent_cli->next;
    pthread_rwlock_unlock(&rwlock_cli);

    printf("delete cli node : %p\n", pointtocurrent_cli);

    if(pointtocurrent_cli != NULL) {
         free(pointtocurrent_cli);
         pointtocurrent_cli = NULL;
    }

    return ;
}

struct List_cli* locateprecli(struct List_cli *pointtocurrent_cli) 
{
    struct List_cli *pointtocli = cli_list_head;//head node

    if(pointtocli == NULL) {
        return pointtocli;
    }
    pthread_rwlock_rdlock(&rwlock_cli);
    while(pointtocli->next != NULL) {

        if(pointtocli->next == pointtocurrent_cli) {
           break; 
        }

        pointtocli = pointtocli->next;
    }
    printf("cli-pre->next: %p\n", pointtocli->next);
    pthread_rwlock_unlock(&rwlock_cli);


    return pointtocli;
}

void clear_resource_ctr(struct List_ctrol *pointtocurrent_ctr)
{   
    struct List_ctrol *pretocurrentctr = NULL;
    
    pretocurrentctr = locateprectr(pointtocurrent_ctr);
    
    if(pretocurrentctr == NULL) {
        printf("control list is empty!\n");
        return ;
    }

    pthread_rwlock_wrlock(&rwlock_ctr);
    pretocurrentctr->next = pointtocurrent_ctr->next;
    pthread_rwlock_unlock(&rwlock_ctr);

    printf("delete ctr node: %p\n", pointtocurrent_ctr);
    if(pointtocurrent_ctr != NULL) {
         free(pointtocurrent_ctr);
         pointtocurrent_ctr = NULL;
    }

    return ;
}

struct List_ctrol* locateprectr(struct List_ctrol *pointtocurrent_ctr) 
{
    struct List_ctrol *pointtoctr = control_list_head;//head node

    if(pointtoctr == NULL) {
        return pointtoctr;
    }

    pthread_rwlock_rdlock(&rwlock_ctr);
    while(pointtoctr->next != NULL) {

        if(pointtoctr->next == pointtocurrent_ctr) {
           break; 
        }

        pointtoctr = pointtoctr->next;
    }
    printf("ctr-pre->next: %p\n", pointtoctr->next);
    pthread_rwlock_unlock(&rwlock_ctr);

    return pointtoctr;
}

int isClient(struct login *CliorCtro_Login)
{
    struct login *Login = CliorCtro_Login;

    return (Login->msg.type == MSG_TYPE_LOGIN) && (Login->type == LOGIN_TYPE_CLIENT);
}

int isControl(struct login *CliorCtro_Login)
{
    struct login *Login = CliorCtro_Login;
       
    return (Login->msg.type == MSG_TYPE_LOGIN) && (Login->type == LOGIN_TYPE_CONTROL);
}

int check_user_pwd(struct login *CliorCtro_Login) 
{
    int i;
    int n;
    
    for(i = 1; i < 65; i++) {
        if(CliorCtro_Login->msg.value[i] != 0xff) {
            break;
            n = 0;
        }
    }
    if(i == 64) {
        n = 1;
    }
    
    return n;
}

void send_login_result(int sockfd, char *sendbuf)
{
    
    memset(sendbuf, 0, 4096);
    
    struct login_result *msg = (struct login_result *)sendbuf;
    
    msg->msg.type = MSG_TYPE_LOGIN_RESULT;
    msg->msg.length = 256 + 4;
    msg->code = LOGIN_STATE_SUCCESS;
    sprintf(msg->msgstr, "%s\n", "the client/control has logined");
    
    printf("sending login_result_packge\n");
    send_tlv_buffer(sockfd, (char *)sendbuf);
    
    return ;
}

void pushtoList_cli(struct List_cli **H, int fd, struct sockaddr_in *cliorcontroladdr)
{
    struct List_cli *p = *H;
    struct List_cli *s = NULL;
    char buf[32];
    char portbuf[6];
    
    if(p == NULL) {
        //HEAD node
        *H = (struct List_cli *)malloc(sizeof(struct List_cli));
        if(*H == NULL) {
            printf("pushtoList_cli malloc error\n");
            exit(-1);
        }
        (*H)->next = NULL;
    }
    p = *H;
    while(p->next != NULL) {
            p = p->next;
    }
      
    s = (struct List_cli *)malloc(sizeof(struct List_cli));
    
    if(s == NULL) {
        printf("pushtoList_cli malloc error\n");
        exit(-1);
    }
    bzero(buf, sizeof(buf));
    bzero(portbuf, sizeof(portbuf));
    
    sprintf(portbuf, "%d", ntohs(cliorcontroladdr->sin_port));
    sprintf(buf, "%s:%s", inet_ntoa(cliorcontroladdr->sin_addr), portbuf);
    
    s->fd = fd;
    strncpy(s->string, buf, sizeof(buf));
    s->origin_time = 0; 
    s->status = 0;
    s->pointtoctr = NULL;
    //pthread_rwlock_init(&(s->rwlock), NULL);
    
    s->next = NULL;
    p->next = s;
    
}

void pushtoList_control(struct List_ctrol **H, int fd, struct sockaddr_in *cliorcontroladdr)
{
    struct List_ctrol *p = *H;
    struct List_ctrol *s = NULL;
    char buf[32];
    char portbuf[6];
    
    if(p == NULL) {
        //HEAD node
        *H = (struct List_ctrol *)malloc(sizeof(struct List_ctrol));
        (*H)->next = NULL;
        
    }
    p = *H;
    
    while(p->next != NULL) {
        p = p->next;
    }
        
    s = (struct List_ctrol *)malloc(sizeof(struct List_ctrol));
    bzero(buf, sizeof(buf));
    bzero(portbuf, sizeof(portbuf));
    
    sprintf(portbuf, "%d", ntohs(cliorcontroladdr->sin_port));
    sprintf(buf, "%s:%s", inet_ntoa(cliorcontroladdr->sin_addr), portbuf);
    
    s->fd = fd;
    strncpy(s->string, buf, sizeof(buf));
    
    s->next = NULL;
    p->next = s;
}

void makelist_client_packge(struct cmd_list_cli_result *buf)
{
    struct List_cli *p = cli_list_head;
    int i = 0;
    char buffer[32];
    
    
    
    if(p == NULL || (p != NULL) && (p->next == NULL)) {//如果当前暂时没有客户端连接上线(被控制端)，就构造空包 或者client都离线了
        
        buf->msg.type = MSG_TYPE_COMMD_LIST_CLIENT_RESULT;
        memset(buffer, 0, sizeof(buffer));
        
        sprintf(buffer, "%s", "the client list is empty now\n");
        strncpy(buf->string[0], buffer, sizeof(buffer));
        
        buf->msg.length = 32;
    } else {
        
        pthread_rwlock_rdlock(&rwlock_cli);
        while (p->next != NULL) {
            p = p->next;
            i++;
        }
        pthread_rwlock_unlock(&rwlock_cli);
        
        buf->msg.type = MSG_TYPE_COMMD_LIST_CLIENT_RESULT;
        buf->msg.length = 32 * i;
        
        pthread_rwlock_rdlock(&rwlock_cli);
        p = cli_list_head;
        i = 0;
        
        while(p->next != NULL) {
            p = p->next;
            strncpy(buf->string[i], p->string, sizeof(p->string));
            i++;
        }
        
        pthread_rwlock_unlock(&rwlock_cli);
    }
    
    return ;
}

struct List_cli* locateclient(char *ipportstr)
{
    struct List_cli *p = cli_list_head;
    
    if(p == NULL) {
        return p;
    } else {
        
        pthread_rwlock_wrlock(&rwlock_cli);
        p = p->next;
        while(p != NULL) {
            if(0 == strncmp(ipportstr, p->string, 32)) {
                printf("has find the client!!\n");
                break;
            } 
            p = p->next;
        }
        pthread_rwlock_unlock(&rwlock_cli);
        
        if(p == NULL) {
            printf("-----------------------------ipportstr is uncorrect!\n");
        }
        return p;
    }
    
}



void do_control_request(int confd, char *buffer)
{
    struct List_cli *pointtocli = NULL;
    struct List_ctrol *pointtoctr = NULL;

    char ipportstr[32];
    char commandstr[256];
    int n = 0;
    MSG *msg = (MSG *)buffer;

    pointtoctr = locatecurrent_control(confd);

    while(1) {
        
        memset(buffer, 0, 4096);
        
        if(recv_tlv_buffer(confd, (char *)buffer) == 0) {
            break;
        }
       
        if(msg->type == MSG_TYPE_HEART) {
            printf("recv a heartbeat from control\n");
            continue;
        }
        
        if(msg->type == MSG_TYPE_COMMD_LIST_CLIENT) {
            printf("recv a list command from control\n");
            
            send_client_list(confd, buffer);
        } else if(msg->type == MSG_TYPE_COMMD && msg->length <= 4096) {
            printf("recv a common command from control\n");
            
            printf("-------------------------------------------parseing command...\n");
            parse_cmd_from_control((char *)buffer, ipportstr, commandstr);
            printf("--------------------------------------------above is the command\n");
            
            pointtocli = locateclient(ipportstr);
            
            do_transpondctr(confd, pointtocli, pointtoctr, commandstr, (char *)buffer);
                
        } 

    }

    clear_resource_ctr(pointtoctr);
    return ;
}

void send_client_list(int confd, char *sendbuf)
{
    int i = 0;
    int n = 0;
    struct cmd_list_cli_result *list_cli_result = (struct cmd_list_cli_result *)sendbuf;
    
    memset(sendbuf, 0, 4096);
    
    makelist_client_packge(list_cli_result);
    
    send_tlv_buffer(confd, (char *)sendbuf);
    
    printf("sending client list to control...\n");
    
    n = list_cli_result->msg.length / 32;
    
    printf("the clients are:\n");
    
    for(i = 0; i < n; i++) {
        printf("%s\n",list_cli_result->string[i]);
    }
    
    return ;
}

//将来自控制端的命令 解析成command命令 + ip:port字符串
void parse_cmd_from_control(char *recvbuf, char *ipportstr, char *commandstr)
{
    char commandstrbuf[256];
    struct commd *command_packge = (struct commd *)recvbuf;
    
    memset(commandstrbuf, 0 , 256);
    strncpy(commandstrbuf, command_packge->commdstr, 256);
    
    printf("from a control:%s\n", command_packge->commdstr);
    
    //sscanf(commandstrbuf, "%s %s\n", commandstr, ipportstr);
    parse_input_from_control(commandstrbuf, commandstr, ipportstr);

    printf("commandstr: %s ipportstr: %s\n", commandstr, ipportstr);
    
    return ;
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

}

void do_transpondctr(int confd, struct List_cli *pointtocli, struct List_ctrol *pointtoctr, char *commandstr, char *sendbuf)
{   

    struct commd *command_packge = (struct commd *)sendbuf;
    struct commd_result *commd_result_packge = (struct commd_result *)sendbuf;

    if(pointtoctr == NULL) {
        printf("locate control failure!\n");
        return ;
    }

    if(pointtocli == NULL) {
        printf("locate client failure!\n");

        send_noclient(confd, (char *)sendbuf);
    } else {
        
        makecommand_packge(command_packge, commandstr);
        
        if(pointtocli->status != 1) {
            
            pthread_rwlock_wrlock(&rwlock_cli);
            pointtocli->status = 1;
            pointtocli->pointtoctr = pointtoctr;
            pointtocli->origin_time = time(NULL);
            pthread_rwlock_unlock(&rwlock_cli);
           
            printf("sending command to client...\n");
            //pthread_rwlock_wrlock(&(pointtocli->rwlock));
            send_tlv_buffer(pointtocli->fd, (char *)sendbuf);
            //pthread_rwlock_unlock(&(pointtocli->rwlock));
            
        } else if (pointtocli->status == 1 && pointtocli->pointtoctr != NULL) {
            
            if(pointtocli->pointtoctr->fd == confd) {
                
                printf("sending command to client...\n");
                //pthread_rwlock_wrlock(&(pointtocli->rwlock));            
                send_tlv_buffer(pointtocli->fd, (char *)sendbuf);
                //pthread_rwlock_unlock(&(pointtocli->rwlock));
                
            } else {
                make_command_result_busy(commd_result_packge);
                
                printf("the client is busy now, sending the msg to the control\n");
                
                //pthread_rwlock_wrlock(&(pointtocli->rwlock));            
                send_tlv_buffer(confd, (char *)sendbuf);
                //pthread_rwlock_unlock(&(pointtocli->rwlock));

            }
        }
     }
   
   return ;
}

void send_noclient(int confd, char *sendbuf)
{
    char no_client[36];
    struct commd_result *command_result_no_client = (struct commd_result *)sendbuf;
    memset(no_client, 0, 36);
    sprintf(no_client, "%s", "the client you input is not exist!\n");

    command_result_no_client->msg.type = MSG_TYPE_COMMD_RESULT;
    command_result_no_client->msg.length = 36;
    strncpy(command_result_no_client->commdstr, no_client, 36);

    send_tlv_buffer(confd, (char *)sendbuf);

    return ;
}

void makecommand_packge(struct commd *command_packge, char *commandstr)
{
    command_packge->msg.type = MSG_TYPE_COMMD;
    
    strncpy(command_packge->commdstr, commandstr, 256);
    
    command_packge->msg.length = 256;
    
    return ;
}

void make_command_result_busy(struct commd_result *commd_result_packge)
{
    commd_result_packge->msg.type = MSG_TYPE_COMMD_RESULT;
    commd_result_packge->msg.length = 256;

    sprintf(commd_result_packge->commdstr, "%s", "the client is busy!\n");

    return ;
}

void do_transpondcli(struct List_cli *pointtocurrent_cli, int confd/*, char *recvbuf*/)
{
    //int thread_create_flag = RESET_THREAD_UNCREATED;
    int status = 0;
    //pthread_t reset_thread = -1;
    struct List_ctrol *pointtoctr = NULL;
    char recvbuf[256]; //4K太长
    MSG *msg = (MSG *)recvbuf;
    struct commd_result *cmd_result = (struct commd_result *)recvbuf;
    

    while(1) {
        
        memset(recvbuf, 0, 256);
        printf("recving packge from client\n");
        
        if(recv_tlv_buffer(confd, (char *)recvbuf) == 0) {
            break;
        }

        
        if(msg->type == MSG_TYPE_HEART) {
            printf("recv a heartbeat\n");
            continue;
        }
        
        if(msg->type == MSG_TYPE_COMMD_RESULT && msg->length <= 4096) {
            
            printf("recv a command result!\n");
            pthread_rwlock_rdlock(&rwlock_cli);
            status = pointtocurrent_cli->status;
            pointtoctr = pointtocurrent_cli->pointtoctr;
            pthread_rwlock_unlock(&rwlock_cli);
                
            if(status == 1 && pointtoctr != NULL) {
                
                printf("the command result from client:%s\n", cmd_result->commdstr);
                //sleep(1);
                //pthread_rwlock_wrlock(&rwlock_ctr);
                send_tlv_buffer(pointtoctr->fd, (char *)recvbuf);
                //pthread_rwlock_unlock(&rwlock_ctr);
                    
                    
                /*if(thread_create_flag != RESET_THREAD_CREATED) {
                    pthread_create(&reset_thread, NULL, (void *)&reset, pointtocurrent_cli);
                    
                    pthread_join(reset_thread, NULL);
                        
                    thread_create_flag = RESET_THREAD_CREATED;
                }*/

                //reset the client to 0 status

                pthread_rwlock_wrlock(&rwlock_cli);
                pointtocurrent_cli->status = 0;
                pointtocurrent_cli->pointtoctr = NULL;
                pthread_rwlock_unlock(&rwlock_cli);
            }
            
        }
           
    }
    /*pthread_rwlock_rdlock(&rwlock_cli);
    pointtoctr = pointtocurrent_cli->pointtoctr;
    pthread_rwlock_unlock(&rwlock_cli);

    send_client_shutdown(pointtoctr->fd);*/

    close(confd);
    return ;
    
}

void send_client_shutdown(int control_fd)
{
    char shutdown_buf[64];
    struct commd_result *shutdown_result = (struct commd_result *)shutdown_buf;
    memset(shutdown_buf, 0, 64);

    shutdown_result->msg.type = MSG_TYPE_COMMD_RESULT;
    shutdown_result->msg.length = 30;
    sprintf(shutdown_result->commdstr, "%s", "the client is showdown!!!\n");

    send_tlv_buffer(control_fd, (char *)shutdown_buf);
    sleep(3);
    return ;
}

void reset(void *arg)
{
    struct List_cli *pointtocli = (struct List_cli *)arg;
    
    long now_time = 0;
    long origin_time = 0;
    
    pthread_rwlock_rdlock(&rwlock_cli);
    origin_time = pointtocli->origin_time;
    pthread_rwlock_unlock(&rwlock_cli);
    
    while(1) {
        now_time = time(NULL);
        if(now_time > origin_time + RESET_TIME_INTERVAL) {
            //reset the client to 0 status
            
            pthread_rwlock_wrlock(&rwlock_cli);
            pointtocli->status = 0;
            pointtocli->pointtoctr = NULL;
            pthread_rwlock_unlock(&rwlock_cli);
        }
        sleep(1);
	}
    
    return ;
    
}

struct List_cli* locatecurrent_cli(int confd)
{
    
    struct List_cli *p = cli_list_head;
    
    if(p == NULL) {
        return p;
    } else {
        pthread_rwlock_rdlock(&rwlock_cli);
        p = p->next;
        while(p != NULL) {
            if(p->fd == confd) {
                printf("the client fd has found!\n");
                break;
            }
            p = p->next;
        }
        pthread_rwlock_unlock(&rwlock_cli);
        return p;
    }
    
}

struct List_ctrol* locatecurrent_control(int confd)
{
    struct List_ctrol *p = control_list_head;
    
    if(p == NULL) {
        return p;
    } else {
        pthread_rwlock_rdlock(&rwlock_ctr);
        p = p->next;
        while(p != NULL) {
            if(p->fd == confd) {
                //printf("the control selected!!\n");
                break;
            }
            p = p->next;
        }
        pthread_rwlock_unlock(&rwlock_ctr);
        return p;
    }
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

int recv_tlv_buffer(int sockfd, char *buf)
{
    int ret = 0;
    int n = 0;
    char *pbuf = buf;
    int headlen = sizeof(MSG);
    MSG *msg = (MSG *)buf;
    
    if((n = recv_full(sockfd, pbuf, headlen)) > 0) {//记得打括号
        if(n == headlen) {
            printf("recv the msg head:%d bytes\n", n);
            ret = 1;
        } else {
            ret = 0;
        }
    }
    
    if(msg->length > 0) {
        if((n = recv_full(sockfd, &pbuf[headlen], msg->length)) > 0) {
            if(n == msg->length) {
                printf("recv the msg tail:%d bytes\n", n);
                //printf("value:%s\n", msg->value);
                ret = 1;
            } else {
                ret = 0;
            }
        }
    }
    
    return ret;
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
