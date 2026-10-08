// The launch window: screen shape, frame rate, multiplayer and updates, remembered in <data>/launcher.plist, then Play.
// Its art is fetched at install time (tools/df_art.py: the game's logo and textured backdrop from its Steam store page,
// or failing that the game's own splash and menu screens) into <data>/art, and a copy in the app's Resources/art.
// Nothing from the game is stored in this project; without the art the window writes the title in plain colours.
// The renderer (Colors: 256 / 16 Million / Hardware) and the in-game resolution stay in the game's own settings screen.
#import <AppKit/AppKit.h>
#include "../runtime/edition.h"

// ---------------------------------------------------------------- settings
@interface DSRow : NSObject
@property NSString *title; @property NSArray<NSDictionary *> *choices; @property NSInteger index;
@property (copy) void (^activate)(void);     /* a row that runs something instead of cycling (Updates) */
@end
@implementation DSRow @end

#ifdef DF2_EDITION                                   /* Delta Force 2's amber menus, Delta Force's green ones */
static NSColor *accent(void) { return [NSColor colorWithSRGBRed:0.98 green:0.72 blue:0.22 alpha:1]; }
static NSColor *accent_dim(void) { return [NSColor colorWithSRGBRed:0.62 green:0.45 blue:0.16 alpha:1]; }
#else
static NSColor *accent(void) { return [NSColor colorWithSRGBRed:0.45 green:0.92 blue:0.36 alpha:1]; }
static NSColor *accent_dim(void) { return [NSColor colorWithSRGBRed:0.27 green:0.55 blue:0.22 alpha:1]; }
#endif
static NSColor *text_c(void) { return [NSColor colorWithSRGBRed:0.93 green:0.93 blue:0.88 alpha:1]; }
static NSColor *dim(void) { return [NSColor colorWithSRGBRed:0.66 green:0.66 blue:0.6 alpha:1]; }
static NSFont *din(CGFloat size) { return [NSFont fontWithName:@"DINCondensed-Bold" size:size] ?: [NSFont systemFontOfSize:size weight:NSFontWeightBold]; }
static NSFont *body(CGFloat size, BOOL bold)
{
    return [NSFont fontWithName:bold ? @"AvenirNextCondensed-DemiBold" : @"AvenirNextCondensed-Medium" size:size]
        ?: [NSFont systemFontOfSize:size weight:bold ? NSFontWeightSemibold : NSFontWeightRegular];
}

// ---------------------------------------------------------------- the view
@interface DSLaunchView : NSView
@property NSArray<DSRow *> *rows; @property NSInteger focus; @property NSInteger hover; @property NSInteger pressed;
@property NSImage *backdrop, *logo; @property NSString *note;
@property (copy) void (^onPlay)(void); @property (copy) void (^onQuit)(void); @property (copy) void (^onChange)(void);
@end

enum { HIT_NONE = -1, HIT_PLAY = 100, HIT_QUIT = 101 };   /* rows: 10*row + 0 (left arrow) / 1 (value) / 2 (right arrow) */

