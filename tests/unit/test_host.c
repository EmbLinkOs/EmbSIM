/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright 2026 Junior Deogracias */
/* test_host.c -- the host layer (core/host) and the pure WebSocket helpers
 * (core/protocol/wsutil): paths, file round trips, the monotonic clock,
 * SHA-1 and base64 vectors, frame headers and a loopback socket echo. */
#include "harness.h"
#include "host/host.h"
#include "protocol/wsutil.h"

static esk_alloc A;

#ifdef _WIN32
#define SEP "\\"
#else
#define SEP "/"
#endif

static void check_join(const char *base, const char *rel, const char *expect)
{
    char *p = esk_host_path_join(&A, base, rel);
    CHECK_STR(p, expect);
    esk_strfree(&A, p);
}

static void test_path_join(void)
{
    check_join("a", "b", "a" SEP "b");
    check_join("a/b", "../c", "a" SEP "c");
    check_join("a/b/", "./c//d/", "a" SEP "b" SEP "c" SEP "d");
    check_join("a", "..", ".");
    check_join("", "x", "x");
    check_join("a", "", "a");
    check_join("../x", "../../y", ".." SEP ".." SEP "y");
    check_join("/usr/share", "embsim/../lib", SEP "usr" SEP "share" SEP "lib");
    check_join("/", "..", SEP);
    check_join("/a", "../../..", SEP);
    check_join("base", "/abs/path", SEP "abs" SEP "path"); /* an absolute rel replaces base */
#ifdef _WIN32
    check_join("C:\\work", "proj/../x", "C:\\work\\x");
    check_join("C:\\work", "D:/other", "D:\\other");
    check_join("C:\\work", "\\\\server\\share\\f", "\\\\server\\share\\f");
    check_join("C:\\", "..", "C:\\");
#endif
    {
        char *d = esk_host_dirname(&A, "a/b/c.txt");
        CHECK_STR(d, "a/b");
        esk_strfree(&A, d);
        d = esk_host_dirname(&A, "c.txt");
        CHECK_STR(d, ".");
        esk_strfree(&A, d);
        d = esk_host_dirname(&A, "/c.txt");
        CHECK_STR(d, "/");
        esk_strfree(&A, d);
        d = esk_host_dirname(&A, "a/b/");
        CHECK_STR(d, "a");
        esk_strfree(&A, d);
    }
}

static void test_file_roundtrip(void)
{
    char *tmp = esk_host_temp_dir(&A);
    char name[64], *path, *data = NULL;
    size_t len = 0;
    unsigned char payload[300];
    size_t i;
    esk_json *names = NULL;
    CHECK(tmp != NULL);
    if (!tmp)
        return;
    CHECK(esk_host_is_dir(tmp));
    esk_snprintf(name, sizeof name, "embsim_test_host_%lu.bin", (unsigned long)(esk_host_monotonic_ms() & 0xFFFFFFu));
    path = esk_host_path_join(&A, tmp, name);
    CHECK(path != NULL);
    for (i = 0; i < sizeof payload; i++)
        payload[i] = (unsigned char)(i * 7u); /* includes zero bytes and high bytes */
    CHECK_EQ(esk_host_write_file(path, payload, sizeof payload), ES_OK);
    CHECK(esk_host_file_exists(path));
    CHECK(!esk_host_is_dir(path));
    CHECK_EQ(esk_host_read_file(&A, path, &data, &len), ES_OK);
    CHECK_EQ(len, sizeof payload);
    if (data) {
        CHECK(memcmp(data, payload, sizeof payload) == 0);
        CHECK_EQ(data[len], 0); /* the extra NUL */
        esk_free(&A, data, len + 1);
    }
    /* the directory listing names the file */
    CHECK_EQ(esk_host_list_dir(&A, tmp, &names), ES_OK);
    if (names) {
        int found = 0;
        for (i = 0; i < esk_json_len(names); i++)
            if (esk_str_eq(esk_json_str(esk_json_at(names, i)), name))
                found = 1;
        CHECK(found);
        esk_json_free(&A, names);
    }
    /* an empty file reads as len 0 with a NUL */
    CHECK_EQ(esk_host_write_file(path, "", 0), ES_OK);
    CHECK_EQ(esk_host_read_file(&A, path, &data, &len), ES_OK);
    CHECK_EQ(len, 0);
    if (data) {
        CHECK_EQ(data[0], 0);
        esk_free(&A, data, 1);
    }
    remove(path);
    CHECK(!esk_host_file_exists(path));
    CHECK_EQ(esk_host_read_file(&A, path, &data, &len), ES_ENOTFOUND);
    CHECK_EQ(esk_host_list_dir(&A, path, &names), ES_ENOTFOUND);
    esk_strfree(&A, path);
    esk_strfree(&A, tmp);
}

