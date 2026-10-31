/*
 * main.mm
 *
 * LICHOMAT Emu, the Cocoa and Metal shell of tier 2 (stage 1b spec 6, plan R4 Task 12): the window, the Dear ImGui
 * backends, the folder pickers and the power cycle's relaunch. The panel itself is panel.cpp; the emulation is the shell
 * of app_shell.h. Objective-C++ with ARC; this is the only Objective-C in the emulator.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#import <Cocoa/Cocoa.h>
#import <CoreText/CoreText.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "app_card.h"
#include "app_shell.h"
#include "app_time.h"
#include "emu_notices.h"
#include "imgui.h"
#include "imgui_impl_metal.h"
#include "imgui_impl_osx.h"
#include "panel.h"
#include "render_build.h"

static app_shell_t *gShell;
static panel_t gPanel;
static char gRender[PATH_MAX];		/* the bundle's lichomat-render, for the first card */
static char gLastPath[PATH_MAX];	/* the folder of the last card operation, for 'replace' */
static int gLastProject;
static int gCardUnreadable;			/* app_shellStart() said so (spec 11) */

/* --version: the notices of spec 10.1, as lichomat-render prints them, under this binary's own first line. Nothing of
 * Cocoa is touched. */
static void emu_printVersion(void)
{
	printf("LICHOMAT Emu %s\n", EMU_RENDER_VERSION);
	printf("%s\n", EMU_TAGLINE);
	printf("Runs LICHOMAT %s with the emulator changes " EMU_FIRMWARE_CHANGES ". Built from commit %s.\n",
		EMU_RENDER_VERSION, EMU_RENDER_COMMIT);
	for (size_t i = 0; i < EMU_LEGAL_COUNT; i++)
		printf("%s\n", emu_legal[i]);
}

static void emu_message(void *ctx, const char *text)
{
	(void)ctx;
	if (strcmp(text, "error: the card folder cannot be read") == 0)
		gCardUnreadable = 1;
	panel_message(&gPanel, text);
}

static void emu_alert(NSString *title, NSString *text)
{
	NSAlert *a = [[NSAlert alloc] init];
	a.messageText = title;
	a.informativeText = text;
	[a addButtonWithTitle:@"OK"];
	[a runModal];
}

/* The power cycle (E18): a new process of this bundle, which waits for this one to end before it takes the MIDI endpoints
 * and the card (--after-pid). This process ends when the system has started the other; if that fails it says so first. */
static int emu_relaunch(void *ctx)
{
	(void)ctx;
	NSWorkspaceOpenConfiguration *cfg = [NSWorkspaceOpenConfiguration configuration];
	cfg.createsNewApplicationInstance = YES;
	cfg.arguments = @[ @"--after-pid", [NSString stringWithFormat:@"%d", getpid()] ];
	[[NSWorkspace sharedWorkspace] openApplicationAtURL:[[NSBundle mainBundle] bundleURL] configuration:cfg
		completionHandler:^(NSRunningApplication *app, NSError *error) {
			(void)app;
			if (error == nil)
				_exit(0);
			NSLog(@"LICHOMAT Emu: the power cycle could not start a new process: %@", error);
			dispatch_async(dispatch_get_main_queue(), ^{
				emu_alert(@"LICHOMAT Emu could not start itself again",
					[NSString stringWithFormat:@"%@\n\nThe card is safe. Start LICHOMAT Emu by hand.", error.localizedDescription]);
				_exit(70);
			});
		}];
	return 0;
}

/* Menlo, from the system, at run time (E23): no font is embedded in the binary */
static bool emu_loadFont(void)
{
	CTFontRef font = CTFontCreateWithName(CFSTR("Menlo-Regular"), 14.0, NULL);
	if (font == NULL)
		return false;
	CFURLRef url = (CFURLRef)CTFontCopyAttribute(font, kCTFontURLAttribute);
	char path[PATH_MAX];
	const bool ok = url != NULL && CFURLGetFileSystemRepresentation(url, true, (UInt8 *)path, sizeof path);
	if (url != NULL)
		CFRelease(url);
	CFRelease(font);
	if (!ok)
		return false;
	ImFontConfig cfg;
	cfg.FontNo = 0;	/* Menlo.ttc holds the regular face first */
	return ImGui::GetIO().Fonts->AddFontFromFileTTF(path, 15.0f, &cfg) != NULL;
}

