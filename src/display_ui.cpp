#include "display_ui.h"

#include <LovyanGFX.hpp>
#include <lvgl.h>
#include <esp_arduino_version.h>

#if LVGL_VERSION_MAJOR != 8
#error "This sketch requires LVGL 8.4.x. Remove LVGL 9.x and install LVGL 8.4.0 from Arduino Library Manager."
#endif

#ifndef ANT_BMS_LV_CONF_MEMORY_OPTIMIZED
#error "LVGL did not load this project's optimized lv_conf.h. Copy lv_conf.h to the LVGL library parent directory or configure LV_CONF_INCLUDE_SIMPLE correctly."
#endif

#include "app_config.h"
#include "cell_page.h"
#include "core/runtime_profile.h"
#include "presentation/main_page.h"
#include "ui/screens.h"
#include "ui/ui.h"

namespace DisplayUi {
namespace {

class DisplayDevice : public lgfx::LGFX_Device {
 public:
  DisplayDevice() {
    configureBus();
    configurePanel();
    setPanel(&panel_);
  }

 private:

  void configureBus() {
    auto config = bus_.config();
    config.spi_host = SPI2_HOST;
    config.spi_mode = 0;
    config.freq_write = AppConfig::Display::SpiWriteFrequencyHz;
    config.freq_read = AppConfig::Display::SpiReadFrequencyHz;
    config.spi_3wire = false;
    config.use_lock = true;
    config.dma_channel = 0;
    config.pin_sclk = AppConfig::Pins::TftSclk;
    config.pin_mosi = AppConfig::Pins::TftMosi;
    config.pin_miso = AppConfig::Pins::TftMiso;
    config.pin_dc = AppConfig::Pins::TftDc;
    bus_.config(config);
    panel_.setBus(&bus_);
  }

  void configurePanel() {
    auto config = panel_.config();
    config.pin_cs = AppConfig::Pins::TftCs;
    config.pin_rst = AppConfig::Pins::TftReset;
    config.pin_busy = -1;

    config.panel_width = 240;
    config.panel_height = 320;
    config.memory_width = 240;
    config.memory_height = 320;
    config.offset_x = 0;
    config.offset_y = 0;
    config.offset_rotation = 4;
    config.readable = false;
    config.invert = AppConfig::Display::InvertColors;
    config.rgb_order = AppConfig::Display::RgbOrderBgr;
    config.dlen_16bit = false;
    config.bus_shared = false;
    panel_.config(config);
  }

