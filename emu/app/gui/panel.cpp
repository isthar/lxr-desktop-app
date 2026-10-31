/*
 * panel.cpp
 *
 * The virtual panel of LICHOMAT Emu (stage 1b spec 6.6, plan R4 Task 12): Dear ImGui code that draws one frame from the
 * shell's snapshot and posts panel events and commands. The controls sit where the owner's layout puts them (E33), in our
 * own colours, with plain function names; the panel carries no maker's or product's name, no logo and no printed
 * graphics of the device (spec 6.6, 10.2).
 *
 * Copyright (C) 2026 Tomasz Krzywicki
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * Licensed under the GNU General Public License version 3, with the additional
 * permission and terms in NOTICE.md. See LICENSE and NOTICE.md in the root of
 * the repository.
 */
#include "panel.h"

#include <mach/mach_time.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "app_bind.h"
#include "app_shell.h"
#include "app_types.h"
#include "emu_notices.h"
#include "imgui.h"
#include "panel_font.h"
#include "panel_layout.h"
#include "render_build.h"

namespace
{

/* our own colours: a dark slate ground, a teal LCD, amber LEDs (spec 6.6) */
const ImU32 kRow = IM_COL32(35, 42, 53, 255);
const ImU32 kRowLine = IM_COL32(58, 69, 84, 255);
const ImU32 kText = IM_COL32(205, 214, 223, 255);
const ImU32 kDim = IM_COL32(128, 142, 158, 255);
const ImU32 kKey = IM_COL32(52, 64, 78, 255);
const ImU32 kKeyHot = IM_COL32(66, 82, 100, 255);
const ImU32 kKeyDown = IM_COL32(88, 120, 152, 255);
const ImU32 kLcdBack = IM_COL32(8, 38, 40, 255);
const ImU32 kLcdOff = IM_COL32(14, 54, 56, 255);
const ImU32 kLcdOn = IM_COL32(118, 236, 214, 255);
const ImU32 kLedOn = IM_COL32(255, 176, 0, 255);
const ImU32 kLedOff = IM_COL32(78, 58, 22, 255);
const ImU32 kDial = IM_COL32(44, 55, 68, 255);
const ImU32 kDialLine = IM_COL32(150, 166, 184, 255);

/* The panel is laid out in points of a reference width and scaled to the window (E33). The controls sit where the owner's
 * muscle memory expects them: the jacks along the top, the display with the knobs under it on the left, the faders and
 * the voice buttons on the right, the three groups of buttons, then the steps. Names are plain function names. */
const float kRefW = 1000.0f, kRefH = 490.0f;
const float kStripH = 64.0f;
const float kDot = 4.0f;			/* the LCD's dot pitch */
const float kLcdPad = 6.0f;
const float kLcdW = 16.0f * 6.0f * kDot + 2.0f * kLcdPad, kLcdH = 17.0f * kDot + 2.0f * kLcdPad;
const float kEncoderX = kLcdW + 54.0f, kEncoderR = 30.0f;
const float kHeaderH = 58.0f;		/* the wordmark above the display */
const float kKnobR = 28.0f, kKnobY = kLcdH + 14.0f;
const float kRightX = 520.0f, kRightW = 480.0f;		/* the faders and the voice buttons */
const float kFaderW = 34.0f, kFaderH = 150.0f;
const float kVoiceY = kFaderH + 30.0f, kVoiceH = 44.0f;
const float kUpperH = kVoiceY + kVoiceH + 4.0f;	/* the right side is the taller one: the faders, then the voices */
const float kKeyW = 55.0f, kKeyH = 48.0f, kKeyGap = 4.0f, kFramePad = 6.0f, kGroupGap = 12.0f;
const float kStepH = 60.0f, kStepGap = 6.0f;
const double kBpmMax = 300.0;	/* the generator's range, as the shell accepts it (APP_GEN_BPM_MAX) */

float gS = 1.0f;			/* this frame's scale */
ImVec2 gOrigin;				/* the screen position of the panel's top left corner */

float S(float v)
{
	return v * gS;
}

ImVec2 P(float x, float y)
{
	return ImVec2(gOrigin.x + x * gS, gOrigin.y + y * gS);
}

double wall(void)
{
	return (double)clock_gettime_nsec_np(CLOCK_UPTIME_RAW) * 1e-9;
}

bool starts(const char *s, const char *prefix)
{
	return strncmp(s, prefix, strlen(prefix)) == 0;
}

/* the first line of text, as far as one line goes */
void firstLine(const char *text, char *out, size_t n)
{
	snprintf(out, n, "%.*s", (int)strcspn(text, "\n"), text);
}

void applyStyle(void)
{
	ImGuiStyle &st = ImGui::GetStyle();
	ImGui::StyleColorsDark(&st);
	st.WindowRounding = 6.0f;
	st.FrameRounding = 4.0f;
	st.GrabRounding = 4.0f;
	st.PopupRounding = 6.0f;
	st.WindowPadding = ImVec2(12.0f, 10.0f);
	st.ItemSpacing = ImVec2(8.0f, 6.0f);
	ImVec4 *c = st.Colors;
	c[ImGuiCol_WindowBg] = ImVec4(0.098f, 0.118f, 0.149f, 1.0f);
	c[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.145f, 0.18f, 1.0f);
	c[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.17f, 0.21f, 1.0f);
	c[ImGuiCol_FrameBg] = ImVec4(0.19f, 0.24f, 0.30f, 1.0f);
	c[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.31f, 0.38f, 1.0f);
	c[ImGuiCol_FrameBgActive] = ImVec4(0.29f, 0.37f, 0.46f, 1.0f);
	c[ImGuiCol_SliderGrab] = ImVec4(0.46f, 0.88f, 0.80f, 1.0f);
	c[ImGuiCol_SliderGrabActive] = ImVec4(0.62f, 0.96f, 0.90f, 1.0f);
	c[ImGuiCol_CheckMark] = ImVec4(1.0f, 0.69f, 0.0f, 1.0f);
	c[ImGuiCol_Button] = ImVec4(0.20f, 0.25f, 0.31f, 1.0f);
	c[ImGuiCol_ButtonHovered] = ImVec4(0.26f, 0.33f, 0.41f, 1.0f);
	c[ImGuiCol_ButtonActive] = ImVec4(0.34f, 0.47f, 0.60f, 1.0f);
	c[ImGuiCol_Header] = ImVec4(0.24f, 0.31f, 0.39f, 1.0f);
}

/* a panel event out; 0 if it went */
int post(app_shell_t *s, uint8_t kind, uint8_t index, int32_t value, double level = 0.0, uint64_t host = 0u)
{
	app_panelEvent_t ev;
	memset(&ev, 0, sizeof ev);
	ev.kind = kind;
	ev.index = index;
	ev.value = value;
	ev.level = level;
	ev.host = host;
	return app_shellPanel(s, &ev);
}

/* --- the display ------------------------------------------------------------------------------------------ */

/* 16 x 2 cells of 5 x 8 dots from panel_glyph(), blank while the display is off; the underline cursor and the blinking
 * cell at 2 Hz of wall time (spec 4.7) */
void drawLcd(const emu_lcdSnapshot_t &l, ImVec2 o)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const float dot = S(kDot), pad = S(kLcdPad);
	dl->AddRectFilled(o, ImVec2(o.x + S(kLcdW), o.y + S(kLcdH)), kLcdBack, S(5.0f));
	const bool phase = ((long)(wall() * 4.0) & 1L) != 0L;
	for (unsigned row = 0; row < 2u; row++)
		for (unsigned col = 0; col < 16u; col++)
		{
			uint8_t g[8];
			panel_glyph(l.chars[row][col], l.cgram, g);
			const bool here = l.displayOn && col == l.cursorCol && row == l.cursorRow;
			if (here && l.cursorOn)
				g[7] = 0x1Fu;	/* the underline */
			const bool invert = here && l.blinkOn && phase;
			for (unsigned y = 0; y < 8u; y++)
				for (unsigned x = 0; x < 5u; x++)
				{
					const bool lit = l.displayOn && (((g[y] >> (4u - x)) & 1u) != 0u);
					const float px = o.x + pad + ((float)col * 6.0f + (float)x) * dot;
					const float py = o.y + pad + ((float)row * 9.0f + (float)y) * dot;
					dl->AddRectFilled(ImVec2(px, py), ImVec2(px + dot - 1.0f, py + dot - 1.0f),
						(lit != invert) ? kLcdOn : kLcdOff, 1.0f);
				}
		}
}

