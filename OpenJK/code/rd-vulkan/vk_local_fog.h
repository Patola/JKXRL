/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace vk_local_fog
{
using Point = std::array<float, 3>;
using Plane = std::array<float, 4>; // inward normal, additive distance
struct Volume
{
	Point mins{}, maxs{}, color{};
	Plane plane{0,0,0,1}; // no visible side: legacy non-surface fog
	float depth = 0;
	bool valid = false;
};
struct Draw
{
	Plane plane{};
	Point color{};
	float depth = 0, eye = 0;
};

inline float Evaluate(const Plane& plane, const float point[3])
{
	return plane[0]*point[0]+plane[1]*point[1]+plane[2]*point[2]+plane[3];
}

// Outward BSP planes, axial bounds first (the legacy BSP compiler contract).
inline Volume Build(const std::vector<Plane>& sides, int visibleSide,
	const Point& color, float depth)
{
	Volume result;
	if (sides.size()<6 || visibleSide < -1 ||
		visibleSide >= static_cast<int>(sides.size()) ||
		!std::isfinite(depth) || depth<=0) return result;
	for (float c : color) if (!std::isfinite(c)) return result;
	for (const auto& p : sides)
		for (float v : p) if (!std::isfinite(v)) return result;
	for (int axis=0;axis<3;++axis)
	{
		for (int side=0;side<2;++side)
			for (int component=0;component<3;++component)
			{
				const float expected = component == axis ? (side ? 1.0f : -1.0f) : 0.0f;
				if (std::fabs(sides[axis*2+side][component]-expected)>.0001f) return result;
			}
		result.mins[axis]=-sides[axis*2][3];
		result.maxs[axis]=sides[axis*2+1][3];
		if (result.mins[axis]>=result.maxs[axis]) return {};
		result.color[axis]=std::clamp(color[axis],0.0f,1.0f);
	}
	if (visibleSide>=0)
	{
		const auto& p=sides[visibleSide];
		const float length=std::sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
		if (length<.0001f) return {};
		result.plane={-p[0]/length,-p[1]/length,-p[2]/length,p[3]/length};
	}
	result.depth=std::max(depth,1.0f);
	result.valid=true;
	return result;
}

inline bool Contains(const Volume& v, const float p[3])
{
	if (!v.valid) return false;
	for (int i=0;i<3;++i) if(p[i]<v.mins[i] || p[i]>v.maxs[i]) return false;
	return true;
}

inline int Select(const std::vector<Volume>& volumes, const Point& mins,
	const Point& maxs, const float eye[3])
{
	int partial=-1, eyePartial=-1;
	for (size_t n=0;n<volumes.size();++n)
	{
		const auto& v=volumes[n];
		if (!v.valid) continue;
		bool overlap=true, inside=true;
		for (int i=0;i<3;++i)
		{
			overlap &= maxs[i]>=v.mins[i] && mins[i]<=v.maxs[i];
			inside &= mins[i]>=v.mins[i] && maxs[i]<=v.maxs[i];
		}
		if (inside) return static_cast<int>(n);
		if (!overlap) continue;
		if (partial<0) partial=static_cast<int>(n);
		if (Contains(v,eye) && eyePartial<0) eyePartial=static_cast<int>(n);
	}
	return eyePartial>=0 ? eyePartial : partial;
}

inline Draw Parameters(const Volume& v, const float eye[3], const float* model=nullptr)
{
	if (!v.valid) return {};
	Draw draw{v.plane,v.color,v.depth,Evaluate(v.plane,eye)};
	if (model)
	{
		for (int column=0;column<4;++column)
			draw.plane[column]=v.plane[0]*model[column*4]+v.plane[1]*model[column*4+1]+
				v.plane[2]*model[column*4+2]+v.plane[3]*model[column*4+3];
	}
	return draw;
}

// Analytic equivalent of CalcFogTexCoords + the square-root fog lookup.
inline float Amount(float distance, float pointDepth, float eyeDepth, float opaqueDepth)
{
	float fraction=1;
	if (eyeDepth<0)
	{
		if (pointDepth<1) return 0;
		fraction=pointDepth/(pointDepth-eyeDepth);
	}
	else if (pointDepth<0) return 0;
	return std::sqrt(std::clamp(distance*fraction/std::max(opaqueDepth,1.0f),0.0f,1.0f));
}
}
