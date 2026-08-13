#pragma once

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <memory>
#include <vector>
#include <rendering/UIWindowBase.h>
#include <rendering/popup/PopUpBuilder.h>
#include <glm/vec4.hpp>
#include "imgui.h"

namespace application::manager
{
	namespace UIMgr
	{
		extern GLFWwindow* gWindow;
		extern ImVec4 gClearColor;
		extern glm::vec4 gViewportClearColor;
		extern std::vector<std::unique_ptr<application::rendering::UIWindowBase>> gWindows;
		extern std::vector<std::unique_ptr<application::rendering::UIWindowBase>> gWaitingWindows;
		extern unsigned int gWindowId;
		extern bool gFirstFrame;
		extern bool gBlockProjectSwitch;
		extern bool gASTCSupported;
		extern int gThemeIndex;
		extern int gBackgroundThemeIndex;
		extern bool gVSyncEnabled;
		extern ImFont* gLogoFont;
		extern ImFont* gHeadingFont;
		extern ImFont* gBigIconFont;
		extern ImFont* gButtonLabelFont;
		extern std::vector<application::rendering::UIWindowBase::WindowType> gRecentTools;

		extern ImGuiID gDockMain;
		extern ImGuiID gDockBottom;

		extern application::rendering::popup::PopUpBuilder gSettingsPopUp;
		extern application::rendering::popup::PopUpBuilder gAddProjectPopUp;
		extern application::rendering::popup::PopUpBuilder gExportProjectPopUp;
		extern application::rendering::popup::PopUpBuilder gEnterLicenseKeyPopUp;
		extern application::rendering::popup::PopUpBuilder gGenerateLicenseKeyPopUp;

		void GLFWErrorCallback(int error, const char* description);
		bool Initialize();
		void Render();
		void Cleanup();
		bool ShouldWindowClose();

		void ApplyTheme(int Index);
		int GetThemeCount();
		const char* GetThemeName(int Index);
		ImVec4 GetAccentColor();

		void ApplyBackgroundTheme(int Index);
		int GetBackgroundThemeCount();
		const char* GetBackgroundThemeName(int Index);
		ImVec4 GetNodeEditorBgColor();
		ImVec4 GetNodeEditorGridColor();

		// Applies gVSyncEnabled to the current GL context (glfwSwapInterval). Call after changing
		// gVSyncEnabled, and once after loading it from PathConfigFile since the window/context
		// already exists by then (same pattern as ApplyTheme/ApplyBackgroundTheme).
		void ApplyVSync();

		std::unique_ptr<application::rendering::UIWindowBase>& OpenWindow(std::unique_ptr<application::rendering::UIWindowBase> Window);
		void UpdateWaitingWindows();
	}
}