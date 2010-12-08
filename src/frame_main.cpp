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
//
// $Id$

/// @file frame_main.cpp
/// @brief Main window creation and control management
/// @ingroup main_ui

#include "config.h"

#ifndef AGI_PRE
#include <wx/filename.h>
#include <wx/image.h>
#include <wx/mimetype.h>
#include <wx/statline.h>
#include <wx/sysopt.h>
#include <wx/tokenzr.h>
#endif

#include <libaegisub/log.h>

#include "ass_file.h"
#include "selection_controller.h"
#include "audio_controller.h"
#include "audio_box.h"
#ifdef WITH_AUTOMATION
#include "auto4_base.h"
#endif
#ifdef WITH_AVISYNTH
#include "avisynth_wrap.h"
#endif
#include "compat.h"
#include "command/command.h"
#include "dialog_detached_video.h"
#include "dialog_search_replace.h"
#include "dialog_styling_assistant.h"
#include "dialog_version_check.h"
#include "drop.h"
#include "frame_main.h"
#include "help_button.h"
#include "hotkeys.h"
#include "keyframe.h"
#include "libresrc/libresrc.h"
#include "main.h"
#include "standard_paths.h"
#include "subs_edit_box.h"
#include "subs_edit_ctrl.h"
#include "subs_grid.h"
#include "text_file_reader.h"
#include "text_file_writer.h"
#include "utils.h"
#include "version.h"
#include "video_box.h"
#include "video_context.h"
#include "video_display.h"
#include "video_provider_manager.h"
#include "video_slider.h"


#ifdef WITH_STARTUPLOG

/// DOCME
#define StartupLog(a) MessageBox(0, a, _T("Aegisub startup log"), 0)
#else

/// DOCME
#define StartupLog(a)
#endif

static void autosave_timer_changed(wxTimer *timer, const agi::OptionValue &opt);

FrameMain::FrameMain (wxArrayString args)
: wxFrame ((wxFrame*)NULL,-1,_T(""),wxDefaultPosition,wxSize(920,700),wxDEFAULT_FRAME_STYLE | wxCLIP_CHILDREN)
{
	StartupLog(_T("Entering FrameMain constructor"));
	temp_context.parent = this;

	// Bind all commands.
	// XXX: This is a hack for now, it will need to be dealt with when other frames are involved.
	int count = cmd::count();
	for (int i = 0; i < count; i++) {
		Bind(wxEVT_COMMAND_MENU_SELECTED, &FrameMain::cmd_call, this, i);
    }

#ifdef __WXMAC__
	Bind(FrameMain::OnAbout, &FrameMain::cmd_call, this, cmd::id("app/about"));
#endif




#ifdef __WXGTK__
/* XXX HACK XXX
 * Gtk just got initialized. And if we're using the SCIM IME,
 * it just did a setlocale(LC_ALL, ""). so, BOOM.
 */
 	setlocale(LC_ALL, "");
	setlocale(LC_CTYPE, "C");
	setlocale(LC_NUMERIC, "C");
/* XXX HACK XXX */
#endif

	// Set application's frame
	AegisubApp::Get()->frame = this;

	// Initialize flags
	HasSelection = false;
	menuCreated = false;
	blockVideoLoad = false;

	StartupLog(_T("Install PNG handler"));
	// Create PNG handler
	wxPNGHandler *png = new wxPNGHandler;
	wxImage::AddHandler(png);

	wxSafeYield();

	// Storage for subs-file-local scripts
#ifdef WITH_AUTOMATION
	StartupLog(_T("Create local Automation script manager"));
	local_scripts = new Automation4::ScriptManager();
	temp_context.local_scripts = local_scripts;
#endif

	// Contexts and controllers
	audioController = new AudioController;
	audioController->AddAudioOpenListener(&FrameMain::OnAudioOpen, this);
	audioController->AddAudioCloseListener(&FrameMain::OnAudioClose, this);

	// Create menu and tool bars
	StartupLog(_T("Apply saved Maximized state"));
	if (OPT_GET("App/Maximized")->GetBool()) Maximize(true);
	StartupLog(_T("Initialize toolbar"));
	InitToolbar();
	StartupLog(_T("Initialize menu bar"));
	InitMenu();
	
	// Create status bar
	StartupLog(_T("Create status bar"));
	CreateStatusBar(2);

	// Set icon
	StartupLog(_T("Set icon"));
#ifdef _WINDOWS
	SetIcon(wxICON(wxicon));
#else
	wxIcon icon;
	icon.CopyFromBitmap(GETIMAGE(wxicon_misc));
	SetIcon(icon);
#endif

	// Contents
	showVideo = true;
	showAudio = true;
	detachedVideo = NULL;
	stylingAssistant = NULL;
	StartupLog(_T("Initialize inner main window controls"));
	InitContents();

	// Set autosave timer
	StartupLog(_T("Set up Auto Save"));
	AutoSave.SetOwner(this, ID_APP_TIMER_AUTOSAVE);
	int time = OPT_GET("App/Auto/Save Every Seconds")->GetInt();
	if (time > 0) {
		AutoSave.Start(time*1000);
	}
	OPT_SUB("App/Auto/Save Every Seconds", autosave_timer_changed, &AutoSave, agi::signal::_1);

	// Set accelerator keys
	StartupLog(_T("Install hotkeys"));
	PreviousFocus = NULL;
	SetAccelerators();

	// Set drop target
	StartupLog(_T("Set up drag/drop target"));
	SetDropTarget(new AegisubFileDropTarget(this));

	// Parse arguments
	StartupLog(_T("Initialize empty file"));
	LoadSubtitles(_T(""));
	StartupLog(_T("Load files specified on command line"));
	LoadList(args);

	// Version checker
	StartupLog(_T("Possibly perform automatic updates check"));
	if (OPT_GET("App/First Start")->GetBool()) {
		OPT_SET("App/First Start")->SetBool(false);
		int result = wxMessageBox(_("Do you want Aegisub to check for updates whenever it starts? You can still do it manually via the Help menu."),_("Check for updates?"),wxYES_NO);
		OPT_SET("App/Auto/Check For Updates")->SetBool(result == wxYES);
	}

	PerformVersionCheck(false);

	StartupLog(_T("Display main window"));
	Show();
	Freeze();
	SetDisplayMode(1, 1);
	Thaw();

	//ShowFullScreen(true,wxFULLSCREEN_NOBORDER | wxFULLSCREEN_NOCAPTION);
	StartupLog(_T("Leaving FrameMain constructor"));
}

/// @brief FrameMain destructor 
FrameMain::~FrameMain () {
	VideoContext::Get()->SetVideo(_T(""));
	audioController->CloseAudio();
	DeInitContents();
	delete audioController;
#ifdef WITH_AUTOMATION
	delete local_scripts;
#endif
}



void FrameMain::cmd_call(wxCommandEvent& event) {
	int id = event.GetId();
	LOG_D("event/select") << "Id: " << id;
	cmd::call(&temp_context, id);
}



