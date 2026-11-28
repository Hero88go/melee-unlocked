// Render-only settings presentation. All values and save behavior remain in pc_settings.cpp.
// Mockup provenance: ui_sources/mockups/ORIGIN.md. No game state is written here.
static bool settings_hit_button(const char* id, ImVec2 size,
                               ImGuiButtonFlags flags = ImGuiButtonFlags_EnableNav) {
  const bool pressed = ImGui::InvisibleButton(id, size, flags);
  settings_page::label_item(id);
  return pressed;
}

// ---- Search and collapsible sections ----
// One mechanism serves both. While "hidden", whatever a page draws goes nowhere: clipped to nothing,
// disabled (no clicks, no focus), and the cursor is put back afterwards, so it takes no space. A
// search hides every page's content and shows only the setting rows whose name matches (the real
// controls, working there); a closed section hides everything up to the next section header. The
// pages themselves stay as they are: the helpers below (and the settings_* row helpers) do this.
static char g_search_text[96] = "";
static bool g_search = false;                 // a query is in the box this frame
static int g_search_page = -1;                // the page drawing now (during a search, every page draws)
static int g_search_hits = 0;
static int g_hidden = 0;                      // hiding depth (0: drawing normally)
static float g_shown_y = 0.0f;                // where the next shown row goes while hiding (cursor y)
static bool g_section_open = true;            // the current section draws
static int g_section_index = 0;               // sections met on this page
static std::string g_goto_label;              // "go to" a search result: this row on g_goto_page
static int g_goto_page = -1;
static double g_goto_flash = -10.0;
static std::string g_goto_flash_label;
static const char* const kSearchPages[] = {"Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes", "Mods"};

// The content's lowest point before a hidden run began: the hidden rows still lay out (clipped), and ImGui keeps
// the lowest point they reached as the window's content height, so a closed section left its own height as empty
// scroll room at the bottom of the page (owner 11-27: "why is there a huge gap here" under L-cancel).
static float g_shown_max_y = 0.0f;
static void settings_hide_begin() {
  if (g_hidden++ == 0) {
    g_shown_y = ImGui::GetCursorPosY();
    g_shown_max_y = ImGui::GetCurrentWindow()->DC.CursorMaxPos.y;
    ImGui::PushClipRect(ImVec2(0, 0), ImVec2(0, 0), false);
    ImGui::BeginDisabled();
  }
}
static void settings_hide_end() {
  if (g_hidden == 0) return;
  if (--g_hidden == 0) {
    ImGui::EndDisabled();
    ImGui::PopClipRect();
    ImGui::SetCursorPosY(g_shown_y);
    ImGui::GetCurrentWindow()->DC.CursorMaxPos.y = g_shown_max_y;   // the hidden rows leave no height behind
  }
}
static std::string settings_lower(const char* s) {
  std::string out;
  for (; s && *s; ++s) {
    if (s[0] == '#' && s[1] == '#') break;   // an ImGui id suffix is not part of the name
    out += (char)std::tolower((unsigned char)*s);
  }
  return out;
}
// Every word of the query matches the name or the page's name, forgivingly (settings_search.h:
// parts of words, typos, abbreviations, other words for the same thing).
static bool settings_search_matches(const char* label) {
  if (!label || label[0] == '#') return false;
  const std::string name = std::string(label) + " " + (g_search_page >= 0 && g_search_page < 8 ? kSearchPages[g_search_page] : "");
  return settings_search::matches(g_search_text, name);
}

// A setting row is about to draw. Returns whether it shows: always outside a search and a closed
// section; in a search only when it matches, and then it is drawn in the shown list, with its page's
// name to the right to go there. Pair a true answer with settings_row_end().
struct SettingsRow {
  bool revealed = false, shown = true;
  std::string label;
};
static SettingsRow settings_row_begin(const char* label) {
  SettingsRow r;
  r.label = label ? label : "";
  if (g_search) {
    r.shown = settings_search_matches(label);
    if (r.shown && g_hidden > 0) {
      // Show it: stop hiding for this row, at the shown list's end.
      ImGui::EndDisabled();
      ImGui::PopClipRect();
      ImGui::SetCursorPosY(g_shown_y);
      r.revealed = true;
      ++g_search_hits;
      // Where it lives: the page's name, a button that goes there.
      ImGui::PushID(label);
      ImGui::PushID(g_search_page);
      const char* page = g_search_page >= 0 && g_search_page < 8 ? kSearchPages[g_search_page] : "";
      ImGui::TextDisabled("%s", page);
      ImGui::SameLine();
      if (ImGui::SmallButton("Go to")) {
        g_goto_label = r.label;
        g_goto_page = g_search_page;
        g_goto_flash = ImGui::GetTime();   // the request's time until the row is found
        g_search_text[0] = 0;
      }
      ImGui::PopID();
      ImGui::PopID();
      ImGui::PushID(g_search_page);   // the same name on two pages stays two controls (popped in settings_row_end)
    }
    return r;
  }
  // Normal drawing: a row asked for by "Go to" scrolls into view and flashes.
  if (!g_goto_label.empty() && g_goto_label == r.label && g_goto_page == g_search_page) {
    ImGui::SetScrollHereY(0.3f);
    g_goto_flash = ImGui::GetTime();
    g_goto_flash_label = r.label;
    g_goto_label.clear();
  }
  return r;
}
static void settings_row_end(const SettingsRow& r) {
  if (!r.revealed) {
    if (!g_search && !r.label.empty() && r.label == g_goto_flash_label && ImGui::GetTime() - g_goto_flash < 1.6) {
      const float a = (float)(1.0 - (ImGui::GetTime() - g_goto_flash) / 1.6);
      ImGui::GetWindowDrawList()->AddRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(),
                                          IM_COL32(255, 210, 90, (int)(255 * a)), 6.0f, 0, 2.5f);
    }
    return;
  }
  ImGui::PopID();
  ImGui::Spacing();
  g_shown_y = ImGui::GetCursorPosY();
  ImGui::PushClipRect(ImVec2(0, 0), ImVec2(0, 0), false);
  ImGui::BeginDisabled();
}

// The pages' own ImGui controls, searchable the same way (a search shows the matching ones).
static std::vector<SettingsRow> g_open_combos;
static bool settings_begin_combo(const char* label, const char* preview, ImGuiComboFlags flags = 0) {
  SettingsRow row = settings_row_begin(label);
  if (!row.shown) return false;
  if (!settings_widgets::BeginCombo(label, preview, flags)) { settings_row_end(row); return false; }
  g_open_combos.push_back(std::move(row));
  return true;
}
static void settings_end_combo() {
  ImGui::EndCombo();
  if (g_open_combos.empty()) return;
  const SettingsRow row = std::move(g_open_combos.back());
  g_open_combos.pop_back();
  settings_row_end(row);
}
template <class Draw> static bool settings_searchable(const char* label, Draw draw) {
  const SettingsRow row = settings_row_begin(label);
  if (!row.shown) return false;
  const bool changed = draw();
  settings_row_end(row);
  return changed;
}
static bool settings_checkbox(const char* label, bool* v) { return settings_searchable(label, [&] { return settings_widgets::Checkbox(label, v); }); }
static bool settings_slider_int(const char* label, int* v, int lo, int hi, const char* format = "%d", ImGuiSliderFlags flags = 0) {
  return settings_searchable(label, [&] { return settings_widgets::SliderInt(label, v, lo, hi, format, flags); });
}
static bool settings_color_edit3(const char* label, float col[3], ImGuiColorEditFlags flags = 0) {
  return settings_searchable(label, [&] { return settings_widgets::ColorEdit3(label, col, flags); });
}

// A page starts (pc_settings.cpp: `if (settings_page_shown(state.active_tab, N))`). During a search every
// page draws, hidden; otherwise only the active one.
static bool settings_page_shown(int active_tab, int page) {
  if (!g_search && active_tab != page) return false;
  // The page before ends: its last section closes.
  if (!g_search) settings_hide_end();
  g_search_page = page;
  g_section_index = 0;
  g_section_open = true;
  return true;
}

// A section header: a line that folds the settings under it, up to the next header. Every section
// starts open (owner, 0.9.210: settings should not hide); each one's state is remembered. During a search
// sections draw no header and every setting is searched.
static void settings_section(const char* title) {
  const int index = g_section_index++;
  if (g_search) return;
  settings_hide_end();   // the section before ends
  ImGuiStorage* storage = ImGui::GetStateStorage();
  ImGui::PushID("section");
  ImGui::PushID(g_search_page);
  ImGui::PushID(title);
  const ImGuiID key = ImGui::GetID("open");
  bool open = storage->GetBool(key, true);
  (void)index;
  // "Go to" opens the section the row is in: until it is drawn, every section of that page opens.
  if (!g_goto_label.empty() && g_goto_page == g_search_page) open = true;
  ImGui::Spacing();
  const ImVec2 at = ImGui::GetCursorScreenPos();
  const float width = ImGui::GetContentRegionAvail().x, height = ImGui::GetFrameHeight() + 6.0f;
  if (ImGui::InvisibleButton("##header", ImVec2(width, height))) { open = !open; }
  storage->SetBool(key, open);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
  draw->AddRectFilled(at, ImVec2(at.x + width, at.y + height), hot ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 10), 5.0f);
  const float mid = at.y + height * 0.5f, s = ImGui::GetFontSize() * 0.32f;
  const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
  if (open)
    draw->AddTriangleFilled(ImVec2(at.x + 8, mid - s * 0.6f), ImVec2(at.x + 8 + 2 * s, mid - s * 0.6f), ImVec2(at.x + 8 + s, mid + s * 0.8f), ink);
  else
    draw->AddTriangleFilled(ImVec2(at.x + 9, mid - s), ImVec2(at.x + 9, mid + s), ImVec2(at.x + 9 + 1.6f * s, mid), ink);
  draw->AddText(ImVec2(at.x + 16 + 2 * s, mid - ImGui::GetFontSize() * 0.5f), ink, title);
  ImGui::PopID();
  ImGui::PopID();
  ImGui::PopID();
  g_section_open = open;
  if (!open) settings_hide_begin();
}

