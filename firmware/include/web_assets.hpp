// Generated from web/. Do not edit.
#pragma once
#include <pgmspace.h>
const char WEB_INDEX[] PROGMEM = R"GLUCO_INDEX(<!doctype html>
<html lang="es">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>Gluco Waveshare</title>
  <link rel="stylesheet" href="/style.css">
  <script src="/app.js" defer></script>
</head>
<body>
<header>
  <div class="brand"><span>GW</span><div><strong>Gluco Waveshare</strong><small id="version">Configuración local</small></div></div>
  <em id="state">Conectando...</em>
</header>
<main>
  <div class="notice" id="notice">Este portal solo funciona mientras el QR está visible en la pantalla.</div>
  <nav>
    <button type="button" data-step="start" class="active">Inicio</button>
    <button type="button" data-step="wifi">Wi-Fi</button>
    <button type="button" data-step="libre">LibreLinkUp</button>
    <button type="button" data-step="patient">Usuario</button>
    <button type="button" data-step="location">Ubicación</button>
    <button type="button" data-step="finish">Guardar</button>
  </nav>
  <form id="form">
    <section id="start" class="step active">
      <p class="eyebrow">AJUSTES</p><h1>¿Qué quieres configurar?</h1>
      <p>Los valores guardados se conservan aunque apagues completamente la pantalla.</p>
      <div class="actions">
        <button type="button" id="openWifi">Cambiar red Wi-Fi</button>
        <button type="button" class="primary" id="openServices">LibreLinkUp, usuario y clima</button>
      </div>
    </section>
    <section id="wifi" class="step">
      <p class="eyebrow">CONEXIÓN DE LA PANTALLA</p><h1>Wi-Fi de casa</h1>
      <p>Usa una red de 2,4 GHz. Al guardar, el portal actual se cerrará y la pantalla mostrará el segundo QR dentro de la red doméstica.</p>
      <div class="scan-header"><h2>Redes disponibles</h2><button type="button" id="scanWifi">Actualizar lista</button></div>
      <p class="hint" id="scanMessage" role="status">Buscando redes al abrir esta pestaña...</p>
      <div id="availableNetworks" class="choices"></div>
      <label>Nombre de red (SSID)<input id="ssid" maxlength="32" required autocomplete="off"></label>
      <label>Contraseña<input id="wifi_password" type="password" maxlength="63" autocomplete="new-password"></label>
      <p class="hint" id="wifiMessage">Si ya existe una clave guardada, déjala vacía para conservarla.</p>
      <button type="button" class="primary" id="testWifi">Guardar Wi-Fi y continuar</button>
      <div id="extraWifiSection" hidden>
        <hr><h2>Otras redes conocidas</h2>
        <p>Guarda hasta cuatro redes adicionales de 2,4 GHz. Si se pierde la conexión, la pantalla las probará de una en una. Mientras esté conectada no cambiará de red.</p>
        <div id="wifiNetworks"></div>
        <div class="actions"><button type="button" id="addNetwork">Añadir red</button><button type="button" id="saveNetworks" class="primary">Guardar estas redes</button></div>
        <p class="hint" id="networksMessage">Las claves ya guardadas se conservan si dejas su campo vacío.</p>
      </div>
    </section>
    <section id="libre" class="step">
      <p class="eyebrow">FREESTYLE LIBRE</p><h1>Inicia sesión</h1>
      <p>Usa la cuenta de <strong>LibreLinkUp</strong> que recibe las lecturas compartidas. No es el portal de informes LibreView.</p>
      <label>Correo<input id="libre_user" type="email" maxlength="160" autocomplete="username"></label>
      <label>Contraseña<input id="libre_password" type="password" maxlength="256" autocomplete="current-password"></label>
      <label>Región<select id="libre_region"><option value="eu">Europa</option><option value="eu2">Europa 2</option><option value="us">Estados Unidos</option><option value="de">Alemania</option><option value="fr">Francia</option><option value="ae">Emiratos</option><option value="ap">Asia-Pacífico</option><option value="au">Australia</option><option value="ca">Canadá</option><option value="jp">Japón</option><option value="la">Latinoamérica</option><option value="ru">Rusia</option></select></label>
      <details><summary>Compatibilidad avanzada</summary><label>Versión declarada de LibreLinkUp<input id="libre_version" maxlength="20" value="5.1.1"></label></details>
      <p class="hint" id="libreMessage">Introduce la cuenta de LibreLinkUp.</p>
      <button type="button" class="primary" id="loginLibre">Iniciar sesión y buscar usuarios</button>
    </section>
    <section id="patient" class="step">
      <p class="eyebrow">CUENTA COMPARTIDA</p><h1>Elige el usuario</h1>
      <p>La pantalla nunca cambiará de persona automáticamente si hay varias conexiones.</p>
      <div id="patients" class="choices"><p class="hint">Inicia sesión en el paso anterior.</p></div>
    </section>
    <section id="location" class="step">
      <p class="eyebrow">OPCIONAL</p><h1>Tu ubicación</h1>
      <p>Selecciona tu localidad para ver el clima. Puedes guardarla ahora o añadirla más tarde en Ajustes.</p>
      <div class="row"><label>Localidad<input id="citySearch" maxlength="80" placeholder="Tu localidad"></label><button type="button" id="searchCity">Buscar</button></div>
      <p class="hint" id="locationMessage">Selecciona un resultado para fijar coordenadas y zona horaria.</p>
      <div id="locations" class="choices"></div>
      <input id="city" type="hidden"><input id="latitude" type="hidden"><input id="longitude" type="hidden"><input id="timezone" type="hidden" value="Europe/Madrid">
    </section>
    <section id="finish" class="step">
      <p class="eyebrow">RESUMEN</p><h1>Guardar y cerrar</h1>
      <div id="summary" class="summary"></div>
      <div class="warning">LibreLinkUp utiliza una interfaz privada que Abbott puede cambiar. Confirma siempre las decisiones y alertas en el sistema oficial del sensor.</div>
      <button type="submit" class="primary" id="save">Guardar permanentemente y cerrar</button>
      <hr><button type="button" class="danger" id="reset">Borrar toda la configuración</button>
    </section>
  </form>
  <p id="message" role="status"></p>