/* How many faces a TrueType collection holds, 1 for a plain font, 0 if it cannot be read */
static unsigned emu_faces(const char *path)
{
	FILE *f = fopen(path, "rb");
	unsigned char h[12];
	unsigned n = 0;
	if (f != NULL && fread(h, 1u, sizeof h, f) == sizeof h)
		n = memcmp(h, "ttcf", 4) == 0 ? ((unsigned)h[8] << 24 | (unsigned)h[9] << 16 | (unsigned)h[10] << 8 | h[11]) : 1u;
	if (f != NULL)
		fclose(f);
	return n;
}

/* The display font of the wordmark (panel.cpp), the first of these that the system has (E33). Nothing is embedded: like
 * Menlo it is read from the system at run time. If none is there, the wordmark is written in the LCD's dots. */
static void emu_loadDisplayFont(void)
{
	static const struct { const char *path; unsigned face; } candidates[] = {
		{ "/System/Library/Fonts/Supplemental/DIN Condensed Bold.ttf", 0u },
		{ "/System/Library/Fonts/Supplemental/Futura.ttc", 4u },	/* Condensed ExtraBold on macOS 26; any face if not */
		{ "/System/Library/Fonts/Supplemental/Impact.ttf", 0u },
	};
	for (const auto &c : candidates)
	{
		const unsigned faces = emu_faces(c.path);
		if (faces == 0u)
			continue;
		ImFontConfig cfg;
		cfg.FontNo = c.face < faces ? c.face : 0u;
		if (ImGui::GetIO().Fonts->AddFontFromFileTTF(c.path, 48.0f, &cfg) != NULL)
			return;
	}
}

@interface EmuDelegate : NSObject <NSApplicationDelegate, NSWindowDelegate, MTKViewDelegate>
@property (nonatomic, strong) NSWindow *window;
@property (nonatomic, strong) MTKView *view;
@property (nonatomic, strong) id<MTLCommandQueue> queue;
@property (nonatomic, strong) id activity;
@property (nonatomic, strong) NSTimer *timer;
@end

@implementation EmuDelegate

/* spec 11: an unreadable card folder goes to Card backups, and a new, seeded card takes its place */
- (BOOL)offerNewCard
{
	NSAlert *a = [[NSAlert alloc] init];
	a.messageText = @"The card folder cannot be read";
	a.informativeText = @"LICHOMAT Emu can move it to Card backups and start with a new card that holds an Init project.";
	[a addButtonWithTitle:@"Make a new seeded card"];
	[a addButtonWithTitle:@"Quit"];
	if ([a runModal] != NSAlertFirstButtonReturn)
		return NO;
	app_paths_t paths;
	char backup[PATH_MAX];
	app_timeZoneRefresh();
	if (app_pathsInit(&paths) != 0 || app_backupName(paths.backups, app_localSeconds(), NULL, backup, sizeof backup) != 0
		|| rename(paths.card, backup) != 0)
	{
		emu_alert(@"The card folder could not be moved", @"Move or remove the folder Card in the app folder by hand.");
		return NO;
	}
	return YES;
}

- (BOOL)startShell
{
	for (int attempt = 0; attempt < 2; attempt++)
	{
		gShell = app_shellNew();
		app_shellOptions_t o;
		memset(&o, 0, sizeof o);
		o.render = gRender[0] ? gRender : NULL;
		o.relaunch = emu_relaunch;
		o.message = emu_message;
		char err[512];
		gCardUnreadable = 0;
		if (app_shellStart(gShell, &o, err, sizeof err) == 0)
			return YES;
		if (gCardUnreadable && attempt == 0 && [self offerNewCard])
			continue;
		if (!gCardUnreadable)
			emu_alert(@"LICHOMAT Emu cannot start", [NSString stringWithUTF8String:err]);
		return NO;
	}
	return NO;
}

- (void)saveFrame
{
	if (gShell == NULL || self.window == nil)
		return;
	const NSRect fr = self.window.frame;
	const int f[4] = { (int)lround(fr.origin.x), (int)lround(fr.origin.y), (int)lround(fr.size.width), (int)lround(fr.size.height) };
	app_shellSetWindow(gShell, f);
}

- (void)windowDidMove:(NSNotification *)note
{
	(void)note;
	[self saveFrame];
}

- (void)windowDidResize:(NSNotification *)note
{
	(void)note;
	[self saveFrame];
}

- (void)showAbout:(id)sender
{
	(void)sender;
	gPanel.showAbout = 1;
}

