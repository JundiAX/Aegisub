// Copyright (c) 2026, Aegisub contributors
//
// Permission to use, copy, modify, and distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

#include "dialogs.h"

#include "include/aegisub/context.h"
#include "options.h"

#include <algorithm>
#include <array>
#include <string>

#include <wx/button.h>
#include <wx/checkbox.h>
#include <wx/choice.h>
#include <wx/dialog.h>
#include <wx/intl.h>
#include <wx/sizer.h>
#include <wx/spinctrl.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

namespace {

struct LayoutSnapshot {
	std::string gridPos;
	bool videoRight;
	bool swapAudioEdit;
	std::string columnOrder;
	bool fitVideo;
	int gridRatio;
	int videoRatio;
};

LayoutSnapshot load_snapshot() {
	return {
		OPT_GET("App/Layout/Grid Position")->GetString(),
		OPT_GET("App/Layout/Video Right")->GetBool(),
		OPT_GET("App/Layout/Swap Audio and Edit")->GetBool(),
		OPT_GET("App/Layout/Column Order")->GetString(),
		OPT_GET("App/Layout/Fit Video")->GetBool(),
		std::clamp(static_cast<int>(OPT_GET("App/Layout/Grid Ratio")->GetInt()), 15, 85),
		std::clamp(static_cast<int>(OPT_GET("App/Layout/Video Ratio")->GetInt()), 15, 85),
	};
}

void save_snapshot(LayoutSnapshot const& s) {
	OPT_SET("App/Layout/Grid Position")->SetString(s.gridPos);
	OPT_SET("App/Layout/Video Right")->SetBool(s.videoRight);
	OPT_SET("App/Layout/Swap Audio and Edit")->SetBool(s.swapAudioEdit);
	OPT_SET("App/Layout/Column Order")->SetString(s.columnOrder);
	OPT_SET("App/Layout/Fit Video")->SetBool(s.fitVideo);
	OPT_SET("App/Layout/Grid Ratio")->SetInt(s.gridRatio);
	OPT_SET("App/Layout/Video Ratio")->SetInt(s.videoRatio);
}

constexpr std::array<const char*, 4> GRID_POS_KEYS = {
	"bottom",
	"top",
	"left",
	"right",
};

constexpr std::array<const char*, 6> COLUMN_ORDER_KEYS = {
	"video,audio,edit",
	"video,edit,audio",
	"audio,video,edit",
	"audio,edit,video",
	"edit,video,audio",
	"edit,audio,video",
};

int index_of_grid_pos(std::string const& key) {
	for (size_t i = 0; i < GRID_POS_KEYS.size(); ++i) {
		if (key == GRID_POS_KEYS[i]) return static_cast<int>(i);
	}
	return 0;
}

int index_of_column_order(std::string const& key) {
	for (size_t i = 0; i < COLUMN_ORDER_KEYS.size(); ++i) {
		if (key == COLUMN_ORDER_KEYS[i]) return static_cast<int>(i);
	}
	return 0;
}

class DialogLayout final : public wxDialog {
	LayoutSnapshot initialState;
	bool fitVideoTouched = false;

	wxChoice *gridPosChoice;
	wxCheckBox *videoRightCheck;
	wxCheckBox *swapAudioEditCheck;
	wxChoice *columnOrderChoice;
	wxCheckBox *fitVideoCheck;
	wxSpinCtrl *gridRatioSpin;
	wxSpinCtrl *videoRatioSpin;

	void SyncControlStates() {
		int gp = gridPosChoice->GetSelection();
		bool isColumnMode = (gp == 2 || gp == 3);
		videoRightCheck->Enable(!isColumnMode);
		swapAudioEditCheck->Enable(!isColumnMode);
		columnOrderChoice->Enable(isColumnMode);
		videoRatioSpin->Enable(fitVideoCheck->GetValue());
	}

