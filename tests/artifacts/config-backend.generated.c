#define _DEFAULT_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdint.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/stat.h>
#define DATA_ROOT "/tmp/elf-config-fixture/"
#define SETTINGS_FILE DATA_ROOT "settings.json"
#define AUTO_LIST_PATH DATA_ROOT "auto.list"
#define PROFILES_FILE DATA_ROOT "profiles.json"
#define AUTO_MAX 64
#define AUTO_NAME_MAX 96
static int write_auto_pair_unlocked(char names[][AUTO_NAME_MAX],int count,const char *serialized,size_t len);
static pthread_mutex_t profiles_mx=PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t auto_list_mx=PTHREAD_MUTEX_INITIALIZER;
static int fail_target,code;
static char response[80000];
static int config_rename(const char *from,const char *to) {
  if((fail_target==1&&!strcmp(to,AUTO_LIST_PATH)) || (fail_target==2&&!strcmp(to,SETTINGS_FILE))){errno=EIO;return -1;}
  return rename(from,to);
}
static void evlog(const char *kind,const char *fmt,...) {(void)kind;(void)fmt;}
static void send_json_code(int c,int status,const char *body) {(void)c;code=status;snprintf(response,sizeof(response),"%s",body);}
static void fs_reply_bad(int c,int status,const char *why) {char msg[512];snprintf(msg,sizeof(msg),"{\"ok\":false,\"message\":\"%s\"}",why);send_json_code(c,status,msg);}
static char *read_body(int c,const char *req,size_t hlen,size_t got,const char *hdrs,const char *hend,size_t max,size_t *outn,const char **why) {
  (void)c;(void)hlen;(void)got;(void)hdrs;(void)hend;(void)max;(void)why;*outn=strlen(req);return strdup(req);
}
static int open_after_jb(void) {return 0;}
static int write_open_after_jb(int flag) {(void)flag;return 0;}
typedef struct {
  uint64_t bitlen;
  uint32_t state[8];
  uint8_t data[64];
  uint32_t datalen;
} sha256_ctx;

