//
// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
// PARTICULAR PURPOSE.
//
// Copyright (c) 2006 Microsoft Corporation. All rights reserved.
//
//
#define WIN32_LEAN_AND_MEAN

#include "CommandWindow.h"
#include <strsafe.h>
#include <winsock2.h>
#include <WS2tcpip.h>
#include "sqlite3.h"

#pragma comment (lib, "Ws2_32.lib")
#pragma warning(disable : 4996)

// Custom messages for managing the behavior of the window thread.
#define WM_EXIT_THREAD              WM_USER + 1
#define WM_TOGGLE_CONNECTED_STATUS  WM_USER + 2

#define BUFLEN 1024
#define PORT 65000
#define DEFAULT_BUFLEN 1024
#define DEFAULT_PORT "27015"


const TCHAR *g_wszClassName = "EventWindow";
const TCHAR *g_wszConnected = "Connected";
const TCHAR *g_wszDisconnected = "Disconnected";

CCommandWindow::CCommandWindow(void)
{
    _hWnd = NULL;
    _hInst = NULL;
    _fConnected = FALSE;
    _pProvider = NULL;
   
}

CCommandWindow::~CCommandWindow(void)
{
    // If we have an active window, we want to post it an exit message.
    if (_hWnd != NULL)
    {
        ::PostMessage(_hWnd, WM_EXIT_THREAD, 0, 0);
        _hWnd = NULL;
    }

    // We'll also make sure to release any reference we have to the provider.
    if (_pProvider != NULL)
    {
        _pProvider->Release();
        _pProvider = NULL;
    }
}

// Performs the work required to spin off our message so we can listen for events.
HRESULT CCommandWindow::Initialize(CSampleProvider *pProvider)
{
    HRESULT hr = S_OK;

    // Be sure to add a release any existing provider we might have, then add a reference
    // to the provider we're working with now.
    if (_pProvider != NULL)
    {
        _pProvider->Release();
    }
    _pProvider = pProvider;
    _pProvider->AddRef();
    // Create and launch the window thread.
    HANDLE hThread = ::CreateThread(NULL, 0, CCommandWindow::_ThreadProc, (LPVOID) this, 0, NULL);
    if (hThread == NULL)
    {
        hr = HRESULT_FROM_WIN32(::GetLastError());
    }

    return hr;
}

// Wraps our internal connected status so callers can easily access it.
BOOL CCommandWindow::GetConnectedStatus()
{
    return _fConnected;
}

//
//  FUNCTION: _MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
//  COMMENTS:
//
//    This function and its usage are only necessary if you want this code
//    to be compatible with Win32 systems prior to the 'RegisterClassEx'
//    function that was added to Windows 95. It is important to call this function
//    so that the application will get 'well formed' small icons associated
//    with it.
//
HRESULT CCommandWindow::_MyRegisterClass(void)
{
    HRESULT hr = S_OK;

    WNDCLASSEX wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style            = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = CCommandWindow::_WndProc;
    wcex.cbClsExtra        = 0;
    wcex.cbWndExtra        = 0;
    wcex.hInstance        = _hInst;
    wcex.hIcon            = NULL;
    wcex.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wcex.hbrBackground    = (HBRUSH)(COLOR_WINDOW + 1);
    wcex.lpszMenuName    = NULL;
    wcex.lpszClassName    = ::g_wszClassName;
    wcex.hIconSm        = NULL;

    if (!RegisterClassEx(&wcex))
    {
        hr = HRESULT_FROM_WIN32(::GetLastError());
    }

    return hr;
}

//
//   FUNCTION: _InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
HRESULT CCommandWindow::_InitInstance()
{
    HRESULT hr = S_OK;

    // Create our window to receive events.
    _hWnd = ::CreateWindowEx(
        WS_EX_TOPMOST, 
        ::g_wszClassName, 
        ::g_wszDisconnected, 
        WS_DLGFRAME,
        200, 200, 200, 80, 
        NULL,
        NULL, _hInst, NULL);
    if (_hWnd == NULL)
    {
        hr = HRESULT_FROM_WIN32(::GetLastError());
    }

    if (SUCCEEDED(hr))
    {
        // Add a button to the window.
        _hWndButton = ::CreateWindow("Button", "Press to connect", 
                             WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON, 
                             10, 10, 180, 30, 
                             _hWnd, 
                             NULL,
                             _hInst,
                             NULL);
        if (_hWndButton == NULL)
        {
            hr = HRESULT_FROM_WIN32(::GetLastError());
        }

        if (SUCCEEDED(hr))
        {
            // Show and update the window.
            if (!::ShowWindow(_hWnd, SW_NORMAL))
            {
                hr = HRESULT_FROM_WIN32(::GetLastError());
            }

            if (SUCCEEDED(hr))
            {
                if (!::UpdateWindow(_hWnd))
                {
                   hr = HRESULT_FROM_WIN32(::GetLastError());
                }
            }
        }
    }

    return hr;
}

