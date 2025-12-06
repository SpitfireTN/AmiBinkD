/*
 * pkt.c -- FTN packet creation and writing
 *
 * Part of AmiBinkD (modernized plain FTN build)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ftnaddr.h"
#include "tools.h"
#include "common.h"

/* Basic FTN packet header structure */
typedef struct {
    unsigned short origNode;
    unsigned short destNode;
    unsigned short year;
    unsigned short month;
    unsigned short day;
    unsigned short hour;
    unsigned short minute;
    unsigned short second;
    unsigned short baud;
    unsigned short pktType;
    unsigned short origNet;
    unsigned short destNet;
    unsigned short prodCode;
    unsigned short serialNo;
    unsigned short origZone;
    unsigned short destZone;
    unsigned short auxNet;
    unsigned short cwVal;
    unsigned short origPoint;
    unsigned short destPoint;
    char password[8];
} ftn_pkt_header_t;

/* Message structure */
typedef struct {
    char *from;
    char *to;
    char *subject;
    char *body;
} ftn_msg_t;

/* Create a new packet file */
FILE *pkt_open(const char *filename, ftn_pkt_header_t *hdr) {
    FILE *fp = fopen(filename, "wb");
    if (!fp) {
        Log(1, "Unable to create pkt file %s", filename);
        return NULL;
    }
    fwrite(hdr, sizeof(ftn_pkt_header_t), 1, fp);
    return fp;
}

/* Write a message into the packet */
int pkt_write_msg(FILE *fp, ftn_msg_t *msg) {
    if (!fp || !msg) return -1;

    fprintf(fp, "\001MSGID: %s %lu\r", msg->from, (unsigned long)time(NULL));
    fprintf(fp, "From: %s\r", msg->from);
    fprintf(fp, "To: %s\r", msg->to);
    fprintf(fp, "Subject: %s\r", msg->subject);
    fprintf(fp, "\r%s\r", msg->body);

    /* Branding tear line */
    fprintf(fp, "--- SpitfireTN Entertainment / ROF HQ\r");

    return 0;
}

/* Close packet */
void pkt_close(FILE *fp) {
    if (!fp) return;
    /* End of packet marker */
    fputc(0, fp);
    fputc(0, fp);
    fclose(fp);
}
