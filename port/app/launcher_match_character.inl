// The accepted lobby match uses the game's existing Direct handoff. This small picker
// supplies its character argument, then the game starts automatically.
struct MatchPicker { int selected=2; bool done=false; std::string opponent; };
LRESULT CALLBACK match_picker_proc(HWND w,UINT message,WPARAM wp,LPARAM lp) {
  auto* choice=(MatchPicker*)GetWindowLongPtrW(w,GWLP_USERDATA);
  if(message==WM_NCCREATE) {
    SetWindowLongPtrW(w,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCTW*)lp)->lpCreateParams);
    return DefWindowProcW(w,message,wp,lp);
  }
  if(message==WM_ERASEBKGND) return 1;
  if(message==WM_PAINT) {
    PAINTSTRUCT paint{}; HDC dc=BeginPaint(w,&paint); RECT client{}; GetClientRect(w,&client);
    vgrad(dc,client,C_CONTENT_TOP,C_CONTENT_BOT);
    draw_text(dc,L"Choose your character",LR(20,15,420,31),g_font_big,C_TEXT,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    draw_text(dc,widen(launcher::lang::fill(launcher::lang::tx("Playing {name}"),launcher::lang::Args{{"name",choice?choice->opponent:launcher::lang::tx("your opponent")}}) +
                       "  |  " + launcher::lang::tx("Click a stock icon to launch Direct")),
              LR(20,49,420,26),g_font_small,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS);
    for(int i=0;i<26;++i) {
      RECT cell=LR(20+(i%9)*46,84+(i/9)*52,40,44);
      bool active=choice&&choice->selected==i;
      round_rect(dc,cell,7,active?C_ACC_HI:C_BTN,active?C_ACC_LO:C_BTN,active?NO_FILL:C_BTN_BORDER);
      HICON icon=(HICON)LoadImageW(GetModuleHandleW(nullptr),MAKEINTRESOURCEW(100+i),IMAGE_ICON,S(30),S(30),LR_SHARED);
      if(icon) DrawIconEx(dc,cell.left+S(5),cell.top+S(7),icon,S(30),S(30),0,nullptr,DI_NORMAL);
    }
    draw_text(dc,L"Click a character to start. Your opponent's code is filled in automatically.",
              LR(20,246,420,26),g_font_small,C_DIM,DT_LEFT|DT_VCENTER|DT_SINGLELINE);
    EndPaint(w,&paint); return 0;
  }
  if(message==WM_LBUTTONUP && choice) {
    const int x=MulDiv(GET_X_LPARAM(lp),96,g_dpi),y=MulDiv(GET_Y_LPARAM(lp),96,g_dpi);
    if(x>=20&&x<20+9*46&&y>=84&&y<84+3*52) {
      int col=(x-20)/46,row=(y-84)/52,index=row*9+col;
      if(index<26 && (x-20)%46<40 && (y-84)%52<44) {
        choice->selected=index; choice->done=true; DestroyWindow(w); return 0;
      }
    }
  }
  if((message==WM_KEYDOWN && wp==VK_RETURN) || message==WM_CLOSE) {
    if(choice) choice->done=true; DestroyWindow(w); return 0;
  }
  return DefWindowProcW(w,message,wp,lp);
}
int choose_match_character(const launcher::lobby::Match& match) {
  static bool registered=false;
  if(!registered) {
    WNDCLASSW wc{}; wc.lpfnWndProc=match_picker_proc; wc.hInstance=GetModuleHandleW(nullptr);
    wc.lpszClassName=L"MeleeUnlockedMatchPicker"; wc.hCursor=LoadCursorW(nullptr,IDC_HAND);
    registered=RegisterClassW(&wc)!=0 || GetLastError()==ERROR_CLASS_ALREADY_EXISTS;
  }
  if(!registered) return match.character;
  MatchPicker selection{match.character,false,match.opponent};
  RECT parent{}; GetWindowRect(g_main,&parent);
  RECT frame{0,0,S(460),S(291)}; AdjustWindowRect(&frame,WS_POPUP|WS_CAPTION,FALSE);
  const int width=frame.right-frame.left,height=frame.bottom-frame.top;
  HWND picker=CreateWindowExW(WS_EX_DLGMODALFRAME,L"MeleeUnlockedMatchPicker",launcher::lang::txw(L"Match accepted").c_str(),
    WS_POPUP|WS_CAPTION,(parent.left+parent.right-width)/2,(parent.top+parent.bottom-height)/2,
    width,height,g_main,nullptr,GetModuleHandleW(nullptr),&selection);
  if(!picker) return match.character;
  EnableWindow(g_main,FALSE); ShowWindow(picker,SW_SHOW); SetForegroundWindow(picker);
  MSG msg{};
  int next=0;
  while(!selection.done && IsWindow(picker) && (next=GetMessageW(&msg,nullptr,0,0))>0) {
    TranslateMessage(&msg); DispatchMessageW(&msg);
  }
  if(next==0) PostQuitMessage((int)msg.wParam);
  if(IsWindow(picker)) DestroyWindow(picker);
  EnableWindow(g_main,TRUE); SetForegroundWindow(g_main);
  return selection.selected;
}