static uint32_t sha256_rotr(uint32_t x, uint32_t n) {
  return (x >> n) | (x << (32 - n));
}

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[]) {
  static const uint32_t k[64] = {
      0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
      0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
      0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
      0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
      0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
      0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
      0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
      0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
  uint32_t m[64], a, b, c, d, e, f, g, h, t1, t2, i, j;
  for (i = 0, j = 0; i < 16; ++i, j += 4)
    m[i] = ((uint32_t)data[j] << 24) | ((uint32_t)data[j + 1] << 16) |
           ((uint32_t)data[j + 2] << 8) | ((uint32_t)data[j + 3]);
  for (; i < 64; ++i) {
    uint32_t s0 = sha256_rotr(m[i - 15], 7) ^ sha256_rotr(m[i - 15], 18) ^ (m[i - 15] >> 3);
    uint32_t s1 = sha256_rotr(m[i - 2], 17) ^ sha256_rotr(m[i - 2], 19) ^ (m[i - 2] >> 10);
    m[i] = m[i - 16] + s0 + m[i - 7] + s1;
  }
  a = ctx->state[0]; b = ctx->state[1]; c = ctx->state[2]; d = ctx->state[3];
  e = ctx->state[4]; f = ctx->state[5]; g = ctx->state[6]; h = ctx->state[7];
  for (i = 0; i < 64; ++i) {
    uint32_t S1 = sha256_rotr(e, 6) ^ sha256_rotr(e, 11) ^ sha256_rotr(e, 25);
    uint32_t ch = (e & f) ^ ((~e) & g);
    t1 = h + S1 + ch + k[i] + m[i];
    uint32_t S0 = sha256_rotr(a, 2) ^ sha256_rotr(a, 13) ^ sha256_rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    t2 = S0 + maj;
    h = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
  }
  ctx->state[0] += a; ctx->state[1] += b; ctx->state[2] += c; ctx->state[3] += d;
  ctx->state[4] += e; ctx->state[5] += f; ctx->state[6] += g; ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx) {
  ctx->datalen = 0;
  ctx->bitlen = 0;
  ctx->state[0] = 0x6a09e667; ctx->state[1] = 0xbb67ae85;
  ctx->state[2] = 0x3c6ef372; ctx->state[3] = 0xa54ff53a;
  ctx->state[4] = 0x510e527f; ctx->state[5] = 0x9b05688c;
  ctx->state[6] = 0x1f83d9ab; ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t data[], size_t len) {
  size_t i;
  for (i = 0; i < len; ++i) {
    ctx->data[ctx->datalen++] = data[i];
    if (ctx->datalen == 64) {
      sha256_transform(ctx, ctx->data);
      ctx->bitlen += 512;
      ctx->datalen = 0;
    }
  }
}

static void sha256_final(sha256_ctx *ctx, uint8_t hash[]) {
  uint32_t i = ctx->datalen;
  if (ctx->datalen < 56) {
    ctx->data[i++] = 0x80;
    while (i < 56) ctx->data[i++] = 0x00;
  } else {
    ctx->data[i++] = 0x80;
    while (i < 64) ctx->data[i++] = 0x00;
    sha256_transform(ctx, ctx->data);
    memset(ctx->data, 0, 56);
  }
  ctx->bitlen += (uint64_t)ctx->datalen * 8;
  ctx->data[63] = (uint8_t)ctx->bitlen;
  ctx->data[62] = (uint8_t)(ctx->bitlen >> 8);
  ctx->data[61] = (uint8_t)(ctx->bitlen >> 16);
  ctx->data[60] = (uint8_t)(ctx->bitlen >> 24);
  ctx->data[59] = (uint8_t)(ctx->bitlen >> 32);
  ctx->data[58] = (uint8_t)(ctx->bitlen >> 40);
  ctx->data[57] = (uint8_t)(ctx->bitlen >> 48);
  ctx->data[56] = (uint8_t)(ctx->bitlen >> 56);
  sha256_transform(ctx, ctx->data);
  for (i = 0; i < 4; ++i) {
    hash[i]      = (ctx->state[0] >> (24 - i * 8)) & 0xff;
    hash[i + 4]  = (ctx->state[1] >> (24 - i * 8)) & 0xff;
    hash[i + 8]  = (ctx->state[2] >> (24 - i * 8)) & 0xff;
    hash[i + 12] = (ctx->state[3] >> (24 - i * 8)) & 0xff;
    hash[i + 16] = (ctx->state[4] >> (24 - i * 8)) & 0xff;
    hash[i + 20] = (ctx->state[5] >> (24 - i * 8)) & 0xff;
    hash[i + 24] = (ctx->state[6] >> (24 - i * 8)) & 0xff;
    hash[i + 28] = (ctx->state[7] >> (24 - i * 8)) & 0xff;
  }
}

static void sha256_hex(const uint8_t *data, size_t len, char out[65]) {
  uint8_t hash[32];
  sha256_ctx ctx;
  int i;
  sha256_init(&ctx);
  sha256_update(&ctx, data, len);
  sha256_final(&ctx, hash);
  for (i = 0; i < 32; i++)
    snprintf(out + i * 2, 3, "%02x", hash[i]);
  out[64] = 0;
}

/* Path check for catalog IO is mirror_disk() in main.c:
 * /data/elf-launcher/mirror/<filename> only. */

#define rename config_rename
static int mkdir_p_under_data(const char *filepath) {
  char dir[512];
  char *slash;
  char *p;
  if (strncmp(filepath, DATA_ROOT, sizeof(DATA_ROOT) - 1))
    return -1;
  snprintf(dir, sizeof(dir), "%s", filepath);
  slash = strrchr(dir, '/');
  if (!slash)
    return 0;
  *slash = 0;
  /* Walk from DATA_ROOT onward only. */
  p = dir + (sizeof(DATA_ROOT) - 1);
  if (p > dir && p[-1] == '/') {
    /* ok */
  }
  for (; *p; p++) {
    if (*p == '/') {
      *p = 0;
      if (mkdir(dir, 0755) && errno != EEXIST)
        return -1;
      *p = '/';
    }
  }
  if (mkdir(dir, 0755) && errno != EEXIST)
    return -1;
  return 0;
}
static int write_atomic_under_data(const char *final_path, const uint8_t *data,
                                  size_t len) {
  char tmp[540];
  FILE *f;
  if (strncmp(final_path, DATA_ROOT, sizeof(DATA_ROOT) - 1))
    return -1;
  if (mkdir_p_under_data(final_path))
    return -2;
  if (snprintf(tmp, sizeof(tmp), "%s.tmp", final_path) >= (int)sizeof(tmp))
    return -3;
  if (strncmp(tmp, DATA_ROOT, sizeof(DATA_ROOT) - 1))
    return -1;
  f = fopen(tmp, "wb");
  if (!f)
    return -4;
  if (fwrite(data, 1, len, f) != len) {
    fclose(f);
    unlink(tmp);
    return -5;
  }
  /* A successful configuration reply must follow a flushed file, rather
   * than rely on buffered data surviving the next console restart. */
  if (fflush(f) || fsync(fileno(f))) {
    fclose(f);
    unlink(tmp);
    return -6;
  }
  if (fclose(f)) {
    unlink(tmp);
    return -6;
  }
  if (rename(tmp, final_path)) {
    unlink(tmp);
    return -7;
  }
  return 0;
}
#undef rename
static int mirror_disk(const char *rel, char *out, size_t outsz) {
  const char *base;
  const char *bs;
  size_t n, i;
  if (!rel || !rel[0] || !out || outsz < 40)
    return -1;
  if (strstr(rel, ".."))
    return -1;
  base = strrchr(rel, '/');
  base = base ? base + 1 : rel;
  bs = strrchr(base, '\\');
  if (bs)
    base = bs + 1;
  n = strlen(base);
  if (n == 0 || n > 180)
    return -1;
  for (i = 0; i < n; i++) {
    unsigned char c = (unsigned char)base[i];
    int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
             (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-' ||
             c == '+';
    if (!ok)
      return -1;
  }
  if (snprintf(out, outsz, "/data/elf-launcher/mirror/%s", base) >= (int)outsz)
    return -1;
  return 0;
}
static int auto_skip_name(const char *base) {
  if (!base || !base[0])
    return 1;
  if (!strcmp(base, "elfldr-ps5.elf") || !strcmp(base, "elfldr.elf") ||
      !strcmp(base, "payload-manager.elf") || !strcmp(base, "pldmgr.elf") ||
      !strcmp(base, "elf-launcher.elf") || !strcmp(base, "wkal-mark.elf") ||
      !strcmp(base, "installer.elf"))
    return 1;
  return 0;
}
static int add_auto_name_checked(char names[][AUTO_NAME_MAX], int *n, int cap,
                                 const char *raw, const char **why) {
  char base[AUTO_NAME_MAX];
  const char *s;
  const char *bs;
  size_t len;
  int i;
  char dest[512];
  if (why)
    *why = "invalid entry";
  if (!raw || !n)
    return 0;
  if (!raw[0]) {
    if (why)
      *why = "empty entry";
    return 0;
  }
  if (*n >= cap) {
    if (why)
      *why = "queue limit reached";
    return 0;
  }
  s = strrchr(raw, '/');
  s = s ? s + 1 : raw;
  bs = strrchr(s, '\\');
  if (bs)
    s = bs + 1;
  len = strlen(s);
  if (len < 5 || len >= sizeof(base)) {
    if (why)
      *why = "invalid filename length";
    return 0;
  }
  if (strcmp(s + len - 4, ".elf") != 0) {
    if (why)
      *why = "filename does not end in .elf";
    return 0;
  }
  memcpy(base, s, len + 1);
  if (auto_skip_name(base)) {
    if (why)
      *why = "reserved launcher filename";
    return 0;
  }
  if (mirror_disk(base, dest, sizeof(dest))) {
    if (why)
      *why = "invalid filename or path";
    return 0;
  }
  for (i = 0; i < *n; i++) {
    if (!strcmp(names[i], base)) {
      if (why)
        *why = "duplicate filename";
      return 0;
    }
  }
  memcpy(names[*n], base, len + 1);
  (*n)++;
  if (why)
    *why = "recognized";
  return 1;
}
static void add_auto_name(char names[][AUTO_NAME_MAX], int *n, int cap,
                          const char *raw) {
  (void)add_auto_name_checked(names, n, cap, raw, NULL);
}
static int write_auto_names_unlocked(const char *list) {
  char names[AUTO_MAX][AUTO_NAME_MAX];
  char serialized[AUTO_MAX * (AUTO_NAME_MAX + 1)];
  int n = 0;
  const char *p = list ? list : "";
  int i;
  size_t used = 0;
  while (*p && n < AUTO_MAX) {
    char tok[AUTO_NAME_MAX];
    size_t k = 0;
    while (*p && *p != ',' && *p != '\n' && *p != '\r' && k + 1 < sizeof(tok))
      tok[k++] = *p++;
    tok[k] = 0;
    if (*p)
      p++;
    if (!add_auto_name_checked(names, &n, AUTO_MAX, tok, NULL))
      return -1; /* Never acknowledge a silently shortened selection. */
  }
  if (*p) return -1;
  for (i = 0; i < n; i++) {
    size_t len = strlen(names[i]);
    if (used + len + 1 > sizeof(serialized))
      return -1;
    memcpy(serialized + used, names[i], len);
    used += len;
    serialized[used++] = '\n';
  }
  if (write_auto_pair_unlocked(names, n, serialized, used))
    return -1;
  return n;
}
static int read_auto_list_impl(char names[][AUTO_NAME_MAX], int cap,
                               int diagnose) {
  FILE *f = fopen(AUTO_LIST_PATH, "r");
  char line[AUTO_NAME_MAX];
  int n = 0, lines = 0;
  if (!f) {
    int open_errno = errno;
    if (diagnose)
      evlog("error", "AutoPayload auto.list open failed: errno=%d", open_errno);
    return 0;
  }
  if (diagnose)
    evlog("info", "AutoPayload reading %s", AUTO_LIST_PATH);
  while (n < cap && fgets(line, sizeof(line), f)) {
    size_t k = strlen(line);
    const char *why = "not recognized";
    int added;
    lines++;
    while (k && (line[k - 1] == '\n' || line[k - 1] == '\r'))
      line[--k] = 0;
    if (diagnose)
      evlog("info", "AutoPayload auto.list line %d: %s", lines,
            k ? line : "(blank)");
    added = add_auto_name_checked(names, &n, cap, line, &why);
    if (diagnose && !added)
      evlog("error", "AutoPayload auto.list line %d ignored: %.96s (%s)",
            lines, line, why);
  }
  if (diagnose && ferror(f))
    evlog("error", "AutoPayload auto.list read error after %d lines", lines);
  fclose(f);
  if (diagnose)
    evlog("info", "AutoPayload auto.list summary: lines=%d recognized=%d",
          lines, n);
  return n;
}
static int read_auto_list(char names[][AUTO_NAME_MAX], int cap) {
  return read_auto_list_impl(names, cap, 0);
}
static int hdr_get(const char *hdrs, const char *end, const char *name,
                   char *out, size_t outsz) {
  size_t nl = strlen(name);
  const char *p = hdrs;
  if (!outsz)
    return -1;
  out[0] = 0;
  while (p && p < end) {
    const char *eol = strstr(p, "\r\n");
    if (!eol || eol > end)
      eol = end;
    if (eol == p)
      break;
    if ((size_t)(eol - p) > nl && !strncasecmp(p, name, nl) && p[nl] == ':') {
      const char *v = p + nl + 1;
      size_t k = 0;
      while (v < eol && (*v == ' ' || *v == '\t'))
        v++;
      while (v < eol && k + 1 < outsz)
        out[k++] = *v++;
      while (k && (out[k - 1] == ' ' || out[k - 1] == '\t'))
        k--;
      out[k] = 0;
      return 0;
    }
    p = eol + 2;
  }
  return -1;
}
static int add_to_auto_list(const char *base) {
  char listed[AUTO_MAX][AUTO_NAME_MAX];
  char joined[AUTO_MAX * AUTO_NAME_MAX];
  size_t used = 0;
  int n, i, write_result;
  pthread_mutex_lock(&profiles_mx);
  pthread_mutex_lock(&auto_list_mx);
  n = read_auto_list_impl(listed, AUTO_MAX, 0);
  joined[0] = 0;
  for (i = 0; i < n; i++) {
    size_t L = strlen(listed[i]);
    if (!strcmp(listed[i], base)) {
      pthread_mutex_unlock(&auto_list_mx);
      pthread_mutex_unlock(&profiles_mx);
      return 0;
    }
    if (used + L + 2 >= sizeof(joined)) {
      pthread_mutex_unlock(&auto_list_mx);pthread_mutex_unlock(&profiles_mx);return -1;
    }
    if (used)
      joined[used++] = ',';
    memcpy(joined + used, listed[i], L + 1);
    used += L;
  }
  if (n >= AUTO_MAX || used + strlen(base) + 2 >= sizeof(joined)) {
    pthread_mutex_unlock(&auto_list_mx);pthread_mutex_unlock(&profiles_mx);return -1;
  }
  {
    if (used)
      joined[used++] = ',';
    snprintf(joined + used, sizeof(joined) - used, "%s", base);
  }
  write_result = write_auto_names_unlocked(joined);
  pthread_mutex_unlock(&auto_list_mx);
  pthread_mutex_unlock(&profiles_mx);
  if (write_result < 0)
    evlog("error", "AutoPayload list update failed for %s", base ? base : "(null)");
  return write_result < 0 ? -1 : 0;
}
static int file_revision_sha256(const char *path, size_t max_size,
                                char revision[65]);
static int read_file_bounded(const char *path, size_t max_size, char **data,
                             size_t *len, int *exists);
static int parse_auto_names_array(const char *body, size_t body_n,
                                  char names[][AUTO_NAME_MAX], int *count,
                                  const char **why);
static int profiles_match_auto(const char *body, size_t len,
                               char names[][AUTO_NAME_MAX], int count);
static void handle_profiles_read(int c) {

    char *buf, *out;
    struct stat st;
    size_t used = 0;
    char revision[65], auto_revision[65], missing[256];
    if (file_revision_sha256(AUTO_LIST_PATH,AUTO_MAX*(AUTO_NAME_MAX+1),auto_revision)) {
      send_json_code(c,500,"{\"ok\":false,\"message\":\"could not read auto revision\"}");return;
    }
    int fd = open(PROFILES_FILE, O_RDONLY);
    if (fd < 0) {
      if (errno != ENOENT || file_revision_sha256(PROFILES_FILE, 65536, revision)) {
        send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not read profiles\"}");
        return;
      }
      snprintf(missing,sizeof(missing),"{\"ok\":true,\"revision\":\"%s\",\"autoRevision\":\"%s\",\"profiles\":null}",revision,auto_revision);
      send_json_code(c,200,missing);
      return;
    }
    if (fstat(fd, &st) || st.st_size < 0 || st.st_size > 65536 ||
        !(buf = malloc((size_t)st.st_size + 1))) {
      close(fd);
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"profiles file invalid or too large\"}");
      return;
    }
    while (used < (size_t)st.st_size) {
      ssize_t r = read(fd, buf + used, (size_t)st.st_size - used);
      if (r < 0 && errno == EINTR)
        continue;
      if (r <= 0) {
        free(buf);
        close(fd);
        send_json_code(c, 500, "{\"ok\":false,\"message\":\"incomplete profiles read\"}");
        return;
      }
      used += (size_t)r;
    }
    close(fd);
    buf[used] = 0;
    sha256_hex((const uint8_t *)buf, used, revision);
    out = malloc(used + 256);
    if (out && used > 1 && buf[0] == '{') {
      snprintf(out, used + 256, "{\"ok\":true,\"revision\":\"%s\",\"autoRevision\":\"%s\",\"profiles\":%s}",
               revision, auto_revision, buf);
      send_json_code(c, 200, out);
    } else {
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not encode profiles\"}");
    }
    free(out);
    free(buf);
    return;

}