/// @brief Initialize toolbar 
void FrameMain::InitToolbar () {
	// Create toolbar
	wxSystemOptions::SetOption(_T("msw.remap"), 0);
	Toolbar = CreateToolBar(wxTB_FLAT | wxTB_HORIZONTAL,-1,_T("Toolbar"));

	// Subtitle control buttons
	Toolbar->AddTool(	cmd::id("subtitle/new"),		_("New"),GETIMAGE(new_toolbutton_24),_("New subtitles"));
	Toolbar->AddTool(	cmd::id("subtitle/open"),		_("Open"),GETIMAGE(open_toolbutton_24),_("Open subtitles"));
	Toolbar->AddTool(	cmd::id("subtitle/save"),		_("Save"),GETIMAGE(save_toolbutton_24),_("Save subtitles"));
	Toolbar->AddSeparator();

	// Video zoom controls
	Toolbar->AddTool(	cmd::id("video/jump"),			_("Jump To..."),GETIMAGE(jumpto_button_24),wxNullBitmap,wxITEM_NORMAL,_("Jump video to time/frame"));
	Toolbar->AddTool(	cmd::id("video/zoom/in"),		_("Zoom in"),GETIMAGE(zoom_in_button_24),wxNullBitmap,wxITEM_NORMAL,_("Zoom video in"));
	Toolbar->AddTool(	cmd::id("video/zoom/out"),		_("Zoom out"),GETIMAGE(zoom_out_button_24),wxNullBitmap,wxITEM_NORMAL,_("Zoom video out"));
	wxArrayString choices;
	for (int i=1;i<=24;i++) {
		wxString toAdd = wxString::Format(_T("%i"),int(i*12.5));
		if (i%2) toAdd += _T(".5");
		toAdd += _T("%");
		choices.Add(toAdd);
	}
	ZoomBox = new wxComboBox(Toolbar,ID_TOOLBAR_ZOOM_DROPDOWN,_T("75%"),wxDefaultPosition,wxDefaultSize,choices,wxCB_DROPDOWN);
	Toolbar->AddControl(ZoomBox);
	Toolbar->AddSeparator();

	// More video buttons
	Toolbar->AddTool(	cmd::id("video/jump/start"),		_("Jump video to start"),GETIMAGE(video_to_substart_24),_("Jumps the video to the start frame of current subtitle"));
	Toolbar->AddTool(	cmd::id("video/jump/end"),			_("Jump video to end"),GETIMAGE(video_to_subend_24),_("Jumps the video to the end frame of current subtitle"));
	Toolbar->AddTool(	cmd::id("time/snap/start_video"),	_("Snap start to video"),GETIMAGE(substart_to_video_24),_("Set start of selected subtitles to current video frame"));
	Toolbar->AddTool(	cmd::id("time/snap/end_video"),		_("Snap end to video"),GETIMAGE(subend_to_video_24),_("Set end of selected subtitles to current video frame"));
	Toolbar->AddTool(	cmd::id("subtitle/select/visible"),	_("Select visible"),GETIMAGE(select_visible_button_24),_("Selects all lines that are currently visible on video frame"));
	Toolbar->AddTool(	cmd::id("time/snap/scene"),			_("Snap subtitles to scene"),GETIMAGE(snap_subs_to_scene_24),_("Snap selected subtitles so they match current scene start/end"));
	Toolbar->AddTool(	cmd::id("time/snap/frame"),			_("Shift subtitles to frame"),GETIMAGE(shift_to_frame_24),_("Shift selected subtitles so first selected starts at this frame"));
	Toolbar->AddSeparator();

	// Property stuff
	Toolbar->AddTool(	cmd::id("tool/style/manager"),	_("Styles Manager"),GETIMAGE(style_toolbutton_24),_("Open Styles Manager"));
	Toolbar->AddTool(	cmd::id("subtitle/properties"),	_("Properties"),GETIMAGE(properties_toolbutton_24),_("Open Properties"));
	Toolbar->AddTool(	cmd::id("subtitle/attachment"),	_("Attachments"),GETIMAGE(attach_button_24),_("Open Attachment List"));
	Toolbar->AddTool(	cmd::id("tool/font_collector"),	_("Fonts Collector"),GETIMAGE(font_collector_button_24),_("Open Fonts Collector"));
	Toolbar->AddSeparator();

	// Automation
#ifdef WITH_AUTOMATION
	Toolbar->AddTool(	cmd::id("am/manager"),	_("Automation"),GETIMAGE(automation_toolbutton_24),_("Open Automation manager"));
	Toolbar->AddSeparator();
#endif

	// Tools
	if (HasASSDraw()) {
		Toolbar->AddTool(cmd::id("tool/assdraw")	,_T("ASSDraw3"),GETIMAGE(assdraw_24),_("Launches ai-chan's \"ASSDraw3\" tool for vector drawing."));
		Toolbar->AddSeparator();
	}
	Toolbar->AddTool(	cmd::id("time/shift"),	_("Shift Times"),GETIMAGE(shift_times_toolbutton_24),_("Open Shift Times Dialogue"));
	Toolbar->AddTool(	cmd::id("tool/style/assistant"),	_("Styling Assistant"),GETIMAGE(styling_toolbutton_24),_("Open Styling Assistant"));
	Toolbar->AddTool(	cmd::id("tool/translation_assistant"),	_("Translation Assistant"),GETIMAGE(translation_toolbutton_24),_("Open Translation Assistant"));
	Toolbar->AddTool(	cmd::id("tool/resampleres"),	_("Resample"),GETIMAGE(resample_toolbutton_24),_("Resample Script Resolution"));
	Toolbar->AddTool(	cmd::id("tool/time/postprocess"),	_("Timing Post-Processor"),GETIMAGE(timing_processor_toolbutton_24),_("Open Timing Post-processor dialog"));
	Toolbar->AddTool(	cmd::id("tool/time/kanji"),	_("Kanji Timer"),GETIMAGE(kara_timing_copier_24),_("Open Kanji Timer dialog"));
	Toolbar->AddTool(	cmd::id("tool/time/kanji"),	_("Spell Checker"),GETIMAGE(spellcheck_toolbutton_24),_("Open Spell checker"));
	Toolbar->AddSeparator();

	// Options
	Toolbar->AddTool(	cmd::id("app/options"),	_("Options"),GETIMAGE(options_button_24),_("Configure Aegisub"));
	Toolbar->AddTool(	cmd::id("grid/tag/cycle_hiding"),	_("Cycle Tag Hidding Mode"),GETIMAGE(toggle_tag_hiding_24),_("Cycle through tag-hiding modes"));

	// Update
	Toolbar->Realize();
}


/// @brief DOCME
/// @param item_text   
/// @param hotkey_name 
/// @return 
wxString MakeHotkeyText(const wxString &item_text, const wxString &hotkey_name) {
	return item_text + wxString(_T("\t")) + Hotkeys.GetText(hotkey_name);
 }


