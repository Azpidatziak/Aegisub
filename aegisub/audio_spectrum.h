// Copyright (c) 2005, 2006, Rodrigo Braz Monteiro
// Copyright (c) 2006, 2007, Niels Martin Hansen
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
// -----------------------------------------------------------------------------
//
// AEGISUB
//
// Website: http://aegisub.cellosoft.com
// Contact: mailto:zeratul@cellosoft.com
//

#ifndef AUDIO_SPECTRUM_H
#define AUDIO_SPECTRUM_H

#include <wx/wxprec.h>
#include <vector>
#include "audio_provider.h"


// Spectrum cache basically caches the raw result of FFT
class AudioSpectrumCache {
public:
	// Type of a single FFT result line
	typedef std::vector<float> CacheLine;

	// Get the overlap'th overlapping FFT in FFT group i, generating it if needed
	virtual CacheLine& GetLine(unsigned long i, unsigned int overlap) = 0;

	// Set the FFT size used
	static void SetLineLength(unsigned long new_length);

	virtual ~AudioSpectrumCache() {};

protected:
	// A cache line containing only zero-values
	static CacheLine null_line;
	// The FFT size
	static unsigned long line_length;
};


class AudioSpectrum {
private:
	// Data provider
	AudioSpectrumCache *cache;

	// Colour pallettes
	unsigned char colours_normal[256*3];
	unsigned char colours_selected[256*3];

	AudioProvider *provider;

	unsigned long line_length; // number of frequency components per line (half of number of samples)
	unsigned long num_lines; // number of lines needed for the audio
	unsigned int fft_overlaps; // number of overlaps used in FFT
	float power_scale; // amplification of displayed power
	int minband; // smallest frequency band displayed
	int maxband; // largest frequency band displayed

public:
	AudioSpectrum(AudioProvider *_provider, unsigned long _line_length);
	~AudioSpectrum();

	void RenderRange(__int64 range_start, __int64 range_end, bool selected, unsigned char *img, int imgleft, int imgwidth, int imgpitch, int imgheight);

	void SetScaling(float _power_scale);
};


#endif
