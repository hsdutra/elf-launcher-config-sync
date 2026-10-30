/* Browser QA against local fixtures only; never connects to a console. */
const {test}=require('node:test'),assert=require('node:assert/strict'),fs=require('node:fs'),path=require('node:path');
const {chromium}=require('playwright');
const sourceHtml=fs.readFileSync(path.join(__dirname,'../index.html'),'utf8');
// Test-only access to the production closure; never written to shipped HTML.
const closeAt=sourceHtml.lastIndexOf('})();');
const html=sourceHtml.slice(0,closeAt)+'window.fixtureEval=function(code){return eval(code);};\n'+sourceHtml.slice(closeAt);
test('vertical list, controls, intervals, focus, drag/drop, profiles and ten-language retained splash',async()=>{
  const browser=await chromium.launch({channel:'chrome',headless:true});
  try{
    const page=await browser.newPage(),errors=[],writes=[];
    page.setDefaultTimeout(15000);
    const run=(fn,arg)=>page.evaluate(({code,arg})=>window.fixtureEval('('+code+')('+JSON.stringify(arg)+')'),{code:fn.toString(),arg});
    const wait=fn=>page.waitForFunction(code=>window.fixtureEval('('+code+')()'),fn.toString());
    page.on('pageerror',e=>errors.push(e.message));
    const names=['kstuff-lite.elf','shadowmountplus.elf','ProsperoMgr.elf'];
    let profiles={active:'Test',order:['Test','Other'],list:{Test:names.slice(),Other:['ProsperoMgr.elf','kstuff-lite.elf']},intervals:{Test:{'kstuff-lite.elf':5,'shadowmountplus.elf':3},Other:{'ProsperoMgr.elf':7}}};
    let revision=1,autoRevision=1,settingsRevision=1,failNextProfile=false;
    let settings={schema:1,lang:'pt',theme:'pearl',autoDelay:10,openAfterJb:true,pins:[],recent:[]};
    const mockRoute=async route=>{
      const req=route.request(),url=new URL(req.url()),p=url.pathname;
      if(url.origin!=='http://127.0.0.1:1000')return route.abort();
      if(p==='/')return route.fulfill({contentType:'text/html',body:html});
      let result={ok:true};
      if(req.method()==='POST'){
        writes.push(p);assert.ok(['/profiles','/settings'].includes(p));
        if(p==='/settings'){
          assert.equal(req.headers()['if-match'],'s'+settingsRevision);
          settings=req.postDataJSON();settingsRevision++;
          return route.fulfill({contentType:'application/json',body:JSON.stringify({ok:true,revision:'s'+settingsRevision})});
        }
        if(failNextProfile){failNextProfile=false;return route.fulfill({status:500,contentType:'application/json',body:JSON.stringify({ok:false,message:'simulated disk failure'})});}
        assert.equal(req.headers()['if-match'],'p'+revision);
        assert.equal(req.headers()['x-auto-revision'],'a'+autoRevision);
        profiles=req.postDataJSON();revision++;autoRevision++;
        result={ok:true,revision:'p'+revision,autoRevision:'a'+autoRevision};
      }else if(p==='/settings')result={ok:true,exists:true,revision:'s'+settingsRevision,settings};
      else if(p==='/profiles')result={ok:true,revision:'p'+revision,autoRevision:'a'+autoRevision,profiles};
      else if(p==='/auto-list')result={ok:true,revision:'a'+autoRevision,names:profiles.list[profiles.active]};
      else if(p==='/auto-status')result={ok:true,session:'fixture-idle',phase:'completed',total:0,items:[]};
      else if(p==='/payloads.json')return route.fulfill({contentType:'application/json',body:'[]'});
      else result={ok:false};
      return route.fulfill({contentType:'application/json',body:JSON.stringify(result)});
    };
    await page.route('**/*',mockRoute);
    await page.goto('http://127.0.0.1:1000/');
    await wait(()=>PERSISTENT_READY);
    // A separate browser session observes the same backend settings.
    const remote=await browser.newPage();remote.setDefaultTimeout(15000);await remote.route('**/*',mockRoute);
    const remoteRun=(fn,arg)=>remote.evaluate(({code,arg})=>window.fixtureEval('('+code+')('+JSON.stringify(arg)+')'),{code:fn.toString(),arg});
    await remote.goto('http://127.0.0.1:1000/');
    await remote.waitForFunction(()=>window.fixtureEval('PERSISTENT_READY'));
    await remoteRun(()=>showHome());
    await run(()=>{
      currentFolder='auto';homeEl.className='';homeEl.innerHTML='';hdrEl.className='show';viewEl.className='show';
      var names=autoNamesNow().concat('available.elf');
      paintAutoPayload(names.map((name,i)=>({p:{name,path:'mirror/'+name,version:'v1.0',info:i===1?'Very long payload description '.repeat(20):''}})));
    });
    const rows=page.locator('.auto-sequence .elfwrap');
    const setRange=async(locator,value)=>locator.evaluate((el,value)=>{
      el.value=value;el.dispatchEvent(new Event('input',{bubbles:true}));el.dispatchEvent(new Event('change',{bubbles:true}));
    },value);
    const homeComponent=await remote.locator('#adelay').evaluate(el=>el.closest('.optrow').outerHTML);
    assert.equal(await page.locator('#adelay').evaluate(el=>el.closest('.optrow').outerHTML),homeComponent);
    await setRange(remote.locator('#adelay'),5);
    await remote.waitForFunction(()=>window.fixtureEval('!SETTINGS_BUSY && autoDelaySec()===5'));
    await page.locator('#adelay').focus();
    await run(()=>{window.savedStart=document.getElementById('adelay');refreshPersistentState();});
    await wait(()=>autoDelaySec()===5);
    assert.equal(await page.locator('#adelay').inputValue(),'5');
    assert.equal(await run(()=>document.activeElement===window.savedStart),true);
    await page.locator('#adelay').press('ArrowRight');
    await wait(()=>!SETTINGS_BUSY&&autoDelaySec()===5.5);
    await setRange(page.locator('#adelay'),10);
    await wait(()=>!SETTINGS_BUSY&&autoDelaySec()===10);
    await remoteRun(()=>refreshPersistentState());
    await remote.waitForFunction(()=>window.fixtureEval('autoDelaySec()===10'));
    assert.equal(await remote.locator('#adelay').inputValue(),'10');
    await remote.reload();await remote.waitForFunction(()=>window.fixtureEval('PERSISTENT_READY'));
    assert.equal(await remoteRun(()=>autoDelaySec()),10);
    await remote.close();
    for(const width of [1920,1280,768,390]){
      await page.setViewportSize({width,height:1080});
      for(const count of [2,3,4,12]){
        await run(n=>{
          const selected=Array.from({length:n},(_,i)=>i<3?['kstuff-lite.elf','shadowmountplus.elf','ProsperoMgr.elf'][i]:'a-very-long-payload-name-'+i+'-'.repeat(80)+'.elf');
          AUTO_NAMES=selected;PROF.list[PROF.active]=selected;
          paintAutoPayload(selected.concat('available.elf').map(name=>({p:{name,path:'mirror/'+name,version:'1.2.3'}})));
        },count);
        const boxes=await rows.evaluateAll(els=>els.map(el=>{const r=el.getBoundingClientRect();return {x:r.x,y:r.y,bottom:r.bottom,width:r.width};}));
        assert.equal(boxes.length,count+1);
        boxes.forEach((b,i)=>{assert.ok(Math.abs(b.x-boxes[0].x)<1);if(i)assert.ok(b.y>=boxes[i-1].bottom);});
        assert.equal(await run(()=>document.documentElement.scrollWidth<=innerWidth),true,`overflow at ${width}/${count}`);
        assert.equal(await page.locator('.auto-sequence input[type=number]').count(),0);
        assert.equal(await page.locator('.auto-sequence input[type=range]').count(),count-1);
      }
    }
    await page.setViewportSize({width:1280,height:1000});
    // Restore the confirmed fixture before exercising actual revisioned writes.
    await run(async()=>{await new Promise(resolve=>loadPersistentState(resolve));paintAutoPayload(AUTO_FOUND.slice(0,4));});
    const row=name=>page.locator('.auto-sequence .elfwrap').filter({has:page.locator('.n',{hasText:name})});
    await row('kstuff-lite.elf').locator('[data-move="1"]').click();
    await wait(()=>AUTO_NAMES[1]==='kstuff-lite.elf'&&!PROFILE_BUSY);
    assert.deepEqual(profiles.list.Test,['shadowmountplus.elf','kstuff-lite.elf','ProsperoMgr.elf']);
    assert.equal(await run(()=>document.activeElement.getAttribute('data-move')),'1');
    assert.equal(await row('kstuff-lite.elf').locator('input').inputValue(),'5');
    const intervalInput=row('kstuff-lite.elf').locator('input');
    await intervalInput.focus();await intervalInput.evaluate(el=>{window.savedRange=el;});
    const beforeSliderWrites=writes.filter(p=>p==='/profiles').length;
    await intervalInput.evaluate(el=>{for(const value of [6,7,8,9]){el.value=value;el.dispatchEvent(new Event('input',{bubbles:true}));}el.dispatchEvent(new Event('change',{bubbles:true}));});
    await wait(()=>!PROFILE_BUSY&&autoInterval('kstuff-lite.elf')===9);
    assert.equal(writes.filter(p=>p==='/profiles').length,beforeSliderWrites+1);
    assert.equal(await run(()=>document.activeElement===window.savedRange),true);
    // D-pad emulation uses the production padNav path and coalesces taps.
    await run(()=>{
      var buttons=Array.from({length:16},()=>({pressed:false}));
      for(var i=0;i<3;i++){buttons[15].pressed=true;padNav({buttons:buttons});buttons[15].pressed=false;padNav({buttons:buttons});}
    });
    await wait(()=>!PROFILE_BUSY&&autoInterval('kstuff-lite.elf')===12);
    assert.equal(writes.filter(p=>p==='/profiles').length,beforeSliderWrites+2);
    await intervalInput.press('ArrowLeft');
    await wait(()=>!PROFILE_BUSY&&autoInterval('kstuff-lite.elf')===11);
    failNextProfile=true;
    await setRange(intervalInput,20);
    await wait(()=>!PROFILE_BUSY&&!PROFILE_QUEUE.length&&!delaySlidersBusy()&&autoInterval('kstuff-lite.elf')===11);
    assert.equal(await row('kstuff-lite.elf').locator('input').inputValue(),'11');
    assert.equal(await run(()=>document.activeElement.className),'auto-interval');
    const beforeMouseWrites=writes.filter(p=>p==='/profiles').length;
    const sliderBox=await intervalInput.boundingBox();
    await page.mouse.move(sliderBox.x+sliderBox.width*11/300,sliderBox.y+sliderBox.height/2);
    await page.mouse.down();await page.mouse.move(sliderBox.x+sliderBox.width*.4,sliderBox.y+sliderBox.height/2,{steps:12});
    assert.equal(writes.filter(p=>p==='/profiles').length,beforeMouseWrites);
    await page.mouse.up();
    await wait(()=>!PROFILE_BUSY&&!delaySlidersBusy()&&autoInterval('kstuff-lite.elf')>50);
    assert.equal(writes.filter(p=>p==='/profiles').length,beforeMouseWrites+1);
    await setRange(intervalInput,11);
    await wait(()=>!PROFILE_BUSY&&autoInterval('kstuff-lite.elf')===11);
    await row('kstuff-lite.elf').dragTo(row('shadowmountplus.elf'));
    await wait(()=>!PROFILE_BUSY&&AUTO_NAMES[0]==='kstuff-lite.elf');
    assert.equal(profiles.intervals.Test['kstuff-lite.elf'],11);
    await run(()=>profSwitch('Other'));
    await wait(()=>!PROFILE_BUSY&&PROF.active==='Other');
    assert.equal(await run(()=>autoInterval('ProsperoMgr.elf')),7);
    await run(async()=>{await new Promise(resolve=>loadPersistentState(resolve));});
    assert.deepEqual(await run(()=>AUTO_NAMES),['ProsperoMgr.elf','kstuff-lite.elf']);
    const backup=await run(()=>JSON.parse(JSON.stringify(bkBuild([]).doc)));
    assert.equal(backup.settings.profiles.intervals.Test['kstuff-lite.elf'],11);
    assert.equal(await run(async settings=>new Promise(resolve=>persistProfilesAndAuto(resolve,settings.profiles,settings.autoPayload)),backup.settings),true);
    assert.equal(profiles.intervals.Other['ProsperoMgr.elf'],7);
    // Four representative cards for the visual deliverable, using fixtures only.
    await run(async()=>new Promise(resolve=>changeProfiles(p=>{
      p.active='Test';p.list.Test=['kstuff-lite.elf','shadowmountplus.elf','PS5Upload.elf','ProsperoMgr.elf'];
      p.intervals.Test['kstuff-lite.elf']=5;
    },resolve)));
    await run(()=>setStatus(t('profSwitched',{name:PROF.active})));
    await row('kstuff-lite.elf').locator('.auto-select').focus();
    await run(()=>{var buttons=Array.from({length:16},()=>({pressed:false}));buttons[13].pressed=true;padNav({buttons:buttons});buttons[13].pressed=false;padNav({buttons:buttons});});
    assert.equal(await run(()=>document.activeElement.className),'auto-interval');
    await run(()=>applyTheme('black'));
    await page.screenshot({path:path.join(__dirname,'artifacts/autopayload-list.png'),fullPage:true,animations:'disabled'});
    await page.setViewportSize({width:390,height:1000});
    await page.screenshot({path:path.join(__dirname,'artifacts/autopayload-list-mobile.png'),fullPage:true,animations:'disabled'});
    await page.setViewportSize({width:1280,height:1000});
    for(const lang of ['pt','en','es','fr','de','it','ar','ru','ja','zh']){
      await run(lang=>{LANG=lang;applyStaticI18n();paintAutoPayload(AUTO_FOUND);},lang);
      assert.equal(await page.locator('.auto-section-head .label').first().textContent(),await run(()=>t('autoExecutionOrder')));
      assert.notEqual(await run(()=>t('autoNextWait')),'autoNextWait');
      await run(lang=>{
        LANG=lang;bootView.session='';bootView.visible=false;bootView.dismissed=false;
        bootRender({ok:true,session:'fixture-'+lang,phase:'interval',delay_remaining:3,total:3,completed:1,items:[{name:'Kstuff Lite.elf',status:'success'},{name:'ShadowMountPlus.elf',status:'pending'},{name:'ProsperoMgr.elf',status:'pending'}]});
      },lang);
      assert.equal(await page.locator('#bootopen').textContent(),await run(()=>bootText(17)));
      assert.equal(await page.locator('#autobanner').getAttribute('dir'),lang==='ar'?'rtl':'ltr');
      assert.ok(!(await page.locator('#bootsummary').textContent()).includes('{n}'));
      await run(()=>{bootView.last.phase='completed';bootView.last.completed=bootView.last.total;bootView.last.items.forEach(x=>x.status='success');bootRender(bootView.last);});
      assert.equal(await page.locator('#autobanner').getAttribute('class'),'show');
    }
    assert.ok(await page.locator('.boot-item-name').first().evaluate(el=>el.getBoundingClientRect().width>150));
    await page.locator('#bootlog').click();
    assert.equal(await page.locator('#autobanner').getAttribute('class'),'show');
    assert.ok(await page.locator('.boot-item-name').first().evaluate(el=>el.getBoundingClientRect().width>150));
    await page.screenshot({path:path.join(__dirname,'artifacts/autopayload-splash.png'),fullPage:true});
    await page.locator('#bootopen').click();
    assert.equal(await page.locator('#autobanner').getAttribute('class'),'');
    assert.ok(writes.length>=4);assert.deepEqual(errors,[]);
  }finally{await browser.close();}
});
