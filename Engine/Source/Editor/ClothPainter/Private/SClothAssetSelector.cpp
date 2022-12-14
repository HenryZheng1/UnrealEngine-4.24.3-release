// Copyright 1998-2019 Epic Games, Inc. All Rights Reserved.

#include "SClothAssetSelector.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/SBoxPanel.h"
#include "EditorStyleSet.h"
#include "DetailLayoutBuilder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Images/SImage.h"
#include "ClothingAsset.h"
#include "ClothPhysicalMeshData.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Engine/SkeletalMesh.h"
#include "ApexClothingUtils.h"
#include "UObject/UObjectIterator.h"
#include "Components/SkeletalMeshComponent.h"
#include "ComponentReregisterContext.h"
#include "Misc/MessageDialog.h"
#include "ClothingSystemEditorInterfaceModule.h"
#include "Modules/ModuleManager.h"
#include "ClothingAssetFactoryInterface.h"
#include "Utils/ClothingMeshUtils.h"
#include "ClothingAssetListCommands.h"
#include "Framework/Commands/GenericCommands.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "SCopyVertexColorSettingsPanel.h"
#include "Editor/ContentBrowser/Private/SAssetPicker.h"
#include "ContentBrowserModule.h"
#include "Animation/DebugSkelMeshComponent.h"

#define LOCTEXT_NAMESPACE "ClothAssetSelector"

FPointWeightMap* FClothingMaskListItem::GetMask()
{
	if(UClothingAssetCommon* Asset = ClothingAsset.Get())
	{
		if(Asset->IsValidLod(LodIndex))
		{
			UClothLODDataBase* LodData = Asset->ClothLodData[LodIndex];
			if(LodData->ParameterMasks.IsValidIndex(MaskIndex))
			{
				return &LodData->ParameterMasks[MaskIndex];
			}
		}
	}

	return nullptr;
}

UClothPhysicalMeshDataBase* FClothingMaskListItem::GetMeshData()
{
	UClothingAssetCommon * Asset = ClothingAsset.Get();
	return Asset && Asset->IsValidLod(LodIndex) ? Asset->ClothLodData[LodIndex]->PhysicalMeshData : nullptr;
}

USkeletalMesh* FClothingMaskListItem::GetOwningMesh()
{
	UClothingAssetCommon * Asset = ClothingAsset.Get();
	return Asset ? Cast<USkeletalMesh>(Asset->GetOuter()) : nullptr;
}

class SAssetListRow : public STableRow<TSharedPtr<FClothingAssetListItem>>
{
public:

	SLATE_BEGIN_ARGS(SAssetListRow)
	{}
		SLATE_EVENT(FSimpleDelegate, OnInvalidateList)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FClothingAssetListItem> InItem)
	{
		Item = InItem;
		OnInvalidateList = InArgs._OnInvalidateList;

		BindCommands();

		STableRow<TSharedPtr<FClothingAssetListItem>>::Construct(
			STableRow<TSharedPtr<FClothingAssetListItem>>::FArguments()
			.Content()
			[
				SNew(SBox)
				.Padding(2.0f)
				[
					SAssignNew(EditableText, SInlineEditableTextBlock)
					.Text(this, &SAssetListRow::GetAssetName)
					.OnTextCommitted(this, &SAssetListRow::OnCommitAssetName)
					.IsSelected(this, &SAssetListRow::IsSelected)
				]
			],
			InOwnerTable
		);
	}

	FText GetAssetName() const
	{
		if(Item.IsValid())
		{
			return FText::FromString(Item->ClothingAsset->GetName());
		}

		return FText::GetEmpty();
	}

	void OnCommitAssetName(const FText& InText, ETextCommit::Type CommitInfo)
	{
		if(Item.IsValid())
		{
			if(UClothingAssetCommon* Asset = Item->ClothingAsset.Get())
			{
				FText TrimText = FText::TrimPrecedingAndTrailing(InText);

				if(Asset->GetName() != TrimText.ToString())
				{
					FName NewName(*TrimText.ToString());

					// Check for an existing object, and if we find one build a unique name based on the request
					if(UObject* ExistingObject = StaticFindObject(UClothingAssetCommon::StaticClass(), Asset->GetOuter(), *NewName.ToString()))
					{
						NewName = MakeUniqueObjectName(Asset->GetOuter(), UClothingAssetCommon::StaticClass(), FName(*TrimText.ToString()));
					}

					Asset->Rename(*NewName.ToString(), Asset->GetOuter());
				}
			}
		}
	}

	void BindCommands()
	{
		check(!UICommandList.IsValid());

		UICommandList = MakeShareable(new FUICommandList);

		const FClothingAssetListCommands& Commands = FClothingAssetListCommands::Get();

		UICommandList->MapAction(
			FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &SAssetListRow::DeleteAsset)
		);

#if WITH_APEX_CLOTHING
		UICommandList->MapAction(
			Commands.ReimportAsset,
			FExecuteAction::CreateSP(this, &SAssetListRow::ReimportAsset),
			FCanExecuteAction::CreateSP(this, &SAssetListRow::CanReimportAsset)
		);
