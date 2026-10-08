// The launch window, laid out like the Dungeon Siege ports': a banner (the game's logo over its own textured background
// and one of its screenshots), the Controls row, and a button for each group of settings (Display, Multiplayer, Key Guide,
// Updates) that opens it as a panel. Choices are remembered in <data>/launcher.plist, then Play.
// The art is fetched at install time (tools/df_art.py: the logo, background and a screenshot from the game's Steam store
// page, or failing that the game's own splash and menu screens) into <data>/art, and a copy in the app's Resources/art.
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
/* A group's button opens it as a panel over the window, with Back where Quit was; Key Guide and Updates run their own. */
@interface DSSection : NSObject
@property NSString *title; @property NSArray<DSRow *> *rows;
@property (copy) void (^activate)(void); @property (copy) NSString *(^summary)(void);
@end
@implementation DSSection @end

@interface DSLaunchView : NSView
@property NSArray<DSRow *> *top; @property NSArray<DSSection *> *sections; @property NSInteger open;   /* the open group, -1 none */
@property NSInteger focus; @property NSInteger hover; @property NSInteger pressed;
@property NSImage *backdrop, *logo, *shot; @property NSString *note;
@property (copy) void (^onPlay)(void); @property (copy) void (^onQuit)(void); @property (copy) void (^onChange)(void);
@end

enum { HIT_NONE = -1, HIT_PLAY = 100, HIT_QUIT = 101, HIT_BACK = 102, HIT_SECTION = 200 };   /* rows: 10*row + 0 (left arrow) / 1 (value) / 2 (right arrow) */
enum { BANNER_H = 252, TOP_Y = 270 };

