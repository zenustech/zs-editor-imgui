#pragma once
#include <map>
#include <string>
#include <string_view>

#include "zensim/TypeAlias.hpp"
#include "zensim/ZpcResource.hpp"

namespace zs {

  struct GUIWindow;

  struct GraphEditor {
    GraphEditor(void *config = nullptr);
    ~GraphEditor();
    GraphEditor(const GraphEditor &) = delete;
    GraphEditor &operator=(const GraphEditor &) = delete;
    GraphEditor(GraphEditor &&o) noexcept { editor = zs::exchange(o.editor, nullptr); }
    GraphEditor &operator=(GraphEditor &&o) noexcept {
      editor = zs::exchange(o.editor, nullptr);
      return *this;
    }
    void *editor;
  };

  struct ImguiSystem {
  private:
    ImguiSystem();

  public:
    enum font_e : u32 { default_font = 0, cn_font };
    static ImguiSystem &instance() {
      static ImguiSystem s_instance{};
      return s_instance;
    }
    static float get_font_size() { return instance()._fontSize; }
    static void *get_font(font_e e) { return instance()._fonts[e]; }
    static void initialize() { (void)instance(); }
    static void *get_window() noexcept;
    static void *get_node_editor(std::string_view tag, void *config = nullptr);
    static bool has_node_editor(std::string_view tag);
    static void erase_node_editor(std::string_view tag);
    static void *create_node_editor(std::string_view tag);
    static void rename_node_editor(std::string_view oldTag, std::string_view newTag);
    static void reset_styles();

    ~ImguiSystem();

    /// @note this is temporary, should config by other means
    void setup(GUIWindow *window);
    void reset();

    void *getNodeEditor(std::string_view tag, void *config = nullptr);
    bool hasNodeEditor(std::string_view tag) const;
    void eraseNodeEditor(std::string_view tag);
    void renameNodeEditor(std::string_view oldTag, std::string_view newTag);
    void addIconFont();
    void *getImguiFont(font_e e) { return _fonts[e]; }

    std::map<u32, void *> _fonts;
    std::map<std::string, GraphEditor> _editors;
    std::string _configFile;
    float _fontSize;
    bool _initialized;
  };

}  // namespace zs