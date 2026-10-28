#pragma once

#include "FullMapSettings.h"
#include <Windows.h>
#include <Utilities/Debug.h>
#include <array>
#include <cstdint>

// Timings are inclusive: Frame contains all stages, Objects contains Snapshot,
// OreSearch and glyphs, ScaleOpenGL contains its upload/readback. Do not sum them.
namespace FullMapPerf
{
 enum class Stage { Frame, Build, Raster, Scan, Compose, Layout, Input, Acquire,
  ScaleCPU, CPULock, CPUPixels, CPUUnlock, ScaleOpenGL, GLInit, GLUpload, GLDraw,
  GLReadback, GLCopy, GLLock, Snapshot, OreSearch, OreGlow, Objects, ObjectLock,
  Glyphs, Labels, Annotations, Mask, CameraBox, Toolbar, RadarAnimations, ReopenRefresh, CacheValidation, Count };
 enum class Work { BuiltCells, RasterCells, ScanCells, LightChanges, FogChanges,
  OverlayChanges, ComposePixels, CompletedImages, SnapshotObjects, OreCells, ComposeTiles, Count };
 struct Counter { double Total=0, Maximum=0; uint64_t Calls=0; };
 constexpr size_t StageCount=static_cast<size_t>(Stage::Count);
 inline std::array<Counter,StageCount> Counters{};
 inline std::array<double,StageCount> CurrentFrame{};
 inline std::array<uint64_t,static_cast<size_t>(Work::Count)> WorkCounts{};
 inline uint64_t Frames=0;
 inline int64_t LastReport=0,LastSlow=0;
 inline constexpr const char* Names[]={"frame","terrain-build","terrain-raster","visibility-light-scan",
  "fog-light-compose","layout","input","acquire-source","scale-cpu","cpu-surface-lock",
  "cpu-scaled-pixels","cpu-surface-unlock","scale-opengl","gl-init","gl-upload","gl-draw-submit",
  "gl-readback-wait","gl-copy-to-game","gl-game-surface-lock","object-snapshot","ore-search",
  "ore-glow","objects","object-surface-lock","object-glyphs","object-labels-cards",
  "annotations-preview","playable-mask","camera-box","toolbar-status","radar-animations","reopen-refresh","terrain-cache-check"};
 static_assert(sizeof(Names)/sizeof(Names[0])==StageCount);
 inline int64_t Now() { LARGE_INTEGER t{}; QueryPerformanceCounter(&t); return t.QuadPart; }
 inline double Milliseconds(int64_t ticks)
 {
  static const double scale=[](){LARGE_INTEGER f{};QueryPerformanceFrequency(&f);return 1000.0/f.QuadPart;}();
  return ticks*scale;
 }
 inline void AddWork(Work work,uint64_t count)
 {
  if(FullMapSettings::PerformanceLogging)WorkCounts[static_cast<size_t>(work)]+=count;
 }
 class Scope
 {
  Stage stage; int64_t start;
 public:
  explicit Scope(Stage value):stage(value),start(FullMapSettings::PerformanceLogging?Now():0){}
  ~Scope(){Finish();}
  Scope(const Scope&)=delete; Scope& operator=(const Scope&)=delete;
  void Finish()
  {
   if(!start)return;
   const double elapsed=Milliseconds(Now()-start);start=0;
   const auto index=static_cast<size_t>(stage);
   auto& c=Counters[index];c.Total+=elapsed;c.Maximum=std::max(c.Maximum,elapsed);++c.Calls;
   CurrentFrame[index]+=elapsed;
  }
 };
 inline void Reset(){Counters={};CurrentFrame={};WorkCounts={};Frames=0;LastReport=LastSlow=0;}
 inline void BeginFrame()
 {
  if(!FullMapSettings::PerformanceLogging)return;
  CurrentFrame={};if(!LastReport)LastReport=Now();
 }
 inline bool SlowFrame()
 {
  if(!FullMapSettings::PerformanceLogging)return false;
  const auto now=Now();
  if(CurrentFrame[0]<FullMapSettings::SlowFrameThreshold
   ||(LastSlow&&Milliseconds(now-LastSlow)<500))return false;
  LastSlow=now;
  Debug::Log("[FullMap] profile SLOW: total=%.3fms threshold=%dms (inclusive stages below)\n",
   CurrentFrame[0],FullMapSettings::SlowFrameThreshold);
  for(size_t i=1;i<CurrentFrame.size();++i)
   if(CurrentFrame[i]>0)Debug::Log("[FullMap] profile slow %s=%.3fms\n",Names[i],CurrentFrame[i]);
  return true;
 }
 inline bool Report(bool force=false)
 {
  if(!FullMapSettings::PerformanceLogging)return false;
  if(!force)++Frames;
  const auto now=Now();if(!LastReport)LastReport=now;
  const double window=Milliseconds(now-LastReport);
  if((!force&&window<FullMapSettings::PerformanceInterval)||!Frames)return false;
  Debug::Log("[FullMap] profile window: frames=%llu elapsed=%.1fms (inclusive timings; stages overlap)\n",Frames,window);
  for(size_t i=0;i<Counters.size();++i)
  {
   const auto& c=Counters[i];if(!c.Calls)continue;
   Debug::Log("[FullMap] profile %s: calls=%llu avg=%.3fms perFrame=%.3fms max=%.3fms total=%.3fms\n",
    Names[i],c.Calls,c.Total/c.Calls,c.Total/Frames,c.Maximum,c.Total);
  }
  constexpr const char* WorkNames[]={"builtCells","rasterCells","scannedCells","lightChanges","fogChanges",
   "overlayChanges","composePixels","completedImages","snapshotObjects","oreCells","composeTiles"};
  static_assert(sizeof(WorkNames)/sizeof(WorkNames[0])==static_cast<size_t>(Work::Count));
  for(size_t i=0;i<WorkCounts.size();++i)
   if(WorkCounts[i])Debug::Log("[FullMap] profile work %s=%llu\n",WorkNames[i],WorkCounts[i]);
  Counters={};WorkCounts={};Frames=0;LastReport=now;return true;
 }
}
