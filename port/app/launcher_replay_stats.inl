// Slippi-style match statistics computed from finalized frames. This is an
// independent implementation of the stat definitions the Slippi Launcher shows
// (conversions, openings, stocks, action counts and input counts).
struct PostData {
  bool valid=false;
  int action=0, internal=0, stocks=0, attack=0, status=0;
  float percent=0, counter=0;
};
struct PreData {
  bool valid=false;
  float jx=0, jy=0, cx=0, cy=0, l=0, r=0;
  unsigned buttons=0;
};
struct FrameSlot { PostData post; PreData pre; };
using FrameData=std::array<FrameSlot,4>;
constexpr int first_frame=-123, first_playable=-39, punish_reset_frames=45, no_frame=INT_MIN;

bool is_dead(int a) { return a>=0x00 && a<=0x0a; }
bool is_damaged(int a) { return (a>=0x4b && a<=0x5b) || a==0x26 || a==0xb9 || a==0xc1; }
bool is_grabbed(int a) { return a>=0xdf && a<=0xe8; }
bool is_command_grabbed(int a) { return ((a>=0x10a && a<=0x130) || (a>=0x147 && a<=0x152)) && a!=0x125; }
bool in_control(int a) { return (a>=0x0e && a<=0x18) || (a>=0x27 && a<=0x29) || (a>0x2c && a<=0x40) || a==0xd4; }
bool aerial_landing(int action) { return action>=0x46 && action<=0x4a; }
// Frames looked back from a special landing. 12 reproduces the Slippi Launcher's counts on
// the reference replay (an 8 frame window undercounts one Luigi wavedash there).
constexpr int wavedash_window=12;
bool is_roll(int a) { return a==0xe9 || a==0xea; }

// ---- actions ----
struct ActionState {
  int action=-1, landing_frames=0, last_l_cancel=0;
  float counter=0;
  std::array<int,32> history{};                  // recent action states, newest at (count-1)%32
  int count=0;
  int back(int n) const { return count>n?history[(count-1-n)%32]:-1; }
};
void action_step(Player& player,ActionState& state,const PostData& post) {
  const int current=post.action, status=post.status, internal=post.internal;
  const float counter=post.counter;
  // L-cancels: a fresh aerial landing reads the game's L-cancel status byte.
  const bool fresh=current!=state.action || state.counter>counter;
  if(fresh) {
    if(aerial_landing(current)) {
      if(status==1) ++player.l_success;
      if(status==2) ++player.l_fail;
    }
    // A failed L-cancel does not count when the aerial landing was edge-cancelled
    // within its L-cancelled landing lag.
    if(aerial_landing(state.action) && (current==0x1d || current==0xf5) &&
       internal<27 && state.landing_frames>0 && state.last_l_cancel==2) {
      int move=state.action-0x46;
      if(move>=0 && move<5 && state.landing_frames<=replay_landing_lag[internal][move][1] && player.l_fail>0)
        --player.l_fail;
    }
    if(status>0) state.last_l_cancel=status;
    state.landing_frames=aerial_landing(current)?1:0;
  } else if(aerial_landing(current)) ++state.landing_frames;
  state.action=current; state.counter=counter;
  // Per-frame action history counters.
  state.history[state.count%32]=current; ++state.count;
  const int previous=state.back(1);
  if(state.back(2)==0x14 && previous==0x12 && current==0x14) ++player.dash_dances;
  if(is_roll(current) && !is_roll(previous)) ++player.rolls;
  if(current==0xeb && previous!=0xeb) ++player.spot_dodges;
  if(current==0xec && previous!=0xec) ++player.air_dodges;
  if(current==0xfc && previous!=0xfc) ++player.ledge_grabs;
  // Wavedash / waveland: special landing straight out of an air dodge or a jump state.
  if(current==0x2b && (previous==0xec || (previous>=0x18 && previous<=0x22))) {
    bool air_dodge=false, knee_bend=false, other=false;
    const int recent=std::min(state.count,wavedash_window);
    for(int n=0;n<recent;++n) {
      const int a=state.back(n);
      if(a==0xec) air_dodge=true; else if(a!=0x2b) other=true;
      if(a==0x18) knee_bend=true;
    }
    if(air_dodge && !other) return;              // a late landing out of a plain air dodge
    if(air_dodge) --player.air_dodges;
    if(knee_bend) ++player.wavedashes; else ++player.wavelands;
  }
}

