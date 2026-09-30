// Copyright NeoMSGO Project. All Rights Reserved.

#include "Core/MSDummyNameLibrary.h"

namespace
{
	const TCHAR* DummyNamePrefix = TEXT("ガンオン");
}

FString MSDummyNames::MakeDummyName(const int32 Index)
{
	return FString::Printf(TEXT("%s%d"), DummyNamePrefix, FMath::Max(1, Index));
}

bool MSDummyNames::IsDummyName(const FString& Name)
{
	FString Clean = Name.TrimStartAndEnd();
	if (!Clean.StartsWith(DummyNamePrefix, ESearchCase::CaseSensitive)) return false;
	const FString Suffix = Clean.Mid(FCString::Strlen(DummyNamePrefix));
	return !Suffix.IsEmpty() && Suffix.IsNumeric();
}
