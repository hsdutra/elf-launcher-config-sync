/* Boot identity and execution journal live on PS5 tmpfs, never on /data.
 * The mount type is checked at runtime. A reboot destroys both, independent
 * of uptime, RTC/NTP changes and sysctlbyname availability. /data is audit only.
 * Called after owning :1000; journal writes are serialized by auto_mu. */
#ifndef AUTO_BOOT_DIR
#define AUTO_BOOT_DIR "/system_tmp/elf-launcher"
#endif
#define AUTO_BOOT_ID_PATH AUTO_BOOT_DIR "/boot-id"
#define AUTO_BOOT_STATE_PATH AUTO_BOOT_DIR "/auto-state-v3"
static char auto_boot_id[33];

typedef struct {
  char magic[8], boot_id[33], session[80], phase[16];
  int total, completed, admitted;
  struct { char name[AUTO_NAME_MAX], status[16], detail[256]; } items[AUTO_MAX];
} auto_journal_t;

static int auto_boot_identity(void) {
  unsigned char random[16];
  char token[33] = {0}, tmp[256];
  FILE *f;
  int i, rc, extra;
  if (!auto_tmp_is_volatile() || (mkdir(AUTO_BOOT_DIR, 0700) && errno != EEXIST))
    return -1;
  f = fopen(AUTO_BOOT_ID_PATH, "rb");
  if (!f && errno == ENOENT) {
    /* Publish a complete token with link(), so competing initializers cannot
     * see a partially written ID or replace the winning boot identity. */
    arc4random_buf(random, sizeof(random));
    for (i = 0; i < 16; i++) snprintf(token + i * 2, 3, "%02x", random[i]);
    snprintf(tmp, sizeof(tmp), AUTO_BOOT_DIR "/boot-id.%d.%s", (int)getpid(), token);
    f = fopen(tmp, "wb");
    if (!f) return -1;
    rc = fwrite(token, 1, 32, f) == 32 ? 0 : -1;
    if (fclose(f)) rc = -1;
    if (!rc && link(tmp, AUTO_BOOT_ID_PATH) && errno != EEXIST) rc = -1;
    unlink(tmp);
    if (rc) return -1;
    f = fopen(AUTO_BOOT_ID_PATH, "rb");
  }
  if (!f) return -1;
  rc = fread(token, 1, 32, f) == 32 ? 0 : -1;
  extra = fgetc(f);
  if (ferror(f) || extra != EOF) rc = -1;
  fclose(f);
  token[32] = 0;
  for (i = 0; i < 32; i++) if (!strchr("0123456789abcdef", token[i]) || !token[i]) rc = -1;
  if (rc) return -1; /* Never regenerate a damaged current-boot identity. */
  memcpy(auto_boot_id, token, sizeof(auto_boot_id));
  return 0;
}

static const char *auto_literal(const char *s, int phase) {
  static const char *phases[] = {"preparing", "waiting", "interval", "loading", "completed", "cancelled", "error", NULL};
  static const char *states[] = {"pending", "loading", "success", "error", "skipped", "cancelled", NULL};
  const char **list = phase ? phases : states;
  int i;
  for (i = 0; list[i]; i++) if (!strcmp(s, list[i])) return list[i];
  return NULL;
}

/* Caller holds auto_mu. No file can make an active/idle state report done. */
static int boot_auto_done(void) {
  return !strcmp(boot_auto.phase, "completed") || !strcmp(boot_auto.phase, "cancelled");
}

/* The persistent legacy marker is deliberately never read as execution proof.
 * Replace it with explicit, versioned diagnostics, never an uptime heuristic. */
static void auto_audit_locked(void) {
  char audit[320];
  int n = snprintf(audit, sizeof(audit),
      "ELFAUTO3 boot=%s session=%s phase=%s done=%d admitted=%d total=%d completed=%d\n",
      auto_boot_id, boot_auto.session, boot_auto.phase, boot_auto_done(),
      boot_auto.admitted, boot_auto.total, boot_auto.completed);
  if (write_atomic_under_data(BOOT_AUTO_DONE_PATH, (const uint8_t *)audit, (size_t)n))
    evlog("error", "AutoPayload diagnostic marker write failed; tmpfs journal remains authoritative");
}

static int write_boot_auto_done(void) {
  auto_journal_t *r = calloc(1, sizeof(*r));
  FILE *f;
  int i, rc;
  if (!r) return -1;
  memcpy(r->magic, "ELFAUTO3", 8);
  memcpy(r->boot_id, auto_boot_id, sizeof(r->boot_id));
  snprintf(r->session, sizeof(r->session), "%s", boot_auto.session);
  snprintf(r->phase, sizeof(r->phase), "%s", boot_auto.phase);
  r->total = boot_auto.total; r->completed = boot_auto.completed; r->admitted = boot_auto.admitted;
  for (i = 0; i < r->total; i++) {
    memcpy(r->items[i].name, boot_auto.items[i].name, AUTO_NAME_MAX);
    memcpy(r->items[i].detail, boot_auto.items[i].detail, 256);
    snprintf(r->items[i].status, 16, "%s", boot_auto.items[i].status);
  }
  f = fopen(AUTO_BOOT_STATE_PATH ".tmp", "wb");
  if (!f) { free(r); return -1; }
  rc = fwrite(r, 1, sizeof(*r), f) == sizeof(*r) ? 0 : -1;
  if (fclose(f)) rc = -1;
  free(r);
  if (!rc && rename(AUTO_BOOT_STATE_PATH ".tmp", AUTO_BOOT_STATE_PATH)) rc = -1;
  if (rc) unlink(AUTO_BOOT_STATE_PATH ".tmp");
  if (!rc) auto_audit_locked();
  return rc;
}

