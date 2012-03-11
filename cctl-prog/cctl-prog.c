#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <stdbool.h>
#include <getopt.h>
#include <sys/time.h>

#define SERIAL_TIMEOUT 2

#ifdef __CYGWIN__
#undef WIN32
#endif

#ifndef WIN32
#include <termios.h>
#include <sys/select.h>
#else
#include <windows.h>
#include <wincon.h>
#include <time.h>
#define B115200 115200
#endif

#include "hex.h"

static struct option long_options[] =
{
    {"help",    no_argument, 0, 'h'},
    {"console",   no_argument, 0, 'c'},
    {"flash",     required_argument, 0, 'f'},
    {"device",     required_argument, 0, 'd'},
    {"timeout",     required_argument, 0, 't'},
    {0, 0, 0, 0}
};

void usage(void)
{
    fprintf(stderr, "ChipCon Tiny Loader Programmer\n");
#ifdef WIN32
    fprintf(stderr, "cctl-prog -d COMx [-c] [-f file.hex]\n");
#else
    fprintf(stderr, "cctl-prog -d /dev/ttyXYZ [-c] [-f file.hex]\n");
#endif
    fprintf(stderr, "  --help           -h          This help\n");
    fprintf(stderr, "  --console        -c          Connect console to serial port on device\n");
    fprintf(stderr, "  --flash=file.hex -f file.hex Reflash device with intel hex file\n");
    fprintf(stderr, "  --timeout=n      -t n        Search for bootload string for n seconds\n");
}

static bool opt_console = false;
static bool opt_flash = false;
static int opt_timeout = 10;
static bool opt_device = false;
static char *flash_filename = NULL;
static char *device_name = NULL;

#ifndef WIN32
static struct termios orig_termios;
#endif

int parse_options(int argc, char **argv)
{
    int c;
    int option_index;

    while(1)
    {
        c = getopt_long (argc, argv, "hcf:d:t:", long_options, &option_index);
        if (c == -1)
            break;
        switch(c)
        {
            case 'h':
                return 1;
            break;
            case 't':
                opt_timeout = atoi(optarg);
            break;
            case 'c':
                opt_console = true;
            break;
            case 'f':
                opt_flash = true;
                flash_filename = strdup(optarg);
            break;
            case 'd':
                opt_device = true;
                device_name = strdup(optarg);
            break;
            default:
                return 1;
            break;
        }
    }

    if (!opt_device)
        return 1;

    if (!opt_flash && !opt_console)
        return 1;

    return 0;
}

static void do_exit(void)
{
#ifndef WIN32
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
#endif
    puts("exiting\n");
}

#ifndef WIN32
static int enableRawMode(void)
{
    struct termios raw;

    if (!isatty(STDIN_FILENO)) 
        return -1;

    if (tcgetattr(STDIN_FILENO,&orig_termios) == -1)
        return -1;

    raw = orig_termios;  /* modify the original mode */
    raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    raw.c_oflag &= ~(OPOST);
    raw.c_cflag |= (CS8);
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;

    if (tcsetattr(STDIN_FILENO,TCSAFLUSH,&raw) < 0)
        return -1;
    return 0;
}
#endif

int serialOpen(char *port)
{
	int fd;
#ifndef WIN32
	struct termios t_opt;
#else
    char path[1024];
    HANDLE hCom = NULL;
#endif

#ifdef WIN32
    if (port[0] != '\\')
    {
        snprintf(path, sizeof(path), "\\\\.\\%s", port);
        port = path;
    }
#endif

#ifndef WIN32
	if ((fd = open(port, O_RDWR | O_NOCTTY | O_NONBLOCK)) < 0)
    {
		fprintf(stderr, "Could not open serial port %s\n", port);
		return -1;
	}
#else
    hCom = CreateFile(port, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);

    if (!hCom || hCom == INVALID_HANDLE_VALUE )
    {
        fprintf(stderr, "Invalid handle for serial port\n");
        return -1;
    }
    else
        fd = (int)hCom;
#endif

#ifndef WIN32
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
#else
{
    DCB dcb = {0};
    HANDLE hCom = (HANDLE)fd;
    COMMTIMEOUTS cto = { 2, 1, 1, 0, 0 };

     if(!SetCommTimeouts(hCom,&cto))
     {
        fprintf(stderr, "SetCommTimeouts failed\n");
        return -1;
     }


    dcb.DCBlength = sizeof(dcb);
    dcb.BaudRate = B115200;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;
    dcb.fBinary = 1;

    if (!SetCommState(hCom, &dcb))
    {
        fprintf(stderr, "Failed to setup serial port\n");
        return -1;
    }
    return (int)hCom;
}
#endif
}