@implementation DSLaunchView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (NSRect)rowRect:(NSInteger)k { return NSMakeRect(70, 206 + 74 * k, self.bounds.size.width - 140, 64); }
- (NSRect)arrow:(NSInteger)k right:(BOOL)right { NSRect r = [self rowRect:k]; return NSMakeRect(right ? NSMaxX(r) - 34 : NSMaxX(r) - 330, r.origin.y + 6, 30, 30); }
- (NSRect)valueRect:(NSInteger)k { NSRect r = [self rowRect:k]; return NSMakeRect(NSMaxX(r) - 300, r.origin.y + 7, 262, 30); }
- (NSRect)playRect { NSRect b = self.bounds; return NSMakeRect(NSMidX(b) + 12, b.size.height - 80, 220, 52); }
- (NSRect)quitRect { NSRect b = self.bounds; return NSMakeRect(NSMidX(b) - 232, b.size.height - 80, 220, 52); }
- (NSInteger)hit:(NSPoint)p
{
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return HIT_QUIT;
    for (NSInteger k = 0; k < (NSInteger)self.rows.count; k++) {
        if (NSPointInRect(p, [self arrow:k right:NO])) return 10 * k;
        if (NSPointInRect(p, [self arrow:k right:YES])) return 10 * k + 2;
        if (NSPointInRect(p, [self valueRect:k])) return 10 * k + 1;
    }
    return HIT_NONE;
}
- (void)updateTrackingAreas
{
    for (NSTrackingArea *t in self.trackingAreas) [self removeTrackingArea:t];
    [self addTrackingArea:[[NSTrackingArea alloc] initWithRect:self.bounds options:NSTrackingMouseMoved | NSTrackingActiveAlways | NSTrackingInVisibleRect owner:self userInfo:nil]];
}
- (void)mouseMoved:(NSEvent *)e { NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]]; if (h != self.hover) { self.hover = h; self.needsDisplay = YES; } }
- (void)mouseDown:(NSEvent *)e { self.pressed = [self hit:[self convertPoint:e.locationInWindow fromView:nil]]; self.needsDisplay = YES; }
- (void)mouseUp:(NSEvent *)e
{
    NSInteger h = [self hit:[self convertPoint:e.locationInWindow fromView:nil]], p = self.pressed; self.pressed = HIT_NONE; self.needsDisplay = YES;
    if (h != p || h == HIT_NONE) return;
    if (h == HIT_PLAY) { if (self.onPlay) self.onPlay(); return; }
    if (h == HIT_QUIT) { if (self.onQuit) self.onQuit(); return; }
    NSInteger k = h / 10; self.focus = k;
    [self step:k by:(h % 10 == 0) ? -1 : 1];
}
- (void)step:(NSInteger)k by:(NSInteger)dir
{
    DSRow *r = self.rows[k]; NSInteger n = (NSInteger)r.choices.count;
    if (r.activate) { r.activate(); self.needsDisplay = YES; return; }
    r.index = (r.index + dir + n) % n; self.needsDisplay = YES;
    if (self.onChange) self.onChange();
}
- (void)keyDown:(NSEvent *)e
{
    switch (e.keyCode) {
    case 0x24: case 0x4c: if (self.rows[self.focus].activate) { self.rows[self.focus].activate(); self.needsDisplay = YES; return; }
              if (self.onPlay) self.onPlay(); return;      /* Return, Enter */
    case 0x35: if (self.onQuit) self.onQuit(); return;                  /* Escape */
    case 0x7e: self.focus = (self.focus + (NSInteger)self.rows.count - 1) % (NSInteger)self.rows.count; self.needsDisplay = YES; return;
    case 0x7d: self.focus = (self.focus + 1) % (NSInteger)self.rows.count; self.needsDisplay = YES; return;
    case 0x7b: [self step:self.focus by:-1]; return;
    case 0x7c: [self step:self.focus by:1]; return;
    }
    [super keyDown:e];
}
static void draw_text(NSString *s, NSFont *f, NSColor *c, NSRect r, NSTextAlignment al, NSShadow *sh, CGFloat kern)
{
    NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new]; ps.alignment = al; ps.lineBreakMode = NSLineBreakByTruncatingTail;
    NSMutableDictionary *a = [@{NSFontAttributeName: f, NSForegroundColorAttributeName: c, NSParagraphStyleAttributeName: ps, NSKernAttributeName: @(kern)} mutableCopy];
    if (sh) a[NSShadowAttributeName] = sh;
    [s drawInRect:r withAttributes:a];
}
/* corner brackets, like the targeting marks of the game's HUD */
static void brackets(NSRect r, CGFloat len, CGFloat w, NSColor *c)
{
    [c setStroke]; NSBezierPath *p = [NSBezierPath bezierPath]; p.lineWidth = w; p.lineCapStyle = NSLineCapStyleSquare;
    CGFloat x0 = r.origin.x, y0 = r.origin.y, x1 = NSMaxX(r), y1 = NSMaxY(r);
    [p moveToPoint:NSMakePoint(x0, y0 + len)]; [p lineToPoint:NSMakePoint(x0, y0)]; [p lineToPoint:NSMakePoint(x0 + len, y0)];
    [p moveToPoint:NSMakePoint(x1 - len, y0)]; [p lineToPoint:NSMakePoint(x1, y0)]; [p lineToPoint:NSMakePoint(x1, y0 + len)];
    [p moveToPoint:NSMakePoint(x1, y1 - len)]; [p lineToPoint:NSMakePoint(x1, y1)]; [p lineToPoint:NSMakePoint(x1 - len, y1)];
    [p moveToPoint:NSMakePoint(x0 + len, y1)]; [p lineToPoint:NSMakePoint(x0, y1)]; [p lineToPoint:NSMakePoint(x0, y1 - len)];
    [p stroke];
}
- (void)drawButton:(NSRect)r title:(NSString *)title code:(NSInteger)code primary:(BOOL)primary
{
    BOOL hot = self.hover == code, down = self.pressed == code;
    NSColor *fill = down ? [accent() colorWithAlphaComponent:0.45] : hot ? [accent() colorWithAlphaComponent:0.28] : primary ? [accent_dim() colorWithAlphaComponent:0.35] : [NSColor colorWithWhite:0 alpha:0.55];
    [fill setFill]; NSRectFillUsingOperation(r, NSCompositingOperationSourceOver);
    [(primary || hot ? accent() : accent_dim()) setStroke]; NSBezierPath *b = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 0.5, 0.5)]; b.lineWidth = 1; [b stroke];
    brackets(NSInsetRect(r, -4, -4), 10, 2, primary || hot ? accent() : accent_dim());
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; sh.shadowOffset = NSMakeSize(0, -1.5); sh.shadowBlurRadius = 2;
    draw_text(title, din(primary ? 30 : 26), hot || primary ? NSColor.whiteColor : text_c(), NSMakeRect(r.origin.x, NSMidY(r) - (primary ? 14 : 12), r.size.width, 34), NSTextAlignmentCenter, sh, 4);
}
- (void)drawArrow:(NSRect)r right:(BOOL)right lit:(BOOL)lit
{
    NSBezierPath *p = [NSBezierPath bezierPath]; CGFloat x0 = r.origin.x + 9, x1 = NSMaxX(r) - 9, y0 = r.origin.y + 7, y1 = NSMaxY(r) - 7, ym = NSMidY(r);
    if (right) { [p moveToPoint:NSMakePoint(x0, y0)]; [p lineToPoint:NSMakePoint(x1, ym)]; [p lineToPoint:NSMakePoint(x0, y1)]; }
    else { [p moveToPoint:NSMakePoint(x1, y0)]; [p lineToPoint:NSMakePoint(x0, ym)]; [p lineToPoint:NSMakePoint(x1, y1)]; }
    [p closePath]; [(lit ? NSColor.whiteColor : accent()) setFill]; [p fill];
}
- (void)drawRect:(NSRect)dirty
{
    NSRect b = self.bounds;
    [[NSColor colorWithWhite:0.04 alpha:1] setFill]; NSRectFill(b);
    if (self.backdrop) {                                /* filling the window, the aspect kept (cropped at the sides) */
        CGFloat sc = MAX(b.size.width / self.backdrop.size.width, b.size.height / self.backdrop.size.height), w = self.backdrop.size.width * sc, h = self.backdrop.size.height * sc;
        [self.backdrop drawInRect:NSMakeRect(NSMidX(b) - w / 2, NSMidY(b) - h / 2, w, h) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
    }
    NSGradient *vig = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0 alpha:0.2], 0.0, [NSColor colorWithWhite:0 alpha:0.72], 1.0, nil];
    [vig drawInRect:b relativeCenterPosition:NSMakePoint(0, 0.2)];
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; sh.shadowOffset = NSMakeSize(0, -1); sh.shadowBlurRadius = 3;
    /* the logo, or the title written out */
    if (self.logo) {
        CGFloat h = 144, w = h * self.logo.size.width / self.logo.size.height; if (w > 440) { w = 440; h = w * self.logo.size.height / self.logo.size.width; }
        NSShadow *glow = [NSShadow new]; glow.shadowColor = [NSColor colorWithWhite:0 alpha:0.85]; glow.shadowOffset = NSZeroSize; glow.shadowBlurRadius = 14;
        [NSGraphicsContext saveGraphicsState]; [glow set];
        [self.logo drawInRect:NSMakeRect(NSMidX(b) - w / 2, 12 + (144 - h) / 2, w, h) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        [NSGraphicsContext restoreGraphicsState];
    }
    else draw_text(@DS2_TITLE.uppercaseString, din(72), NSColor.whiteColor, NSMakeRect(0, 52, b.size.width, 84), NSTextAlignmentCenter, sh, 8);
    draw_text(@"NATIVE EDITION FOR macOS", din(17), accent(), NSMakeRect(0, 164, b.size.width, 22), NSTextAlignmentCenter, sh, 5);
    /* the settings */
    NSRect panel = NSMakeRect(54, 194, b.size.width - 108, 74 * self.rows.count + 14);
    [[NSColor colorWithWhite:0 alpha:0.62] setFill]; NSRectFillUsingOperation(panel, NSCompositingOperationSourceOver);
    [[accent_dim() colorWithAlphaComponent:0.8] setStroke]; NSBezierPath *bp = [NSBezierPath bezierPathWithRect:NSInsetRect(panel, 0.5, 0.5)]; bp.lineWidth = 1; [bp stroke];
    brackets(NSInsetRect(panel, -5, -5), 18, 2.5, accent());
    for (NSInteger k = 0; k < (NSInteger)self.rows.count; k++) {
        DSRow *row = self.rows[k]; NSRect r = [self rowRect:k]; NSDictionary *ch = row.choices[row.index];
        if (k == self.focus) { [[accent() colorWithAlphaComponent:0.12] setFill]; NSRectFillUsingOperation(NSInsetRect(r, -10, -2), NSCompositingOperationSourceOver); [accent() setFill]; NSRectFill(NSMakeRect(r.origin.x - 10, r.origin.y - 2, 3, r.size.height + 4)); }
        draw_text(row.title.uppercaseString, din(24), accent(), NSMakeRect(r.origin.x + 4, r.origin.y + 10, 260, 30), NSTextAlignmentLeft, sh, 2);
        [self drawArrow:[self arrow:k right:NO] right:NO lit:self.hover == 10 * k];
        [self drawArrow:[self arrow:k right:YES] right:YES lit:self.hover == 10 * k + 2];
        draw_text(ch[@"label"], body(20, YES), self.hover == 10 * k + 1 ? NSColor.whiteColor : text_c(), [self valueRect:k], NSTextAlignmentCenter, sh, 0.5);
        draw_text(ch[@"note"] ?: @"", body(12.5, NO), dim(), NSMakeRect(NSMaxX(r) - 340, r.origin.y + 40, 350, 18), NSTextAlignmentCenter, nil, 0);
        if (k + 1 < (NSInteger)self.rows.count) { [[accent_dim() colorWithAlphaComponent:0.35] setFill]; NSRectFillUsingOperation(NSMakeRect(r.origin.x, NSMaxY(r) + 5, r.size.width, 1), NSCompositingOperationSourceOver); }
    }
    if (self.note.length) draw_text(self.note, body(12.5, NO), dim(), NSMakeRect(54, NSMaxY(panel) + 8, b.size.width - 108, 34), NSTextAlignmentCenter, sh, 0);
    [self drawButton:self.quitRect title:@"QUIT" code:HIT_QUIT primary:NO];
    [self drawButton:self.playRect title:@"PLAY" code:HIT_PLAY primary:YES];
}
@end

