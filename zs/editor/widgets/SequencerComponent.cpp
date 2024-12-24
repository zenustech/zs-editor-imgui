#include "SequencerComponent.hpp"

#include <cstdio>

#include "world/system/ResourceSystem.hpp"
#include "IconsMaterialDesign.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "world/scene/Timeline.hpp"

namespace zs {

  void SequencerWidget::disconnectScene() {
    if (auto ctx = reinterpret_cast<SceneContext*>(_sceneCtx))
      ctx->onTimelineSetupChanged().removeSlot(_id);
    onTimeCodeChanged().removeSlot(_id);
  }
  void SequencerWidget::connectScene(std::string_view sceneLabel) {
    disconnectScene();
    _sceneLabel = sceneLabel;
    auto ctx = zs_resources().get_scene_context_ptr(sceneLabel);
    if (ctx) {
      // update metrics upon the current scene
      _start = ctx->getStartTimeCode();
      _end = ctx->getEndTimeCode();
      _current = ctx->getCurrentTimeCode();
      _fps = ctx->getFps();
      _tcps = ctx->getTcps();

      // setup signal-slot(s)
      ctx->onTimelineSetupChanged().assign(_id, [this](const std::vector<TimelineEvent>& events) {
        /// no further signal propagations
        for (const auto& event : events) {
          switch (event.target) {
            case zs::sequencer_component_e::start:
              _start = event.newVal;
              _iStart = (TimeCodeIndex)std::ceil(_start);
              break;
            case zs::sequencer_component_e::end:
              _end = event.newVal;
              _iEnd = (TimeCodeIndex)std::floor(_end);
              break;
            case zs::sequencer_component_e::fps:
              _fps = event.newVal;
              break;
            case zs::sequencer_component_e::tcps:
              _tcps = event.newVal;
              break;
            case zs::sequencer_component_e::cur_tc:
              _current = event.newVal;
              break;
            default:
              break;
          }
        }
      });
      onTimeCodeChanged().assign(_id, [this, ctx](TimeCode newTimeCode) {
        ctx->setCurrentTimeCode(newTimeCode);
        if (std::isnan(newTimeCode))
          _timecode = 0;
        else
          _timecode = static_cast<long long int>(std::floor(newTimeCode));
      });
    }
    _sceneCtx = ctx;
  }
  SequencerWidget::SequencerWidget() : _id{zs_resources().next_widget_id()}, _sceneCtx{nullptr} {
    connectScene(g_defaultSceneLabel);
  }
  SequencerWidget::~SequencerWidget() {
    disconnectScene();
    zs_resources().recycle_widget_id(_id);
  }

  void SequencerWidget::paint() {
    std::string str;
    str.reserve(128);
    auto calcTextWidth = [&str](auto val) -> float {
      std::snprintf(str.data(), 256, "%f", val);
      auto width = ImGui::CalcTextSize(str.c_str()).x;
      return width + ImGui::GetStyle().ItemSpacing.x + ImGui::GetStyle().ItemInnerSpacing.x;
    };

    ImGui::Spacing();
    auto stTagWidth = calcTextWidth(_start);
    auto edTagWidth = calcTextWidth(_end);
    auto scrollBarWidth = ImGui::GetContentRegionAvail().x - stTagWidth - edTagWidth
                          - ImGui::GetStyle().ItemSpacing.x * 2
                          - ImGui::GetStyle().ItemInnerSpacing.x * 2;

    ImGui::Spacing();

    // play
    if (_playStatus) {
      if (ImGui::Button((const char*)ICON_MD_STOP)) {
        _playStatus = false;
      } else {
        auto curTime = getCurrentTime();
        if (!zs_resources().vis_prims_ready())
          _lastTime = curTime;
        else {
          if (auto interval = curTime - _lastTime; interval > getCurrentTimeCodeInterval()) {
            auto ctx = zs_resources().get_scene_context_ptr(_sceneLabel);
            if (ctx) {
              auto curTc = ctx->getCurrentTimeCode();
              _timecode = curTc + 1;
              if (_timecode > _end + detail::deduce_numeric_epsilon<float>()) _timecode = _start;
              onTimeCodeChanged().emit(static_cast<TimeCode>(_timecode));
            }
            _lastTime = curTime;
          }
        }
      }
    } else {
      if (ImGui::Button((const char*)ICON_MD_PLAY_CIRCLE_OUTLINE)) {
        _playStatus = true;
        _lastTime = getCurrentTime();
      }
    }
    // discrete
    ImGui::Checkbox((const char*)u8"连续", &_continuous);
    ImGui::SameLine();
    // fps
    ImGui::SetNextItemWidth(calcTextWidth(_fps));
    ImGui::InputDouble("FPS", &_fps, 0.f, 0.f, "%f");
    // tcps
    ImGui::SameLine();
    ImGui::SetNextItemWidth(calcTextWidth(_tcps));
    ImGui::InputDouble("TCPS", &_tcps, 0.f, 0.f, "%f");

    // st
    ImGui::SetNextItemWidth(stTagWidth);
    if (ImGui::InputDouble("##st", &_start, 0.f, 0.f, "%f") && !_continuous)
      _iStart = (TimeCodeIndex)std::ceil(_start);
    // scrollbar
    ImGui::SameLine();
    ImGui::SetNextItemWidth(scrollBarWidth);
    bool isnan = std::isnan(_current) || std::isnan(_start);
    if (isnan) ImGui::BeginDisabled(isnan);

    if (_continuous) {
      // continuous
      if (isnan) {
        double tmp = 0.;
        ImGui::SliderScalar("##timeline", ImGuiDataType_Double, &tmp, &tmp, &tmp, "%f");
      } else if (ImGui::SliderScalar("##timeline", ImGuiDataType_Double, &_current, &_start, &_end,
                                     "%f")) {
        onTimeCodeChanged().emit(_current);
      }
    } else {
      // discrete
      if (ImGui::SliderScalar("##timeline", ImGuiDataType_S64, &_timecode, &_iStart, &_iEnd,
                              "%lld")) {
        onTimeCodeChanged().emit(static_cast<TimeCode>(_timecode));
      }
    }
    if (isnan) ImGui::EndDisabled();
    // ed
    ImGui::SameLine();
    ImGui::SetNextItemWidth(edTagWidth);
    if (ImGui::InputDouble("##ed", &_end, 0.f, 0.f, "%f") && !_continuous)
      _iEnd = (TimeCodeIndex)std::floor(_end);
  }

  double SequencerWidget::getCurrentTime() {
    struct timespec t;
    std::timespec_get(&t, TIME_UTC);
    return t.tv_sec * 1e3 + t.tv_nsec * 1e-6;
  }

  double SequencerWidget::getCurrentTimeCodeInterval() const {
    if (!std::isnan(_tcps)) return 1000. / _tcps;
    return 1000. / 24;
  }

}  // namespace zs