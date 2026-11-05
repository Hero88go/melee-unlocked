// Exercise the production badge renderer with real ImGui modal draw ordering.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "coming_soon_overlay.h"
#include "practice_menu.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <cfloat>
#include <cstdio>
#include <cstdlib>

// Wall-clock scheduling must not expire the menu heartbeat during a slow build.
// Only this isolated target supplies the clock; the shipped renderer uses steady_clock.
static int64_t test_clock_ms=100000;
int64_t melee_coming_soon_test_clock_ms() { return test_clock_ms; }

#define CHECK(x) do { if (!(x)) { std::fprintf(stderr,"line %d: %s\n",__LINE__,#x); std::exit(1); } } while (0)

// The font provider supplies glyphs only; all badge geometry and layer selection
// come from the production coming_soon_overlay.cpp.
namespace practice_menu {
float text_width(float px, const char* text) { return ImGui::GetFont()->CalcTextSizeA(px,FLT_MAX,0,text).x; }
void text(ImDrawList* dl,float x,float y,float px,uint32_t color,const char* s,float,bool)
{ dl->AddText(nullptr,px,ImVec2(x,y),color,s); }
}

void frame(const char* dialog, bool settled=true)
{
    ImGui::NewFrame();
    ImGui::SetNextWindowPos(ImVec2(200,150),ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(240,180),ImGuiCond_Always);
    ImGui::Begin("Settings",nullptr,ImGuiWindowFlags_NoSavedSettings);
    ImGui::TextUnformatted("Settings content");
    if (dialog) {
        ImGui::OpenPopup(dialog);
        if (ImGui::BeginPopupModal(dialog,nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::TextUnformatted("The current match will end.");
            ImGui::Button("Cancel");
            ImGui::EndPopup();
        }
    }
    ImGui::End();
    // Deliberately call last: the badges must stay below settings and dialogs
    // regardless of the order in which their widgets were submitted.
    coming_soon_overlay::draw(0,0,640,480);
    ImDrawList* badges=ImGui::GetBackgroundDrawList();
    ImDrawList* foreground=ImGui::GetForegroundDrawList();
    CHECK(badges->VtxBuffer.Size>0 && foreground->VtxBuffer.Size==0);
    ImGui::Render();
    const ImDrawData* data=ImGui::GetDrawData();
    CHECK(data->CmdListsCount>=2 && data->CmdLists[0]==badges);
    if (dialog && settled) {
        ImGuiWindow* modal=ImGui::FindWindowByName(dialog);
        CHECK(modal && modal->DrawList->VtxBuffer.Size>0);
        bool found=false;
        for (int i=1;i<data->CmdListsCount;++i) found|=data->CmdLists[i]==modal->DrawList;
        CHECK(found);
    }
}

int main()
{
    ImGui::CreateContext();
    ImGuiIO& io=ImGui::GetIO();
    io.DisplaySize=ImVec2(640,480); io.DeltaTime=1.0f/60;
    io.IniFilename=nullptr; io.LogFilename=nullptr;
    io.Fonts->Build();
    // Keep the menu heartbeat alive until the badges finish popping in.
    for (int i=0;i<12;++i) {
        coming_soon_overlay::showing();
        test_clock_ms+=60;
    }
    frame(nullptr);
    // ImGui measures a new auto-sized popup while hidden on its first frame.
    coming_soon_overlay::showing(); frame("Restart game?",false);
    coming_soon_overlay::showing(); frame("Restart game?");
    ImGui::DestroyContext();
    std::puts("PASS: badges stay below settings and the restart confirmation");
}