#ifdef WIN32
int serialRead(int fd, void* buf, int len)
{
    HANDLE hCom = (HANDLE)fd;
    int res = 0;
    unsigned long bread = 0;

    res = ReadFile(hCom, buf, len, &bread, NULL);

    if (res == FALSE )
        return -1;
    else
        return bread;
}
#else
int serialRead(int fd, void* buf, int len)
{
    struct timeval start, now;
    int rc;

    gettimeofday(&start, NULL);

    do
    {
        if ((rc = read(fd, buf, len)) < 0)
            return rc;
        if (rc > 0)
            return rc;
        gettimeofday(&now, NULL);
    }
    while((now.tv_sec - start.tv_sec) < SERIAL_TIMEOUT);

    return 1;
}
#endif

#ifdef WIN32
int serialWrite(int fd, const void* buf, int len)
{
    HANDLE hCom = (HANDLE)fd;
    int res = 0;
    unsigned long bwritten = 0;

    res = WriteFile(hCom, buf, len, &bwritten, NULL);

    if (res == FALSE )
        return -1;
    else
        return bwritten;
}
#else
int serialWrite(int fd, void* buf, int len)
{
    struct timeval start, now;
    int rc;

    gettimeofday(&start, NULL);

    do
    {
        if ((rc = write(fd, buf, len)) < 0)
            return rc;
        if (rc > 0)
            return rc;
        gettimeofday(&now, NULL);
    }
    while((now.tv_sec - start.tv_sec) < SERIAL_TIMEOUT);

    return 1;
}
#endif

#ifdef WIN32
int serialClose(int fd)
{
    HANDLE hCom = (HANDLE)fd;
    CloseHandle(hCom);
    return 0;
}
#else
#define serialClose close
#endif

int program_page(int fd, int page)
{
    char cmd = 'p';
    char rsp;

    if (serialWrite(fd, &cmd, 1) <= 0)
        return 1;

    if (serialWrite(fd, &page, 1) <= 0)
        return 1;

    if (serialRead(fd, &rsp, 1) <= 0 || rsp != 0)
        return 1;

    return 0;
}

int read_page(int fd, uint8_t page, uint8_t *data)
{
    int remaining;
    int rc;
    char cmd = 'r';
    char rsp;

    if (serialWrite(fd, &cmd, 1) <= 0)
        return 1;

    if (serialWrite(fd, &page, 1) <= 0)
        return 1;

    remaining = 1024;
    while(remaining > 0)
    {
        rc = serialRead(fd, data + (1024 - remaining), remaining);
        if (rc <= 0)
            return 1;
        remaining -= rc;
    }

    if (serialRead(fd, &rsp, 1) <= 0 || rsp != 0)
        return 1;

    return 0;
}

