#pragma once

#include "world/core/Signal.hpp"
// #include "../world/async/Coro.hpp"
#include "world/scene/Timeline.hpp"
#include "WidgetComponent.hpp"
#include "imgui.h"
#include "zensim/ui/Widget.hpp"

namespace zs {

  struct SequencerWidget : WidgetComponentConcept {
    using TimeCodeIndex = long long int;
    struct Group {};

    SequencerWidget();
    ~SequencerWidget();

    void disconnectScene();
    void connectScene(std::string_view sceneLabel);
    void paint() override;
    auto &onTimeCodeChanged() { return _timecodeChanged; }

  protected:
    static double getCurrentTime();
    double getCurrentTimeCodeInterval() const;

    u32 _id;
    bool _continuous{false};  // or discrete
    std::string _sceneLabel;
    void *_sceneCtx;

    bool _playStatus{false};  // play or pause
    double _lastTime;
    Signal<void(TimeCode)> _timecodeChanged;
    TimeCodeIndex _timecode{0}, _iStart{0}, _iEnd{0};
    // closed range
    TimeCode _start{0}, _end{0}, _current{0};
    TimeCode _fps{1}, _tcps{1};
  };

}  // namespace zs