	void ApplyFromControls() {
		int gpIdx = std::clamp(gridPosChoice->GetSelection(), 0, static_cast<int>(GRID_POS_KEYS.size()) - 1);
		int colIdx = std::clamp(columnOrderChoice->GetSelection(), 0, static_cast<int>(COLUMN_ORDER_KEYS.size()) - 1);
		LayoutSnapshot cur{
			GRID_POS_KEYS[gpIdx],
			videoRightCheck->GetValue(),
			swapAudioEditCheck->GetValue(),
			COLUMN_ORDER_KEYS[colIdx],
			fitVideoCheck->GetValue(),
			std::clamp(gridRatioSpin->GetValue(), 15, 85),
			std::clamp(videoRatioSpin->GetValue(), 15, 85),
		};
		save_snapshot(cur);
		SyncControlStates();
	}

	void LoadToControls(LayoutSnapshot const& s) {
		gridPosChoice->SetSelection(index_of_grid_pos(s.gridPos));
		videoRightCheck->SetValue(s.videoRight);
		swapAudioEditCheck->SetValue(s.swapAudioEdit);
		columnOrderChoice->SetSelection(index_of_column_order(s.columnOrder));
		fitVideoCheck->SetValue(s.fitVideo);
		gridRatioSpin->SetValue(s.gridRatio);
		videoRatioSpin->SetValue(s.videoRatio);
		SyncControlStates();
	}

public:
	DialogLayout(wxWindow *parent)
	: wxDialog(parent, wxID_ANY, _("Customize Layout"), wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE)
	, initialState(load_snapshot())
	{
		wxArrayString gridChoices;
		gridChoices.Add(_("Bottom (Default)"));
		gridChoices.Add(_("Top"));
		gridChoices.Add(_("Left (Full Height)"));
		gridChoices.Add(_("Right (Full Height)"));

		wxArrayString colOrderChoices;
		colOrderChoices.Add(_("Video -> Audio -> Edit Box"));
		colOrderChoices.Add(_("Video -> Edit Box -> Audio"));
		colOrderChoices.Add(_("Audio -> Video -> Edit Box"));
		colOrderChoices.Add(_("Audio -> Edit Box -> Video"));
		colOrderChoices.Add(_("Edit Box -> Video -> Audio"));
		colOrderChoices.Add(_("Edit Box -> Audio -> Video"));

		auto *mainSizer = new wxBoxSizer(wxVERTICAL);

		auto *arrangeBox = new wxStaticBoxSizer(wxVERTICAL, this, _("Panel Arrangement"));
		auto *arrangeGrid = new wxFlexGridSizer(2, 6, 8);
		arrangeGrid->AddGrowableCol(1, 1);

		arrangeGrid->Add(new wxStaticText(arrangeBox->GetStaticBox(), wxID_ANY, _("Subtitle Grid position:")),
			wxSizerFlags().CenterVertical());
		gridPosChoice = new wxChoice(arrangeBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, gridChoices);
		arrangeGrid->Add(gridPosChoice, wxSizerFlags().Expand());

		arrangeGrid->Add(new wxStaticText(arrangeBox->GetStaticBox(), wxID_ANY, _("Side column order (Left/Right):")),
			wxSizerFlags().CenterVertical());
		columnOrderChoice = new wxChoice(arrangeBox->GetStaticBox(), wxID_ANY, wxDefaultPosition, wxDefaultSize, colOrderChoices);
		arrangeGrid->Add(columnOrderChoice, wxSizerFlags().Expand());

		arrangeBox->Add(arrangeGrid, 0, wxEXPAND | wxALL, 6);

		videoRightCheck = new wxCheckBox(arrangeBox->GetStaticBox(), wxID_ANY, _("Place Video on the right side (Top/Bottom mode)"));
		swapAudioEditCheck = new wxCheckBox(arrangeBox->GetStaticBox(), wxID_ANY, _("Place Subtitle Edit Box above Audio (Top/Bottom mode)"));
		arrangeBox->Add(videoRightCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);
		arrangeBox->Add(swapAudioEditCheck, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

		mainSizer->Add(arrangeBox, 0, wxEXPAND | wxALL, 8);

		auto *sizingBox = new wxStaticBoxSizer(wxVERTICAL, this, _("Panel Proportions & Video Scaling"));
		fitVideoCheck = new wxCheckBox(sizingBox->GetStaticBox(), wxID_ANY, _("Fit Video to panel size (enables Video splitter)"));
		sizingBox->Add(fitVideoCheck, 0, wxALL, 6);

		auto *ratioGrid = new wxFlexGridSizer(2, 6, 8);
		ratioGrid->AddGrowableCol(1, 1);

		ratioGrid->Add(new wxStaticText(sizingBox->GetStaticBox(), wxID_ANY, _("Subtitle Grid size (%):")),
			wxSizerFlags().CenterVertical());
		gridRatioSpin = new wxSpinCtrl(sizingBox->GetStaticBox(), wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 15, 85, 50);
		ratioGrid->Add(gridRatioSpin, wxSizerFlags().Expand());

		ratioGrid->Add(new wxStaticText(sizingBox->GetStaticBox(), wxID_ANY, _("Video panel size (%):")),
			wxSizerFlags().CenterVertical());
		videoRatioSpin = new wxSpinCtrl(sizingBox->GetStaticBox(), wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxSP_ARROW_KEYS, 15, 85, 45);
		ratioGrid->Add(videoRatioSpin, wxSizerFlags().Expand());

		sizingBox->Add(ratioGrid, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 6);

		auto *tip = new wxStaticText(sizingBox->GetStaticBox(), wxID_ANY, _("Tip: You can also drag the divider bars directly in the main window."));
		sizingBox->Add(tip, 0, wxLEFT | wxRIGHT | wxBOTTOM, 6);

		mainSizer->Add(sizingBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

		auto *bottomSizer = new wxBoxSizer(wxHORIZONTAL);
		auto *resetBtn = new wxButton(this, wxID_ANY, _("Reset Default"));
		bottomSizer->Add(resetBtn, wxSizerFlags().CenterVertical());
		bottomSizer->AddStretchSpacer(1);
		bottomSizer->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), wxSizerFlags().CenterVertical());
		mainSizer->Add(bottomSizer, 0, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 8);

		SetSizerAndFit(mainSizer);
		CenterOnParent();

		LoadToControls(initialState);

		gridPosChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) {
			int sel = gridPosChoice->GetSelection();
			if (!fitVideoTouched)
				fitVideoCheck->SetValue(sel == 2 || sel == 3);
			ApplyFromControls();
		});
		columnOrderChoice->Bind(wxEVT_CHOICE, [this](wxCommandEvent&) { ApplyFromControls(); });
		videoRightCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { ApplyFromControls(); });
		swapAudioEditCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) { ApplyFromControls(); });
		fitVideoCheck->Bind(wxEVT_CHECKBOX, [this](wxCommandEvent&) {
			fitVideoTouched = true;
			ApplyFromControls();
		});
		gridRatioSpin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { ApplyFromControls(); });
		videoRatioSpin->Bind(wxEVT_SPINCTRL, [this](wxSpinEvent&) { ApplyFromControls(); });

		resetBtn->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			fitVideoTouched = false;
			LayoutSnapshot def{ "bottom", false, false, "video,audio,edit", false, 50, 45 };
			LoadToControls(def);
			save_snapshot(def);
		});

		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			ApplyFromControls();
			EndModal(wxID_OK);
		}, wxID_OK);

		Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
			save_snapshot(initialState);
			EndModal(wxID_CANCEL);
		}, wxID_CANCEL);

		Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent &evt) {
			save_snapshot(initialState);
			evt.Skip();
		});
	}
};

} // namespace

void ShowLayoutDialog(agi::Context *c) {
	DialogLayout(c ? c->parent : nullptr).ShowModal();
}
