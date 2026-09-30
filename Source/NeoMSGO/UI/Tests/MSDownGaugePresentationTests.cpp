#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "UI/MSHUDViewModel.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMSDownGaugePresentationTest,
	"NeoMSGO.UI.DownGauge.RemainingSegments",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FMSDownGaugePresentationTest::RunTest(const FString&)
{
	float Remaining=0;
	FLinearColor Color;
	UMSHUDViewModel::EvaluateDownGauge(0,1600,false,Remaining,Color);
	TestEqual(TEXT("undamaged green segment is full"),Remaining,1.f);
	TestTrue(TEXT("first segment is green"),Color.G>Color.R && Color.G>Color.B);
	UMSHUDViewModel::EvaluateDownGauge(799,1600,false,Remaining,Color);
	TestTrue(TEXT("last DP of green is almost empty"),Remaining>0 && Remaining<.002f);
	UMSHUDViewModel::EvaluateDownGauge(800,1800,false,Remaining,Color);
	TestEqual(TEXT("flinch reveals full yellow segment"),Remaining,1.f);
	TestTrue(TEXT("second segment is yellow"),Color.R>Color.B && Color.G>Color.B);
	UMSHUDViewModel::EvaluateDownGauge(1300,1800,false,Remaining,Color);
	TestEqual(TEXT("level-three yellow has its own 1000 DP capacity"),Remaining,.5f);
	UMSHUDViewModel::EvaluateDownGauge(0,1800,true,Remaining,Color);
	TestEqual(TEXT("heavy reaction does not falsely show full gauge after DP reset"),Remaining,0.f);
	UMSHUDViewModel::EvaluateDownGauge(799,1800,false,Remaining,Color);
	TestTrue(TEXT("recovery crossing 800 returns to green"),Color.G>Color.R);
	return true;
}
#endif