#endif

		UICommandList->MapAction(
			Commands.RebuildAssetParams,
			FExecuteAction::CreateSP(this, &SAssetListRow::RebuildLODParameters),
			FCanExecuteAction::CreateSP(this, &SAssetListRow::CanRebuildLODParameters)
		);
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		if(Item.IsValid() && MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
		{
			const FClothingAssetListCommands& Commands = FClothingAssetListCommands::Get();
			FMenuBuilder Builder(true, UICommandList);

			Builder.BeginSection(NAME_None, LOCTEXT("AssetActions_SectionName", "Actions"));
			{
				Builder.AddMenuEntry(FGenericCommands::Get().Delete);
#if WITH_APEX_CLOTHING
				Builder.AddMenuEntry(Commands.ReimportAsset);
#endif
				Builder.AddMenuEntry(Commands.RebuildAssetParams);
			}
			Builder.EndSection();

			FWidgetPath Path = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();

			FSlateApplication::Get().PushMenu(AsShared(), Path, Builder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);

			return FReply::Handled();
		}

		return STableRow<TSharedPtr<FClothingAssetListItem>>::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

private:

	void DeleteAsset()
	{
		//Lambda use to sync one of the UserSectionData section from one LOD Model
		auto SetSkelMeshSourceSectionUserData = [](FSkeletalMeshLODModel& LODModel, const int32 SectionIndex, const int32 OriginalSectionIndex)
		{
			FSkelMeshSourceSectionUserData& SourceSectionUserData = LODModel.UserSectionsData.FindOrAdd(OriginalSectionIndex);
			SourceSectionUserData.bDisabled = LODModel.Sections[SectionIndex].bDisabled;
			SourceSectionUserData.bCastShadow = LODModel.Sections[SectionIndex].bCastShadow;
			SourceSectionUserData.bRecomputeTangent = LODModel.Sections[SectionIndex].bRecomputeTangent;
			SourceSectionUserData.GenerateUpToLodIndex = LODModel.Sections[SectionIndex].GenerateUpToLodIndex;
			SourceSectionUserData.CorrespondClothAssetIndex = LODModel.Sections[SectionIndex].CorrespondClothAssetIndex;
			SourceSectionUserData.ClothingData = LODModel.Sections[SectionIndex].ClothingData;
		};

		if(UClothingAssetCommon* Asset = Item->ClothingAsset.Get())
		{
			if(USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Asset->GetOuter()))
			{
				FScopedSuspendAlternateSkinWeightPreview ScopedSuspendAlternateSkinnWeightPreview(SkelMesh);
				int32 AssetIndex;
				if(SkelMesh->MeshClothingAssets.Find(Asset, AssetIndex))
				{
					// Need to unregister our components so they shut down their current clothing simulation
					FScopedSkeletalMeshPostEditChange ScopedPostEditChange(SkelMesh);
					SkelMesh->PreEditChange(nullptr);

					Asset->UnbindFromSkeletalMesh(SkelMesh);
					SkelMesh->MeshClothingAssets.RemoveAt(AssetIndex);

					// Need to fix up asset indices on sections.
					if(FSkeletalMeshModel* Model = SkelMesh->GetImportedModel())
					{
						for(FSkeletalMeshLODModel& LodModel : Model->LODModels)
						{
							for (int32 SectionIndex = 0; SectionIndex < LodModel.Sections.Num(); ++SectionIndex)
							{
								FSkelMeshSection& Section = LodModel.Sections[SectionIndex];
								if(Section.CorrespondClothAssetIndex > AssetIndex)
								{
									--Section.CorrespondClothAssetIndex;
									//Keep the user section data (build source data) in sync
									SetSkelMeshSourceSectionUserData(LodModel, SectionIndex, Section.OriginalDataSectionIndex);
								}
							}
						}
					}
					OnInvalidateList.ExecuteIfBound();
				}
			}
		}
	}

#if WITH_APEX_CLOTHING
	void ReimportAsset()
	{
		if(UClothingAssetCommon* Asset = Item->ClothingAsset.Get())
		{
			if(USkeletalMesh* SkelMesh = Cast<USkeletalMesh>(Asset->GetOuter()))
			{
				FString ReimportPath = Asset->ImportedFilePath;

				if(ReimportPath.IsEmpty())
				{
					const FText MessageText = LOCTEXT("Warning_NoReimportPath", "There is no reimport path available for this asset, it was likely created in the Editor. Would you like to select a file and overwrite this asset?");
					EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo, MessageText);

					if(MessageReturn == EAppReturnType::Yes)
					{
						ReimportPath = ApexClothingUtils::PromptForClothingFile();
					}
				}

				if(ReimportPath.IsEmpty())
				{
					return;
				}

				// Retry if the file isn't there
				if(!FPaths::FileExists(ReimportPath))
				{
					const FText MessageText = LOCTEXT("Warning_NoFileFound", "Could not find an asset to reimport, select a new file on disk?");
					EAppReturnType::Type MessageReturn = FMessageDialog::Open(EAppMsgType::YesNo, MessageText);

					if(MessageReturn == EAppReturnType::Yes)
					{
						ReimportPath = ApexClothingUtils::PromptForClothingFile();
					}
				}

				FClothingSystemEditorInterfaceModule& ClothingEditorInterface = FModuleManager::Get().LoadModuleChecked<FClothingSystemEditorInterfaceModule>("ClothingSystemEditorInterface");
				UClothingAssetFactoryBase* Factory = ClothingEditorInterface.GetClothingAssetFactory();

				if(Factory && Factory->CanImport(ReimportPath))
				{
					Factory->Reimport(ReimportPath, SkelMesh, Asset);

					OnInvalidateList.ExecuteIfBound();
				}
			}
		}
	}

	bool CanReimportAsset() const
	{
		return Item.IsValid() && !Item->ClothingAsset->ImportedFilePath.IsEmpty();
	}
