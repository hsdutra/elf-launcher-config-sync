const test=require('node:test'),assert=require('node:assert/strict'),fs=require('fs'),vm=require('vm');
const h=fs.readFileSync(require('path').join(__dirname,'../index.html'),'utf8');
const source=h.slice(h.indexOf('/* Backend observer only.'),h.indexOf('function applyRemembered(){'));
function client(storage,fetcher){
  const elements={},requests=[],timers=[];
  const context={LANG:'pt',console,Math,String,Error,encodeURIComponent,Promise,
    esc:s=>String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/"/g,'&quot;'),
    t:(k,v)=>k+':'+JSON.stringify(v),isConsoleHost:()=>true,showHome:()=>{},
    localStorage:{getItem:k=>storage[k],setItem:(k,v)=>{storage[k]=v;}},
    document:{getElementById:id=>elements[id]||(elements[id]={style:{},className:'',focus(){},setAttribute(){},removeAttribute(){}})},
    setTimeout:(fn,ms)=>{const timer={fn,ms};timers.push(timer);return timer;},clearTimeout:timer=>{timer.cancelled=true;},
    fetch:(url,options)=>{requests.push({url,options});return fetcher?fetcher(url,options):Promise.resolve({ok:true,json:()=>Promise.resolve(state(3))});}
  };
  vm.createContext(context);vm.runInContext(source,context);
  return {context,elements,requests,timers};
}
function state(total,phase='waiting'){
  return {ok:true,session:'boot-1',phase,delay_remaining:9.1,delay_total:10,total,completed:0,items:Array.from({length:total},(_,i)=>({name:'payload-'+i+'.elf',status:'pending'}))};
}
test('real countdown and processed fractions for 1, 3 and many ELFs',()=>{
  const c=client({}).context;
  for(const n of [1,3,12,64]){
    const j=state(n);assert.equal(c.bootModel(j).value,'10');
    j.delay_remaining=2.1;assert.equal(c.bootModel(j).value,'3');
    assert.ok(Math.abs(c.bootModel(j).fraction-.79)<.0001);
    j.phase='loading';j.completed=n-1;assert.equal(c.bootModel(j).value,Math.floor((n-1)/n*100)+'%');
  }
});
test('summary distinguishes sent, failed, skipped and cancelled; names are escaped',()=>{
  const c=client({}).context,j=state(4,'completed');
  ['success','error','skipped','cancelled'].forEach((s,i)=>j.items[i].status=s);
  j.items[0].name='<img src=x onerror=alert(1)>';j.completed=4;
  const m=c.bootModel(j);assert.equal(m.value,'100%');assert.match(m.summary,/1 enviados.*1 falharam.*1 ignorados.*1 cancelados/);
  assert.ok(!m.rows.includes('<img'));assert.match(m.rows,/Enviado/);
});
test('refresh and reopen do not replay splash; next boot can show it',()=>{
  const storage={},a=client(storage),j=state(3);a.context.bootRender(j);assert.ok(a.context.bootView.visible);
  const reopened=client(storage);reopened.context.bootRender(j);assert.ok(!reopened.context.bootView.visible);
  j.session='boot-2';reopened.context.bootRender(j);assert.ok(reopened.context.bootView.visible);
});
test('PC and PS5 independently observe the same backend session without sends or settings writes',()=>{
  const pc=client({}),ps5=client({}),j=state(3);pc.context.bootRender(j);ps5.context.bootRender(j);
  assert.ok(pc.context.bootView.visible && ps5.context.bootView.visible);
  assert.equal(pc.context.bootView.session,ps5.context.bootView.session);
  assert.equal(pc.requests.length+ps5.requests.length,0);
});
test('terminal snapshots and empty queues never open splash; observed completion stays until Close',()=>{
  const a=client({});a.context.bootRender(state(0));assert.ok(!a.context.bootView.visible);
  a.context.bootRender(state(3,'completed'));assert.ok(!a.context.bootView.visible);
  a.context.bootRender(state(3));a.context.bootRender(state(3,'completed'));
  assert.ok(!a.timers.find(t=>t.ms===2500));assert.ok(a.context.bootView.visible);
  a.context.bootDismiss();assert.ok(!a.context.bootView.visible);
  a.context.bootRender(state(3));assert.ok(!a.context.bootView.visible);
});
test('cancel is an explicit POST tied to current session; retains results after acceptance',async()=>{
  let accept;const a=client({},()=>new Promise(resolve=>accept=resolve));a.context.bootRender(state(3));a.context.padCancelAuto();
  assert.ok(a.context.bootView.visible);assert.equal(a.requests.length,1);
  assert.equal(a.requests[0].url,'/auto-cancel?session=boot-1');assert.equal(a.requests[0].options.method,'POST');
  a.context.padCancelAuto();assert.equal(a.requests.length,1);
  accept({ok:true});await new Promise(setImmediate);assert.ok(a.context.bootView.visible);
  a.context.bootRender(state(3,'cancelled'));assert.ok(a.context.bootView.visible);
});
test('cancel rejection leaves progress visible and permits retry',async()=>{
  const a=client({},()=>Promise.resolve({ok:false,status:409}));a.context.bootRender(state(3));a.context.padCancelAuto();
  await new Promise(setImmediate);assert.ok(a.context.bootView.visible);assert.ok(!a.context.bootView.cancelling);
});
test('polling only reads status and retries failed connections',async()=>{
  const a=client({},()=>Promise.reject(Error('offline')));a.context.bootPoll();await new Promise(setImmediate);
  assert.equal(a.requests[0].url,'/auto-status');assert.equal(a.requests[0].options.method,'GET');
  assert.ok(a.timers.find(t=>t.ms===1000));
  assert.doesNotMatch(source,/sendPayload\(|trigger-auto|run_path|auto-list|\/settings/);
});
test('late timed-out responses do not overwrite the current snapshot',async()=>{
  let respond;const a=client({},()=>new Promise(resolve=>respond=resolve));a.context.bootPoll();a.timers[0].fn();
  respond({ok:true,json:()=>Promise.resolve(state(3))});await new Promise(setImmediate);
  assert.equal(a.context.bootView.last,null);
});
test('all supported languages provide every splash label',()=>{
  const c=client({}).context;
  for(const lang of ['en','pt','es','fr','de','it','ru','ja','zh','ar']){
    c.LANG=lang;for(let i=0;i<26;i++)assert.ok(c.bootText(i));
  }
});
