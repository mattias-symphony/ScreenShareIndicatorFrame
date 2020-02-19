#import <Cocoa/Cocoa.h>
#include <stdlib.h>

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
        // if( windowid ) {
        //     printf("%s:%s:%s\n", [ownerName UTF8String], [[windowid stringValue] UTF8String], [[ownerPID stringValue] UTF8String]);
        // }
        if( [ownerName isEqualToString:processName] ) {
            // printf("Killing %d\n", [ownerPID intValue]);
            kill( [ownerPID intValue], SIGKILL );
        }
    }
    CFRelease( windowArray );
}


int main( int argc, char** argv ) {
    [NSAutoreleasePool new];

    closeExistingInstances();

    if( argc < 2 ) {
        return EXIT_SUCCESS;
    }

    int displayId = atoi( argv[ 1 ] );

    NSRect windowRect;
    bool found = false;
    for( int i = 0; i < NSScreen.screens.count; ++i ) {
        NSDictionary* screenDictionary = [NSScreen.screens[ i ] deviceDescription];
        NSNumber* screenID = [screenDictionary objectForKey:@"NSScreenNumber"];
        if( [screenID intValue] == displayId ) {
            windowRect = NSScreen.screens[ i ].frame;
            found = true;
            break;
        }
    }

    if( !found ) {
        return EXIT_FAILURE;
    }

    [NSApplication sharedApplication];

    NSWindow* window = [[Window alloc] initWithContentRect:windowRect
        styleMask:NSWindowStyleMaskBorderless backing:NSBackingStoreBuffered defer:NO
        screen:[NSScreen mainScreen]];
    [window setReleasedWhenClosed:YES];
    [window setLevel:CGShieldingWindowLevel()];
    [window setOpaque:NO];
    [window setBackgroundColor:[NSColor clearColor]];
    [window setIgnoresMouseEvents:YES];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [NSApp run];
    return EXIT_SUCCESS;
}