// ---------------------------------------------------------------- choices, settings file, the window
static NSArray<NSDictionary *> *shape_choices(void)
{
    NSSize s = NSScreen.mainScreen.frame.size; int W = (int)(768 * s.width / MAX(s.height, 1) / 16 + 0.5) * 16; if (W > 2048) W = 2048;
    NSString *wide = W > 1024 ? [NSString stringWithFormat:@"1024 × 768 in the game plays at %d × 768; wider view", W] : @"This display is 4:3 already";
    return @[@{@"value": @"screen", @"label": @"This Display", @"note": wide},
             @{@"value": @"off", @"label": @"Original 4:3", @"note": @"Black bars at the sides, as the game shipped"}];
}
static NSArray<NSDictionary *> *framerate_choices(void)
{
    NSInteger hz = NSScreen.mainScreen.maximumFramesPerSecond; if (hz <= 0) hz = 60;
    NSMutableArray *a = [NSMutableArray arrayWithObject:@{@"value": @"display", @"label": [NSString stringWithFormat:@"Display (%ld FPS)", (long)hz],
        @"note": @"Smooth motion between the game's 62.5 ticks a second"}];
    if (hz > 60) [a addObject:@{@"value": @"60", @"label": @"60 FPS", @"note": @"Smooth motion, capped at 60; cooler and quieter"}];
    [a addObject:@{@"value": @"original", @"label": @"Original", @"note": @"The game's own pacing, as it shipped"}];
    return a;
}
static NSArray<NSDictionary *> *novaworld_choices(void)
{
    return @[@{@"value": @"on", @"label": @"Public Game List", @"note": @"NovaWorld finds games through the BitTorrent DHT"},
             @{@"value": @"off", @"label": @"LAN and Addresses", @"note": @"No public list; join by address or on your network"}];
}
static NSArray<NSDictionary *> *router_choices(void)
{
    return @[@{@"value": @"on", @"label": @"Open Port When Hosting", @"note": @"Asks your router (UPnP / NAT-PMP); closed on exit"},
             @{@"value": @"off", @"label": @"Leave Router Alone", @"note": @"Forward UDP 3568 yourself to host online"}];
}
static NSInteger index_of(NSArray<NSDictionary *> *c, NSString *v, NSInteger dflt)
{
    for (NSUInteger k = 0; k < c.count; k++) if ([c[k][@"value"] isEqualToString:v ?: @""]) return (NSInteger)k;
    return dflt;
}
/* the chosen settings as environment for the runtime (each read later, after start-up: df_res.c, ddraw.c, df_frame.c,
 * df_net.c, portmap.c). An environment already set by the player (a terminal) wins. */
