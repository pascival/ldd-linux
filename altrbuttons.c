#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdint.h>
#include <time.h>

#define UIO_NODE "/dev/uio0"
#define UIO_SIZE "/sys/class/uio/uio0/maps/map0/size"
#define BTN_OFFSET (0x50 / 4)
#define IRQ_OFFSET (BTN_OFFSET + (0x8 / 4))
#define EDGE_OFFSET (BTN_OFFSET + (0xC / 4))

int main(int argc, char **argv)
{
	int fd;
	FILE *size_fp;
	unsigned int uio_size;
	unsigned int uio_addr;
	uint32_t *uio_map;
	uint32_t io;

	// virtuell auf physischen berech ummappen
	fd = open(UIO_NODE, O_RDWR);
	if (fd < 0) {
		perror("uio open");
		return -1;
	}

	size_fp = fopen(UIO_SIZE, "r");
	if (size_fp < 0) {
		perror("fopen size");
		return -1;
	}
	fscanf(size_fp, "0x%016X", &uio_size);

	uio_map = mmap(0, uio_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (uio_map == MAP_FAILED) {
		close(fd);
		fclose(size_fp);
		perror("mmap");
		return 2;
	}

	// enable interrupt
	*(uio_map + EDGE_OFFSET) = 0xF;
	*(uio_map + IRQ_OFFSET) = 0xF;

	read(fd, &io, 4);
	printf("Detected button press: %d\n", *(uio_map + EDGE_OFFSET));

	io = 1;
	write(fd, &io, 4); // re-activates interrupts

	// decativate irq
	*(uio_map + IRQ_OFFSET) = 0x0;

	// Un-map and close files
	munmap(uio_map, uio_size);
	close(fd);
	fclose(size_fp);

	return 0;
}
