// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include <cstdint>

// Adapted from Forja SX2 (GPL-3.0+, apps 171-173, commits 4f8835f & 5e3e38c) and hardened with a strict
// strip-invariant bailout check.
//
// For smooth-shaded (IIP == 1) GS_TRIANGLE_CLASS draws, GSVertexTraceFMM::FindMinMax is an associative,
// commutative min/max/NaN reduction over every referenced vertex (handling both even and odd counts).
// A triangle strip of N vertices produces ~3*(N-2) indices, so FindMinMax visits interior vertices up to
// 3 times. This single-pass deduplicator collects each unique vertex index in O(i_count) with no bitmap.
//
// Soundness invariant: at every triangle (a, b, c), every previously seen vertex is in out[0..used-1],
// highest == max(out[0..used-1]), and {px, py, pz} are the previous triangle's indices (all in out[]).
// A vertex v is emitted iff v > highest; otherwise it is skipped iff v is in {px, py, pz} (or an earlier
// vertex of the same triangle). Any vertex v <= highest outside that set causes an immediate bailout
// (returns 0), telling the caller to fall back to the original index buffer with zero risk of dropping
// an out-of-order vertex.
//
// Returns the number of unique indices written to `out` (strictly < i_count when beneficial), or 0 on
// bailout / non-beneficial input. `out` must have capacity >= i_count (or at least max_out).
static inline int CalizaDeduplicateStripIndices(const uint16_t* index, int i_count, uint16_t* out, int max_out)
{
	if (i_count < 6 || (i_count % 3) != 0)
		return 0;

	int used = 0;
	int highest = -1;
	int px = -1, py = -1, pz = -1;

	for (int i = 0; i < i_count; i += 3)
	{
		const int a = index[i + 0];
		const int b = index[i + 1];
		const int c = index[i + 2];

		if (a > highest)
		{
			if (used >= max_out)
				return 0;
			out[used++] = static_cast<uint16_t>(a);
			highest = a;
		}
		else if (a != px && a != py && a != pz)
		{
			return 0;
		}

		if (b > highest)
		{
			if (used >= max_out)
				return 0;
			out[used++] = static_cast<uint16_t>(b);
			highest = b;
		}
		else if (b != px && b != py && b != pz && b != a)
		{
			return 0;
		}

		if (c > highest)
		{
			if (used >= max_out)
				return 0;
			out[used++] = static_cast<uint16_t>(c);
			highest = c;
		}
		else if (c != px && c != py && c != pz && c != a && c != b)
		{
			return 0;
		}

		px = a;
		py = b;
		pz = c;
	}

	return (used > 0 && used < i_count) ? used : 0;
}
