// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "EngineAnalytics.h"
#include "Misc/Guid.h"
#include "Stats/Stats.h"
#include "Misc/ConfigCacheIni.h"
#include "EngineGlobals.h"
#include "Engine/Engine.h"
#include "Misc/EngineBuildSettings.h"
#include "AnalyticsBuildType.h"
#include "AnalyticsEventAttribute.h"
#include "IAnalyticsProviderET.h"
#include "AnalyticsET.h"
#include "GeneralProjectSettings.h"
#include "EngineSessionManager.h"
#include "Misc/EngineVersion.h"
#include "RHI.h"

#if WITH_EDITOR
#include "EditorAnalyticsSession.h"
#include "EditorSessionSummarySender.h"
#include "Analytics/EditorSessionSummaryWriter.h"
#endif

#include "GenericPlatform/GenericPlatformCrashContext.h"

bool FEngineAnalytics::bIsInitialized;
TSharedPtr<IAnalyticsProviderET> FEngineAnalytics::Analytics;
TSharedPtr<FEngineSessionManager> FEngineAnalytics::SessionManager;

#if WITH_EDITOR
static TSharedPtr<FEditorSessionSummaryWriter> SessionSummaryWriter;
static TSharedPtr<FEditorSessionSummarySender> SessionSummarySender;
#endif

/**
* Default config func.
*/
FAnalyticsET::Config DefaultEngineAnalyticsConfigFunc()
{
	return FAnalyticsET::Config();
}

/**
* Engine analytics config to initialize the analytics provider.
* External code should bind this delegate if engine analytics are desired,
* preferably in private code that won't be redistributed.
*/
TFunction<FAnalyticsET::Config()>& GetEngineAnalyticsConfigFunc()
{
	static TFunction<FAnalyticsET::Config()> Config = &DefaultEngineAnalyticsConfigFunc;
	return Config;
}

/**
 * Get analytics pointer
 */
IAnalyticsProvider& FEngineAnalytics::GetProvider()
{
	checkf(bIsInitialized && IsAvailable(), TEXT("FEngineAnalytics::GetProvider called outside of Initialize/Shutdown."));

	return *Analytics.Get();
}
 
