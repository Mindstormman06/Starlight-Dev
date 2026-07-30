#include "UIAINBEditorNodeStringSelector.h"

#include <util/Logger.h>
#include <algorithm>
#include "imgui_stdlib.h"

namespace application::rendering::ainb::nodes
{
    application::rendering::popup::PopUpBuilder UIAINBEditorNodeStringSelector::gAddNewSelection;

    // A branch is the Default/fallback iff DynamicStateName was actually read from the file,
    // which only happens for the last (IsEnd) child - see AINBFile.cpp's Element_StringSelector
    // read/write logic. Every other branch leaves DynamicStateName at its "MapEditor_AINB_NoVal"
    // default and uses Condition instead - which can legitimately be "", so it can't be used to
    // detect the default branch the way S32Selector's literal "Default" sentinel does.
    static bool IsDefaultBranch(const application::file::game::ainb::AINBFile::LinkedNodeInfo& Info)
    {
        return Info.DynamicStateName != "MapEditor_AINB_NoVal";
    }

    static std::string BuildCaseLabel(const std::string& Value)
    {
        return "= \"" + Value + "\"";
    }

    void UIAINBEditorNodeStringSelector::Initialize()
    {
        gAddNewSelection.Title("Add String Case").Flag(ImGuiWindowFlags_NoResize).NeedsConfirmation(false).AddDataStorageInstanced<std::string>([](void* Ptr) { *reinterpret_cast<std::string*>(Ptr) = ""; }).ContentDrawingFunction([](popup::PopUpBuilder& Builder)
        {
            if (ImGui::BeginTable("StringTable", 2, ImGuiTableFlags_BordersInnerV))
			{
				ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("String Case Value").x);

				ImGui::TableNextRow();

				ImGui::TableSetColumnIndex(0);
				ImGui::Text("String Case Value");
				ImGui::TableNextColumn();

				ImGui::PushItemWidth(ImGui::GetCurrentTable()->Columns[1].WidthMax);
                ImGui::InputText("##StringCaseValue", reinterpret_cast<std::string*>(Builder.GetDataStorage(0).mPtr));
                ImGui::PopItemWidth();

                ImGui::EndTable();
            }
        }).Register();
    }

    UIAINBEditorNodeStringSelector::UIAINBEditorNodeStringSelector(int UniqueId, application::file::game::ainb::AINBFile::Node& Node) : UIAINBEditorNodeBase(UniqueId, Node)
    {
        for (application::file::game::ainb::AINBFile::LinkedNodeInfo& Info : mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink])
        {
            if(!IsDefaultBranch(Info))
            {
                mConditions.push_back(Info.Condition);
            }
        }
    }

    void UIAINBEditorNodeStringSelector::DrawImpl()
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
                    std::string Value = *reinterpret_cast<std::string*>(Builder.GetDataStorage(0).mPtr);

                    if(std::find(mConditions.begin(), mConditions.end(), Value) != mConditions.end())
                        return;

                    mConditions.push_back(Value);
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
                if(!IsDefaultBranch(Info) && Info.Condition == *Iter)
                {
                    Linked = true;
                    break;
                }
            }
            // The label doubles as the lookup key RenderLinks uses below, so it must always be
            // built the same way even while culled - only the "-" remove button and its
            // positioning math are cosmetic/interactive and safe to skip off-screen.
            std::string VisualStr = BuildCaseLabel(*Iter);

            if (!mIsCulled)
            {
                float PosX = ImGui::GetCursorPosX();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (mNodeShapeInfo.mHeaderMax.x - mNodeShapeInfo.mHeaderMin.x) - 8 - (18 + ImGui::CalcTextSize(VisualStr.c_str()).x + ImGui::GetStyle().ItemSpacing.x + 16) - ImGui::GetStyle().ItemSpacing.x - ImGui::CalcTextSize("-").x - ImGui::GetStyle().ItemInnerSpacing.x * 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.47f, 0.07f, 0.07f, 1.0f));
                bool WantRemove = ImGui::Button(("-##" + VisualStr + "_" + std::to_string(mNode->NodeIndex)).c_str());
                ImGui::PopStyleColor();
                if(WantRemove)
                {
                    std::string RemValue = *Iter;
                    mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].erase(
                        std::remove_if(mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].begin(), mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].end(), [RemValue](const application::file::game::ainb::AINBFile::LinkedNodeInfo& Info)
                            {
                                return !IsDefaultBranch(Info) && Info.Condition == RemValue;
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

    void UIAINBEditorNodeStringSelector::RenderLinks(std::vector<std::unique_ptr<UIAINBEditorNodeBase>>& Nodes)
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
                StartPinId = mOutputFlowParameters[BuildCaseLabel(Info.Condition)];
            }

            ed::Link(CurrentLinkId++, StartPinId, Nodes[Info.NodeIndex]->mNodeId + 1, GetValueTypeColor(6));
            mLinks.insert({ CurrentLinkId - 1, Link {.mObjectPtr = mNode, .mType = LinkType::Flow, .mParameterIndex = (uint16_t)i } });
            Nodes[Info.NodeIndex]->mFlowLinked = true;
        }

        RenderParameterLinks(Nodes, CurrentLinkId);
    }

    bool UIAINBEditorNodeStringSelector::HasInternalParameters()
    {
        return true;
    }

    ImColor UIAINBEditorNodeStringSelector::GetNodeColor()
    {
        return ImColor(255, 128, 128);
    }

    int UIAINBEditorNodeStringSelector::GetNodeIndex()
    {
        return mNode->NodeIndex;
    }

    //Ensure that the Default link ends up last
    bool UIAINBEditorNodeStringSelector::FinalizeNode()
    {
        if(mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].size() < 2)
        {
            application::util::Logger::Error("UIAINBEditorNodeStringSelector", "The String Selector with index %u is missing at least one output flow link", mNode->NodeIndex);
            return false;
        }

        std::sort(mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].begin(), mNode->LinkedNodes[(int)application::file::game::ainb::AINBFile::LinkedNodeMapping::StandardLink].end(), [](application::file::game::ainb::AINBFile::LinkedNodeInfo& A, application::file::game::ainb::AINBFile::LinkedNodeInfo& B)
        {
            bool ADefault = IsDefaultBranch(A);
            bool BDefault = IsDefaultBranch(B);
            if(ADefault) return false;  // a should go after b
            if(BDefault) return true;   // b should go after a

            return A.Condition < B.Condition;
        });

        return true;
    }

    void UIAINBEditorNodeStringSelector::PostProcessLinkedNodeInfo(Pin& StartPin, application::file::game::ainb::AINBFile::LinkedNodeInfo& Info)
    {
        if (StartPin.mParameterIndex == 0)
        {
            Info.DynamicStateName = "";
        }
        else
        {
            Info.Condition = mConditions[StartPin.mParameterIndex - 1];
        }
    }

    bool UIAINBEditorNodeStringSelector::HasFlowOutputParameters()
    {
        return true;
    }
}
