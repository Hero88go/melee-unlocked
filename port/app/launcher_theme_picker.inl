// Compact HSV wheel popover. Hex input accepts the exact RGB color as an alternative.
HWND g_theme_picker=nullptr, g_theme_hex=nullptr;
COLORREF g_theme_before{}, g_theme_preview{};
double g_theme_h=268, g_theme_s=.65, g_theme_v=.93;
bool g_theme_drag=false;
constexpr int THEME_W=268, THEME_H=324, THEME_CX=134, THEME_CY=156, THEME_RADIUS=78;
std::wstring theme_hex(COLORREF color) {
  wchar_t out[16]{}; swprintf_s(out,L"#%02X%02X%02X",GetRValue(color),GetGValue(color),GetBValue(color)); return out;
}
void theme_edit_color() {
  if(!g_theme_hex) return;
  SetWindowTextW(g_theme_hex,theme_hex(g_theme_preview).c_str());
  SendMessageW(g_theme_hex,EM_SETSEL,0,-1);
}
bool theme_parse_hex(COLORREF& out) {
  wchar_t input[32]{}; GetWindowTextW(g_theme_hex,input,32);
  const wchar_t* digits=input[0]==L'#'?input+1:input;
  if(wcslen(digits)!=6) return false;
  for(int i=0;i<6;++i) if(!iswxdigit(digits[i])) return false;
  const unsigned rgb=wcstoul(digits,nullptr,16);
  out=RGB((rgb>>16)&255,(rgb>>8)&255,rgb&255); return true;
}
void theme_refresh() {
  InvalidateRect(g_main,nullptr,FALSE);
  for(HWND h:g_play) if(h) InvalidateRect(h,nullptr,FALSE);
  launcher::lobby::refresh_theme();
}
void theme_preview_color(COLORREF color) {
  g_theme_preview=color; launcher::theme::accent=color;
  theme_edit_color(); theme_refresh();
  InvalidateRect(g_theme_picker,nullptr,FALSE);
}
void theme_pick_at(int x,int y) {
  double dx=x-THEME_CX,dy=THEME_CY-y, distance=std::sqrt(dx*dx+dy*dy);
  if(distance<=THEME_RADIUS+2 && distance>=3) {
    g_theme_h=std::fmod(std::atan2(dy,dx)*180/3.141592653589793+360,360.0);
    g_theme_s=std::clamp(distance/THEME_RADIUS,0.0,1.0);
    theme_preview_color(launcher::theme::hsv(g_theme_h,g_theme_s,g_theme_v));
  } else if(y>=246 && y<=261 && x>=44 && x<=224) {
    g_theme_v=std::clamp((x-44)/180.0,0.0,1.0);
    theme_preview_color(launcher::theme::hsv(g_theme_h,g_theme_s,g_theme_v));
  }
}
void theme_wheel(HDC dc,int center_x,int center_y,int radius,bool compact=false) {
  const int side=radius*2+1;
  std::vector<uint32_t> pixels(side*side,0);
  for(int y=0;y<side;++y) for(int x=0;x<side;++x) {
    double dx=x-radius,dy=radius-y,dist=std::sqrt(dx*dx+dy*dy);
    const int alpha=int(std::clamp(radius+.5-dist,0.0,1.0)*255+.5);
    if(!alpha) continue;
    double hue=std::fmod(std::atan2(dy,dx)*180/3.141592653589793+360,360.0);
    COLORREF rgb=launcher::theme::hsv(hue,compact?std::max(.42,dist/radius):dist/radius,1);
    pixels[y*side+x]=uint32_t(GetBValue(rgb)*alpha/255) |
      (uint32_t(GetGValue(rgb)*alpha/255)<<8) | (uint32_t(GetRValue(rgb)*alpha/255)<<16) |
      (uint32_t(alpha)<<24);
  }
  BITMAPINFO bmi{}; bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER); bmi.bmiHeader.biWidth=side;
  bmi.bmiHeader.biHeight=-side; bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32; bmi.bmiHeader.biCompression=BI_RGB;
  HDC mem=CreateCompatibleDC(dc);void* bits=nullptr;
  HBITMAP image=CreateDIBSection(dc,&bmi,DIB_RGB_COLORS,&bits,nullptr,0);
  if(image && bits) {
    std::memcpy(bits,pixels.data(),pixels.size()*sizeof(uint32_t));
    HGDIOBJ old=SelectObject(mem,image);
    BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
    AlphaBlend(dc,S(center_x-radius),S(center_y-radius),S(side),S(side),mem,0,0,side,side,blend);
    SelectObject(mem,old);
  }
  if(image) DeleteObject(image);DeleteDC(mem);
}
void theme_paint_picker(HWND hwnd) {
  PAINTSTRUCT ps{}; HDC dc=BeginPaint(hwnd,&ps);
  RECT all{0,0,S(THEME_W),S(THEME_H)}; fill(dc,all,C_CONTENT_BOT);
  round_rect(dc,LR(1,1,THEME_W-2,THEME_H-2),12,C_FIELD,C_FIELD,C_FIELD_BORDER);
  draw_text(dc,L"Launcher color",LR(20,15,180,24),g_font_nav,C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  draw_text(dc,L"Pick a color or enter a hex code",LR(20,42,232,20),g_font_small,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
  theme_wheel(dc,THEME_CX,THEME_CY,THEME_RADIUS);
  const double rad=g_theme_h*3.141592653589793/180.0;
  int dot_x=THEME_CX+int(std::cos(rad)*g_theme_s*THEME_RADIUS), dot_y=THEME_CY-int(std::sin(rad)*g_theme_s*THEME_RADIUS);
  round_rect(dc,LR(dot_x-6,dot_y-6,12,12),6,RGB(255,255,255),RGB(255,255,255),RGB(22,27,42));
  for(int x=44;x<224;++x) {
    RECT bar=LR(x,246,1,15);
    fill(dc,bar,launcher::theme::hsv(g_theme_h,g_theme_s,(x-44)/180.0));
  }
  RECT marker=LR(44+int(g_theme_v*180)-2,243,4,21); fill(dc,marker,C_TEXT);
  round_rect(dc,LR(20,272,105,30),6,C_FIELD,C_FIELD,C_FIELD_BORDER);
  round_rect(dc,LR(137,268,109,35),7,launcher::theme::top(),launcher::theme::bottom(),NO_FILL);
  draw_text(dc,L"Apply",LR(137,268,109,35),g_font,launcher::theme::on_accent(),DT_CENTER|DT_VCENTER|DT_SINGLELINE);
  EndPaint(hwnd,&ps);
}
LRESULT CALLBACK theme_picker_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp) {
  switch(msg) {
    case WM_CREATE:
      g_theme_hex=CreateWindowExW(0,L"EDIT",theme_hex(g_theme_preview).c_str(),WS_CHILD|WS_VISIBLE|ES_AUTOHSCROLL,
                                   S(30),S(277),S(85),S(20),hwnd,(HMENU)1,GetModuleHandleW(nullptr),nullptr);
      SendMessageW(g_theme_hex,WM_SETFONT,(WPARAM)g_font,TRUE); SetWindowTheme(g_theme_hex,L"DarkMode_Explorer",nullptr);
      return 0;
    case WM_PAINT: theme_paint_picker(hwnd); return 0;
    case WM_CTLCOLOREDIT:
      SetTextColor((HDC)wp,C_TEXT); SetBkColor((HDC)wp,C_FIELD); return (LRESULT)g_br_field;
    case WM_LBUTTONDOWN: {
      int x=MulDiv(GET_X_LPARAM(lp),96,g_dpi),y=MulDiv(GET_Y_LPARAM(lp),96,g_dpi);
      if(x>=137&&x<=246&&y>=268&&y<=303) {
        COLORREF entered{};
        if(theme_parse_hex(entered)) { launcher::theme::accent=entered; save_ini(); theme_refresh(); g_theme_before=entered; DestroyWindow(hwnd); }
        else { MessageBeep(MB_ICONWARNING); SetFocus(g_theme_hex); }
      } else { theme_pick_at(x,y); g_theme_drag=true; SetCapture(hwnd); }
      return 0;
    }
    case WM_MOUSEMOVE:
      if(g_theme_drag) theme_pick_at(MulDiv(GET_X_LPARAM(lp),96,g_dpi),MulDiv(GET_Y_LPARAM(lp),96,g_dpi));
      return 0;
    case WM_LBUTTONUP: if(g_theme_drag) {g_theme_drag=false;ReleaseCapture();} return 0;
    case WM_KEYDOWN: if(wp==VK_ESCAPE) {DestroyWindow(hwnd);return 0;} break;
    case WM_CLOSE: DestroyWindow(hwnd); return 0;
    case WM_ACTIVATE: if(LOWORD(wp)==WA_INACTIVE) {DestroyWindow(hwnd);return 0;} break;
    case WM_DESTROY:
      launcher::theme::accent=g_theme_before; theme_refresh(); g_theme_picker=nullptr;g_theme_hex=nullptr;g_theme_drag=false;
      return 0;
  }
  return DefWindowProcW(hwnd,msg,wp,lp);
}
void open_theme_picker() {
  if(g_theme_picker) { SetForegroundWindow(g_theme_picker); return; }
  static bool registered=false;
  if(!registered) {
    WNDCLASSW wc{}; wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=theme_picker_proc;
    wc.lpszClassName=L"MeleeUnlockedThemePicker";wc.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    registered=RegisterClassW(&wc)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
  }
  g_theme_before=g_theme_preview=launcher::theme::accent;
  launcher::theme::to_hsv(g_theme_preview,g_theme_h,g_theme_s,g_theme_v);
  RECT anchor{};GetWindowRect(g_play[6],&anchor);
  RECT work{};SystemParametersInfoW(SPI_GETWORKAREA,0,&work,0);
  int px=std::clamp(int(anchor.right-S(THEME_W)),int(work.left),int(work.right-S(THEME_W)));
  int py=std::clamp(int(anchor.top-S(THEME_H)-S(8)),int(work.top),int(work.bottom-S(THEME_H)));
  g_theme_picker=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST,L"MeleeUnlockedThemePicker",L"Theme",
    WS_POPUP,px,py,S(THEME_W),S(THEME_H),g_main,nullptr,GetModuleHandleW(nullptr),nullptr);
  if(g_theme_picker) { ShowWindow(g_theme_picker,SW_SHOWNORMAL);UpdateWindow(g_theme_picker);SetFocus(g_theme_hex); }
}
