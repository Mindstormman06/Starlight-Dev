#pragma once

#include <rendering/ainb/UIAINBEditorNodeBase.h>
#include <rendering/popup/PopUpBuilder.h>
#include <vector>
#include <string>

namespace application::rendering::ainb::nodes
{
	class UIAINBEditorNodeStringSelector : public application::rendering::ainb::UIAINBEditorNodeBase
	{
	public:
		UIAINBEditorNodeStringSelector(int UniqueId, application::file::game::ainb::AINBFile::Node& Node);

		virtual void DrawImpl() override;
		virtual ImColor GetNodeColor() override;
		virtual int GetNodeIndex() override;
		virtual bool HasInternalParameters() override;
		virtual void RenderLinks(std::vector<std::unique_ptr<UIAINBEditorNodeBase>>& Nodes) override;
        virtual bool FinalizeNode() override;
        virtual void PostProcessLinkedNodeInfo(Pin& StartPin, application::file::game::ainb::AINBFile::LinkedNodeInfo& Info) override;
        virtual bool HasFlowOutputParameters() override;

        static void Initialize();

    private:
        // A case value can legitimately be an empty string - see the constructor/IsDefaultBranch
        // for why default-detection can't be based on the string value itself.
        std::vector<std::string> mConditions;

        static application::rendering::popup::PopUpBuilder gAddNewSelection;
	};
}
