#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
# endif
# include <winsock2.h>
# include <ws2tcpip.h>
#endif

#include "zlan_lobby.h"
#include "common.h"
#include "constants.h"
#include "zod_action_button.h"
#include "zod_volume_settings.h"
#include <SDL/SDL_mixer.h>
#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <SDL/SDL_image.h>
#include <string>
#include <vector>
#include <sstream>
#include <algorithm>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#ifdef _WIN32
  typedef SOCKET LanSocket;
# define LAN_BAD INVALID_SOCKET
#else
# include <sys/types.h>
# include <sys/socket.h>
# include <netinet/in.h>
# include <arpa/inet.h>
# include <netdb.h>
# include <unistd.h>
# include <fcntl.h>
  typedef int LanSocket;
# define LAN_BAD (-1)
#endif

using namespace COMMON;

// Implemented in zstart_menu.cpp; consumed by the next RunZStartMenu().
void ZStartMenuQueueNavigationFromLobby(int button_index);

namespace {
const unsigned short LAN_PORT = 39877;
// Match the actual eight ZOD team identifiers, not the old four-color lobby.
const int COLORS[8] = { RED_TEAM, BLUE_TEAM, GREEN_TEAM, YELLOW_TEAM,
                        PURPLE_TEAM, TEAL_TEAM, WHITE_TEAM, BLACK_TEAM };
const char* COLOR_NAMES[8] = { "RED", "BLUE", "GREEN", "YELLOW",
                               "PURPLE", "TEAL", "WHITE", "BLACK" };
const int COLOR_COUNT = sizeof(COLORS) / sizeof(COLORS[0]);
const int MAX_GUESTS = 7;

void CloseSocket(LanSocket s)
{
    if(s == LAN_BAD) return;
#ifdef _WIN32
    closesocket(s);
#else
    close(s);
#endif
}

class LanNet {
public:
    LanNet(): sock(LAN_BAD), initialized(false) { }
    ~LanNet() { Close(); }
    bool Open(bool host) {
#ifdef _WIN32
        WSADATA ws;
        if(WSAStartup(MAKEWORD(2,2), &ws) != 0) return false;
#endif
        initialized = true;
        sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        if(sock == LAN_BAD) return false;
        int yes = 1;
        setsockopt(sock, SOL_SOCKET, SO_BROADCAST, (const char*)&yes, sizeof(yes));
        sockaddr_in a; memset(&a,0,sizeof(a));
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_ANY);
        a.sin_port = htons(host ? LAN_PORT : 0);
        if(bind(sock,(sockaddr*)&a,sizeof(a)) != 0) return false;
#ifdef _WIN32
        u_long nonblock = 1;
        if(ioctlsocket(sock, FIONBIO, &nonblock) != 0) return false;
#else
        int flags = fcntl(sock,F_GETFL,0);
        if(flags < 0 || fcntl(sock,F_SETFL,flags | O_NONBLOCK) != 0) return false;
#endif
        return true;
    }
    void Close() {
        CloseSocket(sock);
        sock = LAN_BAD;
#ifdef _WIN32
        if(initialized) WSACleanup();
#endif
        initialized = false;
    }
    bool Send(const std::string& ip, unsigned short port, const std::string& msg) {
        if(sock == LAN_BAD || ip.empty()) return false;
        sockaddr_in to; memset(&to,0,sizeof(to));
        to.sin_family = AF_INET;
        to.sin_port = htons(port);
        to.sin_addr.s_addr = inet_addr(ip.c_str());
        if(to.sin_addr.s_addr == INADDR_NONE && ip != "255.255.255.255") return false;
        return sendto(sock, msg.c_str(),(int)msg.size(),0,(sockaddr*)&to,sizeof(to)) >= 0;
    }
    bool Recv(std::string& ip, unsigned short& port, std::string& msg) {
        if(sock == LAN_BAD) return false;
        char data[1024];
        sockaddr_in from; memset(&from,0,sizeof(from));
#ifdef _WIN32
        int flen = sizeof(from);
#else
        socklen_t flen = sizeof(from);
#endif
        int got = recvfrom(sock,data,sizeof(data)-1,0,(sockaddr*)&from,&flen);
        if(got <= 0) return false;
        data[got] = 0;
        ip = inet_ntoa(from.sin_addr);
        port = ntohs(from.sin_port);
        msg.assign(data,(size_t)got);
        return true;
    }
    std::string LocalIP() {
        char host[256];
        if(gethostname(host,sizeof(host)) != 0) return "Unknown (use LAN search)";
        addrinfo hints; memset(&hints,0,sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        addrinfo* addresses = NULL;
        if(getaddrinfo(host,NULL,&hints,&addresses) != 0) return "Unknown (use LAN search)";
        std::string any;
        std::string preferred;
        for(addrinfo* p=addresses;p;p=p->ai_next) {
            const sockaddr_in* addr=(const sockaddr_in*)p->ai_addr;
            std::string ip=inet_ntoa(addr->sin_addr);
            if(ip.substr(0,4)=="127." || ip=="0.0.0.0") continue;
            if(any.empty()) any=ip;
            if(ip.substr(0,3)=="10." || ip.substr(0,8)=="192.168." ||
               ip.substr(0,4)=="172.") { preferred=ip; break; }
        }
        freeaddrinfo(addresses);
        return preferred.empty() ? (any.empty()?"Unknown (use LAN search)":any) : preferred;
    }
private:
    LanSocket sock;
    bool initialized;
};

std::string Clean(const std::string& input, size_t maxlen=36) {
    std::string result;
    for(size_t i=0;i<input.size() && result.size()<maxlen;i++) {
        unsigned char ch=(unsigned char)input[i];
        if(ch>=32 && ch<127 && ch!='|' && ch!='\r' && ch!='\n') result += (char)ch;
    }
    return result;
}
std::string Basename(const std::string& path) {
    size_t slash=path.find_last_of("/\\");
    return Clean(slash==std::string::npos?path:path.substr(slash+1),38);
}
std::vector<std::string> Split(const std::string& str) {
    std::vector<std::string> v; size_t begin=0, at=0;
    while((at=str.find('|',begin))!=std::string::npos) {v.push_back(str.substr(begin,at-begin));begin=at+1;}
    v.push_back(str.substr(begin)); return v;
}
std::string ToText(int value) { char buf[32]; sprintf(buf,"%d",value);return buf; }
int ToInt(const std::string& value, int fallback=0) {
    if(value.empty() || value.size()>9) return fallback;
    for(size_t i=0;i<value.size();i++) if(!isdigit((unsigned char)value[i])) return fallback;
    return atoi(value.c_str());
}
int ColorIndex(int team) {
    for(int i=0;i<COLOR_COUNT;i++) if(COLORS[i]==team) return i;
    return -1;
}
const char* ColorName(int team) {
    int i=ColorIndex(team);return i<0?"UNKNOWN":COLOR_NAMES[i];
}

struct Person {
    std::string ip,name;
    unsigned short port;
    int team;
    bool ready;
    Uint32 seen;
    Person():port(0),team(BLUE_TEAM),ready(false),seen(0) {}
};
struct HostOffer {
    std::string ip,name,map;
    int count;
    Uint32 seen;
    HostOffer():count(0),seen(0) {}
};

void Box(SDL_Surface* s,int x,int y,int w,int h,Uint8 r,Uint8 g,Uint8 b) {
    SDL_Rect a;a.x=x;a.y=y;a.w=w;a.h=h;
    SDL_FillRect(s,&a,SDL_MapRGB(s->format,r,g,b));
}
void Label(SDL_Surface* s,TTF_Font* f,const std::string& txt,int x,int y,
           Uint8 r=220,Uint8 g=204,Uint8 b=174) {
    if(!f || txt.empty()) return;
    SDL_Color color={r,g,b,0};
    SDL_Surface* text=TTF_RenderUTF8_Blended(f,txt.c_str(),color);
    if(!text)return;
    SDL_Rect dest;dest.x=x;dest.y=y;
    SDL_BlitSurface(text,NULL,s,&dest);
    SDL_FreeSurface(text);
}
bool Hit(int x,int y,int a,int b,int w,int h) {
    return x>=a && x<a+w && y>=b && y<b+h;
}
void Button(SDL_Surface* s,TTF_Font* f,const std::string& txt,int x,int y,int w,
            bool enabled=true) {
    Box(s,x-2,y-2,w+4,50,125,113,94);
    Box(s,x,y,w,46,enabled?87:58,enabled?78:55,enabled?63:49);
    Label(s,f,txt,x+12,y+7,enabled?245:149,enabled?220:136,enabled?176:115);
}

class Lobby {
public:
    Lobby(bool is_host,const std::string& map,const std::string& rawname)
      : host(is_host),map_name(Basename(map)),name(Clean(rawname,24)),mode(is_host?HOST:SEARCH),
        screen(NULL),font(NULL),small(NULL),name_font(NULL),nav_font(NULL),
         menu_background(NULL),clean_background(NULL),quit_hover(NULL),nav_selected(3),host_team(RED_TEAM),my_team(is_host?RED_TEAM:BLUE_TEAM),
        ready(false),joined(false),started(false),running(true),last_query(0),last_ping(0),
        last_update(0),chosen(0),scroll_offset(0),host_port(LAN_PORT),last_join(0),typing(false),click_audio(false),click_sound(NULL),click_volume(100),hover_action(0),click_x(0),click_y(0) {
        if(name.empty()) name=is_host?"Host":"Player";
        if(host) {
            Person me;me.name=name;me.team=RED_TEAM;me.ready=true;people.push_back(me);
        }
    }
    bool Run(ZLanLobbyResult& out) {
        if(!net.Open(host)) {
            fprintf(stderr,"ZOD LAN: could not open UDP socket port %d. Check Windows Firewall.\n",LAN_PORT);
            return false;
        }
        lan_ip=net.LocalIP();
        // Preserve the active main-menu SDL video surface on Host/Join.
        // Standalone launch remains supported as a fallback.
        if(!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO))
            if(SDL_Init(SDL_INIT_VIDEO)<0) return false;
        screen=SDL_GetVideoSurface();
        if(!screen || screen->w!=1280 || screen->h!=720)
            screen=SDL_SetVideoMode(1280,720,32,SDL_SWSURFACE | SDL_FULLSCREEN);
        if(!screen) {SDL_Quit();return false;}
        SDL_WM_SetCaption("ZOD - LAN Lobby","ZOD");
        SDL_ShowCursor(SDL_ENABLE);
        SDL_WM_GrabInput(SDL_GRAB_OFF);
        if(TTF_Init()<0) {SDL_Quit();return false;}
        // Use the EXACT same background and fonts as the new main menu.
        // The network lobby remains a separate state; the game and UDP logic are untouched.
        menu_background=IMG_Load("assets/menu/zod_main_menu.png");
        clean_background=IMG_Load("assets/menu/zod_main_menu_clean.png");
        if(clean_background && (clean_background->w != 1280 || clean_background->h != 720)) {
            SDL_FreeSurface(clean_background);
            clean_background=NULL;
        }
        if(!menu_background || !clean_background) {
            fprintf(stderr,"ZOD LAN: required menu artwork missing/invalid; aborting lobby safely.\n");
            if(menu_background) SDL_FreeSurface(menu_background);
            if(clean_background) SDL_FreeSurface(clean_background);
            menu_background=NULL;
            clean_background=NULL;
            TTF_Quit();SDL_Quit();return false;
        }
        font=TTF_OpenFont("assets/menu/Oxanium-Bold.ttf",23);
        small=TTF_OpenFont("assets/Arimo-Regular.ttf",17);
        name_font=TTF_OpenFont("assets/menu/Oxanium-Bold.ttf",23);
        nav_font=TTF_OpenFont("assets/menu/Oxanium-Bold.ttf",28);
        quit_hover=IMG_Load("assets/menu/zod_quit_hover.png");
        if(!font || !small || !name_font || !nav_font) {
            if(font)TTF_CloseFont(font);
            if(small)TTF_CloseFont(small);
            if(name_font)TTF_CloseFont(name_font);
            if(nav_font)TTF_CloseFont(nav_font);
            if(quit_hover)SDL_FreeSurface(quit_hover);
            if(menu_background)SDL_FreeSurface(menu_background);
            if(clean_background)SDL_FreeSurface(clean_background);
            TTF_Quit();SDL_Quit();return false;
        }
        int old_unicode=SDL_EnableUNICODE(1);
        int old_delay=0,old_interval=0;
        SDL_GetKeyRepeat(&old_delay,&old_interval);
        SDL_EnableKeyRepeat(220,70);
        click_volume=ZodLoadSavedVolumes().sound;
        click_audio=SDL_InitSubSystem(SDL_INIT_AUDIO)==0 && Mix_OpenAudio(22050,AUDIO_S16SYS,2,4096)==0;
        if(click_audio) {
            click_sound=Mix_LoadWAV("assets/sounds/CLICK1L.wav");
            if(click_sound) Mix_VolumeChunk(click_sound,(75*click_volume+50)/100);
        }
        status=host?"Share LAN IP or let friends find this game.":"Searching for LAN games...";
        while(running) {
            Uint32 now=SDL_GetTicks();
            Poll(now);
            if(!running) break;
            Events();
            Draw();
            SDL_Delay(16);
        }
        // A host leaving BEFORE game start must return every connected guest
        // to the LAN search screen immediately (not back to Windows/menu).
        if(host && !started)
        {
            for(size_t i=1; i<people.size(); ++i)
                for(int retry=0; retry<3; ++retry)
                    net.Send(people[i].ip, people[i].port, "ZOD1|HOST_LEFT");
        }
        if(click_audio) {
            Mix_HaltChannel(-1);
            if(click_sound) Mix_FreeChunk(click_sound);
            Mix_CloseAudio();
        }
        SDL_EnableUNICODE(old_unicode);
        SDL_EnableKeyRepeat(old_delay,old_interval);
        TTF_CloseFont(font);TTF_CloseFont(small);TTF_CloseFont(name_font);
        TTF_CloseFont(nav_font);
        if(quit_hover)SDL_FreeSurface(quit_hover);
        if(menu_background)SDL_FreeSurface(menu_background);
        if(clean_background)SDL_FreeSurface(clean_background);
        menu_background=NULL;
        clean_background=NULL;
        TTF_Quit();
        // Keep the existing fullscreen SDL window through the LAN launch.
        // ZPlayer::InitSDL will configure the game renderer. Tearing video
        // down here causes an unnecessary Windows desktop/fullscreen bounce.
        // If we are leaving, send an explicit disconnect before closing the socket.
        if(!host && joined && !started) net.Send(host_ip,host_port,"ZOD1|LEAVE");
        net.Close();
        if(!started) return false;
        out.host_ip=host?lan_ip:host_ip;
        out.team=host?host_team:my_team;
        return true;
    }
private:
    enum Screen { HOST, SEARCH, GUEST };
    bool host;
    std::string map_name,name,lan_ip,host_ip,status,input_ip;
    Screen mode;
    SDL_Surface* screen;
    TTF_Font* font;
    TTF_Font* small;
    TTF_Font* name_font;
    TTF_Font* nav_font;
    SDL_Surface* menu_background;
    SDL_Surface* clean_background;
    SDL_Surface* quit_hover;
    int nav_selected;
    LanNet net;
    std::vector<Person> people;
    std::vector<HostOffer> offers;
    int host_team,my_team;
    bool ready,joined,started,running;
    Uint32 last_query,last_ping,last_update;
    int chosen;
    int scroll_offset; // Visual-only; never changes game team/connection IDs.
    unsigned short host_port;
    Uint32 last_join;
    bool typing;
    bool click_audio;
    Mix_Chunk* click_sound;
    int click_volume, hover_action, click_x, click_y;
    ZodActionButton action_button;
    std::string queued_address;
    enum LobbyAction { LA_BACK=7, LA_HOST_COLOR, LA_HOST_START, LA_GUEST_COLOR,
                       LA_GUEST_READY, LA_JOIN, LA_DIRECT };