#endif  // #if WITH_APEX_CLOTHING

	// Using LOD0 of an asset, rebuild the other LOD masks by mapping the LOD0 parameters onto their meshes
	void RebuildLODParameters()
	{
		if(!Item.IsValid())
		{
			return;
		}

		if(UClothingAssetCommon* Asset = Item->ClothingAsset.Get())
		{
			const int32 NumLods = Asset->GetNumLods();

			for(int32 CurrIndex = 0; CurrIndex < NumLods - 1; ++CurrIndex)
			{
				UClothLODDataBase* SourceLod = Asset->ClothLodData[CurrIndex];
				UClothLODDataBase* DestLod = Asset->ClothLodData[CurrIndex + 1];

				DestLod->ParameterMasks.Reset();

				for(FPointWeightMap& SourceMask : SourceLod->ParameterMasks)
				{
					DestLod->ParameterMasks.AddDefaulted();
					FPointWeightMap& DestMask = DestLod->ParameterMasks.Last();

					DestMask.Name = SourceMask.Name;
					DestMask.bEnabled = SourceMask.bEnabled;
					DestMask.CurrentTarget = SourceMask.CurrentTarget;

					ClothingMeshUtils::FVertexParameterMapper ParameterMapper(
						DestLod->PhysicalMeshData->Vertices,
						DestLod->PhysicalMeshData->Normals,
						SourceLod->PhysicalMeshData->Vertices,
						SourceLod->PhysicalMeshData->Normals,
						SourceLod->PhysicalMeshData->Indices
					);

					ParameterMapper.Map(SourceMask.GetValueArray(), DestMask.Values);
				}
			}
		}
	}

	bool CanRebuildLODParameters() const
	{
		if(!Item.IsValid())
		{
			return false;
		}

		if(UClothingAssetCommon* Asset = Item->ClothingAsset.Get())
		{
			if(Asset->GetNumLods() > 1)
			{
				return true;
			}
		}

		return false;
	}

	TSharedPtr<FClothingAssetListItem> Item;
	TSharedPtr<SInlineEditableTextBlock> EditableText;
	FSimpleDelegate OnInvalidateList;
	TSharedPtr<FUICommandList> UICommandList;
};

class SMaskListRow : public SMultiColumnTableRow<TSharedPtr<FClothingMaskListItem>>
{
public:

	SLATE_BEGIN_ARGS(SMaskListRow)
	{}

		SLATE_EVENT(FSimpleDelegate, OnInvalidateList)

	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& InOwnerTable, TSharedPtr<FClothingMaskListItem> InItem, TSharedPtr<SClothAssetSelector> InAssetSelector )
	{
		OnInvalidateList = InArgs._OnInvalidateList;
		Item = InItem;
		AssetSelectorPtr = InAssetSelector;

		BindCommands();

		SMultiColumnTableRow<TSharedPtr<FClothingMaskListItem>>::Construct(FSuperRowType::FArguments(), InOwnerTable);
	}

	static FName Column_Enabled;
	static FName Column_MaskName;
	static FName Column_CurrentTarget;

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& InColumnName) override
	{
		if(InColumnName == Column_Enabled)
		{
			return SNew(SCheckBox)
				.IsEnabled(this, &SMaskListRow::IsMaskCheckboxEnabled, Item)
				.IsChecked(this, &SMaskListRow::IsMaskEnabledChecked, Item)
				.OnCheckStateChanged(this, &SMaskListRow::OnMaskEnabledCheckboxChanged, Item)
				.Padding(2.0f)
				.ToolTipText(LOCTEXT("MaskEnableCheckBox_ToolTip", "Sets whether this mask is enabled and can affect final parameters for its target parameter."));
		}

		if(InColumnName == Column_MaskName)
		{
			return SAssignNew(InlineText, SInlineEditableTextBlock)
				.Text(this, &SMaskListRow::GetMaskName)
				.OnTextCommitted(this, &SMaskListRow::OnCommitMaskName)
				.IsSelected(this, &SMultiColumnTableRow<TSharedPtr<FClothingMaskListItem>>::IsSelectedExclusively);
		}

		if(InColumnName == Column_CurrentTarget)
		{
			FPointWeightMap* Mask = Item->GetMask();
			UEnum* Enum = Item->GetMeshData()->GetFloatArrayTargets();
			if(Enum && Mask)
			{
				return SNew(STextBlock).Text(Enum->GetDisplayNameTextByIndex((int32)Mask->CurrentTarget));
			}
		}

		return SNullWidget::NullWidget;
	}

	FText GetMaskName() const
	{
		if(Item.IsValid())
		{
			if(FPointWeightMap* Mask = Item->GetMask())
			{
				return FText::FromName(Mask->Name);
			}
		}

		return LOCTEXT("MaskName_Invalid", "Invalid Mask");
	}

	void OnCommitMaskName(const FText& InText, ETextCommit::Type CommitInfo)
	{
		if(Item.IsValid())
		{
			if(FPointWeightMap* Mask = Item->GetMask())
			{
				FText TrimText = FText::TrimPrecedingAndTrailing(InText);
				Mask->Name = FName(*TrimText.ToString());
			}
		}
	}

	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		// Spawn menu
		if(MouseEvent.GetEffectingButton() == EKeys::RightMouseButton && Item.IsValid())
		{
			if(FPointWeightMap* Mask = Item->GetMask())
			{
				FMenuBuilder Builder(true, UICommandList);

				FUIAction DeleteAction(FExecuteAction::CreateSP(this, &SMaskListRow::OnDeleteMask));

				Builder.BeginSection(NAME_None, LOCTEXT("MaskActions_SectionName", "Actions"));
				{
					Builder.AddSubMenu(LOCTEXT("MaskActions_SetTarget", "Set Target"), LOCTEXT("MaskActions_SetTarget_Tooltip", "Choose the target for this mask"), FNewMenuDelegate::CreateSP(this, &SMaskListRow::BuildTargetSubmenu));
					Builder.AddMenuEntry(FGenericCommands::Get().Delete);
					Builder.AddSubMenu(LOCTEXT("MaskActions_CopyFromVertexColor", "Copy From Vertex Color"), LOCTEXT("MaskActions_CopyFromVertexColor_Tooltip", "Replace this mask with values from vertex color channel on sim mesh"), FNewMenuDelegate::CreateSP(this, &SMaskListRow::BuildCopyVertexColorSubmenu));
				}
				Builder.EndSection();

				FWidgetPath Path = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();

				FSlateApplication::Get().PushMenu(AsShared(), Path, Builder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect::ContextMenu);

				return FReply::Handled();
			}
		}

		return SMultiColumnTableRow<TSharedPtr<FClothingMaskListItem>>::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

	void EditName()
	{
		if(InlineText.IsValid())
		{
			InlineText->EnterEditingMode();
		}
	}

