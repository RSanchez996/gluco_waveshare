#include "app.hpp"
#include "fonts.hpp"
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <Preferences.h>
Config config;AppState *sharedState=nullptr;SemaphoreHandle_t stateMutex=nullptr;SemaphoreHandle_t storageMutex=nullptr;SemaphoreHandle_t httpMutex=nullptr;QueueHandle_t patientQueue=nullptr;
int arduinoCore=1;
RTC_DATA_ATTR uint32_t bootMagic=0,rapidBoots=0,bootCount=0;
RTC_DATA_ATTR char lastFaultStage[64]{};
namespace {
esp_reset_reason_t lastResetReason=ESP_RST_UNKNOWN;
uint8_t faultBoots=0;
String faultStages[3];
bool persistDiagnosticStage(const char *stage){
    return strcmp(stage,"Iniciando Wi-Fi")==0 ||
           strcmp(stage,"Libre: login HTTPS")==0 ||
           strcmp(stage,"Libre: gráfica HTTPS")==0 ||
           strcmp(stage,"Libre: procesando datos")==0 ||
           strcmp(stage,"Libre: analizando JSON")==0 ||
           strcmp(stage,"Portal: geocodificación HTTPS")==0;
}
void recordFaultBoot(){
    Preferences p;
    if(!p.begin("glucodiag",false))return;
    const uint8_t previous=p.getUChar("faults",0);
    const bool fault=lastResetReason==ESP_RST_TASK_WDT || lastResetReason==ESP_RST_INT_WDT ||
                     lastResetReason==ESP_RST_PANIC || lastResetReason==ESP_RST_WDT;
    faultBoots=fault?(previous>=3?3:previous+1):0;
    if(fault){
        // La fase se conserva en RTC durante reinicios WDT, sin escribir
        // flash mientras el controlador RGB lee el framebuffer de PSRAM.
        const String stage=lastFaultStage[0]?String(lastFaultStage):p.getString("stage","Fase anterior desconocida");
        if(faultBoots>=1 && faultBoots<=3)p.putString((String("fault")+String(faultBoots)).c_str(),stage);
        for(unsigned i=0;i<3;++i)faultStages[i]=p.getString((String("fault")+String(i+1)).c_str(),"");
    }else{
        for(unsigned i=0;i<3;++i){const String key=String("fault")+String(i+1);if(p.isKey(key.c_str()))p.remove(key.c_str());}
        if(p.isKey("stage"))p.remove("stage");
    }
    lastFaultStage[0]=0;
    if(faultBoots!=previous)p.putUChar("faults",faultBoots);
    p.end();
}
}
const char *appPreviousFaultStage(unsigned index){return index<3?faultStages[index].c_str():"";}
void appDiagnosticStage(const char *stage){
    if(!stage||!stage[0])return;
    // RTC conserva la fase tras un WDT sin provocar escrituras NVS en las
    // consultas HTTPS, que interrumpirían el barrido del panel RGB.
    if(!persistDiagnosticStage(stage))return;
    if(storageMutex)xSemaphoreTake(storageMutex,portMAX_DELAY);
    if(strcmp(lastFaultStage,stage)==0){
        if(storageMutex)xSemaphoreGive(storageMutex);
        return;
    }
    strlcpy(lastFaultStage,stage,sizeof(lastFaultStage));
    if(storageMutex)xSemaphoreGive(storageMutex);
    Serial.printf("[FASE] %s\n",stage);
}
const char *appResetReason(){
    switch(lastResetReason){
        case ESP_RST_POWERON:return "Encendido";
        case ESP_RST_SW:return "Software";
        case ESP_RST_PANIC:return "Excepción";
        case ESP_RST_INT_WDT:return "WDT interrupción";
        case ESP_RST_TASK_WDT:return "WDT tarea";
        case ESP_RST_WDT:return "WDT";
        case ESP_RST_BROWNOUT:return "Tensión baja";
        default:return "Otro";
    }
}
uint32_t appBootCount(){return bootCount;}
int appWorkerCore(){return arduinoCore==0?1:0;}
namespace {
void halt(const char *message){Serial.printf("[FATAL] %s\n",message);Serial.println("El error queda detenido para poder leerlo. Reinicia despues de corregirlo.");Serial.flush();while(true)delay(1000);}
void safeMode(){lv_obj_clean(lv_scr_act());lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(0x3A1118),0);lv_obj_t *label=lv_label_create(lv_scr_act());String info="MODO SEGURO\nCausa: ";info+=appResetReason();info+="\n\nFase al reiniciarse:";for(unsigned i=0;i<3;++i)if(faultStages[i].length())info+="\nIntento "+String(i+1)+": "+faultStages[i];info+="\n\nAnota estas fases. Apaga por completo 10 segundos\ny vuelve a encender.";lv_label_set_text(label,info.c_str());lv_obj_set_width(label,730);lv_obj_set_style_text_font(label,&fonts::montserrat20,0);lv_obj_set_style_text_color(label,lv_color_hex(0xFFFFFF),0);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);while(true){lv_timer_handler();delay(5);}}
}
void setup(){
    arduinoCore=xPortGetCoreID();
    Serial.begin(115200);delay(800);lastResetReason=esp_reset_reason();if(bootMagic!=0x47574C43){bootMagic=0x47574C43;rapidBoots=0;bootCount=0;}++rapidBoots;++bootCount;Serial.printf("\n[BOOT 1/6] Gluco Waveshare %s | intento rápido %u\n",APP_VERSION,rapidBoots);Serial.printf("Chip %s | flash %u MB | PSRAM %u MB | reset %d (%s) | arranque %u\n",ESP.getChipModel(),ESP.getFlashChipSize()/1048576U,ESP.getPsramSize()/1048576U,int(lastResetReason),appResetReason(),bootCount);
    Serial.printf("[CPU] Interfaz %d | HTTPS %d (prioridad 0)\n",arduinoCore,appWorkerCore());
    if(!psramFound()||ESP.getPsramSize()<7*1024*1024)halt("No se detectan los 8 MB de PSRAM OPI");
    recordFaultBoot();
    storageMutex=xSemaphoreCreateMutex();stateMutex=xSemaphoreCreateMutex();httpMutex=xSemaphoreCreateMutex();patientQueue=xQueueCreate(1,sizeof(PatientSelection));sharedState=static_cast<AppState *>(heap_caps_calloc(1,sizeof(AppState),MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT));if(!storageMutex||!stateMutex||!httpMutex||!patientQueue||!sharedState)halt("No se pudo reservar memoria de estado");
    // Los reinicios manuales o de alimentación no indican un WDT. El modo
    // seguro se activa solo tras fallos consecutivos realmente registrados.
    displayInit();if(faultBoots>=3)safeMode();appDiagnosticStage("Cargando ajustes");Serial.println("[BOOT 4/6] Leyendo configuración NVS");configLoad();uiInit();
    // Pantalla alimentada por USB: evitar la latencia del modem-sleep en HTTPS.
    // No modifica la persistencia NVS ni fuerza reconexiones durante una consulta.
    WiFi.persistent(false);WiFi.setSleep(false);WiFi.setAutoReconnect(true);WiFi.mode(WIFI_STA);
    configTzTime(config.timezone=="Atlantic/Canary"?"WET0WEST,M3.5.0/1,M10.5.0":config.timezone=="UTC"?"UTC0":"CET-1CEST,M3.5.0,M10.5.0/3","pool.ntp.org","time.cloudflare.com");
    if(!config.ssid.isEmpty()){appDiagnosticStage("Iniciando Wi-Fi");WiFi.begin(config.ssid.c_str(),config.wifiPass.c_str());}
    if(configNeedsSetup()){appDiagnosticStage("Abriendo configuracion");portalStart();uiShowSetup();}appDiagnosticStage("Iniciando tareas");networkStart();Serial.println("[BOOT 6/6] Aplicacion preparada");
}
void loop(){portalLoop();static uint32_t last=0;if(millis()-last>=500){uiTick();last=millis();}if(rapidBoots&&millis()>60000){rapidBoots=0;Serial.println("[BOOT] Estable durante 60 s; contador de reinicios borrado");}if(faultBoots&&millis()>300000){Preferences p;if(p.begin("glucodiag",false)){p.putUChar("faults",0);p.end();faultBoots=0;}}lv_timer_handler();delay(5);}
void markPlannedRestart(){rapidBoots=0;}
