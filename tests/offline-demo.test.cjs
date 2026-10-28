const test=require('node:test'),assert=require('node:assert/strict'),vm=require('vm'),fs=require('fs'),path=require('path');
const demo=fs.readFileSync(path.join(__dirname,'artifacts/autopayload-demo.html'),'utf8');
function fixture(){
 const elements={};let now=0,tick;
 const context={console,Math,String,Error,encodeURIComponent,Promise,Date:{now:()=>now},
  alert:()=>{},clearInterval:()=>{},clearTimeout:()=>{},setTimeout:()=>1,
  setInterval:fn=>{tick=fn;return 1;},
  document:{documentElement:{style:{}},addEventListener:()=>{},getElementById:id=>elements[id]||(elements[id]={style:{},focus(){},setAttribute(){},removeAttribute(){}})}};
 vm.createContext(context);
 for(const s of demo.matchAll(/<script>([\s\S]*?)<\/script>/g))vm.runInContext(s[1],context);
 return {context,elements,advance:ms=>{now+=ms;tick();}};
}
test('generated offline demo runs real presentation for 1/3/12 payloads and a 10-second countdown',()=>{
 for(const n of [1,3,12]){
  const f=fixture();f.context.demo(n);assert.equal(f.elements.bootvalue.textContent,'10');
  f.advance(1000);assert.equal(f.elements.bootvalue.textContent,'9');
  f.advance(9000);assert.equal(f.context.state.items[0].status,'loading');
  for(let i=0;i<2*n+1;i++)f.advance(350);
  assert.equal(f.context.state.phase,'completed');assert.equal(f.context.state.completed,n);
  assert.ok(f.context.state.items.every(x=>x.status==='success'));
 }
});
test('generated offline demo exposes failure/skipped without executing or contacting real payloads',()=>{
 const f=fixture();f.context.demo(3,true);f.advance(10000);for(let i=0;i<7;i++)f.advance(350);
 assert.deepEqual(Array.from(f.context.state.items,x=>x.status),['success','error','skipped']);
 assert.match(f.elements.bootsummary.textContent,/1 falharam/);
});
test('offline demo cancellation during countdown prevents simulated sends',async()=>{
 const f=fixture();f.context.padCancelAuto();await new Promise(setImmediate);f.advance(350);
 assert.equal(f.context.state.phase,'cancelled');assert.ok(f.context.state.items.every(x=>x.status==='cancelled'));
});
test('offline demo cancellation during loading finishes current item and cancels pending items',async()=>{
 const f=fixture();f.advance(10000);f.context.padCancelAuto();await new Promise(setImmediate);f.advance(350);
 assert.deepEqual(Array.from(f.context.state.items,x=>x.status),['success','cancelled','cancelled']);
 assert.equal(f.context.state.phase,'cancelled');
});