</main>
<footer>Los ajustes se guardan en la memoria NVS del ESP32 y sobreviven a reinicios y apagados. Las credenciales no están cifradas en esta versión alfa.</footer>
</body>
</html>
)GLUCO_INDEX";
const char WEB_CSS[] PROGMEM = R"GLUCO_CSS(*{box-sizing:border-box}body{margin:0;background:#000;color:#f2f4f7;font:16px system-ui,-apple-system,Segoe UI,sans-serif;line-height:1.5}header,main,footer{max-width:900px;margin:auto}header{padding:22px 18px;display:flex;align-items:center;justify-content:space-between}.brand{display:flex;align-items:center;gap:13px}.brand>span{background:#21a366;color:#fff;border-radius:13px;padding:9px;font-size:20px;font-weight:800}.brand strong{display:block;font-size:21px}.brand small{display:block;color:#9aa4ae}header em{font-style:normal;background:#123624;color:#7de3aa;border-radius:20px;padding:7px 12px;font-size:13px}main{padding:0 18px 30px}.notice,.warning{border:1px solid #343a40;border-radius:12px;padding:13px 16px;background:#0b0b0d}.warning{margin:18px 0;background:#2b2308;border-color:#6f5b13;color:#f2d96b}nav{display:flex;gap:8px;margin:20px 0;overflow:auto}button{border:0;border-radius:10px;padding:11px 16px;background:#20242a;color:#f2f4f7;font:inherit;font-weight:700;cursor:pointer}button:disabled{opacity:.5;cursor:wait}nav button{white-space:nowrap;font-size:14px}nav button.active{background:#f2f4f7;color:#0b0b0d}.step{display:none;background:#0b0b0d;border:1px solid #20242a;border-radius:18px;padding:27px 30px}.step.active{display:block}.eyebrow{color:#45cf86;font-size:12px;font-weight:800;letter-spacing:1.6px;margin:0}h1{font-size:29px;margin:3px 0 12px}p{color:#b7c0c7}label{display:block;font-weight:700;font-size:14px;margin:17px 0 6px}input,select{display:block;width:100%;margin-top:6px;border:1px solid #4c5660;border-radius:9px;padding:11px 12px;background:#13161a;font:inherit;color:#f2f4f7}input:focus,select:focus,button:focus-visible{outline:3px solid #45cf86;outline-offset:2px}.primary{background:#21a366;color:#fff;margin-top:14px}.danger{background:#c92f49;color:#fff}.hint{font-size:13px;color:#9aa4ae}.row{display:flex;align-items:end;gap:10px}.row label{flex:1}.row button{margin-bottom:6px}.choices{display:grid;gap:10px;margin-top:18px}.choice{border:2px solid #30363d;background:#121519;text-align:left;padding:15px;border-radius:12px}.choice.selected{border-color:#21a366;background:#10271b}.choice strong,.choice small{display:block}.choice small{color:#9aa4ae;margin-top:3px}.summary{display:grid;gap:10px;background:#13161a;border-radius:12px;padding:17px}.summary b{display:inline-block;min-width:105px}details{margin-top:18px}summary{cursor:pointer;color:#b7c0c7}hr{border:0;border-top:1px solid #30363d;margin:25px 0}.actions{display:grid;gap:12px;max-width:430px}.actions .primary{margin-top:0}#message{font-weight:700;min-height:24px}.ok{color:#45cf86}.bad{color:#ff667d}footer{padding:0 18px 30px;color:#8d98a1;font-size:12px}@media(max-width:600px){header{padding:17px 14px}.brand strong{font-size:18px}header em{max-width:145px;text-align:center}main{padding:0 12px 25px}.step{padding:22px 18px}nav{margin:15px 0}.row{flex-wrap:wrap}.row label{min-width:180px}h1{font-size:25px}}

