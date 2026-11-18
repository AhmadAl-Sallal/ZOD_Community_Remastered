#include "server_socket.h"
#include "common.h"
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace COMMON;

ServerSocket::ServerSocket()
{
	int i;
	
	started = 0;
	bound = 0;
	listen_socket = -1;
#ifdef _WIN32
	winsock_initialized = false;
#endif
	client_socket.clear();
	
	event_list = NULL;
	ehandler = NULL;
}

// Each hosted match creates a temporary ZServer. Release every socket before
// the next match binds port 8000; otherwise an old listener remains alive.
ServerSocket::~ServerSocket()
{
	for(size_t i = 0; i < client_socket.size(); ++i)
		delete client_socket[i]; // SocketHandler destructor closes each connection.
	client_socket.clear();

	if(listen_socket != -1)
	{
#ifdef _WIN32
		closesocket(listen_socket);
#else
		close(listen_socket);
#endif
		listen_socket = -1;
	}

	bound = 0;
	started = 0;
#ifdef _WIN32
	if(winsock_initialized)
	{
		WSACleanup();
		winsock_initialized = false;
	}
#endif
}

void ServerSocket::DisconnectAll()
{
	for(size_t i=0;i<client_socket.size();++i) if(client_socket[i]) client_socket[i]->Disconnect();
}

int ServerSocket::PlayersConnected()
{
   return client_socket.size();
}

int ServerSocket::SendMessageAll(int pack_id, const char *data, int size)
{
	vector<SocketHandler*>::iterator s;

	for(s=client_socket.begin();s!=client_socket.end();s++)
		(*s)->SendMessage(pack_id, data, size);

	return 1;
}

int ServerSocket::SendMessage(int player, int pack_id, const char *data, int size)
{
	if(player >= 0 && (size_t)player < client_socket.size() && client_socket[player]) return client_socket[player]->SendMessage(pack_id, data, size);
	else return 0;
}

int ServerSocket::SendMessageAscii(int player, int pack_id, const char *data)
{
	if(player >= 0 && (size_t)player < client_socket.size() && client_socket[player]) return client_socket[player]->SendMessageAscii(pack_id, data);
	else return 0;
}

void ServerSocket::SetEventList(list<Event*> *event_list_)
{
	event_list = event_list_;
}

SocketHandler* ServerSocket::GetHandler(int id)
{
	return id >= 0 && (size_t)id < client_socket.size() ? client_socket[id] : NULL;
}

int ServerSocket::Process()
{
	//check for connects
	CheckConnects();
	
	//check for data
	CheckData();

	return 1;
}

int ServerSocket::CheckData()
{
	int i;
	char *message;
	int size;
	int pack_id;
	vector<SocketHandler*>::iterator the_sock;

	for(i=0, the_sock = client_socket.begin();the_sock != client_socket.end();)
		if(*the_sock)
		{
			//not connected, free it up
			if(!(*the_sock)->Connected())
			{
				delete *the_sock;
				the_sock = client_socket.erase(the_sock);


				//create an event for it
				//event_list->push_back(new Event(OTHER_EVENT, DISCONNECT_EVENT, i, NULL, 0));
				ehandler->ProcessEvent(OTHER_EVENT, DISCONNECT_EVENT, NULL, 0, i);
				continue;
			}
			else if((*the_sock)->DoRecv())
			{
				while((*the_sock)->DoFastProcess(&message, &size, &pack_id))
					ehandler->ProcessEvent(TCP_EVENT, pack_id, message, size, i);
				(*the_sock)->ResetFastProcess();

				//while((*the_sock)->DoProcess(&message, &size, &pack_id))
				//{
				//	//event_list->push_back(new Event(TCP_EVENT, pack_id, i, message, size));
				//	ehandler->ProcessEvent(TCP_EVENT, pack_id,  message, size, i);
				//	//printf("ServerSocket::CheckData:got packet from %d: id:%d\n", i, pack_id);
				//}
			}

			i++;
			the_sock++;
		}

		return 1;
}

