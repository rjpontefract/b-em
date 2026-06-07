/* vid_macos.m - macOS live-resize observer for b-em
 *
 * When the user drags the window resize handle, the macOS main thread updates
 * the Metal drawable backing store asynchronously. If Thread 7 (the Allegro
 * rendering thread) issues OpenGL draw calls at the same moment, the
 * GLDTextureRec::getTextureResource lookup crashes. We fix this by setting
 * vid_live_resizing while a live resize is in progress, which causes
 * video_doblit() to skip all draw calls until the resize is finished.
 */

#import <Cocoa/Cocoa.h>
#include <stdbool.h>

extern volatile bool vid_live_resizing;

@interface BemLiveResizeObserver : NSObject
@end

@implementation BemLiveResizeObserver

- (void)windowWillStartLiveResize:(NSNotification *)notification
{
    vid_live_resizing = true;
}

- (void)windowDidEndLiveResize:(NSNotification *)notification
{
    vid_live_resizing = false;
}

@end

static BemLiveResizeObserver *observer;
static NSWindow *bem_window;

void vid_macos_resize_init(void)
{
    /* Capture b-em's window while it is still key (called right after display
     * creation). We keep this reference so vid_macos_backing_scale() can read
     * the correct backingScaleFactor even when b-em does not have focus and
     * [NSApp mainWindow] / [NSApp keyWindow] return nil. */
    bem_window = [NSApp keyWindow];

    observer = [BemLiveResizeObserver new];
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    [nc addObserver:observer
           selector:@selector(windowWillStartLiveResize:)
               name:NSWindowWillStartLiveResizeNotification
             object:nil];
    [nc addObserver:observer
           selector:@selector(windowDidEndLiveResize:)
               name:NSWindowDidEndLiveResizeNotification
             object:nil];
}

float vid_macos_backing_scale(void)
{
    if (bem_window)
        return (float)[bem_window backingScaleFactor];
    return (float)[[NSScreen mainScreen] backingScaleFactor];
}
