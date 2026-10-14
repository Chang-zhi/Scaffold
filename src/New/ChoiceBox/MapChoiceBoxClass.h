#pragma once

#include <Scaffold.h>
#include <GeneralStructures.h>
#include <Utilities/SavegameDef.h>

#include <string>
#include <vector>
#include <memory>

class ScaffoldStreamWriter;
class ScaffoldStreamReader;
class ChoiceBoxTypeClass;

class MapChoiceBoxClass
{
public:
	// ===== 公开成员 =====
	int ID { -1 };
	std::string Label;
	const ChoiceBoxTypeClass* Type { nullptr };
	int TypeIndex { -1 };

	// 超时标记：ClickedIndex == TIMEOUT_MARKER 表示选择框因 Duration 耗尽而超时（区别于点击）
	static constexpr int TIMEOUT_MARKER = -2;

	int ClickedIndex { -1 };
	bool ClickedConsumed { false };
	int RemainingFrames { -1 };
	int ClickExpireCounter { -1 };
	int ExpiredCounter { -1 };        // 已销毁实例的宽限清理倒计时（防止长期累积）
	bool IsExpired { false };

	// 是否为"超时未选"状态（Duration 耗尽且从未点击）
	bool IsTimedOut() const { return this->IsExpired && this->ClickedIndex == TIMEOUT_MARKER; }

	// 禁止拷贝
	MapChoiceBoxClass(const MapChoiceBoxClass&) = delete;
	MapChoiceBoxClass& operator=(const MapChoiceBoxClass&) = delete;

	virtual ~MapChoiceBoxClass();

	// ===== 构造 =====
	MapChoiceBoxClass(int id, const char* label, const ChoiceBoxTypeClass* pType);

	// ===== 绘制 =====
	void DrawAt(Point2D centerPos);     // 在指定屏幕坐标绘制选择框

	// ===== 交互 =====
	int PollMouseClick();               // 检测本机点击，返回命中的按钮索引（-1=无）；不修改状态
	void ResetChoice();                 // 重置选择状态

	// ===== 虚接口（派生类实现） =====
	virtual bool CanDraw() const;                           // 是否允许绘制
	virtual bool GetDrawPosition(Point2D& outPos) const = 0;// 获取绘制位置（屏幕坐标）
	virtual bool ClampToScreen() const;                     // 是否将选框完整钳制到屏幕内
	virtual const char* GetTypeMarker() const = 0;          // 类型标记字符串

	// ===== 全局管理 =====
	static std::vector<std::shared_ptr<MapChoiceBoxClass>> Array; // 所有选择框实例

	static void DrawAll();      // 每帧绘制入口（全部类型）
	static void DrawWaypoint(); // 仅绘制路径点选择框
	static void DrawScreen();   // 仅绘制屏幕坐标选择框
	static void TickTimers();   // 每逻辑帧推进计时器
	static void ResetFrameState();
	static void ClearAll();     // 清空所有实例
	static void Clear();        // 清空所有实例

	// 按 ID 查找实例
	static MapChoiceBoxClass* FindByID(int id);

	static void ApplyClickEvent(int boxID, int buttonIndex);

	// 全局存档/读档
	static bool SaveGlobals(ScaffoldStreamWriter& Stm);
	static bool LoadGlobals(ScaffoldStreamReader& Stm);

	// ===== 序列化 =====
	virtual bool Load(ScaffoldStreamReader& Stm, bool RegisterForChange);
	virtual bool Save(ScaffoldStreamWriter& Stm) const;

protected:
	MapChoiceBoxClass() = default;

	template <typename T>
	bool Serialize(T& Stm);

private:
	struct BtnLayoutItem;
	struct ButtonRect
	{
		int Index;
		RectangleStruct Rect;
	};
	mutable std::vector<ButtonRect> m_buttonRects;  // 绘制时更新，故为 mutable

	void UpdateButtonRects(Point2D topLeft, int bgWidth, int buttonsStartY,
		const std::vector<BtnLayoutItem>& btnItems);
};
