#ifndef _CLIENTSOCKET_H_
#define _CLIENTSOCKET_H_

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

#include <string>
#include "constants.h"
#include "socket_handler.h"
#include "event_handler.h"

using namespace std;
class ClientSocket
{
	public:
		ClientSocket();
		~ClientSocket();
		
		int Start(const char *address_ = "localhost", int port_ = 8000);
		void SetEventList(list<Event*> *event_list_);
		int Process();
		
		int SendMessage(int pack_id, const char *data, int size);
		int SendMessageAscii(int pack_id, const char *data);
		
		SocketHandler* GetHandler();
		void ClearConnection();
	private:
		int CreateSocket();
		int Connect();
		
		string address;
		int port;
		ZSocketHandle s;
#ifdef _WIN32
		bool winsock_initialized;
#endif
		
		SocketHandler* shandler;
		list<Event*> *event_list;
};

#endif
