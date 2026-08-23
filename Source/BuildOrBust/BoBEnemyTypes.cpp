// Build or Bust — 敌人型号表实现。

#include "BoBEnemyTypes.h"

TStrongObjectPtr<UDataTable> UBoBEnemyLib::EnemyTable;

static const TCHAR* GBoBEnemyTablePath = TEXT("/Game/BoB/Data/DT_BoBEnemy.DT_BoBEnemy");

void UBoBEnemyLib::SetEnemyTable(UDataTable* InTable)
{
	EnemyTable.Reset(InTable);
}

UDataTable* UBoBEnemyLib::Table()
{
	if (!EnemyTable.IsValid())
	{
		if (UDataTable* Loaded = LoadObject<UDataTable>(nullptr, GBoBEnemyTablePath))
		{
			EnemyTable.Reset(Loaded);
		}
		else
		{
			// 表真的不存在时只抱怨一次，别每帧刷屏。
			// 注意这个闩不能盖住上面的加载——之前那版把"尝试过"和"加载失败"
			// 混成一件事，表被 GC 掉之后就永远不再重新加载了
			static bool bWarned = false;
			if (!bWarned)
			{
				bWarned = true;
				UE_LOG(LogTemp, Warning, TEXT("[BoB] 敌人型号表加载不到: %s"),
					GBoBEnemyTablePath);
			}
		}
	}
	return EnemyTable.Get();
}

const FBoBEnemyRow* UBoBEnemyLib::Find(FName RowName)
{
	if (UDataTable* T = Table())
	{
		return T->FindRow<FBoBEnemyRow>(RowName, TEXT("UBoBEnemyLib::Find"), false);
	}
	return nullptr;
}

bool UBoBEnemyLib::GetEnemyRow(FName RowName, FBoBEnemyRow& OutRow)
{
	if (const FBoBEnemyRow* Row = Find(RowName))
	{
		OutRow = *Row;
		return true;
	}
	return false;
}

TArray<FName> UBoBEnemyLib::AllEnemyIds()
{
	if (UDataTable* T = Table())
	{
		return T->GetRowNames();
	}
	return TArray<FName>();
}

TArray<FName> UBoBEnemyLib::AffordableAt(int32 WaveIndex, int32 BudgetLeft)
{
	TArray<FName> Out;
	UDataTable* T = Table();
	if (!T)
	{
		return Out;
	}

	for (const FName& Id : T->GetRowNames())
	{
		const FBoBEnemyRow* Row = T->FindRow<FBoBEnemyRow>(Id, TEXT("AffordableAt"), false);
		if (!Row)
		{
			continue;
		}
		// Boss 不走预算池，由关卡脚本直接投放
		if (Row->Role == EBoBRole::Boss)
		{
			continue;
		}
		if (Row->WaveUnlock > WaveIndex)
		{
			continue;
		}
		if (Row->SpawnCost > BudgetLeft)
		{
			continue;
		}
		Out.Add(Id);
	}
	return Out;
}

bool UBoBEnemyLib::ResolveAssets(const FBoBEnemyRow& Row, FBoBEnemyRow& OutResolved)
{
	OutResolved = Row;
	if (Row.VariantOf.IsNone())
	{
		return !Row.Mesh.IsNull();
	}

	// 变种不新建 mesh 也不新建动画，全部回基础型号那行取
	const FBoBEnemyRow* Base = Find(Row.VariantOf);
	if (!Base)
	{
		return false;
	}
	OutResolved.Mesh = Base->Mesh;
	OutResolved.AnimMove = Base->AnimMove;
	OutResolved.AnimAttack = Base->AnimAttack;
	OutResolved.AnimDeath = Base->AnimDeath;
	// 音效允许变种自己覆盖（静默传道者就是把 WarnSFX 清空）
	if (OutResolved.WarnSFX.IsNull() && !Base->WarnSFX.IsNull())
	{
		OutResolved.WarnSFX = Base->WarnSFX;
	}
	if (OutResolved.LoopSFX.IsNull())
	{
		OutResolved.LoopSFX = Base->LoopSFX;
	}
	if (OutResolved.DeathSFX.IsNull())
	{
		OutResolved.DeathSFX = Base->DeathSFX;
	}
	// 弱点也继承，除非变种自己指定了别的
	if (OutResolved.WeakpointBone.IsNone())
	{
		OutResolved.WeakpointBone = Base->WeakpointBone;
	}
	return !OutResolved.Mesh.IsNull();
}
