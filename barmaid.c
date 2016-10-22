/* Barmaid is a command line tool to manipulate BTW files.
 * Written and placed into the public domain by
 * Elias Oenal <barmaid@eliasoenal.com>
 **/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <ctype.h>
#include <sys/stat.h>

#include "barflate.h"

#define APPNAME "barmaid"
#define BUFF_SIZ                8192
#define LONGEST_MAGIC_STRING    32
#define min(x, y) (((x) < (y)) ? (x) : (y))

const char barmaid_help[] = "barmaid 1.0\n"
                            "Written and placed into the public domain by\n"
                            "Elias Oenal <barmaid@eliasoenal.com>\n"
                            "\n"
                            "usage: " APPNAME " [-options] [<file>]\n"
                            "  either extract (-e) or build (-b) has to be provided\n"
                            "  parameters -c -i -m -p are input in build mode and output in extract mode\n"
                            "  -e         extract mode\n"
                            "  -b         build mode (yet to be implemented)\n"
                            "  -c <file>  container file\n"
                            "  -h         display this help\n"
                            "  -i <file>  preview png image\n"
                            "  -m <file>  mask png image\n"
                            "  -p <file>  prefix file\n"
                            "  -s         heuristics scan for png images\n"
                            "  -v         verbose\n";

typedef struct blobs{
    bool success;
    long prefix_end;
    long png_start[2];
    long png_end[2];
    bool container_zlib;
    long container_start;
    long container_end;
} blobs;

const uint8_t barmaid_start_mseq_png[] = {0x89,  'P',  'N',  'G', 0x0D, 0x0A, 0x1A, 0x0A,
                                          0x00, 0x00, 0x00, 0x0D,  'I',  'H',  'D',  'R'};
const uint8_t barmaid_end_mseq_png[]   = {0x00, 0x00, 0x00, 0x00,  'I',  'E',  'N',  'D',
                                          0xAE, 0x42, 0x60, 0x82};

const uint8_t barmaid_btw_mseq_sof[] = {0x0D, 0x0A, 'B', 'a', 'r', ' ', 'T', 'e', 'n', 'd', 'e', 'r', ' ',
                                        'F', 'o', 'r', 'm', 'a', 't', ' ', 'F', 'i', 'l', 'e', 0x0D, 0x0A};

const uint8_t barmaid_btw_end_of_meta[] = {0xFF, 0xFE, 0xFF, 0x00};
const uint8_t barmaid_btw_zlib[] = {0x00, 0x01};

long barmaid_find_seq(FILE* fil, long offset, const uint8_t* seq, size_t seq_len);
blobs barmaid_heuristic_png(FILE* fil);
blobs barmaid_parse_btw(FILE* fil);
long barmaid_skip_padding(FILE* fil, long offset);
bool barmaid_dump_file(FILE* infile, long start, long end, FILE* outfile);
bool barmaid_is_btw(FILE* fil);

