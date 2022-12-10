#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <time.h>

//mit arm-linux-gnueahbf-gcc -o devmem file.c
#define BASE_ADDR 0xff203080
#define LED_HIGH 0x7ff
#define LED_LOW 0x0

void delay(int number_of_seconds)
{
	// Converting time into clock_t clocks_per_second
	int seconds = 1000000 * number_of_seconds;

	// Storing start time
	clock_t start_time = clock();

	// looping till required time is not achieved
	while (clock() < start_time + seconds)
		;
}

int main(int argc, char **argv)
{
	long count = 10;

	if (argc > 2) {
		printf("Alternate usage: ./altrleds LEDS(0 - 10)\n");
		return -1;
	}

	if (argc == 2) {
		count = strtol(argv[1], NULL, 10);
		if (count > 10)
			count = 10;
	}

	int LEDS = count;

	int i = 0;
	int fd = open("/dev/mem", O_RDWR | O_SYNC);
	if (!fd)
		return -1;

	int pagesize = getpagesize();
	int virt_addr = BASE_ADDR & ~(pagesize - 1);
	int offset = (unsigned)BASE_ADDR & (pagesize - 1);

	// virtuell auf physischen berech ummappen
	uint32_t *reg =
		mmap(0, 4, PROT_READ | PROT_WRITE, MAP_SHARED, fd, virt_addr);
	if (reg == MAP_FAILED) {
		close(fd);
		return -2;
	}

	for (i = 0; i < LEDS; i++)
		*(reg + (offset / 4) + i) = LED_HIGH;

	//Wait 3 seconds before turning off LEDS
	delay(3);

	for (i = 0; i < LEDS; i++)
		*(reg + (offset / 4) + i) = LED_LOW;

	// Un-map and close files
	munmap(reg, 4);
	close(fd);

	return 0;
}