#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <sys/select.h>
#include <sys/types.h>
#include <stdbool.h>

#include "hex.h"

int serial_open(char *port)
{
	int fd;
	struct termios t_opt;

	fd = open(port, O_RDWR | O_NOCTTY);
	if (fd == -1) {
		fprintf(stderr, "Could not open serial port.");
		return -1;
	}

	fcntl(fd, F_SETFL, 0);
	tcgetattr(fd, &t_opt);
	cfsetispeed(&t_opt, B115200);
	cfsetospeed(&t_opt, B115200);
	t_opt.c_cflag |= (CLOCAL | CREAD);
    t_opt.c_cflag &= ~PARENB;
	t_opt.c_cflag &= ~CSTOPB;
	t_opt.c_cflag &= ~CSIZE;
	t_opt.c_cflag |= CS8;
	t_opt.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
	t_opt.c_iflag &= ~(IXON | IXOFF | IXANY);
	t_opt.c_oflag &= ~OPOST;
	t_opt.c_cc[VMIN] = 0;
	t_opt.c_cc[VTIME] = 10;
	tcflush(fd, TCIFLUSH);
	tcsetattr(fd, TCSANOW, &t_opt);

	return fd;
}

int program_page(int fd, int page)
{
    char cmd = 'p';
    char rsp;

    if (write(fd, &cmd, 1) <= 0)
        return 1;

    if (write(fd, &page, 1) <= 0)
        return 1;

    if (read(fd, &rsp, 1) <= 0 || rsp != 0)
        return 1;

    return 0;
}

int read_page(int fd, uint8_t page, uint8_t *data)
{
    int remaining;
    int rc;
    char cmd = 'r';
    char rsp;

    if (write(fd, &cmd, 1) <= 0)
        return 1;

    if (write(fd, &page, 1) <= 0)
        return 1;

    remaining = 1024;
    while(remaining > 0)
    {
        rc = read(fd, data + (1024 - remaining), remaining);
        if (rc <= 0)
            return 1;
        remaining -= rc;
    }

    if (read(fd, &rsp, 1) <= 0 || rsp != 0)
        return 1;

    return 0;
}

int erase_page(int fd, uint8_t page)
{
    char cmd = 'e';
    char rsp;

    if (write(fd, &cmd, 1) <= 0)
        return 1;

    if (write(fd, &page, 1) <= 0)
        return 1;

    if (read(fd, &rsp, 1) <= 0 || rsp != 0)
    {
        fprintf(stderr, "erase_page rsp=%02X\n", rsp);
        return 1;
    }

    return 0;
}

int load_data(int fd, uint8_t *data)
{
    int remaining;
    int rc;
    char cmd = 'l';
    char rsp;

    if (write(fd, &cmd, 1) <= 0)
        return 1;

    remaining = 1024;
    while(remaining > 0)
    {
        rc = write(fd, data + (1024 - remaining), remaining);
        if (rc <= 0)
            return 1;
        remaining -= rc;
    }

    if (read(fd, &rsp, 1) <= 0 || rsp != 0)
        return 1;

    return 0;
}

void dump(uint8_t *p, size_t len)
{
    while(len--)
        printf("%02X", *p++);
    printf("\n");
}

int erase_program_verify_page(int fd, uint8_t *data, uint8_t page)
{
    uint8_t verbuf[1024];

    if (0 != load_data(fd, data))
    {
        fprintf(stderr, "load_data failed\n");
        return 1;
    }

    if (0 != erase_page(fd, page))
    {
        fprintf(stderr, "erase_page failed\n");
        return 1;
    }

    if (0 != program_page(fd, page))
    {
        fprintf(stderr, "program_page failed\n");
        return 1;
    }

    if (0 != read_page(fd, page, verbuf))
    {
        fprintf(stderr, "read_page failed\n");
        return 1;
    }

    if (0!=memcmp(verbuf, data, 1024))
    {
        fprintf(stderr, "verify failed\n");

        printf("verbuf = ");
        dump(verbuf, 1024);
        printf("expected = ");
        dump(data, 1024);

        return 1;
    }

    return 0;
}

int wait_for_bootloader(int fd)
{
    uint8_t c;
    uint8_t prev_c = 0;
    int attempts = 100;

    while(attempts-- > 0)
    {
        printf("Waiting...\n");
        if (read(fd, &c, 1) < 0)
            return 1;
        else
        {
            if (c == 'B' && prev_c == 'B')
                break;
            prev_c = c;
        }
    }

    c = 0x00;
    if (write(fd, &c, 1) <= 0)
        return 1;

    return 0;
}

int send_jump(int fd)
{
    uint8_t cmd = 'j';

    if (write(fd, &cmd, 1) <= 0)
        return 1;
    return 0;
}

int main(int argc, char *argv[])
{
    int fd;
    uint8_t buf[32*1024];
    int i, j;

    if (argc < 3)
    {
        fprintf(stderr, "Usage %s <serial device> <hex file>\n", argv[0]);
        return 1;
    }

    fd = serial_open(argv[1]);
    if (fd < 0)
    {
        fprintf(stderr, "Failed to open %s\n", argv[1]);
        return 1;
    }

    memset(buf, 0xFF, sizeof(buf));
    if (0 != read_hexfile(buf, sizeof(buf), argv[2]))
    {
        fprintf(stderr, "Failed to read %s\n", argv[2]);
        return 1;
    }

    if (0 != wait_for_bootloader(fd))
    {
        fprintf(stderr, "No bootloader detected\n");
        return 1;
    }

    for (i=0x400;i<32*1024;i+=1024)
    {
        bool all_empty = true;
        for (j=i;j<i+1024;j++)
        {
            if (buf[j] != 0xFF)
            {
                all_empty = false;
                break;
            }
        }
        if (!all_empty)
        {
            printf("erase/write/verify page %d\n", i/1024);
            if (0 != erase_program_verify_page(fd, buf + i, i/1024))
            {
                fprintf(stderr, "erase_program_verify_page failed\n");
                return 1;
            }
        }
        else
        {
            printf("erasing page %d\n", i/1024);
            if (0 != erase_page(fd, i/1024))
            {
                fprintf(stderr, "erase failed\n");
                return 1;
            }
        }
    }
    if (0 != send_jump(fd))
    {
        fprintf(stderr, "send jump failed\n");
    }

    close(fd);

    return 0;
}


