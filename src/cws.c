/*
 * Copyright (c) 2010 Putilov Andrey
 * Copyright (c) 2012 Felipe Cruz
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include <openssl/sha.h>

#include "cws.h"
#include "b64.h"

#define _HASHVALUE "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"

static char rn[] = "\r\n";

void
    nullhandshake(struct handshake *hs)
{
    hs->host = NULL;
    hs->key = NULL;
    hs->origin = NULL;
    hs->protocol = NULL;
    hs->resource = NULL;
}

uint8_t*
    get_upto_linefeed(const char *start_from)
{
    uint8_t *write_to;
    uint8_t new_length = strstr(start_from, rn) - start_from;

    assert(new_length);

    write_to = (uint8_t*) malloc(new_length + 1); //+1 for '\x00'

    assert(write_to);

    memcpy(write_to, start_from, new_length);
    write_to[new_length] = 0;

    return write_to;
}

enum ws_frame_type
    ws_parse_handshake(const uint8_t *input_frame,
                       size_t input_len,
                       struct handshake *hs)
{
    const char *input_ptr = (const char *)input_frame;
    const char *end_ptr = (const char *)input_frame + input_len;

    // measure resource size
    char *first = strchr((const char *)input_frame, ' ');
    if (!first)
        return WS_ERROR_FRAME;
    first++;
    char *second = strchr(first, ' ');
    if (!second)
        return WS_ERROR_FRAME;

    char *third = strchr((const uint8_t*)second, '\r');
    if (!third)
        return WS_ERROR_FRAME;

    hs->resource = (char *)malloc(second - first + 1); // +1 is for \x00 symbol
    assert(hs->resource);

    if (sscanf(input_ptr, "GET %s HTTP/1.1\r\n", hs->resource) != 1)
        return WS_ERROR_FRAME;
    input_ptr = strstr(input_ptr, rn) + 2;

    /*
        parse next lines
     */
    uint8_t connection_flag = 0;
    uint8_t upgrade_flag = 0;
    while (input_ptr < end_ptr && input_ptr[0] != '\r' && input_ptr[1] != '\n') {
        if (memcmp(input_ptr, host, strlen(host)) == 0) {
            input_ptr += strlen(host);
            hs->host = get_upto_linefeed(input_ptr);
        } else
            if (memcmp(input_ptr, origin, strlen(origin)) == 0) {
            input_ptr += strlen(origin);
            hs->origin = get_upto_linefeed(input_ptr);
        } else
            if (memcmp(input_ptr, protocol, strlen(protocol)) == 0) {
            input_ptr += strlen(protocol);
            hs->protocol = get_upto_linefeed(input_ptr);
        } else
            if (memcmp(input_ptr, key, strlen(key)) == 0) {
            input_ptr += strlen(key);
            hs->key = get_upto_linefeed(input_ptr);
        } else
            if (memcmp(input_ptr, connection, strlen(connection)) == 0 &&
                (strncasecmp(&input_ptr[strlen(connection)], upgrade_str, 7) == 0 ||
                strcasestr(&input_ptr[strlen(connection)], upgrade_str) != NULL)) {
            connection_flag = 1;
        } else
            if (memcmp(input_ptr, upgrade, strlen(upgrade)) == 0 &&
                strncasecmp(&input_ptr[strlen(upgrade)], websocket_str, 9) == 0) {
            upgrade_flag = 1;
        }

        input_ptr = strstr(input_ptr, rn);
        if (input_ptr)
            input_ptr += 2;
        else
            break;
    }

    // we have read all data, so check them
    if (!hs->host || !hs->origin || !connection_flag || !upgrade_flag)
    {
        return WS_ERROR_FRAME;
    }

    return WS_OPENING_FRAME;
}

enum ws_frame_type
    ws_get_handshake_answer(const struct handshake *hs,
                            uint8_t *out_frame,
                            size_t *out_len)
{
    unsigned char accept_key[30];
    unsigned char digest_key[20];

    if (hs->key == NULL || out_frame == NULL) {
        *out_len = 0;
        return WS_ERROR_FRAME;
    }

    char *pre_key = malloc(strlen(hs->key) + strlen(_HASHVALUE) + 1);
    sprintf(pre_key, "%s%s", hs->key, _HASHVALUE);

    SHA1(pre_key, strlen(pre_key), digest_key);

    free(pre_key);

    debug_print("DigestKey: %s\n", digest_key);

    lws_b64_encode_string(digest_key ,
                          20,
                          accept_key,
                          30);

    debug_print("AcceptKey: %s\n", accept_key);

    unsigned int written = sprintf((char *)out_frame,
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            "Sec-WebSocket-Accept: %s\r\n\r\n", accept_key);

    *out_len = written;
    debug_print("Written %d\n", written);

    return WS_OPENING_FRAME;
}

enum ws_frame_type
    ws_parse_input_frame(uint8_t *input_frame,
                         size_t input_len)
{
    enum ws_frame_type frame_type;

    if (input_frame == NULL)
        return WS_ERROR_FRAME;

    if (input_len < 2)
        return WS_INCOMPLETE_FRAME;

    debug_print("(ws) %d frame type\n", type(input_frame));

    frame_type = type(input_frame);

    return frame_type;
}

enum ws_frame_type
    ws_make_frame(uint8_t *data,
                  size_t data_len,
                  uint8_t **out_frame,
                  size_t *out_len,
                  enum ws_frame_type frame_type,
                  int options)
{
    uint8_t* header;
    int header_len;

    assert(data);

    header = make_header(data_len, frame_type, &header_len, options);

    debug_print("(ws) %s content\n", (char*) data);

    *out_frame = realloc(header, data_len + header_len);
    memcpy((*out_frame) + header_len, data, data_len);
    *out_len = data_len + header_len;

    debug_print("(ws) %s content\n", (char*) out_frame);

    return frame_type;
}

