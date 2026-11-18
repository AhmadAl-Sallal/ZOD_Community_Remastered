#ifndef _WIN32
#include <unistd.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "socket_handler.h"
#include "common.h"

using namespace COMMON;

// Keep the original native 32-bit length/id wire format.
static const int frame_header_size = 8;
static const std::size_t max_send_queue = 8u * 1024u * 1024u;
static const std::size_t flush_budget = 256u * 1024u;
static_assert(sizeof(int) == 4, "ZOD wire format uses 32-bit int");

static bool socket_retryable()
{
#ifdef _WIN32
    const int e = WSAGetLastError();
    return e == WSAEWOULDBLOCK || e == WSAEINTR;
#else
    return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR;
#endif
}

SocketHandler::SocketHandler() : send_offset(0), queued_bytes(0), s((ZSocketHandle)-1), connected(0), fp_ptr(0), buf_size(0)
{
    ipaddress[0] = 0;
}

SocketHandler::SocketHandler(ZSocketHandle socket, struct sockaddr_in address) : SocketHandler()
{
    Init(socket, address);
}

SocketHandler::~SocketHandler() { Disconnect(); }

int SocketHandler::Init(ZSocketHandle socket, struct sockaddr_in address)
{
    Disconnect();
    if(socket == (ZSocketHandle)-1) return 0;
    s = socket;
    s_in = address;
    connected = 1;
    snprintf(ipaddress, sizeof(ipaddress), "%s", inet_ntoa(s_in.sin_addr));
    u_long on_mode = 1;
#ifdef _WIN32
    if(ioctlsocket(s, FIONBIO, &on_mode) != 0)
#else
    if(ioctl(s, FIONBIO, &on_mode) != 0)
#endif
    {
        Disconnect();
        return 0;
    }
    printf("socket connected:%s\n", ipaddress);
    return 1;
}

int SocketHandler::Connected() { return connected; }

int SocketHandler::Disconnect()
{
    if(connected)
    {
#ifdef _WIN32
        closesocket(s);
#else
        close(s);
#endif
        printf("socket disconnected: %s\n", ipaddress);
    }
    s = (ZSocketHandle)-1;
    connected = 0;
    ipaddress[0] = 0;
    buf_size = fp_ptr = 0;
    send_queue.clear();
    send_offset = queued_bytes = 0;
    return 1;
}

bool SocketHandler::FlushSendQueue()
{
    std::size_t budget = flush_budget;
    while(connected && !send_queue.empty() && budget)
    {
        const std::vector<char> &frame = send_queue.front();
        std::size_t remaining = frame.size() - send_offset;
        if(remaining > budget) remaining = budget;
#ifdef MSG_NOSIGNAL
        int sent = send(s, frame.data() + send_offset, (int)remaining, MSG_NOSIGNAL);
#else
        int sent = send(s, frame.data() + send_offset, (int)remaining, 0);
#endif
        if(sent < 0 && socket_retryable()) return true;
        if(sent <= 0) { Disconnect(); return false; }
        send_offset += sent;
        queued_bytes -= sent;
        budget -= sent;
        if(send_offset == frame.size())
        {
            send_queue.pop_front();
            send_offset = 0;
        }
    }
    return connected != 0;
}

int SocketHandler::DoRecv()
{
    if(!connected || !FlushSendQueue()) return 0;
    // Never discard a stream fragment. Let callers consume full frames first.
    if(fp_ptr) ResetFastProcess();
    int room = MAX_DATA_STORED - buf_size;
    if(room <= 0) return 1;
    if(room > MAX_BUF_SIZE) room = MAX_BUF_SIZE;
    int got = recv(s, buf + buf_size, room, 0);
    if(got > 0) { buf_size += got; return 1; }
    if(got < 0 && socket_retryable()) return 0;
    Disconnect();
    return 0;
}

bool SocketHandler::FrameAvailable(int offset, int &payload, int &pack_id)
{
    if(!connected || offset < 0 || offset > buf_size) return false;
    if(buf_size - offset < frame_header_size) return false;
    memcpy(&payload, buf + offset, sizeof(int));
    memcpy(&pack_id, buf + offset + sizeof(int), sizeof(int));
    if(payload < 0 || payload > MAX_DATA_STORED - frame_header_size)
    {
        printf("SocketHandler: invalid frame length %d\n", payload);
        Disconnect();
        return false;
    }
    return payload <= buf_size - offset - frame_header_size;
}

int SocketHandler::DoFastProcess(char **message, int *size, int *pack_id)
{
    if(!FrameAvailable(fp_ptr, *size, *pack_id)) return 0;
    *message = buf + fp_ptr + frame_header_size;
    fp_ptr += frame_header_size + *size;
    return 1;
}