// After the last page: close any hiding, and size the scroll to what was shown.
static void settings_pages_end() {
  while (g_hidden > 0) settings_hide_end();
  if (g_search) {
    ImGui::SetCursorPosY(g_shown_y);
    if (g_search_hits == 0) ImGui::TextDisabled("No setting matches \"%s\".", g_search_text);
    ImGuiWindow* w = ImGui::GetCurrentWindow();
    w->DC.CursorMaxPos.y = ImGui::GetCursorScreenPos().y;
    ImGui::Dummy(ImVec2(0, 0));
  }
}

// The search box at the top of every page. Typing starts a search over every page.
static void settings_search_box() {
  // MELEE_TEST_SETTINGS_SEARCH=<words> (tests): the box starts with that query.
  static bool test_query_set = false;
  if (!test_query_set) {
    test_query_set = true;
    if (const char* q = host::test_env::getenv("MELEE_TEST_SETTINGS_SEARCH")) std::snprintf(g_search_text, sizeof g_search_text, "%s", q);
  }
  // Ctrl+F or "/" from anywhere in the settings puts the cursor in the box.
  const ImGuiIO& io = ImGui::GetIO();
  if (!ImGui::IsAnyItemActive() &&
      ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_F, false)) || ImGui::IsKeyPressed(ImGuiKey_Slash, false)))
    ImGui::SetKeyboardFocusHere();
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##settings_search", "Search all settings (Ctrl+F)", g_search_text, sizeof g_search_text);
  g_search = g_search_text[0] != 0;
  g_search_hits = 0;
  // A "Go to" whose row never drew (it sits in a closed list of its own) gives up after a second.
  if (!g_goto_label.empty() && ImGui::GetTime() - g_goto_flash > 1.0) g_goto_label.clear();
  g_hidden = 0;
  g_search_page = -1;
  ImGui::Spacing();
  if (g_search) settings_hide_begin();
}

// Close (X) buttons are mouse-only. Keyboard/controller focus landing on one meant pressing A
// closed the whole panel, and the same A then reached the game as a fresh press.
static bool settings_close_hit(const char* id, ImVec2 size) {
  ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
  const bool pressed = ImGui::InvisibleButton(id, size, 0);
  ImGui::PopItemFlag();
  return pressed;
}

// Shaped buttons suppress the rectangular nav cursor; callers outline the painted face.
static bool settings_shaped_hit_button(const char* id, ImVec2 size) {
  ImGui::PushStyleColor(ImGuiCol_NavCursor, IM_COL32(0, 0, 0, 0));
  const bool pressed = settings_hit_button(id, size);
  ImGui::PopStyleColor();
  return pressed;
}

// Whether ImGui would have drawn its focus frame on the last item (same test as RenderNavCursor).
static bool settings_nav_frame_on_item() {
  const ImGuiContext& g = *GImGui;
  return g.NavCursorVisible && g.NavId != 0 && g.LastItemData.ID == g.NavId &&
         !ImGui::GetCurrentWindow()->DC.NavHideHighlightOneFrame;
}

// Two-pixel focus outline on the exact face, with a light tint of this panel's accent.
static void settings_nav_frame_outline(ImDrawList* draw, const ImVec2* outline, int count) {
  const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(ImGui::GetColorU32(ImGuiCol_NavCursor));
  const ImU32 light = ImGui::ColorConvertFloat4ToU32(
      ImVec4(.75f + accent.x * .25f, .75f + accent.y * .25f, .75f + accent.z * .25f, 1.0f));
  draw->AddPolyline(outline, count, light, ImDrawFlags_Closed, 2.0f);
}

static void settings_nav_frame(ImDrawList* draw, const ImVec2 (&quad)[4]) {
  settings_nav_frame_outline(draw, quad, 4);
}

// A face lifted up and left off its plate: frame the whole button as players
// see it, face and the plate edge below it (report d6ad1693).
static void settings_nav_frame_lifted(ImDrawList* draw, const ImVec2 (&face)[4],
                                      const ImVec2 (&plate)[4]) {
  const ImVec2 outline[] = {face[0], face[1], plate[1], plate[2], plate[3], face[3]};
  settings_nav_frame_outline(draw, outline, 6);
}

// Hovering a category must not change the active page. Only an explicit
// navigation move may transfer selection to ImGui's focused category item.
static bool settings_nav_input_pressed() {
  return ImGui::IsKeyPressed(ImGuiKey_UpArrow, false) ||
         ImGui::IsKeyPressed(ImGuiKey_DownArrow, false) ||
         ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false) ||
         ImGui::IsKeyPressed(ImGuiKey_RightArrow, false) ||
         ImGui::IsKeyPressed(ImGuiKey_GamepadDpadUp, false) ||
         ImGui::IsKeyPressed(ImGuiKey_GamepadDpadDown, false) ||
         ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft, false) ||
         ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight, false) ||
         ImGui::IsKeyPressed(ImGuiKey_Tab, false);
}

static ImFont* g_settings_heading_font = nullptr;
static ImFont* g_settings_body_font = nullptr;
static ImFont* g_settings_classic_font = nullptr;
static ImFont* g_settings_old_font = nullptr;
static ImFont* g_settings_old_vector_font = nullptr;
// The 0.6.61 font. It is a pixel font: exact at its own size, uneven when a scaled frame draws it
// at 1.33 or 2.5 times that, so a scaled frame takes the outline version of the same design.
static ImFont* settings_old_font() {
  return g_frame_ui_scale.load(std::memory_order_relaxed) > 1.0f && g_settings_old_vector_font ?
      g_settings_old_vector_font : g_settings_old_font;
}
static int g_settings_form_style = 0;
static int g_settings_palette = 0;
static bool g_settings_custom_color_enabled = false;
static ImVec4 g_settings_custom_color = ImVec4(0.96f,0.15f,0.52f,1.0f);
static ImVec2 g_settings_gd_origin;
static float g_settings_gd_scale = 1.0f;
static std::string g_settings_gd_help;
static float g_settings_gd_back_x = 0;
// The GD list (the settings page's scrolling child) and where its rows are this frame, in screen
// pixels: used to rest the scroll on a row boundary and to keep the focused row whole.
static ImGuiWindow* g_settings_gd_list = nullptr;
static float g_settings_gd_snap = 0.0f;        // nearest row top minus the list's top
static bool g_settings_gd_snap_found = false;
static float g_settings_gd_focus_top = 0.0f;   // top of the row holding the controller focus frame
static bool g_settings_gd_focus_seen = false;

static ImU32 settings_accent_for(int appearance, int variant) {
  static constexpr ImU32 palette[7][4] = {
    {IM_COL32(247,38,133,255),IM_COL32(27,204,218,255),IM_COL32(255,157,47,255),IM_COL32(160,89,241,255)},
    {IM_COL32(255,203,68,255),IM_COL32(253,83,110,255),IM_COL32(164,190,252,255),IM_COL32(110,93,215,255)},
    {IM_COL32(240,180,41,255),IM_COL32(55,143,242,255),IM_COL32(48,185,131,255),IM_COL32(232,74,83,255)},
    {IM_COL32(34,151,255,255),IM_COL32(171,101,251,255),IM_COL32(46,197,142,255),IM_COL32(250,179,63,255)},
    {IM_COL32(34,151,255,255),IM_COL32(239,79,166,255),IM_COL32(50,207,178,255),IM_COL32(250,179,63,255)},
    {IM_COL32(100,154,220,255),IM_COL32(173,183,195,255),IM_COL32(80,168,133,255),IM_COL32(220,157,73,255)},
    {IM_COL32(49,92,168,255),IM_COL32(92,111,137,255),IM_COL32(49,122,86,255),IM_COL32(154,97,49,255)}};
  return palette[std::clamp(appearance,0,6)][std::clamp(variant,0,3)];
}
static ImU32 settings_accent(int appearance) {
  if (g_settings_custom_color_enabled)
    return ImGui::ColorConvertFloat4ToU32(g_settings_custom_color);
  return settings_accent_for(appearance,g_settings_palette);
}

// Rotate only saturated chrome colors. Whites, lettering, and dark glass keep their contrast.
static ImU32 settings_palette_tint(int appearance, ImU32 source) {
  if (g_settings_palette == 0 && !g_settings_custom_color_enabled) return source;
  const ImVec4 src=ImGui::ColorConvertU32ToFloat4(source);
  float h,s,v;
  ImGui::ColorConvertRGBtoHSV(src.x,src.y,src.z,h,s,v);
  if (s < .22f || v < .11f) return source;
  const ImVec4 classic=ImGui::ColorConvertU32ToFloat4(
      appearance==0?IM_COL32(247,38,133,255):
      appearance==1?IM_COL32(255,203,68,255):
      appearance==2?IM_COL32(240,180,41,255):IM_COL32(34,151,255,255));
  float base_h,base_s,base_v,target_h,target_s,target_v;
  ImGui::ColorConvertRGBtoHSV(classic.x,classic.y,classic.z,base_h,base_s,base_v);
  const ImVec4 target=ImGui::ColorConvertU32ToFloat4(settings_accent(appearance));
  ImGui::ColorConvertRGBtoHSV(target.x,target.y,target.z,target_h,target_s,target_v);
  h=std::fmod(h+target_h-base_h+1.0f,1.0f);
  float r,g,b;
  ImGui::ColorConvertHSVtoRGB(h,s,v,r,g,b);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(r,g,b,src.w));
}

