const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const test = require('node:test');
const vm = require('node:vm');

const root = path.resolve(__dirname, '..');
const htmlPath = path.join(root, 'index.html');
const embeddedPath = path.join(root, 'host', 'hbinstall', 'webapp', 'index.html');
const mainPath = path.join(root, 'host', 'hbinstall', 'main.c');
const html = fs.readFileSync(htmlPath, 'utf8');
const mainC = fs.readFileSync(mainPath, 'utf8');
const emptyRevision = crypto.createHash('sha256').update('').digest('hex');
const activeNames = ['kstuff-lite.elf', 'shadowmountplus.elf', 'ProsperoMgr.elf'];

function storage(initial) {
  const values = Object.assign({}, initial || {});
  const result = {
    writes: 0,
    getItem(key) { return Object.prototype.hasOwnProperty.call(values, key) ? values[key] : null; },
    setItem(key, value) { this.writes++; values[key] = String(value); },
    removeItem(key) { this.writes++; delete values[key]; }
  };
  return result;
}

function makeServer(options) {
  options = options || {};
  const settings = options.settings || {
    schema: 1, lang: 'pt', theme: 'sky', autoDelay: options.settingsExists===false?3:10,
    openAfterJb: true, pins: ['ProsperoMgr.elf'], recent: []
  };
  return {
    settings: {
      ok: true,
      exists: options.settingsExists !== false,
      revision: options.settingsRevision || (options.settingsExists === false ? emptyRevision : 'settings-r1'),
      settings: JSON.parse(JSON.stringify(settings))
    },
    auto: {ok: true, names: (options.names || activeNames).slice(), revision: options.autoRevision || 'auto-r1'},
    profiles: {
      ok: true,
      revision: options.profileRevision || 'profiles-r1',
      profiles: {
        active: 'HSDUTRA',
        order: ['HSDUTRA'],
        list: {HSDUTRA: (options.names || activeNames).slice()}
      }
    },
    failSettingsWrites: false
  };
}

