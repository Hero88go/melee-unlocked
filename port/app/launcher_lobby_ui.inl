// Native, themed lobby presentation. Network state remains in launcher_lobby.cpp.
const COLORREF ui_bg=RGB(19,26,42), ui_panel=RGB(12,18,33), ui_border=RGB(43,54,82);
const COLORREF ui_text=RGB(231,236,245), ui_dim=RGB(141,155,181);
HFONT ui_font{},ui_small{},ui_title{},ui_name{};
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
    ink(dc,L"Add emoji",RECT{U(14),U(6),U(180),U(28)},ui_name,ui_text);
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
  HWND h=CreateWindowExW(0,type,caption,WS_CHILD|WS_CLIPSIBLINGS|style,0,0,0,0,w,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
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
  place(PLAYERS,right+12,162,268,bottom-174,!rows.empty());
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
  place(REMOVE_FRIEND,38,bottom-44,120,30,lobby_tab==1&&!incoming&&choice>=0&&!adding_friend);
  place(RECORD,38,168,left_width-28,24,lobby_tab==2);
  place(HISTORY,38,202,left_width-28,bottom-216,lobby_tab==2&&!history.empty());
  const bool profile=lobby_tab==3;
  for(int i=0;i<3;++i) {
    place(PROFILE_NAME+i,40,166+i*64,left_width-32,18,profile);
    place(NAME+i,44,194+i*64,left_width-40,24,profile);
  }
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
  place(STATUS,24,bottom+58,width-48,18);
  for(int id:{EMPTY_PLAYERS,EMPTY_FRIENDS,EMPTY_CHAT}) ShowWindow(GetDlgItem(window,id),SW_HIDE);

}
Json profile_config() {
  Json cfg;
  { std::lock_guard<std::mutex> lock(mutex); cfg=config; }
  const bool prior_peer=peer_mode(cfg);
  cfg["mode"]="peer";
  cfg["url"]=prior_peer?text(URL):std::string();
  cfg["name"]=text(NAME); cfg["code"]=account_code; cfg["location"]=text(LOCATION);
  cfg["build"]=build; cfg["ready"]=can_play; cfg["mains"]=selected_mains;
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
    bool primary=id==REQUEST||id==GO_ONLINE||id==ACCEPT||active;
    bool enabled=!(d->itemState&ODS_DISABLED);
    box(d->hDC,r,id==GO_ONLINE||id==REQUEST||id==ADD_FRIEND||id==TAB_PROFILE||id==TAB_HISTORY||id==TAB_FRIENDS||id==TAB_CHAT?ui_bg:ui_panel);
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
    if((d->itemState&ODS_FOCUS) && !(d->itemState&ODS_NOFOCUSRECT)) { InflateRect(&r,-4,-4); DrawFocusRect(d->hDC,&r); }
    return;
  }
  box(d->hDC,r,ui_panel);
  box(d->hDC,r,d->itemState&ODS_SELECTED?launcher::theme::mix(launcher::theme::accent,ui_panel,.82):ui_panel,6);
  if(d->itemID==(UINT)-1) return;
  if(id==CHATLOG && d->itemID<chat_messages.size()) {
    const auto& message=chat_messages[d->itemID];
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
    auto name=wide(p->value("name",std::string("Player"))+(p->value("is_self",false)?"  (You)":""));
    auto version=p->value("build",std::string());
    if(auto colon=version.find(':'); colon!=std::string::npos) version.resize(colon);
    if(!version.empty() && version[0]!='v' && version[0]!='V') version="v"+version;
    RECT line{r.left+U(54),r.top+U(8),r.right-U(68),r.top+U(26)};
    ink(d->hDC,name,line,ui_name,ui_text);
    RECT version_rect{r.right-U(66),r.top+U(8),r.right-U(10),r.top+U(26)};
    ink(d->hDC,wide(version),version_rect,ui_small,launcher::theme::glow(),DT_RIGHT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    line.right=r.right-U(12);
    std::string detail=p->value("incoming",false)?"Friend request":p->value("status",std::string("Online"));
    auto location=p->value("location",std::string()); if(!location.empty()) detail+="  /  "+location;
    { std::lock_guard<std::mutex> lock(mutex); auto it=pings.find(p->value("id",std::string())); if(it!=pings.end()) detail+="  /  "+std::to_string(it->second)+" ms"; }
    line.top+=U(22); line.bottom+=U(22); ink(d->hDC,wide(detail),line,ui_small,ui_dim);
    if(!p->value("stocks",Json::array()).empty()) {
      std::string stocks="Stocks: "; for(auto n:(*p)["stocks"]) stocks+=std::to_string(n.get<int>())+" ";
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
void paint_lobby(HWND w) {
  PAINTSTRUCT ps{}; HDC target=BeginPaint(w,&ps); RECT r{}; GetClientRect(w,&r);
  HDC dc=CreateCompatibleDC(target); HBITMAP bmp=CreateCompatibleBitmap(target,r.right,r.bottom); auto old=SelectObject(dc,bmp);
  box(dc,r,ui_bg);
  RECT title{U(24),U(24),r.right-U(148),U(54)}; ink(dc,L"Multiplayer Lobby",title,ui_title,ui_text);
  const int right=r.right-U(316), bottom=r.bottom-U(94);
  RECT left{U(24),U(110),right-U(18),bottom}, players{right,U(110),r.right-U(24),bottom};
  for(auto card:{left,players}) { box(dc,card,ui_border,12); InflateRect(&card,-1,-1); box(dc,card,ui_panel,12); }
  const wchar_t* headings[]={L"Lobby chat",L"Friends",L"Match history",L"Your profile"};
  RECT heading{left.left+U(16),left.top+U(12),left.right-U(16),left.top+U(42)}; ink(dc,headings[lobby_tab],heading,ui_name,ui_text);
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
    RECT title{x,y+U(64),area.right-U(20),y+U(114)}; ink(dc,heading,title,ui_title,ui_text,DT_LEFT|DT_WORDBREAK);
    RECT description{x,y+U(124),area.right-U(24),y+U(182)}; ink(dc,detail,description,ui_font,ui_dim,DT_LEFT|DT_WORDBREAK);
  };
  if(rows.empty()) { RECT area=players; area.top+=U(46); empty_state(area,L"Find your next match",L"Go online to discover players.\nTheir location and mains appear here.",true); }
  if(lobby_tab==0&&chat_messages.empty()) { RECT area=left; area.top+=U(46); empty_state(area,L"Ready for a few games?",L"Join the lobby, say hello, and send a match request when you're ready.",false); }
  if(lobby_tab==1&&friends.empty()) { RECT area=left; area.top+=U(46); empty_state(area,L"Friends, one click away.",L"Add a player to see when they're online and ready for another game.",true); }
  if(lobby_tab==2&&history.empty()) { RECT area=left; area.top+=U(68); empty_state(area,L"Match history",L"Completed lobby games appear here, with your wins and losses.",false); }
  BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY); SelectObject(dc,old); DeleteObject(bmp); DeleteDC(dc); EndPaint(w,&ps);
}
