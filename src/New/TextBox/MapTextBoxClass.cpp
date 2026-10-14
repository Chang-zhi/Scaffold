#include "MapTextBoxClass.h"

#include <New/TextBox/TechnoTextBoxClass.h>
#include "WaypointTextBoxClass.h"

#include <StringTable.h>
#include <Surface.h>
#include <Drawing.h>
#include <TacticalClass.h>
#include <WWMouseClass.h>
#include <Unsorted.h>

#include <Utilities/Stream.h>
#include <Utilities/Debug.h>
#include <Utilities/SyncState.h>

#include <cstring>

#include <algorithm>
#include <memory>

// ========== 文本框布局常量 ==========
constexpr static const int PADDINGX = 6;          // 文字与边框之间的水平内边距
constexpr static const int PADDINGY = 4;          // 文字与边框之间的垂直内边距
constexpr static const int LINE_HEIGHT = 18;      // 每行文字的高度（行间距）
constexpr static const int BOTTOM_SAFE_HEIGHT = 0; // 底部安全区域（保留像素，防止被UI遮挡）

// 全局数组：存储所有活跃的文本框实例（基类指针，统一管理生命周期）
std::vector<std::shared_ptr<MapTextBoxClass>> MapTextBoxClass::Array;

namespace
{
	SyncState::FrameGate TimerFrame;
}

void MapTextBoxClass::ResetFrameState()
{
	TimerFrame.Reset();
}

static std::vector<std::wstring> WrapText(const wchar_t* text, int maxWidth)
{
	if (!text || wcslen(text) == 0 || maxWidth <= 0)
		return {};

	std::vector<std::wstring> lines;      // 最终换行结果
	std::wstring wStr(text);              // 源文本副本
	std::wstring currentLine;             // 当前正在累积的行

	for (size_t i = 0; i < wStr.length(); ++i)
	{
		wchar_t ch = wStr[i];

		// 处理显式换行符 \n 或 \r\n
		if (ch == L'\n' || ch == L'\r')
		{
			if (!currentLine.empty())
			{
				lines.push_back(currentLine);
				currentLine.clear();
			}
			if (ch == L'\r' && i + 1 < wStr.length() && wStr[i + 1] == L'\n')
				++i; // 跳过 \r\n 中的 \n
			continue;
		}

		// 过滤掉控制字符（保留制表符 \t）
		if (ch < 0x20 && ch != L'\t')
			continue;

		// 试探性：将当前字符拼接到行尾，测量总宽度
		std::wstring testLine = currentLine + ch;
		RectangleStruct dims = Drawing::GetTextDimensions(testLine.c_str(), { 0, 0 }, 0, 2, 0);

		if (dims.Width > maxWidth)
		{
			// 超过最大宽度，触发换行
			if (currentLine.empty())
			{
				// 单个字符就已经超宽，强制放入该行
				lines.push_back(testLine);
			}
			else
			{
				// 将当前行存入结果，用当前字符另起新行
				lines.push_back(currentLine);
				currentLine = ch;
			}
		}
		else
		{
			// 未超宽，继续累加
			currentLine = testLine;
		}
	}

	// 处理最后剩余的一行
	if (!currentLine.empty())
		lines.push_back(currentLine);

	return lines;
}

// ========== 构造 / 析构 ==========

MapTextBoxClass::~MapTextBoxClass() = default;

/**
 * @brief 构造函数
 *
 * @param csfLabel       CSF 标签名（用于从 StringTable 中查找本地化文本）
 * @param maxWidth       单行最大像素宽度（<=0 时默认 250）
 * @param opacityPercent 背景不透明度（0-100，自动 clamp）
 * @param color          文字/边框颜色
 */
MapTextBoxClass::MapTextBoxClass(const char* csfLabel,
							 int maxWidth, int opacityPercent,
							 ColorStruct const& color)
	: CurrentLabel(csfLabel ? csfLabel : "")
	, MaxLineWidth(maxWidth > 0 ? maxWidth : 250)
	, BackgroundOpacity(std::clamp(opacityPercent, 0, 100))
	, Color(color)
	, m_cache(std::make_unique<Cache>()) // 缓存对象，延迟到首次绘制时初始化
{}

// ========== 布局更新 ==========

