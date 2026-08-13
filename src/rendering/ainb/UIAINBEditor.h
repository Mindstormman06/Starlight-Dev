#pragma once

#include <glad/glad.h>
#include <rendering/UIWindowBase.h>
#include <manager/TextureMgr.h>
#include <string>
#include <vector>
#include <set>
#include <glm/vec4.hpp>
#include <file/game/ainb/AINBFile.h>
#include <manager/AINBNodeMgr.h>
#include <optional>
#include <functional>
#include <rendering/popup/PopUpBuilder.h>
#include "UIAINBEditorNodeBase.h"
#include "imgui_node_editor.h"

namespace ed = ax::NodeEditor;

namespace application::rendering::ainb
{
	class UIAINBEditor : public UIWindowBase
	{
		// UIAINBEditorNodeBase's draw code needs to reach mAINBFile.GlobalParameters (to resolve a
		// Blackboard entry's display name) and PushUndoSnapshot() (for Blackboard-link drops), both
		// private below - mirrors how freely this class already reaches into UIAINBEditorNodeBase's
		// own public fields (mNode/mLinks/mPins) in the other direction.
		friend class UIAINBEditorNodeBase;
	public:
		enum class NewNodeToLinkState : uint8_t
		{
			NONE = 0,
			INIT = 1,
			WAITING = 2
		};

		UIAINBEditor() = default;
		UIAINBEditor(const std::string& AINBPath);

		void LoadAINB(const std::string& AINBPath);
		void LoadAINB(const std::vector<unsigned char>& AINBBytes);

		void DrawGeneralWindow();
		void DrawNodeListWindow();
		void DrawGraphWindow();
		void DrawDetailsWindow();

		void InitializeImpl() override;
		void DrawImpl() override;
		void DeleteImpl() override;
		WindowType GetWindowType() override;

		void RefreshTheme();

		std::optional<std::function<void(application::file::game::ainb::AINBFile& File)>> mSaveCallback = std::nullopt;
		std::optional<std::function<void(application::rendering::ainb::UIAINBEditor* Editor) >> mInitCallback = std::nullopt;
		bool mEnableSaveAs = true;
		bool mEnableSaveOverwrite = true;
		bool mEnableSaveInProject = true;
		bool mEnableOpen = true;

		static bool NodeEditorSaveSettings(const char* data, size_t size, ed::SaveReasonFlags reason, void* userPointer);
		static void Initialize();

		static inline bool gEnableCullingOptimization = true;
		// Shows a per-frame timing breakdown overlay on the Graph window (Preferences > Starlight >
		// AINB > Performance Stats), for diagnosing where frame time actually goes.
		static inline bool gShowPerformanceStats = false;

		static const char* gCategoryDropdownItems[3];
		static const char* gValueTypeDropdownItems[6];
		static const char* gParamValueTypeDropdownItems[6];
		static application::gl::Texture* gHeaderTexture;

	private:
		enum class DetailsEditorContentType : uint8_t
		{
			NONE = 0,
			ENTRYPOINT = 1,
			BLACKBOARD = 2
		};

		struct DetailsEditorContentEntryPoint
		{
			application::file::game::ainb::AINBFile::Command* mCommand = nullptr;
		};

		struct DetailsEditorContentBlackBoard
		{
			application::file::game::ainb::AINBFile::GlobalEntry* mVariable = nullptr;
		};

		struct DetailsEditorContent
		{
			DetailsEditorContentType mType = DetailsEditorContentType::NONE;
			std::variant<int, DetailsEditorContentEntryPoint, DetailsEditorContentBlackBoard> mContent = 0;
		};

	public:
		// Drag payload for dragging a Local Blackboard entry onto a node's input/internal parameter
		// to link it. Addressed by (GlobalType, Index) rather than a raw GlobalEntry* so it stays
		// valid even if GlobalParameters reallocates mid-drag.
		static constexpr const char* kBlackboardDragDropId = "AINB_BLACKBOARD_ENTRY";
		struct BlackboardDragPayload
		{
			uint32_t GlobalType;
			uint32_t Index;
		};
	private:
		
		struct GraphRenderAction
		{
			std::function<void()> mFunc;
			uint32_t mFrameDelay = 0;
		};

