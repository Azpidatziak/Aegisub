// Copyright (c) 2011, Thomas Goyne <plorkyeran@aegisub.org>
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
//
// Aegisub Project http://www.aegisub.org/
//
// $Id$

/// @file ass_karaoke.cpp
/// @brief Parse and manipulate ASSA karaoke tags
/// @ingroup subs_storage
///


#include "config.h"

#include "ass_karaoke.h"

#include "ass_dialogue.h"
#include "ass_file.h"
#include "ass_override.h"
#include "include/aegisub/context.h"
#include "selection_controller.h"

#ifndef AGI_PRE
#include <wx/intl.h>
#endif

wxString AssKaraoke::Syllable::GetText(bool k_tag) const {
	wxString ret;

	if (k_tag)
		ret = wxString::Format("{%s%d}", tag_type, duration / 10);

	size_t idx = 0;
	for (std::map<size_t, wxString>::const_iterator ovr = ovr_tags.begin(); ovr != ovr_tags.end(); ++ovr) {
		ret += text.Mid(idx, ovr->first - idx);
		ret += ovr->second;
		idx = ovr->first;
	}
	ret += text.Mid(idx);
	return ret;
}


AssKaraoke::AssKaraoke(AssDialogue *line, bool auto_split, bool normalize)
: no_announce(false)
{
	if (line) SetLine(line, auto_split, normalize);
}

void AssKaraoke::SetLine(AssDialogue *line, bool auto_split, bool normalize) {
	active_line = line;
	line->ParseASSTags();

	syls.clear();
	Syllable syl;
	syl.start_time = line->Start;
	syl.duration = 0;
	syl.tag_type = "\\k";

	for (size_t i = 0; i < line->Blocks.size(); ++i) {
		AssDialogueBlock *block = line->Blocks[i];
		wxString text = block->GetText();

		if (dynamic_cast<AssDialogueBlockPlain*>(block)) {
			// treat comments as overrides rather than dialogue
			if (text.size() && text[0] == '{')
				syl.ovr_tags[syl.text.size()] += text;
			else
				syl.text += text;
		}
		else if (dynamic_cast<AssDialogueBlockDrawing*>(block)) {
			// drawings aren't override tags but they shouldn't show up in the
			// stripped text so pretend they are
			syl.ovr_tags[syl.text.size()] += text;
		}
		else if (AssDialogueBlockOverride *ovr = dynamic_cast<AssDialogueBlockOverride*>(block)) {
			bool in_tag = false;
			for (size_t j = 0; j < ovr->Tags.size(); ++j) {
				AssOverrideTag *tag = ovr->Tags[j];

				if (tag->IsValid() && tag->Name.Left(2).Lower() == "\\k") {
					if (in_tag) {
						syl.ovr_tags[syl.text.size()] += "}";
						in_tag = false;
					}

					// Dealing with both \K and \kf is mildly annoying so just
					// convert them both to \kf
					if (tag->Name == "\\K") tag->Name = "\\kf";

					// Don't bother including zero duration zero length syls
					if (syl.duration > 0 || !syl.text.empty()) {
						syls.push_back(syl);
						syl.text.clear();
						syl.ovr_tags.clear();
					}

					syl.tag_type = tag->Name;
					syl.start_time += syl.duration;
					syl.duration = tag->Params[0]->Get(0) * 10;
				}
				else {
					wxString& otext = syl.ovr_tags[syl.text.size()];
					// Merge adjacent override tags
					if (j == 0 && otext.size())
						otext.RemoveLast();
					else if (!in_tag)
						otext += "{";

					in_tag = true;
					otext += *tag;
				}
			}

			if (in_tag)
				syl.ovr_tags[syl.text.size()] += "}";
		}
	}

	syls.push_back(syl);

	line->ClearBlocks();

	if (normalize) {
		// Normalize the syllables so that the total duration is equal to the line length
		int end_time = active_line->End;
		int last_end = syl.start_time + syl.duration;

		// Total duration is shorter than the line length so just extend the last
		// syllable; this has no effect on rendering but is easier to work with
		if (last_end < end_time)
			syls.back().duration += end_time - last_end;
		else if (last_end > end_time) {
			// Shrink each syllable proportionately
			int start_time = active_line->Start;
			double scale_factor = double(end_time - start_time) / (last_end - start_time);

			for (size_t i = 0; i < size(); ++i) {
				syls[i].start_time = start_time + scale_factor * (syls[i].start_time - start_time);
			}

			for (int i = size() - 1; i > 0; --i) {
				syls[i].duration = end_time - syls[i].start_time;
				end_time = syls[i].start_time;
			}
		}
	}

	// Add karaoke splits at each space
	if (auto_split && syls.size() == 1) {
		size_t pos;
		no_announce = true;
		while ((pos = syls.back().text.find(' ')) != wxString::npos)
			AddSplit(syls.size() - 1, pos + 1);
		no_announce = false;
	}

	AnnounceSyllablesChanged();
}

wxString AssKaraoke::GetText() const {
	wxString text;
	text.reserve(size() * 10);

	for (iterator it = begin(); it != end(); ++it) {
		text += it->GetText(true);
	}

	return text;
}

