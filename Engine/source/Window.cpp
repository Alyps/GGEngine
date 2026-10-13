#include "Window.h"
#include <cassert>    
#include <cstdlib> 
#include "InputDevice.h" 



//DisplayWin32


LRESULT DisplayWindow32::HandleMessage(HWND hwnd, UINT umessage, WPARAM wparam, LPARAM lparam) {

	if (umessage == WM_NCCREATE) {
		auto* cs = reinterpret_cast<CREATESTRUCT*>(lparam);
		auto* context = static_cast<WindowMessageContext*>(cs->lpCreateParams); 
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(context));
		return TRUE;  // или DefWindowProc
	}

	auto* context = reinterpret_cast<WindowMessageContext*>(GetWindowLongPtr(hwnd, GWLP_USERDATA)); 
	if (context == nullptr) {
		return DefWindowProc(hwnd, umessage, wparam, lparam);
	}

	switch (umessage)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	case WM_INPUT:
	{

		if (GET_RAWINPUT_CODE_WPARAM(wparam) == RIM_INPUTSINK) {
			return 0; // игнорируем фоновый ввод
		}

		UINT bufferSize{ 0 };
		GetRawInputData((HRAWINPUT)lparam, RID_INPUT, NULL, &bufferSize, sizeof(RAWINPUTHEADER));

		void* buf = malloc(bufferSize);
		if (buf == nullptr) {
			return 0;
		}


		if (GetRawInputData((HRAWINPUT)lparam, RID_INPUT, buf, &bufferSize, sizeof(RAWINPUTHEADER)) == bufferSize) {
			RAWINPUT* raw = (RAWINPUT*)buf;

			// Обработка клавиатуры
			if (raw->header.dwType == RIM_TYPEKEYBOARD)
			{
				InputDevice::KeyboardInputEventArgs args;
				args.MakeCode = raw->data.keyboard.MakeCode;
				args.Flags = raw->data.keyboard.Flags;
				args.VKey = raw->data.keyboard.VKey;
				args.Message = raw->data.keyboard.Message;

				context->inputDevice->OnKeyDown(args);
			}
			// Обработка мыши
			else if (raw->header.dwType == RIM_TYPEMOUSE)
			{
				InputDevice::RawMouseEventArgs args;
				args.Mode = raw->data.mouse.usFlags;
				args.ButtonFlags = raw->data.mouse.usButtonFlags;
				args.ExtraInformation = raw->data.mouse.ulExtraInformation;
				args.Buttons = raw->data.mouse.ulRawButtons;
				args.WheelDelta = raw->data.mouse.usButtonData;
				args.X = raw->data.mouse.lLastX;
				args.Y = raw->data.mouse.lLastY;

				context->inputDevice->OnMouseMove(args);
			}
		}

		free(buf);
		return 0;

	}
	default:
	{
		return DefWindowProc(hwnd, umessage, wparam, lparam);
	}
	}


}

bool DisplayWindow32::isInit = false;
const LPCWSTR DisplayWindow32::winClassName = L"DisplayWindow32";
HINSTANCE DisplayWindow32::hInstance = GetModuleHandle(nullptr);

DisplayWindow32::DisplayWindow32(long clientWidth, long clientHeight, LPCWSTR header, WindowMessageContext& context) : clientWidth{ clientWidth }, clientHeight{ clientHeight }
{
	assert(isInit);
	RECT windowRect = { 0, 0, static_cast<LONG>(clientWidth), static_cast<LONG>(clientHeight) };
	AdjustWindowRect(&windowRect, WS_OVERLAPPEDWINDOW, FALSE);

	auto dwStyle = WS_SYSMENU | WS_CAPTION | WS_MINIMIZEBOX | WS_THICKFRAME;

	auto posX = (GetSystemMetrics(SM_CXSCREEN) - clientWidth) / 2;
	auto posY = (GetSystemMetrics(SM_CYSCREEN) - clientHeight) / 2;


	win = CreateWindowEx(WS_EX_APPWINDOW, winClassName, header,
		dwStyle,
		posX, posY,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		nullptr, nullptr, hInstance, &context);

	ShowWindow(win, SW_SHOW);
	SetForegroundWindow(win);
	SetFocus(win);

	ShowCursor(true);
}


void DisplayWindow32::InitWindowClass()
{
	if (!isInit) {
		WNDCLASSEX wc;
		wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		wc.lpfnWndProc = &DisplayWindow32::HandleMessage;
		wc.cbClsExtra = 0;
		wc.cbWndExtra = 0;
		wc.hInstance = hInstance;
		wc.hIcon = LoadIcon(nullptr, IDI_WINLOGO);
		wc.hIconSm = wc.hIcon;
		wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
		wc.lpszMenuName = nullptr;
		wc.lpszClassName = winClassName;
		wc.cbSize = sizeof(WNDCLASSEX);
		

		// Register the window class.
		RegisterClassEx(&wc);
		isInit = true;
	}
}
