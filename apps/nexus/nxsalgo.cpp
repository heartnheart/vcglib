#include <vector>
#include <map>
#include <iostream>

//#include <wrap/strip/tristrip.h>

#include "nxsalgo.h"
#include "nexus.h"

using namespace std;
using namespace nxs;
using namespace vcg;

#include "tristripper/tri_stripper.h"
using namespace triangle_stripper;

void nxs::ComputeNormals(Nexus &nexus) {
  assert(nexus.signature & NXS_NORMALS_SHORT ||
	 nexus.signature & NXS_NORMALS_FLOAT);
  
  bool use_short = (nexus.signature & NXS_NORMALS_SHORT) != 0;

  //TODO use a temporary file to store border normals
  unsigned int tmpb_offset = 0;
  vector<unsigned int> tmpb_start;
  VFile<Point3f> tmpb;
  if(!tmpb.Create("tmpb.tmp")) {
    cerr << "Could not create temporary border file\n";
    exit(0);
  }

  for(unsigned int p = 0; p < nexus.index.size(); p++) {
    Border border = nexus.GetBorder(p);
    tmpb_start.push_back(tmpb_offset);
    tmpb_offset += border.Size();
  }

  Point3f zero(0.0f, 0.0f, 0.0f);
  tmpb.Resize(tmpb_offset);
  for(unsigned int i = 0; i < tmpb.Size(); i++)
    tmpb[i] = zero;
  
  //first step normals in the same patch.
  for(unsigned int p = 0; p < nexus.index.size(); p++) {
    Patch &patch = nexus.GetPatch(p);
    
    vector<Point3f> normals;
    normals.resize(patch.nv, Point3f(0, 0, 0));
    

    if(nexus.signature & NXS_FACES) 
      for(unsigned int i = 0; i < patch.nf; i++) {
	unsigned short *f = patch.Face(i);
	Point3f &v0 = patch.Vert(f[0]);
	Point3f &v1 = patch.Vert(f[1]);
	Point3f &v2 = patch.Vert(f[2]);
	
	Point3f norm = (v1 - v0) ^ (v2 - v0); 
	normals[f[0]] += norm;
	normals[f[1]] += norm;
	normals[f[2]] += norm;
      }

    if(nexus.signature & NXS_STRIP) 
      for(int i = 0; i < patch.nf - 2; i++) {
	unsigned short *f = patch.FaceBegin() + i;
	Point3f &v0 = patch.Vert(f[0]);
	Point3f &v1 = patch.Vert(f[1]);
	Point3f &v2 = patch.Vert(f[2]);
	
	Point3f norm = (v1 - v0) ^ (v2 - v0); 
	if(i%2) norm = -norm;
	normals[f[0]] += norm;
	normals[f[1]] += norm;
	normals[f[2]] += norm;
      }
    
    if(use_short) {
      for(unsigned int i = 0; i < patch.nv; i++) {
	Point3f &norm = normals[i];
	norm.Normalize();
	short *n = patch.Norm16(i);
	for(int k = 0; k < 3; k++) {
	  n[k] = (short)(norm[k] * 32766);
	}
	n[3] = 0;
      }
    } else {
      memcpy(patch.Norm16Begin(), &*normals.begin(), 
	     normals.size() * sizeof(Point3f));
    }
    Border border = nexus.GetBorder(p);

    set<unsigned int> close;
    for(unsigned int i = 0; i < border.Size(); i++) {
      Link &link = border[i];
      if(link.IsNull()) continue;
      unsigned int off = tmpb_start[p];
      tmpb[off + i] += normals[link.start_vert];
      close.insert(link.end_patch);
    }

    set<unsigned int>::iterator k;
    for(k = close.begin(); k != close.end(); k++) {
      Border remote = nexus.GetBorder(*k);
      unsigned int off = tmpb_start[*k];

      for(unsigned int i = 0; i < remote.Size(); i++) {
	Link &link = remote[i];
	if(link.IsNull()) continue;
	if(link.end_patch != p) continue;
	tmpb[off + i] += normals[link.end_vert];
      }
    }
  }

  //Second step unify normals across borders
  for(unsigned int p = 0; p < nexus.index.size(); p++) {
    Patch &patch = nexus.GetPatch(p);
    Border border = nexus.GetBorder(p);

    for(unsigned int i = 0; i < border.Size(); i++) {
      Link &link = border[i];
      if(link.IsNull()) continue;
      unsigned int off = tmpb_start[p];
      Point3f &n = tmpb[off + i];
      n.Normalize();
      if(use_short) {
	n *= 32767;
	short *np = patch.Norm16(link.start_vert);
	np[0] = (short)n[0];
	np[1] = (short)n[1];
	np[2] = (short)n[2];
      } else {
	patch.Norm32(link.start_vert) = n;
      }
    }
  }
  //TODO remove temporary file.
}

