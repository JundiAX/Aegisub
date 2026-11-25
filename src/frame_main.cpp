// Copyright (c) 2005, Rodrigo Braz Monteiro, Niels Martin Hansen
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
//   * Redistributions of source code must retain the above copyright notice,
//     this list of conditions and the following disclaimer.
//   * Redistributions in binary form must reproduce the above copyright notice,
//     this list of conditions and the following disclaimer in the documentation
//     and/or other materials provided with the distribution.
//   * Neither the name of the Aegisub Group nor the names of its contributors
//     may be used to endorse or promote products derived from this software
//     without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
//
// Aegisub Project http://www.aegisub.org/

/// @file frame_main.cpp
/// @brief Main window creation and control management
/// @ingroup main_ui

#include "frame_main.h"

#include "include/aegisub/context.h"
#include "include/aegisub/menu.h"
#include "include/aegisub/toolbar.h"
#include "include/aegisub/hotkey.h"

#include "ass_file.h"
#include "async_video_provider.h"
#include "audio_controller.h"
#include "audio_box.h"
#include "base_grid.h"
#include "compat.h"
#include "command/command.h"
#include "dialog_detached_video.h"
#include "dialog_manager.h"
#include "libresrc/libresrc.h"
#include "main.h"
#include "options.h"
#include "project.h"
#include "subs_controller.h"
#include "subs_edit_box.h"
#include "utils.h"
#include "version.h"
#include "video_box.h"
#include "video_controller.h"
#include "video_display.h"

#include <libaegisub/dispatch.h>
#include <libaegisub/log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

#include <wx/brush.h>
#include <wx/dcclient.h>
#include <wx/dnd.h>
#include <wx/msgdlg.h>
#include <wx/panel.h>
#include <wx/pen.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statline.h>
#include <wx/sysopt.h>
#include <wx/toolbar.h>

enum {
	ID_APP_TIMER_STATUSCLEAR = 12002
};

#ifdef WITH_STARTUPLOG
#define StartupLog(a) MessageBox(0, a, "Aegisub startup log", 0)
#else
#define StartupLog(a) LOG_I("frame_main/init") << a
#endif

