// df_icon <logo.png> <backdrop image or -> <out.png> [--df2]: the app's icon (install.sh), from the art tools/df_art.py
// fetched at install time: the game's textured backdrop on a rounded tile, darkened towards its edges, and the logo
// (the dagger in its triangle) over it with a soft glow in the game's colour (red for Delta Force, green-gold for Delta
// Force 2). The games' own icons are 32x32 Windows icons, too small for the Dock.
// Build: clang -fobjc-arc -framework AppKit
#import <AppKit/AppKit.h>

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
    CGFloat H = (y1 - y0 + 1) * (1 - cut);                                       /* bitmap rows run top-down here */
    CGImageRef t = CGImageCreateWithImageInRect(c, CGRectMake(x0, y0, x1 - x0 + 1, H));
    NSImage *r = [[NSImage alloc] initWithCGImage:t size:NSZeroSize]; CGImageRelease(t);
    return cut > 0 ? trimmed(r, 0) : r;
}

int main(int argc, char **argv)
{
    @autoreleasepool {
        if (argc < 4) { fprintf(stderr, "usage: df_icon <logo.png> <backdrop> <out.png> [--df2]\n"); return 2; }
        BOOL df2 = argc > 4 && !strcmp(argv[4], "--df2");
        NSImage *logo = [[NSImage alloc] initWithContentsOfFile:@(argv[1])], *back = [[NSImage alloc] initWithContentsOfFile:@(argv[2])];
        if (!logo || logo.size.width < 64) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
        const CGFloat S = 1024;
        NSBitmapImageRep *rep = [[NSBitmapImageRep alloc] initWithBitmapDataPlanes:NULL pixelsWide:(NSInteger)S pixelsHigh:(NSInteger)S
            bitsPerSample:8 samplesPerPixel:4 hasAlpha:YES isPlanar:NO colorSpaceName:NSDeviceRGBColorSpace bytesPerRow:0 bitsPerPixel:0];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = [NSGraphicsContext graphicsContextWithBitmapImageRep:rep];
        NSGraphicsContext.currentContext.imageInterpolation = NSImageInterpolationHigh;

        // the tile: macOS app-icon proportions (824 of 1024, corner radius ~185), with its drop shadow
        NSRect tile = NSMakeRect(100, 100, 824, 824);
        NSBezierPath *shape = [NSBezierPath bezierPathWithRoundedRect:tile xRadius:185 yRadius:185];
        NSShadow *drop = [NSShadow new]; drop.shadowColor = [NSColor colorWithWhite:0 alpha:0.45]; drop.shadowOffset = NSMakeSize(0, -10); drop.shadowBlurRadius = 24;
        [NSGraphicsContext saveGraphicsState]; [drop set]; [[NSColor blackColor] setFill]; [shape fill]; [NSGraphicsContext restoreGraphicsState];
        [NSGraphicsContext saveGraphicsState];
        [shape addClip];
        NSColor *base = df2 ? [NSColor colorWithSRGBRed:0.25 green:0.16 blue:0.02 alpha:1] : [NSColor colorWithSRGBRed:0.22 green:0.02 blue:0.03 alpha:1];
        [base setFill]; NSRectFill(tile);
        if (back.size.width > 64) {   // a square from the texture's middle
            CGFloat side = MIN(back.size.width, back.size.height);
            [back drawInRect:tile fromRect:NSMakeRect((back.size.width - side) / 2, (back.size.height - side) / 2, side, side)
                   operation:NSCompositingOperationSourceOver fraction:0.9];
        }
        NSGradient *shade = [[NSGradient alloc] initWithColors:@[[NSColor colorWithWhite:0 alpha:0.05], [NSColor colorWithWhite:0 alpha:0.7]]];
        [shade drawInRect:tile relativeCenterPosition:NSMakePoint(0, 0.15)];

        // the logo, as large as fits with a margin, glowing faintly (Delta Force 2's without its "THE ART OF WAR" line)
        logo = trimmed(logo, df2 ? 0.2 : 0);
        NSSize ls = logo.size; CGFloat scale = MIN(700 / ls.width, 740 / ls.height);
        NSRect to = NSMakeRect(512 - ls.width * scale / 2, 512 - ls.height * scale / 2, ls.width * scale, ls.height * scale);
        NSShadow *glow = [NSShadow new]; glow.shadowOffset = NSZeroSize; glow.shadowBlurRadius = 36;
        glow.shadowColor = df2 ? [NSColor colorWithSRGBRed:0.95 green:0.75 blue:0.2 alpha:0.6] : [NSColor colorWithSRGBRed:1 green:0.25 blue:0.2 alpha:0.55];
        [NSGraphicsContext saveGraphicsState]; [glow set];
        [logo drawInRect:to fromRect:NSZeroRect operation:NSCompositingOperationSourceOver fraction:1];
        [NSGraphicsContext restoreGraphicsState];
        [NSGraphicsContext restoreGraphicsState];
        [NSGraphicsContext restoreGraphicsState];
        NSData *png = [rep representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (![png writeToFile:@(argv[3]) atomically:YES]) { fprintf(stderr, "cannot write %s\n", argv[3]); return 1; }
    }
    return 0;
}