/* a small label, centred on x; one that is wider than max breaks at its space */
void label(float cx, float y, const char *text, float max, ImU32 col = kDim)
{
	ImGui::PushFont(NULL, S(11.0f));
	char a[32], b[32] = "";
	snprintf(a, sizeof a, "%s", text);
	char *space = strchr(a, ' ');
	if (space != NULL && ImGui::CalcTextSize(a).x > S(max))
	{
		snprintf(b, sizeof b, "%s", space + 1);
		*space = '\0';
	}
	ImDrawList *dl = ImGui::GetWindowDrawList();
	const ImVec2 sa = ImGui::CalcTextSize(a);
	dl->AddText(ImVec2(cx - sa.x * 0.5f, y), col, a);
	if (b[0])
		dl->AddText(ImVec2(cx - ImGui::CalcTextSize(b).x * 0.5f, y + sa.y), col, b);
	ImGui::PopFont();
}

/* The wordmark above the display, in the display font from the system when the app has loaded one (main.mm: nothing is
 * embedded), else written in the LCD's own dots. The subtitle under it is the version line of the notices. */
void wordmark(ImVec2 o)
{
	static const char word[] = "LICHOMAT";
	ImDrawList *dl = ImGui::GetWindowDrawList();
	ImFontAtlas *atlas = ImGui::GetIO().Fonts;
	if (atlas->Fonts.Size > 1)
	{
		ImGui::PushFont(atlas->Fonts[1], S(48.0f));
		dl->AddText(ImVec2(o.x + S(2.0f), o.y - S(8.0f)), kLedOn, word);
		ImGui::PopFont();
	}
	else
	{
		const float d = S(5.0f);	/* 5 x 7 dots a letter, 6 dots to the pitch */
		for (unsigned i = 0; i < sizeof word - 1u; i++)
			for (unsigned x = 0; x < 5u; x++)
				for (unsigned y = 0; y < 7u; y++)
					if (((panel_font[word[i] - 0x20][x] >> y) & 1u) != 0u)
						dl->AddRectFilled(ImVec2(o.x + ((float)i * 6.0f + (float)x) * d, o.y + (float)y * d),
							ImVec2(o.x + ((float)i * 6.0f + (float)x + 1.0f) * d - 1.0f, o.y + ((float)y + 1.0f) * d - 1.0f), kLedOn);
	}
	char line[64];
	snprintf(line, sizeof line, "emu \xC2\xB7 v%s unofficial", EMU_RENDER_VERSION);
	ImGui::PushFont(NULL, S(11.0f));
	dl->AddText(ImVec2(o.x + S(4.0f), o.y + S(kHeaderH - 17.0f)), kDim, line);
	ImGui::PopFont();
}

/* the endless dial: one notch mark per 15 degrees, turned by the detents it has been given */
void drawEncoder(ImVec2 c, float r, int detents, bool hot)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddCircleFilled(c, r, hot ? IM_COL32(54, 68, 84, 255) : kDial, 40);
	dl->AddCircle(c, r, kRowLine, 40, S(1.5f));
	const float a0 = (float)detents * 0.2617994f;	/* 15 degrees a detent */
	for (int i = 0; i < 12; i++)
	{
		const float a = a0 + (float)i * 0.5235988f;
		const ImVec2 p0(c.x + sinf(a) * (r - S(7.0f)), c.y - cosf(a) * (r - S(7.0f)));
		const ImVec2 p1(c.x + sinf(a) * (r - S(2.0f)), c.y - cosf(a) * (r - S(2.0f)));
		dl->AddLine(p0, p1, i == 0 ? kLedOn : kDialLine, i == 0 ? S(2.5f) : 1.0f);
	}
}

void encoder(panel_t *p, app_shell_t *s, ImVec2 c, float r)
{
	ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
	ImGui::PushID("encoder");
	ImGui::InvisibleButton("dial", ImVec2(2.0f * r, 2.0f * r));
	ImGuiIO &io = ImGui::GetIO();
	if (ImGui::IsItemActivated())
		p->encDragged = 0.0f;
	if (ImGui::IsItemActive())
	{
		p->encRest -= io.MouseDelta.y / 12.0f;	/* one detent per 12 points; up is clockwise */
		p->encDragged += fabsf(io.MouseDelta.x) + fabsf(io.MouseDelta.y);
	}
	if (ImGui::IsItemHovered())
		p->encRest += io.MouseWheel;	/* one detent per notch */
	const int detents = (int)p->encRest;	/* toward 0: the rest stays for the next frame */
	if (detents != 0 && post(s, APP_P_ENCODER, 0u, detents) == 0)
	{
		p->encRest -= (float)detents;
		p->encAngle += detents;
	}
	if (ImGui::IsItemDeactivated() && ImGui::IsItemHovered() && p->encDragged < 3.0f)
		(void)post(s, APP_P_PUSH, 0u, 50);	/* a click without a drag pushes it for 50 ms (spec 4.6) */
	drawEncoder(c, r, p->encAngle, ImGui::IsItemHovered());
	ImGui::PopID();
}

/* --- knobs, faders ----------------------------------------------------------------------------------------- */

