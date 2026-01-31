/* ftn_packet.c — Full FTN Type-2+ packet writer for AmiBinkd v9.10
 *
 * Generates standards-compliant FTN packets suitable for Mystic, BinkD,
 * Husky, Crashmail, Squish, etc.
 *
 * Branding:
 *   Tearline:  AmiBinkd v9.10 / ROF Continuation
 *   Origin:    Provided by caller (e.g. "Reign Of Fire BBS (80:774/100)")
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ftnaddr.h"
#include "tools.h"
#include "common.h"

typedef unsigned short UINT16;

/* FTN Type-2+ packet header (simplified but compatible) */
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
    UINT16 pktType;      /* always 2 for Type-2/2+ */
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
    char   password[8];  /* 8-byte ASCII password, zero-padded */
} FTN_PKT_HDR;

/* Open a new FTN packet file and write header */
FILE *ftn_pkt_open(const char *filename,
                   FTN_ADDR *orig,
                   FTN_ADDR *dest,
                   const char *password,
                   UINT16 prodCode,
                   UINT16 serialNo)
{
    FILE *fp;
    FTN_PKT_HDR hdr;
    time_t now;
    struct tm *tmv;
    size_t pwlen;

    fp = fopen(filename, "wb");
    if (!fp) {
        Log(1, "Unable to create pkt file %s", filename);
        return NULL;
    }

    memset(&hdr, 0, sizeof(hdr));

    now = time(NULL);
    tmv = localtime(&now);

    hdr.origNode = (UINT16)orig->node;
    hdr.destNode = (UINT16)dest->node;
    hdr.year     = (UINT16)((tmv ? tmv->tm_year + 1900 : 2000));
    hdr.month    = (UINT16)((tmv ? tmv->tm_mon + 1 : 1));
    hdr.day      = (UINT16)((tmv ? tmv->tm_mday : 1));
    hdr.hour     = (UINT16)((tmv ? tmv->tm_hour : 0));
    hdr.minute   = (UINT16)((tmv ? tmv->tm_min : 0));
    hdr.second   = (UINT16)((tmv ? tmv->tm_sec : 0));
    hdr.baud     = 300;          /* historical, ignored by most tossers */
    hdr.pktType  = 2;            /* Type-2/2+ */

    hdr.origNet  = (UINT16)orig->net;
    hdr.destNet  = (UINT16)dest->net;
    hdr.prodCode = prodCode;
    hdr.serialNo = serialNo;

    hdr.origZone = (UINT16)orig->z;
    hdr.destZone = (UINT16)dest->z;
    hdr.auxNet   = (UINT16)orig->net;
    hdr.cwVal    = 0;            /* capability word, 0 = basic */
    hdr.origPoint = (UINT16)orig->p;
    hdr.destPoint = (UINT16)dest->p;

    memset(hdr.password, 0, sizeof(hdr.password));
    if (password) {
        pwlen = strlen(password);
        if (pwlen > sizeof(hdr.password))
            pwlen = sizeof(hdr.password);
        memcpy(hdr.password, password, pwlen);
    }

    if (fwrite(&hdr, sizeof(hdr), 1, fp) != 1) {
        Log(1, "Error writing pkt header to %s", filename);
        fclose(fp);
        return NULL;
    }

    return fp;
}

/* Write a single FTN message into the packet */
int ftn_pkt_write_msg(FILE *fp,
                      FTN_ADDR *from,
                      FTN_ADDR *to,
                      const char *fromName,
                      const char *toName,
                      const char *subject,
                      const char *body,
                      const char *originLine)
{
    char addrFrom[FTN_ADDR_SZ + 1];
    time_t now;
    unsigned long msgid;
    const char *chrs = "CP437 2";

    if (!fp || !from || !to || !fromName || !toName || !subject || !body)
        return -1;

    ftnaddress_to_str(addrFrom, from);

    now   = time(NULL);
    msgid = (unsigned long)now;

    /* MSGID */
    fprintf(fp, "\001MSGID: %s %08lx\r", addrFrom, msgid);

    /* INTL */
    fprintf(fp, "\001INTL %d:%d/%d %d:%d/%d\r",
            to->z, to->net, to->node,
            from->z, from->net, from->node);

    /* FMPT/TOPT */
    if (from->p)
        fprintf(fp, "\001FMPT %d\r", from->p);
    if (to->p)
        fprintf(fp, "\001TOPT %d\r", to->p);

    /* CHRS */
    fprintf(fp, "\001CHRS: %s\r", chrs);

    /* Standard message header lines */
    fprintf(fp, "From: %s\r", fromName);
    fprintf(fp, "To: %s\r", toName);
    fprintf(fp, "Subject: %s\r", subject);
    fprintf(fp, "Date: %s\r", ctime(&now)); /* optional, many tossers ignore */

    /* Blank line, then body */
    fprintf(fp, "\r%s\r", body);

    /* Tearline + Origin */
    fprintf(fp, "--- AmiBinkd v9.10 / ROF Continuation\r");
    if (originLine && *originLine)
        fprintf(fp, " * Origin: %s\r", originLine);

    /* Minimal SEEN-BY/PATH (can be expanded later) */
    fprintf(fp, "SEEN-BY: %d/%d\r", from->net, from->node);
    fprintf(fp, "PATH: %d/%d\r", from->net, from->node);

    /* End of message marker: two NULs */
    fputc(0, fp);
    fputc(0, fp);

    return 0;
}

/* Close FTN packet (final two NULs + fclose) */
void ftn_pkt_close(FILE *fp)
{
    if (!fp)
        return;

    /* End of packet marker: two NULs */
    fputc(0, fp);
    fputc(0, fp);

    fclose(fp);
}
