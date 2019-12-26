#include <stdio.h>
#include <stdlib.h>

int main(int argc, char *argv[]) {
	printf("AVM WASP uploader.\n");
	if(argc > 1) {
		printf("Arguments:\n");
		for(int i=1; i<argc; i++) {
			printf("%s\n", argv[i]);
		}
	}
	
	return 0;
}