private:

	void BindCommands()
	{
		check(!UICommandList.IsValid());

		UICommandList = MakeShareable(new FUICommandList);

		UICommandList->MapAction(
			FGenericCommands::Get().Delete,
			FExecuteAction::CreateSP(this, &SMaskListRow::OnDeleteMask)
		);
	}

	UClothLODDataBase* GetCurrentLod() const
	{
		if(Item.IsValid())
		{
			if(UClothingAssetCommon* Asset = Item->ClothingAsset.Get())
			{
				if(Asset->ClothLodData.IsValidIndex(Item->LodIndex))
				{
					UClothLODDataBase* LodData = Asset->ClothLodData[Item->LodIndex];
					return LodData;
				}
			}
		}

		return nullptr;
	}

	void OnDeleteMask()
	{
		USkeletalMesh* CurrentMesh = Item->GetOwningMesh();

		if(CurrentMesh)
		{
			FScopedTransaction CurrTransaction(LOCTEXT("DeleteMask_Transaction", "Delete clothing parameter mask."));
			Item->ClothingAsset->Modify();

		if(UClothLODDataBase* LodData = GetCurrentLod())
		{
			if(LodData->ParameterMasks.IsValidIndex(Item->MaskIndex))
			{
				LodData->ParameterMasks.RemoveAt(Item->MaskIndex);

				// We've removed a mask, so it will need to be applied to the clothing data
				if(Item.IsValid())
				{
					if(UClothingAssetCommon* Asset = Item->ClothingAsset.Get())
					{
						Asset->ApplyParameterMasks();
					}
				}

				OnInvalidateList.ExecuteIfBound();
			}
		}
	}
	}

	void OnSetTarget(int32 InTargetEntryIndex)
	{
		USkeletalMesh* CurrentMesh = Item->GetOwningMesh();

		if(Item.IsValid() && CurrentMesh)
		{
			FScopedTransaction CurrTransaction(LOCTEXT("SetMaskTarget_Transaction", "Set clothing parameter mask target."));
			Item->ClothingAsset->Modify();

			if(FPointWeightMap* Mask = Item->GetMask())
			{
				Mask->CurrentTarget = (uint8)InTargetEntryIndex;
				if(Mask->CurrentTarget == 0)//(uint8)MaskTarget_PhysMesh::None)
				{
					// Make sure to disable this mask if it has no valid target
					Mask->bEnabled = false;
				}

				OnInvalidateList.ExecuteIfBound();
			}
		}
	}

	void BuildTargetSubmenu(FMenuBuilder& Builder)
	{
		Builder.BeginSection(NAME_None, LOCTEXT("MaskTargets_SectionName", "Targets"));
		{
			//UEnum* Enum = StaticEnum<MaskTarget_PhysMesh>();
			UEnum* Enum = Item->GetMeshData()->GetFloatArrayTargets();
			if(Enum)
			{
				const int32 NumEntries = Enum->NumEnums();

				// Iterate to -1 to skip the _MAX entry appended to the end of the enum
				for(int32 Index = 0; Index < NumEntries - 1; ++Index)
				{
					FUIAction EntryAction(FExecuteAction::CreateSP(this, &SMaskListRow::OnSetTarget, Index));

					FText EntryText = Enum->GetDisplayNameTextByIndex(Index);

					Builder.AddMenuEntry(EntryText, FText::GetEmpty(), FSlateIcon(), EntryAction);
				}
			}
		}
		Builder.EndSection();
	}

	/** Build sub menu for choosing which vertex color channel to copy to selected mask */
	void BuildCopyVertexColorSubmenu(FMenuBuilder& Builder)
	{
		if (AssetSelectorPtr.IsValid())
		{
			UClothingAssetCommon* ClothingAsset = AssetSelectorPtr.Pin()->GetSelectedAsset().Get();
			int32 LOD = AssetSelectorPtr.Pin()->GetSelectedLod();
			FPointWeightMap* Mask = Item->GetMask();

			TSharedRef<SWidget> Widget = SNew(SCopyVertexColorSettingsPanel, ClothingAsset, LOD, Mask);

			Builder.AddWidget(Widget, FText::GetEmpty(), true, false);
		}
	}

	// Mask enabled checkbox handling
	ECheckBoxState IsMaskEnabledChecked(TSharedPtr<FClothingMaskListItem> InItem) const
	{
		if(InItem.IsValid())
		{
			if(FPointWeightMap* Mask = InItem->GetMask())
			{
				return Mask->bEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			}
		}

		return ECheckBoxState::Unchecked;
	}

	bool IsMaskCheckboxEnabled(TSharedPtr<FClothingMaskListItem> InItem) const
	{
		if(InItem.IsValid())
		{
			if(FPointWeightMap* Mask = InItem->GetMask())
			{
				return Mask->CurrentTarget != 0; // (uint8)MaskTarget_PhysMesh::None;
			}
		}

		return false;
	}

	void OnMaskEnabledCheckboxChanged(ECheckBoxState InState, TSharedPtr<FClothingMaskListItem> InItem)
	{
		if(InItem.IsValid())
		{
			if(FPointWeightMap* Mask = InItem->GetMask())
			{
				bool bNewEnableState = InState == ECheckBoxState::Checked;

				if(Mask->bEnabled != bNewEnableState)
				{
					if(bNewEnableState)
					{
						// Disable all other masks that affect this target
						if(UClothingAssetCommon* Asset = InItem->ClothingAsset.Get())
						{
							if(Asset->ClothLodData.IsValidIndex(InItem->LodIndex))
							{
								UClothLODDataBase* LodData = Asset->ClothLodData[InItem->LodIndex];

								TArray<FPointWeightMap*> AllTargetMasks;
								LodData->GetParameterMasksForTarget(Mask->CurrentTarget, AllTargetMasks);

								for(FPointWeightMap* TargetMask : AllTargetMasks)
								{
									if(TargetMask && TargetMask != Mask)
									{
										TargetMask->bEnabled = false;
									}
								}
							}
						}
					}

					// Set the flag
					Mask->bEnabled = bNewEnableState;
				}
			}
		}
	}

	FSimpleDelegate OnInvalidateList;
	TSharedPtr<FClothingMaskListItem> Item;
	TSharedPtr<SInlineEditableTextBlock> InlineText;
	TSharedPtr<FUICommandList> UICommandList;
	TWeakPtr<SClothAssetSelector> AssetSelectorPtr;
};