/*void nxs::ComputeTriStrip(unsigned short nfaces, unsigned short *faces, 
		  vector<unsigned short> &strip) {
  

  vector<unsigned int> indices;
  indices.resize(nfaces*3);
  for(unsigned int i = 0; i < nfaces*3; i++) {
    indices[i] = faces[i];
  }
  vector<unsigned int> restrip;
  ComputeStrip(indices, restrip);

  unsigned int len = 0;
  for(unsigned int i = 0; i < restrip.size(); i++) {
    if(restrip[i] != 0xffffffff) {
      strip.push_back(restrip[i]);
      len++;
    } else {
      if(i < restrip.size()-1) { //not the last primitive.
	strip.push_back(restrip[i-1]);
	//TODO optimize this!
	if((len%2) == 1) 	//do not change orientation....
	  strip.push_back(restrip[i-1]);
	strip.push_back(restrip[i+1]);
      }			
      len = 0;
    }
  }
}*/

void nxs::ComputeTriStrip(unsigned short nfaces, unsigned short *faces, 
		  vector<unsigned short> &strip) {

  
  vector<unsigned int> index;
  index.resize(nfaces*3);
  for(int i = 0; i < nfaces*3; i++) {
    index[i] = faces[i];
  }
  int cache_size = 0;
  tri_stripper stripper(index);
  stripper.SetCacheSize(cache_size);		
  // = 0 will disable the cache optimizer
  stripper.SetMinStripSize(0);
  tri_stripper::primitives_vector primitives;
  stripper.Strip(&primitives);

  if(primitives.back().m_Indices.size() < 3) {
    primitives.pop_back();
  }
  //TODO spostare questo dentro il ciclo che rimonta le strip.
  if(primitives.back().m_Type == tri_stripper::PT_Triangles) {
    tri_stripper::primitives p;
    p = primitives.back();
    primitives.pop_back();		
    for(unsigned int i = 0; i < p.m_Indices.size(); i += 3) {
      tri_stripper::primitives s;
      s.m_Type = tri_stripper::PT_Triangle_Strip;
      s.m_Indices.push_back(p.m_Indices[i]);
      s.m_Indices.push_back(p.m_Indices[i+1]);
      s.m_Indices.push_back(p.m_Indices[i+2]);
      primitives.push_back(s);
    }
  }
  
  for(unsigned int i = 0; i < primitives.size(); i++) {
    tri_stripper::primitives &primitive = primitives[i];
    assert(primitive.m_Indices.size() != 0);
    int len = primitive.m_Indices.size();
    for(int l = 0; l < len; l++)  		
      strip.push_back(primitive.m_Indices[l]);
    
    
    if(i < primitives.size()-1) { //not the last primitive.
      strip.push_back(primitive.m_Indices[len-1]);
      //TODO optimize this!
      if((len%2) == 1) 	//do not change orientation....
	strip.push_back(primitive.m_Indices[len-1]);
      strip.push_back(primitives[i+1].m_Indices[0]);
    }			
  }
}
