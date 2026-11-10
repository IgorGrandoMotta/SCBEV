// Testa o JavaScript real da pagina de historico com respostas HTTP simuladas.
const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict'),path=require('node:path');
const codigo=fs.readFileSync(path.join(__dirname,'../firmware/SCBEV_Simples/SCBEV_Simples.ino'),'utf8');
const js=codigo.slice(codigo.indexOf('void historico()')).match(/<script>([\s\S]*?)<\/script>/)[1];
async function executar(texto,ok=true){
  const log={textContent:''};
  const contexto=vm.createContext({document:{getElementById:()=>log},fetch:async()=>({ok,text:async()=>texto}),Error,Date,Number});
  vm.runInContext(js,contexto);
  await new Promise(resolve=>setImmediate(resolve));
  return log.textContent;
}
(async()=>{
  const dados='SEM_HORA | INFOS | Inicio <seguro> | boot=abc ms=1000\n'+
    '09/11/2026 10:00:00 | INFOS | BASE_HORA=1794229200000 | boot=abc ms=2000\n'+
    'SEM_HORA | AVISOS | Outro boot sem hora | boot=def ms=3000\n'+
    '09/11/2026 10:00:05 | AVISOS | Agua | boot=abc ms=4000\n';
  const resultado=await executar(dados);
  assert.ok(!resultado.split('\n')[0].startsWith('SEM_HORA'));
  assert.match(resultado.split('\n')[0],/ \* \| INFOS/);
  assert.match(resultado,/Inicio <seguro>/); // Insercao com textContent, nao HTML.
  assert.match(resultado,/SEM_HORA \| AVISOS \| Outro boot/);
  assert.match(resultado,/09\/11\/2026 10:00:05/);
  assert.equal(await executar('',false),'Memoria indisponivel');
  console.log('OK: data reconstruida do mesmo boot, hora desconhecida preservada, texto literal e falha HTTP.');
})().catch(e=>{console.error(e);process.exitCode=1});