@implementation DSLaunchView
- (BOOL)isFlipped { return YES; }
- (BOOL)acceptsFirstResponder { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
- (NSArray<DSRow *> *)rows { return self.open >= 0 ? self.sections[self.open].rows : self.top; }
- (NSInteger)items { return (NSInteger)self.rows.count + (self.open >= 0 ? 0 : (NSInteger)self.sections.count); }   /* what the keyboard moves between */
- (NSRect)flyRect   /* an open group's panel: above Back and Play */
{
    NSRect b = self.bounds; CGFloat h = b.size.height - 268, need = 80 + 74 * (CGFloat)(self.open >= 0 ? self.sections[self.open].rows.count : 0);
    CGFloat grow = MIN(MAX(need - h, 0), 60);
    return NSMakeRect(40, 168 - grow, b.size.width - 80, h + grow);
}
- (NSRect)rowRect:(NSInteger)k { CGFloat y = self.open >= 0 ? self.flyRect.origin.y + 70 : TOP_Y + 14; return NSMakeRect(70, y + 74 * k, self.bounds.size.width - 140, 64); }
- (NSRect)arrow:(NSInteger)k right:(BOOL)right { NSRect r = [self rowRect:k]; return NSMakeRect(right ? NSMaxX(r) - 34 : NSMaxX(r) - 330, r.origin.y + 6, 30, 30); }
- (NSRect)valueRect:(NSInteger)k { NSRect r = [self rowRect:k]; return NSMakeRect(NSMaxX(r) - 300, r.origin.y + 7, 262, 30); }
- (NSRect)sectionRect:(NSInteger)i
{
    NSInteger n = (NSInteger)self.sections.count, per = 2, line = i / per, inLine = MIN(per, n - line * per), col = i % per;
    CGFloat w = 290, g = 28, x0 = (self.bounds.size.width - (inLine * w + (inLine - 1) * g)) / 2;
    return NSMakeRect(x0 + col * (w + g), TOP_Y + 74 * self.top.count + 44 + line * 100, w, 54);
}
- (NSRect)playRect { NSRect b = self.bounds; return NSMakeRect(NSMidX(b) + 12, b.size.height - 80, 220, 52); }
- (NSRect)quitRect { NSRect b = self.bounds; return NSMakeRect(NSMidX(b) - 232, b.size.height - 80, 220, 52); }
- (NSInteger)hit:(NSPoint)p
{
    if (NSPointInRect(p, self.playRect)) return HIT_PLAY;
    if (NSPointInRect(p, self.quitRect)) return self.open >= 0 ? HIT_BACK : HIT_QUIT;
    if (self.open < 0) for (NSInteger i = 0; i < (NSInteger)self.sections.count; i++) if (NSPointInRect(p, [self sectionRect:i])) return HIT_SECTION + i;
    for (NSInteger k = 0; k < (NSInteger)self.rows.count; k++) {
        if (NSPointInRect(p, [self arrow:k right:NO])) return 10 * k;
        if (NSPointInRect(p, [self arrow:k right:YES])) return 10 * k + 2;
        if (NSPointInRect(p, [self valueRect:k])) return 10 * k + 1;
    }
    if (self.open >= 0 && !NSPointInRect(p, self.flyRect)) return HIT_BACK;   /* a click outside the panel closes it */
    return HIT_NONE;
}
- (void)openSection:(NSInteger)i
{
    DSSection *s = self.sections[i]; self.focus = (NSInteger)self.top.count + i;
    if (s.activate) { s.activate(); self.needsDisplay = YES; return; }
    self.open = i; self.focus = 0; self.hover = HIT_NONE; self.needsDisplay = YES;
}
- (void)closeSection { if (self.open < 0) return; self.focus = (NSInteger)self.top.count + self.open; self.open = -1; self.hover = HIT_NONE; self.needsDisplay = YES; }
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
    if (h == HIT_BACK) { [self closeSection]; return; }
    if (h >= HIT_SECTION) { [self openSection:h - HIT_SECTION]; return; }
    NSInteger k = h / 10; self.focus = k;
    if (self.rows[k].activate) { self.rows[k].activate(); self.needsDisplay = YES; return; }
    [self step:k by:(h % 10 == 0) ? -1 : 1];
}
- (void)step:(NSInteger)k by:(NSInteger)dir
{
    if (k >= (NSInteger)self.rows.count) { NSInteger n = [self items]; self.focus = (self.focus + dir + n) % n; self.needsDisplay = YES; return; }   /* the group buttons */
    DSRow *r = self.rows[k]; NSInteger n = (NSInteger)r.choices.count;
    if (r.activate) { r.activate(); self.needsDisplay = YES; return; }
    r.index = (r.index + dir + n) % n; self.needsDisplay = YES;
    if (self.onChange) self.onChange();
}
- (void)keyDown:(NSEvent *)e
{
    NSInteger n = [self items], rc = (NSInteger)self.rows.count;
    switch (e.keyCode) {
    case 0x24: case 0x4c:                                                /* Return, Enter */
        if (self.focus >= rc) { [self openSection:self.focus - rc]; return; }
        if (self.rows[self.focus].activate) { self.rows[self.focus].activate(); self.needsDisplay = YES; return; }
        if (self.open >= 0) { [self closeSection]; return; }
        if (self.onPlay) self.onPlay(); return;
    case 0x35: if (self.open >= 0) { [self closeSection]; return; }      /* Escape */
               if (self.onQuit) self.onQuit(); return;
    case 0x7e: self.focus = (self.focus + n - 1) % n; self.needsDisplay = YES; return;
    case 0x7d: self.focus = (self.focus + 1) % n; self.needsDisplay = YES; return;
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
static void fill_over(NSRect r) { NSRectFillUsingOperation(r, NSCompositingOperationSourceOver); }
- (void)drawButton:(NSRect)r title:(NSString *)title code:(NSInteger)code primary:(BOOL)primary
{
    BOOL hot = self.hover == code, down = self.pressed == code;
    NSColor *fill = down ? [accent() colorWithAlphaComponent:0.45] : hot ? [accent() colorWithAlphaComponent:0.28] : primary ? [accent_dim() colorWithAlphaComponent:0.35] : [NSColor colorWithWhite:0 alpha:0.6];
    [fill setFill]; fill_over(r);
    [(primary || hot ? accent() : accent_dim()) setStroke]; NSBezierPath *b = [NSBezierPath bezierPathWithRect:NSInsetRect(r, 0.5, 0.5)]; b.lineWidth = 1; [b stroke];
    brackets(NSInsetRect(r, -4, -4), 10, 2, primary || hot ? accent() : accent_dim());
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; sh.shadowOffset = NSMakeSize(0, -1.5); sh.shadowBlurRadius = 2;
    NSFont *f = din(primary ? 30 : 26); NSDictionary *at = @{NSFontAttributeName: f, NSForegroundColorAttributeName: hot || primary ? NSColor.whiteColor : text_c(), NSShadowAttributeName: sh, NSKernAttributeName: @4};
    NSSize sz = [title sizeWithAttributes:at];   /* the capitals centred on the button */
    [title drawAtPoint:NSMakePoint(NSMidX(r) - sz.width / 2, round(NSMidY(r) + f.capHeight / 2 - f.ascender)) withAttributes:at];
}
- (void)drawArrow:(NSRect)r right:(BOOL)right lit:(BOOL)lit
{
    NSBezierPath *p = [NSBezierPath bezierPath]; CGFloat x0 = r.origin.x + 9, x1 = NSMaxX(r) - 9, y0 = r.origin.y + 7, y1 = NSMaxY(r) - 7, ym = NSMidY(r);
    if (right) { [p moveToPoint:NSMakePoint(x0, y0)]; [p lineToPoint:NSMakePoint(x1, ym)]; [p lineToPoint:NSMakePoint(x0, y1)]; }
    else { [p moveToPoint:NSMakePoint(x1, y0)]; [p lineToPoint:NSMakePoint(x0, ym)]; [p lineToPoint:NSMakePoint(x1, y1)]; }
    [p closePath]; [(lit ? NSColor.whiteColor : accent()) setFill]; [p fill];
}
- (void)drawRows:(NSShadow *)sh
{
    for (NSInteger k = 0; k < (NSInteger)self.rows.count; k++) {
        DSRow *row = self.rows[k]; NSRect r = [self rowRect:k]; NSDictionary *ch = row.choices[row.index];
        if (k == self.focus) { [[accent() colorWithAlphaComponent:0.12] setFill]; fill_over(NSInsetRect(r, -10, -2)); [accent() setFill]; NSRectFill(NSMakeRect(r.origin.x - 10, r.origin.y - 2, 3, r.size.height + 4)); }
        draw_text(row.title.uppercaseString, din(24), accent(), NSMakeRect(r.origin.x + 4, r.origin.y + 10, 300, 30), NSTextAlignmentLeft, sh, 2);
        [self drawArrow:[self arrow:k right:NO] right:NO lit:self.hover == 10 * k];
        [self drawArrow:[self arrow:k right:YES] right:YES lit:self.hover == 10 * k + 2];
        draw_text(ch[@"label"], body(20, YES), self.hover == 10 * k + 1 ? NSColor.whiteColor : text_c(), [self valueRect:k], NSTextAlignmentCenter, sh, 0.5);
        draw_text(ch[@"note"] ?: @"", body(12.5, NO), dim(), NSMakeRect(NSMaxX(r) - 340, r.origin.y + 40, 350, 18), NSTextAlignmentCenter, nil, 0);
        if (k + 1 < (NSInteger)self.rows.count) { [[accent_dim() colorWithAlphaComponent:0.35] setFill]; fill_over(NSMakeRect(r.origin.x, NSMaxY(r) + 5, r.size.width, 1)); }
    }
}
- (void)drawPanel:(NSRect)panel
{
    [[NSColor colorWithWhite:0 alpha:0.62] setFill]; fill_over(panel);
    [[accent_dim() colorWithAlphaComponent:0.8] setStroke]; NSBezierPath *bp = [NSBezierPath bezierPathWithRect:NSInsetRect(panel, 0.5, 0.5)]; bp.lineWidth = 1; [bp stroke];
    brackets(NSInsetRect(panel, -5, -5), 18, 2.5, accent());
}
/* an image filling a rectangle, its aspect kept (cropped), at the given share of its own centre */
static void draw_cover(NSImage *img, NSRect r, CGFloat fraction)
{
    NSSize is = img.size; CGFloat sc = MAX(r.size.width / is.width, r.size.height / is.height), w = r.size.width / sc, h = r.size.height / sc;
    [img drawInRect:r fromRect:NSMakeRect((is.width - w) / 2, (is.height - h) / 2, w, h) operation:NSCompositingOperationSourceOver fraction:fraction
     respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
}
- (void)drawBanner:(NSRect)br shadow:(NSShadow *)sh
{
    [NSColor.blackColor setFill]; NSRectFill(br);
    if (self.backdrop) draw_cover(self.backdrop, br, 1);
    if (self.shot) {   /* the screenshot on the right, fading into the background towards the logo */
        NSRect sr = NSMakeRect(br.size.width * 0.38, 0, br.size.width * 0.62, br.size.height);
        NSImage *layer = [NSImage imageWithSize:sr.size flipped:YES drawingHandler:^BOOL(NSRect dst) {
            draw_cover(self.shot, dst, 1);
            NSGradient *fade = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0 alpha:1], 0.0, [NSColor colorWithWhite:0 alpha:0.6], 0.25, [NSColor colorWithWhite:0 alpha:0], 0.55, nil];
            [[NSGraphicsContext currentContext] setCompositingOperation:NSCompositingOperationDestinationOut];
            [fade drawInRect:dst angle:0];
            return YES;
        }];
        [layer drawInRect:sr fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:nil];
    }
    NSGradient *shade = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0 alpha:0.45], 0.0, [NSColor colorWithWhite:0 alpha:0.05], 0.5, [NSColor colorWithWhite:0 alpha:0.15], 1.0, nil];
    [shade drawInRect:br angle:0];
    /* the logo on the left, the subtitle under it */
    NSRect lr = NSMakeRect(28, 16, 300, 186);
    if (self.logo) {
        CGFloat h = lr.size.height, w = h * self.logo.size.width / self.logo.size.height; if (w > lr.size.width) { w = lr.size.width; h = w * self.logo.size.height / self.logo.size.width; }
        NSShadow *glow = [NSShadow new]; glow.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; glow.shadowOffset = NSZeroSize; glow.shadowBlurRadius = 14;
        [NSGraphicsContext saveGraphicsState]; [glow set];
        [self.logo drawInRect:NSMakeRect(NSMidX(lr) - w / 2, lr.origin.y + (lr.size.height - h) / 2, w, h) fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1 respectFlipped:YES hints:@{NSImageHintInterpolation: @(NSImageInterpolationHigh)}];
        [NSGraphicsContext restoreGraphicsState];
    }
    else draw_text(@DS2_TITLE.uppercaseString, din(54), NSColor.whiteColor, NSMakeRect(lr.origin.x - 20, 70, lr.size.width + 40, 64), NSTextAlignmentCenter, sh, 6);
    draw_text(@"NATIVE EDITION FOR macOS", din(16), accent(), NSMakeRect(lr.origin.x - 20, NSMaxY(lr) + 8, lr.size.width + 40, 22), NSTextAlignmentCenter, sh, 4);
    NSGradient *fade = [[NSGradient alloc] initWithStartingColor:[NSColor colorWithWhite:0 alpha:0] endingColor:[NSColor colorWithWhite:0 alpha:0.85]];
    [fade drawInRect:NSMakeRect(0, BANNER_H - 36, br.size.width, 36) angle:90];
    [accent_dim() setFill]; NSRectFill(NSMakeRect(0, BANNER_H, br.size.width, 1.5));
}
- (void)drawRect:(NSRect)dirty
{
    NSRect b = self.bounds;
    NSShadow *sh = [NSShadow new]; sh.shadowColor = [NSColor colorWithWhite:0 alpha:0.9]; sh.shadowOffset = NSMakeSize(0, -1); sh.shadowBlurRadius = 3;
    [[NSColor colorWithWhite:0.04 alpha:1] setFill]; NSRectFill(b);
    if (self.backdrop) draw_cover(self.backdrop, b, 0.55);              /* the game's texture, darker below the banner */
    NSGradient *vig = [[NSGradient alloc] initWithColorsAndLocations:[NSColor colorWithWhite:0 alpha:0.25], 0.0, [NSColor colorWithWhite:0 alpha:0.75], 1.0, nil];
    [vig drawInRect:b relativeCenterPosition:NSMakePoint(0, -0.1)];
    [self drawBanner:NSMakeRect(0, 0, b.size.width, BANNER_H) shadow:sh];
    /* the top level: the Controls row, then a button for each group with a summary under it */
    NSInteger fly = self.open, foc = self.focus, hov = self.hover; self.open = -1;
    if (fly >= 0) self.focus = self.hover = HIT_NONE;           /* under the panel: nothing lit */
    [self drawPanel:NSMakeRect(54, TOP_Y, b.size.width - 108, 74 * self.top.count + 14)];
    [self drawRows:sh];
    for (NSInteger i = 0; i < (NSInteger)self.sections.count; i++) {
        DSSection *s = self.sections[i]; NSRect r = [self sectionRect:i]; NSInteger code = HIT_SECTION + i;
        if (fly < 0 && self.focus == (NSInteger)self.top.count + i) { [[accent() colorWithAlphaComponent:0.22] setFill]; fill_over(NSInsetRect(r, -8, -7)); }
        [self drawButton:r title:s.title.uppercaseString code:code primary:NO];
        NSString *sum = s.summary ? s.summary() : nil;
        if (!sum) { NSMutableArray *l = [NSMutableArray array]; for (DSRow *row in s.rows) [l addObject:row.choices[row.index][@"label"]]; sum = [l componentsJoinedByString:@" · "]; }
        draw_text(sum, body(12.5, NO), dim(), NSMakeRect(r.origin.x - 10, NSMaxY(r) + 8, r.size.width + 20, 32), NSTextAlignmentCenter, sh, 0);
    }
    if (self.note.length) draw_text(self.note, body(12.5, NO), dim(), NSMakeRect(54, [self sectionRect:(NSInteger)self.sections.count - 1].origin.y + 98, b.size.width - 108, 34), NSTextAlignmentCenter, sh, 0);
    self.open = fly; self.focus = foc; self.hover = hov;
    /* an open group: a panel over the window */
    if (self.open >= 0) {
        [[NSColor colorWithWhite:0 alpha:0.6] setFill]; fill_over(b);
        NSRect f = self.flyRect;
        [[NSColor colorWithWhite:0.05 alpha:0.96] setFill]; fill_over(f);
        [accent_dim() setStroke]; NSBezierPath *fp = [NSBezierPath bezierPathWithRect:NSInsetRect(f, 0.5, 0.5)]; fp.lineWidth = 1; [fp stroke];
        brackets(NSInsetRect(f, -5, -5), 22, 3, accent());
        draw_text(self.sections[self.open].title.uppercaseString, din(30), accent(), NSMakeRect(f.origin.x, f.origin.y + 20, f.size.width, 36), NSTextAlignmentCenter, sh, 5);
        [[accent_dim() colorWithAlphaComponent:0.6] setFill]; fill_over(NSMakeRect(f.origin.x + 30, f.origin.y + 60, f.size.width - 60, 1));
        [self drawRows:sh];
    }
    [self drawButton:self.quitRect title:self.open >= 0 ? @"BACK" : @"QUIT" code:self.open >= 0 ? HIT_BACK : HIT_QUIT primary:NO];
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
 * df_net.c, portmap.c; DF_WHEEL_CYCLE by the host, main.m). An environment already set by the player (a terminal) wins. */
