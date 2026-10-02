// Replay library: every .slp as a card (scanned off the UI thread, so the page never freezes), and a
// match stats page in the style players know from the Slippi Launcher.
#include <map>
#include <memory>
#include <tuple>
enum { ID_REPLAY_LIST=800, ID_REPLAY_BROWSE, ID_REPLAY_WATCH, ID_REPLAY_REFRESH, ID_REPLAY_BACK, ID_REPLAY_PREV, ID_REPLAY_NEXT, ID_REPLAY_ASPLAYED };
const UINT WM_APP_REPLAYS_LISTED = WM_APP + 20;   // lParam: heap ReplayScan*
const UINT WM_APP_REPLAY_LOADED = WM_APP + 21;    // lParam: heap ReplayLoaded*
HWND g_replays[8]{};             // list, browse, watch, refresh, back, prev, next, "as it was played"
HWND g_replay_stats=nullptr;     // the scrolling stats page
std::vector<std::filesystem::path> g_replay_files;
std::vector<launcher::replay::Info> g_replay_info;
std::string g_replay_status;
bool g_replay_active=false;
std::atomic<unsigned> g_replay_gen{0};
int g_replay_view=-1;            // -1: the list; otherwise the replay shown on the stats page
int g_replay_hot=-1, g_replay_hot_zone=0;   // hovered card, and 1 over its Stats pill, 2 over Watch
int g_stats_scroll=0, g_stats_height=0;
void report_launch_error(DWORD,const std::string&,const std::string&);
HWND make(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id,HFONT font=nullptr,DWORD ex=0);

struct ReplayScan { unsigned gen=0; std::vector<std::filesystem::path> files; std::vector<launcher::replay::Info> info; int selected=-1; };
struct ReplayLoaded { unsigned gen=0; size_t index=0; launcher::replay::Info info; };

HFONT replay_font(int px,int weight,const wchar_t* face=L"Segoe UI") {
  static std::map<std::tuple<int,int,std::wstring>,HFONT> fonts;
  auto& f=fonts[{px,weight,face}];
  if(!f) f=CreateFontW(-S(px),0,0,0,weight,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,face);
  return f;
}
HFONT replay_symbols(int px) { return replay_font(px,FW_NORMAL,L"Segoe UI Symbol"); }
COLORREF port_color(int port) {
  static const COLORREF c[4]={RGB(0xE0,0x4A,0x5C),RGB(0x4C,0x7C,0xF0),RGB(0xE8,0xB8,0x38),RGB(0x44,0xB8,0x6A)};
  return c[std::clamp(port-1,0,3)];
}
COLORREF stage_tint(int stage) {
  switch(stage) {
    case 0x02:return RGB(0x55,0x2C,0x78);   // Fountain of Dreams
    case 0x03:return RGB(0x2E,0x55,0x4A);   // Pokemon Stadium
    case 0x08:return RGB(0x3A,0x66,0x34);   // Yoshi's Story
    case 0x1c:return RGB(0x2A,0x58,0x7C);   // Dream Land
    case 0x1f:return RGB(0x2C,0x2E,0x70);   // Battlefield
    case 0x20:return RGB(0x48,0x1E,0x62);   // Final Destination
    default:return RGB(0x2A,0x34,0x50);
  }
}
const COLORREF C_GOLD=RGB(0xF2,0xC9,0x4C), C_CARD=RGB(0x13,0x1A,0x2A);
void hgrad(HDC dc,RECT r,COLORREF a,COLORREF b) {
  TRIVERTEX v[2]={{r.left,r.top,COLOR16(GetRValue(a)<<8),COLOR16(GetGValue(a)<<8),COLOR16(GetBValue(a)<<8),0},
                  {r.right,r.bottom,COLOR16(GetRValue(b)<<8),COLOR16(GetGValue(b)<<8),COLOR16(GetBValue(b)<<8),0}};
  GRADIENT_RECT g{0,1}; GradientFill(dc,v,2,&g,1,GRADIENT_FILL_RECT_H);
}
// A translucent colour over what is already drawn (lost stocks, disabled rows).
void shade(HDC dc,RECT r,COLORREF c,BYTE alpha) {
  HDC md=CreateCompatibleDC(dc); HBITMAP b=CreateCompatibleBitmap(dc,1,1); HGDIOBJ old=SelectObject(md,b);
  SetPixel(md,0,0,c); BLENDFUNCTION f{AC_SRC_OVER,0,alpha,0};
  AlphaBlend(dc,r.left,r.top,r.right-r.left,r.bottom-r.top,md,0,0,1,1,f);
  SelectObject(md,old); DeleteObject(b); DeleteDC(md);
}
void char_icon(HDC dc,int character,int x,int y,int size) {
  if(character<0||character>=26) return;
  HICON icon=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(100+character),IMAGE_ICON,size,size,LR_SHARED);
  if(icon) DrawIconEx(dc,x,y,icon,size,size,0,nullptr,DI_NORMAL);
}
int text_width(HDC dc,const std::wstring& s,HFONT f) {
  HGDIOBJ old=SelectObject(dc,f); SIZE z{}; GetTextExtentPoint32W(dc,s.c_str(),(int)s.size(),&z); SelectObject(dc,old); return z.cx;
}
std::wstring replay_clock(int frame) { const int s=std::max(frame,0)/60; wchar_t b[16]; swprintf_s(b,L"%d:%02d",s/60,s%60); return b; }
std::wstring replay_duration(const launcher::replay::Info& r) {
  if(r.last_frame<-122) return L"";
  const int s=(r.last_frame+123)/60; wchar_t b[24]; swprintf_s(b,L"%dm %02ds",s/60,s%60); return b;
}
std::wstring player_tag(const launcher::replay::Player& p) { return widen(p.code.empty()?p.name:p.code); }

void replay_status(const std::string& message) { g_replay_status=message; InvalidateRect(g_main,nullptr,FALSE); }

// "Show it as it was played": an online match leaves a session trace beside its replay, the same
// name with the extension .trace (what the player actually got: waits, rollbacks, long frames). The
// box (g_replay_as_played, kept in launcher.ini) can be ticked only for a replay that has one.
int replay_current() { return g_replay_view>=0?g_replay_view:(g_replays[0]?(int)SendMessageW(g_replays[0],LB_GETCURSEL,0,0):-1); }
bool replay_has_trace(int index) {
  if(index<0||index>=(int)g_replay_files.size()) return false;
  auto trace=g_replay_files[index]; trace.replace_extension(L".trace");
  std::error_code ec; return std::filesystem::is_regular_file(trace,ec);
}
void replay_trace_changed() {
  if(!g_replays[7]) return;
  EnableWindow(g_replays[7],replay_has_trace(replay_current()));
  InvalidateRect(g_replays[7],nullptr,FALSE);
}