void MapTextBoxClass::UpdateLayout()
{
	if (!m_cache)
		m_cache = std::make_unique<Cache>();

	// 优先从 CSF 文件（RA2 的字符串表）中获取本地化文本
	const wchar_t* textPtr = StringTable::TryFetchString(this->CurrentLabel.c_str());

	// Fallback: CSF 标签不存在时，直接将 ANSI 标签名逐字节转成宽字符
	std::wstring fallbackText;
	if (!textPtr || wcslen(textPtr) == 0)
	{
		if (this->CurrentLabel.empty())
		{
			m_cache->CachedLines.clear();
			m_cache->CachedBgWidth = 0;
			m_cache->CachedBgHeight = 0;
			m_cache->IsLayoutDirty = false;
			return;
		}

		// 逐字节转换（标签名通常是 ASCII，不需要 MultiByteToWideChar 的开销）
		fallbackText.reserve(this->CurrentLabel.length());
		for (char ch : this->CurrentLabel)
			fallbackText += static_cast<wchar_t>(static_cast<unsigned char>(ch));
		textPtr = fallbackText.c_str();
	}

	// 再次检查文本是否有效
	if (!textPtr || wcslen(textPtr) == 0)
	{
		m_cache->CachedLines.clear();
		m_cache->CachedBgWidth = 0;
		m_cache->CachedBgHeight = 0;
		m_cache->IsLayoutDirty = false;
		return;
	}

	// 执行自动换行
	m_cache->CachedLines = WrapText(textPtr, this->MaxLineWidth);
	if (m_cache->CachedLines.empty())
	{
		m_cache->CachedBgWidth = 0;
		m_cache->CachedBgHeight = 0;
		m_cache->IsLayoutDirty = false;
		return;
	}

	// 计算最长行的像素宽度
	int maxLineW = 0;
	for (const std::wstring& line : m_cache->CachedLines)
	{
		RectangleStruct dims = Drawing::GetTextDimensions(line.c_str(), { 0, 0 }, 0, 2, 0);
		if (dims.Width > maxLineW)
			maxLineW = dims.Width;
	}

	// 背景框尺寸 = 文字区域 + 内边距
	m_cache->CachedBgWidth = maxLineW + (PADDINGX * 2);
	m_cache->CachedBgHeight = (static_cast<int>(m_cache->CachedLines.size()) * LINE_HEIGHT) + (PADDINGY * 2);
	m_cache->IsLayoutDirty = false;
}

// ========== 缓存管理 ==========

void MapTextBoxClass::ResetCache()
{
	m_cache = std::make_unique<Cache>();
	m_cache->IsLayoutDirty = true;
}

bool MapTextBoxClass::CanDraw() const
{
	return true;
}

// ========== 绘制 ==========