.network-row{display:grid;grid-template-columns:1fr 1fr auto;gap:10px;align-items:end;padding:12px;margin:10px 0;border:1px solid #30363d;border-radius:12px}.network-row label{margin:0}.network-row button{margin-bottom:1px}@media(max-width:600px){.network-row{grid-template-columns:1fr}.network-row button{justify-self:start}}
.scan-header{display:flex;justify-content:space-between;align-items:center;gap:12px;margin-top:24px}.scan-header h2{margin:0;font-size:18px}.scan-header button{font-size:13px}.scan-row{display:flex;align-items:center;gap:8px;padding:10px 12px;border:1px solid #30363d;border-radius:12px;background:#121519}.scan-row>div{flex:1;min-width:0;overflow-wrap:anywhere}.scan-row strong,.scan-row small{display:block}.scan-row small{color:#9aa4ae}.scan-row button{font-size:13px;padding:8px 10px}@media(max-width:600px){.scan-row{flex-wrap:wrap}.scan-row>div{flex-basis:100%}}
)GLUCO_CSS";
const char WEB_JS[] PROGMEM = R"GLUCO_JS("use strict";
(()=>{
const $=id=>document.getElementById(id);
let cfg={},selected={id:"",name:""},location=null,busy=false;
let wifiScanned=false,wifiScanBusy=false;
const maxExtraWifi=4;
function wifiNetworkRow(ssid="",hasPassword=false){
  if($("wifiNetworks").children.length>=maxExtraWifi)return;
  const row=document.createElement("div");row.className="network-row";
  const name=document.createElement("label");name.textContent="Red (SSID)";
  const ssidInput=document.createElement("input");ssidInput.maxLength=32;ssidInput.value=ssid;ssidInput.autocomplete="off";ssidInput.className="network-ssid";name.append(ssidInput);
  const key=document.createElement("label");key.textContent="Contraseña";
  const password=document.createElement("input");password.type="password";password.maxLength=63;password.autocomplete="new-password";
  password.className="network-password";password.placeholder=hasPassword?"Guardada; vacío para conservar":"Deja vacío si es abierta";key.append(password);
  const remove=document.createElement("button");remove.type="button";remove.textContent="Quitar";remove.addEventListener("click",()=>row.remove());
  row.append(name,key,remove);$("wifiNetworks").append(row);
}
$("addNetwork").addEventListener("click",()=>wifiNetworkRow());
$("saveNetworks").addEventListener("click",async()=>{
  if(busy)return;
  const networks=[...$("wifiNetworks").children].map(row=>({ssid:row.querySelector(".network-ssid").value.trim(),password:row.querySelector(".network-password").value}));
  if(networks.some(n=>!n.ssid)){message("Indica el nombre de cada red o quita la fila vacía.",true);return;}
  busy=true;$("saveNetworks").disabled=true;
  try{const r=await api("/api/wifi/networks",{networks});$("networksMessage").textContent=r.message;message(r.message);}
  catch(e){$("networksMessage").textContent=e.message;message(e.message,true)}
  finally{busy=false;$("saveNetworks").disabled=false;}
});

const message=(text,bad=false)=>{$("message").textContent=text;$("message").className=bad?"bad":"ok"};
const wait=ms=>new Promise(resolve=>setTimeout(resolve,ms));
async function api(path,data){
  let r;
  try{
    r=await fetch(path,{cache:"no-store",method:data?"POST":"GET",headers:data?{"Content-Type":"application/json","X-Setup-Token":cfg.csrf||""}:{},body:data?JSON.stringify(data):undefined});
  }catch{
    throw Error("Se perdió la conexión con la pantalla. Comprueba que el móvil sigue en el mismo Wi-Fi, vuelve a escanear el QR y revisa si el ESP32 se ha reiniciado.");
  }
  let out;try{out=await r.json()}catch{throw Error("La pantalla cerró el portal o devolvió una respuesta no válida.")}
  if(!r.ok)throw Error(out.message||`HTTP ${r.status}`);return out;
}
async function scanWifi(){
  if(wifiScanBusy||busy)return;
  wifiScanBusy=true;wifiScanned=true;$('scanWifi').disabled=true;
  $('scanMessage').textContent='Buscando redes cercanas...';
  try{
    let result=await api('/api/wifi/scan',{});
    for(let i=0;result.state==='running'&&i<17;i++){
      await wait(500);result=await api('/api/wifi/scan/status');
    }
    if(result.state!=='ready')throw Error('La búsqueda tardó demasiado. Puedes escribir el SSID manualmente.');
    const data=await api('/api/wifi/scan/status');
    const list=$('availableNetworks');list.replaceChildren();
    for(const item of data.networks||[]){
      const row=document.createElement('div');row.className='scan-row';
      const info=document.createElement('div');
      const name=document.createElement('strong');name.textContent=item.ssid;
      const detail=document.createElement('small');detail.textContent=`${item.secure?'Con clave':'Abierta'} · ${item.rssi} dBm`;
      info.append(name,detail);
      const use=document.createElement('button');use.type='button';use.textContent='Elegir';
      use.addEventListener('click',()=>{
        if($('ssid').value!==item.ssid)$('wifi_password').value='';
        $('ssid').value=item.ssid;
        $('scanMessage').textContent=`Red elegida: ${item.ssid}. Introduce su clave si corresponde.`;
      });
      row.append(info,use);
      if(cfg.portal_mode!=='wifi'){
        const add=document.createElement('button');add.type='button';add.textContent='Añadir';
        add.addEventListener('click',()=>{
          if([...$('wifiNetworks').querySelectorAll('.network-ssid')].some(x=>x.value===item.ssid))return;
          if($('wifiNetworks').children.length>=maxExtraWifi){$('scanMessage').textContent='Ya hay cuatro redes adicionales.';return;}
          wifiNetworkRow(item.ssid);$('scanMessage').textContent=`Añadida ${item.ssid}. Escribe su clave y pulsa Guardar estas redes.`;
        });
        row.append(add);
      }
      list.append(row);
    }
    $('scanMessage').textContent=data.networks?.length?
      'Elige una red de 2,4 GHz o escribe manualmente un SSID oculto.':
      'No se encontraron redes. Puedes escribir el SSID manualmente.';
  }catch(e){$('scanMessage').textContent=e.message}
  finally{wifiScanBusy=false;$('scanWifi').disabled=false}
}
$('scanWifi').addEventListener('click',scanWifi);
function step(id){
  document.querySelectorAll(".step").forEach(x=>x.classList.toggle("active",x.id===id));
  document.querySelectorAll("nav button").forEach(x=>x.classList.toggle("active",x.dataset.step===id));
  if(id==="finish")summary();
  if(id==="wifi"&&!wifiScanned)scanWifi();
}
document.querySelectorAll("nav button").forEach(x=>x.addEventListener("click",()=>step(x.dataset.step)));
$("openWifi").addEventListener("click",()=>step("wifi"));
$("openServices").addEventListener("click",()=>step("libre"));

const credentials=()=>({user:$("libre_user").value.trim(),password:$("libre_password").value,region:$("libre_region").value,version:$("libre_version").value.trim()});

$("testWifi").addEventListener("click",async()=>{
  if(busy)return;busy=true;$("testWifi").disabled=true;$("wifiMessage").textContent="Guardando la red...";
  try{
    const r=await api("/api/wifi",{ssid:$("ssid").value.trim(),password:$("wifi_password").value});
    message(r.message);$("wifiMessage").textContent=r.message;
    setTimeout(()=>{document.body.innerHTML='<main><section class="step active"><h1>Wi-Fi guardado</h1><p>Vuelve a conectar el móvil a la red de casa. La pantalla mostrará un segundo QR cuando tenga una dirección local.</p></section></main>'},500);
  }catch(e){$("wifiMessage").textContent=e.message;message(e.message,true);busy=false;$("testWifi").disabled=false;}
});

$("loginLibre").addEventListener("click",async()=>{
  if(busy)return;busy=true;$("loginLibre").disabled=true;$("libreMessage").textContent="Iniciando sesión desde la pantalla...";
  try{
    await api("/api/libre/login",credentials());
    let r=null;
    for(let elapsed=1;elapsed<=90;elapsed++){
      await wait(1000);r=await api("/api/libre/status");
      if(r.state!=="running")break;
      $("libreMessage").textContent=`Conectando con LibreLinkUp... ${elapsed} s`;
    }
    if(!r||r.state!=="ready")throw Error("LibreLinkUp no respondió en 90 segundos. Inténtalo de nuevo.");
    $("libre_region").value=r.region;$("patients").replaceChildren();selected={id:"",name:""};
    for(const p of r.patients){
      const b=document.createElement("button");b.type="button";b.className="choice";b.innerHTML="<strong></strong><small></small>";
      b.querySelector("strong").textContent=p.name;b.querySelector("small").textContent=p.id;
      b.addEventListener("click",()=>{document.querySelectorAll("#patients .choice").forEach(x=>x.classList.remove("selected"));b.classList.add("selected");selected=p;message(`Usuario seleccionado: ${p.name}`)});
      $("patients").append(b);
    }
    $("libreMessage").textContent=`Sesión correcta: ${r.patients.length} usuario(s) compartido(s).`;message("Sesión correcta. Elige la persona que mostrará la pantalla.");step("patient");
  }catch(e){$("libreMessage").textContent=e.message;message(e.message,true)}finally{busy=false;$("loginLibre").disabled=false;}
});

$("searchCity").addEventListener("click",async()=>{
  if(busy)return;busy=true;$("searchCity").disabled=true;$("locationMessage").textContent="Buscando...";
  try{
    await api("/api/geocode",{query:$("citySearch").value.trim()});
    let r=null;
    for(let elapsed=1;elapsed<=45;elapsed++){
      await wait(1000);r=await api("/api/geocode/status");
      if(r.state!=="running")break;
      $("locationMessage").textContent=`Buscando localidad... ${elapsed} s`;
    }
    if(!r||r.state!=="ready")throw Error("La búsqueda no respondió en 45 segundos. Inténtalo de nuevo.");
    $("locations").replaceChildren();
    for(const l of r.locations||[]){
      const b=document.createElement("button");b.type="button";b.className="choice";b.innerHTML="<strong></strong><small></small>";
      b.querySelector("strong").textContent=l.name;b.querySelector("small").textContent=`${l.latitude}, ${l.longitude} - ${l.timezone}`;
      b.addEventListener("click",()=>{document.querySelectorAll("#locations .choice").forEach(x=>x.classList.remove("selected"));b.classList.add("selected");location=l;$("city").value=l.name;$("latitude").value=l.latitude;$("longitude").value=l.longitude;$("timezone").value=l.timezone;$("locationMessage").textContent=`Seleccionada: ${l.name}`;message("Ubicación seleccionada. Revisa y guarda.")});
      $("locations").append(b);
    }
    if(!(r.locations||[]).length)throw Error("No se encontraron localidades");
  }catch(e){$("locationMessage").textContent=e.message;message(e.message,true)}finally{busy=false;$("searchCity").disabled=false;}
});

function payload(){return{ssid:$("ssid").value.trim(),wifi_password:$("wifi_password").value,libre_user:$("libre_user").value.trim(),libre_password:$("libre_password").value,libre_region:$("libre_region").value,libre_version:$("libre_version").value.trim(),patient_id:selected.id,patient_name:selected.name,city:location?.name||$("city").value,latitude:Number(location?.latitude??$("latitude").value),longitude:Number(location?.longitude??$("longitude").value),timezone:location?.timezone||$("timezone").value,location_set:!!(location||$("city").value)};}
function summary(){
  $("summary").innerHTML="";
  const rows=[["Wi-Fi",$("ssid").value||"Pendiente"],["Cuenta",$("libre_user").value||"Pendiente"],["Usuario",selected.name||"Pendiente"],["Ubicación",location?.name||$("city").value||"Pendiente"]];
  for(const [k,v] of rows){const d=document.createElement("div"),b=document.createElement("b");b.textContent=k;d.append(b,document.createTextNode(v));$("summary").append(d)}
}

$("form").addEventListener("submit",async e=>{
  e.preventDefault();
  if(!selected.id)return message("Inicia sesión y selecciona un usuario.",true);
  if(busy)return;busy=true;$("save").disabled=true;
  try{
    const r=await api("/api/save",payload());message(r.message);
    setTimeout(()=>{document.body.innerHTML='<main><section class="step active"><h1>Configuración guardada</h1><p>Los ajustes quedan en la memoria del ESP32. Ya puedes cerrar esta página.</p></section></main>'},700);
  }catch(e){message(e.message,true);busy=false;$("save").disabled=false;}
});

$("reset").addEventListener("click",async()=>{
  if(!confirm("¿Borrar Wi-Fi, cuenta, usuario, ubicación e histórico local?"))return;
  try{const r=await api("/api/reset",{confirm:"BORRAR"});message(r.message)}catch(e){message(e.message,true)}
});

$("libre_user").addEventListener("input",()=>{if(cfg.libre_user!==$("libre_user").value.trim())selected={id:"",name:""}});
$("libre_region").addEventListener("change",()=>{if(cfg.libre_region!==$("libre_region").value)selected={id:"",name:""}});

async function start(){
  try{
    cfg=await api("/api/config");$("version").textContent=`Versión ${cfg.version}`;
    if(cfg.last_reset_reason?.includes("WDT")||cfg.last_reset_reason==="Excepción"){
      const stage=cfg.last_fault_stage?` · fase: ${cfg.last_fault_stage}`:"";
      message(`Último reinicio: ${cfg.last_reset_reason}${stage}`,true);
    }
    if(cfg.settings_state==="invalid")message("Hay ajustes en NVS, pero no se pudieron leer. Conserva una copia de NVS antes de volver a guardar.",true);
    else if(cfg.settings_state==="unavailable")message("No se pudo abrir la memoria de ajustes NVS. Evita guardar de nuevo hasta comprobar la placa.",true);
    else if(cfg.settings_state==="missing")message("No aparecen ajustes guardados en NVS. Comprueba si tienes una copia anterior antes de configurar de nuevo.",true);
    for(const id of ["ssid","libre_user","libre_region","libre_version","city","latitude","longitude","timezone"])if($(id))$(id).value=cfg[id]??"";
    if(cfg.has_wifi_password)$("wifi_password").placeholder="Guardada; deja vacío para conservar";
    if(cfg.portal_mode!=="wifi"){
      $("extraWifiSection").hidden=false;
      for(const n of cfg.wifi_networks||[])wifiNetworkRow(n.ssid,n.has_password);
    }
    if(cfg.has_libre_password)$("libre_password").placeholder="Guardada; deja vacío para conservar";
    if(cfg.patient_id)selected={id:cfg.patient_id,name:cfg.patient_name||cfg.patient_id};
    if(cfg.location_set)location={name:cfg.city,latitude:cfg.latitude,longitude:cfg.longitude,timezone:cfg.timezone};
    summary();
    if(cfg.portal_mode==="wifi"){
      $("state").textContent="Paso 1 de 2";$("notice").textContent="Configura solamente el Wi-Fi. Después aparecerá un segundo QR en la pantalla.";
      document.querySelectorAll("nav button").forEach(x=>x.hidden=x.dataset.step!=="wifi");step("wifi");
    }else{
      $("state").textContent=`En red local · ${cfg.ip}`;$("notice").textContent="Configura Wi-Fi, LibreLinkUp y, si quieres, tu localidad para el clima. El portal se cerrará al guardar.";step("start");
    }
  }catch(e){message(e.message,true);document.querySelectorAll("button").forEach(x=>x.disabled=true)}
}
start();
})();
)GLUCO_JS";
