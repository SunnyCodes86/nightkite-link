#include "ShowInput.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <string>
#include <map>
#include <vector>
using namespace NightKiteShow;
static void eq(const char* actual, const char* expected) { assert(actual && !strcmp(actual, expected)); }
static Event parsed(const char* s) { char b[LINE_SIZE]; strcpy(b,s); Event e; assert(!parseEvent(b,e)); return e; }
static void codec() {
  Packet p; p.event = parsed("SINGLE ABC123 SEGMENT 42 1 15 10 0 0 255");
  p.id=0x1234; p.senderMs=0x12345678; p.event.executeAt=0x12345a00;
  uint8_t b[ADV_SIZE]; assert(encode(p,b,sizeof(b))); assert(sizeof(b)==31 && PACKET_SIZE==24);
  // Identical to nightkite-multi/test/show_control/test_show_control.cpp.
  const uint8_t golden[] = {0x4e,0x53,0x01,0x87,0x23,0xc1,0xab,0x34,0x12,0x78,0x56,0x34,0x12,0x00,0x5a,0x2a,0x01,0x0f,0x0a,0x00,0x00,0xff,0x73,0x85};
  assert(!memcmp(b+7,golden,24)); const uint8_t framing[]={2,1,6,27,255,255,255}; assert(!memcmp(b,framing,7));
  Packet d; assert(decode(b+7,24,d)); assert(d.id==p.id && d.senderMs==p.senderMs && d.event.executeAt==p.event.executeAt);
  for (int i=0;i<24;++i) { b[7+i]^=1; assert(!decode(b+7,24,d)); b[7+i]^=1; }
  assert(!decode(b+7,23,d)); assert(!decode(nullptr,24,d)); assert(!encode(p,b,30));
  const char* valid[]={"ALL PATTERN 1","GROUP 255 PATTERN 27","SINGLE 000000 BRIGHTNESS 1","ALL SOLID 255 0 128 0","ALL BLACKOUT","ALL RELEASE","ALL CLEAR 255 32","ALL SEGMENT 1 31 69 1 255 255 255","ALL APPLY 255"};
  for (auto s:valid) { p.event=parsed(s); p.senderMs=0xfffffff0; p.event.executeAt=p.senderMs+30000; assert(encode(p,b,31)); assert(decode(b+7,24,d)); assert(d.event.executeAt==p.event.executeAt); }
  for (int lead: {-250,0,1,327,30000}) { p.event.executeAt=p.senderMs+lead; assert(encode(p,b,31)); assert(decode(b+7,24,d)); assert(d.event.executeAt==p.event.executeAt); }
  p.event.executeAt=p.senderMs+30001; assert(!encode(p,b,31)); p.event.executeAt=p.senderMs-251; assert(!encode(p,b,31));
  p=Packet{}; p.event.command=Command::Clock; assert(encode(p,b,31)); p.id=1; assert(!encode(p,b,31));
  const char* invalid[]={"ALL PATTERN 0","ALL PATTERN 28","ALL BRIGHTNESS 0","ALL SOLID 0 0 0 256","ALL SOLID 0 0 0","ALL BLACKOUT 0","ALL RELEASE 1","ALL CLEAR 0 33","ALL SEGMENT 1 32 0 1 0 0 0","ALL SEGMENT 1 0 0 0 0 0 0","ALL SEGMENT 1 0 255 255 0 0 0","GROUP 0 BLACKOUT","GROUP 256 BLACKOUT","SINGLE FFFFFFG BLACKOUT","SINGLE ABC12 BLACKOUT","NONE BLACKOUT","ALL CLOCK","ALL APPLY -1","ALL PATTERN 42949672960"};
  for (auto s:invalid) { char buf[LINE_SIZE]; strcpy(buf,s); Event e; assert(parseEvent(buf,e)); }
}
static uint32_t ready(Engine& e, uint32_t now=0, uint16_t id=65535) {
  e.arm(now,id); assert(e.state()==State::Arming);
  for (unsigned i=0;i<5000;++i,++now) { auto t=e.next(now,true); e.sent(t,now,true); if(e.ready()) { assert(i>=3000); return now; } }
  assert(false); return 0;
}
static void capacity() {
  assert(LOOKAHEAD_MS==5000 && SHOW_START_LEAD_MS==5000 && RECEIVER_BUDGET==8);
  assert(SHOW_TX_COST==3 && capacityTx(true)==51 && capacityTx(false)==69 && CAPACITY_WINDOW_MS==5000);
  CapacityEvent burst[16];
  for(unsigned i=0;i<8;++i) burst[i].executeAt=5000+i*20;
  assert(!capacityFeasible(burst,8,0,true));
  for(unsigned i=0;i<8;++i) burst[i].executeAt=5000+i*50;
  assert(!capacityFeasible(burst,8,0,true));
  for(unsigned i=0;i<8;++i) burst[i].executeAt=1000+i*20;
  eq(capacityFeasible(burst,8,0,true),"capacity");
  for(unsigned i=0;i<9;++i) burst[i].executeAt=5000;
  assert(!capacityFeasible(burst,8,0,true));
  eq(capacityFeasible(burst,9,0,true),"capacity");
  burst[0].executeAt=5000; for(unsigned i=1;i<9;++i) burst[i].executeAt=5451;
  assert(!capacityFeasible(burst,9,0,false));
  eq(capacityFeasible(burst,9,0,true),"capacity");
  for(unsigned i=0;i<8;++i) burst[i].executeAt=5000;
  for(unsigned i=8;i<16;++i) burst[i].executeAt=5451;
  eq(capacityFeasible(burst,16,0,false),"capacity");

  TimelineCapacity audio; audio.reset(true,SHOW_START_LEAD_MS);
  for(unsigned i=0;i<17;++i) assert(!audio.admit(i*250));
  eq(audio.admit(4250),"capacity");
  TimelineCapacity audioLong; audioLong.reset(true,SHOW_START_LEAD_MS);
  for(unsigned i=0;i<100;++i) assert(!audioLong.admit(i*300));
  TimelineCapacity noAudio; noAudio.reset(false,SHOW_START_LEAD_MS);
  for(unsigned i=0;i<23;++i) assert(!noAudio.admit(i*170));
  eq(noAudio.admit(3910),"capacity");

  // Capacity is global per command: ALL/GROUP each cost one event and five
  // separate SINGLE commands cost five, with no receiver-count multiplier.
  TimelineCapacity targets; targets.reset(true,SHOW_START_LEAD_MS);
  for(unsigned i=0;i<7;++i) assert(!targets.admit(i*50));
  assert(!targets.admit(350));
}
static void engine() {
  Engine e; uint32_t now=ready(e,0xfffff000); uint16_t a,b,c;
  e.setAudioActive(true);
  Event one=parsed("SINGLE ABCDEF SOLID 255 0 0 255"); one.executeAt=now+1000;
  assert(!e.enqueue(one,now,a)); one.target.value=0x123456; assert(!e.enqueue(one,now,b)); one.target.kind=TargetKind::All; one.target.value=0; assert(!e.enqueue(one,now,c));
  assert(a==65535 && b==0 && c==1); uint16_t no=777; eq(e.enqueue(one,now,no),"capacity"); assert(no==777 && e.depth()==3 && e.counters.capacityRejects==1);
  std::map<uint16_t,std::vector<Packet>> copies;
  for(unsigned i=0;i<1500;++i,++now) { auto t=e.next(now,true); if(t.kind==TxKind::Event) copies[t.packet.id].push_back(t.packet); e.sent(t,now,true); }
  for(auto id:{a,b,c}) { assert(copies[id].size()==3); for(auto& p:copies[id]) { assert(p.event.executeAt==one.executeAt); assert(!memcmp(p.event.params,one.params,7)); assert(delta(p.event.executeAt,p.senderMs)>=40); } }
  assert(e.depth()==0 && e.counters.maxAudioGap<=120 && e.counters.maxClockGap<=240 && !e.counters.missed);
  e.setAudioActive(false);
  for(unsigned i=0;i<QUEUE_SIZE;++i) { one.executeAt=now+2000+i*1500; assert(!e.enqueue(one,now,a)); }
  one.executeAt=now+2000+QUEUE_SIZE*1500; eq(e.enqueue(one,now,a),"queue_full"); assert(e.depth()==32);
  // Long timeline remains local: only one event can enter receiver lookahead here.
  for(unsigned i=0;i<1000;++i,++now) { auto t=e.next(now,true); if(t.kind==TxKind::Event) assert(delta(t.packet.event.executeAt,now)<=int32_t(LOOKAHEAD_MS)); e.sent(t,now,true); }
  const uint32_t remoteDue=e.counters.lastExecuteAt;
  e.stop(now,false); assert(e.state()==State::Stopping && e.depth()==1); assert(delta(e.nextTime(),remoteDue)>0);
  const uint32_t stopDue=e.nextTime(); e.stop(now+1,true); assert(e.nextTime()==stopDue);
  while(delta(now,stopDue)<=300) { auto t=e.next(now,true); if(t.kind==TxKind::Event) assert(t.packet.event.command==Command::Release && t.packet.event.executeAt==stopDue); e.sent(t,now,true); ++now; }
  assert(e.state()==State::Off);
  ready(e,now,123); now+=5000; e.stop(now,true);
  for(unsigned i=0;i<2000;++i,++now) { auto t=e.next(now,false); e.sent(t,now,false); }
  assert(e.state()==State::Error && e.counters.radioErrors && e.counters.missed);
  Engine stalled; stalled.arm(0,7); for(uint32_t t=0;t<1000;++t) { auto tx=stalled.next(t,false); stalled.sent(tx,t,true); }
  stalled.next(5000,false); assert(!stalled.ready());
  for(uint32_t t=5001;t<7900;++t) { auto tx=stalled.next(t,false); stalled.sent(tx,t,true); assert(!stalled.ready()); }
  Engine late; now=ready(late); one.executeAt=now+800; assert(!late.enqueue(one,now,a)); late.next(now+2000,false); assert(late.counters.missed==1 && late.depth()==0);
  // Start seeds are provided by hardware RNG, never a global monotonically pinned ID.
  Engine replacement; now=ready(replacement,now,12); one.executeAt=now+1000; assert(!replacement.enqueue(one,now,a) && a==12);

  Engine burstEngine; now=ready(burstEngine); burstEngine.setAudioActive(true);
  for(unsigned i=0;i<8;++i) { one.executeAt=now+SHOW_START_LEAD_MS+i*20; assert(!burstEngine.enqueue(one,now,a)); }
  for(unsigned i=0;i<SHOW_START_LEAD_MS+1000;++i) { auto tx=burstEngine.next(now+i,true); burstEngine.sent(tx,now+i,true); }
  assert(burstEngine.counters.events==8*REPEATS && !burstEngine.counters.missed && !burstEngine.depth());
}
struct Memory {
  std::string data; size_t pos=0;
  static int read(void* p) { auto& m=*static_cast<Memory*>(p); return m.pos==m.data.size()?-1:uint8_t(m.data[m.pos++]); }
  static bool rewind(void* p) { static_cast<Memory*>(p)->pos=0; return true; }
  Reader reader() { Reader r; r.context=this; r.read=read; r.rewind=rewind; return r; }
};
static void files() {
  const char* invalid[]={"","NKSHOW 2\n0 ALL BLACKOUT","NKSHOW 1\n","NKSHOW 1\n0 ALL PATTERN 28", "NKSHOW 1\n2 ALL BLACKOUT\n1 ALL RELEASE", "NKSHOW 1\n42949672960 ALL RELEASE", "NKSHOW 1\n0 SINGLE 123 RELEASE", "NKSHOW 1\n0 ALL CLEAR 1 1\n2000 ALL APPLY 1", "NKSHOW 1\n0 ALL CLEAR 1 1", "NKSHOW 1\n0 ALL CLEAR 1 1\n2000 ALL SEGMENT 2 0 0 1 0 0 0", "NKSHOW 1\n0 ALL CLEAR 1 1\n2000 ALL SEGMENT 1 0 0 1 0 0 0\n4000 ALL SEGMENT 1 0 0 1 0 0 0"};
  Engine e;
  for(auto s:invalid) { Memory m; m.data=s; Player p; p.load(m.reader()); for(int i=0;i<100;++i) p.tick(e,0); assert(p.state()==PlayerState::Error && !e.depth()); }
  for(auto s:{std::string(500,'x'),std::string("NKSHOW 1\n0 ALL BLACKOUT\x01")}) { Memory m; m.data=s; Player p; p.load(m.reader()); for(int i=0;i<10;++i)p.tick(e,0);assert(p.state()==PlayerState::Error); }
  Memory m; m.data="# example\r\nNKSHOW 1\nNAME Test show # comment\n0 ALL PATTERN 6\n2000 SINGLE ABCDEF SOLID 255 0 0 255\n2000 SINGLE 123456 SOLID 0 0 255 255\n4000 ALL CLEAR 1 1\n4500 ALL SEGMENT 1 0 0 8 255 0 0\n5000 ALL APPLY 1\n7000 ALL RELEASE";
  Player p; p.load(m.reader()); for(int i=0;i<100;++i)p.tick(e,0); assert(p.state()==PlayerState::Loaded && p.eventCount()==7 && !strcmp(p.name(),"Test show"));
  uint32_t now=ready(e); assert(!p.play(e,now) && p.state()==PlayerState::Validating);
  while(p.state()==PlayerState::Validating) { p.tick(e,now); ++now; }
  assert(p.state()==PlayerState::Playing); const uint32_t showStart=now-1+SHOW_START_LEAD_MS, start=now; unsigned packets=0;
  while(!e.depth()) { p.tick(e,now); ++now; }
  assert(e.nextTime()==showStart);
  for(;now<start+SHOW_START_LEAD_MS+8500;++now) { p.tick(e,now); auto tx=e.next(now,true); if(tx.kind==TxKind::Event)++packets; e.sent(tx,now,true); assert(e.depth()<=8); assert(p.state()!=PlayerState::Error); }
  assert(p.state()==PlayerState::End && packets==21);
  assert(!p.play(e,now)); p.tick(e,now); p.tick(e,now); p.stop(e,now); assert(p.state()==PlayerState::Loaded && e.state()==State::Stopping);
  // A linear preflight with fixed storage, then only one pending line at playback.
  Memory longShow; longShow.data="NKSHOW 1\n"; for(unsigned i=0;i<10000;++i)longShow.data+=std::to_string(i*2000)+" ALL PATTERN 6\n";
  Player longPlayer; longPlayer.load(longShow.reader()); for(unsigned i=0;i<10000 && longPlayer.state()==PlayerState::Validating;++i)longPlayer.tick(e,0); assert(longPlayer.state()==PlayerState::Loaded && longPlayer.eventCount()==10000); assert(sizeof(Player)<800);

  Memory profile; profile.data="NKSHOW 1\n";
  for(unsigned i=0;i<20;++i) profile.data+=std::to_string(i*250)+" ALL BLACKOUT\n";
  Player profilePlayer; profilePlayer.load(profile.reader(),false);
  for(unsigned i=0;i<100 && profilePlayer.state()==PlayerState::Validating;++i) profilePlayer.tick(e,0);
  assert(profilePlayer.state()==PlayerState::Loaded);
  Engine profileEngine; now=ready(profileEngine); profileEngine.setAudioActive(true);
  assert(!profilePlayer.play(profileEngine,now));
  for(unsigned i=0;i<100 && profilePlayer.state()==PlayerState::Validating;++i) profilePlayer.tick(profileEngine,now+i);
  assert(profilePlayer.state()==PlayerState::Error && !strcmp(profilePlayer.error(),"capacity") && profileEngine.depth()==0);

  Memory image; image.data="NKSHOW 1\n0 ALL CLEAR 7 15\n";
  for(unsigned i=0;i<15;++i) image.data+=std::to_string((i+1)*250)+" ALL SEGMENT 7 "+std::to_string(i)+" "+std::to_string(i)+" 1 1 2 3\n";
  image.data+="4000 ALL APPLY 7\n4250 ALL BLACKOUT\n";
  Player imagePlayer; imagePlayer.load(image.reader(),true);
  for(unsigned i=0;i<100 && imagePlayer.state()==PlayerState::Validating;++i) imagePlayer.tick(e,0);
  assert(imagePlayer.state()==PlayerState::Error && !strcmp(imagePlayer.error(),"capacity"));
}
static void requests() {
  Request r; assert(!parseRequest("NKSHOW 1 123 EVENT AT 42 GROUP 2 PATTERN 27",0xfffffff0,r)); assert(r.id==123 && r.event.executeAt==42 && r.event.params[0]==27);
  assert(!parseRequest("NKSHOW 1 124 EVENT IN 1000 ALL BLACKOUT",0xfffffff0,r)); assert(r.event.executeAt==984);
  assert(!parseRequest("NKSHOW 1 125 EVENT NOW ALL RELEASE",50,r)); assert(r.event.executeAt==1050);
  const char* ops[]={"HELLO","VERSION","ARM","DISARM","STATUS","TIME","STOP","PLAY","LIST","AUDIO MIC_FULL","LOAD demo.nks","PUT_BEGIN demo.nks","PUT_LINE NKSHOW 1","PUT_END", "EVENT IN 1000 ALL CLEAR 2 1", "EVENT IN 1000 ALL SEGMENT 2 0 0 8 255 0 0", "EVENT IN 1000 ALL APPLY 2", "EVENT IN 1000 ALL BRIGHTNESS 255", "EVENT IN 1000 ALL SOLID 1 2 3 0"};
  for(auto op:ops) { auto s=std::string("NKSHOW 1 7 ")+op; assert(!parseRequest(s.c_str(),0,r)); }
  const char* bad[]={"NKSHOW 2 1 HELLO","NKSHOW 1 4294967296 HELLO","NKSHOW 1 1 ARM extra","NKSHOW 1 1 EVENT AT -1 ALL BLACKOUT","NKSHOW 1 1 EVENT IN 9999999999 ALL BLACKOUT","NKSHOW 1 1 EVENT NOW SINGLE 00000Z BLACKOUT","NKSHOW 1 1 EVENT NOW ALL BLAC", "NKSHOW 1 1 AUDIO", "NKSHOW 1 1 LOAD a b"};
  for(auto s:bad) assert(parseRequest(s,0,r)); assert(parseRequest(std::string(224,'x').c_str(),0,r)); assert(!parseRequest("NKSHOW 1 2 HELLO",0,r));
  RequestCache cache; const char* response=nullptr; const char* text="NKSHOW 1 12 EVENT NOW ALL BLACKOUT";
  assert(!cache.find(12,text,response)); cache.remember(12,text,"OK event_id=65535"); assert(cache.find(12,text,response)==1 && !strcmp(response,"OK event_id=65535")); assert(cache.find(12,"different",response)==-1);
  for(unsigned i=0;i<16;++i)cache.remember(100+i,"new","OK"); assert(!cache.find(12,text,response));
  // All front ends share Event and Engine. File and USB produce the same event as Live.
  Event live=parsed("ALL PATTERN 6"); live.executeAt=1000;
  FileParser f; FileLine l; char h[]="NKSHOW 1", line[]="1000 ALL PATTERN 6"; assert(!f.line(h,l)); assert(!f.line(line,l)); assert(!parseRequest("NKSHOW 1 9 EVENT AT 1000 ALL PATTERN 6",0,r));
  Packet a,b; a.event=live; b.event=l.value; uint8_t x[31],y[31]; assert(encode(a,x,31)&&encode(b,y,31)&&!memcmp(x,y,31)); b.event=r.event; assert(encode(b,y,31)&&!memcmp(x,y,31));
}
int main() { codec(); capacity(); engine(); files(); requests(); puts("Show codec/capacity/clock/engine/player/parser/cache: PASS"); }
