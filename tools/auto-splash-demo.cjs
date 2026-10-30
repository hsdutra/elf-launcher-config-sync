/* Explicit build-time, standalone offline demo. This file and its output are
 * not embedded in the ELF. Production has no query flag or demo endpoint. */
const fs=require('fs'),path=require('path');
const h=fs.readFileSync(path.join(__dirname,'../index.html'),'utf8');
const css=h.match(/<style>([\s\S]*?)<\/style>/)[1];
const markup=h.match(/<div id="autobanner"[^\n]*/)[0];
const js=h.slice(h.indexOf('/* Backend observer only.'),h.indexOf('function applyRemembered(){'));
const out=path.join(__dirname,'../tests/artifacts/autopayload-demo.html');
fs.mkdirSync(path.dirname(out),{recursive:true});
fs.writeFileSync(out,`<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>AutoPayload · DEMO OFFLINE</title><style>${css}
.demo-controls{position:fixed;bottom:0;left:0;right:0;z-index:300;background:var(--deck);padding:.5rem;display:flex;gap:.5rem;align-items:center;flex-wrap:wrap;border-top:1px solid var(--edge2)}#autobanner{padding-bottom:6rem}.demo-controls button{padding:.4rem}.demo-controls select{padding:.4rem}</style>
<main id="main"><p>DEMO OFFLINE — ELF Launcher</p><p>A simulação terminou. Escolha outro cenário.</p></main>${markup}
<div class="demo-controls"><b>DEMO OFFLINE</b><button onclick="demo(1)">1 ELF</button><button onclick="demo(3)">3 ELF</button><button onclick="demo(12)">12 ELF</button><button onclick="demo(3,true)">Falha + ignorado</button><select onchange="LANG=this.value;if(bootView.last)bootRender(bootView.last)"><option value="pt">Português</option><option value="en">English</option><option value="ar">العربية</option><option value="ja">日本語</option></select><button onclick="dark=!dark;document.documentElement.style.cssText=dark?'--deck:#141820;--ink:#eef2f8;--mute:#a3afc2;--edge2:#394354;--pri:#818cf8;--bad:#fb7185;--mint:#34d399':''">Tema</button></div>
<script>
var LANG='pt',dark=false,clock=null,seq=0,state;
var localStorage={getItem:function(){return null;},setItem:function(){}};
function esc(s){return String(s).replace(/[&<>"']/g,function(c){return {'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c];});}
function t(k,v){if(k==='autoPayload')return LANG==='ar'?'التشغيل التلقائي':LANG==='ja'?'自動ペイロード':'AutoPayload';v=v||{};return LANG==='pt'?'Aguardar antes do carregamento… '+v.n+' s':'Waiting… '+v.n+' s';}
function isConsoleHost(){return false;}
function showHome(){}
function showFolder(){alert(state.items.map(function(x){return x.name+': '+x.status+(x.status==='error'?' (falha simulada)':'');}).join('\\n'));}
function fetch(url){
  if(url.indexOf('/auto-cancel?')===0){state.cancel_requested=true;return Promise.resolve({ok:true});}
  return Promise.reject(Error('Network disabled in offline demo'));
}
${js}
document.getElementById('bootopen').onclick=bootDismiss;
document.getElementById('bootlog').onclick=function(){var list=document.getElementById('bootitems');list.className=list.className==='details'?'':'details';};
document.getElementById('autobannercancel').onclick=padCancelAuto;
document.addEventListener('keydown',function(e){if(e.key==='Escape')padCancelAuto();});
function demo(n,fail){
 clearInterval(clock);clearTimeout(bootView.finish);
 bootView={session:'',visible:false,dismissed:false,last:null,poll:null,finish:null,cancelling:false};
 state={ok:true,session:'demo-'+(++seq),phase:'waiting',delay_remaining:10,delay_total:10,total:n,completed:0,items:[]};
 for(var i=0;i<n;i++)state.items.push({name:['Kstuff Lite.elf','ShadowMountPlus.elf','ProsperoMgr.elf'][i]||'Payload '+(i+1)+'.elf',status:'pending'});
 var began=Date.now(),next=0;bootRender(state);
 clock=setInterval(function(){
  if(state.cancel_requested){for(var k=0;k<n;k++){if(state.items[k].status==='pending')state.items[k].status='cancelled';else if(state.items[k].status==='loading')state.items[k].status='success';}state.phase='cancelled';state.completed=n;}
  else if(Date.now()-began<10000){state.delay_remaining=Math.max(0,10-(Date.now()-began)/1000);}
  else if(next<n){state.phase='loading';if(state.items[next].status==='pending')state.items[next].status='loading';else{state.items[next].status=fail&&next===1?'error':fail&&next===2?'skipped':'success';state.completed=++next;}}
  else state.phase='completed';
  bootRender(state);if(bootTerminal(state))clearInterval(clock);
 },350);
}
demo(3);
</script>`);
console.log(out);
