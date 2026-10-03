/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <array>
#include <algorithm>
#include <cmath>

namespace vk_billboard {
using Point = std::array<float, 3>;
using Matrix = std::array<std::array<float, 4>, 4>;
struct Quad {
	std::array<Point, 4> points{};
	std::array<unsigned, 6> indices{};
	int mode = 0; // 1: square sprite; 2: pivot around the long axis.
};
inline Point Add(Point a, Point b) { for(int i=0;i<3;++i) a[i]+=b[i]; return a; }
inline Point Sub(Point a, Point b) { for(int i=0;i<3;++i) a[i]-=b[i]; return a; }
inline Point Scale(Point a, float s) { for(auto& x:a) x*=s; return a; }
inline float Dot(Point a, Point b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
inline Point Cross(Point a, Point b) { return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}; }
inline bool Normalize(Point& p) {
	const float len=std::sqrt(Dot(p,p));
	if(!std::isfinite(len) || len<1e-6f) return false;
	p=Scale(p,1/len); return true;
}
inline Point Transform(const Matrix& m, Point p) {
	Point out{};
	for(int i=0;i<3;++i) out[i]=m[i][0]*p[0]+m[i][1]*p[1]+m[i][2]*p[2]+m[i][3];
	return out;
}

inline bool Frame(const Quad& quad, Point forward, Point left, Point up, Matrix& matrix)
{
	if(!Normalize(forward) || !Normalize(left) || !Normalize(up)) return false;
	for(const auto& p:quad.points) for(float x:p) if(!std::isfinite(x)) return false;
	for(unsigned i:quad.indices) if(i>=4) return false;
	const auto& p=quad.points;
	auto target=p;
	Point center=Scale(Add(Add(p[0],p[1]),Add(p[2],p[3])),0.25f);
	if(quad.mode==1)
	{
		const Point delta=Sub(p[0],center);
		const float radius=std::sqrt(Dot(delta,delta))*0.707f;
		const Point l=Scale(left,radius), u=Scale(up,radius);
		target={Add(Add(center,l),u),Add(Sub(center,l),u),Sub(Sub(center,l),u),Sub(Add(center,l),u)};
	}
	else if(quad.mode==2)
	{
		constexpr unsigned edges[6][2]={{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
		std::array<unsigned,6> sorted{{0,1,2,3,4,5}};
		const auto length=[&](unsigned e) { const Point d=Sub(p[edges[e][0]],p[edges[e][1]]); return Dot(d,d); };
		std::stable_sort(sorted.begin(),sorted.end(),[&](unsigned a,unsigned b){return length(a)<length(b);});
		const auto* a=edges[sorted[0]]; const auto* b=edges[sorted[1]];
		if(a[0]==b[0] || a[0]==b[1] || a[1]==b[0] || a[1]==b[1]) return false;
		const Point mid[2]={Scale(Add(p[a[0]],p[a[1]]),0.5f),Scale(Add(p[b[0]],p[b[1]]),0.5f)};
		Point major=Sub(mid[1],mid[0]);
		if(!Normalize(major)) return false;
		Point minor=Cross(major,forward);
		// Looking along the strip must not collapse it into a line or produce NaNs.
		if(!Normalize(minor)) { minor=Sub(p[a[0]],p[a[1]]); if(!Normalize(minor)) return false; }
		for(int j=0;j<2;++j)
		{
			const auto* edge=edges[sorted[j]];
			bool ordered=false;
			for(int k=0;k<5;++k) ordered |= quad.indices[k]==edge[0] && quad.indices[k+1]==edge[1];
			const Point width=Scale(minor,std::sqrt(length(sorted[j]))*0.5f*(ordered?-1.0f:1.0f));
			target[edge[0]]=Add(mid[j],width); target[edge[1]]=Sub(mid[j],width);
		}
	}
	else return false;

	const Point e1=Sub(p[1],p[0]),e2=Sub(p[2],p[0]);
	Point normal=Cross(e1,e2), facing=Cross(Sub(target[1],target[0]),Sub(target[2],target[0]));
	if(!Normalize(normal) || !Normalize(facing)) return false;
	const float determinant=Dot(e1,Cross(e2,normal));
	if(std::fabs(determinant)<1e-6f) return false;
	const Point dual1=Scale(Cross(e2,normal),1/determinant),dual2=Scale(Cross(normal,e1),1/determinant);
	const Point f1=Sub(target[1],target[0]),f2=Sub(target[2],target[0]);
	for(int row=0;row<3;++row) {
		for(int col=0;col<3;++col) matrix[row][col]=f1[row]*dual1[col]+f2[row]*dual2[col]+facing[row]*normal[col];
		matrix[row][3]=target[0][row]-matrix[row][0]*p[0][0]-matrix[row][1]*p[0][1]-matrix[row][2]*p[0][2];
	}
	if(Dot(facing,forward)>0) facing=Scale(facing,-1);
	matrix[3]={facing[0],facing[1],facing[2],0};
	const Point error=Sub(Transform(matrix,p[3]),target[3]);
	// Only affine quads are accepted, never silently warp a malformed surface.
	return Dot(error,error)<0.01f;
}
}
