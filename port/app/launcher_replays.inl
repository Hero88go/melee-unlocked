// Replay library: every .slp as a card (scanned off the UI thread, so the page never freezes), and a
// match stats page in the style players know from the Slippi Launcher.
#include <map>
#include <memory>
#include <tuple>
enum { ID_REPLAY_LIST=800, ID_REPLAY_BROWSE, ID_REPLAY_WATCH, ID_REPLAY_REFRESH, ID_REPLAY_BACK, ID_REPLAY_PREV, ID_REPLAY_NEXT, ID_REPLAY_ASPLAYED,
       ID_REPLAY_SEARCH, ID_REPLAY_SORT, ID_REPLAY_HIDESHORT, ID_REPLAY_QUEUE, ID_REPLAY_QUEUE_CLEAR };
const UINT WM_APP_REPLAYS_LISTED = WM_APP + 20;   // lParam: heap ReplayScan*
const UINT WM_APP_REPLAY_LOADED = WM_APP + 21;    // lParam: heap ReplayLoaded*
HWND g_replays[13]{};            // list, browse, watch, refresh, back, prev, next, "as it was played",
                                 // search, sort, "hide short games", watch queue, clear queue
HWND g_replay_stats=nullptr;     // the scrolling stats page
// Everything the scan found, newest first. A replay's place in these two is its index everywhere on
// this page (the stats page, the loader's late results, playback). Only the list box counts in rows.
std::vector<std::filesystem::path> g_replay_files;
std::vector<launcher::replay::Info> g_replay_info;
std::vector<launcher::trace::Overview> g_replay_traces;
RECT g_trace_plot{};
int g_trace_hover=-1;
// The list's rows: indexes into the two above, after the search, "Hide short games" and the sort.
std::vector<int> g_replay_visible;
// The watch queue in the order the player picked. Paths, so it outlives a refresh, a search or a sort.
std::vector<std::filesystem::path> g_replay_queue;
// A queue being played: the copy taken when it started, the next one to start, and what was skipped.
std::vector<std::filesystem::path> g_replay_playing;
size_t g_replay_play_at=0;
int g_replay_skipped=0;
std::string g_replay_skip_reason;
std::filesystem::path g_replay_browsed;   // opened with Browse: listed whatever the filters say, until they change
std::string g_replay_status;
bool g_replay_status_count=false;         // the status line shows the replay count, so a new count may replace it
bool g_replay_active=false;
std::atomic<unsigned> g_replay_gen{0};
int g_replay_view=-1;            // -1: the list; otherwise the replay shown on the stats page (an index, not a row)
int g_replay_hot=-1, g_replay_hot_zone=0;   // hovered row, and 1 over its Stats pill, 2 over Watch, 3 over the queue badge
int g_stats_scroll=0, g_stats_height=0;
void report_launch_error(DWORD,const std::string&,const std::string&);
HWND make(const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id,HFONT font=nullptr,DWORD ex=0);
namespace crash_report { bool newer_than_launch(const std::string&); }   // launcher_crash.inl, below

struct ReplayScan { unsigned gen=0; std::vector<std::filesystem::path> files; std::vector<launcher::replay::Info> info; int selected=-1; };
struct ReplayLoaded { unsigned gen=0; size_t index=0; launcher::replay::Info info; launcher::trace::Overview trace; };

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

void replay_status(const std::string& message) { g_replay_status=message; g_replay_status_count=false; InvalidateRect(g_main,nullptr,FALSE); }

// Rows and indexes. The list box counts in rows (what is listed, in the order shown). Everything
// else on this page takes an index into g_replay_files / g_replay_info, so a search or another sort
// never changes which replay a number means.
int replay_at_row(int row) { return row>=0&&row<(int)g_replay_visible.size()?g_replay_visible[row]:-1; }
int replay_row_of(int index) {
  const auto it=std::find(g_replay_visible.begin(),g_replay_visible.end(),index);
  return index<0||it==g_replay_visible.end()?-1:int(it-g_replay_visible.begin());
}
int replay_selected() { return replay_at_row(g_replays[0]?(int)SendMessageW(g_replays[0],LB_GETCURSEL,0,0):-1); }

// "Show it as it was played": an online match leaves a session trace beside its replay, the same
// name with the extension .trace (what the player actually got: waits, rollbacks, long frames). The
// box (g_replay_as_played, kept in launcher.ini) can be ticked only for a replay that has one.
int replay_current() { return g_replay_view>=0?g_replay_view:replay_selected(); }
bool replay_trace_exists(std::filesystem::path trace) {
  trace.replace_extension(L".trace");
  std::error_code ec; return std::filesystem::is_regular_file(trace,ec);
}
bool replay_has_trace(int index) { return index>=0&&index<(int)g_replay_files.size()&&replay_trace_exists(g_replay_files[index]); }
void replay_trace_changed() {
  if(!g_replays[7]) return;
  EnableWindow(g_replays[7],replay_has_trace(replay_current()));
  InvalidateRect(g_replays[7],nullptr,FALSE);
}
// The buttons that follow the selection, the queue and a running game. The queue's two show only
// on the list, and only while something is queued.
void replay_buttons() {
  const bool idle=!g_playing&&!g_building, queued=g_tab==3&&g_replay_view<0&&!g_replay_queue.empty();
  if(g_replays[2]) EnableWindow(g_replays[2],idle&&replay_current()>=0);
  for(int i:{11,12}) if(g_replays[i]) ShowWindow(g_replays[i],queued?SW_SHOW:SW_HIDE);
  if(g_replays[11]) EnableWindow(g_replays[11],idle&&queued);
}