static void auto_restore(void) {
  auto_journal_t *r;
  FILE *f;
  int i, valid, extra;
  pthread_mutex_lock(&auto_mu);
  if (boot_auto.initialized) { pthread_mutex_unlock(&auto_mu); return; }
  boot_auto.initialized = 1;
  if (auto_boot_identity()) goto unavailable;
  snprintf(boot_auto.session, sizeof(boot_auto.session), "%s", auto_boot_id);
  f = fopen(AUTO_BOOT_STATE_PATH, "rb");
  if (!f) {
    if (errno != ENOENT) goto unavailable;
    boot_auto.ready = 1;
    evlog("info", "AutoPayload fresh boot session=%s; persistent legacy marker is not execution proof", auto_boot_id);
    pthread_mutex_unlock(&auto_mu);
    return;
  }
  r = calloc(1, sizeof(*r));
  if (!r) { fclose(f); goto unavailable; }
  valid = fread(r, 1, sizeof(*r), f) == sizeof(*r);
  extra = fgetc(f);
  if (ferror(f) || extra != EOF) valid = 0;
  fclose(f);
  valid = valid && !memcmp(r->magic, "ELFAUTO3", 8) &&
      !memcmp(r->boot_id, auto_boot_id, sizeof(r->boot_id)) &&
      memchr(r->session, 0, sizeof(r->session)) && !strcmp(r->session, auto_boot_id) &&
      memchr(r->phase, 0, sizeof(r->phase)) && auto_literal(r->phase, 1) &&
      r->total >= 0 && r->total <= AUTO_MAX && r->completed >= 0 && r->completed <= r->total &&
      (r->admitted == 0 || r->admitted == 1);
  for (i = 0; valid && i < r->total; i++)
    valid = memchr(r->items[i].name, 0, AUTO_NAME_MAX) &&
        memchr(r->items[i].detail, 0, 256) && memchr(r->items[i].status, 0, 16) && auto_literal(r->items[i].status, 0);
  if (valid) {
    int processed = 0;
    for (i = 0; i < r->total; i++)
      if (strcmp(r->items[i].status, "pending") && strcmp(r->items[i].status, "loading")) processed++;
    if (processed != r->completed || (!strcmp(r->phase, "completed") && processed != r->total)) valid = 0;
  }
  if (!valid) { free(r); goto unavailable; }
  /* A worker interrupted before its first admission is safe to restart.
   * A completed/cancelled run or any attempted send must never be replayed. */
  if (!r->admitted && strcmp(r->phase, "completed") && strcmp(r->phase, "cancelled")) {
    free(r); boot_auto.ready = 1;
    evlog("info", "AutoPayload resumes before first send; session=%s", auto_boot_id);
    pthread_mutex_unlock(&auto_mu);
    return;
  }
  boot_auto.ready = boot_auto.started = 1;
  boot_auto.admitted = r->admitted;
  boot_auto.total = r->total; boot_auto.completed = r->completed;
  boot_auto.phase = auto_literal(r->phase, 1);
  for (i = 0; i < r->total; i++) {
    memcpy(boot_auto.items[i].name, r->items[i].name, AUTO_NAME_MAX);
    memcpy(boot_auto.items[i].detail, r->items[i].detail, 256);
    boot_auto.items[i].status = auto_literal(r->items[i].status, 0);
    if (!strcmp(boot_auto.phase, "cancelled") && !strcmp(boot_auto.items[i].status, "pending")) {
      boot_auto.items[i].status = "cancelled";
      boot_auto.completed++;
    }
  }
  free(r);
  if (!boot_auto_done()) {
    boot_auto.phase = "error";
    snprintf(boot_auto.message, sizeof(boot_auto.message), "Previous worker interrupted after send admission; execution outcome unknown; automatic replay blocked");
    for (i = 0; i < boot_auto.total; i++) {
      if (!strcmp(boot_auto.items[i].status, "loading")) {
        boot_auto.items[i].status = "error";
        snprintf(boot_auto.items[i].detail, 256, "Execution outcome unknown after launcher interruption");
        boot_auto.completed++;
      } else if (!strcmp(boot_auto.items[i].status, "pending")) {
        boot_auto.items[i].status = "skipped";
        snprintf(boot_auto.items[i].detail, 256, "Not replayed after launcher interruption");
        boot_auto.completed++;
      }
    }
  }
  evlog("info", "AutoPayload restored session=%s phase=%s admitted=%d", auto_boot_id, boot_auto.phase, boot_auto.admitted);
  pthread_mutex_unlock(&auto_mu);
  return;
unavailable:
  boot_auto.phase = "error";
  boot_auto.started = 1;
  snprintf(boot_auto.message, sizeof(boot_auto.message), "Boot identity or tmpfs journal unavailable/invalid; no automatic sends permitted");
  evlog("error", "%s", boot_auto.message);
  pthread_mutex_unlock(&auto_mu);
}
