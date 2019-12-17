#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>
#include "typedefs.h"
#include "httpclient.h"

#ifndef TRUE
#define FALSE	0
#define TRUE	( !FALSE )
#endif /* TRUE */

char *registerServer = "http://auth.dui.ai";
char *productSecret  = "47b31295e4f6eb44828f8c8b3759cb73";
char *productKey     = "1044fd38345a86edcd55bf68b8c306d7";
char *format         = "plain";
char *productId      = "278579737";
char *header         = "Content-Type: application/json\n";
char *post_data      = "{\"deviceName\": \"aispeech_test_5\",\"platform\": \"linux\"}";


#define BUF_SIZE        (2048 * 1)


/* The SHS block size and message digest sizes, in bytes */
typedef struct {
  uint32_t state[5];
  uint32_t count[2];
  unsigned char buffer[64];
} cs_sha1_ctx;

void cs_sha1_init(cs_sha1_ctx *);
void cs_sha1_update(cs_sha1_ctx *, const unsigned char *data, uint32_t len);
void cs_sha1_final(unsigned char digest[20], cs_sha1_ctx *);
void cs_hmac_sha1(const unsigned char *key, size_t key_len,const unsigned char *text, size_t text_len,unsigned char out[20]);


  
union char64long16 {
	unsigned char c[64];
	uint32_t l[16];
};
  
#define rol(value, bits) (((value) << (bits)) | ((value) >> (32 - (bits))))
  
  static uint32_t blk0(union char64long16 *block, int i) {
  /* Forrest: SHA expect BIG_ENDIAN, swap if LITTLE_ENDIAN */
#if BYTE_ORDER == LITTLE_ENDIAN
	block->l[i] =
		(rol(block->l[i], 24) & 0xFF00FF00) | (rol(block->l[i], 8) & 0x00FF00FF);
#endif
	return block->l[i];
  }
  
  /* Avoid redefine warning (ARM /usr/include/sys/ucontext.h define R0~R4) */
#undef blk
#undef R0
#undef R1
#undef R2
#undef R3
#undef R4

#define blk(i)                                                               \
  (block->l[i & 15] = rol(block->l[(i + 13) & 15] ^ block->l[(i + 8) & 15] ^ \
                              block->l[(i + 2) & 15] ^ block->l[i & 15],     \
                          1))
#define R0(v, w, x, y, z, i)                                          \
  z += ((w & (x ^ y)) ^ y) + blk0(block, i) + 0x5A827999 + rol(v, 5); \
  w = rol(w, 30);
#define R1(v, w, x, y, z, i)                                  \
  z += ((w & (x ^ y)) ^ y) + blk(i) + 0x5A827999 + rol(v, 5); \
  w = rol(w, 30);
#define R2(v, w, x, y, z, i)                          \
  z += (w ^ x ^ y) + blk(i) + 0x6ED9EBA1 + rol(v, 5); \
  w = rol(w, 30);
#define R3(v, w, x, y, z, i)                                        \
  z += (((w | x) & y) | (w & x)) + blk(i) + 0x8F1BBCDC + rol(v, 5); \
  w = rol(w, 30);
#define R4(v, w, x, y, z, i)                          \
  z += (w ^ x ^ y) + blk(i) + 0xCA62C1D6 + rol(v, 5); \
  w = rol(w, 30);

