#include "../firmware/include/core.hpp"
#include "../firmware/include/protocol.hpp"
#include <cassert>
int main(){
    using gluco::Range;
    assert(gluco::range(69)==Range::LowRed);
    assert(gluco::range(70)==Range::Green);
    assert(gluco::range(180)==Range::Green);
    assert(gluco::range(181)==Range::HighYellow);
    assert(gluco::range(240)==Range::HighYellow);
    assert(gluco::range(241)==Range::VeryHighOrange);
    assert(gluco::factoryEpoch("2026-09-24T12:30:00Z")==1790253000);
    gluco::Point p[]{{1790253000,100},{1790252700,95},{1790253000,101}};
    assert(gluco::normalize(p,3,1790253000)==2);
    assert(gluco::kHistorySeconds==8*60*60);
    assert(gluco::kMaxPoints==120);
    int delta=0,minutes=0;assert(gluco::delta(p,2,delta,minutes));assert(delta==6&&minutes==5);
}
