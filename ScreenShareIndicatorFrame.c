#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )

#define WINDOW_CLASS_NAME "SymphonyScreenShareIndicator"
#define IDLE_UPDATE_INTERVAL_MS 1000
#define ACTIVE_UPDATE_INTERVAL_MS 10
#define TIMEOUT_BEFORE_IDLE_MS 2000
#define FRAME_COLOR RGB( 0xee, 0x3d, 0x3d )
#define FRAME_WIDTH 4

static BOOL CALLBACK closeExistingInstance( HWND hwnd, LPARAM lparam ) {	
	struct FindWindowData* findWindowData = (struct FindWindowData*) lparam;

	char className[ 256 ] = "";
	GetClassNameA( hwnd, className, sizeof( className ) );

	if( strcmp( className, WINDOW_CLASS_NAME ) == 0 ) {
		PostMessageA( hwnd, WM_CLOSE, 0, 0 );
	}

	return TRUE;
}


struct FindWindowData {
	HWND hwnd;
	BOOL found;
};


static BOOL CALLBACK findWindowHandle( HWND hwnd, LPARAM lparam ) {	
	struct FindWindowData* findWindowData = (struct FindWindowData*) lparam;

	if( hwnd == findWindowData->hwnd ) {
		findWindowData->found = TRUE;
		return FALSE;
	}

	return TRUE;
}


struct WindowData {
	HWND trackedWindow;
	HBRUSH background;	
	HBRUSH foreground;	
	RECT previousWindowRect;
	int activeTimeout;
};