void cs_sha1_transform(uint32_t state[5], const unsigned char buffer[64]) {
  uint32_t a, b, c, d, e;
  union char64long16 block[1];

  memcpy(block, buffer, 64);
  a = state[0];
  b = state[1];
  c = state[2];
  d = state[3];
  e = state[4];
  R0(a, b, c, d, e, 0);
  R0(e, a, b, c, d, 1);
  R0(d, e, a, b, c, 2);
  R0(c, d, e, a, b, 3);
  R0(b, c, d, e, a, 4);
  R0(a, b, c, d, e, 5);
  R0(e, a, b, c, d, 6);
  R0(d, e, a, b, c, 7);
  R0(c, d, e, a, b, 8);
  R0(b, c, d, e, a, 9);
  R0(a, b, c, d, e, 10);
  R0(e, a, b, c, d, 11);
  R0(d, e, a, b, c, 12);
  R0(c, d, e, a, b, 13);
  R0(b, c, d, e, a, 14);
  R0(a, b, c, d, e, 15);
  R1(e, a, b, c, d, 16);
  R1(d, e, a, b, c, 17);
  R1(c, d, e, a, b, 18);
  R1(b, c, d, e, a, 19);
  R2(a, b, c, d, e, 20);
  R2(e, a, b, c, d, 21);
  R2(d, e, a, b, c, 22);
  R2(c, d, e, a, b, 23);
  R2(b, c, d, e, a, 24);
  R2(a, b, c, d, e, 25);
  R2(e, a, b, c, d, 26);
  R2(d, e, a, b, c, 27);
  R2(c, d, e, a, b, 28);
  R2(b, c, d, e, a, 29);
  R2(a, b, c, d, e, 30);
  R2(e, a, b, c, d, 31);
  R2(d, e, a, b, c, 32);
  R2(c, d, e, a, b, 33);
  R2(b, c, d, e, a, 34);
  R2(a, b, c, d, e, 35);
  R2(e, a, b, c, d, 36);
  R2(d, e, a, b, c, 37);
  R2(c, d, e, a, b, 38);
  R2(b, c, d, e, a, 39);
  R3(a, b, c, d, e, 40);
  R3(e, a, b, c, d, 41);
  R3(d, e, a, b, c, 42);
  R3(c, d, e, a, b, 43);
  R3(b, c, d, e, a, 44);
  R3(a, b, c, d, e, 45);
  R3(e, a, b, c, d, 46);
  R3(d, e, a, b, c, 47);
  R3(c, d, e, a, b, 48);
  R3(b, c, d, e, a, 49);
  R3(a, b, c, d, e, 50);
  R3(e, a, b, c, d, 51);
  R3(d, e, a, b, c, 52);
  R3(c, d, e, a, b, 53);
  R3(b, c, d, e, a, 54);
  R3(a, b, c, d, e, 55);
  R3(e, a, b, c, d, 56);
  R3(d, e, a, b, c, 57);
  R3(c, d, e, a, b, 58);
  R3(b, c, d, e, a, 59);
  R4(a, b, c, d, e, 60);
  R4(e, a, b, c, d, 61);
  R4(d, e, a, b, c, 62);
  R4(c, d, e, a, b, 63);
  R4(b, c, d, e, a, 64);
  R4(a, b, c, d, e, 65);
  R4(e, a, b, c, d, 66);
  R4(d, e, a, b, c, 67);
  R4(c, d, e, a, b, 68);
  R4(b, c, d, e, a, 69);
  R4(a, b, c, d, e, 70);
  R4(e, a, b, c, d, 71);
  R4(d, e, a, b, c, 72);
  R4(c, d, e, a, b, 73);
  R4(b, c, d, e, a, 74);
  R4(a, b, c, d, e, 75);
  R4(e, a, b, c, d, 76);
  R4(d, e, a, b, c, 77);
  R4(c, d, e, a, b, 78);
  R4(b, c, d, e, a, 79);
  state[0] += a;
  state[1] += b;
  state[2] += c;
  state[3] += d;
  state[4] += e;
  /* Erase working structures. The order of operations is important,
   * used to ensure that compiler doesn't optimize those out. */
  memset(block, 0, sizeof(block));
  a = b = c = d = e = 0;
  (void) a;
  (void) b;
  (void) c;
  (void) d;
  (void) e;
}

void cs_sha1_init(cs_sha1_ctx *context) {
  context->state[0] = 0x67452301;
  context->state[1] = 0xEFCDAB89;
  context->state[2] = 0x98BADCFE;
  context->state[3] = 0x10325476;
  context->state[4] = 0xC3D2E1F0;
  context->count[0] = context->count[1] = 0;
}
void cs_sha1_update(cs_sha1_ctx *context, const unsigned char *data,
                    uint32_t len) {
  uint32_t i, j;

  j = context->count[0];
  if ((context->count[0] += len << 3) < j) context->count[1]++;
  context->count[1] += (len >> 29);
  j = (j >> 3) & 63;
  if ((j + len) > 63) {
    memcpy(&context->buffer[j], data, (i = 64 - j));
    cs_sha1_transform(context->state, context->buffer);
    for (; i + 63 < len; i += 64) {
      cs_sha1_transform(context->state, &data[i]);
    }
    j = 0;
  } else
    i = 0;
  memcpy(&context->buffer[j], &data[i], len - i);
}
void cs_sha1_final(unsigned char digest[20], cs_sha1_ctx *context) {
  unsigned i;
  unsigned char finalcount[8], c;

  for (i = 0; i < 8; i++) {
    finalcount[i] = (unsigned char) ((context->count[(i >= 4 ? 0 : 1)] >>
                                      ((3 - (i & 3)) * 8)) &
                                     255);
  }
  c = 0200;
  cs_sha1_update(context, &c, 1);
  while ((context->count[0] & 504) != 448) {
    c = 0000;
    cs_sha1_update(context, &c, 1);
  }
  cs_sha1_update(context, finalcount, 8);
  for (i = 0; i < 20; i++) {
    digest[i] =
        (unsigned char) ((context->state[i >> 2] >> ((3 - (i & 3)) * 8)) & 255);
  }
  memset(context, '\0', sizeof(*context));
  memset(&finalcount, '\0', sizeof(finalcount));
}

