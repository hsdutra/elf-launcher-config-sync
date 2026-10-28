const fs=require('fs'),vm=require('vm'),path=require('path'),crypto=require('crypto');
const root=path.join(__dirname,'..');
const file=process.argv[2]?path.resolve(process.argv[2]):path.join(root,'host/hbinstall/elf-launcher-bootsplash.elf');
const elf=fs.readFileSync(file),html=fs.readFileSync(path.join(root,'index.html'));
if(elf.subarray(0,4).toString('hex')!=='7f454c46'||elf.indexOf(html)<0)
  throw Error('ELF header or embedded HTML mismatch');
const demo=fs.readFileSync(path.join(__dirname,'artifacts/autopayload-demo.html'),'utf8');
for(const m of demo.matchAll(/<script>([\s\S]*?)<\/script>/g)) new vm.Script(m[1]);
console.log('PASS: ELF header, exact current embedded HTML, offline demo JavaScript syntax');
console.log('SHA256: '+crypto.createHash('sha256').update(elf).digest('hex'));