/// @brief Initialize menu bar 
void FrameMain::InitMenu() {
	// Deinit menu if needed
	if (menuCreated) {
		SetMenuBar(NULL);
		MenuBar->Destroy();
	}
	
#ifdef __WXMAC__
	// Make sure special menu items are placed correctly on Mac
	wxApp::s_macAboutMenuItemId = Menu_Help_About;
	wxApp::s_macExitMenuItemId = Menu_File_Exit;
	wxApp::s_macPreferencesMenuItemId = Menu_Tools_Options;
	wxApp::s_macHelpMenuTitleName = _("&Help");
#endif

	// Generate menubar
	MenuBar = new wxMenuBar();

	// Create recent subs submenus
	RecentSubs = new wxMenu();
	RecentVids = new wxMenu();
	RecentAuds = new wxMenu();
	RecentTimecodes = new wxMenu();
	RecentKeyframes = new wxMenu();



// ###########
// # FILE MENU
// ###########
	fileMenu = new wxMenu();
	AppendBitmapMenuItem(fileMenu,	cmd::id("subtitle/new"),			MakeHotkeyText(_("&New Subtitles"), _T("New Subtitles")), _("New subtitles"),GETIMAGE(new_toolbutton_16));
	AppendBitmapMenuItem(fileMenu,	cmd::id("subtitle/open"), 			MakeHotkeyText(_("&Open Subtitles..."), _T("Open Subtitles")), _("Opens a subtitles file"),GETIMAGE(open_toolbutton_16));
	AppendBitmapMenuItem(fileMenu,	cmd::id("subtitle/open/charset"),	_("&Open Subtitles with Charset..."), _("Opens a subtitles file with a specific charset"),GETIMAGE(open_with_toolbutton_16));
	fileMenu->Append(				cmd::id("subtitle/open/video"),		_("Open Subtitles from &Video"), _("Opens the subtitles from the current video file"));
	AppendBitmapMenuItem(fileMenu,	cmd::id("subtitle/save"),			MakeHotkeyText(_("&Save Subtitles"), _T("Save Subtitles")), _("Saves subtitles"),GETIMAGE(save_toolbutton_16));
	AppendBitmapMenuItem(fileMenu,	cmd::id("subtitle/save/as"),		_("Save Subtitles as..."), _("Saves subtitles with another name"), GETIMAGE(save_as_toolbutton_16));
	AppendBitmapMenuItem(fileMenu,	cmd::id("tool/export"),				_("Export Subtitles..."), _("Saves a copy of subtitles with processing applied to it."), GETIMAGE(export_menu_16));
	wxMenuItem *RecentParent = new wxMenuItem(fileMenu, ID_SM_FILE_RECENT_SUBS, _("Recent"), _T(""), wxITEM_NORMAL, RecentSubs);
#ifndef __APPLE__
	RecentParent->SetBitmap(GETIMAGE(blank_button_16));
#endif
	fileMenu->Append(RecentParent);
	fileMenu->AppendSeparator();
	AppendBitmapMenuItem (fileMenu,	cmd::id("subtitle/properties"),		_("&Properties..."), _("Open script properties window"),GETIMAGE(properties_toolbutton_16));
	AppendBitmapMenuItem (fileMenu,	cmd::id("subtitle/attachment"),		_("&Attachments..."), _("Open the attachment list"), GETIMAGE(attach_button_16));
	AppendBitmapMenuItem (fileMenu,	cmd::id("tool/font_collector"),		_("&Fonts Collector..."),_("Open fonts collector"), GETIMAGE(font_collector_button_16));
	fileMenu->AppendSeparator();
#ifndef __APPLE__
	// Doesn't work on Mac, only one instance is ever allowed there from OS side
	AppendBitmapMenuItem(fileMenu,cmd::id("app/new_window"), _("New Window"), _("Open a new application window"),GETIMAGE(new_window_menu_16));
#endif
	AppendBitmapMenuItem(fileMenu,cmd::id("app/exit"), MakeHotkeyText(_("E&xit"), _T("Exit")), _("Exit the application"),GETIMAGE(exit_button_16));
	MenuBar->Append(fileMenu, _("&File"));



// ###########
// # EDIT MENU
// ###########
	// NOTE: Undo and Redo are actually controlled in frame_main_events, OnMenuOpen(). They will always be the first two items.
	editMenu = new wxMenu();
	AppendBitmapMenuItem(editMenu,cmd::id("edit/undo"),				MakeHotkeyText(_("&Undo"), _T("Undo")), _("Undoes last action"),GETIMAGE(undo_button_16));
	AppendBitmapMenuItem(editMenu,cmd::id("edit/redo"),				MakeHotkeyText(_("&Redo"), _T("Redo")), _("Redoes last action"),GETIMAGE(redo_button_16));
	editMenu->AppendSeparator();
	AppendBitmapMenuItem(editMenu,cmd::id("edit/line/cut"),			MakeHotkeyText(_("Cut Lines"), _T("Cut")), _("Cut subtitles"), GETIMAGE(cut_button_16));
	AppendBitmapMenuItem(editMenu,cmd::id("edit/line/copy"),		MakeHotkeyText(_("Copy Lines"), _T("Copy")), _("Copy subtitles"), GETIMAGE(copy_button_16));
	AppendBitmapMenuItem(editMenu,cmd::id("edit/line/paste"),		MakeHotkeyText(_("Paste Lines"), _T("Paste")), _("Paste subtitles"), GETIMAGE(paste_button_16));
	AppendBitmapMenuItem(editMenu,cmd::id("edit/line/paste/over"),	MakeHotkeyText(_("Paste Lines Over..."), _T("Paste Over")) , _("Paste subtitles over others"),GETIMAGE(paste_over_button_16));
	editMenu->AppendSeparator();
	AppendBitmapMenuItem(editMenu,cmd::id("subtitle/find"),			MakeHotkeyText(_("&Find..."), _T("Find")), _("Find words in subtitles"),GETIMAGE(find_button_16));
	AppendBitmapMenuItem(editMenu,cmd::id("subtitle/find/next"),	MakeHotkeyText(_("Find Next"), _T("Find Next")), _("Find next match of last word"),GETIMAGE(find_next_menu_16));
	AppendBitmapMenuItem(editMenu,cmd::id("edit/search_replace"),	MakeHotkeyText(_("Search and &Replace..."), _T("Replace")) , _("Find and replace words in subtitles"),GETIMAGE(find_replace_menu_16));
	MenuBar->Append(editMenu, _("&Edit"));



// ################
// # SUBTITLES MENU
// ################
	subtitlesMenu = new wxMenu();
	AppendBitmapMenuItem (subtitlesMenu,	cmd::id("tool/style/manager"),			_("&Styles Manager..."), _("Open styles manager"), GETIMAGE(style_toolbutton_16));
	AppendBitmapMenuItem (subtitlesMenu,	cmd::id("tool/style/assistant"),		_("St&yling Assistant..."), _("Open styling assistant"), GETIMAGE(styling_toolbutton_16));
	AppendBitmapMenuItem (subtitlesMenu,	cmd::id("tool/translation_assistant"),	_("&Translation Assistant..."),_("Open translation assistant"), GETIMAGE(translation_toolbutton_16));
	AppendBitmapMenuItem (subtitlesMenu,	cmd::id("tool/resampleres"),			_("Resample Resolution..."), _("Changes resolution and modifies subtitles to conform to change"), GETIMAGE(resample_toolbutton_16));
	AppendBitmapMenuItem (subtitlesMenu,	cmd::id("subtitle/spellcheck"),			_("Spe&ll Checker..."),_("Open spell checker"), GETIMAGE(spellcheck_toolbutton_16));
	if (HasASSDraw()) {
		subtitlesMenu->AppendSeparator();
		AppendBitmapMenuItem (subtitlesMenu,cmd::id("tool/assdraw"),				_T("ASSDraw3..."),_("Launches ai-chan's \"ASSDraw3\" tool for vector drawing."), GETIMAGE(assdraw_16));
	}
	subtitlesMenu->AppendSeparator();
	wxMenu *InsertMenu = new wxMenu;
	wxMenuItem *InsertParent = new wxMenuItem(subtitlesMenu,ID_SM_SUBTITLES_INSERT,_("&Insert Lines"),_T(""),wxITEM_NORMAL,InsertMenu);
#ifndef __APPLE__
	InsertParent->SetBitmap(GETIMAGE(blank_button_16));
#endif
	AppendBitmapMenuItem(InsertMenu,		cmd::id("subtitle/insert/before"),			_("&Before Current"),_("Inserts a line before current"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(InsertMenu,		cmd::id("subtitle/insert/after"),			_("&After Current"),_("Inserts a line after current"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(InsertMenu,		cmd::id("subtitle/insert/before/videotime"),_("Before Current, at Video Time"),_("Inserts a line before current, starting at video time"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(InsertMenu,		cmd::id("subtitle/insert/after/videotime"),	_("After Current, at Video Time"),_("Inserts a line after current, starting at video time"),GETIMAGE(blank_button_16));
	subtitlesMenu->Append(InsertParent);
	AppendBitmapMenuItem(subtitlesMenu,		cmd::id("edit/line/duplicate"),				MakeHotkeyText(_("&Duplicate Lines"), _T("Grid duplicate rows")),_("Duplicate the selected lines"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(subtitlesMenu,		cmd::id("edit/line/duplicate/shift"),		MakeHotkeyText(_("&Duplicate and Shift by 1 Frame"), _T("Grid duplicate and shift one frame")),_("Duplicate lines and shift by one frame"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(subtitlesMenu,		cmd::id("edit/line/delete"),				MakeHotkeyText(_("Delete Lines"), _T("Grid delete rows")),_("Delete currently selected lines"),GETIMAGE(delete_button_16));
	subtitlesMenu->AppendSeparator();
	wxMenu *JoinMenu = new wxMenu;
	wxMenuItem *JoinParent = new wxMenuItem(subtitlesMenu,ID_SM_SUBTITLES_JOIN,	_("Join Lines"),_T(""),wxITEM_NORMAL,JoinMenu);
#ifndef __APPLE__
	JoinParent->SetBitmap(GETIMAGE(blank_button_16));
#endif
	AppendBitmapMenuItem(JoinMenu,			cmd::id("edit/line/join/concatenate"),	_("&Concatenate"),_("Joins selected lines in a single one, concatenating text together"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(JoinMenu,			cmd::id("edit/line/join/keep_first"),	_("Keep &First"),_("Joins selected lines in a single one, keeping text of first and discarding remaining"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(JoinMenu,			cmd::id("edit/line/join/as_karaoke"),	_("As &Karaoke"),_("Joins selected lines in a single one, as karaoke"),GETIMAGE(blank_button_16));
	subtitlesMenu->Append(JoinParent);
	AppendBitmapMenuItem(subtitlesMenu,		cmd::id("edit/line/recombine"),			_("Recombine Lines"),_("Recombine subtitles when they have been split and merged"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(subtitlesMenu,		cmd::id("edit/line/split/by_karaoke"),	_("Split Lines (by karaoke)"),_("Uses karaoke timing to split line into multiple smaller lines"),GETIMAGE(blank_button_16));
	subtitlesMenu->AppendSeparator();
	wxMenu *SortMenu = new wxMenu;
	wxMenuItem *SortParent = new wxMenuItem(subtitlesMenu,ID_SM_SUBTITLES_SORT,_("Sort Lines"),_T(""),wxITEM_NORMAL,SortMenu);
#ifndef __APPLE__
	SortParent->SetBitmap(GETIMAGE(sort_times_button_16));
#endif
	AppendBitmapMenuItem(SortMenu,			cmd::id("time/sort/start"),				_("&Start Time"),_("Sort all subtitles by their start times"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(SortMenu,			cmd::id("time/sort/end"),				_("&End Time"),_("Sort all subtitles by their end times"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(SortMenu,			cmd::id("time/sort/style"),				_("St&yle Name"),_("Sort all subtitles by their style names"),GETIMAGE(blank_button_16));
	subtitlesMenu->Append(SortParent);
	AppendBitmapMenuItem(subtitlesMenu,		cmd::id("edit/line/swap"),				_("Swap Lines"),_("Swaps the two selected lines"),GETIMAGE(arrow_sort_16));
	AppendBitmapMenuItem (subtitlesMenu,	cmd::id("tool/line/select"),			MakeHotkeyText(_("Select Lines..."), _T("Select lines")), _("Selects lines based on defined criterea"),GETIMAGE(select_lines_button_16));
	MenuBar->Append(subtitlesMenu, _("&Subtitles"));



// #############
// # TIMING MENU
// #############
	timingMenu = new wxMenu();
	AppendBitmapMenuItem(timingMenu,		cmd::id("time/shift"),	MakeHotkeyText(_("S&hift Times..."), _T("Shift times")), _("Shift subtitles by time or frames"),GETIMAGE(shift_times_toolbutton_16));
	AppendBitmapMenuItem(timingMenu,		cmd::id("tool/time/postprocess"),	_("Timing Post-Processor..."), _("Runs a post-processor for timing to deal with lead-ins, lead-outs, scene timing and etc."), GETIMAGE(timing_processor_toolbutton_16));
	AppendBitmapMenuItem (timingMenu,		cmd::id("tool/time/kanji"),	_("Kanji Timer..."),_("Open Kanji timer"),GETIMAGE(kara_timing_copier_16));
	timingMenu->AppendSeparator();
	AppendBitmapMenuItem(timingMenu,		cmd::id("time/snap/start_video"),	MakeHotkeyText(_("Snap Start to Video"), _T("Set Start To Video")), _("Set start of selected subtitles to current video frame"), GETIMAGE(substart_to_video_16));
	AppendBitmapMenuItem(timingMenu,		cmd::id("time/snap/end_video"),	MakeHotkeyText(_("Snap End to Video"), _T("Set End to Video")), _("Set end of selected subtitles to current video frame"), GETIMAGE(subend_to_video_16));
	AppendBitmapMenuItem(timingMenu,		cmd::id("time/snap/scene"),	MakeHotkeyText(_("Snap to Scene"), _T("Snap to Scene")), _("Set start and end of subtitles to the keyframes around current video frame"), GETIMAGE(snap_subs_to_scene_16));
	AppendBitmapMenuItem(timingMenu,		cmd::id("time/frame/current"),	MakeHotkeyText(_("Shift to Current Frame"), _T("Shift by Current Time")), _("Shift selection so first selected line starts at current frame"), GETIMAGE(shift_to_frame_16));
	timingMenu->AppendSeparator();
	wxMenu *ContinuousMenu = new wxMenu;
	wxMenuItem *ContinuousParent = new wxMenuItem(subtitlesMenu,ID_SM_TIMING_CONTINOUS,_("Make Times Continuous"),_T(""),wxITEM_NORMAL,ContinuousMenu);
#ifndef __APPLE__
	ContinuousParent->SetBitmap(GETIMAGE(blank_button_16));
#endif
	AppendBitmapMenuItem(ContinuousMenu,	cmd::id("time/continous/start"),	_("Change &Start"),_("Changes times of subs so start times begin on previous's end time"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(ContinuousMenu,	cmd::id("time/continous/end"),	_("Change &End"),_("Changes times of subs so end times begin on next's start time"),GETIMAGE(blank_button_16));
	timingMenu->Append(ContinuousParent);
	MenuBar->Append(timingMenu, _("&Timing"));



// ############
// # VIDEO MENU
// ############
	videoMenu = new wxMenu();
	AppendBitmapMenuItem(videoMenu,		cmd::id("video/open"),		_("&Open Video..."), _("Opens a video file"), GETIMAGE(open_video_menu_16));
	AppendBitmapMenuItem(videoMenu,		cmd::id("video/close"),		_("&Close Video"), _("Closes the currently open video file"), GETIMAGE(close_video_menu_16));
	wxMenuItem *RecentVidParent = new wxMenuItem(videoMenu, ID_SM_VIDEO_ID_MENU_RECENT_VIDEO, _("Recent"), _T(""), wxITEM_NORMAL, RecentVids);
	videoMenu->Append(RecentVidParent);
	AppendBitmapMenuItem(videoMenu,		cmd::id("video/open/dummy"),	_("Use Dummy Video..."), _("Opens a video clip with solid colour"), GETIMAGE(use_dummy_video_menu_16));
	AppendBitmapMenuItem(videoMenu,		cmd::id("video/details"),	_("Show Video Details..."), _("Shows video details"), GETIMAGE(show_video_details_menu_16));
	videoMenu->AppendSeparator();
	AppendBitmapMenuItem(videoMenu,		cmd::id("timecode/open"),	_("Open Timecodes File..."), _("Opens a VFR timecodes v1 or v2 file"), GETIMAGE(open_timecodes_menu_16));
	AppendBitmapMenuItem(videoMenu,		cmd::id("timecode/save"),	_("Save Timecodes File..."), _("Saves a VFR timecodes v2 file"), GETIMAGE(save_timecodes_menu_16));
	AppendBitmapMenuItem(videoMenu,		cmd::id("timecode/close"),	_("Close Timecodes File"), _("Closes the currently open timecodes file"), GETIMAGE(close_timecodes_menu_16))->Enable(false);
	wxMenuItem *RecentTimesParent = new wxMenuItem(videoMenu, ID_SM_VIDEO_ID_MENU_RECENT_TIMECODES, _("Recent"), _T(""), wxITEM_NORMAL, RecentTimecodes);
	videoMenu->Append(RecentTimesParent);
	videoMenu->AppendSeparator();
	AppendBitmapMenuItem(videoMenu,		cmd::id("keyframe/open"),	_("Open Keyframes..."), _("Opens a keyframe list file"), GETIMAGE(open_keyframes_menu_16));
	AppendBitmapMenuItem(videoMenu,		cmd::id("keyframe/save"),	_("Save Keyframes..."), _("Saves the current keyframe list"), GETIMAGE(save_keyframes_menu_16))->Enable(false);
	AppendBitmapMenuItem(videoMenu,		cmd::id("keyframe/close"),	_("Close Keyframes"), _("Closes the currently open keyframes list"), GETIMAGE(close_keyframes_menu_16))->Enable(false);
	wxMenuItem *RecentKeyframesParent = new wxMenuItem(videoMenu, ID_SM_VIDEO_ID_MENU_RECENT_KEYFRAMES, _("Recent"), _T(""), wxITEM_NORMAL, RecentKeyframes);
	videoMenu->Append(RecentKeyframesParent);
	videoMenu->AppendSeparator();
	AppendBitmapMenuItem(videoMenu,		cmd::id("video/detach"),	_("Detach Video"), _("Detach video, displaying it in a separate Window."), GETIMAGE(detach_video_menu_16));

	wxMenu *ZoomMenu = new wxMenu;
	wxMenuItem *ZoomParent = new wxMenuItem(subtitlesMenu,ID_SM_VIDEO_ZOOM,_("Set Zoom"),_T(""),wxITEM_NORMAL,ZoomMenu);
#ifndef __APPLE__
	ZoomParent->SetBitmap(GETIMAGE(set_zoom_menu_16));
#endif
	ZoomMenu->Append(					cmd::id("video/zoom/50"),	MakeHotkeyText(_T("&50%"), _T("Zoom 50%")), _("Set zoom to 50%"));
	ZoomMenu->Append(					cmd::id("video/zoom/100"),	MakeHotkeyText(_T("&100%"), _T("Zoom 100%")), _("Set zoom to 100%"));
	ZoomMenu->Append(					cmd::id("video/zoom/200"),	MakeHotkeyText(_T("&200%"), _T("Zoom 200%")), _("Set zoom to 200%"));
	videoMenu->Append(ZoomParent);
	wxMenu *AspectMenu = new wxMenu;
	wxMenuItem *AspectParent = new wxMenuItem(subtitlesMenu,ID_SM_VIDEO_OVERRIDE_AR,_("Override Aspect Ratio"),_T(""),wxITEM_NORMAL,AspectMenu);
#ifndef __APPLE__
	AspectParent->SetBitmap(GETIMAGE(override_aspect_menu_16));
#endif
	AspectMenu->AppendCheckItem(		cmd::id("video/aspect/default"),	_("&Default"), _("Leave video on original aspect ratio"));
	AspectMenu->AppendCheckItem(		cmd::id("video/aspect/full"),		_("&Fullscreen (4:3)"), _("Forces video to 4:3 aspect ratio"));
	AspectMenu->AppendCheckItem(		cmd::id("video/aspect/wide"),		_("&Widescreen (16:9)"), _("Forces video to 16:9 aspect ratio"));
	AspectMenu->AppendCheckItem(		cmd::id("video/aspect/cinematic"),	_("&Cinematic (2.35)"), _("Forces video to 2.35 aspect ratio"));
	AspectMenu->AppendCheckItem(		cmd::id("video/aspect/custom"),		_("Custom..."), _("Forces video to a custom aspect ratio"));
	videoMenu->Append(AspectParent);
	videoMenu->AppendCheckItem(			cmd::id("video/show_overscan"),	_("Show Overscan Mask"), _("Show a mask over the video, indicating areas that might get cropped off by overscan on televisions."));
//  This is broken as you can't use Check() on a menu item that has a bitmap.
//	AppendBitmapMenuItem(videoMenu, Menu_Video_Overscan, _("Show Overscan Mask"), _("Show a mask over the video, indicating areas that might get cropped off by overscan on televisions."), GETIMAGE(show_overscan_menu_checked_16));
	videoMenu->AppendSeparator();
	AppendBitmapMenuItem(videoMenu,		cmd::id("video/jump"),	MakeHotkeyText(_("&Jump to..."), _T("Video Jump")), _("Jump to frame or time"), GETIMAGE(jumpto_button_16));
	AppendBitmapMenuItem(videoMenu,		cmd::id("video/jump/start"),	MakeHotkeyText(_("Jump Video to Start"), _T("Jump Video To Start")), _("Jumps the video to the start frame of current subtitle"), GETIMAGE(video_to_substart_16));
	AppendBitmapMenuItem(videoMenu,		cmd::id("video/jump/end"),	MakeHotkeyText(_("Jump Video to End"), _T("Jump Video To End")), _("Jumps the video to the end frame of current subtitle"), GETIMAGE(video_to_subend_16));
	MenuBar->Append(videoMenu, _("&Video"));



// ##########
// AUDIO MENU
// ##########
	audioMenu = new wxMenu();
	AppendBitmapMenuItem(audioMenu,		cmd::id("audio/open"),	_("&Open Audio File..."), _("Opens an audio file"), GETIMAGE(open_audio_menu_16));
	AppendBitmapMenuItem(audioMenu,		cmd::id("audio/open/video"),	_("Open Audio from &Video"), _("Opens the audio from the current video file"), GETIMAGE(open_audio_from_video_menu_16));
	AppendBitmapMenuItem(audioMenu,		cmd::id("audio/close"),	_("&Close Audio"), _("Closes the currently open audio file"), GETIMAGE(close_audio_menu_16));
	wxMenuItem *RecentAudParent = new wxMenuItem(audioMenu, ID_SM_AUDIO_ID_MENU_RECENT_AUDIO, _("Recent"), _T(""), wxITEM_NORMAL, RecentAuds);
	audioMenu->Append(RecentAudParent);
	audioMenu->AppendSeparator();
	audioMenu->Append(cmd::id("audio/view/spectrum"), _("Spectrum display"), _("Display audio as a frequency-power spectrogrph"), wxITEM_RADIO);
	audioMenu->Append(cmd::id("audio/view/waveform"), _("Waveform display"), _("Display audio as a linear amplitude graph"), wxITEM_RADIO);
#ifdef _DEBUG
	audioMenu->AppendSeparator();
	audioMenu->Append(					cmd::id("audio/open/blank"),	_T("Open 2h30 Blank Audio"), _T("Open a 150 minutes blank audio clip, for debugging"));
	audioMenu->Append(					cmd::id("audio/open/noise"),	_T("Open 2h30 Noise Audio"), _T("Open a 150 minutes noise-filled audio clip, for debugging"));
#endif
	MenuBar->Append(audioMenu, _("&Audio"));



// ###############
// AUTOMATION MENU
// ###############
#ifdef WITH_AUTOMATION
	automationMenu = new wxMenu();
	AppendBitmapMenuItem (automationMenu,	cmd::id("am/manager"), _("&Automation..."),_("Open automation manager"), GETIMAGE(automation_toolbutton_16));
	automationMenu->AppendSeparator();
	MenuBar->Append(automationMenu, _("&Automation"));
#endif



// #########
// VIEW MENU
// #########
	viewMenu = new wxMenu();
	AppendBitmapMenuItem(viewMenu,	cmd::id("app/language"),			_T("&Language..."), _("Select Aegisub interface language"), GETIMAGE(languages_menu_16));
	AppendBitmapMenuItem(viewMenu,	cmd::id("app/options"),				MakeHotkeyText(_("&Options..."), _T("Options")), _("Configure Aegisub"), GETIMAGE(options_button_16));
	viewMenu->AppendSeparator();
	viewMenu->AppendRadioItem(		cmd::id("app/display/subs"),		_("Subs Only View"), _("Display subtitles only"));
	viewMenu->AppendRadioItem(		cmd::id("app/display/video_subs"),	_("Video+Subs View"), _("Display video and subtitles only"));
	viewMenu->AppendRadioItem(		cmd::id("app/display/audio_subs"),	_("Audio+Subs View"), _("Display audio and subtitles only"));
	viewMenu->AppendRadioItem(		cmd::id("app/display/full"),		_("Full view"), _("Display audio, video and subtitles"));
	viewMenu->AppendSeparator();
	viewMenu->AppendRadioItem(		cmd::id("grid/tags/show"),		_("Show Tags"), _("Show full override tags in the subtitle grid"));
	viewMenu->AppendRadioItem(		cmd::id("grid/tags/simplify"),	_("Simplify Tags"), _("Replace override tags in the subtitle grid with a simplified placeholder"));
	viewMenu->AppendRadioItem(		cmd::id("grid/tags/hide"),		_("Hide Tags"), _("Hide override tags in the subtitle grid"));
	MenuBar->Append(viewMenu, _("Vie&w"));



// #########
// HELP MENU
// #########
	helpMenu = new wxMenu();
	AppendBitmapMenuItem (helpMenu,		cmd::id("help/contents"), MakeHotkeyText(_("&Contents..."), _T("Help")), _("Help topics"), GETIMAGE(contents_button_16));
#ifdef __WXMAC__
	AppendBitmapMenuItem (helpMenu,		cmd::id("help/files"), MakeHotkeyText(_("&All Files") + _T("..."), _T("Help")), _("Help topics"), GETIMAGE(contents_button_16));
#endif
	helpMenu->AppendSeparator();
	AppendBitmapMenuItem(helpMenu,		cmd::id("help/website"), _("&Website..."), _("Visit Aegisub's official website"),GETIMAGE(website_button_16));
	AppendBitmapMenuItem(helpMenu,		cmd::id("help/forums"), _("&Forums..."), _("Visit Aegisub's forums"),GETIMAGE(forums_button_16));
	AppendBitmapMenuItem(helpMenu,		cmd::id("help/bugs"), _("&Bug Tracker..."), _("Visit Aegisub's bug tracker to report bugs and request new features"),GETIMAGE(bugtracker_button_16));
	AppendBitmapMenuItem (helpMenu,		cmd::id("help/irc"), _("&IRC Channel..."), _("Visit Aegisub's official IRC channel"), GETIMAGE(irc_button_16));
#ifndef __WXMAC__
	helpMenu->AppendSeparator();
#endif
	AppendBitmapMenuItem(helpMenu,		cmd::id("app/updates"), _("&Check for Updates..."), _("Check to see if there is a new version of Aegisub available"),GETIMAGE(blank_button_16));
	AppendBitmapMenuItem(helpMenu,		cmd::id("app/about"), _("&About..."), _("About Aegisub"),GETIMAGE(about_menu_16));
	AppendBitmapMenuItem(helpMenu,		cmd::id("app/log"), _("&Log window..."), _("Aegisub event log"),GETIMAGE(about_menu_16));
	MenuBar->Append(helpMenu, _("&Help"));

	// Set the bar as this frame's
	SetMenuBar(MenuBar);

	// Set menu created flag
	menuCreated = true;
}

/// @brief Initialize contents 
void FrameMain::InitContents() {
	AssFile::top = ass = new AssFile;
	temp_context.ass = ass;
	ass->AddCommitListener(&FrameMain::OnSubtitlesFileChanged, this);

	// Set a background panel
	StartupLog(_T("Create background panel"));
	Panel = new wxPanel(this,-1,wxDefaultPosition,wxDefaultSize,wxTAB_TRAVERSAL | wxCLIP_CHILDREN);

	// Video area;
	StartupLog(_T("Create video box"));
	videoBox = new VideoBox(Panel, false, ZoomBox, ass);
	temp_context.videoBox = videoBox;
	VideoContext::Get()->audio = audioController;
	wxBoxSizer *videoSizer = new wxBoxSizer(wxVERTICAL);
	videoSizer->Add(videoBox, 0, wxEXPAND);
	videoSizer->AddStretchSpacer(1);

	// Subtitles area
	StartupLog(_T("Create subtitles grid"));
	SubsGrid = new SubtitlesGrid(this,Panel,-1,ass,wxDefaultPosition,wxSize(600,100),wxWANTS_CHARS | wxSUNKEN_BORDER,_T("Subs grid"));
	temp_context.SubsGrid = SubsGrid;
	videoBox->videoSlider->grid = SubsGrid;
	VideoContext::Get()->grid = SubsGrid;
	Search.grid = SubsGrid;

	// Tools area
	StartupLog(_T("Create tool area splitter window"));
	audioSash = new wxSashWindow(Panel, ID_SASH_MAIN_AUDIO, wxDefaultPosition, wxDefaultSize, wxSW_3D|wxCLIP_CHILDREN);
	wxBoxSizer *audioSashSizer = new wxBoxSizer(wxHORIZONTAL);
	audioSash->SetSashVisible(wxSASH_BOTTOM, true);

	// Audio area
	StartupLog(_T("Create audio box"));
	audioBox = new AudioBox(audioSash, audioController, SubsGrid, ass);
	temp_context.audioBox = audioBox;
	audioBox->frameMain = this;
	audioSashSizer->Add(audioBox, 1, wxEXPAND);
	audioSash->SetSizer(audioSashSizer);
	audioBox->Fit();
	audioSash->SetMinimumSizeY(audioBox->GetSize().GetHeight());

	// Editing area
	StartupLog(_T("Create subtitle editing box"));
	EditBox = new SubsEditBox(Panel,SubsGrid);
	temp_context.EditBox = EditBox;

	// Set sizers/hints
	StartupLog(_T("Arrange main sizers"));
	ToolsSizer = new wxBoxSizer(wxVERTICAL);
	ToolsSizer->Add(audioSash, 0, wxEXPAND);
	ToolsSizer->Add(EditBox, 1, wxEXPAND);
	TopSizer = new wxBoxSizer(wxHORIZONTAL);
	TopSizer->Add(videoSizer, 0, wxEXPAND, 0);
	TopSizer->Add(ToolsSizer, 1, wxEXPAND | wxLEFT | wxRIGHT | wxBOTTOM, 5);
	MainSizer = new wxBoxSizer(wxVERTICAL);
	MainSizer->Add(new wxStaticLine(Panel),0,wxEXPAND | wxALL,0);
	MainSizer->Add(TopSizer,0,wxEXPAND | wxALL,0);
	MainSizer->Add(SubsGrid,1,wxEXPAND | wxALL,0);
	Panel->SetSizer(MainSizer);
	//MainSizer->SetSizeHints(Panel);
	//SetSizer(MainSizer);

	// Set display
	StartupLog(_T("Perform layout"));
	Layout();
	StartupLog(_T("Set focus to edting box"));
	EditBox->TextEdit->SetFocus();
	StartupLog(_T("Leaving InitContents"));
}

/// @brief Deinitialize controls 
void FrameMain::DeInitContents() {
	if (detachedVideo) detachedVideo->Destroy();
	if (stylingAssistant) stylingAssistant->Destroy();
	SubsGrid->ClearMaps();
	delete audioBox;
	delete EditBox;
	delete videoBox;
	delete ass;
	HelpButton::ClearPages();
	VideoContext::Get()->audio = NULL;
}

/// @brief Update toolbar 
void FrameMain::UpdateToolbar() {
	// Collect flags
	bool isVideo = VideoContext::Get()->IsLoaded();
	HasSelection = true;
	int selRows = SubsGrid->GetNumberSelection();

	// Update
	wxToolBar* toolbar = GetToolBar();
	toolbar->FindById(cmd::id("video/jump"))->Enable(isVideo);
	toolbar->FindById(cmd::id("video/zoom/in"))->Enable(isVideo && !detachedVideo);
	toolbar->FindById(cmd::id("video/zoom/out"))->Enable(isVideo && !detachedVideo);
	ZoomBox->Enable(isVideo && !detachedVideo);

	toolbar->FindById(cmd::id("video/jump/start"))->Enable(isVideo && selRows > 0);
	toolbar->FindById(cmd::id("video/jump/end"))->Enable(isVideo && selRows > 0);

	toolbar->FindById(cmd::id("time/snap/start_video"))->Enable(isVideo && selRows == 1);
	toolbar->FindById(cmd::id("time/snap/end_video"))->Enable(isVideo && selRows == 1);

	toolbar->FindById(cmd::id("subtitle/select/visible"))->Enable(isVideo);
	toolbar->FindById(cmd::id("time/snap/scene"))->Enable(isVideo && selRows > 0);
	toolbar->FindById(cmd::id("time/snap/frame"))->Enable(isVideo && selRows > 0);
	toolbar->Realize();
}

/// @brief Open subtitles 
/// @param filename 
/// @param charset  
void FrameMain::LoadSubtitles (wxString filename,wxString charset) {
	// First check if there is some loaded
	if (ass && ass->loaded) {
		if (TryToCloseSubs() == wxCANCEL) return;
	}

	// Setup
	bool isFile = !filename.empty();

	// Load
	try {
		// File exists?
		if (isFile) {
			wxFileName fileCheck(filename);
			if (!fileCheck.FileExists()) {
				throw agi::FileNotFoundError(STD_STR(filename));
			}

			// Make sure that file isn't actually a timecode file
			try {
				TextFileReader testSubs(filename,charset);
				wxString cur = testSubs.ReadLineFromFile();
				if (cur.Left(10) == _T("# timecode")) {
					LoadVFR(filename);
					OPT_SET("Path/Last/Timecodes")->SetString(STD_STR(fileCheck.GetPath()));
					return;
				}
			}
			catch (...) {
				// if trying to load the file as timecodes fails it's fairly
				// safe to assume that it is in fact not a timecode file
			}
		}

		// Proceed into loading
		SubsGrid->ClearMaps();
		if (isFile) {
			ass->Load(filename,charset);
			if (SubsGrid->GetRows()) {
				SubsGrid->SetActiveLine(SubsGrid->GetDialogue(0));
				SubsGrid->SelectRow(0);
			}
			wxFileName fn(filename);
			StandardPaths::SetPathValue(_T("?script"),fn.GetPath());
			OPT_SET("Path/Last/Subtitles")->SetString(STD_STR(fn.GetPath()));
		}
		else {
			SubsGrid->LoadDefault();
			StandardPaths::SetPathValue(_T("?script"),_T(""));
		}
		SubsGrid->SetColumnWidths();
	}
	catch (agi::FileNotFoundError const&) {
		wxMessageBox(filename + L" not found.", L"Error", wxOK | wxICON_ERROR, NULL);
		config::mru->Remove("Subtitle", STD_STR(filename));
		return;
	}
	catch (const wchar_t *err) {
		wxMessageBox(wxString(err), _T("Error"), wxOK | wxICON_ERROR, NULL);
		return;
	}
	catch (wxString err) {
		wxMessageBox(err, _T("Error"), wxOK | wxICON_ERROR, NULL);
		return;
	}
	catch (...) {
		wxMessageBox(_T("Unknown error"), _T("Error"), wxOK | wxICON_ERROR, NULL);
		return;
	}

	// Save copy
	wxFileName origfile(filename);
	if (ass->CanSave() && OPT_GET("App/Auto/Backup")->GetBool() && origfile.FileExists()) {
		// Get path
		wxString path = lagi_wxString(OPT_GET("Path/Auto/Backup")->GetString());
		if (path.IsEmpty()) path = origfile.GetPath();
		wxFileName dstpath(path);
		if (!dstpath.IsAbsolute()) path = StandardPaths::DecodePathMaybeRelative(path, _T("?user/"));
		path += _T("/");
		dstpath.Assign(path);
		if (!dstpath.DirExists()) wxMkdir(path);

		// Save
		wxString backup = path + origfile.GetName() + _T(".ORIGINAL.") + origfile.GetExt();
		wxCopyFile(filename,backup,true);
	}

	// Sync
	SynchronizeProject(true);

	// Update title bar
	UpdateTitle();
}

/// @brief Save subtitles 
/// @param saveas      
/// @param withCharset 
/// @return 
bool FrameMain::SaveSubtitles(bool saveas,bool withCharset) {
	// Try to get filename from file
	wxString filename;
	if (saveas == false && ass->CanSave()) filename = ass->filename;

	// Failed, ask user
	if (filename.IsEmpty()) {
		VideoContext::Get()->Stop();
		wxString path = lagi_wxString(OPT_GET("Path/Last/Subtitles")->GetString());
		wxFileName origPath(ass->filename);
		filename =  wxFileSelector(_("Save subtitles file"),path,origPath.GetName() + _T(".ass"),_T("ass"),AssFile::GetWildcardList(1),wxFD_SAVE | wxFD_OVERWRITE_PROMPT,this);
	}

	// Actually save
	if (!filename.empty()) {
		// Store path
		wxFileName filepath(filename);
		OPT_SET("Path/Last/Subtitles")->SetString(STD_STR(filepath.GetPath()));

		// Fix me, ghetto hack for correct relative path generation in SynchronizeProject()
		ass->filename = filename;

		// Synchronize
		SynchronizeProject();

		// Get charset
		wxString charset = _T("");
		if (withCharset) {
			charset = wxGetSingleChoice(_("Choose charset code:"), _T("Charset"),agi::charset::GetEncodingsList<wxArrayString>(),this,-1, -1,true,250,200);
			if (charset.IsEmpty()) return false;
		}

		// Save
		try {
			ass->Save(filename,true,true,charset);
			UpdateTitle();
		}
		catch (const agi::Exception& err) {
			wxMessageBox(lagi_wxString(err.GetMessage()), "Error", wxOK | wxICON_ERROR, NULL);
			return false;
		}
		catch (const wchar_t *err) {
			wxMessageBox(wxString(err), _T("Error"), wxOK | wxICON_ERROR, NULL);
			return false;
		}
		catch (...) {
			wxMessageBox(_T("Unknown error"), _T("Error"), wxOK | wxICON_ERROR, NULL);
			return false;
		}
		return true;
	}
	return false;
}

/// @brief Try to close subtitles 
/// @param enableCancel 
/// @return 
int FrameMain::TryToCloseSubs(bool enableCancel) {
	if (ass->IsModified()) {
		int flags = wxYES_NO;
		if (enableCancel) flags |= wxCANCEL;
		int result = wxMessageBox(_("Save before continuing?"), _("Unsaved changes"), flags,this);
		if (result == wxYES) {
			// If it fails saving, return cancel anyway
			if (SaveSubtitles(false)) return wxYES;
			else return wxCANCEL;
		}
		return result;
	}
	else return wxYES;
}

/// @brief Set the video and audio display visibilty
/// @param video -1: leave unchanged; 0: hide; 1: show
/// @param audio -1: leave unchanged; 0: hide; 1: show
void FrameMain::SetDisplayMode(int video, int audio) {
	if (!IsShownOnScreen()) return;

	bool sv = false, sa = false;

	if (video == -1) sv = showVideo;
	else if (video)  sv = VideoContext::Get()->IsLoaded() && !detachedVideo;

	if (audio == -1) sa = showAudio;
	else if (audio)  sa = audioController->IsAudioOpen();

	// See if anything changed
	if (sv == showVideo && sa == showAudio) return;

	showVideo = sv;
	showAudio = sa;

	bool didFreeze = !IsFrozen();
	if (didFreeze) Freeze();

	VideoContext::Get()->Stop();

	// Set display
	TopSizer->Show(videoBox, showVideo, true);
	ToolsSizer->Show(audioSash, showAudio, true);

	// Update
	UpdateToolbar();
	MainSizer->CalcMin();
	MainSizer->RecalcSizes();
	MainSizer->Layout();
	Layout();

	if (didFreeze) Thaw();
}

/// @brief Update title bar 
void FrameMain::UpdateTitle() {
	// Determine if current subs are modified
	bool subsMod = ass->IsModified();
	
	// Create ideal title
	wxString newTitle = _T("");
#ifndef __WXMAC__
	if (subsMod) newTitle << _T("* ");
	if (ass->filename != _T("")) {
		wxFileName file (ass->filename);
		newTitle << file.GetFullName();
	}
	else newTitle << _("Untitled");
	newTitle << _T(" - Aegisub ") << GetAegisubLongVersionString();
#else
	// Apple HIG says "untitled" should not be capitalised
	// and the window is a document window, it shouldn't contain the app name
	// (The app name is already present in the menu bar)
	if (ass->filename != _T("")) {
		wxFileName file (ass->filename);
		newTitle << file.GetFullName();
	}
	else newTitle << _("untitled");
#endif

#if defined(__WXMAC__) && !defined(__LP64__)
	// On Mac, set the mark in the close button
	OSXSetModified(subsMod);
#endif

	// Get current title
	wxString curTitle = GetTitle();
	if (curTitle != newTitle) SetTitle(newTitle);
}

/// @brief Updates subs with video/whatever data 
/// @param fromSubs 
void FrameMain::SynchronizeProject(bool fromSubs) {
	// Retrieve data from subs
	if (fromSubs) {
		// Reset the state
		long videoPos = 0;
		long videoAr = 0;
		double videoArValue = 0.0;
		double videoZoom = 0.;

		// Get AR
		wxString arString = ass->GetScriptInfo(_T("Video Aspect Ratio"));
		if (arString.Left(1) == _T("c")) {
			videoAr = 4;
			arString = arString.Mid(1);
			arString.ToDouble(&videoArValue);
		}
		else if (arString.IsNumber()) arString.ToLong(&videoAr);

		// Get new state info
		ass->GetScriptInfo(_T("Video Position")).ToLong(&videoPos);
		ass->GetScriptInfo(_T("Video Zoom Percent")).ToDouble(&videoZoom);
		wxString curSubsVideo = DecodeRelativePath(ass->GetScriptInfo(_T("Video File")),ass->filename);
		wxString curSubsVFR = DecodeRelativePath(ass->GetScriptInfo(_T("VFR File")),ass->filename);
		wxString curSubsKeyframes = DecodeRelativePath(ass->GetScriptInfo(_T("Keyframes File")),ass->filename);
		wxString curSubsAudio = DecodeRelativePath(ass->GetScriptInfo(_T("Audio URI")),ass->filename);
		wxString AutoScriptString = ass->GetScriptInfo(_T("Automation Scripts"));

		// Check if there is anything to change
		int autoLoadMode = OPT_GET("App/Auto/Load Linked Files")->GetInt();
		bool hasToLoad = false;
		if (curSubsAudio !=audioController->GetAudioURL() ||
			curSubsVFR != VideoContext::Get()->GetTimecodesName() ||
			curSubsVideo != VideoContext::Get()->videoName ||
			curSubsKeyframes != VideoContext::Get()->GetKeyFramesName()
#ifdef WITH_AUTOMATION
			|| !AutoScriptString.IsEmpty() || local_scripts->GetScripts().size() > 0
#endif
			) {
			hasToLoad = true;
		}

		// Decide whether to load or not
		bool doLoad = false;
		if (hasToLoad) {
			if (autoLoadMode == 1) doLoad = true;
			else if (autoLoadMode == 2) {
				int result = wxMessageBox(_("Do you want to load/unload the associated files?"),_("(Un)Load files?"),wxYES_NO);
				if (result == wxYES) doLoad = true;
			}
		}

		if (doLoad) {
			// Video
			if (curSubsVideo != VideoContext::Get()->videoName) {
				LoadVideo(curSubsVideo);
				if (VideoContext::Get()->IsLoaded()) {
					VideoContext::Get()->SetAspectRatio(videoAr,videoArValue);
					videoBox->videoDisplay->SetZoom(videoZoom);
					VideoContext::Get()->JumpToFrame(videoPos);
				}
			}

			VideoContext::Get()->LoadTimecodes(curSubsVFR);
			VideoContext::Get()->LoadKeyframes(curSubsKeyframes);

			// Audio
			if (curSubsAudio != audioController->GetAudioURL()) {
				audioController->OpenAudio(curSubsAudio);
			}

			// Automation scripts
#ifdef WITH_AUTOMATION
			local_scripts->RemoveAll();
			wxStringTokenizer tok(AutoScriptString, _T("|"), wxTOKEN_STRTOK);
			wxFileName assfn(ass->filename);
			wxString autobasefn(lagi_wxString(OPT_GET("Path/Automation/Base")->GetString()));
			while (tok.HasMoreTokens()) {
				wxString sfnames = tok.GetNextToken().Trim(true).Trim(false);
				wxString sfnamel = sfnames.Left(1);
				sfnames.Remove(0, 1);
				wxString basepath;
				if (sfnamel == _T("~")) {
					basepath = assfn.GetPath();
				} else if (sfnamel == _T("$")) {
					basepath = autobasefn;
				} else if (sfnamel == _T("/")) {
					basepath = _T("");
				} else {
					wxLogWarning(_T("Automation Script referenced with unknown location specifier character.\nLocation specifier found: %s\nFilename specified: %s"),
						sfnamel.c_str(), sfnames.c_str());
					continue;
				}
				wxFileName sfname(sfnames);
				sfname.MakeAbsolute(basepath);
				if (sfname.FileExists()) {
					sfnames = sfname.GetFullPath();
					local_scripts->Add(Automation4::ScriptFactory::CreateFromFile(sfnames, true));
				} else {
					wxLogWarning(_T("Automation Script referenced could not be found.\nFilename specified: %s%s\nSearched relative to: %s\nResolved filename: %s"),
						sfnamel.c_str(), sfnames.c_str(), basepath.c_str(), sfname.GetFullPath().c_str());
				}
			}
#endif
		}

		// Display
		SetDisplayMode(1,1);
	}

	// Store data on ass
	else {
		// Setup
		wxString seekpos = _T("0");
		wxString ar = _T("0");
		wxString zoom = _T("6");
		if (VideoContext::Get()->IsLoaded()) {
			seekpos = wxString::Format(_T("%i"),VideoContext::Get()->GetFrameN());
			zoom = wxString::Format(_T("%f"),videoBox->videoDisplay->GetZoom());

			int arType = VideoContext::Get()->GetAspectRatioType();
			if (arType == 4) ar = wxString(_T("c")) + AegiFloatToString(VideoContext::Get()->GetAspectRatioValue());
			else ar = wxString::Format(_T("%i"),arType);
		}
		
		// Store audio data
		ass->SetScriptInfo(_T("Audio URI"),MakeRelativePath(audioController->GetAudioURL(),ass->filename));

		// Store video data
		ass->SetScriptInfo(_T("Video File"),MakeRelativePath(VideoContext::Get()->videoName,ass->filename));
		ass->SetScriptInfo(_T("Video Aspect Ratio"),ar);
		ass->SetScriptInfo(_T("Video Zoom Percent"),zoom);
		ass->SetScriptInfo(_T("Video Position"),seekpos);
		ass->SetScriptInfo(_T("VFR File"),MakeRelativePath(VideoContext::Get()->GetTimecodesName(),ass->filename));
		ass->SetScriptInfo(_T("Keyframes File"),MakeRelativePath(VideoContext::Get()->GetKeyFramesName(),ass->filename));

		// Store Automation script data
		// Algorithm:
		// 1. If script filename has Automation Base Path as a prefix, the path is relative to that (ie. "$")
		// 2. Otherwise try making it relative to the ass filename
		// 3. If step 2 failed, or absolut path is shorter than path relative to ass, use absolute path ("/")
		// 4. Otherwise, use path relative to ass ("~")
#ifdef WITH_AUTOMATION
		wxString scripts_string;
		wxString autobasefn(lagi_wxString(OPT_GET("Path/Automation/Base")->GetString()));

		const std::vector<Automation4::Script*> &scripts = local_scripts->GetScripts();
		for (unsigned int i = 0; i < scripts.size(); i++) {
			Automation4::Script *script = scripts[i];

			if (i != 0)
				scripts_string += _T("|");

			wxString autobase_rel, assfile_rel;
			wxString scriptfn(script->GetFilename());
			autobase_rel = MakeRelativePath(scriptfn, autobasefn);
			assfile_rel = MakeRelativePath(scriptfn, ass->filename);

			if (autobase_rel.size() <= scriptfn.size() && autobase_rel.size() <= assfile_rel.size()) {
				scriptfn = _T("$") + autobase_rel;
			} else if (assfile_rel.size() <= scriptfn.size() && assfile_rel.size() <= autobase_rel.size()) {
				scriptfn = _T("~") + assfile_rel;
			} else {
				scriptfn = _T("/") + wxFileName(scriptfn).GetFullPath(wxPATH_UNIX);
			}

			scripts_string += scriptfn;
		}
		ass->SetScriptInfo(_T("Automation Scripts"), scripts_string);
#endif
	}
}

/// @brief Loads video 
/// @param file     
/// @param autoload 
void FrameMain::LoadVideo(wxString file,bool autoload) {
	if (blockVideoLoad) return;
	Freeze();
	try {
		VideoContext::Get()->SetVideo(file);
	}
	catch (const wchar_t *error) {
		wxMessageBox(error, _T("Error opening video file"), wxOK | wxICON_ERROR, this);
	}
	catch (...) {
		wxMessageBox(_T("Unknown error"), _T("Error opening video file"), wxOK | wxICON_ERROR, this);
	}

	if (VideoContext::Get()->IsLoaded()) {
		int vidx = VideoContext::Get()->GetWidth(), vidy = VideoContext::Get()->GetHeight();

		// Set zoom level based on video resolution and window size
		double zoom = videoBox->videoDisplay->GetZoom();
		wxSize windowSize = GetSize();
		if (vidx*3*zoom > windowSize.GetX()*4 || vidy*4*zoom > windowSize.GetY()*6)
			videoBox->videoDisplay->SetZoom(zoom * .25);
		else if (vidx*3*zoom > windowSize.GetX()*2 || vidy*4*zoom > windowSize.GetY()*3)
			videoBox->videoDisplay->SetZoom(zoom * .5);

		// Check that the video size matches the script video size specified
		int scriptx = SubsGrid->ass->GetScriptInfoAsInt(_T("PlayResX"));
		int scripty = SubsGrid->ass->GetScriptInfoAsInt(_T("PlayResY"));
		if (scriptx != vidx || scripty != vidy) {
			switch (OPT_GET("Video/Check Script Res")->GetInt()) {
				case 1:
					// Ask to change on mismatch
					if (wxMessageBox(wxString::Format(_("The resolution of the loaded video and the resolution specified for the subtitles don't match.\n\nVideo resolution:\t%d x %d\nScript resolution:\t%d x %d\n\nChange subtitles resolution to match video?"), vidx, vidy, scriptx, scripty), _("Resolution mismatch"), wxYES_NO, this) != wxYES)
						break;
					// Fallthrough to case 2
				case 2:
					// Always change script res
					SubsGrid->ass->SetScriptInfo(_T("PlayResX"), wxString::Format(_T("%d"), vidx));
					SubsGrid->ass->SetScriptInfo(_T("PlayResY"), wxString::Format(_T("%d"), vidy));
					SubsGrid->ass->Commit(_("Change script resolution"));
					break;
				case 0:
				default:
					// Never change
					break;
			}
		}
	}

	SetDisplayMode(1,-1);
	EditBox->UpdateFrameTiming();

	DetachVideo(VideoContext::Get()->IsLoaded() && OPT_GET("Video/Detached/Enabled")->GetBool());
	Thaw();
}

void FrameMain::LoadVFR(wxString filename) {
	if (filename.empty()) {
		VideoContext::Get()->CloseTimecodes();
	}
	else {
		VideoContext::Get()->LoadTimecodes(filename);
	}
	EditBox->UpdateFrameTiming();
}

/// @brief Open help 
void FrameMain::OpenHelp(wxString) {
	HelpButton::OpenPage(_T("Main"));
}

/// @brief Detach video window 
/// @param detach 
void FrameMain::DetachVideo(bool detach) {
	if (detach) {
		if (!detachedVideo) {
			detachedVideo = new DialogDetachedVideo(this, videoBox->videoDisplay->GetClientSize());
			temp_context.detachedVideo = detachedVideo;
			detachedVideo->Show();
		}
	}
	else if (detachedVideo) {
		detachedVideo->Destroy();
		detachedVideo = NULL;
		SetDisplayMode(1,-1);
	}
	UpdateToolbar();
}

/// @brief Sets status and clear after n milliseconds
/// @param text 
/// @param ms   
void FrameMain::StatusTimeout(wxString text,int ms) {
	SetStatusText(text,1);
	StatusClear.SetOwner(this, ID_APP_TIMER_STATUSCLEAR);
	StatusClear.Start(ms,true);
}

/// @brief Setup accelerator table 
void FrameMain::SetAccelerators() {
	std::vector<wxAcceleratorEntry> entry;
	entry.reserve(32);

	// Standard
	entry.push_back(Hotkeys.GetAccelerator(_T("Video global prev frame"),cmd::id("video/frame/prev")));
	entry.push_back(Hotkeys.GetAccelerator(_T("Video global next frame"),cmd::id("video/frame/next")));
	entry.push_back(Hotkeys.GetAccelerator(_T("Video global focus seek"),cmd::id("video/focus_seek")));
	entry.push_back(Hotkeys.GetAccelerator(_T("Grid global prev line"),cmd::id("grid/line/prev")));
	entry.push_back(Hotkeys.GetAccelerator(_T("Grid global next line"),cmd::id("grid/line/next")));
	entry.push_back(Hotkeys.GetAccelerator(_T("Save Subtitles Alt"),cmd::id("subtitle/save")));
	entry.push_back(Hotkeys.GetAccelerator(_T("Video global zoom in"),cmd::id("video/zoom/in")));
	entry.push_back(Hotkeys.GetAccelerator(_T("Video global zoom out"),cmd::id("video/zoom/out")));
	entry.push_back(Hotkeys.GetAccelerator(_T("Video global play"),cmd::id("video/frame/play")));

	// Medusa
	bool medusaPlay = OPT_GET("Audio/Medusa Timing Hotkeys")->GetBool();
	if (medusaPlay && audioController->IsAudioOpen()) {
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Play"),cmd::id("medusa/play")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Stop"),cmd::id("medusa/stop")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Play Before"),cmd::id("medusa/play/before")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Play After"),cmd::id("medusa/play/after")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Next"),cmd::id("medusa/next")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Previous"),cmd::id("medusa/previous")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Shift Start Forward"),cmd::id("medusa/shift/start/forward")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Shift Start Back"),cmd::id("medusa/shift/start/back")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Shift End Forward"),cmd::id("medusa/shift/end/forward")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Shift End Back"),cmd::id("medusa/shift/end/back")));
		entry.push_back(Hotkeys.GetAccelerator(_T("Audio Medusa Enter"),cmd::id("medusa/enter")));
	}

	// Set table
	wxAcceleratorTable table(entry.size(),&entry[0]);
	SetAcceleratorTable(table);
}

/// @brief Load list of files 
/// @param list 
/// @return 
bool FrameMain::LoadList(wxArrayString list) {
	// Build list
	wxArrayString List;
	for (size_t i=0;i<list.Count();i++) {
		wxFileName file(list[i]);
		if (file.IsRelative()) file.MakeAbsolute();
		if (file.FileExists()) List.Add(file.GetFullPath());
	}

	// Video formats
	wxArrayString videoList;
	videoList.Add(_T("avi"));
	videoList.Add(_T("mkv"));
	videoList.Add(_T("mp4"));
	videoList.Add(_T("d2v"));
	videoList.Add(_T("mpg"));
	videoList.Add(_T("mpeg"));
	videoList.Add(_T("ogm"));
	videoList.Add(_T("avs"));
	videoList.Add(_T("wmv"));
	videoList.Add(_T("asf"));
	videoList.Add(_T("mov"));
	videoList.Add(_T("rm"));
	videoList.Add(_T("y4m"));
	videoList.Add(_T("yuv"));

	// Subtitle formats
	wxArrayString subsList;
	subsList.Add(_T("ass"));
	subsList.Add(_T("ssa"));
	subsList.Add(_T("srt"));
	subsList.Add(_T("sub"));
	subsList.Add(_T("txt"));
	subsList.Add(_T("ttxt"));

	// Audio formats
	wxArrayString audioList;
	audioList.Add(_T("wav"));
	audioList.Add(_T("mp3"));
	audioList.Add(_T("ogg"));
	audioList.Add(_T("wma"));
	audioList.Add(_T("ac3"));
	audioList.Add(_T("aac"));
	audioList.Add(_T("mpc"));
	audioList.Add(_T("ape"));
	audioList.Add(_T("flac"));
	audioList.Add(_T("mka"));
	audioList.Add(_T("m4a"));

	// Scan list
	wxString audio = _T("");
	wxString video = _T("");
	wxString subs = _T("");
	wxString ext;
	for (size_t i=0;i<List.Count();i++) {
		wxFileName file(List[i]);
		ext = file.GetExt().Lower();

		if (subs.IsEmpty() && subsList.Index(ext) != wxNOT_FOUND) subs = List[i];
		if (video.IsEmpty() && videoList.Index(ext) != wxNOT_FOUND) video = List[i];
		if (audio.IsEmpty() && audioList.Index(ext) != wxNOT_FOUND) audio = List[i];
	}

	// Set blocking
	blockVideoLoad = (video != _T(""));

	// Load files
	if (subs != _T("")) {
		LoadSubtitles(subs);
	}
	if (blockVideoLoad) {
		blockVideoLoad = false;
		LoadVideo(video);
	}
	if (!audio.IsEmpty())
		audioController->OpenAudio(audio);

	// Result
	return ((subs != _T("")) || (audio != _T("")) || (video != _T("")));
}


/// @brief Sets the descriptions for undo/redo 
void FrameMain::SetUndoRedoDesc() {
	editMenu->SetHelpString(0,_T("Undo ")+ass->GetUndoDescription());
	editMenu->SetHelpString(1,_T("Redo ")+ass->GetRedoDescription());
}

/// @brief Check if ASSDraw is available 
bool FrameMain::HasASSDraw() {
#ifdef __WINDOWS__
	wxFileName fn(StandardPaths::DecodePath(_T("?data/ASSDraw3.exe")));
	return fn.FileExists();
#else
	return false;
#endif
}

static void autosave_timer_changed(wxTimer *timer, const agi::OptionValue &opt) {
	int freq = opt.GetInt();
	if (freq <= 0) {
		timer->Stop();
	}
	else {
		timer->Start(freq * 1000);
	}
}
