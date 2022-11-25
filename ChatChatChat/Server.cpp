#define _WINSOCK_DEPRECATED_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS

#include <stdio.h>
#include <WinSock2.h>
#include <process.h>
#include <string.h>

int server_init();
int server_close();
unsigned int WINAPI do_chat_service(void* param);
unsigned int WINAPI recv_and_forward(void* param);
int add_client(int index);
int read_client(int index);
void remove_client(int index);
int notify_client(char* message);
char* get_client_ip(int index);

typedef struct sock_info
{
    SOCKET s;
    HANDLE ev;
    char nick[50];
    char ipaddr[50];
}SOCK_INFO;

int         port_number = 9999;
const   int client_count = 10;
SOCK_INFO   sock_array[client_count + 1];
int         total_socket_count = 0;

int main(int argc, char* argv[])
{
    unsigned int tid;
    char message[MAXBYTE] = "";
    HANDLE mainthread;

    printf("\n사용법 : mcodes_server [포트번호]\n");
    printf("         ex) mcodes_server.exe 9999\n");
    printf("         ex) mcodes_server.exe \n\n");

    if (argv[1] != NULL)
        port_number = atoi(argv[1]);

    mainthread = (HANDLE)_beginthreadex(NULL, 0, do_chat_service, (void*)0, 0, &tid);
    if (mainthread)
    {
        while (1)
        {
            gets_s(message, MAXBYTE);
            if (strcmp(message, "/x") == 0)
                break;

            notify_client(message);
        }
        server_close();
        WSACleanup();
        CloseHandle(mainthread);
    }

    return 0;
}

int server_init()
{
    WSADATA wsadata;
    SOCKET s;
    SOCKADDR_IN server_address;

    memset(&sock_array, 0, sizeof(sock_array));
    total_socket_count = 0;
    if (WSAStartup(MAKEWORD(2, 2), &wsadata) != 0)
    {
        puts("WSAStartup 에러.");
        return -1;
    }

    if ((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        puts("socket 에러.");
        return -1;
    }

    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port_number);

    if (bind(s, (struct sockaddr*)&server_address, sizeof(server_address)) < 0)
    {
        puts("bind 에러");
        return -2;
    }

    if (listen(s, SOMAXCONN) < 0)
    {
        puts("listen 에러");
        return -3;
    }

    return s;
}

int server_close()
{
    for (int i = 1; i < total_socket_count; i++)
    {
        closesocket(sock_array[i].s);
        WSACloseEvent(sock_array[i].ev);
    }

    return 0;
}

unsigned int WINAPI do_chat_service(void* param)

{
    SOCKET  server_socket;
    WSANETWORKEVENTS ev;
    int index;
    WSAEVENT handle_array[client_count + 1];

    server_socket = server_init();
    if (server_socket < 0)
    {
        printf("초기화 에러\n");
        exit(0);
    }
    else
    {
        printf("\n >> 서버 초기화가 완료되었습니다.(포트번호:%d)\n", port_number);

        HANDLE event = WSACreateEvent();
        sock_array[total_socket_count].ev = event;
        sock_array[total_socket_count].s = server_socket;
        strcpy_s(sock_array[total_socket_count].nick, "svr");
        strcpy_s(sock_array[total_socket_count].ipaddr, "0.0.0.0");

        WSAEventSelect(server_socket, event, FD_ACCEPT);
        total_socket_count++;

        while (true)
        {
            memset(&handle_array, 0, sizeof(handle_array));
            for (int i = 0; i < total_socket_count; i++)
                handle_array[i] = sock_array[i].ev;

            index = WSAWaitForMultipleEvents(total_socket_count,
                handle_array, false, INFINITE, false);
            if ((index != WSA_WAIT_FAILED) && (index != WSA_WAIT_TIMEOUT))
            {
                WSAEnumNetworkEvents(sock_array[index].s, sock_array[index].ev, &ev);
                if (ev.lNetworkEvents == FD_ACCEPT)
                    add_client(index);
                else if (ev.lNetworkEvents == FD_READ)
                    read_client(index);
                else if (ev.lNetworkEvents == FD_CLOSE)
                    remove_client(index);
            }
        }
        closesocket(server_socket);
    }

    WSACleanup();
    _endthreadex(0);

    return 0;

}

