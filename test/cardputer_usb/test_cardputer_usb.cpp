#include <assert.h>
#include <stdint.h>
static bool bridgeMode=false;
static const int SERIAL_BAUD=115200;
struct { bool usbBridge = false; } linkSettings;
#define PERIPH_RCC_ATOMIC() if (true)
static unsigned deviceClockEnables=0;
static void usb_serial_jtag_ll_enable_bus_clock(bool enable) {
 assert(enable);++deviceClockEnables;
}
static unsigned devicePhySelections=0;
static void usb_serial_jtag_ll_phy_enable_external(bool external) {
 assert(!external && deviceClockEnables==1);++devicePhySelections;
}
struct DeviceDriver {
 unsigned starts=0;
 void setRxBufferSize(int) {}
 void setTxBufferSize(int) {}
 void setTxTimeoutMs(int timeout) { assert(timeout == 0); }
 void begin(int) { assert(devicePhySelections==1); ++starts; }
} bridgeSerial;
#include "usb_role_under_test.inc"
struct Driver {
 bool initialized=false, fail=false;
 unsigned starts=0, probes=0;
 bool begin(int,int,int,int){++starts;initialized=!fail;return initialized;}
 explicit operator bool(){assert(initialized);++probes;return false;}
};
struct Transport {
 Driver hostSerial;
 bool started=false;
#include "usb_transport_under_test.inc"
};
int main(){
 // PC connected: every existing connection poll must safely return disconnected.
 linkSettings.usbBridge=true;beginUsbRole();assert(bridgeMode && bridgeSerial.starts==1);Transport bridge;
 for(unsigned i=0;i<100;++i)assert(!bridge.connected());
 assert(!bridge.hostSerial.starts&&!bridge.hostSerial.probes);
 // No PC: preserve the lazy USB host initialization and normal connection query.
 bridgeSerial.starts=0;devicePhySelections=0;deviceClockEnables=0;linkSettings.usbBridge=false;beginUsbRole();assert(!bridgeMode && !bridgeSerial.starts && !devicePhySelections && !deviceClockEnables);Transport host;
 assert(!host.connected());assert(host.started&&host.hostSerial.starts==1&&host.hostSerial.probes==1);
 assert(!host.connected());assert(host.hostSerial.starts==1&&host.hostSerial.probes==2);
 Transport failed;failed.hostSerial.fail=true;assert(!failed.connected());assert(!failed.hostSerial.probes);
}
