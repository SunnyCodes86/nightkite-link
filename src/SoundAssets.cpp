#include "SoundAssets.h"

namespace NightKiteSoundAssets {

namespace {

const SoundStep STARTUP_STEPS[] = {
    {0, 115, 330.0f, 392.0f, 660.0f, 0.88f},
    {105, 135, 494.0f, 523.25f, 988.0f, 0.86f},
    {225, 165, 659.25f, 740.0f, 1318.5f, 0.82f},
    {360, 150, 990.0f, 880.0f, 1480.0f, 0.50f},
};

const SoundStep KEY_TEXT_STEPS_1[] = {
    {0, 26, 520.0f, 460.0f, 780.0f, 0.46f},
};

const SoundStep KEY_TEXT_STEPS_2[] = {
    {0, 24, 680.0f, 610.0f, 1020.0f, 0.44f},
};

const SoundStep KEY_TEXT_STEPS_3[] = {
    {0, 30, 820.0f, 700.0f, 1230.0f, 0.48f},
};

const SoundStep KEY_TEXT_STEPS_4[] = {
    {0, 16, 930.0f, 820.0f, 1395.0f, 0.36f},
    {18, 18, 1030.0f, 900.0f, 1545.0f, 0.30f},
};

const SoundStep KEY_TEXT_STEPS_5[] = {
    {0, 27, 1250.0f, 1080.0f, 1875.0f, 0.38f},
    {14, 16, 760.0f, 680.0f, 1140.0f, 0.18f},
};

const SoundStep NAVIGATE_STEPS_1[] = {
    {0, 46, 720.0f, 930.0f, 1080.0f, 0.82f},
};

const SoundStep NAVIGATE_STEPS_2[] = {
    {0, 42, 640.0f, 820.0f, 960.0f, 0.78f},
};

const SoundStep PAGE_CHANGE_STEPS[] = {
    {0, 54, 520.0f, 720.0f, 1040.0f, 0.76f},
    {48, 66, 760.0f, 1040.0f, 1520.0f, 0.66f},
};

const SoundStep QUEUE_TICK_STEPS[] = {
    {0, 58, 680.0f, 520.0f, 340.0f, 0.72f},
};

const SoundStep TRANSFER_COMPLETE_STEPS[] = {
    {0, 92, 720.0f, 880.0f, 1080.0f, 0.58f},
    {82, 118, 920.0f, 1250.0f, 1380.0f, 0.54f},
};

const SoundStep CONFIRM_STEPS[] = {
    {0, 82, 560.0f, 660.0f, 1120.0f, 0.78f},
    {75, 105, 760.0f, 980.0f, 1520.0f, 0.74f},
};

const SoundStep CANCEL_STEPS[] = {
    {0, 78, 430.0f, 330.0f, 860.0f, 0.78f},
    {70, 105, 290.0f, 210.0f, 580.0f, 0.70f},
};

const SoundStep SUCCESS_STEPS[] = {
    {0, 105, 392.0f, 440.0f, 784.0f, 0.72f},
    {95, 125, 523.25f, 587.33f, 1046.5f, 0.76f},
    {210, 165, 659.25f, 880.0f, 1318.5f, 0.72f},
};

const SoundStep ERROR_STEPS[] = {
    {0, 115, 310.0f, 230.0f, 620.0f, 0.86f},
    {125, 155, 230.0f, 155.0f, 460.0f, 0.82f},
};

}  // namespace

const SoundClip startupClip = {STARTUP_STEPS, sizeof(STARTUP_STEPS) / sizeof(STARTUP_STEPS[0]), 540, 0.86f};
const SoundClip keyClip = {KEY_TEXT_STEPS_1, sizeof(KEY_TEXT_STEPS_1) / sizeof(KEY_TEXT_STEPS_1[0]), 34, 0.46f};
const SoundClip keyClips[KEY_VARIANT_COUNT] = {
    {KEY_TEXT_STEPS_1, sizeof(KEY_TEXT_STEPS_1) / sizeof(KEY_TEXT_STEPS_1[0]), 34, 0.46f},
    {KEY_TEXT_STEPS_2, sizeof(KEY_TEXT_STEPS_2) / sizeof(KEY_TEXT_STEPS_2[0]), 32, 0.44f},
    {KEY_TEXT_STEPS_3, sizeof(KEY_TEXT_STEPS_3) / sizeof(KEY_TEXT_STEPS_3[0]), 38, 0.46f},
    {KEY_TEXT_STEPS_4, sizeof(KEY_TEXT_STEPS_4) / sizeof(KEY_TEXT_STEPS_4[0]), 42, 0.44f},
    {KEY_TEXT_STEPS_5, sizeof(KEY_TEXT_STEPS_5) / sizeof(KEY_TEXT_STEPS_5[0]), 36, 0.42f},
};
const SoundClip navigateClip = {NAVIGATE_STEPS_1, sizeof(NAVIGATE_STEPS_1) / sizeof(NAVIGATE_STEPS_1[0]), 54, 0.72f};
const SoundClip navigateClips[NAVIGATE_VARIANT_COUNT] = {
    {NAVIGATE_STEPS_1, sizeof(NAVIGATE_STEPS_1) / sizeof(NAVIGATE_STEPS_1[0]), 54, 0.72f},
    {NAVIGATE_STEPS_2, sizeof(NAVIGATE_STEPS_2) / sizeof(NAVIGATE_STEPS_2[0]), 50, 0.72f},
};
const SoundClip pageChangeClip = {PAGE_CHANGE_STEPS, sizeof(PAGE_CHANGE_STEPS) / sizeof(PAGE_CHANGE_STEPS[0]), 124, 0.78f};
const SoundClip queueTickClip = {QUEUE_TICK_STEPS, sizeof(QUEUE_TICK_STEPS) / sizeof(QUEUE_TICK_STEPS[0]), 62, 0.64f};
const SoundClip transferCompleteClip = {
    TRANSFER_COMPLETE_STEPS, sizeof(TRANSFER_COMPLETE_STEPS) / sizeof(TRANSFER_COMPLETE_STEPS[0]), 220, 0.68f};
const SoundClip confirmClip = {CONFIRM_STEPS, sizeof(CONFIRM_STEPS) / sizeof(CONFIRM_STEPS[0]), 190, 0.78f};
const SoundClip cancelClip = {CANCEL_STEPS, sizeof(CANCEL_STEPS) / sizeof(CANCEL_STEPS[0]), 190, 0.74f};
const SoundClip successClip = {SUCCESS_STEPS, sizeof(SUCCESS_STEPS) / sizeof(SUCCESS_STEPS[0]), 405, 0.76f};
const SoundClip errorClip = {ERROR_STEPS, sizeof(ERROR_STEPS) / sizeof(ERROR_STEPS[0]), 315, 0.80f};

}  // namespace NightKiteSoundAssets
