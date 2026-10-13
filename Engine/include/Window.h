#pragma once
#include <windows.h>

class InputDevice;

struct WindowMessageContext {
	InputDevice* inputDevice;
};

class DisplayWindow32 {
private:
	friend InputDevice;
	static const LPCWSTR winClassName;
	static HINSTANCE hInstance;

	static bool isInit;

	long clientWidth;
	long clientHeight;

	HWND win;

public:
	DisplayWindow32(long clientWidth, long clientHeight, LPCWSTR header, WindowMessageContext& context);
	static void InitWindowClass();
	inline HWND getWindow() const { return win; }
	inline long getWidth() const { return clientWidth; }
	inline long getHeight() const { return clientHeight; }


	static LRESULT HandleMessage(HWND hwnd, UINT umessage, WPARAM wparam, LPARAM lparam);

};