static void handle_profiles(int c, int is_post, const char *req, size_t hlen, size_t got,
                            const char *hdrs, const char *hend) {
  if (!is_post) {
    pthread_mutex_lock(&profiles_mx);
    pthread_mutex_lock(&auto_list_mx);
    handle_profiles_read(c);
    pthread_mutex_unlock(&auto_list_mx);
    pthread_mutex_unlock(&profiles_mx);
    return;
  } else {
    const char *why;
    size_t n;
    char *body;
    char expected[80], auto_expected[80], current[65], auto_current[65];
    char auto_header[AUTO_MAX * (AUTO_NAME_MAX + 3) + 8];
    char joined[AUTO_MAX * AUTO_NAME_MAX], response[256];
    char names[AUTO_MAX][AUTO_NAME_MAX];
    char *old_profiles = NULL;
    size_t old_profiles_n = 0, used = 0;
    int old_profiles_exist = 0, auto_count = 0, i, write_count;
    body = read_body(c, req, hlen, got, hdrs, hend, 65536, &n, &why);
    if (!body) {
      fs_reply_bad(c, 400, why);
      return;
    }
    if (body[0] != '{' || body[n - 1] != '}') {
      free(body);
      fs_reply_bad(c, 400, "not JSON");
      return;
    }
    if (hdr_get(hdrs, hend, "If-Match", expected, sizeof(expected)) ||
        !expected[0] ||
        hdr_get(hdrs, hend, "X-Auto-Revision", auto_expected,
                sizeof(auto_expected)) || !auto_expected[0] ||
        hdr_get(hdrs, hend, "X-Auto-List", auto_header,
                sizeof(auto_header))) {
      free(body);
      send_json_code(c, 428, "{\"ok\":false,\"message\":\"profile and auto revisions required\"}");
      return;
    }
    if (parse_auto_names_array(auto_header, strlen(auto_header), names,
                               &auto_count, &why)) {
      free(body);
      fs_reply_bad(c, 400, why);
      return;
    }
    if (!profiles_match_auto(body, n, names, auto_count)) {
      free(body);
      fs_reply_bad(c, 400, "active profile must match the ordered AutoPayload list");
      return;
    }
    joined[0] = 0;
    for (i = 0; i < auto_count; i++) {
      size_t len = strlen(names[i]);
      if (used + len + (i ? 1 : 0) >= sizeof(joined)) {
        free(body);
        fs_reply_bad(c, 400, "auto.list too large");
        return;
      }
      if (i)
        joined[used++] = ',';
      memcpy(joined + used, names[i], len);
      used += len;
      joined[used] = 0;
    }
    pthread_mutex_lock(&profiles_mx);
    pthread_mutex_lock(&auto_list_mx);
    if (file_revision_sha256(PROFILES_FILE, 65536, current)) {
      pthread_mutex_unlock(&auto_list_mx);
      pthread_mutex_unlock(&profiles_mx);
      free(body);
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not read profiles revision\"}");
      return;
    }
    if (file_revision_sha256(AUTO_LIST_PATH,
                             AUTO_MAX * (AUTO_NAME_MAX + 1), auto_current)) {
      pthread_mutex_unlock(&auto_list_mx);
      pthread_mutex_unlock(&profiles_mx);
      free(body);
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not read auto.list revision\"}");
      return;
    }
    if (strcmp(expected, current) || strcmp(auto_expected, auto_current)) {
      pthread_mutex_unlock(&auto_list_mx);
      pthread_mutex_unlock(&profiles_mx);
      free(body);
      snprintf(response, sizeof(response),
               "{\"ok\":false,\"conflict\":true,\"revision\":\"%s\",\"autoRevision\":\"%s\"}",
               current, auto_current);
      send_json_code(c, 409, response);
      return;
    }
    if (read_file_bounded(PROFILES_FILE, 65536, &old_profiles,
                          &old_profiles_n, &old_profiles_exist)) {
      pthread_mutex_unlock(&auto_list_mx);
      pthread_mutex_unlock(&profiles_mx);
      free(body);
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not back up profiles\"}");
      return;
    }
    if (write_atomic_under_data(PROFILES_FILE, (const uint8_t *)body, n)) {
      free(old_profiles);
      pthread_mutex_unlock(&auto_list_mx);
      pthread_mutex_unlock(&profiles_mx);
      free(body);
      fs_reply_bad(c, 500, "write failed");
      return;
    }
    write_count = write_auto_names_unlocked(joined);
    if (write_count != auto_count) {
      int rollback_failed = old_profiles_exist
                                ? write_atomic_under_data(PROFILES_FILE,
                                                          (const uint8_t *)old_profiles,
                                                          old_profiles_n)
                                : (unlink(PROFILES_FILE) != 0 && errno != ENOENT);
      if (rollback_failed)
        evlog("error", "profile/auto transaction rollback failed");
      free(old_profiles);
      pthread_mutex_unlock(&auto_list_mx);
      pthread_mutex_unlock(&profiles_mx);
      free(body);
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"auto.list write failed; profile rolled back\"}");
      return;
    }
    sha256_hex((const uint8_t *)body, n, current);
    if (file_revision_sha256(AUTO_LIST_PATH,
                             AUTO_MAX * (AUTO_NAME_MAX + 1), auto_current)) {
      evlog("error", "profile/auto transaction saved but revision read failed");
      auto_current[0] = 0;
    }
    free(old_profiles);
    pthread_mutex_unlock(&auto_list_mx);
    pthread_mutex_unlock(&profiles_mx);
    free(body);
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"revision\":\"%s\",\"autoRevision\":\"%s\"}",
             current, auto_current);
    send_json_code(c, 200, response);
  }
}

