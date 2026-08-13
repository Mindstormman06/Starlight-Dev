#include "UIAINBEditorNodeBase.h"

#include <rendering/ainb/UIAINBEditor.h>
#include <rendering/ImGuiNodeEditorExt.h>
#include "imgui_stdlib.h"
#include <rendering/ImGuiExt.h>
#include <util/Logger.h>
#include <util/IconsFontAwesome6.h>

namespace application::rendering::ainb
{
	UIAINBEditorNodeBase::UIAINBEditorNodeBase(int UniqueId, application::file::game::ainb::AINBFile::Node& Node) : mNodeId(UniqueId), mNode(&Node)
	{
        mOutputParameters.resize(6);
        mInputParameters.resize(6);
        mLinkedOutputParams.resize(6);

        mInputLabelCache.resize(6);
        mOutputLabelCache.resize(6);
        for (uint8_t i = 0; i < 6; i++)
        {
            mInputLabelCache[i].resize(mNode->InputParameters[i].size());
            mOutputLabelCache[i].resize(mNode->OutputParameters[i].size());
        }
	}

    const UIAINBEditorNodeBase::ParamLabelCache& UIAINBEditorNodeBase::GetCachedParamLabel(std::vector<std::vector<ParamLabelCache>>& Cache, uint8_t Type, uint8_t Index, const std::string& Name, const std::string& Class)
    {
        ParamLabelCache& Entry = Cache[Type][Index];
        if (!Entry.Built)
        {
            Entry.Label = Name + " (" + (Type == (int)application::file::game::ainb::AINBFile::ValueType::UserDefined ? Class : GetValueTypeName(Type)) + ")";
            Entry.Width = ImGui::CalcTextSize(Entry.Label.c_str()).x;
            Entry.Built = true;
        }
        return Entry;
    }

    UIAINBEditorNodeBase::ResolvedOutputPin UIAINBEditorNodeBase::ResolveLinkedOutputPin(std::vector<std::unique_ptr<UIAINBEditorNodeBase>>& Nodes, int NodeIndex, uint8_t Category, int ParameterIndex)
    {
        ResolvedOutputPin Result;

        if (NodeIndex < 0 || (size_t)NodeIndex >= Nodes.size() || ParameterIndex < 0)
            return Result;

        UIAINBEditorNodeBase* Target = Nodes[NodeIndex].get();

        if (Category < Target->mOutputParameters.size() && (size_t)ParameterIndex < Target->mOutputParameters[Category].size())
        {
            Result.Valid = true;
            Result.Category = Category;
            Result.PinId = Target->mOutputParameters[Category][ParameterIndex];
            return Result;
        }

        for (uint8_t OtherCategory = 0; OtherCategory < Target->mOutputParameters.size(); OtherCategory++)
        {
            if (OtherCategory == Category)
                continue;

            if ((size_t)ParameterIndex < Target->mOutputParameters[OtherCategory].size())
            {
                Result.Valid = true;
                Result.CrossCategory = true;
                Result.Category = OtherCategory;
                Result.PinId = Target->mOutputParameters[OtherCategory][ParameterIndex];
                return Result;
            }
        }

        return Result;
    }

    // Yellow used only for links whose source output lives in a different value-type category
    // than the consuming input expects - not necessarily broken (real game nodes do this), but
    // worth a second look, so it gets a caution color and a hover tooltip rather than looking
    // identical to an ordinary link.
    static const ImColor kCrossCategoryLinkColor = ImColor(235, 200, 40);