// ---------------------------------------------------------------------------- search, sort, filter
// g_replay_sort (launcher.ini replaysort=) is an index into these.
const char* const kReplaySorts[4]={"Most recent","Least recent","Longest game","Shortest game"};
std::wstring replay_lower(std::wstring s) { if(!s.empty()) CharLowerBuffW(s.data(),(DWORD)s.size()); return s; }
// The search box's words in lower case. A replay is listed when every one of them is found in it.
std::vector<std::wstring> replay_search_words() {
  std::vector<std::wstring> words;
  if(!g_replays[8]) return words;
  wchar_t text[256]{}; GetWindowTextW(g_replays[8],text,256);
  std::wstring word;
  for(const wchar_t* c=text;;++c) {
    if(*c&&!iswspace(*c)) { word+=*c; continue; }
    if(!word.empty()) { words.push_back(replay_lower(word)); word.clear(); }
    if(!*c) break;
  }
  return words;
}
// A game under 30 seconds. A length not read yet is not short (a recording the game never closed
// has none until its frames are read): nothing is hidden on a guess.
bool replay_short(const launcher::replay::Info& r) { return r.last_frame>-123&&r.last_frame<30*60; }
bool replay_listed(int index,const std::vector<std::wstring>& words) {
  const auto& r=g_replay_info[index];
  if(!g_replay_browsed.empty()&&g_replay_files[index]==g_replay_browsed) return true;
  if(g_replay_hide_short&&replay_short(r)) return false;
  if(words.empty()) return true;
  // What a search looks through: each player's connect code, name and character, the stage, the file name.
  std::wstring text;
  for(const auto& p:r.players) text+=widen(p.code)+L'\n'+widen(p.name)+L'\n'+widen(launcher::replay::character_name(p.character))+L'\n';
  text+=widen(r.stage)+L'\n'+g_replay_files[index].filename().wstring();
  text=replay_lower(std::move(text));
  for(const auto& w:words) if(text.find(w)==std::wstring::npos) return false;
  return true;
}
void replay_count_status() {
  const auto all=std::to_string(g_replay_files.size());
  // The launcher built without the Slippi layer never shows this page (no rail entry): there the
  // lines of this file that name those replays, look for them or start their viewer are left out.
#ifdef MELEE_NO_SLIPPI
  if(g_replay_files.empty()) replay_status("");
#else
  if(g_replay_files.empty()) replay_status("Choose a Slippi replay to get started.");
#endif
  else if(g_replay_visible.size()==g_replay_files.size()) replay_status(launcher::lang::fill(launcher::lang::tx("{count} replays"),launcher::lang::Args{{"count",all}}));
  else replay_status(launcher::lang::fill(launcher::lang::tx("{shown} of {count} replays"),launcher::lang::Args{{"shown",std::to_string(g_replay_visible.size())},{"count",all}}));
  g_replay_status_count=true;
}
// Fills the list from everything scanned: the search and "Hide short games", then the chosen order.
// `select` is the replay to leave selected (-1, or one that is not listed: the first row). "Watch
// Replay" plays the selected card, so a change the player made shows the list from its top with a
// card in sight selected. `keep_scroll` is for a rebuild nobody asked for (a match's length arrived
// late): the list stays where it is.
void replay_view_build(int select,bool keep_scroll=false) {
  HWND list=g_replays[0]; if(!list) return;
  const int top=(int)SendMessageW(list,LB_GETTOPINDEX,0,0);
  const auto words=replay_search_words();
  g_replay_visible.clear();
  for(int i=0;i<(int)g_replay_files.size();++i) if(replay_listed(i,words)) g_replay_visible.push_back(i);
  if(g_replay_sort==1) std::reverse(g_replay_visible.begin(),g_replay_visible.end());
  else if(g_replay_sort>=2) {
    // A length not known goes last either way. Equal lengths keep the newest first.
    const bool longest=g_replay_sort==2;
    std::stable_sort(g_replay_visible.begin(),g_replay_visible.end(),[longest](int a,int b){
      const int fa=g_replay_info[a].last_frame, fb=g_replay_info[b].last_frame;
      if((fa>-123)!=(fb>-123)) return fa>-123;
      return longest?fa>fb:fa<fb;
    });
  }
  SendMessageW(list,WM_SETREDRAW,FALSE,0);
  SendMessageW(list,LB_RESETCONTENT,0,0);
  for(int i:g_replay_visible) SendMessageW(list,LB_ADDSTRING,0,(LPARAM)g_replay_files[i].filename().c_str());
  int row=replay_row_of(select); if(row<0&&!g_replay_visible.empty()) row=0;
  if(row>=0) SendMessageW(list,LB_SETCURSEL,row,0);
  if(keep_scroll) SendMessageW(list,LB_SETTOPINDEX,top,0);
  SendMessageW(list,WM_SETREDRAW,TRUE,0); InvalidateRect(list,nullptr,TRUE);
  g_replay_hot=-1; g_replay_hot_zone=0;
  // With nothing to list the box is hidden: the page says why in its place (paint_replays).
  ShowWindow(list,g_tab==3&&g_replay_view<0&&!g_replay_visible.empty()?SW_SHOW:SW_HIDE);
  replay_buttons(); replay_trace_changed();
  if(g_main) InvalidateRect(g_main,nullptr,FALSE);
}
// The search text or "Hide short games" changed.
void replay_filter_changed() {
  g_replay_browsed.clear();
  replay_view_build(-1);
  if(!g_replay_files.empty()) replay_count_status();   // while the first scan runs, "Loading replays..." stays
}
// A replay's details were read again (the loader's full read, or the stats page opening before it).
// That can change what the list goes by, the length above all. The rows are rebuilt only then.
void replay_info_changed(int index,launcher::replay::Info info) {
  const auto words=replay_search_words();
  const bool was=replay_listed(index,words); const int length=g_replay_info[index].last_frame;
  g_replay_info[index]=std::move(info);
  if(replay_listed(index,words)!=was||(g_replay_sort>=2&&g_replay_info[index].last_frame!=length)) {
    replay_view_build(replay_selected(),true);
    if(g_replay_status_count) replay_count_status();
  } else if(const int row=replay_row_of(index);row>=0) {
    RECT r{}; if(SendMessageW(g_replays[0],LB_GETITEMRECT,row,(LPARAM)&r)!=LB_ERR) InvalidateRect(g_replays[0],&r,FALSE);
  }
}
void replay_sort_caption() {
  if(!g_replays[9]) return;
  const auto text=launcher::lang::fill(launcher::lang::tx("Sort: {order}"),launcher::lang::Args{{"order",launcher::lang::tx(kReplaySorts[std::clamp(g_replay_sort,0,3)])}});
  SetWindowTextW(g_replays[9],(widen(text)+L"  \x25BE").c_str());
}
// The sort button: a menu of the orders, as the Language button on the Play page.
void pick_replay_sort() {
  HMENU menu=CreatePopupMenu();
  for(int i=0;i<4;++i) AppendMenuW(menu,MF_STRING|(i==g_replay_sort?MF_CHECKED:0),1+i,widen(launcher::lang::tx(kReplaySorts[i])).c_str());
  RECT r{}; GetWindowRect(g_replays[9],&r);
  const int pick=TrackPopupMenu(menu,TPM_RETURNCMD|TPM_NONOTIFY,r.left,r.bottom,0,g_main,nullptr);
  DestroyMenu(menu);
  if(pick<1||pick>4||pick-1==g_replay_sort) return;
  g_replay_sort=pick-1; save_ini();
  replay_sort_caption(); replay_view_build(-1);
}
void replay_hide_short_toggle() {
  g_replay_hide_short=!g_replay_hide_short; save_ini();
  if(g_replays[10]) InvalidateRect(g_replays[10],nullptr,FALSE);
  replay_filter_changed();
}