// Each appearance supplies its own interaction colors, including popups and focus.
struct SettingsAppearanceScope {
  bool old = false;
  ImGuiStyle saved;
  explicit SettingsAppearanceScope(int appearance) {
    old = appearance == 7;
    if (old) {
      saved = ImGui::GetStyle();
      ImGui::GetStyle() = ImGuiStyle();
      ImGui::StyleColorsDark();
      ImGui::GetStyle().ScaleAllSizes(1.25f);
      ImGui::PushFont(settings_old_font());
      return;
    }
    const ImVec4 accent = ImGui::ColorConvertU32ToFloat4(settings_accent(appearance));
    const ImVec4 muted = appearance == 0 ? ImVec4(0.17f,0.06f,0.14f,1) :
        appearance == 2 ? ImVec4(0.18f,0.21f,0.25f,1) : ImVec4(0.10f,0.15f,0.22f,1);
    ImGui::PushStyleColor(ImGuiCol_Button,muted);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,accent);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,accent);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,muted);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,ImVec4(accent.x*.4f,accent.y*.4f,accent.z*.4f,1));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,muted);
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,accent);
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive,ImVec4(0.95f,0.96f,1,1));
    ImGui::PushStyleColor(ImGuiCol_CheckMark,accent);
    ImGui::PushStyleColor(ImGuiCol_Header,ImVec4(accent.x*.55f,accent.y*.55f,accent.z*.55f,1));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,accent);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,accent);
    ImGui::PushStyleColor(ImGuiCol_PopupBg,ImVec4(0.04f,0.055f,0.085f,0.99f));
    ImGui::PushStyleColor(ImGuiCol_NavCursor,accent);
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,appearance==1?10.0f:appearance==2?0.0f:4.0f);
  }
  ~SettingsAppearanceScope() {
    if (old) { ImGui::PopFont(); ImGui::GetStyle() = saved; }
    else { ImGui::PopStyleVar(); ImGui::PopStyleColor(14); }
  }
};

static void settings_hint(const char* format, ...) {
  va_list args;
  va_start(args, format);
  if (g_settings_form_style == 2) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) || ImGui::IsItemFocused()) {
      char text[2048];
      std::vsnprintf(text, sizeof text, format, args);
      g_settings_gd_help = text;
      std::replace(g_settings_gd_help.begin(), g_settings_gd_help.end(), '\n', ' ');
    }
  } else ImGui::TextDisabledV(format,args);
  va_end(args);
}

// Current GD kit list/widget geometry, in the source's unsheared 640x480 space.
struct SettingsGdRow {
  float y = 0, lift = 0;
  bool selected = false, activated = false;
  ImU32 ink = 0;
  ImVec2 point(float x, float py) const {
    return ImVec2(g_settings_gd_origin.x + (x + (240.0f - py) * 0.25f) * g_settings_gd_scale,
                   g_settings_gd_origin.y + py * g_settings_gd_scale);
  }
  void quad(float x0, float y0, float x1, float y1, ImU32 color) const {
    const ImVec2 q[] = {point(x0,y0), point(x1,y0), point(x1,y1), point(x0,y1)};
    ImGui::GetWindowDrawList()->AddConvexPolyFilled(q,4,color);
  }
  void text(float x, float baseline, const char* label, ImU32 color, float max_width = 1000) const {
    gd_kit_text(ImGui::GetWindowDrawList(), "row", g_settings_gd_origin,
                g_settings_gd_scale, x, baseline, label, color, max_width);
  }
};

static SettingsGdRow settings_gd_row(const char* label) {
  if (ImGui::GetCurrentWindow()->DC.IsSameLine)
    ImGui::NewLine();
  SettingsGdRow row;
  row.y = (ImGui::GetCursorScreenPos().y - g_settings_gd_origin.y) / g_settings_gd_scale;
  ImGui::SetCursorScreenPos(row.point(88.5f, row.y));
  row.activated = settings_shaped_hit_button("##kit_row", ImVec2(451.5f * g_settings_gd_scale, 30.0f * g_settings_gd_scale));
  settings_page::label_item(label);
  const bool nav_frame = settings_nav_frame_on_item();
  if (ImGuiWindow* list = ImGui::GetCurrentWindow(); list == g_settings_gd_list) {
    const float top = ImGui::GetItemRectMin().y, bottom = ImGui::GetItemRectMax().y;
    const float offset = top - list->InnerRect.Min.y;
    if (!g_settings_gd_snap_found || std::fabs(offset) < std::fabs(g_settings_gd_snap))
      g_settings_gd_snap = offset;
    g_settings_gd_snap_found = true;
    if (nav_frame) { g_settings_gd_focus_seen = true; g_settings_gd_focus_top = top; }
    // The row that has just taken the focus is brought fully inside the list, clear of the hint
    // bar below it. Once per change of focus, so the wheel can still scroll past it afterwards.
    static ImGuiID focus_scrolled = 0;
    if (ImGui::IsItemFocused() && focus_scrolled != ImGui::GetItemID()) {
      focus_scrolled = ImGui::GetItemID();
      if (top < list->InnerRect.Min.y) ImGui::SetScrollFromPosY(top - list->Pos.y, 0.0f);
      else if (bottom > list->InnerRect.Max.y) ImGui::SetScrollFromPosY(bottom - list->Pos.y, 1.0f);
    }
  }
  row.selected = ImGui::IsItemHovered() || ImGui::IsItemFocused() || ImGui::IsItemActive();
  const bool disabled = (ImGui::GetItemFlags() & ImGuiItemFlags_Disabled) != 0;
  const ImGuiID id = ImGui::GetID("lift");
  row.lift = ImGui::GetStateStorage()->GetFloat(id, 0.0f);
  row.lift += ((row.selected ? -5.0f : 0.0f) - row.lift) * std::min(1.0f, ImGui::GetIO().DeltaTime * 20.0f);
  ImGui::GetStateStorage()->SetFloat(id, row.lift);
  row.ink = disabled ? IM_COL32(125,136,166,255) : row.selected ? IM_COL32(10,14,24,255) : IM_COL32(242,239,228,255);
  if (row.selected) row.quad(96,row.y,540,row.y+30,
                             settings_palette_tint(2,IM_COL32(169,118,26,255)));
  row.quad(96+row.lift,row.y+row.lift,540+row.lift,row.y+30+row.lift,
           row.selected ? settings_accent(2) : IM_COL32(46,54,64,255));
  if (nav_frame) {
    // Follow the animated lifted face, plus the gold plate while it shows.
    const ImVec2 frame[] = {row.point(96+row.lift,row.y+row.lift), row.point(540+row.lift,row.y+row.lift),
                            row.point(540+row.lift,row.y+30+row.lift), row.point(96+row.lift,row.y+30+row.lift)};
    const ImVec2 plate[] = {row.point(96,row.y), row.point(540,row.y),
                            row.point(540,row.y+30), row.point(96,row.y+30)};
    if (row.selected) settings_nav_frame_lifted(ImGui::GetWindowDrawList(), frame, plate);
    else settings_nav_frame(ImGui::GetWindowDrawList(), frame);
  }
  const char* display_label=std::strcmp(label,"Exclusive fullscreen (experimental)")==0?
      "Exclusive fullscreen":label;
  row.text(112+row.lift,row.y+20.28f+row.lift,display_label,row.ink,216);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s",label);
  return row;
}

static bool settings_gd_toggle(const char* label, bool* value) {
  ImGui::PushID(label);
  const SettingsGdRow row = settings_gd_row(label);
  bool changed = row.activated;
  if (row.selected && (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_RightArrow) ||
                       ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)))
    changed = true;
  if (changed) *value = !*value;
  const float x=340+row.lift, y=row.y+15+row.lift;
  row.quad(x,y-10,x+120,y+10,IM_COL32(10,14,24,255));
  const ImGuiID position_id = ImGui::GetID("position");
  float pos = ImGui::GetStateStorage()->GetFloat(position_id,*value?60.0f:0.0f);
  pos += ((*value?60.0f:0.0f)-pos)*std::min(1.0f,ImGui::GetIO().DeltaTime*20.0f);
  ImGui::GetStateStorage()->SetFloat(position_id,pos);
  row.quad(x+2+pos,y-8,x+58+pos,y+8,row.selected?IM_COL32(255,221,119,255):IM_COL32(242,239,228,255));
  row.text(x+30-gd_kit_text_width("row","OFF")*.5f,y+5.28f,"OFF",!*value?IM_COL32(10,14,24,255):IM_COL32(149,157,167,255));
  row.text(x+90-gd_kit_text_width("row","ON")*.5f,y+5.28f,"ON",*value?IM_COL32(10,14,24,255):IM_COL32(149,157,167,255));
  ImGui::PopID();
  return changed;
}

