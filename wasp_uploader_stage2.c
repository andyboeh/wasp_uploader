#include <arpa/inet.h>
#include <linux/if_packet.h>
#include <linux/ip.h>
#include <linux/udp.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <unistd.h>

#define ETHER_TYPE 	0x88bd
#define BUF_SIZE	1024

int main(int argc, char *argv[]) {
	int sockfd;
	int i;
	int sockopt;
	uint8_t buf[BUF_SIZE];
	struct ifreq ifopts;	/* set promiscuous mode */
	ssize_t numbytes;
	
	printf("AVM WASP Stage 2 uploader.\n");
	if(argc > 1) {
		printf("Arguments:\n");
		for(int i=1; i<argc; i++) {
			printf("%s\n", argv[i]);
		}
	}
	
	if(argc < 3) {
		printf("Usage: %s ath_tgt_fw2.bin eth0\n", argv[0]);
		return 1;
	}
	
	printf("Using file: %s\n", argv[1]);
	printf("Using Dev : %s\n", argv[2]);
	

	/* Header structures */
	//struct ether_header *eh = (struct ether_header *) buf;
	//struct iphdr *iph = (struct iphdr *) (buf + sizeof(struct ether_header));
	//struct udphdr *udph = (struct udphdr *) (buf + sizeof(struct iphdr) + sizeof(struct ether_header));

	/* Open PF_PACKET socket, listening for EtherType ETHER_TYPE */
	if ((sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETHER_TYPE))) == -1) {
		perror("listener: socket");	
		return -1;
	}
	
	strncpy(ifopts.ifr_name, argv[2], IFNAMSIZ-1);
	ioctl(sockfd, SIOCGIFFLAGS, &ifopts);
	ifopts.ifr_flags |= IFF_PROMISC;
	ioctl(sockfd, SIOCSIFFLAGS, &ifopts);

	/* Allow the socket to be reused - incase connection is closed prematurely */
	if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof sockopt) == -1) {
		perror("setsockopt");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	/* Bind to device */
	if (setsockopt(sockfd, SOL_SOCKET, SO_BINDTODEVICE, argv[2], IFNAMSIZ-1) == -1)	{
		perror("SO_BINDTODEVICE");
		close(sockfd);
		exit(EXIT_FAILURE);
	}

	//FIXME: Timeout
	while(1) {
		printf("listener: Waiting to recvfrom...\n");
		numbytes = recvfrom(sockfd, buf, BUF_SIZE, 0, NULL, NULL);
		printf("listener: got packet %lu bytes\n", numbytes);	
		
		printf("\tData:");
		for (i=0; i<numbytes; i++)
			printf("%02x:", buf[i]);
		printf("\n");
	}
	return 0;
}