// ---- conversions ----
struct PunishMove { int id=0; float damage=0; };
struct Punish {
  int attacker=0, victim=0, start=0, end=no_frame;
  float start_percent=0, current_percent=0;
  bool killed=false;
  Opening opening=Opening::Neutral;
  std::vector<PunishMove> moves;
};
struct PunishState { int active=-1, move=-1, last_hit=-1, reset=0; };
void punish_step(std::vector<Punish>& list,PunishState& state,int attacker,int victim,int frame,
                 const PostData& me,const PostData* me_prev,const PostData& opp,const PostData* opp_prev) {
  const int oa=opp.action;
  const bool stunned=is_damaged(oa) || is_grabbed(oa) || is_command_grabbed(oa);
  const float taken=opp_prev?opp.percent-opp_prev->percent:0.f;
  // A hit belongs to a new move once the attacker's animation changed or restarted.
  if(me.action!=state.last_hit || me.counter<(me_prev?me_prev->counter:0.f)) state.last_hit=-1;
  if(stunned) {
    if(state.active<0) {
      Punish punish;punish.attacker=attacker;punish.victim=victim;punish.start=frame;
      punish.start_percent=opp_prev?opp_prev->percent:0.f;punish.current_percent=opp.percent;
      list.push_back(punish);state.active=int(list.size())-1;
    }
    if(taken!=0) {
      auto& moves=list[state.active].moves;
      if(state.last_hit<0) {moves.push_back({me.attack,0.f});state.move=int(moves.size())-1;}
      if(state.move>=0) moves[state.move].damage+=taken;
      state.last_hit=me_prev?me_prev->action:-1;
    }
  }
  if(state.active<0) return;
  auto& punish=list[state.active];
  const bool lost=opp_prev && opp.stocks<opp_prev->stocks;
  if(!lost) punish.current_percent=opp.percent;
  if(stunned) state.reset=0;
  if((state.reset==0 && in_control(oa)) || state.reset>0) ++state.reset;
  if(lost) punish.killed=true;
  if(lost || state.reset>punish_reset_frames) {punish.end=frame;state.active=-1;state.move=-1;}
}
void classify_openings(std::vector<Punish>& list) {
  std::vector<int> order(list.size());
  for(size_t i=0;i<order.size();++i) order[i]=int(i);
  std::stable_sort(order.begin(),order.end(),[&](int a,int b){return list[a].start<list[b].start;});
  std::array<int,2> last_end{no_frame,no_frame};  // latest punish end with this player as the victim
  for(size_t i=0;i<order.size();) {
    size_t j=i;
    while(j<order.size() && list[order[j]].start==list[order[i]].start) ++j;
    const bool trade=j-i>=2;
    for(size_t k=i;k<j;++k) {
      auto& punish=list[order[k]];
      last_end[punish.victim]=punish.end;
      if(trade) {punish.opening=Opening::Trade;continue;}
      const int opp_end=last_end[punish.attacker];
      punish.opening=opp_end!=no_frame && opp_end!=0 && opp_end>punish.start?Opening::CounterHit:Opening::Neutral;
    }
    i=j;
  }
}

// ---- inputs ----
int stick_region(double x,double y) {
  // Thresholds compare in double precision, as the reference implementation does.
  const double t=0.2875;
  if(x>=t && y>=t) return 1;
  if(x>=t && y<=-t) return 2;
  if(x<=-t && y<=-t) return 3;
  if(x<=-t && y>=t) return 4;
  if(y>=t) return 5;
  if(x>=t) return 6;
  if(y<=-t) return 7;
  if(x<=-t) return 8;
  return 0;
}
void input_step(Player& player,const PreData& cur,const PreData& prev) {
  unsigned pressed=~prev.buttons & cur.buttons & 0xfffu;
  int count=0;for(;pressed;pressed&=pressed-1) ++count;
  player.inputs+=count;player.digital_inputs+=count;
  const int joy=stick_region(cur.jx,cur.jy), c=stick_region(cur.cx,cur.cy);
  if(joy!=stick_region(prev.jx,prev.jy) && joy!=0) ++player.inputs;
  if(c!=stick_region(prev.cx,prev.cy) && c!=0) ++player.inputs;
  if(prev.l<0.3 && cur.l>=0.3) ++player.inputs;
  if(prev.r<0.3 && cur.r>=0.3) ++player.inputs;
}