- (void)buildMenu
{
	NSMenu *bar = [[NSMenu alloc] init];
	NSMenuItem *appItem = [[NSMenuItem alloc] init];
	[bar addItem:appItem];
	NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"LICHOMAT Emu"];
	NSMenuItem *about = [appMenu addItemWithTitle:@"About LICHOMAT Emu" action:@selector(showAbout:) keyEquivalent:@""];
	about.target = self;
	[appMenu addItem:[NSMenuItem separatorItem]];
	[appMenu addItemWithTitle:@"Quit LICHOMAT Emu" action:@selector(terminate:) keyEquivalent:@"q"];
	appItem.submenu = appMenu;
	[NSApp setMainMenu:bar];
}

- (void)applicationDidFinishLaunching:(NSNotification *)note
{
	(void)note;
	/* §6.1: App Nap would stretch the firmware thread's timers */
	self.activity = [[NSProcessInfo processInfo] beginActivityWithOptions:(NSActivityUserInitiated | NSActivityLatencyCritical)
		reason:@"real-time emulation"];
	[self buildMenu];
	if (![self startShell])
	{
		[NSApp terminate:nil];
		return;
	}

	int f[4];
	app_shellWindow(gShell, f);
	const NSRect saved = NSMakeRect(f[0], f[1], f[2], f[3]);
	BOOL onScreen = NO;
	for (NSScreen *screen in [NSScreen screens])
		if (NSIntersectsRect(saved, screen.visibleFrame))
			onScreen = YES;
	self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1100, 730)
		styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable)
		backing:NSBackingStoreBuffered defer:NO];
	self.window.title = @"LICHOMAT Emu (unofficial)";	/* spec 10.2: the title says unofficial */
	self.window.releasedWhenClosed = NO;
	self.window.contentMinSize = NSMakeSize(760.0, 420.0);	// the panel scales to the window
	self.window.delegate = self;
	if (onScreen)
		[self.window setFrame:saved display:NO];
	else
		[self.window center];

	id<MTLDevice> device = MTLCreateSystemDefaultDevice();
	if (device == nil)
	{
		emu_alert(@"LICHOMAT Emu needs Metal", @"No Metal device was found.");
		[NSApp terminate:nil];
		return;
	}
	self.queue = [device newCommandQueue];
	self.view = [[MTKView alloc] initWithFrame:[self.window.contentView bounds] device:device];
	self.view.delegate = self;
	self.view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
	self.view.clearColor = MTLClearColorMake(0.098, 0.118, 0.149, 1.0);
	self.view.preferredFramesPerSecond = 60;
	self.window.contentView = self.view;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO &io = ImGui::GetIO();
	io.IniFilename = NULL;	/* the app keeps its own settings (spec 6.9) */
	io.LogFilename = NULL;
	if (!emu_loadFont())
	{
		emu_alert(@"LICHOMAT Emu cannot find its font", @"The system font Menlo could not be loaded.");
		[NSApp terminate:nil];
		return;
	}
	emu_loadDisplayFont();	/* the second font of the atlas; the wordmark finds it there */
	ImGui_ImplMetal_Init(device);
	ImGui_ImplOSX_Init(self.view);

	[self.window makeKeyAndOrderFront:nil];
	[NSApp activateIgnoringOtherApps:YES];
	/* the shell's per-frame work goes on while the window is hidden and no frame is drawn */
	self.timer = [NSTimer timerWithTimeInterval:0.05 repeats:YES block:^(NSTimer *t) {
		(void)t;
		(void)app_shellFrame(gShell);
	}];
	[[NSRunLoop currentRunLoop] addTimer:self.timer forMode:NSRunLoopCommonModes];
}

- (BOOL)applicationSupportsSecureRestorableState:(NSApplication *)app
{
	(void)app;
	return YES;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication *)sender
{
	(void)sender;
	return YES;
}

- (void)applicationWillTerminate:(NSNotification *)note
{
	(void)note;
	[self.timer invalidate];
	if (gShell != NULL)
		app_shellStop(gShell);	/* the firmware thread, the MIDI endpoints, the settings of the last half second */
	if (ImGui::GetCurrentContext() != NULL)
	{
		ImGui_ImplMetal_Shutdown();
		ImGui_ImplOSX_Shutdown();
		ImGui::DestroyContext();
	}
}