wxString AssKaraoke::GetTagType() const {
	return begin()->tag_type;
}

void AssKaraoke::SetTagType(wxString const& new_type) {
	for (size_t i = 0; i < size(); ++i) {
		syls[i].tag_type = new_type;
	}
}

void AssKaraoke::AddSplit(size_t syl_idx, size_t pos) {
	syls.insert(syls.begin() + syl_idx + 1, Syllable());
	Syllable &syl = syls[syl_idx];
	Syllable &new_syl = syls[syl_idx + 1];

	// If the syl is empty or the user is adding a syllable past the last
	// character then pos will be out of bounds. Doing this is a bit goofy,
	// but it's sometimes required for complex karaoke scripts
	if (pos < syl.text.size()) {
		new_syl.text = syl.text.Mid(pos);
		syl.text = syl.text.Left(pos);
	}

	if (new_syl.text.empty())
		new_syl.duration = 0;
	else {
		new_syl.duration = syl.duration * new_syl.text.size() / (syl.text.size() + new_syl.text.size());
		syl.duration -= new_syl.duration;
	}

	assert(syl.duration >= 0);

	new_syl.start_time = syl.start_time + syl.duration;
	new_syl.tag_type = syl.tag_type;

	// Move all override tags after the split to the new syllable and fix the indices
	size_t text_len = syl.text.size();
	for (ovr_iterator it = syl.ovr_tags.begin(); it != syl.ovr_tags.end(); ) {
		if (it->first < text_len)
			++it;
		else {
			new_syl.ovr_tags[it->first - text_len] = it->second;
			syl.ovr_tags.erase(it++);
		}
	}

	if (!no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::RemoveSplit(size_t syl_idx) {
	// Don't allow removing the first syllable
	if (syl_idx == 0) return;

	Syllable &syl = syls[syl_idx];
	Syllable &prev = syls[syl_idx - 1];

	prev.duration += syl.duration;
	for (ovr_iterator it = syl.ovr_tags.begin(); it != syl.ovr_tags.end(); ++it) {
		prev.ovr_tags[it->first + prev.text.size()] = it->second;
	}
	prev.text += syl.text;

	syls.erase(syls.begin() + syl_idx);

	if (!no_announce) AnnounceSyllablesChanged();
}

void AssKaraoke::SetStartTime(size_t syl_idx, int time) {
	// Don't allow moving the first syllable
	if (syl_idx == 0) return;

	Syllable &syl = syls[syl_idx];
	Syllable &prev = syls[syl_idx - 1];

	assert(time >= prev.start_time);
	assert(time <= syl.start_time + syl.duration);

	int delta = time - syl.start_time;
	syl.start_time = time;
	syl.duration -= delta;
	prev.duration += delta;
}

void AssKaraoke::SetLineTimes(int start_time, int end_time) {
	assert(end_time >= start_time);

	size_t idx = 0;
	do {
		int delta = start_time - syls[idx].start_time;
		syls[idx].start_time = start_time;
		syls[idx].duration = std::max(0, syls[idx].duration - delta);
	} while (++idx < syls.size() && syls[idx].start_time < start_time);

	idx = syls.size() - 1;
	while (syls[idx].start_time > end_time) {
		syls[idx].start_time = end_time;
		syls[idx].duration = 0;
		--idx;
	}
	syls[idx].duration = end_time - syls[idx].start_time;
}

void AssKaraoke::SplitLines(std::set<AssDialogue*> const& lines, agi::Context *c) {
	if (lines.empty()) return;

	AssKaraoke kara;

	std::set<AssDialogue*> sel = c->selectionController->GetSelectedSet();

	bool did_split = false;
	for (std::list<AssEntry*>::iterator it = c->ass->Line.begin(); it != c->ass->Line.end(); ++it) {
		AssDialogue *diag = dynamic_cast<AssDialogue*>(*it);
		if (!diag || !lines.count(diag)) continue;

		kara.SetLine(diag);

		// If there aren't at least two tags there's nothing to split
		if (kara.size() < 2) continue;

		bool in_sel = sel.count(diag) > 0;

		c->ass->Line.erase(it++);

		for (iterator kit = kara.begin(); kit != kara.end(); ++kit) {
			AssDialogue *new_line = new AssDialogue(*diag);

			new_line->Start = kit->start_time;
			new_line->End = kit->start_time + kit->duration;
			new_line->Text = kit->GetText(false);

			c->ass->Line.insert(it, new_line);

			if (in_sel)
				sel.insert(new_line);
		}
		sel.erase(diag);
		delete diag;
		--it;
	}

	AssDialogue *new_active = c->selectionController->GetActiveLine();
	if (!sel.count(c->selectionController->GetActiveLine()))
		new_active = *sel.begin();
	c->selectionController->SetSelectionAndActive(sel, new_active);

	if (did_split)
		c->ass->Commit(_("splitting"), AssFile::COMMIT_DIAG_ADDREM | AssFile::COMMIT_DIAG_FULL);
}
