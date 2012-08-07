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

#include "dbg.h"
#include "hex.h"

#define FLASH_SIZE (32*1024)

static bool opt_flash = false;
static char *flash_filename = NULL;

static struct option long_options[] =
{
    {"help",    no_argument, 0, 'h'},
    {"flash",     required_argument, 0, 'f'},
    {0, 0, 0, 0}
};

static void usage(void)
{
    fprintf(stderr, "ChipCon Pi Loader, Toby Jaffey <toby-ccpl@hodgepig.org>\n");
    fprintf(stderr, "cctl-prog [-f file.hex]\n");
    fprintf(stderr, "  --help           -h          This help\n");
    fprintf(stderr, "  --flash=file.hex -f file.hex Reflash device with intel hex file\n");
}

static int parse_options(int argc, char **argv)
{
    int c;
    int option_index;

    while(1)
    {
        c = getopt_long (argc, argv, "hf:", long_options, &option_index);
        if (c == -1)
            break;
        switch(c)
        {
            case 'h':
                return 1;
            break;
            case 'f':
                opt_flash = true;
                flash_filename = strdup(optarg);
            break;
            default:
                return 1;
            break;
        }
    }

    return 0;
}

static void dump(const uint8_t *p, size_t len)
{
    while(len--)
        printf("%02X", *p++);
    printf("\n");
}

static int program_verify_page(const uint8_t *data, uint8_t page)
{
    uint8_t verbuf[1024];

    if (0 != dbg_writepage(page, data))
    {
        fprintf(stderr, "program_page failed\n");
        return 1;
    }

    if (0 != dbg_readpage(page, verbuf))
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

int main(int argc, char *argv[])
{
    uint8_t *buf;
    int i;

    if (NULL == (buf=malloc(FLASH_SIZE)))
    {
        fprintf(stderr, "out of ram\n");
        return 1;
    }   

    if (0 != parse_options(argc, argv))
    {
        usage();
        return 1;
    }

    if (!opt_flash)
    {
        usage();
        return 1;
    }

    if (opt_flash)
    {
        if (0 != dbg_init())
        {
            fprintf(stderr, "Failed to initialise (run as root for /dev/mem access)\n");
            return 1;
        }

        memset(buf, 0xFF, FLASH_SIZE);
        if (0 != read_hexfile(buf, FLASH_SIZE, flash_filename))
        {
            fprintf(stderr, "Failed to read %s\n", flash_filename);
            return 1;
        }

        if (0 != dbg_mass_erase())
        {
            fprintf(stderr, "CC1110 mass erase failed\n");
            return 1;
        }

        for (i=0;i<FLASH_SIZE;i+=1024)
        {
            bool skip = true;
            int j;

            for (j=i;j<i+1024;j++)
            {
                if (buf[j] != 0xFF)
                {
                    skip = false;
                    break;
                }
            }
            if (skip)
            {
                printf("Skipping blank page %d\n", i/1024);
                continue;
            }

            printf("Programming and verifying page %d\n", i/1024);
            if (0 != program_verify_page(buf + i, i/1024))
            {
                fprintf(stderr, "FAILED\n");
                return 1;
            }
        }

        printf("Programming complete\n");
        dbg_reset();
    }

    return 0;
}


