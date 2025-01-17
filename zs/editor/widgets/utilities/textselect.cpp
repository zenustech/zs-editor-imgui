// Copyright 2024 Aidan Sun
// SPDX-License-Identifier: MIT

#define IMGUI_DEFINE_MATH_OPERATORS

#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <string>
#include <string_view>
#include <utility>

#include "utf8.h"
#include <imgui.h>
#include <imgui_internal.h>

#include "textselect.hpp"

// Simple word boundary detection, accounts for Latin Unicode blocks only.
static bool isBoundary(utf8::utfchar32_t c) {
  using Range = std::pair<utf8::utfchar32_t, utf8::utfchar32_t>;
  std::array ranges{
      Range{0x20, 0x2F}, Range{0x3A, 0x40}, Range{0x5B, 0x5E},
      Range{0x60, 0x60}, Range{0x7B, 0xBF}, Range{0xD7, 0xF7},
  };

  return std::find_if(ranges.begin(), ranges.end(), [c](const Range &r) {
           return c >= r.first && c <= r.second;
         }) != ranges.end();
}

// Gets the number of UTF-8 characters (not bytes) in a string.
static size_t utf8Length(std::string_view s) {
  return utf8::unchecked::distance(s.begin(), s.end());
}

// Gets the display width of a substring.
static float substringSizeX(std::string_view s, size_t start,
                            size_t length = std::string_view::npos) {
  // Convert char-based start and length into byte-based iterators
  auto stringStart = s.begin();
  utf8::unchecked::advance(stringStart, start);

  auto stringEnd = stringStart;
  if (length == std::string_view::npos)
    stringEnd = s.end();
  else
    utf8::unchecked::advance(stringEnd, std::min(utf8Length(s), length));

  // Calculate text size between start and end
  return ImGui::CalcTextSize(&*stringStart,
                             &*stringStart + (stringEnd - stringStart))
      .x;
}

constexpr size_t midpoint(size_t __a, size_t __b) noexcept {
  using T = std::make_signed_t<size_t>;
  return __a > __b ? __a - T((__a - __b) / 2) : __a + T((__b - __a) / 2);
}

// Gets the index of the character the mouse cursor is over.
static size_t getCharIndex(std::string_view s, float cursorPosX, size_t start,
                           size_t end) {
  // Ignore cursor position when it is invalid
  if (cursorPosX < 0)
    return 0;

  // Check for exit conditions
  if (s.empty())
    return 0;
  if (end < start)
    return utf8Length(s);

  // Midpoint of given string range
  size_t midIdx = midpoint(start, end);
  // size_t midIdx = start + (end - start) / 2;

  // Display width of the entire string up to the midpoint, gives the x-position
  // where the (midIdx + 1)th char starts
  float widthToMid = substringSizeX(s, 0, midIdx + 1);

  // Same as above but exclusive, gives the x-position where the (midIdx)th char
  // starts
  float widthToMidEx = substringSizeX(s, 0, midIdx);

  // Perform a recursive binary search to find the correct index
  // If the mouse position is between the (midIdx)th and (midIdx + 1)th
  // character positions, the search ends
  if (cursorPosX < widthToMidEx)
    return getCharIndex(s, cursorPosX, start, midIdx - 1);
  else if (cursorPosX > widthToMid)
    return getCharIndex(s, cursorPosX, midIdx + 1, end);
  else
    return midIdx;
}

// Wrapper for getCharIndex providing the initial bounds.
static size_t getCharIndex(std::string_view s, float cursorPosX) {
  return getCharIndex(s, cursorPosX, 0, utf8Length(s));
}

// Gets the scroll delta for the given cursor position and window bounds.
static float getScrollDelta(float v, float min, float max) {
  const float deltaScale = 10.0f * ImGui::GetIO().DeltaTime;
  const float maxDelta = 100.0f;

  if (v < min)
    return std::max(-(min - v), -maxDelta) * deltaScale;
  else if (v > max)
    return std::min(v - max, maxDelta) * deltaScale;

  return 0.0f;
}

