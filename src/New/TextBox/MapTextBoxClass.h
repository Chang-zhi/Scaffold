#pragma once

#include <Scaffold.h>
#include <GeneralStructures.h>
#include <Utilities/SavegameDef.h>

#include <string>
#include <vector>
#include <memory>

class ScaffoldStreamWriter;
class ScaffoldStreamReader;

class MapTextBoxClass
{
public:
	// ===== 公开成员 =====
	std::string CurrentLabel;       // CSF 标签名（用于查找本地化文本，或直接作为显示文本）
	int MaxLineWidth { 250 };       // 单行最大像素宽度
	int BackgroundOpacity { 75 };   // 背景不透明度 (0-100)
	ColorStruct Color { 255, 215, 0 };  // 文字/边框颜色

	// 禁止拷贝
	MapTextBoxClass(const MapTextBoxClass&) = delete;
	MapTextBoxClass& operator=(const MapTextBoxClass&) = delete;

	virtual ~MapTextBoxClass();

	// ===== 绘制 =====
	void DrawAt(Point2D centerPos);     // 在指定屏幕坐标绘制文本框

	// ===== 布局缓存 =====
	void UpdateLayout();                // 计算换行和背景尺寸
	void ResetCache();                  // 重置缓存，下次绘制时重新布局

	// ===== 虚接口（派生类实现） =====
	virtual bool CanDraw() const;                           // 是否允许绘制
	virtual bool GetDrawPosition(Point2D& outPos) const = 0;// 获取绘制位置（屏幕坐标）
	virtual const char* GetTypeMarker() const = 0;          // 类型标记字符串

	// ===== 全局管理 =====
	static std::vector<std::shared_ptr<MapTextBoxClass>> Array;  // 所有文本框实例

	static void DrawAll();      // 每帧绘制入口
	static void TickTimers();   // 每逻辑帧推进计时器
	static void ResetFrameState();
	static void ClearAll();     // 清空所有实例
	static void Clear();        // 清空所有实例（同 ClearAll）

	// 全局存档/读档
	static bool SaveGlobals(ScaffoldStreamWriter& Stm);
	static bool LoadGlobals(ScaffoldStreamReader& Stm);

	// ===== 序列化 =====
	virtual bool Load(ScaffoldStreamReader& Stm, bool RegisterForChange);
	virtual bool Save(ScaffoldStreamWriter& Stm) const;

protected:
	MapTextBoxClass() = default;    // 默认构造（供反序列化使用）
	MapTextBoxClass(const char* csfLabel,
				  int maxWidth = 250, int opacityPercent = 75,
				  ColorStruct const& color = ColorStruct { 255, 215, 0 });

	int VerticalOffset { 0 };       // 垂直偏移量（预留，暂未使用）

	// 剩余显示帧数：-1 = 由用户手动控制（无限），>=0 时每帧递减，归零时自动移除
	int RemainingFrames { -1 };

	template <typename T>
	bool Serialize(T& Stm);

private:

	struct Cache
	{
		bool IsLayoutDirty { true };                // 是否需要重新计算布局
		std::vector<std::wstring> CachedLines;      // 换行后的文字行数组
		int CachedBgWidth { 0 };                    // 背景框宽度（含内边距）
		int CachedBgHeight { 0 };                   // 背景框高度（含内边距）
	};
	std::unique_ptr<Cache> m_cache;                 // 布局缓存（懒初始化）
};
