#import <Cocoa/Cocoa.h>
#import <Vision/Vision.h>
#import <ApplicationServices/ApplicationServices.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static NSDictionary *window_info(pid_t pid)
{
    NSArray *windows = CFBridgingRelease(CGWindowListCopyWindowInfo(
        kCGWindowListOptionOnScreenOnly | kCGWindowListExcludeDesktopElements, kCGNullWindowID));
    for (NSDictionary *window in windows) {
        if ([window[(__bridge NSString *)kCGWindowOwnerPID] intValue] == pid &&
            [window[(__bridge NSString *)kCGWindowLayer] intValue] == 0 &&
            [window[(__bridge NSString *)kCGWindowName] isEqualToString:@"Termole"]) {
            return window;
        }
    }
    return nil;
}

static int find_window(pid_t pid)
{
    NSDictionary *window = window_info(pid);
    if (!window) return 1;
    printf("%u\n", [window[(__bridge NSString *)kCGWindowNumber] unsignedIntValue]);
    return 0;
}

static id attribute(AXUIElementRef element, CFStringRef key);

static bool focus_window(pid_t pid)
{
    NSDictionary *info = window_info(pid);
    CGRect rect;
    if (!info || !CGRectMakeWithDictionaryRepresentation(
        (__bridge CFDictionaryRef)info[(__bridge NSString *)kCGWindowBounds], &rect)) return false;
    NSRunningApplication *app = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    AXUIElementRef target = AXUIElementCreateApplication(pid);
    NSArray *windows = attribute(target, kAXWindowsAttribute);
    CFRelease(target);
    if (windows.count != 1) {
        fprintf(stderr, "Cannot inspect pid %d windows (AX trusted=%d, count=%lu)\n",
                pid, AXIsProcessTrusted(), (unsigned long)windows.count);
        return false;
    }
    AXUIElementRef window = (__bridge AXUIElementRef)windows.firstObject;
    if (![attribute(window, kAXTitleAttribute) isEqualToString:@"Termole"]) return false;
    AXUIElementRef system = AXUIElementCreateSystemWide();
    AXError raised = AXUIElementPerformAction(window, kAXRaiseAction);
    bool focused = false;
    pid_t front = -1;
    for (int i = 0; i < 50; ++i) {
        id current = attribute(system, kAXFocusedApplicationAttribute);
        if (current) AXUIElementGetPid((__bridge AXUIElementRef)current, &front);
        id focused_window = front == pid && current ?
            attribute((__bridge AXUIElementRef)current, kAXFocusedWindowAttribute) : nil;
        if (focused_window && CFEqual(window, (__bridge CFTypeRef)focused_window)) {
            focused = true;
            break;
        }
        // Click the title bar only if the system's hit test identifies this
        // process at the click point. This cannot insert text or select cells.
        if (i == 0) {
            CGPoint p = CGPointMake(CGRectGetMidX(rect), CGRectGetMinY(rect) + 12);
            AXUIElementRef hit = NULL;
            pid_t hit_pid = -1;
            if (AXUIElementCopyElementAtPosition(system, p.x, p.y, &hit) == kAXErrorSuccess && hit) {
                AXUIElementGetPid(hit, &hit_pid);
                CFRelease(hit);
            }
            if (hit_pid == pid) {
                const CGEventType clicks[] = {kCGEventLeftMouseDown, kCGEventLeftMouseUp};
                for (int j = 0; j < 2; ++j) {
                    CGEventRef event = CGEventCreateMouseEvent(NULL, clicks[j], p, kCGMouseButtonLeft);
                    CGEventPost(kCGHIDEventTap, event);
                    CFRelease(event);
                    usleep(40000);
                }
            }
        }
        [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:0.02]];
    }
    CFRelease(system);
    if (!focused) fprintf(stderr, "Focus failed: target=%d front=%d policy=%ld AXRaise=%d\n",
                          pid, front, (long)app.activationPolicy, (int)raised);
    return focused;
}

static void key(CGKeyCode code, const UniChar *character, CGEventFlags flags)
{
    for (int down = 1; down >= 0; --down) {
        CGEventRef event = CGEventCreateKeyboardEvent(NULL, code, down);
        CGEventSetFlags(event, flags);
        if (character) CGEventKeyboardSetUnicodeString(event, 1, character);
        CGEventPost(kCGHIDEventTap, event);
        CFRelease(event);
        usleep(8000);
    }
}