int ServerSocket::CheckConnects()
{
	struct sockaddr_in temp_in;
	ZSocketHandle temp_id;
	int so_sndbuf_amount = 200000;
#ifdef _WIN32
	int c_temp_addrlen;
	int tst = sizeof(so_sndbuf_amount);
#else
	socklen_t c_temp_addrlen;
	socklen_t tst = sizeof(so_sndbuf_amount);
#endif
	ZSocketHandle &s = listen_socket;

	c_temp_addrlen = sizeof(temp_in);
	temp_id = accept(s,(struct sockaddr*)  &temp_in, &c_temp_addrlen);

	//accept
	if(temp_id != (ZSocketHandle)-1) //got one so lets put him in
	{
		//we want a big buffer to send the map in particular
		if(setsockopt(temp_id, SOL_SOCKET, SO_SNDBUF, (char*)&so_sndbuf_amount, sizeof(so_sndbuf_amount)) == -1)
			printf("ServerSocket::CheckConnects:error setting SO_SNDBUF to %d\n", so_sndbuf_amount);

		//testing purposes
		//if(!getsockopt(temp_id, SOL_SOCKET, SO_SNDBUF, (char*)&so_sndbuf_amount, &tst))
		//	printf("ServerSocket::CheckConnects:SO_SNDBUF:%d\n", so_sndbuf_amount);

		SocketHandler *accepted = new SocketHandler(temp_id, temp_in);
        if(!accepted->Connected()) { delete accepted; return 0; }
        client_socket.push_back(accepted);

		string connect_ip = inet_ntoa(temp_in.sin_addr);
		ehandler->ProcessEvent(OTHER_EVENT, CONNECT_EVENT, (char*)connect_ip.c_str(), connect_ip.size()+1, client_socket.size()-1);
		//event_list->push_back(new Event(OTHER_EVENT, CONNECT_EVENT, client_socket.size()-1, (char*)connect_ip.c_str(), connect_ip.size()+1));
		//event_list->push_back(new Event(OTHER_EVENT, CONNECT_EVENT, client_socket.size()-1, NULL, 0));

		printf("ServerSocket::CheckConnects:client connected:%s\n", inet_ntoa(temp_in.sin_addr));
	}

	return 1;
}

int ServerSocket::Start(int port_)
{
	port = port_;
	
	if(!CreateSocket()) return 0;
	
	if(!Bind()) return 0;
	
	if(!Listen()) return 0;
	
	started = 1;
	
	return 1;
}

int ServerSocket::CreateSocket()
{
	ZSocketHandle &s = listen_socket;
	
	if(s != -1) return 1;
	
#ifdef _WIN32
	WSADATA wsaData;
	
	if(!winsock_initialized)
	{
		int error = WSAStartup(0x202,&wsaData);
		if(error != 0)
		{
			printf("ServerSocket::CreateSocket:WSAStartup failed:%d\n", error);
			return 0;
		}
		winsock_initialized = true;
	}
#endif
	
	if((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == (ZSocketHandle)-1)
	{
		printf("ServerSocket::CreateSocket:error in socket creation\n");
		return 0;
	}

	//so the server can do a quick restart after a crash
	int on = 1;
	if(setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (char*)&on, sizeof(on)) == -1)
		printf("ServerSocket::CreateSocket:error setting SO_REUSEADDR on\n");
	
	return 1;
}

int ServerSocket::Bind()
{
	struct sockaddr_in si_me;
	ZSocketHandle &s = listen_socket;
	
	if(bound) return 1;
	
	memset((char *) &si_me, 0, sizeof(si_me));
	si_me.sin_family = AF_INET;
	si_me.sin_port = htons(port);
	si_me.sin_addr.s_addr = htonl(INADDR_ANY);
	
	int retries=0;
    while(bind(s,(struct sockaddr*)&si_me,sizeof(si_me))==-1)
    {
        if(++retries>=10) { fprintf(stderr,"ServerSocket: port %d unavailable\n",port); return 0; }
        uni_pause(100);
    }
	bound = 1; // The bind succeeded; this flag was previously never updated.
	return 1;
}

int ServerSocket::Listen()
{
	u_long on_mode = 1;
	ZSocketHandle &s = listen_socket;
	
	if(started) return 1;
	
	if(listen(s,5)==-1) { fprintf(stderr,"ServerSocket: listen failed\n"); return 0; }
	
#ifdef _WIN32
	if(ioctlsocket(s, FIONBIO, &on_mode)!=0) return 0;
#else		
	if(ioctl(s, FIONBIO, &on_mode)!=0) return 0;
#endif
	
	return 1;
}
