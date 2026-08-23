// Build or Bust — 术语表(阶段0)实现。

#include "BoBTerms.h"

TWeakObjectPtr<UDataTable> UBoBLoc::TermTable;

static const TCHAR* GBoBTermTablePath = TEXT("/Game/BoB/Data/DT_BoBTerms.DT_BoBTerms");

void UBoBLoc::SetTermTable(UDataTable* InTable)
{
	TermTable = InTable;
}

const FBoBTermRow* UBoBLoc::Find(FName Key)
{
	if (!TermTable.IsValid())
	{
		// 只尝试懒加载一次，避免表未建时每帧刷警告
		static bool bAttempted = false;
		if (!bAttempted)
		{
			bAttempted = true;
			TermTable = LoadObject<UDataTable>(nullptr, GBoBTermTablePath);
		}
	}
	if (UDataTable* T = TermTable.Get())
	{
		return T->FindRow<FBoBTermRow>(Key, TEXT("UBoBLoc::Find"), false);
	}
	return nullptr;
}

FText UBoBLoc::Term(FName Key)
{
	if (const FBoBTermRow* Row = Find(Key))
	{
		if (!Row->Display.IsEmpty())
		{
			return Row->Display;
		}
	}
	return FText::FromName(Key);   // 回退：没配就显示 Key 本身，绝不显示空
}

FText UBoBLoc::Flavor(FName Key)
{
	if (const FBoBTermRow* Row = Find(Key))
	{
		return Row->Flavor;
	}
	return FText::GetEmpty();
}
