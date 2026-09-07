#include "ShowRuntime.h"
#include <assert.h>
#include <string.h>
#include <string>
uint32_t hostMs=0;
static bool armed=false;
static bool arm(){armed=true;return true;}
static void off(){armed=false;}
static bool advertise(const uint8_t* p,size_t n){assert(n==31 && p[7]=='N' && p[8]=='S');return true;}
static bool storage(){return true;}
struct Console:Stream {
 std::string input,output;size_t pos=0;
 int available()override{return input.size()-pos;}
 int read()override{return pos<input.size()?input[pos++]:-1;}
 int availableForWrite()override{return 7;} // Partial writes must preserve exact framing.
 size_t write(const uint8_t* p,size_t n)override{output.append((const char*)p,n);return n;}
};
int main(){
 ShowRuntime r;fs::FS sd;ShowRuntime::Hardware hw={arm,off,advertise,nullptr,nullptr,storage};r.begin(sd,hw);
 Console c;
 auto send=[&](const std::string& line){c.input+=line+"\n";c.output.clear();for(int i=0;i<400;++i){r.tick(&c);++hostMs;}return c.output;};
 auto out=send("NKSHOW 1 1 HELLO");assert(out.find("NKSHOW 1 1 OK")!=std::string::npos && out.back()=='\n');
 out=send(std::string(300,'x'));assert(out.find("ERROR code=line_length")!=std::string::npos);
 out=send("NKSHOW 1 2 TIME");assert(out.find("NKSHOW 1 2 OK")!=std::string::npos);
 assert(send("NKSHOW 1 3 PUT_BEGIN demo.nks").find(" OK ")!=std::string::npos);
 assert(send("NKSHOW 1 4 PUT_LINE NKSHOW 1").find(" OK ")!=std::string::npos);
 const auto repeated=send("NKSHOW 1 4 PUT_LINE NKSHOW 1");assert(repeated.find(" OK ")!=std::string::npos);
 assert(send("NKSHOW 1 5 PUT_LINE 0 ALL PATTERN 6").find(" OK ")!=std::string::npos);
 assert(send("NKSHOW 1 6 PUT_END").find(" OK ")!=std::string::npos);
 assert(sd.exists("/shows/demo.nks") && *sd.files["/shows/demo.nks"]=="NKSHOW 1\n0 ALL PATTERN 6\n");
 assert(send("NKSHOW 1 7 PUT_BEGIN demo.nks").find("code=exists")!=std::string::npos);
 assert(send("NKSHOW 1 8 LOAD ../demo.nks").find("code=filename")!=std::string::npos);
 assert(send("NKSHOW 1 9 LOAD demo.nks").find(" OK ")!=std::string::npos);assert(r.player.state()==NightKiteShow::PlayerState::Loaded);
 out=send("NKSHOW 1 10 ARM");assert(armed&&out.find("state=ARMING")!=std::string::npos);
 for(int i=0;i<4000;++i){r.tick(&c);++hostMs;}assert(r.engine.ready());
 out=send("NKSHOW 1 11 EVENT NOW ALL PATTERN 23");assert(out.find("event_id=65535")!=std::string::npos);assert(send("NKSHOW 1 11 EVENT NOW ALL PATTERN 23")==out);
 assert(r.engine.depth()==1);assert(send("NKSHOW 1 11 EVENT NOW ALL PATTERN 24").find("code=request_conflict")!=std::string::npos);
 send("NKSHOW 1 12 DISARM");for(int i=0;i<2500;++i){r.tick(&c);++hostMs;}assert(!armed&&!r.engine.active());
 assert(send("NKSHOW 1 13 PUT_BEGIN bad.nks").find(" OK ")!=std::string::npos);assert(send("NKSHOW 1 14 PUT_LINE NKSHOW 2").find("code=version")!=std::string::npos);assert(!sd.exists("/shows/bad.nks")&&!sd.exists("/shows/bad.nks.part"));
 puts("Show runtime USB framing/idempotence/upload/player: PASS");
}