TextSelect::Selection TextSelect::getSelection() const {
  // Start and end may be out of order (ordering is based on Y position)
  bool startBeforeEnd =
      selectStart.y < selectEnd.y ||
      (selectStart.y == selectEnd.y && selectStart.x < selectEnd.x);

  // Reorder X points if necessary
  size_t startX = startBeforeEnd ? selectStart.x : selectEnd.x;
  size_t endX = startBeforeEnd ? selectEnd.x : selectStart.x;

  // Get min and max Y positions for start and end
  size_t startY = std::min(selectStart.y, selectEnd.y);
  size_t endY = std::max(selectStart.y, selectEnd.y);

  return {startX, startY, endX, endY};
}

void TextSelect::handleMouseDown(const ImVec2 &cursorPosStart) {
  const float textHeight = ImGui::GetTextLineHeightWithSpacing();
  ImVec2 mousePos = ImGui::GetMousePos() - cursorPosStart;

  // Get Y position of mouse cursor, in terms of line number (capped to the
  // index of the last line)
  const size_t numLines = getNumLines();
  if (numLines == 0)
    return;
  size_t y = std::min(static_cast<size_t>(std::floor(mousePos.y / textHeight)),
                      numLines - 1);
  if (y < 0)
    return;

  std::string_view currentLine = getLineAtIdx(y);
  size_t x = getCharIndex(currentLine, mousePos.x);

  // Get mouse click count and determine action
  if (int mouseClicks = ImGui::GetMouseClickedCount(ImGuiMouseButton_Left);
      mouseClicks > 0) {
    if (mouseClicks % 3 == 0) {
      // Triple click - select line
      selectStart = {0, y};
      selectEnd = {utf8Length(currentLine), y};
    } else if (mouseClicks % 2 == 0) {
      // Double click - select word
      // Initialize start and end iterators to current cursor position
      utf8::unchecked::iterator startIt{currentLine.data()};
      utf8::unchecked::iterator endIt{currentLine.data()};
      for (size_t i = 0; i < x; i++) {
        startIt++;
        endIt++;
      }

      // Scan to left until a word boundary is reached
      for (size_t startInv = 0; startInv <= x; startInv++) {
        if (isBoundary(*startIt))
          break;
        selectStart = {x - startInv, y};
        startIt--;
      }

      // Scan to right until a word boundary is reached
      for (size_t end = x; end <= utf8Length(currentLine); end++) {
        selectEnd = {end, y};
        if (isBoundary(*endIt))
          break;
        endIt++;
      }

      // If a boundary character was double-clicked, select that character
      if (selectEnd.x == selectStart.x)
        selectEnd.x++;
    } else if (ImGui::IsKeyDown(ImGuiMod_Shift)) {
      // Single click with shift - select text from start to click
      // The selection starts from the beginning if no start position exists
      if (selectStart.isInvalid())
        selectStart = {0, 0};

      selectEnd = {x, y};
    } else {
      // Single click - set start position, invalidate end position
      selectStart = {x, y};
      selectEnd = {std::string_view::npos, std::string_view::npos};
    }
  } else if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    // Mouse dragging - set end position
    selectEnd = {x, y};
  }
}

void TextSelect::handleScrolling() const {
  // Window boundaries
  ImVec2 windowMin = ImGui::GetWindowPos();
  ImVec2 windowMax = windowMin + ImGui::GetWindowSize();

  // Get current and active window information from Dear ImGui state
  ImGuiWindow *currentWindow = ImGui::GetCurrentWindow();
  const ImGuiWindow *activeWindow = GImGui->ActiveIdWindow;

  ImGuiID scrollXID = ImGui::GetWindowScrollbarID(currentWindow, ImGuiAxis_X);
  ImGuiID scrollYID = ImGui::GetWindowScrollbarID(currentWindow, ImGuiAxis_Y);
  ImGuiID activeID = ImGui::GetActiveID();
  bool scrollbarsActive = activeID == scrollXID || activeID == scrollYID;

  // Do not handle scrolling if:
  // - There is no active window
  // - The current window is not active
  // - The user is scrolling via the scrollbars
  if (activeWindow == nullptr || activeWindow->ID != currentWindow->ID ||
      scrollbarsActive)
    return;

  // Get scroll deltas from mouse position
  ImVec2 mousePos = ImGui::GetMousePos();
  float scrollXDelta = getScrollDelta(mousePos.x, windowMin.x, windowMax.x);
  float scrollYDelta = getScrollDelta(mousePos.y, windowMin.y, windowMax.y);

  // If there is a nonzero delta, scroll in that direction
  if (std::abs(scrollXDelta) > 0.0f)
    ImGui::SetScrollX(ImGui::GetScrollX() + scrollXDelta);
  if (std::abs(scrollYDelta) > 0.0f)
    ImGui::SetScrollY(ImGui::GetScrollY() + scrollYDelta);
}

