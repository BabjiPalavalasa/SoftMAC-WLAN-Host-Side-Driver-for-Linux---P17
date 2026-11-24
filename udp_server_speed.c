#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8888
#define MAX_BUF 2276

int main(void) {
    int server_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len;
    char buffer[MAX_BUF + 1];
    unsigned long packets_received = 0;

    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        perror("Server socket creation failed");
        return 1;
    }

    int rcvbuf = 16 * 1024 * 1024;
    if (setsockopt(server_sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf)) < 0) {
        perror("setsockopt SO_RCVBUF failed");
    }

    int actual_rcvbuf = 0;
    socklen_t optlen = sizeof(actual_rcvbuf);
    getsockopt(server_sock, SOL_SOCKET, SO_RCVBUF, &actual_rcvbuf, &optlen);

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    if (bind(server_sock, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        return 1;
    }

    printf("UDP Server listening on port %d (SO_RCVBUF = %d bytes)...\n",
           PORT, actual_rcvbuf);

    while (1) {
        addr_len = sizeof(client_addr);
        int n = recvfrom(server_sock, buffer, MAX_BUF, 0,
                         (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom failed");
            break;
        }

        if (n <= 6) {
            buffer[n] = '\0';
            buffer[strcspn(buffer, "\r\n")] = '\0';
            if (strcmp(buffer, "exit") == 0) {
                printf("Exit command received. Total packets received: %lu\n",
                       packets_received);
                break;
            }
        }

        packets_received++;
    }

    close(server_sock);
    return 0;
}