static int type_command(pid_t pid, NSString *command, bool submit)
{
    if (!focus_window(pid)) return 1;
    // US ANSI keyboard positions used by the macOS CI runner. AppKit text
    // interpretation can translate the physical key independently of the
    // Unicode string that Quartz attaches to an event.
    static const struct { char plain, shifted; CGKeyCode code; } keys[] = {
        {'a','A',0}, {'s','S',1}, {'d','D',2}, {'f','F',3}, {'h','H',4}, {'g','G',5},
        {'z','Z',6}, {'x','X',7}, {'c','C',8}, {'v','V',9}, {'b','B',11}, {'q','Q',12},
        {'w','W',13}, {'e','E',14}, {'r','R',15}, {'y','Y',16}, {'t','T',17},
        {'1','!',18}, {'2','@',19}, {'3','#',20}, {'4','$',21}, {'6','^',22}, {'5','%',23},
        {'=','+',24}, {'9','(',25}, {'7','&',26}, {'-','_',27}, {'8','*',28}, {'0',')',29},
        {']','}',30}, {'o','O',31}, {'u','U',32}, {'[','{',33}, {'i','I',34}, {'p','P',35},
        {'l','L',37}, {'\'','"',39}, {'k','K',40}, {';',':',41}, {'\\','|',42}, {',','<',43},
        {'/','?',44}, {'n','N',45}, {'m','M',46}, {'.','>',47}, {' ',' ',49}, {'`','~',50},
        {'j','J',38}
    };
    for (NSUInteger i = 0; i < command.length; ++i) {
        UniChar character = [command characterAtIndex:i];
        size_t k = 0;
        while (k < sizeof(keys) / sizeof(keys[0]) &&
               character != keys[k].plain && character != keys[k].shifted) ++k;
        if (k == sizeof(keys) / sizeof(keys[0])) {
            fprintf(stderr, "Unsupported keyboard character U+%04x\n", character);
            return 1;
        }
        key(keys[k].code, &character, character == keys[k].plain ? 0 : kCGEventFlagMaskShift);
    }
    if (submit) key(36, NULL, 0); // Return on an Apple keyboard.
    return 0;
}

static int read_lines(NSString *file, bool geometry)
{
    NSURL *url = [NSURL fileURLWithPath:file];
    VNImageRequestHandler *handler = [[VNImageRequestHandler alloc] initWithURL:url options:@{}];
    VNRecognizeTextRequest *request = [[VNRecognizeTextRequest alloc] init];
    request.recognitionLevel = VNRequestTextRecognitionLevelAccurate;
    request.recognitionLanguages = @[@"en-US"];
    request.usesLanguageCorrection = NO;
    NSError *error = nil;
    if (![handler performRequests:@[request] error:&error]) {
        fprintf(stderr, "%s\n", error.localizedDescription.UTF8String);
        return 1;
    }
    for (VNRecognizedTextObservation *line in request.results) {
        VNRecognizedText *text = [line topCandidates:1].firstObject;
        if (text) {
            if (geometry) printf("%.6f\t", CGRectGetMidY(line.boundingBox));
            puts(text.string.UTF8String);
        }
    }
    return 0;
}

static bool cursor_color(NSBitmapImageRep *image, NSInteger x, NSInteger y)
{
    NSColor *color = [[image colorAtX:x y:y] colorUsingColorSpace:NSColorSpace.sRGBColorSpace];
    return fabs(color.redComponent - 228 / 255.0) < 0.035 &&
           fabs(color.greenComponent - 231 / 255.0) < 0.035 &&
           fabs(color.blueComponent - 236 / 255.0) < 0.035;
}

// Look for the solid inverted blank cell on the command's OCR row. Sampling
// three heights excludes bright glyph strokes in that row.
static int cursor_x(NSString *file, double vision_y)
{
    NSBitmapImageRep *image = [NSBitmapImageRep imageRepWithData:[NSData dataWithContentsOfFile:file]];
    NSInteger row = (NSInteger)((1 - vision_y) * image.pixelsHigh);
    if (!image || row < 4 || row + 4 >= image.pixelsHigh) return 1;
    NSInteger run = 0;
    for (NSInteger x = 0; x < image.pixelsWide; ++x) {
        if (cursor_color(image, x, row - 4) && cursor_color(image, x, row) &&
            cursor_color(image, x, row + 4)) {
            if (++run == 7) {
                printf("%ld\n", (long)(x - run + 1));
                return 0;
            }
        } else {
            run = 0;
        }
    }
    return 1;
}

static bool bounds(NSDictionary *window, CGRect *rect)
{
    return window && CGRectMakeWithDictionaryRepresentation(
        (__bridge CFDictionaryRef)window[(__bridge NSString *)kCGWindowBounds], rect);
}

static int drag(pid_t pid, double start, double end, double y)
{
    NSDictionary *window = window_info(pid);
    CGRect rect;
    if (!bounds(window, &rect) || start < 0 || end >= 1 || end <= start || y <= 0 || y >= 1)
        return 1;
    const unsigned int wid = [window[(__bridge NSString *)kCGWindowNumber] unsignedIntValue];
    const CGEventType types[] = {kCGEventLeftMouseDown, kCGEventLeftMouseDragged, kCGEventLeftMouseUp};
    for (int i = 0; i < 3; ++i) {
        CGPoint point = CGPointMake(rect.origin.x + (i == 0 ? start : end) * rect.size.width,
                                    rect.origin.y + (1 - y) * rect.size.height);
        CGEventRef event = CGEventCreateMouseEvent(NULL, types[i], point, kCGMouseButtonLeft);
        CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointer, wid);
        CGEventSetIntegerValueField(event, kCGMouseEventWindowUnderMousePointerThatCanHandleThisEvent, wid);
        CGEventPostToPid(pid, event);
        CFRelease(event);
        usleep(50000);
    }
    return 0;
}

