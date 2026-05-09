//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#include "stdafx.h"
#include "Win32Application.h"
#include "InputManager.h"
#include "Camera.h"

HWND Win32Application::m_hwnd = nullptr;

int Win32Application::Run(DXSample* pSample, HINSTANCE hInstance, int nCmdShow)
{
    // Parse the command line parameters
    int argc;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    pSample->ParseCommandLineArgs(argv, argc);
    LocalFree(argv);

    // Initialize the window class.
    WNDCLASSEX windowClass = { 0 };
    windowClass.cbSize = sizeof(WNDCLASSEX);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = WindowProc;
    windowClass.hInstance = hInstance;
    windowClass.hCursor = LoadCursor(NULL, IDC_ARROW);
    windowClass.lpszClassName = L"DXSampleClass";
    RegisterClassEx(&windowClass);

    RECT windowRect = { 0, 0, static_cast<LONG>(pSample->GetWidth()), static_cast<LONG>(pSample->GetHeight()) };
    AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

    // Create the window and store a handle to it.
    m_hwnd = CreateWindow(
        windowClass.lpszClassName,
        pSample->GetTitle(),
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        windowRect.right - windowRect.left,
        windowRect.bottom - windowRect.top,
        nullptr,        // We have no parent window.
        nullptr,        // We aren't using menus.
        hInstance,
        pSample);

    // Initialize the sample. OnInit is defined in each child-implementation of DXSample.
    pSample->OnInit();

    ShowWindow(m_hwnd, nCmdShow);

    // Main sample loop.
    MSG msg = {};
    while (msg.message != WM_QUIT)
    {
        // Process any messages in the queue.
        if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        else
        {
            pSample->OnUpdate();
            pSample->OnRender();
        }
    }

    pSample->OnDestroy();

    // Return this part of the WM_QUIT message to Windows.
    return static_cast<char>(msg.wParam);
}

// Main message handler for the sample.
LRESULT CALLBACK Win32Application::WindowProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    static int lastX = 0;
    static int lastY = 0;

    DXSample* pSample = reinterpret_cast<DXSample*>(GetWindowLongPtr(hWnd, GWLP_USERDATA));

    switch (message)
    {
    case WM_CREATE:
        {
            // Save the DXSample* passed in to CreateWindow.
            LPCREATESTRUCT pCreateStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
            SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pCreateStruct->lpCreateParams));
        }
        return 0;

    case WM_KEYDOWN:
        InputManager::Get().SetKeyState(static_cast<UINT8>(wParam), true);
        /*if (pSample)
        {
            pSample->OnKeyDown(static_cast<UINT8>(wParam));
        }*/
        return 0;

    case WM_KEYUP:
        InputManager::Get().SetKeyState(static_cast<UINT8>(wParam), false);
        /*if (pSample)
        {
            pSample->OnKeyUp(static_cast<UINT8>(wParam));
        }*/
        return 0;

    case WM_MOUSEMOVE:
    {
        int xPos = (int)(short)LOWORD(lParam);
        int yPos = (int)(short)HIWORD(lParam);

        if (pSample && !Camera::Get().m_isMouseLocked)
        {
            
            InputManager::Get().UpdateMouse(
                static_cast<float>(xPos - lastX),
                static_cast<float>(yPos - lastY)
            );
            lastX = xPos;
            lastY = yPos;
        }
        
        return 0;
    }
    case WM_LBUTTONDOWN:
        InputManager::Get().SetKeyState(VK_LBUTTON, true);
        lastX = (int)(short)LOWORD(lParam);
        lastY = (int)(short)HIWORD(lParam);
        SetCapture(hWnd);
        return 0;

    case WM_LBUTTONUP:
        InputManager::Get().SetKeyState(VK_LBUTTON, false);
        ReleaseCapture();
        return 0;

    case WM_RBUTTONDOWN:
        InputManager::Get().SetKeyState(VK_RBUTTON, true);
        lastX = (int)(short)LOWORD(lParam);
        lastY = (int)(short)HIWORD(lParam);
        SetCapture(hWnd);
        return 0;

    case WM_RBUTTONUP:
        InputManager::Get().SetKeyState(VK_RBUTTON, false);
        ReleaseCapture();
        return 0;

    /*case WM_PAINT:
        if (pSample)
        {
            pSample->OnUpdate();
            pSample->OnRender();
        }
        return 0;*/

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }

    // Handle any messages the switch statement didn't.
    return DefWindowProc(hWnd, message, wParam, lParam);
}
