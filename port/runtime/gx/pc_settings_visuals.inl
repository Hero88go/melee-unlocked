// Render-only settings presentation. All values and save behavior remain in pc_settings.cpp.
// Mockup provenance: ui_sources/mockups/ORIGIN.md. No game state is written here.
static bool settings_hit_button(const char* id, ImVec2 size,
                               ImGuiButtonFlags flags = ImGuiButtonFlags_EnableNav) {
  return ImGui::InvisibleButton(id, size, flags);
}

// Close (X) buttons are mouse-only. Keyboard/controller focus landing on one meant pressing A
// closed the whole panel, and the same A then reached the game as a fresh press.
static bool settings_close_hit(const char* id, ImVec2 size) {
  ImGui::PushItemFlag(ImGuiItemFlags_NoNav, true);
  const bool pressed = ImGui::InvisibleButton(id, size, 0);
  ImGui::PopItemFlag();
  return pressed;
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
static int g_settings_form_style = 0;
static int g_settings_palette = 0;
static bool g_settings_custom_color_enabled = false;
static ImVec4 g_settings_custom_color = ImVec4(0.96f,0.15f,0.52f,1.0f);
static ImVec2 g_settings_gd_origin;
static float g_settings_gd_scale = 1.0f;
static std::string g_settings_gd_help;
static float g_settings_gd_back_x = 0;
static bool g_settings_focus_next_gd_row = false;

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
      ImGui::PushFont(g_settings_old_font);
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
  row.activated = settings_hit_button("##kit_row", ImVec2(451.5f * g_settings_gd_scale, 30.0f * g_settings_gd_scale));
  row.selected = ImGui::IsItemHovered() || ImGui::IsItemFocused() || ImGui::IsItemActive();
  if (g_settings_focus_next_gd_row) {
    // SetKeyboardFocusHere(-1) here targets the item before this row (the category
    // control that just disappeared), leaving the new page with a dead navigation ID.
    ImGui::SetItemDefaultFocus();
    g_settings_focus_next_gd_row = false;
    row.selected = true;
  }
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
  if (row.activated) {
    const float mouse_design = (ImGui::GetIO().MousePos.x-g_settings_gd_origin.x)/g_settings_gd_scale-(240-row.y-15)*.25f;
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouse_design<370) direction=-1;
    else if (ImGui::IsMouseReleased(ImGuiMouseButton_Left) && mouse_design>500) direction=1;
    else ImGui::OpenPopup("##kit_choices");
  }
  bool changed = direction != 0 && count>0;
  if (changed) *value=(*value+direction+count)%count;
  const float x=340+row.lift, baseline=row.y+20.28f+row.lift;
  row.text(x,baseline,"<",row.ink);
  row.text(x+182,baseline,">",row.ink);
  const char* shown= *value>=0 && *value<count ? items[*value] : "Custom";
  if(std::strcmp(label,"Aspect ratio")==0) {
    static constexpr const char* short_aspects[]={
        "Auto (73:60 / 16:9)","73:60 native","4:3","16:9","Stretch"};
    if(*value>=0 && *value<5) shown=short_aspects[*value];
  }
  const float value_width=std::min(158.0f,gd_kit_text_width("row",shown));
  row.text(x+95-value_width*.5f,baseline,shown,row.ink,158);
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
static void settings_page_footer(SettingsState& state, int appearance) {
  const ImVec2 p = ImGui::GetWindowPos(), s = ImGui::GetWindowSize();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  if (appearance == 6 || appearance == 7) {
    const float top=p.y+s.y-35.0f;
    draw->AddLine(ImVec2(p.x+8,top),ImVec2(p.x+s.x-8,top),IM_COL32(70,78,88,255));
    ImGui::SetCursorScreenPos(ImVec2(p.x+8,top+6));
    if (ImGui::Button("Save settings",ImVec2(ImGui::CalcTextSize("Save settings").x + 20.0f,27))) state.dirty=true;
    ImGui::SameLine(0.0f,6.0f);
    if (ImGui::Button("Close",ImVec2(70,27))) state.open=false;
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
    const bool clicked=settings_hit_button(chip==0?"##page_back":"##page_return",
                                           ImVec2(w,appearance==0?38.0f:34.0f));
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
    const char* label=chip==0?"B  BACK":"F1  RETURN";
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
    const bool clicked = settings_hit_button("##page_back", ImVec2(w, h));
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
    const ImU32 ink = IM_COL32(242, 239, 228, 255);
    const float font = 15.0f * g_settings_gd_scale;
    const ImVec2 label = settings_heading_font()->CalcTextSizeA(font, FLT_MAX, 0.0f, "B  BACK");
    draw->AddText(settings_heading_font(), font,
                  ImVec2(at.x + (w - label.x) * 0.5f, at.y + (h - label.y) * 0.5f - lift), ink, "B  BACK");
    if (clicked) settings_back_page(state, appearance);
  }
  const float menu_x = p.x+s.x-(gd?34:40);
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

static bool settings_toggle(const char* label, bool* value) {
  if (g_settings_form_style == 2 && label[0] != '#') return settings_gd_toggle(label,value);
  if (g_settings_form_style >= 6) return ImGui::Checkbox(label,value);
  if (ImGui::GetCurrentWindow()->DC.IsSameLine)
    ImGui::NewLine();
  ImGui::PushID(label);
  const float width = std::max(120.0f, ImGui::GetContentRegionAvail().x);
  const float text_width = std::max(65.0f, width - 68.0f);
  const ImVec2 text_size = ImGui::CalcTextSize(label, nullptr, true, text_width);
  const float height = std::max(36.0f, text_size.y + 16.0f);
  const bool clicked = settings_hit_button("##switch", ImVec2(width, height));
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

static bool settings_combo(const char* label, int* value, const char* const items[], int count) {
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
    if (!stacked && label[0] != '#') { ImGui::SameLine(0.0f,4.0f); ImGui::TextUnformatted(label); }
    ImGui::PopID();
    return result;
  }
  ImGui::PushID(label);
  settings_field_label(label);
  const bool result = ImGui::Combo("##value", value, items, count);
  ImGui::PopID();
  return result;
}

static bool settings_slider(const char* label, int* value, int low, int high,
                            const char* format = "%d", ImGuiSliderFlags flags = 0) {
  if (g_settings_form_style == 2 && label[0] != '#') return settings_gd_slider(label,value,low,high,format);
  if (g_settings_form_style >= 6) {
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(std::min(330.0f, std::max(120.0f, ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(label).x - 12.0f)));
    const bool result=ImGui::SliderInt("##value",value,low,high,format,flags);
    if (label[0] != '#') { ImGui::SameLine(0.0f,4.0f); ImGui::TextUnformatted(label); }
    ImGui::PopID();
    return result;
  }
  ImGui::PushID(label);
  settings_field_label(label);
  const bool result = ImGui::SliderInt("##value", value, low, high, format, flags);
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
  static constexpr ImU32 all_colors[4][7] = {
    {IM_COL32(255,201,25,255),IM_COL32(230,55,153,255),IM_COL32(37,165,91,255),
     IM_COL32(104,65,205,255),IM_COL32(37,129,223,255),IM_COL32(241,105,59,255),IM_COL32(139,83,218,255)},
    {IM_COL32(255,178,60,255),IM_COL32(255,93,127,255),IM_COL32(110,221,104,255),
     IM_COL32(85,189,255,255),IM_COL32(95,130,251,255),IM_COL32(255,126,70,255),IM_COL32(204,110,246,255)},
    {IM_COL32(244,213,147,255),IM_COL32(236,172,195,255),IM_COL32(167,205,165,255),
     IM_COL32(192,183,226,255),IM_COL32(163,195,225,255),IM_COL32(234,190,159,255),IM_COL32(194,179,214,255)},
    {IM_COL32(117,179,246,255),IM_COL32(230,126,210,255),IM_COL32(112,214,191,255),
     IM_COL32(154,151,246,255),IM_COL32(110,171,234,255),IM_COL32(242,152,138,255),IM_COL32(195,145,242,255)}};
  return all_colors[std::clamp(variant,0,3)][std::clamp(index,0,6)];
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
      "Video", "Audio", "Game", "Controls", "Overlays", "Customize", "Gecko Codes"};
  // Four glossy tiles above three, with the game visible beside them.
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
  for (int i = 0; i < 7; ++i) {
    const int row = i < 4 ? 0 : 1, col = i < 4 ? i : i - 4;
    const float progress = std::clamp((state.dashboard_home_frames - i * 1.0f) / 15.0f, 0.0f, 1.0f);
    const float eased = 1.0f - std::pow(1.0f - progress, 3.0f);
    const float x = left + col * (tile_w + gap);
    const float y = top + row * (tile_h + gap) + (1.0f - eased) * 45.0f;
    ImGui::SetCursorScreenPos(ImVec2(x, y));
    ImGui::PushID(i);
    const bool activated = settings_hit_button("##toy_tile", ImVec2(tile_w, tile_h));
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
  static const char* names[]={"Video","Audio","Game","Controls","Overlays","Customize","Gecko Codes"};
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