static bool settings_gd_combo(const char* label, int* value, const char* const items[], int count) {
  ImGui::PushID(label);
  const SettingsGdRow row = settings_gd_row(label);
  int direction = 0;
  if (row.selected) {
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) direction=-1;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) direction=1;
  }
  // The text shown for the current value: a few rows use a shorter form of their long names.
  const auto shown_value=[&]() -> const char* {
    const char* text= *value>=0 && *value<count ? items[*value] : "Custom";
    if(std::strcmp(label,"Aspect ratio")==0) {
      static constexpr const char* short_aspects[]={
          "Auto (73:60 / 16:9)","73:60 native","4:3","16:9","Stretch"};
      if(*value>=0 && *value<5) text=short_aspects[*value];
    } else if(std::strcmp(label,"Display")==0) {
      // The full names are in the list and on hover.
      static constexpr const char* short_displays[]={"Window","Borderless","Exclusive"};
      if(*value>=0 && *value<3) text=short_displays[*value];
    } else if(std::strcmp(label,"Widescreen")==0) {
      static constexpr const char* short_widescreen[]={"Off","Fill screen","16:9","16:10","Custom"};
      if(*value>=0 && *value<5) text=short_widescreen[*value];
    }
    return text;
  };
  // The value sits between the two arrows: 158 units wide, with "<" at 340 and ">" at 522. A longer
  // value keeps its right edge and moves "<" toward the label, as far as the label leaves room,
  // so it is cut only when the whole row is too short for it.
  const float label_end=112+std::min(216.0f,gd_kit_text_width("row",label))+14;
  const auto left_arrow=[&](const char* text) {
    const float need=gd_kit_text_width("row",text);
    return need<=158.0f ? 340.0f : std::max(std::min(340.0f,label_end),498.0f-need);
  };
  if (row.activated) {
    const float mouse_design = (ImGui::GetIO().MousePos.x-g_settings_gd_origin.x)/g_settings_gd_scale-(240-row.y-15)*.25f;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouse_design<left_arrow(shown_value())+30) direction=-1;
    else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouse_design>500) direction=1;
    else ImGui::OpenPopup("##kit_choices");
  }
  bool changed = direction != 0 && count>0;
  if (changed) *value=(*value+direction+count)%count;
  const char* shown=shown_value();
  const float arrow=left_arrow(shown);
  const float x=arrow+row.lift, baseline=row.y+20.28f+row.lift;
  row.text(x,baseline,"<",row.ink);
  row.text(522+row.lift,baseline,">",row.ink);
  const float value_room=498.0f-arrow;
  const float value_need=gd_kit_text_width("row",shown);
  const float value_width=std::min(value_room,value_need);
  row.text(x+16+(value_room-value_width)*.5f,baseline,shown,row.ink,value_room);
  if (value_need>value_room) {
    // Still cut: the whole value on hover, and in the hint bar while the row is selected.
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s",label,shown);
    if (row.selected) g_settings_gd_help=std::string(label)+": "+shown;
  }
  if (ImGui::BeginPopup("##kit_choices")) {
    for (int i=0;i<count;++i)
      if (ImGui::Selectable(items[i],*value==i)) { *value=i;changed=true; }
    ImGui::EndPopup();
  }
  ImGui::PopID();
  return changed;
}

static bool settings_gd_slider(const char* label, int* value, int low, int high, const char* format) {
  ImGui::PushID(label);
  const SettingsGdRow row = settings_gd_row(label);
  const int previous=*value;
  const float x=340+row.lift, y=row.y+15+row.lift;
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
      ImGui::GetIO().MouseClickedPos[0].x >= row.point(x-8,y).x) {
    const float local=(ImGui::GetIO().MousePos.x-row.point(x,y).x)/(130*g_settings_gd_scale);
    *value=std::clamp((int)std::round(low+std::clamp(local,0.0f,1.0f)*(high-low)),low,high);
  }
  if (row.selected) {
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) *value=std::max(low,*value-1);
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) *value=std::min(high,*value+1);
  }
  const float fraction=high>low?std::clamp(float(*value-low)/(high-low),0.0f,1.0f):0;
  const ImU32 fill=row.selected?settings_palette_tint(2,IM_COL32(169,118,26,255)):IM_COL32(149,157,167,255);
  row.quad(x,y-3,x+130,y+3,IM_COL32(10,14,24,255));
  row.quad(x,y-3,x+130*fraction,y+3,fill);
  row.quad(x+130*fraction-4,y-9,x+130*fraction+4,y+9,row.ink);
  char value_text[64];std::snprintf(value_text,sizeof value_text,format,*value);
  row.text(x+148,y+5.28f,value_text,row.ink,44);
  ImGui::PopID();
  return previous!=*value;
}

static ImFont* settings_heading_font() {
  return g_settings_heading_font ? g_settings_heading_font : ImGui::GetFont();
}

static void settings_back_page(SettingsState& state, int appearance) {
  if (appearance == 0 && state.clean_detail_open) state.clean_detail_open = false;
  else if (appearance == 1 && state.dashboard_detail_open) state.dashboard_detail_open = false;
  else if (appearance == 2 && state.gd_detail_open) state.gd_detail_open = false;
  else if (appearance == 3 && state.radial_detail_open) state.radial_detail_open = false;
  else if (appearance == 4 && state.wide_detail_open) state.wide_detail_open = false;
  else state.open = false;
}

static void settings_slanted_text(ImDrawList* draw, ImFont* font, float size,
                                  ImVec2 pos, ImU32 color, const char* text, float slope);