int erase_page(int fd, uint8_t page)
{
    char cmd = 'e';
    char rsp;

    if (serialWrite(fd, &cmd, 1) <= 0)
        return 1;

    if (serialWrite(fd, &page, 1) <= 0)
        return 1;

    if (serialRead(fd, &rsp, 1) <= 0 || rsp != 0)
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

    if (serialWrite(fd, &cmd, 1) <= 0)
        return 1;

    remaining = 1024;
    while(remaining > 0)
    {
        rc = serialWrite(fd, data + (1024 - remaining), remaining);
        if (rc <= 0)
            return 1;
        remaining -= rc;
    }

    if (serialRead(fd, &rsp, 1) <= 0 || rsp != 0)
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

int wait_for_bootloader(int fd, int timeout)
{
    uint8_t c = 0;
    uint8_t prev_c = 0;
    struct timeval start, now;
    int rc;
    int last_sec = -1;

    gettimeofday(&start, NULL);

    serialWrite(fd, "+++", 3);

    printf("Waiting %ds for bootloader, reset board now\n", timeout);

    do
    {
        gettimeofday(&now, NULL);
        if (((now.tv_sec - start.tv_sec)) != last_sec)
        {
            printf(".");
            fflush(stdout);
            last_sec = (now.tv_sec - start.tv_sec);
        }

        if ((rc = serialRead(fd, &c, 1)) < 0)
        {
            fprintf(stderr, "read failed\n");
            return 1;
        }
        else
        if (rc == 1)
        {
            if (c == 'B' && prev_c == 'B')
                break;
            prev_c = c;
        }
    }
    while((now.tv_sec - start.tv_sec) < timeout);
    if (now.tv_sec - start.tv_sec >= timeout)
        return 1;

    c = 0x00;
    if (serialWrite(fd, &c, 1) <= 0)
        return 1;

    return 0;
}

int send_jump(int fd)
{
    uint8_t cmd = 'j';

    if (serialWrite(fd, &cmd, 1) <= 0)
        return 1;
    return 0;
}

#ifndef WIN32
void do_console(int fd)
{
    fd_set rfds;
    struct timeval tv;
    int rc;

    if (0 != enableRawMode())
    {
        fprintf(stderr, "Not a TTY?\n");
        return;
    }

    while(1)
    {
        uint8_t c;
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        int maxfd = 1;

        FD_ZERO(&rfds);
        FD_SET(0, &rfds);
#ifndef WIN32
        FD_SET(fd, &rfds);
        maxfd = fd+1;
#endif

        rc = select(maxfd, &rfds, NULL, NULL, &tv);

        if (rc == -1)
        {
            fprintf(stderr, "select failed\n");
            return;
        }
        else
        if (rc)
        {
            if (FD_ISSET(0, &rfds))
            {
                if(read(0, &c, 1) > 0)
                {
                    if (c == 0x03 || c == 0x04) // ctrl-d, ctrl-c
                    {
                        return;
                    }
                    if (1 != serialWrite(fd, &c, 1))
                    {
                        fprintf(stderr, "write error\n");
                        return;
                    }
                }
            }
            else
            if (FD_ISSET(fd, &rfds))
            {
                if(serialRead(fd, &c, 1) > 0)
                {
                    if (1 != write(1, &c, 1))
                    {
                        fprintf(stderr, "write error\n");
                        return;
                    }
                }
            }
        }
        else
        {
            // timeout
        }
    }
}
#else

DWORD CALLBACK SerialRxThread( HANDLE h )
{
    OVERLAPPED ov;
    HANDLE hconn = GetStdHandle(STD_INPUT_HANDLE);
    BOOL quit = FALSE;
    char kb;
    INPUT_RECORD b1;

    if (hconn == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "Invalid input handle\n");
        exit(1);
    }

    ZeroMemory(&ov,sizeof(ov));
    ov.hEvent = CreateEvent(NULL,FALSE,FALSE,NULL);
    if (ov.hEvent == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "CreateEvent failed\n");
        SetCommMask(h,0);
        return 0;
    }

    SetConsoleMode(hconn,0);

    do
    {
        char buf[2] = {0,0};
        DWORD rd = 0;

        WaitForSingleObject(hconn,INFINITE);

        // fetch input
        if(!ReadConsoleInput(hconn,&b1,1,&rd))
        {
            fprintf(stderr, "Failed to read from console, perhaps we're not running in cmd.exe?\n");
            quit  = TRUE;
        }
        else
        {
            kb=b1.Event.KeyEvent.uChar.AsciiChar;
            buf[0] = kb;
        }

        if(rd)
        {
            DWORD wr = 1;
            if (buf[0] == 0x3 ) // ctrl-c
            {
                quit = TRUE;
                break;
            }
            else
            {
                if (buf[0] != 0x0  && b1.Event.KeyEvent.bKeyDown)
                {
                    if (!WriteFile(h,buf,wr,&wr,&ov) )
                    {
                        if(GetLastError() == ERROR_IO_PENDING)
                        {
                            if (!GetOverlappedResult(h,&ov,&wr,TRUE) )
                            {
                                fprintf(stderr, "GetOverlappedResult failed\n");
                                quit = TRUE;
                            }
                        }
                    }
                    else
                    {
                        fprintf(stderr, "WriteFile failed\n");
                        quit = TRUE;
                    }
                } 
            }
        }
    }
    while(!quit);

    if (!SetCommMask(h,0))  // send request to terminate thread
        fprintf(stderr, "SetCommMask-GetLastError: %ld\n", GetLastError());
    return 0;
}