void drawKnob(ImVec2 c, float r, int degrees, bool hot)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddCircleFilled(c, r, hot ? IM_COL32(54, 68, 84, 255) : kDial, 36);
	dl->AddCircle(c, r, kRowLine, 36, S(1.5f));
	const float a = (float)degrees * 0.0174533f;	/* clockwise from the top */
	dl->AddLine(ImVec2(c.x + sinf(a) * S(5.0f), c.y - cosf(a) * S(5.0f)),
		ImVec2(c.x + sinf(a) * (r - S(3.0f)), c.y - cosf(a) * (r - S(3.0f))), kLedOn, S(3.0f));
}

/* a knob: 1 degree per point of vertical drag, 5 per wheel notch; whole degrees go out (spec 6.6) */
void knob(panel_t *p, app_shell_t *s, unsigned k, ImVec2 c, float r)
{
	ImGui::SetCursorScreenPos(ImVec2(c.x - r, c.y - r));
	ImGui::PushID(100 + (int)k);
	ImGui::InvisibleButton("knob", ImVec2(2.0f * r, 2.0f * r));
	ImGuiIO &io = ImGui::GetIO();
	if (ImGui::IsItemActive())
		p->knobRest[k] -= io.MouseDelta.y;	/* up is clockwise */
	if (ImGui::IsItemHovered())
		p->knobRest[k] += 5.0f * io.MouseWheel;
	const int deg = (int)p->knobRest[k];	/* toward 0: the rest stays for the next frame */
	if (deg != 0 && post(s, APP_P_KNOB, (uint8_t)(k + 1u), deg) == 0)
	{
		p->knobRest[k] -= (float)deg;
		p->knobAngle[k] = ((p->knobAngle[k] + deg) % 360 + 360) % 360;
	}
	drawKnob(c, r, p->knobAngle[k], ImGui::IsItemHovered() || ImGui::IsItemActive());
	ImGui::PopID();
}

/* a fader: 0-1, a double click returns it to full (spec 6.6). What the firmware has been sent is kept apart, so that a
 * full panel ring only delays a level. */
void fader(panel_t *p, app_shell_t *s, unsigned i, ImVec2 pos)
{
	ImGui::SetCursorScreenPos(pos);
	ImGui::PushID(200 + (int)i);
	ImGui::VSliderFloat("##fader", ImVec2(S(kFaderW), S(kFaderH)), &p->fader[i], 0.0f, 1.0f, "", ImGuiSliderFlags_NoInput);
	if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		p->faderHold[i] = 1u;
	if (p->faderHold[i])	/* the second press of a double click is still dragging the slider: full until it is let go */
	{
		p->fader[i] = 1.0f;
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
			p->faderHold[i] = 0u;
	}
	if (p->fader[i] != p->faderSent[i] && post(s, APP_P_FADER, (uint8_t)(i + 1u), 0, (double)p->fader[i]) == 0)
		p->faderSent[i] = p->fader[i];
	ImGui::PopID();
}

/* --- buttons ------------------------------------------------------------------------------------------------ */

enum { GLYPH_TEXT, GLYPH_LEFT, GLYPH_RIGHT };

void drawKey(ImVec2 a, ImVec2 b, const char *text, int glyph, float font, bool down, bool hot, bool led, bool latched)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(a, b, down ? kKeyDown : hot ? kKeyHot : kKey, S(5.0f));
	dl->AddRect(a, b, latched ? kLedOn : kRowLine, S(5.0f), 0, latched ? S(2.0f) : 1.0f);
	const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
	if (glyph == GLYPH_TEXT)
	{
		ImGui::PushFont(NULL, S(font));
		const ImVec2 sz = ImGui::CalcTextSize(text);
		dl->AddText(ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f + S(3.0f)), kText, text);
		ImGui::PopFont();
	}
	else	/* the bar buttons: an arrow that points the way, drawn, so that it needs nothing of the font */
	{
		const float d = glyph == GLYPH_LEFT ? -1.0f : 1.0f, h = S(8.0f);
		dl->AddTriangleFilled(ImVec2(c.x + d * h, c.y + S(3.0f)), ImVec2(c.x - d * h, c.y + S(3.0f) - h),
			ImVec2(c.x - d * h, c.y + S(3.0f) + h), kText);
	}
	dl->AddCircleFilled(ImVec2(b.x - S(9.0f), a.y + S(9.0f)), S(3.5f), led ? kLedOn : kLedOff, 12);
}

/* a button: mouse down and up; a secondary click latches it down until the next click; the Shift key holds SHIFT and the
 * space bar presses START/STOP (spec 6.6) */
void button(panel_t *p, app_shell_t *s, unsigned n, ImVec2 pos, ImVec2 size, uint64_t leds, const char *text, int glyph,
	float font)
{
	ImGui::SetCursorScreenPos(pos);
	ImGui::PushID((int)n);
	ImGui::InvisibleButton(panel_names[n], size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
	const bool left = ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left);
	const bool right = ImGui::IsItemClicked(ImGuiMouseButton_Right);
	if (p->latched[n] && (left || right))
		p->latched[n] = 0;	/* the next click releases the latch */
	else if (right)
		p->latched[n] = 1;
	p->mouse[n] = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
	const bool keys = !p->modalOpen && !ImGui::GetIO().WantTextInput;
	const bool key = keys && ((n == PANEL_BUTTON_SHIFT && (ImGui::IsKeyDown(ImGuiKey_LeftShift) || ImGui::IsKeyDown(ImGuiKey_RightShift)))
		|| (n == PANEL_BUTTON_START && ImGui::IsKeyDown(ImGuiKey_Space)));
	const bool down = p->mouse[n] || p->latched[n] || key;
	if (down != (p->down[n] != 0u) && post(s, APP_P_BUTTON, (uint8_t)n, down ? 1 : 0) == 0)
		p->down[n] = down ? 1u : 0u;	/* a full ring leaves it for the next frame: a button is never left held */
	drawKey(pos, ImVec2(pos.x + size.x, pos.y + size.y), text, glyph, font, p->down[n] != 0u, ImGui::IsItemHovered(),
		panel_ledOn(leds, n), p->latched[n] != 0u);
	ImGui::PopID();
}

/* the 9 buttons under the faders: the bar arrows with their label between them, then the 7 voices (E33) */
void barAndVoices(panel_t *p, app_shell_t *s, uint64_t leds)
{
	const panel_group_t &g = panel_groups[PANEL_GROUP_BAR_VOICES];
	const float arrowW = 34.0f, barW = 30.0f;
	const float voiceW = (kRightW - 2.0f * arrowW - barW - 7.0f * kKeyGap) / 7.0f;
	float x = kRightX;
	for (unsigned i = 0; i < g.count; i++)
	{
		const unsigned n = g.button[i];
		if (i < 2u)	/* the arrows: bar- points left, bar+ right */
		{
			button(p, s, n, P(x, kVoiceY), ImVec2(S(arrowW), S(kVoiceH)), leds, panel_names[n], i == 0u ? GLYPH_LEFT : GLYPH_RIGHT, 11.0f);
			x += arrowW;
			if (i == 0u)
			{
				label(P(x + barW * 0.5f, kVoiceY + kVoiceH * 0.5f - 6.0f).x, P(0.0f, kVoiceY + kVoiceH * 0.5f - 6.0f).y, "bar", barW);
				x += barW;
			}
			else
				x += kKeyGap;
		}
		else
		{
			button(p, s, n, P(x, kVoiceY), ImVec2(S(voiceW), S(kVoiceH)), leds, panel_names[n], GLYPH_TEXT, 11.0f);
			x += voiceW + kKeyGap;
		}
	}
}

