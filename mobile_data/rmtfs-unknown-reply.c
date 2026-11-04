/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Answer QMI requests that the stock rmtfs does not implement.
 *
 * rmtfs only prints "[RMTFS] Unknown request: N" and sends nothing back, so a
 * modem thread waiting for the response can stall the modem boot (seen on sky:
 * request 7 right after ALLOC_BUFF, after which no NAS/DMS/UIM service ever
 * appears). rmtfs itself stays untouched; this file is linked into
 * aera-mobile-rmtfs and hooks two calls through the linker's --wrap option,
 * the same mechanism readonly-guard.c already uses.
 */
#include <libqrtr.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Reply mode comes from the runtime config (key "rmtfs_reply") in
 * /tmp/aera-dbg.conf, /sdcard/aera-dbg.conf or /data/media/0/aera-dbg.conf:
 *   off (default)  log the request only, send nothing (stock behaviour)
 *   ok             reply success (result=0 error=0)
 *   err            reply failure (result=1 error=0x47, invalid cmd)
 * Change it with: aera-dbg set rmtfs_reply ok
 */
#include <string.h>

/* Request ids stock rmtfs handles: OPEN, CLOSE, RW_IOVEC, ALLOC_BUFF, GET_DEV_ERROR */
#define LAST_KNOWN_ID 5

static int g_rmtfs_sock = -1;

/* returns 0=off, 1=ok, 2=err */
static int reply_mode(void)
{
    static const char *const files[] = {
        "/tmp/aera-dbg.conf", "/sdcard/aera-dbg.conf", "/data/media/0/aera-dbg.conf"
    };
    char line[128];
    unsigned i;

    for (i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        FILE *f = fopen(files[i], "r");
        int mode = -1;

        if (!f)
            continue;
        while (fgets(line, sizeof(line), f)) {
            const char *v;
            if (strncmp(line, "rmtfs_reply", 11))
                continue;
            v = line + 11;
            while (*v == ' ' || *v == '\t')
                v++;
            if (*v != '=')
                continue;
            v++;
            while (*v == ' ' || *v == '\t')
                v++;
            mode = (v[0] == 'o' && v[1] == 'k') ? 1 : (v[0] == 'e') ? 2 : 0;
        }
        fclose(f);
        if (mode >= 0)
            return mode;
    }
    return 0;
}

int __real_qrtr_open(int rport);
int __wrap_qrtr_open(int rport)
{
    int fd = __real_qrtr_open(rport);
    if (fd >= 0 && g_rmtfs_sock < 0)
        g_rmtfs_sock = fd; /* rmtfs opens exactly one socket: its service port */
    return fd;
}

int __real_qmi_decode_header(const struct qrtr_packet *pkt, unsigned int *msg_id);
int __wrap_qmi_decode_header(const struct qrtr_packet *pkt, unsigned int *msg_id)
{
    int ret = __real_qmi_decode_header(pkt, msg_id);
    const uint8_t *req = pkt->data;
    uint8_t buf[14];
    size_t i, n;
    int mode;

    if (ret != 0 || (*msg_id >= 1 && *msg_id <= LAST_KNOWN_ID))
        return ret;

    /* log the raw request so the message can be identified */
    n = pkt->data_len < 64 ? pkt->data_len : 64;
    fprintf(stderr, "[RMTFS] unhandled request id=%u from %u:%u len=%zu:",
            *msg_id, pkt->node, pkt->port, (size_t)pkt->data_len);
    for (i = 0; i < n; i++)
        fprintf(stderr, " %02x", req[i]);
    fputc('\n', stderr);

    /* respond only to requests (flags bits 1/2 clear), never to indications */
    mode = reply_mode();
    fprintf(stderr, "[RMTFS] reply mode=%s\n", mode == 1 ? "ok" : mode == 2 ? "err" : "off");
    if (mode == 0 || g_rmtfs_sock < 0 || pkt->data_len < 7 || (req[0] & 0x06))
        return ret;

    buf[0] = 0x02;                          /* QMI response */
    buf[1] = req[1]; buf[2] = req[2];       /* transaction id */
    buf[3] = *msg_id & 0xff; buf[4] = (*msg_id >> 8) & 0xff;
    buf[5] = 7;      buf[6] = 0;            /* message length */
    buf[7] = 0x02;                          /* TLV: result */
    buf[8] = 4;      buf[9] = 0;
    buf[10] = (mode == 2); buf[11] = 0;
    buf[12] = (mode == 2) ? 0x47 : 0; buf[13] = 0;

    if (qrtr_sendto(g_rmtfs_sock, pkt->node, pkt->port, buf, sizeof(buf)) < 0)
        fprintf(stderr, "[RMTFS] failed to send reply for request %u\n", *msg_id);
    else
        fprintf(stderr, "[RMTFS] replied to request %u (mode=%s)\n",
                *msg_id, mode == 1 ? "ok" : "err");
    return ret;
}
