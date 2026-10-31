/*
 * dev_main.c
 *
 * lichomat-emu-dev (plan E1): LICHOMAT Emu without Cocoa. It runs the shell on
 * the terminal: commands on stdin or from a script at wall times, the LCD, the
 * LEDs and the diagnostics as lines on stdout. It is a test tool; the bundle
 * does not hold it.
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include <errno.h>
#include <mach/mach_time.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "app_mach.h"
#include "app_shell.h"

#define DEV_DUE_MAX 256u
#define DEV_LINE 256u
#define DEV_ENDPOINTS 64

typedef struct due { double at; char line[DEV_LINE]; } due_t;

static struct
{
	app_shell_t *shell;
	double t0, seconds, snapWall;
	int printLeds, quit, stdinOpen;
	due_t due[DEV_DUE_MAX];
	unsigned nDue;
	char lcd[2][16];
	int lcdShown;
	uint64_t leds;
} D;
static volatile sig_atomic_t gSignal;

static double wallNow(void)
{
	return app_wallSeconds() - D.t0;
}

static void onSignal(int sig)
{
	(void)sig;
	gSignal = 1;
}

static void usage(void)
{
	fputs("usage: lichomat-emu-dev [--card DIR] [--no-audio] [--no-midi] [--device UID] [--buffer N]\n"
		"                        [--seconds S] [--script FILE] [--print-diag] [--print-leds]\n", stderr);
}

static void message(void *ctx, const char *text)
{
	(void)ctx;
	printf("%s\n", text);
}

static void later(double at, const char *line)
{
	if (D.nDue == DEV_DUE_MAX)
	{
		printf("error: too many pending commands\n");
		return;
	}
	D.due[D.nDue].at = at;
	snprintf(D.due[D.nDue].line, DEV_LINE, "%s", line);
	D.nDue++;
}

static int loadScript(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f)
	{
		fprintf(stderr, "lichomat-emu-dev: %s: %s\n", path, strerror(errno));
		return -1;
	}
	char line[DEV_LINE];
	int n = 0;
	while (fgets(line, sizeof line, f))
	{
		n++;
		char *end;
		line[strcspn(line, "\r\n")] = '\0';
		const char *p = line + strspn(line, " \t");
		if (*p == '\0' || *p == '#')
			continue;
		const double at = strtod(p, &end);
		if (end == p || at < 0.0 || *end == '\0')
		{
			fprintf(stderr, "lichomat-emu-dev: %s:%d: expected SECONDS COMMAND\n", path, n);
			fclose(f);
			return -1;
		}
		later(at, end + strspn(end, " \t"));
	}
	fclose(f);
	return 0;
}

static void printDiag(void)
{
	const app_snapshot_t *s = app_shellSnapshot(D.shell);
	if (!s)
	{
		printf("error: no snapshot yet\n");
		return;
	}
	const app_diagnostics_t *g = &s->diag;
	const int lead = g->leadMin <= g->leadMax;
	printf("diag blockRate=%.2f load=%.3f L=%.2fms D=%.2fms Deff=%.2fms lead=%.2f..%.2fms inLat=%.2f..%.2fms "
		"outUnderruns=%llu fwUnderruns=%llu output=%s realtime=%d vt=%.3f wall=%.3f\n",
		g->blockRate, g->load, g->L * 1e3, g->D * 1e3, g->Deff * 1e3, lead ? g->leadMin * 1e3 : 0.0,
		lead ? g->leadMax * 1e3 : 0.0, g->inLatMin * 1e3, g->inLatMax * 1e3, (unsigned long long)g->outUnderruns,
		(unsigned long long)g->core.underruns, g->outputRunning && g->output.name[0] ? g->output.name : "none",
		g->realtime, (double)s->vt / EMU_CPU_HZ, D.snapWall);
}

/* the newest snapshot: the LCD when its rows changed, the LEDs when their mask did */
static void printSnapshot(void)
{
	const app_snapshot_t *s = app_shellSnapshot(D.shell);
	char rows[2][16];
	for (unsigned r = 0; r < 2u; r++)
		for (unsigned c = 0; c < 16u; c++)
		{
			const uint8_t ch = s->lcd.chars[r][c];
			rows[r][c] = ch >= 0x20u && ch <= 0x7Eu ? (char)ch : '?';
		}
	if (!D.lcdShown || memcmp(rows, D.lcd, sizeof rows) != 0)
	{
		memcpy(D.lcd, rows, sizeof rows);
		D.lcdShown = 1;
		printf("lcd %.3f |%.16s|%.16s|\n", D.snapWall, rows[0], rows[1]);
	}
	if (D.printLeds && s->leds != D.leds)
	{
		D.leds = s->leds;
		printf("led %.3f %llx\n", D.snapWall, (unsigned long long)s->leds);
	}
}

