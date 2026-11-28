"""Execute the actual gated menu and Doubles game callbacks without disc assets."""
from pathlib import Path
import os
import sys
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


# The page's mpTicket body for a native Doubles ticket (claude/v2 web/src/components/Player.tsx), and the
# site's Doubles request check (web/src/lib/native-teams.ts, the mu_doubles branch). Undefined keys drop.
def page_body(msg):
    body={'slug':'melee-unlocked','native_build':'build-1'}
    if msg.get('teamsLobby')==1:
        body.update({'teams_lobby':1,'party_room':msg.get('party_room'),'party_search':msg.get('partySearch')})
        if msg.get('mu_doubles')==1: body['mu_doubles']=1
        if msg.get('mu_doubles')==1 and msg.get('local_players')==2 and not msg.get('party'): body['local_players']=2
    body.update({'players':msg.get('players'),'mode':msg.get('mode'),'lobby':msg.get('lobby')})
    if msg.get('party'): body['party']=msg['party']
    teams=msg.get('teamsLobby')==1 and msg.get('mode')=='teams'
    private=msg.get('mu_doubles')==1 and msg.get('ranked') is not True and msg.get('queue')=='private' and isinstance(msg.get('room'),str)
    body['queue']=('private' if private else 'casual') if teams else 'casual'
    if isinstance(msg.get('room'),str): body['room']=msg['room']
    return {k:v for k,v in body.items() if v is not None or k=='party_search' and 'partySearch' in msg}
def route_kind(body):
    import re
    room=re.compile(r'^[a-z0-9]{6,12}$')  # ROOM_CODE_RE
    if body.get('mu_doubles')!=1 or body.get('teams_lobby')!=1 or body.get('mode')!='teams' or \
       not isinstance(body.get('native_build'),str) or not body.get('slug') or body.get('ranked') is True or \
       any(body.get(k) is not None for k in ('dev','world','direct','spectate','watch','broadcast','doubles','party_size',
           'party_local','match_room','wager','tourney','tr','tournament','nativeTournamentSeat','server','teams_invite')):
        return None
    l=body.get('lobby') or {}
    if l.get('version')!=1 or body.get('players')!=4: return None
    if body.get('room') is not None:
        ok=isinstance(body['room'],str) and room.match(body['room']) and body.get('queue')=='private' and \
           l.get('maxLocalPlayers')==2 and l.get('minPlayers') in (2,4) and \
           all(body.get(k) is None for k in ('party','party_room','party_search','local_players'))
        return ('home' if l['minPlayers']==2 else 'match') if ok else None
    s=body.get('party_search')
    if body.get('queue')!='casual' or l.get('minPlayers')!=4 or l.get('maxLocalPlayers')!=1 or \
       not isinstance(body.get('party_room'),str) or not room.match(body['party_room']) or \
       not isinstance(s,str) or not re.match(r'^[a-z0-9]{12,32}$',s) or \
       body.get('local_players') not in (None,2) or body.get('local_players')==2 and body.get('party') is not None:
        return None
    if body.get('party') is not None and (body['party'].get('n')!=2 or body['party'].get('id')!=s): return None
    return 'search'