static int file_revision_sha256(const char *path, size_t max_size,
                                char revision[65]) {
  struct stat st;
  uint8_t *buf;
  size_t used = 0;
  int fd = open(path, O_RDONLY);
  if (fd < 0) {
    if (errno != ENOENT)
      return -1;
    sha256_hex((const uint8_t *)"", 0, revision);
    return 0;
  }
  if (fstat(fd, &st) || st.st_size < 0 || (uint64_t)st.st_size > max_size) {
    close(fd);
    return -1;
  }
  buf = malloc((size_t)st.st_size + 1);
  if (!buf) {
    close(fd);
    return -1;
  }
  while (used < (size_t)st.st_size) {
    ssize_t n = read(fd, buf + used, (size_t)st.st_size - used);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      free(buf);
      close(fd);
      return -1;
    }
    used += (size_t)n;
  }
  close(fd);
  sha256_hex(buf, used, revision);
  free(buf);
  return 0;
}

static void json_skip_space(const char **p) {
  while (**p == ' ' || **p == '\t' || **p == '\r' || **p == '\n')
    (*p)++;
}

static int json_skip_string(const char **p, const char *end) {
  if (*p >= end || *(*p)++ != '"')
    return -1;
  while (*p < end) {
    unsigned char c = (unsigned char)*(*p)++;
    if (c == '"')
      return 0;
    if (c < 0x20)
      return -1;
    if (c == '\\') {
      if (*p >= end)
        return -1;
      c = (unsigned char)*(*p)++;
      if (c == 'u') {
        int i;
        for (i = 0; i < 4; i++) {
          if (*p >= end || !isxdigit((unsigned char)**p))
            return -1;
          (*p)++;
        }
      } else if (!strchr("\\\"/bfnrt", c))
        return -1;
    }
  }
  return -1;
}

static int json_skip_value(const char **p, const char *end) {
  json_skip_space(p);
  if (*p >= end)
    return -1;
  if (**p == '"')
    return json_skip_string(p, end);
  if (**p == '{' || **p == '[') {
    char closes[64];
    int depth = 0;
    closes[depth++] = **p == '{' ? '}' : ']';
    (*p)++;
    while (*p < end && depth) {
      if (**p == '"') {
        if (json_skip_string(p, end))
          return -1;
      } else if (**p == '{' || **p == '[') {
        if (depth >= (int)sizeof(closes))
          return -1;
        closes[depth++] = **p == '{' ? '}' : ']';
        (*p)++;
      } else if (**p == '}' || **p == ']') {
        if (**p != closes[depth - 1])
          return -1;
        depth--;
        (*p)++;
      } else
        (*p)++;
    }
    return depth ? -1 : 0;
  }
  while (*p < end && **p != ',' && **p != '}' && **p != ']' &&
         **p != ' ' && **p != '\t' && **p != '\r' && **p != '\n')
    (*p)++;
  return 0;
}