  lgfx::Panel_ST7789 panel_;
  lgfx::Bus_SPI bus_;
};

DisplayDevice lcd;
lv_disp_draw_buf_t drawBufferDescriptor;

lv_color_t drawBuffer[AppConfig::Display::Width *
                      AppConfig::Display::LvglBufferLines];
uint16_t transferBuffer[AppConfig::Display::Width];
lv_disp_drv_t displayDriver;
lv_disp_t *lvglDisplay = nullptr;

bool initialized = false;
bool enabled = true;
bool backlightPwmReady = false;
uint8_t requestedBrightnessPercent = 100U;
Page activePage = Page::Main;
uint32_t lastLvglTickAt = 0;

void resetPanel() {
  pinMode(AppConfig::Pins::TftReset, OUTPUT);
  digitalWrite(AppConfig::Pins::TftReset, HIGH);
  delay(AppConfig::Display::ResetHighBeforePulseMs);
  digitalWrite(AppConfig::Pins::TftReset, LOW);
  delay(AppConfig::Display::ResetLowPulseMs);
  digitalWrite(AppConfig::Pins::TftReset, HIGH);
  delay(AppConfig::Display::ResetRecoveryMs);
}

void flushDisplay(lv_disp_drv_t *driver,
                  const lv_area_t *area,
                  lv_color_t *pixels) {
  if (enabled && area != nullptr && pixels != nullptr) {
    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;
    lcd.pushImage(area->x1,
                  area->y1,
                  width,
                  height,
                  reinterpret_cast<const lgfx::rgb565_t *>(pixels));
  }

  lv_disp_flush_ready(driver);
}

uint8_t brightnessToDuty(uint8_t percent) {
  if (percent > 100U) percent = 100U;
  const uint16_t logicalDuty = static_cast<uint16_t>(percent) * 255U / 100U;
  return AppConfig::Pins::TftBacklightOn == HIGH
             ? static_cast<uint8_t>(logicalDuty)
             : static_cast<uint8_t>(255U - logicalDuty);
}

void writeBacklightPercent(uint8_t percent) {
  const uint8_t duty = brightnessToDuty(percent);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  if (backlightPwmReady) {
    ledcWrite(AppConfig::Pins::TftBacklight, duty);
    return;
  }
#else
  if (backlightPwmReady) {
    ledcWrite(0, duty);
    return;
  }
#endif
  const bool on = percent > 0U;
  const uint8_t active = AppConfig::Pins::TftBacklightOn;
  digitalWrite(AppConfig::Pins::TftBacklight,
               on ? active : (active == HIGH ? LOW : HIGH));
}

void configureBacklightPwm() {
  pinMode(AppConfig::Pins::TftBacklight, OUTPUT);
#if ESP_ARDUINO_VERSION_MAJOR >= 3
  backlightPwmReady = ledcAttach(
      AppConfig::Pins::TftBacklight,
      AppConfig::Display::BacklightPwmFrequencyHz,
      AppConfig::Display::BacklightPwmResolutionBits);
#else
  ledcSetup(0,
            AppConfig::Display::BacklightPwmFrequencyHz,
            AppConfig::Display::BacklightPwmResolutionBits);
  ledcAttachPin(AppConfig::Pins::TftBacklight, 0);
  backlightPwmReady = true;
#endif
}

void configureHardwarePins() {
  configureBacklightPwm();

  writeBacklightPercent(0U);

  pinMode(AppConfig::Pins::TftCs, OUTPUT);
  digitalWrite(AppConfig::Pins::TftCs, HIGH);
}


void applyPanelProfile() {
  lcd.fillScreen(TFT_BLACK);

  size_t lineOffset = 0;
  uint16_t lineY = 0;
  const uint16_t width = AppConfig::Display::Width;
  const uint16_t height = AppConfig::Display::Height;

  for (uint16_t packetIndex = 0; packetIndex < RuntimeProfile::spanCount(); ++packetIndex) {
    uint32_t packet = RuntimeProfile::spanAt(packetIndex);
    uint16_t count = static_cast<uint16_t>(packet >> 16);
    const uint16_t color = static_cast<uint16_t>(packet & 0xFFFFu);

    while (count > 0 && lineY < height) {
      const uint16_t remaining = static_cast<uint16_t>(width - lineOffset);
      const uint16_t chunk = count < remaining ? count : remaining;
      for (uint16_t index = 0; index < chunk; ++index) {
        transferBuffer[lineOffset + index] = color;
      }

      lineOffset = static_cast<uint16_t>(lineOffset + chunk);
      count = static_cast<uint16_t>(count - chunk);

      if (lineOffset == width) {
        lcd.pushImage(0, lineY, width, 1, reinterpret_cast<const lgfx::rgb565_t *>(transferBuffer));
        lineOffset = 0;
        ++lineY;
      }
    }
  }

  if (lineOffset > 0 && lineY < height) {
    for (uint16_t fill = lineOffset; fill < width; ++fill) transferBuffer[fill] = 0u;
    lcd.pushImage(0, lineY, width, 1, reinterpret_cast<const lgfx::rgb565_t *>(transferBuffer));
  }

  delay(RuntimeProfile::settleWindowMs());
}

void initializeLvglDisplay() {
  lv_init();
  lv_disp_draw_buf_init(&drawBufferDescriptor,
                        drawBuffer,
                        nullptr,
                        AppConfig::Display::Width *
                            AppConfig::Display::LvglBufferLines);

  lv_disp_drv_init(&displayDriver);
  displayDriver.hor_res = AppConfig::Display::Width;
  displayDriver.ver_res = AppConfig::Display::Height;
  displayDriver.flush_cb = flushDisplay;
  displayDriver.draw_buf = &drawBufferDescriptor;
  displayDriver.full_refresh = 0;
  lvglDisplay = lv_disp_drv_register(&displayDriver);
}

void refreshNow() {
  if (lvglDisplay == nullptr) return;
  lv_obj_invalidate(lv_scr_act());
  lv_refr_now(lvglDisplay);
}

}

bool begin() {
  if (initialized) return true;

  configureHardwarePins();
  resetPanel();
  if (!lcd.init()) return false;

  lcd.setRotation(AppConfig::Display::Rotation);
  lcd.setSwapBytes(AppConfig::Display::SwapColorBytes);
  writeBacklightPercent(requestedBrightnessPercent);
  applyPanelProfile();

  initializeLvglDisplay();
  ui_init();
  MainPage::begin();
  CellPage::update(BmsData{});

  initialized = true;
  showPage(Page::Main);
  lastLvglTickAt = millis();
  refreshNow();

  writeBacklightPercent(requestedBrightnessPercent);
  return true;
}

void loop() {
  if (!initialized) return;

  const uint32_t now = millis();
  const uint32_t elapsed = now - lastLvglTickAt;
  if (elapsed > 0U) {
    lv_tick_inc(elapsed);
    lastLvglTickAt = now;
  }

  MainPage::loop(now);

  if (enabled) lv_timer_handler();
}

void update(const BmsData &data) {
  if (!initialized) return;
  MainPage::update(data);
  CellPage::update(data);
}

void updatePreview(const BmsData &data,
                   float previewRangeKm,
                   float previewPowerW) {
  if (!initialized) return;
  MainPage::updatePreview(data, previewRangeKm, previewPowerW);
  CellPage::update(data);
}

void setConnected(bool connected) {
  if (!initialized) return;
  MainPage::setConnected(connected);
}

void showPage(Page page) {
  if (!initialized) return;

  lv_obj_t *targetScreen = nullptr;
  switch (page) {
    case Page::Main:
      targetScreen = objects.main;
      break;
    case Page::Cells:
      targetScreen = CellPage::screen();
      break;
  }

  if (targetScreen == nullptr) return;
  activePage = page;
  lv_scr_load_anim(targetScreen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}

void togglePage() {
  if (!initialized || !enabled) return;

  if (activePage == Page::Main) {
    CellPage::showFirstGroup();
    showPage(Page::Cells);
    return;
  }

  if (!CellPage::showNextGroup()) showPage(Page::Main);
}

void setBrightnessPercent(uint8_t percent) {
  if (percent > 100U) percent = 100U;
  requestedBrightnessPercent = percent;
  if (initialized && enabled) writeBacklightPercent(requestedBrightnessPercent);
}

void setEnabled(bool state) {
  if (!initialized || enabled == state) return;

  enabled = state;
  if (!enabled) {
    writeBacklightPercent(0U);
    delay(AppConfig::Display::SleepCommandSettleMs);
    lcd.sleep();
    return;
  }

  lcd.wakeup();
  delay(AppConfig::Display::WakeupSettleMs);
  writeBacklightPercent(requestedBrightnessPercent);
  refreshNow();
}

bool isEnabled() {
  return enabled;
}

}
