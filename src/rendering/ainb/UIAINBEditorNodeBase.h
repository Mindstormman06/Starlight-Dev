#pragma once

#include "imgui_node_editor.h"
#include "imgui_internal.h"
#include <file/game/ainb/AINBFile.h>
#include <string>
#include <unordered_map>
#include <optional>

namespace ed = ax::NodeEditor;

namespace application::rendering::ainb
{
	class UIAINBEditor;

	class UIAINBEditorNodeBase
	{
	public:
		struct NodeShapeInfo
		{
			ImVec2 mHeaderMin;
			ImVec2 mHeaderMax;
		};

		enum class PinType : uint8_t
		{
			Int = 0,
			Bool = 1,
			Float = 2,
			String = 3,
			Vec3f = 4,
			UserDefined = 5,
			Flow = 6
		};

		struct Pin
		{
			ed::PinKind mKind;
			PinType mType;
			application::file::game::ainb::AINBFile::Node* mNode;
			void* mObjectPtr = nullptr;
			int32_t mParameterIndex = -1;
			bool mAllowMultipleLinks = true;
			bool mAlreadyLinked = false;
		};

		enum class LinkType
		{
			Parameter,
			Flow
		};

		struct Link
		{
			void* mObjectPtr;
			LinkType mType;
			uint16_t mNodeIndex;
			uint16_t mParameterIndex;
			bool mCrossCategory = false; // source output was only found by searching outside the input's own value-type category
		};

		// Result of looking up the editor pin id for a node's output parameter reference.
		struct ResolvedOutputPin
		{
			bool Valid = false;
			bool CrossCategory = false; // pin was only found by searching outside the requested category
			uint8_t Category = 0; // the value-type category that actually contains this output on the target node
			uint32_t PinId = 0;
		};

		UIAINBEditorNodeBase(int UniqueId, application::file::game::ainb::AINBFile::Node& Node);
		virtual ~UIAINBEditorNodeBase() = default;
		
		void Reset();

		void DrawNodeHeader(const std::string& Title, application::file::game::ainb::AINBFile::Node* Node = nullptr);
		static std::string GetValueTypeName(application::file::game::ainb::AINBFile::ValueType ValueType);
		static std::string GetValueTypeName(uint8_t ValueType);
        static ImColor GetValueTypeColor(application::file::game::ainb::AINBFile::ValueType ValueType);
        ImColor GetPinTypeColor(PinType Type);
        static ImColor GetValueTypeColor(uint8_t ValueType);
        UIAINBEditorNodeBase::PinType ValueTypeToPinType(application::file::game::ainb::AINBFile::ValueType Type);
        UIAINBEditorNodeBase::PinType ValueTypeToPinType(uint8_t Type);
		void DrawPinIcon(bool Connected, uint32_t Alpha, PinType Type);
		ImRect DrawPin(uint32_t Id, bool Connected, uint32_t Alpha, PinType Type, ed::PinKind Kind, const std::string& Name, bool IsHeaderPin = false);
		void DrawParameterValue(application::file::game::ainb::AINBFile::ValueType Type, const std::string& Name, uint32_t Id, void* Dest);
		void Draw();
		void DrawInternalParameterSeparator();

		void DrawInputParameter(uint8_t Type, uint8_t Index);
		void DrawInternalParameter(uint8_t Type, uint8_t Index);
		void DrawOutputParameter(uint8_t Type, uint8_t Index);
		void DrawOutputFlowParameter(const std::string& Text, bool Linked, uint8_t Index, bool AllowMultipleLinks = false);

		virtual void DrawImpl() = 0;
		virtual ImColor GetNodeColor() = 0;
		virtual int GetNodeIndex();
		virtual bool HasInternalParameters();
		virtual void RenderLinks(std::vector<std::unique_ptr<UIAINBEditorNodeBase>>& Nodes) = 0;
		virtual void PostProcessLinkedNodeInfo(Pin& StartPin, application::file::game::ainb::AINBFile::LinkedNodeInfo& Info) {}
		virtual bool FinalizeNode() { return true; }
		virtual bool HasFlowOutputParameters() = 0;

		// Resolves the editor pin id for NodeIndex's output at ParameterIndex. AINB output
		// parameters are grouped into 6 value-type categories, but a node's real output for
		// a given index isn't always in the category the consuming input happens to be
		// declared under (e.g. QueryMathVector3fIsZero's boolean result is natively stored
		// under Int, not Bool, and the game runs graphs that wire it straight into Bool
		// inputs without complaint) - Category is only a starting guess. This checks it
		// first, then searches the node's other categories for the same index, so a
		// same-node reference is always found regardless of which category holds it.
		// Returns Valid = false only if NodeIndex is out of range or truly no category has
		// an entry at ParameterIndex at all.
		static ResolvedOutputPin ResolveLinkedOutputPin(std::vector<std::unique_ptr<UIAINBEditorNodeBase>>& Nodes, int NodeIndex, uint8_t Category, int ParameterIndex);