function makeClient(options) {
  options = options || {};
  const server = options.server || makeServer(options);
  const requests = [];
  const status = [];
  const localStorage = storage(options.localStorage);
  const sessionStorage = storage(options.sessionStorage);
  function response(body, code) {
    code = code || 200;
    const wire=JSON.stringify(body);
    return {ok: code >= 200 && code < 300, status: code, json: function(){ return Promise.resolve(JSON.parse(wire)); }};
  }
  function fetchMock(url, request) {
    request = request || {};
    requests.push({url: url, request: request});
    const method = request.method || 'GET';
    if (method === 'GET' && url === '/settings') return Promise.resolve(response(server.settings));
    if (method === 'GET' && url === '/auto-list') return Promise.resolve(response(server.auto));
    if (method === 'GET' && url === '/profiles') return Promise.resolve(response(server.profiles));
    if (method === 'POST' && url === '/settings') {
      if (server.failSettingsWrites) return Promise.resolve(response({ok:false,message:'disk full'}, 500));
      if (request.headers['If-Match'] !== server.settings.revision) {
        return Promise.resolve(response({ok:false, message:'revision conflict'}, 409));
      }
      server.settings.settings = JSON.parse(request.body);
      server.settings.exists = true;
      server.settings.revision = 'settings-r' + (Number(String(server.settings.revision).replace(/\D/g, '')) + 1 || 2);
      return Promise.resolve(response({ok:true, revision:server.settings.revision}));
    }
    if (method === 'POST' && url === '/profiles') {
      if(server.failProfileWrites) return Promise.resolve(response({ok:false,message:'disk full'},500));
      if (request.headers['If-Match'] !== server.profiles.revision ||
          request.headers['X-Auto-Revision'] !== server.auto.revision) {
        return Promise.resolve(response({ok:false, conflict:true}, 409));
      }
      server.profiles.profiles = JSON.parse(request.body);
      server.auto.names = JSON.parse(request.headers['X-Auto-List']);
      server.profiles.revision += '+1';
      server.auto.revision += '+1';
      return Promise.resolve(response({ok:true, revision:server.profiles.revision, autoRevision:server.auto.revision}));
    }
    return Promise.resolve(response({ok:false, message:'unexpected request'}, 404));
  }
  const context = {
    fetch: fetchMock,
    localStorage: localStorage,
    sessionStorage: sessionStorage,
    __isHost: options.isHost !== false,
    __isPs5: !!options.isPs5,
    I18N: {en:{}, pt:{}},
    currentFolder: null,
    homeEl: null,
    setStatus: function(message){ status.push(String(message)); },
    applyStaticI18n: function(){},
    applyTheme: function(){},
    paintBrowserChoice: function(){},
    paintAutoDelayChoice: function(){},
    paintPins: function(){},
    paintRecent: function(){},
    showAutoPayloadFolder: function(){},
    showHome: function(){},
    renderNav: function(){},
    qkOpen: function(){ return false; },
    qkPaint: function(){},
    profRepaint: function(){},
    setInterval: function(){ return 1; },
    clearTimeout: function(){},
    document:{getElementById:function(){return null;}},
    t:function(key){return key;},
    setTimeout: function(){ return 1; }
  };
  const setup = `
    var DEVICE_SETTINGS=null, SETTINGS_REV='', SETTINGS_READY=false, SETTINGS_EXISTS=false;
    var AUTO_NAMES=[], AUTO_REV='', PROFILE_REV='', PROFILE_QUEUE=[], PROFILE_BUSY=false, PERSISTENT_READY=false;
    var SETTINGS_QUEUE=[], SETTINGS_BUSY=false, PERSISTENT_POLL=0, PERSISTENT_EPOCH=0, LANG='en';
    var PIN_KEY='ps5elfs-pins', RECENT_KEY='ps5elfs-recent';
    function isConsoleHost(){ return __isHost; }
    function onPs5Browser(){ return __isPs5; }
    function defaultDeviceSettings(){ return {schema:1,lang:'en',theme:'pearl',autoDelay:3,openAfterJb:false,pins:[],recent:[]}; }
    function normDelay(v){ var n=parseFloat(v); if(isNaN(n)) return 3; return Math.round(Math.min(10,Math.max(0,n))*2)/2; }
    function themeById(id){ return ['pearl','gold','black','gray','white','lavender','sky'].indexOf(id)>=0 ? {id:id} : null; }
    function profValid(p){ return !!(p && p.list && p.order && p.order.length && p.list[p.active]); }
    function profDefault(){ var list={}; list.Default=AUTO_NAMES.slice(); return {active:'Default',order:['Default'],list:list}; }
    function profRepaint(){}
  `;
  const start = html.indexOf('function persistentGet(path){');
  const end = html.indexOf('/* Exact category names used by our payloads.json', start);
  assert.notEqual(start, -1, 'persistent client block exists');
  assert.notEqual(end, -1, 'persistent client block terminator exists');
  vm.runInNewContext(setup + html.slice(start, end), context, {filename:'index.html'});
  for(const pair of [['function loadAuto(){','function setAutoBox('],['function autoDelaySec(){','function paintAutoDelayChoice('],['function profSave(','function profChipsHtml('],['function paintAutoRow(','function rowFromTarget(']]){
    vm.runInNewContext(html.slice(html.indexOf(pair[0]),html.indexOf(pair[1],html.indexOf(pair[0]))),context);
  }
  context.profSaveT=0;
  return {context, requests, status, server, localStorage, sessionStorage};
}

function loaded(done) {
  return new Promise(function(resolve){ done(resolve); });
}

test('all inline scripts parse and the embedded HTML matches the source', function(){
  const embedded = fs.readFileSync(embeddedPath, 'utf8');
  assert.equal(embedded, html);
  const scripts = html.match(/<script\b[^>]*>([\s\S]*?)<\/script>/gi) || [];
  assert.ok(scripts.length > 0);
  scripts.forEach(function(tag, index){
    const body = tag.replace(/^<script\b[^>]*>/i, '').replace(/<\/script>$/i, '');
    new vm.Script(body, {filename:'index.html script ' + index});
  });
});

test('opening from PC and PS5 reads the same PS5 configuration without browser writes', async function(){
  const server = makeServer();
  const pc = makeClient({server:server, isHost:true, isPs5:false, localStorage:{'ps5elfs-auto':'{}','ps5elfs-theme':'black'}});
  const ps5 = makeClient({server:server, isHost:true, isPs5:true, localStorage:{'ps5elfs-auto':'{}','ps5elfs-theme':'gray'}});
  await loaded(function(done){ pc.context.loadPersistentState(done); });
  await loaded(function(done){ ps5.context.loadPersistentState(done); });
  assert.deepEqual(Array.from(pc.context.AUTO_NAMES), activeNames);
  assert.deepEqual(Array.from(ps5.context.AUTO_NAMES), activeNames);
  assert.equal(pc.context.DEVICE_SETTINGS.theme, 'sky');
  assert.equal(ps5.context.DEVICE_SETTINGS.theme, 'sky');
  assert.equal(pc.localStorage.writes + ps5.localStorage.writes, 0);
  assert.equal(pc.requests.filter(function(x){ return x.request.method==='POST'; }).length, 0);
  assert.equal(ps5.requests.filter(function(x){ return x.request.method==='POST'; }).length, 0);
});