// ---------------------------------------------------------------------------- the watch queue
// A replay's place in the queue: 1 plays first, 0 is not queued.
int replay_queue_place(const std::filesystem::path& file) {
  const auto it=std::find(g_replay_queue.begin(),g_replay_queue.end(),file);
  return it==g_replay_queue.end()?0:int(it-g_replay_queue.begin())+1;
}
void replay_queue_changed() {
  if(g_replays[11]) SetWindowTextW(g_replays[11],widen(launcher::lang::fill(launcher::lang::tx("Watch queue ({count})"),launcher::lang::Args{{"count",std::to_string(g_replay_queue.size())}})).c_str());
  replay_buttons();
  if(g_replays[0]) InvalidateRect(g_replays[0],nullptr,FALSE);   // taking one out renumbers every badge after it
}
void replay_queue_toggle(int index) {
  if(index<0||index>=(int)g_replay_files.size()) return;
  const auto it=std::find(g_replay_queue.begin(),g_replay_queue.end(),g_replay_files[index]);
  if(it==g_replay_queue.end()) g_replay_queue.push_back(g_replay_files[index]); else g_replay_queue.erase(it);
  replay_queue_changed();
}
void replay_queue_clear() { g_replay_queue.clear(); replay_queue_changed(); }

// ---------------------------------------------------------------------------- scanning
void refresh_replays(const std::filesystem::path& selected={}) {
  auto choice=selected;
  if(const int previous=replay_selected();choice.empty()&&previous>=0) choice=g_replay_files[previous];
  std::vector<std::filesystem::path> folders{std::filesystem::u8path(g_dir)/"Replays",std::filesystem::u8path(work_dir())/"Replays"};
#ifndef MELEE_NO_SLIPPI   // that build never reads the other program's replay folder
  PWSTR documents=nullptr;
  if(SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents,0,nullptr,&documents))) { folders.push_back(std::filesystem::path(documents)/"Slippi");CoTaskMemFree(documents); }
#endif
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
#ifndef MELEE_NO_SLIPPI   // nothing is a replay there
        std::error_code fe;
        if(extension==L".slp"&&it->is_regular_file(fe)) found.emplace_back(it->last_write_time(fe),it->path());
#endif
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
      auto* loaded=new ReplayLoaded{gen,i,launcher::replay::inspect(files[i],true),launcher::trace::load(files[i])};
      if(!PostMessageW(g_main,WM_APP_REPLAY_LOADED,0,(LPARAM)loaded)) { delete loaded; return; }
    }
  }).detach();
}
void stats_changed();
void replay_layout();
void replays_listed(ReplayScan* scan) {
  std::unique_ptr<ReplayScan> own(scan);
  if(scan->gen!=g_replay_gen) return;
  const bool stats=g_replay_view>=0;
  const auto shown=g_replay_view>=0&&g_replay_view<(int)g_replay_files.size()?g_replay_files[g_replay_view]:std::filesystem::path();
  g_replay_files=std::move(scan->files); g_replay_info=std::move(scan->info);
  g_replay_traces.clear(); g_replay_traces.resize(g_replay_files.size()); g_trace_hover=-1;
  // The stats page's replay first: the rows below are built for the page as it will be shown.
  if(g_replay_view>=0) {
    auto it=std::find(g_replay_files.begin(),g_replay_files.end(),shown);
    g_replay_view=it==g_replay_files.end()?-1:int(it-g_replay_files.begin());
  }
  replay_view_build(scan->selected);
  replay_count_status();
  if(stats&&g_replay_view<0) replay_layout();   // its replay is gone: back to the list
  stats_changed();
}
// The loader's result carries the index it was read for. The scan it belongs to is still the one
// listed (gen), and an index stays the same replay whatever the search and sort did to the rows.
void replay_loaded(ReplayLoaded* loaded) {
  std::unique_ptr<ReplayLoaded> own(loaded);
  if(loaded->gen!=g_replay_gen||loaded->index>=g_replay_info.size()) return;
  g_replay_traces[loaded->index]=std::move(loaded->trace);
  replay_info_changed((int)loaded->index,std::move(loaded->info));
  if((int)loaded->index==g_replay_view) stats_changed();
}

