#pragma once

// =============================================================================
// ScenarioVariables - 剧本变量(局部 / 全局)的统一访问入口
//
// 数据来源有两条:
//   1. Phobos Interop API(自 API 1.1.0 起提供 Variables_Get/Set{Local,Global})。
//      提供方为基于 std::map 的扩展存储, 值为完整 int, 写入时不存在即创建;
//      Get 约定: S_OK = 变量存在; S_FALSE = 变量不存在(输出置 0); E_FAIL = ScenarioExt 未就绪。
//   2. 原版 ScenarioClass 的固定数组 GlobalVariables[50] / LocalVariables[100], 值仅 1 字节(char)。
//      无提供方时回退到此处。
//
// 关于数量上限:
//   原版变量数量是硬性的(局部 100 / 全局 50, 即上面两个数组的长度), 因而本模块对两条
//   来源施加同一上限, 并把越界明确视为“索引非法”, 而非“变量不存在”。调用方据此可以
//   区分失败原因, 不会再让越界索引静默退化成 0。
//
// 职责边界: 只提供与来源无关的读写, 自身不产生日志等副作用, 便于推断与测试;
// 诊断信息由调用方在拿到失败信号后按需输出。
// =============================================================================
class ScenarioVariables
{
public:
	// 变量作用域。使用强类型枚举, 取代 ReadVar(true/false, ...) 这类布尔盲参。
	enum class Scope
	{
		Local,  // 局部变量: 有效索引 [0, LocalCount)
		Global, // 全局变量: 有效索引 [0, GlobalCount)
	};

	// 各作用域的变量数量, 取自原版数组长度(见 ScenarioClass.h)。
	static constexpr int LocalCount = 100;
	static constexpr int GlobalCount = 50;

	// 返回给定作用域的变量数量。
	static constexpr int Count(Scope scope)
	{
		return scope == Scope::Global ? GlobalCount : LocalCount;
	}

	// 判断索引是否落在该作用域的有效范围内。
	// 越界属于“索引非法”, 与“变量存在但值为 0”是两回事。
	static constexpr bool IsValidIndex(Scope scope, int index)
	{
		return index >= 0 && index < Count(scope);
	}

	// 读取变量。
	// 成功: 写入 outValue 并返回 true。提供方返回 S_FALSE(变量不存在)时同样返回 true,
	//       此时 outValue 为 0 —— 与“不存在的变量读作 0”的既有行为一致。
	// 失败: 索引越界、ScenarioExt 未就绪或读取调用失败时, 不修改 outValue 并返回 false。
	// 优先使用 Interop API, 不可用时回退到原版 ScenarioClass 存储。
	static bool TryRead(Scope scope, int index, int& outValue);

	// 读取变量, 失败时返回 0。
	// 为兼容既有调用点保留此便捷接口; 需要区分“读到 0”与“读取失败”时应改用 TryRead。
	static int Read(Scope scope, int index);

	// 写入变量。成功返回 true; 索引越界或不可写入时返回 false。
	// 原版回退路径按 1 字节存储, 超出 char 范围的值会被截断; 提供方路径存储完整 int。
	// 说明: 当前暂无调用方, 保留备用(供后续由外部写入剧本变量的动作/事件使用)。
	static bool TryWrite(Scope scope, int index, int value);
};