class DoublesWiring(unittest.TestCase):
    def compile_run(self, source, enabled=True, extra=(), x86=False):
        with tempfile.TemporaryDirectory(prefix='doubles-') as directory:
            cpp=Path(directory)/'test.cpp'
            exe=Path(directory)/'test'
            cpp.write_text(source)
            command=['c++','-std=c++17','-pthread','-I'+str(ROOT/'sourceport/game/include'),
                     '-I'+str(ROOT/'port/runtime/hle'),'-I'+str(ROOT/'port/third_party'),'-I'+str(ROOT/'port/app'),
                     '-I'+str(ROOT/'port/runtime/abi')]
            if enabled: command+=['-DMELEE_DOUBLES=1']
            if x86 and sys.platform=='darwin': command+=['-arch','x86_64']
            if os.name=='nt': command+=['-include',str(Path(__file__).resolve().parent/'posix_fixture_compat.h')]
            subprocess.run(command+[str(cpp),*[str(ROOT/p) for p in extra],'-o',str(exe)],check=True)
            return subprocess.run([str(exe)],check=True,capture_output=True,text=True).stdout

    def test_menu_on_and_off_with_optional_tournament(self):
        source=r'''
#include <cassert>
enum { MU_SLP_MODE_RANKED, MU_SLP_MODE_UNRANKED, MU_SLP_MODE_DIRECT, MU_SLP_MODE_TEAMS,
MU_SLP_MODE_PARTY, MU_SLP_OPT_LOGIN, MU_SLP_OPT_LOGOUT, MU_SLP_OPT_UPDATE, MU_SLP_OPT_FRIENDS,
MU_SLP_OPT_SLIPPI, MU_SLP_OPT_TOURNAMENT, MU_SLP_OPT_DOUBLES };
#include "mu_online_menu.h"
int main() {
  for(int tournament=0;tournament<2;++tournament) {
    tournament_offered=tournament;
    int doubles=0,slippi=0,tourney=0;
    for(int row=0;row<mu_online_menu_count(1,0);++row) {
      const int opt=mu_online_menu_option(1,0,row);
      doubles+=opt==MU_SLP_OPT_DOUBLES; slippi+=opt==MU_SLP_OPT_SLIPPI;
      tourney+=opt==MU_SLP_OPT_TOURNAMENT;
      assert(mu_online_menu_row(1,0,opt)==row);
    }
#ifdef MELEE_DOUBLES
    const int built=1;   // the YouGame page's Doubles row (back 11-21)
#else
    const int built=0;
#endif
    assert(doubles==built); assert(mu_online_menu_count(1,0)==3+built+tournament);
    assert(slippi==0); assert(tourney==tournament);
    assert(mu_online_menu_count(1,1)==4);
    assert(mu_online_menu_option(1,1,0)==MU_SLP_MODE_DIRECT);   // Unranked is the second row (11-19)
    assert(mu_online_menu_option(1,1,1)==MU_SLP_MODE_UNRANKED);
    assert(mu_online_menu_option(1,1,2)==MU_SLP_MODE_TEAMS);
  }
}
'''
        self.compile_run(source)
        self.compile_run(source,False)

    def test_production_fixed_rules_css_and_stage_bypass(self):
        game=(ROOT/'sourceport/game/shim/mu_doubles.c').read_text()
        game=re.sub(r'^#include[^\n]*\n','',game,flags=re.M)
        css=(ROOT/'sourceport/game/include/mu_doubles_css.inc').read_text()
        css=re.sub(r'^#include[^\n]*\n','',css,flags=re.M)
        stubs=r'''
#include <cassert>
#include <cstring>
using u8=unsigned char;
enum { GM_VS=2,GM_MENU=1,GM_COUNT=99,Gm_PKind_Human=0,Gm_PKind_Cpu=1,VS_MELEE=0,GS_CSS=1,GS_VS=2,GS_RESULTS=3,
GM_GAMEMODESTATE_TERMINATE=-1,lbDvdPreload_3=3 };
struct Rules { int is_teams=0,stkind=0,stocks=0,minutes=0,items=0,friendly_fire=0; };
struct Player { int team=-1,slot_type=3,stocks=0,ckind=9,color=2; };
struct Start { Rules rules; Player players[4]; };
struct VsModeData { Start start; };
struct CSSData { VsModeData vs; int pending_scene_change=0,match_type=0; };
struct Info { int scene; void* enter_data; void* exit_data; };
struct GameModeState { int id,preload=0,flags=0; void(*prep)(GameModeState*)=nullptr; void(*decide)(GameModeState*)=nullptr; Info info{}; };
struct GameMode { int kind; GameModeState* states; };
struct GameRules { int mode=0,stock_count=0,stock_time_limit=0,friendly_fire=0; };
struct GamePrefs { int item_freq=0; unsigned long long item_mask=0; };
static GameRules rules; static GamePrefs prefs; static VsModeData vs;
static GameModeState original[1]={{-1}};
static GameMode modes[2]={{GM_VS,original},{GM_COUNT,nullptr}};
struct MatchEnd { int outcome=0; };
static struct { MatchEnd match_end; } gmVsMelee_VsExitInfo;
static int gmVsMelee_StartData,gmVsMelee_ResultsEnterData,next_state=-1,next_mode=-1,results=0;
static CSSData* mnCharSel_804D6CB0;
struct CSSDoor { int team=-1,p_kind=3,p_kind_prev=3,is_hold_cpu_slider=0; float togglebtn_left=10,togglebtn_right=20; };
struct CSSCursorData { int x5=0,x6=0; };
static CSSCursorData cursors[4];
static CSSCursorData* mnCharSel_804A0BC0[4]={&cursors[0],&cursors[1],&cursors[2],&cursors[3]};
static struct { CSSDoor doors[4]; } mnCharSel_803F0DFC;
static int mu_friends_css_frames,redraws[4];
void mnCharSel_8025DB34(u8 d){++redraws[d];}
GameRules* gmMainLib_GetGameRules(){return &rules;}
GamePrefs* gmMainLib_GetGamePrefs(){return &prefs;}
VsModeData* gmVsMelee_GetVsData(){return &vs;}
GameMode* gm_GetAllGameModes(){return modes;}
CSSData* gm_GetGameModeStateEnterData(GameModeState* s){return (CSSData*)s->info.enter_data;}
CSSData* gm_GetGameModeStateExitData(GameModeState* s){return (CSSData*)s->info.exit_data;}
void gm_80167BC8(VsModeData* v){
 v->start.rules.stocks=rules.stock_count;v->start.rules.minutes=rules.stock_time_limit;
 v->start.rules.items=prefs.item_freq;v->start.rules.friendly_fire=rules.friendly_fire;
}
void gmVsMelee_EnterCss(GameModeState* s,VsModeData* v,int){
 auto* c=(CSSData*)s->info.enter_data;c->vs=*v;mnCharSel_804D6CB0=c;
}
void gmVsMelee_ExitCss(GameModeState* s,VsModeData* v){*v=((CSSData*)s->info.exit_data)->vs;}
void gmVsMelee_EnterVs(GameModeState*,VsModeData*,void*,void*){}
void gmVsMelee_ExitVs(GameModeState*,int,int){}
void gmVsMelee_EnterResults(GameModeState*){++results;}
void gm_ChangeGameModeAfterCurrentScene(int m){next_mode=m;}
void gm_SetNextGameModeStateId(int s){next_state=s;}
int mu_online_abi_command(unsigned int,const unsigned char* q,unsigned int,unsigned char* out,unsigned int,unsigned int* got){
 assert(*q==11);const unsigned char values[10]={4,8,0,1,0,28,0,0,1,1};
 std::memcpy(out,values,10);*got=10;return 0;
}
'''
        checks=r'''
int main(){
 assert(mu_doubles_boot());assert(mu_doubles_boot());assert(modes[0].states!=original);
 modes[0].states[0].prep(&modes[0].states[0]);
 auto* c=mnCharSel_804D6CB0;assert(c);
 c->vs.start.rules.is_teams=0;rules.stock_count=99;prefs.item_freq=4;
 mu_doubles_css_frame();
 assert(c->vs.start.rules.is_teams==1);assert(c->vs.start.rules.stkind==28);
 assert(c->vs.start.rules.stocks==4);assert(c->vs.start.rules.minutes==8);
 assert(c->vs.start.rules.friendly_fire==1);assert(prefs.item_freq==255);assert(prefs.item_mask==0);
 for(int p=0;p<4;++p){
  assert(c->vs.start.players[p].team==p/2);assert(c->vs.start.players[p].slot_type==Gm_PKind_Human);
  assert(c->vs.start.players[p].ckind==9);assert(c->vs.start.players[p].color==2);
  assert(mnCharSel_803F0DFC.doors[p].team==p/2);assert(mnCharSel_803F0DFC.doors[p].p_kind==0);
  assert(redraws[p]==0); // the scene's first frame has not built the doors yet
 }
 // From the next frame each door redraws once with its frozen team, then not again until
 // the next CSS visit (the scene's entry resets the frame count).
 mu_friends_css_frames=1;mu_doubles_css_frame();mu_doubles_css_frame();
 for(int p=0;p<4;++p) assert(redraws[p]==1);
 mu_friends_css_frames=0;mu_doubles_css_frame();mu_friends_css_frames=1;mu_doubles_css_frame();
 for(int p=0;p<4;++p) assert(redraws[p]==2);
 // The retail team box moves door 1 (and redraws it): the next frame puts the frozen team back and redraws it.
 mnCharSel_803F0DFC.doors[1].team=2;mu_doubles_css_frame();
 assert(mnCharSel_803F0DFC.doors[1].team==0);assert(redraws[1]==3);assert(redraws[0]==2);
 // The HMN/CPU switch takes no presses (empty bounds). A door that turned CPU anyway, its hand on the level
 // slider, is drawn back as a player and the hand lets go.
 for(int p=0;p<4;++p) assert(mnCharSel_803F0DFC.doors[p].togglebtn_left == mnCharSel_803F0DFC.doors[p].togglebtn_right);
 mnCharSel_803F0DFC.doors[0].p_kind=mnCharSel_803F0DFC.doors[0].p_kind_prev=1;mnCharSel_803F0DFC.doors[0].is_hold_cpu_slider=1;
 cursors[2].x5=1;cursors[2].x6=4;cursors[3].x5=1;cursors[3].x6=5;mu_doubles_css_frame();
 assert(mnCharSel_803F0DFC.doors[0].p_kind==0);assert(mnCharSel_803F0DFC.doors[0].p_kind_prev==1);assert(redraws[0]==3);
 assert(mnCharSel_803F0DFC.doors[0].is_hold_cpu_slider==0);assert(cursors[2].x5==2);assert(cursors[3].x5==1);
 modes[0].states[0].decide(&modes[0].states[0]);assert(next_state==1);
 assert(modes[0].states[1].info.scene==GS_VS); // no stage selection state
 // A decided match (time, game, or a team's last stock) shows its results, then the menu; a no contest goes straight back.
 next_state=-1;gmVsMelee_VsExitInfo.match_end.outcome=3;modes[0].states[1].decide(&modes[0].states[1]);
 assert(next_state==2);assert(next_mode==-1);
 gmVsMelee_VsExitInfo.match_end.outcome=1;modes[0].states[1].decide(&modes[0].states[1]);
 assert(next_state==2);assert(next_mode==-1);assert(modes[0].states[2].info.scene==GS_RESULTS);
 modes[0].states[2].prep(&modes[0].states[2]);assert(results==1);
 modes[0].states[2].decide(&modes[0].states[2]);assert(next_mode==GM_MENU);
 next_mode=-1;gmVsMelee_VsExitInfo.match_end.outcome=7;modes[0].states[1].decide(&modes[0].states[1]);
 assert(next_mode==GM_MENU);
 // Giving VS back asks for Online on Doubles once.
 assert(!mu_doubles_take_menu_return());
 mu_doubles_enter_mode(GM_MENU);assert(!mu_doubles_active());assert(modes[0].states==original);
 // Every other mode gets the switches back.
 mu_doubles_css_frame();
 for(int p=0;p<4;++p){assert(mnCharSel_803F0DFC.doors[p].togglebtn_left==10);assert(mnCharSel_803F0DFC.doors[p].togglebtn_right==20);}
 assert(mu_doubles_take_menu_return());assert(!mu_doubles_take_menu_return());
}
'''
        self.compile_run(stubs+game+css+checks)

    def test_source_boot_and_ticket_bridge_are_wired(self):
        host=(ROOT/'port/app/source_host.cpp').read_text()
        runtime=(ROOT/'port/runtime/hle/yougame_doubles_runtime.inc').read_text()
        link=(ROOT/'port/runtime/hle/yougame_link.cpp').read_text()
        self.assertIn('if (doubles_tick()==-51) return -51;',host)
        self.assertIn('if (operation==64) return doubles_checkpoint();',host)
        self.assertIn('#include "doubles_boot.inc"',host)
        self.assertIn('console.config().doubles_teams[i]',host)
        self.assertIn('yougame::doubles::report(',host)
        self.assertIn('home->send(returned)',runtime)
        self.assertLess(runtime.index('match->close()'),runtime.index('home->send(returned)'))
        self.assertIn('bridge(ticket(h.compatibility,h.code,2),10000)',runtime)
        self.assertIn('io.doubles=true',runtime)
        self.assertIn('const std::string line = "yg " + request.dump()',link)
        self.assertIn('std::string data = body.dump()',link)

    def test_page_field_contract_and_picker(self):
        import json
        reference=Path('../yougame/web/src/components/Player.tsx')
        if reference.exists():
            page=reference.read_text()
            self.assertIn('msg.teamsLobby === 1',page)
            self.assertIn('party_search: msg.partySearch',page)
            for field in ('mu_doubles','party_room','party','local_players'):
                self.assertIn('msg.'+field,page)
            self.assertNotIn('msg.teams_lobby',page)
            self.assertNotIn('msg.party_search',page)
        link=(ROOT/'port/runtime/hle/yougame_link.cpp').read_text()
        begin=link.index('  if(doubles) {',link.index('std::string pick_friend_room(',link.index('#include "yougame_doubles_runtime.inc"')))
        end=link.index('\n  }',begin)+4
        picker=link[begin:end]
        output=self.compile_run(r'''
#include "yougame_doubles.h"
#include <iostream>
int main(){
 using namespace yougame;
 bool doubles=true; auto picker_request=friends::picker("homeaaaa");
'''+picker+r'''
 std::cout<<doubles::ticket("mu:fingerprint","homeaaaa",2).dump()<<"\n";
 std::cout<<doubles::search_ticket("mu:fingerprint","homeaaaa","abcdefghijklmnopqrst",2).dump()<<"\n";
 std::cout<<doubles::ticket("mu:fingerprint","matchaaa",4).dump()<<"\n";
 std::cout<<picker_request.dump()<<"\n";
 std::cout<<doubles::search_ticket("mu:fingerprint","homeaaaa","abcdefghijklmnopqrst",1).dump()<<"\n";
}
''')
        requests=[json.loads(line) for line in output.splitlines()]
        for q in requests:
            self.assertEqual(q['teamsLobby'],1)
            self.assertEqual(q['mu_doubles'],1)
            self.assertNotIn('teams_lobby',q)
            self.assertNotIn('party_search',q)
        home,duo,match,picker,solo=requests
        for q in (home,match,picker): self.assertNotIn('partySearch',q)
        self.assertEqual(duo['partySearch'],'abcdefghijklmnopqrst')
        self.assertEqual(duo['party_room'],'homeaaaa')
        # What the page posts (Player.tsx mpTicket, native build, mu_doubles) and how the site's Doubles check
        # (native-teams.ts nativeTeamsRequest) reads it: home, search alone, search as two, match.
        kinds=[route_kind(page_body(q)) for q in (home,solo,duo,match)]
        self.assertEqual(kinds,['home','search','search','match'])
        for q in (home,match):
            body=page_body(q)
            for k in ('party','party_room','party_search','local_players'): self.assertNotIn(k,body)
        self.assertNotIn('party',page_body(solo))
        self.assertEqual(page_body(duo)['party'],{'id':duo['partySearch'],'n':2})
        # The old shape (a null search on every ticket, snake_case from the game) would not pass.
        self.assertIsNone(route_kind(page_body({**home,'teams_lobby':1,'teamsLobby':None})))

    def test_launcher_forwards_doubles_and_off_is_unchanged(self):
        launcher=(ROOT/'port/app/launcher_yougame.cpp').read_text()
        begin=launcher.index('    if (type == "mpTicket") {')
        end=launcher.index('\n  } catch (const std::exception& e)',begin)
        block=launcher[begin:end]
        source=r'''
#include <nlohmann/json.hpp>
#include <cassert>
#include <string>
using json=nlohmann::json;
const char* kSlug="melee-unlocked";
static json sent;
struct Backend {
 std::string player_id(){return "fixture";}
 std::string ticket(const std::string& body){sent=json::parse(body);return R"({"md":1,"ticket":"fixture"})";}
};
json parse_or_empty(const std::string& s){return s.empty()?json::object():json::parse(s);}
std::string listed_build_id(const std::string&){return "fixture-build";}
json reply(int,json result){return result;}
json respond(json q){
 const std::string type="mpTicket",build_json="{}";Backend backend;
 struct {std::string name="fixture";} a;
'''+block+r'''
 return {};
}
int main(){
 json q={{"teamsLobby",1},{"partySearch","abcdefghijklmnopqrst"},{"mu_doubles",1},{"party_room","homeaaaa"},
 {"party",{{"id","abcdefghijklmnopqrst"},{"n",2}}},{"local_players",2},{"lobby",{{"version",1}}},{"queue","private"}};
 auto result=respond(q);
 assert(sent["lobby"]==q["lobby"]);
#ifdef MELEE_DOUBLES
 assert(sent["teams_lobby"]==1 && sent["party_search"]==q["partySearch"]);
 for(const char* k:{"mu_doubles","party_room","party","local_players","queue"})assert(sent[k]==q[k]);
 assert(result["result"]["md"]==1);
#else
 assert(sent["queue"]=="casual");assert(!sent.count("teams_lobby") && !sent.count("party_search"));
 assert(!sent.count("mu_doubles") && !sent.count("party_room") && !sent.count("party") && !sent.count("local_players"));
 assert(!result["result"].count("md"));
#endif
 assert(!sent.count("teamsLobby") && !sent.count("partySearch"));
}
'''
        self.compile_run(source)
        self.compile_run(source,False)

    def test_pre_save_c_continuation(self):
        import importlib.util
        spec=importlib.util.spec_from_file_location('fixture',ROOT/'tools/native_major_fixture.py')
        fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
        pad=(ROOT/'sourceport/game/shim/mu_doubles_bootstrap.c').read_text()
        functions=fixture.extract(pad,'doubles_restart')+fixture.extract(pad,'mu_doubles_main')
        self.compile_run(r'''
#include <cassert>
#include <cstdint>
static void** doubles_boot_context;
static int captures,seeds,mains;
static void(*restart_handler)();
void mu_doubles_set_restart(void(*fn)()){restart_handler=fn;}
void mu_doubles_restart(){restart_handler();}
struct Api { int(*console_origin)(int); int(*console_seed)(uint32_t*); };
int origin(int op){assert(op==64);++captures;return 1;}
int seed(uint32_t* value){*value=123;return 1;}
void mu_apply_seed(uint32_t value){assert(value==123);++seeds;}
static Api api{origin,seed};static Api* mu_host=&api;
'''+functions+r'''
int game_main(){if(++mains==1)mu_doubles_restart();return 37;}
int main(){assert(mu_doubles_main(game_main)==37);assert(captures==1);assert(seeds==1);assert(mains==2);}
''',x86=True)

    def test_host_restart_codes_reach_the_game_restart(self):
        # The production frame hook and its host half together: -51 and -50 must reach the
        # restart handlers, not be folded into "a frame began" over a just-restored boot image.
        import importlib.util
        spec=importlib.util.spec_from_file_location('fixture',ROOT/'tools/native_major_fixture.py')
        fixture=importlib.util.module_from_spec(spec);spec.loader.exec_module(fixture)
        pad=(ROOT/'sourceport/game/shim/mu_pad.c').read_text()
        hook=(ROOT/'sourceport/game/shim/mu_console_pad.c').read_text()
        functions=fixture.extract(pad,'mu_console_host_frame_begin')+fixture.extract(hook,'mu_console_frame_begin')
        out=self.compile_run(r'''
#include <cassert>
#include <csetjmp>
#include <cstdint>
#include <cstdio>
static int host_result,replaced,doubles_restarts,css_restarts;
static std::jmp_buf restarted;
struct Api { int(*console_seed)(uint32_t*); int(*console_frame_begin)(); };
int frame(){return host_result;}
static Api api{nullptr,frame};static Api* mu_host=&api;
void mu_apply_seed(uint32_t){}
int mu_console_rollback_active(){return 0;}
void mu_console_replace_raw(){++replaced;}
void mu_doubles_restart(){++doubles_restarts;std::longjmp(restarted,1);}
void mu_css_origin_restart(){++css_restarts;std::longjmp(restarted,1);}
'''+functions+r'''
int main(){
 host_result=0;assert(mu_console_frame_begin()==0);assert(replaced==0);
 host_result=1;assert(mu_console_frame_begin()==1);assert(replaced==1);
 host_result=-51;if(!setjmp(restarted)){mu_console_frame_begin();assert(!"-51 resumed the old stack");}
 assert(doubles_restarts==1);assert(replaced==1);
 host_result=-50;if(!setjmp(restarted)){mu_console_frame_begin();assert(!"-50 resumed the old stack");}
 assert(css_restarts==1);assert(replaced==1);
 api.console_frame_begin=nullptr;assert(mu_console_frame_begin()==0);
 std::puts("ok");
}
''')
        self.assertEqual(out.strip(),'ok')

    def test_pre_save_host_reset_compiles_and_restores(self):
        host=(ROOT/'port/app/source_host.cpp').read_text()
        a=host.index('struct CssOriginRuntime {')
        b=host.index('} g_css_origin;',a)+len('} g_css_origin;')
        fixture=(ROOT/'port/tests/doubles_boot_fixture.inc').read_text()+host[a:b]+r'''
static CssOriginRuntime g_doubles_origin;
#include "doubles_boot.inc"
struct Link:yougame::console::SessionLink {
 void send_pad(uint32_t,const MuPadStatus&)override{}
 std::optional<MuPadStatus> pad(int,uint32_t)override{return MuPadStatus{};}
 void send_digest(uint32_t,uint64_t)override{}
 std::optional<uint64_t> digest(int,uint32_t)override{return {};}
 void pump(int)override{}
 std::optional<std::string> lost()const override{return {};}
};
int main(){
 g_game.state_regions=[](MuStateRegion* out,unsigned){out[0]={game_bytes,sizeof game_bytes};return 1u;};
 g_game.state_exclusions=[](MuStateRegion*,unsigned){return 0u;};
 static int test_major=1,test_minor=0; static bool query_ok=true; static int menu_returns=0;
 g_game.practice=[](int op,int32_t* out,int){if(op==MU_PRACTICE_SCENE){out[0]=test_major;out[1]=test_minor;}if(op==MU_PRACTICE_FRIENDS_RETURN)++menu_returns;return query_ok?0:-1;};
 setenv("YOUGAME_DATA",std::filesystem::path(__FILE__).parent_path().string().c_str(),1);
 g_css_origin.armed=true;game_bytes[0]=42;
 auto* initial=std::malloc(8);g_css_origin.live_allocations[initial]=8;
 assert(doubles_checkpoint());game_bytes[0]=99;
 auto* later=std::malloc(16);g_css_origin.live_allocations[later]=16;
 auto launch=std::make_shared<yougame::doubles::Launch>();launch->link=std::make_unique<Link>();
 auto& c=launch->config;c.session_id="bootstrap-test";c.ports={1,2,3,4};c.my_port=1;
 c.doubles=true;c.epoch_ms=1793443200123LL;c.seed=123;
 auto queue=[&]{auto q=std::make_shared<yougame::doubles::Launch>();q->config=c;q->link=std::make_unique<Link>();
   std::lock_guard<std::mutex> lock(yougame::doubles::ui.mutex);yougame::doubles::ui.launch=q;};
 auto refused=[&]{queue();assert(doubles_tick()==0);assert(game_bytes[0]==99);assert(!mounted);
   assert(!g_doubles_launch);assert(yougame::doubles::ui.commands.back()=="unavailable");assert(!yougame::doubles::ui.idle);};
 slippi::online::mode=0;refused();slippi::online::mode=-1; // Ranked/Unranked
 g_replaying=true;refused();g_replaying=false;
 test_major=2;test_minor=2;refused(); // local match
 query_ok=false;refused();query_ok=true;test_major=1;test_minor=0;
 auto& engine=yougame::console::console_engine();auto other=c;other.doubles=false;
 assert(engine.begin(std::make_unique<Link>(),other,{}));refused(); // Friends/console match
 assert(engine.active() && !engine.config().doubles);engine.end("done");engine=yougame::console::Engine{};
 // A refused begin must return home without resetting any game memory or terminating.
 auto invalid=std::make_shared<yougame::doubles::Launch>();invalid->config=c;
 {std::lock_guard<std::mutex> lock(yougame::doubles::ui.mutex);yougame::doubles::ui.launch=invalid;}
 assert(doubles_tick()==0);assert(game_bytes[0]==99);assert(!g_console_boot);assert(!mounted);
 queue();g_handoff_disc_depth=1;assert(doubles_tick()==0);assert(game_bytes[0]==99);g_handoff_disc_depth=0;
 assert(doubles_tick()==-51);assert(game_bytes[0]==42);assert(mounted);assert(g_console_boot);
 assert(yougame::console::console_engine().active());assert(g_css_origin.live_allocations.size()==1);
 {std::lock_guard<std::mutex> lock(yougame::doubles::ui.mutex);yougame::doubles::ui.stop_match=true;}
 assert(doubles_tick()==0);assert(!g_console_boot);assert(!mounted);
 assert(yougame::doubles::ui.stopped);
 // Runtime foregrounds only after the stop acknowledgement, on a later body.
 test_major=2;test_minor=2;
 {std::lock_guard<std::mutex> lock(yougame::doubles::ui.mutex);yougame::doubles::ui.menu=true;}
 assert(doubles_tick()==0);assert(menu_returns==1);
 {std::lock_guard<std::mutex> lock(yougame::doubles::ui.mutex);yougame::doubles::ui.menu=true;}
 assert(doubles_tick()==0);assert(menu_returns==1); // unrelated local match stays untouched
 std::free(initial);
}
'''
        self.compile_run(fixture,extra=('port/runtime/hle/yougame_console_engine.cpp','port/runtime/hle/native_savestate.cpp'))

    def test_failed_match_requeues_and_names_the_helper(self):
        hle=ROOT/'port/runtime/hle'
        # Whitespace-blind: formatting changes must not fail this, only code changes.
        squash=lambda s: re.sub(r'\s+','',s)
        def has(needle,path): self.assertIn(squash(needle),squash((hle/path).read_text()),needle)
        # The seat whose app refused the link helper (no_direct) is told why instead of a generic failure;
        # every attach starts by clearing the flag, so only a fresh no_direct sets it.
        has('bool attach(int ms) { helper_missing = false;','yougame_link.cpp')
        has('helper_missing = code == "no_direct";','yougame_link.cpp')
        has('io.helper_missing = [b] { return b->helper_missing.load(); };','yougame_link.cpp')
        has('return fail(i.helper_missing() ? kHelperMissing : "couldn\'t connect to your friend");','yougame_console_link.cpp')
        has('if (reason == kHelperMissing) return kHelperMissing;','yougame_console_notice.h')
        # A matchmaking seat without the helper goes to the room relay (room_relay_wiring_test.py); the
        # helper line stays for a relay that can't start.
        has('if (helper_missing) {\n        no_helper_cooldown();','yougame_link.cpp')
        has('host::log("yougame: the room relay could not start");\n      return fail(console::kHelperMissing, false);','yougame_link.cpp')
        # The notice is sticky over observe()'s status, and the lease is read from every home snapshot.
        has('ui.line=h.notice.empty()?h.status:h.notice;','yougame_doubles_runtime.inc')
        has('leased = (has_party && m["party"].value("playing",json())==true) ||','yougame_doubles.h')
        # The idle home's line comes from home_line() on every roster change, on a stopped search and after a match,
        # so a partner's join or leave shows on both screens; the runtime logs the 1<->2 change.
        has('else if (phase==Phase::Home) status = home_line();','yougame_doubles.h')
        has('if (participants.size()<=2) status = home_line();','yougame_doubles.h')
        has('stopped=just_joined=false; status=home_line(); return {{"t","party-back"}};','yougame_doubles.h')
        # Search errors keep their reason through later snapshots; a roster change clears a press notice.
        has('h.search_ended("The search stopped. Press START to search again','yougame_doubles_runtime.inc')
        has('h.search_ended("Lost the search connection. Press START to search again','yougame_doubles_runtime.inc')
        has('if (humans>0 && press_notice) { h.notice.clear(); press_notice=false; }','yougame_doubles_runtime.inc')
        has('if (humans==1 && h.participants.size()==2 && h.other_seat()) host::log("doubles: partner joined (2 players in the home)");','yougame_doubles_runtime.inc')
        has('else if (humans==2 && h.participants.size()==1) host::log("doubles: partner left (1 player in the home)");','yougame_doubles_runtime.inc')
        # A failed admission, or a dissolve before Playing on either socket, goes through after_failure.
        has('*failure=error; return std::shared_ptr<Launch>{};','yougame_doubles_runtime.inc')
        has('const auto by=dissolved_by(); withdraw(); back(); after_failure(failure ? *failure : std::string(),by);','yougame_doubles_runtime.inc')
        rt=squash((hle/'yougame_doubles_runtime.inc').read_text())
        self.assertEqual(rt.count(squash('const bool started=h.phase==Phase::Playing; back(); if (!started) after_failure(')),2)
        # The host requeues three times per START (START and Playing reset it), only once Rooms released the home and
        # the game is idle in front, for the same duo and role that pressed START, never after a dissolve this home
        # caused; counted when the send goes out. A seat without its helper declines a new search instead.
        has('if (requeues>=3) { h.notice=kCappedLine;','yougame_doubles_runtime.inc')
        has('if (!pending_requeue || !home || home->closed() || h.leased || h.phase!=Phase::Home || retained || !ui.idle.load()) return;','yougame_doubles_runtime.inc')
        has('if (!q || !start_host || !h.is_host() || seats()!=start_seats) { pending_requeue=false;','yougame_doubles_runtime.inc')
        has('if (ours(by)) { pending_requeue=false;','yougame_doubles_runtime.inc')
        has('if (!home->send(*q)) return; pending_requeue=false; h.retry=++requeues;','yougame_doubles_runtime.inc')
        has('if (helper_out) { withdraw();','yougame_doubles_runtime.inc')
        has('start_seats=seats(); start_host=h.is_host(); requeues=0; h.retry=0; home->send(q);','yougame_doubles_runtime.inc')
        has('publish(h,false); requeues=0;','yougame_doubles_runtime.inc')

    def test_match_end_shows_results_and_returns_to_doubles(self):
        squash=lambda s: re.sub(r'\s+','',s)
        def has(needle,path): self.assertIn(squash(needle),squash((ROOT/path).read_text()),needle)
        runtime_inc='port/runtime/hle/'+'yougame_doubles_runtime.inc'
        # Without a recording the scene's exit still sends the Game End record the result is read from.
        has('if (!recording_active && !mu_replay_on() && mu_doubles_active()) { emit_game_end(gmVs_GetSceneState()->match_result); return; }','sourceport/game/shim/mu_replay.c')
        # A match room closed after its result waits for the game's own return from the results screen.
        has('if(seen_css && scene[0]==1 && !left_css) { left_css=true; doubles::ui.command("match-ended"); }','port/app/doubles_boot.inc')
        # The menu comes back on Online with Doubles highlighted, and an open home keeps the cursor there.
        has('if (pages() && (mu_doubles_take_menu_return() || doubles_teams_back)) {','sourceport/game/shim/mu_slippi_menu.c')
        has('*hovered = (u8) mu_online_menu_row(pages(), 0, MU_SLP_OPT_DOUBLES);','sourceport/game/shim/mu_slippi_menu.c')
        has('mn_804A04F0.hovered_selection = (u8) mu_online_menu_row(pages(), 0, MU_SLP_OPT_DOUBLES);','sourceport/game/shim/mu_slippi_menu.c')
        has('q.assign(1, yougame::session() && ui.active && ui.foreground && !ui.playing ? 1 : 0);','port/runtime/hle/slippi_online.cpp')
        # A team's last stock (outcome 3) is a decided result, not a no contest.
        has('const bool invalid=size<6 || payload[0]<1 || payload[0]>3;','port/app/source_host.cpp')
        # After the result: no notice for the dissolve, the session's end waits for the menu, and the home's lobby and
        # dissolve are held until then (a home that drops is rejoined, never sent back over the results screen).
        has('hooks.notice=[](const char* line) { if(!*line || !yougame::doubles::ui.over()) screen_label::show(line,8.0); };','port/app/doubles_boot.inc')
        has('if(!engine.active() && !left_css && !doubles::ui.finished()) { left_css=true; doubles::ui.command("match-ended"); }','port/app/doubles_boot.inc')
        has('if (text(value,"t")=="result") { std::lock_guard<std::mutex> lock(ui.mutex); ui.result_in=true; }',runtime_inc)
        has('const bool holding=h.phase==Phase::Playing && ui.finished();',runtime_inc)
        has('if (!reopen_home()) { if (in_match()) back(); done=true; }',runtime_inc)
        has('} else if (terminal(value) && h.result_seen && h.phase==Phase::Playing && ui.finished()) {',runtime_inc)
        has('if (got && holding && !home_terminal(m)) { held.push_back(std::move(m)); got=false; }',runtime_inc)
        has('if (in_match() && !holding && !teams_route) back();',runtime_inc)
        # The finish goes out before the pass's commands, so a match-ended right behind it cannot drop it.
        rt=squash((ROOT/runtime_inc).read_text())
        self.assertLess(rt.index(squash('for(const auto& report:reports) if(match) match->send(report);')),rt.index(squash('for (const auto& command:commands) {')))


    def test_home_input_has_no_focus_gate_and_says_why(self):
        # An unfocused game window must not swallow the home's START/B: the polls follow the
        # background-input rule that read_devices already applies, never window focus.
        window=(ROOT/'port/runtime/host/window.cpp').read_text(encoding='utf-8-sig')
        self.assertNotIn('invite_focused',window)
        self.assertIn('const bool home_input = g_background_input || (g_hwnd && window_focused(g_hwnd));',window)
        self.assertIn('yougame::doubles::ui.poll(i, pads[i].err == 0 && home_input, menu_buttons) ||',window)
        self.assertIn('yougame::console::entry.poll(i, pads[i].err == 0 && home_input, menu_buttons)) {',window)
        # Buttons still held from the home stay off for the menu until released, on both read paths.
        self.assertIn('if (const uint16_t carried = yougame::doubles::ui.carried_over(i)) {',window)
        host=(ROOT/'port/app/source_host.cpp').read_text(encoding='utf-8-sig')
        self.assertIn('else raw[port].button &= static_cast<uint16_t>(~yougame::doubles::ui.carried_over(port));',host)
        self.assertIn('bool g_background_input = true;',window)
        # The invite list's answer names whom it invited; the overlay draws the home through shown(), which lingers.
        self.assertIn('if (invited) *invited=doubles::invited(picked);',(ROOT/'port/runtime/hle/yougame_link.cpp').read_text())
        settings=(ROOT/'port/runtime/gx/pc_settings.cpp').read_text(encoding='utf-8-sig')
        self.assertIn('yougame::doubles::ui.shown(now, slippi::online::is_online_match())',settings)
        self.assertIn('if (!focused && !g_background_input)',window)
        runtime=(ROOT/'port/runtime/hle/yougame_doubles_runtime.inc').read_text(encoding='utf-8')
        for line in ('host::log("doubles: START, searching")','host::log("doubles: START ignored (not the host)")',
                     'host::log("doubles: START before the home joined; searching once it has")','host::log("doubles: Z ignored (%s)"'):
            self.assertIn(line,runtime)
        settings=(ROOT/'port/runtime/gx/pc_settings.cpp').read_text(encoding='utf-8-sig')
        box=settings.index('ImGui::Begin("##yougame_doubles_home"')
        self.assertLess(settings.rindex('#ifdef MELEE_DOUBLES',0,box),box)
        entry=(ROOT/'port/runtime/hle/yougame_console_entry.h').read_text(encoding='utf-8')
        self.assertIn('if (!doubles::ui.status().empty()) return {};',entry)  # no "Z: cancel" pill under the box

class LobbySessionEnd(unittest.TestCase):
    def test_a_lobby_session_never_counts_as_over_for_training(self):
        src=(ROOT/'port/runtime/hle/native_practice.cpp').read_text()
        self.assertIn('session_watch.tick(session, match_running, slippi::online::friends_lobby_armed())',src)

    def test_leaving_the_home_tells_the_page_the_room_is_gone(self):
        # The home's report is recorded like the Friends lobby's, so its leave sends mpRoom code null and the
        # YouGame app drops its Lobby and Invite page (the public held-room contract).
        src=(ROOT/'port/runtime/hle/yougame_link.cpp').read_text()
        self.assertIn('report_lobby(room_code, lobby::doubles_room_report(room_code, room_code, false, doubles_epoch',src)
        gone=src[src.index('void report_lobby_gone('):src.index('void shutdown_room(')]
        self.assertIn('{"type", "mpRoom"}, {"code", nullptr}, {"players", json::array()}',gone)
        self.assertIn('if (r->lobby_session) report_lobby_gone(r->code);',src)

if __name__=='__main__':unittest.main()