// ---------------------------------------------------------------------------- layout
void replay_layout() {
  const bool on=g_tab==3, stats=on&&g_replay_view>=0, list=on&&!stats;
  // Typing must not reach a search box that is no longer shown.
  if(!list&&g_replays[8]&&GetFocus()==g_replays[8]) SetFocus(g_main);
  if(g_replays[0]) ShowWindow(g_replays[0],list&&!g_replay_visible.empty()?SW_SHOW:SW_HIDE);
  for(int i:{1,3,8,9,10}) if(g_replays[i]) ShowWindow(g_replays[i],list?SW_SHOW:SW_HIDE);
  for(int i:{4,5,6}) if(g_replays[i]) ShowWindow(g_replays[i],stats?SW_SHOW:SW_HIDE);
  if(g_replays[7]) { ShowWindow(g_replays[7],on?SW_SHOW:SW_HIDE); replay_trace_changed(); }
  if(g_replay_stats) ShowWindow(g_replay_stats,stats?SW_SHOW:SW_HIDE);
  if(g_replays[2]) {
    RECT r=stats?LR(800,22,158,38):LR(808,622,150,34);
    SetWindowPos(g_replays[2],nullptr,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
    SetWindowTextW(g_replays[2],stats?L"\x25B6  Watch replay":L"Watch Replay");
    ShowWindow(g_replays[2],on?SW_SHOW:SW_HIDE);
  }
  replay_sort_caption();   // launcher.ini is read after the controls are made
  replay_buttons();
  if(g_main) InvalidateRect(g_main,nullptr,FALSE);
}
void replay_open_stats(int index) {
  if(index<0||index>=(int)g_replay_info.size()) return;
  if(!g_replay_info[index].stats_loaded) replay_info_changed(index,launcher::replay::inspect(g_replay_files[index],true));
  g_replay_view=index; g_stats_scroll=0; g_trace_hover=-1;
  if(const int row=replay_row_of(index);row>=0) SendMessageW(g_replays[0],LB_SETCURSEL,row,0);
  replay_layout(); stats_changed(); SetFocus(g_replay_stats);
}
void replay_back() { g_replay_view=-1; replay_layout(); SetFocus(g_replays[0]); }
// Previous and next on the stats page walk the list as it is shown: searched, filtered, sorted.
void replay_step(int delta) {
  if(g_replay_view<0||g_replay_visible.empty()) return;
  const int row=replay_row_of(g_replay_view);
  replay_open_stats(g_replay_visible[row<0?0:std::clamp(row+delta,0,(int)g_replay_visible.size()-1)]);
}
void replay_selection_changed() { InvalidateRect(g_replays[0],nullptr,FALSE); replay_trace_changed(); replay_buttons(); }

void browse_replay() {
  wchar_t file[32768]{}; auto folder=widen(g_dir+"\\Replays");
  OPENFILENAMEW dialog{sizeof dialog}; dialog.hwndOwner=g_main;
  const std::wstring dialog_title=launcher::lang::txw(L"Open replay");
#ifndef MELEE_NO_SLIPPI
  dialog.lpstrFilter=L"Slippi replays (*.slp)\0*.slp\0";
#endif
  dialog.lpstrTitle=dialog_title.c_str();
  dialog.lpstrFile=file; dialog.nMaxFile=32768; dialog.lpstrInitialDir=folder.c_str();
  dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
  // The file the player went looking for is listed and selected whatever the search box says.
  if(GetOpenFileNameW(&dialog)) { g_replay_browsed=std::filesystem::path(file); refresh_replays(g_replay_browsed); }
}
// Started: the game is running on the replay. Skipped: this replay cannot be played. Failed: no
// replay can be right now (a game is running, the disc or the playback files are missing). The
// status line says why for the last two.
enum class ReplayStart { Started, Skipped, Failed };
// Starts the game on one replay. `place` of `count` is its place in a running queue, 0 for a replay
// watched on its own.
ReplayStart replay_start(const std::filesystem::path& file,int place=0,int count=0) {
  if(g_playing||g_building) { replay_status("Close the running game before opening a replay."); return ReplayStart::Failed; }
  if(g_iso.empty()||!file_exists(g_iso)) { replay_status("Choose your Melee disc image on the Play page first."); return ReplayStart::Failed; }
  const std::string name=file.filename().u8string();
  // A queued replay can be gone by the time its turn comes.
  if(std::error_code ec;!std::filesystem::is_regular_file(file,ec)) {
    replay_status(launcher::lang::fill(launcher::lang::tx("{file} is no longer there."),launcher::lang::Args{{"file",name}}));
    return ReplayStart::Skipped;
  }
  // A replay of a match played on a mod (its stage is not in the standard game) would sit on a blank
  // screen: the standard game has no such stage. Say so instead of starting it.
  {
    const auto info=launcher::replay::inspect(file);
    if(info.valid&&info.stage_id>85) {
      replay_status(launcher::lang::fill(launcher::lang::tx("This replay was recorded with a mod (stage {id} is not in the standard game). It needs that mod to play."),launcher::lang::Args{{"id",std::to_string(info.stage_id)}}));
      return ReplayStart::Skipped;
    }
  }
  std::string exe;
  if(g_engine==ENGINE_SOURCE&&!source_exe_dir().empty()) exe=source_exe_dir()+"\\melee_source.exe";
  else {
    std::vector<std::string> candidates{active_dir()+"\\melee_port_playback.exe"};
#ifndef MELEE_NO_SLIPPI
    auto root=repo_root();
    if(g_active_version.empty()&&!root.empty()) for(auto dir:{"build-sourceport-slippi","build-sourceport","build-playback"}) candidates.push_back(root+"\\"+dir+"\\port\\Release\\melee_port_playback.exe");
#endif
    for(const auto& path:candidates) if(file_exists(path)) { exe=path; break; }
  }
  if(exe.empty()||!file_exists(exe)) { replay_status("Replay playback is not installed for this build."); return ReplayStart::Failed; }
  auto sys=active_dir()+"\\SysPlayback";
#ifndef MELEE_NO_SLIPPI
  if(!file_exists(sys+"\\codehandler.bin")) sys=repo_root()+"\\port\\slippi_sys_playback";
#endif
  if(!file_exists(sys+"\\codehandler.bin")) { replay_status("The playback system files are missing from this installation."); return ReplayStart::Failed; }
  const auto cwd=work_dir();
  std::wstring command=widen("\""+exe+"\""+game_args()+" --sys-dir \""+sys+"\" --replay \"")+file.wstring()+L"\"";
  // The viewer finds "<replay>.trace" beside the replay itself and shows the session's own timing.
  if(g_replay_as_played&&replay_trace_exists(file)) command+=L" --as-experienced";
  // Test runs only (MELEE_LAUNCHER_TEST), as for Play: extra game arguments (hidden, muted) and the
  // command line written to a file, so a queue can be checked without a window opening.
  if(g_launcher_test) {
    if(const char* extra=std::getenv("MELEE_LAUNCHER_TEST_GAME_ARGS")) command+=L" "+widen(extra);
    if(const char* log_path=std::getenv("MELEE_LAUNCHER_TEST_LAUNCH_LOG"))
      if(FILE* f=std::fopen(log_path,"ab")) { std::fprintf(f,"%s\n",file.filename().u8string().c_str()); std::fclose(f); }
  }
  PROCESS_INFORMATION process{}; DWORD error=launcher::start_process(widen(exe),command,widen(cwd),0,process);
  if(error) { report_launch_error(error,exe,cwd); replay_status("The replay could not be started."); return ReplayStart::Failed; }
  CloseHandle(process.hThread); crash_report::note_launch(); g_playing=true; g_replay_active=true;
  launcher::lobby::game_running(true); EnableWindow(g_play_btn,FALSE); replay_buttons();
  replay_status(place?launcher::lang::fill(launcher::lang::tx("Watching {index} of {count}: {file}"),launcher::lang::Args{{"index",std::to_string(place)},{"count",std::to_string(count)},{"file",name}})
                     :launcher::lang::fill(launcher::lang::tx("Watching {file}"),launcher::lang::Args{{"file",name}}));
  // A queue's next replay opens where the last one closed: the launcher is minimized already and stays so.
  if(!IsIconic(g_main)) ShowWindow(g_main,SW_MINIMIZE);
  std::thread([h=process.hProcess]{ WaitForSingleObject(h,INFINITE); DWORD code=0; GetExitCodeProcess(h,&code); CloseHandle(h); PostMessageW(g_main,WM_APP_GAME_DONE,code,0); }).detach();
  return ReplayStart::Started;
}
void watch_replay() {
  const int index=replay_current();
  if(index>=0&&index<(int)g_replay_files.size()) replay_start(g_replay_files[index]);
}
// Goes on to the next replay of the running queue that can be played. False when the queue is over,
// with the status line saying how it ended.
bool replay_queue_next() {
  const int count=(int)g_replay_playing.size();
  while(g_replay_play_at<g_replay_playing.size()) {
    const auto file=g_replay_playing[g_replay_play_at++];
    const auto started=replay_start(file,(int)g_replay_play_at,count);
    if(started==ReplayStart::Started) return true;
    if(started==ReplayStart::Failed) { g_replay_playing.clear(); return false; }
    ++g_replay_skipped; g_replay_skip_reason=launcher::lang::tx(g_replay_status);
  }
  replay_status(g_replay_skipped?launcher::lang::fill(launcher::lang::tx("Queue finished. Skipped {count}: {reason}"),launcher::lang::Args{{"count",std::to_string(g_replay_skipped)},{"reason",g_replay_skip_reason}})
                                :std::string("Queue finished."));
  g_replay_playing.clear();
  return false;
}
// "Watch queue": the queue as it is now, played in order. Changing the queue while it plays changes
// the next run, not this one.
void watch_replay_queue() {
  if(g_replay_queue.empty()) return;
  if(g_playing||g_building) { replay_status("Close the running game before opening a replay."); return; }
  g_replay_playing=g_replay_queue; g_replay_play_at=0; g_replay_skipped=0; g_replay_skip_reason.clear();
  replay_queue_next();
}
// The game has ended (WM_APP_GAME_DONE, with its exit code). True when a queue went on to its next
// replay: the launcher then stays as it is, minimized, as while a game runs.
bool replay_game_done(DWORD code) {
  if(!g_replay_active) { replay_buttons(); return false; }
  g_replay_active=false;
  // Exit code 0: the replay played to its end, and a queue goes on. 4: the viewer closed the window
  // first, which is a stop and not an error, and stops a queue too. A crash can also end in a clean
  // code, so the crash file decides: starting another game now would make that file look older than
  // the launch, and the offer to send the report would never come.
  if(!g_replay_playing.empty()&&code==0&&!crash_report::newer_than_launch(work_dir()+"\\melee_port_crash.txt")) {
    if(replay_queue_next()) return true;
  } else replay_status(code==4?"Replay stopped.":(code?"Replay playback ended with an error. Check melee_port.log.":"Replay finished."));
  g_replay_playing.clear(); replay_buttons();
  return false;
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
void card_zones(RECT card,RECT* stats,RECT* watch,RECT* queue) {
  *watch={card.right-S(46),card.top+S(12),card.right-S(16),card.top+S(42)};
  *stats={card.right-S(118),card.top+S(14),card.right-S(56),card.top+S(40)};
  *queue={card.right-S(156),card.top+S(13),card.right-S(128),card.top+S(41)};
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
  const int index=replay_at_row((int)item->itemID);   // the list box numbers its rows, not the replays
  if(index<0) return;
  const auto& replay=g_replay_info[index];
  const bool selected=(item->itemState&ODS_SELECTED)!=0, hot=(int)item->itemID==g_replay_hot;
  RECT card=r; InflateRect(&card,-S(4),-S(4)); card.right-=S(4);
  HRGN rgn=CreateRoundRectRgn(card.left,card.top,card.right+1,card.bottom+1,S(16),S(16));
  const int inner=SaveDC(dc); ExtSelectClipRgn(dc,rgn,RGN_AND);
  hgrad(dc,card,hot?RGB(0x18,0x21,0x35):C_CARD,lerp(C_CARD,stage_tint(replay.stage_id),hot?75:60,100));
  RestoreDC(dc,inner); DeleteObject(rgn);
  round_rect(dc,card,8,NO_FILL,NO_FILL,selected?C_ACC_HI:(hot?C_BTN_BORDER:RGB(0x24,0x2E,0x46)));
  RECT stats,watch,queue; card_zones(card,&stats,&watch,&queue);
  // Players. Four of them with long names would run under the buttons on the right: cut off before those.
  int x=card.left+S(16); const int cy=card.top+S(28);
  const int players=SaveDC(dc); IntersectClipRect(dc,card.left,card.top,queue.left-S(8),card.bottom);
  if(replay.players.empty()) draw_text(dc,widen(replay.path.stem().u8string()),RECT{x,cy-S(12),queue.left-S(8),cy+S(12)},replay_font(13,FW_SEMIBOLD),C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  for(size_t i=0;i<replay.players.size();++i) {
    if(i) { const bool vs=replay.players.size()==2; draw_text(dc,vs?L"vs":L"\x00B7",RECT{x+S(4),cy-S(10),x+S(30),cy+S(10)},replay_font(12,FW_SEMIBOLD),C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE); x+=S(34); }
    x=draw_player_chip(dc,replay.players[i],replay.winner==(int)i,x,cy,C_CARD);
  }
  RestoreDC(dc,players);
  // Date, length, stage
  std::wstring detail=widen(launcher::replay::display_date(replay));
  if(auto d=replay_duration(replay);!d.empty()) detail+=L"     \x23F1 "+d;
  if(!replay.stage.empty()) detail+=L"     \x25B2 "+widen(replay.stage);
  draw_text(dc,detail,RECT{card.left+S(16),card.top+S(48),card.right-S(200),card.bottom-S(8)},replay_font(12,FW_NORMAL),RGB(0xC4,0xCD,0xDD),DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  draw_text(dc,replay.path.filename().wstring(),RECT{card.right-S(260),card.top+S(48),card.right-S(16),card.bottom-S(8)},replay_font(11,FW_NORMAL),C_DIM,DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
  // The queue badge: this replay's place in the watch queue, or an empty ring with a plus to add it.
  const int place=replay_queue_place(g_replay_files[index]), radius=(queue.right-queue.left)/2;
  const bool hq=hot&&g_replay_hot_zone==3;
  if(place) {
    round_rect(dc,queue,radius,C_ACC_HI,C_ACC_LO,hq?C_TEXT:NO_FILL);
    draw_text(dc,std::to_wstring(place),queue,replay_font(12,FW_BOLD),C_PLAY_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  } else {
    round_rect(dc,queue,radius,RGB(0x1B,0x24,0x38),RGB(0x1B,0x24,0x38),hq?C_ACC_HI:C_BTN_BORDER);
    draw_text(dc,L"+",queue,replay_font(14,FW_SEMIBOLD),hq?C_TEXT:C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  }
  // Stats and Watch
  const bool hs=hot&&g_replay_hot_zone==1, hw=hot&&g_replay_hot_zone==2;
  round_rect(dc,stats,13,hs?C_BTN:RGB(0x1B,0x24,0x38),hs?C_BTN:RGB(0x1B,0x24,0x38),hs?C_ACC_HI:C_BTN_BORDER);
  draw_text(dc,L"Stats",stats,replay_font(11,FW_SEMIBOLD),C_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  round_rect(dc,watch,15,hw?C_OK:RGB(0x1B,0x24,0x38),hw?C_OK:RGB(0x1B,0x24,0x38),C_OK);
  RECT tri=watch; tri.left+=S(2);
  draw_text(dc,L"\x25B6",tri,replay_symbols(11),hw?RGB(0x0E,0x15,0x22):C_OK,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
}
// Everything here is in rows: what the list box reports and what the hover state keeps. A row
// becomes a replay (replay_at_row) only where something is done with it.
LRESULT CALLBACK replay_list_proc(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
  auto zone_at=[&](POINT p,int* row)->int {
    const LRESULT hit=SendMessageW(h,LB_ITEMFROMPOINT,0,MAKELPARAM(p.x,p.y));
    *row=HIWORD(hit)?-1:LOWORD(hit);
    if(*row<0||*row>=(int)g_replay_visible.size()) { *row=-1; return 0; }
    RECT r{}; SendMessageW(h,LB_GETITEMRECT,*row,(LPARAM)&r);
    RECT card=r; InflateRect(&card,-S(4),-S(4)); card.right-=S(4);
    RECT stats,watch,queue; card_zones(card,&stats,&watch,&queue);
    return PtInRect(&watch,p)?2:(PtInRect(&stats,p)?1:(PtInRect(&queue,p)?3:0));
  };
  switch(m) {
    case WM_MOUSEMOVE: {
      int row; const int zone=zone_at(POINT{GET_X_LPARAM(l),GET_Y_LPARAM(l)},&row);
      if(row!=g_replay_hot||zone!=g_replay_hot_zone) {
        RECT r{};
        if(g_replay_hot>=0&&SendMessageW(h,LB_GETITEMRECT,g_replay_hot,(LPARAM)&r)!=LB_ERR) InvalidateRect(h,&r,FALSE);
        g_replay_hot=row; g_replay_hot_zone=zone;
        if(row>=0&&SendMessageW(h,LB_GETITEMRECT,row,(LPARAM)&r)!=LB_ERR) InvalidateRect(h,&r,FALSE);
      }
      TRACKMOUSEEVENT t{sizeof t,TME_LEAVE,h,0}; TrackMouseEvent(&t);
      break;
    }
    case WM_MOUSELEAVE:
      if(g_replay_hot>=0) { RECT r{}; if(SendMessageW(h,LB_GETITEMRECT,g_replay_hot,(LPARAM)&r)!=LB_ERR) InvalidateRect(h,&r,FALSE); }
      g_replay_hot=-1; g_replay_hot_zone=0;
      break;
    case WM_SETCURSOR: { POINT p; GetCursorPos(&p); ScreenToClient(h,&p); int row; zone_at(p,&row); if(row>=0) { SetCursor(LoadCursorW(nullptr,IDC_HAND)); return TRUE; } break; }
    case WM_LBUTTONUP: {
      const LRESULT r=DefSubclassProc(h,m,w,l);
      int row; const int zone=zone_at(POINT{GET_X_LPARAM(l),GET_Y_LPARAM(l)},&row);
      if(row>=0) {
        SendMessageW(h,LB_SETCURSEL,row,0);
        // Watch plays the selection just made. The badge only queues: the card does not open.
        if(zone==2) watch_replay(); else if(zone==3) replay_queue_toggle(replay_at_row(row)); else replay_open_stats(replay_at_row(row));
      }
      return r;
    }
    case WM_KEYDOWN:
      if(w==VK_RETURN) { replay_open_stats(replay_selected()); return 0; }
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
// Full match, wall-clock columns: isolated peaks and marks survive reduction.
int draw_trace_overview(HDC dc,int x,int width,int y,const launcher::trace::Overview& view) {
  if(!view.present) return 0;
  draw_text(dc,L"Network and timing",RECT{x,y,x+width,y+S(28)},replay_font(20,FW_SEMIBOLD),C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  if(!view.error.empty()) {
    cell(dc,widen(view.error),RECT{x,y+S(32),x+width,y+S(72)},C_DIM);
    return S(90);
  }
  if(view.bins.empty()) return S(40);
  wchar_t text[256];
  swprintf_s(text,L"%.1f seconds   %zu waits   %zu rollbacks   %zu shed   %zu advanced   %zu marks",
             view.seconds,view.waits,view.rollbacks,view.sheds,view.advances,
             view.marks[0]+view.marks[1]+view.marks[2]+view.marks[3]);
  cell(dc,text,RECT{x,y+S(30),x+width,y+S(56)},C_DIM);
  const int px=x+S(10), pw=std::max(1,width-S(20));
  const int ty=y+S(64), th=S(90), ry=y+S(160), rh=S(24), my=y+S(190);
  g_trace_plot=RECT{px,ty,px+pw,my+S(18)};
  fill(dc,RECT{px,ty,px+pw,ty+th},C_FIELD);
  fill(dc,RECT{px,ry,px+pw,ry+rh},C_FIELD);
  const COLORREF interval=RGB(90,110,140),work=RGB(120,220,140),wait=RGB(230,70,70);
  const COLORREF roll=RGB(255,150,60),ping=RGB(0,220,220),shed=C_GOLD,advance=RGB(120,200,255);
  const COLORREF mark_colors[4]={C_TEXT,RGB(220,130,255),RGB(100,180,255),RGB(240,130,180)};
  const uint8_t mark_flags[4]={net_trace::kMark,net_trace::kMarkVisual,net_trace::kMarkInput,net_trace::kMarkAudio};
  int previous_x=0,previous_ping_y=0; bool previous=false;
  HPEN pen=CreatePen(PS_SOLID,std::max(1,S(1)),ping); HGDIOBJ old_pen=SelectObject(dc,pen);
  for(size_t i=0;i<view.bins.size();++i) {
    const auto& b=view.bins[i];
    const int bx=px+int(i*pw/view.bins.size()),end=std::max(bx+1,px+int((i+1)*pw/view.bins.size()));
    if(b.waits) fill(dc,RECT{bx,ty,end,ty+th},wait);
    const int ih=int(th*std::clamp(b.interval_ms/50.0,0.0,1.0));
    const int wh=int(th*std::clamp(b.work_ms/50.0f,0.0f,1.0f));
    fill(dc,RECT{bx,ty+th-ih,end,ty+th},b.interval_ms>25?C_GOLD:interval);
    fill(dc,RECT{bx,ty+th-wh,end,ty+th},work);
    if(b.sheds) fill(dc,RECT{bx,ty,end,ty+S(4)},shed);
    if(b.advances) fill(dc,RECT{bx,ty+S(5),end,ty+S(9)},advance);
    if(b.rollbacks) {
      const int h=std::max(S(2),rh*int(std::min<unsigned>(b.depth,7))/7);
      fill(dc,RECT{bx,ry+rh-h,end,ry+rh},roll);
    }
    for(int m=0;m<4;++m) if(b.flags&mark_flags[m])
      fill(dc,RECT{bx,my+S(m*4),std::max(end,bx+S(2)),my+S(m*4+3)},mark_colors[m]);
    if(b.ticks) {
      const int ping_y=ty+th-int(th*std::min<unsigned>(b.ping_ms,200)/200);
      if(previous) { MoveToEx(dc,previous_x,previous_ping_y,nullptr); LineTo(dc,bx,ping_y); }
      previous_x=bx;previous_ping_y=ping_y;previous=true;
    } else previous=false;
  }
  SelectObject(dc,old_pen);DeleteObject(pen);
  fill(dc,RECT{px,ty+th-int(th*(1000.0/60.0)/50),px+pw,ty+th-int(th*(1000.0/60.0)/50)+1},C_FAINT);
  cell(dc,L"0:00",RECT{px-S(10),y+S(212),px+pw/2,y+S(232)},C_DIM);
  const int seconds=int(std::min(view.seconds, double(INT_MAX)));
  swprintf_s(text,L"%d:%02d elapsed",seconds/60,seconds%60);
  cell(dc,text,RECT{px+pw/2,y+S(212),px+pw+S(6),y+S(232)},C_DIM,nullptr,DT_RIGHT);
  struct Key { const wchar_t* label; COLORREF color; };
  const Key keys[]={{L"Frame (top: 50 ms)",interval},{L"Work",work},{L"Wait",wait},{L"Rollback (top: 7)",roll},
    {L"Ping (top: 200 ms)",ping},{L"Shed",shed},{L"Advanced",advance},
    {L"Mark",mark_colors[0]},{L"Visual",mark_colors[1]},{L"Input",mark_colors[2]},{L"Audio",mark_colors[3]}};
  int kx=px,ky=y+S(236);
  for(const auto& key:keys) {
    const int kw=S(18)+text_width(dc,key.label,replay_font(11,FW_NORMAL));
    if(kx>px&&kx+kw>px+pw) { kx=px;ky+=S(20); }
    fill(dc,RECT{kx,ky+S(5),kx+S(7),ky+S(12)},key.color);
    draw_text(dc,key.label,RECT{kx+S(10),ky,kx+kw,ky+S(18)},replay_font(11,FW_NORMAL),C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    kx+=kw+S(8);
  }
  const int detail_y=ky+S(25);
  if(g_trace_hover>=0&&g_trace_hover<(int)view.bins.size()) {
    const auto& b=view.bins[g_trace_hover];
    const int hx=px+int(size_t(g_trace_hover)*pw/view.bins.size());
    fill(dc,RECT{hx,ty,hx+1,my+S(18)},C_TEXT);
    swprintf_s(text,L"%.1f s   peak frame %.1f ms   work %.1f ms   ping %u ms",
               view.seconds*g_trace_hover/view.bins.size(),b.interval_ms,double(b.work_ms),unsigned(b.ping_ms));
    cell(dc,text,RECT{x,detail_y,x+width,detail_y+S(22)},C_TEXT);
    swprintf_s(text,L"%u ticks   %u waits   %u rollbacks (depth %u)   %u shed   %u advanced",
               b.ticks,b.waits,b.rollbacks,unsigned(b.depth),b.sheds,b.advances);
    cell(dc,text,RECT{x,detail_y+S(22),x+width,detail_y+S(44)},C_DIM);
  } else {
    cell(dc,L"Move over the graph to inspect a time range.",RECT{x,detail_y,x+width,detail_y+S(22)},C_DIM);
    if(view.skipped) {
      swprintf_s(text,L"%zu incomplete or invalid timing records skipped.",view.skipped);
      cell(dc,text,RECT{x,detail_y+S(22),x+width,detail_y+S(44)},C_DIM);
    }
  }
  return detail_y+S(58)-y;
}
// Draws the page with its top at y (the scroll offset already applied) and returns its height.
int draw_stats(HDC dc,int width,int y) {
  const int top=y;
  g_trace_plot={};
  if(g_replay_view<0||g_replay_view>=(int)g_replay_info.size()) return 0;
  const auto& r=g_replay_info[g_replay_view];
  const int X0=S(4), W=width-S(8);
  if(g_replay_view<(int)g_replay_traces.size())
    y+=draw_trace_overview(dc,X0,W,y,g_replay_traces[g_replay_view]);
  if(r.players.size()!=2||!r.valid) {
    draw_text(dc,r.valid?L"Detailed stats are shown for one-on-one matches.":L"This file has no readable match data.",RECT{X0,y+S(40),X0+W,y+S(80)},replay_font(14,FW_NORMAL),C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    return y-top+S(120);
  }
  if(!r.stats_loaded) { draw_text(dc,L"Reading the match...",RECT{X0,y+S(40),X0+W,y+S(80)},replay_font(14,FW_NORMAL),C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE); return y-top+S(120); }
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
    case WM_PRINTCLIENT: {
      RECT cr;GetClientRect(h,&cr);fill((HDC)w,cr,C_CONTENT_BOT);
      draw_stats((HDC)w,cr.right,-g_stats_scroll);return 0;
    }
    case WM_MOUSEMOVE: {
      int hover=-1;POINT p{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
      if(PtInRect(&g_trace_plot,p)&&g_replay_view>=0&&g_replay_view<(int)g_replay_traces.size()) {
        const auto size=g_replay_traces[g_replay_view].bins.size();
        if(size) hover=int(std::min(size-1,size_t(p.x-g_trace_plot.left)*size/size_t(g_trace_plot.right-g_trace_plot.left)));
      }
      if(hover!=g_trace_hover) { g_trace_hover=hover;InvalidateRect(h,nullptr,FALSE); }
      if(!g_launcher_test) { TRACKMOUSEEVENT tracking{sizeof tracking,TME_LEAVE,h,0};TrackMouseEvent(&tracking); }
      return 0;
    }
    case WM_MOUSELEAVE: g_trace_hover=-1;InvalidateRect(h,nullptr,FALSE);return 0;
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
// "Hide short games" looks like "Show it as it was played", box, tick and label. That one is a
// button the main window draws (draw_button). This one is a small window of the page's own that
// draws and clicks itself, into a bitmap first so a repaint never shows it half done.
void paint_replay_tick(HWND h,HDC target) {
  RECT r; GetClientRect(h,&r);
  RECT wr; GetWindowRect(h,&wr); MapWindowPoints(nullptr,g_main,(POINT*)&wr,2);
  HDC dc=CreateCompatibleDC(target); HBITMAP b=CreateCompatibleBitmap(target,r.right,r.bottom); HGDIOBJ old=SelectObject(dc,b);
  vgrad(dc,r,content_bg_at(wr.top),content_bg_at(wr.bottom));
  RECT box{r.left+S(1),r.top+S(5),r.left+S(17),r.top+S(21)};
  round_rect(dc,box,3,C_FIELD,C_FIELD,C_BTN_BORDER);
  if(g_replay_hide_short) {
    HPEN pen=CreatePen(PS_SOLID,S(2),C_ACC_HI); HGDIOBJ old_pen=SelectObject(dc,pen);
    MoveToEx(dc,box.left+S(3),box.top+S(8),nullptr);
    LineTo(dc,box.left+S(7),box.bottom-S(4));
    LineTo(dc,box.right-S(3),box.top+S(3));
    SelectObject(dc,old_pen); DeleteObject(pen);
  }
  RECT label=r; label.left+=S(24);
  draw_text(dc,L"Hide short games",label,g_font_small,C_DIM,DT_LEFT|DT_SINGLELINE|DT_VCENTER);
  BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);
  SelectObject(dc,old); DeleteObject(b); DeleteDC(dc);
}
LRESULT CALLBACK replay_tick_proc(HWND h,UINT m,WPARAM w,LPARAM l) {
  switch(m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc=BeginPaint(h,&ps); paint_replay_tick(h,dc); EndPaint(h,&ps); return 0; }
    case WM_PRINTCLIENT: paint_replay_tick(h,(HDC)w); return 0;   // the launcher's test captures print every control off screen
    case WM_LBUTTONUP: SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(h),BN_CLICKED),(LPARAM)h); return 0;
  }
  return DefWindowProcW(h,m,w,l);
}
void create_replay_controls() {
  g_replays[0]=make(L"LISTBOX",L"",LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL,212,128,748,482,ID_REPLAY_LIST);
  SendMessageW(g_replays[0],LB_SETITEMHEIGHT,0,S(84)); SetWindowTheme(g_replays[0],L"DarkMode_Explorer",nullptr);
  SetWindowSubclass(g_replays[0],replay_list_proc,1,0);
  g_replays[1]=make(L"BUTTON",L"Browse...",BS_OWNERDRAW,212,622,120,34,ID_REPLAY_BROWSE);
  g_replays[2]=make(L"BUTTON",L"Watch Replay",BS_OWNERDRAW,808,622,150,34,ID_REPLAY_WATCH);
  g_replays[3]=make(L"BUTTON",L"Refresh",BS_OWNERDRAW,340,622,100,34,ID_REPLAY_REFRESH);
  g_replays[4]=make(L"BUTTON",L"\x2190",BS_OWNERDRAW,212,26,36,34,ID_REPLAY_BACK);
  g_replays[5]=make(L"BUTTON",L"\x2039",BS_OWNERDRAW,800,68,30,26,ID_REPLAY_PREV);
  g_replays[6]=make(L"BUTTON",L"\x203A",BS_OWNERDRAW,928,68,30,26,ID_REPLAY_NEXT);
  g_replays[7]=make(L"BUTTON",L"",BS_OWNERDRAW,212,668,470,24,ID_REPLAY_ASPLAYED);   // drawn by draw_button, as the other tick box
  // The row over the list: search (its frame is drawn by paint_replays), sort, "Hide short games".
  g_replays[8]=make(L"EDIT",L"",ES_AUTOHSCROLL,222,96,300,18,ID_REPLAY_SEARCH);
  SendMessageW(g_replays[8],EM_SETLIMITTEXT,200,0);
  SendMessageW(g_replays[8],EM_SETCUEBANNER,TRUE,(LPARAM)launcher::lang::txw(L"Search players, characters, stages, files").c_str());
  g_replays[9]=make(L"BUTTON",L"",BS_OWNERDRAW,542,90,200,30,ID_REPLAY_SORT);
  WNDCLASSEXW tick{sizeof tick}; tick.lpfnWndProc=replay_tick_proc; tick.hInstance=GetModuleHandleW(nullptr);
  tick.hCursor=LoadCursorW(nullptr,IDC_ARROW); tick.lpszClassName=L"MeleeUnlockedReplayTick";
  RegisterClassExW(&tick);
  g_replays[10]=make(tick.lpszClassName,L"",0,754,93,206,24,ID_REPLAY_HIDESHORT);
  // The queue's buttons, under "Watch Replay". Shown while something is queued (replay_buttons).
  g_replays[11]=make(L"BUTTON",L"",BS_OWNERDRAW,808,662,150,30,ID_REPLAY_QUEUE);
  g_replays[12]=make(L"BUTTON",L"Clear queue",BS_OWNERDRAW,698,662,102,30,ID_REPLAY_QUEUE_CLEAR);
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
    // Where this replay stands in the list as it is shown, which is what the arrows beside it walk.
    if(const int row=replay_row_of(g_replay_view);row>=0)
      draw_text(dc,std::to_wstring(row+1)+L" / "+std::to_wstring(g_replay_visible.size()),LR(830,68,98,26),replay_font(12,FW_SEMIBOLD),C_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    fill(dc,LR(212,104,760,2),C_ACC_LO);
    return;
  }
  draw_text(dc,L"Replay Viewer",LR(214,24,420,32),g_font_big,C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  draw_text(dc,L"Relive your matches and see the details that matter.",LR(214,58,560,22),g_font,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  round_rect(dc,LR(212,90,320,30),7,C_FIELD,C_FIELD,C_FIELD_BORDER);   // the search box
  if(g_replay_visible.empty()) {
    // The list box is hidden while it has no rows (replay_view_build), so this shows in its place.
    round_rect(dc,LR(212,128,748,482),12,C_FIELD,C_FIELD,C_FIELD_BORDER);
    const bool loading=g_replay_status.rfind("Loading",0)==0, none=g_replay_files.empty();
    draw_text(dc,!none?L"No replays match":(loading?L"Loading replays...":L"No replays yet"),LR(212,316,748,34),g_font_big,C_TEXT,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    if(!none) draw_text(dc,L"Try another search, or untick Hide short games.",LR(212,354,748,28),g_font,C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
#ifndef MELEE_NO_SLIPPI
    else if(!loading) draw_text(dc,L"Play a match or browse for a .slp file.",LR(212,354,748,28),g_font,C_DIM,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
#endif
  }
  draw_text(dc,widen(g_replay_status),LR(460,622,330,34),g_font_small,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
}