static void apply(NSString *shape, NSString *fps, NSString *nova, NSString *router)
{
    if (!getenv("DF_RES") && [shape isEqualToString:@"off"]) setenv("DF_RES", "off", 1);
    if (!getenv("DSR_FPSCAP") && !getenv("DF_HIGH_FPS")) {
        if ([fps isEqualToString:@"original"]) setenv("DF_HIGH_FPS", "0", 1);
        else if ([fps isEqualToString:@"60"]) setenv("DSR_FPSCAP", "60", 1);
    }
    if (!getenv("DF_NO_P2P") && [nova isEqualToString:@"off"]) setenv("DF_NO_P2P", "1", 1);
    if (!getenv("DS_NO_PORTMAP") && [router isEqualToString:@"off"]) setenv("DS_NO_PORTMAP", "1", 1);
}
/* the art: <data>/art, else the copy in the app (Resources/art) */
static NSImage *art(NSString *data, NSString *name)
{
    NSMutableArray *dirs = [NSMutableArray arrayWithObject:[data stringByAppendingPathComponent:@"art"]];
    if (NSBundle.mainBundle.resourcePath) [dirs addObject:[NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"art"]];
    for (NSString *d in dirs) for (NSString *ext in @[@"png", @"jpg"]) {
        NSImage *i = [[NSImage alloc] initWithContentsOfFile:[[d stringByAppendingPathComponent:name] stringByAppendingPathExtension:ext]];
        if (i.size.width > 16) return i;
    }
    return nil;
}
/* the part of an image with any opacity (Steam's logos come with empty margins) */
static NSImage *trimmed(NSImage *img)
{
    CGImageRef c = [img CGImageForProposedRect:NULL context:nil hints:nil]; if (!c) return img;
    size_t w = CGImageGetWidth(c), h = CGImageGetHeight(c); uint8_t *px = calloc(w * h, 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB(); CGContextRef g = CGBitmapContextCreate(px, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(g, CGRectMake(0, 0, w, h), c);
    size_t x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (size_t y = 0; y < h; y++) for (size_t x = 0; x < w; x++) if (px[(y * w + x) * 4 + 3] > 8) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    CGContextRelease(g); CGColorSpaceRelease(cs); free(px);
    if (x1 < x0) return img;
    CGImageRef t = CGImageCreateWithImageInRect(c, CGRectMake(x0, y0, x1 - x0 + 1, y1 - y0 + 1));   /* bitmap rows run top-down here */
    NSImage *r = [[NSImage alloc] initWithCGImage:t size:NSZeroSize]; CGImageRelease(t); return r;
}
@interface DSBlockTarget : NSObject      /* a button action as a block */
@property (copy) void (^fire)(NSInteger tag);
- (void)clicked:(NSButton *)b;
@end
@implementation DSBlockTarget
- (void)clicked:(NSButton *)b { if (self.fire) self.fire(b.tag); }
@end

// ---------------------------------------------------------------- updates
/* install.sh gives each app an updater (Contents/MacOS/update, passed in DS_UPDATER): it fetches the branch the app
 * follows (main, or nightly for Nightly) from GitHub into a worktree of the checkout the app was installed from and
 * rebuilds the app from it, keeping the game folder, saves and settings. The launcher asks GitHub (in the
 * background, each time it opens) whether that branch moved past the commit the app was built from. */
enum { UPD_NONE, UPD_CHECKING, UPD_CURRENT, UPD_AVAILABLE, UPD_ERROR };
static void on_main(dispatch_block_t b) { CFRunLoopPerformBlock(CFRunLoopGetMain(), kCFRunLoopCommonModes, b); CFRunLoopWakeUp(CFRunLoopGetMain()); }
static NSString *short_commit(NSString *c) { return c.length > 7 ? [c substringToIndex:7] : (c ?: @""); }
static NSString *update_check(NSString *updater)   /* the newest commit of the branch on GitHub; nil if it can't be reached */
{
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:@"/bin/bash"]; t.arguments = @[updater, @"--check"];
    NSPipe *p = [NSPipe pipe]; t.standardOutput = p; t.standardError = [NSFileHandle fileHandleWithNullDevice];
    if (![t launchAndReturnError:nil]) return nil;
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, 20 * NSEC_PER_SEC), dispatch_get_global_queue(0, 0), ^{ if (t.running) [t terminate]; });
    NSData *d = [p.fileHandleForReading readDataToEndOfFile]; [t waitUntilExit];
    NSString *out = [[[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] stringByTrimmingCharactersInSet:NSCharacterSet.whitespaceAndNewlineCharacterSet];
    return t.terminationStatus == 0 && out.length >= 40 ? [out substringToIndex:40] : nil;
}
static NSArray<NSDictionary *> *update_choice(int state, NSString *branch, NSString *built, NSString *latest)
{
    NSString *label = @"Not available", *note = @"This copy wasn't installed from the GitHub repository";
    switch (state) {
    case UPD_CHECKING: label = @"Checking..."; note = [NSString stringWithFormat:@"Asking GitHub for the newest %@", branch]; break;
    case UPD_CURRENT: label = @"Up to date"; note = [NSString stringWithFormat:@"%@ %@", branch, short_commit(built)]; break;
    case UPD_AVAILABLE: label = @"Update available"; note = [NSString stringWithFormat:@"%@ -> %@: press to install", short_commit(built), short_commit(latest)]; break;
    case UPD_ERROR: label = @"Couldn't check"; note = @"GitHub unreachable or not signed in: press to retry"; break;
    }
    return @[@{@"value": @"", @"label": label, @"note": note}];
}
/* the phase of install.sh's output, in words */
static NSString *update_phase(NSString *line, NSString *branch)
{
    if ([line hasPrefix:@"== fetching"]) return @"Downloading the update from GitHub";
    if ([line hasPrefix:[NSString stringWithFormat:@"== %@:", branch]]) return [@"Got " stringByAppendingString:[line substringFromIndex:branch.length + 4]];
    if ([line hasPrefix:@"== analysis"]) return @"Rebuilding from your game: analysing its code (1 of 4)";
    if ([line hasPrefix:@"== lifting"]) return @"Rebuilding from your game: translating to native code (2 of 4)";
    if ([line hasPrefix:@"== compiling"]) return @"Rebuilding from your game: compiling (3 of 4)";
    if ([line hasPrefix:@"== installing"]) return @"Installing the new app (4 of 4)";
    return nil;
}
/* runs the updater with a progress window (modal); returns YES when the app was rebuilt */
static BOOL update_run(NSWindow *parent, NSString *updater, NSString *branch, NSString *logPath)
{
    NSPanel *pn = [[NSPanel alloc] initWithContentRect:NSMakeRect(0, 0, 520, 170) styleMask:NSWindowStyleMaskTitled backing:NSBackingStoreBuffered defer:NO];
    pn.title = @"Updating";
    NSTextField *title = [NSTextField labelWithString:@"Updating to the newest version"]; title.font = [NSFont boldSystemFontOfSize:14]; title.frame = NSMakeRect(20, 128, 480, 22);
    NSTextField *status = [NSTextField wrappingLabelWithString:@"Starting..."]; status.frame = NSMakeRect(20, 64, 480, 56);
    NSProgressIndicator *spin = [[NSProgressIndicator alloc] initWithFrame:NSMakeRect(20, 44, 480, 12)]; spin.indeterminate = YES; [spin startAnimation:nil];
    NSTextField *time = [NSTextField labelWithString:@"This takes a few minutes. Your saves and settings are kept."]; time.font = [NSFont systemFontOfSize:11]; time.textColor = NSColor.secondaryLabelColor; time.frame = NSMakeRect(20, 14, 360, 18);
    NSButton *btn = [NSButton buttonWithTitle:@"Close" target:nil action:nil]; btn.frame = NSMakeRect(400, 8, 100, 30); btn.hidden = YES;
    for (NSView *x in @[title, status, spin, time, btn]) [pn.contentView addSubview:x];
    if (parent) [pn setFrameOrigin:NSMakePoint(NSMidX(parent.frame) - 260, NSMidY(parent.frame) - 85)]; else [pn center];
    [[NSFileManager defaultManager] createFileAtPath:logPath contents:nil attributes:nil];
    NSFileHandle *log = [NSFileHandle fileHandleForWritingAtPath:logPath];
    NSTask *t = [NSTask new]; t.executableURL = [NSURL fileURLWithPath:@"/bin/bash"]; t.arguments = @[updater];
    NSPipe *pipe = [NSPipe pipe]; t.standardOutput = pipe; t.standardError = pipe;
    NSMutableString *pending = [NSMutableString new], *tail = [NSMutableString new];
    pipe.fileHandleForReading.readabilityHandler = ^(NSFileHandle *h) {
        NSData *d = h.availableData; if (!d.length) return;
        [log writeData:d];
        NSString *chunk = [[NSString alloc] initWithData:d encoding:NSUTF8StringEncoding] ?: @"";
        @synchronized (pending) {
            [pending appendString:chunk];
            NSRange nl;
            while ((nl = [pending rangeOfString:@"\n"]).location != NSNotFound) {
                NSString *line = [pending substringToIndex:nl.location]; [pending deleteCharactersInRange:NSMakeRange(0, nl.location + 1)];
                [tail appendFormat:@"%@\n", line]; if (tail.length > 600) [tail deleteCharactersInRange:NSMakeRange(0, tail.length - 600)];
                NSString *ph = update_phase(line, branch);
                if (ph) on_main(^{ status.stringValue = ph; });
            }
        }
    };
    __block BOOL ok = NO;
    __attribute__((objc_precise_lifetime)) DSBlockTarget *bt = [DSBlockTarget new];   /* a button's target is weak: kept here */
    bt.fire = ^(NSInteger tag) { (void)tag; [NSApp stopModal]; };
    btn.target = bt; btn.action = @selector(clicked:);
    t.terminationHandler = ^(NSTask *task) {
        int code = task.terminationStatus;
        on_main(^{
            pipe.fileHandleForReading.readabilityHandler = nil; [log closeFile];
            [spin stopAnimation:nil]; spin.hidden = YES; btn.hidden = NO;
            if (code == 0) { ok = YES; title.stringValue = @"Updated"; status.stringValue = @"The new version is installed. The app restarts when you press Restart."; btn.title = @"Restart"; }
            else {
                NSString *last; @synchronized (pending) { last = [[tail componentsSeparatedByString:@"\n"] componentsJoinedByString:@" "]; }
                title.stringValue = @"The update didn't finish";
                status.stringValue = [NSString stringWithFormat:@"Nothing was changed if it stopped before installing. Details: %@  (%@)", logPath,
                                      last.length > 160 ? [last substringFromIndex:last.length - 160] : last];
                time.stringValue = @"";
            }
        });
    };
    if (![t launchAndReturnError:nil]) { [log closeFile]; return NO; }
    [pn makeKeyAndOrderFront:nil];
    [NSApp runModalForWindow:pn];
    [pn orderOut:nil];
    return ok;
}