test('reopening the page performs reads only and keeps the ordered auto.list', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  await loaded(function(done){ client.context.loadPersistentState(done); });
  assert.deepEqual(Array.from(client.context.AUTO_NAMES), activeNames);
  assert.equal(client.server.auto.names.join(','), activeNames.join(','));
  assert.equal(client.requests.filter(function(x){ return x.request.method==='POST'; }).length, 0);
});

test('an empty PC localStorage cannot initialize or erase server settings', async function(){
  const server = makeServer({settingsExists:false});
  const client = makeClient({server:server, isHost:true, isPs5:false, localStorage:{'ps5elfs-auto':'{}','ps5elfs-theme':'black'}});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  assert.deepEqual(Array.from(client.context.AUTO_NAMES), activeNames);
  assert.equal(server.settings.exists, false);
  assert.equal(client.requests.filter(function(x){ return x.request.method==='POST'; }).length, 0);
  assert.equal(client.localStorage.writes, 0);
});

test('opening on PS5 never initializes missing settings from stale browser values', async function(){
  const server = makeServer({settingsExists:false});
  const legacy = {
    'ps5elfs-lang':'pt', 'ps5elfs-theme':'black', 'ps5elfs-auto-delay':'10',
    'ps5elfs-pins':'["ProsperoMgr.elf"]', 'ps5elfs-recent':'[]'
  };
  const client = makeClient({server:server, isHost:true, isPs5:true, localStorage:legacy});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  const post = client.requests.find(function(x){ return x.url==='/settings' && x.request.method==='POST'; });
  assert.equal(post,undefined);
  assert.equal(server.settings.settings.lang, 'pt');
  assert.equal(server.settings.settings.theme, 'sky');
  assert.equal(server.settings.settings.autoDelay, 3);
  assert.equal(server.settings.exists,false);
  assert.deepEqual(server.settings.settings.pins, ['ProsperoMgr.elf']);
  assert.deepEqual(Array.from(client.context.AUTO_NAMES), activeNames);
});

test('explicit settings changes use POST with If-Match and preserve unrelated settings', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  const saved = await loaded(function(done){ client.context.saveDeviceSettings({theme:'black'}, done); });
  assert.equal(saved, true);
  const post = client.requests.find(function(x){ return x.url==='/settings' && x.request.method==='POST'; });
  assert.ok(post);
  assert.equal(post.request.headers['If-Match'], 'settings-r1');
  assert.equal(JSON.parse(post.request.body).theme, 'black');
  assert.equal(JSON.parse(post.request.body).lang, 'pt');
  assert.equal(client.localStorage.writes, 0);
});

test('rapid settings edits are serialized using each returned revision', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  const first = loaded(function(done){ client.context.saveDeviceSettings({theme:'black'}, done); });
  const second = loaded(function(done){ client.context.saveDeviceSettings({lang:'ru'}, done); });
  assert.deepEqual(await Promise.all([first, second]), [true, true]);
  const posts = client.requests.filter(function(x){ return x.url==='/settings' && x.request.method==='POST'; });
  assert.equal(posts.length, 2);
  assert.equal(posts[0].request.headers['If-Match'], 'settings-r1');
  assert.equal(posts[1].request.headers['If-Match'], 'settings-r2');
  assert.equal(client.server.settings.settings.theme, 'black');
  assert.equal(client.server.settings.settings.lang, 'ru');
});

test('rapid recent and pin mutations compose against the latest settings snapshot', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  const recentOne = loaded(function(done){
    client.context.updateDeviceSettings(function(next){ next.recent.unshift({name:'one.elf',ok:true,at:1}); }, done);
  });
  const recentTwo = loaded(function(done){
    client.context.updateDeviceSettings(function(next){ next.recent.unshift({name:'two.elf',ok:true,at:2}); }, done);
  });
  const pinOne = loaded(function(done){
    client.context.updateDeviceSettings(function(next){ next.pins.push('one.elf'); }, done);
  });
  const pinTwo = loaded(function(done){
    client.context.updateDeviceSettings(function(next){ next.pins.push('two.elf'); }, done);
  });
  assert.deepEqual(await Promise.all([recentOne, recentTwo, pinOne, pinTwo]), [true,true,true,true]);
  assert.deepEqual(client.server.settings.settings.recent.map(function(x){ return x.name; }), ['two.elf','one.elf']);
  assert.deepEqual(client.server.settings.settings.pins, ['ProsperoMgr.elf','one.elf','two.elf']);
});