int main(int argc, char *argv[])
{
    unsigned int verbosity = 0;
    bool extract = false;
    bool build = false;
    bool heuristic = false;
    bool help = false;
    int status = 0;

    char* png_str[2] = {NULL, NULL};
    FILE* png[2] = {NULL, NULL};
    char* file_str = NULL;
    FILE* file = NULL;
    char* prefix_str = NULL;
    FILE* prefix = NULL;
    char* container_str = NULL;
    FILE* container = NULL;

    int opt;
    while((opt = getopt(argc, argv, "behsvc:i:m:p:")) != -1)
    {
        switch(opt)
        {
        case 'h':
            help = true;
            break;
        case 's':
            heuristic = true;
            break;
        case 'p':
            prefix_str = optarg;
            break;
        case 'c':
            container_str = optarg;
            break;
        case 'b':
            build = true;
            fprintf(stderr, "%s: -b not yet implemented\n", APPNAME);
            goto err_abort;
        case 'e':
            extract = true;
            break;
        case 'i': // Preview image
            png_str[0] = optarg;
            break;
        case 'm': // Mask image
            png_str[1] = optarg;
            break;
        case 'v':
            verbosity = 1;
            break;
        }
    }

    if(help || argc < 2)
    {
        fprintf(stderr, barmaid_help);
        goto end;
    }

    if(!(build ^ extract))
    {
        fprintf(stderr, "%s: either build (-b) or extract (-e) required\n", APPNAME);
        goto err_abort;
    }

    if(optind + 1 < argc)
    {
        fprintf(stderr, "%s: too many arguments\n", APPNAME);
        goto err_abort;
    }

    if(optind < argc)
    {
        file_str = argv[optind];
        file = fopen(file_str, extract?"rb":"wb");
        if(!file)
        {
            fprintf(stderr, "%s: %s: failed to open file\n", APPNAME, file_str);
            goto err_abort;
        }
    }
    else
    {
        fprintf(stderr, "%s: filename required\n", APPNAME);
        goto err_abort;
    }

    // Parse file
    blobs b;
    if(heuristic)
    {
        if(verbosity)
            fprintf(stderr, "%s: heuristics active - functionality limited\n", APPNAME);

        b = barmaid_heuristic_png(file);
        if(!b.success)
        {
            fprintf(stderr, "%s: %s: heuristic failed to identify images\n", APPNAME, file_str);
            goto err_abort;
        }
    }
    else
    {
        b = barmaid_parse_btw(file);
        if(!b.success)
        {
            fprintf(stderr, "%s: %s: failed to parse file\n", APPNAME, file_str);
            goto err_abort;
        }
    }

    // Open image files
    for(int i = 0; i < 2; i++)
    {
        if(png_str[i])
        {
            png[i] = fopen(png_str[i], extract?"wb":"rb");
            if(!png[i])
            {
                fprintf(stderr, "%s: %s: failed to open file\n", APPNAME, png_str[i]);
                goto err_abort;
            }
        }
    }

    if(!heuristic)
    {
        // Open container file
        if(container_str)
        {
            container = fopen(container_str, extract?"wb":"rb");
            if(!container)
            {
                fprintf(stderr, "%s: %s: failed to open file\n", APPNAME, container_str);
                goto err_abort;
            }
        }

        // Open prefix file
        if(prefix_str)
        {
            prefix = fopen(prefix_str, extract?"wb":"rb");
            if(!prefix)
            {
                fprintf(stderr, "%s: %s: failed to open file\n", APPNAME, prefix_str);
                goto err_abort;
            }
        }
    }

    // Dump preview images
    for(int i = 0; i < 2; i++)
    {
        if(verbosity)
            fprintf(stderr, "%s: found PNG #%i: 0x%lX - 0x%lX\n", APPNAME, i, b.png_start[i], b.png_end[i]);
        if(png[i])
        {
            if(!barmaid_dump_file(file, b.png_start[i], b.png_end[i], png[i]))
            {
                fprintf(stderr, "%s: %s: failed to write png\n", APPNAME, png_str[i]);
                goto err_abort;
            }
            else if(verbosity)
                fprintf(stderr, "%s: %s: wrote png\n", APPNAME, png_str[i]);
        }
    }

    // Process container and prefix, if heuristics are disabled
    if(!heuristic)
    {
        if(verbosity)
            fprintf(stderr, "%s: identified prefix: 0x%X - 0x%lX\n", APPNAME, 0, b.prefix_end);
        if(prefix)
        {
            fseek(file, 0, SEEK_SET);
            if(!barmaid_dump_file(file, 0, b.prefix_end, prefix))
            {
                fprintf(stderr, "%s: %s: failed to dump prefix\n", APPNAME, prefix_str);
                goto err_abort;
            }
            else if(verbosity)
                fprintf(stderr, "%s: %s: wrote prefix\n", APPNAME, prefix_str);
        }

        if(verbosity)
            fprintf(stderr, "%s: found %s container: 0x%lX - 0x%lX\n",
                    APPNAME, b.container_zlib?"compressed":"uncompressed",
                    b.container_start, b.container_end);
        if(container)
        {
            fseek(file, b.container_start, SEEK_SET);
            if(b.container_zlib)
            {
                if(inf(file, container) != 0)
                {
                    fprintf(stderr, "%s: %s: failed to extract container\n", APPNAME, container_str);
                    goto err_abort;
                }
                else if(verbosity)
                    fprintf(stderr, "%s: %s: wrote extracted container\n", APPNAME, container_str);
            }
            else
            {
                if(!barmaid_dump_file(file, b.container_start, b.container_end, container))
                {
                    fprintf(stderr, "%s: %s: failed to dump container\n", APPNAME, container_str);
                    goto err_abort;
                }
                else if(verbosity)
                    fprintf(stderr, "%s: %s: wrote container\n", APPNAME, container_str);
            }
        }
    }

    goto end;

err_abort:
    status = 1;

end:
    if(file)
        fclose(file);
    if(png[0])
        fclose(png[0]);
    if(png[1])
        fclose(png[1]);
    if(prefix)
        fclose(prefix);
    if(container)
        fclose(container);
    return status;
}


// Using a simple heuristic to find the preview images
blobs barmaid_heuristic_png(FILE* fil)
{
    blobs b = {b.success = false, -1, {-1, -1}, {-1, -1}, false, -1, -1};
    long offset = 0;

    for(int i = 0; i < 2; i++)
    {
        b.png_start[i] = barmaid_find_seq(fil, offset, barmaid_start_mseq_png, sizeof(barmaid_start_mseq_png));
        if(b.png_start[i] < 0)
            return b;
        b.png_end[i] = barmaid_find_seq(fil, b.png_start[i], barmaid_end_mseq_png, sizeof(barmaid_end_mseq_png));
        if(b.png_end[i] < 0)
            return b;

        b.png_end[i] += sizeof(barmaid_end_mseq_png);
        offset = b.png_end[i];
    }
    b.success = true;
    return b;
}