FName SMaskListRow::Column_Enabled(TEXT("Enabled"));
FName SMaskListRow::Column_MaskName(TEXT("Name"));
FName SMaskListRow::Column_CurrentTarget(TEXT("CurrentTarget"));

SClothAssetSelector::~SClothAssetSelector()
{
	if(Mesh)
	{
		Mesh->UnregisterOnClothingChange(MeshClothingChangedHandle);
	}

	if(GEditor)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SClothAssetSelector::Construct(const FArguments& InArgs, USkeletalMesh* InMesh)
{
	FClothingAssetListCommands::Register();

	Mesh = InMesh;
	OnSelectionChanged = InArgs._OnSelectionChanged;

	// Register callback for external changes to clothing items
	if(Mesh)
	{
		MeshClothingChangedHandle = Mesh->RegisterOnClothingChange(FSimpleMulticastDelegate::FDelegate::CreateSP(this, &SClothAssetSelector::OnRefresh));
	}

	if(GEditor)
	{
		GEditor->RegisterForUndo(this);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+SVerticalBox::Slot()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		.AutoHeight()
		[
			SNew(SExpandableArea)
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f))
			.BodyBorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle"))
			.BodyBorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f))
			.HeaderContent()
			[
				SAssignNew(AssetHeaderBox, SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AssetExpander_Title", "Clothing Data"))
					.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.ShadowOffset(FVector2D(1.0f, 1.0f))
				]
#if WITH_APEX_CLOTHING
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				.Padding(0.0f, 0.0f, 4.0f, 0.0f)
				[
					SNew(SButton)
					.ButtonStyle(FEditorStyle::Get(), "RoundButton")
					.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
					.ContentPadding(FMargin(2, 0))
					.OnClicked(this, &SClothAssetSelector::OnImportApexFileClicked)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0, 1))
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("Plus"))
						]

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(FMargin(2, 0, 0, 0))
						[
							SNew(STextBlock)
							.Font(IDetailLayoutBuilder::GetDetailFontBold())
							.Text(LOCTEXT("NewAssetButtonText", "Import APEX file"))
							.Visibility(this, &SClothAssetSelector::GetAssetHeaderButtonTextVisibility)
							.ShadowOffset(FVector2D(1, 1))
						]
					]
				]
#endif  // #if WITH_APEX_CLOTHING
				+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.HAlign(HAlign_Right)
					.Padding(0.0f, 0.0f, 4.0f, 0.0f)
					[
						SNew(SComboButton)
						.ButtonStyle(FEditorStyle::Get(), "RoundButton")
					.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
					.ContentPadding(FMargin(2, 0))
					.OnGetMenuContent(this, &SClothAssetSelector::OnGenerateSkeletalMeshPickerForClothCopy)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					.ButtonContent()
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
					.VAlign(VAlign_Center)
					.AutoWidth()
					.Padding(FMargin(2, 0, 0, 0))
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.Text(LOCTEXT("CopyClothingFromMeshText", "Copy Clothing from SkeletalMesh"))
					.ShadowOffset(FVector2D(1, 1))
					]
					]
					]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				[
					SNew(SComboButton)
					.ButtonStyle(FEditorStyle::Get(), "RoundButton")
					.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
					.ContentPadding(FMargin(2, 0))
					.OnGetMenuContent(this, &SClothAssetSelector::OnGetLodMenu)
					.HasDownArrow(true)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
						.Text(this, &SClothAssetSelector::GetLodButtonText)
						.ShadowOffset(FVector2D(1, 1))
					]
				]
			]
			.BodyContent()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.Padding(3.0f)
				.AutoHeight()
				[
					SNew(SBox)
					.MinDesiredHeight(100.0f)
					.MaxDesiredHeight(100.0f)
					[
						SAssignNew(AssetList, SAssetList)
						.ItemHeight(24)
						.ListItemsSource(&AssetListItems)
						.OnGenerateRow(this, &SClothAssetSelector::OnGenerateWidgetForClothingAssetItem)
						.OnSelectionChanged(this, &SClothAssetSelector::OnAssetListSelectionChanged)
						.ClearSelectionOnClick(false)
						.SelectionMode(ESelectionMode::Single)
					]
				]
			]
		]
		+ SVerticalBox::Slot()
		.Padding(0.0f, 0.0f, 0.0f, 2.0f)
		.AutoHeight()
		[
			SNew(SExpandableArea)
			.BorderBackgroundColor(FLinearColor(0.6f, 0.6f, 0.6f))
			.BodyBorderImage(FEditorStyle::GetBrush("DetailsView.CategoryMiddle"))
			.BodyBorderBackgroundColor(FLinearColor(1.0f, 1.0f, 1.0f))
			.HeaderContent()
			[
				SAssignNew(MaskHeaderBox, SHorizontalBox)
				+SHorizontalBox::Slot()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("MaskExpander_Title", "Masks"))
					.Font(FEditorStyle::GetFontStyle("DetailsView.CategoryFontStyle"))
					.ShadowOffset(FVector2D(1.0f, 1.0f))
				]
				+SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Right)
				[
					SAssignNew(NewMaskButton, SButton)
					.ButtonStyle(FEditorStyle::Get(), "RoundButton")
					.ForegroundColor(FEditorStyle::GetSlateColor("DefaultForeground"))
					.ContentPadding(FMargin(2, 0))
					.OnClicked(this, &SClothAssetSelector::AddNewMask)
					.IsEnabled(this, &SClothAssetSelector::CanAddNewMask)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)

						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(FMargin(0, 1))
						[
							SNew(SImage)
							.Image(FEditorStyle::GetBrush("Plus"))
						]

						+ SHorizontalBox::Slot()
						.VAlign(VAlign_Center)
						.AutoWidth()
						.Padding(FMargin(2, 0, 0, 0))
						[
							SNew(STextBlock)
							.Font(IDetailLayoutBuilder::GetDetailFontBold())
							.Text(LOCTEXT("NewMaskButtonText", "Mask"))
							.Visibility(this, &SClothAssetSelector::GetMaskHeaderButtonTextVisibility)
							.ShadowOffset(FVector2D(1, 1))
						]
					]
				]
			]
			.BodyContent()
			[
				SNew(SBox)
				.MinDesiredHeight(100.0f)
				.MaxDesiredHeight(200.0f)
				.Padding(FMargin(3.0f))
				[
					SAssignNew(MaskList, SMaskList)
					.ItemHeight(24)
					.ListItemsSource(&MaskListItems)
					.OnGenerateRow(this, &SClothAssetSelector::OnGenerateWidgetForMaskItem)
					.OnSelectionChanged(this, &SClothAssetSelector::OnMaskSelectionChanged)
					.ClearSelectionOnClick(false)
					.SelectionMode(ESelectionMode::Single)
					.HeaderRow
					(
						SNew(SHeaderRow)
						+ SHeaderRow::Column(SMaskListRow::Column_Enabled)
						.FixedWidth(24)
						.DefaultLabel(LOCTEXT("MaskListHeader_Enabled", "Enabled"))
						.HeaderContent()
						[
							SNew(SBox)
						]
						+ SHeaderRow::Column(SMaskListRow::Column_MaskName)
						.FillWidth(0.5f)
						.DefaultLabel(LOCTEXT("MaskListHeader_Name", "Name"))
						+ SHeaderRow::Column(SMaskListRow::Column_CurrentTarget)
						.FillWidth(0.3f)
						.DefaultLabel(LOCTEXT("MaskListHeader_Target", "Target"))
					)
				]
			]
		]
	];

	RefreshAssetList();
	RefreshMaskList();
}