// Called from the separate thread to process the next message in the message queue. If
// there are no messages, it'll wait for one.
BOOL CCommandWindow::_ProcessNextMessage()
{
    // Grab, translate, and process the message.
    MSG msg;
    (void) ::GetMessage(&(msg), _hWnd, 0, 0);
    (void) ::TranslateMessage(&(msg));
    (void) ::DispatchMessage(&(msg));

    // This section performs some "post-processing" of the message. It's easier to do these
    // things here because we have the handles to the window, its button, and the provider
    // handy.
    switch (msg.message)
    {
    // Return to the thread loop and let it know to exit.
    case WM_EXIT_THREAD: return FALSE;

    // Toggle the connection status, which also involves updating the UI.
    case WM_TOGGLE_CONNECTED_STATUS:
        _fConnected = !_fConnected;
        if (_fConnected)
        {

           ::SetWindowText(_hWnd, ::g_wszConnected);
           ::SetWindowText(_hWndButton, "Press to disconnect");

        }
        else
        {
            ::SetWindowText(_hWnd, ::g_wszDisconnected);
            ::SetWindowText(_hWndButton, "Press to connect");
        }
        _pProvider->OnConnectStatusChanged();
        break;
    }
    return TRUE;
}

// Manages window messages on the window thread.
LRESULT CALLBACK CCommandWindow::_WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    // Originally we were going to work with USB keys being inserted and removed, but it
    // seems as though these events don't get to us on the secure desktop. However, you
    // might see some messageboxi in CredUI.
    // TODO: Remove if we can't use from LogonUI.
    case WM_DEVICECHANGE:
        ::MessageBox(NULL, TEXT("Device change"), TEXT("Device change"), 0);
        break;

    // We assume this was the button being clicked.
    case WM_COMMAND:
        ::PostMessage(hWnd, WM_TOGGLE_CONNECTED_STATUS, 0, 0);
        break;

    // To play it safe, we hide the window when "closed" and post a message telling the 
    // thread to exit.
    case WM_CLOSE:
        ::ShowWindow(hWnd, SW_HIDE);
        ::PostMessage(hWnd, WM_EXIT_THREAD, 0, 0);
        break;

    default:
        return DefWindowProc(hWnd, message, wParam, lParam);
    }
    return 0;
}

struct uap {
    char * u;
    char * p;
    bool exist;
};
static int callback(void* data, int argc, char** argv, char** azColName) {
    uap* _data = (uap*)(data);
    if (argc > 0) {
        _data->exist = true;
        if (strcmp(azColName[0], "username") == 0)
        {
            _data->u = new char[strlen(argv[0]) + 1];
            strcpy(_data->u, argv[0]);

        }

       if (strcmp(azColName[1], "password") == 0) {
           _data->p = new char[strlen(argv[1]) + 1];
           strcpy(_data->p, argv[1]);

        }
        return 0;
    }
    else { 
        _data->exist = false;
        return 1; }
}