/* Shows the launch window (modal) unless DS_NO_LAUNCHER is set; returns 0 to quit. DS_LAUNCHER_SHOT=<png> renders it
 * to an image instead (development). */
int ds_launcher_run(const char *game_dir, const char *data_dir)
{
    (void)game_dir;
    NSString *data = @(data_dir), *plistPath = [data stringByAppendingPathComponent:@"launcher.plist"];
    NSDictionary *saved = [NSDictionary dictionaryWithContentsOfFile:plistPath] ?: @{};
    DSRow *shape = [DSRow new], *fps = [DSRow new], *nova = [DSRow new], *router = [DSRow new];
    shape.title = @"Screen Shape"; shape.choices = shape_choices(); shape.index = index_of(shape.choices, saved[@"screenShape"], 0);
    fps.title = @"Frame Rate"; fps.choices = framerate_choices(); fps.index = index_of(fps.choices, saved[@"frameRate"], 0);
    nova.title = @"NovaWorld"; nova.choices = novaworld_choices(); nova.index = index_of(nova.choices, saved[@"novaWorld"], 0);
    router.title = @"Router"; router.choices = router_choices(); router.index = index_of(router.choices, saved[@"router"], 0);
    NSString *(^val)(DSRow *) = ^NSString *(DSRow *r) { return r.choices[r.index][@"value"]; };
    const char *shot = getenv("DS_LAUNCHER_SHOT");
    if (getenv("DS_NO_LAUNCHER") && !shot) {
        if (saved.count) apply(val(shape), val(fps), val(nova), val(router));
        return 1;
    }

    NSString *updater = getenv("DS_UPDATER") ? @(getenv("DS_UPDATER")) : nil, *branch = getenv("DS_UPDATE_BRANCH") ? @(getenv("DS_UPDATE_BRANCH")) : @"main";
    NSString *built = getenv("DS_BUILT_COMMIT") ? @(getenv("DS_BUILT_COMMIT")) : @"";
    if (![[NSFileManager defaultManager] isExecutableFileAtPath:updater ?: @""]) updater = nil;
    DSRow *upd = [DSRow new]; upd.title = @"Updates";
    __block int ustate = updater ? UPD_CHECKING : UPD_NONE; __block NSString *latest = nil;
    upd.choices = update_choice(ustate, branch, built, nil);
    BOOL shotMode = shot != NULL;
    NSArray<DSRow *> *rows = @[shape, fps, nova, router, upd];
    DSLaunchView *v = [[DSLaunchView alloc] initWithFrame:NSMakeRect(0, 0, 780, 720)];
    v.rows = rows; v.hover = v.pressed = HIT_NONE;
    v.note = @"Renderer (Colors: Hardware, 16 Million, 256) and resolution: in the game's own Options. Join by address under Internet.";
    __weak DSLaunchView *wv = v; __weak DSRow *wu = upd;
    void (^check)(void) = ^{
        if (!updater) return;
        ustate = UPD_CHECKING; wu.choices = update_choice(ustate, branch, built, nil); wv.needsDisplay = YES;
        void (^done)(NSString *) = ^(NSString *l) {
            latest = l; ustate = !l ? UPD_ERROR : [l isEqualToString:built] ? UPD_CURRENT : UPD_AVAILABLE;
            wu.choices = update_choice(ustate, branch, built, l); wv.needsDisplay = YES;
        };
        if (shotMode) { done(update_check(updater)); return; }               /* development: the result in the image */
        dispatch_async(dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{ NSString *l = update_check(updater); on_main(^{ done(l); }); });
    };
    check();
    upd.activate = ^{
        if (ustate == UPD_CHECKING) return;
        if (ustate != UPD_AVAILABLE) { check(); return; }
        NSAlert *a = [NSAlert new];
        a.messageText = [NSString stringWithFormat:@"Update to the newest %@?", [branch isEqualToString:@"nightly"] ? @"Nightly" : @"version"];
        a.informativeText = [NSString stringWithFormat:@"The %@ branch (%@) is downloaded from GitHub and the app is rebuilt from your own copy of the game, "
                             "which takes a few minutes. Your game folder, soldiers and settings are kept. The app restarts when it is done.", branch, short_commit(latest)];
        [a addButtonWithTitle:@"Update"]; [a addButtonWithTitle:@"Cancel"];
        if ([a runModal] != NSAlertFirstButtonReturn) return;
        NSString *logPath = [data stringByAppendingPathComponent:@"update.log"];
        if (update_run(wv.window, updater, branch, logPath)) {   /* restart: the new app, then this one ends */
            NSString *app = [[[updater stringByDeletingLastPathComponent] stringByDeletingLastPathComponent] stringByDeletingLastPathComponent];
            NSTask *o = [NSTask new]; o.executableURL = [NSURL fileURLWithPath:@"/usr/bin/open"]; o.arguments = @[@"-n", app];
            [o launchAndReturnError:nil]; [o waitUntilExit];
            exit(0);
        }
        check();
    };
    v.backdrop = art(data, @"backdrop") ?: art(data, @"menu");
    NSImage *logo = art(data, @"logo"); v.logo = logo ? trimmed(logo) : nil;
    if (shot) {   /* development: the window as an image */
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds]; [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(shot) atomically:YES];
        return 0;
    }

    NSWindow *w = [[NSWindow alloc] initWithContentRect:v.frame styleMask:NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskFullSizeContentView
                                                backing:NSBackingStoreBuffered defer:NO];
    w.releasedWhenClosed = NO;   /* ARC owns it: AppKit's default would release it again when the close button closes it (a crash on exit) */
    w.titlebarAppearsTransparent = YES; w.titleVisibility = NSWindowTitleHidden; w.movableByWindowBackground = YES; w.title = @DS2_TITLE;
    w.backgroundColor = NSColor.blackColor; w.contentView = v; [w center]; [w makeFirstResponder:v];
    __block int result = 0;
    v.onPlay = ^{ result = 1; [NSApp stopModal]; };
    v.onQuit = ^{ result = 0; [NSApp stopModal]; };
    [[NSNotificationCenter defaultCenter] addObserverForName:NSWindowWillCloseNotification object:w queue:nil usingBlock:^(NSNotification *n) { (void)n; [NSApp stopModal]; }];
    [NSApp activateIgnoringOtherApps:YES];
    [w makeKeyAndOrderFront:nil];
    [NSApp runModalForWindow:w];
    [w orderOut:nil];
    if (!result) return 0;
    [@{@"screenShape": val(shape), @"frameRate": val(fps), @"novaWorld": val(nova), @"router": val(router)} writeToFile:plistPath atomically:YES];
    apply(val(shape), val(fps), val(nova), val(router));
    return 1;
}