TWeakObjectPtr<UClothingAssetCommon> SClothAssetSelector::GetSelectedAsset() const
{
	return SelectedAsset;
	
}

int32 SClothAssetSelector::GetSelectedLod() const
{
	return SelectedLod;
}

int32 SClothAssetSelector::GetSelectedMask() const
{
	return SelectedMask;
}

void SClothAssetSelector::PostUndo(bool bSuccess)
{
	OnRefresh();
}

#if WITH_APEX_CLOTHING
FReply SClothAssetSelector::OnImportApexFileClicked()
{
	if(Mesh)
	{
		ApexClothingUtils::PromptAndImportClothing(Mesh);
		OnRefresh();

		return FReply::Handled();
	}

	return FReply::Unhandled();
}
#endif  // #if WITH_APEX_CLOTHING

void SClothAssetSelector::OnCopyClothingAssetSelected(const FAssetData& AssetData)
{
	USkeletalMesh* SourceSkelMesh = Cast<USkeletalMesh>(AssetData.GetAsset());

	if (Mesh && SourceSkelMesh && Mesh != SourceSkelMesh)
	{
		FScopedTransaction Transaction(LOCTEXT("CopiedClothingAssetsFromSkelMesh", "Copied clothing assets from another SkelMesh"));
		Mesh->Modify();
		FClothingSystemEditorInterfaceModule& ClothingEditorModule = FModuleManager::LoadModuleChecked<FClothingSystemEditorInterfaceModule>("ClothingSystemEditorInterface");
		UClothingAssetFactoryBase* AssetFactory = ClothingEditorModule.GetClothingAssetFactory();

		for (UClothingAssetBase* ClothingAsset : SourceSkelMesh->MeshClothingAssets)
		{
			UClothingAssetCommon* NewAsset = Cast<UClothingAssetCommon>(AssetFactory->CreateFromExistingCloth(Mesh, SourceSkelMesh, ClothingAsset));
			Mesh->AddClothingAsset(NewAsset);
		}
		OnRefresh();
	}
	FSlateApplication::Get().DismissAllMenus();
}

TSharedRef<SWidget> SClothAssetSelector::OnGenerateSkeletalMeshPickerForClothCopy()
{
	FContentBrowserModule& ContentBrowserModule = FModuleManager::Get().LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"));

	FAssetPickerConfig AssetPickerConfig;
	AssetPickerConfig.Filter.ClassNames.Add(USkeletalMesh::StaticClass()->GetFName());
	AssetPickerConfig.OnAssetSelected = FOnAssetSelected::CreateSP(this, &SClothAssetSelector::OnCopyClothingAssetSelected);
	AssetPickerConfig.bAllowNullSelection = true;
	AssetPickerConfig.InitialAssetViewType = EAssetViewType::List;
	AssetPickerConfig.bFocusSearchBoxWhenOpened = true;
	AssetPickerConfig.bShowBottomToolbar = false;
	AssetPickerConfig.SelectionMode = ESelectionMode::Single;

	return SNew(SBox)
		.WidthOverride(300)
		.HeightOverride(400)
		[
			ContentBrowserModule.Get().CreateAssetPicker(AssetPickerConfig)
		];
}