void MapTextBoxClass::DrawAt(Point2D centerPos)
{
	// 检查渲染表面和战术视图是否可用
	if (!DSurface::Composite || !TacticalClass::Instance)
		return;

	// 初始化缓存并进行延迟布局更新
	if (!m_cache)
		m_cache = std::make_unique<Cache>();
	if (m_cache->IsLayoutDirty)
		UpdateLayout();

	// 无有效内容则不绘制
	if (m_cache->CachedLines.empty() || m_cache->CachedBgWidth <= 0 || m_cache->CachedBgHeight <= 0)
		return;

	int bgWidth = m_cache->CachedBgWidth;
	int bgHeight = m_cache->CachedBgHeight;

	// 背景框左上角（居中定位）
	Point2D topLeft = {
		centerPos.X - (bgWidth / 2),
		centerPos.Y - (bgHeight / 2)
	};

	// ===== 底部裁剪 =====
	// 文本框不应超出屏幕底部（避免被界面栏遮挡）
	int viewHeight = DSurface::ViewBounds.Height;
	int clipBottomY = viewHeight - BOTTOM_SAFE_HEIGHT;

	if (topLeft.Y >= clipBottomY)
		return;

	int drawHeight = bgHeight;
	bool isClipped = false;
	if (topLeft.Y + bgHeight > clipBottomY)
	{
		drawHeight = clipBottomY - topLeft.Y;
		isClipped = true;
		if (drawHeight < LINE_HEIGHT)
			return; // 裁剪后连一行都放不下，直接跳过
	}

	// 完全在屏幕左侧或上方，跳过
	if (topLeft.X + bgWidth < 0 || topLeft.Y + drawHeight < 0)
		return;

	RectangleStruct bgRect = { topLeft.X, topLeft.Y, bgWidth, drawHeight };

	// ===== 鼠标悬停检测 =====
	// 鼠标移到文本框上时暂停绘制，让玩家能点选被遮挡的单位
	RectangleStruct fullBgRect = { topLeft.X, topLeft.Y, bgWidth, bgHeight };
	Point2D mousePos = WWMouseClass::Instance->XY1;
	if (mousePos.X >= fullBgRect.X &&
		mousePos.X <= fullBgRect.X + fullBgRect.Width &&
		mousePos.Y >= fullBgRect.Y &&
		mousePos.Y <= fullBgRect.Y + fullBgRect.Height)
	{
		return;
	}

	// ===== 绘制半透明黑色背景 =====
	ColorStruct bgColor = { 0, 0, 0 };
	DSurface::Composite->FillRectTrans(&bgRect, &bgColor, this->BackgroundOpacity);

	// 将颜色转为游戏引擎所需的颜色整数值
	int colorInt = Drawing::RGB_To_Int(this->Color);
	Point2D p1, p2;

	// ===== 绘制边框（上、左、右、下） =====
	// 上边
	p1 = { topLeft.X, topLeft.Y };
	p2 = { topLeft.X + bgWidth - 1, topLeft.Y };
	DSurface::Composite->DrawLine(&p1, &p2, colorInt);

	// 左边
	p1 = { topLeft.X, topLeft.Y };
	p2 = { topLeft.X, topLeft.Y + drawHeight - 1 };
	DSurface::Composite->DrawLine(&p1, &p2, colorInt);

	// 右边
	p1 = { topLeft.X + bgWidth - 1, topLeft.Y };
	p2 = { topLeft.X + bgWidth - 1, topLeft.Y + drawHeight - 1 };
	DSurface::Composite->DrawLine(&p1, &p2, colorInt);

	// 底边（被裁剪时跳过，避免画出界）
	if (!isClipped)
	{
		p1 = { topLeft.X, topLeft.Y + bgHeight - 1 };
		p2 = { topLeft.X + bgWidth - 1, topLeft.Y + bgHeight - 1 };
		DSurface::Composite->DrawLine(&p1, &p2, colorInt);
	}

	// ===== 逐行绘制文字 =====
	RectangleStruct bounds = DSurface::ViewBounds;
	int currentY = topLeft.Y + PADDINGY;
	for (const std::wstring& line : m_cache->CachedLines)
	{
		// 当前行超出裁剪区域则停止
		if (currentY + LINE_HEIGHT > topLeft.Y + drawHeight + PADDINGY)
			break;

		Point2D textPos = { topLeft.X + PADDINGX, currentY };
		DSurface::Composite->DrawText(
			line.c_str(),
			&bounds,
			&textPos,
			colorInt,
			0,
			TextPrintType::Metal12 | TextPrintType::BrightColor);

		currentY += LINE_HEIGHT;
	}
}

// ========== 序列化（存档/读档） ==========

template <typename T>
bool MapTextBoxClass::Serialize(T& Stm)
{
	return Stm
		.Process(this->CurrentLabel)        // CSF 标签名
		.Process(this->MaxLineWidth)        // 最大行宽
		.Process(this->BackgroundOpacity)   // 背景不透明度
		.Process(this->Color)               // 文字/边框颜色
		.Process(this->RemainingFrames)     // 剩余显示帧数
		.Success();
}

void MapTextBoxClass::Clear()
{
	WaypointTextBoxClass::Array.clear();
	TechnoTextBoxClass::Array.clear();
	Array.clear();
}

bool MapTextBoxClass::Load(ScaffoldStreamReader& Stm, bool RegisterForChange)
{
	return Serialize(Stm);
}

bool MapTextBoxClass::Save(ScaffoldStreamWriter& Stm) const
{
	return const_cast<MapTextBoxClass*>(this)->Serialize(Stm);
}



// ========== 逻辑帧计时 ==========