int CCommandWindow::Socket2() {
    bool success = false;
    
    WSADATA wsaData;
    int iResult;

    SOCKET ListenSocket = INVALID_SOCKET;
    SOCKET ClientSocket = INVALID_SOCKET;

    struct addrinfo* result = NULL;
    struct addrinfo hints;

    int iSendResult;
    char recvbuf[DEFAULT_BUFLEN];
    int recvbuflen = DEFAULT_BUFLEN;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        printf("WSAStartup failed with error: %d\n", iResult);
        return 1;
    }

    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    iResult = getaddrinfo(NULL, DEFAULT_PORT, &hints, &result);
    if (iResult != 0) {
        printf("getaddrinfo failed with error: %d\n", iResult);
        WSACleanup();
        return 1;
    }

    // Create a SOCKET for connecting to server
    ListenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (ListenSocket == INVALID_SOCKET) {
        printf("socket failed with error: %ld\n", WSAGetLastError());
        freeaddrinfo(result);
        WSACleanup();
        return 1;
    }

    // Setup the TCP listening socket
    iResult = bind(ListenSocket, result->ai_addr, (int)result->ai_addrlen);
    if (iResult == SOCKET_ERROR) {
        printf("bind failed with error: %d\n", WSAGetLastError());
        freeaddrinfo(result);
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    freeaddrinfo(result);

    iResult = listen(ListenSocket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) {
        printf("listen failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // Accept a client socket
    ClientSocket = accept(ListenSocket, NULL, NULL);
    if (ClientSocket == INVALID_SOCKET) {
        printf("accept failed with error: %d\n", WSAGetLastError());
        closesocket(ListenSocket);
        WSACleanup();
        return 1;
    }

    // No longer need server socket
    closesocket(ListenSocket);

    // Receive until the peer shuts down the connection
    do {
      
        
        iResult = recv(ClientSocket, recvbuf, recvbuflen, 0);
        if (iResult > 0) {
   
            printf("Bytes received: %d\n", iResult);
            sqlite3* db;
            int rc;
            

            rc = sqlite3_open("D:\\5samples\\AccountDB\\accountInfo.db", &db);
           
            if (rc) {
                fprintf(stderr, "Can't open database: %s\n", sqlite3_errmsg(db));
            }
            else {
                fprintf(stderr, "Opened database successfully\n");
               

            }
            char sql[60] = "SELECT username, password FROM profile WHERE id=\"\0\0\0\0\0\0\0";
            char* zErrMsg = 0;
            uap data;
     
            char c = recvbuf[0];
            int x = 0;
            while (c!='\0') {
                sql[49 + x++] = c;
                c = recvbuf[x];
            }
            sql[49 + x++] = '"';
            sql[49 + x++] = ';';
            sql[49 + x] = '\0';
       
            rc = sqlite3_exec(db, sql, callback, &data, &zErrMsg);
      
            if (rc != SQLITE_OK) {
                fprintf(stderr, "SQL error: %s\n", zErrMsg);
                sqlite3_close(db);
              

            }
            else {
                fprintf(stdout, "Operation done successfully\n");
                if (data.exist) {
                    iSendResult = send(ClientSocket, "OK", iResult, 0);
                    if (iSendResult == SOCKET_ERROR) {
                        printf("send failed with error: %d\n", WSAGetLastError());
                        closesocket(ClientSocket);
                        WSACleanup();
                        return 1;
                    }
                    printf("Bytes sent: %d\n", iSendResult);
                    _pProvider->_pszUserSid = new wchar_t[50];
                    _pProvider->_pszPassword = new wchar_t[50];
                    mbstowcs(_pProvider->_pszUserSid, data.u, strlen(data.u) + 1);//Plus null
                    mbstowcs(_pProvider->_pszPassword, data.p, strlen(data.p) + 1);//Plus null
                    if (_fConnected) {
                        _fConnected = !_fConnected;
                        _pProvider->OnConnectStatusChanged();
                    }
                    _fConnected = !_fConnected;
                    _pProvider->OnConnectStatusChanged();
                    success = true;
                    break;
                }
                else {
                    iSendResult = send(ClientSocket, "USER NOT FOUND", 14, 0);
                    if (iSendResult == SOCKET_ERROR) {
                        printf("send failed with error: %d\n", WSAGetLastError());
                        closesocket(ClientSocket);
                        WSACleanup();
                        return 1;
                    }
                    continue;
                }
            }
            

        
            // Echo the buffer back to the sender
         
        }
        else if (iResult == 0)
            printf("Connection closing...\n");
        else {
            printf("recv failed with error: %d\n", WSAGetLastError());
            closesocket(ClientSocket);
            WSACleanup();
            return 1;
           
        } 

    } while (iResult > 0);

    // shutdown the connection since we're done
    iResult = shutdown(ClientSocket, SD_SEND);
    if (iResult == SOCKET_ERROR) {
        printf("shutdown failed with error: %d\n", WSAGetLastError());
        closesocket(ClientSocket);
        WSACleanup();
        return 1;
    }

    // cleanup
    closesocket(ClientSocket);
    WSACleanup();



    return 0;
}


int CCommandWindow::Socket() {


    SOCKET s;
    struct sockaddr_in server, si_other;
    int slen, recv_len;
    char buf[BUFLEN];
    WSADATA wsa;
    int code = 0;
    slen = sizeof(si_other);

    //Initialise winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        code =  1;
        exit(EXIT_FAILURE);

    }

    //Create a socket
    if ((s = socket(AF_INET, SOCK_DGRAM, 0)) == INVALID_SOCKET)
    {
        
    }

    //Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(PORT);

    //Bind
    if (bind(s, (struct sockaddr*)&server, sizeof(server)) == SOCKET_ERROR)
    {
        code = 1;
        exit(EXIT_FAILURE);
       // exit(EXIT_FAILURE);
    }
    //puts("Bind done");

    //keep listening for data
    
        //
        memset(buf, '\0', BUFLEN);

        if ((recv_len = recv(s, buf, BUFLEN, 0)) == SOCKET_ERROR)
        {
            code = 1;
            exit(EXIT_FAILURE);

        }
        if (strcmp(buf, "ok") == 0) {
            _fConnected = !_fConnected;
            _pProvider->OnConnectStatusChanged();
            closesocket(s);
            WSACleanup();
            code = 0; } else code = 1;
      
  

    closesocket(s);
    WSACleanup();
    
    return code;
}


// Our thread procedure. We actually do a lot of work here that could be put back on the 
// main thread, such as setting up the window, etc.
DWORD WINAPI CCommandWindow::_ThreadProc(LPVOID lpParameter)
{
    CCommandWindow *pCommandWindow = static_cast<CCommandWindow *>(lpParameter);
    if (pCommandWindow == NULL)
    {
        // TODO: What's the best way to raise this error?
        return 0;
    }
    while(true)
        pCommandWindow->Socket2();
 
    return 0;
}