		// Returns true if this node's last-known bounds (from the previous frame) fall entirely
		// outside the currently visible viewport. Nodes that have never been laid out yet (size
		// still zero) are always reported as not culled so their first layout pass can run.
		bool ComputeCulled();

		// Shared implementation of the per-value-type Input/Multi parameter link rendering
		// used by every node type that has value parameters (Default, BoolSelector,
		// S32Selector, F32Selector). CurrentLinkId is threaded through by reference since
		// callers may have already consumed ids for their own flow links.
		void RenderParameterLinks(std::vector<std::unique_ptr<UIAINBEditorNodeBase>>& Nodes, uint32_t& CurrentLinkId);

		int mUniqueId = 0;
		int mNodeId;
		bool mEnableFlow = false;
		bool mFlowLinked = false;
		bool mIsEntryPoint = false;
		// Set at the start of every Draw() call, based on the node's last-known bounds vs. the
		// current viewport. Node subtypes with their own inline widgets (e.g. the +/- case buttons
		// on the selector/sequential nodes) should skip that extra work while this is true - the
		// node is off-screen, but its pins must still be submitted every frame (see DrawInputParameter/
		// DrawOutputParameter/DrawOutputFlowParameter) so links to/from it keep rendering.
		bool mIsCulled = false;
		NodeShapeInfo mNodeShapeInfo;
		std::vector<std::vector<uint32_t>> mOutputParameters; //6 value types -> vector of output pin ids
		std::unordered_map<std::string, uint32_t> mOutputFlowParameters; //name -> pin id
		std::vector<std::vector<uint32_t>> mInputParameters; // 6 value types -> vector of input pin ids
		std::vector<std::vector<uint32_t>> mLinkedOutputParams; // 6 value types -> vector of output param indexes
		std::unordered_map<uint32_t, Pin> mPins;
		std::unordered_map<uint32_t, Link> mLinks;
		application::file::game::ainb::AINBFile::Node* mNode;
		// Set once by UIAINBEditor::AddEditorNode right after construction - lets draw code reach
		// the owning editor's AINBFile (e.g. to resolve a Blackboard entry's name) and undo stack.
		UIAINBEditor* mOwner = nullptr;
	private:
		ImVec2 mInternalParameterStartPos;

		// Result of a Blackboard entry dropped onto a parameter widget - mirrors
		// UIAINBEditor::BlackboardDragPayload's shape without needing that type's full definition here.
		struct BlackboardLinkTarget
		{
			uint32_t GlobalType;
			uint32_t Index;
		};

		struct BlackboardChipAction
		{
			bool Unlink = false;
			std::optional<BlackboardLinkTarget> Relink; // set if a different Blackboard entry was dropped on the chip
		};

		// Draws a read-only "linked to Blackboard entry" chip (icon + Name, colored by Type), an
		// unlink button, and accepts drops of a different matching-type entry to swap the link.
		BlackboardChipAction DrawBlackboardLinkChip(uint8_t Type, const std::string& Name, uint32_t Id);
		// Makes the last-submitted ImGui item a drop target for a Blackboard palette entry. Returns
		// the dropped entry's (GlobalType, Index) only if its value type matches Type; a
		// mismatched-type drop is silently ignored.
		std::optional<BlackboardLinkTarget> AcceptBlackboardDrop(uint8_t Type);

		// A parameter's "Name (Type/Class)" pin label and its measured width never change after
		// the node is constructed (nothing in the editor renames/retypes an existing InputEntry/
		// OutputEntry), so rebuilding the string and re-measuring it every visible frame is pure
		// waste - built lazily on first draw instead (needs a live ImGui font, so can't happen in
		// the constructor) and kept for the node's lifetime.
		struct ParamLabelCache
		{
			bool Built = false;
			std::string Label;
			float Width = 0.0f;
		};
		std::vector<std::vector<ParamLabelCache>> mInputLabelCache; // [Type][Index], sized in the constructor
		std::vector<std::vector<ParamLabelCache>> mOutputLabelCache; // [Type][Index], sized in the constructor
		const ParamLabelCache& GetCachedParamLabel(std::vector<std::vector<ParamLabelCache>>& Cache, uint8_t Type, uint8_t Index, const std::string& Name, const std::string& Class);
	};
}