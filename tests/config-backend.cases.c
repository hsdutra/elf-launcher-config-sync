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
  /* Real paired writes preserve profile-specific intervals and filename order. */
  revisions(pr,ar);
  snprintf(profile,sizeof(profile),"{\"active\":\"Default\",\"order\":[\"Default\",\"DNS\"],\"list\":{\"Default\":%s,\"DNS\":[\"nanodns.elf\"]},\"intervals\":{\"Default\":{\"kstuff-lite.elf\":5,\"ProsperoMgr.elf\":300},\"DNS\":{\"nanodns.elf\":3}}}",three);
  save_profile(profile,three,pr,ar);assert(code==200);
  int ms[AUTO_MAX];assert(auto_sequence_snapshot(checked_names,ms)==3);
  assert(ms[0]==5000 && ms[1]==0 && ms[2]==300000);
  assert(!strcmp(checked_names[0],"kstuff-lite.elf"));
  /* A failed auto.list replacement rolls back interval edits too. */
  revisions(pr,ar);fail_target=1;save_profile(profile,three,pr,ar);assert(code==500);fail_target=0;
  assert(auto_sequence_snapshot(checked_names,ms)==3 && ms[0]==5000);
  const char *bad_interval[]={"-1","301","1.5","\"5\"","null","true","1e2","01"};
  for(unsigned i=0;i<sizeof(bad_interval)/sizeof(bad_interval[0]);i++) {
    revisions(pr,ar);
    snprintf(profile,sizeof(profile),"{\"active\":\"Default\",\"order\":[\"Default\"],\"list\":{\"Default\":%s},\"intervals\":{\"Default\":{\"kstuff-lite.elf\":%s}}}",three,bad_interval[i]);
    save_profile(profile,three,pr,ar);assert(code==400);
    assert(auto_sequence_snapshot(checked_names,ms)==3 && ms[0]==5000);
  }
  const char *legacy_profile="{\"active\":\"intervals\",\"order\":[\"intervals\"],\"list\":{\"intervals\":[]}}";
  assert(intervals_validate(legacy_profile,strlen(legacy_profile)));
  revisions(pr,ar);
  const char *reordered="[\"ProsperoMgr.elf\",\"kstuff-lite.elf\",\"shadowmountplus.elf\"]";
  snprintf(profile,sizeof(profile),"{\"active\":\"Default\",\"order\":[\"Default\",\"DNS\"],\"list\":{\"Default\":%s,\"DNS\":[\"nanodns.elf\"]},\"intervals\":{\"Default\":{\"kstuff-lite.elf\":5,\"ProsperoMgr.elf\":300},\"DNS\":{\"nanodns.elf\":3}}}",reordered);
  save_profile(profile,reordered,pr,ar);assert(code==200);
  assert(auto_sequence_snapshot(checked_names,ms)==3 && ms[0]==300000 && ms[1]==5000 && ms[2]==0);
  revisions(pr,ar);
  snprintf(profile,sizeof(profile),"{\"active\":\"DNS\",\"order\":[\"Default\",\"DNS\"],\"list\":{\"Default\":%s,\"DNS\":[\"nanodns.elf\"]},\"intervals\":{\"Default\":{\"kstuff-lite.elf\":5,\"ProsperoMgr.elf\":300},\"DNS\":{\"nanodns.elf\":3}}}",reordered);
  save_profile(profile,"[\"nanodns.elf\"]",pr,ar);assert(code==200);
  assert(auto_sequence_snapshot(checked_names,ms)==1 && ms[0]==3000);
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
