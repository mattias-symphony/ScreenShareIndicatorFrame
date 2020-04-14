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
	uint32_t hash;
	BOOL found;
	RECT bounds;
};

uint32_t SuperFastHash (const char * data, int len); // Definition at end of file


static BOOL CALLBACK findScreen( HMONITOR monitor, HDC dc, LPRECT rect, LPARAM lparam ) {	
	struct FindScreenData* findScreenData = (struct FindScreenData*) lparam;

    MONITORINFOEXA info;
    info.cbSize = sizeof( info );
    GetMonitorInfoA( monitor, (MONITORINFO*) &info );

    uint32_t hash = SuperFastHash( info.szDevice, strlen( info.szDevice ) );

    // If no hash is passed in (hash==0), pick the first screen we find
    if( findScreenData->hash == 0 || hash == findScreenData->hash ) {
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


static int trackScreen( RECT bounds ) {

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
		NULL, WS_VISIBLE, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top , NULL, NULL, 
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


int main( int argc, char* argv[] ) {
	EnumWindows( closeExistingInstance, 0 );
	if( argc == 2 ) {
        uint64_t arg = atoll( argv[ 1 ] );
        struct FindScreenData findScreenData = { (uint32_t) arg, FALSE };
        
        EnumDisplayMonitors( NULL, NULL, findScreen, (LPARAM) &findScreenData );
        if( findScreenData.found ) {
            return trackScreen( findScreenData.bounds );
        }
        
        
		HWND trackedWindow = (HWND)( (uintptr_t) atoll( argv[ 1 ] ) );
		if( trackWindow == NULL ) {
			return EXIT_FAILURE;
		}
		return trackWindow( trackedWindow );
	} else if( argc > 2 ) {
        return EXIT_FAILURE;
    }

	return EXIT_SUCCESS;
}




	
		
// Copyright (c) 2010, Paul Hsieh
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
//   list of conditions and the following disclaimer.
// * Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
// * Neither my name, Paul Hsieh, nor the names of any other contributors to the
//   code use may not be used to endorse or promote products derived from this
//   software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
#include <stdint.h>
#include <stdlib.h>
#undef get16bits
#if (defined(__GNUC__) && defined(__i386__)) || defined(__WATCOMC__) \
  || defined(_MSC_VER) || defined (__BORLANDC__) || defined (__TURBOC__)
#define get16bits(d) (*((const uint16_t *) (d)))
#endif
#if !defined (get16bits)
#define get16bits(d) ((((uint32_t)(((const uint8_t *)(d))[1])) << 8)\
                       +(uint32_t)(((const uint8_t *)(d))[0]) )
#endif
uint32_t SuperFastHash (const char * data, int len) {
uint32_t hash = len, tmp;
int rem;
    if (len <= 0 || data == NULL) return 0;
    rem = len & 3;
    len >>= 2;
    /* Main loop */
    for (;len > 0; len--) {
        hash  += get16bits (data);
        tmp    = (get16bits (data+2) << 11) ^ hash;
        hash   = (hash << 16) ^ tmp;
        data  += 2*sizeof (uint16_t);
        hash  += hash >> 11;
		}    
    /* Handle end cases */
    switch (rem) {
        case 3: hash += get16bits (data);
                hash ^= hash << 16;
                hash ^= ((signed char)data[sizeof (uint16_t)]) << 18;
                hash += hash >> 11;
                break;
        case 2: hash += get16bits (data);
                hash ^= hash << 11;
                hash += hash >> 17;
                break;
        case 1: hash += (signed char)*data;
                hash ^= hash << 10;
                hash += hash >> 1;
	}
    /* Force "avalanching" of final 127 bits */
    hash ^= hash << 3;
    hash += hash >> 5;
    hash ^= hash << 4;
    hash += hash >> 17;
    hash ^= hash << 25;
    hash += hash >> 6;
    return hash;
}