static void apply(NSString *shape, NSString *fps, NSString *nova, NSString *router, NSString *controls)
{
    if (!getenv("DF_RES") && [shape isEqualToString:@"off"]) setenv("DF_RES", "off", 1);
    if (!getenv("DSR_FPSCAP") && !getenv("DF_HIGH_FPS")) {
        if ([fps isEqualToString:@"original"]) setenv("DF_HIGH_FPS", "0", 1);
        else if ([fps isEqualToString:@"60"]) setenv("DSR_FPSCAP", "60", 1);
    }
    if (!getenv("DF_NO_P2P") && [nova isEqualToString:@"off"]) setenv("DF_NO_P2P", "1", 1);
    if (!getenv("DS_NO_PORTMAP") && [router isEqualToString:@"off"]) setenv("DS_NO_PORTMAP", "1", 1);
    if (!getenv("DF_WHEEL_CYCLE") && [controls isEqualToString:@"modern"]) setenv("DF_WHEEL_CYCLE", "1", 1);   /* the wheel cycles weapons */
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
/* the part of an image with any opacity (Steam's logos come with empty margins), its bottom `cut` share left off */
static NSImage *trimmed(NSImage *img, CGFloat cut)
{
    CGImageRef c = [img CGImageForProposedRect:NULL context:nil hints:nil]; if (!c) return img;
    size_t w = CGImageGetWidth(c), h = CGImageGetHeight(c); uint8_t *px = calloc(w * h, 4);
    CGColorSpaceRef cs = CGColorSpaceCreateDeviceRGB(); CGContextRef g = CGBitmapContextCreate(px, w, h, 8, w * 4, cs, (CGBitmapInfo)kCGImageAlphaPremultipliedLast);
    CGContextDrawImage(g, CGRectMake(0, 0, w, h), c);
    size_t x0 = w, y0 = h, x1 = 0, y1 = 0;
    for (size_t y = 0; y < h; y++) for (size_t x = 0; x < w; x++) if (px[(y * w + x) * 4 + 3] > 8) { if (x < x0) x0 = x; if (x > x1) x1 = x; if (y < y0) y0 = y; if (y > y1) y1 = y; }
    CGContextRelease(g); CGColorSpaceRelease(cs); free(px);
    if (x1 < x0) return img;
    CGImageRef t = CGImageCreateWithImageInRect(c, CGRectMake(x0, y0, x1 - x0 + 1, (y1 - y0 + 1) * (1 - cut)));   /* bitmap rows run top-down here */
    NSImage *r = [[NSImage alloc] initWithCGImage:t size:NSZeroSize]; CGImageRelease(t);
    return cut > 0 ? trimmed(r, 0) : r;
}

// ---------------------------------------------------------------- controls: the games' own key maps
/* Each soldier carries a key map: 12-byte entries {Windows virtual key, mouse button (1-3, 0 none), 0} in the order of
 * the game's Remap Keyboard screen. Delta Force: 46 entries at 0x6a0 of each of player01..09.ply. Delta Force 2: 59
 * entries at 0x2e0 of each of the five 0x6e8-byte soldier records after df2plyrs.sav's 8-byte header ("FPBC"), and the
 * same 59 at the start of Df2.dfk, the map a new soldier gets. Classic is the map the games shipped with; Modern moves
 * to WASD and a modern shooter's keys, and every action the move displaced gets a key of its own (Key Guide lists them).
 * Only the key of an entry is written: the mouse buttons (fire 1, scope 2, prone 3) stay. The files are written into the
 * data folder's copy of the game folder (the runtime reads it first), the originals backed up once to <data>/backup.
 * The game's own Remap screen still works: a map that is neither preset shows as Custom and is left alone. */
#ifdef DF2_EDITION
enum { KEYS_N = 59 };
static const uint8_t classic_keys[KEYS_N] = {
    0x25,0x27,0x26,0x28,0x22,0x23,0x2E,0x20,0x11,0x53,0x10,0x08,0x42,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xDD,0xDB,0x54,0x59,0x57,0x41,0x56,
    0x44,0x4D,0x50,0x4C,0xBA,0xBE,0x09,0x4F,0x52,0x46,0x47,0x43,0x48,0x51,0x24,0x58,0x5A,0x4E,0x49,0x6F,0x6A,0x6D,0x6B,0x61,0x63,0x60,0x6E,0xDC,0x45};
static const uint8_t modern_keys[KEYS_N] = {
    0x41,0x44,0x57,0x53,0x5A,0x43,0x58,0x20,0x11,0x51,0x10,0x08,0x42,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x47,0x30,0xDD,0xDB,0x54,0x59,0xBC,0x56,0x2E,
    0x48,0x52,0x50,0x4C,0xBA,0xBE,0x4A,0x4F,0x23,0x46,0x09,0x4D,0xC0,0x39,0x24,0x21,0x22,0x4E,0x49,0x6F,0x6A,0x6D,0x6B,0x61,0x63,0x60,0x6E,0xDC,0x45};
#else
enum { KEYS_N = 46 };
static const uint8_t classic_keys[KEYS_N] = {
    0x25,0x27,0x26,0x28,0x22,0x23,0x2E,0x20,0x11,0x53,0x10,0x08,0x42,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xBB,0xBD,0x54,0x59,0x57,0x41,0x56,
    0x44,0x4D,0x68,0x62,0x66,0x64,0x09,0x4F,0x52,0x46,0x47,0x43,0x48,0x51,0x45,0x24};
static const uint8_t modern_keys[KEYS_N] = {
    0x41,0x44,0x57,0x53,0x5A,0x43,0x58,0x20,0x11,0x51,0x10,0x08,0x42,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x47,0x30,0xBB,0xBD,0x54,0x59,0xBC,0x56,0x2E,
    0x48,0x52,0x68,0x62,0x66,0x64,0x4A,0x4F,0x23,0x46,0x09,0x4D,0xC0,0x39,0x45,0x24};
#endif
/* the files holding key maps, each with its maps: (name, map offsets, the matching soldier-name offsets, -1 for none) */
static NSArray<NSArray *> *keymap_files(void)
{
#ifdef DF2_EDITION
    NSMutableArray *maps = [NSMutableArray array], *names = [NSMutableArray array];
    for (int r = 0; r < 5; r++) { [maps addObject:@(8 + r * 0x6e8 + 0x2e0)]; [names addObject:@(8 + r * 0x6e8 + 4)]; }
    return @[@[@"df2plyrs.sav", maps, names], @[@"Df2.dfk", @[@0], @[@-1]]];
#else
    NSMutableArray *a = [NSMutableArray array];
    for (int k = 1; k <= 9; k++) [a addObject:@[[NSString stringWithFormat:@"player%02d.ply", k], @[@0x6a0], @[@4]]];
    return a;
#endif
}
/* the file the game reads: the data folder's copy if there is one, else the game folder's */
static NSString *keymap_source(NSString *game, NSString *data, NSString *name)
{
    NSString *ov = [[data stringByAppendingPathComponent:@"game"] stringByAppendingPathComponent:name];
    if ([NSFileManager.defaultManager fileExistsAtPath:ov]) return ov;
    for (NSString *f in [NSFileManager.defaultManager contentsOfDirectoryAtPath:game error:nil])   /* the game folder's spelling */
        if ([f caseInsensitiveCompare:name] == NSOrderedSame) return [game stringByAppendingPathComponent:f];
    return nil;
}
static int keymap_kind(const uint8_t *p)    /* 1 Classic, 2 Modern, 0 something else */
{
    int c = 1, m = 1;
    for (int k = 0; k < KEYS_N; k++) { uint32_t v; memcpy(&v, p + 12 * k, 4); if (v != classic_keys[k]) c = 0; if (v != modern_keys[k]) m = 0; }
    return c ? 1 : m ? 2 : 0;
}
/* a soldier someone made: a name, and not the empty slot's "New Operative" (Delta Force 2 leaves an empty slot blank) */
static BOOL soldier_named(NSData *d, NSInteger at)
{
    if (at < 0 || at + 16 > (NSInteger)d.length) return NO;
    const char *n = (const char *)d.bytes + at; char name[17] = {0}; memcpy(name, n, 16);
    return name[0] && strcasecmp(name, "New Operative");
}
/* the named soldiers' maps now: "classic", "modern", "custom" (any of them changed in the game), or nil when they are
 * a mix of the two presets (or there are none) */
static NSString *controls_state(NSString *game, NSString *data)
{
    int c = 0, m = 0, other = 0;
    for (NSArray *f in keymap_files()) {
        NSString *src = keymap_source(game, data, f[0]); NSData *d = src ? [NSData dataWithContentsOfFile:src] : nil; if (!d) continue;
        NSArray *maps = f[1], *names = f[2];
        for (NSUInteger i = 0; i < maps.count; i++) {
            NSUInteger o = [maps[i] unsignedIntegerValue];
            if (o + 12 * KEYS_N > d.length || !soldier_named(d, [names[i] integerValue])) continue;
            int k = keymap_kind((const uint8_t *)d.bytes + o); if (k == 1) c++; else if (k == 2) m++; else other++;
        }
    }
    return other ? @"custom" : c && !m ? @"classic" : m && !c ? @"modern" : nil;
}
/* writes a preset into the maps (in the data folder's copy): every map when `all` (the player picked the layout just
 * now), else the empty slots' and the ones still on a preset (a soldier made since gets the layout; one changed in the
 * game's Remap screen keeps its keys). Only changed files are written; returns NO if one couldn't be. */
static BOOL controls_apply(NSString *game, NSString *data, BOOL modern, BOOL all)
{
    NSFileManager *fm = NSFileManager.defaultManager; NSString *ovd = [data stringByAppendingPathComponent:@"game"], *bk = [data stringByAppendingPathComponent:@"backup/controls"];
    BOOL first = ![fm fileExistsAtPath:bk], ok = YES;
    [fm createDirectoryAtPath:ovd withIntermediateDirectories:YES attributes:nil error:nil];
    if (first) [fm createDirectoryAtPath:bk withIntermediateDirectories:YES attributes:nil error:nil];
    const uint8_t *keys = modern ? modern_keys : classic_keys;
    for (NSArray *f in keymap_files()) {
        NSString *src = keymap_source(game, data, f[0]); NSMutableData *d = src ? [NSMutableData dataWithContentsOfFile:src] : nil; if (!d) continue;
        NSData *was = [d copy]; NSArray *maps = f[1], *names = f[2];
        for (NSUInteger i = 0; i < maps.count; i++) {
            NSUInteger o = [maps[i] unsignedIntegerValue]; if (o + 12 * KEYS_N > d.length) continue;
            uint8_t *p = (uint8_t *)d.mutableBytes + o;
            if (!all && soldier_named(d, [names[i] integerValue]) && !keymap_kind(p)) continue;   /* the player's own keys */
            for (int k = 0; k < KEYS_N; k++) { uint32_t v = keys[k]; memcpy(p + 12 * k, &v, 4); }
        }
        if ([d isEqualToData:was]) continue;
        if (first) [was writeToFile:[bk stringByAppendingPathComponent:f[0]] atomically:YES];   /* the maps as they were, once */
        if (![d writeToFile:[ovd stringByAppendingPathComponent:f[0]] atomically:YES]) ok = NO;
    }
    return ok;
}
static NSArray<NSDictionary *> *controls_choices(BOOL custom)
{
    NSMutableArray *a = [NSMutableArray arrayWithObjects:
        @{@"value": @"classic", @"label": @"Classic", @"note": @"The game's own keys: arrows to move, mouse to look"},
        @{@"value": @"modern", @"label": @"Modern", @"note": @"WASD, Space, C, Z, R, G, mouse wheel: see Key Guide"}, nil];
    if (custom) [a addObject:@{@"value": @"custom", @"label": @"Custom", @"note": @"Your own, from the game's Remap screen: kept as it is"}];
    return a;
}
/* the Key Guide: the chosen layout's keys */
static NSString *key_guide(BOOL modern)
{
    if (!modern) return
        @"Move: arrow keys (Shift walks)   Look: mouse\n"
         "Fire: left button or Ctrl   Scope: right button or S\n"
         "Jump: Space   Stand / Crouch / Prone: fn-Delete / fn-Right / fn-Down (Delete / End / Page Down)\n"
         "Weapons: 1–0, Backspace or middle button cycles\n"
         "Change magazine: M   Binoculars: B   Drop: D\n"
         "Map: C   Goals: G   Orders: O   Waypoint: W   Audio command: A\n"
         "Talk: T   Team talk: Y\n\n"
         "As in the game's manual. Change any key in the game's Settings › Keyboard Remap.";
    return
        @"Move: W A S D (Shift walks)   Look: mouse\n"
         "Fire: left button (or Ctrl)   Scope: right button (or Q)\n"
         "Jump: Space   Crouch: C   Prone: Z   Stand: X\n"
         "Reload (change magazine): R   Grenade: G\n"
         "Weapons: 1–8, mouse wheel or Backspace cycles   Laser designator: 0\n"
         "Binoculars: B   Drop item: H\n"
         "Objectives: Tab   Map: M   Orders: O   Recent messages: fn-Right (End)\n"
         "Next waypoint: ,   Audio command: V   Next flag: 9   Team tags: F\n"
#ifdef DF2_EDITION
         "Night vision: N   Scope zoom: E   Scope zero: fn-Up / fn-Down\n"
         "Forward observer: J   Turbo: `   Video resolution: fn-Delete\n"
#else
         "Previous waypoint: E   Forward observer: J   Turbo: `   Video resolution: fn-Delete\n"
#endif
         "Talk: T   Team talk: Y\n\n"
         "Written into your soldiers' own key maps: the game's Settings › Keyboard Remap shows them, and can change any.";
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
 * to an image instead (development; DS_LAUNCHER_OPEN=<group> renders that group's panel open). */
int ds_launcher_run(const char *game_dir, const char *data_dir)
{
    NSString *game = @(game_dir), *data = @(data_dir), *plistPath = [data stringByAppendingPathComponent:@"launcher.plist"];
    NSDictionary *saved = [NSDictionary dictionaryWithContentsOfFile:plistPath] ?: @{};
    DSRow *shape = [DSRow new], *fps = [DSRow new], *nova = [DSRow new], *router = [DSRow new], *controls = [DSRow new];
    shape.title = @"Screen Shape"; shape.choices = shape_choices(); shape.index = index_of(shape.choices, saved[@"screenShape"], 0);
    fps.title = @"Frame Rate"; fps.choices = framerate_choices(); fps.index = index_of(fps.choices, saved[@"frameRate"], 0);
    nova.title = @"NovaWorld"; nova.choices = novaworld_choices(); nova.index = index_of(nova.choices, saved[@"novaWorld"], 0);
    router.title = @"Router"; router.choices = router_choices(); router.index = index_of(router.choices, saved[@"router"], 0);
    NSString *keys = controls_state(game, data) ?: saved[@"controls"] ?: @"classic";   /* what the soldiers' key maps are now */
    controls.title = @"Controls"; controls.choices = controls_choices([keys isEqualToString:@"custom"]); controls.index = index_of(controls.choices, keys, 0);
    NSString *(^val)(DSRow *) = ^NSString *(DSRow *r) { return r.choices[r.index][@"value"]; };
    const char *shot = getenv("DS_LAUNCHER_SHOT");
    if (getenv("DS_NO_LAUNCHER") && !shot) {
        if (saved.count) apply(val(shape), val(fps), val(nova), val(router), keys);
        return 1;
    }

    NSString *updater = getenv("DS_UPDATER") ? @(getenv("DS_UPDATER")) : nil, *branch = getenv("DS_UPDATE_BRANCH") ? @(getenv("DS_UPDATE_BRANCH")) : @"main";
    NSString *built = getenv("DS_BUILT_COMMIT") ? @(getenv("DS_BUILT_COMMIT")) : @"";
    if (![[NSFileManager defaultManager] isExecutableFileAtPath:updater ?: @""]) updater = nil;
    DSRow *upd = [DSRow new]; upd.title = @"Updates";
    __block int ustate = updater ? UPD_CHECKING : UPD_NONE; __block NSString *latest = nil;
    upd.choices = update_choice(ustate, branch, built, nil);
    BOOL shotMode = shot != NULL;
    DSSection *display = [DSSection new], *multi = [DSSection new], *guide = [DSSection new], *updSec = [DSSection new];
    display.title = @"Display"; display.rows = @[shape, fps];
    multi.title = @"Multiplayer"; multi.rows = @[nova, router];
    guide.title = @"Key Guide"; guide.summary = ^NSString * { return [val(controls) isEqualToString:@"modern"] ? @"Modern: WASD · Space · C · Z · R · G · Tab · M"
                                                                    : [val(controls) isEqualToString:@"custom"] ? @"Your own keys, set in the game" : @"Classic: arrows · Ctrl · S · Delete · End · Page Down"; };
    updSec.title = @"Updates"; updSec.summary = ^NSString * { NSDictionary *c = upd.choices[upd.index]; return [c[@"note"] length] ? [NSString stringWithFormat:@"%@ · %@", c[@"label"], c[@"note"]] : c[@"label"]; };
    DSLaunchView *v = [[DSLaunchView alloc] initWithFrame:NSMakeRect(0, 0, 780, 760)];
    v.top = @[controls]; v.sections = @[display, multi, guide, updSec]; v.open = -1; v.hover = v.pressed = HIT_NONE;
    v.note = @"Renderer (Colors: Hardware, 16 Million, 256) and resolution: in the game's own Settings. Join by address under Internet.";
    __weak DSLaunchView *wv = v; __weak DSRow *wu = upd, *wc = controls;
    guide.activate = ^{
        BOOL modern = [val(wc) isEqualToString:@"modern"];
        NSAlert *a = [NSAlert new]; a.messageText = modern ? @"Modern Controls" : [val(wc) isEqualToString:@"custom"] ? @"Your Controls" : @"Classic Controls";
        a.informativeText = [val(wc) isEqualToString:@"custom"] ? @"Your soldiers' keys were changed in the game's own Settings › Keyboard Remap, which lists them. "
                                                                   "Choose Classic or Modern under Controls to start again from one of those." : key_guide(modern);
        [a addButtonWithTitle:@"OK"]; [a runModal];
    };
    updSec.activate = ^{ if (wu.activate) wu.activate(); };
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
    #ifdef DF2_EDITION
    CGFloat tagline = 0.2;                                  /* Delta Force 2's logo without its "THE ART OF WAR" line */
#else
    CGFloat tagline = 0;
#endif
    NSImage *logo = art(data, @"logo"); v.logo = logo ? trimmed(logo, tagline) : nil;
    v.shot = art(data, @"shot");
    if (shot) {   /* development: the window as an image */
        const char *open = getenv("DS_LAUNCHER_OPEN");
        if (open) for (NSInteger i = 0; i < (NSInteger)v.sections.count; i++) if ([v.sections[i].title caseInsensitiveCompare:@(open)] == NSOrderedSame) v.open = i;
        if (getenv("DS_LAUNCHER_PICK")) controls.index = index_of(controls.choices, @(getenv("DS_LAUNCHER_PICK")), controls.index);
        NSBitmapImageRep *rep = [v bitmapImageRepForCachingDisplayInRect:v.bounds]; [v cacheDisplayInRect:v.bounds toBitmapImageRep:rep];
        [[rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}] writeToFile:@(shot) atomically:YES];
        if (getenv("DS_LAUNCHER_APPLY")) {          /* development: the Controls choice written as Play would */
            NSString *c = val(controls); if (![c isEqualToString:@"custom"]) controls_apply(game, data, [c isEqualToString:@"modern"], ![c isEqualToString:keys]);
            printf("controls: %s -> %s\n", keys.UTF8String, (controls_state(game, data) ?: @"mixed").UTF8String);
        }
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
    NSString *cv = val(controls);
    if (![cv isEqualToString:@"custom"] && !controls_apply(game, data, [cv isEqualToString:@"modern"], ![cv isEqualToString:keys])) {
        NSAlert *a = [NSAlert new]; a.messageText = @"The controls couldn't be changed";
        a.informativeText = [NSString stringWithFormat:@"The soldiers' key maps in %@ couldn't be written. The game starts with the keys it had.", [data stringByAppendingPathComponent:@"game"]];
        [a runModal]; cv = controls_state(game, data) ?: keys;
    }
    [@{@"screenShape": val(shape), @"frameRate": val(fps), @"novaWorld": val(nova), @"router": val(router), @"controls": cv} writeToFile:plistPath atomically:YES];
    apply(val(shape), val(fps), val(nova), val(router), cv);
    return 1;
}