static id attribute(AXUIElementRef element, CFStringRef key)
{
    CFTypeRef result = NULL;
    if (AXUIElementCopyAttributeValue(element, key, &result) != kAXErrorSuccess) return nil;
    return CFBridgingRelease(result);
}

static int change_window(pid_t pid, bool close, CGSize size)
{
    if (!window_info(pid) || !AXIsProcessTrusted()) return 1;
    AXUIElementRef app = AXUIElementCreateApplication(pid);
    NSArray *windows = attribute(app, kAXWindowsAttribute);
    CFRelease(app);
    if (windows.count != 1) return 1;
    AXUIElementRef window = (__bridge AXUIElementRef)windows.firstObject;
    if (![attribute(window, kAXTitleAttribute) isEqualToString:@"Termole"]) return 1;
    if (close) {
        id button = attribute(window, kAXCloseButtonAttribute);
        return button && AXUIElementPerformAction((__bridge AXUIElementRef)button,
                                                   kAXPressAction) == kAXErrorSuccess ? 0 : 1;
    }
    AXValueRef value = AXValueCreate(kAXValueTypeCGSize, &size);
    AXError result = AXUIElementSetAttributeValue(window, kAXSizeAttribute, value);
    CFRelease(value);
    return result == kAXErrorSuccess ? 0 : 1;
}

static int named_key(pid_t pid, NSString *name)
{
    NSDictionary *codes = @{@"copy": @8, @"paste": @9, @"up": @126, @"esc": @53, @"enter": @36};
    NSNumber *code = codes[name];
    if (!code || !focus_window(pid)) return 1;
    bool cmd = [name isEqualToString:@"copy"] || [name isEqualToString:@"paste"];
    if (cmd) {
        CGEventRef modifier = CGEventCreateKeyboardEvent(NULL, 55, true);
        CGEventSetFlags(modifier, kCGEventFlagMaskCommand);
        CGEventPost(kCGHIDEventTap, modifier);
        CFRelease(modifier);
    }
    key(code.unsignedShortValue, NULL, cmd ? kCGEventFlagMaskCommand : 0);
    if (cmd) {
        CGEventRef modifier = CGEventCreateKeyboardEvent(NULL, 55, false);
        CGEventSetFlags(modifier, 0);
        CGEventPost(kCGHIDEventTap, modifier);
        CFRelease(modifier);
    }
    return 0;
}

int main(int argc, const char **argv)
{
    @autoreleasepool {
        if (argc == 2 && strcmp(argv[1], "permissions") == 0) {
            if (!AXIsProcessTrusted()) {
                fputs("Accessibility control is unavailable for this runner.\n", stderr);
                return 1;
            }
            if (!CGPreflightScreenCaptureAccess()) {
                fputs("Screen Recording access is unavailable for this runner.\n", stderr);
                return 1;
            }
            if (!CGPreflightPostEventAccess()) {
                fputs("Keyboard event posting access is unavailable for this runner.\n", stderr);
                return 1;
            }
            return 0;
        }
        if (argc == 3 && strcmp(argv[1], "window") == 0) return find_window(atoi(argv[2]));
        if (argc == 3 && strcmp(argv[1], "read") == 0) return read_lines(@(argv[2]), false);
        if (argc == 4 && strcmp(argv[1], "type") == 0)
            return type_command(atoi(argv[2]), @(argv[3]), true);
        if (argc == 4 && strcmp(argv[1], "text") == 0)
            return type_command(atoi(argv[2]), @(argv[3]), false);
        if (argc == 4 && strcmp(argv[1], "key") == 0) return named_key(atoi(argv[2]), @(argv[3]));
        if (argc == 3 && strcmp(argv[1], "lines") == 0) return read_lines(@(argv[2]), true);
        if (argc == 4 && strcmp(argv[1], "cursor") == 0)
            return cursor_x(@(argv[2]), atof(argv[3]));
        if (argc == 3 && strcmp(argv[1], "close") == 0)
            return change_window(atoi(argv[2]), true, CGSizeZero);
        if (argc == 5 && strcmp(argv[1], "resize") == 0)
            return change_window(atoi(argv[2]), false, CGSizeMake(atoi(argv[3]), atoi(argv[4])));
        if (argc == 6 && strcmp(argv[1], "drag") == 0)
            return drag(atoi(argv[2]), atof(argv[3]), atof(argv[4]), atof(argv[5]));
        if (argc == 3 && strcmp(argv[1], "size") == 0) {
            CGRect rect;
            if (!bounds(window_info(atoi(argv[2])), &rect)) return 1;
            printf("%.0f %.0f\n", rect.size.width, rect.size.height);
            return 0;
        }
        return 2;
    }
}
