#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define PORT 8888
#define MAX_BUF 2276

int main() {
    int server_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[MAX_BUF + 1];


    server_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_sock < 0) {
        perror("Server socket creation failed");
        return 1;
    }

    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);


    
    if (bind(server_sock, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_sock);
        return 1;
    }

    printf("UDP Server listening on port %d...\n", PORT);

   while (1) {
        memset(buffer, 0, MAX_BUF + 1);
        addr_len = sizeof(client_addr);

        int n = recvfrom(server_sock, buffer, MAX_BUF, 0,
                         (struct sockaddr *)&client_addr, &addr_len);
        if (n < 0) {
            perror("recvfrom failed");
            break;
        }

        buffer[n] = '\0';
        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("Received %d bytes from %s:%d -> '%s'\n",
               n, inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port), buffer);
        fflush(stdout);

        if (strcmp(buffer, "exit") == 0) {
            printf("Exit command received. Shutting down server...\n");
            break;
        }
    }
    close(server_sock);
    return 0;
}