static ImU32 settings_mix_color(ImU32 a, ImU32 b, float amount);
// A visible way out of the game from any settings page (players on fullscreen asked for one: Esc
// opens this panel, so the old Esc-to-quit route was hidden). Opens the usual Quit confirmation.
static void settings_quit_button(SettingsState& state, ImVec2 at, ImVec2 size) {
  ImGui::SetCursorScreenPos(at);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
  ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.42f, 0.10f, 0.12f, 0.92f));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.66f, 0.16f, 0.18f, 1.0f));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.82f, 0.22f, 0.22f, 1.0f));
  if (ImGui::Button("Quit game##footer_quit", size)) state.confirm = SettingsState::Confirm::Quit;
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Close Melee Unlocked (asks first)");
  ImGui::PopStyleColor(3);
  ImGui::PopStyleVar();
}
// The same Quit game button on each style's first page (the category list), where its pages' footer has
// it: Esc then one click quits, no category page first (player request 11-20).
static void settings_home_quit(SettingsState& state, int appearance) {
  const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize(), saved = ImGui::GetCursorPos();
  const float quit_w = ImGui::CalcTextSize("Quit game").x + 20.0f;
  if (appearance == 0) {
    // Clean's category rows reach down to its chips: the button goes in the free top right, beside the X.
    settings_quit_button(state, ImVec2(p.x + s.x - 37.0f - 10.0f - quit_w, p.y + 16.0f), ImVec2(quit_w, 28.0f));
  } else if (appearance == 2) {
    const float top = p.y + s.y * (430.0f / 480.0f), menu_x = p.x + s.x - 34.0f;
    settings_quit_button(state, ImVec2(menu_x - quit_w - 8.0f, top), ImVec2(quit_w, 28.0f * g_settings_gd_scale));
  } else {
    settings_quit_button(state, ImVec2(p.x + s.x - 40.0f - quit_w - 8.0f, p.y + s.y - 34.0f), ImVec2(quit_w, 30.0f));
  }
  ImGui::SetCursorPos(saved);
}
static void settings_page_footer(SettingsState& state, int appearance) {
  const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float quit_w = ImGui::CalcTextSize("Quit game").x + 20.0f;
  if (appearance == 6 || appearance == 7) {
    const float top=p.y+s.y-35.0f;
    draw->AddLine(ImVec2(p.x+8,top),ImVec2(p.x+s.x-8,top),IM_COL32(70,78,88,255));
    ImGui::SetCursorScreenPos(ImVec2(p.x+8,top+6));
    if (ImGui::Button("Save settings",ImVec2(ImGui::CalcTextSize("Save settings").x + 20.0f,27))) state.dirty=true;
    ImGui::SameLine(0.0f,6.0f);
    if (ImGui::Button("Close",ImVec2(70,27))) state.open=false;
    settings_quit_button(state, ImVec2(p.x+s.x-8-quit_w, top+6), ImVec2(quit_w, 27));
    return;
  }
  if (appearance == 5) {
    // Simple: the same rounded buttons as its category list (selected purple, resting navy),
    // instead of Clean side's slanted chips.
    const float top = p.y + s.y - 50.0f;
    ImGui::SetCursorScreenPos(ImVec2(p.x + 8.0f, top));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.40f, 0.27f, 0.62f, 1));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.68f, 0.47f, 0.90f, 1));
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.53f, 0.27f, 0.72f, 1));
    if (ImGui::Button("B  Back##simple_back", ImVec2(143.0f, 38.0f))) settings_back_page(state, appearance);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, 8.0f);
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.12f, 0.13f, 0.27f, 0.96f));
    if (ImGui::Button(yougame::stdio_bridge() ? "Return##simple_return" : "F1  Return##simple_return", ImVec2(143.0f, 38.0f))) state.open = false;
    ImGui::PopStyleColor(3);
    ImGui::PopStyleVar();
    const float menu_x = p.x + s.x - 40;
    settings_quit_button(state, ImVec2(menu_x - quit_w - 8.0f, top + 3.0f), ImVec2(quit_w, 32.0f));
    ImGui::SetCursorScreenPos(ImVec2(menu_x, top));
    if (settings_hit_button("##page_more", ImVec2(32, 32))) ImGui::OpenPopup("##settings_actions");
    draw->AddText(ImVec2(menu_x + 6, top + 8), IM_COL32(240, 244, 252, 255), "...");
    if (ImGui::BeginPopup("##settings_actions")) {
      ImGui::TextDisabled(state.dirty ? "Saving changes..." : state.saved ? "Settings saved" : "Changes save automatically");
      ImGui::Separator();
      if (ImGui::MenuItem("Return to game")) state.open = false;
      if (ImGui::MenuItem("Restart game")) state.confirm = SettingsState::Confirm::Restart;
      if (ImGui::MenuItem("Quit game")) state.confirm = SettingsState::Confirm::Quit;
      ImGui::EndPopup();
    }
    return;
  }
  const bool gd = appearance == 2;
  const float top = gd ? p.y + s.y * (430.0f / 480.0f) : p.y+s.y-34;
  const float left = gd ? p.x+s.x*.13f : p.x+22;
  const float clean_chip_w=(s.x-78.0f)*.5f;
  const float chip_w=appearance==0?clean_chip_w:145.0f;
  if (!gd && appearance != 0)
    draw->AddRectFilled(ImVec2(p.x,top),ImVec2(p.x+s.x,p.y+s.y),IM_COL32(8,17,31,215));
  for (int chip=0;chip<2 && !gd;++chip) {
    const float x=appearance==0?p.x+11.0f+chip*(clean_chip_w+5.0f):left+chip*(chip_w+10.0f);
    const float w=appearance==0?clean_chip_w:chip_w;
    ImGui::SetCursorScreenPos(ImVec2(x,top-5));
    const bool clicked=settings_shaped_hit_button(chip==0?"##page_back":"##page_return",
                                                  ImVec2(w,appearance==0?38.0f:34.0f));
    const bool nav_frame=settings_nav_frame_on_item();
    const bool hovered=ImGui::IsItemHovered()||ImGui::IsItemFocused();
    state.footer_hover[chip]+=( (hovered?1.0f:0.0f)-state.footer_hover[chip])*
                               std::min(1.0f,ImGui::GetIO().DeltaTime*16.0f);
    const float pop=state.footer_hover[chip]*5.0f;
    const ImVec2 card[]={ImVec2(x+12,top+5-pop),ImVec2(x+w,top-7-pop),
                         ImVec2(x+w-10,top+23-pop),ImVec2(x,top+34-pop)};
    const ImVec2 shadow[]={ImVec2(card[0].x+2,card[0].y+4),ImVec2(card[1].x+2,card[1].y+4),
                           ImVec2(card[2].x+2,card[2].y+4),ImVec2(card[3].x+2,card[3].y+4)};
    draw->AddConvexPolyFilled(shadow,4,IM_COL32(0,0,0,105));
    draw->AddConvexPolyFilled(card,4,chip==0?settings_palette_tint(appearance,IM_COL32(154,17,95,248)):
                              hovered?IM_COL32(52,34,66,250):IM_COL32(25,20,37,245));
    draw->AddLine(card[0],card[1],hovered?IM_COL32(255,224,246,255):
                  settings_palette_tint(appearance,IM_COL32(255,135,204,220)),1.8f);
    if (nav_frame) settings_nav_frame(draw,card);
    const char* label=chip==0?"B  BACK":yougame::stdio_bridge()?"RETURN":"F1  RETURN";
    settings_slanted_text(draw,settings_heading_font(),13.0f,
                          ImVec2(x+(appearance==0?18.0f:14.0f),top+6-pop),
                          chip==0?IM_COL32(255,245,251,255):IM_COL32(255,224,242,255),label,.08f);
    if (clicked) {
      if (chip==0) settings_back_page(state,appearance);
      else state.open=false;
    }
  }
  if (gd) {
    // A real button, not just the hint glyph: a raised slanted card that lights up on hover.
    const float w = 118.0f * g_settings_gd_scale, h = 28.0f * g_settings_gd_scale;
    const ImVec2 at(g_settings_gd_back_x - 8.0f * g_settings_gd_scale, top - 2.0f * g_settings_gd_scale);
    ImGui::SetCursorScreenPos(at);
    const bool clicked = settings_shaped_hit_button("##page_back", ImVec2(w, h));
    const bool nav_frame = settings_nav_frame_on_item();
    const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    state.footer_hover[0] += ((hovered ? 1.0f : 0.0f) - state.footer_hover[0]) *
                             std::min(1.0f, ImGui::GetIO().DeltaTime * 16.0f);
    const float lift = state.footer_hover[0] * 3.0f * g_settings_gd_scale;
    const float slant = 7.0f * g_settings_gd_scale;
    const ImVec2 face[] = {ImVec2(at.x + slant, at.y - lift), ImVec2(at.x + w, at.y - lift),
                           ImVec2(at.x + w - slant, at.y + h - lift), ImVec2(at.x, at.y + h - lift)};
    const ImVec2 shade[] = {ImVec2(face[0].x + 3, face[0].y + 4), ImVec2(face[1].x + 3, face[1].y + 4),
                            ImVec2(face[2].x + 3, face[2].y + 4), ImVec2(face[3].x + 3, face[3].y + 4)};
    draw->AddConvexPolyFilled(shade, 4, IM_COL32(0, 0, 0, 120));
    draw->AddConvexPolyFilled(face, 4, settings_mix_color(IM_COL32(46, 54, 64, 255), IM_COL32(229, 72, 59, 255),
                                                          0.35f + 0.65f * state.footer_hover[0]));
    if (nav_frame) settings_nav_frame(draw, face);
    const ImU32 ink = IM_COL32(242, 239, 228, 255);
    const float font = 15.0f * g_settings_gd_scale;
    const ImVec2 label = settings_heading_font()->CalcTextSizeA(font, FLT_MAX, 0.0f, "B  BACK");
    draw->AddText(settings_heading_font(), font,
                  ImVec2(at.x + (w - label.x) * 0.5f, at.y + (h - label.y) * 0.5f - lift), ink, "B  BACK");
    if (clicked) settings_back_page(state, appearance);
  }
  const float menu_x = p.x+s.x-(gd?34:40);
  // Clean's two chips fill the width, so there the button sits above them on the right.
  if (appearance == 0) settings_quit_button(state, ImVec2(p.x+s.x-12.0f-quit_w, top-44.0f), ImVec2(quit_w, 26.0f));
  else settings_quit_button(state, ImVec2(menu_x-quit_w-8.0f, top), ImVec2(quit_w, gd ? 28.0f * g_settings_gd_scale : 30.0f));
  ImGui::SetCursorScreenPos(ImVec2(menu_x,top));
  if (settings_hit_button("##page_more",ImVec2(32,32))) ImGui::OpenPopup("##settings_actions");
  draw->AddText(ImVec2(menu_x+6,top+5),IM_COL32(240,244,252,255),"...");
  if (ImGui::BeginPopup("##settings_actions")) {
    ImGui::TextDisabled(state.dirty?"Saving changes...":state.saved?"Settings saved":"Changes save automatically");
    ImGui::Separator();
    if (ImGui::MenuItem("Return to game")) state.open=false;
    if (ImGui::MenuItem("Restart game")) state.confirm=SettingsState::Confirm::Restart;
    if (ImGui::MenuItem("Quit game")) state.confirm=SettingsState::Confirm::Quit;
    ImGui::EndPopup();
  }
}

// Label above value: narrow drawers and large desktop pages use the same readable rhythm.
// A hidden label remains hidden for controls that already have a heading in their parent.
static void settings_field_label(const char* label) {
  if (ImGui::GetCurrentWindow()->DC.IsSameLine)
    ImGui::NewLine();
  if ((g_settings_form_style == 0 || g_settings_form_style == 3 || g_settings_form_style == 4) && label[0] != '#') {
    const ImVec2 start = ImGui::GetCursorScreenPos();
    const float x=ImGui::GetCursorPosX(), width=ImGui::GetContentRegionAvail().x;
    ImDrawList* draw=ImGui::GetWindowDrawList();
    constexpr float label_h=26.0f;
    const ImU32 bg=g_settings_form_style==0?IM_COL32(18,20,35,224):
                   g_settings_form_style==3?IM_COL32(24,38,54,242):IM_COL32(12,28,47,218);
    draw->AddRectFilled(start,ImVec2(start.x+width,start.y+label_h),bg,7.0f);
    draw->AddRectFilled(start,ImVec2(start.x+3,start.y+label_h),settings_accent(g_settings_form_style),2.0f);
    draw->AddText(settings_heading_font(),15.0f,ImVec2(start.x+12,start.y+4),
                  IM_COL32(229,235,245,255),label);
    ImGui::SetCursorPos(ImVec2(x,ImGui::GetCursorPosY()+label_h+5.0f));
    ImGui::SetNextItemWidth(width);
    return;
  }
  if (g_settings_form_style == 0 && label[0] != '#') {
    const ImVec2 p=ImGui::GetCursorScreenPos();
    const float width=ImGui::GetContentRegionAvail().x;
    ImDrawList* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(p,ImVec2(p.x+width,p.y+28),IM_COL32(18,20,35,224),6.0f);
    draw->AddRectFilled(p,ImVec2(p.x+3,p.y+28),settings_accent(0),2.0f);
  }
  if (label[0] != '#' || label[1] != '#') {
    if (g_settings_form_style == 0) ImGui::PushFont(settings_heading_font());
    ImGui::TextUnformatted(label);
    if (g_settings_form_style == 0) ImGui::PopFont();
  }
  ImGui::SetNextItemWidth(-1.0f);
}

