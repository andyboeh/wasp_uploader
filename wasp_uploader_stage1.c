// Based on mdio-tool.c Copyright (C) 2013 Pieter Voorthuijsen

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <net/if.h>
#include <linux/sockios.h>
#include <linux/mii.h>

#ifndef __GLIBC__
#include <linux/if_arp.h>
#include <linux/if_ether.h>
#endif

#define CHUNK_SIZE	14

#define MDIO_ADDR	0x07

#define REG_ZERO	0x0
#define REG_STATUS	0x700
#define REG_DATA1	0x702
#define REG_DATA2	0x704
#define REG_DATA3	0x706
#define REG_DATA4	0x708
#define REG_DATA5	0x70a
#define REG_DATA6	0x70c
#define REG_DATA7	0x70e

#define RESP_RETRY			0x0102
#define RESP_OK				0x0002
#define RESP_WAIT			0x0401
#define RESP_COMPLETED		0x0000
#define RESP_READY_TO_START	0x0202
#define RESP_STARTING       0x00c9
#define RESP_UNKNOWN        0x00ca

#define CMD_SET_PARAMS		0x0c01
#define CMD_SET_CHECKSUM	0x0801
#define CMD_SET_DATA		0x0e01
#define CMD_START_FIRMWARE  0x0201

static const uint32_t start_addr = 0xbd003000;
static const uint32_t exec_addr = 0xbd003000;
static const uint32_t checksum_static = 0x6a83ffc7;

static int skfd = -1;		/* AF_INET socket for ioctl() calls. */
static struct ifreq ifr;

static const char mac_data[CHUNK_SIZE] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x04, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};


off_t fsize(const char *filename) {
    struct stat st; 

    if (stat(filename, &st) == 0)
        return st.st_size;

    return -1; 
}


static int mdio_read(int location, int *value)
{
    struct mii_ioctl_data *mii = (struct mii_ioctl_data *)&ifr.ifr_data;
    mii->reg_num = location;

    if (ioctl(skfd, SIOCGMIIREG, &ifr) < 0) {
		fprintf(stderr, "SIOCGMIIREG on %s failed: %s\n", ifr.ifr_name,
		strerror(errno));
		return -1;
    }

	*value = mii->val_out;
    return 0;
}

static int mdio_write(int location, int value)
{
    struct mii_ioctl_data *mii = (struct mii_ioctl_data *)&ifr.ifr_data;
    mii->reg_num = location;
    mii->val_in = value;

	if (ioctl(skfd, SIOCSMIIREG, &ifr) < 0) {
		fprintf(stderr, "SIOCSMIIREG on %s failed: %s\n", ifr.ifr_name,
		strerror(errno));
		return -1;
    }
    return 0;
}

static int write_header(const uint32_t start_addr, const uint32_t len, const uint32_t exec_addr) {
	int regval;
	mdio_write(REG_DATA1, ((start_addr & 0xffff0000) >> 16));
	mdio_write(REG_DATA2, (start_addr & 0x0000ffff));
	mdio_write(REG_DATA3, ((len & 0xffff0000) >> 16));
	mdio_write(REG_DATA4, (len & 0x0000ffff));
	mdio_write(REG_DATA5, ((exec_addr & 0xffff0000) >> 16));
	mdio_write(REG_DATA6, (exec_addr & 0x0000ffff));
	mdio_write(REG_STATUS, CMD_SET_PARAMS);
	mdio_read(REG_ZERO, &regval);
	if(regval != RESP_OK) {
		printf("Error writing header!\n");
		return 1;
	}
	mdio_read(REG_STATUS, &regval);
	if(regval != RESP_OK) {
		printf("Error writing header!\n");
		return 1;
	}
	return 0;
}

static int write_checksum(const uint32_t checksum) {
	int regval;
	mdio_write(REG_DATA1, ((checksum & 0xffff0000) >> 16));
	mdio_write(REG_DATA2, (checksum & 0x0000ffff));
	mdio_write(REG_DATA3, 0x0000);
	mdio_write(REG_DATA4, 0x0000);
	mdio_write(REG_STATUS, CMD_SET_CHECKSUM);
	mdio_read(REG_ZERO, &regval);
	if(regval != RESP_OK) {
		printf("Error writing checksum!\n");
		return 1;
	}
	mdio_read(REG_STATUS, &regval);
	if(regval != RESP_OK) {
		printf("Error writing checksum!\n");
		return 1;
	}
	return 0;
}

