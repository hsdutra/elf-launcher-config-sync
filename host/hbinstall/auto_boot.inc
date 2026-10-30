/* One owner per server/boot. HTTP readers never dispatch work. All state,
 * including the cancellation/send admission boundary, is protected by mu.
 * Never hold mu while waiting for elfldr, sending or writing HTTP. Small
 * atomic journal snapshots are written under mu before send admission. */
static pthread_mutex_t auto_mu = PTHREAD_MUTEX_INITIALIZER;
static struct {
  int started, cancel, total, completed, delay_ms, admitted, ready, initialized;
  int64_t deadline;
  char session[80];
  char message[192];
  const char *phase;
  struct { char name[AUTO_NAME_MAX], detail[256]; const char *status; } items[AUTO_MAX];
} boot_auto = {.phase = "idle"};

#include "auto_journal.inc"

static int auto_claim(void) {
  auto_restore();
  pthread_mutex_lock(&auto_mu);
  if (boot_auto.started || !boot_auto.ready) {
    evlog("info", "AutoPayload claim skipped: session=%s phase=%s admitted=%d", boot_auto.session, boot_auto.phase, boot_auto.admitted);
    pthread_mutex_unlock(&auto_mu);
    return 0;
  }
  boot_auto.started = 1;
  boot_auto.cancel = boot_auto.total = boot_auto.completed = boot_auto.admitted = 0;
  boot_auto.deadline = 0;
  memset(boot_auto.items, 0, sizeof(boot_auto.items));
  boot_auto.phase = "preparing";
  boot_auto.message[0] = 0;
  pthread_mutex_unlock(&auto_mu);
  return 1;
}

static void auto_start_failed(void) {
  pthread_mutex_lock(&auto_mu);
  /* No worker existed, so this must not consume a legitimate execution. */
  boot_auto.started = 0;
  boot_auto.phase = "error";
  snprintf(boot_auto.message, sizeof(boot_auto.message), "Worker creation failed before any send; execution not consumed");
  pthread_mutex_unlock(&auto_mu);
}

static int auto_save_locked(void) {
  if (!write_boot_auto_done()) return 0;
  boot_auto.phase = "error";
  if (!boot_auto.admitted) boot_auto.started = 0;
  boot_auto.deadline = 0;
  snprintf(boot_auto.message, sizeof(boot_auto.message), "Cannot save current-boot execution journal; no further sends permitted");
  evlog("error", "%s", boot_auto.message);
  return -1;
}

static int auto_cancelled(void) {
  int cancelled;
  pthread_mutex_lock(&auto_mu);
  cancelled = boot_auto.cancel;
  pthread_mutex_unlock(&auto_mu);
  return cancelled;
}

static void auto_finish(void) {
  int i;
  pthread_mutex_lock(&auto_mu);
  for (i = 0; i < boot_auto.total; i++) {
    if (!strcmp(boot_auto.items[i].status, "pending")) {
      boot_auto.items[i].status = "cancelled";
      boot_auto.completed++;
    }
  }
  boot_auto.phase = boot_auto.cancel ? "cancelled" : "completed";
  boot_auto.deadline = 0;
  (void)auto_save_locked();
  pthread_mutex_unlock(&auto_mu);
}

