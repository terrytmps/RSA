#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/select.h>
#include <sys/time.h>
#include <netinet/ip_icmp.h>

// Fonction de calcul du checksum ICMP.
unsigned short checksum(void *b, int len) {
    unsigned short *buf = b;
    unsigned int sum = 0;
    unsigned short result;

    for (; len > 1; len -= 2)
        sum += *buf++;
    if (len == 1)
        sum += *(unsigned char*)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

// Fonction pour vérifier si un hôte est actif en utilisant ICMP.
int is_host_active(const char* ip_address, char *buffer) {
    int sockfd = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sockfd < 0) {
        perror("socket");
        return 0;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    inet_pton(AF_INET, ip_address, &addr.sin_addr);

    struct icmp echo_req;
    memset(&echo_req, 0, sizeof(echo_req));
    echo_req.icmp_type = ICMP_ECHO;
    echo_req.icmp_code = 0;
    echo_req.icmp_id = getpid() & 0xFFFF; // Ensure it fits into 16 bits
    echo_req.icmp_seq = 1;
    // Important: Calculate the checksum after setting all fields of the header
    echo_req.icmp_cksum = checksum(&echo_req, sizeof(echo_req));

    if (sendto(sockfd, &echo_req, sizeof(echo_req), 0, (struct sockaddr*)&addr, sizeof(addr)) <= 0) {
        perror("sendto");
        close(sockfd);
        return 0;
    }

    // Setting up timeout for recvfrom
    struct timeval tv;
    tv.tv_sec = 0;  // Set timeout to 0 second
    tv.tv_usec = 50000;
    setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));

    char buf[1024];
    struct sockaddr_in response_addr;
    socklen_t len = sizeof(response_addr);
    if (recvfrom(sockfd, buf, sizeof(buf), 0, (struct sockaddr*)&response_addr, &len) > 0) {
        // Additional check here for ICMP echo reply could be implemented
        close(sockfd);
        return 1;
    }
    close(sockfd);
    return 0;
}


// Fonction pour calculer la première adresse IP du réseau
void calculateNetworkAddress(struct in_addr ip, struct in_addr mask, struct in_addr *networkAddress) {
    networkAddress->s_addr = ip.s_addr & mask.s_addr;
}

// Fonction pour calculer la dernière adresse IP du réseau
void calculateBroadcastAddress(struct in_addr networkAddress, struct in_addr mask, struct in_addr *broadcastAddress) {
    broadcastAddress->s_addr = networkAddress.s_addr | ~mask.s_addr;
}

void scan_horizontal(char *buffer) {
    struct ifaddrs *ifap, *ifa;
    struct sockaddr_in *sa;
    char gateway_ip[INET_ADDRSTRLEN];
    struct in_addr mask;
    struct in_addr networkAddress;
    struct in_addr broadcastAddress;
    char message[1024];

    // Récupère la liste des interfaces réseau
    if (getifaddrs(&ifap) != 0) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // Parcourt la liste des interfaces
    for (ifa = ifap; ifa != NULL; ifa = ifa->ifa_next) {
        // Vérifie si l'interface est active et de type IPv4
        if (ifa->ifa_addr != NULL && ifa->ifa_addr->sa_family == AF_INET) {
            sa = (struct sockaddr_in *)ifa->ifa_addr;


            // Exclut les interfaces loopback (adresse IP locale)
            if (strcmp(inet_ntoa(sa->sin_addr), "127.0.0.1") != 0) {
                // Convertit l'adresse IP binaire en une chaîne de caractères lisible
                const char *ip_address = inet_ntoa(sa->sin_addr);
                mask = ((struct sockaddr_in *)ifa->ifa_netmask)->sin_addr;

                // Si l'interface a une adresse IP, affiche-la
                if (ip_address != NULL) {

                    sprintf(message, "Interface: %s\n", ifa->ifa_name);
                    strcat(buffer, message);

                    sprintf(message, "IP Address: %s\n", ip_address);
                    strcat(buffer, message);

                    sprintf(message, "Subnet Mask: %s\n", inet_ntoa(mask));
                    strcat(buffer, message);

                    printf("Interface: %s\n", ifa->ifa_name);
                    printf("IP Address: %s\n", ip_address);
                    printf("Subnet Mask: %s\n", inet_ntoa(mask));

                    // Calculer l'adresse du réseau
                    calculateNetworkAddress(sa->sin_addr, mask, &networkAddress);
                    sprintf(message, "Network Address: %s\n", inet_ntoa(networkAddress));
                    strcat(buffer, message);
                    printf("Network Address: %s\n", inet_ntoa(networkAddress));

                    // Calculer l'adresse de diffusion
                    calculateBroadcastAddress(networkAddress, mask, &broadcastAddress);
                    sprintf(message, "Broadcast Address: %s\n", inet_ntoa(broadcastAddress));
                    strcat(buffer, message);
                    printf("Broadcast Address: %s\n", inet_ntoa(broadcastAddress));
                    
                    // Scan réseau : Boucle à travers toutes les adresses IP du réseau
                    sprintf(message, "Scanning IP: %s\n", ip_address);
                    strcat(buffer, message);
                    printf("Scanning Network...\n");
                    for (unsigned int i = ntohl(networkAddress.s_addr) + 1; i < ntohl(broadcastAddress.s_addr); i++) {
                        struct in_addr currentAddress;
                        currentAddress.s_addr = htonl(i);
                        if (is_host_active(inet_ntoa(currentAddress), buffer)) {
                            snprintf(message, 256, "Host %s is active.\n", inet_ntoa(currentAddress));
                            strcat(buffer, message);
                            printf("Host %s is active.\n", inet_ntoa(currentAddress));
                        }
                        else {
                            snprintf(message, 256, "Host %s is inactive.\n", inet_ntoa(currentAddress));
                            strcat(buffer, message);
                            printf("Host %s is inactive or the request timeout.\n", inet_ntoa(currentAddress));
                        }
                        //Voir si ca marche avec mon IP : 
                        // if (is_host_active(ip_address)) {
                        //     printf("Host %s is active.\n", ip_address);
                        // }
                    }
                }
            }
        }
    }

    // Libère la mémoire allouée pour la liste des interfaces
    freeifaddrs(ifap);
}

// int main() {
//     char buffer[1024] = {0};
//     scan_horizontal(buffer);
//     return 0;
// }