static bool settings_toggle_draw(const char* label, bool* value);
// Every setting row goes through these three: a search shows only the matching ones, a closed section
// none (settings_row_begin above).
static bool settings_toggle(const char* label, bool* value) {
  const SettingsRow row = settings_row_begin(label);
  if (!row.shown) return false;
  const bool changed = settings_toggle_draw(label, value);
  settings_row_end(row);
  return changed;
}
static bool settings_toggle_draw(const char* label, bool* value) {
  if (g_settings_form_style == 2 && label[0] != '#') return settings_gd_toggle(label,value);
  if (g_settings_form_style >= 6) return settings_widgets::Checkbox(label,value);
  if (ImGui::GetCurrentWindow()->DC.IsSameLine)
    ImGui::NewLine();
  ImGui::PushID(label);
  const float width = std::max(120.0f, ImGui::GetContentRegionAvail().x);
  const float text_width = std::max(65.0f, width - 68.0f);
  const ImVec2 text_size = ImGui::CalcTextSize(label, nullptr, true, text_width);
  const float height = std::max(36.0f, text_size.y + 16.0f);
  // Clean side paints this row as a slanted strip; its focus frame follows the strip.
  const bool slanted = g_settings_form_style == 0;
  const bool clicked = slanted ? settings_shaped_hit_button("##switch", ImVec2(width, height)) :
                                 settings_hit_button("##switch", ImVec2(width, height));
  settings_page::label_item(label);
  const bool nav_frame = slanted && settings_nav_frame_on_item();
  if (clicked) *value = !*value;
  const bool focused = ImGui::IsItemHovered() || ImGui::IsItemFocused();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
  const ImU32 ink = ImGui::GetColorU32(ImGuiCol_Text);
  if (g_settings_form_style == 0) {
    const ImVec2 strip[]={ImVec2(a.x+7,a.y),ImVec2(b.x,a.y-4),
                          ImVec2(b.x-4,b.y-4),ImVec2(a.x,b.y)};
    draw->AddConvexPolyFilled(strip,4,IM_COL32(10,12,27,205));
    draw->AddLine(strip[0],strip[1],settings_palette_tint(0,IM_COL32(215,49,144,160)),1.0f);
    if (nav_frame) settings_nav_frame(draw,strip);
  } else if (g_settings_form_style == 3 || g_settings_form_style == 4) {
    draw->AddRectFilled(a,b,g_settings_form_style==3?
                        IM_COL32(30,42,58,249):IM_COL32(12,28,47,218),10.0f);
    // The solid row surface carries the control; edge rails added visual noise and
    // made the first label look clipped against a blue line.
  }
  if (focused) draw->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(1,1,1,0.045f)), 5.0f);
  draw->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(a.x+(g_settings_form_style==0||g_settings_form_style>=3?12.0f:0.0f), a.y + 8.0f), ink,
                label, nullptr, text_width);
  const ImVec2 start(b.x - 48.0f, a.y + (height - 24.0f) * 0.5f);
  const ImVec2 end(b.x - 2.0f, start.y + 24.0f);
  const ImU32 accent = settings_accent(g_settings_form_style);
  draw->AddRectFilled(start, end, *value ? accent : IM_COL32(63, 74, 91, 255), 12.0f);
  if (focused) draw->AddRect(start, end, IM_COL32(229, 237, 251, 210), 12.0f, 0, 1.5f);
  const ImGuiID id = ImGui::GetID("position");
  float position = ImGui::GetStateStorage()->GetFloat(id, *value ? 1.0f : 0.0f);
  position += ((*value ? 1.0f : 0.0f) - position) * std::min(1.0f, ImGui::GetIO().DeltaTime * 18.0f);
  ImGui::GetStateStorage()->SetFloat(id, position);
  draw->AddCircleFilled(ImVec2(start.x + 12.0f + position * 22.0f, start.y + 12.0f),
                        8.5f, IM_COL32(248, 250, 255, 255), 24);
  ImGui::PopID();
  return clicked;
}

static bool settings_combo_draw(const char* label, int* value, const char* const items[], int count);
static bool settings_combo(const char* label, int* value, const char* const items[], int count) {
  const SettingsRow row = settings_row_begin(label);
  if (!row.shown) return false;
  const bool changed = settings_combo_draw(label, value, items, count);
  settings_row_end(row);
  return changed;
}
static bool settings_combo_draw(const char* label, int* value, const char* const items[], int count) {
  if (g_settings_form_style == 2 && label[0] != '#') return settings_gd_combo(label,value,items,count);
  if (g_settings_form_style >= 6) {
    ImGui::PushID(label);
    const float available = ImGui::GetContentRegionAvail().x;
    const float width = std::min(330.0f, std::max(120.0f, available - ImGui::CalcTextSize(label).x - 12.0f));
    const bool stacked = *value >= 0 && *value < count &&
        ImGui::CalcTextSize(items[*value]).x + ImGui::GetFrameHeight() + 12.0f > width;
    if (stacked && label[0] != '#') ImGui::TextUnformatted(label);
    ImGui::SetNextItemWidth(stacked ? available : width);
    const bool result=ImGui::Combo("##value",value,items,count);
    settings_page::label_item(label);
    if (!stacked && label[0] != '#') { ImGui::SameLine(0.0f,4.0f); ImGui::TextUnformatted(label); }
    ImGui::PopID();
    return result;
  }
  ImGui::PushID(label);
  settings_field_label(label);
  const bool result = ImGui::Combo("##value", value, items, count);
    settings_page::label_item(label);
  ImGui::PopID();
  return result;
}

static bool settings_slider_draw(const char* label, int* value, int low, int high, const char* format, ImGuiSliderFlags flags);
static bool settings_slider(const char* label, int* value, int low, int high,
                            const char* format = "%d", ImGuiSliderFlags flags = 0) {
  const SettingsRow row = settings_row_begin(label);
  if (!row.shown) return false;
  const bool changed = settings_slider_draw(label, value, low, high, format, flags);
  settings_row_end(row);
  return changed;
}
static bool settings_slider_draw(const char* label, int* value, int low, int high, const char* format, ImGuiSliderFlags flags) {
  if (g_settings_form_style == 2 && label[0] != '#') return settings_gd_slider(label,value,low,high,format);
  if (g_settings_form_style >= 6) {
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(std::min(330.0f, std::max(120.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x - 12.0f)));
    const bool result=ImGui::SliderInt("##value",value,low,high,format,flags);
    settings_page::label_item(label);
    if (label[0] != '#') { ImGui::SameLine(0.0f,4.0f); ImGui::TextUnformatted(label); }
    ImGui::PopID();
    return result;
  }
  ImGui::PushID(label);
  settings_field_label(label);
  const bool result = ImGui::SliderInt("##value", value, low, high, format, flags);
    settings_page::label_item(label);
  ImGui::PopID();
  return result;
}

static void settings_slanted_text(ImDrawList* draw, ImFont* font, float size,
                                  ImVec2 pos, ImU32 color, const char* text, float slope = 0.12f) {
  const int begin=draw->VtxBuffer.Size;
  draw->AddText(font,size,pos,color,text);
  for (int v=begin;v<draw->VtxBuffer.Size;++v) {
    ImVec2& p=draw->VtxBuffer[v].pos;
    p.y-=(p.x-pos.x)*slope;
    p.x+=(size-(p.y-pos.y))*0.14f;
  }
}

static void settings_dashboard_icon(int index, ImVec2 c, float scale, ImU32 face) {
  ImDrawList* d=ImGui::GetWindowDrawList();
  const auto p=[&](float x,float y){return ImVec2(c.x+x*scale,c.y+y*scale);};
  const ImU32 ink=IM_COL32(25,39,67,255), edge=IM_COL32(79,97,131,255);
  switch(index) {
    case 0:
      d->AddRectFilled(p(-16,-12),p(16,9),ink,3*scale);
      d->AddRectFilled(p(-12,-8),p(12,5),IM_COL32(86,128,163,255),1*scale);
      d->AddLine(p(-11,-7),p(11,-7),edge,2*scale);
      d->AddRectFilled(p(-2,9),p(2,14),ink);
      d->AddRectFilled(p(-10,14),p(10,17),ink,1*scale);
      break;
    case 1:
      d->AddRectFilled(p(-15,-5),p(-8,5),ink,1*scale);
      d->AddTriangleFilled(p(-9,-5),p(1,-12),p(1,12),ink);
      d->PathArcTo(p(0,0),10*scale,-0.78f,0.78f,12);d->PathStroke(ink,0,4*scale);
      d->PathArcTo(p(0,0),17*scale,-0.78f,0.78f,16);d->PathStroke(ink,0,4*scale);
      break;
    case 2:
      for(int tooth=0;tooth<8;++tooth) {
        const float a=tooth*0.785398f;
        d->AddLine(p(std::cos(a)*7,std::sin(a)*7),p(std::cos(a)*16,std::sin(a)*16),ink,7*scale);
      }
      d->AddCircleFilled(c,12*scale,ink,32);
      d->AddCircleFilled(c,4*scale,face,20);
      break;
    case 3: {
      const ImVec2 q[]={p(-14,-8),p(14,-8),p(20,12),p(13,15),p(6,7),p(-6,7),p(-13,15),p(-20,12)};
      d->AddConvexPolyFilled(q,8,ink);
      d->AddLine(p(-12,-1),p(-4,-1),edge,3*scale);d->AddLine(p(-8,-5),p(-8,3),edge,3*scale);
      d->AddCircleFilled(p(9,-2),2*scale,edge);d->AddCircleFilled(p(13,2),2*scale,edge);
      break;
    }
    case 4:
      d->AddRectFilled(p(-13,-12),p(9,8),IM_COL32(62,84,117,255),2*scale);
      d->AddRectFilled(p(-7,-6),p(15,14),ink,2*scale);
      d->AddLine(p(-3,-2),p(10,-2),edge,2*scale);d->AddLine(p(-3,3),p(7,3),edge,2*scale);
      break;
    case 5:
      d->AddLine(p(-10,13),p(10,-9),ink,9*scale);
      d->AddLine(p(-3,5),p(9,-8),edge,3*scale);
      d->AddCircleFilled(p(13,-12),5*scale,ink,16);
      d->AddLine(p(-10,-13),p(-10,-5),ink,2*scale);d->AddLine(p(-14,-9),p(-6,-9),ink,2*scale);
      break;
    default:
      d->AddRectFilled(p(-11,-15),p(11,15),ink,2*scale);
      d->AddTriangleFilled(p(3,-15),p(11,-7),p(3,-7),edge);
      d->AddLine(p(-6,-2),p(6,-2),edge,2*scale);d->AddLine(p(-6,4),p(6,4),edge,2*scale);
      d->AddLine(p(-6,10),p(3,10),edge,2*scale);
      break;
  }
}