static void run_headless_auto(const char *source) {
  char listed[AUTO_MAX][AUTO_NAME_MAX], use[AUTO_MAX][AUTO_NAME_MAX];
  char path[512], emsg[256];
  int intervals[AUTO_MAX], i, n, e, delay_ms = settings_auto_delay_ms();
  if (delay_ms < 0) {
    pthread_mutex_lock(&auto_mu);
    boot_auto.phase = "error"; boot_auto.started = 0;
    snprintf(boot_auto.message, sizeof(boot_auto.message), "Cannot read persisted AutoPayload delay; automatic sends blocked");
    pthread_mutex_unlock(&auto_mu);
    return;
  }
  n = auto_sequence_snapshot(listed, intervals);
  if (n < 0) {
    pthread_mutex_lock(&auto_mu);
    boot_auto.phase = "error"; boot_auto.started = 0;
    snprintf(boot_auto.message, sizeof(boot_auto.message), "Cannot read AutoPayload intervals; automatic sends blocked");
    pthread_mutex_unlock(&auto_mu);
    return;
  }
  e = existing_names(listed, n, use, AUTO_MAX, "auto.list");
  evlog("info", "AutoPayload source=%s listed=%d runnable=%d delay=%dms",
        source, n, e, delay_ms);
  pthread_mutex_lock(&auto_mu);
  boot_auto.total = n;
  boot_auto.delay_ms = delay_ms;
  for (i = 0; i < n; i++) {
    snprintf(boot_auto.items[i].name, AUTO_NAME_MAX, "%s", listed[i]);
    boot_auto.items[i].status = "pending";
  }
  boot_auto.phase = n ? "waiting" : "completed";
  boot_auto.deadline = n ? mono_ms() + delay_ms : 0;
  if (auto_save_locked()) { pthread_mutex_unlock(&auto_mu); return; }
  pthread_mutex_unlock(&auto_mu);
  /* Cancellable monotonic delay; an empty queue adds no wait. */
  while (n && !auto_cancelled()) {
    int64_t remaining;
    pthread_mutex_lock(&auto_mu);
    remaining = boot_auto.deadline - mono_ms();
    pthread_mutex_unlock(&auto_mu);
    if (remaining <= 0) break;
    usleep((unsigned int)(remaining > 50 ? 50 : remaining) * 1000);
  }
  for (i = 0; i < n && !auto_cancelled(); i++) {
    const char *why = NULL;
    int rc, acquired = 0;
    if (auto_disk_path_reason(listed[i], path, sizeof(path), &why)) {
      pthread_mutex_lock(&auto_mu);
      boot_auto.items[i].status = "skipped";
      snprintf(boot_auto.items[i].detail, 256, "%s", why ? why : "missing");
      boot_auto.completed++;
      if (auto_save_locked()) { pthread_mutex_unlock(&auto_mu); return; }
      pthread_mutex_unlock(&auto_mu);
      evlog("error", "AutoPayload skipped %s: %s", listed[i], why ? why : "missing");
      continue;
    }
    /* Waiting for another sender must remain cancellable. */
    while (!auto_cancelled() && !(acquired = send_lock_try())) usleep(50000);
    pthread_mutex_lock(&auto_mu);
    if (boot_auto.cancel) {
      pthread_mutex_unlock(&auto_mu);
      if (acquired) send_unlock();
      break;
    }
    boot_auto.phase = "loading";
    boot_auto.deadline = 0;
    boot_auto.items[i].status = "loading";
    /* Commit the admission BEFORE calling elfldr. A restart after this point
     * cannot establish the outcome and must not replay the queue. */
    {
      int previous = boot_auto.admitted;
      boot_auto.admitted = 1;
      if (auto_save_locked()) {
        boot_auto.admitted = previous;
        if (!previous) boot_auto.started = 0;
        pthread_mutex_unlock(&auto_mu);
        send_unlock();
        return;
      }
    }
    pthread_mutex_unlock(&auto_mu);
    /* Admission above is the start boundary. Cancellation never kills a send
     * already admitted, nor interrupts a running payload. */
    rc = elfldr_send_path(path, "", emsg, sizeof(emsg), 1000);
    send_unlock();
    pthread_mutex_lock(&auto_mu);
    boot_auto.items[i].status = rc == SEND_OK ? "success" : "error";
    snprintf(boot_auto.items[i].detail, 256, "%s", emsg[0] ? emsg : send_err_text(rc));
    boot_auto.completed++;
    if (auto_save_locked()) { pthread_mutex_unlock(&auto_mu); return; }
    pthread_mutex_unlock(&auto_mu);
    evlog(rc == SEND_OK ? "load" : "error", "AutoPayload %s: %s; rc=%d reply=%.100s; execution unconfirmed",
          listed[i], rc == SEND_OK ? "sent to elfldr" : "send failed", rc, emsg);
    /* Start the interval only after the transport result. Never wait after
     * the last item; values remain attached to filenames in profiles.json. */
    if (i+1<n && intervals[i]>0 && !auto_cancelled()) {
      pthread_mutex_lock(&auto_mu);
      boot_auto.phase="interval";
      boot_auto.deadline=mono_ms()+intervals[i];
      if (auto_save_locked()) { pthread_mutex_unlock(&auto_mu); return; }
      pthread_mutex_unlock(&auto_mu);
      while (!auto_cancelled()) {
        int64_t remaining;
        pthread_mutex_lock(&auto_mu);
        remaining=boot_auto.deadline-mono_ms();
        pthread_mutex_unlock(&auto_mu);
        if(remaining<=0)break;
        usleep((unsigned int)(remaining>50?50:remaining)*1000);
      }
    }
  }
  auto_finish();
}

