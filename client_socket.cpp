#include "client_socket.h"
#include "common.h"
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace COMMON;

ClientSocket::ClientSocket()
{
	s = -1;
#ifdef _WIN32
	winsock_initialized = false;
#endif
	shandler = NULL;
	event_list = NULL;
}

ClientSocket::~ClientSocket()
{
	ClearConnection();
#ifdef _WIN32
	if(winsock_initialized)
	{
		WSACleanup();
		winsock_initialized = false;
	}
#endif
}

int ClientSocket::SendMessage(int pack_id, const char *data, int size)
{
	if(shandler) return shandler->SendMessage(pack_id, data, size);
	else return 0;
}

int ClientSocket::SendMessageAscii(int pack_id, const char *data)
{
	if(shandler) return shandler->SendMessageAscii(pack_id, data);
	else return 0;
}

void ClientSocket::SetEventList(list<Event*> *event_list_)
{
	event_list = event_list_;
}

int ClientSocket::Start(const char *address_, int port_)
{
	address = address_;
	port = port_;
	
	if(!CreateSocket()) return 0;
	
	if(!Connect()) return 0;
	
	return 1;
}

void ClientSocket::ClearConnection()
{
	if(shandler)
	{
		delete shandler; // Its destructor closes the active socket.
		shandler = NULL;
	}
	else if(s != -1) // Connect failed before a SocketHandler was created.
	{
#ifdef _WIN32
		closesocket(s);
#else
		close(s);
#endif
	}

	s = -1;
}

int ClientSocket::Process()
{
	char *message;
	int size;
	int pack_id;
	if(!shandler) return 0;
	
	//not connected, free it up
	if(!shandler->Connected())
	{
		ClearConnection();
		
		event_list->push_back(new Event(OTHER_EVENT, DISCONNECT_EVENT, 0, NULL, 0));
	}
	else if(shandler->DoRecv())
	{
		while(shandler->DoFastProcess(&message, &size, &pack_id))
			event_list->push_back(new Event(TCP_EVENT, pack_id, 0, message, size));
		shandler->ResetFastProcess();

		/*
		while(shandler->DoProcess(&message, &size, &pack_id))
		{
			event_list->push_back(new Event(TCP_EVENT, pack_id, 0, message, size));
 			//printf("ClientSocket::Process:got packet id:%d\n", pack_id);
		}*/
	}

	/*
	while(shandler->PacketAvailable() && shandler->GetPacket(&message, &size, &pack_id))
		event_list->push_back(new Event(TCP_EVENT, pack_id, 0, message, size));	
	*/

	return 1;
}

SocketHandler* ClientSocket::GetHandler()
{
	return shandler;
}

int ClientSocket::Connect()
{
	struct sockaddr_in c_in;
	struct hostent * host;
	
	host = gethostbyname(address.c_str());
	
	if(!host) 
	{
		printf("could not resolve host '%s'\n", address.c_str());
		return 0;
	}
	
	memcpy((char*)&c_in.sin_addr, (char*)host->h_addr, host->h_length);
	c_in.sin_family = AF_INET;
	c_in.sin_port = htons(port);

	// A local server is started on another thread. Retry briefly while it
	// opens its listener, using a NEW socket for each failed connect attempt.
	// The former static timeout carried over from match to match.
	const double first_attempt = current_time();
	while(connect(s, (struct sockaddr *)&c_in, sizeof c_in) < 0)
	{
		if(current_time() - first_attempt >= 15.0)
		{
			printf("could not connect to '%s'\n", address.c_str());
			ClearConnection();
			return 0;
		}
		ClearConnection();
		uni_pause(100);
		if(!CreateSocket()) return 0;
	}
	
	shandler = new SocketHandler(s,c_in);
	if(!shandler->Connected()) { ClearConnection(); return 0; }
	
	event_list->push_back(new Event(OTHER_EVENT, CONNECT_EVENT, 0, NULL, 0));
	
	return 1;
}

int ClientSocket::CreateSocket()
{
	if(s != -1) return 1;
	
#ifdef _WIN32
	WSADATA wsaData;
	
	if(!winsock_initialized)
	{
		int error = WSAStartup(0x202,&wsaData);
		if(error != 0)
		{
			printf("ClientSocket::CreateSocket:WSAStartup failed:%d\n", error);
			return 0;
		}
		winsock_initialized = true;
	}
#endif
	
	if((s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == (ZSocketHandle)-1)
	{
		printf("ClientSocket::CreateSocket:error in socket creation\n");
		return 0;
	}
	
	return 1;
}
