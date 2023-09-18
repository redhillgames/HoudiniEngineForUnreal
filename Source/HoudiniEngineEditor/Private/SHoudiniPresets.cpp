﻿/*
* Copyright (c) <2023> Side Effects Software Inc.
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
*
* 1. Redistributions of source code must retain the above copyright notice,
*    this list of conditions and the following disclaimer.
*
* 2. The name of Side Effects Software may not be used to endorse or
*    promote products derived from this software without specific prior
*    written permission.
*
* THIS SOFTWARE IS PROVIDED BY SIDE EFFECTS SOFTWARE "AS IS" AND ANY EXPRESS
* OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
* OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
* NO EVENT SHALL SIDE EFFECTS SOFTWARE BE LIABLE FOR ANY DIRECT, INDIRECT,
* INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA,
* OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
* LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
* NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
* EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "SHoudiniPresets.h"

#include "AssetSelection.h"
#include "HoudiniAsset.h"
#include "HoudiniParameter.h"
#include "HoudiniParameterColor.h"
#include "HoudiniParameterFile.h"
#include "HoudiniParameterFloat.h"
#include "HoudiniParameterInt.h"
#include "HoudiniParameterRamp.h"
#include "HoudiniParameterString.h"
#include "HoudiniPreset.h"
#include "HoudiniPresetFactory.h"
#include "HoudiniToolsEditor.h"

#include "AssetViewUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Interfaces/IMainFrameModule.h"
#include "PropertyCustomizationHelpers.h"
#include "SWarningOrErrorBox.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SScrollBox.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 3
	#include "UnrealClient.h"
#endif

#define LOCTEXT_NAMESPACE "HoudiniPresets"

SHoudiniPresetUIBase::SHoudiniPresetUIBase()
	: bIsValidPresetPath(true)
	, bApplyOnlyToSource(true)
	, bRevertHDAParameters(false)
	, bCanInstantiate(true)
	, bApplyTempCookFolder(false)
	, bApplyBakeFolder(false)
	, bApplyBakeOptions(false)
	, bApplyAssetOptions(false)
	, bApplyStaticMeshGenSettings(false)
	, bApplyProxyMeshGenSettings(false)
	, LabelColumnWidth(0.35f)
	, NameColumnWidth(0.3f)
	, ValueColumnWidth(0.35f)
	, bSelectAll(true)
{
}

#define PRESET_CAST_OR_CONTINUE(Type, Var) \
	Type* Var = Cast<Type>(Param); \
	if (!IsValid(Var)) \
	{ \
		continue; \
	} \

void
SHoudiniPresetUIBase::Construct(const FArguments& InArgs)
{
	TWeakObjectPtr<UHoudiniAssetComponent> HAC = InArgs._HoudiniAssetComponent.Get();
	HoudiniAssetComponent = HAC;

	if (!HAC.IsValid())
	{
		HOUDINI_LOG_ERROR(TEXT("Trying to create a preset using an invalid HoudiniAssetComponent"));
		return;
	}
	
	SourceHoudiniAsset = HAC->GetHoudiniAsset();
	
	PresetName = SourceHoudiniAsset->GetName();
	PresetLabel = PresetName;
	PresetDescription = FString();

	if (SourceHoudiniAsset.IsValid() && IsValid(SourceHoudiniAsset->HoudiniToolData))
	{
		// Extract preset name, label from HoudiniToolData
		PresetName = SourceHoudiniAsset->GetName();
		PresetLabel = FHoudiniToolsEditor::ResolveHoudiniAssetLabel(SourceHoudiniAsset.Get());
		PresetDescription = SourceHoudiniAsset->HoudiniToolData->ToolTip;
	}
	
	const int32 NumParms = HAC->GetNumParameters();
	bApplyBakeFolder = false;
	bApplyTempCookFolder = false;
	bApplyBakeOptions = true;
	bApplyAssetOptions = true;
	bApplyStaticMeshGenSettings = true;
	bApplyProxyMeshGenSettings = true;

	TSharedRef<SVerticalBox> Container = SNew(SVerticalBox);

	auto CreateSplitterFn = [] () -> TSharedRef<SSplitter>
	{
		return SNew(SSplitter)
		
		.Style(_GetEditorStyle(), "DetailsView.Splitter")
		.PhysicalSplitterHandleSize(1.0f)
		.HitDetectionSplitterHandleSize(5.0f);
		// .HighlightedHandleIndex(ColumnSizeData.HoveredSplitterIndex)
		// .OnHandleHovered(ColumnSizeData.OnSplitterHandleHovered);
	};
	
	const FMargin ContentPadding(4.f, 2.f);

	auto AddParameterItemRowFn = [this, Container, ContentPadding] (TSharedRef<SSplitter> Splitter, const FString& Label, const FString& Name, const FString& Tooltip, const FString& ValueStr)
	{
		// Item Row
		Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Panel"))
				.Content()
				[
					Splitter
				]
			];

		// Item Label
		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetLabelColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnLabelColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Panel"))
				.Content()
				[
					SNew(SHorizontalBox)

					// Parameter Checkbox
					+ SHorizontalBox::Slot()
					[
						SNew(SCheckBox)
						.IsChecked_Lambda( [this, Name]() -> ECheckBoxState { return GetParameterCheckState(Name); } )
						.OnCheckStateChanged_Lambda( [this, Name](ECheckBoxState NewState) -> void { OnParameterCheckStateChanged(NewState, Name); bSelectAll = false; } )
						.ToolTipText( FText::FromString(Tooltip) )
						.Content()
						[
							SNew(STextBlock)
							.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
							.Text(FText::FromString(Label))
						]
					]
				]
			];

		// Item Name
		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetNameColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnNameColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Panel"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(FText::FromString(Name))
					.ToolTipText(FText::FromString(Name))
				]
			];

		// Item Value
		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetValueColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnValueColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Panel"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(FText::FromString(ValueStr))
					.ToolTipText(FText::FromString(ValueStr))
				]
			];
	};


	auto AddInputItemRowFn = [this, Container, ContentPadding] (TSharedRef<SSplitter> Splitter, const bool bIsParameterInput, const int32 InputIndex, const FString& ParameterName, const FString& Label, const FString& Tooltip, const FString& Type, const int32 NumObjects)
	{
		// Item Row
		Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 1.f)
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Panel"))
				.Content()
				[
					Splitter
				]
			];

		// Input Label
		
		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetLabelColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnLabelColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Panel"))
				.Content()
				[
					SNew(SHorizontalBox)

					// Parameter Checkbox
					
					+ SHorizontalBox::Slot()
					[
						SNew(SCheckBox)
						.IsChecked_Lambda( [this, bIsParameterInput, InputIndex, ParameterName]() -> ECheckBoxState { return bIsParameterInput ? GetParameterCheckState(ParameterName) : GetInputCheckState(InputIndex); } )
						.OnCheckStateChanged_Lambda( [this, bIsParameterInput, InputIndex, ParameterName](const ECheckBoxState NewState) -> void { if (bIsParameterInput) OnParameterCheckStateChanged(NewState, ParameterName); else OnInputCheckStateChanged(NewState, InputIndex); bSelectAll = false; } )
						// .ToolTipText( FText::FromString(Tooltip) )
						.Content()
						[
							SNew(STextBlock)
							.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
							.Text(FText::FromString(Label))
							.ToolTipText(FText::FromString(Tooltip))
						]
					]
				]
			];

		// Input Type

		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetNameColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnNameColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Panel"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(FText::FromString(Type))
					.ToolTipText(FText::FromString(Type))
				]
			];

		// Number of Objects
		
		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetValueColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnValueColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Panel"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text(FText::FromString(FString::FromInt(NumObjects)))
					.ToolTipText(FText::FromString(FString::FromInt(NumObjects)))
				]
			];
	};

	// Preset settings

	// Grid panel for Preset properties
	TSharedRef<SGridPanel> GridPanel =
		SNew(SGridPanel)
		.FillColumn(1, 1.0f);
	
	Container->AddSlot()
	.AutoHeight()
	.Padding(0.f, 0.f, 0.f, 10.f)
	[
		SNew(STextBlock)
			.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
			.Text( LOCTEXT("CreatePresetFromHDA_PresetPropertiesHeading", "Properties") )
			.ToolTipText( LOCTEXT("CreatePresetFromHDA_PresetPropertiesHeadingTooltip", "Properties that define the behaviour of the preset") )
			.TransformPolicy(ETextTransformPolicy::ToUpper)
	];
	Container->AddSlot()
	.AutoHeight()
	[
		GridPanel
	];

	// Preset - Name
	int Row = 0;
	GridPanel->AddSlot(0, Row)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CreatePresetFromHDA_Name", "Asset Name"))
			.ToolTipText(LOCTEXT("CreatePresetFromHDA_NameTooltip", "The name of the HoudiniPreset asset."))
		];
	GridPanel->AddSlot(1, Row)
		.VAlign(VAlign_Center)
		.Padding(0.f, 3.f)
		[
			SNew(SEditableTextBox)
			.Text_Lambda( [this]() -> FText { return FText::FromString(PresetName); } )
			.IsEnabled( IsAssetNameEnabled() )
			.OnTextChanged(this, &SHoudiniPresetUIBase::OnPresetNameChanged)
			.OnTextCommitted(this, &SHoudiniPresetUIBase::OnPresetNameCommitted)
		];

	// Preset - Label
	
	Row++;
	GridPanel->AddSlot(0, Row)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CreatePresetFromHDA_Label", "Label"))
			.ToolTipText(LOCTEXT("CreatePresetFromHDA_LabelTooltip", "The label is the display name of the preset, typically seen in the UI."))
		];
	GridPanel->AddSlot(1, Row)
		.VAlign(VAlign_Center)
		.Padding(0.f, 3.f)
		[
			SNew(SEditableTextBox)
			.Text_Lambda( [this]() -> FText { return FText::FromString(PresetLabel); } )
			.OnTextChanged(this, &SHoudiniPresetUIBase::OnPresetLabelChanged)
			.OnTextCommitted(this, &SHoudiniPresetUIBase::OnPresetLabelCommitted)
		];

	// Preset - Description

	Row++;
	GridPanel->AddSlot(0, Row)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CreatePresetFromHDA_Label", "Description"))
			.ToolTipText(LOCTEXT("CreatePresetFromHDA_LabelTooltip", "Description for the preset."))
		];
	GridPanel->AddSlot(1, Row)
		.VAlign(VAlign_Center)
		.Padding(0.f, 3.f)
		[
			SNew(SEditableTextBox)
			.Text_Lambda( [this]() -> FText { return FText::FromString(PresetDescription); } )
			.OnTextChanged(this, &SHoudiniPresetUIBase::OnPresetDescriptionChanged)
			.OnTextCommitted(this, &SHoudiniPresetUIBase::OnPresetDescriptionCommitted)
		];

	// Preset - SourceHoudiniAsset
	
	Row++;
	GridPanel->AddSlot(0, Row)
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CreatePresetFromHDA_Label", "Source Asset"))
			.ToolTipText(LOCTEXT("CreatePresetFromHDA_LabelTooltip", "The source HoudiniAsset for this preset. This can be used to instantiate the preset directly or limit the preset to only be applied to this HDA."))
		];

	GridPanel->AddSlot(1, Row)
		.VAlign(VAlign_Center)
		.Padding(0.f, 3.f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UHoudiniAsset::StaticClass())
			.EnableContentPicker(true)
			.DisplayUseSelected(true)
			.DisplayBrowse(true)
			.DisplayThumbnail(true)
			.ObjectPath(this, &SHoudiniPresetUIBase::GetSourceAssetPath)
			.ThumbnailPool( UThumbnailManager::Get().GetSharedThumbnailPool() )
			.OnObjectChanged(this, &SHoudiniPresetUIBase::OnPresetAssetChanged)
		];

	// Preset - Apply Only To Source
	
	Row++;
	GridPanel->AddSlot(0, Row)
		.VAlign(VAlign_Center)
		.ColumnSpan(2)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SHoudiniPresetUIBase::GetApplyOnlyToSourceCheckState)
			.OnCheckStateChanged(this, &SHoudiniPresetUIBase::OnApplyOnlyToSourceCheckStateChanged)
			.Content()
			[
				SNew(STextBlock)
				.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text(LOCTEXT("CreatePresetFromHDA_ApplyOnlyToSource", "Apply Only To Source Asset"))
				.ToolTipText(LOCTEXT("CreatePresetFromHDA_ApplyOnlyToSourceTooltip", "The preset can only be applied to HDA instances that match the SourceHoudiniAsset type."))
			]
		];

	// Preset - Can Be Instantiated
	
	Row++;
	GridPanel->AddSlot(0, Row)
		.VAlign(VAlign_Center)
		.ColumnSpan(2)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SHoudiniPresetUIBase::GetCanBeInstantiatedCheckState)
			.OnCheckStateChanged(this, &SHoudiniPresetUIBase::OnCanBeInstantiatedCheckStateChanged)
			.Content()
			[
				SNew(STextBlock)
				.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text(LOCTEXT("CreatePresetFromHDA_CanBeInstantiated", "Can be instantiated"))
				.ToolTipText(LOCTEXT("CreatePresetFromHDA_CanBeInstantiatedTooltip", "This allows the preset to be instantiated directly (instantiate the source asset and apply this preset)."))
			]
		];

	// Preset - RevertHDAParameters

	Row++;
	GridPanel->AddSlot(0, Row)
		.VAlign(VAlign_Center)
		.ColumnSpan(2)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(SCheckBox)
			.IsChecked(this, &SHoudiniPresetUIBase::GetRevertHDAParametersCheckState)
			.OnCheckStateChanged(this, &SHoudiniPresetUIBase::OnRevertHDAParametersCheckStateChanged)
			.Content()
			[
				SNew(STextBlock)
				.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
				.Text(LOCTEXT("CreatePresetFromHDA_RevertHDAParameters", "Revert HDA Parameters"))
				.ToolTipText(LOCTEXT("CreatePresetFromHDA_RevertHDAParametersTooltip", "Revert all the HDA parameters before applying this preset."))
			]
		];


	// Options

	Container->AddSlot()
	.AutoHeight()
	.Padding(0.f, 8.f, 0.f, 10.f)
	[
		SNew(STextBlock)
			.Font( FAppStyle::Get().GetFontStyle("HeadingExtraSmall") )
			.Text( LOCTEXT("CreatePresetFromHDA_PresetOptionsHeading", "Options") )
			.ToolTipText( LOCTEXT("CreatePresetFromHDA_PresetOptionsHeadingTooltip", "HDA generate, bake and asset options to include in this preset.") )
			.TransformPolicy(ETextTransformPolicy::ToUpper)
	];

	{
		// Bake Options

		{
			// Temp Cook Folder
			TSharedPtr<SSplitter> Splitter = CreateSplitterFn();
			
			Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 1.f)
			[
				Splitter.ToSharedRef()
			];
			
			Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetLabelColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnLabelColumnResized)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda( [this]() -> ECheckBoxState { return bApplyTempCookFolder ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
					.OnCheckStateChanged_Lambda( [this](const ECheckBoxState NewState) -> void { bApplyTempCookFolder = NewState == ECheckBoxState::Checked; } )
					// .ToolTipText( LOCTEXT("CreatePresetFromHDA_TempCookFolderTooltip", "Whether or not this preset will apply bake options.") )
					.Content()
					[
						SNew(STextBlock)
						.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("CreatePresetFromHDA_TempCookFolderLabel", "Temporary Cook Folder"))
					]
			];

			Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetNameColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnNameColumnResized)
			[
				SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text( FText::FromString(HAC->TemporaryCookFolder.Path) )
			];

			Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetValueColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnValueColumnResized);
			
			
			// Bake Folder
			Splitter = CreateSplitterFn();
			
			Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 1.f)
			[
				Splitter.ToSharedRef()
			];
			
			Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetLabelColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnLabelColumnResized)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda( [this]() -> ECheckBoxState { return bApplyBakeFolder ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
					.OnCheckStateChanged_Lambda( [this](const ECheckBoxState NewState) -> void { bApplyBakeFolder = NewState == ECheckBoxState::Checked; } )
					// .ToolTipText( LOCTEXT("CreatePresetFromHDA_TempCookFolderTooltip", "Whether or not this preset will apply bake options.") )
					.Content()
					[
						SNew(STextBlock)
						.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("CreatePresetFromHDA_BakeFolderLabel", "Bake Folder"))
					]
			];

			Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetNameColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnNameColumnResized)
			[
				SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text( FText::FromString(HAC->BakeFolder.Path) )
			];

			Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetValueColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnValueColumnResized);
		}


		// Apply Bake Options 

		Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 1.f)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda( [this]() -> ECheckBoxState { return bApplyBakeOptions ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
					.OnCheckStateChanged_Lambda( [this](const ECheckBoxState NewState) -> void { bApplyBakeOptions = NewState == ECheckBoxState::Checked; } )
					.ToolTipText( LOCTEXT("CreatePresetFromHDA_ApplyBakeOptionsTooltip", "Whether or not this preset will apply bake options.") )
					.Content()
					[
						SNew(STextBlock)
						.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("CreatePresetFromHDA_ApplyBakeOptionsTooltip", "Apply Bake Options"))
					]
			];

		TSharedPtr<SVerticalBox> BakeOptionsContainer = SNew(SVerticalBox)
			.Visibility_Lambda([this]() -> EVisibility
			{
				return bApplyBakeOptions ? EVisibility::Visible : EVisibility::Collapsed;
			});

	}

	{
		// Asset Options
		Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 1.f)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda( [this]() -> ECheckBoxState { return bApplyAssetOptions ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
					.OnCheckStateChanged_Lambda( [this](const ECheckBoxState NewState) -> void { bApplyAssetOptions = NewState == ECheckBoxState::Checked; } )
					.ToolTipText( LOCTEXT("CreatePresetFromHDA_ApplyAssetOptionsTooltip", "Whether or not this preset will apply asset options.") )
					.Content()
					[
						SNew(STextBlock)
						.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("CreatePresetFromHDA_ApplyAssetOptionsTooltip", "Apply Asset Options"))
					]
			];
	}

	
	// Parameter List Header
	
	{
		Container->AddSlot()
		.AutoHeight()
		.Padding(0.f, 8.f, 0.f, 10.f)
		[
			SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
				.Text( LOCTEXT("CreatePresetFromHDA_PresetParametersHeading", "Parameters") )
				.ToolTipText( LOCTEXT("CreatePresetFromHDA_PresetPropertiesHeadingTooltip", "HDA parameters that will be included in this preset.") )
				.TransformPolicy(ETextTransformPolicy::ToUpper)
		];

		Container->AddSlot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, 0.f)
		[
			SNew(SCheckBox)
				.IsChecked_Lambda( [this]() -> ECheckBoxState { return bSelectAll ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
				.OnCheckStateChanged_Lambda( [this](ECheckBoxState NewState) -> void
				{
					bSelectAll =  NewState == ECheckBoxState::Checked;
					if (bSelectAll)
					{
						SelectAllParameters();
					}
					else
					{
						KeepParameters.Empty();
					}
				} )
				.ToolTipText( LOCTEXT("CreatePresetFromHDA_SelectAllTooltip", "Select or deselect all parameters") )
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
					.Text( LOCTEXT("CreatePresetFromHDA_SelectAllTooltip", "Select All") )
				]
		];

		
		// Header (transparent splitter)
		TSharedRef<SSplitter> Splitter = SNew(SSplitter)
			.Style(_GetEditorStyle(), "PropertyTable.InViewport.Splitter")
			.PhysicalSplitterHandleSize(1.0f)
			.HitDetectionSplitterHandleSize(5.0f);

		// Header - Row
		Container->AddSlot()
			.Padding(FMargin(0.f, 5.f))
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Header"))
				.Content()
				[
					Splitter
				]
			];

		// Header - Label
		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetLabelColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnLabelColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Header"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.BoldFont")))
					.Text(FText::FromString("Label"))
				]
			];
		
		// Header - Name
		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetNameColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnNameColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Header"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.BoldFont")))
					.Text(FText::FromString("Name"))
				]
			];

		// Header - Value
		Splitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetValueColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnValueColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Header"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.BoldFont")))
					.Text(FText::FromString("Value"))
				]
			];
	}

	// List of parameter checkboxes
	for (int i = 0; i < NumParms; i++)
	{
		UHoudiniParameter* Param = HAC->GetParameterAt(i);
		if (!IsValid(Param))
		{
			continue;
		}

		if (Param->GetParameterName() == TEXT("HAPI"))
		{
			continue;
		}

		TSharedRef<SSplitter> Splitter = CreateSplitterFn();
		
		const FString ParamName = Param->GetParameterName();
		const FString ParamLabel = Param->GetParameterLabel();
		const FString ParamTooltip = Param->GetParameterHelp();

		// By default we'll include all parameters for the preset
		KeepParameters.Add(ParamName);
		
		switch (Param->GetParameterType())
		{
			// Float based parameters
			case EHoudiniParameterType::Color:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterColor>(Param), FloatValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::ColorRamp:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterRampColor>(Param), RampColorValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::File:
			case EHoudiniParameterType::FileDir:
			case EHoudiniParameterType::FileGeo:
			case EHoudiniParameterType::FileImage:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterFile>(Param), StringValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::Float:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterFloat>(Param), FloatValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::FloatRamp:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterRampFloat>(Param), RampFloatValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::Input:
				// This input will be processed during the Input Loop at the end
				break;
			case EHoudiniParameterType::Int:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterInt>(Param), IntValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::IntChoice:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterChoice>(Param), IntValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::Toggle:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterToggle>(Param), IntValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::String:
			case EHoudiniParameterType::StringAssetRef:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterString>(Param), StringValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			case EHoudiniParameterType::StringChoice:
				if (FString ValueStr; FHoudiniPresetHelpers::IngestParameter(Cast<UHoudiniParameterChoice>(Param), StringValues, ValueStr))
				{
					AddParameterItemRowFn(Splitter, ParamLabel, ParamName, ParamTooltip, ValueStr);
				}
				break;
			
			default: ;
		}
	} // End for loop over parms

	// Inputs

	{
		Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 8.f, 0.f, 10.f)
			[
				SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
					.Text( LOCTEXT("CreatePresetFromHDA_PresetInputsHeading", "Inputs") )
					.ToolTipText( LOCTEXT("CreatePresetFromHDA_PresetPropertiesHeadingTooltip", "HDA parameters that will be included in this preset.") )
					.TransformPolicy(ETextTransformPolicy::ToUpper)
			];

		// Input Column Header (transparent splitter)
		
		TSharedRef<SSplitter> HeaderSplitter = SNew(SSplitter)
			.Style(_GetEditorStyle(), "PropertyTable.InViewport.Splitter")
			.PhysicalSplitterHandleSize(1.0f)
			.HitDetectionSplitterHandleSize(5.0f);

		// Header - Row
		Container->AddSlot()
			.Padding(FMargin(0.f, 5.f))
			.AutoHeight()
			[
				SNew(SBorder)
				.Padding(FMargin(4.f, 4.f, 4.f, 4.f))
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Header"))
				.Content()
				[
					HeaderSplitter
				]
			];

		// Header - Label
		HeaderSplitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetLabelColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnLabelColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Header"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.BoldFont")))
					.Text(FText::FromString("Label"))
				]
			];
		
		// Header - Type
		HeaderSplitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetNameColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnNameColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Header"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.BoldFont")))
					.Text(FText::FromString("Type"))
				]
			];

		// Header - Number of Objects
		HeaderSplitter->AddSlot()
			.Value(this, &SHoudiniPresetUIBase::GetValueColumnWidth)
			.OnSlotResized(this, &SHoudiniPresetUIBase::OnValueColumnResized)
			[
				SNew(SBorder)
				.Padding(ContentPadding)
				.BorderImage(FAppStyle::Get().GetBrush("DetailsView.CategoryMiddle"))
				.BorderBackgroundColor(FAppStyle::Get().GetSlateColor("Colors.Header"))
				.Content()
				[
					SNew(STextBlock)
					.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.BoldFont")))
					.Text(FText::FromString("Number of Objects"))
				]
			];

		// Ingest hard wired inputs
		const int32 NumInputs = HAC->GetNumInputs();
		for (int32 i = 0; i < NumInputs; i++)
		{
			UHoudiniInput* Input = HAC->GetInputAt(i);
			if (!IsValid(Input))
			{
				continue;
			}
			if (!FHoudiniPresetHelpers::IsSupportedInputType(Input->GetInputType()))
			{
				continue;
			}

			const bool bIsObjectPathParameter = Input->IsObjectPathParameter();
			FString ParameterName;
			FString InputLabel;
			FString InputTooltip;
			if (bIsObjectPathParameter)
			{
				ParameterName = Input->GetInputName();
				UHoudiniParameter* Param = HAC->FindParameterByName(ParameterName);
				if (IsValid(Param))
				{
					InputLabel = Param->GetParameterLabel();
					InputTooltip = FString::Format(TEXT("{0} ({1})"), { Param->GetParameterLabel(), ParameterName});
				}
			}
			else
			{
				InputLabel = FString::Format(TEXT("Input: {0}"), {Input->GetInputIndex()});
				InputTooltip = InputLabel;
			}
			
			if (bIsObjectPathParameter)
			{
				// This input is a parameter-based input. Track it along with other inputs.
				KeepParameters.Add(ParameterName);
			}
			else
			{
				// This is an absolute (hardwired) input. Track it by index.
				KeepInputs.Add(i);
			}
			
			const int32 NumObjects = Input->GetNumberOfInputObjects();
			FString InputTypeName = UHoudiniInput::InputTypeToString( Input->GetInputType() );
			FString ValueStr;
			FHoudiniPresetHelpers::IngestGenericInput(Input, bIsObjectPathParameter, ParameterName, InputValues, ValueStr);

			TSharedRef<SSplitter> Splitter = CreateSplitterFn();

			AddInputItemRowFn(Splitter, bIsObjectPathParameter, i, ParameterName, InputLabel, InputTooltip, InputTypeName, NumObjects);
		}
	}

	// Mesh Generation Settings

	Container->AddSlot()
	.AutoHeight()
	.Padding(0.f, 8.f, 0.f, 10.f)
	[
		SNew(STextBlock)
			.Font( FAppStyle::Get().GetFontStyle("HeadingExtraSmall") )
			.Text( LOCTEXT("CreatePresetFromHDA_PresetMeshGenHeading", "Mesh Generation") )
			.ToolTipText( LOCTEXT("CreatePresetFromHDA_PresetMeshGenHeadingTooltip", "Mesh generation settings to include in this preset.") )
			.TransformPolicy(ETextTransformPolicy::ToUpper)
	];

	{
		// Houdini Mesh Generation
		Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 1.f)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda( [this]() -> ECheckBoxState { return bApplyStaticMeshGenSettings ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
					.OnCheckStateChanged_Lambda( [this](const ECheckBoxState NewState) -> void { bApplyStaticMeshGenSettings = NewState == ECheckBoxState::Checked; } )
					.ToolTipText( LOCTEXT("CreatePresetFromHDA_ApplyMeshGenSettingsTooltip", "Whether or not this preset will apply Mesh Generation settings.") )
					.Content()
					[
						SNew(STextBlock)
						.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("CreatePresetFromHDA_ApplyMeshGenSettingsTooltip", "Apply Mesh Generation Settings"))
					]
			];

		// Houdini Proxy Mesh Generation
		Container->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 1.f)
			[
				SNew(SCheckBox)
					.IsChecked_Lambda( [this]() -> ECheckBoxState { return bApplyProxyMeshGenSettings ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; } )
					.OnCheckStateChanged_Lambda( [this](const ECheckBoxState NewState) -> void { bApplyProxyMeshGenSettings = NewState == ECheckBoxState::Checked; } )
					.ToolTipText( LOCTEXT("CreatePresetFromHDA_ApplyProxyMeshGenSettingsTooltip", "Whether or not this preset will apply Proxy Mesh Generation settings.") )
					.Content()
					[
						SNew(STextBlock)
						.Font(_GetEditorStyle().GetFontStyle(TEXT("PropertyWindow.NormalFont")))
						.Text(LOCTEXT("CreatePresetFromHDA_ApplyProxyMeshGenSettingsTooltip", "Apply Proxy Mesh Generation Settings"))
					]
			];
		
	}

	// Main Panel Structure

	TSharedPtr<SWidget> ActionButtons = CreateActionButtonsRow();
	TSharedPtr<SVerticalBox> DescriptionLinesContainer = SNew(SVerticalBox);

	for( const FText& Line : GetDescriptionLines()  )
	{
		DescriptionLinesContainer->AddSlot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.AutoWrapText(true)
				.Text(Line)
			];
	}

	ChildSlot
	[
		SNew(SBorder)
		.Padding(18.0f)
		.BorderImage( _GetBrush("Docking.Tab.ContentAreaBrush") )
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::Get().GetBrush("Brushes.Panel"))
			.Padding(10.0f)
			[
				SNew(SVerticalBox)

				// -----------------------------
				// Dialog Title
				// -----------------------------
				+ SVerticalBox::Slot()
				.VAlign(VAlign_Center)
				.HAlign(HAlign_Fill)
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(FAppStyle::Get().GetFontStyle("HeadingExtraSmall"))
					.Text( GetDialogTitle() )
					.TransformPolicy(ETextTransformPolicy::ToUpper)
				]
				
				// -----------------------------
				// Description
				// -----------------------------
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f)
				[
					DescriptionLinesContainer.ToSharedRef()
				]
				
				// -----------------------------
				// Error Box
				// -----------------------------
				+SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 5.0f)
				[
					SNew(SWarningOrErrorBox)
					.MessageStyle(EMessageStyle::Error)
					.Visibility(this, &SHoudiniPresetUIBase::GetErrorVisibility)
					.Message(this, &SHoudiniPresetUIBase::GetErrorText)
				]
			
				// Scrollable properties / parameters container
				+SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+SScrollBox::Slot()
					[
						Container
					]
				]

				// Status Bar
				
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SGridPanel)

					// Preset Location
					
					+SGridPanel::Slot(0,0)
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("CreatePresetFromHDA_LocationLabel", "Location"))
						.ToolTipText(LOCTEXT("CreatePresetFromHDA_LocationTooltip", "Location of the resulting preset asset."))
					]
					+SGridPanel::Slot(1, 0)
					.VAlign(VAlign_Center)
					.Padding(0.f, 5.0f)
					[
						SNew(STextBlock)
						.Text(this, &SHoudiniPresetUIBase::GetPresetAssetPathText)
					]
				]

				// Action Bar
				
				+SVerticalBox::Slot()
				.AutoHeight()
				[
					// Action Buttons Row
					ActionButtons.IsValid() ? ActionButtons.ToSharedRef() : CreateEmptyActionButtonsRow()
				]
			]
		]
	];

	PostConstruct();
}

TSharedRef<SWidget> SHoudiniPresetUIBase::CreateEmptyActionButtonsRow()
{
	return SNew(SHorizontalBox)
	+SHorizontalBox::Slot()
	.HAlign(HAlign_Right)
	[
		// Create Preset Button
	
		SNew(SButton)
		.ButtonStyle(&_GetEditorStyle().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
		.IsEnabled(false)
		.Visibility(EVisibility::Hidden)
		.Content()
		[
			SNew(STextBlock)
			.TextStyle( &_GetEditorStyle().GetWidgetStyle<FTextBlockStyle>("PrimaryButtonText") )
			.Text(FText::FromString(TEXT("DummyText")))
		]
	];
}


TSharedRef<SWindow>
SHoudiniPresetUIBase::CreateFloatingWindow(
	const FText& WindowTitle,
	const FVector2D InitialSize
	)
{
	 TSharedRef<SWindow> NewSlateWindow = SNew(SWindow)
		.Title(WindowTitle)
		.ClientSize(InitialSize)
		.SizingRule( ESizingRule::UserSized )
		.SupportsMaximize(false).SupportsMinimize(false);

	// If the main frame exists parent the window to it
	TSharedPtr< SWindow > ParentWindow;
	if ( FModuleManager::Get().IsModuleLoaded("MainFrame") )
	{
		IMainFrameModule& MainFrame = FModuleManager::GetModuleChecked<IMainFrameModule>("MainFrame");
		ParentWindow = MainFrame.GetParentWindow();
	}

	if ( ParentWindow.IsValid() )
	{
		// Parent the window to the main frame 
		FSlateApplication::Get().AddWindowAsNativeChild( NewSlateWindow, ParentWindow.ToSharedRef() );
	}
	else
	{
		FSlateApplication::Get().AddWindow( NewSlateWindow );
	}

	FDetailsViewArgs Args;
	Args.bHideSelectionTip = true;
	Args.bLockable = false;
	Args.bAllowMultipleTopLevelObjects = true;
	Args.ViewIdentifier = FName(WindowTitle.ToString());
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	Args.bShowPropertyMatrixButton = false;
	Args.bShowOptions = false;
	Args.bShowModifiedPropertiesOption = false;
	Args.bShowObjectLabel = false;

	return NewSlateWindow;
}

ECheckBoxState SHoudiniPresetUIBase::GetParameterCheckState(const FString& ParamName) const
{
	if (KeepParameters.Contains(ParamName))
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

void SHoudiniPresetUIBase::OnParameterCheckStateChanged(ECheckBoxState CheckState, const FString& ParamName)
{
	if (CheckState == ECheckBoxState::Checked)
	{
		KeepParameters.Add(ParamName);
	}
	else
	{
		KeepParameters.Remove(ParamName);
	}
}

ECheckBoxState SHoudiniPresetUIBase::GetInputCheckState(const int32 InputIndex) const
{
	if (KeepInputs.Contains(InputIndex))
	{
		return ECheckBoxState::Checked;
	}
	return ECheckBoxState::Unchecked;
}

void SHoudiniPresetUIBase::OnInputCheckStateChanged(ECheckBoxState CheckState, const int32 InputIndex)
{
	if (CheckState == ECheckBoxState::Checked)
	{
		KeepInputs.Add(InputIndex);
	}
	else
	{
		KeepInputs.Remove(InputIndex);
	}
}

FString SHoudiniPresetUIBase::GetSourceAssetPath() const
{
	return SourceHoudiniAsset.IsValid() ? SourceHoudiniAsset->GetPathName() : FString("");
}

void SHoudiniPresetUIBase::OnPresetAssetChanged(const FAssetData& AssetData)
{
	SourceHoudiniAsset = Cast<UHoudiniAsset>(AssetData.GetAsset());
}

FString SHoudiniPresetUIBase::GetPresetAssetBasePath() const
{
	FString PresetBasePath;
	if (HoudiniAssetComponent.IsValid() && IsValid(HoudiniAssetComponent->GetHoudiniAsset()))
	{
		return FPaths::Combine( FPaths::GetPath( HoudiniAssetComponent->GetHoudiniAsset()->GetPathName() ), TEXT("Presets") );
	}
	
	return FString();
}

FString SHoudiniPresetUIBase::GetPresetAssetPath() const
{
	FString PresetBasePath = GetPresetAssetBasePath();

	if (PresetBasePath.IsEmpty())
	{
		return FString();
	}

	return FPaths::Combine(PresetBasePath, PresetName);
}

bool
SHoudiniPresetUIBase::IsValidPresetPath()
{
	if (!FHoudiniToolsEditor::IsValidPackageName(PresetName, &PresetNameError))
	{
		return false;
	}

	return true;
}

EVisibility
SHoudiniPresetUIBase::GetErrorVisibility() const
{
	if (!bIsValidPresetPath)
	{
		return EVisibility::Visible; 
	}

	return EVisibility::Hidden;
}

FText
SHoudiniPresetUIBase::GetErrorText() const
{
	if (!bIsValidPresetPath)
	{
		return PresetNameError;
	}
	
	return FText();
}

void
SHoudiniPresetUIBase::SelectAllParameters()
{
	if (!HoudiniAssetComponent.IsValid())
	{
		return;
	}
	
	UHoudiniAssetComponent* HAC = HoudiniAssetComponent.Get();
	const int32 NumParams = HAC->GetNumParameters();
	for (int i = 0; i < NumParams; i++)
	{
		UHoudiniParameter* Param = HAC->GetParameterAt(i);
		if (!IsValid(Param))
		{
			continue;
		}
		KeepParameters.Add(Param->GetParameterName());
	}
}

void SHoudiniPresetUIBase::PopulateAssetFromUI(UHoudiniPreset* Asset)
{
	if (!IsValid(Asset))
	{
		HOUDINI_LOG_ERROR(TEXT("Error populating Preset from UI. Invalid UHoudiniPreset asset."));
		return;
	}

	if (!HoudiniAssetComponent.IsValid())
	{
		HOUDINI_LOG_ERROR(TEXT("Error populating Preset from UI. Invalid Houdini Asset Component asset."));
		return;
	}
	
	const UHoudiniAssetComponent* HAC = HoudiniAssetComponent.Get();

	Asset->Modify();
	
	// Populate the preset asset
	Asset->Name = PresetLabel;
	Asset->Description = PresetDescription;
	Asset->SourceHoudiniAsset = SourceHoudiniAsset.Get();
	Asset->bApplyOnlyToSource = bApplyOnlyToSource;
	Asset->bCanInstantiate = bCanInstantiate;
	Asset->bRevertHDAParameters = bRevertHDAParameters;

	Asset->bApplyTemporaryCookFolder = bApplyTempCookFolder;
	Asset->TemporaryCookFolder = HAC->TemporaryCookFolder.Path;
	Asset->bApplyBakeFolder = bApplyBakeFolder;
	Asset->BakeFolder = HAC->BakeFolder.Path;

	FHoudiniToolsEditor::CopySettingsToPreset(HAC, bApplyAssetOptions, bApplyBakeOptions, bApplyStaticMeshGenSettings, bApplyProxyMeshGenSettings, Asset);

	// Transfer int params that we want to keep (checked by the user)
	Asset->IntParameters.Empty();
	for (auto& Entry : IntValues)
	{
		if (KeepParameters.Contains(Entry.Key))
		{
			Asset->IntParameters.Add(Entry.Key, Entry.Value);
		}
	}
	
	// Transfer float parameters
	Asset->FloatParameters.Empty();
	for (auto& Entry : FloatValues)
	{
		if (KeepParameters.Contains(Entry.Key))
		{
			Asset->FloatParameters.Add(Entry.Key, Entry.Value);
		}
	}

	// Transfer string parameters
	Asset->StringParameters.Empty();
	for (auto& Entry : StringValues)
	{
		if (KeepParameters.Contains(Entry.Key))
		{
			Asset->StringParameters.Add(Entry.Key, Entry.Value);
		}
	}
	
	// Transform Ramp (float) parameters
	Asset->RampFloatParameters.Empty();
	for (auto& Entry : RampFloatValues)
	{
		if (KeepParameters.Contains(Entry.Key))
		{
			Asset->RampFloatParameters.Add(Entry.Key, Entry.Value);
		}
	}

	// Transform Ramp (color) parameters
	Asset->RampColorParameters.Empty();
	for (auto& Entry : RampColorValues)
	{
		if (KeepParameters.Contains(Entry.Key))
		{
			Asset->RampColorParameters.Add(Entry.Key, Entry.Value);
		}
	}

	// Transfer input parameters
	Asset->InputParameters.Empty();
	for (int32 InputIndex = 0; InputIndex < InputValues.Num(); ++InputIndex)
	{
		auto& Entry = InputValues[InputIndex];
		if (Entry.bIsParameterInput)
		{
			if (KeepParameters.Contains(Entry.ParameterName))
			{
				Asset->InputParameters.Add(Entry);
			}
		}
		else
		{
			if (KeepInputs.Contains(InputIndex))
			{
				Asset->InputParameters.Add(Entry);
			}
		}
	}

	Asset->MarkPackageDirty();

	// Capture thumbnail for the preset from the viewport.
	FViewport* Viewport = GEditor->GetActiveViewport();
	if ( ensure(GCurrentLevelEditingViewportClient) && ensure(Viewport) )
	{
		//have to re-render the requested viewport
		FLevelEditorViewportClient* OldViewportClient = GCurrentLevelEditingViewportClient;
		//remove selection box around client during render
		GCurrentLevelEditingViewportClient = NULL;
		Viewport->Draw();


		TArray<FAssetData> AssetDataArray;
		AssetDataArray.Add( FHoudiniToolsEditor::GetAssetDataByObject(Asset) );
		AssetViewUtils::CaptureThumbnailFromViewport(Viewport, AssetDataArray);

		//redraw viewport to have the yellow highlight again
		GCurrentLevelEditingViewportClient = OldViewportClient;
		Viewport->Draw();
	}
}


FReply
SHoudiniPresetUIBase::HandleCancelClicked()
{
	CloseWindow();

	return FReply::Handled();
}


void
SHoudiniPresetUIBase::CloseWindow()
{
	TSharedPtr<SWindow> ContainingWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());

	if ( ContainingWindow.IsValid() )
	{
		ContainingWindow->RequestDestroyWindow();
	}
}


void
SHoudiniCreatePresetFromHDA::CreateDialog(TWeakObjectPtr<UHoudiniAssetComponent> HAC)
{
	const FText Title = LOCTEXT("CreatePreset_WindowTitle", "Create Preset");
	const TSharedRef<SWindow> Window = CreateFloatingWindow(Title, FVector2D(750, 600));

	Window->SetContent(
		SNew(SHoudiniCreatePresetFromHDA)
		.HoudiniAssetComponent(HAC)
		);
}


TSharedPtr<SWidget>
SHoudiniCreatePresetFromHDA::CreateActionButtonsRow()
{
	return SNew(SHorizontalBox)
		+SHorizontalBox::Slot()
		.HAlign(HAlign_Right)
		[
			// Create Preset Button

			SNew(SButton)
			.ButtonStyle(&_GetEditorStyle().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
			.OnClicked(this, &SHoudiniCreatePresetFromHDA::HandleCreatePresetClicked)
			.IsEnabled_Lambda([this]()->bool { return bIsValidPresetPath; })
			.Content()
			[
				SNew(STextBlock)
				.TextStyle( &_GetEditorStyle().GetWidgetStyle<FTextBlockStyle>("PrimaryButtonText") )
				.Text(LOCTEXT("CreatePresetFromHDA_CreatePreset", "Create Preset"))
			]
		];
}

FText SHoudiniCreatePresetFromHDA::GetDialogTitle()
{
	return LOCTEXT( "HoudiniTool_CreatePackageTitle", "Create Houdini Tool Preset " );
}

TArray<FText> SHoudiniCreatePresetFromHDA::GetDescriptionLines()
{
	return
	{
		LOCTEXT("HoudiniTool_CreatePackageDescription", "Enter a name for your new Preset asset. Asset names may only contain alphanumeric characters, and may not contain a space."),
		LOCTEXT("HoudiniTool_CreatePackageDescription2", "When you click the Create Preset button, a new Preset asset will created at the displayed location.")
	};
}


FReply
SHoudiniCreatePresetFromHDA::HandleCreatePresetClicked()
{
	if (!HoudiniAssetComponent.IsValid())
	{
		return FReply::Handled();
	}
	const UHoudiniAssetComponent* HAC = HoudiniAssetComponent.Get();
	
	const FString PresetBasePath = GetPresetAssetBasePath();

	UHoudiniPresetFactory* Factory = NewObject<UHoudiniPresetFactory>();

	const FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");
	UHoudiniPreset* Asset = Cast<UHoudiniPreset>(AssetToolsModule.Get().CreateAsset(
		PresetName, PresetBasePath,
		UHoudiniPreset::StaticClass(), Factory, FName("CreateNewHoudiniToolsPreset")));

	if (!Asset)
	{
		return FReply::Handled();
	}

	PopulateAssetFromUI(Asset);

	CloseWindow();
	
	FHoudiniToolsEditor::BrowseToObjectInContentBrowser(Asset);
	return FReply::Handled();
}


void
SHoudiniUpdatePresetFromHDA::CreateDialog(TWeakObjectPtr<UHoudiniAssetComponent> HAC)
{
	const FText Title = LOCTEXT("UpdatePreset_WindowTitle", "Update Preset");
	const TSharedRef<SWindow> Window = CreateFloatingWindow(Title, FVector2D(750, 600));

	Window->SetContent(
		SNew(SHoudiniUpdatePresetFromHDA)
		.HoudiniAssetComponent(HAC)
		);
}


FReply
SHoudiniUpdatePresetFromHDA::HandleUpdatePresetClicked()
{
	if (!Preset.IsValid())
	{
		HOUDINI_LOG_ERROR(TEXT("Preset asset is invalid. Cannot update."));
		return FReply::Handled();
	}

	PopulateAssetFromUI(Preset.Get());

	CloseWindow();

	return FReply::Handled();
}


TSharedPtr<SWidget>
SHoudiniUpdatePresetFromHDA::CreateActionButtonsRow()
{
	return SNew(SHorizontalBox)
	+SHorizontalBox::Slot()
	.HAlign(HAlign_Right)
	[
		// Create Preset Button

		SNew(SButton)
		.ButtonStyle(&_GetEditorStyle().GetWidgetStyle<FButtonStyle>("PrimaryButton"))
		.OnClicked(this, &SHoudiniUpdatePresetFromHDA::HandleUpdatePresetClicked)
		.IsEnabled_Lambda([this]()->bool { return bIsValidPresetPath; })
		.Content()
		[
			SNew(STextBlock)
			.TextStyle( &_GetEditorStyle().GetWidgetStyle<FTextBlockStyle>("PrimaryButtonText") )
			.Text(LOCTEXT("UpdatePreset_UpdatePresetButtonText", "Update Preset"))
		]
	];
}


void
SHoudiniUpdatePresetFromHDA::PostConstruct()
{
	SHoudiniPresetUIBase::PostConstruct();

	Preset.Reset();

	// Get the selected asset that needs to be updated
	TArray<FAssetData> SelectedAssets; 
	AssetSelectionUtils::GetSelectedAssets(SelectedAssets);
	if (SelectedAssets.Num() != 1)
	{
		HOUDINI_LOG_ERROR(TEXT("Error constructing Update Preset window. Only one HoudiniPreset asset may be selected."));
		CloseWindow();
		return;
	}

	const FAssetData& AssetData = SelectedAssets[0];
	UHoudiniPreset* SelectedPreset = Cast<UHoudiniPreset>(AssetData.GetAsset());
	if (!IsValid(SelectedPreset))
	{
		HOUDINI_LOG_ERROR(TEXT("Error constructing Update Preset window. A valid HoudiniPreset asset must be selected."));
		CloseWindow();
		return;
	}

	Preset = SelectedPreset;

	// Only select the parameters that are already preset in the HoudiniPreset asset.
	
	bSelectAll = false;
	KeepParameters.Empty();
	TArray<FString> Keys;
	
	SelectedPreset->FloatParameters.GetKeys(Keys);
	KeepParameters.Append( Keys );
	Keys.Empty();
	
	SelectedPreset->IntParameters.GetKeys(Keys);
	KeepParameters.Append( Keys );
	Keys.Empty();

	SelectedPreset->StringParameters.GetKeys(Keys);
	KeepParameters.Append( Keys );
	Keys.Empty();

	SelectedPreset->RampFloatParameters.GetKeys(Keys);
	KeepParameters.Append( Keys );
	Keys.Empty();

	SelectedPreset->RampColorParameters.GetKeys(Keys);
	KeepParameters.Append( Keys );
	Keys.Empty();

	KeepInputs.Empty();
	for(const FHoudiniPresetInputValue& Entry : SelectedPreset->InputParameters)
	{
		if (Entry.bIsParameterInput)
		{
			KeepParameters.Add(Entry.ParameterName);
		}
		else
		{
			KeepInputs.Add( Entry.InputIndex );
		}
	}

	// Sync other parameters from the preset to the UI
	bApplyOnlyToSource = SelectedPreset->bApplyOnlyToSource;
	bRevertHDAParameters = SelectedPreset->bRevertHDAParameters;
	bCanInstantiate = SelectedPreset->bCanInstantiate;

	bApplyTempCookFolder = SelectedPreset->bApplyTemporaryCookFolder;
	bApplyBakeFolder = SelectedPreset->bApplyBakeFolder;
	bApplyBakeOptions = SelectedPreset->bApplyBakeOptions;
	bApplyAssetOptions = SelectedPreset->bApplyAssetOptions;
	bApplyStaticMeshGenSettings = SelectedPreset->bApplyStaticMeshGenSettings;
	bApplyProxyMeshGenSettings = SelectedPreset->bApplyProxyMeshGenSettings;

	PresetLabel = SelectedPreset->Name;
	PresetDescription = SelectedPreset->Description;
	SourceHoudiniAsset = SelectedPreset->SourceHoudiniAsset;
}


FText
SHoudiniUpdatePresetFromHDA::GetDialogTitle()
{
	return LOCTEXT( "HoudiniTool_UpdatePackageTitle", "Update Houdini Tool Preset " );
}


TArray<FText>
SHoudiniUpdatePresetFromHDA::GetDescriptionLines()
{
	return
	{
		LOCTEXT("HoudiniTool_UpdatePackageDescription1", "Options and selected parameters have been populated from the selected preset. Review all parameters to ensure the desired parameters have been selected."),
		LOCTEXT("HoudiniTool_UpdatePackageDescription2", "When you click the Update Preset button, the preset will be updated to match the options and selected parameters from this dialog.")
	};
}
