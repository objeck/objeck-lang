/***************************************************************************
 * Provides runtime support for Windows (Win32) based systems.
 *
 * Copyright (c) 2008-2022, Randy Hollines
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in
 * the documentation and/or other materials provided with the distribution.
 * - Neither the name of the Objeck Team nor the names of its
 * contributors may be used to endorse or promote products derived
 * from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
 * TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 ***************************************************************************/

#include <winsock2.h>
#include <ws2tcpip.h>

#include "win32.h"

SOCKET IPSocket::Open(const char* address, const int port) {
  SOCKET sock = INVALID_SOCKET;
  struct addrinfo* result = nullptr, *ptr = nullptr, hints;

  char port_str[8];
	_itoa_s(port, port_str, sizeof(port_str), 10);

  ZeroMemory(&hints, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_protocol = IPPROTO_TCP;

  if(getaddrinfo(address, port_str, &hints, &result) != 0) {
    WSACleanup();
    return -1;
  }

  for(ptr = result; ptr != nullptr; ptr = ptr->ai_next) {
		sock = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol);
    if(sock == INVALID_SOCKET) {
      WSACleanup();
      return -1;
    }

    if(connect(sock, ptr->ai_addr, (int)ptr->ai_addrlen) == SOCKET_ERROR) {
      closesocket(sock);
      sock = INVALID_SOCKET;
      continue;
    }
    break;
  }
  freeaddrinfo(result);

	return sock == INVALID_SOCKET ? -1 : sock;
}

vector<string> IPSocket::Resolve(const char* address) {
	vector<string> addresses;

	struct addrinfo* result;
	if(getaddrinfo(address, nullptr, nullptr, &result)) {
		freeaddrinfo(result);
		return vector<string>();
	}

	struct addrinfo* res;
	for(res = result; res != nullptr; res = res->ai_next) {
		char hostname[NI_MAXHOST] = {0};
		if(getnameinfo(res->ai_addr, (socklen_t)res->ai_addrlen, hostname, NI_MAXHOST, nullptr, 0, 0)) {
			freeaddrinfo(result);
			return vector<string>();
		}

		if(*hostname != '\0') {
			addresses.push_back(hostname);
		}
	}

	freeaddrinfo(result);
	return addresses;
}

SOCKET IPSocket::Accept(SOCKET server, char* client_address, int& client_port) {
	struct sockaddr_in pin;
	int addrlen = sizeof(pin);

	SOCKET client = accept(server, (struct sockaddr*)&pin, &addrlen);
	if(client == INVALID_SOCKET) {
		client_address[0] = '\0';
		client_port = -1;
		return -1;
	}

	char buffer[INET_ADDRSTRLEN] = { 0 };
	inet_ntop(AF_INET, &(pin.sin_addr), buffer, INET_ADDRSTRLEN);
	strncpy_s(client_address, INET_ADDRSTRLEN - 1, buffer, INET_ADDRSTRLEN);
	client_port = ntohs(pin.sin_port);

	return client;
}
