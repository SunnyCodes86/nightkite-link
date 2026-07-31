#include <assert.h>

#include "NightKitePlayback.h"
#include "Nk4LineParser.h"

int main()
{
  const Nk4Line ok = parseNk4Line("NK4 seq=17 ok section=config brightness=159 pattern=7");
  assert(ok.valid && ok.ok && !ok.error && ok.sequence == 17);
  assert(ok.value("brightness") == "159" && ok.value("pattern") == "7");

  const Nk4Line event = parseNk4Line("NK4 event=status pattern=8");
  assert(event.valid && event.event && event.sequence == -1);

  const Nk4Line error = parseNk4Line("NK4 seq=18 err code=busy msg=retry");
  assert(error.error && error.value("code") == "busy");

  const Nk4Line play = parseNk4Line(
      "NK4 seq=19 ok section=play play_mode=autoplay boot_mode=last autoplay_enabled=1 autoplay_interval=30");
  assert(play.value("play_mode") == NightKitePlayback::PLAY_MODES[1]);
  assert(play.value("boot_mode") == NightKitePlayback::BOOT_MODES[0]);
  assert(play.value("autoplay_enabled") == "1" && play.value("autoplay_interval") == "30");
  assert(NightKitePlayback::AUTOPLAY_INTERVALS[4] == 30);
  assert(!parseNk4Line("legacy response").valid);
  return 0;
}