/* the 16 buttons above the steps, in three framed groups: the transport, the modes, the pages (E33) */
void buttonGroups(panel_t *p, app_shell_t *s, float y, uint64_t leds)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	float x = 0.0f;
	for (unsigned gi = PANEL_GROUP_TRANSPORT; gi <= PANEL_GROUP_PAGES; gi++)
	{
		const panel_group_t &g = panel_groups[gi];
		const float w = (float)g.count * kKeyW + (float)(g.count - 1u) * kKeyGap + 2.0f * kFramePad;
		dl->AddRectFilled(P(x, y), P(x + w, y + kKeyH + 2.0f * kFramePad), kRow, S(6.0f));
		dl->AddRect(P(x, y), P(x + w, y + kKeyH + 2.0f * kFramePad), kRowLine, S(6.0f));
		for (unsigned i = 0; i < g.count; i++)
			button(p, s, g.button[i], P(x + kFramePad + (float)i * (kKeyW + kKeyGap), y + kFramePad), ImVec2(S(kKeyW), S(kKeyH)),
				leds, panel_names[g.button[i]], GLYPH_TEXT, 11.0f);
		x += w + kGroupGap;
	}
}

/* the 16 steps, larger, with what SHIFT and each of them do together written under it */
void steps(panel_t *p, app_shell_t *s, float y, uint64_t leds)
{
	const panel_group_t &g = panel_groups[PANEL_GROUP_STEPS];
	const float w = (kRefW - 15.0f * kStepGap) / 16.0f;
	for (unsigned i = 0; i < g.count; i++)
	{
		const float x = (float)i * (w + kStepGap);
		char number[8];
		snprintf(number, sizeof number, "%u", i + 1u);
		button(p, s, g.button[i], P(x, y), ImVec2(S(w), S(kStepH)), leds, number, GLYPH_TEXT, 15.0f);
		label(P(x + w * 0.5f, 0.0f).x, P(0.0f, y + kStepH + 3.0f).y, panel_shiftNames[i], w + 2.0f);
	}
}

/* --- the jack strip -------------------------------------------------------------------------------------------- */

void sendGenerator(panel_t *p, app_shell_t *s)
{
	app_cmd_t c;
	memset(&c, 0, sizeof c);
	c.kind = APP_CMD_GENERATOR;
	c.a = p->genOn ? 1 : 0;
	c.x = (double)p->genBpm;
	c.b = p->genPpq;
	(void)app_shellCommand(s, &c);
}

/* a name over a control in the jack strip */
void jackLabel(float x, const char *text)
{
	ImGui::PushFont(NULL, S(11.0f));
	ImGui::GetWindowDrawList()->AddText(P(x, 8.0f), kDim, text);
	ImGui::PopFont();
}

/* the jacks along the top: clock in with its pulse generator, clock out, reset in, the card, the output jacks (spec 6.6) */
void jackStrip(panel_t *p, app_shell_t *s, const app_snapshot_t *snap, double now)
{
	ImDrawList *dl = ImGui::GetWindowDrawList();
	dl->AddRectFilled(P(0.0f, 0.0f), P(kRefW, kStripH), kRow, S(6.0f));
	const float cy = 30.0f;	/* the controls' line */

	/* clock in: one pulse per click, stamped with the click's host time (E13); the generator beside it */
	jackLabel(12.0f, "clk in");
	ImGui::SetCursorScreenPos(P(12.0f, cy));
	ImGui::Button("pulse", ImVec2(S(64.0f), S(26.0f)));
	if (ImGui::IsItemActivated())
		(void)post(s, APP_P_CLOCK, 0u, 1, 0.0, mach_absolute_time());
	ImGui::SetCursorScreenPos(P(88.0f, cy));
	bool on = p->genOn != 0u;
	bool changed = ImGui::Checkbox("gen", &on);
	p->genOn = on ? 1u : 0u;
	ImGui::SameLine(0.0f, S(8.0f));
	ImGui::SetNextItemWidth(S(84.0f));
	changed |= ImGui::DragFloat("##bpm", &p->genBpm, 0.5f, 1.0f, (float)kBpmMax, "%.1f BPM", ImGuiSliderFlags_AlwaysClamp);
	ImGui::SameLine(0.0f, S(8.0f));
	changed |= ImGui::RadioButton("4", &p->genPpq, 4);
	ImGui::SameLine(0.0f, S(6.0f));
	changed |= ImGui::RadioButton("24", &p->genPpq, 24);
	ImGui::SameLine(0.0f, S(4.0f));
	ImGui::TextDisabled("PPQ");
	if (changed)
		sendGenerator(p, s);

	/* clock out: the LED, latched, so that a pulse of 5 ms stays visible for a few frames */
	jackLabel(402.0f, "clk out");
	if (snap && snap->clockOut)
		p->clockOutAt = now;
	dl->AddCircleFilled(P(416.0f, cy + 13.0f), S(7.0f), now - p->clockOutAt < 0.08 ? kLedOn : kLedOff, 16);

	/* reset in, the card switch */
	jackLabel(480.0f, "rst in");
	ImGui::SetCursorScreenPos(P(484.0f, cy));
	bool reset = p->resetOn != 0u;
	if (ImGui::Checkbox("##rst", &reset))
		p->resetOn = reset ? 1u : 0u;
	if (p->resetOn != p->resetSent && post(s, APP_P_RESET, 0u, p->resetOn, 0.0, mach_absolute_time()) == 0)
		p->resetSent = p->resetOn;
	jackLabel(560.0f, "sd card");
	ImGui::SetCursorScreenPos(P(566.0f, cy));
	bool card = p->cardIn != 0u;
	if (ImGui::Checkbox("##card", &card))
		p->cardIn = card ? 1u : 0u;
	if (p->cardIn != p->cardSent && post(s, APP_P_CARD, 0u, p->cardIn) == 0)
		p->cardSent = p->cardIn;

	/* the output jacks: checked is plugged (spec 4.6) */
	static const float jackX[4] = { 650.0f, 740.0f, 830.0f, 920.0f };
	for (unsigned j = 0; j < 4u; j++)
	{
		jackLabel(jackX[j], panel_jackNames[j]);
		ImGui::SetCursorScreenPos(P(jackX[j] + 4.0f, cy));
		ImGui::PushID(300 + (int)j);
		bool plugged = (p->jacks & panel_jackBits[j]) != 0u;
		if (ImGui::Checkbox("##jack", &plugged))
			p->jacks = (uint8_t)((p->jacks & ~panel_jackBits[j]) | (plugged ? panel_jackBits[j] : 0u));
		ImGui::PopID();
	}
	if (p->jacks != p->jacksSent && post(s, APP_P_JACKS, 0u, p->jacks) == 0)
		p->jacksSent = p->jacks;
}