void cs_hmac_sha1(const unsigned char *key, size_t keylen,const unsigned char *data, size_t datalen,unsigned char out[20]) 
{
  cs_sha1_ctx ctx;
  unsigned char buf1[64], buf2[64], tmp_key[20], i;

  if (keylen > sizeof(buf1)) {
    cs_sha1_init(&ctx);
    cs_sha1_update(&ctx, key, keylen);
    cs_sha1_final(tmp_key, &ctx);
    key = tmp_key;
    keylen = sizeof(tmp_key);
  }

  memset(buf1, 0, sizeof(buf1));
  memset(buf2, 0, sizeof(buf2));
  memcpy(buf1, key, keylen);
  memcpy(buf2, key, keylen);

  for (i = 0; i < sizeof(buf1); i++) {
    buf1[i] ^= 0x36;
    buf2[i] ^= 0x5c;
  }

  cs_sha1_init(&ctx);
  cs_sha1_update(&ctx, buf1, sizeof(buf1));
  cs_sha1_update(&ctx, data, datalen);
  cs_sha1_final(out, &ctx);

  cs_sha1_init(&ctx);
  cs_sha1_update(&ctx, buf2, sizeof(buf2));
  cs_sha1_update(&ctx, out, 20);
  cs_sha1_final(out, &ctx);
}

//void cs_to_hex(char *to, const unsigned char *p, size_t len) WEAK;
void cs_to_hex(char *to, const unsigned char *p, size_t len) 
{
  static const char *hex = "0123456789abcdef";

  for (; len--; p++) {
    *to++ = hex[p[0] >> 4];
    *to++ = hex[p[0] & 0x0f];
  }
  *to = '\0';
}


#define MAX_LEN 1024
static double _clock_gettime() 
{
	struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return  ts.tv_sec + ts.tv_nsec / 1.0E9;
}

static char *get_register_url(void) 
{
    int n = strlen(registerServer) + strlen(productId) + strlen(productKey) + 256;
    char *url = (char *)malloc(n);
    if(url) {
    	char sig[41] = {0};
    	unsigned char hmac_sha1_out[20];
    	srand(time(NULL));
    	long int nonce = (long int)rand();
    	double timestamp = _clock_gettime() * 1000;

    	n = sprintf(url, "%s%s%ld%s%lf", productKey, format, nonce, productId, timestamp);
        url[n] = '\0';
    	//printf("productSecret:%s  -len:%d\nurl:%s  -len:%d\n",productSecret, (size_t)strlen(productSecret), url, (size_t)strlen(url));
    	cs_hmac_sha1((const unsigned char *)productSecret,
    		(size_t)strlen(productSecret),
    		(const unsigned char *)url,
    		(size_t)strlen(url),
    		hmac_sha1_out);
    	cs_to_hex(sig, hmac_sha1_out, 20);

    	n = sprintf(url, "%s/auth/device/register?productKey=%s&format=%s&productId=%s&timestamp=%lf&nonce=%ld&sig=%s",
    		        registerServer, productKey, format, productId, timestamp, nonce, sig);
        url[n] = '\0';
    	//printf("register url:%s\n", url);
    }
	return url;
}

void do_register(void) 
{
    char *url = get_register_url();

    httpclient_t client = {0};
    httpclient_data_t client_data = {0};
    char *buf = NULL;
    buf = pvPortMalloc(BUF_SIZE);
    if (buf == NULL) {
       printf("Malloc failed.\r\n");
       return;
    }
    memset(buf, 0, sizeof(buf));
    client_data.response_buf = buf;                 //Sets a buffer to store the result.
    client_data.response_buf_len = BUF_SIZE;        //Sets the buffer size.
    httpclient_set_custom_header(&client, header);  //Sets the custom header if needed.
    client_data.post_buf = post_data;               //Sets the user data to be posted.
    client_data.post_buf_len = strlen(post_data);   //Sets the post data length.
    httpclient_post(&client, url, &client_data);
    printf("Data received: %s\r\n", client_data.response_buf);
}

#if 0
int main(int argc, char* argv[])
{
    char *url = NULL;
    char *post_body = "{\"deviceName\": \"aispeech_test_1\",\"platform\": \"linux\"}";
    printf("post_body:%s\n", post_body);
    url = get_register_url();

    printf("URL:%s\n", url);
	return 0;
}
#endif
