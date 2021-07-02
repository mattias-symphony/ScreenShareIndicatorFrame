#include <windows.h>
#include <dwmapi.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#pragma comment( lib, "user32.lib" )
#pragma comment( lib, "gdi32.lib" )
#pragma comment( lib, "dwmapi.lib" )

#define WINDOW_CLASS_NAME "SymphonyScreenShareIndicator"
#define IDLE_UPDATE_INTERVAL_MS 1000
#define ACTIVE_UPDATE_INTERVAL_MS 10
#define TIMEOUT_BEFORE_IDLE_MS 2000
#define FRAME_COLOR RGB( 0xee, 0x3d, 0x3d )
#define FRAME_WIDTH 4


void internal_log( char const* file, int line, char const* func, char const* format, ... ) {
	static FILE* log_file = NULL;
	if( !file && !func && !format ) {
		if( log_file ) {
			fclose( log_file );
			log_file = NULL;
		}
		return;
	}
	if( !log_file ) {
		char filename[ MAX_PATH ];
		ExpandEnvironmentStringsA( "%LOCALAPPDATA%\\ScreenShareIndicatorFrameLogs", filename, sizeof( filename ) );
		CreateDirectoryA( filename, NULL );
		sprintf( filename + strlen( filename ), "\\ssif_%d.log", (int)time( NULL ) );		
		log_file = fopen( filename, "a" );
		if( !log_file ) {
			return;
		}
	}
		
	fprintf( log_file, "%s(%03d): %s(): ", file, line, func );
	va_list args;
	va_start( args, format );
	vfprintf( log_file, format, args );
	fflush( log_file );
	va_end( args );
}


void close_log_file( void ) {
	internal_log( NULL, 0, NULL, NULL );
}


#define LOG( str, ... ) internal_log( __FILE__, __LINE__, __func__, str "\n", __VA_ARGS__ )


// Get an errror description for the last error from Windows, for logging purposes
char const* error_message( void  ) {
    DWORD error = GetLastError();
    if( error == 0 ) {
        return "";
    }
    
    LPSTR buffer = NULL;
    size_t size = FormatMessageA( 
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
        NULL, error, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), (LPSTR)&buffer, 0, NULL);
    
    static char message[ 1024 ] = "";
    if( buffer ) {
        strncpy( message, buffer, sizeof( message ) );
        message[ sizeof( message ) - 1 ] = '\0';
        LocalFree(buffer);
    }
	int i = strlen( message ) - 1;
	while( i > 0 && ( message[ i ] == '\n' || message[ i ] == '\r' ) ) {
		message[ i ] = '\0';
		--i;
	}
            
    return message;
}


static BOOL CALLBACK closeExistingInstance( HWND hwnd, LPARAM lparam ) {	
	LOG( "closeExistingInstance( hwnd=%llu, lparam=%llu )", (uint64_t)(uintptr_t)hwnd, (uint64_t)(uintptr_t)lparam );
	struct FindWindowData* findWindowData = (struct FindWindowData*) lparam;

	char className[ 256 ] = "";
	int result = GetClassNameA( hwnd, className, sizeof( className ) );
	LOG( "GetClassNameA returned %d", result );
	if( result ) {
		LOG( "classname=%s", className );
	} else {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}

	LOG( "strcmp(%s,%s)", className, WINDOW_CLASS_NAME );
	if( strcmp( className, WINDOW_CLASS_NAME ) == 0 ) {
		LOG( "%s==%s", className, WINDOW_CLASS_NAME );
		LOG( "Posting WM_CLOSE message to %llu", (uint64_t)(uintptr_t)hwnd ); 
		BOOL success = PostMessageA( hwnd, WM_CLOSE, 0, 0 );
		LOG( "PostMessageA %s", success ? "successful" : "failed" );
		if( !success ) {
			LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
		}
	} else {
		LOG( "%s!=%s", className, WINDOW_CLASS_NAME );
	}

	return TRUE;
}


struct FindWindowData {
	HWND hwnd;
	BOOL found;
};


