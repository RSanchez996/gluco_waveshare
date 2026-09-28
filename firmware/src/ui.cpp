#include "app.hpp"
#include "fonts.hpp"
#include "theme.hpp"
#include "weather_icons.hpp"
#include <WiFi.h>
#include <algorithm>
#include <cmath>
namespace {
enum class Page{Home,Clock,Users,Setup};Page page=Page::Home;AppState snapshot;
enum class PendingPage{None,Home,Clock,Users,Setup,CloseSetup};PendingPage pendingPage=PendingPage::None;
lv_obj_t *timeLabel=nullptr,*dateLabel=nullptr,*weatherLabel=nullptr,*weatherSummary=nullptr,*homeIcon=nullptr,*glucoseLabel=nullptr,*detailLabel=nullptr,*errorLabel=nullptr,*graph=nullptr,*arrow=nullptr;
lv_obj_t *glucosePanel=nullptr,*glucoseAccent=nullptr;
lv_obj_t *clockBig=nullptr,*clockDate=nullptr,*clockWeather=nullptr,*clockSummary=nullptr,*clockIcon=nullptr,*hoursBox=nullptr,*hoursTab=nullptr,*daysTab=nullptr,*setupInfo=nullptr,*setupQr=nullptr;
int64_t selectedGraphEpoch=0;
weathericons::Icon homeGlyph{},clockGlyph{},hourGlyphs[6]{},dayGlyphs[4]{};
enum class ForecastView{Hours,Days};ForecastView forecastView=ForecastView::Hours;
int64_t renderedWeather=-1,renderedGlucose=-1,renderedMinute=-1,renderedGraphBucket=-1,renderedLatestEpoch=-1;
int64_t renderedRequestStep=-1;
int renderedLatestValue=-1;size_t renderedPoints=size_t(-1);char renderedGlucoseError[160]{},renderedWeatherError[140]{};PortalMode renderedPortal=PortalMode::Off;
char pendingPatientId[100]{};
int64_t renderedConnections=-1;size_t renderedConnectionCount=size_t(-1);char renderedConnectionsError[160]{};
lv_color_t c(uint32_t value){return lv_color_hex(value);}uint32_t rangeColor(double value){switch(gluco::range(value)){case gluco::Range::LowRed:return theme::Red;case gluco::Range::Green:return theme::Green;case gluco::Range::HighYellow:return theme::Yellow;case gluco::Range::VeryHighOrange:return theme::Orange;default:return theme::Muted;}}
void resetRenderCache(){renderedWeather=renderedGlucose=renderedMinute=renderedGraphBucket=renderedLatestEpoch=renderedRequestStep=-1;renderedLatestValue=-1;renderedPoints=size_t(-1);renderedGlucoseError[0]=renderedWeatherError[0]=0;}
String displayCity(){String s=config.city;const int comma=s.indexOf(',');if(comma>0)s.remove(comma);s.trim();return s;}
const char *shortWeatherText(int code){if(code==0)return "Despejado";if(code<=3)return "Nubes";if(code<=48)return "Niebla";if(code<=57)return "Llovizna";if(code<=67)return "Lluvia";if(code<=77)return "Nieve";if(code<=82)return "Chubascos";if(code<=86)return "Nieve";return "Tormenta";}
void base(lv_obj_t *root){lv_obj_set_style_bg_color(root,c(theme::Background),0);lv_obj_set_style_bg_opa(root,LV_OPA_COVER,0);lv_obj_clear_flag(root,LV_OBJ_FLAG_SCROLLABLE);}
lv_obj_t *label(lv_obj_t *parent,int x,int y,int w,const char *text,const lv_font_t *font=&fonts::montserrat16,uint32_t color=theme::Ink){lv_obj_t *o=lv_label_create(parent);lv_obj_set_pos(o,x,y);lv_obj_set_width(o,w);lv_label_set_text(o,text);lv_obj_set_style_text_font(o,font,0);lv_obj_set_style_text_color(o,c(color),0);return o;}
lv_obj_t *button(lv_obj_t *parent,int x,int y,int w,const char *text,lv_event_cb_t cb,void *userData=nullptr){lv_obj_t *b=lv_btn_create(parent);lv_obj_set_pos(b,x,y);lv_obj_set_size(b,w,52);lv_obj_set_style_bg_color(b,c(theme::Button),0);lv_obj_set_style_bg_color(b,c(0x265166),LV_STATE_PRESSED);lv_obj_set_style_border_width(b,1,0);lv_obj_set_style_border_color(b,c(theme::Grid),0);lv_obj_set_style_radius(b,14,0);lv_obj_set_style_shadow_width(b,0,0);lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,userData);lv_obj_t *l=lv_label_create(b);lv_label_set_text(l,text);lv_obj_set_style_text_font(l,&fonts::montserrat20,0);lv_obj_set_style_text_color(l,c(theme::Ink),0);lv_obj_center(l);return b;}
void drawLine(lv_draw_ctx_t *ctx,double x1,double y1,double x2,double y2,uint32_t color,int width=2){lv_draw_line_dsc_t d;lv_draw_line_dsc_init(&d);d.color=c(color);d.width=width;d.round_start=1;d.round_end=1;lv_point_t a{lv_coord_t(std::lround(x1)),lv_coord_t(std::lround(y1))},b{lv_coord_t(std::lround(x2)),lv_coord_t(std::lround(y2))};lv_draw_line(ctx,&d,&a,&b);}
void drawDashes(lv_draw_ctx_t *ctx,int x,int y,int width,uint32_t color){
    for(int offset=0;offset<width;offset+=16)
        drawLine(ctx,x+offset,y,x+std::min(offset+7,width),y,color,1);
}
void drawSettingsIcon(lv_event_t *event){
    lv_area_t area;lv_obj_get_coords(lv_event_get_target(event),&area);
    auto *ctx=lv_event_get_draw_ctx(event);
    const int x=(area.x1+area.x2)/2,y=(area.y1+area.y2)/2;
    for(int i=0;i<8;++i){
        const double angle=i*3.14159265358979323846/4.0;
        drawLine(ctx,x+9*cos(angle),y+9*sin(angle),
                 x+15*cos(angle),y+15*sin(angle),theme::Ink,5);
    }
    lv_draw_rect_dsc_t disk;lv_draw_rect_dsc_init(&disk);
    disk.bg_color=c(theme::Ink);disk.radius=LV_RADIUS_CIRCLE;
    lv_area_t rim{lv_coord_t(x-11),lv_coord_t(y-11),lv_coord_t(x+11),lv_coord_t(y+11)};
    lv_draw_rect(ctx,&disk,&rim);
    disk.bg_color=c(theme::Button);
    lv_area_t center{lv_coord_t(x-4),lv_coord_t(y-4),lv_coord_t(x+4),lv_coord_t(y+4)};
    lv_draw_rect(ctx,&disk,&center);
}
void drawText(lv_draw_ctx_t *ctx,int x,int y,int w,const char *text,uint32_t color,lv_text_align_t align=LV_TEXT_ALIGN_LEFT){lv_draw_label_dsc_t d;lv_draw_label_dsc_init(&d);d.color=c(color);d.font=&fonts::montserrat14;d.align=align;lv_area_t area{lv_coord_t(x),lv_coord_t(y),lv_coord_t(x+w),lv_coord_t(y+20)};lv_draw_label(ctx,&d,&area,text,nullptr);}
void drawGraph(lv_event_t *e){
    lv_area_t bounds;lv_obj_get_coords(lv_event_get_target(e),&bounds);
    auto *ctx=lv_event_get_draw_ctx(e);
    const int x=bounds.x1+45,y=bounds.y1+30;
    const int w=lv_area_get_width(&bounds)-62,h=lv_area_get_height(&bounds)-67;
    const int64_t now=time(nullptr);
    int high=300,low=40;
    for(size_t i=0;i<snapshot.pointCount;++i){
        high=std::max(high,(int(snapshot.points[i].glucose)/50+1)*50);
        if(snapshot.points[i].glucose<40)low=20;
    }
    auto py=[&](double value){return y+h-(value-low)*h/(high-low);};
    drawText(ctx,x+5,bounds.y1+7,180,"ÚLTIMAS 8 HORAS",theme::Blue);
    lv_draw_rect_dsc_t band;lv_draw_rect_dsc_init(&band);
    band.bg_color=c(theme::Green);band.bg_opa=LV_OPA_10;
    lv_area_t safe{lv_coord_t(x),lv_coord_t(py(180)),lv_coord_t(x+w),lv_coord_t(py(70))};
    lv_draw_rect(ctx,&band,&safe);
    for(int value:{70,180,240}){
        const uint32_t threshold=value==70?theme::Red:(value==180?theme::Yellow:theme::Orange);
        drawDashes(ctx,x,int(std::lround(py(value))),w,threshold);
        char txt[8];snprintf(txt,sizeof(txt),"%d",value);
        drawText(ctx,bounds.x1+4,py(value)-8,34,txt,threshold,LV_TEXT_ALIGN_RIGHT);
    }
    for(int i=0;i<5;++i){
        const int px=x+i*w/4;
        drawLine(ctx,px,y,px,y+h,theme::Grid,1);
        char tick[10]="--:--";
        if(now>=1700000000){
            const time_t t=now-(4-i)*2*60*60;tm local{};
            localtime_r(&t,&local);strftime(tick,sizeof(tick),"%H:%M",&local);
        }
        drawText(ctx,std::min(px-24,x+w-48),y+h+8,50,tick,theme::Muted);
    }
    for(size_t i=0;i<snapshot.pointCount;++i){
        const auto &p=snapshot.points[i];
        if(p.epoch<now-gluco::kHistorySeconds||p.epoch>now)continue;
        const double px=x+gluco::graphX(p.epoch,now,w),yy=py(p.glucose);
        if(i && gluco::graphConnect(snapshot.points[i-1],p)){
            const auto &q=snapshot.points[i-1];
            if(q.epoch>=now-gluco::kHistorySeconds)
                drawLine(ctx,x+gluco::graphX(q.epoch,now,w),py(q.glucose),px,yy,
                         rangeColor((q.glucose+p.glucose)/2.0),3);
        }
        lv_draw_rect_dsc_t dot;lv_draw_rect_dsc_init(&dot);
        dot.bg_color=c(rangeColor(p.glucose));dot.radius=LV_RADIUS_CIRCLE;
        const int radius=i==snapshot.pointCount-1?4:2;
        lv_area_t area{lv_coord_t(px-radius),lv_coord_t(yy-radius),lv_coord_t(px+radius),lv_coord_t(yy+radius)};
        lv_draw_rect(ctx,&dot,&area);
    }
    if(!selectedGraphEpoch)return;
    const gluco::Point *selected=nullptr;
    for(size_t i=0;i<snapshot.pointCount;++i){
        if(snapshot.points[i].epoch==selectedGraphEpoch){selected=&snapshot.points[i];break;}
    }
    if(!selected||selected->epoch<now-gluco::kHistorySeconds||selected->epoch>now)return;
    const int sx=int(std::lround(x+gluco::graphX(selected->epoch,now,w)));
    const int sy=int(std::lround(py(selected->glucose)));
    drawLine(ctx,sx,y,sx,y+h,theme::Blue,2);
    lv_draw_rect_dsc_t marker;lv_draw_rect_dsc_init(&marker);
    marker.bg_color=c(rangeColor(selected->glucose));marker.radius=LV_RADIUS_CIRCLE;
    lv_area_t pointArea{lv_coord_t(sx-6),lv_coord_t(sy-6),lv_coord_t(sx+6),lv_coord_t(sy+6)};
    lv_draw_rect(ctx,&marker,&pointArea);
    time_t timestamp=selected->epoch;tm local{};localtime_r(&timestamp,&local);
    char stamp[20]="--/-- --:--";strftime(stamp,sizeof(stamp),"%d/%m %H:%M",&local);
    char detail[48];snprintf(detail,sizeof(detail),"%s   %d mg/dL",stamp,int(selected->glucose));
    const int tooltipX=std::max(x+190,std::min(sx-112,bounds.x2-239));
    lv_draw_rect_dsc_t bubble;lv_draw_rect_dsc_init(&bubble);
    bubble.bg_color=c(theme::Button);bubble.bg_opa=LV_OPA_COVER;bubble.radius=7;
    lv_area_t tooltip{lv_coord_t(tooltipX),lv_coord_t(bounds.y1+3),lv_coord_t(tooltipX+234),lv_coord_t(bounds.y1+27)};
    lv_draw_rect(ctx,&bubble,&tooltip);
    drawText(ctx,tooltipX+8,bounds.y1+5,218,detail,theme::Ink,LV_TEXT_ALIGN_CENTER);
}
void chooseGraphPoint(lv_event_t *event){
    if(!snapshot.pointCount)return;
    lv_point_t touch{};lv_indev_t *indev=lv_indev_get_act();
    if(!indev)return;
    lv_indev_get_point(indev,&touch);
    lv_area_t bounds;lv_obj_get_coords(lv_event_get_target(event),&bounds);
    const int x=bounds.x1+45,y=bounds.y1+30;
    const int w=lv_area_get_width(&bounds)-62,h=lv_area_get_height(&bounds)-67;
    if(touch.x<x||touch.x>x+w||touch.y<y||touch.y>y+h)return;
    const int64_t now=time(nullptr);
    int bestDistance=w+1;int64_t closestEpoch=0;
    for(size_t i=0;i<snapshot.pointCount;++i){
        const auto &p=snapshot.points[i];
        if(p.epoch<now-gluco::kHistorySeconds||p.epoch>now)continue;
        const int px=x+int(std::lround(gluco::graphX(p.epoch,now,w)));
        const int distance=std::abs(px-touch.x);
        if(distance<bestDistance){bestDistance=distance;closestEpoch=p.epoch;}
    }
    if(closestEpoch&&closestEpoch!=selectedGraphEpoch){selectedGraphEpoch=closestEpoch;lv_obj_invalidate(graph);}
}
void drawArrow(lv_event_t *e){if(!snapshot.pointCount)return;const char *d=snapshot.direction;double angle=0;bool known=true,twice=false;if(!strcmp(d,"Flat"))angle=0;else if(!strcmp(d,"FortyFiveUp"))angle=-.7854;else if(!strcmp(d,"FortyFiveDown"))angle=.7854;else if(!strcmp(d,"SingleUp")||!strcmp(d,"DoubleUp")){angle=-1.5708;twice=!strcmp(d,"DoubleUp");}else if(!strcmp(d,"SingleDown")||!strcmp(d,"DoubleDown")){angle=1.5708;twice=!strcmp(d,"DoubleDown");}else known=false;lv_area_t area;lv_obj_get_coords(lv_event_get_target(e),&area);auto *ctx=lv_event_get_draw_ctx(e);uint32_t color=rangeColor(snapshot.points[snapshot.pointCount-1].glucose);if(!known){drawText(ctx,area.x1+20,area.y1+22,55,"--",theme::Muted);return;}for(int i=0;i<(twice?2:1);++i){double cx=area.x1+(twice?25+i*27:40),cy=area.y1+35,dx=cos(angle),dy=sin(angle),tx=cx+23*dx,ty=cy+23*dy;drawLine(ctx,cx-20*dx,cy-20*dy,tx,ty,color,5);drawLine(ctx,tx,ty,tx-12*dx-9*dy,ty-12*dy+9*dx,color,5);drawLine(ctx,tx,ty,tx-12*dx+9*dy,ty-12*dy-9*dx,color,5);}}
// Un callback táctil no debe destruir el propio botón aún en procesamiento.
// uiTick aplica la transición desde el bucle principal, fuera del evento LVGL.
void settings(lv_event_t *){pendingPage=PendingPage::Setup;}
void sleepHomeBackground(lv_event_t *event){
    // La zona superior apaga la retroiluminación; la gráfica queda reservada
    // para inspeccionar medidas y los botones mantienen sus acciones.
    if(page!=Page::Home||lv_event_get_target(event)!=lv_scr_act()||!graph)return;
    lv_indev_t *indev=lv_indev_get_act();lv_point_t touch{};
    if(indev){lv_indev_get_point(indev,&touch);lv_area_t bounds;lv_obj_get_coords(graph,&bounds);
        if(touch.y<bounds.y1)displaySleep();}
}
void clockPage(lv_event_t *);void homePage(lv_event_t *);void usersPage(lv_event_t *);
void updateForecast();
void requestHours(lv_event_t *){forecastView=ForecastView::Hours;updateForecast();}
void requestDays(lv_event_t *){forecastView=ForecastView::Days;updateForecast();}
void buildHome(){
    page=Page::Home;selectedGraphEpoch=0;resetRenderCache();lv_obj_clean(lv_scr_act());base(lv_scr_act());
    lv_obj_add_flag(lv_scr_act(),LV_OBJ_FLAG_CLICKABLE);
    timeLabel=label(lv_scr_act(),26,12,210,"--:--",&fonts::montserrat48);
    dateLabel=label(lv_scr_act(),28,70,380,"Esperando hora",&fonts::montserrat16,theme::Muted);
    lv_obj_t *weatherCard=lv_obj_create(lv_scr_act());
    lv_obj_set_pos(weatherCard,492,12);lv_obj_set_size(weatherCard,290,168);
    lv_obj_set_style_bg_color(weatherCard,c(theme::Card),0);
    lv_obj_set_style_border_width(weatherCard,1,0);
    lv_obj_set_style_border_color(weatherCard,c(theme::Grid),0);
    lv_obj_set_style_radius(weatherCard,18,0);
    lv_obj_clear_flag(weatherCard,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(weatherCard,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_pad_all(weatherCard,0,0);
    homeGlyph.background=theme::Card;
    homeIcon=weathericons::create(weatherCard,10,9,73,&homeGlyph);
    weatherLabel=label(weatherCard,91,20,177,"-- °C",&fonts::montserrat28);
    weatherSummary=label(weatherCard,12,85,266,"Clima pendiente",&fonts::montserrat14,theme::Muted);
    lv_label_set_long_mode(weatherSummary,LV_LABEL_LONG_WRAP);
    glucosePanel=lv_obj_create(lv_scr_act());lv_obj_set_pos(glucosePanel,18,100);lv_obj_set_size(glucosePanel,455,100);
    lv_obj_set_style_bg_color(glucosePanel,c(theme::Card),0);
    lv_obj_set_style_border_width(glucosePanel,1,0);
    lv_obj_set_style_border_color(glucosePanel,c(theme::Grid),0);
    lv_obj_set_style_radius(glucosePanel,18,0);
    lv_obj_set_style_pad_all(glucosePanel,0,0);
    lv_obj_clear_flag(glucosePanel,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(glucosePanel,LV_OBJ_FLAG_CLICKABLE);
    glucoseAccent=lv_obj_create(lv_scr_act());lv_obj_set_pos(glucoseAccent,21,116);lv_obj_set_size(glucoseAccent,4,62);
    lv_obj_set_style_bg_color(glucoseAccent,c(theme::Muted),0);
    lv_obj_set_style_border_width(glucoseAccent,0,0);
    lv_obj_set_style_radius(glucoseAccent,3,0);
    lv_obj_clear_flag(glucoseAccent,LV_OBJ_FLAG_CLICKABLE);
    label(lv_scr_act(),32,105,300,"GLUCOSA ACTUAL",&fonts::montserrat14,theme::Muted);
    glucoseLabel=label(lv_scr_act(),30,130,162,"---",&fonts::montserrat48,theme::Muted);
    label(lv_scr_act(),177,157,76,"mg/dL",&fonts::montserrat14,theme::Muted);
    arrow=lv_obj_create(lv_scr_act());lv_obj_set_pos(arrow,224,124);lv_obj_set_size(arrow,90,69);
    lv_obj_set_style_bg_opa(arrow,LV_OPA_TRANSP,0);lv_obj_set_style_border_width(arrow,0,0);
    lv_obj_clear_flag(arrow,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(arrow,drawArrow,LV_EVENT_DRAW_MAIN,nullptr);
    detailLabel=label(lv_scr_act(),326,143,143,"Sin lectura",&fonts::montserrat16,theme::Muted);
    errorLabel=label(lv_scr_act(),28,181,735,"Esperando LibreLinkUp...",&fonts::montserrat14,theme::Muted);
    lv_label_set_long_mode(errorLabel,LV_LABEL_LONG_DOT);
    graph=lv_obj_create(lv_scr_act());lv_obj_set_pos(graph,18,208);lv_obj_set_size(graph,764,201);
    lv_obj_set_style_bg_color(graph,c(theme::Card),0);
    lv_obj_set_style_border_width(graph,1,0);lv_obj_set_style_border_color(graph,c(theme::Grid),0);lv_obj_set_style_radius(graph,18,0);
    lv_obj_set_style_pad_all(graph,0,0);lv_obj_clear_flag(graph,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(graph,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(graph,drawGraph,LV_EVENT_DRAW_MAIN,nullptr);
    lv_obj_add_event_cb(graph,chooseGraphPoint,LV_EVENT_CLICKED,nullptr);
    button(lv_scr_act(),18,420,304,"Reloj y clima",clockPage);
    button(lv_scr_act(),338,420,304,"Usuario",usersPage);
    lv_obj_t *gear=button(lv_scr_act(),696,420,86,"",settings);
    lv_obj_add_event_cb(gear,drawSettingsIcon,LV_EVENT_DRAW_MAIN,nullptr);
}
void buildClock(){
    page=Page::Clock;resetRenderCache();lv_obj_clean(lv_scr_act());base(lv_scr_act());
    clockBig=label(lv_scr_act(),30,23,328,"--:--",&fonts::montserrat48);
    clockDate=label(lv_scr_act(),34,91,350,"Esperando hora",&fonts::montserrat20,theme::Muted);
    lv_obj_t *weatherCard=lv_obj_create(lv_scr_act());
    lv_obj_set_pos(weatherCard,405,17);lv_obj_set_size(weatherCard,371,126);
    lv_obj_set_style_bg_color(weatherCard,c(theme::Card),0);
    lv_obj_set_style_border_width(weatherCard,1,0);
    lv_obj_set_style_border_color(weatherCard,c(theme::Grid),0);
    lv_obj_set_style_radius(weatherCard,18,0);
    lv_obj_clear_flag(weatherCard,LV_OBJ_FLAG_SCROLLABLE);
    clockGlyph.background=theme::Card;
    clockIcon=weathericons::create(weatherCard,13,15,82,&clockGlyph);
    lv_obj_set_style_pad_all(weatherCard,0,0);
    clockWeather=label(weatherCard,103,9,243,"-- °C",&fonts::montserrat28);
    clockSummary=label(weatherCard,103,46,260,"Clima pendiente",&fonts::montserrat14,theme::Muted);
    lv_label_set_long_mode(clockSummary,LV_LABEL_LONG_WRAP);
    label(lv_scr_act(),28,169,330,"PRONÓSTICO LOCAL",&fonts::montserrat16,theme::Blue);
    hoursTab=button(lv_scr_act(),278,420,244,"Por horas",requestHours);
    daysTab=button(lv_scr_act(),538,420,244,"4 días",requestDays);
    hoursBox=lv_obj_create(lv_scr_act());lv_obj_set_pos(hoursBox,24,217);lv_obj_set_size(hoursBox,752,190);
    lv_obj_set_style_bg_color(hoursBox,c(theme::Card),0);
    lv_obj_set_style_border_width(hoursBox,1,0);lv_obj_set_style_border_color(hoursBox,c(theme::Grid),0);lv_obj_set_style_radius(hoursBox,18,0);
    lv_obj_set_style_pad_all(hoursBox,0,0);lv_obj_clear_flag(hoursBox,LV_OBJ_FLAG_SCROLLABLE);
    label(hoursBox,35,40,660,"Esperando pronóstico...",&fonts::montserrat20,theme::Muted);
    button(lv_scr_act(),18,420,244,"Glucosa",homePage);
}
void selectUser(lv_event_t *event){
    const size_t index=size_t(reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    if(index>=snapshot.connectionCount)return;
    if(requestPatientSelection(snapshot.connections[index].id,snapshot.connections[index].name)){
        selectedGraphEpoch=0;
        strlcpy(pendingPatientId,snapshot.connections[index].id,sizeof(pendingPatientId));
        pendingPage=PendingPage::Home;
    }
}
void buildUsers(){
    page=Page::Users;lv_obj_clean(lv_scr_act());base(lv_scr_act());
    label(lv_scr_act(),28,20,600,"Elegir usuario de glucosa",&fonts::montserrat28);
    String current="Usuario actual: "+(config.patientName.isEmpty()?String("ninguno"):config.patientName);
    label(lv_scr_act(),30,58,700,current.c_str(),&fonts::montserrat16,theme::Muted);
    if(!snapshot.connectionCount){
        const char *message=snapshot.connectionsError[0]?snapshot.connectionsError:"Actualizando usuarios de LibreLinkUp...";
        auto *info=label(lv_scr_act(),55,155,690,message,&fonts::montserrat20,theme::Muted);lv_label_set_long_mode(info,LV_LABEL_LONG_WRAP);
    }else{
        lv_obj_t *list=lv_obj_create(lv_scr_act());lv_obj_set_pos(list,28,92);lv_obj_set_size(list,744,305);lv_obj_set_style_bg_color(list,c(theme::Card),0);lv_obj_set_style_border_width(list,0,0);lv_obj_set_style_radius(list,18,0);lv_obj_set_style_pad_all(list,10,0);lv_obj_set_scroll_dir(list,LV_DIR_VER);
        for(size_t i=0;i<snapshot.connectionCount;++i){
            auto *b=button(list,12,12+int(i)*62,690,snapshot.connections[i].name,selectUser,reinterpret_cast<void *>(uintptr_t(i)));
            if(config.patientId==snapshot.connections[i].id)lv_obj_set_style_bg_color(b,c(theme::Green),0);
        }
    }
    if(snapshot.connectionsError[0])label(lv_scr_act(),30,399,560,snapshot.connectionsError,&fonts::montserrat14,theme::Yellow);
    button(lv_scr_act(),614,414,168,"Volver",homePage);
    renderedConnections=snapshot.connectionsFetched;renderedConnectionCount=snapshot.connectionCount;strlcpy(renderedConnectionsError,snapshot.connectionsError,sizeof(renderedConnectionsError));
}
void closeSetup(lv_event_t *){pendingPage=PendingPage::CloseSetup;}
void buildSetup(){
    page=Page::Setup;renderedPortal=portalMode();setupQr=nullptr;setupInfo=nullptr;lv_obj_clean(lv_scr_act());base(lv_scr_act());
    if(renderedPortal==PortalMode::WifiAccessPoint){
        label(lv_scr_act(),28,22,650,"Paso 1 de 2  ·  Configurar Wi-Fi",&fonts::montserrat28);
        label(lv_scr_act(),30,65,720,"Escanea el QR, conecta el móvil a la red temporal y guarda el Wi-Fi de casa.",&fonts::montserrat16,theme::Muted);
        setupQr=lv_qrcode_create(lv_scr_act(),230,c(0x000000),c(0xFFFFFF));lv_obj_set_pos(setupQr,36,125);String qr="WIFI:T:WPA;S:"+portalSsid()+";P:"+portalPassword()+";;";lv_qrcode_update(setupQr,qr.c_str(),qr.length());
        setupInfo=label(lv_scr_act(),310,140,455,"",&fonts::montserrat20);lv_label_set_long_mode(setupInfo,LV_LABEL_LONG_WRAP);
    }else if(renderedPortal==PortalMode::WaitingForWifi){
        label(lv_scr_act(),28,22,700,"Conectando a la red de casa",&fonts::montserrat28);
        label(lv_scr_act(),70,155,660,"Wi-Fi guardado en memoria.\n\nEspera mientras la pantalla se conecta y sincroniza la red.\n\nCuando esté preparada aparecerá el segundo QR.",&fonts::montserrat24,theme::Ink);
    }else{
        label(lv_scr_act(),28,22,700,"Paso 2 de 2  ·  Servicios y ajustes",&fonts::montserrat28);
        label(lv_scr_act(),30,65,730,"Conecta el móvil al mismo Wi-Fi de casa y escanea este segundo QR.",&fonts::montserrat16,theme::Muted);
        setupQr=lv_qrcode_create(lv_scr_act(),230,c(0x000000),c(0xFFFFFF));lv_obj_set_pos(setupQr,36,125);String qr=portalUrl();lv_qrcode_update(setupQr,qr.c_str(),qr.length());
        String info="Abre desde el móvil:\n\n"+portalUrl()+"\n\nSeñal Wi-Fi: "+String(WiFi.RSSI())+" dBm\n\nDesde el portal puedes cambiar la red y configurar LibreLinkUp. Clima configurable.\n\nSe cerrará al guardar o tras 10 minutos.";
        setupInfo=label(lv_scr_act(),310,130,455,info.c_str(),&fonts::montserrat20);lv_label_set_long_mode(setupInfo,LV_LABEL_LONG_WRAP);
    }
    button(lv_scr_act(),600,405,170,"Cerrar",closeSetup);
}
void clockPage(lv_event_t *){pendingPage=PendingPage::Clock;}void homePage(lv_event_t *){pendingPage=PendingPage::Home;}void usersPage(lv_event_t *){pendingPage=PendingPage::Users;}
void setClock(lv_obj_t *timeOut,lv_obj_t *dateOut){
    if(!timeOut||!dateOut)return;
    const time_t now=time(nullptr);
    if(now<1700000000){
        lv_label_set_text(timeOut,"--:--");
        lv_label_set_text(dateOut,"Sincronizando hora");
        return;
    }
    tm local{};localtime_r(&now,&local);
    char timeText[12],dateText[64];
    strftime(timeText,sizeof(timeText),"%H:%M",&local);
    const char *days[]{"domingo","lunes","martes","miércoles","jueves","viernes","sábado"};
    const char *months[]{"enero","febrero","marzo","abril","mayo","junio",
                         "julio","agosto","septiembre","octubre","noviembre","diciembre"};
    snprintf(dateText,sizeof(dateText),"%s, %d de %s",days[local.tm_wday],local.tm_mday,months[local.tm_mon]);
    lv_label_set_text(timeOut,timeText);lv_label_set_text(dateOut,dateText);
}
void updateWeather(lv_obj_t *temp,lv_obj_t *summary,lv_obj_t *icon,weathericons::Icon &glyph){
    if(!temp||!summary||!icon)return;
    if(!snapshot.weatherValid){
        lv_label_set_text(temp,"-- °C");
        lv_label_set_text(summary,snapshot.weatherError[0]?snapshot.weatherError:"Esperando clima...");
        lv_obj_add_flag(icon,LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const String city=displayCity();
    char value[24],details[180];
    snprintf(value,sizeof(value),"%.0f °C",snapshot.temperature);
    snprintf(details,sizeof(details),"%s\n%s · Sens. %.0f°\nMáx %.0f° · Mín %.0f°",
             city.c_str(),weatherText(snapshot.weatherCode),snapshot.apparent,
             snapshot.high,snapshot.low);
    lv_label_set_text(temp,value);
    lv_label_set_text(summary,details);
    glyph.code=snapshot.weatherCode;
    glyph.day=snapshot.weatherIsDay;
    lv_obj_clear_flag(icon,LV_OBJ_FLAG_HIDDEN);
    lv_obj_invalidate(icon);
}
void updateForecast(){
    if(!hoursBox)return;
    if(hoursTab&&daysTab){
        lv_obj_set_style_bg_color(hoursTab,c(forecastView==ForecastView::Hours?0x1C563F:theme::Button),0);
        lv_obj_set_style_bg_color(daysTab,c(forecastView==ForecastView::Days?0x1C563F:theme::Button),0);
    }
    lv_obj_clean(hoursBox);
    if(forecastView==ForecastView::Hours){
        if(!snapshot.hourCount){
            label(hoursBox,26,55,700,snapshot.weatherError[0]?snapshot.weatherError:
                  "Cargando previsión por horas...",&fonts::montserrat20,theme::Muted);
            return;
        }
        for(size_t i=0;i<snapshot.hourCount;++i){
            auto *card=lv_obj_create(hoursBox);
            lv_obj_set_pos(card,12+int(i)*122,12);lv_obj_set_size(card,112,166);
            lv_obj_set_style_bg_color(card,c(i==0?theme::Button:theme::Background),0);
            lv_obj_set_style_border_width(card,1,0);
            lv_obj_set_style_border_color(card,c(theme::Grid),0);
            lv_obj_set_style_radius(card,15,0);
            lv_obj_set_style_pad_all(card,0,0);
            lv_obj_clear_flag(card,LV_OBJ_FLAG_SCROLLABLE);
            const time_t t=snapshot.hours[i].epoch;tm local{};localtime_r(&t,&local);
            char hour[10],temp[20],rain[32];
            strftime(hour,sizeof(hour),"%H:%M",&local);
            snprintf(temp,sizeof(temp),"%.0f °C",snapshot.hours[i].temperature);
            snprintf(rain,sizeof(rain),"Lluvia %d%%",snapshot.hours[i].rain);
            auto *txt=label(card,0,8,112,hour,&fonts::montserrat16,theme::Muted);
            lv_obj_set_style_text_align(txt,LV_TEXT_ALIGN_CENTER,0);
            hourGlyphs[i].code=snapshot.hours[i].code;
            hourGlyphs[i].day=snapshot.hours[i].isDay;
            hourGlyphs[i].background=i==0?theme::Button:theme::Background;
            weathericons::create(card,27,34,58,&hourGlyphs[i]);
            txt=label(card,0,96,112,temp,&fonts::montserrat20);
            lv_obj_set_style_text_align(txt,LV_TEXT_ALIGN_CENTER,0);
            txt=label(card,0,136,112,rain,&fonts::montserrat14,theme::Blue);
            lv_obj_set_style_text_align(txt,LV_TEXT_ALIGN_CENTER,0);
        }
    }else{
        if(!snapshot.dayCount){
            label(hoursBox,26,55,700,snapshot.weatherError[0]?snapshot.weatherError:
                  "Cargando previsión de cuatro días...",&fonts::montserrat20,theme::Muted);
            return;
        }
        const char *weekday[]{"Dom","Lun","Mar","Mié","Jue","Vie","Sáb"};
        for(size_t i=0;i<snapshot.dayCount;++i){
            auto *card=lv_obj_create(hoursBox);
            lv_obj_set_pos(card,12+int(i)*182,9);lv_obj_set_size(card,172,173);
            lv_obj_set_style_bg_color(card,c(i==0?theme::Button:theme::Background),0);
            lv_obj_set_style_border_width(card,1,0);lv_obj_set_style_border_color(card,c(theme::Grid),0);lv_obj_set_style_radius(card,15,0);
            lv_obj_set_style_pad_all(card,0,0);lv_obj_clear_flag(card,LV_OBJ_FLAG_SCROLLABLE);
            const time_t t=snapshot.days[i].epoch;tm local{};localtime_r(&t,&local);
            char day[22],temp[28],rain[32];
            if(i==0)strlcpy(day,"Hoy",sizeof(day));
            else snprintf(day,sizeof(day),"%s %d",weekday[local.tm_wday],local.tm_mday);
            snprintf(temp,sizeof(temp),"%.0f° / %.0f°",snapshot.days[i].high,snapshot.days[i].low);
            if(snapshot.days[i].rain>=0)snprintf(rain,sizeof(rain),"Lluvia %d%%",snapshot.days[i].rain);
            else strlcpy(rain,"Lluvia --",sizeof(rain));
            auto *txt=label(card,0,8,172,day,&fonts::montserrat16,theme::Muted);
            lv_obj_set_style_text_align(txt,LV_TEXT_ALIGN_CENTER,0);
            dayGlyphs[i].code=snapshot.days[i].code;
            dayGlyphs[i].day=true;
            dayGlyphs[i].background=i==0?theme::Button:theme::Background;
            weathericons::create(card,55,30,62,&dayGlyphs[i]);
            txt=label(card,0,93,172,temp,&fonts::montserrat20);
            lv_obj_set_style_text_align(txt,LV_TEXT_ALIGN_CENTER,0);
            txt=label(card,4,123,164,shortWeatherText(snapshot.days[i].code),&fonts::montserrat14,theme::Muted);
            lv_obj_set_style_text_align(txt,LV_TEXT_ALIGN_CENTER,0);
            txt=label(card,0,148,172,rain,&fonts::montserrat14,theme::Blue);
            lv_obj_set_style_text_align(txt,LV_TEXT_ALIGN_CENTER,0);
        }
    }
}

}
void uiInit(){
    // lv_obj_clean() no borra los eventos del objeto pantalla: se registra una sola vez.
    lv_obj_add_event_cb(lv_scr_act(),sleepHomeBackground,LV_EVENT_CLICKED,nullptr);
    buildHome();
}
void uiShowSetup(){if(!portalActive())portalStart();buildSetup();}
void uiShowHome(){if(page==Page::Setup)buildHome();}
void uiTick(){
    const PendingPage next=pendingPage;
    pendingPage=PendingPage::None;
    switch(next){
        case PendingPage::Home: buildHome(); break;
        case PendingPage::Clock: buildClock(); break;
        case PendingPage::Users: buildUsers(); break;
        case PendingPage::Setup: portalStart(); buildSetup(); break;
        case PendingPage::CloseSetup: portalStop(); buildHome(); break;
        case PendingPage::None: break;
    }
    xSemaphoreTake(stateMutex,portMAX_DELAY);memcpy(&snapshot,sharedState,sizeof(snapshot));xSemaphoreGive(stateMutex);
    if(page==Page::Setup){if(portalMode()!=renderedPortal){buildSetup();return;}if(renderedPortal==PortalMode::WifiAccessPoint&&setupInfo){String text="Red temporal:  "+portalSsid()+"\n\nClave:  "+portalPassword()+"\n\nPortal:  http://192.168.4.1\n\nTras guardar se cerrará esta red y aparecerá un segundo QR.";lv_label_set_text(setupInfo,text.c_str());}return;}
    if(page==Page::Users){if(renderedConnections!=snapshot.connectionsFetched||renderedConnectionCount!=snapshot.connectionCount||strcmp(renderedConnectionsError,snapshot.connectionsError))buildUsers();return;}
    if(page==Page::Home && pendingPatientId[0]){
        if(strcmp(snapshot.activePatientId,pendingPatientId)==0 ||
           strcmp(snapshot.glucoseError,"No se pudo guardar el usuario seleccionado")==0){
            pendingPatientId[0]=0;
            resetRenderCache();
            lv_obj_clear_flag(graph,LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(arrow,LV_OBJ_FLAG_HIDDEN);
        }else{
            if(!lv_obj_has_flag(graph,LV_OBJ_FLAG_HIDDEN))lv_obj_add_flag(graph,LV_OBJ_FLAG_HIDDEN);
            if(!lv_obj_has_flag(arrow,LV_OBJ_FLAG_HIDDEN))lv_obj_add_flag(arrow,LV_OBJ_FLAG_HIDDEN);
            if(strcmp(lv_label_get_text(glucoseLabel),"---"))lv_label_set_text(glucoseLabel,"---");
            if(strcmp(lv_label_get_text(detailLabel),"Cambio pendiente"))lv_label_set_text(detailLabel,"Cambio pendiente");
            const char *waiting="Cambio en cola; espera HTTPS";
            if(strcmp(lv_label_get_text(errorLabel),waiting))lv_label_set_text(errorLabel,waiting);
            const int64_t waitingMinute=time(nullptr)/60;
            if(waitingMinute!=renderedMinute){
                renderedMinute=waitingMinute;
                setClock(timeLabel,dateLabel);
            }
            return;
        }
    }
    const int64_t now=time(nullptr),minute=now/60;
    const bool minuteChanged=minute!=renderedMinute;
    if(minuteChanged)renderedMinute=minute;
    const int64_t latestEpoch=snapshot.pointCount?snapshot.points[snapshot.pointCount-1].epoch:0;
    const int latestValue=snapshot.pointCount?snapshot.points[snapshot.pointCount-1].glucose:0;
    const bool glucoseChanged=renderedGlucose!=snapshot.glucoseFetched||renderedPoints!=snapshot.pointCount||
        renderedLatestEpoch!=latestEpoch||renderedLatestValue!=latestValue||strcmp(renderedGlucoseError,snapshot.glucoseError);
    const uint32_t requestElapsed=snapshot.glucoseRequestStartedMs ? millis()-snapshot.glucoseRequestStartedMs : 0;
    const int64_t requestStep=snapshot.glucoseRequestStartedMs ? requestElapsed/15000 : -1;
    const bool chartChanged=renderedPoints!=snapshot.pointCount||renderedLatestEpoch!=latestEpoch||renderedLatestValue!=latestValue;
    if(chartChanged&&selectedGraphEpoch){
        bool found=false;
        for(size_t i=0;i<snapshot.pointCount;++i)if(snapshot.points[i].epoch==selectedGraphEpoch){found=true;break;}
        if(!found)selectedGraphEpoch=0;
    }
    const bool weatherChanged=renderedWeather!=snapshot.weatherFetched||strcmp(renderedWeatherError,snapshot.weatherError);
    if(page==Page::Home){
        if(minuteChanged)setClock(timeLabel,dateLabel);
        if(weatherChanged)updateWeather(weatherLabel,weatherSummary,homeIcon,homeGlyph);
        if(glucoseChanged||minuteChanged){
            if(snapshot.pointCount){
                const auto &latest=snapshot.points[snapshot.pointCount-1];
                char value[12];snprintf(value,sizeof(value),"%d",latest.glucose);
                lv_label_set_text(glucoseLabel,value);
                lv_obj_set_style_text_color(glucoseLabel,c(gluco::stale(latest.epoch,now)?theme::Muted:rangeColor(latest.glucose)),0);
                lv_obj_set_style_bg_color(glucoseAccent,c(gluco::stale(latest.epoch,now)?theme::Muted:rangeColor(latest.glucose)),0);
                int diff=0,minutes=0;char detail[80];
                if(gluco::delta(snapshot.points,snapshot.pointCount,diff,minutes))
                    snprintf(detail,sizeof(detail),"%+d en %d min",diff,minutes);
                else strlcpy(detail,"Sin delta",sizeof(detail));
                lv_label_set_text(detailLabel,detail);
            }else{
                lv_label_set_text(glucoseLabel,"---");
                lv_label_set_text(detailLabel,"Sin lectura");
                lv_obj_set_style_bg_color(glucoseAccent,c(theme::Muted),0);
            }
            lv_obj_invalidate(arrow);
        }
        if(glucoseChanged||minuteChanged||requestStep!=renderedRequestStep){
            char status[180];
            if(snapshot.pointCount){
                time_t t=snapshot.points[snapshot.pointCount-1].epoch;tm local{};
                localtime_r(&t,&local);char stamp[20];strftime(stamp,sizeof(stamp),"%d/%m %H:%M",&local);
                if(snapshot.glucoseRequestStartedMs)
                    snprintf(status,sizeof(status),"Última lectura %s · Libre: %lu s",stamp,(unsigned long)(requestStep*15));
                else snprintf(status,sizeof(status),"Última lectura %s%s%s",stamp,
                    snapshot.glucoseError[0]?" · ":"",snapshot.glucoseError);
            }else if(snapshot.glucoseRequestStartedMs)
                snprintf(status,sizeof(status),"Libre: %lu s de espera",(unsigned long)(requestStep*15));
            else strlcpy(status,snapshot.glucoseError[0]?snapshot.glucoseError:"Esperando LibreLinkUp...",sizeof(status));
            lv_label_set_text(errorLabel,status);
        }
        renderedRequestStep=requestStep;
        // Reposicionar la gráfica cada diez minutos sin redibujarla cada minuto.
        const int64_t graphBucket=now/(10*60);
        if(chartChanged||(snapshot.pointCount&&graphBucket!=renderedGraphBucket))lv_obj_invalidate(graph);
        renderedGraphBucket=graphBucket;
    }else if(page==Page::Clock){
        if(minuteChanged)setClock(clockBig,clockDate);
        if(weatherChanged){
            updateWeather(clockWeather,clockSummary,clockIcon,clockGlyph);
            updateForecast();
        }
    }
    renderedGlucose=snapshot.glucoseFetched;renderedPoints=snapshot.pointCount;renderedLatestEpoch=latestEpoch;renderedLatestValue=latestValue;strlcpy(renderedGlucoseError,snapshot.glucoseError,sizeof(renderedGlucoseError));
    renderedWeather=snapshot.weatherFetched;strlcpy(renderedWeatherError,snapshot.weatherError,sizeof(renderedWeatherError));
}
