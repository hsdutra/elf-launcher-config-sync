/* Host harness exercises the production worker, locks and endpoints.
 * Only disk/elfldr/HTTP dependencies are simulated; no network is used. */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#define AUTO_MAX 64
#define AUTO_NAME_MAX 128
#define SEND_OK 0
static int count, delay, sends, fail_at, missing_at, cancel_in_send, marker_error, journal_error, volatile_mount = 1;
static int64_t first_send_ms;
#define AUTO_BOOT_DIR "/tmp/elf-launcher-auto-fixture"
#define BOOT_AUTO_DONE_PATH "/tmp/elf-launcher-auto-fixture-audit"
static char response[200000];
static int code;
static int64_t mono_ms(void) { struct timespec ts; clock_gettime(CLOCK_MONOTONIC,&ts); return (int64_t)ts.tv_sec*1000+ts.tv_nsec/1000000; }
static int auto_tmp_is_volatile(void) { return volatile_mount; }
static int write_atomic_under_data(const char *path,const uint8_t *data,size_t len) {
  if(marker_error)return -1;
  FILE *f=fopen(path,"wb");if(!f)return -1;
  int rc=fwrite(data,1,len,f)==len?0:-1;if(fclose(f))rc=-1;return rc;
}
static int journal_rename(const char *a,const char *b) {
  if(journal_error){errno=EIO;return -1;}return rename(a,b);
}
static void evlog(const char *kind,const char *fmt,...) {(void)kind;(void)fmt;}
static int settings_auto_delay_ms(void) { return delay; }
static int read_auto_list_impl(char names[][AUTO_NAME_MAX],int cap,int diagnose) {
  (void)cap;(void)diagnose; for(int i=0;i<count;i++)snprintf(names[i],AUTO_NAME_MAX,"payload-%d.elf",i);return count;
}
static int existing_names(char a[][AUTO_NAME_MAX],int n,char b[][AUTO_NAME_MAX],int cap,const char *s) {(void)a;(void)b;(void)cap;(void)s;return n;}
static int auto_disk_path_reason(const char *name,char *path,size_t len,const char **why) {
  int i=-1;sscanf(name,"payload-%d.elf",&i);if(i==missing_at){*why="file missing";return -1;}snprintf(path,len,"%s",name);return 0;
}
static int send_busy;
static int send_lock_try(void) { return !__sync_lock_test_and_set(&send_busy,1); }
static void send_unlock(void) { __sync_lock_release(&send_busy); }
static int elfldr_send_path(const char *,const char *,char *,size_t,int);
static const char *send_err_text(int rc) {return rc?"simulated rejection":"sent";}
static int json_escape_name(const char *s,char *d,size_t n) {snprintf(d,n,"%s",s);return 0;}
static void send_json_code(int c,int status,const char *s) {(void)c;code=status;snprintf(response,sizeof(response),"%s",s);}
static void send_json(int c,int ok,const char *s,size_t n) {(void)n;send_json_code(c,ok?200:500,s);}
static void qget(const char *qs,const char *key,char *out,size_t len) {(void)key;snprintf(out,len,"%s",qs+8);}
#define rename journal_rename
#include "../host/hbinstall/auto_boot.inc"
#undef rename
static int elfldr_send_path(const char *path,const char *args,char *msg,size_t len,int ms) {
  (void)args;(void)ms;int i;sscanf(path,"payload-%d.elf",&i);if(!sends)first_send_ms=mono_ms();sends++;
  handle_boot_auto(0);assert(strstr(response,"\"done\":false") && strstr(response,"\"phase\":\"loading\""));
  handle_auto_status(0);assert(strstr(response,"\"done\":false") && strstr(response,"\"phase\":\"loading\""));
  if(cancel_in_send){char qs[100];snprintf(qs,sizeof(qs),"session=%s",boot_auto.session);handle_auto_cancel(0,qs,1);}
  snprintf(msg,len,"%s",i==fail_at?"rejected":"transport accepted");return i==fail_at?-3:0;
}
static void reset(int n) {
  unlink(AUTO_BOOT_ID_PATH);unlink(AUTO_BOOT_STATE_PATH);unlink(AUTO_BOOT_STATE_PATH ".tmp");
  unlink(BOOT_AUTO_DONE_PATH);
  memset(&boot_auto,0,sizeof(boot_auto));boot_auto.phase="idle";
  memset(auto_boot_id,0,sizeof(auto_boot_id));
  sends=delay=cancel_in_send=marker_error=send_busy=journal_error=0;count=n;fail_at=missing_at=-1;volatile_mount=1;
  first_send_ms=0;
}
static void restart_server(void) {
  memset(&boot_auto,0,sizeof(boot_auto));boot_auto.phase="idle";
  memset(auto_boot_id,0,sizeof(auto_boot_id));
  auto_restore();
}
static void endpoint_state(const char *phase,int done) {
  char part[128],snapshot[200000];
  snprintf(part,sizeof(part),"\"phase\":\"%s\"",phase);
  handle_boot_auto(0);assert(code==200 && strstr(response,part));
  assert(strstr(response,done?"\"done\":true":"\"done\":false"));
  snprintf(snapshot,sizeof(snapshot),"%s",response);
  handle_auto_status(0);assert(code==200 && strstr(response,part));
  assert(strstr(response,done?"\"done\":true":"\"done\":false"));
  if(done)assert(boot_auto.session[0] && strcmp(boot_auto.phase,"idle"));
  snprintf(part,sizeof(part),"\"session\":\"%s\"",boot_auto.session);
  assert(strstr(snapshot,part) && strstr(response,part));
}
static void *claim_thread(void *arg) {int *wins=arg;if(auto_claim())__sync_fetch_and_add(wins,1);return NULL;}
static void *run_thread(void *arg) {(void)arg;run_headless_auto("test");return NULL;}
static void cancel_current(void) {char qs[100];snprintf(qs,sizeof(qs),"session=%s",boot_auto.session);handle_auto_cancel(0,qs,1);assert(code==200);}
int main(void) {
  int sizes[]={0,1,3,12,64};
  for(unsigned int s=0;s<sizeof(sizes)/sizeof(sizes[0]);s++){
    reset(sizes[s]);assert(auto_claim());run_headless_auto("test");
    assert(sends==count && boot_auto.completed==count);assert(!strcmp(boot_auto.phase,"completed"));
    assert(!auto_claim());handle_auto_status(0);assert(code==200);assert(strstr(response,"transport-only"));
    int prior=sends;for(int i=0;i<20;i++)handle_auto_status(0);assert(sends==prior);
  }
  reset(3);fail_at=1;missing_at=2;assert(auto_claim());run_headless_auto("test");
  assert(sends==2 && boot_auto.completed==3);assert(!strcmp(boot_auto.items[0].status,"success"));
  assert(!strcmp(boot_auto.items[1].status,"error"));assert(!strcmp(boot_auto.items[2].status,"skipped"));
  reset(3);delay=10000;assert(auto_claim());pthread_t worker;pthread_create(&worker,NULL,run_thread,NULL);
  usleep(60000);handle_auto_status(0);assert(strstr(response,"waiting"));assert(boot_auto.deadline-mono_ms()>9000);
  endpoint_state("waiting",0);
  int64_t t=mono_ms();cancel_current();pthread_join(worker,NULL);assert(mono_ms()-t<150);assert(sends==0);
  assert(!strcmp(boot_auto.phase,"cancelled"));assert(boot_auto.completed==3);assert(!auto_claim());
  restart_server();endpoint_state("cancelled",1);assert(!auto_claim() && sends==0);
  reset(3);cancel_in_send=1;assert(auto_claim());run_headless_auto("test");
  assert(sends==1 && !send_busy);assert(!strcmp(boot_auto.items[0].status,"success"));assert(!strcmp(boot_auto.items[1].status,"cancelled"));
  reset(3);delay=10000;assert(auto_claim());send_busy=1;delay=0;pthread_create(&worker,NULL,run_thread,NULL);
  usleep(60000);cancel_current();pthread_join(worker,NULL);assert(sends==0 && send_busy==1);send_busy=0;
  reset(3);assert(auto_claim());handle_auto_cancel(0,"session=old",1);assert(code==409 && !boot_auto.cancel);
  handle_auto_cancel(0,NULL,0);assert(code==405 && !boot_auto.cancel);
  reset(3);assert(auto_claim());journal_error=1;run_headless_auto("journal failure");
  endpoint_state("error",0);assert(sends==0 && !boot_auto.started);
  journal_error=0;assert(auto_claim());run_headless_auto("retry safe");assert(sends==3);
  reset(3);marker_error=1;assert(auto_claim());run_headless_auto("diagnostic failure");
  endpoint_state("completed",1);assert(sends==3);
  reset(3);pthread_t threads[32];int wins=0;
  for(int i=0;i<32;i++)pthread_create(&threads[i],NULL,claim_thread,&wins);
  for(int i=0;i<32;i++)pthread_join(threads[i],NULL);
  assert(wins==1);run_headless_auto("concurrent");assert(sends==3);
  /* Exact regression: persistent 103 (or a two-number legacy epoch) is NOT
   * current-boot proof, whether current uptime is larger, smaller or equal. */
  const char *legacy[]={"103\n","999999999\n","0\n","103 0\n","103 1700000000\n","garbage\n"};
  for(unsigned int i=0;i<sizeof(legacy)/sizeof(legacy[0]);i++){
    reset(3);FILE *f=fopen(BOOT_AUTO_DONE_PATH,"w");assert(f);fputs(legacy[i],f);fclose(f);
    assert(auto_claim());endpoint_state("preparing",0);run_headless_auto("legacy");
    endpoint_state("completed",1);assert(sends==3);
  }
  /* A reboot removes tmpfs only. The persistent audit and clocks can remain
   * identical; a new boot ID must still allow exactly one new execution. */
  reset(3);assert(auto_claim());run_headless_auto("boot A");char first[80];snprintf(first,sizeof(first),"%s",boot_auto.session);
  restart_server();endpoint_state("completed",1);assert(!auto_claim() && sends==3);
  unlink(AUTO_BOOT_ID_PATH);unlink(AUTO_BOOT_STATE_PATH);restart_server();
  assert(strcmp(first,boot_auto.session));endpoint_state("idle",0);
  assert(auto_claim());run_headless_auto("boot B");assert(sends==6);assert(!auto_claim());
  /* Reservation/thread failure must not consume anything or write a journal. */
  reset(3);assert(auto_claim());assert(access(AUTO_BOOT_STATE_PATH,F_OK));auto_start_failed();
  endpoint_state("error",0);assert(!boot_auto.started && sends==0);
  assert(auto_claim());run_headless_auto("thread retry");assert(sends==3);
  /* Saved countdown is restartable; admission with an unknown outcome is not. */
  reset(3);assert(auto_claim());boot_auto.phase="waiting";assert(!write_boot_auto_done());
  restart_server();assert(auto_claim());run_headless_auto("countdown restart");assert(sends==3);
  reset(3);assert(auto_claim());boot_auto.total=3;boot_auto.admitted=1;boot_auto.phase="loading";
  for(int i=0;i<3;i++){snprintf(boot_auto.items[i].name,AUTO_NAME_MAX,"payload-%d.elf",i);boot_auto.items[i].status=i?"pending":"loading";}
  assert(!write_boot_auto_done());restart_server();endpoint_state("error",0);
  assert(!auto_claim() && sends==0 && boot_auto.admitted && boot_auto.completed==3);
  assert(strstr(boot_auto.message,"outcome unknown"));
  reset(3);assert(auto_claim());boot_auto.phase="completed";boot_auto.total=3;boot_auto.completed=0;
  for(int i=0;i<3;i++)boot_auto.items[i].status="pending";
  assert(!write_boot_auto_done());restart_server();endpoint_state("error",0);assert(!auto_claim());
  /* Non-tmpfs storage must never become a persistent-boot fallback. */
  reset(3);volatile_mount=0;assert(!auto_claim());endpoint_state("error",0);assert(sends==0);
  reset(3);assert(auto_claim());run_headless_auto("before identity damage");
  FILE *damaged=fopen(AUTO_BOOT_ID_PATH,"w");assert(damaged);fputs("103",damaged);fclose(damaged);
  restart_server();endpoint_state("error",0);assert(!auto_claim() && sends==3);
  reset(3);delay=10000;assert(auto_claim());int64_t began=mono_ms();run_headless_auto("full 10-second delay");
  assert(first_send_ms-began>=10000 && sends==3);endpoint_state("completed",1);
  reset(3);delay=-1;assert(auto_claim());run_headless_auto("invalid persisted delay");
  endpoint_state("error",0);assert(sends==0 && !boot_auto.started && !boot_auto.admitted);
  reset(0);rmdir(AUTO_BOOT_DIR);
  puts("PASS: worker queues, countdown, cancellation, 32 concurrent claims, real journal I/O, legacy markers (including 103), reboot, server restart, thread failure, safe pre-send retry, uncertain admission, corrupt snapshots and consistent endpoints");
}