// ---------------------------------------------------------------------------- scanning
void refresh_replays(const std::filesystem::path& selected={}) {
  auto choice=selected;
  int previous=(int)SendMessageW(g_replays[0],LB_GETCURSEL,0,0);
  if(choice.empty()&&previous>=0&&previous<(int)g_replay_files.size()) choice=g_replay_files[previous];
  std::vector<std::filesystem::path> folders{std::filesystem::u8path(g_dir)/"Replays",std::filesystem::u8path(work_dir())/"Replays"};
  PWSTR documents=nullptr;
  if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&documents))) { folders.push_back(std::filesystem::path(documents)/"Slippi");CoTaskMemFree(documents); }
  const unsigned gen=++g_replay_gen;
  if(g_replay_files.empty()) replay_status("Loading replays...");
  std::thread([gen,folders,choice]{
    // Dates come from the directory listing itself, read once per file: sorting with a file
    // query per comparison froze the page for seconds on a large Slippi folder.
    std::vector<std::pair<std::filesystem::file_time_type,std::filesystem::path>> found;
    for(const auto& folder:folders) {
      std::error_code ec;
      std::filesystem::recursive_directory_iterator it(folder,std::filesystem::directory_options::skip_permission_denied,ec),end;
      int scanned=0;
      while(!ec&&it!=end&&scanned++<20000) {
        if(it.depth()>3) it.disable_recursion_pending();
        auto extension=it->path().extension().wstring();
        std::transform(extension.begin(),extension.end(),extension.begin(),::towlower);
        std::error_code fe;
        if(extension==L".slp"&&it->is_regular_file(fe)) found.emplace_back(it->last_write_time(fe),it->path());
        it.increment(ec);
      }
    }
    std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.second<b.second;});
    found.erase(std::unique(found.begin(),found.end(),[](const auto& a,const auto& b){return a.second==b.second;}),found.end());
    if(!choice.empty()&&std::none_of(found.begin(),found.end(),[&](const auto& f){return f.second==choice;})) {
      std::error_code fe; found.emplace_back(std::filesystem::last_write_time(choice,fe),choice);
    }
    std::sort(found.begin(),found.end(),[](const auto& a,const auto& b){return a.first>b.first;});
    // Keep navigation instant for players with years of Slippi recordings.
    if(found.size()>500) {
      auto picked=std::find_if(found.begin(),found.end(),[&](const auto& f){return f.second==choice;});
      if(picked!=found.end()&&picked>=found.begin()+500) { auto keep=*picked; found.resize(499); found.push_back(keep); }
      else found.resize(500);
    }
    auto* scan=new ReplayScan; scan->gen=gen;
    for(const auto& f:found) { scan->files.push_back(f.second); scan->info.push_back(launcher::replay::inspect(f.second)); }
    for(size_t i=0;i<scan->files.size();++i) if(scan->files[i]==choice) scan->selected=(int)i;
    const auto files=scan->files;
    if(g_replay_gen!=gen||!PostMessageW(g_main,WM_APP_REPLAYS_LISTED,0,(LPARAM)scan)) { delete scan; return; }
    // Then every match's full stats, newest first, so winners and the stats page fill in.
    for(size_t i=0;i<files.size()&&g_replay_gen==gen;++i) {
      auto* loaded=new ReplayLoaded{gen,i,launcher::replay::inspect(files[i],true)};
      if(!PostMessageW(g_main,WM_APP_REPLAY_LOADED,0,(LPARAM)loaded)) { delete loaded; return; }
    }
  }).detach();
}
void stats_changed();
void replays_listed(ReplayScan* scan) {
  std::unique_ptr<ReplayScan> own(scan);
  if(scan->gen!=g_replay_gen) return;
  const auto shown=g_replay_view>=0&&g_replay_view<(int)g_replay_files.size()?g_replay_files[g_replay_view]:std::filesystem::path();
  g_replay_files=std::move(scan->files); g_replay_info=std::move(scan->info);
  SendMessageW(g_replays[0],WM_SETREDRAW,FALSE,0);
  SendMessageW(g_replays[0],LB_RESETCONTENT,0,0);
  for(const auto& f:g_replay_files) SendMessageW(g_replays[0],LB_ADDSTRING,0,(LPARAM)f.filename().c_str());
  int pick=scan->selected>=0?scan->selected:(g_replay_files.empty()?-1:0);
  if(pick>=0) SendMessageW(g_replays[0],LB_SETCURSEL,pick,0);
  SendMessageW(g_replays[0],WM_SETREDRAW,TRUE,0); InvalidateRect(g_replays[0],nullptr,TRUE);
  if(g_replay_view>=0) {
    auto it=std::find(g_replay_files.begin(),g_replay_files.end(),shown);
    g_replay_view=it==g_replay_files.end()?-1:int(it-g_replay_files.begin());
  }
  EnableWindow(g_replays[2],!g_replay_files.empty()&&!g_playing&&!g_building);
  replay_trace_changed();
  replay_status(g_replay_files.empty()?"Choose a Slippi replay to get started.":launcher::lang::fill(launcher::lang::tx("{count} replays"),launcher::lang::Args{{"count",std::to_string(g_replay_files.size())}}));
  stats_changed();
}
void replay_loaded(ReplayLoaded* loaded) {
  std::unique_ptr<ReplayLoaded> own(loaded);
  if(loaded->gen!=g_replay_gen||loaded->index>=g_replay_info.size()) return;
  g_replay_info[loaded->index]=std::move(loaded->info);
  RECT r{}; if(SendMessageW(g_replays[0],LB_GETITEMRECT,loaded->index,(LPARAM)&r)!=LB_ERR) InvalidateRect(g_replays[0],&r,FALSE);
  if((int)loaded->index==g_replay_view) stats_changed();
}

