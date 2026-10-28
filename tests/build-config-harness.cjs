// Compile current production configuration handlers on Linux. Only HTTP
// transport, logging and the legacy open-page preference are stubbed.
const fs=require('fs'),path=require('path');
const root=path.join(__dirname,'..'),main=fs.readFileSync(path.join(root,'host/hbinstall/main.c'),'utf8');
const update=fs.readFileSync(path.join(root,'host/hbinstall/update_impl.inc'),'utf8');
function fn(source,name){
  const re=new RegExp('^static [^;\\n]*\\b'+name+'\\([^;]*?\\) \\{','m'),m=re.exec(source);
  if(!m)throw Error('Function not found: '+name);
  let i=m.index+m[0].length,depth=1,mode='';
  for(;i<source.length;i++){
    const a=source[i],b=source[i+1];
    if(mode==='line'){if(a==='\n')mode='';continue;}
    if(mode==='comment'){if(a==='*'&&b==='/'){mode='';i++;}continue;}
    if(mode){if(a==='\\'){i++;continue;}if(a===mode)mode='';continue;}
    if(a==='/'&&b==='/'){mode='line';i++;continue;}if(a==='/'&&b==='*'){mode='comment';i++;continue;}
    if(a==='"'||a==="'"){mode=a;continue;}
    if(a==='{')depth++;if(a==='}'&&!--depth)return source.slice(m.index,i+1);
  }
  throw Error('Unterminated function: '+name);
}
let out=fs.readFileSync(path.join(__dirname,'config-backend.prelude.c'),'utf8');
out+=update.slice(update.indexOf('typedef struct'),update.indexOf('static int mkdir_p_under_data'));
out+='#define rename config_rename\n'+fn(update,'mkdir_p_under_data')+'\n'+fn(update,'write_atomic_under_data')+'\n#undef rename\n';
for(const name of ['mirror_disk','auto_skip_name','add_auto_name_checked','add_auto_name','write_auto_names_unlocked','read_auto_list_impl','read_auto_list','hdr_get','add_to_auto_list'])out+=fn(main,name)+'\n';
const start=main.indexOf('static int file_revision_sha256(const char *path, size_t max_size,',main.indexOf('#define PROFILES_FILE'));
const end=main.indexOf('/* ---- system info:',start);
out+=main.slice(start,end);
out+='\n'+fs.readFileSync(path.join(__dirname,'config-backend.cases.c'),'utf8');
fs.mkdirSync(path.join(__dirname,'artifacts'),{recursive:true});
fs.writeFileSync(path.join(__dirname,'artifacts/config-backend.generated.c'),out);
console.log('Generated production C configuration harness');