EVisibility SClothAssetSelector::GetAssetHeaderButtonTextVisibility() const
{
	bool bShow = AssetHeaderBox.IsValid() && AssetHeaderBox->IsHovered();

	return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

EVisibility SClothAssetSelector::GetMaskHeaderButtonTextVisibility() const
{
	bool bShow = MaskHeaderBox.IsValid() && MaskHeaderBox->IsHovered();

	return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

TSharedRef<SWidget> SClothAssetSelector::OnGetLodMenu()
{
	FMenuBuilder Builder(true, nullptr);

	int32 NumLods = 0;

	if(UClothingAssetCommon* CurrAsset = SelectedAsset.Get())
	{
		NumLods = CurrAsset->GetNumLods();
	}

	if(NumLods == 0)
	{
		Builder.AddMenuEntry(LOCTEXT("LodMenu_NoLods", "Select an asset..."), FText::GetEmpty(), FSlateIcon(), FUIAction());
	}
	else
	{
		for(int32 LodIdx = 0; LodIdx < NumLods; ++LodIdx)
		{
			FText ItemText = FText::Format(LOCTEXT("LodMenuItem", "LOD{0}"), FText::AsNumber(LodIdx));
			FText ToolTipText = FText::Format(LOCTEXT("LodMenuItemToolTip", "Select LOD{0}"), FText::AsNumber(LodIdx));

			FUIAction Action;
			Action.ExecuteAction.BindSP(this, &SClothAssetSelector::OnClothingLodSelected, LodIdx);

			Builder.AddMenuEntry(ItemText, ToolTipText, FSlateIcon(), Action);
		}
	}

	return Builder.MakeWidget();
}

FText SClothAssetSelector::GetLodButtonText() const
{
	if(SelectedLod == INDEX_NONE)
	{
		return LOCTEXT("LodButtonGenTextEmpty", "LOD");
	}

	return FText::Format(LOCTEXT("LodButtonGenText", "LOD{0}"), FText::AsNumber(SelectedLod));
}

TSharedRef<ITableRow> SClothAssetSelector::OnGenerateWidgetForClothingAssetItem(TSharedPtr<FClothingAssetListItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if(UClothingAssetCommon* Asset = InItem->ClothingAsset.Get())
	{
		return SNew(SAssetListRow, OwnerTable, InItem)
			.OnInvalidateList(this, &SClothAssetSelector::OnRefresh);
	}

	return SNew(STableRow<TSharedPtr<FClothingAssetListItem>>, OwnerTable)
		.Content()
		[
			SNew(STextBlock).Text(FText::FromString(TEXT("No Assets Available")))
		];
}

void SClothAssetSelector::OnAssetListSelectionChanged(TSharedPtr<FClothingAssetListItem> InSelectedItem, ESelectInfo::Type InSelectInfo)
{
	if(InSelectedItem.IsValid() && InSelectInfo != ESelectInfo::Direct)
	{
		SetSelectedAsset(InSelectedItem->ClothingAsset);
	}
}

TSharedRef<ITableRow> SClothAssetSelector::OnGenerateWidgetForMaskItem(TSharedPtr<FClothingMaskListItem> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if(FPointWeightMap* Mask = InItem->GetMask())
	{
		return SNew(SMaskListRow, OwnerTable, InItem, SharedThis(this))
			.OnInvalidateList(this, &SClothAssetSelector::OnRefresh);
	}

	return SNew(STableRow<TSharedPtr<FClothingMaskListItem>>, OwnerTable)
	[
		SNew(STextBlock).Text(LOCTEXT("MaskList_NoMasks", "No masks available"))
	];
}

void SClothAssetSelector::OnMaskSelectionChanged(TSharedPtr<FClothingMaskListItem> InSelectedItem, ESelectInfo::Type InSelectInfo)
{
	if(InSelectedItem.IsValid() 
		&& InSelectedItem->ClothingAsset.IsValid() 
		&& InSelectedItem->LodIndex != INDEX_NONE 
		&& InSelectedItem->MaskIndex != INDEX_NONE
		&& InSelectedItem->MaskIndex != SelectedMask
		&& InSelectInfo != ESelectInfo::Direct)
	{
		SetSelectedMask(InSelectedItem->MaskIndex);
	}
}

FReply SClothAssetSelector::AddNewMask()
{
	if(UClothingAssetCommon* Asset = SelectedAsset.Get())
	{
		if(Asset->ClothLodData.IsValidIndex(SelectedLod))
		{
			UClothLODDataBase* LodData = Asset->ClothLodData[SelectedLod];
			const int32 NumRequiredValues = LodData->PhysicalMeshData->Vertices.Num();

			LodData->ParameterMasks.AddDefaulted();

			FPointWeightMap& NewMask = LodData->ParameterMasks.Last();

			NewMask.Name = TEXT("New Mask");
			NewMask.CurrentTarget = 0;// (uint8)MaskTarget_PhysMesh::None;
			NewMask.Values.AddZeroed(NumRequiredValues);

			OnRefresh();
		}
	}

	return FReply::Handled();
}

bool SClothAssetSelector::CanAddNewMask() const
{
	return SelectedAsset.Get() != nullptr;
}

void SClothAssetSelector::OnRefresh()
{
	RefreshAssetList();
	RefreshMaskList();
}

void SClothAssetSelector::RefreshAssetList()
{
	UClothingAssetCommon* CurrSelectedAsset = nullptr;
	int32 SelectedItem = INDEX_NONE;

	if(AssetList.IsValid())
	{
		TArray<TSharedPtr<FClothingAssetListItem>> SelectedItems;
		AssetList->GetSelectedItems(SelectedItems);

		if(SelectedItems.Num() > 0)
		{
			CurrSelectedAsset = SelectedItems[0]->ClothingAsset.Get();
		}
	}

	AssetListItems.Empty();

	for(UClothingAssetBase* Asset : Mesh->MeshClothingAssets)
	{
		UClothingAssetCommon* ConcreteAsset = Cast<UClothingAssetCommon>(Asset);

		TSharedPtr<FClothingAssetListItem> Entry = MakeShareable(new FClothingAssetListItem);

		Entry->ClothingAsset = ConcreteAsset;

		AssetListItems.Add(Entry);

		if(ConcreteAsset == CurrSelectedAsset)
		{
			SelectedItem = AssetListItems.Num() - 1;
		}
	}

	if(AssetListItems.Num() == 0)
	{
		// Add an invalid entry so we can show a "none" line
		AssetListItems.Add(MakeShareable(new FClothingAssetListItem));
	}

	if(AssetList.IsValid())
	{
		AssetList->RequestListRefresh();

		if(SelectedItem != INDEX_NONE)
		{
			AssetList->SetSelection(AssetListItems[SelectedItem]);
		}
	}
}

void SClothAssetSelector::RefreshMaskList()
{
	int32 CurrSelectedLod = INDEX_NONE;
	int32 CurrSelectedMask = INDEX_NONE;
	int32 SelectedItem = INDEX_NONE;

	if(MaskList.IsValid())
	{
		TArray<TSharedPtr<FClothingMaskListItem>> SelectedItems;

		MaskList->GetSelectedItems(SelectedItems);

		if(SelectedItems.Num() > 0)
		{
			CurrSelectedLod = SelectedItems[0]->LodIndex;
			CurrSelectedMask = SelectedItems[0]->MaskIndex;
		}
	}

	MaskListItems.Empty();

	UClothingAssetCommon* Asset = SelectedAsset.Get();
	if(Asset && Asset->IsValidLod(SelectedLod))
	{
		UClothLODDataBase* LodData = Asset->ClothLodData[SelectedLod];
		const int32 NumMasks = LodData->ParameterMasks.Num();

		for(int32 Index = 0; Index < NumMasks; ++Index)
		{
			TSharedPtr<FClothingMaskListItem> NewItem = MakeShareable(new FClothingMaskListItem);
			NewItem->ClothingAsset = SelectedAsset;
			NewItem->LodIndex = SelectedLod;
			NewItem->MaskIndex = Index;
			MaskListItems.Add(NewItem);

			if(NewItem->LodIndex == CurrSelectedLod &&
				NewItem->MaskIndex == CurrSelectedMask)
			{
				SelectedItem = MaskListItems.Num() - 1;
			}
		}
	}

	if(MaskListItems.Num() == 0)
	{
		// Add invalid entry so we can make a widget for "none"
		TSharedPtr<FClothingMaskListItem> NewItem = MakeShareable(new FClothingMaskListItem);
		MaskListItems.Add(NewItem);
	}

	if(MaskList.IsValid())
	{
		MaskList->RequestListRefresh();

		if(SelectedItem != INDEX_NONE)
		{
			MaskList->SetSelection(MaskListItems[SelectedItem]);
		}
	}
}

void SClothAssetSelector::OnClothingLodSelected(int32 InNewLod)
{
	if(InNewLod == INDEX_NONE)
	{
		SetSelectedLod(InNewLod);
		//ClothPainterSettings->OnAssetSelectionChanged.Broadcast(SelectedAsset.Get(), SelectedLod, SelectedMask);
	}

	if(SelectedAsset.IsValid())
	{
		SetSelectedLod(InNewLod);

		int32 NewMaskSelection = INDEX_NONE;
		if(SelectedAsset->ClothLodData.IsValidIndex(SelectedLod))
		{
			UClothLODDataBase* LodData = SelectedAsset->ClothLodData[SelectedLod];

			if(LodData->ParameterMasks.Num() > 0)
			{
				NewMaskSelection = 0;
			}
		}

		SetSelectedMask(NewMaskSelection);
	}
}

void SClothAssetSelector::SetSelectedAsset(TWeakObjectPtr<UClothingAssetCommon> InSelectedAsset)
{
	SelectedAsset = InSelectedAsset;

	RefreshMaskList();

	if(UClothingAssetCommon* NewAsset = SelectedAsset.Get())
	{
		if(NewAsset->GetNumLods() > 0)
		{
			SetSelectedLod(0);

			UClothLODDataBase* LodData = NewAsset->ClothLodData[SelectedLod];
			if(LodData->ParameterMasks.Num() > 0)
			{
				SetSelectedMask(0);
			}
			else
			{
				SetSelectedMask(INDEX_NONE);
			}
		}
		else
		{
			SetSelectedLod(INDEX_NONE);
			SetSelectedMask(INDEX_NONE);
		}

		OnSelectionChanged.ExecuteIfBound(SelectedAsset, SelectedLod, SelectedMask);
	}
}

void SClothAssetSelector::SetSelectedLod(int32 InLodIndex, bool bRefreshMasks /*= true*/)
{
	if(InLodIndex != SelectedLod)
	{
		SelectedLod = InLodIndex;

		if(MaskList.IsValid() && bRefreshMasks)
		{
			// New LOD means new set of masks, refresh that list
			RefreshMaskList();
		}

		OnSelectionChanged.ExecuteIfBound(SelectedAsset, SelectedLod, SelectedMask);
	}
}

void SClothAssetSelector::SetSelectedMask(int32 InMaskIndex)
{
	SelectedMask = InMaskIndex;

	if(MaskList.IsValid())
	{
		TSharedPtr<FClothingMaskListItem>* FoundItem = nullptr;
		if(InMaskIndex != INDEX_NONE)
		{
			// Find the item so we can select it in the list
			FoundItem = MaskListItems.FindByPredicate([&](const TSharedPtr<FClothingMaskListItem>& InItem)
			{
				return InItem->MaskIndex == InMaskIndex;
			});
		}

		if(FoundItem)
		{
			MaskList->SetSelection(*FoundItem);
		}
		else
		{
			MaskList->ClearSelection();
		}
	}

	OnSelectionChanged.ExecuteIfBound(SelectedAsset, SelectedLod, SelectedMask);
}

#undef LOCTEXT_NAMESPACE