namespace {

enum class GridPos { Bottom, Top, Left, Right };
enum class PanelKind { Video, Audio, Edit };

GridPos parse_grid_pos(std::string const& s) {
	if (s == "top") return GridPos::Top;
	if (s == "left") return GridPos::Left;
	if (s == "right") return GridPos::Right;
	return GridPos::Bottom;
}

std::array<PanelKind, 3> parse_column_order(std::string const& s) {
	std::vector<PanelKind> order;
	auto add_unique = [&](PanelKind kind) {
		if (std::find(order.begin(), order.end(), kind) == order.end())
			order.push_back(kind);
	};

	size_t start = 0;
	while (start <= s.size()) {
		size_t comma = s.find(',', start);
		std::string token = s.substr(start, comma == std::string::npos ? std::string::npos : comma - start);
		if (token == "video") add_unique(PanelKind::Video);
		else if (token == "audio") add_unique(PanelKind::Audio);
		else if (token == "edit") add_unique(PanelKind::Edit);
		if (comma == std::string::npos) break;
		start = comma + 1;
	}

	add_unique(PanelKind::Video);
	add_unique(PanelKind::Audio);
	add_unique(PanelKind::Edit);
	return { order[0], order[1], order[2] };
}

int clamp_split(int total, int ratio, int minA, int minB) {
	if (total <= 0) return 0;
	if (minA + minB >= total) {
		if (minA + minB == 0) return total * ratio / 100;
		return static_cast<int>(std::lround(static_cast<double>(total) * minA / (minA + minB)));
	}
	int target = static_cast<int>(std::lround(total * (ratio / 100.0)));
	return std::clamp(target, minA, total - minB);
}

class LayoutSash final : public wxWindow {
	bool horizontal = false;
	bool dragging = false;
	wxPoint dragStartScreen;
	int lastDelta = 0;
	std::function<void()> onBeginDrag;
	std::function<void(int, bool)> onDrag;

public:
	LayoutSash(wxWindow *parent, std::function<void()> beginCb, std::function<void(int, bool)> dragCb)
	: wxWindow(parent, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxFULL_REPAINT_ON_RESIZE)
	, onBeginDrag(std::move(beginCb))
	, onDrag(std::move(dragCb))
	{
		SetHorizontal(false);
		Bind(wxEVT_PAINT, [this](wxPaintEvent&) {
			wxPaintDC dc(this);
			wxSize sz = GetClientSize();
			dc.SetBrush(wxBrush(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNFACE)));
			dc.SetPen(*wxTRANSPARENT_PEN);
			dc.DrawRectangle(0, 0, sz.GetWidth(), sz.GetHeight());
			dc.SetPen(wxPen(wxSystemSettings::GetColour(wxSYS_COLOUR_BTNSHADOW)));
			if (horizontal) {
				int y = sz.GetHeight() / 2;
				dc.DrawLine(0, y, sz.GetWidth(), y);
			}
			else {
				int x = sz.GetWidth() / 2;
				dc.DrawLine(x, 0, x, sz.GetHeight());
			}
		});
		Bind(wxEVT_LEFT_DOWN, [this](wxMouseEvent&) {
			dragging = true;
			lastDelta = 0;
			dragStartScreen = wxGetMousePosition();
			if (onBeginDrag) onBeginDrag();
			CaptureMouse();
		});
		Bind(wxEVT_MOTION, [this](wxMouseEvent&) {
			if (!dragging || !HasCapture()) return;
			wxPoint cur = wxGetMousePosition();
			int delta = horizontal ? (cur.y - dragStartScreen.y) : (cur.x - dragStartScreen.x);
			lastDelta = delta;
			if (onDrag) onDrag(delta, false);
		});
		Bind(wxEVT_LEFT_UP, [this](wxMouseEvent&) {
			if (!dragging) return;
			wxPoint cur = wxGetMousePosition();
			int delta = horizontal ? (cur.y - dragStartScreen.y) : (cur.x - dragStartScreen.x);
			lastDelta = delta;
			dragging = false;
			if (HasCapture()) ReleaseMouse();
			if (onDrag) onDrag(delta, true);
		});
		Bind(wxEVT_MOUSE_CAPTURE_LOST, [this](wxMouseCaptureLostEvent&) {
			if (!dragging) return;
			dragging = false;
			if (onDrag) onDrag(lastDelta, true);
		});
	}

	void SetHorizontal(bool horiz) {
		horizontal = horiz;
		SetCursor(wxCursor(horizontal ? wxCURSOR_SIZENS : wxCURSOR_SIZEWE));
		Refresh();
	}
};

} // namespace

class MainLayoutPanel final : public wxPanel {
	FrameMain *frame = nullptr;
	BaseGrid *grid = nullptr;
	VideoBox *video = nullptr;
	AudioBox *audio = nullptr;
	SubsEditBox *edit = nullptr;
	wxStaticLine *separatorLine = nullptr;
	LayoutSash *mainSash = nullptr;
	LayoutSash *videoSash = nullptr;

	GridPos gridPos = GridPos::Bottom;
	bool videoRight = false;
	bool swapAudioEdit = false;
	std::array<PanelKind, 3> columnOrder = { PanelKind::Video, PanelKind::Audio, PanelKind::Edit };
	bool fitVideo = false;
	int gridRatio = 50;
	int videoRatio = 45;

	int dragStartGridRatio = 50;
	int dragStartVideoRatio = 45;
	bool videoSashBeforeVideo = false;
	int lastMainSpan = 1;
	int lastVideoSpan = 1;

