#ifndef FTN_PACKET_H
#define FTN_PACKET_H

#include <stdio.h>
#include "ftnaddr.h"

/* Basic 16‑bit type for FTN packet fields */
typedef unsigned short UINT16;

/* FTN Type‑2+ packet header (compatible with FTS‑0001 / FSC‑0048) */
typedef struct {
    UINT16 origNode;
    UINT16 destNode;
    UINT16 year;
    UINT16 month;
    UINT16 day;
    UINT16 hour;
    UINT16 minute;
    UINT16 second;
    UINT16 baud;
    UINT16 pktType;      /* always 2 */
    UINT16 origNet;
    UINT16 destNet;
    UINT16 prodCode;
    UINT16 serialNo;
    UINT16 origZone;
    UINT16 destZone;
    UINT16 auxNet;
    UINT16 cwVal;
    UINT16 origPoint;
    UINT16 destPoint;
    char   password[8];  /* 8‑byte ASCII password */
} FTN_PKT_HDR;

/* Open a new FTN packet file and write header */
FILE *ftn_pkt_open(const char *filename,
                   FTN_ADDR *orig,
                   FTN_ADDR *dest,
                   const char *password,
                   UINT16 prodCode,
                   UINT16 serialNo);

/* Write a single FTN message block into the packet */
int ftn_pkt_write_msg(FILE *fp,
                      FTN_ADDR *from,
                      FTN_ADDR *to,
                      const char *fromName,
                      const char *toName,
                      const char *subject,
                      const char *body,
                      const char *originLine);

/* Close FTN packet (writes final two NULs) */
void ftn_pkt_close(FILE *fp);

#endif /* FTN_PACKET_H */
