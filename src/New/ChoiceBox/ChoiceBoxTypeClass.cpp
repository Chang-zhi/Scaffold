#include "ChoiceBoxTypeClass.h"

#include <Scaffold.h>
#include <CCINIClass.h>

#include <Utilities/INIParser.h>
#include <Utilities/Stream.h>
#include <Utilities/TemplateDef.h>

#include <cstdio>

template<>
const char* Enumerable<ChoiceBoxTypeClass>::GetMainSection()
{
	return "ChoiceBoxTypes";
}

// ========== ChoiceBoxButton 序列化 ==========
bool ChoiceBoxButton::Load(ScaffoldStreamReader& Stm, bool RegisterForChange)
{
	return Stm.Process(this->Text, RegisterForChange).Success();
}

bool ChoiceBoxButton::Save(ScaffoldStreamWriter& Stm) const
{
	return Stm.Process(const_cast<std::string&>(this->Text)).Success();
}

// ========== INI 加载 ==========
void ChoiceBoxTypeClass::LoadFromINI(CCINIClass* pINI)
{
	const char* section = this->Name;

	if (!pINI->GetSection(section))
		return;

	INI_EX exINI(pINI);

	this->Title.Read(exINI, section, "Title");
	this->Title_Center.Read(exINI, section, "Title.Center");
	this->Description.Read(exINI, section, "Description");
	this->MaxWidth.Read(exINI, section, "MaxWidth");
	if (this->MaxWidth <= 0)
		this->MaxWidth = 250;
	this->BackgroundOpacity.Read(exINI, section, "BackgroundOpacity");
	this->Duration.Read(exINI, section, "Duration");
	this->Button_Count.Read(exINI, section, "Button.Count");
	this->Button_Layout.Read(exINI, section, "Button.Layout");
	this->Button_Mode.Read(exINI, section, "Button.Mode");
	this->Button_Width.Read(exINI, section, "Button.Width");
	this->Button_Height.Read(exINI, section, "Button.Height");
	
	this->Buttons.clear();
	for (int i = 1; i <= this->Button_Count; ++i)
	{
		char key[32];
		std::sprintf(key, "Button.Text%d", i);

		ChoiceBoxButton btn;

		if (exINI.ReadString(section, key))
		{
			btn.Text = exINI.value();
		}

		this->Buttons.push_back(btn);
	}

	this->Color.Read(exINI, section, "Color");
}

// ========== 序列化模板 ==========
template <typename T>
void ChoiceBoxTypeClass::Serialize(T& Stm)
{
	Stm
		.Process(this->Title)
		.Process(this->Title_Center)
		.Process(this->Description)
		.Process(this->Button_Count)
		.Process(this->Button_Layout)
		.Process(this->Button_Mode)
		.Process(this->Button_Width)
		.Process(this->Button_Height)
		.Process(this->Buttons)
		.Process(this->MaxWidth)
		.Process(this->BackgroundOpacity)
		.Process(this->Color)
		.Process(this->Duration)
		;
}

void ChoiceBoxTypeClass::LoadFromStream(ScaffoldStreamReader& Stm)
{
	this->Serialize(Stm);
}

void ChoiceBoxTypeClass::SaveToStream(ScaffoldStreamWriter& Stm)
{
	this->Serialize(Stm);
}
