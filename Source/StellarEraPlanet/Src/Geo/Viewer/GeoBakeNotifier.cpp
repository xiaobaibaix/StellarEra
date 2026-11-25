#if WITH_EDITOR
// 后台地质烘焙的编辑器通知：开始一条「正在烘焙…」，结束原地更新为成功/失败再淡出。
#include "VoxelPlanetActor.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

namespace
{
	TSharedPtr<SNotificationItem> GGeoBakeNotification;
	FDelegateHandle GStartedHandle;
	FDelegateHandle GFinishedHandle;

	FText MakeFinishedText(bool bSuccess, double Seconds)
	{
		return FText::FromString(bSuccess
			? FString::Printf(TEXT("地质烘焙完成（%.1f 秒）"), Seconds)
			: TEXT("地质烘焙失败，详见输出日志"));
	}

	void HandleGeoBakeStarted(AVoxelPlanetActor* Actor)
	{
		UE_LOG(LogTemp, Display, TEXT("[通知] 地质烘焙中…（%s）"), *Actor->GetName());
		if (GGeoBakeNotification.IsValid())
		{
			GGeoBakeNotification->ExpireAndFadeout();
			GGeoBakeNotification.Reset();
		}
		FNotificationInfo Info(FText::FromString(FString::Printf(TEXT("地质烘焙中…（%s）"), *Actor->GetName())));
		Info.bFireAndForget = false;
		Info.bUseThrobber = true;
		Info.bUseSuccessFailIcons = false;
		Info.bAllowThrottleWhenFrameRateIsLow = false;
		GGeoBakeNotification = FSlateNotificationManager::Get().AddNotification(Info);
		if (GGeoBakeNotification.IsValid())
		{
			GGeoBakeNotification->SetCompletionState(SNotificationItem::CS_Pending);
		}
	}

	void HandleGeoBakeFinished(AVoxelPlanetActor* /*Actor*/, bool bSuccess, double Seconds)
	{
		UE_LOG(LogTemp, Display, TEXT("[通知] 地质烘焙结束 ok=%d %.1f s"), bSuccess ? 1 : 0, Seconds);
		if (GGeoBakeNotification.IsValid())
		{
			GGeoBakeNotification->SetText(MakeFinishedText(bSuccess, Seconds));
			GGeoBakeNotification->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
			GGeoBakeNotification->ExpireAndFadeout();
			GGeoBakeNotification.Reset();
			return;
		}
		// 没看到开始（例如别处触发的烘焙）：直接出一条结果通知。
		FNotificationInfo Info(MakeFinishedText(bSuccess, Seconds));
		Info.bFireAndForget = true;
		Info.ExpireDuration = 3.f;
		Info.bUseSuccessFailIcons = false;
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

void RegisterGeoBakeNotifications()
{
	GStartedHandle = AVoxelPlanetActor::GeoBakeStarted.AddStatic(&HandleGeoBakeStarted);
	GFinishedHandle = AVoxelPlanetActor::GeoBakeFinished.AddStatic(&HandleGeoBakeFinished);
}

void UnregisterGeoBakeNotifications()
{
	AVoxelPlanetActor::GeoBakeStarted.Remove(GStartedHandle);
	AVoxelPlanetActor::GeoBakeFinished.Remove(GFinishedHandle);
}
#endif
