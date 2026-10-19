// The Online page of the Linux launcher: the same public peer lobby as the Windows launcher
// (launcher_lobby_p2p.cpp), so Linux and Windows players see each other, chat, and start Slippi
// Direct matches with each other. The build string is the Windows Source Port's, since the two play
// the same frames. Included from launcher_linux.cpp.
// SPDX-License-Identifier: GPL-2.0-or-later
namespace {
std::string g_lobby_problem;
char g_chat_line[256] = "";
int g_lobby_character = 2;   // Fox: the character the game is started with for a lobby match
std::set<std::string> g_lobby_launched;

nlohmann::json lobby_profile() {
  nlohmann::json p = nlohmann::json::object();
  if (auto profile = slippi::account::resolve(fs::path(g_dir) / "User" / "Slippi")) {
    p["name"] = slippi::account::text(profile.data, "displayName");
    p["code"] = slippi::account::text(profile.data, "connectCode");
  }
  p["build"] = std::string(MELEE_PORT_VERSION) + ":source";
  p["mains"] = nlohmann::json::array({g_lobby_character});
  p["ready"] = !g_iso.empty();
  p["platform"] = "linux";
  return p;
}

void lobby_go_online() {
  if (g_lobby) return;
  const auto profile = lobby_profile();
  if (profile.value("code", std::string()).empty()) { g_lobby_problem = "Online play needs a Slippi account: log in once in the Slippi Launcher."; return; }
  try {
    g_lobby = std::make_unique<launcher::lobby::PeerLobby>(g_dir);
    g_lobby->join(profile);
    g_lobby_problem.clear();
    status("You are in the online lobby.");
  } catch (const std::exception& e) {
    g_lobby.reset();
    g_lobby_problem = e.what();
  }
}

void lobby_command(const std::string& action, const nlohmann::json& data = nlohmann::json::object()) {
  if (!g_lobby) return;
  try { g_lobby->command(action, data); } catch (const std::exception& e) { status(e.what()); }
}

// An accepted match: start the game on Slippi Direct against that code, as the Windows launcher does.
void lobby_launches() {
  if (!g_lobby) return;
  nlohmann::json launch;
  while (g_lobby->take_launch(launch)) {
    const std::string id = launch.value("request", std::string());
    if (id.empty() || !g_lobby_launched.insert(id).second || playing()) continue;
    const std::string code = launch.value("code", std::string());
    bool valid = !code.empty() && code.size() <= 18 && code.find('#') != std::string::npos;
    for (char c : code) if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '#')) valid = false;
    if (!valid) { status("The match could not start: the opponent's code is not valid."); continue; }
    const int character = std::clamp(launch.value("character", g_lobby_character), 0, 25);
    status("Match accepted. Starting Slippi Direct against " + launch.value("name", code) + "...");
    play({"--lobby-direct", code, "--lobby-character", std::to_string(character)});
  }
}

void page_online_impl() {
  ImGui::TextWrapped("The Melee Unlocked lobby: players on Windows and Linux, on the same version, can chat and play Slippi Direct matches with each other.");
  if (!g_lobby) {
    if (!g_lobby_problem.empty()) ImGui::TextColored(ImVec4(1, 0.6f, 0.5f, 1), "%s", g_lobby_problem.c_str());
    if (ImGui::Button("Go online", ImVec2(200, 40))) lobby_go_online();
    return;
  }
  lobby_launches();
  const nlohmann::json state = g_lobby->state();
  if (ImGui::Button("Leave the lobby")) { lobby_command("leave"); g_lobby.reset(); status("You left the lobby."); return; }
  ImGui::SameLine();
  ImGui::TextDisabled("%s", g_lobby->invite().c_str());
  const auto pings = g_lobby->pings();
  ImGui::Columns(2, nullptr, true);
  ImGui::Text("Players");
  ImGui::BeginChild("players", ImVec2(0, -4), true);
  const std::string self = g_lobby->id();
  for (const auto& p : state.value("players", nlohmann::json::array())) {
    if (!p.is_object()) continue;
    const std::string id = p.value("id", std::string());
    if (id == self) continue;
    ImGui::PushID(id.c_str());
    const auto ping = pings.find(id);
    ImGui::Text("%s  %s", p.value("name", std::string("?")).c_str(), p.value("code", std::string()).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s %s", p.value("build", std::string()).c_str(), ping != pings.end() ? (std::to_string(ping->second) + " ms").c_str() : "");
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 80);
    if (ImGui::SmallButton("Challenge")) lobby_command("request", {{"target", id}, {"mode", "vanilla"}});
    ImGui::PopID();
  }
  ImGui::EndChild();
  ImGui::NextColumn();
  ImGui::Text("Requests");
  for (const auto& r : state.value("requests", nlohmann::json::array())) {
    if (!r.is_object()) continue;
    const std::string id = r.value("id", std::string()), st = r.value("state", std::string());
    const bool incoming = r.value("to", std::string()) == self;
    ImGui::PushID(id.c_str());
    ImGui::Text("%s %s (%s)", incoming ? "From" : "To", r.value(incoming ? "from_name" : "to_name", r.value(incoming ? "from" : "to", std::string())).c_str(), st.c_str());
    if (st == "pending") {
      ImGui::SameLine();
      if (incoming && ImGui::SmallButton("Accept")) lobby_command("accept", {{"request", id}});
      ImGui::SameLine();
      if (ImGui::SmallButton(incoming ? "Decline" : "Cancel")) lobby_command("cancel", {{"request", id}});
    }
    ImGui::PopID();
  }
  ImGui::Separator();
  ImGui::Text("Chat");
  ImGui::BeginChild("chat", ImVec2(0, -40), true);
  for (const auto& m : state.value("chat", nlohmann::json::array()))
    if (m.is_object()) ImGui::TextWrapped("%s: %s", m.value("name", std::string("?")).c_str(), m.value("text", std::string()).c_str());
  if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::SetNextItemWidth(-70);
  const bool send = ImGui::InputText("##chat", g_chat_line, sizeof g_chat_line, ImGuiInputTextFlags_EnterReturnsTrue);
  ImGui::SameLine();
  if ((ImGui::Button("Send") || send) && g_chat_line[0]) {
    lobby_command("chat", {{"text", launcher::lobby::clean_chat_text(g_chat_line)}});
    g_chat_line[0] = 0;
  }
  ImGui::Columns(1);
}
}  // namespace
