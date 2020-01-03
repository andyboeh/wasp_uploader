/*
 * Simple stage 2 firmware uploader for AVM WASP as found in the FRITZ!Box 3390
 *
 * The protocol was found by sniffing ethernet traffic between the two SoCs,
 * so some things might be wrong or incomplete.
 *
 * Important: if the switch is configured for VLAN tagging, the eth0.1 interface
 *            has to be used, not eth0!
 *
 * (c) 2019 Andreas Böhler
 */


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

#define ETHER_TYPE 			0x88bd
#define BUF_SIZE			1056
#define COUNTER_INCR		4

#define MAX_PAYLOAD_SIZE	1028
#define CHUNK_SIZE			1024
#define WASP_HEADER_LEN		14

#define PACKET_START		0x1200
#define CMD_FIRMWARE_DATA	0x0104
#define CMD_START_FIRMWARE	0xd400

#define RESP_DISCOVER		0x0000
#define RESP_CONFIG			0x1000
#define RESP_OK				0x0100
#define RESP_STARTING		0x0200
#define RESP_ERROR			0x0300

typedef enum {
	DOWNLOAD_TYPE_UNKNOWN = 0,
	DOWNLOAD_TYPE_FIRMWARE,
	DOWNLOAD_TYPE_CONFIG
} t_download_type;

static const uint32_t m_load_addr = 0x81a00000;

static uint8_t wasp_mac[] = {0x00, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa};
static uint16_t m_packet_counter = 0;
static t_download_type m_download_type = DOWNLOAD_TYPE_UNKNOWN;
static int m_socket_initialized = 0;


typedef struct __attribute__((packed)) {
	union {
		uint8_t data[MAX_PAYLOAD_SIZE + WASP_HEADER_LEN];
		struct __attribute__((packed)) {
			uint16_t	packet_start;
			uint8_t		pad_one[5];
			uint16_t	command;
			uint16_t	response;
			uint16_t	counter;
			uint8_t		pad_two;
			uint8_t		payload[MAX_PAYLOAD_SIZE];
		};
	};
} t_wasp_packet;

static int send_packet(t_wasp_packet *packet, int payloadlen, char *devname) {
	char sendbuf[BUF_SIZE];
	static int sockfd;
	static struct ifreq if_idx;
	static struct ifreq if_mac;
	struct ether_header *eh = (struct ether_header *) sendbuf;
	int tx_len = 0;
	struct sockaddr_ll socket_address;

	if(!m_socket_initialized) {
		if ((sockfd = socket(AF_PACKET, SOCK_RAW, IPPROTO_RAW)) == -1) {
	    	perror("socket");
	    	return 1;
		}

		/* Get the index of the interface to send on */
		memset(&if_idx, 0, sizeof(struct ifreq));
		strncpy(if_idx.ifr_name, devname, IFNAMSIZ-1);

		if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0) {
			perror("SIOCGIFINDEX");
			return 1;
		}

		/* Get the MAC address of the interface to send on */
		memset(&if_mac, 0, sizeof(struct ifreq));
		strncpy(if_mac.ifr_name, devname, IFNAMSIZ-1);
		if (ioctl(sockfd, SIOCGIFHWADDR, &if_mac) < 0) {
			perror("SIOCGIFHWADDR");
			return 1;
		}
		m_socket_initialized = 1;
	}
	
	memset(sendbuf, 0, BUF_SIZE);
	
	eh->ether_shost[0] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[0];
	eh->ether_shost[1] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[1];
	eh->ether_shost[2] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[2];
	eh->ether_shost[3] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[3];
	eh->ether_shost[4] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[4];
	eh->ether_shost[5] = ((uint8_t *)&if_mac.ifr_hwaddr.sa_data)[5];
	for(int i=0; i<6; i++) {
		eh->ether_dhost[i] = wasp_mac[i];
	}
	/* Ethertype field */
	eh->ether_type = ETHER_TYPE;
	tx_len += sizeof(struct ether_header);

	for(int i=0; i<WASP_HEADER_LEN; i++) {
		sendbuf[tx_len++] = packet->data[i];
	}
	
	for(int i=0; i<payloadlen; i++) {
		sendbuf[tx_len++] = packet->data[WASP_HEADER_LEN + i];
	}
	
	/* Index of the network device */
	socket_address.sll_ifindex = if_idx.ifr_ifindex;
	/* Address length*/
	socket_address.sll_halen = ETH_ALEN;
	/* Destination MAC */

	for(int i=0; i<6; i++) {
		socket_address.sll_addr[i] = wasp_mac[i];
	}

	/* Send packet */
	if (sendto(sockfd, sendbuf, tx_len, 0, (struct sockaddr*)&socket_address, sizeof(struct sockaddr_ll)) < 0) {
		printf("Send failed\n");
		return 1;
	}

	return 0;

}