/* the folder for a card operation (spec 6.7), picked in a sheet; app_shellCardOp() takes the card out first */
- (void)chooseFolderFor:(int)request project:(int)project
{
	NSOpenPanel *panel = [NSOpenPanel openPanel];
	panel.canChooseDirectories = YES;
	panel.canChooseFiles = NO;
	panel.canCreateDirectories = YES;
	panel.allowsMultipleSelection = NO;
	if (request == PANEL_REQ_IMPORT)
	{
		panel.prompt = @"Import";
		panel.message = @"Pick the card folder to import. The current card goes to Card backups, and the app restarts.";
	}
	else if (request == PANEL_REQ_EXPORT)
	{
		panel.prompt = @"Export";
		panel.message = @"Pick an empty folder for a copy of the card.";
	}
	else
	{
		panel.prompt = @"Export";
		panel.message = [NSString stringWithFormat:@"Pick the card folder that receives project %d.", project];
	}
	[panel beginSheetModalForWindow:self.window completionHandler:^(NSModalResponse response) {
		if (response != NSModalResponseOK || panel.URL == nil)
			return;
		snprintf(gLastPath, sizeof gLastPath, "%s", panel.URL.fileSystemRepresentation);
		gLastProject = project;
		const int op = request == PANEL_REQ_IMPORT ? APP_OP_IMPORT : request == PANEL_REQ_EXPORT ? APP_OP_EXPORT : APP_OP_PROJECT;
		if (app_shellCardOp(gShell, op, gLastPath, project, 0) != 0)
			panel_message(&gPanel, "error: a card operation or a power cycle is running; try again in a moment");
	}];
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size
{
	(void)view;
	(void)size;
}

- (void)drawInMTKView:(MTKView *)view
{
	if (gShell == NULL)
		return;
	MTLRenderPassDescriptor *pass = view.currentRenderPassDescriptor;
	id<MTLCommandBuffer> cb = [self.queue commandBuffer];
	if (pass == nil)
	{
		[cb commit];
		return;
	}
	ImGui_ImplMetal_NewFrame(pass);
	ImGui_ImplOSX_NewFrame(view);
	ImGui::NewFrame();
	(void)app_shellFrame(gShell);
	panel_frame(&gPanel, gShell);
	ImGui::Render();
	id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:pass];
	ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), cb, enc);
	[enc endEncoding];
	[cb presentDrawable:view.currentDrawable];
	[cb commit];

	int project = 0;
	const int request = panel_request(&gPanel, &project);
	if (request == PANEL_REQ_REPLACE)
	{
		if (app_shellCardOp(gShell, APP_OP_PROJECT, gLastPath, gLastProject, 1) != 0)
			panel_message(&gPanel, "error: a card operation or a power cycle is running; try again in a moment");
	}
	else if (request != PANEL_REQ_NONE)
		[self chooseFolderFor:request project:project];
}

@end

static EmuDelegate *gDelegate;

int main(int argc, char **argv)
{
	int afterPid = 0;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--version") == 0)
		{
			emu_printVersion();
			return 0;
		}
		if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
		{
			puts("usage: LICHOMAT Emu [--version]\n"
				"The settings, the card and the crash notes are in the app folder, or in $LICHOMAT_EMU_HOME.");
			return 0;
		}
		if (strcmp(argv[i], "--after-pid") == 0 && i + 1 < argc)
			afterPid = atoi(argv[++i]);
		/* anything else (the system passes some arguments of its own) is not ours */
	}
	/* E18: before any Cocoa or CoreMIDI call, and never on the main run loop: the process of the power cycle that this one
	 * replaces may still own the MIDI endpoints and the card. Up to 5 s; then the start goes on. */
	if (afterPid > 0)
		(void)app_waitPid(afterPid, 5.0);
	@autoreleasepool
	{
		/* The app puts its own window back from its settings (spec 6.9). Without this, the system asks at the next start
		 * whether to reopen the windows, after a crash or a kill, and the app waits for the answer behind that alert. */
		[[NSUserDefaults standardUserDefaults] registerDefaults:@{ @"ApplePersistenceIgnoreState" : @YES }];
		NSString *render = [[NSBundle mainBundle] pathForAuxiliaryExecutable:@"lichomat-render"];
		if (render != nil)
			snprintf(gRender, sizeof gRender, "%s", render.fileSystemRepresentation);
		panel_init(&gPanel);
		NSApplication *app = [NSApplication sharedApplication];
		[app setActivationPolicy:NSApplicationActivationPolicyRegular];
		gDelegate = [[EmuDelegate alloc] init];
		app.delegate = gDelegate;
		[app run];
	}
	return 0;
}