/* Source Code from Gnu-Darwin Project
 * http://www.gnu-darwin.org/www001/src/ports/irc/iip/work/iip-1.1.0/src/base/buffer.c
 */

//write a big endian int
void w64to8(uint8_t *dstbuffer, uint64_t value, size_t length) {
	if(dstbuffer == NULL) {
		return;
	}

	for(dstbuffer += length - 1; length > 0; length--, dstbuffer--) {
		*dstbuffer = (uint8_t)value;
		value >>= 8;
	}
}
//end of gnu-darwin code

uint8_t*
    make_header(size_t data_len,
                 enum ws_frame_type frame_type,
                 int *header_len,
                 int options)
{
    uint8_t *header;
    uint8_t end_byte;

    if (data_len < 1) {
        return NULL;
    }

    if ((options & FINAL_FRAME) == 0x0) {
        end_byte = 0x0;
    } else if (options & FINAL_FRAME) {
        end_byte = 0x80;
    } else {
        return NULL;
    }

    if (data_len < 126) {
        header = malloc(sizeof(uint8_t) * 2);
        header[0] = end_byte | frame_type;
        header[1] = (uint8_t) data_len;
        *header_len = 2;
        return header;
    } else if (data_len > 126 && data_len < 65536) {
        header = malloc(sizeof(uint8_t) * 4);
        header[0] = end_byte | frame_type;
        header[1] = (uint8_t) 0x7e;
        header[2] = (uint8_t) (data_len >> 8);
        header[3] = (uint8_t) data_len & 0xff;
        *header_len = 4;
        return header;
    } else if (data_len >= 65536) {
        header = malloc(sizeof(uint8_t) * 10);
        header[0] = end_byte | frame_type;
        header[1] = (uint8_t) 0x7f;
        w64to8(&header[2], data_len, 8);
        *header_len = 10;
        return header;
    }
    return NULL;
}

int
    _end_frame(uint8_t *packet)
{
    return (packet[0] & 0x80) == 0x80;
}

enum ws_frame_type
    type(uint8_t *packet)
{
    int opcode = packet[0] & 0xf;

    if (opcode == 0x01) {
        return WS_TEXT_FRAME;
    } else if (opcode == 0x00) {
        return WS_INCOMPLETE_FRAME;
    } else if (opcode == 0x02) {
        return WS_BINARY_FRAME;
    } else if (opcode == 0x08) {
        return WS_CLOSING_FRAME;
    } else if (opcode == 0x09) {
        return WS_PING_FRAME;
    } else if (opcode == 0x0A) {
        return WS_PONG_FRAME;
    }
}

int
    _masked(uint8_t *packet)
{
    return (packet[1] >> 7) & 0x1;
}

uint64_t
    f_uint64(uint8_t *value)
{
    uint64_t length = 0;

    for (int i = 0; i < 8; i++) {
        length = (length << 8) | value[i];
    }

    return length;
}

uint16_t
    f_uint16(uint8_t *value)
{
    uint16_t length = 0;

    for (int i = 0; i < 2; i++) {
        length = (length << 8) | value[i];
    }

    return length;
}

uint64_t
    _payload_length(uint8_t *packet)
{
    int length = -1;
    uint8_t temp = 0;

    if (_masked(packet)) {
        temp = packet[1];
        length = (temp &= ~(1 << 7));
    } else {
        length = packet[1];
    }

    if (length < 125) {
        return length;
    } else if (length == 126) {
        return f_uint16(&packet[2]);
    } else if (length == 127) {
        return f_uint64(&packet[2]);
    } else {
        return length;
    }
}

uint8_t*
    _extract_mask_len1(uint8_t *packet)
{
    uint8_t *mask;
    int j = 0;

    mask = malloc(sizeof(uint8_t) * 4);

    for(int i = 2; i < 6; i++) {
        mask[j] = packet[i];
        j++;
    }

    return mask;
}

uint8_t*
    _extract_mask_len2(uint8_t *packet)
{
    uint8_t *mask;
    int j = 0;

    mask = malloc(sizeof(uint8_t) * 4);

    for(int i = 4; i < 8; i++) {
        mask[j] = packet[i];
        j++;
    }

    return mask;
}

uint8_t*
    _extract_mask_len3(uint8_t *packet)
{
    uint8_t *mask;
    int j = 0;

    mask = malloc(sizeof(uint8_t) * 4);

    for(int i = 10; i < 14; i++) {
        mask[j] = packet[i];
        j++;
    }

    return mask;
}

uint8_t*
    unmask(uint8_t *packet, uint64_t length, uint8_t *mask)
{
    for (int i = 0; i < length; i++) {
        packet[i] ^= mask[i % 4];
    }

    free(mask);
    return packet;
}

uint8_t*
    extract_payload(uint8_t *packet, uint64_t *length)
{
    uint8_t *mask;
    int m = _masked(packet);
    *length = _payload_length(packet);

    if (m == 1) {
        if (*length < 126) {
            mask = _extract_mask_len1(packet);
            return unmask(&packet[6], *length, mask);
        } else if (*length > 126 && *length < 65536) {
            mask = _extract_mask_len2(packet);
            return unmask(&packet[8], *length, mask);
        } else if (*length >= 65536) {
            mask = _extract_mask_len3(packet);
            return unmask(&packet[14], *length, mask);
        }
    } else {
        if (*length < 126) {
            return &packet[2];
        } else if (*length > 126 && *length < 65536) {
            return &packet[4];
        } else if (*length >= 65536) {
            return &packet[10];
        }
    }
    return NULL;
}