int main(int argc, char *argv[]) {
	int sockfd;
	int valid = 1;
	int done = 0;
	int i;
	int sockopt;
	uint8_t buf[BUF_SIZE];
	struct ifreq ifopts;	/* set promiscuous mode */
	ssize_t numbytes;
	t_wasp_packet *packet = (t_wasp_packet *) (buf + sizeof(struct ether_header));
	t_wasp_packet s_packet;
	FILE *fp = NULL;
	char *fn;
	ssize_t read;
	int data_offset = 0;
	int fsize;
	int cfgsize;
	int num_chunks;
	int chunk_counter = 1;

	printf("AVM WASP Stage 2 uploader.\n");
	
	if(argc < 3) {
		printf("Usage: %s ath_tgt_fw2.bin eth0 [config.tar.gz]\n", argv[0]);
		return 1;
	}
	
	printf("Using file  : %s\n", argv[1]);
	printf("Using Dev   : %s\n", argv[2]);
	
	if(argc > 3) {
		printf("Using config: %s\n", argv[3]);
		
		fp = fopen(argv[3], "rb");
		if(fp == NULL) {
			printf("Input file not found: %s\n", argv[3]);
		}
		fseek(fp, 0, SEEK_END);
		cfgsize = ftell(fp);
		fseek(fp, 0, SEEK_SET);
		fclose(fp);
		fp = NULL;
	}
	
	fp = fopen(argv[1], "rb");
	if(fp == NULL) {
		printf("Input file not found: %s\n", argv[1]);
	}
	fseek(fp, 0, SEEK_END);
	fsize = ftell(fp);
	fseek(fp, 0, SEEK_SET);
	fclose(fp);
	fp = NULL;

	/* Header structures */
	struct ether_header *eh = (struct ether_header *) buf;
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
	while(!done) {
		//printf("listener: Waiting to recvfrom...\n");
		numbytes = recvfrom(sockfd, buf, BUF_SIZE, 0, NULL, NULL);
		//printf("listener: got packet %lu bytes\n", numbytes);	

		if(numbytes < 30) {
			printf("Packet too small, discarding\n");
			continue;
		}
		/*
		for(i=0; i<6; i++) {
			if(eh->ether_shost[i] != wasp_mac[i])
				valid = 0;
		}
		*/
		
		if(eh->ether_type != ETHER_TYPE)
			valid = 0;
		
		if(!valid)
			continue;
			
		for(i=0; i<6; i++) {
			wasp_mac[i] = eh->ether_shost[i];
		}
		
		
		if((packet->packet_start == PACKET_START) && (packet->response == RESP_DISCOVER)) {
			printf("Got discovery packet, starting firmware download...\n");
			m_packet_counter = 0;
			m_download_type = DOWNLOAD_TYPE_FIRMWARE;
			fn = argv[1];
			num_chunks = fsize / CHUNK_SIZE;
			if(fsize % CHUNK_SIZE != 0) {
				num_chunks++;
			}

			printf("Going to send %d chunks.\n", num_chunks);
		} else if((packet->packet_start == PACKET_START) && (packet->response == RESP_CONFIG)) {
			printf("Got config discovery packet, starting config download...\n");
			m_packet_counter = 0;
			m_download_type = DOWNLOAD_TYPE_CONFIG;
			fn = argv[3];
			num_chunks = cfgsize / CHUNK_SIZE;
			if(cfgsize % CHUNK_SIZE != 0) {
				num_chunks++;
			}

			printf("Going to send %d chunks.\n", num_chunks);
		} else if((packet->packet_start == PACKET_START) && (packet->response == RESP_OK)) {
			memset(&s_packet, 0, sizeof(s_packet));

			//printf("Got reply, sending next chunk...\n");
		} else if((packet->packet_start == PACKET_START) && (packet->response == RESP_ERROR)) {
			printf("Received an error packet!\n");
			done = 1;
			continue;
		} else if((packet->packet_start == PACKET_START) && (packet->response == RESP_STARTING)) {
			printf("Successfully uploaded stage 2 firmware!\n");
			if(argc <= 3) {
				done = 1;
			}
			continue;
		} else {
			printf("Got unknown packet!\n");
			continue;
		}
		if(m_packet_counter == 0) {
			//printf("Sending first chunk!\n");
			if(fp == NULL) {
				fp = fopen(fn, "rb");
			}
			else {
				fseek(fp, 0, SEEK_SET);
			}
			if(m_download_type == DOWNLOAD_TYPE_FIRMWARE) {
				memcpy(s_packet.payload, &m_load_addr, sizeof(m_load_addr));
				data_offset = sizeof(m_load_addr);
			} else {
				data_offset = 0;
			}
		} else {
			data_offset = 0;
		}
		if(!feof(fp)) {
			read = fread(&s_packet.payload[data_offset], 1, CHUNK_SIZE, fp);
			s_packet.packet_start = PACKET_START;
			if(chunk_counter == num_chunks) {
				//printf("Sending last chunk!\n");
				s_packet.response = CMD_START_FIRMWARE;
				if(m_download_type == DOWNLOAD_TYPE_FIRMWARE) {
					memcpy(&s_packet.payload[data_offset + read], &m_load_addr, sizeof(m_load_addr));
					data_offset += sizeof(m_load_addr);
				}
			} else {
				s_packet.command = CMD_FIRMWARE_DATA;
			}
			s_packet.counter = m_packet_counter;
			if(send_packet(&s_packet, read + data_offset, argv[2]) != 0) {
				printf("Error sending packet.\n");
				continue;
			}
			m_packet_counter += COUNTER_INCR;
			chunk_counter++;
		} else {
			//printf("EOF\n");
			fclose(fp);
			fp = NULL;
		}
	}
	if(fp)
		fclose(fp);
	close(sockfd);
	
	return 0;
}
