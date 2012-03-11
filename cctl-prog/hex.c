#include <stdio.h>
#include <unistd.h>
#include <ctype.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#ifdef __CYGWIN__
#undef WIN32
#endif

#ifndef WIN32
#include <arpa/inet.h>
#else
#include "byteswap.h"
#endif

static uint8_t tolowercase(uint8_t ch)
{
    if ((ch >= 'A') && (ch <= 'Z'))
        return ch + 0x20;       // Convert uppercase to lowercase
    return ch;
}

static int8_t parseHexDigit(uint8_t digit)
{
    digit = tolowercase(digit);
    if (isdigit(digit))
        return (int8_t)digit - '0';
    if ((digit >= 'a') && (digit <= 'f'))
        return (int8_t)digit + 0xA - 'a';
    return -1;      // Error case - input wasn't a valid hex digit
}


static int hexstring_parse(const char *hexstr, uint8_t *buf, size_t *buflen)
{
    size_t hexstrlen = *buflen * 2;
    size_t i;

    if (hexstrlen & 0x1)
    {
        fprintf(stderr, "hexstring_parse: not even\n");
        return 1;
    }

    if (*buflen < hexstrlen/2)
    {
        fprintf(stderr, "hexstring_parse: buffer too small %zu < %zu\n", *buflen, hexstrlen/2);
        return 1;
    }

    for (i=0;i<hexstrlen;i+=2)
    {
        int8_t a, b;
        if (-1 == (a = parseHexDigit(hexstr[i])))
        {
            fprintf(stderr, "hexstring_parse: bad digit 0x%02X\n", hexstr[i]);
            return 1;
        }
        if (-1 == (b = parseHexDigit(hexstr[i+1])))
        {
            fprintf(stderr, "hexstring_parse: bad digit 0x%02X\n", hexstr[i+1]);
            return 1;
        }
        *buf++ = (a << 4) | b;
    }

    *buflen = hexstrlen/2;

    return 0;
}

static int read_record(uint8_t *buf, size_t buflen, const char *line, bool *eof)
{
    size_t len;
    uint8_t sum = 0;
    uint8_t record_sum;
    uint8_t record_len;
    uint8_t type;
    uint16_t addr;
    uint8_t data[256];
    int i;

    *eof = false;

    if (line[0] != ':')
    {
        fprintf(stderr, "bad hexfile: no start\n");
        return 1;
    }
    line+=1;

    len = 1;
    if (0 != hexstring_parse(line, &record_len, &len))
    {
        fprintf(stderr, "bad hexfile: no len '%s'\n", line);
        return 1;
    }
    line+=len * 2;

    len = 2;
    if (0 != hexstring_parse(line, (uint8_t *)&addr, &len))
    {
        fprintf(stderr, "bad hexfile: no addr\n");
        return 1;
    }
    addr = ntohs(addr);
    line+=len * 2;

    len = 1;
    if (0 != hexstring_parse(line, &type, &len))
    {
        fprintf(stderr, "bad hexfile: no type\n");
        return 1;
    }
    line+=len * 2;

    len = record_len;
    if (0 != hexstring_parse(line, data, &len))
    {
        fprintf(stderr, "bad hexfile: no data\n");
        return 1;
    }
    line+=len * 2;

    len = 1;
    if (0 != hexstring_parse(line, &record_sum, &len))
    {
        fprintf(stderr, "bad hexfile: no sum\n");
        return 1;
    }
    line+=len * 2;

    if (type == 0)
    {
        sum += record_len;
        sum += addr >> 8;
        sum += addr & 0xFF;

        for (i=0;i<record_len;i++)
            sum += data[i];
        sum = (sum ^ 0xFF) + 1;

        if (sum != record_sum)
        {
            fprintf(stderr, "bad hexfile, checksum mismatch\n");
            return 1;
        }

        if (addr + record_len > buflen)
        {
            fprintf(stderr, "bad hexfile, too big\n");
            return 1;
        }
        memcpy(buf + addr, data, record_len);
    }
    else
    if (type == 1)
    {
        *eof = true;
    }
    else
    {
        fprintf(stderr, "bad hexfile: unknown record type %02X\n", type);
    }

    return 0;
}

int read_hexfile(uint8_t *buf, size_t buflen, const char *filename)
{
    FILE *fp;
    char line[1024];
    bool eof;

    if (NULL == (fp = fopen(filename, "ro")))
        return 1;

    while (NULL != fgets(line, sizeof(line), fp))
    {
        char *p = (line + strlen(line)) - 1;

        while(p > line)
        {
            if (isspace((int)(*p)))
                *p = 0;
            p--;
        }

        if (0 != read_record(buf, buflen, line, &eof))
            goto fail;

        if (eof)
            break;
    }

    fclose(fp);
    return 0;
fail:
    fclose(fp);
    return 1;
}