    void UIAINBEditorNodeBase::RenderParameterLinks(std::vector<std::unique_ptr<UIAINBEditorNodeBase>>& Nodes, uint32_t& CurrentLinkId)
    {
        for (uint8_t i = 0; i < application::file::game::ainb::AINBFile::ValueTypeCount; i++)
        {
            for (uint16_t j = 0; j < mNode->InputParameters[i].size(); j++)
            {
                application::file::game::ainb::AINBFile::InputEntry& Input = mNode->InputParameters[i][j];
                if (Input.NodeIndex >= 0) //Single link
                {
                    uint8_t SourceCategory = i;
                    if (!Input.Function.Instructions.empty())
                    {
                        application::file::game::ainb::AINBFile::ValueType DataType;
                        switch (Input.Function.InputDataType)
                        {
                        case application::file::game::ainb::EXB::Type::Bool:
                            DataType = application::file::game::ainb::AINBFile::ValueType::Bool;
                            break;
                        case application::file::game::ainb::EXB::Type::F32:
                            DataType = application::file::game::ainb::AINBFile::ValueType::Float;
                            break;
                        case application::file::game::ainb::EXB::Type::S32:
                            DataType = application::file::game::ainb::AINBFile::ValueType::Int;
                            break;
                        case application::file::game::ainb::EXB::Type::String:
                            DataType = application::file::game::ainb::AINBFile::ValueType::String;
                            break;
                        case application::file::game::ainb::EXB::Type::Vec3f:
                            DataType = application::file::game::ainb::AINBFile::ValueType::Vec3f;
                            break;
                        default:
                            DataType = (application::file::game::ainb::AINBFile::ValueType)i;
                        }

                        SourceCategory = (uint8_t)DataType;
                    }

                    ResolvedOutputPin Resolved = ResolveLinkedOutputPin(Nodes, Input.NodeIndex, SourceCategory, Input.ParameterIndex);

                    uint32_t LinkId = CurrentLinkId++;
                    mLinks.insert({ LinkId, Link {.mObjectPtr = &Input, .mType = LinkType::Parameter, .mNodeIndex = (uint16_t)Input.NodeIndex, .mParameterIndex = (uint16_t)Input.ParameterIndex, .mCrossCategory = Resolved.CrossCategory } });

                    if (Resolved.Valid)
                    {
                        ed::Link(LinkId, Resolved.PinId, mInputParameters[i][j], Resolved.CrossCategory ? kCrossCategoryLinkColor : GetValueTypeColor(i));
                    }
                    else
                    {
                        application::util::Logger::Warning("UIAINBEditorNodeBase", "Node %d input '%s' references node %d output %d, which doesn't exist under any value type category - link skipped", mNode->NodeIndex, Input.Name.c_str(), Input.NodeIndex, Input.ParameterIndex);
                    }
                }
                for (application::file::game::ainb::AINBFile::MultiEntry& Entry : Input.Sources) // Multi link
                {
                    ResolvedOutputPin Resolved = ResolveLinkedOutputPin(Nodes, Entry.NodeIndex, i, Entry.ParameterIndex);
                    if (Resolved.Valid)
                    {
                        uint32_t LinkId = CurrentLinkId++;
                        ed::Link(LinkId, Resolved.PinId, mInputParameters[i][j], Resolved.CrossCategory ? kCrossCategoryLinkColor : GetValueTypeColor(i));
                        mLinks.insert({ LinkId, Link {.mObjectPtr = &Input, .mType = LinkType::Parameter, .mNodeIndex = Entry.NodeIndex, .mParameterIndex = Entry.ParameterIndex, .mCrossCategory = Resolved.CrossCategory } });
                    }
                    else
                    {
                        application::util::Logger::Warning("UIAINBEditorNodeBase", "Node %d input '%s' multi-source references node %d output %d, which doesn't exist under any value type category - link skipped", mNode->NodeIndex, Input.Name.c_str(), Entry.NodeIndex, Entry.ParameterIndex);
                    }
                }
            }
        }
    }

    void UIAINBEditorNodeBase::Reset()
    {
        // mLinkedOutputParams is NOT cleared here - it's a pure function of graph topology, kept
        // valid across frames and only rebuilt by UIAINBEditor::RefreshLinkedOutputParams() when
        // the topology actually changes (see mLinkedOutputParamsDirty).
        for (uint8_t i = 0; i < application::file::game::ainb::AINBFile::ValueTypeCount; i++) {
            mOutputParameters[i].clear();
            mInputParameters[i].clear();
        }
        mOutputFlowParameters.clear();
        mPins.clear();
        mLinks.clear();

        mUniqueId = mNodeId;

        mIsEntryPoint = false;
    }

	bool UIAINBEditorNodeBase::ComputeCulled()
	{
		ImVec2 NodeSize = ed::GetNodeSize(mNodeId);
		if (NodeSize.x <= 0.0f || NodeSize.y <= 0.0f)
			return false; // never laid out yet - draw fully so its bounds get established

		ImVec2 NodePos = ed::GetNodePosition(mNodeId);

		// GetScreenSize() only returns the canvas's width/height, not its on-screen origin, so
		// comparing it against CanvasToScreen()'s absolute screen coordinates only worked when
		// the Graph panel happened to sit at the window's top-left corner - anywhere else (e.g.
		// docked to the right), nodes toward the right edge get culled early. Working entirely
		// in canvas space (converting the actual visible viewport into it) avoids the mismatch.
		ImVec2 WindowPos = ImGui::GetWindowPos();
		ImVec2 WindowSize = ImGui::GetWindowSize();
		ImVec2 ViewportMin = ed::ScreenToCanvas(WindowPos);
		ImVec2 ViewportMax = ed::ScreenToCanvas(ImVec2(WindowPos.x + WindowSize.x, WindowPos.y + WindowSize.y));

		// Bounds are one frame stale (they predate this frame's layout), so pad the viewport
		// test a bit to avoid pop-in right at the edges. Margin is meant as screen pixels, so
		// convert it to canvas units by the current zoom - but cap it: at low zoom (e.g. zoomed
		// out to view a widely spread-out, auto-laid-out graph) that conversion balloons into
		// hundreds/thousands of canvas units, pulling large numbers of genuinely off-screen nodes
		// into full per-frame draw cost for a pop-in guard that's imperceptible at that scale anyway.
		float Margin = 64.0f / ed::GetCurrentZoom();
		if (Margin > 256.0f)
			Margin = 256.0f;
		return NodePos.x + NodeSize.x < ViewportMin.x - Margin || NodePos.x > ViewportMax.x + Margin ||
		       NodePos.y + NodeSize.y < ViewportMin.y - Margin || NodePos.y > ViewportMax.y + Margin;
	}