static int json_top_field(const char *json, size_t len, const char *wanted,
                          const char **value, size_t *value_len) {
  const char *p = json, *end = json + len;
  const char *found_value = NULL;
  size_t found_len = 0;
  json_skip_space(&p);
  if (p >= end || *p++ != '{')
    return -1;
  for (;;) {
    const char *key_start, *key_end, *start;
    size_t key_len;
    json_skip_space(&p);
    if (p < end && *p == '}') {
      p++;
      json_skip_space(&p);
      if (p != end || !found_value)
        return -1;
      *value = found_value;
      *value_len = found_len;
      return 0;
    }
    if (p >= end)
      return -1;
    key_start = p;
    if (json_skip_string(&p, end))
      return -1;
    key_end = p - 1;
    key_len = (size_t)(key_end - key_start - 1);
    if (memchr(key_start + 1, '\\', key_len))
      return -1;
    json_skip_space(&p);
    if (p >= end || *p++ != ':')
      return -1;
    json_skip_space(&p);
    start = p;
    if (json_skip_value(&p, end))
      return -1;
    if (key_len == strlen(wanted) && !memcmp(key_start + 1, wanted, key_len)) {
      if (found_value)
        return -1;
      found_value = start;
      found_len = (size_t)(p - start);
    }
    json_skip_space(&p);
    if (p >= end)
      return -1;
    if (*p == ',') {
      p++;
      json_skip_space(&p);
      if (p >= end || *p == '}')
        return -1;
      continue;
    }
    if (*p == '}')
      continue;
    return -1;
  }
}

static int read_file_bounded(const char *path, size_t max_size, char **data,
                             size_t *len, int *exists) {
  struct stat st;
  char *buf;
  size_t used = 0;
  int fd;
  *data = NULL;
  *len = 0;
  *exists = 0;
  fd = open(path, O_RDONLY);
  if (fd < 0)
    return errno == ENOENT ? 0 : -1;
  if (fstat(fd, &st) || st.st_size < 0 || (uint64_t)st.st_size > max_size) {
    close(fd);
    return -1;
  }
  buf = malloc((size_t)st.st_size + 1);
  if (!buf) {
    close(fd);
    return -1;
  }
  while (used < (size_t)st.st_size) {
    ssize_t n = read(fd, buf + used, (size_t)st.st_size - used);
    if (n < 0 && errno == EINTR)
      continue;
    if (n <= 0) {
      free(buf);
      close(fd);
      return -1;
    }
    used += (size_t)n;
  }
  close(fd);
  buf[used] = 0;
  *data = buf;
  *len = used;
  *exists = 1;
  return 0;
}

static int settings_open_after_jb(int *open_flag) {
  char *data;
  size_t len, value_len;
  int exists, rc;
  const char *value;
  rc = read_file_bounded(SETTINGS_FILE, 65536, &data, &len, &exists);
  if (rc || !exists)
    return -1;
  rc = json_top_field(data, len, "openAfterJb", &value, &value_len);
  if (!rc && value_len == 4 && !memcmp(value, "true", 4))
    *open_flag = 1;
  else if (!rc && value_len == 5 && !memcmp(value, "false", 5))
    *open_flag = 0;
  else
    rc = -1;
  free(data);
  return rc;
}

static int settings_delay_value(const char *data, size_t len, int *ms) {
  const char *value;
  size_t value_len;
  char number[32], *endp;
  double delay, twice;
  if (json_top_field(data, len, "autoDelay", &value, &value_len) ||
      !value_len || value_len >= sizeof(number) || !isdigit((unsigned char)value[0]))
    return -1;
  memcpy(number, value, value_len); number[value_len] = 0;
  delay = strtod(number, &endp);
  if (endp == number || *endp || !(delay >= 0 && delay <= 10)) return -1;
  twice = delay * 2;
  if (twice != (double)(long)twice) return -1;
  *ms = (int)(delay * 1000.0 + 0.5);
  return 0;
}

static int settings_auto_delay_ms(void) {
  char *data;
  size_t len;
  int exists;
  int result;
  if (read_file_bounded(SETTINGS_FILE, 65536, &data, &len, &exists)) {
    evlog("error", "AutoPayload delay unavailable: cannot read %s", SETTINGS_FILE);
    return -1;
  }
  if (!exists) {
    evlog("info", "AutoPayload delay=3000ms source=default (settings.json absent)");
    return 3000;
  }
  if (settings_delay_value(data, len, &result)) result = -1;
  free(data);
  evlog(result < 0 ? "error" : "info", "AutoPayload delay=%dms source=settings.json%s", result,
        result < 0 ? " (invalid; automatic sends blocked)" : "");
  return result;
}

static int settings_validate(const char *json, size_t len, int *open_flag) {
  static const char *langs[] = {"en", "ar", "es", "fr", "de", "pt", "ru",
                                "ja", "zh", "it", 0};
  static const char *themes[] = {"pearl", "gold", "black", "gray", "white",
                                 "lavender", "sky", 0};
  const char *value;
  size_t value_len;
  char lang[8];
  char theme[24];
  int i, delay_ms;
  const char *lang_value;
  size_t lang_len;
  if (!json || len < 2 || json[0] != '{' || json[len - 1] != '}' ||
      json_top_field(json, len, "lang", &lang_value, &lang_len) ||
      lang_len < 4 || lang_len >= sizeof(lang) || lang_value[0] != '"' ||
      lang_value[lang_len - 1] != '"' ||
      memchr(lang_value + 1, '\\', lang_len - 2))
    return 0;
  memcpy(lang, lang_value + 1, lang_len - 2);
  lang[lang_len - 2] = 0;
  for (i = 0; langs[i]; i++)
    if (!strcmp(lang, langs[i]))
      break;
  if (!langs[i] || json_top_field(json, len, "schema", &value, &value_len) ||
      value_len != 1 || value[0] != '1')
    return 0;
  if (json_top_field(json, len, "theme", &value, &value_len) ||
      value_len < 3 || value_len >= sizeof(theme) || value[0] != '"' ||
      value[value_len - 1] != '"')
    return 0;
  memcpy(theme, value + 1, value_len - 2);
  theme[value_len - 2] = 0;
  for (i = 0; themes[i]; i++)
    if (!strcmp(theme, themes[i]))
      break;
  if (!themes[i])
    return 0;
  if (settings_delay_value(json, len, &delay_ms)) return 0;
  if (json_top_field(json, len, "openAfterJb", &value, &value_len))
    return 0;
  if (value_len == 4 && !memcmp(value, "true", 4))
    *open_flag = 1;
  else if (value_len == 5 && !memcmp(value, "false", 5))
    *open_flag = 0;
  else
    return 0;
  if (json_top_field(json, len, "pins", &value, &value_len) ||
      value_len < 2 || value[0] != '[' || value[value_len - 1] != ']')
    return 0;
  if (json_top_field(json, len, "recent", &value, &value_len) ||
      value_len < 2 || value[0] != '[' || value[value_len - 1] != ']')
    return 0;
  return 1;
}