	void LayoutTools(int tx, int ty, int tw, int th, bool showAudio, int audioMinH, int editMinH) {
		int pad = FromDIP(5);
		int ix = tx + pad;
		int iw = std::max(0, tw - 2 * pad);
		int iy = ty;
		int ih = std::max(0, th - pad);

		if (showAudio) {
			int ah = std::min(audioMinH, std::max(FromDIP(70), ih - editMinH));
			int eh = std::max(0, ih - ah);
			if (!swapAudioEdit) {
				audio->SetSize(ix, iy, iw, ah);
				edit->SetSize(ix, iy + ah, iw, eh);
			}
			else {
				edit->SetSize(ix, iy, iw, eh);
				audio->SetSize(ix, iy + eh, iw, ah);
			}
		}
		else {
			edit->SetSize(ix, iy, iw, ih);
		}
	}

public:
	MainLayoutPanel(FrameMain *frame)
	: wxPanel(frame, -1, wxDefaultPosition, wxDefaultSize, wxTAB_TRAVERSAL | wxCLIP_CHILDREN)
	, frame(frame)
	{
		mainSash = new LayoutSash(
			this,
			[this] { dragStartGridRatio = gridRatio; },
			[this](int delta, bool commit) {
				int sign = (gridPos == GridPos::Left || gridPos == GridPos::Top) ? 1 : -1;
				int newRatio = std::clamp(
					dragStartGridRatio + static_cast<int>(std::lround(sign * delta * 100.0 / std::max(1, lastMainSpan))),
					15, 85);
				if (newRatio != gridRatio) {
					gridRatio = newRatio;
					Layout();
					Update();
				}
				if (commit && OPT_GET("App/Layout/Grid Ratio")->GetInt() != gridRatio)
					OPT_SET("App/Layout/Grid Ratio")->SetInt(gridRatio);
			});

		videoSash = new LayoutSash(
			this,
			[this] { dragStartVideoRatio = videoRatio; },
			[this](int delta, bool commit) {
				int sign = 1;
				if (gridPos == GridPos::Left || gridPos == GridPos::Right)
					sign = videoSashBeforeVideo ? -1 : 1;
				else
					sign = videoRight ? -1 : 1;

				int newRatio = std::clamp(
					dragStartVideoRatio + static_cast<int>(std::lround(sign * delta * 100.0 / std::max(1, lastVideoSpan))),
					15, 85);
				if (newRatio != videoRatio) {
					videoRatio = newRatio;
					Layout();
					Update();
				}
				if (commit && OPT_GET("App/Layout/Video Ratio")->GetInt() != videoRatio)
					OPT_SET("App/Layout/Video Ratio")->SetInt(videoRatio);
			});

		Bind(wxEVT_SIZE, [this](wxSizeEvent &evt) {
			Layout();
			evt.Skip();
		});
	}

	void SetPanes(BaseGrid *g, VideoBox *v, AudioBox *a, SubsEditBox *e, wxStaticLine *sep) {
		grid = g;
		video = v;
		audio = a;
		edit = e;
		separatorLine = sep;
	}

	void ReloadOptions(bool syncRatios) {
		gridPos = parse_grid_pos(OPT_GET("App/Layout/Grid Position")->GetString());
		videoRight = OPT_GET("App/Layout/Video Right")->GetBool();
		swapAudioEdit = OPT_GET("App/Layout/Swap Audio and Edit")->GetBool();
		columnOrder = parse_column_order(OPT_GET("App/Layout/Column Order")->GetString());
		fitVideo = OPT_GET("App/Layout/Fit Video")->GetBool();
		if (syncRatios) {
			gridRatio = std::clamp(static_cast<int>(OPT_GET("App/Layout/Grid Ratio")->GetInt()), 15, 85);
			videoRatio = std::clamp(static_cast<int>(OPT_GET("App/Layout/Video Ratio")->GetInt()), 15, 85);
		}
		if (video)
			video->SetFitToPane(fitVideo);
		Layout();
	}