void MapTextBoxClass::TickTimers()
{
	if (!TimerFrame.TryEnter(Unsorted::CurrentFrame))
		return;

	std::vector<std::shared_ptr<MapTextBoxClass>> expired; // 收集本帧过期的实例

	for (auto& pLabel : Array)
	{
		if (!pLabel)
			continue;

		// RemainingFrames >= 0 表示有时限，每逻辑帧减 1，减到 0 时标记为过期
		if (pLabel->RemainingFrames >= 0 && --pLabel->RemainingFrames <= 0)
			expired.push_back(pLabel);
	}

	// ===== 移除过期实例 =====
	// 注意：必须同时从基类 Array 和对应的派生类 Array 中移除
	for (auto& pExpired : expired)
	{
		auto const marker = pExpired->GetTypeMarker();

		// 从派生类数组中移除
		if (std::strcmp(marker, "WaypointTextBoxClass") == 0)
		{
			auto* pRaw = static_cast<WaypointTextBoxClass*>(pExpired.get());
			auto& derived = WaypointTextBoxClass::Array;
			auto it = std::find_if(derived.begin(), derived.end(),
				[pRaw](const auto& sp) { return sp.get() == pRaw; });
			if (it != derived.end())
				derived.erase(it);
		}
		else if (std::strcmp(marker, "TechnoTextBoxClass") == 0)
		{
			auto* pRaw = static_cast<TechnoTextBoxClass*>(pExpired.get());
			auto& derived = TechnoTextBoxClass::Array;
			auto it = std::find_if(derived.begin(), derived.end(),
				[pRaw](const auto& sp) { return sp.get() == pRaw; });
			if (it != derived.end())
				derived.erase(it);
		}

		// 从基类数组中移除
		auto baseIt = std::find(Array.begin(), Array.end(), pExpired);
		if (baseIt != Array.end())
			Array.erase(baseIt);
	}

	// 每帧清理已摧毁单位的残留标签
	TechnoTextBoxClass::CleanupDeadLabels();
}

// ========== 全局绘制入口（渲染帧调用） ==========

void MapTextBoxClass::DrawAll()
{
	for (auto& pLabel : Array)
	{
		if (!pLabel || !pLabel->CanDraw())
			continue;

		Point2D pos {};
		if (!pLabel->GetDrawPosition(pos))
			continue;

		pLabel->DrawAt(pos);
	}
}

/**
 * @brief 清空所有文本框（用于场景重置等）
 */
void MapTextBoxClass::ClearAll()
{
	Array.clear();
}

// ========== 全局存档/读档 ==========

bool MapTextBoxClass::SaveGlobals(ScaffoldStreamWriter& Stm)
{
	Stm.Process(TimerFrame.LastFrame);
	Stm.Save(Array.size());
	for (auto const& item : Array)
	{
		Stm.Save(item.get());                             // 保存旧指针地址
		ScaffoldFixedString<64> marker(item->GetTypeMarker()); // 保存类型标记
		Stm.Save(marker);
		item->Save(Stm);                                  // 保存实例数据
	}
	return true;
}

bool MapTextBoxClass::LoadGlobals(ScaffoldStreamReader& Stm)
{
	// 清除当前所有已存在的实例
	Clear();
	Stm.Process(TimerFrame.LastFrame);

	size_t Count = 0;
	if (!Stm.Load(Count))
		return false;

	Array.reserve(Count);
	for (size_t i = 0; i < Count; ++i)
	{
		void* oldPtr = nullptr;
		ScaffoldFixedString<64> typeMarker;

		if (!Stm.Load(oldPtr) || !Stm.Load(typeMarker))
			return false;

		if (std::strcmp(typeMarker.data(), "WaypointTextBoxClass") == 0)
		{
			auto newObj = std::make_shared<WaypointTextBoxClass>();
			if (!newObj->Load(Stm, false))
				return false;
			WaypointTextBoxClass::Array.push_back(newObj);
			Array.push_back(std::move(newObj));
		}
		else if (std::strcmp(typeMarker.data(), "TechnoTextBoxClass") == 0)
		{
			auto newObj = std::make_shared<TechnoTextBoxClass>();
			if (!newObj->Load(Stm, false))
				return false;
			TechnoTextBoxClass::Array.push_back(newObj);
			Array.push_back(std::move(newObj));
		}
		else
		{
			Debug::Log("[MapTextBoxClass] Warning: unknown type marker \"%s\"!\n",
				typeMarker.data());
			return false;
		}
	}

	return true;
}
