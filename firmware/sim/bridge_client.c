/**
 * Simulator: TCP transport to the PC app (127.0.0.1:8766). link.c reads and applies the frames –
 * this only connects, passes bytes through and reconnects when the connection drops.
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdio.h>

#include "bridge_client.h"
#include "link.h"
#include "lvgl.h"

#define BRIDGE_HOST "127.0.0.1"
#define BRIDGE_PORT 8766
#define HELLO       "{\"proto\":1,\"fw\":\"sim-0.1\",\"has_knob\":true,\"w\":800,\"h\":480}"

static SOCKET sock = INVALID_SOCKET;

/* link.c only calls this while holding its send lock */
static void tcp_send(const uint8_t * data, size_t len)
{
    SOCKET s = sock;
    while(s != INVALID_SOCKET && len > 0) {
        int n = send(s, (const char *)data, (int)len, 0);
        if(n <= 0) return;
        data += n;
        len -= (size_t)n;
    }
}

static DWORD WINAPI net_thread(LPVOID arg)
{
    LV_UNUSED(arg);
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    static uint8_t buf[16 * 1024];

    for(;;) {
        SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in addr = { 0 };
        addr.sin_family = AF_INET;
        addr.sin_port = htons(BRIDGE_PORT);
        inet_pton(AF_INET, BRIDGE_HOST, &addr.sin_addr);

        if(s == INVALID_SOCKET || connect(s, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            if(s != INVALID_SOCKET) closesocket(s);
            Sleep(2000);
            continue;
        }
        sock = s;
        link_set_connected(true);
        link_send_hello();
        printf("bridge connected\n");

        for(;;) {
            int n = recv(s, (char *)buf, sizeof(buf), 0);
            if(n <= 0) break;
            link_feed(buf, (size_t)n);
        }

        sock = INVALID_SOCKET;
        closesocket(s);
        link_set_connected(false);
        printf("bridge disconnected, retrying in 2 s\n");
        Sleep(2000);
    }
}

void bridge_client_start(void)
{
    link_init(tcp_send, NULL, HELLO); /* the PC has its own clock */
    CreateThread(NULL, 0, net_thread, NULL, 0, NULL);
}