/* --- the windows from the menu bar -------------------------------------------------------------------------- */

bool windowBegin(const char *title, int *open, ImVec2 size, ImVec2 pos)
{
	if (!*open)
		return false;
	bool o = true;
	ImGui::SetNextWindowPos(pos, ImGuiCond_FirstUseEver);	/* each window its own place, not all on top of one another */
	ImGui::SetNextWindowSize(size, ImGuiCond_FirstUseEver);
	const bool visible = ImGui::Begin(title, &o, ImGuiWindowFlags_NoSavedSettings);
	*open = o ? 1 : 0;
	if (!visible)
		ImGui::End();
	return visible;
}

void kv(const char *key, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
void kv(const char *key, const char *fmt, ...)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::TextColored(ImVec4(0.55f, 0.6f, 0.66f, 1.0f), "%s", key);
	ImGui::TableSetColumnIndex(1);
	va_list ap;
	va_start(ap, fmt);
	ImGui::TextV(fmt, ap);
	va_end(ap);
}

void section(const char *title)
{
	ImGui::TableNextRow();
	ImGui::TableSetColumnIndex(0);
	ImGui::Spacing();
	ImGui::TextColored(ImVec4(0.46f, 0.88f, 0.80f, 1.0f), "%s", title);
}

#define U64(x) ((unsigned long long)(x))

/* every field of app_diagnostics_t (spec 6.9), and the card warnings (spec 11) */
void diagnostics(panel_t *p, app_shell_t *s, const app_snapshot_t *snap)
{
	if (!windowBegin("Diagnostics", &p->showDiag, ImVec2(560.0f, 640.0f), ImVec2(520.0f, 40.0f)))
		return;
	if (!snap)
		ImGui::TextUnformatted("No snapshot yet.");
	else if (ImGui::BeginTable("diag", 2, ImGuiTableFlags_RowBg))
	{
		const app_diagnostics_t &g = snap->diag;
		const emu_counters_t &c = g.core;
		const app_fwCounters_t &w = g.fw;
		ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthFixed, 200.0f);
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);
		section("output");
		kv("device", "%s", g.outputRunning && g.output.name[0] ? g.output.name : "none");
		kv("formats and converter", "%s", g.output.format[0] ? g.output.format : "-");
		kv("rate (device, client)", "%.1f Hz, %.1f Hz", g.output.deviceRate, g.output.clientRate);
		kv("buffer, channels", "%u frames, %u", g.output.bufferFrames, g.output.channels);
		kv("latency (device, stream, safety)", "%u, %u, %u frames", g.output.deviceLatency, g.output.streamLatency,
			g.output.safetyOffset);
		kv("firmware thread", "%s", g.realtime ? "time-constraint policy" : "default priority (the system refused the policy)");
		section("timing");
		kv("block rate", "%.2f blocks/s (689.7 expected)", g.blockRate);
		kv("load", "%.1f %% of real time", g.load * 100.0);
		kv("output underruns", "%llu", U64(g.outUnderruns));
		kv("callbacks, overloads, map jumps", "%llu, %llu, %llu", U64(g.callbacks), U64(g.overloads), U64(g.mapJumps));
		kv("L (output latency)", "%.2f ms", g.L * 1e3);
		kv("D (slave delay)", "%.2f ms, %s", g.D * 1e3, g.dAuto ? "automatic" : "set by hand");
		kv("effective D", g.Deff > 0.0 ? "%.2f ms" : "none: internal sync", g.Deff * 1e3);
		kv("sync", "%s", g.extSync == 1 ? "jack" : g.extSync == 2 ? "MIDI" : "internal");
		if (g.leadMin <= g.leadMax)
			kv("lead of drained events", "%.2f to %.2f ms", g.leadMin * 1e3, g.leadMax * 1e3);
		else
			kv("lead of drained events", "none yet");
		if (g.inLatMin <= g.inLatMax)
			kv("input latency (last 2 s)", "%.2f to %.2f ms", g.inLatMin * 1e3, g.inLatMax * 1e3);
		else
			kv("input latency (last 2 s)", "no input");
		section("counters (spec 5.5)");
		kv("blocks, underruns, passes", "%llu, %llu, %llu", U64(c.blocks), U64(c.underruns), U64(c.passes));
		kv("DIN in, lost, out", "%llu, %llu, %llu", U64(c.dinIn), U64(c.dinLost), U64(c.dinOut));
		kv("USB in, dropped, out", "%llu, %llu, %llu", U64(c.usbIn), U64(c.usbInDropped), U64(c.usbOut));
		kv("clock edges, late inputs", "%llu, %llu", U64(c.clockEdges), U64(c.late));
		kv("tick calls", "%llu", U64(c.tickCalls));
		kv("queue overflow, dropouts", "%u, %u", w.queueOverflow, w.dropouts);
		kv("sync ignored, intervals excluded", "%u, %u", w.syncIgnored, w.intervalsExcluded);
		kv("tempo restarts, sync overflows", "%u, %u", w.tempoRestarts, w.syncOverflows);
		kv("USB transmit dropped", "%u", w.usbTxDropped);
		section("rings and MIDI");
		kv("MIDI in dropped", "%llu", U64(g.midiInDropped));
		kv("MIDI out dropped, unsent", "%llu, %llu", U64(g.midiOutDropped), U64(g.midiUnsent));
		kv("MIDI stale, DIN orphans", "%llu, %llu", U64(g.midiStale), U64(g.dinOrphans));
		kv("panel ring dropped", "%llu", U64(g.panelDropped));
		kv("snapshot ring dropped", "%llu", U64(g.snapDropped));
		kv("acknowledgement ring dropped", "%llu", U64(g.ackDropped));
		kv("warnings", "%llu", U64(g.warnings));
		ImGui::EndTable();
	}
	const int n = app_shellWarnings(s);
	ImGui::Separator();
	ImGui::Text("Card warnings (%d)", n);
	for (int i = 0; i < n; i++)
		ImGui::TextWrapped("%s", app_shellWarning(s, i));
	ImGui::End();
}

void sendOutput(app_shell_t *s, const char *uid, int frames)
{
	app_cmd_t c;
	memset(&c, 0, sizeof c);
	c.kind = APP_CMD_OUTPUT;
	snprintf(c.text, sizeof c.text, "%s", uid);
	c.a = frames;
	(void)app_shellCommand(s, &c);
}