		// Full-state snapshot for undo/redo, deep-copying every piece of state the editor can mutate.
		struct UndoSnapshot
		{
			std::vector<application::file::game::ainb::AINBFile::Node> Nodes;
			std::vector<application::file::game::ainb::AINBFile::Command> Commands;
			std::vector<application::file::game::ainb::AINBFile::EntryStringEntry> EntryStrings;
			std::vector<application::file::game::ainb::AINBFile::ChildReplace> Replacements;
			std::vector<std::vector<application::file::game::ainb::AINBFile::GlobalEntry>> GlobalParameters;
			// Parallel to Nodes - canvas position isn't part of AINBFile::Node, it lives in the node editor library.
			std::vector<ImVec2> NodePositions;
		};

		struct VisualNode
		{
			int32_t mX = 0;
			int32_t mY = 0;
			int32_t _mRelY = 0;
			int32_t mWidth = 0;
			int32_t mHeight = 0;
			int32_t _mTreeHeight = 0;
			application::file::game::ainb::AINBFile::Node* mPtr = nullptr;

			VisualNode() = default;
			explicit VisualNode(application::file::game::ainb::AINBFile::Node& Node) : mPtr(&Node) {}
			explicit VisualNode(application::file::game::ainb::AINBFile::Node* Node) : mPtr(Node) {}
		};

		struct CommandGroup 
		{
			std::string CommandName;
			VisualNode* RootNode;
			std::set<VisualNode*> Nodes;  // all nodes belonging to this command
			glm::vec4 Bounds;  // x, y, w, h
		};

		// Recomputes every node's mLinkedOutputParams (which output pins have an incoming link,
		// purely a function of graph topology) from scratch. Only needs to run when the topology
		// could have changed, not every frame - see mLinkedOutputParamsDirty.
		void RefreshLinkedOutputParams();
		bool mLinkedOutputParamsDirty = true;

		// Per-frame timing breakdown for DrawGraphWindow, shown when gShowPerformanceStats is on.
		// Smoothed average tracks steady-state cost; Peak tracks the worst frame seen in the current
		// ~1 second window (reset by PerfStats::TickPeakWindow) - averaging alone would wash out the
		// exact intermittent spikes this is meant to help find.
		struct TimedValue
		{
			float Smoothed = 0.0f;
			float Peak = 0.0f;
			void Update(float Ms)
			{
				Smoothed = Smoothed * 0.9f + Ms * 0.1f;
				if (Ms > Peak)
					Peak = Ms;
			}
		};
		struct PerfStats
		{
			TimedValue mReset;
			TimedValue mLinkRefresh;
			TimedValue mNodeDraw;
			TimedValue mRenderLinks;
			TimedValue mInteraction; // everything else between RenderLinks and ed::End (link creation, context menus, popups)
			TimedValue mEdEnd; // the vendored node-editor library's own end-of-frame cost (sort + channel splitter), not Starlight's code
			TimedValue mTotal;
			// The real wall-clock time between successive frames (ImGui's own io.DeltaTime) -
			// covers the ENTIRE frame, not just DrawGraphWindow: other panels, ImGui::Render(),
			// the GL submission, and glfwSwapBuffers' vsync wait. If this spikes while mTotal
			// doesn't, the stutter is happening outside Starlight's AINB draw code entirely -
			// most likely GPU present/vsync, not CPU work we can instrument here.
			TimedValue mFullFrame;
			int mVisibleNodeCount = 0;
			int mTotalNodeCount = 0;
			float mZoom = 1.0f; // captured while mNodeEditorContext is current - DrawPerformanceStatsOverlay() runs after ed::SetCurrentEditor(nullptr)
			float mPeakWindowElapsed = 0.0f;

			void TickPeakWindow(float DeltaTimeSeconds)
			{
				mPeakWindowElapsed += DeltaTimeSeconds;
				if (mPeakWindowElapsed < 1.0f)
					return;

				mPeakWindowElapsed = 0.0f;
				mReset.Peak = mLinkRefresh.Peak = mNodeDraw.Peak = mRenderLinks.Peak = mInteraction.Peak = mEdEnd.Peak = mTotal.Peak = mFullFrame.Peak = 0.0f;
			}
		};
		PerfStats mPerfStats;
		void DrawPerformanceStatsOverlay();

