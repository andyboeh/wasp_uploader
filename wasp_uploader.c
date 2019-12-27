#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <inttypes.h>
#include <unistd.h>

#define CHUNK_SIZE	14

#define REG_ZERO	"register0"
#define REG_STATUS	"register700"
#define REG_DATA1	"register702"
#define REG_DATA2	"register704"
#define REG_DATA3	"register706"
#define REG_DATA4	"register708"
#define REG_DATA5	"register70a"
#define REG_DATA6	"register70c"
#define REG_DATA7	"register70e"

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



static const char mac_data[CHUNK_SIZE] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x04, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};


off_t fsize(const char *filename) {
    struct stat st; 

    if (stat(filename, &st) == 0)
        return st.st_size;

    return -1; 
}

static int check_register(char *basepath, char *regpath) {
	char tmpPath[512];
	int len;
	len = strlen(basepath);
	struct stat st;
	
	strcpy(tmpPath, basepath);
	if(basepath[len-1] != '/')
		strcat(tmpPath, "/");
	strcat(tmpPath, regpath);
	if(stat(tmpPath, &st) < 0) {
		printf("Register not found: %s\n", regpath);
		return -1;
	}
	return 0;
}

static int check_registers(char *basepath) {
	int ret = 0;
	ret |= check_register(basepath, REG_ZERO);
	ret |= check_register(basepath, REG_STATUS);
	ret |= check_register(basepath, REG_DATA1);
	ret |= check_register(basepath, REG_DATA2);
	ret |= check_register(basepath, REG_DATA3);
	ret |= check_register(basepath, REG_DATA4);
	ret |= check_register(basepath, REG_DATA5);
	ret |= check_register(basepath, REG_DATA6);
	ret |= check_register(basepath, REG_DATA7);
	return ret;
}

static int write_register(char *basepath, char *regpath, uint16_t value) {
	char tmpPath[512];
	int len;
	len = strlen(basepath);
	
	strcpy(tmpPath, basepath);
	if(basepath[len-1] != '/')
		strcat(tmpPath, "/");
	strcat(tmpPath, regpath);
	
	FILE *fp;
	fp = fopen(tmpPath, "w");
	if(fp == NULL)
		return 1;
	fprintf(fp, "%" PRIu16, value);
	fclose(fp);
	return 0;
}

static int read_register(char *basepath, char *regpath, uint16_t *value) {
	char tmpPath[512];
	int len;
	len = strlen(basepath);
	
	strcpy(tmpPath, basepath);
	if(basepath[len-1] != '/')
		strcat(tmpPath, "/");
	strcat(tmpPath, regpath);
	
	FILE *fp;
	fp = fopen(tmpPath, "r");
	if(fp == NULL)
		return 1;
	fscanf(fp, "0x%" SCNu16, value);
	fclose(fp);
	return 0;
}

static int write_header(char *basepath, const uint32_t start_addr, const uint32_t len, const uint32_t exec_addr) {
	uint16_t regval;
	write_register(basepath, REG_DATA1, ((start_addr & 0xffff0000) >> 16));
	write_register(basepath, REG_DATA2, (start_addr & 0x0000ffff));
	write_register(basepath, REG_DATA3, ((len & 0xffff0000) >> 16));
	write_register(basepath, REG_DATA4, (len & 0x0000ffff));
	write_register(basepath, REG_DATA5, ((exec_addr & 0xffff0000) >> 16));
	write_register(basepath, REG_DATA6, (exec_addr & 0x0000ffff));
	write_register(basepath, REG_STATUS, CMD_SET_PARAMS);
	read_register(basepath, REG_ZERO, &regval);
	if(regval != RESP_OK) {
		printf("Error writing header!\n");
		return 1;
	}
	read_register(basepath, REG_STATUS, &regval);
	if(regval != RESP_OK) {
		printf("Error writing header!\n");
		return 1;
	}
	return 0;
}

static int write_checksum(char *basepath, const uint32_t checksum) {
	uint16_t regval;
	write_register(basepath, REG_DATA1, ((checksum & 0xffff0000) >> 16));
	write_register(basepath, REG_DATA2, (checksum & 0x0000ffff));
	write_register(basepath, REG_DATA3, 0x0000);
	write_register(basepath, REG_DATA4, 0x0000);
	write_register(basepath, REG_STATUS, CMD_SET_CHECKSUM);
	read_register(basepath, REG_ZERO, &regval);
	if(regval != RESP_OK) {
		printf("Error writing checksum!\n");
		return 1;
	}
	read_register(basepath, REG_STATUS, &regval);
	if(regval != RESP_OK) {
		printf("Error writing checksum!\n");
		return 1;
	}
	return 0;
}