	bool Layout() override {
		if (!grid || !video || !audio || !edit || !separatorLine)
			return wxPanel::Layout();

		wxSize client = GetClientSize();
		int W = client.GetWidth();
		int H = client.GetHeight();
		if (W <= 0 || H <= 0)
			return false;

		bool showVideo = frame->IsVideoShown();
		bool showAudio = frame->IsAudioShown();
		video->Show(showVideo);
		audio->Show(showAudio);
		edit->Show(true);
		grid->Show(true);

		wxSize editMin = edit->GetEffectiveMinSize();
		int minEditH = std::max(FromDIP(120), editMin.GetHeight());
		int audioMinH = showAudio ? std::max(FromDIP(80), audio->GetMinSize().GetHeight()) : 0;
		int sashSize = FromDIP(5);
		int lineH = std::max(1, separatorLine->GetBestSize().GetHeight());

		if (gridPos == GridPos::Left || gridPos == GridPos::Right) {
			separatorLine->Hide();

			std::vector<PanelKind> visibleCol;
			for (PanelKind kind : columnOrder) {
				if (kind == PanelKind::Video && !showVideo) continue;
				if (kind == PanelKind::Audio && !showAudio) continue;
				visibleCol.push_back(kind);
			}

			audio->SetSashOnTop(showAudio && !visibleCol.empty() && visibleCol.back() == PanelKind::Audio);

			int minWsW = std::max(FromDIP(260), editMin.GetWidth());
			if (showVideo && !fitVideo)
				minWsW = std::max(minWsW, video->GetEffectiveMinSize().GetWidth());
			int minGridW = FromDIP(180);

			int availW = std::max(0, W - sashSize);
			lastMainSpan = std::max(1, availW);
			int gridW = clamp_split(availW, gridRatio, minGridW, minWsW);
			int wsW = std::max(0, availW - gridW);

			mainSash->SetHorizontal(false);
			mainSash->Show();
			int wsX = 0;
			if (gridPos == GridPos::Left) {
				grid->SetSize(0, 0, gridW, H);
				mainSash->SetSize(gridW, 0, sashSize, H);
				wsX = gridW + sashSize;
			}
			else {
				wsX = 0;
				mainSash->SetSize(wsW, 0, sashSize, H);
				grid->SetSize(wsW + sashSize, 0, gridW, H);
			}

			bool showVideoSash = showVideo && visibleCol.size() > 1;
			int colSashH = showVideoSash ? sashSize : 0;
			int minVidH = showVideo ? (fitVideo ? FromDIP(120) : video->GetEffectiveMinSize().GetHeight()) : 0;

			int audioH = 0;
			if (showAudio) {
				int maxAudioH = std::max(FromDIP(70), H - colSashH - minVidH - minEditH);
				audioH = std::min(audioMinH, maxAudioH);
			}

			int remH = std::max(0, H - audioH - colSashH);
			lastVideoSpan = std::max(1, remH);
			int videoH = 0;
			int editH = 0;
			if (showVideo) {
				if (fitVideo) {
					videoH = clamp_split(remH, videoRatio, FromDIP(120), minEditH);
					editH = std::max(0, remH - videoH);
				}
				else {
					videoH = std::min(video->GetEffectiveMinSize().GetHeight(), std::max(FromDIP(100), remH - minEditH));
					editH = std::max(0, remH - videoH);
				}
			}
			else {
				editH = remH;
			}

			videoSashBeforeVideo = (!visibleCol.empty() && visibleCol.back() == PanelKind::Video);
			if (!showVideoSash)
				videoSash->Hide();
			else
				videoSash->SetHorizontal(true);

			int y = 0;
			for (PanelKind kind : visibleCol) {
				if (kind == PanelKind::Video) {
					if (showVideoSash && videoSashBeforeVideo) {
						videoSash->SetSize(wsX, y, wsW, colSashH);
						videoSash->Show();
						y += colSashH;
					}
					video->SetSize(wsX, y, wsW, videoH);
					y += videoH;
					if (showVideoSash && !videoSashBeforeVideo) {
						videoSash->SetSize(wsX, y, wsW, colSashH);
						videoSash->Show();
						y += colSashH;
					}
				}
				else if (kind == PanelKind::Audio) {
					audio->SetSize(wsX, y, wsW, audioH);
					y += audioH;
				}
				else {
					edit->SetSize(wsX, y, wsW, editH);
					y += editH;
				}
			}
			return true;
		}

		// Top / Bottom layout
		audio->SetSashOnTop(swapAudioEdit);

		int pad = FromDIP(5);
		int toolsMinH = (showAudio ? audioMinH : 0) + minEditH + pad;
		int vidMinH = showVideo ? (fitVideo ? FromDIP(140) : video->GetEffectiveMinSize().GetHeight()) : 0;
		int wsNaturalH = std::max(vidMinH, toolsMinH);

		int wsY = 0;
		int wsH = 0;
		if (fitVideo) {
			separatorLine->Hide();
			int availH = std::max(0, H - sashSize);
			lastMainSpan = std::max(1, availH);
			int minGridH = FromDIP(100);
			int minWsH = std::max(FromDIP(160), toolsMinH);
			int gridH = clamp_split(availH, gridRatio, minGridH, minWsH);
			wsH = std::max(0, availH - gridH);

			mainSash->SetHorizontal(true);
			mainSash->Show();
			if (gridPos == GridPos::Bottom) {
				wsY = 0;
				mainSash->SetSize(0, wsH, W, sashSize);
				grid->SetSize(0, wsH + sashSize, W, gridH);
			}
			else {
				grid->SetSize(0, 0, W, gridH);
				mainSash->SetSize(0, gridH, W, sashSize);
				wsY = gridH + sashSize;
			}
		}
		else {
			mainSash->Hide();
			separatorLine->Show();
			int availH = std::max(0, H - lineH);
			wsH = std::min(wsNaturalH, std::max(FromDIP(120), availH - FromDIP(80)));
			int gridH = std::max(0, availH - wsH);

			if (gridPos == GridPos::Bottom) {
				separatorLine->SetSize(0, 0, W, lineH);
				wsY = lineH;
				grid->SetSize(0, lineH + wsH, W, gridH);
			}
			else {
				grid->SetSize(0, 0, W, gridH);
				separatorLine->SetSize(0, gridH, W, lineH);
				wsY = gridH + lineH;
			}
		}

		if (showVideo) {
			if (fitVideo) {
				int availW = std::max(0, W - sashSize);
				lastVideoSpan = std::max(1, availW);
				int minToolsW = std::max(FromDIP(260), editMin.GetWidth()) + 2 * pad;
				int vidW = clamp_split(availW, videoRatio, FromDIP(180), minToolsW);
				int toolsW = std::max(0, availW - vidW);

				videoSash->SetHorizontal(false);
				videoSash->Show();
				if (!videoRight) {
					video->SetSize(0, wsY, vidW, wsH);
					videoSash->SetSize(vidW, wsY, sashSize, wsH);
					LayoutTools(vidW + sashSize, wsY, toolsW, wsH, showAudio, audioMinH, minEditH);
				}
				else {
					LayoutTools(0, wsY, toolsW, wsH, showAudio, audioMinH, minEditH);
					videoSash->SetSize(toolsW, wsY, sashSize, wsH);
					video->SetSize(toolsW + sashSize, wsY, vidW, wsH);
				}
			}
			else {
				videoSash->Hide();
				int minToolsW = editMin.GetWidth() + 2 * pad;
				int vidW = std::min(video->GetEffectiveMinSize().GetWidth(), std::max(FromDIP(160), W - minToolsW));
				int toolsW = std::max(0, W - vidW);
				if (!videoRight) {
					video->SetSize(0, wsY, vidW, wsH);
					LayoutTools(vidW, wsY, toolsW, wsH, showAudio, audioMinH, minEditH);
				}
				else {
					LayoutTools(0, wsY, toolsW, wsH, showAudio, audioMinH, minEditH);
					video->SetSize(toolsW, wsY, vidW, wsH);
				}
			}
		}
		else {
			videoSash->Hide();
			LayoutTools(0, wsY, W, wsH, showAudio, audioMinH, minEditH);
		}

		return true;
	}
};

