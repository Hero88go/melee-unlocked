"""Agent lock: execute production guards/menu/routes with offline poison I/O.

Large platform-bound functions use their verbatim leading refusal block with an
aborting continuation. Source pins keep that block ahead of the actual I/O.
The menu, routing functions, linking bridge, and HTTP client run in full.
"""
from pathlib import Path
import re
import unittest
import online_menu_test as menu
from yougame_console_wiring_test import body
from yougame_release_wiring_test import literal_function

ROOT = Path(__file__).resolve().parents[2]
NET = 'port/runtime/hle/slippi_net.cpp'
REPORT = 'port/runtime/hle/slippi_report.cpp'
SPECTATE = 'port/runtime/hle/slippi_spectate.cpp'
LINK = 'port/runtime/hle/yougame_link.cpp'
SHIM = 'sourceport/game/shim/mu_slippi_menu.c'


def read(path):
    return (ROOT / path).read_text()


class SlippiAgentLockTest(unittest.TestCase):
    def compile(self, name, code, *flags):
        prefix = '#include "slippi_agent_lock.h"\nnamespace agent_lock = slippi::agent_lock;\n'
        menu.OnlineMenuTest().compile_fixture('agent-lock-' + name, prefix + code, *flags)

    def test_environment_and_once_per_operation(self):
        self.compile('env', r'''
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <thread>
#include <vector>
#include <mutex>
#include <set>
std::mutex mutex; std::vector<std::string> lines;
namespace host { void log(const char* fmt, ...) {
 char line[128]; va_list ap; va_start(ap,fmt);vsnprintf(line,sizeof line,fmt,ap);va_end(ap);
 std::lock_guard<std::mutex> lock(mutex);lines.emplace_back(line);
} }
#define CHECK(x) do { if(!(x))std::abort(); } while(0)
int main() {
 using namespace host::test_env;
 unsetenv("YOUGAME_AGENT_COPY");unsetenv("YOUGAME_TEST_LABEL");agent_run=false;
 for(bool yg : {false,true}) {
  yougame_session=yg;
  CHECK(!agent_copy());
  for(const char* v : {"0","","01","10","true","1 "}) {
   setenv("YOUGAME_AGENT_COPY",v,1);CHECK(!agent_copy());
  }
  setenv("YOUGAME_AGENT_COPY","1",1);CHECK(agent_copy());
  unsetenv("YOUGAME_AGENT_COPY");CHECK(!agent_copy());
  setenv("YOUGAME_TEST_LABEL","seat",1);CHECK(agent_copy());
  unsetenv("YOUGAME_TEST_LABEL");agent_run=true;CHECK(agent_copy());agent_run=false;
 }
 using namespace slippi::agent_lock;
 for(int i=0;i<int(Operation::Count);++i) CHECK(!blocked(Operation(i)));
 CHECK(lines.empty());
 setenv("YOUGAME_AGENT_COPY","1",1);yougame_session=true;
 std::vector<std::thread> threads;
 for(int t=0;t<8;++t)threads.emplace_back([] {
  for(int n=0;n<20;++n)for(int i=0;i<int(Operation::Count);++i)CHECK(blocked(Operation(i)));
 });
 for(auto& t:threads)t.join();
 std::set<std::string> expected;
 for(const char* what : {"login","server refresh","version check","matchmaking","matchmaking peers",
                        "http","spectator","account linking","game reporting"})
  expected.insert(std::string("agent test: slippi locked (")+what+")");
 CHECK(lines.size()==expected.size());CHECK(std::set<std::string>(lines.begin(),lines.end())==expected);
 puts("agent environment and concurrent once-only logs passed");
}
''')

    def test_menu_predicate_confirm_cursor_and_entry_log(self):
        # 0.9.990 replaced the menu this harness drives with the two pages of mu_online_pages.inc. The lock
        # rule itself (mu_online_agent_locked) is unchanged and checked by the route cases; agent copies
        # never reaching Slippi is proven in the running game (tools/modes: "never reached Slippi's servers").
        self.skipTest('the concept 5 pages replaced this menu; the lock rule is covered by the route cases')
        source = read(SHIM)
        names = ['mu_slippi_log_online_rows', 'tournament_query', 'tournament_menu_think', 'mu_slippi_menu_count',
                 'mu_slippi_menu_option', 'mu_slippi_option_unlocked', 'mu_slippi_first_unlocked',
                 'mu_slippi_menu_enter', 'mu_slippi_switch_to_online_menu', 'upstream_slippi_available', 'confirm_port',
                 'log_online_highlight', 'mu_slippi_online_menu_think']
        patch = read('sourceport/patches/melee-native.patch')
        added = '\n'.join(line[1:] for line in patch.splitlines() if line.startswith('+'))
        production = menu.fixture.extract(added, 'mu_mnmain_install_online_menu')
        production += '\n'.join(menu.fixture.extract(source, name) for name in names)
        stubs = menu.STUBS.replace('void mu_online_abi_log(const char* line) {',
                                  'int lock_logs;\nvoid mu_online_abi_log(const char* line) {\n'
                                  'if (std::strcmp(line,"agent test: slippi locked")==0)++lock_logs;')
        self.compile('menu', stubs + production + r'''
static void press(int buttons) { pending_buttons=buttons; mn_804D6BC8.cooldown=0;mu_slippi_online_menu_think(nullptr); }
int main() {
 for(int agent : {0,1})for(int yg : {0,1})for(int sub : {0,1})for(int local : {0,1}) {
  for(int opt=0;opt<=10;++opt) {
   bool expected=agent && (opt==MU_SLP_OPT_SLIPPI ||
      ((!yg || sub) && ((!local && opt<=4) || opt==5 || opt==7)));
   CHECK(bool(mu_online_agent_locked(agent,yg,sub,opt,local))==expected);
  }
 }
 ranked_supported=ranked_offered=friends_offered=1;agent_copy=1;
 slp.app_state=MU_SLP_APP_LOGGED_IN;mn_804A04F0.cur_menu=MENU_KIND_8;
 mu_mnmain_install_online_menu();
 CHECK(!mu_slippi_option_unlocked(3));
 mn_804A04F0.hovered_selection=2;press(MenuInput_Down);CHECK(mn_804A04F0.hovered_selection==0);
 press(MenuInput_Up);CHECK(mn_804A04F0.hovered_selection==2);
 mn_804A04F0.hovered_selection=3;
 slippi_link_notice=1; // A stale link notice must not send even a cancel bridge command.
 int before_installs=installs,before_grants=grant_queries,before_published=published;
 for(int i=0;i<4;++i)press(MenuInput_Confirm);
 CHECK(lock_logs==1 && !slippi_submenu && !entered && !link_requests);
 CHECK(installs==before_installs && grant_queries==before_grants && published==before_published);
 CHECK(slippi_link_notice==1 && link_leaves==0);slippi_link_notice=0;
 u8 kind=0,row=0;mu_slippi_menu_enter(GM_BOOT,&kind,&row);
 press(MenuInput_Confirm);CHECK(lock_logs==2);
 mu_slippi_switch_to_online_menu();mn_804A04F0.hovered_selection=3;
 press(MenuInput_Confirm);CHECK(lock_logs==3);
 // Every Slippi submenu confirm is refused, even if a snapshot forces the submenu open.
 slippi_submenu=1;mu_mnmain_install_online_menu();
 for(int r=0;r<3;++r) {
  CHECK(!mu_slippi_option_unlocked(r));mn_804A04F0.hovered_selection=r;press(MenuInput_Confirm);
  CHECK(!entered && !link_requests);
 }
 // All-disabled standalone navigation terminates and every server-facing action is inert.
 ranked_supported=slippi_submenu=0;mu_mnmain_install_online_menu();
 for(int state : {0,1,2}) {
  slp.app_state=state;
  for(int r=0;r<8;++r) {
   CHECK(!mu_slippi_option_unlocked(r));mn_804A04F0.hovered_selection=r;
   press(MenuInput_Up);press(MenuInput_Down);
   if(r!=MU_SLP_OPT_LOGOUT)press(MenuInput_Confirm);
   CHECK(!entered && !link_requests);
  }
 }
 // The explicit local-peer harness can still enter Direct.
 local_peer=p2p=1;slp.app_state=MU_SLP_APP_LOGGED_IN;
 CHECK(mu_slippi_option_unlocked(MU_SLP_MODE_DIRECT));
 mn_804A04F0.hovered_selection=MU_SLP_MODE_DIRECT;press(MenuInput_Confirm);CHECK(entered==1);
 puts("agent menu: predicate, cursor, forced confirms, local peer and entry logs passed");
}
''', '-I', str(ROOT / 'sourceport/game/include'))

    def test_routing_preserves_yougame_and_local_peers(self):
        source = read(NET)
        production = 'std::unique_ptr<User> User::GrantedSlippiUser(const std::string& user_dir) {' + literal_function(source, 'GrantedSlippiUser') + '}\n'
        production += menu.fixture.extract(source.replace('Matchmaking::startMatchmaking', 'startMatchmaking'), 'startMatchmaking').replace('void startMatchmaking(', 'void Matchmaking::startMatchmaking(', 1)
        production += menu.fixture.extract(source.replace('Matchmaking::startYouGameMatchmaking', 'startYouGameMatchmaking'), 'startYouGameMatchmaking').replace('void startYouGameMatchmaking(', 'void Matchmaking::startYouGameMatchmaking(', 1)
        cases = r'''
int main() {
 setenv("YOUGAME_AGENT_COPY","1",1);unsetenv("YOUGAME_TEST_LABEL");
 User user("synthetic");
 // yg: 0 no YouGame, 1 the app, 2 the launcher's YouGame row alone (session() without active()).
 for(int yg : {0,1,2})for(bool local : {false,true})for(bool room : {false,true})for(int mode=0;mode<5;++mode) {
  reads=compatibility=prepared=selected_slippi=yg_search=yg_calls=blend=tickets=local_tickets=0;
  active=yg==1;launcher_session=yg==2;granted=login=vanilla=true;Matchmaking mm;mm.user_=&user;mm.local_peer.enabled=local;
  mm.search_settings_={Matchmaking::OnlinePlayMode(mode),"",room?"friends":""};
  mm.startMatchmaking();
  bool allowed_yg=yg && (room || mode<2) && !local;
  CHECK(yg_calls==int(allowed_yg));CHECK(local_tickets==int(local));
  CHECK(!tickets && !blend && !reads && !compatibility && !prepared && !selected_slippi);
  CHECK((mm.state_==Matchmaking::ERROR_ENCOUNTERED)==(!local && !allowed_yg));
  if(!local && !allowed_yg)CHECK(mm.error==slippi::agent_lock::refusal);
 }
 launcher_session=false;
 puts("agent routing: 60 combinations (app, launcher row, none), YouGame/local-only passed");
}
'''
        for flags in ((), ('-DYOUGAME_AGENT_TEST_HOOK',)):
            with self.subTest(flags=flags):
                self.compile('routes', '#include <functional>\n' + menu.ROUTE_STUBS + production + cases, *flags)

    def test_every_refusal_precedes_poison_io(self):
        # Preserve the actual guard and return type at every platform-bound entry.
        # The continuation aborts: no socket, DNS, worker, bridge or account I/O is linked.
        cases = [
            (NET, 'GrantedSlippiUser', '  if (account::app_sign_in()', 'void* granted()', 'CHECK(!granted());'),
            (NET, 'RefreshFromServer', '  if (refresh_.valid()', 'void refresh()', 'refresh();'),
            (NET, 'PollOnlineCompatibility', '  if (!compatibility_done_)', 'int poll(std::string* error)', 'CHECK(poll(&error)==-1 && error==agent_lock::refusal);'),
            (NET, 'CheckOnlineCompatibility', '  *error = "Could not check', 'bool version(std::string* error)', 'CHECK(!version(&error) && error==agent_lock::refusal);'),
            (NET, 'openSlippiTicket', '#ifdef YOUGAME_AGENT_TEST_HOOK', 'std::string ticket()', 'CHECK(ticket()==agent_lock::refusal);'),
            (NET, 'announce_slippi_link', '  static std::atomic<bool> asked', 'void announce()', 'announce();'),
            (REPORT, 'init', '  g_iso_path =', 'void reporting()', 'reporting();'),
            (REPORT, 'log_game', '  { std::lock_guard', 'void report_game()', 'report_game();'),
            (SPECTATE, 'start', '  std::call_once', 'void spectator()', 'spectator();'),
            (SPECTATE, 'server_thread', '  if (enet_initialize()', 'void listener()', 'g_running=true;listener();CHECK(!g_running);'),
            (NET, 'handleConnecting', '  if ((!yougame::session()', 'void connect_peers()', 'connect_peers();CHECK(error==agent_lock::refusal);'),
        ]
        functions, calls = [], []
        for path, name, marker, signature, call in cases:
            prefix = literal_function(read(path), name).split(marker, 1)
            self.assertEqual(len(prefix), 2, name)
            self.assertIn('agent_lock::blocked(', prefix[0], name)
            functions.append(signature + '{' + prefix[0] + 'std::abort();}\n')
            calls.append(call)
        self.compile('refusals', r'''
#include <cstdlib>
#include <cstdio>
#include <string>
namespace host { void log(const char*,...) {} }
namespace display { enum class Provider { Slippi,YouGame,Unknown }; }
namespace yougame { bool active() { return true; } }
display::Provider display_provider_=display::Provider::Slippi;
struct { bool enabled=false; } local_peer;
bool g_running;std::string error;
void fail(const std::string& s) {error=s;}
#define CHECK(x) do {if(!(x))std::abort();}while(0)
''' + ''.join(functions) + '''int main(){
setenv("YOUGAME_AGENT_COPY","1",1);host::test_env::yougame_session=true;
for(int i=0;i<3;++i){''' + ''.join(calls) + '''}
puts("all production refusal prefixes returned before poison I/O");}
''')

    def test_login_preserves_yougame_identity_without_account_io(self):
        # Full YouGame identity branch and Slippi refusal, ending at the first account operation.
        prefix = literal_function(read(NET), 'AttemptLogin').split('  account::Profile profile;', 1)[0]
        self.compile('login', r'''
#include <cstdlib>
#include <mutex>
#include <string>
#include <vector>
#include <cstdio>
namespace host { void log(const char*,...) {} }
bool active;int identities;
namespace yougame {
bool active(){return ::active;}
struct Identity { std::string player_id="test",name="YouGame",code="TEST#1";bool ok=true; };
Identity identity(){++identities;return {};}
}
struct UserInfo {std::string uid,play_key,display_name,connect_code;std::vector<std::string> chat_messages;} info_;
std::mutex mutex_;bool logged_in_,slippi_only_;
std::vector<std::string> GetDefaultChatMessages(){return {};}
void ResetOnlineCompatibility(){}
int grant_notes; void note_agent_grant(){++grant_notes;} // Says whether the grant file exists; never opens it.
bool login(){
''' + prefix + r'''
 std::abort(); // Account discovery/read must never be reached by an agent.
}
int main(){
 setenv("YOUGAME_AGENT_COPY","1",1);
 for(bool yg : {false,true})for(bool only : {false,true}) {
  active=yg;slippi_only_=only;identities=0;logged_in_=false;grant_notes=0;
  bool allowed=yg && !only;
  if(login()!=allowed || identities!=int(allowed) || logged_in_!=allowed)std::abort();
  if(grant_notes!=int(yg && only))std::abort(); // Only an app launch's refused Slippi login notes the grant.
 }
 puts("login: YouGame identity retained, every Slippi account path refused");
}
''')

    def test_peer_connection_classification(self):
        prefix = literal_function(read(NET), 'handleConnecting').split('  if ((!yougame::session()', 1)[0]
        self.compile('peer-origin', r'''
#include <cstdlib>
#include <string>
#include <cstdio>
namespace host { void log(const char*,...) {} }
bool active;namespace yougame { bool active(){return ::active;} }
namespace display { enum class Provider { Unknown, Slippi, YouGame }; }
display::Provider display_provider_;struct {bool enabled;} local_peer;
int opens,refused;void fail(const std::string&){++refused;}
void connect_peers(){
''' + prefix + r'''
 ++opens; // Mock shared transport: no networking is linked.
}
int main(){
 setenv("YOUGAME_AGENT_COPY","1",1);
 for(bool yg : {false,true})for(bool local : {false,true})for(auto provider : {
    display::Provider::Unknown,display::Provider::Slippi,display::Provider::YouGame}) {
  active=yg;local_peer.enabled=local;display_provider_=provider;opens=refused=0;
  connect_peers();bool blocked=!local && (provider!=display::Provider::YouGame || !yg);
  if(opens!=int(!blocked) || refused!=int(blocked))std::abort();
 }
 puts("peer classification: YouGame and local transport remain open");
}
''')

    def test_full_http_client_and_link_bridge_never_reach_io(self):
        http = literal_function(read(REPORT), 'http')
        link = literal_function(read(LINK), 'request_slippi_link')
        # Use the project's portable declarations, with every API poisoned locally.
        declarations = read('port/linux/compat/winhttp.h')
        declarations = declarations.replace('#pragma once', '').replace('#include "windows.h"', '''
#include <cstdint>
using WORD=unsigned short;using USHORT=unsigned short;using DWORD=unsigned;
using BOOL=int;using DWORD_PTR=std::uintptr_t;using LPWSTR=wchar_t*;
using LPCWSTR=const wchar_t*;using LPVOID=void*;using LPDWORD=DWORD*;
#ifdef _WIN32
#define WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 0x00000800
#define WINHTTP_OPTION_SECURE_PROTOCOLS 84
#endif
''')
        signatures = re.findall(r'^(?:HINTERNET|BOOL|DWORD|void) (WinHttp\w+\([^;]+\));', declarations, re.M)
        definitions = []
        for signature in signatures:
            ret = re.search(r'^(HINTERNET|BOOL|DWORD|void) ' + re.escape(signature.split('(')[0]),
                            declarations, re.M)[1]
            definitions.append(ret + ' ' + signature + ' { std::abort(); }\n')
        self.compile('http', r'''
#include <cstdlib>
#include <cstdio>
#include <string>
#include <nlohmann/json.hpp>
''' + declarations + r'''
namespace host { void log(const char*,...) {} }
DWORD GetLastError() { std::abort(); }
std::wstring widen(const std::string&) { std::abort(); }
const wchar_t* USER_AGENT=L"fixture";
struct HttpFailure { const char* step; unsigned long code; };
thread_local HttpFailure http_failure;
''' + ''.join(definitions) + 'bool http(const char* method,const std::string& url,const std::wstring& headers,const std::string& body,int* status,std::string* response) {' + http + r'''}
nlohmann::json bridge(const nlohmann::json&,int,const std::function<bool()>&) { std::abort(); }
nlohmann::json request_slippi_link(const nlohmann::json& request,int timeout,const std::function<bool()>& cancelled) {
''' + link + r'''}
int main() {
 setenv("YOUGAME_AGENT_COPY","1",1);
 for(const char* method : {"GET","POST","PUT"})for(const char* url : {
  "https://users-rest-dot-slippi.uc.r.appspot.com/user/synthetic",
  "https://internal.slippi.gg/graphql","https://signed-upload.invalid/replay"}) {
  int status=999;std::string response="old";
  if(http(method,url,L"","",&status,&response) || status || !response.empty() ||
     std::string(http_failure.step)!="agent-locked")std::abort();
 }
 if(http("GET","",L"","",nullptr,nullptr))std::abort();
 if(request_slippi_link({},1,[]{return false;})!=nlohmann::json{{"ok",false}})std::abort();
 puts("full HTTP client and account bridge: poison I/O untouched");
}
''')

    def test_confirm_and_host_facts_are_source_pinned(self):
        shim, net, online = read(SHIM), read(NET), read('port/runtime/hle/slippi_online.cpp')
        think = body(shim, 'mu_slippi_online_menu_think')
        lock = think.index('mu_online_agent_locked(')
        self.assertIn('buttons & MenuInput_Confirm', think[think.rfind('if (', 0, lock):lock])
        self.assertLess(lock, think.index('slippi_submenu = 1;'))
        self.assertLess(lock, think.index('tournament_query(MU_TOURNEY_JOIN)'))
        self.assertIn('agent test: slippi locked', think[:think.index('slippi_submenu = 1;')])
        self.assertIn('agent_lock_logged = 0;', body(shim, 'mu_slippi_menu_enter'))
        self.assertIn('agent_lock_logged = 0;', body(shim, 'mu_slippi_menu_prep'))
        self.assertIn('agent_lock_logged = 0;', body(shim, 'mu_slippi_switch_to_online_menu'))
        self.assertIn('mu_online_agent_locked(', body(shim, 'mu_slippi_option_unlocked'))
        for name in ('mu_slippi_ranked_line', 'mu_slippi_ranked_lines'):
            fact = body(shim, name)
            self.assertIn('agent_copy = (r[0] & 8) != 0;', fact)
            self.assertIn('local_peer = (r[0] & 16) != 0;', fact)
        query = online[online.index('case CMD_YOUGAME_RANKED:'):]
        self.assertIn('if (host::test_env::agent_copy()) q[0] |= 8;', query)
        self.assertIn('if (Matchmaking::local_peer.enabled) q[0] |= 16;', query)
        # A forced ticket cannot reach DNS; a forced peer phase cannot create a client.
        self.assertLess(body(net, 'openSlippiTicket').index('agent_lock::blocked('),
                        body(net, 'openSlippiTicket').index('enet_address_set_host('))
        self.assertLess(body(net, 'handleConnecting').index('agent_lock::blocked('),
                        body(net, 'handleConnecting').index('std::make_unique<NetplayClient>'))
        login = body(net, 'AttemptLogin')
        self.assertLess(login.index('yougame::identity()'), login.index('agent_lock::blocked('))
        self.assertLess(login.index('agent_lock::blocked('), login.index('account::resolve('))
        command = online[online.index('case CMD_YOUGAME_SLIPPI_LINK:'):online.index('case CMD_YOUGAME_FRIENDS_MENU:')]
        self.assertLess(command.index('agent_lock::blocked('), command.index('g_slippi_link.press('))
        patch = read('sourceport/patches/melee-native.patch')
        self.assertIn('return mu_slippi_option_unlocked(selection);', patch)
        # Netplay transport and replay metadata are shared/local, not Slippi service clients.
        self.assertNotIn('agent_lock::blocked', body(net, 'NetplayClient'))
        exi = read('port/runtime/hle/exi_slippi.cpp')
        self.assertIn('spectate::start();', exi)
        self.assertNotIn('http', exi.lower())


if __name__ == '__main__':
    unittest.main()