// ---------------------------------------------------------------------------- layout
void replay_layout() {
  const bool on=g_tab==3, stats=on&&g_replay_view>=0;
  for(int i:{0,1,3}) if(g_replays[i]) ShowWindow(g_replays[i],on&&!stats?SW_SHOW:SW_HIDE);
  for(int i:{4,5,6}) if(g_replays[i]) ShowWindow(g_replays[i],stats?SW_SHOW:SW_HIDE);
  if(g_replays[7]) { ShowWindow(g_replays[7],on?SW_SHOW:SW_HIDE); replay_trace_changed(); }
  if(g_replay_stats) ShowWindow(g_replay_stats,stats?SW_SHOW:SW_HIDE);
  if(g_replays[2]) {
    RECT r=stats?LR(800,22,158,38):LR(808,622,150,34);
    SetWindowPos(g_replays[2],nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowTextW(g_replays[2],stats?L"\x25B6  Watch replay":L"Watch Replay");
    ShowWindow(g_replays[2],on?SW_SHOW:SW_HIDE);
  }
  if(g_main) InvalidateRect(g_main,nullptr,FALSE);
}
void replay_open_stats(int index) {
  if(index<0||index>=(int)g_replay_info.size()) return;
  if(!g_replay_info[index].stats_loaded) g_replay_info[index]=launcher::replay::inspect(g_replay_files[index],true);
  g_replay_view=index; g_stats_scroll=0;
  SendMessageW(g_replays[0],LB_SETCURSEL,index,0);
  replay_layout(); stats_changed(); SetFocus(g_replay_stats);
}
void replay_back() { g_replay_view=-1; replay_layout(); SetFocus(g_replays[0]); }
void replay_step(int delta) { if(g_replay_view>=0) replay_open_stats(std::clamp(g_replay_view+delta,0,(int)g_replay_files.size()-1)); }
void replay_selection_changed() { InvalidateRect(g_replays[0],nullptr,FALSE); replay_trace_changed(); }

void browse_replay() {
  wchar_t file[32768]{}; auto folder=widen(g_dir+"\\Replays");
  OPENFILENAMEW dialog{sizeof dialog}; dialog.hwndOwner=g_main;
  const std::wstring dialog_title=launcher::lang::txw(L"Open replay");
  dialog.lpstrFilter=L"Slippi replays (*.slp)\0*.slp\0"; dialog.lpstrTitle=dialog_title.c_str();
  dialog.lpstrFile=file; dialog.nMaxFile=32768; dialog.lpstrInitialDir=folder.c_str();
  dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
  if(GetOpenFileNameW(&dialog)) refresh_replays(std::filesystem::path(file));
}
void watch_replay() {
  if(g_playing||g_building) { replay_status("Close the running game before opening a replay."); return; }
  int index=g_replay_view>=0?g_replay_view:(int)SendMessageW(g_replays[0],LB_GETCURSEL,0,0);
  if(index<0||index>=(int)g_replay_files.size()) return;
  if(g_iso.empty()||!file_exists(g_iso)) { replay_status("Choose your Melee disc image on the Play page first."); return; }
  // A replay of a match played on a mod (its stage is not in the standard game) would sit on a blank
  // screen: the standard game has no such stage. Say so instead of starting it.
  {
    const auto info=launcher::replay::inspect(g_replay_files[index]);
    if(info.valid&&info.stage_id>85) {
      replay_status(launcher::lang::fill(launcher::lang::tx("This replay was recorded with a mod (stage {id} is not in the standard game). It needs that mod to play."),launcher::lang::Args{{"id",std::to_string(info.stage_id)}}));
      return;
    }
  }
  std::string exe;
  if(g_engine==ENGINE_SOURCE&&!source_exe_dir().empty()) exe=source_exe_dir()+"\\melee_source.exe";
  else {
    std::vector<std::string> candidates{active_dir()+"\\melee_port_playback.exe"};
    auto root=repo_root();
    if(g_active_version.empty()&&!root.empty()) for(auto dir:{"build-sourceport-slippi","build-sourceport","build-playback"}) candidates.push_back(root+"\\"+dir+"\\port\\Release\\melee_port_playback.exe");
    for(const auto& path:candidates) if(file_exists(path)) { exe=path; break; }
  }
  if(exe.empty()||!file_exists(exe)) { replay_status("Replay playback is not installed for this build."); return; }
  auto sys=active_dir()+"\\SysPlayback";
  if(!file_exists(sys+"\\codehandler.bin")) sys=repo_root()+"\\port\\slippi_sys_playback";
  if(!file_exists(sys+"\\codehandler.bin")) { replay_status("The playback system files are missing from this installation."); return; }
  const auto cwd=work_dir();
  std::wstring command=widen("\""+exe+"\""+game_args()+" --sys-dir \""+sys+"\" --replay \"")+g_replay_files[index].wstring()+L"\"";
  // The viewer finds "<replay>.trace" beside the replay itself and shows the session's own timing.
  if(g_replay_as_played&&replay_has_trace(index)) command+=L" --as-experienced";
  PROCESS_INFORMATION process{}; DWORD error=launcher::start_process(widen(exe),command,widen(cwd),0,process);
  if(error) { report_launch_error(error,exe,cwd); return; }
  CloseHandle(process.hThread); crash_report::note_launch(); g_playing=true; g_replay_active=true;
  launcher::lobby::game_running(true); EnableWindow(g_play_btn,FALSE); EnableWindow(g_replays[2],FALSE);
  replay_status(launcher::lang::fill(launcher::lang::tx("Watching {file}"),launcher::lang::Args{{"file",g_replay_files[index].filename().u8string()}}));
  ShowWindow(g_main,SW_MINIMIZE);
  std::thread([h=process.hProcess]{ WaitForSingleObject(h,INFINITE); DWORD code=0; GetExitCodeProcess(h,&code); CloseHandle(h); PostMessageW(g_main,WM_APP_GAME_DONE,code,0); }).detach();
}

// ---------------------------------------------------------------------------- the list
// A player: character icon, then a pill in the port's colour with the connect code, a crown on the winner.
int draw_player_chip(HDC dc,const launcher::replay::Player& p,bool winner,int x,int cy,COLORREF under) {
  char_icon(dc,p.character,x,cy-S(12),S(24)); x+=S(28);
  const auto tag=player_tag(p); HFONT f=replay_font(12,FW_BOLD);
  const int w=text_width(dc,tag,f)+S(22);
  RECT pill{x,cy-S(11),x+w,cy+S(11)};
  const COLORREF pc=port_color(p.port);
  round_rect(dc,pill,11,lerp(under,pc,45,100),lerp(under,pc,45,100),pc);
  draw_text(dc,tag,pill,f,C_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  if(winner) { RECT crown{pill.right-S(9),pill.top-S(12),pill.right+S(9),pill.top+S(4)}; draw_text(dc,L"\x265B",crown,replay_symbols(13),C_GOLD,DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
  return pill.right;
}
void card_zones(RECT card,RECT* stats,RECT* watch) {
  *watch={card.right-S(46),card.top+S(12),card.right-S(16),card.top+S(42)};
  *stats={card.right-S(118),card.top+S(14),card.right-S(56),card.top+S(40)};
}
void draw_replay_card(DRAWITEMSTRUCT* item,HDC dc);
void draw_replay(DRAWITEMSTRUCT* item) {
  // A list box draws through its parent's DC, so a half-visible last card would spill over the
  // buttons below the list: everything here is clipped to the list first.
  HDC dc=item->hDC; const int saved=SaveDC(dc);
  RECT client; GetClientRect(item->hwndItem,&client); IntersectClipRect(dc,client.left,client.top,client.right,client.bottom);
  draw_replay_card(item,dc);
  RestoreDC(dc,saved);
}
void draw_replay_card(DRAWITEMSTRUCT* item,HDC dc) {
  RECT r=item->rcItem; fill(dc,r,C_CONTENT_BOT);
  if(item->itemID>=g_replay_files.size()) return;
  const auto& replay=g_replay_info[item->itemID];
  const bool selected=(item->itemState&ODS_SELECTED)!=0, hot=(int)item->itemID==g_replay_hot;
  RECT card=r; InflateRect(&card,-S(4),-S(4)); card.right-=S(4);
  HRGN rgn=CreateRoundRectRgn(card.left,card.top,card.right+1,card.bottom+1,S(16),S(16));
  const int inner=SaveDC(dc); ExtSelectClipRgn(dc,rgn,RGN_AND);
  hgrad(dc,card,hot?RGB(0x18,0x21,0x35):C_CARD,lerp(C_CARD,stage_tint(replay.stage_id),hot?75:60,100));
  RestoreDC(dc,inner); DeleteObject(rgn);
  round_rect(dc,card,8,NO_FILL,NO_FILL,selected?C_ACC_HI:(hot?C_BTN_BORDER:RGB(0x24,0x2E,0x46)));
  // Players
  int x=card.left+S(16); const int cy=card.top+S(28);
  if(replay.players.empty()) draw_text(dc,widen(replay.path.stem().u8string()),RECT{x,cy-S(12),card.right-S(140),cy+S(12)},replay_font(13,FW_SEMIBOLD),C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  for(size_t i=0;i<replay.players.size();++i) {
    if(i) { const bool vs=replay.players.size()==2; draw_text(dc,vs?L"vs":L"\x00B7",RECT{x+S(4),cy-S(10),x+S(30),cy+S(10)},replay_font(12,FW_SEMIBOLD),C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE); x+=S(34); }
    x=draw_player_chip(dc,replay.players[i],replay.winner==(int)i,x,cy,C_CARD);
  }
  // Date, length, stage
  std::wstring detail=widen(launcher::replay::display_date(replay));
  if(auto d=replay_duration(replay);!d.empty()) detail+=L"     \x23F1 "+d;
  if(!replay.stage.empty()) detail+=L"     \x25B2 "+widen(replay.stage);
  draw_text(dc,detail,RECT{card.left+S(16),card.top+S(48),card.right-S(200),card.bottom-S(8)},replay_font(12,FW_NORMAL),RGB(0xC4,0xCD,0xDD),DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  draw_text(dc,replay.path.filename().wstring(),RECT{card.right-S(260),card.top+S(48),card.right-S(16),card.bottom-S(8)},replay_font(11,FW_NORMAL),C_DIM,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  // Stats and Watch
  RECT stats,watch; card_zones(card,&stats,&watch);
  const bool hs=hot&&g_replay_hot_zone==1, hw=hot&&g_replay_hot_zone==2;
  round_rect(dc,stats,13,hs?C_BTN:RGB(0x1B,0x24,0x38),hs?C_BTN:RGB(0x1B,0x24,0x38),hs?C_ACC_HI:C_BTN_BORDER);
  draw_text(dc,L"Stats",stats,replay_font(11,FW_SEMIBOLD),C_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  round_rect(dc,watch,15,hw?C_OK:RGB(0x1B,0x24,0x38),hw?C_OK:RGB(0x1B,0x24,0x38),C_OK);
  RECT tri=watch; tri.left+=S(2);
  draw_text(dc,L"\x25B6",tri,replay_symbols(11),hw?RGB(0x0E,0x15,0x22):C_OK,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
LRESULT CALLBACK replay_list_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
  auto zone_at=[&](POINT p,int* index)->int {
    const LRESULT hit=SendMessageW(h,LB_ITEMFROMPOINT,0,MAKELPARAM(p.x,p.y));
    *index=HIWORD(hit)?-1:LOWORD(hit);
    if(*index<0||*index>=(int)g_replay_files.size()) { *index=-1; return 0; }
    RECT r{}; SendMessageW(h,LB_GETITEMRECT,*index,(LPARAM)&r);
    RECT card=r; InflateRect(&card,-S(4),-S(4)); card.right-=S(4);
    RECT stats,watch; card_zones(card,&stats,&watch);
    return PtInRect(&watch,p)?2:(PtInRect(&stats,p)?1:0);
  };
  switch(m) {
    case WM_MOUSEMOVE: {
      int index; const int zone=zone_at(POINT{GET_X_LPARAM(l),GET_Y_LPARAM(l)},&index);
      if(index!=g_replay_hot||zone!=g_replay_hot_zone) {
        RECT r{};
        if(g_replay_hot>=0&&SendMessageW(h,LB_GETITEMRECT,g_replay_hot,(LPARAM)&r)!=LB_ERR) InvalidateRect(h,&r,FALSE);
        g_replay_hot=index; g_replay_hot_zone=zone;
        if(index>=0&&SendMessageW(h,LB_GETITEMRECT,index,(LPARAM)&r)!=LB_ERR) InvalidateRect(h,&r,FALSE);
      }
      TRACKMOUSEEVENT t{sizeof t,TME_LEAVE,h,0}; TrackMouseEvent(&t);
      break;
    }
    case WM_MOUSELEAVE:
      if(g_replay_hot>=0) { RECT r{}; if(SendMessageW(h,LB_GETITEMRECT,g_replay_hot,(LPARAM)&r)!=LB_ERR) InvalidateRect(h,&r,FALSE); }
      g_replay_hot=-1; g_replay_hot_zone=0;
      break;
    case WM_SETCURSOR: { POINT p; GetCursorPos(&p); ScreenToClient(h,&p); int index; zone_at(p,&index); if(index>=0) { SetCursor(LoadCursorW(nullptr,IDC_HAND)); return TRUE; } break; }
    case WM_LBUTTONUP: {
      const LRESULT r=DefSubclassProc(h,m,w,l);
      int index; const int zone=zone_at(POINT{GET_X_LPARAM(l),GET_Y_LPARAM(l)},&index);
      if(index>=0) { SendMessageW(h,LB_SETCURSEL,index,0); if(zone==2) watch_replay(); else replay_open_stats(index); }
      return r;
    }
    case WM_KEYDOWN:
      if(w==VK_RETURN) { replay_open_stats((int)SendMessageW(h,LB_GETCURSEL,0,0)); return 0; }
      break;
  }
  return DefSubclassProc(h,m,w,l);
}

// ---------------------------------------------------------------------------- the stats page
const COLORREF C_ROW_A=RGB(0x16,0x1E,0x30), C_ROW_B=RGB(0x1A,0x23,0x37), C_SECTION=RGB(0x23,0x2D,0x47), C_HEAD=RGB(0x28,0x33,0x50);
std::wstring pct(double v) { wchar_t b[24]; swprintf_s(b,L"%d%%",(int)std::floor(v+0.5)); return b; }   // half up, as Slippi shows
std::wstring pct_trunc(double v) { wchar_t b[24]; swprintf_s(b,L"%d%%",(int)v); return b; }
std::wstring num1(double v) { wchar_t b[24]; swprintf_s(b,L"%.1f",v); return b; }
std::wstring count_of(int part,int whole) { return pct(whole?100.0*part/whole:0.0)+L" ("+std::to_wstring(part)+L" / "+std::to_wstring(whole)+L")"; }
std::wstring count_share(int mine,int theirs) { return std::to_wstring(mine)+L" ("+pct(mine+theirs?100.0*mine/(mine+theirs):0.0)+L")"; }
COLORREF damage_color(float d) { return d>=70?RGB(0xF0,0x6A,0x55):(d>=30?C_GOLD:C_OK); }

struct StatRow { std::wstring label, value[2]; int better=-1; bool section=false; };
// better: 1 higher wins, -1 lower wins, 0 no highlight
StatRow stat(const wchar_t* label,std::wstring a,std::wstring b,double va,double vb,int dir) {
  StatRow r{label,{std::move(a),std::move(b)}};
  if(dir&&va!=vb) r.better=(dir>0)==(va>vb)?0:1;
  return r;
}
std::vector<StatRow> overall_rows(const launcher::replay::Info& r) {
  const auto& a=r.players[0]; const auto& b=r.players[1];
  const double minutes=std::max(1.0,double(r.last_frame+39))/3600.0;   // Slippi's per-minute base
  auto per_kill=[](const launcher::replay::Player& p){ return p.kills?double(p.openings)/p.kills:0.0; };
  auto per_open=[](const launcher::replay::Player& p){ return p.openings?p.damage_done/p.openings:0.0; };
  auto conv=[](const launcher::replay::Player& p){ return p.openings?double(p.successful_conversions)/p.openings:0.0; };
  auto lc=[](const launcher::replay::Player& p){ int t=p.l_success+p.l_fail; return t?double(p.l_success)/t:0.0; };
  auto trade=[](const launcher::replay::Player& p){ return p.trades?double(p.beneficial_trades)/p.trades:0.0; };
  auto slash=[](std::initializer_list<int> v){ std::wstring s; for(int n:v) s+=(s.empty()?L"":L" / ")+std::to_wstring(n); return s; };
  std::vector<StatRow> rows;
  rows.push_back({L"Offense",{},-1,true});
  rows.push_back(stat(L"Kills",std::to_wstring(a.kills),std::to_wstring(b.kills),a.kills,b.kills,1));
  rows.push_back(stat(L"Damage Done",num1(a.damage_done),num1(b.damage_done),a.damage_done,b.damage_done,1));
  auto rate=[&](const launcher::replay::Player& p){ wchar_t t[48]; swprintf_s(t,L"%.1f%% (%d / %d)",100*conv(p),p.successful_conversions,p.openings); return std::wstring(t); };
  rows.push_back(stat(L"Opening Conversion Rate",rate(a),rate(b),conv(a),conv(b),1));
  rows.push_back(stat(L"Openings / Kill",a.kills?num1(per_kill(a)):L"N/A",b.kills?num1(per_kill(b)):L"N/A",a.kills?per_kill(a):1e9,b.kills?per_kill(b):1e9,-1));
  rows.push_back(stat(L"Damage / Opening",num1(per_open(a)),num1(per_open(b)),per_open(a),per_open(b),1));
  rows.push_back({L"Defense",{},-1,true});
  rows.push_back(stat(L"Actions (Roll / Air Dodge / Spot Dodge)",slash({a.rolls,a.air_dodges,a.spot_dodges}),slash({b.rolls,b.air_dodges,b.spot_dodges}),0,0,0));
  rows.push_back({L"Neutral",{},-1,true});
  rows.push_back(stat(L"Neutral Wins",count_share(a.neutral_wins,b.neutral_wins),count_share(b.neutral_wins,a.neutral_wins),a.neutral_wins,b.neutral_wins,1));
  rows.push_back(stat(L"Counter Hits",count_share(a.counter_hits,b.counter_hits),count_share(b.counter_hits,a.counter_hits),a.counter_hits,b.counter_hits,1));
  rows.push_back(stat(L"Beneficial Trades",std::to_wstring(a.beneficial_trades)+L" ("+pct(100*trade(a))+L")",std::to_wstring(b.beneficial_trades)+L" ("+pct(100*trade(b))+L")",a.beneficial_trades,b.beneficial_trades,1));
  rows.push_back(stat(L"Actions (Wavedash / Waveland / Dash Dance / Ledgegrab)",slash({a.wavedashes,a.wavelands,a.dash_dances,a.ledge_grabs}),slash({b.wavedashes,b.wavelands,b.dash_dances,b.ledge_grabs}),0,0,0));
  rows.push_back({L"General",{},-1,true});
  rows.push_back(stat(L"Inputs / Minute",num1(a.inputs/minutes),num1(b.inputs/minutes),a.inputs,b.inputs,1));
  rows.push_back(stat(L"Digital Inputs / Minute",num1(a.digital_inputs/minutes),num1(b.digital_inputs/minutes),a.digital_inputs,b.digital_inputs,1));
  rows.push_back(stat(L"L-Cancel Success Rate",count_of(a.l_success,a.l_success+a.l_fail),count_of(b.l_success,b.l_success+b.l_fail),lc(a),lc(b),1));
  return rows;
}
// A table's header strip: the player's icon and name.
void player_header(HDC dc,RECT r,const launcher::replay::Player& p) {
  fill(dc,r,C_HEAD);
  char_icon(dc,p.character,r.left+S(10),(r.top+r.bottom)/2-S(11),S(22));
  draw_text(dc,widen(p.name),RECT{r.left+S(40),r.top,r.right-S(8),r.bottom},replay_font(14,FW_SEMIBOLD),C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
void cell(HDC dc,const std::wstring& s,RECT r,COLORREF c,HFONT f=nullptr,UINT align=DT_LEFT) {
  r.left+=S(10); r.right-=S(6);
  draw_text(dc,s,r,f?f:replay_font(12,FW_NORMAL),c,align|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
// Draws the page with its top at y (the scroll offset already applied) and returns its height.
int draw_stats(HDC dc,int width,int y) {
  const int top=y;
  if(g_replay_view<0||g_replay_view>=(int)g_replay_info.size()) return 0;
  const auto& r=g_replay_info[g_replay_view];
  const int X0=S(4), W=width-S(8);
  if(r.players.size()!=2||!r.valid) {
    draw_text(dc,r.valid?L"Detailed stats are shown for one-on-one matches.":L"This file has no readable match data.",RECT{X0,y+S(40),X0+W,y+S(80)},replay_font(14,FW_NORMAL),C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    return S(120);
  }
  if(!r.stats_loaded) { draw_text(dc,L"Reading the match...",RECT{X0,y+S(40),X0+W,y+S(80)},replay_font(14,FW_NORMAL),C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE); return S(120); }
  // Overall
  draw_text(dc,L"Overall",RECT{X0,y+S(8),X0+W,y+S(40)},replay_font(20,FW_SEMIBOLD),C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE); y+=S(48);
  const int c0=W*50/100, cw=(W-c0)/2;
  fill(dc,RECT{X0,y,X0+W,y+S(40)},C_HEAD);
  for(int p=0;p<2;++p) { const int x=X0+c0+p*cw; char_icon(dc,r.players[p].character,x+S(10),y+S(9),S(22)); draw_text(dc,widen(r.players[p].name),RECT{x+S(38),y,x+cw-S(6),y+S(40)},replay_font(14,FW_SEMIBOLD),C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS); }
  y+=S(40);
  int n=0;
  for(const auto& row:overall_rows(r)) {
    const int h=row.section?S(28):S(34);
    RECT line{X0,y,X0+W,y+h};
    if(row.section) { fill(dc,line,C_SECTION); cell(dc,row.label,line,C_TEXT,replay_font(12,FW_BOLD)); }
    else {
      fill(dc,line,(n++&1)?C_ROW_B:C_ROW_A);
      cell(dc,row.label,RECT{X0,y,X0+c0,y+h},C_TEXT,replay_font(13,FW_NORMAL));
      for(int p=0;p<2;++p) {
        const int x=X0+c0+p*cw; fill(dc,RECT{x,y,x+1,y+h},C_SEP);
        const bool best=row.better==p;
        cell(dc,row.value[p],RECT{x,y,x+cw,y+h},best?C_GOLD:C_TEXT,replay_font(13,best?FW_BOLD:FW_NORMAL));
      }
    }
    y+=h;
  }
  y+=S(26);
  const int half=(W-S(14))/2;
  // Kills: each table lists the opponent's stocks and how they ended.
  draw_text(dc,L"Kills",RECT{X0,y,X0+W,y+S(32)},replay_font(20,FW_SEMIBOLD),C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE); y+=S(40);
  int end_y=y;
  for(int p=0;p<2;++p) {
    const int x=X0+p*(half+S(14)); int ty=y;
    player_header(dc,RECT{x,ty,x+half,ty+S(38)},r.players[p]); ty+=S(38);
    const int col[5]={0,half*13/100,half*26/100,half*56/100,half*80/100};
    const wchar_t* names[5]={L"Start",L"End",L"Kill Move",L"Direction",L"Percent"};
    fill(dc,RECT{x,ty,x+half,ty+S(28)},C_SECTION);
    for(int c=0;c<5;++c) cell(dc,names[c],RECT{x+col[c],ty,x+(c<4?col[c+1]:half),ty+S(28)},C_TEXT,replay_font(12,FW_SEMIBOLD));
    ty+=S(28);
    const auto& stocks=r.players[1-p].stock_list;
    for(size_t i=0;i<stocks.size();++i) {
      const auto& s=stocks[i]; RECT line{x,ty,x+half,ty+S(32)}; fill(dc,line,(i&1)?C_ROW_B:C_ROW_A);
      auto at=[&](int c){ return RECT{x+col[c],ty,x+(c<4?col[c+1]:half),ty+S(32)}; };
      cell(dc,i==0?L"\x2013":replay_clock(s.start_frame),at(0),C_TEXT);
      cell(dc,s.end_frame<0?L"\x2013":replay_clock(s.end_frame),at(1),C_TEXT);
      cell(dc,s.end_frame<0?L"\x2013":widen(launcher::replay::move_name(s.kill_move)),at(2),C_TEXT);
      static const wchar_t* arrows[4]={L"\x2193",L"\x2190",L"\x2192",L"\x2191"};
      if(s.end_frame>=0&&s.direction>=0&&s.direction<4) cell(dc,arrows[s.direction],at(3),C_OK,replay_font(15,FW_BOLD));
      else cell(dc,L"\x2013",at(3),C_TEXT);
      cell(dc,pct_trunc(s.percent),at(4),C_TEXT);
      ty+=S(32);
    }
    end_y=std::max(end_y,ty);
  }
  y=end_y+S(26);
  // Openings & Conversions: each player's punishes, with the opponent's stocks after every kill.
  draw_text(dc,L"Openings && Conversions",RECT{X0,y,X0+W,y+S(32)},replay_font(20,FW_SEMIBOLD),C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE); y+=S(40);
  end_y=y;
  for(int p=0;p<2;++p) {
    /* one table per player across the whole width, the second under the first */
    const int x=X0; const int half=W; int ty=end_y+(p?S(18):0);
    player_header(dc,RECT{x,ty,x+half,ty+S(38)},r.players[p]); ty+=S(38);
    const int col[6]={0,half*11/100,half*22/100,half*36/100,half*62/100,half*76/100};
    const wchar_t* names[6]={L"Start",L"End",L"Damage",L"Range",L"Moves",L"Opening"};
    fill(dc,RECT{x,ty,x+half,ty+S(28)},C_SECTION);
    for(int c=0;c<6;++c) cell(dc,names[c],RECT{x+col[c],ty,x+(c<5?col[c+1]:half),ty+S(28)},C_TEXT,replay_font(12,FW_SEMIBOLD));
    ty+=S(28);
    const auto& opp=r.players[1-p]; int lost=0, row=0;
    for(const auto& c:r.players[p].conversions) {
      auto at=[&](int k){ return RECT{x+col[k],ty,x+(k<5?col[k+1]:half),ty+S(32)}; };
      fill(dc,RECT{x,ty,x+half,ty+S(32)},(row++&1)?C_ROW_B:C_ROW_A);
      const float dmg=float(int(c.end_percent-c.start_percent));
      cell(dc,replay_clock(c.start_frame),at(0),C_TEXT);
      cell(dc,c.end_frame<0?L" 13":replay_clock(c.end_frame),at(1),C_TEXT);
      cell(dc,pct_trunc(dmg),at(2),damage_color(dmg),replay_font(12,dmg>=30?FW_BOLD:FW_NORMAL));
      cell(dc,L"("+pct_trunc(c.start_percent)+L" - "+pct_trunc(c.end_percent)+L")",at(3),C_DIM);
      cell(dc,std::to_wstring(c.moves),at(4),C_TEXT);
      const wchar_t* open=c.opening==launcher::replay::Opening::CounterHit?L"Counter Hit":(c.opening==launcher::replay::Opening::Trade?L"Trade":L"Neutral");
      cell(dc,open,at(5),C_TEXT);
      ty+=S(32);
      if(c.killed) {
        ++lost; fill(dc,RECT{x,ty,x+half,ty+S(30)},C_SECTION);
        for(int s=0;s<std::max(opp.start_stocks,lost);++s) {
          RECT ic{x+S(10)+s*S(22),ty+S(6),x+S(28)+s*S(22),ty+S(24)};
          char_icon(dc,opp.character,ic.left,ic.top,S(18));
          if(s>=opp.start_stocks-lost) shade(dc,ic,C_SECTION,170);
        }
        ty+=S(30);
      }
    }
    if(r.players[p].conversions.empty()) { fill(dc,RECT{x,ty,x+half,ty+S(32)},C_ROW_A); cell(dc,L"No openings",RECT{x,ty,x+half,ty+S(32)},C_DIM); ty+=S(32); }
    end_y=std::max(end_y,ty);
  }
  return end_y+S(24)-top;
}
void stats_changed() {
  if(!g_replay_stats) return;
  RECT cr; GetClientRect(g_replay_stats,&cr);
  HDC screen=GetDC(g_replay_stats); HDC md=CreateCompatibleDC(screen);
  HBITMAP b=CreateCompatibleBitmap(screen,1,1); HGDIOBJ old=SelectObject(md,b);
  g_stats_height=draw_stats(md,cr.right,0);
  SelectObject(md,old); DeleteObject(b); DeleteDC(md); ReleaseDC(g_replay_stats,screen);
  g_stats_scroll=std::clamp(g_stats_scroll,0,std::max(0,g_stats_height-int(cr.bottom)));
  SCROLLINFO si{sizeof si,SIF_RANGE|SIF_PAGE|SIF_POS,0,std::max(0,g_stats_height-1),(UINT)cr.bottom,g_stats_scroll};
  SetScrollInfo(g_replay_stats,SB_VERT,&si,TRUE);
  InvalidateRect(g_replay_stats,nullptr,FALSE);
  if(g_main) InvalidateRect(g_main,nullptr,FALSE);
}
void stats_scroll_to(int y) {
  RECT cr; GetClientRect(g_replay_stats,&cr);
  y=std::clamp(y,0,std::max(0,g_stats_height-int(cr.bottom)));
  if(y==g_stats_scroll) return;
  g_stats_scroll=y; SetScrollPos(g_replay_stats,SB_VERT,y,TRUE); InvalidateRect(g_replay_stats,nullptr,FALSE);
}
LRESULT CALLBACK stats_proc(HWND h,UINT m,WPARAM w,LPARAM l) {
  switch(m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); RECT cr; GetClientRect(h,&cr);
      HDC md=CreateCompatibleDC(dc); HBITMAP b=CreateCompatibleBitmap(dc,cr.right,cr.bottom); HGDIOBJ old=SelectObject(md,b);
      fill(md,cr,C_CONTENT_BOT);
      draw_stats(md,cr.right,-g_stats_scroll);
      BitBlt(dc,0,0,cr.right,cr.bottom,md,0,0,SRCCOPY);
      SelectObject(md,old); DeleteObject(b); DeleteDC(md); EndPaint(h,&ps);
      return 0;
    }
    case WM_MOUSEWHEEL: stats_scroll_to(g_stats_scroll-GET_WHEEL_DELTA_WPARAM(w)*S(90)/WHEEL_DELTA); return 0;
    case WM_VSCROLL: {
      SCROLLINFO si{sizeof si,SIF_ALL}; GetScrollInfo(h,SB_VERT,&si); int y=si.nPos;
      switch(LOWORD(w)) {
        case SB_LINEUP:y-=S(40);break; case SB_LINEDOWN:y+=S(40);break;
        case SB_PAGEUP:y-=(int)si.nPage;break; case SB_PAGEDOWN:y+=(int)si.nPage;break;
        case SB_THUMBTRACK:case SB_THUMBPOSITION:y=si.nTrackPos;break;
        case SB_TOP:y=0;break; case SB_BOTTOM:y=g_stats_height;break;
      }
      stats_scroll_to(y); return 0;
    }
    case WM_KEYDOWN:
      if(w==VK_ESCAPE||w==VK_BACK) { replay_back(); return 0; }
      if(w==VK_LEFT) { replay_step(-1); return 0; }
      if(w==VK_RIGHT) { replay_step(1); return 0; }
      if(w==VK_DOWN||w==VK_UP) { stats_scroll_to(g_stats_scroll+(w==VK_DOWN?S(40):-S(40))); return 0; }
      break;
    case WM_LBUTTONDOWN: SetFocus(h); return 0;
    case WM_SIZE: stats_changed(); return 0;
  }
  return DefWindowProcW(h,m,w,l);
}
void create_replay_controls() {
  g_replays[0]=make(L"LISTBOX",L"",LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL,212,96,748,514,ID_REPLAY_LIST);
  SendMessageW(g_replays[0],LB_SETITEMHEIGHT,0,S(84)); SetWindowTheme(g_replays[0],L"DarkMode_Explorer",nullptr);
  SetWindowSubclass(g_replays[0],replay_list_proc,1,0);
  g_replays[1]=make(L"BUTTON",L"Browse...",BS_OWNERDRAW,212,622,120,34,ID_REPLAY_BROWSE);
  g_replays[2]=make(L"BUTTON",L"Watch Replay",BS_OWNERDRAW,808,622,150,34,ID_REPLAY_WATCH);
  g_replays[3]=make(L"BUTTON",L"Refresh",BS_OWNERDRAW,340,622,100,34,ID_REPLAY_REFRESH);
  g_replays[4]=make(L"BUTTON",L"\x2190",BS_OWNERDRAW,212,26,36,34,ID_REPLAY_BACK);
  g_replays[5]=make(L"BUTTON",L"\x2039",BS_OWNERDRAW,800,68,30,26,ID_REPLAY_PREV);
  g_replays[6]=make(L"BUTTON",L"\x203A",BS_OWNERDRAW,928,68,30,26,ID_REPLAY_NEXT);
  g_replays[7]=make(L"BUTTON",L"",BS_OWNERDRAW,212,668,520,24,ID_REPLAY_ASPLAYED);   // drawn by draw_button, as the other tick box
  WNDCLASSEXW wc{sizeof wc}; wc.lpfnWndProc=stats_proc; wc.hInstance=GetModuleHandleW(nullptr);
  wc.hCursor=LoadCursorW(nullptr,IDC_ARROW); wc.lpszClassName=L"MeleeUnlockedReplayStats";
  RegisterClassExW(&wc);
  g_replay_stats=CreateWindowExW(0,wc.lpszClassName,L"",WS_CHILD|WS_VSCROLL,S(212),S(112),S(756),S(REPLAY_H-112-38),g_main,nullptr,wc.hInstance,nullptr);
  SetWindowTheme(g_replay_stats,L"DarkMode_Explorer",nullptr);
  replay_layout();
}

// ---------------------------------------------------------------------------- page chrome
void paint_replays(HDC dc) {
  if(g_replay_view>=0&&g_replay_view<(int)g_replay_info.size()) {
    const auto& r=g_replay_info[g_replay_view];
    int x=S(262); const int cy=S(42);
    for(size_t i=0;i<r.players.size()&&i<4;++i) {
      const auto& p=r.players[i];
      if(i) { draw_text(dc,r.players.size()==2?L"vs":L"\x00B7",RECT{x,cy-S(12),x+S(30),cy+S(12)},replay_font(14,FW_SEMIBOLD),C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE); x+=S(34); }
      char_icon(dc,p.character,x,cy-S(16),S(32)); x+=S(38);
      const auto name=widen(p.name); HFONT nf=replay_font(15,FW_SEMIBOLD);
      const int nw=std::min(text_width(dc,name,nf),S(150));
      draw_text(dc,name,RECT{x,cy-S(18),x+nw,cy+S(2)},nf,C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      RECT tag{x+nw+S(6),cy-S(15),x+nw+S(32),cy-S(1)};
      round_rect(dc,tag,7,port_color(p.port),port_color(p.port),NO_FILL);
      draw_text(dc,L"P"+std::to_wstring(p.port),tag,replay_font(10,FW_BOLD),C_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
      if(r.winner==(int)i) draw_text(dc,L"\x265B",RECT{tag.right+S(2),tag.top-S(2),tag.right+S(20),tag.bottom},replay_symbols(13),C_GOLD,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
      draw_text(dc,widen(p.code),RECT{x,cy+S(2),x+nw+S(40),cy+S(18)},replay_font(11,FW_NORMAL),C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
      x+=nw+S(46);
    }
    std::wstring line=widen(launcher::replay::display_date(r));
    if(auto d=replay_duration(r);!d.empty()) line+=L"      \x23F1 "+d;
    if(!r.stage.empty()) line+=L"      \x25B2 "+widen(r.stage);
    if(!r.played_on.empty()) line+=L"      "+widen(r.played_on);
    draw_text(dc,line,LR(214,70,580,24),replay_font(12,FW_NORMAL),RGB(0xC4,0xCD,0xDD),DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    draw_text(dc,std::to_wstring(g_replay_view+1)+L" / "+std::to_wstring(g_replay_files.size()),LR(830,68,98,26),replay_font(12,FW_SEMIBOLD),C_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    fill(dc,LR(212,104,760,2),C_ACC_LO);
    return;
  }
  draw_text(dc,L"Replay Viewer",LR(214,24,420,32),g_font_big,C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  draw_text(dc,L"Relive your matches and see the details that matter.",LR(214,58,560,22),g_font,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  if(g_replay_files.empty()) {
    round_rect(dc,LR(212,96,748,514),12,C_FIELD,C_FIELD,C_FIELD_BORDER);
    const bool loading=g_replay_status.rfind("Loading",0)==0;
    draw_text(dc,loading?L"Loading replays...":L"No replays yet",LR(212,300,748,34),g_font_big,C_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(!loading) draw_text(dc,L"Play a match or browse for a .slp file.",LR(212,338,748,28),g_font,C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  }
  draw_text(dc,widen(g_replay_status),LR(460,622,330,34),g_font_small,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
