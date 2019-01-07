// Copyright (C) 2016-2019 Semtech (International) AG. All rights reserved.
//
// This file is subject to the terms and conditions defined in file 'LICENSE',
// which is part of this source code package.

#if defined(CFG_prog_crc32)

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>


static uint32_t crc_table[256];

static void init_crc_table () {
    const uint32_t POLYNOMIAL = 0xEDB88320;
    uint32_t remainder;
    unsigned char b = 0;
    do {
        remainder = b;
        for( int bit = 8; bit > 0; --bit )
            remainder = (remainder >> 1) ^ (POLYNOMIAL * (remainder & 1));
        crc_table[b] = remainder;
    } while( ++b );
}


uint32_t crc32 (uint32_t crc, const void *buf, size_t size) {
    const uint8_t *p = (uint8_t*)buf;

   crc = crc ^ ~0U;
   while( size-- > 0 )
       crc = crc_table[(crc ^ *p++) & 0xFF] ^ (crc >> 8);
   return crc ^ ~0U;
}


int main (int argc, char** argv) {
    if( argc<=1 ) {
        fprintf(stderr,
                "usage: crc32 TABLE\n"
                "usage: crc32 {-|file}..\n");
        return 1;
    }
    init_crc_table();

    if( argc == 2 && strcmp(argv[1], "TABLE") == 0 ) {
        printf("static const uint32_t crc_table[256] = {\n");
        for( int i=0; i<256; i+=8 ) {
            printf("    ");
            for( int j=0; j<8; j++ )
                printf("0x%08X,", crc_table[i+j]);
            printf("\n");
        }
        printf("};\n");
        return 0;
    }

    unsigned all = 0;
    for( int i=1; i<argc; i++ ) {
        char* file = argv[i];
        FILE* f;
        if( strcmp(file, "-") == 0 ) {
            f = stdin;
        } else {
            f = fopen(file,"r");
            if( f == NULL ) {
                fprintf(stderr, "Failed to open '%s': %s\n", file, strerror(errno));
                continue;
            }
        }
        char buf[8*1024];
        unsigned crc = 0;
        while(1) {
            int l = fread(buf, 1, sizeof(buf), f);
            crc = crc32(crc, buf, l);
            all = crc32(all, buf, l);
            if( l < sizeof(buf) )
                break;
        }
        if( ferror(f) ) {
            fclose(f);
            fprintf(stderr, "Failed to read '%s': %s\n", file, strerror(errno));
            continue;
        }
        printf("0x%08X %s\n", crc, file);
        fclose(f);
    }
    if( argc > 2 )
        printf("0x%08X over all files\n", all);
    return 0;
}

#endif //defined(CFG_prog_crc32)