void FEngineAnalytics::Initialize()
{
	checkf(!bIsInitialized, TEXT("FEngineAnalytics::Initialize called more than once."));

	check(GEngine);

#if WITH_EDITOR
	// this will only be true for builds that have editor support (desktop platforms)
	// The idea here is to only send editor events for actual editor runs, not for things like -game runs of the editor.
	bool bIsEditorRun = GIsEditor && !IsRunningCommandlet();
#else
	bool bIsEditorRun = false;
#endif

#if UE_BUILD_DEBUG
	const bool bShouldInitAnalytics = false;
#else
	// Outside of the editor, the only engine analytics usage is the hardware survey
	const bool bShouldInitAnalytics = bIsEditorRun && GEngine->AreEditorAnalyticsEnabled();
#endif

	if (bShouldInitAnalytics)
	{
		// Get the default config.
		FAnalyticsET::Config Config = GetEngineAnalyticsConfigFunc()();
		// Set any fields that weren't set by default.
		if (Config.APIKeyET.IsEmpty())
		{
			// We always use the "Release" analytics account unless we're running in analytics test mode (usually with
			// a command-line parameter), or we're an internal Epic build
			const EAnalyticsBuildType AnalyticsBuildType = GetAnalyticsBuildType();
			const bool bUseReleaseAccount =
				(AnalyticsBuildType == EAnalyticsBuildType::Development || AnalyticsBuildType == EAnalyticsBuildType::Release) &&
				!FEngineBuildSettings::IsInternalBuild();	// Internal Epic build
			const TCHAR* BuildTypeStr = bUseReleaseAccount ? TEXT("Release") : TEXT("Dev");

			FString UE4TypeOverride;
			bool bHasOverride = GConfig->GetString(TEXT("Analytics"), TEXT("UE4TypeOverride"), UE4TypeOverride, GEngineIni);
			const TCHAR* UE4TypeStr = bHasOverride ? *UE4TypeOverride : FEngineBuildSettings::IsPerforceBuild() ? TEXT("Perforce") : TEXT("UnrealEngine");
			Config.APIKeyET = FString::Printf(TEXT("UEEditor.%s.%s"), UE4TypeStr, BuildTypeStr);
		}
		if (Config.APIServerET.IsEmpty())
		{
			Config.APIServerET = TEXT("https://datarouter.ol.epicgames.com/");
		}
		if (Config.AppEnvironment.IsEmpty())
		{
			Config.AppEnvironment = TEXT("datacollector-source");
		}
		if (Config.AppVersionET.IsEmpty())
		{
			Config.AppVersionET = FEngineVersion::Current().ToString();
		}

		// Connect the engine analytics provider (if there is a configuration delegate installed)
		Analytics = FAnalyticsET::Get().CreateAnalyticsProvider(Config);

		if (Analytics.IsValid())
		{
			Analytics->SetUserID(FString::Printf(TEXT("%s|%s|%s"), *FPlatformMisc::GetLoginId(), *FPlatformMisc::GetEpicAccountId(), *FPlatformMisc::GetOperatingSystemId()));

			TArray<FAnalyticsEventAttribute> StartSessionAttributes;
			GEngine->CreateStartupAnalyticsAttributes( StartSessionAttributes );
			// Add project info whether we are in editor or game.
			const UGeneralProjectSettings& ProjectSettings = *GetDefault<UGeneralProjectSettings>();
			FString OSMajor;
			FString OSMinor;
			FPlatformMemoryStats Stats = FPlatformMemory::GetStats();
			StartSessionAttributes.Emplace(TEXT("ProjectName"), ProjectSettings.ProjectName);
			StartSessionAttributes.Emplace(TEXT("ProjectID"), ProjectSettings.ProjectID);
			StartSessionAttributes.Emplace(TEXT("ProjectDescription"), ProjectSettings.Description);
			StartSessionAttributes.Emplace(TEXT("ProjectVersion"), ProjectSettings.ProjectVersion);
			StartSessionAttributes.Emplace(TEXT("GPUVendorID"), GRHIVendorId);
			StartSessionAttributes.Emplace(TEXT("GPUDeviceID"), GRHIDeviceId);
			StartSessionAttributes.Emplace(TEXT("GRHIDeviceRevision"), GRHIDeviceRevision);
			StartSessionAttributes.Emplace(TEXT("GRHIAdapterInternalDriverVersion"), GRHIAdapterInternalDriverVersion);
			StartSessionAttributes.Emplace(TEXT("GRHIAdapterUserDriverVersion"), GRHIAdapterUserDriverVersion);
			StartSessionAttributes.Emplace(TEXT("TotalPhysicalRAM"), static_cast<uint64>(Stats.TotalPhysical));
			StartSessionAttributes.Emplace(TEXT("CPUPhysicalCores"), FPlatformMisc::NumberOfCores());
			StartSessionAttributes.Emplace(TEXT("CPULogicalCores"), FPlatformMisc::NumberOfCoresIncludingHyperthreads());
			StartSessionAttributes.Emplace(TEXT("DesktopGPUAdapter"), FPlatformMisc::GetPrimaryGPUBrand());
			StartSessionAttributes.Emplace(TEXT("RenderingGPUAdapter"), GRHIAdapterName);
			StartSessionAttributes.Emplace(TEXT("CPUVendor"), FPlatformMisc::GetCPUVendor());
			StartSessionAttributes.Emplace(TEXT("CPUBrand"), FPlatformMisc::GetCPUBrand());
			FPlatformMisc::GetOSVersions(/*out*/ OSMajor, /*out*/ OSMinor);
			StartSessionAttributes.Emplace(TEXT("OSMajor"), OSMajor);
			StartSessionAttributes.Emplace(TEXT("OSMinor"), OSMinor);
			StartSessionAttributes.Emplace(TEXT("OSVersion"), FPlatformMisc::GetOSVersion());
			StartSessionAttributes.Emplace(TEXT("Is64BitOS"), FPlatformMisc::Is64bitOperatingSystem());
			Analytics->StartSession(MoveTemp(StartSessionAttributes));

			bIsInitialized = true;
		}

		// Create the session manager singleton
		if (!SessionManager.IsValid())
		{
			SessionManager = MakeShareable(new FEngineSessionManager(EEngineSessionManagerMode::Editor));
			SessionManager->Initialize();
		}

#if WITH_EDITOR
		if (!SessionSummaryWriter.IsValid())
		{
			SessionSummaryWriter = MakeShareable(new FEditorSessionSummaryWriter());
			SessionSummaryWriter->Initialize();

			// This scope is a hack for 4.24.3: It adds 3 extra keys to the session summary to let CrashReportClientEditor
			// impersonate the Editor when sending the summary report (See EditorSessionSummarySender.cpp).
			{
				FGuid SessionId;
				FString SessionIdStr = Analytics->GetSessionID();
				if (FGuid::Parse(SessionIdStr, SessionId))
				{
					// Convert session GUID to one without braces or other chars that might not be suitable for storage
					SessionIdStr = SessionId.ToString(EGuidFormats::DigitsWithHyphens);
				}

				// Add extra fields to the session here for 4.24.3. Done here to keep the FEditorAnalyticsSession public header untouched and avoiding undesired dependencies,
				// but the member were added to FEditorAnalyticsSession in 4.25.
				const FString StoreId(TEXT("Epic Games"));
				const FString SessionSummarySection(TEXT("Unreal Engine/Session Summary/1_0"));
				const FString StorageLocation = SessionSummarySection + TEXT("/") + SessionIdStr;
				const FString AppIdStoreKey(TEXT("AppId"));
				const FString AppVersionStoreKey(TEXT("AppVersion"));
				const FString UserIdStoreKey(TEXT("UserId"));

				FEditorAnalyticsSession::Lock();
				FPlatformMisc::SetStoredValue(StoreId, StorageLocation, AppIdStoreKey, Analytics->GetAppID());
				FPlatformMisc::SetStoredValue(StoreId, StorageLocation, AppVersionStoreKey, Analytics->GetAppVersion());
				FPlatformMisc::SetStoredValue(StoreId, StorageLocation, UserIdStoreKey, Analytics->GetUserID());
				FEditorAnalyticsSession::Unlock();
			}
		}

		if (!SessionSummarySender.IsValid())
		{
			// if we're using out-of-process crash reporting, then we don't need to create a sender in this process.
			if (!FGenericCrashContext::IsOutOfProcessCrashReporter())
			{
				SessionSummarySender = MakeShared<FEditorSessionSummarySender>(FEngineAnalytics::GetProvider(), TEXT("Editor"), FPlatformProcess::GetCurrentProcessId());
			}
		}
#endif
	}
}