	void UIAINBEditorNodeBase::Draw()
	{
		mIsCulled = application::rendering::ainb::UIAINBEditor::gEnableCullingOptimization ? ComputeCulled() : false;

		DrawImpl();

        if (!mIsCulled && ImGui::IsItemVisible())
		{
            int Alpha = ImGui::GetStyle().Alpha;
            ImColor HeaderColor = GetNodeColor();
            HeaderColor.Value.w = Alpha;

            ImDrawList* DrawList = ed::GetNodeBackgroundDrawList(mNodeId);

            const auto uv = ImVec2(
                (mNodeShapeInfo.mHeaderMax.x - mNodeShapeInfo.mHeaderMin.x) / (float)(4.0f * UIAINBEditor::gHeaderTexture->mWidth),
                (mNodeShapeInfo.mHeaderMax.y - mNodeShapeInfo.mHeaderMin.y) / (float)(4.0f * UIAINBEditor::gHeaderTexture->mHeight));

            DrawList->AddImageRounded((ImTextureID)UIAINBEditor::gHeaderTexture->mID,
                mNodeShapeInfo.mHeaderMin,
                mNodeShapeInfo.mHeaderMax,
                ImVec2(0.0f, 0.0f), uv,
#if IMGUI_VERSION_NUM > 18101
                HeaderColor, ed::GetStyle().NodeRounding, ImDrawFlags_RoundCornersTop);
#else
                HeaderColor, ed::GetStyle().NodeRounding, 1 | 2);
#endif

            if (GetNodeIndex() >= 0)
            {
                ImGuiTextBuffer Builder;
                Builder.appendf("#%i", GetNodeIndex());

                auto TextSize = ImGui::CalcTextSize(Builder.c_str());
                auto Padding = ImVec2(2.0f, 2.0f);
                auto WidgetSize = ImVec2(TextSize.x + Padding.x * 2, TextSize.y + Padding.y * 2);

                auto WidgetPosition = ImVec2(mNodeShapeInfo.mHeaderMax.x + Padding.x, mNodeShapeInfo.mHeaderMin.y - Padding.y - WidgetSize.y);

                DrawList->AddRectFilled(WidgetPosition, ImVec2(WidgetPosition.x + WidgetSize.x, WidgetPosition.y + WidgetSize.y), IM_COL32(100, 80, 80, 190), 3.0f, ImDrawFlags_RoundCornersAll);
                DrawList->AddRect(WidgetPosition, ImVec2(WidgetPosition.x + WidgetSize.x, WidgetPosition.y + WidgetSize.y), IM_COL32(200, 160, 160, 190), 3.0f, ImDrawFlags_RoundCornersAll);
                DrawList->AddText(ImVec2(WidgetPosition.x + Padding.x, WidgetPosition.y + Padding.y), IM_COL32(255, 255, 255, 255), Builder.c_str());
            }

            if (HasInternalParameters())
            {
                float BorderWidth = ed::GetStyle().NodeBorderWidth;

                ImGui::SetCursorPos(mInternalParameterStartPos);

                ImGui::Text("Internal parameters");

                ImVec2 HeaderSeparatorLeft = ImVec2(mNodeShapeInfo.mHeaderMin.x - BorderWidth / 2, ImGui::GetCursorPosY() - 2.5f);
                ImVec2 HeaderSeparatorRight = ImVec2(mNodeShapeInfo.mHeaderMax.x, ImGui::GetCursorPosY() - 1.5f);

                ed::GetNodeBackgroundDrawList(mNodeId)->AddLine(HeaderSeparatorLeft, HeaderSeparatorRight, ImColor(255, 255, 255, (int)(ImGui::GetStyle().Alpha * 255 / 2)), BorderWidth);
            }
        }
	}

	void UIAINBEditorNodeBase::DrawNodeHeader(const std::string& Title, application::file::game::ainb::AINBFile::Node* Node)
    {
        if (mEnableFlow)
        {
            float Alpha = ImGui::GetStyle().Alpha;
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Alpha);
            ImRect HeaderRect = DrawPin(mUniqueId++, mFlowLinked, Alpha * 255, PinType::Flow, ed::PinKind::Input, Title, true);
            ImGui::PopStyleVar();
            mPins.insert({ mUniqueId - 1, Pin {.mKind = ed::PinKind::Input, .mType = PinType::Flow, .mNode = Node } });
            ImGui::Dummy(ImVec2(0, 8));

            mNodeShapeInfo.mHeaderMin = ImVec2(ImGui::GetItemRectMin().x - ed::GetStyle().NodePadding.x + ed::GetStyle().NodeBorderWidth - 1, HeaderRect.GetTL().y - 12);
            mNodeShapeInfo.mHeaderMax = ImVec2(mNodeShapeInfo.mHeaderMin.x + ed::GetNodeSize(mNodeId).x - ed::GetStyle().NodeBorderWidth, ImGui::GetItemRectMin().y + ed::GetStyle().NodePadding.y - 8 + ed::GetStyle().NodeBorderWidth + 1);
        }
        else
        {
            ImGui::Text("%s", Title.c_str());
            ImGui::Dummy(ImVec2(0, 8));
            mNodeShapeInfo.mHeaderMin = ImVec2(ImGui::GetItemRectMin().x - ed::GetStyle().NodePadding.x + ed::GetStyle().NodeBorderWidth - 1, ImGui::GetItemRectMin().y - ed::GetStyle().NodePadding.y - ImGui::GetTextLineHeightWithSpacing() + ed::GetStyle().NodeBorderWidth - 1);
            mNodeShapeInfo.mHeaderMax = ImVec2(mNodeShapeInfo.mHeaderMin.x + ed::GetNodeSize(mNodeId).x - ed::GetStyle().NodeBorderWidth, ImGui::GetItemRectMin().y + ed::GetStyle().NodePadding.y - 8 + ed::GetStyle().NodeBorderWidth + 1);
        }

        // Header title is plain text, so it doesn't participate in the node editor's own click hit-test - handle it manually.
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsMouseHoveringRect(mNodeShapeInfo.mHeaderMin, mNodeShapeInfo.mHeaderMax))
        {
            std::vector<ed::NodeId> SelectedNodes;
            SelectedNodes.resize(ed::GetSelectedObjectCount());
            SelectedNodes.resize(ed::GetSelectedNodes(SelectedNodes.data(), (int)SelectedNodes.size()));
            for (ed::NodeId& Id : SelectedNodes)
                ed::DeselectNode(Id);

            std::vector<ed::LinkId> SelectedLinks;
            SelectedLinks.resize(ed::GetSelectedObjectCount());
            SelectedLinks.resize(ed::GetSelectedLinks(SelectedLinks.data(), (int)SelectedLinks.size()));
            for (ed::LinkId& Id : SelectedLinks)
                ed::DeselectLink(Id);

            ed::SelectNode(mNodeId, true);
        }
	}

    std::string UIAINBEditorNodeBase::GetValueTypeName(application::file::game::ainb::AINBFile::ValueType ValueType)
    {
        return GetValueTypeName((int)ValueType);
    }

    std::string UIAINBEditorNodeBase::GetValueTypeName(uint8_t ValueType)
    {
        switch (ValueType) {
        case 0:
            return "Int";
        case 1:
            return "Bool";
        case 2:
            return "Float";
        case 3:
            return "String";
        case 4:
            return "Vec3f";
        case 5:
            return "UserDefined";
        default:
            return "Unknown";
        }
    }

    ImColor UIAINBEditorNodeBase::GetValueTypeColor(application::file::game::ainb::AINBFile::ValueType ValueType)
    {
        return GetValueTypeColor((int)ValueType);
    }

    ImColor UIAINBEditorNodeBase::GetPinTypeColor(PinType Type)
    {
        return GetValueTypeColor((int)Type);
    }

    ImColor UIAINBEditorNodeBase::GetValueTypeColor(uint8_t ValueType)
    {
        switch (ValueType) {
        case 0:
            return ImColor(34, 215, 168);
        case 1:
            return ImColor(0, 163, 234);
        case 2:
            return ImColor(162, 250, 84);
        case 3:
            return ImColor(247, 0, 206);
        case 4:
            return ImColor(247, 195, 33);
        case 5:
            return ImColor(195, 124, 243);
        case 6:
            return ImColor(255, 255, 255);
        default:
            return ImColor(0, 0, 0);
        }
    }

    UIAINBEditorNodeBase::PinType UIAINBEditorNodeBase::ValueTypeToPinType(application::file::game::ainb::AINBFile::ValueType Type)
    {
        return ValueTypeToPinType((int)Type);
    }

    UIAINBEditorNodeBase::PinType UIAINBEditorNodeBase::ValueTypeToPinType(uint8_t Type)
    {
        switch (Type) {
        case 0:
            return PinType::Int;
        case 1:
            return PinType::Bool;
        case 2:
            return PinType::Float;
        case 3:
            return PinType::String;
        case 4:
            return PinType::Vec3f;
        case 5:
            return PinType::UserDefined;
        case 6:
            return PinType::Flow; //Should never be the case
        default:
            return PinType::Int;
        }
    }

    void UIAINBEditorNodeBase::DrawPinIcon(bool Connected, uint32_t Alpha, PinType Type)
    {
        ax::Drawing::IconType PinIconType;
        ImColor Color = GetPinTypeColor(Type);
        Color.Value.w = Alpha / 255.0f;
        switch (Type) {
        case PinType::Flow:
            PinIconType = ax::Drawing::IconType::Flow;
            break;
        case PinType::Bool:
        case PinType::Int:
        case PinType::Float:
        case PinType::String:
        case PinType::Vec3f:
        case PinType::UserDefined:
            PinIconType = ax::Drawing::IconType::Circle;
            break;
        default:
            return;
        }

        ax::Widgets::Icon(ImVec2(24.0f * ImGui::GetPlatformIO().Monitors[0].DpiScale, 24.0f * ImGui::GetPlatformIO().Monitors[0].DpiScale), PinIconType, Connected, Color, ImColor(32, 32, 32, Alpha));
    }

    void UIAINBEditorNodeBase::DrawParameterValue(application::file::game::ainb::AINBFile::ValueType Type, const std::string& Name, uint32_t Id, void* Dest)
    {
        switch (Type) {
        case application::file::game::ainb::AINBFile::ValueType::Int:
            ImGui::PushItemWidth(ImGui::CalcTextSize(std::to_string(*reinterpret_cast<int*>(Dest)).c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x * 2);
            ImGui::InputScalar(("##" + Name + std::to_string(Id)).c_str(), ImGuiDataType_S32, Dest, nullptr, nullptr, nullptr, ImGuiInputTextFlags_CharsDecimal);
            ImGui::PopItemWidth();
            break;
        case application::file::game::ainb::AINBFile::ValueType::Float:
            ImGui::PushItemWidth(ImGui::CalcTextSize(std::to_string(*reinterpret_cast<float*>(Dest)).c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x * 2);
            ImGui::InputScalar(("##" + Name + std::to_string(Id)).c_str(), ImGuiDataType_Float, Dest, nullptr, nullptr, nullptr, ImGuiInputTextFlags_CharsScientific);
            ImGui::PopItemWidth();
            break;
        case application::file::game::ainb::AINBFile::ValueType::Bool:
            ImGui::Checkbox(("##" + Name + std::to_string(Id)).c_str(), (bool*)Dest);
            break;
        case application::file::game::ainb::AINBFile::ValueType::String:
            ImGui::PushItemWidth(ImGui::CalcTextSize(reinterpret_cast<std::string*>(Dest)->c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x * 2);
            ImGui::InputText(("##" + Name + std::to_string(Id)).c_str(), (std::string*)Dest);
            ImGui::PopItemWidth();
            break;
        case application::file::game::ainb::AINBFile::ValueType::Vec3f:
            ImGuiExt::InputScalarNWidth(("##" + Name + std::to_string(Id)).c_str(), ImGuiDataType_Float, &((glm::vec3*)Dest)->x, 3, ImGui::CalcTextSize(std::to_string(((glm::vec3*)Dest)->x).c_str()).x + ImGui::CalcTextSize(std::to_string(((glm::vec3*)Dest)->y).c_str()).x + ImGui::CalcTextSize(std::to_string(((glm::vec3*)Dest)->z).c_str()).x, nullptr, nullptr, nullptr, ImGuiInputTextFlags_CharsScientific);
            break;
        case application::file::game::ainb::AINBFile::ValueType::UserDefined:
            ImGui::NewLine();
            break;
        default:
            application::util::Logger::Error("UIAINBNodeEditorBase", "Unknown parameter value type: %u", (int)Type);
            break;
        }
    }

    ImRect UIAINBEditorNodeBase::DrawPin(uint32_t Id, bool Connected, uint32_t Alpha, PinType Type, ed::PinKind Kind, const std::string& Name, bool IsHeaderPin)
    {
        ImVec2 IconPos;
        ImRect InputsRect;

        ImVec2 ReturnPos;

        if (Kind == ed::PinKind::Input)
        {
            DrawPinIcon(Connected, Alpha, Type);
            IconPos = ImGui::GetItemRectMin();
            ImGui::SameLine();
            if(!IsHeaderPin || !mIsEntryPoint)
            {
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5.0f);
                ImGui::TextUnformatted(Name.c_str());
            }
            else
            {
                ReturnPos = ImGui::GetCursorPos();
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 2.0f);
                ImGui::TextUnformatted(Name.c_str());
            }
        }
        else if (Kind == ed::PinKind::Output)
        {
            ImGui::TextUnformatted(Name.c_str());
            InputsRect = ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
            ImGui::SameLine();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 5.0f);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 5.0f);
            DrawPinIcon(Connected, Alpha, Type);
            IconPos = ImGui::GetItemRectMin();
        }

        ImRect HeaderPos = (IsHeaderPin && mIsEntryPoint) ? ImRect(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMin().y + 7.0f), ImVec2(ImGui::GetItemRectMax().x, ImGui::GetItemRectMax().y + 7.0f)) : ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        if (Kind == ed::PinKind::Input)
        {
            InputsRect = HeaderPos;

            InputsRect.Min.x -= ImGui::GetStyle().ItemSpacing.x + 24;
            InputsRect.Min.y -= 6;

            InputsRect.Max.x += 2;
            InputsRect.Max.y += 6;
        }
        else if (Kind == ed::PinKind::Output)
        {
            InputsRect.Max.x += ImGui::GetStyle().ItemSpacing.x + 24;
            InputsRect.Max.y += 6;

            InputsRect.Min.x -= 2;
            InputsRect.Min.y -= 6;
        }

        IconPos.y += 12;
        IconPos.x += 8;

        ImGui::SameLine();

        ed::BeginPin(Id, Kind);
        ed::PinPivotRect(IconPos, IconPos);
        ed::PinRect(InputsRect.GetTL(), InputsRect.GetBR());
        ed::EndPin();

        if(IsHeaderPin && mIsEntryPoint)
        {
            ImVec2 FinalPos = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(ReturnPos.x, ReturnPos.y + ImGui::CalcTextSize("a").y - ImGui::GetStyle().ItemSpacing.y));
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
            ImGui::Text("Entry Point");
            ImGui::PopStyleColor();
            ImGui::SetCursorPos(FinalPos);
        }

        return HeaderPos;
    }

    UIAINBEditorNodeBase::BlackboardChipAction UIAINBEditorNodeBase::DrawBlackboardLinkChip(uint8_t Type, const std::string& Name, uint32_t Id)
    {
        BlackboardChipAction Action;

        ImColor Color = GetValueTypeColor(Type);
        ImGui::TextColored(Color.Value, ICON_FA_LINK " %s", Name.c_str());
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Linked to Blackboard entry \"%s\"\nDrop a different entry here to relink", Name.c_str());
        Action.Relink = AcceptBlackboardDrop(Type);

        ImGui::SameLine();
        ImGui::PushID((int)Id);
        Action.Unlink = ImGui::SmallButton(ICON_FA_LINK_SLASH);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Unlink from Blackboard");
        ImGui::PopID();

        return Action;
    }

    std::optional<UIAINBEditorNodeBase::BlackboardLinkTarget> UIAINBEditorNodeBase::AcceptBlackboardDrop(uint8_t Type)
    {
        std::optional<BlackboardLinkTarget> Result;

        // Called for every visible input/internal parameter, every frame, regardless of whether
        // anything is being dragged - on the overwhelming majority of frames nothing is, so bail
        // out on a single cheap global check before touching the (per-item) BeginDragDropTarget/
        // EndDragDropTarget pair at all.
        const ImGuiPayload* ActivePayload = ImGui::GetDragDropPayload();
        if (ActivePayload == nullptr || !ActivePayload->IsDataType(UIAINBEditor::kBlackboardDragDropId))
            return Result;

        if (!ImGui::BeginDragDropTarget())
            return Result;

        if (const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload(UIAINBEditor::kBlackboardDragDropId))
        {
            UIAINBEditor::BlackboardDragPayload Dropped = *(const UIAINBEditor::BlackboardDragPayload*)Payload->Data;
            // A mismatched value type (e.g. a String Blackboard entry dropped on a Float input) is
            // silently ignored rather than corrupting the parameter.
            if (application::file::game::ainb::AINBFile::GlobalTypeToValueType((uint8_t)Dropped.GlobalType) == Type)
                Result = BlackboardLinkTarget{ Dropped.GlobalType, Dropped.Index };
        }

        ImGui::EndDragDropTarget();
        return Result;
    }

    void UIAINBEditorNodeBase::DrawInputParameter(uint8_t Type, uint8_t Index)
    {
        // Input param (-> Name [Value])
        application::file::game::ainb::AINBFile::InputEntry& Param = mNode->InputParameters[Type][Index];

        // Param.Value must always hold the variant alternative matching its bucket (Type) -
        // both the editable widget below and the linked-width calculation reinterpret_cast it.
        if (Type != Param.Value.index())
        {
            switch (Type)
            {
            case (int)application::file::game::ainb::AINBFile::ValueType::Int:
                Param.Value = (uint32_t)0;
                break;
            case (int)application::file::game::ainb::AINBFile::ValueType::Float:
                Param.Value = 0.0f;
                break;
            case (int)application::file::game::ainb::AINBFile::ValueType::Bool:
                Param.Value = false;
                break;
            case (int)application::file::game::ainb::AINBFile::ValueType::String:
                Param.Value = "";
                break;
            case (int)application::file::game::ainb::AINBFile::ValueType::Vec3f:
                Param.Value = glm::vec3(0, 0, 0);
                break;
            }
        }

        float Alpha = ImGui::GetStyle().Alpha;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Alpha);
        // The "(Type)" suffix is cached (see GetCachedParamLabel) - Name/Type/Class never change
        // after the node is constructed, so there's no per-frame cost to reusing the full label
        // even while off-screen.
        DrawPin(mUniqueId++, Param.NodeIndex >= 0 || !Param.Sources.empty() || Param.GlobalParametersIndex != 0xFFFF, Alpha * 255, ValueTypeToPinType(Type), ed::PinKind::Input,
            GetCachedParamLabel(mInputLabelCache, Type, Index, Param.Name, Param.Class).Label);
        ImGui::PopStyleVar();
        mInputParameters[Type].push_back(mUniqueId - 1);
        mPins.insert({ mUniqueId - 1, Pin {.mKind = ed::PinKind::Input, .mType = ValueTypeToPinType(Type), .mNode = mNode, .mObjectPtr = &Param, .mParameterIndex = Index } });
        ImGui::SameLine();

        if (mIsCulled)
        {
            ImGui::Dummy(ImVec2(0.0f, 0.0f));
            return;
        }

        auto ApplyBlackboardLink = [this, &Param](const BlackboardLinkTarget& Target)
        {
            mOwner->PushUndoSnapshot();
            Param.GlobalParametersIndex = Target.Index;
            Param.NodeIndex = -1;
            Param.ParameterIndex = 0;
            Param.Sources.clear();
            Param.EXBIndex = 0xFFFF;
            Param.Flags.clear();
        };

        if (Param.NodeIndex == -1 && Param.Sources.empty() && Param.GlobalParametersIndex == 0xFFFF) // Not linked to anything, so the value has to be set directly
        {
            DrawParameterValue(static_cast<application::file::game::ainb::AINBFile::ValueType>(Type), Param.Name, mUniqueId, (void*)&Param.Value);
            std::optional<BlackboardLinkTarget> Dropped = AcceptBlackboardDrop(Type);
            if (Dropped && mOwner != nullptr)
                ApplyBlackboardLink(*Dropped);
        }
        else if (Param.NodeIndex == -1 && Param.Sources.empty()) // Linked to a Blackboard entry (GlobalParametersIndex != 0xFFFF)
        {
            // Points at the live Name string rather than copying it - it can be renamed via the
            // Details panel at any time, so it isn't safe to cache the way the pin labels above are.
            static const std::string kInvalidBlackboardEntry = "<invalid Blackboard entry>";
            const std::string* EntryName = &kInvalidBlackboardEntry;
            if (mOwner != nullptr)
            {
                auto& Entries = mOwner->mAINBFile.GlobalParameters[application::file::game::ainb::AINBFile::ValueTypeToGlobalType(Type)];
                if (Param.GlobalParametersIndex < Entries.size())
                    EntryName = &Entries[Param.GlobalParametersIndex].Name;
            }

            BlackboardChipAction Action = DrawBlackboardLinkChip(Type, *EntryName, mUniqueId);
            if (mOwner != nullptr)
            {
                if (Action.Unlink)
                {
                    mOwner->PushUndoSnapshot();
                    Param.GlobalParametersIndex = 0xFFFF;
                }
                else if (Action.Relink)
                {
                    ApplyBlackboardLink(*Action.Relink);
                }
            }
        }
        else
        {
            if (Type == (int)application::file::game::ainb::AINBFile::ValueType::UserDefined)
            {
                ImGui::NewLine();
            }
            else
            {
                float Width = 0.0f;
                void* Dest = &Param.Value;
                switch (Type) {
                case (int)application::file::game::ainb::AINBFile::ValueType::Int:
                    Width = ImGui::CalcTextSize(std::to_string(*reinterpret_cast<int*>(Dest)).c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x * 2;
                    break;
                case (int)application::file::game::ainb::AINBFile::ValueType::Float:
                    Width = ImGui::CalcTextSize(std::to_string(*reinterpret_cast<float*>(Dest)).c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x * 2;
                    break;
                case (int)application::file::game::ainb::AINBFile::ValueType::Bool:
                    Width = 16.0f;
                    break;
                case (int)application::file::game::ainb::AINBFile::ValueType::String:
                    Width = ImGui::CalcTextSize(reinterpret_cast<std::string*>(Dest)->c_str()).x + ImGui::GetStyle().ItemInnerSpacing.x * 2;
                    break;
                case (int)application::file::game::ainb::AINBFile::ValueType::Vec3f:
                    Width = ImGui::CalcTextSize(std::to_string(((glm::vec3*)Dest)->x).c_str()).x + ImGui::CalcTextSize(std::to_string(((glm::vec3*)Dest)->y).c_str()).x + ImGui::CalcTextSize(std::to_string(((glm::vec3*)Dest)->z).c_str()).x;
                    break;
                }

                ImGui::Dummy(ImVec2(Width, 0));
            }
        }
    }

    void UIAINBEditorNodeBase::DrawInternalParameter(uint8_t Type, uint8_t Index)
    {
        // Internal param (Name [Value])
        application::file::game::ainb::AINBFile::ImmediateParameter& Immediate = mNode->ImmediateParameters[Type][Index];

        bool ValueTypeMismatch = Immediate.ValueType != Immediate.Value.index();
        if (ValueTypeMismatch)
        {
            switch (Type)
            {
            case (int)application::file::game::ainb::AINBFile::ValueType::Int:
                Immediate.Value = (uint32_t)0;
                break;
            case (int)application::file::game::ainb::AINBFile::ValueType::Float:
                Immediate.Value = 0.0f;
                break;
            case (int)application::file::game::ainb::AINBFile::ValueType::Bool:
                Immediate.Value = false;
                break;
            case (int)application::file::game::ainb::AINBFile::ValueType::String:
                Immediate.Value = "";
                break;
            case (int)application::file::game::ainb::AINBFile::ValueType::Vec3f:
                Immediate.Value = glm::vec3(0, 0, 0);
                break;
            }
        }

        // Internal parameters have no pins/links, so off-screen we can skip the name/value
        // widgets entirely - just reserve a row of roughly the right height so the node's
        // cached bounds (used by ComputeCulled next frame) don't collapse.
        if (mIsCulled)
        {
            ImGui::Dummy(ImVec2(0.0f, ImGui::GetFrameHeightWithSpacing()));
            mUniqueId++;
            return;
        }

        ImGui::TextUnformatted(Immediate.Name.c_str());
        ImGui::SameLine();

        if (Immediate.GlobalParametersIndex == 0xFFFF)
        {
            DrawParameterValue(static_cast<application::file::game::ainb::AINBFile::ValueType>(Type), Immediate.Name, mUniqueId, (void*)&Immediate.Value);
            std::optional<BlackboardLinkTarget> Dropped = AcceptBlackboardDrop(Type);
            if (Dropped && mOwner != nullptr)
            {
                mOwner->PushUndoSnapshot();
                Immediate.GlobalParametersIndex = Dropped->Index;
                Immediate.EXBIndex = 0xFFFF;
                Immediate.Flags.clear();
            }
        }
        else
        {
            // Points at the live Name string rather than copying it - it can be renamed via the
            // Details panel at any time, so it isn't safe to cache the way the pin labels are.
            static const std::string kInvalidBlackboardEntry = "<invalid Blackboard entry>";
            const std::string* EntryName = &kInvalidBlackboardEntry;
            if (mOwner != nullptr)
            {
                auto& Entries = mOwner->mAINBFile.GlobalParameters[application::file::game::ainb::AINBFile::ValueTypeToGlobalType(Type)];
                if (Immediate.GlobalParametersIndex < Entries.size())
                    EntryName = &Entries[Immediate.GlobalParametersIndex].Name;
            }

            BlackboardChipAction Action = DrawBlackboardLinkChip(Type, *EntryName, mUniqueId);
            if (mOwner != nullptr)
            {
                if (Action.Unlink)
                {
                    mOwner->PushUndoSnapshot();
                    Immediate.GlobalParametersIndex = 0xFFFF;
                }
                else if (Action.Relink)
                {
                    mOwner->PushUndoSnapshot();
                    Immediate.GlobalParametersIndex = Action.Relink->Index;
                    Immediate.EXBIndex = 0xFFFF;
                    Immediate.Flags.clear();
                }
            }
        }
        mUniqueId++;
    }

    void UIAINBEditorNodeBase::DrawInternalParameterSeparator()
    {
        if (HasInternalParameters())
        {
            mInternalParameterStartPos = ImGui::GetCursorPos();
            ImGui::NewLine();
        }
    }

    void UIAINBEditorNodeBase::DrawOutputParameter(uint8_t Type, uint8_t Index)
    {
        //Output param (ParamName (Type/Class) ->)
        application::file::game::ainb::AINBFile::OutputEntry& Param = mNode->OutputParameters[Type][Index];

        // Label/width are cached (see GetCachedParamLabel) - Name/Type/Class never change after
        // the node is constructed, so this is a lookup rather than a rebuild+remeasure every frame.
        const ParamLabelCache& LabelCache = GetCachedParamLabel(mOutputLabelCache, Type, Index, Param.Name, Param.Class);

        if (!mIsCulled)
        {
            float Offset = (mNodeShapeInfo.mHeaderMax.x - mNodeShapeInfo.mHeaderMin.x) - ed::GetStyle().NodePadding.x - ed::GetStyle().NodeBorderWidth - 24.0f * ImGui::GetPlatformIO().Monitors[0].DpiScale - ImGui::GetStyle().ItemSpacing.x * 2.0f - LabelCache.Width;
            if(Offset > 0)
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Offset);
        }
        float Alpha = ImGui::GetStyle().Alpha;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Alpha);
        DrawPin(mUniqueId++, std::find(mLinkedOutputParams[Type].begin(), mLinkedOutputParams[Type].end(), Index) != mLinkedOutputParams[Type].end(), Alpha * 255, ValueTypeToPinType(Type), ed::PinKind::Output,
            LabelCache.Label);
        ImGui::PopStyleVar();
        mOutputParameters[Type].push_back(mUniqueId - 1);
        mPins.insert({ mUniqueId - 1, Pin {.mKind = ed::PinKind::Output, .mType = ValueTypeToPinType(Type), .mNode = mNode, .mObjectPtr = &Param, .mParameterIndex = Index } });
    }

    void UIAINBEditorNodeBase::DrawOutputFlowParameter(const std::string& Text, bool Linked, uint8_t Index, bool AllowMultipleLinks)
    {
        if (!mIsCulled)
        {
            float Offset = (mNodeShapeInfo.mHeaderMax.x - mNodeShapeInfo.mHeaderMin.x) - ed::GetStyle().NodePadding.x - ed::GetStyle().NodeBorderWidth - 24.0f * ImGui::GetPlatformIO().Monitors[0].DpiScale - ImGui::GetStyle().ItemSpacing.x * 2.0f - ImGui::CalcTextSize(Text.c_str()).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + Offset);
        }
        float Alpha = ImGui::GetStyle().Alpha;
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, Alpha);
        DrawPin(mUniqueId++, Linked, Alpha * 255, PinType::Flow, ed::PinKind::Output, Text);
        mPins.insert({ mUniqueId - 1, Pin {.mKind = ed::PinKind::Output, .mType = PinType::Flow, .mNode = mNode, .mParameterIndex = Index, .mAllowMultipleLinks = AllowMultipleLinks, .mAlreadyLinked = Linked } });
        ImGui::PopStyleVar();

        mOutputFlowParameters.insert({ Text, mUniqueId - 1 });
    }

    int UIAINBEditorNodeBase::GetNodeIndex()
    {
        return -1;
    }

    bool UIAINBEditorNodeBase::HasInternalParameters()
    {
        return false;
    }
}