#ifndef _SERVERSOCKET_H_
#define _SERVERSOCKET_H_

#ifdef _WIN32
#include <windows.h>
#include <direct.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#endif

#include <vector>

#include "constants.h"
#include "socket_handler.h"
#include "event_handler.h"

using namespace std;
class ZServer;

class ServerSocket
{
	public:
		ServerSocket();
		~ServerSocket();
		int Start(int port = 8000);
		void SetEventList(list<Event*> *event_list_);
		void SetEventHandler(EventHandler<ZServer> *ehandler_) { ehandler = ehandler_; }
		int Process();
		
		int SendMessage(int player, int pack_id, const char *data, int size);
		int SendMessageAll(int pack_id, const char *data, int size);
		int SendMessageAscii(int player, int pack_id, const char *data);
		
		int PlayersConnected();
		void DisconnectAll();
		
		SocketHandler* GetHandler(int id);
	private:
		int Bind();
		int Listen();
		int CreateSocket();
		int CheckConnects();
		int CheckData();
		
		int port;
		int bound;
		int started;
		
		ZSocketHandle listen_socket;
#ifdef _WIN32
		bool winsock_initialized;
#endif
		vector<SocketHandler*> client_socket;
		list<Event*> *event_list;
		EventHandler<ZServer> *ehandler;
};

#endif
