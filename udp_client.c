#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
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
    char *interface_name = "wlan1";

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

    printf("UDP Client forced to %s, targeting Unicast IP (%s:%d).\n", interface_name, TARGET_IP, PORT);

    while (1) {
        printf("\nEnter message: ");
        if (fgets(message, sizeof(message), stdin) == NULL) {
            break;
        }

        message[strcspn(message, "\r\n")] = '\0';
        if (strlen(message) == 0) continue;

        int n = sendto(client_sock, message, strlen(message), 0,
                       (const struct sockaddr *)&server_addr, sizeof(server_addr));
        if (n < 0) {
            perror("sendto failed");
            break;
        }

        printf("Successfully sent %d bytes ('%s') into driver.\n", n, message);

        if (strcmp(message, "exit") == 0) break;
    }

    close(client_sock);
    return 0; 
}