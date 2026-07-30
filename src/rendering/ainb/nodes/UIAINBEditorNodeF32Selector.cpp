#include "UIAINBEditorNodeF32Selector.h"

#include <util/Logger.h>
#include <algorithm>

namespace application::rendering::ainb::nodes
{
    application::rendering::popup::PopUpBuilder UIAINBEditorNodeF32Selector::gAddNewSelection;

    // A branch is the Default/fallback iff DynamicStateName was actually read from the file,
    // which only happens for the last (IsEnd) child - see AINBFile.cpp's Element_F32Selector
    // read/write logic. Every other branch leaves it at its "MapEditor_AINB_NoVal" default and
    // uses ConditionMin/ConditionMax instead.
    static bool IsDefaultBranch(const application::file::game::ainb::AINBFile::LinkedNodeInfo& Info)
    {
        return Info.DynamicStateName != "MapEditor_AINB_NoVal";
    }

    static std::string BuildCaseLabel(float Min, float Max)
    {
        return "[" + std::to_string(Min) + ", " + std::to_string(Max) + ")";
    }

    void UIAINBEditorNodeF32Selector::Initialize()
    {
        gAddNewSelection.Title("Add F32 Case").Flag(ImGuiWindowFlags_NoResize).NeedsConfirmation(false)
            .AddDataStorage(sizeof(float))
            .AddDataStorage(sizeof(float))
            .ContentDrawingFunction([](popup::PopUpBuilder& Builder)
        {
            if (ImGui::BeginTable("F32Table", 2, ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("F32 Case Min").x);

				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::Text("F32 Case Min");
				ImGui::TableNextColumn();
				ImGui::PushItemWidth(ImGui::GetCurrentTable()->Columns[1].WidthMax);
                ImGui::InputScalar("##F32CaseMin", ImGuiDataType_Float, Builder.GetDataStorage(0).mPtr, nullptr, nullptr, nullptr, ImGuiInputTextFlags_CharsScientific);
                ImGui::PopItemWidth();

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::Text("F32 Case Max");
                ImGui::TableNextColumn();
                ImGui::PushItemWidth(ImGui::GetCurrentTable()->Columns[1].WidthMax);
                ImGui::InputScalar("##F32CaseMax", ImGuiDataType_Float, Builder.GetDataStorage(1).mPtr, nullptr, nullptr, nullptr, ImGuiInputTextFlags_CharsScientific);
                ImGui::PopItemWidth();

                ImGui::EndTable();
            }
        }).Register();
    }

