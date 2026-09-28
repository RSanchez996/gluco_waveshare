"use strict";
(()=>{
const $=id=>document.getElementById(id);
let cfg={},selected={id:"",name:""},location=null,busy=false;

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
function step(id){
  document.querySelectorAll(".step").forEach(x=>x.classList.toggle("active",x.id===id));
  document.querySelectorAll("nav button").forEach(x=>x.classList.toggle("active",x.dataset.step===id));
  if(id==="finish")summary();
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
    const r=await api("/api/geocode",{query:$("citySearch").value.trim()});$("locations").replaceChildren();
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
    for(const id of ["ssid","libre_user","libre_region","libre_version","city","latitude","longitude","timezone"])if($(id))$(id).value=cfg[id]??"";
    if(cfg.has_wifi_password)$("wifi_password").placeholder="Guardada; deja vacío para conservar";
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