blobs barmaid_parse_btw(FILE* fil)
{
    blobs b = {b.success = false, -1, {-1, -1}, {-1, -1}, false, -1, -1};

    if(!barmaid_is_btw(fil))
        return b;

    // Find end of header
    long found = barmaid_find_seq(fil, sizeof(barmaid_btw_mseq_sof), barmaid_btw_end_of_meta,
                                 sizeof(barmaid_btw_end_of_meta));
    if(found < 0)
        return b;

    long start_of_blobsize = barmaid_skip_padding(fil, found + sizeof(barmaid_btw_end_of_meta));
    b.prefix_end = start_of_blobsize;

    uint8_t buff[4];
    uint32_t blobsize = 0;

    // Parse the two PNG images
    for(int i = 0; i < 2; i++)
    {
        fseek(fil, start_of_blobsize, SEEK_SET);

        if(!fread(&buff, sizeof(buff[0]), sizeof(buff), fil))
            return b;

        // Portable way to deal with little endian
        for(int j = 3; j >= 0; j--)
        {
            blobsize <<= 8;
            blobsize |= buff[j];
        }

        b.png_start[i] = start_of_blobsize + sizeof(buff);
        b.png_end[i] = b.png_start[i] + blobsize;
        start_of_blobsize = barmaid_skip_padding(fil, b.png_end[i]);
    }

    // Start of container
    b.container_start = start_of_blobsize;
    if(fread(&buff, sizeof(buff[0]), 2, fil) != 2)
        return b;
    if(!memcmp(buff, barmaid_btw_zlib, sizeof(barmaid_btw_zlib)))
    {
        b.container_start += sizeof(barmaid_btw_zlib);
        b.container_zlib = true;
    }

    // End of container
    struct stat st;
    // Using POSIX to determine file size
    int fd = fileno(fil);
    if(!fd || fstat(fd, &st))
        return b;
    b.container_end = st.st_size;
    if(b.container_start <= 0 || b.container_end <= 0)
        return b;

    b.success = true;
    return b;
}

long barmaid_skip_padding(FILE* fil, long offset)
{
    uint8_t buff[4];
    if(offset >= 0)
        if(fseek(fil, offset, SEEK_SET))
            return -2;

    while(1)
    {
        long cur_pos = ftell(fil);
        if(!fread(&buff, sizeof(buff[0]), sizeof(buff), fil))
        {
            fseek(fil, cur_pos, SEEK_SET);
            return -1;
        }

        if(buff[0] || buff[1] || buff[2] || buff[3])
        {
            fseek(fil, cur_pos, SEEK_SET);
            return cur_pos;
        }

    }
}

// Check for magic sequence at start of file
bool barmaid_is_btw(FILE* fil)
{
    uint8_t buff[BUFF_SIZ];
    size_t read = fread(buff, 1, sizeof(barmaid_btw_mseq_sof), fil);
    if(read && !memcmp(buff, barmaid_btw_mseq_sof, sizeof(barmaid_btw_mseq_sof)))
        return true;
    return false;
}

long barmaid_find_seq(FILE* fil, long offset, const uint8_t* seq, size_t seq_len)
{
    uint8_t search_buff[BUFF_SIZ + LONGEST_MAGIC_STRING];

    if(!fil)
        return -2;
    if(offset >= 0)
        if(fseek(fil, offset, SEEK_SET))
            return -3;

    int chunk_count = 0;
    long read;
    while((read = fread(&search_buff[LONGEST_MAGIC_STRING], 1, BUFF_SIZ, fil)))
    {
        long i = 0;
        if(chunk_count)
        {
            memmove(search_buff, &search_buff[BUFF_SIZ], LONGEST_MAGIC_STRING);
            i -= LONGEST_MAGIC_STRING;
        }

        for(; i < read; i++)
        {
            if(!memcmp(&search_buff[LONGEST_MAGIC_STRING + i], seq, seq_len))
            {
                long pos = offset + i + chunk_count * BUFF_SIZ;
                fseek(fil, pos, SEEK_SET);
                return pos;
            }
        }

        chunk_count++;
    }

    return -1;
}

bool barmaid_dump_file(FILE* infile, long start, long end, FILE* outfile)
{
    size_t size = end - start;
    uint8_t buff[BUFF_SIZ];

    fseek(infile, start, SEEK_SET);
    fseek(outfile, 0, SEEK_SET);
    for(size_t i = 0; i < size; i += sizeof(buff))
    {
        size_t chunk = min(size - i, sizeof(buff));
        if(fread(buff, 1, chunk, infile) != chunk)
            goto err_abort;

        if(fwrite(buff, 1, chunk, outfile) != chunk)
            goto err_abort;
    }
    return true;

err_abort:
    return false;
}
