#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

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

static const uint32_t start_addr = 0xbd003000;
static const uint32_t exec_addr = 0xbd003000;
static const uint32_t checksum_static = 0x6a83ffc7;

static const uint32_t CMD_SET_PARAMS = 0x0c01;
static const uint32_t CMD_SET_CHECKSUM = 0x0801;
static const uint32_t CMD_SET_DATA = 0x0e01;

static const char mac_data[CHUNK_SIZE] = {0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0x04, 0x20, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00};

static const uint32_t RESP_OK = 0x02;
static const uint32_t RESP_WAIT = 0x0401;

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
	fp = fopen(tmpPath, "wb");
	if(fp == NULL)
		return 1;
	fwrite(&value, 2, 1, fp);
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
	fp = fopen(tmpPath, "rb");
	if(fp == NULL)
		return 1;
	fread(value, 2, 1, fp);
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
	
	regval = data[0];
	if(len > 1)
		regval = data[0] << 8 | data[1];
	write_register(basepath, REG_DATA1, regval);
	if(len > 2) {
		regval = data[2];
		if(len > 3)
			regval = data[2] << 8 | data[3];
		write_register(basepath, REG_DATA2, regval);
	}
	if(len > 4) {
		regval = data[4];
		if(len > 5)
			regval = data[4] << 8 | data[5];
		write_register(basepath, REG_DATA3, regval);
	}
	if(len > 6) {
		regval = data[6];
		if(len > 7)
			regval = data[6] << 8 | data[7];
		write_register(basepath, REG_DATA4, regval);
	}
	if(len > 8) {
		regval = data[8];
		if(len > 9)
			regval = data[8] << 8 | data[9];
		write_register(basepath, REG_DATA5, regval);
	}
	if(len > 10) {
		regval = data[10];
		if(len > 11)
			regval = data[10] << 8 | data[11];
		write_register(basepath, REG_DATA6, regval);
	}
	if(len > 12) {
		regval = data[12];
		if(len > 13)
			regval = data[12] << 8 | data[13];
		write_register(basepath, REG_DATA7, regval);
	}
	
	write_register(basepath, REG_STATUS, CMD_SET_DATA);
	
	read_register(basepath, REG_ZERO, &regval);
	if(regval != RESP_OK) {
		printf("Error writing chunk!\n");
		return 1;
	}
	read_register(basepath, REG_STATUS, &regval);
	if(regval != RESP_OK) {
		printf("Error writing chunk!\n");
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
	if(regval != 0x2) {
		printf("Error: WASP not ready (0x%x)\n", regval);
		return 1;
	}

	read_register(argv[2], REG_ZERO, &regval);
	if(regval != 0x2) {
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
	
	// FIXME: Timeout
	read_register(argv[2], REG_STATUS, &regval);
	while(regval == RESP_WAIT) {
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