		void GraphDeselect(bool Nodes = true, bool Links = true);
		void DeleteNode(ed::NodeId NodeId, bool PushSnapshot = true);
		void DeleteNodeLink(ed::LinkId LinkId, bool PushSnapshot = true);
		// Copies a node's type/name/parameter values with all connections and identity stripped; Paste appends a fresh instance of it.
		void CopyNode(ed::NodeId NodeId);
		void PasteNode(ImVec2 Position);
		// Marks Producer as a precondition of Consumer and mirrors the link into the Generic plug array the Selector/Expression node types require.
		void RegisterParameterLink(uint32_t ProducerIndex, application::file::game::ainb::AINBFile::Node* Consumer, application::file::game::ainb::AINBFile::InputEntry& Input);
		void UnregisterParameterLink(uint32_t ProducerIndex, application::file::game::ainb::AINBFile::Node* Consumer, const std::string& ParameterName);
		// Fixes up every GlobalParametersIndex reference after deleting blackboard entry DeletedIndex of the given GlobalType.
		void FixupBlackboardDeletion(uint32_t GlobalType, uint32_t DeletedIndex);
		void AddEditorNode(application::file::game::ainb::AINBFile::Node* Node, application::manager::AINBNodeMgr::NodeDef* Def = nullptr);
		void UpdateEditorNodeIndices();

		UndoSnapshot CaptureSnapshot();
		// Call before any user-initiated mutation of Nodes/Commands/EntryStrings/Replacements/GlobalParameters.
		void PushUndoSnapshot();
		void RestoreSnapshot(UndoSnapshot Snapshot);
		void Undo();
		void Redo();

		void AutoLayoutEnforceDataLinkDirection(std::vector<VisualNode>& AllNodes);
		void AutoLayoutGreedyCollisionResolve(std::vector<VisualNode>& AllNodes);
		int32_t AutoLayoutCalcNodeWidth(VisualNode& Node);
		void AutoLayoutCalcTreeHeight(VisualNode* n, std::vector<VisualNode*>& Visited, std::vector<VisualNode>& Nodes);
		bool AutoLayoutCheckCollision(int32_t x, int32_t y, int32_t w, int32_t h, std::vector<glm::i32vec4>& Placed);
		int32_t AutoLayoutResolveCollision(int32_t x, int32_t y, int32_t w, int32_t h, std::vector<glm::i32vec4>& Placed);
		void AutoLayoutPlaceSubtree(VisualNode* n, int32_t x, int32_t y, std::vector<VisualNode*>& Placed, std::vector<glm::i32vec4>& Bounds, std::vector<VisualNode>& Nodes);
		void AutoLayout();
		void AutoLayoutCollectCommandNodes(VisualNode* Root, std::set<VisualNode*>& OutNodes, std::set<VisualNode*>& Visited, std::vector<VisualNode>& AllNodes);
		void AutoLayoutResolveGroupCollisions(std::vector<CommandGroup>& Groups);
		void ExportNodeLayout(const std::string& Path);
		void LoadNodeLayout(const std::string& Path);

		std::string GetWindowTitle();

		application::file::game::ainb::AINBFile mAINBFile;
		std::string mAINBPath = "";

		DetailsEditorContent mDetailsEditorContent;
		
		bool mNodeListOpen = true;
		bool mGeneralOpen = true;
		bool mGraphOpen = true;
		bool mDetailsOpen = true;

		bool mWantAutoLayout = false;
		bool mWantExportLayout = false;
		bool mWantLoadLayout = false;
		std::string mLoadLayoutPath = "";
		std::string mExportLayoutPath = "";

		ed::EditorContext* mNodeEditorContext = nullptr;
		int mNodeEditorUniqueId = 1;

		std::vector<std::unique_ptr<UIAINBEditorNodeBase>> mNodes;
		std::vector<GraphRenderAction> mGraphRenderActions;
		std::vector<std::function<void()>> mRenderEndActions;

		std::vector<UndoSnapshot> mUndoStack;
		std::vector<UndoSnapshot> mRedoStack;
		static const size_t mMaxUndoHistory = 100;

		application::file::game::ainb::AINBFile::Node mNodeClipboard;
		bool mHasNodeClipboard = false;

		ed::NodeId mContextNodeId;
		ed::LinkId mContextLinkId;

		std::string mNodeListSearchBar;

		std::string mAddNodeSearchBar;
		bool mFocusAddNodeSearchBar = false;
		bool mShowAllNodes = false;
		std::unordered_map<std::string, int> mCategoriesOriginalState;

		ImVec2 mOpenPopupPosition;
		UIAINBEditorNodeBase::Pin mNewNodeLinkPin;
		NewNodeToLinkState mNewNodeLinkState = NewNodeToLinkState::NONE;
		int32_t mNewNodeStartIndex = -1;

		static application::rendering::popup::PopUpBuilder gCreateNewNodePopUp;
	};
}