static ImU32 settings_dashboard_tile_color(int index, int variant) {
  static constexpr ImU32 all_colors[4][8] = {
    {IM_COL32(255,201,25,255),IM_COL32(230,55,153,255),IM_COL32(37,165,91,255),
     IM_COL32(104,65,205,255),IM_COL32(37,129,223,255),IM_COL32(241,105,59,255),IM_COL32(139,83,218,255),IM_COL32(33,178,185,255)},
    {IM_COL32(255,178,60,255),IM_COL32(255,93,127,255),IM_COL32(110,221,104,255),
     IM_COL32(85,189,255,255),IM_COL32(95,130,251,255),IM_COL32(255,126,70,255),IM_COL32(204,110,246,255),IM_COL32(64,212,216,255)},
    {IM_COL32(244,213,147,255),IM_COL32(236,172,195,255),IM_COL32(167,205,165,255),
     IM_COL32(192,183,226,255),IM_COL32(163,195,225,255),IM_COL32(234,190,159,255),IM_COL32(194,179,214,255),IM_COL32(165,213,210,255)},
    {IM_COL32(117,179,246,255),IM_COL32(230,126,210,255),IM_COL32(112,214,191,255),
     IM_COL32(154,151,246,255),IM_COL32(110,171,234,255),IM_COL32(242,152,138,255),IM_COL32(195,145,242,255),IM_COL32(122,212,224,255)}};
  return all_colors[std::clamp(variant,0,3)][std::clamp(index,0,7)];
}

static int settings_icon_variant() {
  static const int variant=[] {
    const char* value=std::getenv("MELEE_UI_ICON_VARIANT");
    return value && value[0]=='B' ? 1 : 0;
  }();
  return variant;
}

// Alternate icon language for the reference comparisons. B uses a common 2.4 px stroke,
// larger pictograms, and fewer small details so the set stays legible on the game at 800x600.
static void settings_symbol_icon(int index,ImVec2 c,float scale,ImU32 ink) {
  ImDrawList* d=ImGui::GetWindowDrawList();
  const auto p=[&](float x,float y){return ImVec2(c.x+x*scale,c.y+y*scale);};
  const float w=2.6f*scale;
  switch(index) {
    case 0:
      d->AddRect(p(-15,-11),p(15,8),ink,3*scale,0,w);
      d->AddLine(p(-12,5),p(12,5),ink,w);
      d->AddLine(p(0,8),p(0,15),ink,w);
      d->AddLine(p(-9,15),p(9,15),ink,w);
      break;
    case 1:
      d->AddRectFilled(p(-14,-6),p(-8,6),ink,2*scale);
      d->AddTriangleFilled(p(-8,-6),p(1,-13),p(1,13),ink);
      d->PathArcTo(p(1,0),11*scale,-.78f,.78f,14);d->PathStroke(ink,0,w);
      d->PathArcTo(p(1,0),18*scale,-.78f,.78f,18);d->PathStroke(ink,0,w);
      break;
    case 2:
      for(int tooth=0;tooth<8;++tooth) {
        const float a=tooth*.78539816f;
        d->AddLine(p(std::cos(a)*10,std::sin(a)*10),
                   p(std::cos(a)*17,std::sin(a)*17),ink,5.5f*scale);
      }
      d->AddCircleFilled(c,11*scale,ink,32);
      d->AddCircleFilled(c,4*scale,IM_COL32(24,39,57,255),24);
      break;
    case 3: {
      const ImVec2 pad[]={p(-18,-5),p(-15,-10),p(15,-10),p(18,-5),
                          p(21,11),p(16,14),p(10,8),p(-10,8),p(-16,14),p(-21,11)};
      d->AddConvexPolyFilled(pad,10,ink);
      d->AddLine(p(-12,-1),p(-3,-1),IM_COL32(37,55,79,255),w);
      d->AddLine(p(-7.5f,-5.5f),p(-7.5f,3.5f),IM_COL32(37,55,79,255),w);
      d->AddCircleFilled(p(8,-3),2*scale,IM_COL32(37,55,79,255));
      d->AddCircleFilled(p(13,1),2*scale,IM_COL32(37,55,79,255));
      break;
    }
    case 4:
      d->AddRect(p(-17,-11),p(5,7),ink,3*scale,0,w);
      d->AddRect(p(-5,-5),p(17,13),ink,3*scale,0,w);
      d->AddLine(p(0,3),p(12,3),ink,w);
      break;
    case 5:
      d->AddCircle(c,16*scale,ink,32,w);
      d->AddCircleFilled(p(-7,-3),2.4f*scale,ink);
      d->AddCircleFilled(p(0,-9),2.4f*scale,ink);
      d->AddCircleFilled(p(8,-3),2.4f*scale,ink);
      d->AddCircleFilled(p(5,6),2.4f*scale,ink);
      break;
    case 7:
      d->AddRect(p(-15,-12),p(15,15),ink,3*scale,0,w);
      d->AddLine(p(-15,-3),p(15,-3),ink,w);
      d->AddLine(p(0,-12),p(0,-3),ink,w);
      d->AddLine(p(-6,6),p(6,6),ink,w);
      d->AddLine(p(0,0),p(0,12),ink,w);
      break;
    default:
      d->AddLine(p(-5,-12),p(-17,0),ink,w);
      d->AddLine(p(-17,0),p(-5,12),ink,w);
      d->AddLine(p(5,-12),p(17,0),ink,w);
      d->AddLine(p(17,0),p(5,12),ink,w);
      break;
  }
}

static ImU32 settings_mix_color(ImU32 a, ImU32 b, float amount) {
  const ImVec4 left=ImGui::ColorConvertU32ToFloat4(a);
  const ImVec4 right=ImGui::ColorConvertU32ToFloat4(b);
  const float t=std::clamp(amount,0.0f,1.0f);
  return ImGui::ColorConvertFloat4ToU32(ImVec4(
      left.x+(right.x-left.x)*t,left.y+(right.y-left.y)*t,
      left.z+(right.z-left.z)*t,left.w+(right.w-left.w)*t));
}

// Tint the rounded polygon vertices rather than layering rectangular gradient strips. The
// gradient keeps its smooth, anti-aliased corners even when the tile is scaled down to 800x600.
static void settings_rounded_gradient(ImDrawList* draw, ImVec2 a, ImVec2 b,
                                      float rounding, ImU32 top, ImU32 bottom,
                                      ImDrawFlags corners=0) {
  const int first=draw->VtxBuffer.Size;
  draw->AddRectFilled(a,b,IM_COL32_WHITE,rounding,corners);
  for(int v=first;v<draw->VtxBuffer.Size;++v) {
    const float t=(draw->VtxBuffer[v].pos.y-a.y)/std::max(1.0f,b.y-a.y);
    // AddRectFilled's anti-alias fringe has zero alpha; keep it when tinting the vertices.
    const float coverage=((draw->VtxBuffer[v].col>>IM_COL32_A_SHIFT)&0xff)/255.0f;
    ImVec4 shade=ImGui::ColorConvertU32ToFloat4(settings_mix_color(top,bottom,t));
    shade.w*=coverage;
    draw->VtxBuffer[v].col=ImGui::ColorConvertFloat4ToU32(shade);
  }
}