static void handle_auto_status(int c) {
  /* Heap buffer avoids a large HTTP stack frame. Escape all disk/loader text. */
  size_t cap = AUTO_MAX * (AUTO_NAME_MAX * 6 + 256 * 6 + 180) + 1024, k;
  char *json = malloc(cap), name[AUTO_NAME_MAX * 6 + 1], detail[256 * 6 + 1], message[384];
  int i, remaining;
  int64_t now;
  if (!json) { send_json_code(c, 500, "{\"ok\":false}"); return; }
  pthread_mutex_lock(&auto_mu);
  now = mono_ms();
  remaining = boot_auto.deadline > now ? (int)(boot_auto.deadline - now) : 0;
  json_escape_name(boot_auto.message, message, sizeof(message));
  k = (size_t)snprintf(json, cap,
      "{\"ok\":true,\"session\":\"%s\",\"phase\":\"%s\",\"delay_remaining\":%.3f,"
      "\"delay_total\":%.3f,\"total\":%d,\"completed\":%d,\"cancel_requested\":%s,"
      "\"done\":%s,\"consumed\":%s,\"message\":\"%s\",\"confirmation\":\"transport-only\",\"items\":[",
      boot_auto.session, boot_auto.phase, remaining / 1000.0, boot_auto.delay_ms / 1000.0,
      boot_auto.total, boot_auto.completed, boot_auto.cancel ? "true" : "false",
      boot_auto_done() ? "true" : "false", boot_auto.started ? "true" : "false", message);
  for (i = 0; i < boot_auto.total; i++) {
    json_escape_name(boot_auto.items[i].name, name, sizeof(name));
    json_escape_name(boot_auto.items[i].detail, detail, sizeof(detail));
    k += (size_t)snprintf(json + k, cap - k,
        "%s{\"name\":\"%s\",\"status\":\"%s\",\"detail\":\"%s\",\"execution_confirmed\":false}",
        i ? "," : "", name, boot_auto.items[i].status, detail);
  }
  pthread_mutex_unlock(&auto_mu);
  k += (size_t)snprintf(json + k, cap - k, "]}");
  send_json(c, 1, json, k);
  free(json);
}

static void handle_auto_cancel(int c, const char *qs, int is_post) {
  char session[80];
  if (!is_post) { send_json_code(c, 405, "{\"ok\":false,\"message\":\"use POST\"}"); return; }
  session[0] = 0;
  if (qs) qget(qs, "session", session, sizeof(session));
  pthread_mutex_lock(&auto_mu);
  if (!session[0] || strcmp(session, boot_auto.session)) {
    pthread_mutex_unlock(&auto_mu);
    send_json_code(c, 409, "{\"ok\":false,\"message\":\"stale session\"}");
    return;
  }
  if (!strcmp(boot_auto.phase, "preparing") || !strcmp(boot_auto.phase, "waiting") || !strcmp(boot_auto.phase, "loading") || !strcmp(boot_auto.phase, "interval"))
    boot_auto.cancel = 1;
  boot_auto.deadline = 0;
  /* The worker will persist the final snapshot; an accepted cancellation
   * before any admission must also survive a server replacement. */
  if (boot_auto.cancel && !boot_auto.admitted) {
    boot_auto.phase = "cancelled";
    (void)auto_save_locked();
  }
  pthread_mutex_unlock(&auto_mu);
  evlog("info", "AutoPayload cancellation requested; admitted send is allowed to finish");
  send_json_code(c, 200, "{\"ok\":true}");
}

static void handle_boot_auto(int c) {
  char json[384];
  int n;
  pthread_mutex_lock(&auto_mu);
  n = snprintf(json, sizeof(json), "{\"ok\":true,\"done\":%s,\"session\":\"%s\",\"phase\":\"%s\",\"consumed\":%s}",
      boot_auto_done() ? "true" : "false", boot_auto.session, boot_auto.phase, boot_auto.started ? "true" : "false");
  pthread_mutex_unlock(&auto_mu);
  send_json(c, 1, json, (size_t)n);
}
