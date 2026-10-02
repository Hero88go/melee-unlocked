// Native, themed lobby presentation. Network state remains in launcher_lobby.cpp.
const COLORREF ui_bg=RGB(19,26,42), ui_panel=RGB(12,18,33), ui_border=RGB(43,54,82);
const COLORREF ui_text=RGB(231,236,245), ui_dim=RGB(141,155,181);
HFONT ui_font{},ui_small{},ui_small_bold{},ui_title{},ui_name{};
HBRUSH ui_brush{},ui_background{},ui_field_brush{};
const COLORREF ui_field=RGB(16,23,38);
HICON stock_icons[26]{};
HICON emoji_icons[8]{};
POINT stock_icon_offsets[26]{};
POINT emoji_icon_offsets[8]{};
HWND emoji_popup{};
const wchar_t* emoji_chars[8]={L"\U0001F44B",L"\U0001F44D",L"\U0001F602",L"\U0001F525",
  L"\u2764\uFE0F",L"\U0001F3AE",L"\U0001F91D",L"\U0001F389"};
std::wstring stock_names[26];
LRESULT CALLBACK chat_proc(HWND h,UINT message,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR) {
  if(message==WM_GETDLGCODE && lp && ((MSG*)lp)->wParam==VK_RETURN) return DLGC_WANTALLKEYS;
  if(message==WM_KEYDOWN && wp==VK_RETURN) { SendMessageW(GetParent(h),WM_COMMAND,MAKEWPARAM(SEND,BN_CLICKED),0); return 0; }
  if(message==WM_NCDESTROY) RemoveWindowSubclass(h,chat_proc,id);
  return DefSubclassProc(h,message,wp,lp);
}
int U(int n) { return MulDiv(n,owner?GetDpiForWindow(owner):96,96); }
void box(HDC dc,RECT r,COLORREF color,int radius=0) {
  HBRUSH b=CreateSolidBrush(color); auto old=SelectObject(dc,b);
  auto pen=SelectObject(dc,GetStockObject(NULL_PEN));
  if(radius) RoundRect(dc,r.left,r.top,r.right,r.bottom,U(radius),U(radius)); else FillRect(dc,&r,b);
  SelectObject(dc,pen); SelectObject(dc,old); DeleteObject(b);
}
void ink(HDC dc,const std::wstring& s,RECT r,HFONT font,COLORREF color,UINT flags=DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS) {
  auto old=SelectObject(dc,font); SetBkMode(dc,TRANSPARENT); SetTextColor(dc,color);
  DrawTextW(dc,s.c_str(),-1,&r,flags); SelectObject(dc,old);
}
// One line drawn in pieces, each with its own font and color (a player card's mod badges: "Open to"
// bold, "Has" dim). The piece that reaches the right edge ends in an ellipsis.
struct TextRun { std::wstring text; HFONT font; COLORREF color; };
void ink_runs(HDC dc,const std::vector<TextRun>& runs,RECT r) {
  for(const auto& run:runs) {
    if(r.left>=r.right) break;
    if(run.text.empty()) continue;
    auto old=SelectObject(dc,run.font);
    SIZE size{}; GetTextExtentPoint32W(dc,run.text.c_str(),(int)run.text.size(),&size);
    SelectObject(dc,old);
    ink(dc,run.text,r,run.font,run.color);
    r.left+=size.cx;
  }
}
// ---- private chat rooms: a tab each on the Chat page, next to the public lobby chat ----
const Json* find_room(const std::string& id) {
  for(const auto& room:private_rooms) if(room.value("id",std::string())==id) return &room;
  return nullptr;
}
// The other player wrote something the player has not had in front of them yet.
bool room_unread(const Json& room) {
  const Json messages=room.value("messages",Json::array());
  if(!messages.is_array() || messages.empty()) return false;
  const Json& last=messages.back();
  if(last.value("sender",std::string())!=room.value("peer",std::string())) return false;
  const auto seen=room_seen.find(room.value("id",std::string()));
  return seen==room_seen.end() || seen->second!=last.value("id",std::string());
}
bool any_room_unread() {
  for(const auto& room:private_rooms) if(room_unread(room)) return true;
  return false;
}
// Brings a room (or "" for the public lobby chat) to the front. What was typed for the room in front
// is cleared, so a line meant for one room can never be sent to another.
void show_room(const std::string& id) {
  if(active_room==id) return;
  active_room=id; chat_switched=true;
  if(window) SetWindowTextW(GetDlgItem(window,CHAT),L"");
}
// The tabs, drawn where the "Lobby chat" heading otherwise is. Each private tab has a close mark; a
// dot and bold text mean unread messages. The hit boxes are kept for the mouse.
void paint_room_tabs(HDC dc,const RECT& card) {
  struct Tab { std::string id; std::wstring title; bool unread; };
  std::vector<Tab> tabs;
  tabs.push_back({std::string(),launcher::lang::trw("lobby.pm.tab_public"),false});
  for(const auto& room:private_rooms)
    tabs.push_back({room.value("id",std::string()),wide(room.value("name",std::string("?"))),room_unread(room)});
  const int gap=U(6), pad=U(11), cross=U(18), dot=U(12);
  const int x0=card.left+U(14), x1=card.right-U(14), top=card.top+U(10), bottom=card.top+U(40);
  const int count=(int)tabs.size();
  const int share=std::max(U(40),(x1-x0-gap*(count-1))/count);
  int x=x0;
  for(const auto& tab:tabs) {
    const bool front=tab.id==active_room, closable=!tab.id.empty();
    HFONT font=(tab.unread || front)?ui_name:ui_font;
    auto old=SelectObject(dc,font);
    SIZE size{}; GetTextExtentPoint32W(dc,tab.title.c_str(),(int)tab.title.size(),&size);
    SelectObject(dc,old);
    const int width=std::min(share,int(size.cx)+2*pad+(closable?cross:0)+(tab.unread?dot:0));
    RECT r{x,top,x+width,bottom};
    if(r.right>x1) break;
    box(dc,r,front?launcher::theme::mix(launcher::theme::accent,ui_panel,.55):RGB(33,43,66),9);
    RECT label_rect{r.left+pad,r.top,r.right-pad-(closable?cross:0),r.bottom};
    if(tab.unread) {
      RECT mark{r.left+pad,r.top+(r.bottom-r.top)/2-U(3),r.left+pad+U(7),r.top+(r.bottom-r.top)/2+U(4)};
      box(dc,mark,launcher::theme::glow(),7);
      label_rect.left+=dot;
    }
    ink(dc,tab.title,label_rect,font,(front || tab.unread)?ui_text:ui_dim);
    RECT close_rect{};
    if(closable) {
      close_rect=RECT{r.right-pad-U(12),r.top,r.right-U(5),r.bottom};
      // The multiplication sign as a close mark.
      ink(dc,std::wstring(1,(wchar_t)0x00D7),close_rect,ui_font,front?ui_text:ui_dim,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    }
    room_tab_hits.push_back(r); room_close_hits.push_back(close_rect); room_tab_ids.push_back(tab.id);
    x=r.right+gap;
  }
}
// Line breaks at spaces, measured with the font that draws the text. DrawText's own word break
// split a Russian heading inside a word; text without spaces (Japanese, Chinese) is left for
// DT_WORDBREAK to break by character.
std::wstring wrap_words(HDC dc,HFONT font,const std::wstring& s,int width) {
  auto old=SelectObject(dc,font);
  std::wstring out;
  size_t start=0;
  while(start<=s.size()) {
    size_t end=s.find(L'\n',start); if(end==std::wstring::npos) end=s.size();
    const std::wstring paragraph=s.substr(start,end-start);
    std::wstring line;
    for(size_t i=0;i<=paragraph.size();) {
      size_t j=paragraph.find(L' ',i); if(j==std::wstring::npos) j=paragraph.size();
      const std::wstring word=paragraph.substr(i,j-i);
      const std::wstring candidate=line.empty()?word:line+L" "+word;
      SIZE size{}; GetTextExtentPoint32W(dc,candidate.c_str(),(int)candidate.size(),&size);
      if(size.cx>width && !line.empty()) { out+=line+L"\n"; line=word; } else line=candidate;
      i=j+1;
    }
    out+=line;
    if(end<s.size()) out+=L"\n";
    start=end+1;
  }
  SelectObject(dc,old);
  return out;
}
POINT visible_icon_offset(HICON icon) {
  ICONINFO info{}; if(!icon || !GetIconInfo(icon,&info)) return {};
  POINT result{};
  if(info.hbmColor) {
    BITMAP bitmap{};
    if(GetObjectW(info.hbmColor,sizeof bitmap,&bitmap) && bitmap.bmWidth>0 && bitmap.bmHeight>0) {
      const int width=bitmap.bmWidth,height=bitmap.bmHeight;
      std::vector<unsigned> pixels(size_t(width)*height);
      BITMAPINFO bmi{}; bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
      bmi.bmiHeader.biWidth=width; bmi.bmiHeader.biHeight=-height;
      bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
      HDC dc=GetDC(nullptr);
      if(GetDIBits(dc,info.hbmColor,0,height,pixels.data(),&bmi,DIB_RGB_COLORS)) {
        double mass=0,xmass=0,ymass=0;
        for(int y=0;y<height;++y) for(int x=0;x<width;++x) {
          const double alpha=(pixels[size_t(y)*width+x]>>24)&255;
          mass+=alpha; xmass+=x*alpha; ymass+=y*alpha;
        }
        if(mass>0) {
          result.x=std::clamp(LONG(std::lround(((width-1)*.5-xmass/mass)*24/width)),LONG(-3),LONG(3));
          result.y=std::clamp(LONG(std::lround(((height-1)*.5-ymass/mass)*24/height)),LONG(-3),LONG(3));
        }
      }
      ReleaseDC(nullptr,dc);
    }
  }
  if(info.hbmColor) DeleteObject(info.hbmColor);
  if(info.hbmMask) DeleteObject(info.hbmMask);
  return result;
}
void centered_icon(HDC dc,HICON icon,RECT r,int side,POINT offset={}) {
  if(!icon) return;
  const int size=U(side);
  DrawIconEx(dc,r.left+(r.right-r.left-size)/2+U(offset.x*side/24),
                 r.top+(r.bottom-r.top-size)/2+U(offset.y*side/24),icon,size,size,0,nullptr,DI_NORMAL);
}
LRESULT CALLBACK emoji_popup_proc(HWND w,UINT message,WPARAM wp,LPARAM lp) {
  if(message==WM_PAINT) {
    PAINTSTRUCT ps{};HDC dc=BeginPaint(w,&ps);
    box(dc,RECT{0,0,U(198),U(112)},ui_border,10);
    box(dc,RECT{U(1),U(1),U(197),U(111)},ui_panel,9);
    ink(dc,launcher::lang::txw(L"Add emoji"),RECT{U(14),U(6),U(180),U(28)},ui_name,ui_text);
    for(int i=0;i<8;++i) {
      int x=U(12+(i%4)*46),y=U(35+(i/4)*37);
      box(dc,RECT{x,y,x+U(40),y+U(33)},RGB(33,43,66),7);
      centered_icon(dc,emoji_icons[i],RECT{x,y,x+U(40),y+U(33)},22,emoji_icon_offsets[i]);
    }
    EndPaint(w,&ps);return 0;
  }
  if(message==WM_LBUTTONUP) {
    int x=MulDiv(GET_X_LPARAM(lp),96,owner?GetDpiForWindow(owner):96);
    int y=MulDiv(GET_Y_LPARAM(lp),96,owner?GetDpiForWindow(owner):96);
    int col=(x-12)/46,row=(y-35)/37;
    if(x>=12&&y>=35&&col>=0&&col<4&&row>=0&&row<2 && x-12-col*46<40 && y-35-row*37<33) {
      const int index=row*4+col;
      SendMessageW(GetDlgItem(window,CHAT),EM_REPLACESEL,TRUE,(LPARAM)emoji_chars[index]);
    }
    DestroyWindow(w);SetFocus(GetDlgItem(window,CHAT));return 0;
  }
  if(message==WM_ACTIVATE && LOWORD(wp)==WA_INACTIVE) { DestroyWindow(w);return 0; }
  if(message==WM_DESTROY) {emoji_popup=nullptr;return 0;}
  return DefWindowProcW(w,message,wp,lp);
}
void show_emoji_picker() {
  if(emoji_popup) {DestroyWindow(emoji_popup);return;}
  static bool registered=false;
  if(!registered) {
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=emoji_popup_proc;
    wc.lpszClassName=L"MeleeUnlockedEmojiPicker";wc.hCursor=LoadCursorW(nullptr,IDC_HAND);
    registered=RegisterClassW(&wc)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
  }
  RECT anchor{};GetWindowRect(GetDlgItem(window,EMOJI),&anchor);
  emoji_popup=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"MeleeUnlockedEmojiPicker",L"Emoji",
    WS_POPUP,anchor.left-U(90),anchor.top-U(117),U(198),U(112),window,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(emoji_popup) {ShowWindow(emoji_popup,SW_SHOWNORMAL);UpdateWindow(emoji_popup);}
}
void add(HWND w,int id,const wchar_t* type,const wchar_t* caption,int,int,int,int,DWORD style=0) {
  if(!wcscmp(type,L"BUTTON")) style=BS_OWNERDRAW|WS_TABSTOP;
  if(!wcscmp(type,L"EDIT")) style=(style&~WS_BORDER)|WS_TABSTOP;
  if(!wcscmp(type,L"LISTBOX")) style=(style&~(WS_BORDER|WS_HSCROLL))|LBS_OWNERDRAWFIXED|LBS_HASSTRINGS|LBS_NOTIFY|WS_TABSTOP;
  // Captions in the player's language (English text is the lookup key; unknown text stays as is).
  const std::wstring shown=launcher::lang::txw(caption?caption:L"");
  HWND h=CreateWindowExW(0,type,shown.c_str(),WS_CHILD|WS_CLIPSIBLINGS|style,0,0,0,0,w,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
  SendMessageW(h,WM_SETFONT,(WPARAM)ui_font,TRUE);
  if(!wcscmp(type,L"LISTBOX")) SendMessageW(h,LB_SETITEMHEIGHT,0,U(id==PLAYERS||id==FRIENDS?76:54));
}
void place(int id,int x,int y,int width,int height,bool show=true) {
  HWND h=GetDlgItem(window,id); if(!h) return;
  RECT previous{}; GetWindowRect(h,&previous); MapWindowPoints(nullptr,window,(POINT*)&previous,2);
  RECT next{U(x),U(y),U(x+width),U(y+height)};
  if(!EqualRect(&previous,&next)) SetWindowPos(h,nullptr,next.left,next.top,next.right-next.left,next.bottom-next.top,SWP_NOZORDER|SWP_NOACTIVATE);
  const bool visible=(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)!=0;
  if(show!=visible) { ShowWindow(h,show?SW_SHOWNA:SW_HIDE); InvalidateRect(window,&previous,FALSE); InvalidateRect(window,&next,FALSE); }

}
// A control's text width in layout units (1/96 inch), in the font the lobby draws it with.
int text_width(int id) {
  HWND h=GetDlgItem(window,id); if(!h) return 0;
  const int length=GetWindowTextLengthW(h); if(length<=0) return 0;
  std::wstring s(length+1,0); s.resize(GetWindowTextW(h,s.data(),length+1));
  HDC dc=GetDC(h); auto old=SelectObject(dc,ui_font);
  SIZE size{}; GetTextExtentPoint32W(dc,s.c_str(),(int)s.size(),&size);
  SelectObject(dc,old); ReleaseDC(h,dc);
  return MulDiv(size.cx,96,owner?GetDpiForWindow(owner):96);
}
void layout() {
  if(!window) return;
  RECT r{}; GetClientRect(window,&r); const int width=MulDiv(r.right,96,owner?GetDpiForWindow(owner):96);
  const int height=MulDiv(r.bottom,96,owner?GetDpiForWindow(owner):96);
  const int right=width-316, left_width=right-42, bottom=height-94;
  place(GO_ONLINE,width-132,24,108,30);
  place(ONLINE_HINT,24,66,width-48,34);
  place(TAB_PROFILE,24,bottom+12,82,34); place(TAB_FRIENDS,114,bottom+12,82,34); place(TAB_HISTORY,204,bottom+12,82,34);
  place(TAB_CHAT,294,bottom+12,132,34,lobby_tab!=0);
  place(PLAYER_HEADING,right+14,124,264,24);
  // The Show filter has its own row: beside the heading it would not fit in every language.
  place(PLAYER_FILTER,right+12,154,268,28,!roster_all.empty());
  // Private Chat sits under the list, inside the Players card.
  place(PLAYERS,right+12,190,268,bottom-240,!rows.empty());
  place(PM_PLAYER,right+12,bottom-46,268,32,!rows.empty());
  place(REQUEST,right,bottom+12,181,34); place(ADD_FRIEND,right+189,bottom+12,103,34);
  bool pending=!requests.empty() && lobby_tab==0;
  int content_bottom=bottom-(pending?122:0);
  place(CHATLOG,38,166,left_width-28,content_bottom-222,lobby_tab==0&&!chat_messages.empty());
  place(CHAT,38,content_bottom-43,left_width-130,26,lobby_tab==0);
  place(EMOJI,left_width-85,content_bottom-46,30,32,lobby_tab==0);
  place(SEND,left_width-50,content_bottom-46,62,32,lobby_tab==0);
  place(REQUESTS,38,content_bottom+4,left_width-28,62,pending);
  place(ACCEPT,38,content_bottom+72,116,30,pending); place(DECLINE,162,content_bottom+72,126,30,pending);
  place(FRIENDS,38,166,left_width-28,bottom-(adding_friend?320:222),lobby_tab==1&&!friends.empty());
  int choice=(int)SendMessageW(GetDlgItem(window,FRIENDS),LB_GETCURSEL,0,0);
  bool incoming=choice>=0 && choice<(int)friends.size() && friends[choice].value("incoming",false);
  place(FRIEND_ACCEPT,38,bottom-44,118,30,lobby_tab==1&&incoming&&!adding_friend);
  place(FRIEND_DECLINE,164,bottom-44,86,30,lobby_tab==1&&incoming&&!adding_friend);
  // A friend: Invite to Match first (what a friend is for), then Remove Friend.
  const bool chosen_friend=lobby_tab==1&&!incoming&&choice>=0&&!adding_friend;
  place(INVITE_FRIEND,38,bottom-44,140,30,chosen_friend);
  place(REMOVE_FRIEND,186,bottom-44,120,30,chosen_friend);
  place(PM_FRIEND,314,bottom-44,140,30,chosen_friend);
  place(RECORD,38,168,left_width-28,24,lobby_tab==2);
  place(HISTORY,38,202,left_width-28,bottom-216,lobby_tab==2&&!history.empty());
  const bool profile=lobby_tab==3;
  for(int i=0;i<3;++i) {
    place(PROFILE_NAME+i,40,166+i*64,left_width-32,18,profile);
    // The Location field shares its row with the Open to button.
    place(NAME+i,44,194+i*64,i==2?left_width-40-206:left_width-40,24,profile);
  }
  place(OPEN_TO,left_width-196,191+2*64,196,30,profile);
  place(PROFILE_MAINS,40,358,left_width-32,20,profile);
  for(int i=0;i<26;++i) place(CHARACTER_FIRST+i,40+(i%9)*42,388+(i/9)*42,36,36,profile);
  for(int id:{ADVANCED,PROFILE_MODE,MODE,URL_LABEL,URL}) ShowWindow(GetDlgItem(window,id),SW_HIDE);
  place(AUTO_REJECT,40,524,174,30,profile);
  place(REQUEST_SOUND,222,524,172,30,profile);
  place(VOLUME_LABEL,170,610,left_width-162,30,profile);
  place(SAVE_PROFILE,40,610,114,30,profile);
  place(FRIEND_HINT,38,bottom-136,left_width-28,36,lobby_tab==1&&adding_friend);
  place(FRIEND_CODE,44,bottom-90,left_width-46,26,lobby_tab==1&&adding_friend);
  place(FRIEND_SEND,38,bottom-46,150,30,lobby_tab==1&&adding_friend);
  // Copy code sits right after "Can't reach X. Use Slippi Direct with their code: X#1" while it shows.
  const bool copy=!copy_button_code.empty();
  const int copy_x=std::min(24+(copy?text_width(STATUS):0)+12,width-24-104);
  place(COPY_CODE,copy_x,bottom+53,104,26,copy);
  place(STATUS,24,bottom+58,copy?copy_x-32:width-48,18);
  for(int id:{EMPTY_PLAYERS,EMPTY_FRIENDS,EMPTY_CHAT}) ShowWindow(GetDlgItem(window,id),SW_HIDE);

}
Json profile_config() {
  Json cfg;
  { std::lock_guard<std::mutex> lock(mutex); cfg=config; }
  const bool prior_peer=peer_mode(cfg);
  cfg["mode"]="peer";
  // Before the Lobby page was ever opened there are no fields to read: the saved profile holds them.
  if(window) {
    cfg["url"]=prior_peer?text(URL):std::string();
    cfg["name"]=text(NAME); cfg["location"]=text(LOCATION);
  } else if(!prior_peer) cfg["url"]=std::string();
  if(cfg.value("name",std::string()).empty()) cfg["name"]=account_name;
  cfg["code"]=account_code;
  cfg["build"]=build; cfg["ready"]=can_play; cfg["mains"]=selected_mains;
  // What this PC has and what the player takes requests for (see launcher_lobby_p2p.h).
  if(!mod_has.empty()) cfg["has"]=mod_has; else cfg.erase("has");
  cfg["open"]=open_list();
  if(custom_launch_ready && !custom_hash.empty() && valid_iso_name(current_prefs.custom_name))
    cfg["iso"]={{"n",current_prefs.custom_name},{"h",custom_hash}};
  else cfg.erase("iso");
  int wins=0,losses=0;
  for(const auto& game:history) {
    if(game.value("result",std::string())=="win") ++wins;
    else if(game.value("result",std::string())=="loss") ++losses;
  }
  cfg["wins"]=wins; cfg["losses"]=losses;
  return cfg;
}
void draw_control(DRAWITEMSTRUCT* d) {
  const int id=(int)d->CtlID; RECT r=d->rcItem;
  if(d->CtlType==ODT_BUTTON) {
    if(id==EMOJI) {
      box(d->hDC,r,ui_bg);
      box(d->hDC,r,RGB(33,43,66),8);
      centered_icon(d->hDC,emoji_icons[2],r,20,emoji_icon_offsets[2]);
      return;
    }
    if(id>=CHARACTER_FIRST && id<CHARACTER_FIRST+26) {
      int n=id-CHARACTER_FIRST; bool selected=std::find(selected_mains.begin(),selected_mains.end(),n)!=selected_mains.end();
      box(d->hDC,r,ui_panel); box(d->hDC,r,selected?launcher::theme::top():ui_border,9);
      RECT inside=r; InflateRect(&inside,-U(2),-U(2)); box(d->hDC,inside,selected?launcher::theme::mix(launcher::theme::accent,ui_panel,.7):RGB(23,31,48),7);
      if(stock_icons[n]) centered_icon(d->hDC,stock_icons[n],r,24,stock_icon_offsets[n]);
      else ink(d->hDC,wide(characters[n]).substr(0,2),r,ui_small,ui_text,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
      return;
    }
    bool active=(id>=TAB_CHAT&&id<=TAB_PROFILE&&id-TAB_CHAT==lobby_tab);
    bool primary=id==REQUEST||id==GO_ONLINE||id==ACCEPT||id==INVITE_FRIEND||active;
    bool enabled=!(d->itemState&ODS_DISABLED);
    box(d->hDC,r,id==GO_ONLINE||id==REQUEST||id==ADD_FRIEND||id==TAB_PROFILE||id==TAB_HISTORY||id==TAB_FRIENDS||id==TAB_CHAT||id==COPY_CODE?ui_bg:ui_panel);
    COLORREF top=primary?launcher::theme::top():RGB(33,43,66), bot=primary?launcher::theme::bottom():top;
    if(!enabled) top=bot=RGB(26,33,50);
    if(d->itemState&ODS_SELECTED) top=bot=primary?launcher::theme::pressed():RGB(24,33,52);
    HRGN clip=CreateRoundRectRgn(r.left,r.top,r.right,r.bottom,U(9),U(9));
    SaveDC(d->hDC); ExtSelectClipRgn(d->hDC,clip,RGN_AND);
    for(int y=r.top;y<r.bottom;++y) {
      int n=y-r.top, total=std::max(1,int(r.bottom-r.top));
      COLORREF c=RGB(GetRValue(top)+(GetRValue(bot)-GetRValue(top))*n/total,GetGValue(top)+(GetGValue(bot)-GetGValue(top))*n/total,GetBValue(top)+(GetBValue(bot)-GetBValue(top))*n/total);
      box(d->hDC,RECT{r.left,y,r.right,y+1},c);
    }
    RestoreDC(d->hDC,-1); DeleteObject(clip);
    if(!primary) {
      HPEN pen=CreatePen(PS_SOLID,1,enabled?RGB(53,65,95):ui_border); auto oldPen=SelectObject(d->hDC,pen); auto oldBrush=SelectObject(d->hDC,GetStockObject(NULL_BRUSH));
      RoundRect(d->hDC,r.left,r.top,r.right,r.bottom,U(9),U(9)); SelectObject(d->hDC,oldBrush); SelectObject(d->hDC,oldPen); DeleteObject(pen);
    }
    wchar_t caption[128]{}; GetWindowTextW(d->hwndItem,caption,128);
    ink(d->hDC,caption,r,ui_font,enabled?(primary?launcher::theme::on_accent():ui_text):ui_dim,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
    // Back to Chat carries a dot while a private room has unread messages.
    if(id==TAB_CHAT && any_room_unread()) {
      RECT mark{r.right-U(15),r.top+U(6),r.right-U(8),r.top+U(13)};
      box(d->hDC,mark,launcher::theme::glow(),7);
    }
    if((d->itemState&ODS_FOCUS) && !(d->itemState&ODS_NOFOCUSRECT)) { InflateRect(&r,-4,-4); DrawFocusRect(d->hDC,&r); }
    return;
  }
  box(d->hDC,r,ui_panel);
  box(d->hDC,r,d->itemState&ODS_SELECTED?launcher::theme::mix(launcher::theme::accent,ui_panel,.82):ui_panel,6);
  if(d->itemID==(UINT)-1) return;
  if(id==CHATLOG && d->itemID<chat_messages.size()) {
    const auto& message=chat_messages[d->itemID];
    if(message.value("system",false)) {   // a line from the launcher itself: the other player closed the room
      RECT line{r.left+U(12),r.top,r.right-U(12),r.bottom};
      ink(d->hDC,wide(message.value("text",std::string())),line,ui_small,ui_dim,DT_CENTER|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
      return;
    }
    RECT name{r.left+U(12),r.top+U(5),r.right-U(78),r.top+U(27)};
    ink(d->hDC,wide(message.value("name",std::string("Player"))),name,ui_name,launcher::theme::glow());
    std::string stamp="";auto wall=message.value("wall_time",int64_t(0));
    if(wall>1000000000) {
      std::time_t seconds=(std::time_t)wall;std::tm local{};
      if(localtime_s(&local,&seconds)==0) {char buffer[16]{};std::strftime(buffer,sizeof buffer,"%I:%M %p",&local);stamp=buffer;}
    }
    RECT time{r.right-U(88),r.top+U(5),r.right-U(12),r.top+U(27)};
    ink(d->hDC,wide(stamp),time,ui_small,ui_dim,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);
    RECT body{r.left+U(12),r.top+U(27),r.right-U(12),r.bottom-U(4)};
    ink(d->hDC,wide(message.value("text",std::string())),body,ui_font,ui_text,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    return;
  }
  const Json* p=nullptr;
  if(id==PLAYERS&&d->itemID<rows.size()) p=&rows[d->itemID];
  if(id==FRIENDS&&d->itemID<friends.size()) p=&friends[d->itemID];
  if(p) {
    const auto mains=p->value("mains",Json::array());
    int main=mains.empty()?-1:mains[0].get<int>();
    RECT avatar{r.left+U(10),r.top+U(13),r.left+U(42),r.top+U(45)};
    if(main>=0 && main<26 && stock_icons[main]) centered_icon(d->hDC,stock_icons[main],avatar,32,stock_icon_offsets[main]);
    else { box(d->hDC,avatar,launcher::theme::mix(launcher::theme::accent,ui_panel,.7),12); ink(d->hDC,wide(p->value("name",std::string("?"))).substr(0,1),avatar,ui_font,ui_text,DT_CENTER|DT_VCENTER|DT_SINGLELINE); }
    auto name=wide(p->value("name",std::string("Player"))+(p->value("is_self",false)?"  "+launcher::lang::tx("(You)"):std::string()));
    auto version=p->value("build",std::string());
    std::string kind;
    if(auto colon=version.find(':'); colon!=std::string::npos) { kind=version.substr(colon+1); version.resize(colon); }
    if(!version.empty() && version[0]!='v' && version[0]!='V') version="v"+version;
    // Both players need the same Game Build: show it, not only the version.
    if(kind=="source") version+="  Source Port"; else if(kind=="recomp") version+="  Static Recomp";
    // Share the header using the fonts that actually draw it. A fixed 150-unit build
    // slot left only 64 units for the nickname and localized "(You)" on a roster card.
    const int text_left=avatar.right+U(12), text_right=r.right-U(12);
    const int content_width=std::max(0,text_right-text_left), header_gap=U(8);
    auto measure=[&](const std::wstring& text,HFONT font) {
      auto old=SelectObject(d->hDC,font);
      SIZE size{}; GetTextExtentPoint32W(d->hDC,text.c_str(),(int)text.size(),&size);
      SelectObject(d->hDC,old); return int(size.cx);
    };
    const auto build=wide(version);
    const int name_width=measure(name,ui_name)+U(2);
    const int build_width=build.empty()?0:std::min(measure(build,ui_small)+U(2),
      std::max(0,content_width-header_gap-std::min(name_width,content_width/2)));
    RECT line{text_left,r.top+U(8),build_width?text_right-build_width-header_gap:text_right,r.top+U(26)};
    ink(d->hDC,name,line,ui_name,ui_text);
    RECT version_rect{text_right-build_width,r.top+U(8),text_right,r.top+U(26)};
    if(build_width) ink(d->hDC,build,version_rect,ui_small,launcher::theme::glow(),DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    line.right=r.right-U(12);
    std::string detail=p->value("incoming",false)?"Friend request":p->value("status",std::string("Online"));
    // A player who cannot take a match yet says so instead of looking free: the player's own card from
    // this PC's setup (disc, built game, Slippi sign-in), everyone else's from their profile.
    const bool self_card=p->value("is_self",false);
    if(detail=="Online" && !(self_card?can_play:p->value("ready",true)))
      detail=launcher::lang::tr(self_card?"lobby.card.finish_setup":"lobby.card.setup_unfinished");
    else detail=launcher::lang::tx(detail);   // "Online", "In game", "Friend request"...; any other text stays as is
    auto location=p->value("location",std::string()); if(!location.empty()) detail+="  /  "+location;
    { std::lock_guard<std::mutex> lock(mutex); auto it=pings.find(p->value("id",std::string())); if(it!=pings.end()) detail+="  /  "+std::to_string(it->second)+" ms"; }
    // Mods: "Open to Akaneia" in bold when they take those requests, "Has ACE" dimmed when installed
    // but not open, the custom ISO by name ("same ISO" when its content matches this player's).
    std::vector<TextRun> runs{{wide(detail),ui_small,ui_dim}};
    {
      const COLORREF faint=launcher::theme::mix(ui_dim,ui_panel,.35);
      const Json has=p->value("has",Json::object());
      for(const char* m:{"akaneia","ace"}) if(has.is_object() && has.count(m)) {
        const bool open_mod=open_to(*p,m);
        runs.push_back({L"  /  ",ui_small,ui_dim});
        runs.push_back({wide(launcher::lang::tx(open_mod?"Open to":"Has")+" "+(std::string(m)=="akaneia"?"Akaneia":"ACE")),
                        open_mod?ui_small_bold:ui_small,open_mod?ui_text:faint});
      }
      const Json iso=p->value("iso",Json::object());
      if(iso.is_object() && iso.count("n") && open_to(*p,"custom")) {
        runs.push_back({L"  /  ",ui_small,ui_dim});
        runs.push_back({wide(iso.value("n",std::string())),ui_small_bold,ui_text});
        if(!self_card && !custom_hash.empty() && iso.value("h",std::string())==custom_hash)
          runs.push_back({wide(" ("+launcher::lang::tx("same ISO")+")"),ui_small,ui_dim});
      }
      if(!open_to(*p,"vanilla") && p->count("open")) runs.push_back({wide("  /  "+launcher::lang::tx("mods only")),ui_small,ui_dim});
    }
    line.top+=U(22); line.bottom+=U(22); ink_runs(d->hDC,runs,line);
    if(!p->value("stocks",Json::array()).empty()) {
      std::string stocks=launcher::lang::tx("Stocks:")+" "; for(auto n:(*p)["stocks"]) stocks+=std::to_string(n.get<int>())+" ";
      line.top+=U(21); line.bottom+=U(21); ink(d->hDC,wide(stocks),line,ui_small,ui_dim);
    } else if(d->itemState&ODS_SELECTED && id==PLAYERS) {
      std::string record="W "+std::to_string(p->value("wins",0))+"  /  L "+std::to_string(p->value("losses",0));
      std::string code=p->value("code",std::string());
      if(!code.empty()) record+="   "+code;
      line.top+=U(21);line.bottom+=U(21);
      ink(d->hDC,wide(record),line,ui_small,launcher::theme::glow());
    } else {
      int x=line.left;
      for(auto c:mains) { int n=c.get<int>(); if(n>=0&&n<26&&stock_icons[n]) { centered_icon(d->hDC,stock_icons[n],RECT{x,r.top+U(51),x+U(18),r.top+U(69)},18,stock_icon_offsets[n]); x+=U(26); } }
    }
  } else {
    int len=(int)SendMessageW(d->hwndItem,LB_GETTEXTLEN,d->itemID,0); std::wstring s(std::max(0,len)+1,0);
    if(len>=0) SendMessageW(d->hwndItem,LB_GETTEXT,d->itemID,(LPARAM)s.data());
    InflateRect(&r,-U(8),-U(5)); ink(d->hDC,s,r,ui_small,ui_text,DT_LEFT|DT_WORDBREAK|DT_END_ELLIPSIS);
  }
}
// print: a device context to draw into instead of the window (WM_PRINTCLIENT, for test captures).
void paint_lobby(HWND w,HDC print=nullptr) {
  PAINTSTRUCT ps{}; HDC target=print?print:BeginPaint(w,&ps); RECT r{}; GetClientRect(w,&r);
  HDC dc=CreateCompatibleDC(target); HBITMAP bmp=CreateCompatibleBitmap(target,r.right,r.bottom); auto old=SelectObject(dc,bmp);
  box(dc,r,ui_bg);
  RECT title{U(24),U(24),r.right-U(148),U(54)}; ink(dc,launcher::lang::txw(L"Multiplayer Lobby"),title,ui_title,ui_text);
  const int right=r.right-U(316), bottom=r.bottom-U(94);
  RECT left{U(24),U(110),right-U(18),bottom}, players{right,U(110),r.right-U(24),bottom};
  for(auto card:{left,players}) { box(dc,card,ui_border,12); InflateRect(&card,-1,-1); box(dc,card,ui_panel,12); }
  const wchar_t* headings[]={L"Lobby chat",L"Friends",L"Match history",L"Your profile"};
  RECT heading{left.left+U(16),left.top+U(12),left.right-U(16),left.top+U(42)};
  room_tab_hits.clear(); room_close_hits.clear(); room_tab_ids.clear();
  if(lobby_tab==0 && !private_rooms.empty()) paint_room_tabs(dc,left);
  else ink(dc,launcher::lang::txw(headings[lobby_tab]),heading,ui_name,ui_text);
  if(lobby_tab==3) {
    RECT track{U(slider_left),U(slider_y),U(slider_right),U(slider_y+5)}; box(dc,track,ui_border,5);
    RECT amount=track; amount.right=amount.left+(amount.right-amount.left)*sound_volume/100; box(dc,amount,launcher::theme::accent,5);
    int knob=track.left+(track.right-track.left)*sound_volume/100;
    RECT circle{knob-U(7),track.top-U(5),knob+U(7),track.bottom+U(5)}; box(dc,circle,launcher::theme::glow(),14);
  }
  for(int id:{CHAT,NAME,CODE,LOCATION,URL,FRIEND_CODE}) {
    HWND h=GetDlgItem(window,id); if(!(GetWindowLongPtrW(h,GWL_STYLE)&WS_VISIBLE)) continue;
    RECT field{}; GetWindowRect(h,&field); MapWindowPoints(nullptr,w,(POINT*)&field,2); InflateRect(&field,U(5),U(5));
    box(dc,field,ui_border,7); InflateRect(&field,-1,-1); box(dc,field,ui_field,7);
  }
  auto empty_state=[&](RECT area,const wchar_t* heading,const wchar_t* detail,bool social) {
    int x=area.left+U(24), y=area.top+U(28);
    RECT badge{x,y,x+U(46),y+U(46)}; box(dc,badge,RGB(32,35,59),16);
    HPEN pen=CreatePen(PS_SOLID,U(2),launcher::theme::glow()); auto oldPen=SelectObject(dc,pen); auto oldBrush=SelectObject(dc,GetStockObject(NULL_BRUSH));
    if(social) { Ellipse(dc,x+U(16),y+U(9),x+U(30),y+U(23)); RoundRect(dc,x+U(9),y+U(26),x+U(37),y+U(39),U(12),U(12)); }
    else { RoundRect(dc,x+U(10),y+U(11),x+U(36),y+U(31),U(6),U(6)); MoveToEx(dc,x+U(16),y+U(31),nullptr); LineTo(dc,x+U(16),y+U(37)); LineTo(dc,x+U(23),y+U(31)); }
    SelectObject(dc,oldBrush); SelectObject(dc,oldPen); DeleteObject(pen);
    RECT title{x,y+U(64),area.right-U(20),y+U(128)}; ink(dc,wrap_words(dc,ui_title,launcher::lang::txw(heading),title.right-title.left),title,ui_title,ui_text,DT_LEFT|DT_WORDBREAK);
    RECT description{x,y+U(132),area.right-U(24),y+U(196)}; ink(dc,wrap_words(dc,ui_font,launcher::lang::txw(detail),description.right-description.left),description,ui_font,ui_dim,DT_LEFT|DT_WORDBREAK);
  };
  if(rows.empty()) { RECT area=players; area.top+=U(46); empty_state(area,L"Find your next match",L"Go online to discover players.\nTheir location and mains appear here.",true); }
  if(lobby_tab==0&&chat_messages.empty()&&!active_room.empty()) {
    // An open private room nobody has written in yet: who it is with, and that it is private.
    const Json* room=find_room(active_room);
    RECT hint{left.left+U(24),left.top+U(64),left.right-U(24),left.top+U(128)};
    const std::wstring text=launcher::lang::trw("lobby.pm.empty",{{"name",room?room->value("name",std::string()):std::string()}});
    ink(dc,wrap_words(dc,ui_font,text,hint.right-hint.left),hint,ui_font,ui_dim,DT_LEFT|DT_WORDBREAK);
  } else if(lobby_tab==0&&chat_messages.empty()) { RECT area=left; area.top+=U(46); empty_state(area,L"Ready for a few games?",L"Join the lobby, say hello, and send a match request when you're ready.",false); }
  if(lobby_tab==1&&friends.empty()) { RECT area=left; area.top+=U(46); empty_state(area,L"Friends, one click away.",L"Add a player to see when they're online and ready for another game.",true); }
  if(lobby_tab==2&&history.empty()) { RECT area=left; area.top+=U(68); empty_state(area,L"Match history",L"Completed lobby games appear here, with your wins and losses.",false); }
  BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY); SelectObject(dc,old); DeleteObject(bmp); DeleteDC(dc);
  if(!print) EndPaint(w,&ps);
}