void audioWindow(panel_t *p, app_shell_t *s, const app_snapshot_t *snap, double now)
{
	if (!windowBegin("Audio", &p->showAudio, ImVec2(470.0f, 250.0f), ImVec2(30.0f, 60.0f)))
		return;
	const app_settings_t *set = app_shellSettings(s);
	if (now - p->devAt > 2.0)
	{
		p->nDev = app_shellDevices(s, p->dev, PANEL_DEVICES);
		p->devAt = now;
	}
	int cur = -1;
	for (int i = 0; i < p->nDev; i++)
		if (set->outputUid[0] != '\0' && strcmp(set->outputUid, p->dev[i].uid) == 0)
			cur = i;
	char shown[300];
	if (set->outputUid[0] == '\0')
		snprintf(shown, sizeof shown, "system default");
	else if (cur >= 0)
		snprintf(shown, sizeof shown, "%s", p->dev[cur].name);
	else
		snprintf(shown, sizeof shown, "%s (not available)", set->outputUid);
	if (ImGui::BeginCombo("output device", shown))
	{
		if (ImGui::Selectable("system default", set->outputUid[0] == '\0'))
			sendOutput(s, "", (int)set->bufferFrames);
		for (int i = 0; i < p->nDev; i++)
		{
			char item[300];
			snprintf(item, sizeof item, "%s (%u ch)##%d", p->dev[i].name, p->dev[i].channels, i);
			if (ImGui::Selectable(item, i == cur))
				sendOutput(s, p->dev[i].uid, (int)set->bufferFrames);
		}
		ImGui::EndCombo();
	}
	static const int sizes[] = { 0, 64, 128, 256, 512, 1024, 2048 };
	char buf[40];
	if (set->bufferFrames == 0u)
		snprintf(buf, sizeof buf, "the device's own");
	else
		snprintf(buf, sizeof buf, "%u frames", set->bufferFrames);
	if (ImGui::BeginCombo("buffer", buf))
	{
		for (unsigned i = 0; i < sizeof sizes / sizeof sizes[0]; i++)
		{
			char item[40];
			if (sizes[i] == 0)
				snprintf(item, sizeof item, "the device's own");
			else
				snprintf(item, sizeof item, "%d frames", sizes[i]);
			if (ImGui::Selectable(item, (int)set->bufferFrames == sizes[i]))
				sendOutput(s, set->outputUid, sizes[i]);
		}
		ImGui::EndCombo();
	}
	static const char *const mixNames[4] = { "sum", "out1", "out2", "four" };
	static const char *const mixLabels[4] = { "stereo sum of both outputs", "main out only", "out 2 only",
		"four channels (needs a device with four)" };
	int mix = 0;
	for (int k = 0; k < 4; k++)
		if (strcmp(set->mix, mixNames[k]) == 0)
			mix = k;
	ImGui::TextUnformatted("mix");
	for (int k = 0; k < 4; k++)
		if (ImGui::RadioButton(mixLabels[k], mix == k))
		{
			app_cmd_t c;
			memset(&c, 0, sizeof c);
			c.kind = APP_CMD_MIX;
			c.a = k;
			(void)app_shellCommand(s, &c);
		}
	ImGui::Separator();
	if (snap && snap->diag.outputRunning)
		ImGui::TextWrapped("Running on %s: %.0f Hz, %u frames per buffer.", snap->diag.output.name,
			snap->diag.output.deviceRate, snap->diag.output.bufferFrames);
	else
		ImGui::TextUnformatted("No audio output is open.");
	ImGui::End();
}

/* one DIN port: the stored binding (spec 6.9) shown as 'not connected' when unbound or not there */
void dinCombo(app_shell_t *s, const char *label, int source, const app_endpoint_t *list, int n, int32_t uid, const char *name)
{
	const int cur = app_bindPick(list, n, uid, name);
	const bool stored = uid != 0 || name[0] != '\0';
	char shown[200];
	if (cur >= 0)
		snprintf(shown, sizeof shown, "%s", list[cur].name);
	else if (stored)
		snprintf(shown, sizeof shown, "%s (not connected)", name[0] ? name : "stored port");
	else
		snprintf(shown, sizeof shown, "not connected");
	if (ImGui::BeginCombo(label, shown))
	{
		if (ImGui::Selectable("not connected", !stored))
			app_shellBind(s, source, 0, "");
		for (int i = 0; i < n; i++)
		{
			char item[200];
			snprintf(item, sizeof item, "%s##%d", list[i].name, i);
			if (ImGui::Selectable(item, i == cur))
				app_shellBind(s, source, list[i].uid, list[i].name);
		}
		ImGui::EndCombo();
	}
}

void midiWindow(panel_t *p, app_shell_t *s, const app_snapshot_t *snap, double now)
{
	if (!windowBegin("MIDI", &p->showMidi, ImVec2(500.0f, 300.0f), ImVec2(60.0f, 340.0f)))
		return;
	const app_settings_t *set = app_shellSettings(s);
	if (now - p->epAt > 1.0)
	{
		p->nSrc = app_shellEndpoints(s, 1, p->src, PANEL_ENDPOINTS);
		p->nDst = app_shellEndpoints(s, 0, p->dst, PANEL_ENDPOINTS);
		p->epAt = now;
	}
	ImGui::TextUnformatted("USB: the virtual source and destination \"LICHOMAT Emu\".");
	dinCombo(s, "DIN input", 1, p->src, p->nSrc, set->dinSrcUid, set->dinSrcName);
	dinCombo(s, "DIN output", 0, p->dst, p->nDst, set->dinDstUid, set->dinDstName);
	ImGui::Separator();
	bool automatic = set->dAuto != 0;
	bool send = false;
	if (ImGui::Checkbox("D automatic", &automatic))
	{
		if (!automatic)
			p->dMs = (float)set->dMs;
		send = true;
	}
	if (!automatic)
	{
		send |= ImGui::SliderFloat("D", &p->dMs, 0.0f, 200.0f, "%.1f ms", ImGuiSliderFlags_AlwaysClamp);
	}
	if (send)
	{
		app_cmd_t c;
		memset(&c, 0, sizeof c);
		c.kind = APP_CMD_DELAY;
		c.a = automatic ? 1 : 0;
		c.x = (double)p->dMs / 1e3;
		(void)app_shellCommand(s, &c);
	}
	if (snap)
	{
		const app_diagnostics_t &g = snap->diag;
		ImGui::Text("D now %.2f ms, L %.2f ms; effective D %s", g.D * 1e3, g.L * 1e3, g.Deff > 0.0 ? "" : "none (internal sync)");
		if (g.Deff > 0.0)
		{
			ImGui::SameLine();
			ImGui::Text("%.2f ms", g.Deff * 1e3);
		}
	}
	ImGui::End();
}