test('a PS5 settings change appears in the PC client on its next poll', async function(){
  const server = makeServer();
  const pc = makeClient({server:server, isHost:true, isPs5:false, localStorage:{'ps5elfs-pins':'[]'}});
  const ps5 = makeClient({server:server, isHost:true, isPs5:true, localStorage:{'ps5elfs-pins':'["old-local"]'}});
  await loaded(function(done){ pc.context.loadPersistentState(done); });
  await loaded(function(done){ ps5.context.loadPersistentState(done); });
  const saved = await loaded(function(done){ ps5.context.saveDeviceSettings({pins:['shadowmountplus.elf']}, done); });
  assert.equal(saved, true);
  pc.context.refreshPersistentState();
  await new Promise(function(resolve){ setImmediate(resolve); });
  assert.deepEqual(Array.from(pc.context.DEVICE_SETTINGS.pins), ['shadowmountplus.elf']);
  assert.deepEqual(Array.from(server.settings.settings.pins), ['shadowmountplus.elf']);
  assert.equal(pc.localStorage.writes + ps5.localStorage.writes, 0);
});

test('a settings write failure preserves the prior server value and is visible', async function(){
  const server = makeServer();
  const client = makeClient({server:server, isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  const previous = server.settings.settings.theme;
  server.failSettingsWrites = true;
  const saved = await loaded(function(done){ client.context.saveDeviceSettings({theme:'black'}, done); });
  assert.equal(saved, false);
  await new Promise(function(resolve){ setImmediate(resolve); });
  assert.equal(server.settings.settings.theme, previous);
  assert.equal(client.context.DEVICE_SETTINGS.theme, previous);
  assert.ok(client.status.some(function(x){ return x.indexOf('Settings not saved:')===0; }));
});

test('profile writes submit both revisions and retain AutoPayload order', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  const names = ['ProsperoMgr.elf', 'kstuff-lite.elf', 'shadowmountplus.elf'];
  client.context.AUTO_NAMES = names.slice();
  client.context.PROF.list[client.context.PROF.active] = names.slice();
  const saved = await loaded(function(done){ client.context.persistProfilesAndAuto(done); });
  assert.equal(saved, true);
  const post = client.requests.find(function(x){ return x.url==='/profiles' && x.request.method==='POST'; });
  assert.ok(post);
  assert.equal(post.request.headers['If-Match'], 'profiles-r1');
  assert.equal(post.request.headers['X-Auto-Revision'], 'auto-r1');
  assert.deepEqual(JSON.parse(post.request.headers['X-Auto-List']), names);
  assert.deepEqual(client.server.auto.names, names);
});

test('rapid profile edits are queued against successive auto/profile revisions', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  const firstNames = ['kstuff-lite.elf'];
  client.context.AUTO_NAMES = firstNames.slice();
  client.context.PROF.list.HSDUTRA = firstNames.slice();
  const first = loaded(function(done){ client.context.persistProfilesAndAuto(done); });
  const nextNames = ['kstuff-lite.elf', 'ProsperoMgr.elf'];
  client.context.AUTO_NAMES = nextNames.slice();
  client.context.PROF.list.HSDUTRA = nextNames.slice();
  const second = loaded(function(done){ client.context.persistProfilesAndAuto(done); });
  assert.deepEqual(await Promise.all([first, second]), [true, true]);
  const posts = client.requests.filter(function(x){ return x.url==='/profiles' && x.request.method==='POST'; });
  assert.equal(posts.length, 2);
  assert.equal(posts[1].request.headers['If-Match'], 'profiles-r1+1');
  assert.equal(posts[1].request.headers['X-Auto-Revision'], 'auto-r1+1');
  assert.deepEqual(client.server.auto.names, nextNames);
});

test('an explicit empty AutoPayload list is saved, not confused with an empty browser', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  client.context.AUTO_NAMES=[];
  client.context.PROF.list[client.context.PROF.active]=[];
  const saved = await loaded(function(done){ client.context.persistProfilesAndAuto(done); });
  assert.equal(saved, true);
  assert.deepEqual(client.server.auto.names, []);
  assert.deepEqual(client.server.profiles.profiles.list.HSDUTRA, []);
});