static pthread_mutex_t settings_mx = PTHREAD_MUTEX_INITIALIZER;
static void handle_settings(int c, int is_post, const char *req, size_t hlen,
                            size_t got, const char *hdrs, const char *hend) {
  if (!is_post) {
    char *data, *out;
    size_t len, outsz;
    int exists, open_flag, delay_ms;
    char revision[65], defaults[256];
    if (read_file_bounded(SETTINGS_FILE, 65536, &data, &len, &exists)) {
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not read settings\"}");
      return;
    }
    if (!exists) {
      open_flag = open_after_jb();
      snprintf(defaults, sizeof(defaults),
               "{\"schema\":1,\"lang\":\"en\",\"theme\":\"pearl\",\"autoDelay\":3,\"openAfterJb\":%s,\"pins\":[],\"recent\":[]}",
               open_flag ? "true" : "false");
      data = strdup(defaults);
      if (!data) {
        send_json_code(c, 500, "{\"ok\":false,\"message\":\"no memory\"}");
        return;
      }
      len = strlen(data);
      sha256_hex((const uint8_t *)"", 0, revision);
    } else {
      if (!settings_validate(data, len, &open_flag)) {
        free(data);
        send_json_code(c, 500, "{\"ok\":false,\"message\":\"settings.json is invalid\"}");
        return;
      }
      sha256_hex((const uint8_t *)data, len, revision);
    }
    if (settings_delay_value(data, len, &delay_ms)) { free(data); send_json_code(c, 500, "{\"ok\":false,\"message\":\"invalid delay\"}"); return; }
    outsz = len + 256;
    out = malloc(outsz);
    if (!out) {
      free(data);
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"no memory\"}");
      return;
    }
    snprintf(out, outsz,
             "{\"ok\":true,\"exists\":%s,\"revision\":\"%s\",\"autoDelayMs\":%d,\"delaySource\":\"%s\",\"settings\":%s}",
             exists ? "true" : "false", revision, delay_ms, exists ? "settings.json" : "default", data);
    send_json_code(c, 200, out);
    free(out);
    free(data);
    return;
  }
  {
    const char *why;
    size_t len;
    char *body;
    char expected[80], current[65], response[160];
    int open_flag;
    body = read_body(c, req, hlen, got, hdrs, hend, 65536, &len, &why);
    if (!body) {
      fs_reply_bad(c, 400, why);
      return;
    }
    if (!settings_validate(body, len, &open_flag)) {
      free(body);
      send_json_code(c, 400, "{\"ok\":false,\"message\":\"invalid settings schema\"}");
      return;
    }
    if (hdr_get(hdrs, hend, "If-Match", expected, sizeof(expected)) ||
        !expected[0]) {
      free(body);
      send_json_code(c, 428, "{\"ok\":false,\"message\":\"If-Match revision required\"}");
      return;
    }
    pthread_mutex_lock(&settings_mx);
    if (file_revision_sha256(SETTINGS_FILE, 65536, current)) {
      pthread_mutex_unlock(&settings_mx);
      free(body);
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not read settings revision\"}");
      return;
    }
    if (strcmp(expected, current)) {
      pthread_mutex_unlock(&settings_mx);
      free(body);
      snprintf(response, sizeof(response),
               "{\"ok\":false,\"conflict\":true,\"revision\":\"%s\"}", current);
      send_json_code(c, 409, response);
      return;
    }
    /* The normal trusted-origin/custom-header guard also covers an explicit
     * first write from PC. GET never initializes settings from localStorage. */
    if (write_atomic_under_data(SETTINGS_FILE, (const uint8_t *)body, len)) {
      pthread_mutex_unlock(&settings_mx);
      free(body);
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"atomic settings write failed\"}");
      return;
    }
    sha256_hex((const uint8_t *)body, len, current);
    if (write_open_after_jb(open_flag))
      evlog("error", "settings saved but open-after-jb legacy mirror could not be updated");
    pthread_mutex_unlock(&settings_mx);
    free(body);
    snprintf(response, sizeof(response),
             "{\"ok\":true,\"revision\":\"%s\"}", current);
    send_json_code(c, 200, response);
  }
}

static int parse_auto_names_array(const char *body, size_t body_n,
                                  char names[][AUTO_NAME_MAX], int *count,
                                  const char **why) {
  const char *p = body, *end = body + body_n;
  int n = 0;
  json_skip_space(&p);
  if (p >= end || *p++ != '[')
    goto bad_json;
  json_skip_space(&p);
  if (p < end && *p == ']') {
    p++;
    json_skip_space(&p);
    if (p != end)
      goto bad_json;
    *count = 0;
    return 0;
  }
  for (;;) {
    char item[AUTO_NAME_MAX];
    const char *start, *item_why;
    size_t len;
    json_skip_space(&p);
    if (p >= end || *p++ != '"')
      goto bad_json;
    start = p;
    while (p < end && *p != '"') {
      if (*p == '\\' || (unsigned char)*p < 0x20)
        goto bad_json;
      p++;
    }
    if (p >= end)
      goto bad_json;
    len = (size_t)(p - start);
    p++;
    if (!len || len >= sizeof(item)) {
      *why = "invalid filename length";
      return -1;
    }
    memcpy(item, start, len);
    item[len] = 0;
    if (!add_auto_name_checked(names, &n, AUTO_MAX, item, &item_why)) {
      *why = item_why;
      return -1;
    }
    json_skip_space(&p);
    if (p < end && *p == ']') {
      p++;
      json_skip_space(&p);
      if (p != end)
        goto bad_json;
      *count = n;
      return 0;
    }
    if (p >= end || *p++ != ',')
      goto bad_json;
  }
bad_json:
  *why = "expected a JSON array of ELF filenames";
  return -1;
}

static int profile_active_value(const char *body, size_t len, const char **out, size_t *out_n) {
  const char *active, *list, *order, *p, *end;
  size_t active_n, list_n, order_n;
  int found = 0;
  if (json_top_field(body, len, "active", &active, &active_n) ||
      active_n < 3 || active[0] != '"' || active[active_n - 1] != '"' ||
      json_top_field(body, len, "order", &order, &order_n) || order_n < 2 || order[0] != '[' ||
      json_top_field(body, len, "list", &list, &list_n) || list_n < 2 || list[0] != '{') return 0;
  p = list + 1; end = list + list_n;
  for (;;) {
    const char *key, *value;
    size_t key_n;
    json_skip_space(&p);
    if (p < end && *p == '}') return found == 1;
    key = p;
    if (json_skip_string(&p, end)) return 0;
    key_n = (size_t)(p - key);
    json_skip_space(&p);
    if (p >= end || *p++ != ':') return 0;
    json_skip_space(&p); value = p;
    if (json_skip_value(&p, end)) return 0;
    if (key_n == active_n && !memcmp(key, active, key_n)) {
      if (++found != 1) return 0;
      *out=value; *out_n=(size_t)(p-value);
    }
    json_skip_space(&p);
    if (p < end && *p == ',') { p++; continue; }
    if (p < end && *p == '}') return found == 1;
    return 0;
  }
}