/// Handle files drag and dropped onto Aegisub
class AegisubFileDropTarget final : public wxFileDropTarget {
	agi::Context *context;
public:
	AegisubFileDropTarget(agi::Context *context) : context(context) { }
	bool OnDropFiles(wxCoord, wxCoord, wxArrayString const& filenames) override {
		std::vector<agi::fs::path> files;
		for (wxString const& fn : filenames)
			files.push_back(from_wx(fn));
		agi::dispatch::Main().Async([=, this] { context->project->LoadList(files); });
		return true;
	}
};

FrameMain::FrameMain()
: wxFrame(nullptr, -1, "")
, context(std::make_unique<agi::Context>())
{
	StartupLog("Entering FrameMain constructor");

	SetSize(FromDIP(wxSize(920, 700)));

#ifdef __WXGTK__
	// XXX HACK XXX
	// We need to set LC_ALL to "" here for input methods to work reliably.
	setlocale(LC_ALL, "");

	// However LC_NUMERIC must be "C", otherwise some parsing fails.
	setlocale(LC_NUMERIC, "C");
#endif

	StartupLog("Initializing context controls");
	BindConnection(context->ass->AddCommitListener(&FrameMain::UpdateTitle, this));
	BindConnection(context->subsController->AddFileOpenListener(&FrameMain::OnSubtitlesOpen, this));
	BindConnection(context->subsController->AddFileSaveListener(&FrameMain::UpdateTitle, this));
	BindConnection(context->project->AddAudioProviderListener(&FrameMain::OnAudioOpen, this));
	BindConnection(context->project->AddVideoProviderListener(&FrameMain::OnVideoOpen, this));

	StartupLog("Initializing context frames");
	context->parent = this;
	context->frame = this;

	StartupLog("Apply saved Maximized state");
	if (OPT_GET("App/Maximized")->GetBool()) Maximize(true);

	StartupLog("Initialize toolbar");
	wxSystemOptions::SetOption("msw.remap", 0);
	BindConnection(OPT_SUB("App/Show Toolbar", &FrameMain::EnableToolBar, this));
	EnableToolBar(*OPT_GET("App/Show Toolbar"));

	StartupLog("Initialize menu bar");
	menu::GetMenuBar("main", this, (wxID_HIGHEST + 1) + 10000, context.get());

	StartupLog("Create status bar");
	CreateStatusBar(2);

	StartupLog("Set icon");
#ifdef _WIN32
	SetIcon(wxICON(wxicon));
#else
	wxIcon icon;
	icon.CopyFromBitmap(GETIMAGE(wxicon));
	SetIcon(icon);
#endif

	StartupLog("Create views and inner main window controls");
	InitContents();
	BindConnection(OPT_SUB("Video/Detached/Enabled", &FrameMain::OnVideoDetach, this));
	BindConnection(OPT_SUB("App/Layout/Grid Position", &FrameMain::OnLayoutChanged, this));
	BindConnection(OPT_SUB("App/Layout/Video Right", &FrameMain::OnLayoutChanged, this));
	BindConnection(OPT_SUB("App/Layout/Swap Audio and Edit", &FrameMain::OnLayoutChanged, this));
	BindConnection(OPT_SUB("App/Layout/Column Order", &FrameMain::OnLayoutChanged, this));
	BindConnection(OPT_SUB("App/Layout/Fit Video", &FrameMain::OnLayoutChanged, this));
	BindConnection(OPT_SUB("App/Layout/Grid Ratio", &FrameMain::OnLayoutChanged, this));
	BindConnection(OPT_SUB("App/Layout/Video Ratio", &FrameMain::OnLayoutChanged, this));

	StartupLog("Set up drag/drop target");
	SetDropTarget(new AegisubFileDropTarget(context.get()));

	StartupLog("Load default file");
	context->project->CloseSubtitles();

	StartupLog("Display main window");
	AddFullScreenButton(this);
	Show();
	SetDisplayMode(1, 1);

	StartupLog("Leaving FrameMain constructor");
}