test('a concurrent profile change is reported and reloaded instead of overwritten', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  client.server.auto.names=['shadowmountplus.elf'];
  client.server.auto.revision='auto-r2';
  client.server.profiles.profiles.list.HSDUTRA=['shadowmountplus.elf'];
  client.server.profiles.revision='profiles-r2';
  client.context.AUTO_NAMES=['kstuff-lite.elf'];
  client.context.PROF.list.HSDUTRA=['kstuff-lite.elf'];
  const saved = await loaded(function(done){ client.context.persistProfilesAndAuto(done); });
  assert.equal(saved, false);
  assert.deepEqual(client.server.auto.names, ['shadowmountplus.elf']);
  assert.deepEqual(Array.from(client.context.AUTO_NAMES), ['shadowmountplus.elf']);
});

test('a settings conflict keeps the other browser value and refreshes the client', async function(){
  const client = makeClient({isHost:true, isPs5:false});
  await loaded(function(done){ client.context.loadPersistentState(done); });
  client.server.settings.settings.lang='pt';
  client.server.settings.revision='settings-r2';
  const saved = await loaded(function(done){ client.context.saveDeviceSettings({theme:'black'}, done); });
  assert.equal(saved, false);
  await new Promise(function(resolve){ setImmediate(resolve); });
  assert.equal(client.server.settings.settings.lang, 'pt');
  assert.equal(client.context.DEVICE_SETTINGS.lang, 'pt');
  assert.ok(client.status.some(function(x){ return x.indexOf('Settings not saved:')===0; }));
});