static int write_chunk(const char *data, const int len) {
	int regval;
	
	regval = (data[0] & 0xff);
	if(len > 1)
		regval = (regval << 8) | (data[1] & 0xff);
	mdio_write(REG_DATA1, regval);
	if(len > 2) {
		regval = (data[2] & 0xff);
		if(len > 3)
			regval = (regval << 8) | (data[3] & 0xff);
		mdio_write(REG_DATA2, regval);
	}
	if(len > 4) {
		regval = (data[4] & 0xff);
		if(len > 5)
			regval = (regval << 8) | (data[5] & 0xff);
		mdio_write(REG_DATA3, regval);
	}
	if(len > 6) {
		regval = (data[6] & 0xff);
		if(len > 7)
			regval = (regval << 8) | (data[7] & 0xff);
		mdio_write(REG_DATA4, regval);
	}
	if(len > 8) {
		regval = (data[8] & 0xff);
		if(len > 9)
			regval = (regval << 8) | (data[9] & 0xff);
		mdio_write(REG_DATA5, regval);
	}
	if(len > 10) {
		regval = (data[10] & 0xff);
		if(len > 11)
			regval = (regval << 8) | (data[11] & 0xff);
		mdio_write(REG_DATA6, regval);
	}
	if(len > 12) {
		regval = (data[12] & 0xff);
		if(len > 13)
			regval = (regval << 8) | (data[13] & 0xff);
		mdio_write(REG_DATA7, regval);
	}
	
	mdio_write(REG_STATUS, CMD_SET_DATA);
	
	mdio_read(REG_ZERO, &regval);
	if((regval != RESP_OK) && (regval != RESP_COMPLETED) && (regval != RESP_WAIT)) {
		printf("Error writing chunk: REG_ZERO = 0x%x!\n", regval);
		return 1;
	}
	mdio_read(REG_STATUS, &regval);
	if((regval != RESP_OK) && (regval != RESP_WAIT) && (regval != RESP_COMPLETED)) {
		printf("Error writing chunk: REG_STATUS = 0x%x!\n", regval);
		return 1;
	}
	return 0;
}

int main(int argc, char *argv[]) {
	char data[CHUNK_SIZE];
	size_t read = 0;
	off_t size;
	int regval;
	struct mii_ioctl_data *mii = (struct mii_ioctl_data *)&ifr.ifr_data;
  
	printf("AVM WASP Stage 1 uploader.\n");
	if(argc > 1) {
		printf("Arguments:\n");
		for(int i=1; i<argc; i++) {
			printf("%s\n", argv[i]);
		}
	}
	
	if(argc < 3) {
		printf("Usage: %s ath_tgt_fw1.bin eth0\n", argv[0]);
		return 1;
	}
	
	printf("Using file     : %s\n", argv[1]);
	printf("Ethernet device: %s\n", argv[2]);
	
	size = fsize(argv[1]);
	if(size < 0) {
		printf("Input file not found.\n");
		return 1;
	}
	
	if(size > 0xffff) {
		printf("Error: Input file too big\n");
		return 1;
	}

	/* Open a basic socket. */
	if ((skfd = socket(AF_INET, SOCK_DGRAM,0)) < 0) {
		perror("socket");
		return -1;
	}

	strncpy(ifr.ifr_name, argv[2], IFNAMSIZ);
	mii->phy_id = MDIO_ADDR;

	mdio_read(REG_STATUS, &regval);
	if(regval != RESP_OK) {
		printf("Error: WASP not ready (0x%x)\n", regval);
		return 1;
	}

	mdio_read(REG_ZERO, &regval);
	if(regval != RESP_OK) {
		printf("Error: WASP not ready (0x%x)\n", regval);
		return 1;
	}

	if(write_header(start_addr, size, exec_addr) < 0)
		return 1;

	if(write_checksum(checksum_static) < 0)
		return 1;

	FILE *fp = fopen(argv[1], "rb");
	while(!feof(fp)) {
		read = fread(data, 1, CHUNK_SIZE, fp);
		if(write_chunk(data, read) < 0) {
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	
	printf("Done uploading firmware.\n");
	
	sleep(1);
	
	mdio_write(REG_STATUS, CMD_START_FIRMWARE);
	printf("Firmware start command sent.\n");

	// FIXME: Timeout
	mdio_read(REG_STATUS, &regval);
	while(regval != RESP_UNKNOWN) {
		mdio_read(REG_STATUS, &regval);
	}

	mdio_write(REG_STATUS, CMD_START_FIRMWARE);
	printf("Firmware start command sent.\n");	

	// FIXME: Timeout
	mdio_read(REG_STATUS, &regval);
	while(regval != RESP_OK) {
		mdio_read(REG_STATUS, &regval);
	}
	
	write_chunk(mac_data, CHUNK_SIZE);
	
	printf("Firmware upload successful!\n");

	// FIXME: Checksum calcuation is yet unknown
	/*
	FILE *fp = fopen(argv[1], "rb");

	while(!feof(fp) && !ferror(fp)) {
		read = fread(data, 1, sizeof(data), fp);
		if(read > 0) {
			dataC = (data[0] << 24) | (data[1] << 16) | (data[2] << 8) | data[3];
			//printf("%08x\n", crc);
			crc = crc ^ dataC;
			for(int j=0; j<(int)read; j++) {
				cs += data[j];
			}
			//printf("%08x\n", crc);
		}
	}
	if(!ferror(fp)) {
		printf("0x%08x\n", crc);
		printf("0x%08x\n", cs);
	}
	fclose(fp);
	*/
	
	

	return 0;
}