void SocketHandler::ResetFastProcess()
{
    if(fp_ptr < 0 || fp_ptr > buf_size) { Disconnect(); return; }
    if(!fp_ptr) return;
    const int remaining = buf_size - fp_ptr;
    if(remaining) memmove(buf, buf + fp_ptr, remaining);
    buf_size = remaining;
    fp_ptr = 0;
}

int SocketHandler::DoProcess(char **message, int *size, int *pack_id)
{
    char *source;
    if(!DoFastProcess(&source, size, pack_id)) return 0;
    if(*size) memcpy(dp_temp_buf, source, *size);
    *message = dp_temp_buf;
    ResetFastProcess();
    return 1;
}

int SocketHandler::PacketAvailable()
{
    int size, id;
    if(FrameAvailable(fp_ptr, size, id)) return 1;
    if(!DoRecv()) return 0;
    return FrameAvailable(fp_ptr, size, id);
}

int SocketHandler::GetPacket(char **message, int *size, int *pack_id)
{
    if(!PacketAvailable()) return 0;
    return DoProcess(message, size, pack_id);
}

int SocketHandler::SendMessage(int pack_id, const char *data, int size)
{
    if(!connected) return 0;
    if(size < 0 || size > MAX_DATA_STORED - frame_header_size || (size && !data)) return 0;
    if(!FlushSendQueue()) return 0;
    const std::size_t total = frame_header_size + (std::size_t)size;
    if(total > max_send_queue - queued_bytes)
    {
        printf("SocketHandler: send queue limit exceeded\n");
        Disconnect();
        return 0;
    }
    try
    {
        send_queue.push_back(std::vector<char>(total));
    }
    catch(const std::bad_alloc &) { Disconnect(); return 0; }
    std::vector<char> &frame = send_queue.back();
    memcpy(frame.data(), &size, sizeof(int));
    memcpy(frame.data() + sizeof(int), &pack_id, sizeof(int));
    if(size) memcpy(frame.data() + frame_header_size, data, size);
    queued_bytes += total;
    return FlushSendQueue() ? 1 : 0;
}

int SocketHandler::SendMessageAscii(int pack_id, const char *data)
{
    if(!data || strlen(data) >= MAX_DATA_STORED - frame_header_size) return 0;
    return SendMessage(pack_id, data, (int)strlen(data) + 1);
}

char *SocketHandler::GetMAC(char *buf)
{
	//clear
	memset(buf, 0, 6);

#ifdef _WIN32
	IP_ADAPTER_INFO AdapterInfo[16];

	DWORD dwBufLen = sizeof(AdapterInfo);

	DWORD dwStatus = GetAdaptersInfo(AdapterInfo, &dwBufLen);
	if(dwStatus == ERROR_SUCCESS) 
	{
		PIP_ADAPTER_INFO pAdapterInfo = AdapterInfo;

		do 
		{
			for(int i=0; i<6;i++)
				if(pAdapterInfo->Address[i])
				{
					memcpy(buf, pAdapterInfo->Address, 6);
					return buf;
				}

			pAdapterInfo = pAdapterInfo->Next;
		}
		while(pAdapterInfo);
	}
#else
	int skfd;
	struct ifreq sIfReq;
	struct if_nameindex *pIfList, *pIfList_tofree;

	//create socket
	skfd = socket(AF_INET,SOCK_DGRAM,0);
	if(skfd<0)
	{
		printf( "SocketHandler::GetMAC: create socket failed\n");
		return buf;
	}

	pIfList = if_nameindex();
	pIfList_tofree = pIfList;

	for ( pIfList; *(char *)pIfList != 0; pIfList++ )
	{
		strncpy( sIfReq.ifr_name, pIfList->if_name, IF_NAMESIZE );

		if ( ioctl(skfd, SIOCGIFHWADDR, &sIfReq) != 0 )
			printf( "SocketHandler::GetMAC: ioctl failed\n");
		else
		{
			for(int i=0; i<6;i++)
				if(sIfReq.ifr_ifru.ifru_hwaddr.sa_data[i])
				{
					memcpy(buf, sIfReq.ifr_ifru.ifru_hwaddr.sa_data, 6);

					if(pIfList_tofree) if_freenameindex(pIfList_tofree);
					close(skfd);

					return buf;
				}
		}
	}

	if(pIfList_tofree) if_freenameindex(pIfList_tofree);
	close(skfd);
#endif

	printf("MAC ADDRESS: ");
	for(int i=0;i<6;i++) printf("%02x-", (unsigned char)buf[i]);
	printf("\n");

	return buf;
}