static int post(const app_panelEvent_t *ev)
{
	if (app_shellPanel(D.shell, ev) != 0)
	{
		printf("error: the panel ring is full\n");
		return -1;
	}
	return 0;
}

static int send(const app_cmd_t *c)
{
	if (app_shellCommand(D.shell, c) != 0)
	{
		printf("error: the command ring is full\n");
		return -1;
	}
	return 0;
}

static void button(int n, int down)
{
	app_panelEvent_t ev;
	memset(&ev, 0, sizeof ev);
	ev.kind = APP_P_BUTTON;
	ev.index = (uint8_t)n;
	ev.value = down;
	(void)post(&ev);
}

static int parseInt(const char *s, long lo, long hi, long *out)
{
	char *end;
	if (!s)
		return -1;
	errno = 0;
	const long v = strtol(s, &end, 0);
	if (end == s || *end != '\0' || errno || v < lo || v > hi)
		return -1;
	*out = v;
	return 0;
}

static int parseNum(const char *s, double lo, double hi, double *out)
{
	char *end;
	if (!s)
		return -1;
	const double v = strtod(s, &end);
	if (end == s || *end != '\0' || !(v >= lo && v <= hi))
		return -1;
	*out = v;
	return 0;
}

/* one command line; the words are split in place */
static void command(char *line)
{
	char *w[4] = { NULL, NULL, NULL, NULL };
	unsigned n = 0;
	char *save = NULL;
	for (char *t = strtok_r(line, " \t", &save); t && n < 4u; t = strtok_r(NULL, " \t", &save))
		w[n++] = t;
	if (n == 0u)
		return;
	const char *c = w[0];
	long a, b;
	double x, y;
	app_panelEvent_t ev;
	app_cmd_t cmd;
	memset(&ev, 0, sizeof ev);
	memset(&cmd, 0, sizeof cmd);
	if (!strcmp(c, "press") || !strcmp(c, "down") || !strcmp(c, "up"))
	{
		if (parseInt(w[1], 0, 40, &a) != 0)
			printf("error: %s N: button 0-40\n", c);
		else if (!strcmp(c, "press"))
		{
			long ms = 50;
			if (w[2] && parseInt(w[2], 1, 60000, &ms) != 0)
				printf("error: press N [MS]: 1-60000\n");
			else
			{
				char up[DEV_LINE];
				button((int)a, 1);
				snprintf(up, sizeof up, "up %ld", a);
				later(wallNow() + (double)ms / 1000.0, up);
			}
		}
		else
			button((int)a, !strcmp(c, "down"));
	}
	else if (!strcmp(c, "enc"))
	{
		if (parseInt(w[1], -1000, 1000, &a) != 0)
			printf("error: enc D: detents\n");
		else
		{
			ev.kind = APP_P_ENCODER;
			ev.value = (int32_t)a;
			(void)post(&ev);
		}
	}
	else if (!strcmp(c, "push"))
	{
		a = 50;
		if (w[1] && parseInt(w[1], 40, 60000, &a) != 0)
			printf("error: push [MS]: 40-60000\n");
		else
		{
			ev.kind = APP_P_PUSH;
			ev.value = (int32_t)a;
			(void)post(&ev);
		}
	}
	else if (!strcmp(c, "knob"))
	{
		if (parseInt(w[1], 1, 4, &a) != 0 || parseInt(w[2], -3600, 3600, &b) != 0)
			printf("error: knob K DEG: K 1-4\n");
		else
		{
			ev.kind = APP_P_KNOB;
			ev.index = (uint8_t)a;
			ev.value = (int32_t)b;
			(void)post(&ev);
		}
	}
	else if (!strcmp(c, "fader"))
	{
		if (parseInt(w[1], 1, 6, &a) != 0 || parseNum(w[2], 0.0, 1.0, &x) != 0)
			printf("error: fader F L: F 1-6, L 0-1\n");
		else
		{
			ev.kind = APP_P_FADER;
			ev.index = (uint8_t)a;
			ev.level = x;
			(void)post(&ev);
		}
	}
	else if (!strcmp(c, "jacks"))
	{
		if (parseInt(w[1], 0, 255, &a) != 0)
			printf("error: jacks MASK: 0-255\n");
		else
		{
			ev.kind = APP_P_JACKS;
			ev.value = (int32_t)a;
			(void)post(&ev);
		}
	}
	else if (!strcmp(c, "card"))
	{
		if (w[1] && (!strcmp(w[1], "in") || !strcmp(w[1], "out")))
		{
			ev.kind = APP_P_CARD;
			ev.value = !strcmp(w[1], "in");
			(void)post(&ev);
		}
		else
			printf("error: card in|out\n");
	}
	else if (!strcmp(c, "pulse"))
	{
		ev.kind = APP_P_CLOCK;
		ev.host = mach_absolute_time();
		ev.value = 1;
		(void)post(&ev);
	}
	else if (!strcmp(c, "reset"))
	{
		if (w[1] && (!strcmp(w[1], "on") || !strcmp(w[1], "off")))
		{
			ev.kind = APP_P_RESET;
			ev.host = mach_absolute_time();
			ev.value = !strcmp(w[1], "on");
			(void)post(&ev);
		}
		else
			printf("error: reset on|off\n");
	}
	else if (!strcmp(c, "gen"))
	{
		cmd.kind = APP_CMD_GENERATOR;
		if (w[1] && !strcmp(w[1], "off"))
			(void)send(&cmd);
		else if (parseNum(w[1], 1.0, 999.0, &x) != 0 || parseInt(w[2], 4, 24, &a) != 0 || (a != 4 && a != 24))
			printf("error: gen BPM PPQ|off: BPM 1-999, PPQ 4 or 24\n");
		else
		{
			cmd.a = 1;
			cmd.x = x;
			cmd.b = (int32_t)a;
			(void)send(&cmd);
		}
	}
	else if (!strcmp(c, "delay"))
	{
		cmd.kind = APP_CMD_DELAY;
		if (w[1] && !strcmp(w[1], "auto"))
		{
			cmd.a = 1;
			(void)send(&cmd);
		}
		else if (parseNum(w[1], 0.0, 200.0, &y) != 0)
			printf("error: delay auto|MS: 0-200\n");
		else
		{
			cmd.x = y / 1000.0;
			(void)send(&cmd);
		}
	}
	else if (!strcmp(c, "mix"))
	{
		static const char *const names[] = { "sum", "out1", "out2", "four" };
		int k = -1;
		for (int i = 0; w[1] && i < 4; i++)
			if (!strcmp(w[1], names[i]))
				k = i;
		if (k < 0)
			printf("error: mix sum|out1|out2|four\n");
		else
		{
			cmd.kind = APP_CMD_MIX;
			cmd.a = k;
			(void)send(&cmd);
		}
	}
	else if (!strcmp(c, "devices"))
	{
		app_outputDevice_t dev[32];
		const int count = app_shellDevices(D.shell, dev, 32);
		for (int i = 0; i < count; i++)
			printf("device %u %s %u %s\n", dev[i].id, dev[i].uid, dev[i].channels, dev[i].name);
		if (count == 0)
			printf("device none\n");
	}
	else if (!strcmp(c, "output"))
	{
		a = 0;
		if (!w[1] || (w[2] && parseInt(w[2], 0, 4096, &a) != 0) || strlen(w[1]) >= sizeof cmd.text)
			printf("error: output UID|default [N]: N 0-4096 frames, 0 the device's\n");
		else
		{
			cmd.kind = APP_CMD_OUTPUT;
			if (strcmp(w[1], "default") != 0)
				snprintf(cmd.text, sizeof cmd.text, "%s", w[1]);
			cmd.a = (int32_t)a;
			(void)send(&cmd);
		}
	}
	else if (!strcmp(c, "midi"))
	{
		app_endpoint_t ep[DEV_ENDPOINTS];
		int total = 0;
		for (int source = 1; source >= 0; source--)
		{
			const int count = app_shellEndpoints(D.shell, source, ep, DEV_ENDPOINTS);
			for (int i = 0; i < count; i++)
				printf("midi %s %d %d %s\n", source ? "source" : "destination", i, (int)ep[i].uid, ep[i].name);
			total += count;
		}
		if (total == 0)
			printf("midi none\n");
	}
	else if (!strcmp(c, "din-src") || !strcmp(c, "din-dst"))
	{
		const int source = !strcmp(c, "din-src");
		app_endpoint_t ep[DEV_ENDPOINTS];
		const int count = app_shellEndpoints(D.shell, source, ep, DEV_ENDPOINTS);
		if (w[1] && !strcmp(w[1], "off"))
		{
			app_shellBind(D.shell, source, 0, "");
			printf("din %s off\n", source ? "source" : "destination");
		}
		else if (parseInt(w[1], 0, DEV_ENDPOINTS - 1, &a) != 0 || a >= count)
			printf("error: %s N|off: N is the index that `midi` lists\n", c);
		else
		{
			app_shellBind(D.shell, source, ep[a].uid, ep[a].name);
			printf("din %s %s (%d)\n", source ? "source" : "destination", ep[a].name, (int)ep[a].uid);
		}
	}
	else if (!strcmp(c, "diag"))
		printDiag();
	else if (!strcmp(c, "quit"))
		D.quit = 1;
	else
		printf("error: unknown command: %s\n", c);
}

