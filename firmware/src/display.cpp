#include "app.hpp"
#include "fonts.hpp"
#include <esp_display_panel.hpp>
#include <esp_heap_caps.h>
using namespace esp_panel::board;
using namespace esp_panel::drivers;
namespace {
Board *board=nullptr; Touch *touchDevice=nullptr;bool screenSleeping=false,wakeGuard=false;
void flush(lv_disp_drv_t *drv,const lv_area_t *area,lv_color_t *pixels){
    board->getLCD()->drawBitmap(area->x1,area->y1,area->x2-area->x1+1,area->y2-area->y1+1,reinterpret_cast<uint8_t *>(pixels));
    lv_disp_flush_ready(drv);
}
void touch(lv_indev_drv_t *,lv_indev_data_t *data){
    TouchPoint point;const bool pressed=touchDevice&&touchDevice->readPoints(&point,1,0)>0;
    if(screenSleeping&&pressed){displayWake();wakeGuard=true;data->state=LV_INDEV_STATE_RELEASED;return;}
    if(wakeGuard){if(!pressed)wakeGuard=false;data->state=LV_INDEV_STATE_RELEASED;return;}
    data->state=pressed?LV_INDEV_STATE_PRESSED:LV_INDEV_STATE_RELEASED;
    if(pressed){data->point.x=point.x;data->point.y=point.y;}
}
void fatal(const char *message){Serial.printf("[FATAL DISPLAY] %s\n",message);Serial.flush();while(true)delay(1000);}
}
void displayInit(){
    Serial.println("[BOOT 2/6] Perfil oficial Waveshare 4.3B: ST7262 + GT911 + CH422G");
    board=new Board();if(!board||!board->init())fatal("board->init() fallo");
    auto lcd=board->getLCD();
    if(lcd){
        lcd->configFrameBufferNumber(1);
        // El perfil Waveshare v1.0.4 usa RGB a 16 MHz y bounce buffer de 10
        // líneas. 12 MHz deja más margen de PSRAM durante HTTPS; la interfaz
        // es estática y no necesita la frecuencia de refresco de la demo.
        auto *bus=lcd->getBus();
        if(bus&&bus->getBasicAttributes().type==ESP_PANEL_BUS_TYPE_RGB&&
           !static_cast<BusRGB *>(bus)->configRGB_FreqHz(12*1000*1000))
            fatal("No se pudo configurar el reloj RGB");
    }
    if(!board->begin())fatal("board->begin() fallo");
    if(!board->getLCD())fatal("LCD RGB no creado");
    touchDevice=board->getTouch();if(!touchDevice)Serial.println("[AVISO] GT911 no disponible; la pantalla continuara");
    if(board->getBacklight())board->getBacklight()->on();
    lv_init();fonts::init();constexpr size_t count=800*18;
    auto *a=static_cast<lv_color_t *>(heap_caps_malloc(count*sizeof(lv_color_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    auto *b=static_cast<lv_color_t *>(heap_caps_malloc(count*sizeof(lv_color_t),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT));
    if(!a)fatal("sin memoria interna para LVGL");
    static lv_disp_draw_buf_t draw;lv_disp_draw_buf_init(&draw,a,b,count);
    static lv_disp_drv_t driver;lv_disp_drv_init(&driver);driver.hor_res=800;driver.ver_res=480;driver.flush_cb=flush;driver.draw_buf=&draw;lv_disp_drv_register(&driver);
    if(touchDevice){static lv_indev_drv_t input;lv_indev_drv_init(&input);input.type=LV_INDEV_TYPE_POINTER;input.read_cb=touch;lv_indev_drv_register(&input);}
    lv_obj_set_style_bg_color(lv_scr_act(),lv_color_hex(0x000000),0);
    lv_obj_t *label=lv_label_create(lv_scr_act());lv_label_set_text(label,"Gluco Waveshare\nPantalla y táctil: OK\nIniciando servicios...");
    lv_obj_set_style_text_font(label,&fonts::montserrat28,0);lv_obj_set_style_text_color(label,lv_color_hex(0xFFFFFF),0);lv_obj_set_style_text_align(label,LV_TEXT_ALIGN_CENTER,0);lv_obj_center(label);lv_refr_now(nullptr);
    Serial.println("[BOOT 3/6] LCD 800x480, LVGL y tactil preparados");
}
void displayWake(){screenSleeping=false;if(board&&board->getBacklight())board->getBacklight()->on();}
void displaySleep(){screenSleeping=true;if(board&&board->getBacklight())board->getBacklight()->off();Serial.println("[PANTALLA] Retroiluminacion apagada; toca para encender");}
bool displayIsSleeping(){return screenSleeping;}