static BOOL CALLBACK findWindowHandle( HWND hwnd, LPARAM lparam ) {	
	LOG( "findWindowHandle( hwnd=%llu, lparam=%llu )", (uint64_t)(uintptr_t)hwnd, (uint64_t)(uintptr_t)lparam );
	struct FindWindowData* findWindowData = (struct FindWindowData*) lparam;

	LOG( "findWindowData->hwnd=%llu", (uint64_t)(uintptr_t)findWindowData->hwnd );
	if( hwnd == findWindowData->hwnd ) {
		LOG( "hwnd==findWindowData->hwnd" );
		findWindowData->found = TRUE;
		LOG( "findWindowData->found=TRUE" );
		LOG( "window found, stopping enumeration" );
		return FALSE;
	} else {
		LOG( "hwnd!=findWindowData->hwnd" );
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
	LOG( "trackWindowTimerProc( hwnd=%llu, message=%u, id=%llu, m=%u )", (uint64_t)(uintptr_t)hwnd, message, (uint64_t)(uintptr_t)id, ms );
	struct WindowData* windowData = (struct WindowData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
	
	RECT windowRect;
	LOG( "DwmGetWindowAttribute(%ull)", (uint64_t)(uintptr_t)windowData->trackedWindow );
	if( DwmGetWindowAttribute( windowData->trackedWindow, DWMWA_EXTENDED_FRAME_BOUNDS, &windowRect, sizeof( windowRect ) ) == S_OK ) {
		LOG( "DwmGetWindowAttribute successful" );
		LOG( "windowRect={%d,%d,%d,%d}", windowRect.left, windowRect.top, windowRect.right, windowRect.bottom );
		LOG( "previousWindowRect={%d,%d,%d,%d}", windowData->previousWindowRect.left, windowData->previousWindowRect.top, windowData->previousWindowRect.right, windowData->previousWindowRect.bottom );
		if( EqualRect( &windowRect, &windowData->previousWindowRect ) ) {
			LOG( "windowRect==previousWindowRect" );
			LOG( "activeTimeout=%d", windowData->activeTimeout );
			LOG( "activeTimeout -= ACTIVE_UPDATE_INTERVAL_MS (%u)", ACTIVE_UPDATE_INTERVAL_MS );
			windowData->activeTimeout -= ACTIVE_UPDATE_INTERVAL_MS;
			LOG( "activeTimeout=%d", windowData->activeTimeout );
			if( windowData->activeTimeout <= 0 ) {
				if( SetTimer( hwnd, (UINT_PTR)"timer", IDLE_UPDATE_INTERVAL_MS, trackWindowTimerProc ) ) {
					LOG( "SetTimer(%d) successful", IDLE_UPDATE_INTERVAL_MS );
				} else {
					LOG( "SetTimer(%d) failed", IDLE_UPDATE_INTERVAL_MS );
					LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
				}				
			} else {
				if( SetTimer( hwnd, (UINT_PTR)"timer", ACTIVE_UPDATE_INTERVAL_MS, trackWindowTimerProc ) ) {
					LOG( "SetTimer(%d) successful", ACTIVE_UPDATE_INTERVAL_MS );
				} else {
					LOG( "SetTimer(%d) failed", ACTIVE_UPDATE_INTERVAL_MS );
					LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
				}				
			}
		} else {
			LOG( "windowRect!=previousWindowRect" );
			LOG( "activeTimeout=%d", windowData->activeTimeout );
			LOG( "activeTimeout = TIMEOUT_BEFORE_IDLE_MS (%u)", TIMEOUT_BEFORE_IDLE_MS );
			windowData->activeTimeout = TIMEOUT_BEFORE_IDLE_MS;
			LOG( "activeTimeout=%d", windowData->activeTimeout );
			if( SetTimer( hwnd, (UINT_PTR)"timer", ACTIVE_UPDATE_INTERVAL_MS, trackWindowTimerProc ) ) {
				LOG( "SetTimer(%d) successful", ACTIVE_UPDATE_INTERVAL_MS );
			} else {
				LOG( "SetTimer(%d) failed", ACTIVE_UPDATE_INTERVAL_MS );
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}				
		}

		RECT trackingRect = windowRect;
		LOG( "trackingRect={%d,%d,%d,%d}", trackingRect.left, trackingRect.top, trackingRect.right, trackingRect.bottom );
		WINDOWPLACEMENT wndpl = { sizeof( wndpl ) };
		if( GetWindowPlacement( windowData->trackedWindow, &wndpl ) && wndpl.showCmd == SW_MAXIMIZE ) {
			LOG( "GetWindowPlacement succesful" );
			HMONITOR monitor = MonitorFromWindow( windowData->trackedWindow, MONITOR_DEFAULTTONEAREST );
			LOG( "MonitorFromWindow(MONITOR_DEFAULTTONEAREST) monitor=%llu", (uint64_t)(uintptr_t)monitor );
			MONITORINFO info;
			info.cbSize = sizeof( info );
			if( GetMonitorInfoA( monitor, &info ) ) {
				LOG( "GetMonitorInfoA succesful" );
			} else {
				LOG( "GetMonitorInfoA failed" );
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
			LOG( "info.rcMonitor={%d,%d,%d,%d}", info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom );
			LOG( "info.rcWork={%d,%d,%d,%d}", info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom );
			LOG( "info.dwFlags=%u", info.dwFlags );
			trackingRect = info.rcWork;
			LOG( "trackingRect={%d,%d,%d,%d}", trackingRect.left, trackingRect.top, trackingRect.right, trackingRect.bottom );
		} else {
			LOG( "GetWindowPlacement failed" );
			LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
		}				
		if( SetWindowPos( hwnd, windowData->trackedWindow, trackingRect.left, trackingRect.top, 
			trackingRect.right - trackingRect.left, trackingRect.bottom - trackingRect.top,  
			SWP_NOACTIVATE | SWP_NOZORDER ) ) {
				LOG( "SetWindowPos(hwnd, trackedWindow) succesful" );				
		} else {
			LOG( "SetWindowPos(hwnd, trackedWindow) failed" );
			LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
		}
		if( SetWindowPos( windowData->trackedWindow, hwnd, 0, 0, 0, 0,  SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE  ) ) {
				LOG( "SetWindowPos(trackedWindow, hwnd) succesful" );				
		} else {
			LOG( "SetWindowPos(trackedWindow, hwnd) failed" );
			LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
		}
		windowData->previousWindowRect = windowRect;
	} else {
		LOG( "DwmGetWindowAttribute failed" );
		if( CloseWindow( hwnd ) ) {
			LOG( "CloseWindow successful" );
		} else {
			LOG( "CloseWindow failed" );
			LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
		}
		PostQuitMessage( 0 );
		LOG( "PostQuitMessage" );
	}
}


static LRESULT CALLBACK trackWindowWndProc( HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam) {
	struct WindowData* windowData = (struct WindowData*) GetWindowLongPtrA( hwnd, GWLP_USERDATA );
	switch( message ) {
		case WM_CLOSE: {
			LOG( "WM_CLOSE" );
			PostQuitMessage( 0 );
			LOG( "PostQuitMessage" );
		} break;
		case WM_ERASEBKGND: {
			LOG( "WM_ERASEBKGND" );
			return 0;
        } break;
		case WM_PAINT: {
			LOG( "WM_PAINT" );
			RECT r;
			BOOL result = GetClientRect( hwnd, &r );
			LOG( "GetClientRect %s", result ? "successful" : "failed" );
			if( result ) {
				LOG( "r={%d,%d,%d,%d}", r.left, r.top, r.right, r.bottom );
			} else {
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
			PAINTSTRUCT ps; 
			HDC dc = BeginPaint( hwnd, &ps );
			LOG( "BeginPaint dc=%llu", (uint64_t)(uintptr_t)dc );
			LOG( "ps.hdc=%llu", (uint64_t)(uintptr_t)ps.hdc );
			LOG( "ps.fErase=%s", ps.fErase ? "true" : "false" );
			LOG( "ps.rcPaint={%d,%d,%d,%d}", ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right, ps.rcPaint.bottom );
			LOG( "ps.fRestore=%s", ps.fRestore ? "true" : "false" );
			LOG( "ps.fIncUpdate=%s", ps.fIncUpdate ? "true" : "false" );
			
			if( FillRect( dc, &r, windowData->foreground ) ) {
				LOG( "FillRect successful" );
			} else {
				LOG( "FillRect failed" );
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
			
			if( InflateRect( &r, -FRAME_WIDTH, -FRAME_WIDTH ) ) {
				LOG( "InflateRect(%d) successful", -FRAME_WIDTH );
				LOG( "r={%d,%d,%d,%d}", r.left, r.top, r.right, r.bottom );
			} else {
				LOG( "InflateRect(%d) failed", -FRAME_WIDTH );
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
			if( FillRect( dc, &r, windowData->background ) ) {
				LOG( "FillRect successful" );
			} else {
				LOG( "FillRect failed" );
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
			EndPaint( hwnd, &ps );
			LOG( "EndPaint successful" );
		} break;
	}
    return DefWindowProc( hwnd, message, wparam, lparam);
}


static int trackWindow( HWND trackedWindow ) {
	LOG( "trackWindow( hwnd=%llu )", (uint64_t)(uintptr_t)trackedWindow );
	struct FindWindowData findWindowData = { 
		trackedWindow, 
		FALSE 
	};
	
	LOG( "calling EnumWindows(findWindowHandle)" );
	BOOL result = EnumWindows( findWindowHandle, (LPARAM) &findWindowData );
	LOG( "EnumWindows(findWindowHandle) %s", !result && !findWindowData.found && GetLastError() ? "failed" : "successful" );
	if( !result && !findWindowData.found && GetLastError() ) {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	LOG( "findWindowData.found=%s", findWindowData.found ? "true" : "false" );
	if( !findWindowData.found ) {
		LOG( "EXIT_FAILURE" );
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

    if( RegisterClassA( &wc ) ) {
		LOG( "RegisterClassA successful");
	} else {
		LOG( "RegisterClassA failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	
	LOG( "creating window");
    HWND hwnd = CreateWindowExA( WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW, wc.lpszClassName, 
		NULL, WS_BORDER,  CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, NULL, NULL, 
		GetModuleHandleA( NULL ), 0 );
	LOG( "CreateWindowExA %s", hwnd ? "successful" : "failed" );
	if( !hwnd ) {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}

	COLORREF transparency_key = RGB( 0, 255, 255 );
	HBRUSH background = CreateSolidBrush( transparency_key );
	LOG( "CreateSolidBrush(transparency_key) %s", background ? "successful" : "failed" );
	if( !background ) {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	HBRUSH foreground = CreateSolidBrush( FRAME_COLOR );
	LOG( "CreateSolidBrush(FRAME_COLOR) %s", foreground ? "successful" : "failed" );
	if( !foreground ) {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	struct WindowData windowData = { 
		trackedWindow, 
		background, 
		foreground 
	};

	if( SetWindowLongPtrA( hwnd, GWLP_USERDATA, (LONG_PTR)&windowData ) || !GetLastError() ) {
		LOG( "SetWindowLongPtrA(GWLP_USERDATA) successful" );
	} else {
		LOG( "SetWindowLongPtrA(GWLP_USERDATA) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( SetWindowLongA(hwnd, GWL_STYLE, WS_VISIBLE ) ) {
		LOG( "SetWindowLongA(GWL_STYLE) successful" );
	} else {
		LOG( "SetWindowLongA(GWL_STYLE) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( SetLayeredWindowAttributes(hwnd, transparency_key, 0, LWA_COLORKEY) ) {
		LOG( "SetLayeredWindowAttributes successful" );
	} else {
		LOG( "SetLayeredWindowAttributes failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( SetWindowLongPtrA( hwnd, GWLP_HWNDPARENT, (LONG_PTR) trackedWindow ) || !GetLastError() ) {
		LOG( "SetWindowLongPtrA(GWLP_HWNDPARENT) successful" );
	} else {
		LOG( "SetWindowLongPtrA(GWLP_HWNDPARENT) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	LOG( "starting timer" );
	trackWindowTimerProc( hwnd, 0, 0, 0 );

	LOG( "windows message pump" );
	MSG msg = { NULL };
	while( GetMessage( &msg, NULL, 0, 0 ) ) {
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	if( DeleteObject( windowData.foreground ) ) {
		LOG( "DeleteObject(foreground) successful" );
	} else {
		LOG( "DeleteObject(foreground) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	
	if( DeleteObject( windowData.background ) ) {
		LOG( "DeleteObject(background) successful" );
	} else {
		LOG( "DeleteObject(background) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( DestroyWindow( hwnd ) ) {
		LOG( "DestroyWindow successful" );
	} else {
		LOG( "DestroyWindow failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( UnregisterClassA( wc.lpszClassName, GetModuleHandleA( NULL ) ) ) {
		LOG( "UnregisterClassA successful" );
	} else {
		LOG( "UnregisterClassA failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}

	LOG( "EXIT_SUCCESS" );
	return EXIT_SUCCESS;
}



struct FindScreenData {
	uint32_t hash;
	BOOL found;
	RECT bounds;
};

uint32_t SuperFastHash (const char * data, int len); // Definition at end of file


static BOOL CALLBACK findScreen( HMONITOR monitor, HDC dc, LPRECT rect, LPARAM lparam ) {	
	LOG( "findScreen( monitor=%llu, dc=%llu, rect=%llu, lparam=%llu )", (uint64_t)(uintptr_t)monitor, (uint64_t)(uintptr_t)dc, (uint64_t)(uintptr_t)rect, (uint64_t)(uintptr_t)lparam );
	struct FindScreenData* findScreenData = (struct FindScreenData*) lparam;
	if( rect ) {
		LOG( "rect={%d,%d,%d,%d}", rect->left, rect->top, rect->right, rect->bottom );
	}

    MONITORINFOEXA info;
    info.cbSize = sizeof( info );
	if( GetMonitorInfoA( monitor, (MONITORINFO*) &info ) ) {
		LOG( "GetMonitorInfoA succesful" );
	} else {
		LOG( "GetMonitorInfoA failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	LOG( "info.szDevice=%s", info.szDevice );
	LOG( "info.rcMonitor={%d,%d,%d,%d}", info.rcMonitor.left, info.rcMonitor.top, info.rcMonitor.right, info.rcMonitor.bottom );
	LOG( "info.rcWork={%d,%d,%d,%d}", info.rcWork.left, info.rcWork.top, info.rcWork.right, info.rcWork.bottom );
	LOG( "info.dwFlags=%u", info.dwFlags );

    uint32_t hash = SuperFastHash( info.szDevice, strlen( info.szDevice ) );
	LOG( "hash=%u", hash );

    // If no hash is passed in (hash==0), pick the first screen we find
	LOG( "findScreenData->hash=%u", findScreenData->hash );
    if( findScreenData->hash == 0 || hash == findScreenData->hash ) {
		LOG( "findScreenData->hash == 0 || hash == findScreenData->hash" );
		LOG( "findScreenData->found = TRUE" );
		findScreenData->found = TRUE;
		findScreenData->bounds = *rect;
		LOG( "findScreenData->bounds={%d,%d,%d,%d}", findScreenData->bounds.left, findScreenData->bounds.top, findScreenData->bounds.right, findScreenData->bounds.bottom );
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
			LOG( "WM_CLOSE" );
			PostQuitMessage( 0 );
			LOG( "PostQuitMessage" );
		} break;
		case WM_PAINT: {
			LOG( "WM_PAINT" );
			RECT r;
			BOOL result = GetClientRect( hwnd, &r );
			LOG( "GetClientRect %s", result ? "successful" : "failed" );
			if( result ) {
				LOG( "r={%d,%d,%d,%d}", r.left, r.top, r.right, r.bottom );
			} else {
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
			
			if( FillRect( screenData->deviceContext, &r, screenData->foreground ) ) {
				LOG( "FillRect successful" );
			} else {
				LOG( "FillRect failed" );
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
			
			if( InflateRect( &r, -5, -5 ) ) {
				LOG( "InflateRect(%d) successful", -5 );
				LOG( "r={%d,%d,%d,%d}", r.left, r.top, r.right, r.bottom );
			} else {
				LOG( "InflateRect(%d) failed", -FRAME_WIDTH );
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
			r.bottom -= 1;
			LOG( "r={%d,%d,%d,%d}", r.left, r.top, r.right, r.bottom );
			if( FillRect( screenData->deviceContext, &r, screenData->background ) ) {
				LOG( "FillRect successful" );
			} else {
				LOG( "FillRect failed" );
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
		} break;
	}
    return DefWindowProc( hwnd, message, wparam, lparam);
}


static int trackScreen( RECT bounds ) {
	LOG( "trackScreen( bounds={%d,%d,%d,%d} )", bounds.left, bounds.top, bounds.right, bounds.bottom );

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

    if( RegisterClassA( &wc ) ) {
		LOG( "RegisterClassA successful");
	} else {
		LOG( "RegisterClassA failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}

	LOG( "creating window");
    HWND hwnd = CreateWindowExA( WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_TOOLWINDOW | WS_EX_TOPMOST, wc.lpszClassName, 
		NULL, WS_VISIBLE, bounds.left, bounds.top, bounds.right - bounds.left, bounds.bottom - bounds.top , NULL, NULL, 
		GetModuleHandleA( NULL ), 0 );
	LOG( "CreateWindowExA %s", hwnd ? "successful" : "failed" );
	if( !hwnd ) {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}

	COLORREF transparency_key = RGB( 0, 255, 255 );
	HBRUSH background = CreateSolidBrush( transparency_key );
	LOG( "CreateSolidBrush(transparency_key) %s", background ? "successful" : "failed" );
	if( !background ) {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	HBRUSH foreground = CreateSolidBrush( FRAME_COLOR );
	LOG( "CreateSolidBrush(FRAME_COLOR) %s", foreground ? "successful" : "failed" );
	if( !foreground ) {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	struct ScreenData screenData = { 
		GetDC( hwnd ), 
		background, 
		foreground 
	};

	if( SetWindowLongPtrA( hwnd, GWLP_USERDATA, (LONG_PTR)&screenData ) || !GetLastError() ) {
		LOG( "SetWindowLongPtrA(GWLP_USERDATA) successful" );
	} else {
		LOG( "SetWindowLongPtrA(GWLP_USERDATA) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( SetWindowLongA(hwnd, GWL_STYLE, WS_VISIBLE ) ) {
		LOG( "SetWindowLongA(GWL_STYLE) successful" );
	} else {
		LOG( "SetWindowLongA(GWL_STYLE) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( SetLayeredWindowAttributes(hwnd, transparency_key, 0, LWA_COLORKEY) ) {
		LOG( "SetLayeredWindowAttributes successful" );
	} else {
		LOG( "SetLayeredWindowAttributes failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( UpdateWindow( hwnd ) ) {
		LOG( "UpdateWindow successful" );
	} else {
		LOG( "UpdateWindow failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}

	LOG( "windows message pump" );
	MSG msg = { NULL };
	while (GetMessage(&msg, NULL, 0, 0)) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	if( DeleteObject( screenData.foreground ) ) {
		LOG( "DeleteObject(foreground) successful" );
	} else {
		LOG( "DeleteObject(foreground) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	
	if( DeleteObject( screenData.background ) ) {
		LOG( "DeleteObject(background) successful" );
	} else {
		LOG( "DeleteObject(background) failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	ReleaseDC( hwnd, screenData.deviceContext );
	if( DestroyWindow( hwnd ) ) {
		LOG( "DestroyWindow successful" );
	} else {
		LOG( "DestroyWindow failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	if( UnregisterClassA( wc.lpszClassName, GetModuleHandleA( NULL ) ) ) {
		LOG( "UnregisterClassA successful" );
	} else {
		LOG( "UnregisterClassA failed" );
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}

	LOG( "EXIT_SUCCESS" );
	return EXIT_SUCCESS;
}


int main( int argc, char* argv[] ) {
	LOG( "ScreenShareIndicatorFrame" );
	LOG( "argc=%d", argc );
	for( int i = 0; i < argc; ++i ) {
		LOG( "argv[%d]=%s", i, argv[ i ] );
	}
	// Dynamic binding of functions not available on win 7
	HMODULE user32lib = LoadLibraryA( "user32.dll" );
	LOG( "user32lib=%s", user32lib ? "valid" : "null" );
	if( user32lib ) {
		LOG( "user32lib loaded" );
		BOOL (WINAPI *SetProcessDpiAwarenessContextPtr) ( DPI_AWARENESS_CONTEXT ) = (BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT)) GetProcAddress( user32lib, "SetProcessDpiAwarenessContext" );
		LOG( "SetProcessDpiAwarenessContextPtr=%s", SetProcessDpiAwarenessContextPtr ? "valid" : "null" );

		if( SetProcessDpiAwarenessContextPtr ) {
			#define SCREENSHARE_INDICATOR_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2  ((DPI_AWARENESS_CONTEXT)-4)
			LOG( "calling SetProcessDpiAwarenessContextPtr with param %d", SCREENSHARE_INDICATOR_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 );
			BOOL result = SetProcessDpiAwarenessContextPtr( SCREENSHARE_INDICATOR_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2  );
			LOG( "SetProcessDpiAwarenessContextPtr %s", result ? "successful" : "failed" );
			if( !result ) {
				LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
			}
		}

		LOG( "unloading user32lib" );
		FreeLibrary( user32lib );
		LOG( "user32lib unloaded" );
	} else {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}

	LOG( "closing existing instances" );
	BOOL result = EnumWindows( closeExistingInstance, 0 );
	LOG( "EnumWindows(closeExistingInstance) %s", result ? "successful" : "failed" );
	if( !result ) {
		LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
	}
	
	if( argc == 2 ) {
		LOG( "argc==2" );
        uint64_t arg = atoll( argv[ 1 ] );
		LOG( "arg==%llu", arg );
        struct FindScreenData findScreenData = { (uint32_t) arg, FALSE };
        
		LOG( "calling EnumDisplayMonitors" );
        result = EnumDisplayMonitors( NULL, NULL, findScreen, (LPARAM) &findScreenData );
		LOG( "EnumDisplayMonitors %s", result ? "successful" : "failed" );
		if( !result ) {
			LOG( "GetLastError(%d): %s", GetLastError(), error_message() );
		}
		LOG( "findScreenData.found==%s", findScreenData.found ? "true" : "false" );
        if( findScreenData.found ) {
			LOG( "tracking screen" );
			LOG( "findScreenData.bounds={%d,%d,%d,%d}", findScreenData.bounds.left, findScreenData.bounds.top, findScreenData.bounds.right, findScreenData.bounds.bottom );
            int result = trackScreen( findScreenData.bounds );
			LOG( "%s", result == EXIT_SUCCESS ? "EXIT_SUCCESS" : "EXIT_FAILURE" );	
			close_log_file();
			return result;
        }
        
		LOG( "tracking window" );
		HWND trackedWindow = (HWND)( (uintptr_t) atoll( argv[ 1 ] ) );
		if( trackedWindow == NULL ) {
			LOG( "trackedWindow==NULL" );	
			LOG( "EXIT_FAILURE" );	
			close_log_file();
			return EXIT_FAILURE;
		}
		int result = trackWindow( trackedWindow );
		LOG( "%s", result == EXIT_SUCCESS ? "EXIT_SUCCESS" : "EXIT_FAILURE" );	
		close_log_file();
		return result;
	} else if( argc > 2 ) {
		LOG( "argc>2" );
		LOG( "EXIT_FAILURE" );	
		close_log_file();
        return EXIT_FAILURE;
    }

	LOG( "argc<2" );
	LOG( "close existing instances requested" );
	LOG( "EXIT_SUCCESS" );	
	close_log_file();
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