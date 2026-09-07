#include <cassert>
struct DisplayDevice {};
struct Unified { DisplayDevice Display; } M5;
// Force dynamic initialization, as in the real Cardputer constructor.
DisplayDevice& initializeDisplay() {
  static volatile unsigned initialized = 0;
  ++initialized;
  return M5.Display;
}
struct Cardputer { DisplayDevice& Display = initializeDisplay(); };
extern Cardputer M5Cardputer;
struct M5Canvas {
  explicit M5Canvas(DisplayDevice* display) : parent(display) {}
  DisplayDevice* parent;
};
// Use the actual firmware binding with the observed ESP32 constructor order:
// application canvas first, Cardputer reference initialization afterwards.
#include "canvas_under_test.inc"
Cardputer M5Cardputer;
int main() { assert(uiCanvas.parent == &M5.Display); }
