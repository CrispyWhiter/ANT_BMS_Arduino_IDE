#include "cell_page.h"

#include <stdio.h>

#include "app_config.h"
#include "ui/fonts.h"

namespace CellPage {
namespace {

namespace Color {
constexpr uint32_t Background = 0x000324;
constexpr uint32_t Panel = 0x05082A;
constexpr uint32_t Card = 0x030625;
constexpr uint32_t Border = 0x30405F;
constexpr uint32_t Title = 0x9BD74E;
constexpr uint32_t Normal = 0x00FFE9;
constexpr uint32_t Maximum = 0xFF863C;
constexpr uint32_t Minimum = 0xFFE200;
constexpr uint32_t Muted = 0x596076;
constexpr uint32_t White = 0xFFFFFF;
}

constexpr int kColumnCount = 3;
constexpr int kStartX = 4;
constexpr int kStartY = 64;
constexpr int kCardWidth = 102;
constexpr int kCardHeight = 22;
constexpr int kHorizontalGap = 3;
constexpr int kVerticalGap = 3;
constexpr uint8_t kVisibleCellCount = AppConfig::Display::VisibleCellCount;
constexpr uint8_t kMaximumGroupCount =
    (AppConfig::Protocol::MaxCellCount + kVisibleCellCount - 1U) /
    kVisibleCellCount;

static_assert(kVisibleCellCount == 21,
              "单体页面布局固定要求 VisibleCellCount == 21");
static_assert(kMaximumGroupCount == 2,
              "当前分页逻辑按最多 32 串、每页 21 串设计");

lv_obj_t *page = nullptr;
lv_obj_t *maximumValue = nullptr;
lv_obj_t *minimumValue = nullptr;
lv_obj_t *cellCountValue = nullptr;
lv_obj_t *pageIndicator = nullptr;
lv_obj_t *cellLabels[kVisibleCellCount] = {};

BmsData latestData{};
bool latestDataAvailable = false;
uint8_t activeGroup = 0;

lv_obj_t *createLabel(lv_obj_t *parent,
                      const char *text,
                      int x,
                      int y,
                      int width,
                      int height,
                      const lv_font_t *font,
                      uint32_t color,
                      lv_text_align_t alignment = LV_TEXT_ALIGN_CENTER) {
  lv_obj_t *label = lv_label_create(parent);
  lv_obj_set_pos(label, x, y);
  lv_obj_set_size(label, width, height);
  lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
  lv_label_set_text(label, text);
  lv_obj_set_style_text_font(label, font, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_color(label, lv_color_hex(color), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_text_align(label, alignment, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_clear_flag(label, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  return label;
}

lv_obj_t *createPanel(lv_obj_t *parent,
                      int x,
                      int y,
                      int width,
                      int height,
                      uint32_t background,
                      uint32_t border) {
  lv_obj_t *panel = lv_obj_create(parent);
  lv_obj_remove_style_all(panel);
  lv_obj_set_pos(panel, x, y);
  lv_obj_set_size(panel, width, height);
  lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(panel, lv_color_hex(background), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(panel, lv_color_hex(border), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(panel, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(panel, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(panel, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  return panel;
}

lv_obj_t *createCellCard(int x, int y) {
  lv_obj_t *label = createLabel(page,
                                "--",
                                x,
                                y,
                                kCardWidth,
                                kCardHeight,
                                &lv_font_montserrat_14,
                                Color::Normal,
                                LV_TEXT_ALIGN_LEFT);
  lv_obj_set_style_bg_color(label, lv_color_hex(Color::Card), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(label, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(label, lv_color_hex(Color::Border), LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(label, 1, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_radius(label, 3, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_left(label, 4, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_top(label, 2, LV_PART_MAIN | LV_STATE_DEFAULT);
  return label;
}

void createSummaryCard(int x, const char *title, lv_obj_t **valueTarget) {
  lv_obj_t *panel = createPanel(page, x, 26, 101, 35, Color::Panel, Color::Border);
  createLabel(panel, title, 0, 1, 101, 16, &ui_font_found_cn_16, Color::White);
  *valueTarget = createLabel(panel, "--", 0, 17, 101, 17, &lv_font_montserrat_14, Color::Normal);
}

void updateSummary(const BmsData &data) {
  char text[32];

  if (data.valid && data.maxCell > 0U) {
    snprintf(text, sizeof(text), "%.3fV C%02u", data.maxCellVoltage, data.maxCell);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  lv_label_set_text(maximumValue, text);
  lv_obj_set_style_text_color(maximumValue,
                              lv_color_hex(Color::Maximum),
                              LV_PART_MAIN | LV_STATE_DEFAULT);

  if (data.valid && data.minCell > 0U) {
    snprintf(text, sizeof(text), "%.3fV C%02u", data.minCellVoltage, data.minCell);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  lv_label_set_text(minimumValue, text);
  lv_obj_set_style_text_color(minimumValue,
                              lv_color_hex(Color::Minimum),
                              LV_PART_MAIN | LV_STATE_DEFAULT);

  if (data.valid) {
    snprintf(text, sizeof(text), "%uS", data.cellCount);
  } else {
    snprintf(text, sizeof(text), "--");
  }
  lv_label_set_text(cellCountValue, text);
}

uint8_t groupCountFor(const BmsData &data) {
  if (!data.valid || data.cellCount == 0U) return 1U;
  const uint8_t count = static_cast<uint8_t>(
      (data.cellCount + kVisibleCellCount - 1U) / kVisibleCellCount);
  return count > kMaximumGroupCount ? kMaximumGroupCount : count;
}

void updatePageIndicator(const BmsData &data) {
  if (pageIndicator == nullptr) return;
  char text[8];
  snprintf(text, sizeof(text), "%u/%u",
           static_cast<unsigned>(activeGroup + 1U),
           static_cast<unsigned>(groupCountFor(data)));
  lv_label_set_text(pageIndicator, text);
}

void updateCellCard(uint8_t slotIndex,
                    uint8_t absoluteIndex,
                    const BmsData &data) {
  lv_obj_t *label = cellLabels[slotIndex];
  if (label == nullptr) return;

  const bool beyondModel = absoluteIndex >= AppConfig::Protocol::MaxCellCount;
  const bool beyondActualOnLaterPage =
      activeGroup > 0U && data.valid && absoluteIndex >= data.cellCount;
  if (beyondModel || beyondActualOnLaterPage) {
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_clear_flag(label, LV_OBJ_FLAG_HIDDEN);

  const bool available = data.valid && absoluteIndex < data.cellCount;
  const bool isMaximum = available && (absoluteIndex + 1U) == data.maxCell;
  const bool isMinimum = available && (absoluteIndex + 1U) == data.minCell;

  uint32_t valueColor = available ? Color::Normal : Color::Muted;
  uint32_t borderColor = Color::Border;
  uint8_t borderWidth = 1;

  if (isMaximum) {
    valueColor = Color::Maximum;
    borderColor = Color::Maximum;
    borderWidth = 2;
  } else if (isMinimum) {
    valueColor = Color::Minimum;
    borderColor = Color::Minimum;
    borderWidth = 2;
  }

  char text[20];
  if (available) {
    snprintf(text, sizeof(text), "C%02u %.3f",
             absoluteIndex + 1U, data.cells[absoluteIndex]);
  } else {
    snprintf(text, sizeof(text), "C%02u --", absoluteIndex + 1U);
  }

  lv_label_set_text(label, text);
  lv_obj_set_style_text_color(label,
                              lv_color_hex(valueColor),
                              LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_color(label,
                                lv_color_hex(borderColor),
                                LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(label,
                                borderWidth,
                                LV_PART_MAIN | LV_STATE_DEFAULT);
}

void render(const BmsData &data) {
  if (page == nullptr) return;

  const uint8_t groupCount = groupCountFor(data);
  if (activeGroup >= groupCount) activeGroup = 0U;

  updateSummary(data);
  updatePageIndicator(data);
  const uint8_t firstCell = activeGroup * kVisibleCellCount;
  for (uint8_t slot = 0; slot < kVisibleCellCount; ++slot) {
    updateCellCard(slot, static_cast<uint8_t>(firstCell + slot), data);
  }
}

}

void create() {
  if (page != nullptr) return;

  page = lv_obj_create(nullptr);
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scrollbar_mode(page, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_bg_color(page,
                            lv_color_hex(Color::Background),
                            LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_bg_opa(page, LV_OPA_COVER, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_border_width(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);
  lv_obj_set_style_pad_all(page, 0, LV_PART_MAIN | LV_STATE_DEFAULT);

  createLabel(page, "单体电压", 90, 3, 140, 20, &ui_font_found_cn_16, Color::Title);
  pageIndicator = createLabel(page, "1/1", 246, 3, 66, 20,
                              &lv_font_montserrat_14, Color::Muted);
  createSummaryCard(4, "最高电压", &maximumValue);
  createSummaryCard(109, "最低电压", &minimumValue);
  createSummaryCard(214, "串数", &cellCountValue);

  for (uint8_t i = 0; i < kVisibleCellCount; ++i) {
    const int column = i % kColumnCount;
    const int row = i / kColumnCount;
    const int x = kStartX + column * (kCardWidth + kHorizontalGap);
    const int y = kStartY + row * (kCardHeight + kVerticalGap);
    cellLabels[i] = createCellCard(x, y);
  }

  BmsData emptyData;
  render(latestDataAvailable ? latestData : emptyData);
}

void update(const BmsData &data) {
  latestData = data;
  latestDataAvailable = true;
  render(data);
}

lv_obj_t *screen() {
  create();
  return page;
}

void showFirstGroup() {
  activeGroup = 0U;
  BmsData emptyData;
  render(latestDataAvailable ? latestData : emptyData);
}

bool showNextGroup() {
  BmsData emptyData;
  const BmsData &data = latestDataAvailable ? latestData : emptyData;
  const uint8_t count = groupCountFor(data);
  if (activeGroup + 1U < count) {
    ++activeGroup;
    render(data);
    return true;
  }

  activeGroup = 0U;
  render(data);
  return false;
}

}
