#pragma once
#include <windows.h>

class InputDevice;
class DisplayWindow32;

struct WindowMessageContext {
	InputDevice* inputDevice = nullptr;
	DisplayWindow32* window = nullptr;
};

class DisplayWindow32 {
private:
	friend InputDevice;
	static const LPCWSTR winClassName;
	static HINSTANCE hInstance;

	static bool isInit;

	long clientWidth;
	long clientHeight;

	HWND win = nullptr;
	WindowMessageContext& messageContext;

public:
	DisplayWindow32(long clientWidth, long clientHeight, LPCWSTR header, WindowMessageContext& context);
	~DisplayWindow32();
	DisplayWindow32(const DisplayWindow32&) = delete;
	DisplayWindow32& operator=(const DisplayWindow32&) = delete;
	static void InitWindowClass();
	inline HWND getWindow() const { return win; }
	inline long getWidth() const { return clientWidth; }
	inline long getHeight() const { return clientHeight; }


	static LRESULT HandleMessage(HWND hwnd, UINT umessage, WPARAM wparam, LPARAM lparam);

};