test('C rejects legacy destructive GET and exposes conditional atomic writes', function(){
  const getGuard = mainC.match(/if \(qs && strstr\(qs, "names="\)\) \{\s*send_json_code\(c, 405,[\s\S]{0,140}?\}/);
  assert.ok(getGuard, 'GET /auto-list?names= is rejected');
  assert.ok(mainC.includes('\\"count\\":%d,\\"revision\\":\\"%s\\",\\"names\\":['));
  assert.match(mainC, /If-Match revision required/);
  assert.match(mainC, /write_atomic_under_data\(AUTO_LIST_PATH/);
  assert.match(mainC, /X-Auto-Revision/);
  assert.match(mainC, /settings.json is invalid/);
});

test('NanoDNS row stays confirmed until ACK, persists four names, then removes on explicit toggle', async()=>{
  const c=makeClient({}),ctx=c.context;await loaded(cb=>ctx.loadPersistentState(cb));
  const original=ctx.fetch;let release,posted;
  ctx.fetch=(url,request)=>request&&request.method==='POST'?new Promise(resolve=>{posted={url,request};release=()=>original(url,request).then(resolve);}):original(url,request);
  const attrs={'data-name':'nanodns.elf'},tag={textContent:''};
  const select={setAttribute:(k,v)=>{tag[k]=v;}};
  const row={parentNode:{getAttribute:k=>attrs[k]},className:'elf nom autorow',querySelector:selector=>selector==='.auto-select'?select:tag,getAttribute:k=>attrs[k],setAttribute:(k,v)=>attrs[k]=v,removeAttribute:k=>delete attrs[k]};
  ctx.autoRowToggle(row);
  assert.equal(ctx.AUTO_NAMES.length,3);assert.equal(ctx.PROF.list.HSDUTRA.length,3);
  assert.ok(!row.className.includes('ison'));assert.equal(attrs['aria-busy'],'true');
  assert.equal(posted.url,'/profiles');assert.equal(posted.request.headers['X-ELFL'],'1');
  assert.deepEqual(JSON.parse(posted.request.headers['X-Auto-List']),activeNames.concat('nanodns.elf'));
  release();await new Promise(setImmediate);
  assert.equal(ctx.AUTO_NAMES.length,4);assert.equal(c.server.auto.names.length,4);assert.ok(row.className.includes('ison'));
  assert.equal(tag['aria-checked'],'true');
  ctx.fetch=original;
  assert.equal(await loaded(cb=>ctx.setAuto('nanodns.elf',false,cb)),true);
  assert.deepEqual(c.server.auto.names,activeNames);
  assert.deepEqual(c.server.profiles.profiles.list.HSDUTRA,activeNames);
  assert.notEqual(c.server.auto.revision,'auto-r1');
});

test('failed NanoDNS save invokes callback, leaves confirmed selection and reports backend error',async()=>{
  const c=makeClient({});await loaded(cb=>c.context.loadPersistentState(cb));c.server.failProfileWrites=true;
  assert.equal(await loaded(cb=>c.context.setAuto('nanodns.elf',true,cb)),false);
  assert.deepEqual(Array.from(c.context.AUTO_NAMES),activeNames);
  assert.equal(c.context.PROF.list.HSDUTRA.length,3);
  assert.ok(c.status.some(s=>s.includes('Profile not saved: disk full')));
});

test('rapid explicit toggles compose against confirmed ACKs without dropping either edit',async()=>{
  const c=makeClient({});await loaded(cb=>c.context.loadPersistentState(cb));
  const a=loaded(cb=>c.context.setAuto('nanodns.elf',true,cb));
  const b=loaded(cb=>c.context.setAuto('second.elf',true,cb));
  assert.equal(c.context.AUTO_NAMES.length,3);
  assert.deepEqual(await Promise.all([a,b]),[true,true]);
  assert.deepEqual(c.server.auto.names,activeNames.concat('nanodns.elf','second.elf'));
});

test('profile switch commits ordered names only after ACK and survives reload',async()=>{
  const c=makeClient({});c.server.profiles.profiles.order.push('DNS');c.server.profiles.profiles.list.DNS=['nanodns.elf','ProsperoMgr.elf'];
  await loaded(cb=>c.context.loadPersistentState(cb));c.context.evPost=()=>{};
  c.context.profSwitch('DNS');assert.equal(c.context.PROF.active,'HSDUTRA');await new Promise(setImmediate);
  assert.equal(c.context.PROF.active,'DNS');assert.deepEqual(c.server.auto.names,['nanodns.elf','ProsperoMgr.elf']);
  const reopened=makeClient({server:c.server});await loaded(cb=>reopened.context.loadPersistentState(cb));
  assert.equal(reopened.context.PROF.active,'DNS');assert.deepEqual(Array.from(reopened.context.AUTO_NAMES),c.server.auto.names);
});

test('3/5/10-second explicit delay changes persist on PC, including absent settings and empty auto list',async()=>{
  const c=makeClient({settingsExists:false,names:[],localStorage:{'ps5elfs-auto-delay':'0'}});
  await loaded(cb=>c.context.loadPersistentState(cb));assert.equal(c.requests.filter(r=>r.request.method==='POST').length,0);
  for(const seconds of [3,5,10]){
    c.context.setAutoDelay(seconds);await new Promise(setImmediate);
    assert.equal(c.server.settings.settings.autoDelay,seconds);assert.equal(c.context.autoDelaySec(),seconds);
  }
  const reopened=makeClient({server:c.server,isPs5:true,localStorage:{'ps5elfs-auto-delay':'3'}});
  await loaded(cb=>reopened.context.loadPersistentState(cb));assert.equal(reopened.context.autoDelaySec(),10);
  assert.equal(c.server.auto.names.length,0);
  assert.equal(reopened.requests.filter(r=>r.request.method==='POST').length,0);
});

test('two browsers editing selections report conflict and recover the four confirmed payloads',async()=>{
  const server=makeServer(),pc=makeClient({server}),ps5=makeClient({server,isPs5:true});
  await loaded(cb=>pc.context.loadPersistentState(cb));await loaded(cb=>ps5.context.loadPersistentState(cb));
  assert.equal(await loaded(cb=>pc.context.setAuto('nanodns.elf',true,cb)),true);
  assert.equal(await loaded(cb=>ps5.context.setAuto('conflicting.elf',true,cb)),false);
  assert.deepEqual(server.auto.names,activeNames.concat('nanodns.elf'));
  assert.deepEqual(Array.from(ps5.context.AUTO_NAMES),server.auto.names);
  assert.ok(ps5.status.some(s=>s.startsWith('Profile not saved:')));
});

test('a stale in-flight refresh cannot undo a newly confirmed selection',async()=>{
  const c=makeClient({});await loaded(cb=>c.context.loadPersistentState(cb));
  const original=c.context.fetch,release=[];
  c.context.fetch=(url,request)=>!request||!request.method?new Promise(resolve=>original(url,request).then(r=>release.push(()=>resolve(r)))):original(url,request);
  c.context.refreshPersistentState();await new Promise(setImmediate);
  assert.equal(await loaded(cb=>c.context.setAuto('nanodns.elf',true,cb)),true);
  release.forEach(f=>f());await new Promise(setImmediate);
  assert.equal(c.context.AUTO_NAMES.length,4);assert.equal(c.context.PROF.list.HSDUTRA.length,4);
});
