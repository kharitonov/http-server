#include <stdio.h>
#include <winsock2.h>
#include <Windows.h>
#include <ws2tcpip.h>
#include <process.h>
#include <time.h>
#include <ctype.h>
#include <io.h>  
#include <fcntl.h> 
#include <sys/types.h>  
#include <direct.h>
#include <sys/stat.h>
#include <Urlmon.h>

#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "urlmon") // for finding the file mime type

#define DEFAULT_BUFLEN 512

char recvbuf[DEFAULT_BUFLEN];
int iResult, iSendResult;
int recvbuflen = DEFAULT_BUFLEN;

char *DIR;

void handleFile(SOCKET ClientSocket, char *filename);
void reply404(SOCKET ClientSocket);


unsigned __stdcall ClientSession(SOCKET *ClientSocket)
{
	do
	{
		iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
		if (iResult > 0)
		{

			// parse for the 3 relevant HTTP fields
			char param[255] = "";
			char param2[255] = "";
			char param3[9] = "";
			char *token1 = NULL;
			char *next_token1 = NULL;


			token1 = strtok_s(recvbuf, " ", &next_token1);
			strncat_s(param, sizeof(param), token1, ((sizeof param) - strlen(param) - 1));
			if (strcmp("GET", param) == 0)
			{
				token1 = strtok_s(NULL, " ", &next_token1);
				if (token1 != NULL)
				{
					strncat_s(param2, sizeof(param2), token1, ((sizeof param2) - strlen(param2) - 1));
					token1 = strtok_s(NULL, " ", &next_token1);

					if (token1 != NULL)
					{
						strncat_s(param3, sizeof(param3), token1, ((sizeof param3) - strlen(param3) - 1));
						if ((strcmp(param3, "HTTP/1.1") == 0 || strcmp(param3, "HTTP/1.0") == 0))
						{
							handleFile(ClientSocket, param2, DIR);
						}

					}
				}
			}

		}
	} while (iResult >= 0);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc < 3)
	{
		printf("usage:server.exe <PORT> <WWW DIRECTORY ROOT>\n");
		return 1;
	}
	char *DEFAULT_PORT = argv[1];
	DIR = argv[2];

	WSADATA wsaData;
	iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (iResult != 0)
	{
		return 1;
	}


	// set up addrinfo for the socket
	struct addrinfo *result = NULL, *ptr = NULL, hints;
	ZeroMemory(&hints, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	hints.ai_flags = AI_PASSIVE;
	iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);

	if (iResult != 0)
	{
		printf("getaddrinfo failed: %d\n", iResult);
		WSACleanup();
		return 1;
	}




	//create socket
	SOCKET ListenSocket = INVALID_SOCKET;
	ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
	if (ListenSocket == INVALID_SOCKET)
	{
		printf("listensocket failed: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		WSACleanup();
		return 1;
	}



	//bind and listen
	iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
	if (iResult == SOCKET_ERROR)
	{
		printf("bind failed: %d\n", WSAGetLastError());
		freeaddrinfo(result);
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}
	if (listen(ListenSocket, SOMAXCONN) == SOCKET_ERROR)
	{
		printf("error at listen: %d\n", WSAGetLastError());
		closesocket(ListenSocket);
		WSACleanup();
		return 1;
	}




	// accept connection and hand it off to stdcall thread so multiple clients can connect
	SOCKET ClientSocket = INVALID_SOCKET;
	while ((ClientSocket = accept(ListenSocket, NULL, NULL)))
	{
		unsigned threadID;
		HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, &ClientSession, (void*)ClientSocket, 0, &threadID);
	}

	return 0;
}