void messagesWindow(panel_t *p)
{
	if (!windowBegin("Messages", &p->showMessages, ImVec2(620.0f, 260.0f), ImVec2(100.0f, 450.0f)))
		return;
	const unsigned n = p->lines < PANEL_LINES ? p->lines : PANEL_LINES;
	for (unsigned i = 0; i < n; i++)
		ImGui::TextWrapped("%s", p->line[(p->lines - n + i) % PANEL_LINES]);
	if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
		ImGui::SetScrollHereY(1.0f);
	ImGui::End();
}

/* About (spec 10.1): one TextUnformatted() per notice */
void aboutWindow(panel_t *p)
{
	if (!windowBegin("About LICHOMAT Emu", &p->showAbout, ImVec2(760.0f, 360.0f), ImVec2(180.0f, 160.0f)))
		return;
	char line[300];
	snprintf(line, sizeof line, "LICHOMAT Emu %s", EMU_RENDER_VERSION);
	ImGui::TextUnformatted(line);
	ImGui::TextUnformatted(EMU_TAGLINE);
	snprintf(line, sizeof line, "Runs LICHOMAT %s with the emulator changes " EMU_FIRMWARE_CHANGES ". Built from commit %s.",
		EMU_RENDER_VERSION, EMU_RENDER_COMMIT);
	ImGui::TextUnformatted(line);
	ImGui::Spacing();
	for (size_t i = 0; i < EMU_LEGAL_COUNT; i++)
		ImGui::TextUnformatted(emu_legal[i]);
	ImGui::Spacing();
	ImGui::TextUnformatted("Dear ImGui: Copyright (c) 2014-2026 Omar Cornut");	/* the copyright line of its LICENSE.txt */
	ImGui::TextUnformatted("MIT licence.");
	ImGui::Spacing();
	ImGui::TextUnformatted("The emulator sounds very close to the device, not identical.");	/* risk 7 */
	ImGui::End();
}

/* --- popups --------------------------------------------------------------------------------------------------- */

void popupCentre(void)
{
	ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
}

/* the crash note of an earlier run: its text is read from the file in the app folder (spec 6.9) */
void loadCrashNote(panel_t *p, const app_shell_t *s)
{
	p->crashText[0] = '\0';
	if (strchr(p->crashName, '/') != NULL || p->crashName[0] == '\0')
		return;
	char path[1200];
	snprintf(path, sizeof path, "%s/%s", app_shellHome(s), p->crashName);
	FILE *f = fopen(path, "r");
	if (!f)
		return;
	const size_t n = fread(p->crashText, 1u, sizeof p->crashText - 1u, f);
	p->crashText[n] = '\0';
	fclose(f);
}