// ---- whole game ----
void compute_stats(Info& info,const std::vector<FrameData>& frames) {
  if(info.players.size()!=2) return;
  const int port[2]={info.players[0].port-1,info.players[1].port-1};
  auto ready=[&](size_t i){return frames[i][port[0]].post.valid && frames[i][port[1]].post.valid;};
  size_t begin=0;
  while(begin<frames.size() && !ready(begin)) ++begin;
  std::array<ActionState,2> actions{};
  std::array<PunishState,2> punish_states{};
  std::array<int,2> stock_open{-1,-1};
  std::array<std::vector<Stock>,2> stocks;
  std::vector<Punish> punishes;
  size_t last=begin;
  for(size_t i=begin;i<frames.size() && ready(i);++i) {
    last=i;
    const int frame=int(i)+first_frame;
    const bool has_prev=i>begin;
    for(int p=0;p<2;++p) {
      auto& player=info.players[p];
      const auto& me=frames[i][port[p]];
      const PostData* me_prev=has_prev?&frames[i-1][port[p]].post:nullptr;
      const auto& opp=frames[i][port[1-p]].post;
      const PostData* opp_prev=has_prev?&frames[i-1][port[1-p]].post:nullptr;
      action_step(player,actions[p],me.post);
      player.stocks=me.post.stocks;
      punish_step(punishes,punish_states[p],p,1-p,frame,me.post,me_prev,opp,opp_prev);
      // Own stocks: a stock starts once the player is no longer in a dead state.
      if(stock_open[p]<0) {
        if(!is_dead(me.post.action)) {Stock stock;stock.start_frame=frame;stocks[p].push_back(stock);stock_open[p]=int(stocks[p].size())-1;}
      } else if(me_prev && me.post.stocks<me_prev->stocks) {
        auto& stock=stocks[p][stock_open[p]];
        stock.end_frame=frame;stock.percent=me_prev->percent;
        const int a=me.post.action;
        stock.direction=a==0?0:a==1?1:a==2?2:3;
        stock_open[p]=-1;
      } else stocks[p][stock_open[p]].percent=me.post.percent;
      if(frame>=first_playable && has_prev && me.pre.valid && frames[i-1][port[p]].pre.valid)
        input_step(player,me.pre,frames[i-1][port[p]].pre);
    }
  }
  classify_openings(punishes);
  // Kill moves: the last move of the punish that ended on the frame the stock was lost.
  for(int p=0;p<2;++p) for(auto& stock:stocks[p]) {
    if(stock.direction<0) continue;
    for(const auto& punish:punishes)
      if(punish.victim==p && punish.killed && punish.end==stock.end_frame) {
        stock.kill_move=punish.moves.empty()?-1:punish.moves.back().id;
        break;
      }
  }
  for(int p=0;p<2;++p) {
    auto& player=info.players[p];
    std::vector<const Punish*> own_trades, opp_trades;
    for(const auto& punish:punishes) {
      const bool mine=punish.attacker==p;
      if(!mine) {
        if(!punish.moves.empty() && punish.opening==Opening::Trade) opp_trades.push_back(&punish);
        continue;
      }
      ++player.openings;
      if(punish.killed) ++player.kills;
      if(punish.moves.size()>1) ++player.successful_conversions;
      for(const auto& move:punish.moves) player.damage_done+=move.damage;
      if(!punish.moves.empty()) {
        if(punish.opening==Opening::Neutral) ++player.neutral_wins;
        else if(punish.opening==Opening::CounterHit) ++player.counter_hits;
        else {++player.trades;own_trades.push_back(&punish);}
      }
      Conversion conversion;
      conversion.start_frame=punish.start-first_frame;
      conversion.end_frame=punish.end==no_frame?-1:punish.end-first_frame;
      conversion.start_percent=punish.start_percent;conversion.end_percent=punish.current_percent;
      conversion.moves=int(punish.moves.size());conversion.killed=punish.killed;
      conversion.last_move=punish.moves.empty()?-1:punish.moves.back().id;
      conversion.opening=punish.opening;
      player.conversions.push_back(conversion);
    }
    for(size_t k=0;k<own_trades.size() && k<opp_trades.size();++k) {
      const float mine=own_trades[k]->current_percent-own_trades[k]->start_percent;
      const float theirs=opp_trades[k]->current_percent-opp_trades[k]->start_percent;
      if(mine>theirs) ++player.beneficial_trades;
    }
    for(auto stock:stocks[p]) {
      stock.start_frame-=first_frame;
      if(stock.direction>=0) stock.end_frame-=first_frame;
      player.stock_list.push_back(stock);
    }
  }
  // Winner: the player with stocks left, then more stocks, then lower percent.
  if(last<frames.size() && ready(last)) {
    const auto& a=frames[last][port[0]].post;const auto& b=frames[last][port[1]].post;
    if(a.stocks!=b.stocks) info.winner=a.stocks>b.stocks?0:1;
    else if(a.stocks>0 && a.percent!=b.percent) info.winner=a.percent<b.percent?0:1;
  }
}
