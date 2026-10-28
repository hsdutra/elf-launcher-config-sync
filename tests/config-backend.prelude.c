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