void TextSelect::drawSelection(const ImVec2 &cursorPosStart) const {
  if (!hasSelection())
    return;

  // Start and end positions
  auto [startX, startY, endX, endY] = getSelection();

  size_t numLines = getNumLines();
  if (startY >= numLines || endY >= numLines)
    return;

  // Add a rectangle to the draw list for each line contained in the selection
  for (size_t i = startY; i <= endY; i++) {
    std::string_view line = getLineAtIdx(i);

    // Display sizes
    // The width of the space character is used for the width of newlines.
    const float newlineWidth = ImGui::CalcTextSize(" ").x;
    const float textHeight = ImGui::GetTextLineHeightWithSpacing();

    // The first and last rectangles should only extend to the selection
    // boundaries The middle rectangles (if any) enclose the entire line + some
    // extra width for the newline.
    float minX = i == startY ? substringSizeX(line, 0, startX) : 0;
    float maxX = i == endY ? substringSizeX(line, 0, endX)
                           : substringSizeX(line, 0) + newlineWidth;

    // Rectangle height equals text height
    float minY = static_cast<float>(i) * textHeight;
    float maxY = static_cast<float>(i + 1) * textHeight;

    // Get rectangle corner points offset from the cursor's start position in
    // the window
    ImVec2 rectMin = cursorPosStart + ImVec2{minX, minY};
    ImVec2 rectMax = cursorPosStart + ImVec2{maxX, maxY};

    // Draw the rectangle
    ImU32 color = ImGui::GetColorU32(ImGuiCol_TextSelectedBg);
    ImGui::GetWindowDrawList()->AddRectFilled(rectMin, rectMax, color);
  }
}

void TextSelect::copy() const {
  if (!hasSelection())
    return;

  auto [startX, startY, endX, endY] = getSelection();

  // Collect selected text in a single string
  std::string selectedText;

  for (size_t i = startY; i <= endY; i++) {
    // Similar logic to drawing selections
    size_t subStart = i == startY ? startX : 0;
    std::string_view line = getLineAtIdx(i);

    auto stringStart = line.begin();
    utf8::unchecked::advance(stringStart, subStart);

    auto stringEnd = stringStart;
    if (i == endY)
      utf8::unchecked::advance(stringEnd, endX - subStart);
    else
      stringEnd = line.end();

    selectedText +=
        line.substr(stringStart - line.begin(), stringEnd - stringStart);
  }

  ImGui::SetClipboardText(selectedText.c_str());
}

void TextSelect::selectAll() {
  size_t lastLineIdx = getNumLines() - 1;
  std::string_view lastLine = getLineAtIdx(lastLineIdx);

  // Set the selection range from the beginning to the end of the last line
  selectStart = {0, 0};
  selectEnd = {utf8Length(lastLine), lastLineIdx};
}

void TextSelect::update() {
  // ImGui::GetCursorStartPos() is in window coordinates so it is added to the
  // window position
  ImVec2 cursorPosStart = ImGui::GetWindowPos() + ImGui::GetCursorStartPos();

  // Switch cursors if the window is hovered
  bool hovered = ImGui::IsWindowHovered();
  if (hovered)
    ImGui::SetMouseCursor(ImGuiMouseCursor_TextInput);

  // Handle mouse events
  if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    if (hovered)
      handleMouseDown(cursorPosStart);
    else
      handleScrolling();
  }

  drawSelection(cursorPosStart);

  ImGuiID windowID = ImGui::GetCurrentWindow()->ID;

  // Keyboard shortcuts
  if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_A))
    selectAll();
  else if (ImGui::Shortcut(ImGuiMod_Shortcut | ImGuiKey_C))
    copy();
}