void FEngineAnalytics::Shutdown(bool bIsEngineShutdown)
{
	// Destroy the session manager singleton if it exists
	if (SessionManager.IsValid() && bIsEngineShutdown)
	{
		SessionManager->Shutdown();
		SessionManager.Reset();
	}

#if WITH_EDITOR
	if (SessionSummaryWriter.IsValid())
	{
		SessionSummaryWriter->Shutdown();
		SessionSummaryWriter.Reset();
	}

	if (SessionSummarySender.IsValid())
	{
		SessionSummarySender->Shutdown();
		SessionSummarySender.Reset();
	}
#endif

	bIsInitialized = false;

	ensure(!Analytics.IsValid() || Analytics.IsUnique());
	Analytics.Reset();
}

void FEngineAnalytics::Tick(float DeltaTime)
{
	QUICK_SCOPE_CYCLE_COUNTER(STAT_FEngineAnalytics_Tick);

	if (SessionManager.IsValid())
	{
		SessionManager->Tick(DeltaTime);
	}

#if WITH_EDITOR
	if (SessionSummaryWriter.IsValid())
	{
		SessionSummaryWriter->Tick(DeltaTime);
	}

	if (SessionSummarySender.IsValid())
	{
		SessionSummarySender->Tick(DeltaTime);
	}
#endif
}
