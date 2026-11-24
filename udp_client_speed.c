#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <net/if.h>

#define PORT 8888
#define TARGET_IP "10.0.0.2"
#define MAX_BUF 2276

int main() {
    int client_sock;
    struct sockaddr_in server_addr;
    char message[MAX_BUF];
    char *interface_name = "wlan0";
    unsigned long packets_sent = 0;
    time_t end_time;

    client_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_sock < 0) {
        perror("Client socket creation failed");
        return 1;
    }

    if (setsockopt(client_sock, SOL_SOCKET, SO_BINDTODEVICE,
                   interface_name, strlen(interface_name) + 1) < 0) {
        perror("Bind to device failed. You MUST run this client with sudo!");
        return 1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    server_addr.sin_addr.s_addr = inet_addr(TARGET_IP);

    memset(message, 'A', sizeof(message));

    printf("Sending %d-byte packets on %s to %s:%d for 5 seconds...\n",
           MAX_BUF, interface_name, TARGET_IP, PORT);

    struct timespec now, end;
    clock_gettime(CLOCK_MONOTONIC, &end);
    end.tv_sec += 5;

    while (1) {
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (now.tv_sec > end.tv_sec ||
        (now.tv_sec == end.tv_sec && now.tv_nsec >= end.tv_nsec))
        break;
        
        int n = sendto(client_sock, message, sizeof(message), 0,
                       (const struct sockaddr *)&server_addr, sizeof(server_addr));
        if (n < 0) {
            if (errno == ENOBUFS)
                continue;
            perror("sendto failed");
            break;
        }
        packets_sent++;
    }
   
    usleep(100000); 
    const char *exit_msg = "exit";
    for (int i = 0; i < 5; i++) {
        sendto(client_sock, exit_msg, strlen(exit_msg), 0,
               (const struct sockaddr *)&server_addr, sizeof(server_addr));
        usleep(10000);
    }
    printf("Done. Total packets sent in ~5 seconds: %lu\n", packets_sent);

    close(client_sock);
    return 0;
}