    int FindGuest(const std::string& ip,unsigned short port) const {
        for(size_t i=1;i<people.size();i++)
            if(people[i].ip==ip && people[i].port==port) return (int)i;
        return -1;
    }
    bool ColorFree(int team,int exception=-1) const {
        for(size_t i=0;i<people.size();i++)
            if((int)i!=exception && people[i].team==team) return false;
        return true;
    }
    int AvailableColor(int preferred,int except=-1) const {
        if(ColorIndex(preferred)>=0 && ColorFree(preferred,except)) return preferred;
        for(int i=0;i<COLOR_COUNT;i++) if(ColorFree(COLORS[i],except))return COLORS[i];
        return -1;
    }
    std::string StatePacket() const {
        std::string msg="ZOD1|STATE|"+map_name+"|"+ToText((int)people.size());
        for(size_t i=0;i<people.size();i++) {
            msg += "|"+Clean(people[i].name,24)+"|"+ToText(people[i].team)+"|"+(people[i].ready?"1":"0");
        }
        return msg;
    }
    void BroadcastState() {
        if(!host) return;
        std::string msg=StatePacket();
        for(size_t i=1;i<people.size();i++) net.Send(people[i].ip,people[i].port,msg);
    }
    void HostMessage(const std::string& ip,unsigned short port,const std::vector<std::string>& v,Uint32 now) {
        if(v.size()<2 || v[0]!="ZOD1") return;
        if(v[1]=="DISCOVER") {
            net.Send(ip,port,"ZOD1|HERE|"+name+"|"+map_name+"|"+ToText((int)people.size()));
            return;
        }
        if(v[1]=="JOIN" && v.size()>=4) {
            int which=FindGuest(ip,port);
            if(which<0) {
                if(people.size()>=MAX_GUESTS+1) {
                    net.Send(ip,port,"ZOD1|FULL");return;
                }
                Person guest;guest.ip=ip;guest.port=port;
                guest.name=Clean(v[2],24);if(guest.name.empty())guest.name="Player";
                guest.team=AvailableColor(ToInt(v[3],BLUE_TEAM));
                guest.ready=false;guest.seen=now;
                people.push_back(guest);
                BroadcastState();
            } else people[which].seen=now;
            which=FindGuest(ip,port);
            net.Send(ip,port,"ZOD1|YOU|"+ToText(people[which].team)+"|"+(people[which].ready?"1":"0"));
            net.Send(ip,port,StatePacket());return;
        }
        int which=FindGuest(ip,port);
        if(which<0) return;
        people[which].seen=now;
        if(v[1]=="LEAVE") {people.erase(people.begin()+which);BroadcastState();}
        else if(v[1]=="READY" && v.size()>=3) {
            people[which].ready=(v[2]=="1");
            net.Send(ip,port,"ZOD1|YOU|"+ToText(people[which].team)+"|"+(people[which].ready?"1":"0"));
            BroadcastState();
        } else if(v[1]=="COLOR" && v.size()>=3) {
            int request=ToInt(v[2],-1);
            if(ColorIndex(request)>=0 && ColorFree(request,which)) {
                people[which].team=request;
                people[which].ready=false;
            }
            net.Send(ip,port,"ZOD1|YOU|"+ToText(people[which].team)+"|"+(people[which].ready?"1":"0"));
            BroadcastState();
        } else if(v[1]=="PING") {
            net.Send(ip,port,"ZOD1|YOU|"+ToText(people[which].team)+"|"+(people[which].ready?"1":"0"));
            net.Send(ip,port,StatePacket());
        }
    }
    void ClientMessage(const std::string& ip,unsigned short port,const std::vector<std::string>& v,Uint32 now) {
        if(v.size()<2 || v[0]!="ZOD1")return;
        if(v[1]=="HERE" && mode==SEARCH && v.size()>=5) {
            if(ToInt(v[4])>MAX_GUESTS+1 || ToInt(v[4])<1) return;
            size_t i=0;
            for(;i<offers.size();i++) if(offers[i].ip==ip)break;
            HostOffer o;o.ip=ip;o.name=Clean(v[2],24);o.map=Clean(v[3],38);
            o.count=ToInt(v[4]);o.seen=now;
            if(i<offers.size())offers[i]=o;
            else if(offers.size()<8)offers.push_back(o);
            return;
        }
        if(mode!=GUEST || ip!=host_ip || port!=host_port) return;
        if(v[1]=="HOST_LEFT") {
            joined=false;
            ready=false;
            people.clear();
            scroll_offset=0;
            host_ip.clear();
            mode=SEARCH;
            typing=false;
            last_query=0;
            last_update=now;
            offers.clear();
            chosen=0;
            scroll_offset=0;
            status="Host left the lobby. Searching for another game...";
            return;
        }
        if(v[1]=="FULL") {status="This lobby is full.";mode=SEARCH;joined=false;return;}
        if(v[1]=="YOU" && v.size()>=4) {
            int assigned=ToInt(v[2],-1);
            if(ColorIndex(assigned)>=0) {my_team=assigned;ready=(v[3]=="1");}
            return;
        }
        if(v[1]=="START" && v.size()>=3 && joined) {
            int team=ToInt(v[2],-1);
            if(ColorIndex(team)<0) return;
            my_team=team;started=true;running=false;return;
        }
        if(v[1]=="STATE" && v.size()>=7) {
            int n=ToInt(v[3]);
            if(n<1 || n>MAX_GUESTS+1 || v.size()!=(size_t)(4+n*3))return;
            std::vector<Person> fresh;
            for(int i=0;i<n;i++) {
                Person p;p.name=Clean(v[4+i*3],24);
                p.team=ToInt(v[5+i*3],-1);
                p.ready=(v[6+i*3]=="1");
                if(ColorIndex(p.team)<0)return;
                fresh.push_back(p);
            }
            people=fresh;
            map_name=Clean(v[2],38);
            joined=true;last_update=now;
            // YOU packets carry the authoritative color and Ready status.
            // Player names need not be unique on the LAN.
            status="Connected. Select a color and press Ready.";
        }
    }
    void Poll(Uint32 now) {
        std::string ip,msg;unsigned short port;
        for(int i=0;i<64 && net.Recv(ip,port,msg);i++) {
            std::vector<std::string> v=Split(msg);
            if(host) HostMessage(ip,port,v,now);
            else ClientMessage(ip,port,v,now);
        }
        if(host) {
            bool changed=false;
            for(size_t i=1;i<people.size();) {
                if(now-people[i].seen>12000) {people.erase(people.begin()+i);changed=true;}
                else i++;
            }
            if(changed) BroadcastState();
        } else if(mode==SEARCH && now-last_query>1400) {
            net.Send("255.255.255.255",LAN_PORT,"ZOD1|DISCOVER");
            last_query=now;
            for(size_t i=0;i<offers.size();) {
                if(now-offers[i].seen>6000)offers.erase(offers.begin()+i);
                else i++;
            }
            if(chosen>=(int)offers.size())chosen=offers.empty()?0:(int)offers.size()-1;
        } else if(mode==GUEST) {
            if(!joined && now-last_join>1100) {
                net.Send(host_ip,host_port,"ZOD1|JOIN|"+name+"|"+ToText(my_team));
                last_join=now;
            }
            if(joined && now-last_ping>2000) {
                net.Send(host_ip,host_port,"ZOD1|PING");last_ping=now;
            }
            if(joined && now-last_update>13000) {
                status="Host disconnected. Searching for another game...";
                joined=false;ready=false;people.clear();
                mode=SEARCH;last_query=0;offers.clear();chosen=0;
            } else if(!joined && now-last_update>9000) {
                status="No reply from host. Searching for LAN games...";
                mode=SEARCH;last_query=0;offers.clear();chosen=0;
            }
        }
    }
    void Join(const std::string& address) {
        if(address.empty())return;
        host_ip=address;host_port=LAN_PORT;
        mode=GUEST;joined=false;ready=false;
        people.clear();my_team=BLUE_TEAM;scroll_offset=0;
        last_join=0;last_update=SDL_GetTicks();last_ping=0;
        status="Contacting host " + host_ip + "...";
    }
    bool AllReady() const {
        if(!host || people.size()<2) return false;
        for(size_t i=1;i<people.size();i++) if(!people[i].ready)return false;
        return true;
    }
    void BeginGame() {
        if(!AllReady()) {status="All guests must press Ready first.";return;}
        // Send several copies so packet loss on Wi-Fi is less likely.
        for(int retry=0;retry<7;retry++) {
            for(size_t i=1;i<people.size();i++)
                net.Send(people[i].ip,people[i].port,"ZOD1|START|"+ToText(people[i].team));
            SDL_Delay(55);
        }
        started=true;running=false;
    }
    void MyNextColor() {
        if(host) {
            int old=ColorIndex(host_team);
            for(int step=1;step<=COLOR_COUNT;step++) {
                int next=COLORS[(old+step)%COLOR_COUNT];
                if(ColorFree(next,0)) {host_team=next;people[0].team=next;BroadcastState();return;}
            }
        } else if(joined) {
            int old=ColorIndex(my_team);
            for(int step=1;step<=COLOR_COUNT;step++) {
                int next=COLORS[(old+step)%COLOR_COUNT];
                bool occupied=false;
                for(size_t i=0;i<people.size();i++)
                    if(people[i].team==next)occupied=true;
                if(!occupied && next!=my_team) {
                    ready=false;
                    net.Send(host_ip,host_port,"ZOD1|COLOR|"+ToText(next));return;
                }
            }
        }
    }
    // Integrated lobby: every lobby control is INSIDE the right panel.
    // The active main navigation is redrawn on the left each frame.
    static const int LOBBY_PANEL_X=699;
    static const int LOBBY_PANEL_W=348;
    static const int LOBBY_LIST_X=710;
    static const int LOBBY_LIST_Y=346;
    static const int LOBBY_ROW_H=34;
    static const int LOBBY_ROWS=4;
    static const int LOBBY_COLOR_X=713;
    static const int LOBBY_ACTION_X=878;
    static const int LOBBY_BUTTON_Y=511;
    static const int LOBBY_BUTTON_W=156;
    static const int LOBBY_IP_X=713;
    static const int LOBBY_IP_Y=482;
    static const int LOBBY_ARROW_X=713;
    static const int LOBBY_ARROW_Y=267;

