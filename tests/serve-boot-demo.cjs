const http=require('http'),fs=require('fs'),path=require('path');
const demo=path.join(__dirname,'artifacts/autopayload-demo.html');
http.createServer((req,res)=>{res.setHeader('Content-Type','text/html; charset=utf-8');res.end(fs.readFileSync(demo));}).listen(8765,'127.0.0.1',()=>console.log('Offline demo: http://127.0.0.1:8765'));