FrameMain::~FrameMain () {
	context->project->CloseAudio();
	context->project->CloseVideo();

	DestroyChildren();
}

void FrameMain::EnableToolBar(agi::OptionValue const& opt) {
	if (opt.GetBool()) {
		if (!GetToolBar()) {
			toolbar::AttachToolbar(this, "main", context.get(), "Default");
			GetToolBar()->Realize();
		}
	}
	else if (wxToolBar *old_tb = GetToolBar()) {
		SetToolBar(nullptr);
		delete old_tb;
		Layout();
	}
}

void FrameMain::InitContents() {
	StartupLog("Create background panel");
	mainPanel = new MainLayoutPanel(this);

	StartupLog("Create subtitles grid");
	context->subsGrid = new BaseGrid(mainPanel, context.get());

	StartupLog("Create video box");
	videoBox = new VideoBox(mainPanel, false, context.get());

	StartupLog("Create audio box");
	context->audioBox = audioBox = new AudioBox(mainPanel, context.get());

	StartupLog("Create subtitle editing box");
	editBox = new SubsEditBox(mainPanel, context.get());

	StartupLog("Arrange main sizers");
	auto separatorLine = new wxStaticLine(mainPanel);
	mainPanel->SetPanes(context->subsGrid, videoBox, audioBox, editBox, separatorLine);
	mainPanel->ReloadOptions(true);

	StartupLog("Perform layout");
	Layout();
	StartupLog("Leaving InitContents");
}

void FrameMain::OnLayoutChanged() {
	if (!mainPanel || IsBeingDeleted()) return;
	bool didFreeze = !IsFrozen();
	if (didFreeze) Freeze();
	mainPanel->ReloadOptions(true);
	if (didFreeze) Thaw();
}