    static int MainButtonAt(int x,int y) {
        const int ys[6]={198,247,296,345,394,528};
        for(int i=0;i<6;i++) {
            int bx=(i==5)?330:230;
            int bw=(i==5)?205:275;
            int bh=(i==5)?54:45;
            if(Hit(x,y,bx,ys[i],bw,bh))return i;
        }
        return -1;
    }
    int ActionAt(int x,int y) const {
        const int nav=MainButtonAt(x,y);
        if(nav>=0) return nav+1;
        if(Hit(x,y,LOBBY_ARROW_X-6,LOBBY_ARROW_Y-7,40,41)) return LA_BACK;
        if(Hit(x,y,LOBBY_COLOR_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,38)) {
            if(mode==HOST) return LA_HOST_COLOR;
            if(mode==GUEST) return joined?LA_GUEST_COLOR:0;
            return !offers.empty() && chosen>=0 && chosen<(int)offers.size()?LA_JOIN:0;
        }
        if(Hit(x,y,LOBBY_ACTION_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,38)) {
            if(mode==HOST) return LA_HOST_START;
            if(mode==GUEST) return joined?LA_GUEST_READY:0;
            return LA_DIRECT;
        }
        return 0;
    }
    void QueueAction(int x,int y) {
        const int id=ActionAt(x,y);
        if(id==LA_HOST_START && !AllReady()) {BeginGame();return;}
        if(!action_button.Queue(id,SDL_GetTicks())) return;
        click_x=x;click_y=y;
        queued_address=id==LA_JOIN?offers[chosen].ip:std::string();
        if(click_sound && click_volume>0) Mix_PlayChannel(-1,click_sound,0);
    }
    unsigned ActionDelay() const {
        const int id=action_button.pending;
        const bool exits=(id>=1 && id<=LA_BACK) || id==LA_HOST_START;
        return exits && click_sound && click_volume>0?350:110;
    }
    void DrawNavigation() {
        // Match exactly the button font, positions and blue hover of RunZStartMenu.
        static const char* captions[5]={"Start Game","Replay Level","Select Map","Multiplayer","Map Editor"};
        static const int ys[5]={203,252,301,350,399};
        if((nav_selected==5 || action_button.Visual(6,hover_action)) && quit_hover) {
            SDL_Rect p;p.x=310;p.y=506;p.w=0;p.h=0;
            SDL_BlitSurface(quit_hover,NULL,screen,&p);
            if(action_button.Visual(6,hover_action)==2) {
                Box(screen,330,528,205,2,13,55,92);
                Box(screen,330,528,2,54,13,55,92);
            }
        }
        for(int i=0;i<5;i++) {
            const int state=action_button.Visual(i+1,hover_action);
            const int dx=state==2?1:0,dy=state==2?2:0;
            Label(screen,nav_font,captions[i],281+dx,ys[i]+2+dy,5,7,12);
            if(nav_selected==i || state) {
                Label(screen,nav_font,captions[i],279+dx,ys[i]+dy,0,65,145);
                Label(screen,nav_font,captions[i],281+dx,ys[i]+dy,0,65,145);
                Label(screen,nav_font,captions[i],280+dx,ys[i]+dy,state==1?110:65,state==1?225:195,255);
            } else Label(screen,nav_font,captions[i],280,ys[i],215,216,222);
        }
    }
    int OfferScroll() const {
        if(chosen < LOBBY_ROWS) return 0;
        return chosen - LOBBY_ROWS + 1;
    }
    std::string Fit(TTF_Font* f,const std::string& s,int max_width) const {
        if(!f || max_width<=0) return "";
        int w=0,h=0;
        if(TTF_SizeUTF8(f,s.c_str(),&w,&h)==0 && w<=max_width)return s;
        std::string shortened=s;
        while(!shortened.empty()) {
            shortened.erase(shortened.size()-1);
            std::string test=shortened+"...";
            if(TTF_SizeUTF8(f,test.c_str(),&w,&h)==0 && w<=max_width)
                return test;
        }
        return "";
    }
    void Fitted(const std::string& s,int x,int y,int width,
                Uint8 r=212,Uint8 g=218,Uint8 b=227) {
        Label(screen,small,Fit(small,s,width),x,y,r,g,b);
    }
    void Inset(int x,int y,int w,int h) {
        Box(screen,x,y,w,h,75,88,99);
        Box(screen,x+2,y+2,w-4,h-4,13,20,29);
    }
    void NeoButton(const std::string& caption,int x,int y,int width,bool enabled) {
        const int state=enabled?action_button.Visual(ActionAt(x+width/2,y+19),hover_action):0;
        const bool over=state==1,pressed=state==2;
        Box(screen,x,y,width,38,enabled?(over?120:pressed?48:70):48,
            enabled?(over?200:pressed?95:145):67,enabled?(over?240:pressed?135:191):82);
        Box(screen,x+2,y+2,width-4,34,enabled?(over?28:pressed?9:18):24,
            enabled?(over?75:pressed?30:50):33,enabled?(over?104:pressed?48:75):41);
        const std::string label=Fit(small,caption,width-6);
        int tw=0,th=0;
        TTF_SizeUTF8(small,label.c_str(),&tw,&th);
        Label(screen,small,label,x+(width-tw)/2+(pressed?1:0),y+(38-th)/2+(pressed?2:0),
              over?170:105,enabled?(over?243:216):119,enabled?255:130);
    }
    void DrawHeaderAndStatus() {
        SDL_BlitSurface(clean_background,NULL,screen,NULL);
        // Restore the approved Name artwork to its original coordinates.
        SDL_Rect source={706,187,340,64};
        SDL_Rect dest={706,187,0,0};
        SDL_BlitSurface(menu_background,&source,screen,&dest);
        DrawNavigation();
        // Name is always visible in the original frame.
        SDL_Rect old_clip;SDL_GetClipRect(screen,&old_clip);
        SDL_Rect clip={828,205,207,35};SDL_SetClipRect(screen,&clip);
        Label(screen,name_font,Fit(name_font,name,195),834,209,225,229,237);
        SDL_SetClipRect(screen,&old_clip);
        // Back arrow returns to MULTIPLAYER (not the settings screen).
        const int back=action_button.Visual(LA_BACK,hover_action);
        Label(screen,font,"<",LOBBY_ARROW_X+(back==2),LOBBY_ARROW_Y+(back==2?2:0),back==1?170:96,back==1?243:211,255);
        Box(screen,LOBBY_ARROW_X,LOBBY_ARROW_Y+27,23,2,24,94,137);
        const char* title=mode==SEARCH?"JOIN LOBBY":(host?"HOST LOBBY":"JOIN LOBBY");
        int width=0,height=0;
        TTF_SizeUTF8(font,title,&width,&height);
        Label(screen,font,title,870-width/2,264,91,207,252);
        Box(screen,710,292,328,1,55,98,132);
        // Hide map name and player count: only information needed by the user.
        if(host) Fitted(std::string("LAN IP: ")+lan_ip,715,302,318,110,205,247);
        else if(mode==GUEST) Fitted(std::string("Host IP: ")+host_ip,715,302,318,110,205,247);
        else Fitted("Available LAN games",715,302,318,110,205,247);
        if(mode==SEARCH) Fitted("Select a host or use Direct IP",715,320,318,163,182,200);
        else Fitted(host?"Waiting for players...":"Connected to host",715,320,318,163,182,200);
    }
    // Host: pinned at row 0; three scrollable guest rows.
    // Guest: host at row 0, self at row 1, two scrollable other-player rows.
    // Only screen indices move; teams, host/guest identity and network packets
    // retain their authoritative server-managed values.
    int SelfIndex() const {
        if(host || !joined) return -1;
        for(size_t i=1;i<people.size();++i)
            if(people[i].team==my_team) return (int)i; // unique colors
        return -1;
    }
    int MaxPlayerScroll() const {
        const int fixed=host ? 1 : (joined ? 2 : 1);
        const int scrolled=(int)people.size()-fixed;
        const int slots=LOBBY_ROWS-fixed;
        return std::max(0,scrolled-slots);
    }
    std::vector<int> VisiblePlayerIndices() const {
        std::vector<int> visible,others;
        if(people.empty()) return visible;
        visible.push_back(0); // host always first
        const int mine=SelfIndex();
        if(!host && joined) visible.push_back(mine>=0?mine:-2);
        for(size_t i=1;i<people.size();++i) {
            if((int)i!=mine) others.push_back((int)i);
        }
        const int slots=LOBBY_ROWS-(int)visible.size();
        const int max_offset=std::max(0,(int)others.size()-slots);
        const int begin=std::min(scroll_offset,max_offset);
        for(int row=0; row<slots && begin+row<(int)others.size();++row)
            visible.push_back(others[begin+row]);
        return visible;
    }
    void DrawPlayers() {
        // Disconnections may shrink the roster while the viewer is scrolled.
        if(scroll_offset > MaxPlayerScroll()) scroll_offset=MaxPlayerScroll();
        static const Uint8 team_rgb[8][3]={
            {227,63,62}, {46,154,236}, {60,193,99}, {220,186,53},
            {157,108,191}, {57,192,182}, {228,232,235}, {89,96,105}
        };
        const int mine=SelfIndex();
        const std::vector<int> visible=VisiblePlayerIndices();
        for(int row=0;row<LOBBY_ROWS;row++) {
            const int y=LOBBY_LIST_Y+row*LOBBY_ROW_H;
            Inset(710,y-1,328,32);
            if(row<(int)visible.size()) {
                const int idx=visible[row];
                // During a brief STATE/YOU reordering, keep "YOU" pinned even
                // before the host's new color assignment packet arrives.
                Person provisional;
                provisional.name=name;
                provisional.team=my_team;
                provisional.ready=ready;
                const Person& p=idx==-2 ? provisional : people[idx];
                const bool is_me=host ? idx==0 : (idx==mine && mine>=0) || idx==-2;
                const std::string shown_name=is_me && !host ? "YOU: "+p.name : p.name;
                if(is_me) Box(screen,714,y+3,3,24,58,175,228);
                Fitted(shown_name,719,y+5,142,212,222,235);
                const int ci=ColorIndex(p.team);
                const Uint8* team_color=team_rgb[ci>=0?ci:0];
                Box(screen,870,y+9,9,12,team_color[0],team_color[1],team_color[2]);
                Fitted(ColorName(p.team),884,y+5,70,team_color[0],team_color[1],team_color[2]);
                Label(screen,small,p.ready?"READY":"WAIT",962,y+5,
                      p.ready?104:185,p.ready?223:176,p.ready?160:151);
            } else {
                Fitted("Waiting for player...",719,y+5,270,101,119,134);
            }
        }
        // Lightweight page indicator uses the existing empty corner above
        // the list rather than adding any row or enlarging the lobby.
        if(MaxPlayerScroll()>0) {
            const int fixed=host ? 1 : 2;
            const int scrolled=(int)people.size()-fixed;
            const int slots=LOBBY_ROWS-fixed;
            const int first=std::min(scroll_offset,MaxPlayerScroll())+1;
            const int last=std::min(scrolled,first+slots-1);
            Fitted(ToText(first)+"-"+ToText(last)+" / "+ToText(scrolled),963,321,70,110,205,247);
        }
        Fitted(status,713,486,322,160,178,198);
        NeoButton("COLOR",LOBBY_COLOR_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,true);
        if(host) NeoButton("START GAME",LOBBY_ACTION_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,AllReady());
        else NeoButton(ready?"NOT READY":"READY",LOBBY_ACTION_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,joined);
    }
    void DrawSearch() {
        const int top=OfferScroll();
        for(int row=0;row<LOBBY_ROWS;row++) {
            const int i=top+row;
            const int y=LOBBY_LIST_Y+row*LOBBY_ROW_H;
            Inset(710,y-1,328,32);
            if(i<(int)offers.size()) {
                if(i==chosen) {
                    Box(screen,713,y+1,321,27,23,67,108);
                    Box(screen,713,y+1,3,27,73,204,255);
                }
                Fitted(offers[i].name,720,y+5,275,220,232,246);
            } else if(offers.empty() && row==1) {
                Fitted("Searching for LAN hosts...",720,y+5,286,166,185,203);
            }
        }
        if(typing) {
            Inset(LOBBY_IP_X,LOBBY_IP_Y,324,26);
            Fitted(input_ip+"_",719,486,308,206,222,237);
        } else {
            Fitted(status,713,486,322,160,178,198);
        }
        NeoButton("JOIN GAME",LOBBY_COLOR_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,!offers.empty());
        NeoButton(typing?"CONNECT IP":"DIRECT IP",LOBBY_ACTION_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,true);
    }
    void ReturnToMultiplayer() {
        ZStartMenuQueueNavigationFromLobby(3);
        running=false;
    }
    void Events() {
        SDL_Event e;
        bool dispatch=action_button.Ready(SDL_GetTicks(),ActionDelay());
        while(dispatch || SDL_PollEvent(&e)) {
            const bool committed=dispatch;
            if(committed) {
                dispatch=false;
                const int id=action_button.pending;
                action_button.Reset();
                if(ActionAt(click_x,click_y)!=id) continue;
                if(id==LA_JOIN) {
                    // Discovery can reorder/remove offers while the button animates.
                    // Never join a different host from the one originally clicked.
                    int index=-1;
                    for(size_t i=0;i<offers.size();++i) if(offers[i].ip==queued_address) index=(int)i;
                    if(index<0) continue;
                    chosen=index;
                }
                e=SDL_Event(); e.type=SDL_MOUSEBUTTONUP;e.button.button=SDL_BUTTON_LEFT;
                e.button.x=click_x;e.button.y=click_y;
            }
            if(e.type==SDL_QUIT) {
                ZStartMenuQueueNavigationFromLobby(5);
                running=false;
                return;
            }
            if(action_button.pending) continue;
            if(e.type==SDL_ACTIVEEVENT && !e.active.gain && (e.active.state&(SDL_APPINPUTFOCUS|SDL_APPMOUSEFOCUS)))
                action_button.CancelHold();
            else if(!committed && e.type==SDL_MOUSEBUTTONDOWN && e.button.button==SDL_BUTTON_LEFT) {
                action_button.Press(ActionAt(e.button.x,e.button.y));
            } else if(!committed && e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_LEFT && action_button.held) {
                const int held=action_button.held;action_button.CancelHold();
                if(held==ActionAt(e.button.x,e.button.y)) QueueAction(e.button.x,e.button.y);
            } else if(!committed && e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_LEFT && ActionAt(e.button.x,e.button.y)) continue;
            else if(e.type==SDL_MOUSEMOTION) {
                int over=MainButtonAt(e.motion.x,e.motion.y);
                nav_selected=(over>=0)?over:3;
            } else if(e.type==SDL_KEYDOWN) {
                SDLKey key=e.key.keysym.sym;
                if(key==SDLK_ESCAPE) {
                    QueueAction(LOBBY_ARROW_X,LOBBY_ARROW_Y);
                    continue;
                }
                if(mode==SEARCH) {
                    if(typing) {
                        if(key==SDLK_RETURN || key==SDLK_KP_ENTER) {
                            if(!input_ip.empty()) QueueAction(LOBBY_ACTION_X,LOBBY_BUTTON_Y);
                        } else if(key==SDLK_BACKSPACE && !input_ip.empty()) input_ip.erase(input_ip.size()-1);
                        else if(e.key.keysym.unicode>=33 && e.key.keysym.unicode<=126 && input_ip.size()<63) {
                            char c=(char)e.key.keysym.unicode;
                            if(isalnum((unsigned char)c) || c=='.' || c=='-') input_ip+=c;
                        }
                    } else if(key==SDLK_DOWN && chosen+1<(int)offers.size())chosen++;
                    else if(key==SDLK_UP && chosen>0)chosen--;
                    else if((key==SDLK_RETURN || key==SDLK_KP_ENTER) && !offers.empty()) QueueAction(LOBBY_COLOR_X,LOBBY_BUTTON_Y);
                } else if(mode==HOST) {
                    if(key==SDLK_RETURN || key==SDLK_KP_ENTER) QueueAction(LOBBY_ACTION_X,LOBBY_BUTTON_Y);
                    else if(key==SDLK_c) QueueAction(LOBBY_COLOR_X,LOBBY_BUTTON_Y);
                } else if(mode==GUEST) {
                    if(key==SDLK_c) QueueAction(LOBBY_COLOR_X,LOBBY_BUTTON_Y);
                    else if((key==SDLK_RETURN || key==SDLK_KP_ENTER) && joined) QueueAction(LOBBY_ACTION_X,LOBBY_BUTTON_Y);
                }
            } else if(e.type==SDL_MOUSEBUTTONDOWN &&
                      (e.button.button==SDL_BUTTON_WHEELUP || e.button.button==SDL_BUTTON_WHEELDOWN)) {
                if(mode==SEARCH) {
                    if(!offers.empty()) {
                        if(e.button.button==SDL_BUTTON_WHEELUP && chosen>0) --chosen;
                        if(e.button.button==SDL_BUTTON_WHEELDOWN && chosen+1<(int)offers.size()) ++chosen;
                    }
                } else if(Hit(e.button.x,e.button.y,LOBBY_LIST_X,LOBBY_LIST_Y,327,LOBBY_ROWS*LOBBY_ROW_H)) {
                    // Only scroll the non-pinned players in the roster region.
                    if(e.button.button==SDL_BUTTON_WHEELUP && scroll_offset>0) --scroll_offset;
                    if(e.button.button==SDL_BUTTON_WHEELDOWN && scroll_offset<MaxPlayerScroll()) ++scroll_offset;
                }
            } else if(e.type==SDL_MOUSEBUTTONUP && e.button.button==SDL_BUTTON_LEFT) {
                const int x=e.button.x,y=e.button.y;
                const int main_button=MainButtonAt(x,y);
                if(main_button>=0) {
                    ZStartMenuQueueNavigationFromLobby(main_button);
                    running=false;
                    return;
                }
                if(Hit(x,y,LOBBY_ARROW_X-6,LOBBY_ARROW_Y-7,40,41)) {
                    ReturnToMultiplayer();
                    return;
                }
                if(mode==HOST) {
                    if(Hit(x,y,LOBBY_COLOR_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,38)) MyNextColor();
                    else if(Hit(x,y,LOBBY_ACTION_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,38)) BeginGame();
                } else if(mode==GUEST) {
                    if(Hit(x,y,LOBBY_COLOR_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,38)) MyNextColor();
                    else if(Hit(x,y,LOBBY_ACTION_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,38) && joined) {
                        ready=!ready;
                        net.Send(host_ip,host_port,std::string("ZOD1|READY|")+(ready?"1":"0"));
                    }
                } else {
                    const int top=OfferScroll();
                    if(Hit(x,y,LOBBY_LIST_X,LOBBY_LIST_Y,327,LOBBY_ROWS*LOBBY_ROW_H)) {
                        int i=top+(y-LOBBY_LIST_Y)/LOBBY_ROW_H;
                        if(i>=0 && i<(int)offers.size()) chosen=i;
                    } else if(Hit(x,y,LOBBY_IP_X,LOBBY_IP_Y,324,26)) {
                        typing=true;
                    } else if(Hit(x,y,LOBBY_COLOR_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,38)) {
                        if(!offers.empty())Join(offers[chosen].ip);
                    } else if(Hit(x,y,LOBBY_ACTION_X,LOBBY_BUTTON_Y,LOBBY_BUTTON_W,38)) {
                        if(typing && !input_ip.empty()) {typing=false;Join(input_ip);}
                        else {typing=true;status="Type host IP, press Enter";}
                    }
                }
            }
        }
    }
    void Draw() {
        // Same ZOD steel-and-neon illustration and right-hand pane as the
        // working Replay / Select Map / Multiplayer main-menu panels.
        int mx=0,my=0;SDL_GetMouseState(&mx,&my);
        hover_action=ActionAt(mx,my);
        DrawHeaderAndStatus();
        if(mode==SEARCH)DrawSearch();
        else DrawPlayers();
        SDL_Flip(screen);
    }
};
} // namespace

bool RunZLanLobby(bool host,const std::string& map_path,
                  const std::string& player_name,ZLanLobbyResult& result)
{
    Lobby lobby(host,map_path,player_name);
    return lobby.Run(result);
}
