#include "ShowRuntime.h"
#include <assert.h>
#include <string.h>
#include <string>
uint32_t hostMs=0;
static bool armed=false;
static bool audioOn=false;
static bool arm(){armed=true;return true;}
static void off(){armed=false;}
static bool advertise(const uint8_t* p,size_t n){assert(p&&(n==31||n==29));return true;}
static bool audio(NightKiteSync::BeaconInput& input){input.version=NightKiteSync::VERSION_V2;return audioOn;}
static const char* audioMode(const char* mode){if(!strcmp(mode,"OFF")){audioOn=false;return nullptr;}if(!strcmp(mode,"MANUAL")){audioOn=true;return nullptr;}return "audio_mode";}
static bool storage(){return true;}
static void audioStatus(char* fields,size_t size){snprintf(fields,size,"sr=16000 fft=512");}
struct Console:Stream {
 std::string input,output;size_t pos=0;
 int available()override{return input.size()-pos;}
 int read()override{return pos<input.size()?input[pos++]:-1;}
 int availableForWrite()override{return 7;} // Partial writes must preserve exact framing.
 size_t write(const uint8_t* p,size_t n)override{output.append((const char*)p,n);return n;}
};
int main(){
 ShowRuntime r;fs::FS sd;ShowRuntime::Hardware hw={arm,off,advertise,audio,audioMode,storage,audioStatus};r.begin(sd,hw);
 Console c;
 auto send=[&](const std::string& line){c.input+=line+"\n";c.output.clear();for(int i=0;i<400;++i){r.tick(&c);++hostMs;}return c.output;};
 auto out=send("NKSHOW 1 1 HELLO");assert(out.find("NKSHOW 1 1 OK")!=std::string::npos && out.find("lookahead_ms=5000")!=std::string::npos && out.back()=='\n');
 out=send(std::string(300,'x'));assert(out.find("ERROR code=line_length")!=std::string::npos);
 out=send("NKSHOW 1 2 TIME");assert(out.find("NKSHOW 1 2 OK")!=std::string::npos);
 out=send("NKSHOW 1 20 STATUS");assert(out.find("capacity_profile=no_audio")!=std::string::npos&&out.find("capacity_tx=69")!=std::string::npos);
 out=send("NKSHOW 1 21 AUDIO MANUAL");assert(out.find(" OK ")!=std::string::npos);
 out=send("NKSHOW 1 23 AUDIO_STATUS");assert(out.find("sr=16000 fft=512")!=std::string::npos);
 out=send("NKSHOW 1 22 STATUS");assert(out.find("capacity_profile=audio")!=std::string::npos&&out.find("capacity_tx=51")!=std::string::npos);
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