static void test_clock(void)
{
    uint64_t t0 = esk_host_monotonic_ms(), t1, t2;
    int i;
    for (i = 0; i < 1000; i++) {
        t1 = esk_host_monotonic_ms();
        CHECK(t1 >= t0);
        t0 = t1;
    }
    esk_host_sleep_ms(15);
    t2 = esk_host_monotonic_ms();
    CHECK(t2 >= t0 + 10); /* a sleep moves the clock (coarse clocks allowed) */
}

static void hex(const unsigned char *d, size_t n, char *out)
{
    static const char H[] = "0123456789abcdef";
    size_t i;
    for (i = 0; i < n; i++) {
        out[2 * i] = H[d[i] >> 4];
        out[2 * i + 1] = H[d[i] & 15];
    }
    out[2 * n] = 0;
}

static void check_sha1(const char *msg, const char *expect)
{
    unsigned char d[ESK_SHA1_DIGEST_LEN];
    char h[41];
    esk_sha1((const unsigned char *)msg, strlen(msg), d);
    hex(d, sizeof d, h);
    CHECK_STR(h, expect);
}

static void test_sha1(void)
{
    char buf[1000];
    unsigned char d[ESK_SHA1_DIGEST_LEN];
    char h[41];
    check_sha1("", "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check_sha1("abc", "a9993e364706816aba3e25717850c26c9cd0d89d");
    check_sha1("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    /* exactly 55, 56 and 64 bytes exercise the padding boundaries */
    memset(buf, 'a', sizeof buf);
    esk_sha1((const unsigned char *)buf, 55, d); hex(d, sizeof d, h);
    CHECK_STR(h, "c1c8bbdc22796e28c0e15163d20899b65621d65a");
    esk_sha1((const unsigned char *)buf, 56, d); hex(d, sizeof d, h);
    CHECK_STR(h, "c2db330f6083854c99d4b5bfb6e8f29f201be699");
    esk_sha1((const unsigned char *)buf, 64, d); hex(d, sizeof d, h);
    CHECK_STR(h, "0098ba824b5c16427bd7a1122a5a442a25ec644d");
    esk_sha1((const unsigned char *)buf, 1000, d); hex(d, sizeof d, h);
    CHECK_STR(h, "291e9a6c66994949b57ba5e650361e98fc36b1ba");
}

static void check_b64(const char *msg, const char *expect)
{
    char out[64];
    unsigned char back[64];
    size_t n = esk_base64_encode((const unsigned char *)msg, strlen(msg), out, sizeof out);
    CHECK_EQ(n, strlen(expect));
    CHECK_STR(out, expect);
    n = esk_base64_decode(expect, strlen(expect), back, sizeof back);
    CHECK_EQ(n, strlen(msg));
    CHECK(n != (size_t)-1 && memcmp(back, msg, n) == 0);
}

static void test_base64(void)
{
    char small[4];
    unsigned char back[8];
    check_b64("", "");
    check_b64("f", "Zg==");
    check_b64("fo", "Zm8=");
    check_b64("foo", "Zm9v");
    check_b64("foob", "Zm9vYg==");
    check_b64("fooba", "Zm9vYmE=");
    check_b64("foobar", "Zm9vYmFy");
    /* a too-small buffer reports the needed size and writes nothing */
    CHECK_EQ(esk_base64_encode((const unsigned char *)"foobar", 6, small, sizeof small), 8);
    CHECK_EQ(esk_base64_decode("Zm9v", 4, back, 2), (size_t)-1);
    CHECK_EQ(esk_base64_decode("Zm9$", 4, back, sizeof back), (size_t)-1);
    CHECK_EQ(esk_base64_decode("Zm9vYg", 6, back, sizeof back), 4); /* padding optional */
}

static void test_ws_accept_and_frames(void)
{
    char accept[ESK_WS_ACCEPT_LEN + 1];
    unsigned char hdr[ESK_WS_HEADER_MAX];
    esk_ws_frame f;
    /* the example of RFC 6455 §1.3 */
    CHECK_EQ(esk_ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", 24, accept), 0);
    CHECK_STR(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    CHECK(esk_ws_accept_key("", 0, accept) != 0);

    CHECK_EQ(esk_ws_encode_header(hdr, 1, ESK_WS_OP_TEXT, 5), 2);
    CHECK_EQ(hdr[0], 0x81);
    CHECK_EQ(hdr[1], 5);
    CHECK_EQ(esk_ws_encode_header(hdr, 1, ESK_WS_OP_BINARY, 300), 4);
    CHECK_EQ(hdr[1], 126);
    CHECK_EQ(hdr[2], 1);
    CHECK_EQ(hdr[3], 44);
    CHECK_EQ(esk_ws_encode_header(hdr, 0, ESK_WS_OP_TEXT, 70000), 10);
    CHECK_EQ(hdr[0], 0x01);
    CHECK_EQ(hdr[1], 127);
    CHECK_EQ(hdr[9], 70000 & 0xFF);
    CHECK_EQ(hdr[8], (70000 >> 8) & 0xFF);
    CHECK_EQ(hdr[7], (70000 >> 16) & 0xFF);

    /* a masked client frame "Hello" (RFC 6455 §5.7) */
    {
        unsigned char frame[] = {0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58};
        CHECK_EQ(esk_ws_decode_header(frame, 1, &f), 1);
        CHECK_EQ(esk_ws_decode_header(frame, 5, &f), 1);
        CHECK_EQ(esk_ws_decode_header(frame, sizeof frame, &f), 0);
        CHECK(f.fin);
        CHECK(f.masked);
        CHECK_EQ(f.opcode, ESK_WS_OP_TEXT);
        CHECK_EQ(f.header_len, 6);
        CHECK_EQ(f.payload_len, 5);
        esk_ws_unmask(&f, frame + f.header_len, 5);
        CHECK(memcmp(frame + 6, "Hello", 5) == 0);
    }
    {
        unsigned char ext[] = {0x82, 0x7E, 0x01, 0x00};
        CHECK_EQ(esk_ws_decode_header(ext, sizeof ext, &f), 0);
        CHECK_EQ(f.payload_len, 256);
        CHECK_EQ(f.header_len, 4);
        CHECK(!f.masked);
    }
    {
        unsigned char bad_rsv[] = {0xC1, 0x00};
        unsigned char bad_op[] = {0x83, 0x00};
        unsigned char frag_ctl[] = {0x09, 0x00};
        unsigned char long_ctl[] = {0x89, 0x7E, 0x00, 0x80};
        CHECK_EQ(esk_ws_decode_header(bad_rsv, 2, &f), -1);
        CHECK_EQ(esk_ws_decode_header(bad_op, 2, &f), -1);
        CHECK_EQ(esk_ws_decode_header(frag_ctl, 2, &f), -1);
        CHECK_EQ(esk_ws_decode_header(long_ctl, 4, &f), -1);
    }
}

static void test_socket_echo(void)
{
    esk_host_sock listener = ESK_HOST_SOCK_INVALID, client = ESK_HOST_SOCK_INVALID, peer = ESK_HOST_SOCK_INVALID;
    int port = 0;
    char buf[64];
    long n;
    CHECK_EQ(esk_host_sock_init(), ES_OK);
    CHECK_EQ(esk_host_sock_listen("127.0.0.1", 0, &listener), ES_OK);
    CHECK_EQ(esk_host_sock_local_port(listener, &port), ES_OK);
    CHECK(port > 0);
    CHECK_EQ(esk_host_sock_wait(listener, 10), 0); /* nobody yet */
    CHECK_EQ(esk_host_sock_connect("localhost", port, &client), ES_OK);
    CHECK_EQ(esk_host_sock_wait(listener, 1000), 1);
    CHECK_EQ(esk_host_sock_accept(listener, &peer), ES_OK);
    CHECK_EQ(esk_host_sock_recv(peer, buf, sizeof buf, 10), 0); /* timeout: nothing sent */
    CHECK_EQ(esk_host_sock_send(client, "ping", 4, 1000), 4);
    n = esk_host_sock_recv(peer, buf, sizeof buf, 1000);
    CHECK_EQ(n, 4);
    CHECK(n == 4 && memcmp(buf, "ping", 4) == 0);
    CHECK_EQ(esk_host_sock_send(peer, buf, (size_t)n, 1000), 4);
    n = esk_host_sock_recv(client, buf, sizeof buf, 1000);
    CHECK_EQ(n, 4);
    CHECK(n == 4 && memcmp(buf, "ping", 4) == 0);
    /* closing one end is reported as -1 on the other */
    esk_host_sock_close(client);
    CHECK_EQ(esk_host_sock_recv(peer, buf, sizeof buf, 1000), -1);
    esk_host_sock_close(peer);
    esk_host_sock_close(listener);
    /* nothing listens there any more */
    CHECK(esk_host_sock_connect("127.0.0.1", port, &client) != ES_OK);
    CHECK_EQ(esk_host_sock_listen("300.1.1.1", 0, &listener), ES_EINVAL);
    esk_host_sock_shutdown();
}

static void test_user_dirs(void)
{
    char *d = esk_host_user_data_dir(&A);
    /* may be NULL without a home; when present it ends with the product name */
    if (d) {
        size_t n = strlen(d);
        CHECK(n > 6);
        CHECK(strstr(d, "embsim") != NULL || strstr(d, "EmbSIM") != NULL);
        esk_strfree(&A, d);
    }
    CHECK_EQ(esk_host_open_browser(NULL), ES_EINVAL);
}

TEST_MAIN_BEGIN
esk_alloc_init(&A, NULL);
RUN(test_path_join);
RUN(test_file_roundtrip);
RUN(test_clock);
RUN(test_sha1);
RUN(test_base64);
RUN(test_ws_accept_and_frames);
RUN(test_socket_echo);
RUN(test_user_dirs);
CHECK_EQ(A.live_bytes, 0); /* no leak through the allocator */
TEST_MAIN_END
