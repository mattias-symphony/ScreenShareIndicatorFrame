#import <Cocoa/Cocoa.h>
#include <stdlib.h>

int trackedWindowId;

@interface View : NSView
@end

@implementation View
- (void)drawRect:(NSRect)rect {
    NSColor* color = [NSColor colorWithDeviceRed:(0xee/255.0f) green:(0x3d/255.0f) blue:(0x3d/255.0f) alpha:1.0f];
	[color set];
	NSRectFill(rect);

	rect.origin.x += 5;
	rect.origin.y += 5;
	rect.size.width -= 10;
	rect.size.height -= 10;

	[[NSColor clearColor] set];
	NSRectFill( rect );
}
@end

@interface Window : NSWindow
@end

@implementation Window
- (void)setContentView:(NSView *)aView {
	NSRect bounds = [self frame];
    View* view = [[View alloc] initWithFrame:bounds];
    [super setContentView:view];
}

-(void)updateWindow {
    NSRect windowRect;
    CFArrayRef windowArray = CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly, kCGNullWindowID );
    NSMutableArray *windowsInMap = [NSMutableArray arrayWithCapacity:64];
    NSArray*  windows = (NSArray*) CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly, kCGNullWindowID );
    NSUInteger count = [windows count];
    for (NSUInteger i = 0; i < count; i++) {
        NSDictionary*   nswindowsdescription = [windows objectAtIndex:i];
        NSNumber* windowid = (NSNumber*)[nswindowsdescription objectForKey:@"kCGWindowNumber"];
        if( [windowid intValue] == trackedWindowId ) {
            CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)[nswindowsdescription objectForKey:@"kCGWindowBounds"], &windowRect);
            break;
        }
    }
    CFRelease( windowArray );

    [self orderWindow:NSWindowAbove relativeTo:trackedWindowId];

    windowRect.origin.y = [ NSScreen.screens[ 0 ] frame ].size.height - windowRect.origin.y - windowRect.size.height;   
    [super setFrame:windowRect display: YES];
}
@end


void closeExistingInstances( void ) {
    NSString* processName = NSProcessInfo.processInfo.processName;
    CFArrayRef windowArray = CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly, kCGNullWindowID );
    NSMutableArray *windowsInMap = [NSMutableArray arrayWithCapacity:64];
    NSArray*  windows = (NSArray*) CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly, kCGNullWindowID );
    NSUInteger count = [windows count];
    for (NSUInteger i = 0; i < count; i++) {
        NSDictionary*   nswindowsdescription = [windows objectAtIndex:i];
        NSNumber* windowid = (NSNumber*)[nswindowsdescription objectForKey:@"kCGWindowNumber"];
        NSString* ownerName = (NSString*)[nswindowsdescription objectForKey:@"kCGWindowOwnerName"];
        NSNumber* ownerPID = (NSNumber*)[nswindowsdescription objectForKey:@"kCGWindowOwnerPID"];
        if( [ownerName isEqualToString:processName] ) {
            kill( [ownerPID intValue], SIGKILL );
        }
    }
    CFRelease( windowArray );
}


int trackWindow( int displayId ) {
    NSRect windowRect;
    CFArrayRef windowArray = CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly, kCGNullWindowID );
    NSMutableArray *windowsInMap = [NSMutableArray arrayWithCapacity:64];
    NSArray*  windows = (NSArray*) CGWindowListCopyWindowInfo( kCGWindowListOptionOnScreenOnly, kCGNullWindowID );
    NSUInteger count = [windows count];
    for (NSUInteger i = 0; i < count; i++) {
        NSDictionary*   nswindowsdescription = [windows objectAtIndex:i];
        NSNumber* windowid = (NSNumber*)[nswindowsdescription objectForKey:@"kCGWindowNumber"];
        if( [windowid intValue] == displayId ) {
            CGRectMakeWithDictionaryRepresentation((CFDictionaryRef)[nswindowsdescription objectForKey:@"kCGWindowBounds"], &windowRect);
            NSNumber* ownerPID = (NSNumber*)[nswindowsdescription objectForKey:@"kCGWindowOwnerPID"];
            NSRunningApplication* app = [NSRunningApplication runningApplicationWithProcessIdentifier: [ownerPID intValue] ];
            [app activateWithOptions: NSApplicationActivateIgnoringOtherApps];
         }
    }
    CFRelease( windowArray );

    windowRect.origin.y = [[NSScreen mainScreen] frame].size.height - windowRect.origin.y - windowRect.size.height;

    [NSApplication sharedApplication];

    trackedWindowId = displayId;

    NSWindow* window = [[Window alloc] initWithContentRect:windowRect
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO
        screen:NSScreen.screens[ 0 ]];
    [NSTimer scheduledTimerWithTimeInterval:0.1 target:window selector:@selector(updateWindow) userInfo:nil repeats:YES];    
    [window setReleasedWhenClosed:YES];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setIgnoresMouseEvents:YES];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
    return EXIT_SUCCESS;
}


int trackScreen( NSRect windowRect ) {
    [NSApplication sharedApplication];
    NSWindow* window = [[Window alloc] initWithContentRect:windowRect
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO
        screen:NSScreen.screens[ 0 ] ];
    [window setReleasedWhenClosed:YES];
    [window setLevel:CGShieldingWindowLevel()];
    [window setCollectionBehavior:NSWindowCollectionBehaviorCanJoinAllSpaces];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setIgnoresMouseEvents:YES];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
    return EXIT_SUCCESS;
}

int main( int argc, char** argv ) {
    [NSAutoreleasePool new];
    closeExistingInstances();

    if( argc < 2 ) {
        return EXIT_SUCCESS;
    }

    int displayId = atoi( argv[ 1 ] );

    for( int i = 0; i < NSScreen.screens.count; ++i ) {
        NSDictionary* screenDictionary = [NSScreen.screens[ i ] deviceDescription];
        NSNumber* screenID = [screenDictionary objectForKey:@"NSScreenNumber"];
        if( [screenID intValue] == displayId ) {
            NSRect windowRect = NSScreen.screens[ i ].frame;
            return trackScreen( windowRect );
            break;
        }
    }

    return trackWindow( displayId );
}