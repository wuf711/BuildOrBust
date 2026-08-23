// Build or Bust — 术语表(阶段0)。改名/改味只动这张表，不动逻辑。

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BoBTerms.generated.h"

/** 术语表一行：逻辑 Key -> 显示名 + 味道文案。 */
USTRUCT(BlueprintType)
struct FBoBTermRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 界面显示名(如 Key=Anchor -> "序核") */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BoB")
	FText Display;

	/** 味道文案(可空) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="BoB")
	FText Flavor;
};

/**
 *  术语查表：全局唯一入口 UBoBLoc::Term(Key)。
 *  懒加载 /Game/BoB/Data/DT_BoBTerms（缺失则回退成 Key 本身），
 *  后期整体改名/改味/改游戏名只改那张 DataTable，不动逻辑。
 */
UCLASS()
class BUILDORBUST_API UBoBLoc : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** 取显示名；查不到回退为 Key 字符串。 */
	UFUNCTION(BlueprintCallable, Category="BoB|Terms")
	static FText Term(FName Key);

	/** 取味道文案；查不到返回空。 */
	UFUNCTION(BlueprintCallable, Category="BoB|Terms")
	static FText Flavor(FName Key);

	/** 显式指定术语表(可选；不调则懒加载默认路径)。 */
	UFUNCTION(BlueprintCallable, Category="BoB|Terms")
	static void SetTermTable(UDataTable* InTable);

private:
	static const FBoBTermRow* Find(FName Key);
	static TWeakObjectPtr<UDataTable> TermTable;
};