void FrameMain::SetDisplayMode(int video, int audio) {
	if (!IsShownOnScreen()) return;

	bool sv = false, sa = false;

	if (video == -1) sv = showVideo;
	else if (video)  sv = context->project->VideoProvider() && !context->dialog->Get<DialogDetachedVideo>();

	if (audio == -1) sa = showAudio;
	else if (audio)  sa = !!context->project->AudioProvider();

	// See if anything changed
	if (sv == showVideo && sa == showAudio) return;

	showVideo = sv;
	showAudio = sa;

	bool didFreeze = !IsFrozen();
	if (didFreeze) Freeze();

	context->videoController->Stop();

	videoBox->Show(showVideo);
	audioBox->Show(showAudio);

	mainPanel->Layout();
	Layout();

	if (didFreeze) Thaw();
}

void FrameMain::UpdateTitle() {
	wxString newTitle;
	if (context->subsController->IsModified()) newTitle << "* ";
	newTitle << context->subsController->Filename().filename().wstring();

#ifndef __WXMAC__
	newTitle << " - Aegisub " << GetAegisubLongVersionString();
#endif

#if defined(__WXMAC__)
	// On Mac, set the mark in the close button
	OSXSetModified(context->subsController->IsModified());
#endif

	if (GetTitle() != newTitle) SetTitle(newTitle);
}

void FrameMain::OnVideoOpen(AsyncVideoProvider *provider) {
	if (!provider) {
		SetDisplayMode(0, -1);
		return;
	}

	Freeze();
	int vidx = provider->GetWidth(), vidy = provider->GetHeight();

	// Set zoom level based on video resolution and window size
	double zoom = context->videoDisplay->GetWindowZoom();
	wxSize windowSize = GetSize();
	if (vidx*3*zoom > windowSize.GetX()*4 || vidy*4*zoom > windowSize.GetY()*6)
		context->videoDisplay->SetWindowZoom(zoom * .25);
	else if (vidx*3*zoom > windowSize.GetX()*2 || vidy*4*zoom > windowSize.GetY()*3)
		context->videoDisplay->SetWindowZoom(zoom * .5);

	SetDisplayMode(1,-1);

	if (OPT_GET("Video/Detached/Enabled")->GetBool() && !context->dialog->Get<DialogDetachedVideo>())
		cmd::call("video/detach", context.get());
	Thaw();
}

void FrameMain::OnVideoDetach(agi::OptionValue const& opt) {
	if (opt.GetBool())
		SetDisplayMode(0, -1);
	else if (context->project->VideoProvider())
		SetDisplayMode(1, -1);
}

void FrameMain::StatusTimeout(wxString text,int ms) {
	SetStatusText(text,1);
	StatusClear.SetOwner(this, ID_APP_TIMER_STATUSCLEAR);
	StatusClear.Start(ms,true);
}

BEGIN_EVENT_TABLE(FrameMain, wxFrame)
	EVT_TIMER(ID_APP_TIMER_STATUSCLEAR, FrameMain::OnStatusClear)
	EVT_CLOSE(FrameMain::OnCloseWindow)
	EVT_CHAR_HOOK(FrameMain::OnKeyDown)
	EVT_MOUSEWHEEL(FrameMain::OnMouseWheel)
END_EVENT_TABLE()

void FrameMain::OnCloseWindow(wxCloseEvent &event) {
	wxEventBlocker blocker(this, wxEVT_CLOSE_WINDOW);

	context->videoController->Stop();
	context->audioController->Stop();

	// Ask user if he wants to save first
	if (context->subsController->TryToClose(event.CanVeto()) == wxCANCEL) {
		event.Veto();
		return;
	}

	context->dialog.reset();

	// Store maximization state
	OPT_SET("App/Maximized")->SetBool(IsMaximized());

	Destroy();
}

void FrameMain::OnStatusClear(wxTimerEvent &) {
	SetStatusText("",1);
}

void FrameMain::OnAudioOpen(agi::AudioProvider *provider) {
	if (provider)
		SetDisplayMode(-1, 1);
	else
		SetDisplayMode(-1, 0);
}

void FrameMain::OnSubtitlesOpen() {
	UpdateTitle();
	SetDisplayMode(1, 1);
}

void FrameMain::OnKeyDown(wxKeyEvent &event) {
	hotkey::check("Main Frame", context.get(), event);
}

void FrameMain::OnMouseWheel(wxMouseEvent &evt) {
	ForwardMouseWheelEvent(this, evt);
}
