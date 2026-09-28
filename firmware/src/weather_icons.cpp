#include "weather_icons.hpp"
#include "theme.hpp"
#include <algorithm>

namespace weathericons {
namespace {
void disk(lv_draw_ctx_t *ctx, int x, int y, int radius, uint32_t color) {
    lv_draw_rect_dsc_t desc; lv_draw_rect_dsc_init(&desc);
    desc.bg_color = lv_color_hex(color);
    desc.radius = LV_RADIUS_CIRCLE;
    lv_area_t area{lv_coord_t(x-radius), lv_coord_t(y-radius),
                   lv_coord_t(x+radius), lv_coord_t(y+radius)};
    lv_draw_rect(ctx, &desc, &area);
}
void segment(lv_draw_ctx_t *ctx, int x1, int y1, int x2, int y2,
             uint32_t color, int width = 3) {
    lv_draw_line_dsc_t desc; lv_draw_line_dsc_init(&desc);
    desc.color = lv_color_hex(color); desc.width = width;
    desc.round_start = true; desc.round_end = true;
    lv_point_t from{lv_coord_t(x1), lv_coord_t(y1)}, to{lv_coord_t(x2), lv_coord_t(y2)};
    lv_draw_line(ctx, &desc, &from, &to);
}
void sun(lv_draw_ctx_t *ctx, int x, int y, int scale) {
    disk(ctx, x, y, scale * 8 / 10, theme::Yellow);
    const int dx[8]={10,7,0,-7,-10,-7,0,7};
    const int dy[8]={0,7,10,7,0,-7,-10,-7};
    const int inner=scale*12/10,outer=scale*17/10;
    for (int i = 0; i < 8; ++i) {
        segment(ctx, x+dx[i]*inner/10, y+dy[i]*inner/10,
                x+dx[i]*outer/10, y+dy[i]*outer/10,
                theme::Yellow, std::max(2,scale/6));
    }
}
void moon(lv_draw_ctx_t *ctx, int x, int y, int scale, uint32_t background) {
    disk(ctx, x, y, scale, theme::Yellow);
    disk(ctx, x + scale * 6 / 10, y - scale * 5 / 10, scale, background);
}
void cloud(lv_draw_ctx_t *ctx, int x, int y, int scale, uint32_t color) {
    disk(ctx,x-scale,y+scale/4,scale*7/10,color);
    disk(ctx,x,y-scale/4,scale,color);
    disk(ctx,x+scale,y+scale/6,scale*8/10,color);
    lv_draw_rect_dsc_t desc;lv_draw_rect_dsc_init(&desc);
    desc.bg_color=lv_color_hex(color);desc.radius=scale/3;
    lv_area_t body{lv_coord_t(x-scale*16/10),lv_coord_t(y),
                   lv_coord_t(x+scale*16/10),lv_coord_t(y+scale*8/10)};
    lv_draw_rect(ctx,&desc,&body);
}
void draw(lv_event_t *event) {
    const auto *spec=static_cast<const Icon *>(lv_event_get_user_data(event));
    if(!spec)return;
    lv_area_t bounds;lv_obj_get_coords(lv_event_get_target(event),&bounds);
    auto *ctx=lv_event_get_draw_ctx(event);
    const int side=std::min(lv_area_get_width(&bounds),lv_area_get_height(&bounds));
    const int unit=std::max(9,side/5);
    const int x=(bounds.x1+bounds.x2)/2,y=(bounds.y1+bounds.y2)/2;
    const int code=spec->code;
    const bool fog=code==45||code==48;
    const bool rain=(code>=51&&code<=67)||(code>=80&&code<=82);
    const bool snow=(code>=71&&code<=77)||(code>=85&&code<=86);
    const bool storm=code>=95;
    const bool clear=code==0;
    const bool partly=code==1||code==2;
    if(clear){
        if(spec->day)sun(ctx,x,y,unit);
        else moon(ctx,x,y,unit,spec->background);
        return;
    }
    if(partly){
        if(spec->day)sun(ctx,x-unit,y-unit/2,unit*7/10);
        else moon(ctx,x-unit,y-unit/2,unit*8/10,spec->background);
    }
    cloud(ctx,x+(partly?unit/4:0),y-(rain||snow||fog||storm?unit/3:0),unit,
          fog?theme::Muted:(partly?theme::Ink:0xB9C8D5));
    if(fog){
        for(int i=0;i<3;++i)segment(ctx,x-unit*13/10,y+unit+i*unit/3,
                                     x+unit*13/10,y+unit+i*unit/3,theme::Muted,2);
    }else if(rain){
        for(int i=-1;i<=1;++i)segment(ctx,x+i*unit*7/10,y+unit*8/10,
                                      x+i*unit*7/10-unit/5,y+unit*14/10,theme::Blue,3);
    }else if(snow){
        for(int i=-1;i<=1;++i){
            const int px=x+i*unit*7/10,py=y+unit*12/10;
            segment(ctx,px-3,py,px+3,py,theme::Ink,2);
            segment(ctx,px,py-3,px,py+3,theme::Ink,2);
        }
    }else if(storm){
        segment(ctx,x,y+unit/2,x-unit/3,y+unit*12/10,theme::Yellow,4);
        segment(ctx,x-unit/3,y+unit*12/10,x+unit/5,y+unit*12/10,theme::Yellow,4);
        segment(ctx,x+unit/5,y+unit*12/10,x-unit/4,y+unit*17/10,theme::Yellow,4);
    }
}
}

lv_obj_t *create(lv_obj_t *parent,int x,int y,int size,Icon *data){
    auto *obj=lv_obj_create(parent);
    lv_obj_set_pos(obj,x,y);lv_obj_set_size(obj,size,size);
    lv_obj_set_style_bg_opa(obj,LV_OPA_TRANSP,0);
    lv_obj_set_style_border_width(obj,0,0);
    lv_obj_set_style_pad_all(obj,0,0);
    lv_obj_set_style_shadow_width(obj,0,0);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj,LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(obj,draw,LV_EVENT_DRAW_MAIN,data);
    return obj;
}
}