static void CALLBACK trackWindowTimerProc( HWND hwnd, UINT message, UINT_PTR id, DWORD ms ) {
	struct WindowData* windowData = (struct WindowData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
	
	RECT windowRect;
	if( GetWindowRect( windowData->trackedWindow, &windowRect ) ) {
		if( EqualRect( &windowRect, &windowData->previousWindowRect ) ) {
			windowData->activeTimeout -= ACTIVE_UPDATE_INTERVAL_MS;
			if( windowData->activeTimeout <= 0 ) {
				SetTimer( hwnd, (UINT_PTR)"timer", IDLE_UPDATE_INTERVAL_MS, trackWindowTimerProc );
			} else {
				SetTimer( hwnd, (UINT_PTR)"timer", ACTIVE_UPDATE_INTERVAL_MS, trackWindowTimerProc );
			}
		} else {
			windowData->activeTimeout = TIMEOUT_BEFORE_IDLE_MS;
			SetTimer( hwnd, (UINT_PTR)"timer", ACTIVE_UPDATE_INTERVAL_MS, trackWindowTimerProc );
		}

		RECT trackingRect = windowRect;
		WINDOWPLACEMENT wndpl = { sizeof( wndpl ) };
		if( GetWindowPlacement( windowData->trackedWindow, &wndpl ) && wndpl.showCmd == SW_MAXIMIZE ) {
			InflateRect( &trackingRect, -7, -8 );
			trackingRect.top -= 1;
			trackingRect.right -= 1;
		} else {
			InflateRect( &trackingRect, -6, -6 );
			trackingRect.top -= 7;
		}
		SetWindowPos( hwnd, windowData->trackedWindow, trackingRect.left, trackingRect.top, 
			trackingRect.right - trackingRect.left, trackingRect.bottom - trackingRect.top,  
			SWP_NOACTIVATE | SWP_NOZORDER );
		SetWindowPos( windowData->trackedWindow, hwnd, 0, 0, 0, 0,  SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE  );
		windowData->previousWindowRect = windowRect;
	} else {
		CloseWindow( hwnd );
		PostQuitMessage( 0 );
	}
}


static LRESULT CALLBACK trackWindowWndProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	struct WindowData* windowData = (struct WindowData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
	switch( message ) {
		case WM_CLOSE: {
			PostQuitMessage( 0 );
		} break;
		case WM_ERASEBKGND: {
			return 0;
        } break;
		case WM_PAINT: {
			RECT r;
			GetClientRect( hwnd, &r );
			PAINTSTRUCT ps; 
			HDC dc = BeginPaint( hwnd, &ps );
			FillRect( dc, &r, windowData->foreground );
			InflateRect( &r, -FRAME_WIDTH, -FRAME_WIDTH );
			FillRect( dc, &r, windowData->background );
			EndPaint( hwnd, &ps );
		} break;
	}
    return DefWindowProc( hwnd, message, wparam, lparam);
}


static int trackWindow( HWND trackedWindow ) {
	struct FindWindowData findWindowData = { 
		trackedWindow, 
		FALSE 
	};
	
	EnumWindows( findWindowHandle, (LPARAM) &findWindowData );
	if( !findWindowData.found ) {
		return EXIT_FAILURE;
	}

    WNDCLASSA wc = { 
		CS_OWNDC, 
		(WNDPROC) trackWindowWndProc, 
		0, 
		0, 
		GetModuleHandleA( NULL ), 
		NULL, 
		NULL, 
		NULL, 
		NULL, 
		WINDOW_CLASS_NAME 
	};

    RegisterClassA( &wc );

    HWND hwnd = CreateWindowExA( WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, wc.lpszClassName, 
		NULL, WS_BORDER,  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, 
		GetModuleHandleA( NULL ), 0 );

	COLORREF transparency_key = RGB( 0, 255, 255 );
	struct WindowData windowData = { 
		trackedWindow, 
		CreateSolidBrush( transparency_key ), 
		CreateSolidBrush( FRAME_COLOR ) 
	};

	SetWindowLongPtrA( hwnd, GWLP_USERDATA, (LONG_PTR)&windowData );
	SetWindowLongA(hwnd, GWL_STYLE, WS_VISIBLE );
	SetLayeredWindowAttributes(hwnd, transparency_key, 0, LWA_COLORKEY);
	SetWindowLongPtrA( hwnd, GWLP_HWNDPARENT, (LONG_PTR) trackedWindow );
	trackWindowTimerProc( hwnd, 0, 0, 0 );


	MSG msg = { NULL };
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	DeleteObject( windowData.foreground );
	DeleteObject( windowData.background );
	DestroyWindow( hwnd );
	UnregisterClassA( wc.lpszClassName, GetModuleHandleA( NULL ) );

	return EXIT_SUCCESS;
}



struct FindScreenData {
	int x;
      int y;
	BOOL found;
	RECT bounds;
};


static BOOL CALLBACK findScreen( HMONITOR monitor, HDC dc, LPRECT rect, LPARAM lparam ) {	
	struct FindScreenData* findScreenData = (struct FindScreenData*) lparam;

	if( rect->left == findScreenData->x && rect->top == findScreenData->y ) {
		findScreenData->found = TRUE;
		findScreenData->bounds = *rect;
		return FALSE;
	}

	return TRUE;
}


struct ScreenData {
	HDC deviceContext;
	HBRUSH background;	
	HBRUSH foreground;	
};


static LRESULT CALLBACK trackScreenWndProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	struct ScreenData* screenData = (struct ScreenData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
	switch( message ) {
		case WM_CLOSE: {
			PostQuitMessage( 0 );
		} break;
		case WM_PAINT: {
			RECT r;
			GetClientRect( hwnd, &r );
			FillRect( screenData->deviceContext, &r, screenData->foreground );
			InflateRect( &r, -5, -5 );
			r.bottom -= 1;
			FillRect( screenData->deviceContext, &r, screenData->background );
		} break;
	}
    return DefWindowProc( hwnd, message, wparam, lparam);
}


static int trackScreen( int x, int y ) {
	struct FindScreenData findScreenData = { 
		x, y,
		FALSE 
	};
	
	EnumDisplayMonitors( NULL, NULL, findScreen, (LPARAM) &findScreenData );
	if( !findScreenData.found ) {
		return EXIT_FAILURE;
	}

    WNDCLASSA wc = { 
		CS_OWNDC | CS_HREDRAW | CS_VREDRAW, 
		(WNDPROC) trackScreenWndProc, 
		0, 
		0, 
		GetModuleHandleA( NULL ), 
		NULL, 
		NULL, 
		NULL, 
		NULL, 
		WINDOW_CLASS_NAME 
	};

    RegisterClassA( &wc );

    HWND hwnd = CreateWindowExA( WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, 
		NULL, WS_VISIBLE,  findScreenData.bounds.left, findScreenData.bounds.top, 
		findScreenData.bounds.right - findScreenData.bounds.left, 
		findScreenData.bounds.bottom - findScreenData.bounds.top , NULL, NULL, 
		GetModuleHandleA( NULL ), 0 );

	COLORREF transparency_key = RGB( 0, 255, 255 );
	struct ScreenData screenData = { 
		GetDC( hwnd ), 
		CreateSolidBrush( transparency_key ), 
		CreateSolidBrush( FRAME_COLOR ) 
	};

	SetWindowLongPtrA( hwnd, GWLP_USERDATA, (LONG_PTR)&screenData );
	SetWindowLongA(hwnd, GWL_STYLE, WS_VISIBLE );
	SetLayeredWindowAttributes(hwnd, transparency_key, 0, LWA_COLORKEY);
	UpdateWindow( hwnd );
	MSG msg = { NULL };
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	DeleteObject( screenData.foreground );
	DeleteObject( screenData.background );
	ReleaseDC( hwnd, screenData.deviceContext );
	DestroyWindow( hwnd );
	UnregisterClassA( wc.lpszClassName, GetModuleHandleA( NULL ) );

	return EXIT_SUCCESS;
}


int app( int argc, char* argv[] ) {
	if( argc == 2 ) {
		HWND trackedWindow = (HWND)( (uintptr_t) atoll( argv[ 1 ] ) );
		if( trackWindow == NULL ) {
			return EXIT_FAILURE;
		}
		return trackWindow( trackedWindow );
	} else if( argc == 3 ) {
		return trackScreen( atoi( argv[ 1 ] ), atoi( argv[ 2 ] ) );
	}

	return EXIT_FAILURE;
}


struct FindTestWindowData {
	char const* title;
	HWND hwnd;
};


static BOOL CALLBACK findTestWindow( HWND hwnd, LPARAM lparam ) {	
	struct FindTestWindowData* findTestWindowData = (struct FindTestWindowData*) lparam;
	int length = GetWindowTextLengthA( hwnd );
	if( IsWindowVisible( hwnd ) && length > 0 ) {
	
		char* buffer = (char*) malloc( length + 1 );
		if( !buffer ) {
			return FALSE;
		}
		GetWindowTextA( hwnd, buffer, length + 1 );
		BOOL isTestWindow = strstr( buffer, findTestWindowData->title ) != NULL;
		free( buffer );
		
		if( isTestWindow ) {
			findTestWindowData->hwnd = hwnd;
			return FALSE;
		}    
	}
	return TRUE;
}


static int testWindow( char const* title ) {
	struct FindTestWindowData findTestWindowData = {
		title,
		NULL
	};
	EnumWindows( findTestWindow, (LPARAM) &findTestWindowData );
	if( findTestWindowData.hwnd == NULL ) {
		return EXIT_FAILURE;
	}

	char buffer[ 16 ];
	sprintf_s( buffer, sizeof( buffer ), "%" PRIu64,(uint64_t) findTestWindowData.hwnd );

	char* argv[] = { buffer, buffer };
	return app( 2, argv );
}


static int testScreen( char* x, char* y ) {
	char* argv[] = { x, x, y };
	return app( 3, argv );
}


int main( int argc, char* argv[] ) {
	EnumWindows( closeExistingInstance, 0 );

	if( argc >= 2 && strcmp( argv[ 1 ], "--test" ) == 0 ) {
		if( argc == 2 ) {
			return testWindow( "test" );
		} else if( argc == 3 ) {
			return testWindow( argv[ 2 ] );
		} else if( argc == 4 ) {
			return testScreen( argv[ 2 ], argv[ 3 ] );
		}	
		return EXIT_FAILURE;
	}

    return app( argc, argv );
}