static int profiles_match_auto(const char *body, size_t len,
                               char names[][AUTO_NAME_MAX], int count) {
  const char *value, *why;
  size_t value_n;
  char expected[AUTO_MAX][AUTO_NAME_MAX];
  int n, i;
  if (!profile_active_value(body,len,&value,&value_n) ||
      parse_auto_names_array(value,value_n,expected,&n,&why) || n != count) return 0;
  for (i=0;i<n;i++) if (strcmp(expected[i],names[i])) return 0;
  return 1;
}

/* All callers hold profiles_mx then auto_list_mx. Keep direct list edits,
 * uploads and /profiles on the same paired write path. Preserve inactive
 * profiles byte-for-byte and roll back the profile if auto.list fails. */
static int write_auto_pair_unlocked(char names[][AUTO_NAME_MAX], int count,
                                    const char *serialized, size_t len) {
  char *old=NULL, *updated=NULL, array[AUTO_MAX*(AUTO_NAME_MAX+3)+4];
  const char *value;
  size_t old_n=0, value_n=0, k=0, prefix, updated_n;
  int exists, i, rc=0, changed=0;
  if (read_file_bounded(PROFILES_FILE,65536,&old,&old_n,&exists)) return -1;
  if (exists) {
    if (!profile_active_value(old,old_n,&value,&value_n)) { free(old);return -1; }
    array[k++]='[';
    for (i=0;i<count;i++) k+=(size_t)snprintf(array+k,sizeof(array)-k,"%s\"%s\"",i?",":"",names[i]);
    array[k++]=']';array[k]=0;
    changed=k!=value_n || memcmp(array,value,k);
    if (changed) {
      prefix=(size_t)(value-old);updated_n=old_n-value_n+k;
      if (updated_n>65536) { free(old);return -1; }
      updated=malloc(updated_n);
      if (!updated) { free(old);return -1; }
      memcpy(updated,old,prefix);memcpy(updated+prefix,array,k);
      memcpy(updated+prefix+k,value+value_n,old_n-prefix-value_n);
      rc=write_atomic_under_data(PROFILES_FILE,(const uint8_t *)updated,updated_n);
      free(updated);
      if (rc) { free(old);return -1; }
    }
  }
  rc=write_atomic_under_data(AUTO_LIST_PATH,(const uint8_t *)serialized,len);
  if (rc && changed && write_atomic_under_data(PROFILES_FILE,(const uint8_t *)old,old_n))
    evlog("error","AutoPayload paired write rollback failed; configuration needs review");
  free(old);
  return rc ? -1 : 0;
}

static int auto_list_response(char *out, size_t outsz, int ok,
                              char names[][AUTO_NAME_MAX], int count,
                              const char *revision) {
  size_t used;
  int i, n;
  n = snprintf(out, outsz,
               "{\"ok\":%s,\"count\":%d,\"revision\":\"%s\",\"names\":[",
               ok ? "true" : "false", count, revision);
  if (n < 0 || (size_t)n >= outsz)
    return -1;
  used = (size_t)n;
  for (i = 0; i < count; i++) {
    n = snprintf(out + used, outsz - used, "%s\"%s\"",
                 i ? "," : "", names[i]);
    if (n < 0 || (size_t)n >= outsz - used)
      return -1;
    used += (size_t)n;
  }
  if (used + 3 > outsz)
    return -1;
  memcpy(out + used, "]}", 3);
  return (int)(used + 2);
}

static void handle_auto_list(int c, int is_post, const char *req, size_t hlen,
                             size_t got, const char *hdrs, const char *hend,
                             const char *qs) {
  char names[AUTO_MAX][AUTO_NAME_MAX];
  char json[AUTO_MAX * 160 + 256];
  char revision[65], expected[80], joined[AUTO_MAX * AUTO_NAME_MAX];
  int count = 0, rc;
  if (!is_post) {
    if (qs && strstr(qs, "names=")) {
      send_json_code(c, 405, "{\"ok\":false,\"message\":\"use POST /auto-list\"}");
      return;
    }
    pthread_mutex_lock(&profiles_mx);
  pthread_mutex_lock(&auto_list_mx);
    count = read_auto_list(names, AUTO_MAX);
    rc = file_revision_sha256(AUTO_LIST_PATH,
                              AUTO_MAX * (AUTO_NAME_MAX + 1), revision);
    pthread_mutex_unlock(&auto_list_mx);
  pthread_mutex_unlock(&profiles_mx);
    if (rc || auto_list_response(json, sizeof(json), 1, names, count, revision) < 0)
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not read auto.list\"}");
    else
      send_json_code(c, 200, json);
    return;
  }
  {
    const char *why = "invalid request";
    size_t body_n = 0;
    char *body = read_body(c, req, hlen, got, hdrs, hend, 8192, &body_n, &why);
    int i;
    size_t used = 0;
    if (!body) {
      fs_reply_bad(c, 400, why);
      return;
    }
    if (parse_auto_names_array(body, body_n, names, &count, &why)) {
      free(body);
      fs_reply_bad(c, 400, why);
      return;
    }
    if (hdr_get(hdrs, hend, "If-Match", expected, sizeof(expected)) ||
        !expected[0]) {
      free(body);
      send_json_code(c, 428, "{\"ok\":false,\"message\":\"If-Match revision required\"}");
      return;
    }
    joined[0] = 0;
    for (i = 0; i < count; i++) {
      size_t len = strlen(names[i]);
      if (used + len + (i ? 1 : 0) >= sizeof(joined)) {
        free(body);
        send_json_code(c, 400, "{\"ok\":false,\"message\":\"auto.list too large\"}");
        return;
      }
      if (i)
        joined[used++] = ',';
      memcpy(joined + used, names[i], len);
      used += len;
      joined[used] = 0;
    }
    free(body);
  }
  pthread_mutex_lock(&profiles_mx);
  pthread_mutex_lock(&auto_list_mx);
  rc = file_revision_sha256(AUTO_LIST_PATH,
                            AUTO_MAX * (AUTO_NAME_MAX + 1), revision);
  if (rc) {
    pthread_mutex_unlock(&auto_list_mx);
  pthread_mutex_unlock(&profiles_mx);
    send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not read auto.list revision\"}");
    return;
  }
  if (strcmp(expected, revision)) {
    count = read_auto_list(names, AUTO_MAX);
    rc = auto_list_response(json, sizeof(json), 0, names, count, revision);
    pthread_mutex_unlock(&auto_list_mx);
  pthread_mutex_unlock(&profiles_mx);
    if (rc < 0)
      send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not report conflict\"}");
    else
      send_json_code(c, 409, json);
    return;
  }
  rc = write_auto_names_unlocked(joined);
  if (rc < 0 || file_revision_sha256(AUTO_LIST_PATH,
                                     AUTO_MAX * (AUTO_NAME_MAX + 1), revision)) {
    pthread_mutex_unlock(&auto_list_mx);
  pthread_mutex_unlock(&profiles_mx);
    send_json_code(c, 500, "{\"ok\":false,\"message\":\"atomic auto.list write failed\"}");
    return;
  }
  count = rc;
  rc = auto_list_response(json, sizeof(json), 1, names, count, revision);
  pthread_mutex_unlock(&auto_list_mx);
  pthread_mutex_unlock(&profiles_mx);
  if (rc < 0)
    send_json_code(c, 500, "{\"ok\":false,\"message\":\"could not report saved auto.list\"}");
  else
    send_json_code(c, 200, json);
}


