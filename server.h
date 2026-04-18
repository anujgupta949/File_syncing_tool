#ifndef SERVER_H
#define SERVER_H

#include <winsock2.h>
#include <ws2tcpip.h>

bool SendJson(SOCKET ClientSocket, const char *JsonString);
char *ReceiveJson(SOCKET ClientSocket);

bool InitializeClientSocket(const char *ServerName,
							const char *Port,
							const char *OutboundJson,
							char **InboundJson);

bool InitializeServerSocket(const char *BindAddress,
							const char *Port,
							const char *OutboundJson,
							char **InboundJson);

bool RunSyncServerSession(const char *BindAddress,
						  const char *Port,
						  const char *ServerRootFolder);

bool RunSyncClientSession(const char *ServerName,
						  const char *Port,
						  const char *ClientRootFolder);

#endif