void do_console(int fd)
{
    DWORD mask;
    DWORD id;
    DWORD i;
    OVERLAPPED ov;
    HANDLE h = (HANDLE)fd;

    // start serial receiver thread
    HANDLE hconin = CreateThread(NULL, 0, SerialRxThread, h, 0, &id);
    if (hconin == INVALID_HANDLE_VALUE )
    {
        fprintf(stderr, "CreateThread failed\n");
        return;
    }
    CloseHandle(hconin);

    // Overlapped IO event
    ZeroMemory(&ov, sizeof(ov));
    ov.hEvent = CreateEvent(NULL, FALSE, FALSE, NULL);
    if(ov.hEvent == INVALID_HANDLE_VALUE)
    {
        fprintf(stderr, "CreateThread failed\n");
        return;
    }

    // only interested in rx
    if(!SetCommMask(h, EV_RXCHAR))
    {
        fprintf(stderr, "SetCommMask failed\n");
        return;
    }

    while (1)
    {
        if (!WaitCommEvent(h, &mask, &ov))
        {
            if (GetLastError() == ERROR_IO_PENDING )
            {
                DWORD r;
                if (!GetOverlappedResult(h,&ov,&r,TRUE) )
                {
                    fprintf(stderr, "GetOverlappedResult failed\n");
                    break;
                }
            }
            else
            {
                break;
            }
        }

        // See if we've been asked to terminate
        if ( mask == 0 )
            break;

        if (mask & EV_RXCHAR)   // received data
        {
            char buf[1024] = {0};
            DWORD readcount;

            do
            {
                readcount = 0;
                if (!ReadFile(h, buf, sizeof(buf), &readcount, &ov))
                {
                    if (GetLastError() == ERROR_IO_PENDING)
                    {
                        if (!GetOverlappedResult(h, &ov, &readcount, TRUE))
                        {
                            fprintf(stderr, "GetOverlappedResult failed\n");
                            break;
                        }
                    }
                    else
                    {
                        fprintf(stderr, "ReadFile failed\n");
                        break;
                    }
                }
                for (i=0;i<readcount;i++)
                {
                    putchar(buf[i]);
                    fflush(stdout);
                }
            }
            while(readcount);
        }
        mask = 0;
    }
    CloseHandle(ov.hEvent);
}
#endif


int main(int argc, char *argv[])
{
    int fd;
    uint8_t *buf;
    int i, j;

    if (NULL == (buf=malloc(32*1024)))
    {
        fprintf(stderr, "out of ram\n");
        return 1;
    }   

    if (0 != parse_options(argc, argv))
    {
        usage();
        return 1;
    }

    if ((fd = serialOpen(device_name)) < 0)
    {
        fprintf(stderr, "Failed to open %s\n", device_name);
        return 1;
    }

    if (opt_flash)
    {
        memset(buf, 0xFF, 32*1024);
        if (0 != read_hexfile(buf, 32*1024, flash_filename))
        {
            fprintf(stderr, "Failed to read %s\n", flash_filename);
            return 1;
        }

        if (0 != wait_for_bootloader(fd, opt_timeout))
        {
            fprintf(stderr, "No bootloader detected\n");
            return 1;
        }
        else
        {
            printf("Bootloader detected\n");
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
                printf("Erasing, programming and verifying page %d\n", i/1024);
                if (0 != erase_program_verify_page(fd, buf + i, i/1024))
                {
                    fprintf(stderr, "erase_program_verify_page failed\n");
                    return 1;
                }
            }
            else
            {
                printf("Erasing page %d\n", i/1024);
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
            return 1;
        }

        printf("Programming complete\n");
    }

    if (opt_console)
    {
        atexit(do_exit);
        printf("Connected to %s, ctrl-c to exit\n", device_name);
        do_console(fd);
    }

    serialClose(fd);

    return 0;
}