    UIAINBEditorNodeF32Selector::UIAINBEditorNodeF32Selector(int UniqueId, application::file::game::ainb::AINBFile::Node& Node) : UIAINBEditorNodeBase(UniqueId, Node)
    {
        for (application::file::game::ainb::AINBFile::LinkedNodeInfo& Info : mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink])
        {
            if(!IsDefaultBranch(Info))
            {
                mConditions.push_back({ Info.ConditionMin, Info.ConditionMax });
            }
        }
    }

    void UIAINBEditorNodeF32Selector::DrawImpl()
    {
        ed::PushStyleVar(ed::StyleVar_NodePadding, ImVec4(8, 8, 8, 8));
        ed::BeginNode(mUniqueId++);

        DrawNodeHeader(mNode->GetName(), mNode);

        for (uint8_t Type = 0; Type < application::file::game::ainb::AINBFile::ValueTypeCount; Type++)
		{
			for (uint8_t Index = 0; Index < mNode->InputParameters[Type].size(); Index++)
			{
				DrawInputParameter(Type, Index);
			}
		}

        if (!mIsCulled)
        {
            float BasePosX = ImGui::GetCursorPosX();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            if(ImGui::Button(("+##" + std::to_string(mNode->NodeIndex)).c_str()))
            {
                gAddNewSelection.Open([this](popup::PopUpBuilder& Builder)
                {
                    float Min = *reinterpret_cast<float*>(Builder.GetDataStorage(0).mPtr);
                    float Max = *reinterpret_cast<float*>(Builder.GetDataStorage(1).mPtr);

                    for (std::pair<float, float>& Condition : mConditions)
                        if (Condition.first == Min && Condition.second == Max)
                            return;

                    mConditions.push_back({ Min, Max });
                });
            }
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::SetCursorPosX(BasePosX);
        }

        bool HasDefaultLink = false;
        for (application::file::game::ainb::AINBFile::LinkedNodeInfo& Info : mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink])
        {
            if(IsDefaultBranch(Info))
            {
                HasDefaultLink = true;
                break;
            }
        }
        DrawOutputFlowParameter("Default", HasDefaultLink, 0);

        uint32_t Index = 0;
        for(auto Iter = mConditions.begin(); Iter != mConditions.end(); )
        {
            bool Linked = false;
            for (application::file::game::ainb::AINBFile::LinkedNodeInfo& Info : mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink])
            {
                if(!IsDefaultBranch(Info) && Info.ConditionMin == Iter->first && Info.ConditionMax == Iter->second)
                {
                    Linked = true;
                    break;
                }
            }
            // The label doubles as the lookup key RenderLinks uses below, so it must always be
            // built the same way even while culled - only the "-" remove button and its
            // positioning math are cosmetic/interactive and safe to skip off-screen.
            std::string VisualStr = BuildCaseLabel(Iter->first, Iter->second);

            if (!mIsCulled)
            {
                float PosX = ImGui::GetCursorPosX();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (mNodeShapeInfo.mHeaderMax.x - mNodeShapeInfo.mHeaderMin.x) - 8 - (18 + ImGui::CalcTextSize(VisualStr.c_str()).x + ImGui::GetStyle().ItemSpacing.x + 16) - ImGui::GetStyle().ItemSpacing.x - ImGui::CalcTextSize("-").x - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.47f, 0.07f, 0.07f, 1.0f));
                bool WantRemove = ImGui::Button(("-##" + VisualStr + "_" + std::to_string(mNode->NodeIndex)).c_str());
                ImGui::PopStyleColor();
                if(WantRemove)
                {
                    float RemMin = Iter->first, RemMax = Iter->second;
                    mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].erase(
                        std::remove_if(mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].begin(), mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].end(), [RemMin, RemMax](const application::file::game::ainb::AINBFile::LinkedNodeInfo& Info)
                            {
                                return !IsDefaultBranch(Info) && Info.ConditionMin == RemMin && Info.ConditionMax == RemMax;
                            }),
                        mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].end());

                    Iter = mConditions.erase(Iter);
                    continue;
                }
                ImGui::SameLine();
                ImGui::SetCursorPosX(PosX);
            }
            DrawOutputFlowParameter(VisualStr, Linked, 1 + Index);
            Index++;
            Iter++;
        }

        DrawInternalParameterSeparator();

        for (uint8_t Type = 0; Type < application::file::game::ainb::AINBFile::ValueTypeCount; Type++)
        {
            for (uint8_t Index = 0; Index < mNode->ImmediateParameters[Type].size(); Index++)
            {
                DrawInternalParameter(Type, Index);
            }
        }

        ed::EndNode();
        ed::PopStyleVar();
    }

    void UIAINBEditorNodeF32Selector::RenderLinks(std::vector<std::unique_ptr<UIAINBEditorNodeBase>>& Nodes)
    {
        uint32_t CurrentLinkId = mNodeId + 500; //Link start at +500
        for (int i = 0; i < mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].size(); i++)
        {
            application::file::game::ainb::AINBFile::LinkedNodeInfo& Info = mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink][i];

            if (Info.NodeIndex == -1 || Info.NodeIndex >= Nodes.size())
                continue;

            int32_t StartPinId = -1;

            if(IsDefaultBranch(Info))
            {
                StartPinId = mOutputFlowParameters["Default"];
            }
            else
            {
                StartPinId = mOutputFlowParameters[BuildCaseLabel(Info.ConditionMin, Info.ConditionMax)];
            }

            ed::Link(CurrentLinkId++, StartPinId, Nodes[Info.NodeIndex]->mNodeId + 1, GetValueTypeColor(6));
            mLinks.insert({ CurrentLinkId - 1, Link {.mObjectPtr = mNode, .mType = LinkType::Flow, .mParameterIndex = (uint16_t)i } });
            Nodes[Info.NodeIndex]->mFlowLinked = true;
        }

        RenderParameterLinks(Nodes, CurrentLinkId);
    }

    bool UIAINBEditorNodeF32Selector::HasInternalParameters()
    {
        return true;
    }

    ImColor UIAINBEditorNodeF32Selector::GetNodeColor()
    {
        return ImColor(255, 128, 128);
    }

    int UIAINBEditorNodeF32Selector::GetNodeIndex()
    {
        return mNode->NodeIndex;
    }

    //Ensure that the Default link ends up last
    bool UIAINBEditorNodeF32Selector::FinalizeNode()
    {
        if(mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].size() < 2)
        {
            application::util::Logger::Error("UIAINBEditorNodeF32Selector", "The F32 Selector with index %u is missing at least one output flow link", mNode->NodeIndex);
            return false;
        }

        std::sort(mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].begin(), mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].end(), [](application::file::game::ainb::AINBFile::LinkedNodeInfo& A, application::file::game::ainb::AINBFile::LinkedNodeInfo& B)
        {
            bool ADefault = IsDefaultBranch(A);
            bool BDefault = IsDefaultBranch(B);
            if(ADefault) return false;  // a should go after b
            if(BDefault) return true;   // b should go after a

            return A.ConditionMin < B.ConditionMin;
        });

        return true;
    }

    void UIAINBEditorNodeF32Selector::PostProcessLinkedNodeInfo(Pin& StartPin, application::file::game::ainb::AINBFile::LinkedNodeInfo& Info)
    {
        if (StartPin.mParameterIndex == 0)
        {
            Info.DynamicStateName = "";
        }
        else
        {
            Info.ConditionMin = mConditions[StartPin.mParameterIndex - 1].first;
            Info.ConditionMax = mConditions[StartPin.mParameterIndex - 1].second;
        }
    }

    bool UIAINBEditorNodeF32Selector::HasFlowOutputParameters()
    {
        return true;
    }
}