int add_client(int index)
{
    SOCKADDR_IN addr;
    int len = 0;
    SOCKET accept_sock;

    if (total_socket_count == FD_SETSIZE)
        return 1;
    else {

        len = sizeof(addr);
        memset(&addr, 0, sizeof(addr));
        accept_sock = accept(sock_array[0].s, (SOCKADDR*)&addr, &len);

        HANDLE event = WSACreateEvent();
        sock_array[total_socket_count].ev = event;
        sock_array[total_socket_count].s = accept_sock;
        strcpy_s(sock_array[total_socket_count].ipaddr, inet_ntoa(addr.sin_addr));

        WSAEventSelect(accept_sock, event, FD_READ | FD_CLOSE);

        total_socket_count++;
        printf(" >> 신규 클라이언트 접속(IP : %s)\n", inet_ntoa(addr.sin_addr));

        char msg[256];
        sprintf_s(msg, " >> 신규 클라이언트 접속(IP : %s)\n", inet_ntoa(addr.sin_addr));
        notify_client(msg);
    }

    return 0;
}

int read_client(int index)
{
    unsigned int tid;
    HANDLE mainthread = (HANDLE)_beginthreadex(NULL, 0, recv_and_forward, (void*)index, 0, &tid);
    WaitForSingleObject(mainthread, INFINITE);

    CloseHandle(mainthread);

    return 0;
}

unsigned int WINAPI recv_and_forward(void* param)
{
    int index = (int)param;
    char message[MAXBYTE], share_message[MAXBYTE];
    SOCKADDR_IN client_address;
    int recv_len = 0, addr_len = 0;
    char* token1 = NULL;
    char* next_token = NULL;

    memset(&client_address, 0, sizeof(client_address));

    if ((recv_len = recv(sock_array[index].s, message, MAXBYTE, 0)) > 0)
    {
        addr_len = sizeof(client_address);
        getpeername(sock_array[index].s, (SOCKADDR*)&client_address, &addr_len);
        strcpy_s(share_message, message);

        if (strlen(sock_array[index].nick) <= 0)
        {
            token1 = strtok_s(message, "]", &next_token);
            strcpy_s(sock_array[index].nick, token1 + 1);
        }
        for (int i = 1; i < total_socket_count; i++)
            send(sock_array[i].s, share_message, MAXBYTE, 0);
    }

    _endthreadex(0);
    return 0;
}

void remove_client(int index)
{
    char remove_ip[256];
    char message[MAXBYTE];

    strcpy_s(remove_ip, get_client_ip(index));
    printf(" >> 클라이언트 접속 종료(Index: %d, IP: %s, 별명: %s)\n", index, remove_ip, sock_array[index].nick);
    sprintf_s(message, " >> 클라이언트 접속 종료(IP: %s, 별명: %s)\n", remove_ip, sock_array[index].nick);

    closesocket(sock_array[index].s);
    WSACloseEvent(sock_array[index].ev);

    total_socket_count--;
    sock_array[index].s = sock_array[total_socket_count].s;
    sock_array[index].ev = sock_array[total_socket_count].ev;
    strcpy_s(sock_array[index].ipaddr, sock_array[total_socket_count].ipaddr);
    strcpy_s(sock_array[index].nick, sock_array[total_socket_count].nick);

    notify_client(message);
}

char* get_client_ip(int index)
{
    static char ipaddress[256];
    int    addr_len;
    struct sockaddr_in    sock;

    addr_len = sizeof(sock);
    if (getpeername(sock_array[index].s, (struct sockaddr*)&sock, &addr_len) < 0)
        return NULL;

    strcpy_s(ipaddress, inet_ntoa(sock.sin_addr));
    return ipaddress;
}


int notify_client(char* message)
{
    for (int i = 1; i < total_socket_count; i++)
        send(sock_array[i].s, message, MAXBYTE, 0);

    return 0;
}