static int write_chunk(char *basepath, const char *data, const int len) {
	uint16_t regval;
	
	regval = (data[0] & 0xff);
	if(len > 1)
		regval = (regval << 8) | (data[1] & 0xff);
	write_register(basepath, REG_DATA1, regval);
	if(len > 2) {
		regval = (data[2] & 0xff);
		if(len > 3)
			regval = (regval << 8) | (data[3] & 0xff);
		write_register(basepath, REG_DATA2, regval);
	}
	if(len > 4) {
		regval = (data[4] & 0xff);
		if(len > 5)
			regval = (regval << 8) | (data[5] & 0xff);
		write_register(basepath, REG_DATA3, regval);
	}
	if(len > 6) {
		regval = (data[6] & 0xff);
		if(len > 7)
			regval = (regval << 8) | (data[7] & 0xff);
		write_register(basepath, REG_DATA4, regval);
	}
	if(len > 8) {
		regval = (data[8] & 0xff);
		if(len > 9)
			regval = (regval << 8) | (data[9] & 0xff);
		write_register(basepath, REG_DATA5, regval);
	}
	if(len > 10) {
		regval = (data[10] & 0xff);
		if(len > 11)
			regval = (regval << 8) | (data[11] & 0xff);
		write_register(basepath, REG_DATA6, regval);
	}
	if(len > 12) {
		regval = (data[12] & 0xff);
		if(len > 13)
			regval = (regval << 8) | (data[13] & 0xff);
		write_register(basepath, REG_DATA7, regval);
	}
	
	write_register(basepath, REG_STATUS, CMD_SET_DATA);
	
	read_register(basepath, REG_ZERO, &regval);
	if((regval != RESP_OK) && (regval != RESP_COMPLETED) && (regval != RESP_WAIT)) {
		printf("Error writing chunk: REG_ZERO = 0x%x!\n", regval);
		return 1;
	}
	read_register(basepath, REG_STATUS, &regval);
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
	uint16_t regval;
  
	printf("AVM WASP uploader.\n");
	if(argc > 1) {
		printf("Arguments:\n");
		for(int i=1; i<argc; i++) {
			printf("%s\n", argv[i]);
		}
	}
	
	if(argc < 3) {
		printf("Usage: %s ath_tgt_fw1.bin /sys/bus/mdio/devices/00:07\n", argv[0]);
		return 1;
	}
	
	printf("Using file: %s\n", argv[1]);
	printf("Sysfs path: %s\n", argv[2]);
	
	size = fsize(argv[1]);
	if(size < 0) {
		printf("Input file not found.\n");
		return 1;
	}
	
	if(size > 0xffff) {
		printf("Error: Input file too big\n");
		return 1;
	}
	
	if(check_registers(argv[2]) < 0)
		return 1;
		
	read_register(argv[2], REG_STATUS, &regval);
	if(regval != RESP_OK) {
		printf("Error: WASP not ready (0x%x)\n", regval);
		return 1;
	}

	read_register(argv[2], REG_ZERO, &regval);
	if(regval != RESP_OK) {
		printf("Error: WASP not ready (0x%x)\n", regval);
		return 1;
	}

	if(write_header(argv[2], start_addr, size, exec_addr) < 0)
		return 1;

	if(write_checksum(argv[2], checksum_static) < 0)
		return 1;

	FILE *fp = fopen(argv[1], "rb");
	while(!feof(fp)) {
		read = fread(data, 1, CHUNK_SIZE, fp);
		if(write_chunk(argv[2], data, read) < 0) {
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	
	printf("Done uploading firmware.\n");
	
	sleep(1);
	
	write_register(argv[2], REG_STATUS, CMD_START_FIRMWARE);
	printf("Firmware start command sent.\n");

	// FIXME: Timeout
	read_register(argv[2], REG_STATUS, &regval);
	while(regval != RESP_UNKNOWN) {
		read_register(argv[2], REG_STATUS, &regval);
	}

	write_register(argv[2], REG_STATUS, CMD_START_FIRMWARE);
	printf("Firmware start command sent.\n");	

	// FIXME: Timeout
	read_register(argv[2], REG_STATUS, &regval);
	while(regval != RESP_OK) {
		read_register(argv[2], REG_STATUS, &regval);
	}
	
	write_chunk(argv[2], mac_data, CHUNK_SIZE);
	
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