void handleFile(SOCKET ClientSocket, char *filename, char *DIR)
{

	//create file location string, open
	char pathBuff[255] = "";
	strncat_s(pathBuff, sizeof(pathBuff), DIR, ((sizeof pathBuff) - strlen(pathBuff) - 1));
	strncat_s(pathBuff, sizeof(pathBuff), filename, ((sizeof pathBuff) - strlen(pathBuff) - 1));


	FILE *fHandle;
	fopen_s(&fHandle, pathBuff, "rb");

	

	if (fHandle == NULL)
	{
		char notFound[255] = "HTTP/1.1 404 Not Found; charset=UTF-8\r\nCache-Control: no-cache, private\r\nContent-Length: 14\r\nContent-Type: text/html\r\n\r\n404: Not FoundConnection: close";
		send(ClientSocket, notFound, strlen((notFound)), 0);
		closesocket(ClientSocket);
		return;
	}

	if (fseek(fHandle, 0, SEEK_END) != 0)
	{
		fclose(fHandle);
		printf("broken\n");
	}

	size_t file_len = ftell(fHandle);
	rewind(fHandle);


	



	char buffer[1024];
	char msglength[50] = ""; // content-length placeholder
	char baseHeader[255] = "HTTP/1.1 200 OK; charset=UTF-8\r\nCache-Control: no-cache, private\r\nContent-Length: ";
	char *clcr = "\r\n\r\n";


	_itoa_s(file_len, msglength, sizeof(msglength), 10);
	strncat_s(baseHeader, sizeof(baseHeader), msglength, ((sizeof baseHeader) - strlen(baseHeader) - 1));


	



	size_t sent3 = 0;
	


	int fReadCount = 0;
	int elementsRead;

	// read chunks of the file into buffer, then send the chunk in the second while loop.
	while ((elementsRead = fread(buffer, sizeof(char), sizeof(buffer), fHandle)) > 0)
	{
		size_t   i;
		LPWSTR out;
		char mimeType[256];
		++fReadCount;
		if (fReadCount == 1)
		{

			// the stylesheet renders improperly when using the findmimetype call because it only checks the contents of the file, so here i check the extension to see if it's a .css or not, and manually set the header if so.
			char *check = pathBuff;
			check += (strlen(pathBuff) - 4);
			if (strcmp(check, ".css") == 0)
			{
				strncat_s(baseHeader, sizeof(baseHeader), "\r\n", ((sizeof baseHeader) - strlen(baseHeader) - 1));
				strncat_s(baseHeader, sizeof(baseHeader), "Content-Type: text/css", ((sizeof baseHeader) - strlen(baseHeader) - 1));
				send(ClientSocket, baseHeader, strlen((baseHeader)), 0);
				send(ClientSocket, clcr, strlen((clcr)), 0);
			}
			else
			{
				HRESULT asdf = FindMimeFromData(NULL, NULL, buffer, 256, NULL, FMFD_DEFAULT, &out, 0);
				wcstombs_s(&i, mimeType, (size_t)256, out, (size_t)256);
				strncat_s(baseHeader, sizeof(baseHeader), "\r\n", ((sizeof baseHeader) - strlen(baseHeader) - 1));
				strncat_s(baseHeader, sizeof(baseHeader), "Content-Type: ", ((sizeof baseHeader) - strlen(baseHeader) - 1));
				strncat_s(baseHeader, sizeof(baseHeader), mimeType, ((sizeof baseHeader) - strlen(baseHeader) - 1));
				send(ClientSocket, baseHeader, strlen((baseHeader)), 0);
				send(ClientSocket, clcr, strlen((clcr)), 0);
			}

		}
		//


		//send the data, which is read piece by piece by fread(), in chunks and verify that they were received properly
		char *loc = buffer;
		int bytesToSend = elementsRead;
		while (bytesToSend > 0 && sent3 < file_len)
		{
			int ret = send(ClientSocket, loc, bytesToSend, 0);

			if (ret == SOCKET_ERROR)
			{
				printf("WSAGetLastError(): %d\n", WSAGetLastError());
				return;
			}
				
			bytesToSend -= ret;
			loc += ret;
			sent3 += ret;
		}
	}

	fclose(fHandle);
	closesocket(ClientSocket);
}