void popups(panel_t *p, app_shell_t *s)
{
	if (p->popupCrash)
	{
		loadCrashNote(p, s);
		ImGui::OpenPopup("Crash note");
		p->popupCrash = 0;
	}
	if (p->popupHang)
	{
		ImGui::OpenPopup("Stopped");
		p->popupHang = 0;
	}
	if (p->popupAsk)
	{
		ImGui::OpenPopup("Replace project");
		p->popupAsk = 0;
	}
	if (p->popupError)
	{
		ImGui::OpenPopup("Problem");
		p->popupError = 0;
	}
	if (p->popupProject)
	{
		ImGui::OpenPopup("Export project");
		p->popupProject = 0;
	}
	popupCentre();
	if (ImGui::BeginPopupModal("Crash note", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("The last run ended with a fault. The note is in the app folder:");
		ImGui::TextUnformatted(p->crashName);
		if (ImGui::BeginChild("note", ImVec2(680.0f, 240.0f), ImGuiChildFlags_Borders))
			ImGui::TextUnformatted(p->crashText[0] ? p->crashText : p->crashHead);
		ImGui::EndChild();
		if (ImGui::Button("OK", ImVec2(96.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	popupCentre();
	if (ImGui::BeginPopupModal("Stopped", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(p->hangText);
		if (ImGui::Button("Restart", ImVec2(96.0f, 0.0f)))
			app_shellRestart(s);	/* the settings are saved and the MIDI ports disposed of; no card out: the thread is stuck */
		ImGui::EndPopup();
	}
	popupCentre();
	if (ImGui::BeginPopupModal("Replace project", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(p->askText);
		if (ImGui::Button("Replace", ImVec2(96.0f, 0.0f)))
		{
			p->request = PANEL_REQ_REPLACE;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	popupCentre();
	if (ImGui::BeginPopupModal("Problem", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted(p->errorText);
		if (ImGui::Button("OK", ImVec2(96.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
	popupCentre();
	if (ImGui::BeginPopupModal("Export project", NULL, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Write one project of the card into a card folder that you pick.");
		ImGui::SetNextItemWidth(120.0f);
		ImGui::InputInt("project (0-63)", &p->projectNumber);
		if (p->projectNumber < 0)
			p->projectNumber = 0;
		if (p->projectNumber > 63)
			p->projectNumber = 63;
		if (ImGui::Button("Choose folder...", ImVec2(140.0f, 0.0f)))
		{
			p->request = PANEL_REQ_PROJECT;
			p->requestProject = p->projectNumber;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void menuBar(panel_t *p, app_shell_t *s)
{
	if (!ImGui::BeginMainMenuBar())
		return;
	if (ImGui::BeginMenu("Card"))
	{
		if (ImGui::MenuItem("Import card folder..."))
			p->request = PANEL_REQ_IMPORT;
		if (ImGui::MenuItem("Export card..."))
			p->request = PANEL_REQ_EXPORT;
		if (ImGui::MenuItem("Export project..."))
			p->popupProject = 1;
		ImGui::Separator();
		if (ImGui::MenuItem("Power cycle"))
			app_shellPowerCycle(s);
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Window"))
	{
		bool b = p->showAudio != 0;
		if (ImGui::MenuItem("Audio", NULL, &b))
			p->showAudio = b ? 1 : 0;
		b = p->showMidi != 0;
		if (ImGui::MenuItem("MIDI", NULL, &b))
			p->showMidi = b ? 1 : 0;
		b = p->showDiag != 0;
		if (ImGui::MenuItem("Diagnostics", NULL, &b))
			p->showDiag = b ? 1 : 0;
		b = p->showMessages != 0;
		if (ImGui::MenuItem("Messages", NULL, &b))
			p->showMessages = b ? 1 : 0;
		ImGui::EndMenu();
	}
	if (ImGui::BeginMenu("Help"))
	{
		if (ImGui::MenuItem("About LICHOMAT Emu"))
			p->showAbout = 1;
		ImGui::EndMenu();
	}
	ImGui::EndMainMenuBar();
}

/* --- the panel ----------------------------------------------------------------------------------------------- */

void mainWindow(panel_t *p, app_shell_t *s, const app_snapshot_t *snap, double now)
{
	const ImGuiViewport *vp = ImGui::GetMainViewport();
	const float margin = 10.0f, footer = 22.0f;
	/* the scale that fits the window, the panel centred across it; height to spare goes to the gaps between the rows */
	const float availW = vp->WorkSize.x - 2.0f * margin, availH = vp->WorkSize.y - 2.0f * margin - footer;
	gS = fminf(availW / kRefW, availH / kRefH);
	if (gS < 0.4f)
		gS = 0.4f;
	const float spare = fmaxf(0.0f, availH / gS - kRefH);
	const float gap = 12.0f + fminf(spare / 3.0f, 30.0f);
	gOrigin = ImVec2(vp->WorkPos.x + margin + (availW - kRefW * gS) * 0.5f, vp->WorkPos.y + margin);

	ImGui::SetNextWindowPos(vp->WorkPos);
	ImGui::SetNextWindowSize(vp->WorkSize);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin("panel", NULL, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
		| ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
	ImGui::PushFont(NULL, 15.0f * gS);

	/* the jacks along the top */
	jackStrip(p, s, snap, now);
	float y = kStripH + gap;

	/* the wordmark, the display and the encoder, labelled data, with the knobs under the display */
	wordmark(P(0.0f, y));
	drawLcd(snap->lcd, P(0.0f, y + kHeaderH));
	const ImVec2 dial = P(kEncoderX, y + kHeaderH + kLcdH * 0.5f - 8.0f);
	encoder(p, s, dial, S(kEncoderR));
	label(dial.x, dial.y + S(kEncoderR) + S(3.0f), "data", 60.0f);
	const float colW = (float)(16u * 6u) * kDot / (float)PANEL_KNOBS;	/* knob i edits the parameter in display column i */
	for (unsigned k = 0; k < PANEL_KNOBS; k++)
	{
		const float cx = kLcdPad + ((float)k + 0.5f) * colW;
		knob(p, s, k, P(cx, y + kHeaderH + kKnobY + kKnobR), S(kKnobR));
		char name[16];
		snprintf(name, sizeof name, "knob %u", k + 1u);
		label(P(cx, 0.0f).x, P(0.0f, y + kHeaderH + kKnobY + 2.0f * kKnobR + 3.0f).y, name, 80.0f);
	}

	/* the faders the firmware reads, in the places of the first six of seven; the seventh place stays free */
	const float pitch = kRightW / 7.0f;
	for (unsigned i = 0; i < PANEL_FADERS; i++)
	{
		const float cx = kRightX + ((float)i + 0.5f) * pitch;
		fader(p, s, i, P(cx - kFaderW * 0.5f, y));
		label(P(cx, 0.0f).x, P(0.0f, y + kFaderH + 3.0f).y, panel_faderNames[i], pitch);
	}
	/* the arrows and the voices under them: the same y, shifted by the upper body's origin */
	gOrigin.y += S(y);
	barAndVoices(p, s, snap->leds);
	gOrigin.y -= S(y);
	y += kUpperH + gap;

	buttonGroups(p, s, y, snap->leds);
	y += kKeyH + 2.0f * kFramePad + gap;
	steps(p, s, y, snap->leds);

	ImGui::PopFont();
	/* the newest line the shell said, at the foot of the window */
	ImGui::SetCursorScreenPos(ImVec2(vp->WorkPos.x + margin + 2.0f, vp->WorkPos.y + vp->WorkSize.y - footer));
	if (p->lines > 0u)
	{
		char text[PANEL_LINE];
		firstLine(p->line[(p->lines - 1u) % PANEL_LINES], text, sizeof text);
		const bool bad = starts(text, "error:") || starts(text, "warning:") || starts(text, "hang:");
		ImGui::PushStyleColor(ImGuiCol_Text, bad ? ImVec4(1.0f, 0.69f, 0.0f, 1.0f) : ImVec4(0.5f, 0.56f, 0.63f, 1.0f));
		ImGui::TextUnformatted(text);
		ImGui::PopStyleColor();
	}
	else
		ImGui::Dummy(ImVec2(1.0f, 1.0f));	/* an item after the last SetCursor, or ImGui asserts */
	ImGui::End();
	ImGui::PopStyleVar();
}

}	// namespace

void panel_init(panel_t *p)
{
	memset(p, 0, sizeof *p);
	for (unsigned i = 0; i < PANEL_FADERS; i++)
		p->fader[i] = p->faderSent[i] = 1.0f;
	p->jacks = p->jacksSent = EMU_JACKS_ALL;
	p->cardIn = p->cardSent = 1u;
	p->genBpm = 120.0f;
	p->genPpq = 24;
	p->projectNumber = 0;
}

void panel_message(panel_t *p, const char *text)
{
	snprintf(p->line[p->lines % PANEL_LINES], PANEL_LINE, "%s", text);
	p->lines++;
	p->lineAt = wall();
	if (starts(text, "crash note: "))
	{
		char head[PANEL_LINE];
		firstLine(text + strlen("crash note: "), p->crashName, sizeof p->crashName);
		const char *nl = strchr(text, '\n');
		snprintf(head, sizeof head, "%s", nl ? nl + 1 : text);
		snprintf(p->crashHead, sizeof p->crashHead, "%s", head);
		p->popupCrash = 1;
	}
	else if (starts(text, "hang:"))
	{
		snprintf(p->hangText, sizeof p->hangText, "%s", text);
		p->popupHang = 1;
	}
	else if (starts(text, "ask:"))
	{
		snprintf(p->askText, sizeof p->askText, "%s", text + strlen("ask: "));
		p->popupAsk = 1;
	}
	else if (starts(text, "error:"))
	{
		snprintf(p->errorText, sizeof p->errorText, "%s", text + strlen("error: "));
		p->popupError = 1;
	}
}

void panel_frame(panel_t *p, app_shell_t *s)
{
	const double now = wall();
	if (!p->started)
	{
		const app_settings_t *set = app_shellSettings(s);
		applyStyle();
		p->dMs = (float)set->dMs;
		if (set->genBpm >= 1.0 && set->genBpm <= kBpmMax)
			p->genBpm = (float)set->genBpm;
		if (set->genPpq == 4 || set->genPpq == 24)
			p->genPpq = set->genPpq;
		p->started = 1;
	}
	const app_snapshot_t *snap = app_shellSnapshot(s);
	menuBar(p, s);
	if (snap)	/* nothing but the menu bar before the first snapshot */
		mainWindow(p, s, snap, now);
	audioWindow(p, s, snap, now);
	midiWindow(p, s, snap, now);
	diagnostics(p, s, snap);
	messagesWindow(p);
	aboutWindow(p);
	popups(p, s);
	p->modalOpen = ImGui::IsPopupOpen(NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) ? 1 : 0;
}

int panel_request(panel_t *p, int *project)
{
	const int r = p->request;
	if (project)
		*project = p->requestProject;
	p->request = PANEL_REQ_NONE;
	return r;
}
