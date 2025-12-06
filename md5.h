#ifndef PROTOTYPES
#define PROTOTYPES 1
#endif

typedef unsigned char *POINTER;

typedef unsigned short int UINT2;
typedef unsigned long int UINT4;

typedef struct {
  UINT4 state[4];        /* state (ABCD) */
  UINT4 count[2];        /* number of bits, modulo 2^64 (lsb first) */
  unsigned char buffer[64]; /* input buffer */
} MD5_CTX;

#define MD5_DIGEST_LEN 16
typedef unsigned char MDcaddr_t[MD5_DIGEST_LEN];
#define MD_CHALLENGE_LEN 16

/* Core MD5 functions */
void MD5Init(MD5_CTX *context);
void MD5Update(MD5_CTX *context, unsigned char *input, unsigned int inputLen);
void MD5Final(unsigned char digest[MD5_DIGEST_LEN], MD5_CTX *context);

/* Higher‑level helpers */
unsigned char *MD_getChallenge(char *src, STATE *st);
char *MD_buildDigest(char *pw, unsigned char *challenge);
void MD_toString(char *rs, int len, unsigned char *digest);
