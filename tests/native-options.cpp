#include "native-options.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
// Validate user input and rolling history, including stopped and missing seconds.
int main(){
 // Web-only HTTP API must never receive controller focus.
 for(unsigned i=0;i<native::screen_option_count;i++)assert(native::screen_field(i)!=3);
 assert(native::adjacent_field(2,1)==6&&native::adjacent_field(4,-1)==6);
 assert(native::adjacent_field(0,-1)==5&&native::adjacent_field(5,1)==0);
 native::Options active,pending=active;
 assert(!native::edit(pending,0,"16")&&pending.threads==16&&active.threads==8);
 for(auto s:{"0","17","-1","8x"," 8","99999999999999999999"})assert(native::edit(pending,0,s));
 assert(pending.threads==16);
 assert(!native::edit(pending,1,"pool.example:3333"));
 assert(!native::edit(pending,1,"[::1]:3333"));
 for(auto s:{"pool","host:0","host:65536",":3333","http://host:3333","::1:3333","host :3333","[]:3333"})assert(native::edit(pending,1,s));
 assert(!native::edit(pending,2,"worker-1"));assert(native::edit(pending,2,""));assert(native::edit(pending,2,"user\n"));
 assert(!native::edit(pending,3,"off")&&!pending.http);
 assert(!native::edit(pending,3,"auto")&&pending.http&&pending.port==0);
 assert(!native::edit(pending,3,"12345")&&pending.port==12345);
 assert(native::edit(pending,3,"65536")&&pending.port==12345);
 assert(!native::edit(pending,4,"0"));assert(!native::edit(pending,4,"99"));assert(native::edit(pending,4,"100"));
 // Rig ID is optional, bounded, and independent of unconfirmed active settings.
 assert(!native::edit(pending,5,"ps5-rig")&&!strcmp(pending.rig_id,"ps5-rig")&&!*active.rig_id);
 assert(native::edit(pending,5,"bad\nrig"));char oversized[193];memset(oversized,'x',192);oversized[192]=0;assert(native::edit(pending,5,oversized));
 assert(!native::edit(pending,5,""));char display[192];native::field(pending,5,display,sizeof(display),true);assert(!strcmp(display,"Default"));
 // Proxy editing can select a validated endpoint or restore disabled/null.
 assert(!native::edit(pending,6,"10.1.1.111:8964")&&!strcmp(pending.proxy,"10.1.1.111:8964"));
 assert(native::edit(pending,6,"host:0")&&native::edit(pending,6,"http://host:8964"));
 assert(!native::edit(pending,6,"off")&&!*pending.proxy);assert(!native::edit(pending,6,""));
 native::field(pending,6,display,sizeof(display),true);assert(!strcmp(display,"Off"));
 native::HourAverage h;
 for(unsigned s=100;s<3699;s++)h.add(s,2000);
 assert(!h.ready());h.add(3699,2000);assert(h.ready()&&std::abs(h.value()-2000)<1e-6);
 for(unsigned s=3700;s<5500;s++)h.add(s,0);
 assert(std::abs(h.value()-1000)<1e-6);h.add(10000,4000);assert(std::abs(h.value()-4000.0/3600)<1e-6);
 std::cout<<"Options validation and hour average passed\n";
}
