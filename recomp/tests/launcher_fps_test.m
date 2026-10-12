#include "../host/launcher.m"
#include <assert.h>
static void clear_fps(void) { unsetenv("DSR_FPSCAP"); unsetenv("DF_HIGH_FPS"); unsetenv("DSR_VSYNC"); }
int main(void) {
 @autoreleasepool {
  [NSApplication sharedApplication];
  for (NSString *v in @[@"1",@"60",@"237",@"10000"]) assert(valid_fps(v));
  for (NSString *v in @[@"",@"0",@"-1",@"10001",@"60.5",@"60x"]) assert(!valid_fps(v));
  NSArray *choices=framerate_choices(@"237");
  assert(index_of(choices,@"237",-1)>=0 && index_of(choices,@"unlimited",-1)>=0);
  assert(index_of(choices,@"custom",-1)>=0 && index_of(choices,@"original",-1)>=0);
  for (NSString *v in @[@"1",@"237",@"10000",@"unlimited"]) {
   clear_fps(); apply(@"off",v,@"off",@"off",@"classic");
   assert(!strcmp(getenv("DSR_FPSCAP"),[v isEqualToString:@"unlimited"]?"0":v.UTF8String));
   assert(!strcmp(getenv("DSR_VSYNC"),"0"));
  }
  clear_fps(); apply(@"off",@"original",@"off",@"off",@"classic");
  assert(!strcmp(getenv("DF_HIGH_FPS"),"0") && !getenv("DSR_FPSCAP"));
  clear_fps(); apply(@"off",@"display",@"off",@"off",@"classic");
  assert(!getenv("DF_HIGH_FPS") && !getenv("DSR_FPSCAP") && !getenv("DSR_VSYNC"));
  clear_fps(); setenv("DSR_FPSCAP","144",1); setenv("DSR_VSYNC","1",1);
  apply(@"off",@"unlimited",@"off",@"off",@"classic");
  assert(!strcmp(getenv("DSR_FPSCAP"),"144") && !strcmp(getenv("DSR_VSYNC"),"1"));
  clear_fps(); setenv("DF_HIGH_FPS","0",1);
  apply(@"off",@"237",@"off",@"off",@"classic");
  assert(!getenv("DSR_FPSCAP") && !getenv("DSR_VSYNC"));
  puts("Launcher FPS: numeric limits, saved custom values, unlimited, original and environment precedence passed");
 }
}