static void revisions(char profiles[65],char auto_rev[65]) {
  assert(!file_revision_sha256(PROFILES_FILE,65536,profiles));
  assert(!file_revision_sha256(AUTO_LIST_PATH,65536,auto_rev));
}
static void save_profile(const char *body,const char *names,const char *pr,const char *ar) {
  char headers[8192];snprintf(headers,sizeof(headers),"If-Match: %s\r\nX-Auto-Revision: %s\r\nX-Auto-List: %s\r\n",pr,ar,names);
  handle_profiles(0,1,body,0,strlen(body),headers,headers+strlen(headers));
}
static void settings_post(const char *body,const char *revision) {
  char headers[256];snprintf(headers,sizeof(headers),"If-Match: %s\r\n",revision);
  handle_settings(0,1,body,0,strlen(body),headers,headers+strlen(headers));
}
static void check_names(int n,const char *last) {
  char names[AUTO_MAX][AUTO_NAME_MAX];assert(read_auto_list(names,AUTO_MAX)==n);
  if(last)assert(!strcmp(names[n-1],last));
}
int main(void) {
  char checked_names[AUTO_MAX][AUTO_NAME_MAX];int checked_count=0;
  add_auto_name(checked_names,&checked_count,AUTO_MAX,"nanodns.elf");assert(checked_count==1);
  const char *four="[\"kstuff-lite.elf\",\"shadowmountplus.elf\",\"ProsperoMgr.elf\",\"nanodns.elf\"]";
  const char *three="[\"kstuff-lite.elf\",\"shadowmountplus.elf\",\"ProsperoMgr.elf\"]";
  char profile[8192],pr[65],ar[65],before[65],rev[65],settings[512];
  mkdir(DATA_ROOT,0700);unlink(AUTO_LIST_PATH);unlink(PROFILES_FILE);unlink(SETTINGS_FILE);
  revisions(pr,ar);snprintf(profile,sizeof(profile),"{\"active\":\"Default\",\"order\":[\"Default\"],\"list\":{\"Default\":%s}}",four);
  save_profile(profile,four,pr,ar);assert(code==200);check_names(4,"nanodns.elf");
  revisions(pr,ar);snprintf(before,sizeof(before),"%s",ar);
  for(int i=0;i<10;i++)handle_auto_list(0,0,"",0,0,"","",NULL);
  assert(code==200&&strstr(response,"nanodns.elf"));revisions(pr,ar);assert(!strcmp(before,ar));
  save_profile(profile,four,"stale",ar);assert(code==409);check_names(4,"nanodns.elf");
  save_profile(profile,three,pr,ar);assert(code==400);check_names(4,"nanodns.elf");
  snprintf(profile,sizeof(profile),"{\"active\":\"Default\",\"order\":[\"Default\"],\"list\":{\"Default\":%s}}",three);
  fail_target=1;save_profile(profile,three,pr,ar);assert(code==500);check_names(4,"nanodns.elf");
  char check[65];assert(!file_revision_sha256(PROFILES_FILE,65536,check));assert(!strcmp(pr,check));
  fail_target=0;save_profile(profile,three,pr,ar);assert(code==200);check_names(3,"ProsperoMgr.elf");
  revisions(pr,ar);assert(strcmp(before,ar));
  snprintf(profile,sizeof(profile),"{\"active\":\"DNS\",\"order\":[\"Default\",\"DNS\"],\"list\":{\"Default\":%s,\"DNS\":[\"nanodns.elf\"]}}",three);
  save_profile(profile,"[\"nanodns.elf\"]",pr,ar);assert(code==200);check_names(1,"nanodns.elf");
  revisions(pr,ar);char header[256];snprintf(header,sizeof(header),"If-Match: %s\r\n",ar);
  handle_auto_list(0,1,three,0,strlen(three),header,header+strlen(header),NULL);assert(code==200);
  assert(!add_to_auto_list("nanodns.elf"));check_names(4,"nanodns.elf");
  char *profile_data;size_t profile_n;int profile_exists,names_n;
  assert(!read_file_bounded(PROFILES_FILE,65536,&profile_data,&profile_n,&profile_exists));
  names_n=read_auto_list(checked_names,AUTO_MAX);assert(profiles_match_auto(profile_data,profile_n,checked_names,names_n));
  assert(strstr(profile_data,three));free(profile_data);
  fail_target=1;assert(add_to_auto_list("rejected.elf")==-1);check_names(4,"nanodns.elf");fail_target=0;
  /* Missing settings remain missing on GET. Their default is explicit. */
  handle_settings(0,0,"",0,0,"","");assert(code==200&&strstr(response,"\"autoDelayMs\":3000")&&strstr(response,"\"exists\":false"));
  assert(access(SETTINGS_FILE,F_OK));
  assert(write_auto_names_unlocked("")==0);
  for(int seconds=3;seconds<=10;seconds+=seconds==3?2:5){
    assert(!file_revision_sha256(SETTINGS_FILE,65536,rev));
    snprintf(settings,sizeof(settings),"{\"schema\":1,\"lang\":\"pt\",\"theme\":\"sky\",\"autoDelay\":%d,\"openAfterJb\":true,\"pins\":[],\"recent\":[]}",seconds);
    settings_post(settings,rev);assert(code==200);assert(settings_auto_delay_ms()==seconds*1000);
    int open_flag=0;assert(!settings_open_after_jb(&open_flag)&&open_flag==1);
    handle_settings(0,0,"",0,0,"","");assert(code==200);char field[64];snprintf(field,sizeof(field),"\"autoDelayMs\":%d",seconds*1000);assert(strstr(response,field));
    check_names(0,NULL);
  }
  assert(!file_revision_sha256(SETTINGS_FILE,65536,rev));
  fail_target=2;settings_post(settings,rev);assert(code==500&&settings_auto_delay_ms()==10000);fail_target=0;
  settings_post(settings,"stale");assert(code==409&&settings_auto_delay_ms()==10000);
  /* Missing auto.list is independent of settings.json. */
  unlink(AUTO_LIST_PATH);assert(settings_auto_delay_ms()==10000);
  FILE *f=fopen(SETTINGS_FILE,"w");assert(f);fputs("{\"autoDelay\":\"10\"}",f);fclose(f);
  assert(settings_auto_delay_ms()==-1);handle_settings(0,0,"",0,0,"","");assert(code==500);
  const char *bad[]={"{\"autoDelay\":NaN}","{\"autoDelay\":11}","{\"autoDelay\":-1}","{\"autoDelay\":0.25}"};
  for(unsigned int i=0;i<sizeof(bad)/sizeof(bad[0]);i++){int ms;assert(settings_delay_value(bad[i],strlen(bad[i]),&ms));}
  unlink(SETTINGS_FILE);unlink(PROFILES_FILE);unlink(AUTO_LIST_PATH);rmdir(DATA_ROOT);
  puts("PASS: production C handlers: four payloads/NanoDNS, removal, active profile, read-only GET/revisions, conflicts, rollback/write failures, 3/5/10 seconds, empty/missing auto.list, explicit PC initialization, invalid delay");
}