static void settings_dashboard_tiles(SettingsState& state) {
  static constexpr const char* labels[] = {
      "Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes", "Mods"};
  // Two rows of four glossy tiles, with the game visible beside them.
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
  const ImVec2 saved = ImGui::GetCursorPos();
  const float scale = std::min(s.x / 900.0f, s.y / 600.0f);
  const float grid_w = std::min(s.x - 68.0f, 820.0f);
  const float gap = 15.0f * scale;
  const float tile_w = (grid_w - 3.0f * gap) / 4.0f;
  // The reference tiles are near-square. Derive the card height from its width,
  // with a viewport cap to preserve two unclipped rows at short window heights.
  const float tile_h = std::min(tile_w, (s.y - 200.0f) * 0.44f);
  const float left = p.x + (s.x - grid_w) * 0.5f;
  const float top = p.y + (s.y - 2.0f * tile_h - gap) * 0.60f;
  const float title_y = p.y + 45.0f * scale;
  const float heading_size = 76.0f * scale;
  const ImVec2 heading_extent = settings_heading_font()->CalcTextSizeA(
      heading_size, FLT_MAX, 0.0f, "Settings");
  const ImVec2 heading_pos(p.x + (s.x - heading_extent.x) * 0.5f, title_y);
  draw->AddText(settings_heading_font(), heading_size,
                ImVec2(heading_pos.x + 2.0f, heading_pos.y + 3.0f),
                IM_COL32(23, 32, 48, 160), "Settings");
  draw->AddText(settings_heading_font(), heading_size, heading_pos,
                IM_COL32(255, 252, 244, 255), "Settings");
  state.dashboard_home_frames = std::min(30.0f,
      state.dashboard_home_frames + ImGui::GetIO().DeltaTime * 60.0f);
  for (int i = 0; i < 8; ++i) {
    const int row = i < 4 ? 0 : 1, col = i < 4 ? i : i - 4;
    const float progress = std::clamp((state.dashboard_home_frames - i * 1.0f) / 15.0f, 0.0f, 1.0f);
    const float eased = 1.0f - std::pow(1.0f - progress, 3.0f);
    const float x = left + col * (tile_w + gap);
    const float y = top + row * (tile_h + gap) + (1.0f - eased) * 45.0f;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushID(i);
    const bool activated = settings_shaped_hit_button("##toy_tile", ImVec2(tile_w, tile_h));
    const bool nav_frame = settings_nav_frame_on_item();
    if (i == state.active_tab) ImGui::SetItemDefaultFocus();
    if (state.home_focus_reset && i == 0) {
      ImGui::SetFocusID(ImGui::GetItemID(), ImGui::GetCurrentWindow());
      state.home_focus_reset = false;
    }
    const bool focused = ImGui::IsItemFocused();
    // The rightmost Controls tile sits near the dashboard's safe-area edge. Check the
    // rendered tile bounds as well as ImGui's item state so a neighboring nav item cannot
    // suppress its hover response on wide or scaled windows.
    const bool hovered = ImGui::IsItemHovered() ||
        ImGui::IsMouseHoveringRect(ImVec2(x,y),ImVec2(x+tile_w,y+tile_h),true);
    ImGui::PopID();
    if (focused && state.open && settings_nav_input_pressed()) state.active_tab = i;
    const bool selected = state.active_tab == i || hovered;
    const float target = selected ? 1.0f : 0.0f;
    state.dashboard_hover[i] += (target - state.dashboard_hover[i]) *
        std::min(1.0f, ImGui::GetIO().DeltaTime * 14.0f);
    const float lift = state.dashboard_hover[i] * 5.0f;
    const ImVec2 a(x, y - lift), b(x + tile_w, y + tile_h - lift);
    const float rounding = std::min(tile_w*.20f,tile_h*.25f);
    draw->AddRectFilled(ImVec2(x + 3, y + 11), ImVec2(b.x + 4, b.y + 9),
                        IM_COL32(2, 5, 16, 48), rounding+5);
    draw->AddRectFilled(ImVec2(x + 2, y + 8), ImVec2(b.x + 3, b.y + 6),
                        IM_COL32(5, 10, 25, 84), rounding+2);
    const ImU32 tile_color=settings_dashboard_tile_color(i,g_settings_palette);
    const ImU32 side_top=settings_mix_color(tile_color,IM_COL32(20,25,53,255),.22f);
    const ImU32 side_bottom=settings_mix_color(tile_color,IM_COL32(9,13,37,255),.42f);
    settings_rounded_gradient(draw,ImVec2(a.x,a.y+7),ImVec2(b.x,b.y+7),rounding,
                              side_top,side_bottom);
    // Keep the deep lower shading and soft wall for dimensionality, with no hard
    // contrasting outline around the tile.
    settings_rounded_gradient(draw,a,ImVec2(b.x,b.y-5),rounding,
        settings_mix_color(tile_color,IM_COL32_WHITE,.10f),
        settings_mix_color(tile_color,IM_COL32(27,31,69,255),.16f));
    settings_rounded_gradient(draw,ImVec2(a.x+5,a.y+4),
        ImVec2(b.x-5,a.y+tile_h*.55f),rounding-5,
        IM_COL32(255,255,255,42),IM_COL32(255,255,255,0),ImDrawFlags_RoundCornersTop);
    if (nav_frame)
      draw->AddRect(a, ImVec2(b.x,b.y-5), settings_mix_color(tile_color,IM_COL32_WHITE,.75f), rounding, 0, 2.0f);
    draw->PushClipRect(ImVec2(a.x+5,a.y+4),ImVec2(b.x-5,b.y-7),true);
    draw->AddEllipseFilled(ImVec2(a.x+tile_w*.46f,a.y+tile_h*.06f),
                           ImVec2(tile_w*.53f,tile_h*.31f),
                           IM_COL32(255,255,255,9),0,48);
    draw->PopClipRect();
    // Color is carried by the face and shadow, not a shiny rim or drawn glare streak.
    if (selected)
      draw->AddRectFilled(ImVec2(a.x+7,a.y+7),ImVec2(b.x-7,b.y-7),
                          IM_COL32(255,255,255,10),rounding-5);
    const ImVec2 icon((a.x + b.x) * 0.5f, a.y + tile_h * 0.40f);
    if(settings_icon_variant()!=0)
      settings_symbol_icon(i,ImVec2(icon.x+1.2f,icon.y+2.0f),
                           std::max(1.04f,scale*1.31f),IM_COL32(4,12,29,78));
    if(settings_icon_variant()==0)
      settings_dashboard_icon(i,icon,std::max(1.15f,scale*1.60f),tile_color);
    else
      settings_symbol_icon(i,icon,std::max(1.04f,scale*1.31f),IM_COL32(26,40,66,255));
    const float font_size = std::max(17.0f, 22.0f * scale);
    const ImVec2 measured = settings_heading_font()->CalcTextSizeA(font_size, FLT_MAX, 0, labels[i]);
    const ImVec2 label_pos((a.x + b.x - measured.x) * 0.5f, a.y + tile_h * 0.70f);
    draw->AddText(settings_heading_font(),font_size,label_pos,
                  IM_COL32(20,30,54,255),labels[i]);
    if (activated && state.open) {
      state.active_tab = i;
      state.dashboard_detail_open = true;
      state.content_anim_frame = 0.0f;
    }
  }
  draw->AddText(ImVec2(left, p.y + s.y - 31), IM_COL32(248, 249, 254, 255), "A  SELECT     B  BACK");
  ImGui::SetCursorScreenPos(ImVec2(p.x + s.x - 49, p.y + 18));
  if (settings_close_hit("##close_dashboard", ImVec2(32, 32))) state.open = false;
  draw->AddText(ImVec2(p.x + s.x - 39, p.y + 23), IM_COL32(250, 250, 255, 255), "X");
  ImGui::SetCursorPos(saved);
}

static void settings_detail_header(SettingsState& state,int appearance) {
  static const char* names[]={"Video","Audio","Game","Controls","Overlays","Customize","Gecko Codes", "Mods"};
  const ImVec2 p=ImGui::GetWindowPos(),s=ImGui::GetWindowSize();
  ImDrawList* d=ImGui::GetWindowDrawList();
  const bool dashboard=appearance==1;
  if (dashboard) d->AddRectFilled(p,ImVec2(p.x+s.x,p.y+102),IM_COL32(15,23,35,244),14.0f);
  else {
    const ImVec2 box_end(std::min(p.x+s.x-63,p.x+405),p.y+96);
    d->AddRectFilled(ImVec2(p.x+12,p.y+8),box_end,IM_COL32(10,23,40,248),20.0f);
    d->AddRect(ImVec2(p.x+12,p.y+8),box_end,settings_palette_tint(3,IM_COL32(96,169,235,145)),20.0f,0,1.5f);
    // Keep the compact radial detail header clean: its underline sat directly
    // against the live-game layer and read like a blue rail running into the title.
  }
  const ImVec2 icon(p.x+66,p.y+51);
  if(dashboard) {
    d->AddRectFilled(ImVec2(p.x+25,p.y+16),ImVec2(p.x+105,p.y+91),IM_COL32(7,13,26,255),17);
    const ImU32 tile=settings_dashboard_tile_color(state.active_tab,g_settings_palette);
    settings_rounded_gradient(d,ImVec2(p.x+25,p.y+12),ImVec2(p.x+105,p.y+84),17,
        settings_mix_color(tile,IM_COL32_WHITE,.38f),
        settings_mix_color(tile,IM_COL32(25,30,55,255),.18f));
    if(settings_icon_variant()==0)settings_dashboard_icon(state.active_tab,icon,1.1f,tile);
    else settings_symbol_icon(state.active_tab,icon,1.0f,IM_COL32(26,40,66,255));
  } else {
    d->AddCircleFilled(ImVec2(icon.x,icon.y+4),36,IM_COL32(3,9,19,255),64);
    d->AddCircleFilled(icon,34,IM_COL32(40,57,73,255),64);
    d->AddCircleFilled(icon,29,IM_COL32(17,28,43,255),64);
    d->AddCircle(icon,34,IM_COL32(132,160,181,205),64,1.5f);
    d->PathArcTo(icon,29,-1.4f,.6f,24);d->PathStroke(settings_accent(3),0,3.0f);
    if(settings_icon_variant()==0)draw_settings_icon(state.active_tab,icon,IM_COL32(238,249,255,255));
    else settings_symbol_icon(state.active_tab,icon,.9f,IM_COL32(238,249,255,255));
  }
  d->AddText(settings_heading_font(),32,ImVec2(p.x+127,p.y+20),IM_COL32(247,250,255,255),names[state.active_tab]);
  d->AddText(ImVec2(p.x+129,p.y+63),IM_COL32(181,198,218,255),dashboard?"<  Back to tiles":"<  Back to the wheel");
  ImGui::SetCursorScreenPos(ImVec2(p.x+20,p.y+10));
  if(settings_hit_button("##category_home",ImVec2(305,80))) {
    settings_back_page(state,appearance);state.dashboard_home_frames=0;
  }
  ImGui::SetCursorScreenPos(ImVec2(p.x+s.x-43,p.y+17));
  if(settings_close_hit("##detail_close",ImVec2(30,30)))state.open=false;
  if (!dashboard) {
    d->AddCircleFilled(ImVec2(p.x+s.x-28,p.y+32),24,IM_COL32(10,23,40,220),32);
    d->AddCircle(ImVec2(p.x+s.x-28,p.y+32),24,settings_palette_tint(3,IM_COL32(96,169,235,145)),32,1.5f);
  }
  d->AddLine(ImVec2(p.x+s.x-35,p.y+25),ImVec2(p.x+s.x-21,p.y+39),IM_COL32(220,234,249,255),2);
  d->AddLine(ImVec2(p.x+s.x-21,p.y+25),ImVec2(p.x+s.x-35,p.y+39),IM_COL32(220,234,249,255),2);
}