/* the script lines and releases that are due, earliest first */
static void runDue(void)
{
	for (;;)
	{
		unsigned k = DEV_DUE_MAX;
		const double now = wallNow();
		for (unsigned i = 0; i < D.nDue; i++)
			if (D.due[i].at <= now && (k == DEV_DUE_MAX || D.due[i].at < D.due[k].at))
				k = i;
		if (k == DEV_DUE_MAX)
			return;
		char line[DEV_LINE];
		memcpy(line, D.due[k].line, DEV_LINE);
		D.due[k] = D.due[--D.nDue];
		command(line);
	}
}

/* stdin: one command per line; returns after at most 20 ms */
static void readInput(void)
{
	static char buf[512];
	static size_t len;
	if (!D.stdinOpen)
	{
		const struct timespec d = { 0, 20000000L };
		(void)nanosleep(&d, NULL);	/* no spinning once stdin is at its end */
		return;
	}
	struct pollfd p = { 0, POLLIN, 0 };
	if (poll(&p, 1, 20) <= 0)
		return;
	if (p.revents & (POLLIN | POLLHUP | POLLERR | POLLNVAL))
	{
		const ssize_t got = read(0, buf + len, sizeof buf - len - 1u);
		if (got <= 0)
		{
			D.stdinOpen = 0;
			if (len)
			{
				buf[len] = '\0';
				command(buf);
				len = 0;
			}
			return;
		}
		len += (size_t)got;
	}
	for (char *nl; (nl = memchr(buf, '\n', len)) != NULL;)
	{
		*nl = '\0';
		command(buf);
		len -= (size_t)(nl + 1 - buf);
		memmove(buf, nl + 1, len);
	}
	if (len >= sizeof buf - 1u)
		len = 0;	/* a line too long is dropped */
}

