#pragma once
#include "FullMapGeometry.h"
#include <cstdint>

// Experimental, offscreen scaling only. It leaves the game's DirectDraw/WGL
// presentation untouched and returns RGB565 pixels for the existing compositor.
namespace FullMapOpenGL
{
 struct Image
 {
  const uint16_t* Pixels; int Width,Height,Pitch; // Pitch in WORDs, may be negative.
  uint64_t Revision; // 0 forces upload; nonzero changes whenever the source changes.
 };
 struct Result
 {
  const uint16_t* Pixels=nullptr;int Width=0,Height=0,Pitch=0;
  // Valid until the next Scale or Reset; no OpenGL context remains current.
 };
 struct Statistics { uint64_t Uploads=0,Frames=0,UploadedBytes=0; };
 bool Scale(const Image& image, FullMapGeometry::Rect source,
  FullMapGeometry::Rect destination, FullMapGeometry::Rect clip, Result& output);
 void Reset(); // Called on FullMap close/scenario clear, on the render thread.
 bool Active();
 const char* LastError();
 Statistics GetStatistics();
}