int main(int argc, char **argv)
{
	app_shellOptions_t o;
	const char *script = NULL;
	int printDiagAtEnd = 0;
	memset(&o, 0, sizeof o);
	o.message = message;
	D.seconds = -1.0;
	D.stdinOpen = 1;
	setvbuf(stdout, NULL, _IOLBF, 0);	/* tests read a pipe; line by line */
	for (int i = 1; i < argc; i++)
	{
		const char *a = argv[i];
		if (!strcmp(a, "--card") && i + 1 < argc)
			o.cardDir = argv[++i];
		else if (!strcmp(a, "--no-audio"))
			o.noAudio = 1;
		else if (!strcmp(a, "--no-midi"))
			o.noMidi = 1;
		else if (!strcmp(a, "--device") && i + 1 < argc)
			o.deviceUid = argv[++i];
		else if (!strcmp(a, "--buffer") && i + 1 < argc)
		{
			double frames;
			if (parseNum(argv[++i], 0.0, 4096.0, &frames) != 0)
			{
				usage();
				return 1;
			}
			o.bufferFrames = (uint32_t)frames;
		}
		else if (!strcmp(a, "--seconds") && i + 1 < argc)
		{
			if (parseNum(argv[++i], 0.0, 86400.0, &D.seconds) != 0)
			{
				usage();
				return 1;
			}
		}
		else if (!strcmp(a, "--script") && i + 1 < argc)
			script = argv[++i];
		else if (!strcmp(a, "--print-diag"))
			printDiagAtEnd = 1;
		else if (!strcmp(a, "--print-leds"))
			D.printLeds = 1;
		else
		{
			usage();
			return 1;
		}
	}
	if (script && loadScript(script) != 0)
		return 1;
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_handler = onSignal;
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGTERM, &sa, NULL);
	D.shell = app_shellNew();
	char err[256] = "";
	D.t0 = app_wallSeconds();
	if (!D.shell || app_shellStart(D.shell, &o, err, sizeof err) != 0)
	{
		fprintf(stderr, "lichomat-emu-dev: %s\n", err[0] ? err : "out of memory");
		return 2;
	}
	while (!D.quit && !gSignal && !(D.seconds >= 0.0 && wallNow() >= D.seconds))
	{
		readInput();
		runDue();
		if (app_shellFrame(D.shell))
		{
			D.snapWall = wallNow();
			printSnapshot();
		}
	}
	if (app_shellFrame(D.shell))
	{
		D.snapWall = wallNow();
		printSnapshot();
	}
	if (printDiagAtEnd)
		printDiag();
	app_shellStop(D.shell);
	return 0;
}
