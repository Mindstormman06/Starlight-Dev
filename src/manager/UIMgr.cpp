#include "UIMgr.h"

#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include <rendering/ImGuizmo.h>
#include <cassert>
#include <cfloat>
#include <algorithm>
#include <util/Logger.h>
#include <manager/PopUpMgr.h>
#include <manager/TextureMgr.h>
#include <manager/BfresRendererMgr.h>
#include <manager/FramebufferMgr.h>
#include <manager/ShaderMgr.h>
#include <manager/ProjectMgr.h>
#include <util/FileUtil.h>
#include <util/Math.h>
#include <rendering/ainb/UIAINBEditor.h>
#include <rendering/collision/UICollisionGenerator.h>
#include <rendering/mapeditor/UIMapEditor.h>
#include <rendering/actor/UIActorTool.h>
#include <rendering/plugin/UIPlugins.h>
#include <Editor.h>
#include <file/tool/PathConfigFile.h>
#include <file/tool/LicenseFile.h>
#include <util/IconsFontAwesome6.h>
#include <util/fa-solid-900.h>
#include <util/ImGuiNotify.h>
#include <util/portable-file-dialogs.h>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include <dwmapi.h>
#pragma comment(lib, "dwmapi.lib")
#endif

#define TOOL_IMGUI_VIEWPORTS_ENABLED 0
#define TOOL_GL_DEBUG 0

namespace application::manager
{
    namespace
    {
#if TOOL_GL_DEBUG == 1
        bool gUseDebugCallback = false;

        const char* GLErrorToString(GLenum Error)
        {
            switch (Error)
            {
            case GL_INVALID_ENUM: return "GL_INVALID_ENUM";
            case GL_INVALID_VALUE: return "GL_INVALID_VALUE";
            case GL_INVALID_OPERATION: return "GL_INVALID_OPERATION";
            case GL_STACK_OVERFLOW: return "GL_STACK_OVERFLOW";
            case GL_STACK_UNDERFLOW: return "GL_STACK_UNDERFLOW";
            case GL_OUT_OF_MEMORY: return "GL_OUT_OF_MEMORY";
            case GL_INVALID_FRAMEBUFFER_OPERATION: return "GL_INVALID_FRAMEBUFFER_OPERATION";
            default: return "GL_UNKNOWN_ERROR";
            }
        }

        void PollGLErrors(const char* Stage)
        {
            GLenum Error = glGetError();
            while (Error != GL_NO_ERROR)
            {
                application::util::Logger::Error("OpenGL", "%s: %s (0x%X)", Stage, GLErrorToString(Error), Error);
                Error = glGetError();
            }
        }
#endif

        struct ThemePreset
        {
            const char* Name;
            ImVec4 Hover;
            ImVec4 Active;
        };

        const ThemePreset gThemePresets[] = {
            { "Starlight Blue", ImVec4(0.114f, 0.592f, 0.925f, 1.0f), ImVec4(0.0f, 0.467f, 0.784f, 1.0f) },
            { "Violet",         ImVec4(0.580f, 0.420f, 0.930f, 1.0f), ImVec4(0.400f, 0.220f, 0.780f, 1.0f) },
            { "Emerald",        ImVec4(0.204f, 0.780f, 0.349f, 1.0f), ImVec4(0.106f, 0.549f, 0.220f, 1.0f) },
            { "Amber",          ImVec4(0.949f, 0.600f, 0.106f, 1.0f), ImVec4(0.800f, 0.451f, 0.0f, 1.0f) },
            { "Rose",           ImVec4(0.929f, 0.325f, 0.427f, 1.0f), ImVec4(0.780f, 0.157f, 0.278f, 1.0f) },
        };

        constexpr int gThemePresetCount = sizeof(gThemePresets) / sizeof(gThemePresets[0]);

        struct BackgroundThemePreset
        {
            const char* Name;
            ImVec4 Text;
            ImVec4 TextDisabled;
            ImVec4 BaseBg;          // WindowBg/ChildBg/PopupBg/TitleBg*/Tab/TabUnfocused/NavHighlight/DragDropTarget
            ImVec4 SurfaceBg;       // FrameBg/MenuBarBg/ScrollbarBg/Button/Header
            ImVec4 BorderColor;     // Border/BorderShadow/Separator*
            ImVec4 GrabColor;       // ScrollbarGrab/ResizeGripActive
            ImVec4 GrabHoverColor;  // ScrollbarGrabHovered/ScrollbarGrabActive
            ImVec4 TableHeaderBg;
            ImVec4 TableBorderStrong;
            ImVec4 TableBorderLight;
            ImVec4 TableRowBgAlt;
            ImVec4 NavWindowingDimBg;
            glm::vec4 ViewportClearColor;
            ImVec4 NodeEditorBg;
            ImVec4 NodeEditorGrid;
        };

        const BackgroundThemePreset gBackgroundThemePresets[] = {
            // Light
            { "Light",
                ImVec4(0.09f, 0.09f, 0.10f, 1.0f), ImVec4(0.50f, 0.50f, 0.52f, 1.0f),
                ImVec4(0.94f, 0.94f, 0.95f, 1.0f), ImVec4(0.86f, 0.86f, 0.88f, 1.0f),
                ImVec4(0.75f, 0.75f, 0.77f, 1.0f), ImVec4(0.70f, 0.70f, 0.73f, 1.0f), ImVec4(0.60f, 0.60f, 0.64f, 1.0f),
                ImVec4(0.82f, 0.82f, 0.85f, 1.0f), ImVec4(0.65f, 0.65f, 0.68f, 1.0f), ImVec4(0.78f, 0.78f, 0.81f, 1.0f),
                ImVec4(0.0f, 0.0f, 0.0f, 0.04f), ImVec4(0.2f, 0.2f, 0.2f, 0.2f),
                glm::vec4(0.62f, 0.64f, 0.67f, 1.0f),
                ImVec4(0.86f, 0.86f, 0.88f, 0.94f), ImVec4(0.55f, 0.55f, 0.58f, 0.35f)
            },
            // Slate
            { "Slate",
                ImVec4(0.95f, 0.96f, 0.98f, 1.0f), ImVec4(0.55f, 0.58f, 0.62f, 1.0f),
                ImVec4(0.180f, 0.196f, 0.216f, 1.0f), ImVec4(0.235f, 0.255f, 0.278f, 1.0f),
                ImVec4(0.35f, 0.38f, 0.42f, 1.0f), ImVec4(0.35f, 0.38f, 0.42f, 1.0f), ImVec4(0.42f, 0.45f, 0.50f, 1.0f),
                ImVec4(0.22f, 0.24f, 0.27f, 1.0f), ImVec4(0.38f, 0.41f, 0.46f, 1.0f), ImVec4(0.28f, 0.30f, 0.34f, 1.0f),
                ImVec4(1.0f, 1.0f, 1.0f, 0.05f), ImVec4(0.8f, 0.8f, 0.85f, 0.2f),
                glm::vec4(0.09f, 0.10f, 0.12f, 1.0f),
                ImVec4(0.14f, 0.155f, 0.175f, 0.90f), ImVec4(0.45f, 0.48f, 0.52f, 0.15f)
            },
            // Gunmetal (default)
            { "Gunmetal",
                ImVec4(1.0f, 1.0f, 1.0f, 1.0f), ImVec4(0.592f, 0.592f, 0.592f, 1.0f),
                ImVec4(0.145f, 0.145f, 0.149f, 1.0f), ImVec4(0.2f, 0.2f, 0.216f, 1.0f),
                ImVec4(0.306f, 0.306f, 0.306f, 1.0f), ImVec4(0.322f, 0.322f, 0.333f, 1.0f), ImVec4(0.353f, 0.353f, 0.373f, 1.0f),
                ImVec4(0.188f, 0.188f, 0.2f, 1.0f), ImVec4(0.31f, 0.31f, 0.349f, 1.0f), ImVec4(0.227f, 0.227f, 0.247f, 1.0f),
                ImVec4(1.0f, 1.0f, 1.0f, 0.06f), ImVec4(0.8f, 0.8f, 0.8f, 0.2f),
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                ImVec4(0.235f, 0.235f, 0.275f, 0.784f), ImVec4(0.471f, 0.471f, 0.471f, 0.157f)
            },
            // Midnight
            { "Midnight",
                ImVec4(0.92f, 0.92f, 0.94f, 1.0f), ImVec4(0.45f, 0.45f, 0.48f, 1.0f),
                ImVec4(0.055f, 0.055f, 0.065f, 1.0f), ImVec4(0.09f, 0.09f, 0.10f, 1.0f),
                ImVec4(0.18f, 0.18f, 0.20f, 1.0f), ImVec4(0.16f, 0.16f, 0.18f, 1.0f), ImVec4(0.22f, 0.22f, 0.25f, 1.0f),
                ImVec4(0.08f, 0.08f, 0.09f, 1.0f), ImVec4(0.18f, 0.18f, 0.21f, 1.0f), ImVec4(0.13f, 0.13f, 0.15f, 1.0f),
                ImVec4(1.0f, 1.0f, 1.0f, 0.04f), ImVec4(0.5f, 0.5f, 0.55f, 0.2f),
                glm::vec4(0.0f, 0.0f, 0.0f, 1.0f),
                ImVec4(0.02f, 0.02f, 0.025f, 0.95f), ImVec4(0.30f, 0.30f, 0.32f, 0.12f)
            },
        };

        constexpr int gBackgroundThemePresetCount = sizeof(gBackgroundThemePresets) / sizeof(gBackgroundThemePresets[0]);
        constexpr int gDefaultBackgroundThemeIndex = 2; // Gunmetal

        const application::rendering::UIWindowBase::WindowType kDefaultToolOrder[] = {
            application::rendering::UIWindowBase::WindowType::EDITOR_MAP,
            application::rendering::UIWindowBase::WindowType::EDITOR_AINB,
            application::rendering::UIWindowBase::WindowType::EDITOR_ACTOR,
            application::rendering::UIWindowBase::WindowType::EDITOR_COLLISION,
            application::rendering::UIWindowBase::WindowType::EDITOR_PLUGINS,
        };

        const char* GetToolDisplayName(application::rendering::UIWindowBase::WindowType Type)
        {
            switch (Type)
            {
            case application::rendering::UIWindowBase::WindowType::EDITOR_MAP: return "Map Editor";
            case application::rendering::UIWindowBase::WindowType::EDITOR_AINB: return "AINB Editor";
            case application::rendering::UIWindowBase::WindowType::EDITOR_ACTOR: return "Actor Editor";
            case application::rendering::UIWindowBase::WindowType::EDITOR_COLLISION: return "Collision Generator";
            case application::rendering::UIWindowBase::WindowType::EDITOR_PLUGINS: return "Plugins";
            default: return "Unknown";
            }
        }

        const char* GetToolIcon(application::rendering::UIWindowBase::WindowType Type)
        {
            switch (Type)
            {
            case application::rendering::UIWindowBase::WindowType::EDITOR_MAP: return ICON_FA_MAP;
            case application::rendering::UIWindowBase::WindowType::EDITOR_AINB: return ICON_FA_DIAGRAM_PROJECT;
            case application::rendering::UIWindowBase::WindowType::EDITOR_ACTOR: return ICON_FA_PERSON;
            case application::rendering::UIWindowBase::WindowType::EDITOR_COLLISION: return ICON_FA_SHAPES;
            case application::rendering::UIWindowBase::WindowType::EDITOR_PLUGINS: return ICON_FA_PLUG;
            default: return ICON_FA_PUZZLE_PIECE;
            }
        }

        void LaunchTool(application::rendering::UIWindowBase::WindowType Type)
        {
            switch (Type)
            {
            case application::rendering::UIWindowBase::WindowType::EDITOR_MAP:
                UIMgr::OpenWindow(std::make_unique<application::rendering::map_editor::UIMapEditor>());
                break;
            case application::rendering::UIWindowBase::WindowType::EDITOR_AINB:
                UIMgr::OpenWindow(std::make_unique<application::rendering::ainb::UIAINBEditor>());
                break;
            case application::rendering::UIWindowBase::WindowType::EDITOR_ACTOR:
                UIMgr::OpenWindow(std::make_unique<application::rendering::actor::UIActorTool>());
                break;
            case application::rendering::UIWindowBase::WindowType::EDITOR_COLLISION:
                UIMgr::OpenWindow(std::make_unique<application::rendering::collision::UICollisionGenerator>());
                break;
            case application::rendering::UIWindowBase::WindowType::EDITOR_PLUGINS:
                UIMgr::OpenWindow(std::make_unique<application::rendering::plugin::UIPlugins>());
                break;
            default:
                break;
            }
        }

        void OpenPreferencesPopUp()
        {
            UIMgr::gSettingsPopUp.Open([](application::rendering::popup::PopUpBuilder& Builder)
                {
                    application::util::FileUtil::ValidatePaths();
                    application::Editor::InitializeRomFSPathDependant();

                    application::file::tool::PathConfigFile::Save(application::util::FileUtil::GetWorkingDirFilePath("Config.epathcfg"));
                });
        }

        // Home tab fonts are baked at kFontOversample times their intended on-screen size so that
        // scaling them up to fill a large/fullscreen window (via explicit font_size in AddText,
        // never via SetWindowFontScale) stays a downscale of the baked bitmap, not a magnification
        // of it - that's what actually causes ImGui bitmap-font blur when a window grows.
        constexpr float kFontOversample = 2.2f;
        constexpr float kLogoTargetSize = 56.0f;
        constexpr float kHeadingTargetSize = 24.0f;
        constexpr float kIconTargetSize = 32.0f;
        constexpr float kLabelTargetSize = 20.0f;

        bool gHomeWindowFirstFrame = true;

        // Custom button: draws a themed background rect plus an oversized icon (in a fixed-width slot
        // so icons line up across buttons) and an accent-colored label with room before the right edge.
        // Font sizes are requested explicitly (never via SetWindowFontScale) so they only ever downscale
        // from the oversampled bake - see kFontOversample above for why that matters. DpiScale here is
        // the monitor's DPI factor, NOT the Home tab's window-width Scale - buttons deliberately don't
        // grow with the big logo/heading text, only with actual display DPI.
        bool DrawToolButton(const char* Icon, const std::string& Label, const ImVec2& Size, float DpiScale)
        {
            ImGuiWindow* Window = ImGui::GetCurrentWindow();
            ImGuiID Id = Window->GetID(Label.c_str());
            ImVec2 Pos = ImGui::GetCursorScreenPos();
            ImRect Bb(Pos, ImVec2(Pos.x + Size.x, Pos.y + Size.y));

            ImGui::ItemSize(Size);
            if (!ImGui::ItemAdd(Bb, Id))
                return false;

            bool Hovered, Held;
            bool Pressed = ImGui::ButtonBehavior(Bb, Id, &Hovered, &Held);

            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImU32 BgColor = ImGui::GetColorU32(Held ? ImGuiCol_ButtonActive : (Hovered ? ImGuiCol_ButtonHovered : ImGuiCol_Button));
            DrawList->AddRectFilled(Bb.Min, Bb.Max, BgColor, ImGui::GetStyle().FrameRounding);

            const float IconSlotWidth = Size.x * 0.22f;

            ImFont* IconFont = UIMgr::gBigIconFont ? UIMgr::gBigIconFont : ImGui::GetFont();
            float IconRenderSize = kIconTargetSize * DpiScale;
            ImVec2 IconSize = IconFont->CalcTextSizeA(IconRenderSize, FLT_MAX, 0.0f, Icon);
            ImVec2 IconPos(Bb.Min.x + (IconSlotWidth - IconSize.x) * 0.5f, Bb.Min.y + (Size.y - IconSize.y) * 0.5f);
            DrawList->AddText(IconFont, IconRenderSize, IconPos, ImGui::GetColorU32(ImGuiCol_Text), Icon);

            ImFont* LabelFont = UIMgr::gButtonLabelFont ? UIMgr::gButtonLabelFont : ImGui::GetFont();
            float LabelRenderSize = kLabelTargetSize * DpiScale;
            ImVec2 LabelSize = LabelFont->CalcTextSizeA(LabelRenderSize, FLT_MAX, 0.0f, Label.c_str());
            ImVec2 LabelPos(Bb.Min.x + IconSlotWidth, Bb.Min.y + (Size.y - LabelSize.y) * 0.5f);
            DrawList->AddText(LabelFont, LabelRenderSize, LabelPos, ImGui::ColorConvertFloat4ToU32(UIMgr::GetAccentColor()), Label.c_str());

            return Pressed;
        }

        // Decorative watermark pinned to the bottom-right corner of the Home window, tinted with the
        // current accent color. Source asset is expected to be a white shape on transparency so the
        // tint multiply reads as a solid accent-colored silhouette. Scales off the same factor as the
        // rest of the tab instead of a window-percentage-with-a-cap, so it keeps growing on big/fullscreen
        // windows instead of topping out early.
        void DrawHomeDecoration(float Scale)
        {
            application::gl::Texture* DecorationTexture = application::manager::TextureMgr::GetAssetTexture("HomeDecoration");
            if (!DecorationTexture || DecorationTexture->mWidth <= 0 || DecorationTexture->mHeight <= 0)
                return;

            // The Texture class bakes GL_NEAREST magnification (fine for pixel-art icons), which looks
            // blocky once this watermark is scaled up; force smooth bilinear sampling for this texture only.
            static bool FilterConfigured = false;
            if (!FilterConfigured)
            {
                glBindTexture(GL_TEXTURE_2D, DecorationTexture->mID);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glBindTexture(GL_TEXTURE_2D, 0);
                FilterConfigured = true;
            }

            ImVec2 WindowPos = ImGui::GetWindowPos();
            ImVec2 WindowSize = ImGui::GetWindowSize();

            constexpr float BaseDecorationWidth = 320.0f;
            float TargetWidth = BaseDecorationWidth * Scale;
            float AspectRatio = static_cast<float>(DecorationTexture->mHeight) / static_cast<float>(DecorationTexture->mWidth);
            float TargetHeight = TargetWidth * AspectRatio;

            ImVec2 BottomRight(WindowPos.x + WindowSize.x, WindowPos.y + WindowSize.y);
            ImVec2 TopLeft(BottomRight.x - TargetWidth, BottomRight.y - TargetHeight);

            ImVec4 Tint = UIMgr::GetAccentColor();
            Tint.w = 0.18f;

            // Texture.cpp loads with stbi_set_flip_vertically_on_load(true), which flips the source
            // image into GL's bottom-up row order; sampling it with straight (0,0)-(1,1) UVs therefore
            // renders it upside down, so the V coordinates are swapped here to compensate.
            ImGui::GetWindowDrawList()->AddImage((ImTextureID)(intptr_t)DecorationTexture->mID, TopLeft, BottomRight,
                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f), ImGui::ColorConvertFloat4ToU32(Tint));
        }

        void DrawHomeWindow()
        {
            if (gHomeWindowFirstFrame)
            {
                ImGui::SetNextWindowDockID(UIMgr::gDockMain);
                gHomeWindowFirstFrame = false;
            }

            if (!ImGui::Begin("Home", nullptr, ImGuiWindowFlags_NoCollapse))
            {
                ImGui::End();
                return;
            }

            // The logo/heading text and the corner decoration scale off the window's own width so the
            // home screen reads consistently whether it's a small docked panel or the full viewport.
            // This Scale is only ever used to pick an explicit font_size/image size to request - never
            // via SetWindowFontScale, which would blur bitmap fonts/textures by magnifying them past
            // their baked resolution.
            constexpr float ReferenceWidth = 1280.0f;
            constexpr float MinScale = 0.6f;
            constexpr float MaxScale = 2.0f;

            float Scale = ImGui::GetWindowSize().x / ReferenceWidth;
            Scale = Scale < MinScale ? MinScale : (Scale > MaxScale ? MaxScale : Scale);

            // Buttons deliberately do NOT use the above Scale - growing them in lockstep with the big
            // logo text made them look oversized on wide windows. They only track the monitor's actual
            // DPI factor, so they stay a steady, readable size regardless of window width.
            const float DpiScale = ImGui::GetPlatformIO().Monitors[0].DpiScale;

            DrawHomeDecoration(Scale);

            ImDrawList* DrawList = ImGui::GetWindowDrawList();
            ImVec2 ContentStart = ImGui::GetCursorScreenPos();
            const float Margin = 32.0f * Scale;
            ImVec2 LogoPos(ContentStart.x + Margin, ContentStart.y + Margin);

            ImFont* LogoFont = UIMgr::gLogoFont ? UIMgr::gLogoFont : ImGui::GetFont();
            float LogoRenderSize = kLogoTargetSize * Scale;
            ImVec2 LogoSize = LogoFont->CalcTextSizeA(LogoRenderSize, FLT_MAX, 0.0f, "Starlight");
            DrawList->AddText(LogoFont, LogoRenderSize, LogoPos, ImGui::ColorConvertFloat4ToU32(UIMgr::GetAccentColor()), "Starlight");

            // Heading sits to the right of the logo, vertically centered against the logo's own height.
            const float ColumnGap = 48.0f * Scale;
            ImFont* HeadingFont = UIMgr::gHeadingFont ? UIMgr::gHeadingFont : ImGui::GetFont();
            float HeadingRenderSize = kHeadingTargetSize * Scale;
            const char* HeadingText = "How would you like to get started?";
            ImVec2 HeadingSize = HeadingFont->CalcTextSizeA(HeadingRenderSize, FLT_MAX, 0.0f, HeadingText);
            ImVec2 HeadingPos(LogoPos.x + LogoSize.x + ColumnGap, LogoPos.y + (LogoSize.y - HeadingSize.y) * 0.5f);
            DrawList->AddText(HeadingFont, HeadingRenderSize, HeadingPos, ImGui::GetColorU32(ImGuiCol_Text), HeadingText);

            // Buttons form a single left-aligned column starting under the logo, below the whole
            // logo+heading row (not tucked under just the heading's side of it).
            float RowBottom = LogoPos.y + LogoSize.y;
            float HeadingBottom = HeadingPos.y + HeadingSize.y;
            if (HeadingBottom > RowBottom)
                RowBottom = HeadingBottom;

            ImVec2 ButtonAreaPos(LogoPos.x, RowBottom + 28.0f * Scale);
            const ImVec2 ButtonSize = ImVec2(300.0f * DpiScale, 54.0f * DpiScale);
            const float ButtonSpacing = 10.0f * DpiScale;

            if (!application::util::FileUtil::gPathsValid)
            {
                ImGui::SetCursorScreenPos(ButtonAreaPos);
                if (DrawToolButton(ICON_FA_GEAR, "Configure", ButtonSize, DpiScale))
                {
                    OpenPreferencesPopUp();
                }
            }
            else
            {
                std::vector<application::rendering::UIWindowBase::WindowType> ToolsToShow;

                if (!UIMgr::gRecentTools.empty())
                {
                    for (size_t i = 0; i < UIMgr::gRecentTools.size() && ToolsToShow.size() < 3; i++)
                        ToolsToShow.push_back(UIMgr::gRecentTools[i]);
                }
                else
                {
                    for (application::rendering::UIWindowBase::WindowType Type : kDefaultToolOrder)
                        ToolsToShow.push_back(Type);
                }

                ImVec2 CurrentButtonPos = ButtonAreaPos;
                for (application::rendering::UIWindowBase::WindowType Type : ToolsToShow)
                {
                    ImGui::SetCursorScreenPos(CurrentButtonPos);
                    if (DrawToolButton(GetToolIcon(Type), GetToolDisplayName(Type), ButtonSize, DpiScale))
                    {
                        LaunchTool(Type);
                    }
                    CurrentButtonPos.y += ButtonSize.y + ButtonSpacing;
                }
            }

            ImGui::End();
        }
    }

	GLFWwindow* UIMgr::gWindow = nullptr;
    ImVec4 UIMgr::gClearColor = ImVec4(0.145f, 0.145f, 0.149f, 1.00f);
    glm::vec4 UIMgr::gViewportClearColor = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
    std::vector<std::unique_ptr<application::rendering::UIWindowBase>> UIMgr::gWindows;
    std::vector<std::unique_ptr<application::rendering::UIWindowBase>> UIMgr::gWaitingWindows;
    unsigned int UIMgr::gWindowId = 0;
    bool UIMgr::gFirstFrame = true;
    bool UIMgr::gASTCSupported = false;
    int UIMgr::gThemeIndex = 0;
    int UIMgr::gBackgroundThemeIndex = gDefaultBackgroundThemeIndex;
    ImFont* UIMgr::gLogoFont = nullptr;
    ImFont* UIMgr::gHeadingFont = nullptr;
    ImFont* UIMgr::gBigIconFont = nullptr;
    ImFont* UIMgr::gButtonLabelFont = nullptr;
    std::vector<application::rendering::UIWindowBase::WindowType> UIMgr::gRecentTools;
    ImGuiID UIMgr::gDockMain;
	ImGuiID UIMgr::gDockBottom;
    application::rendering::popup::PopUpBuilder UIMgr::gSettingsPopUp;
    application::rendering::popup::PopUpBuilder UIMgr::gAddProjectPopUp;
    application::rendering::popup::PopUpBuilder UIMgr::gExportProjectPopUp;
    application::rendering::popup::PopUpBuilder UIMgr::gEnterLicenseKeyPopUp;
    application::rendering::popup::PopUpBuilder UIMgr::gGenerateLicenseKeyPopUp;
    bool UIMgr::gBlockProjectSwitch = false;

    void APIENTRY glDebugOutput(GLenum source,
        GLenum type,
        unsigned int id,
        GLenum severity,
        GLsizei length,
        const char* message,
        const void* userParam)
    {
        // ignore non-significant error/warning codes
        if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

        std::cout << "---------------" << std::endl;
        std::cout << "Debug message (" << id << "): " << message << std::endl;

        switch (source)
        {
        case GL_DEBUG_SOURCE_API:             std::cout << "Source: API"; break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   std::cout << "Source: Window System"; break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER: std::cout << "Source: Shader Compiler"; break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:     std::cout << "Source: Third Party"; break;
        case GL_DEBUG_SOURCE_APPLICATION:     std::cout << "Source: Application"; break;
        case GL_DEBUG_SOURCE_OTHER:           std::cout << "Source: Other"; break;
        } std::cout << std::endl;

        switch (type)
        {
        case GL_DEBUG_TYPE_ERROR:               std::cout << "Type: Error"; break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: std::cout << "Type: Deprecated Behaviour"; break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  std::cout << "Type: Undefined Behaviour"; break;
        case GL_DEBUG_TYPE_PORTABILITY:         std::cout << "Type: Portability"; break;
        case GL_DEBUG_TYPE_PERFORMANCE:         std::cout << "Type: Performance"; break;
        case GL_DEBUG_TYPE_MARKER:              std::cout << "Type: Marker"; break;
        case GL_DEBUG_TYPE_PUSH_GROUP:          std::cout << "Type: Push Group"; break;
        case GL_DEBUG_TYPE_POP_GROUP:           std::cout << "Type: Pop Group"; break;
        case GL_DEBUG_TYPE_OTHER:               std::cout << "Type: Other"; break;
        } std::cout << std::endl;

        switch (severity)
        {
        case GL_DEBUG_SEVERITY_HIGH:         std::cout << "Severity: high"; break;
        case GL_DEBUG_SEVERITY_MEDIUM:       std::cout << "Severity: medium"; break;
        case GL_DEBUG_SEVERITY_LOW:          std::cout << "Severity: low"; break;
        case GL_DEBUG_SEVERITY_NOTIFICATION: std::cout << "Severity: notification"; break;
        } std::cout << std::endl;
        std::cout << std::endl;
    }

	void UIMgr::GLFWErrorCallback(int error, const char* description)
	{
        application::util::Logger::Error("GLFW", "Code: %i, Description: %s", error, description);
	}

    int UIMgr::GetThemeCount()
    {
        return gThemePresetCount;
    }

    const char* UIMgr::GetThemeName(int Index)
    {
        if (Index < 0 || Index >= gThemePresetCount)
            Index = 0;

        return gThemePresets[Index].Name;
    }

    void UIMgr::ApplyTheme(int Index)
    {
        if (Index < 0 || Index >= gThemePresetCount)
            Index = 0;

        gThemeIndex = Index;
        const ThemePreset& Preset = gThemePresets[Index];
        ImGuiStyle& Style = ImGui::GetStyle();

        Style.Colors[ImGuiCol_FrameBgHovered] = Preset.Hover;
        Style.Colors[ImGuiCol_FrameBgActive] = Preset.Active;
        Style.Colors[ImGuiCol_CheckMark] = Preset.Active;
        Style.Colors[ImGuiCol_SliderGrab] = Preset.Hover;
        Style.Colors[ImGuiCol_SliderGrabActive] = Preset.Active;
        Style.Colors[ImGuiCol_ButtonHovered] = Preset.Hover;
        Style.Colors[ImGuiCol_ButtonActive] = Preset.Hover;
        Style.Colors[ImGuiCol_HeaderHovered] = Preset.Hover;
        Style.Colors[ImGuiCol_HeaderActive] = Preset.Active;
        Style.Colors[ImGuiCol_TabHovered] = Preset.Hover;
        Style.Colors[ImGuiCol_TabActive] = Preset.Active;
        Style.Colors[ImGuiCol_TabUnfocusedActive] = Preset.Active;
        Style.Colors[ImGuiCol_TabSelectedOverline] = Preset.Active;
        Style.Colors[ImGuiCol_TabDimmedSelectedOverline] = Preset.Active;
        Style.Colors[ImGuiCol_DockingPreview] = Preset.Active;
        Style.Colors[ImGuiCol_TextLink] = Preset.Hover;
        Style.Colors[ImGuiCol_PlotLines] = Preset.Active;
        Style.Colors[ImGuiCol_PlotLinesHovered] = Preset.Hover;
        Style.Colors[ImGuiCol_PlotHistogram] = Preset.Active;
        Style.Colors[ImGuiCol_PlotHistogramHovered] = Preset.Hover;
        Style.Colors[ImGuiCol_TextSelectedBg] = Preset.Active;
    }

    ImVec4 UIMgr::GetAccentColor()
    {
        return gThemePresets[gThemeIndex].Active;
    }

    int UIMgr::GetBackgroundThemeCount()
    {
        return gBackgroundThemePresetCount;
    }

    const char* UIMgr::GetBackgroundThemeName(int Index)
    {
        if (Index < 0 || Index >= gBackgroundThemePresetCount)
            Index = gDefaultBackgroundThemeIndex;

        return gBackgroundThemePresets[Index].Name;
    }

    ImVec4 UIMgr::GetNodeEditorBgColor()
    {
        return gBackgroundThemePresets[gBackgroundThemeIndex].NodeEditorBg;
    }

    ImVec4 UIMgr::GetNodeEditorGridColor()
    {
        return gBackgroundThemePresets[gBackgroundThemeIndex].NodeEditorGrid;
    }

#if defined(_WIN32)
    namespace
    {
        // Recolors the native window chrome (title bar) to match the current background theme instead
        // of leaving it as Windows' default bright white, using DWM APIs. DWMWA_USE_IMMERSIVE_DARK_MODE
        // has been supported since Windows 10 2004; DWMWA_CAPTION_COLOR/DWMWA_TEXT_COLOR are Windows 11+
        // only and DwmSetWindowAttribute simply (harmlessly) fails on older systems that lack them.
        void UpdateTitleBarTheme()
        {
            if (!UIMgr::gWindow)
                return;

            HWND Hwnd = glfwGetWin32Window(UIMgr::gWindow);
            if (!Hwnd)
                return;

            const BackgroundThemePreset& Preset = gBackgroundThemePresets[UIMgr::gBackgroundThemeIndex];

            float Luminance = 0.2126f * Preset.BaseBg.x + 0.7152f * Preset.BaseBg.y + 0.0722f * Preset.BaseBg.z;
            BOOL UseDarkMode = Luminance < 0.5f ? TRUE : FALSE;
            DwmSetWindowAttribute(Hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &UseDarkMode, sizeof(UseDarkMode));

            auto ToColorRef = [](const ImVec4& Color) -> COLORREF
            {
                BYTE R = static_cast<BYTE>(ImClamp(Color.x, 0.0f, 1.0f) * 255.0f + 0.5f);
                BYTE G = static_cast<BYTE>(ImClamp(Color.y, 0.0f, 1.0f) * 255.0f + 0.5f);
                BYTE B = static_cast<BYTE>(ImClamp(Color.z, 0.0f, 1.0f) * 255.0f + 0.5f);
                return RGB(R, G, B);
            };

            COLORREF CaptionColor = ToColorRef(Preset.BaseBg);
            DwmSetWindowAttribute(Hwnd, DWMWA_CAPTION_COLOR, &CaptionColor, sizeof(CaptionColor));

            COLORREF TextColor = ToColorRef(Preset.Text);
            DwmSetWindowAttribute(Hwnd, DWMWA_TEXT_COLOR, &TextColor, sizeof(TextColor));
        }
    }
#endif

    void UIMgr::ApplyBackgroundTheme(int Index)
    {
        if (Index < 0 || Index >= gBackgroundThemePresetCount)
            Index = gDefaultBackgroundThemeIndex;

        gBackgroundThemeIndex = Index;
        const BackgroundThemePreset& Preset = gBackgroundThemePresets[Index];
        ImGuiStyle& Style = ImGui::GetStyle();

        Style.Colors[ImGuiCol_Text] = Preset.Text;
        Style.Colors[ImGuiCol_TextDisabled] = Preset.TextDisabled;

        Style.Colors[ImGuiCol_WindowBg] = Preset.BaseBg;
        Style.Colors[ImGuiCol_ChildBg] = Preset.BaseBg;
        Style.Colors[ImGuiCol_PopupBg] = Preset.BaseBg;
        Style.Colors[ImGuiCol_TitleBg] = Preset.BaseBg;
        Style.Colors[ImGuiCol_TitleBgActive] = Preset.BaseBg;
        Style.Colors[ImGuiCol_TitleBgCollapsed] = Preset.BaseBg;
        Style.Colors[ImGuiCol_Tab] = Preset.BaseBg;
        Style.Colors[ImGuiCol_TabUnfocused] = Preset.BaseBg;
        Style.Colors[ImGuiCol_ResizeGrip] = Preset.BaseBg;
        Style.Colors[ImGuiCol_NavHighlight] = Preset.BaseBg;
        Style.Colors[ImGuiCol_DragDropTarget] = Preset.BaseBg;

        Style.Colors[ImGuiCol_FrameBg] = Preset.SurfaceBg;
        Style.Colors[ImGuiCol_MenuBarBg] = Preset.SurfaceBg;
        Style.Colors[ImGuiCol_ScrollbarBg] = Preset.SurfaceBg;
        Style.Colors[ImGuiCol_Button] = Preset.SurfaceBg;
        Style.Colors[ImGuiCol_Header] = Preset.SurfaceBg;
        Style.Colors[ImGuiCol_ResizeGripHovered] = Preset.SurfaceBg;

        Style.Colors[ImGuiCol_Border] = Preset.BorderColor;
        Style.Colors[ImGuiCol_BorderShadow] = Preset.BorderColor;
        Style.Colors[ImGuiCol_Separator] = Preset.BorderColor;
        Style.Colors[ImGuiCol_SeparatorHovered] = Preset.BorderColor;
        Style.Colors[ImGuiCol_SeparatorActive] = Preset.BorderColor;

        Style.Colors[ImGuiCol_ScrollbarGrab] = Preset.GrabColor;
        Style.Colors[ImGuiCol_ResizeGripActive] = Preset.GrabColor;
        Style.Colors[ImGuiCol_ScrollbarGrabHovered] = Preset.GrabHoverColor;
        Style.Colors[ImGuiCol_ScrollbarGrabActive] = Preset.GrabHoverColor;

        Style.Colors[ImGuiCol_TableHeaderBg] = Preset.TableHeaderBg;
        Style.Colors[ImGuiCol_TableBorderStrong] = Preset.TableBorderStrong;
        Style.Colors[ImGuiCol_TableBorderLight] = Preset.TableBorderLight;
        Style.Colors[ImGuiCol_TableRowBgAlt] = Preset.TableRowBgAlt;
        Style.Colors[ImGuiCol_NavWindowingDimBg] = Preset.NavWindowingDimBg;

        gClearColor = Preset.BaseBg;
        gViewportClearColor = Preset.ViewportClearColor;

        for (auto& Window : gWindows)
        {
            if (Window->GetWindowType() == application::rendering::UIWindowBase::WindowType::EDITOR_AINB)
            {
                static_cast<application::rendering::ainb::UIAINBEditor*>(Window.get())->RefreshTheme();
            }
        }

#if defined(_WIN32)
        UpdateTitleBarTheme();
#endif
    }

	bool UIMgr::Initialize()
	{
		assert(gWindow == nullptr && "UI was already initialized");

		glfwSetErrorCallback(GLFWErrorCallback);
		if (!glfwInit())
		{
            application::util::Logger::Error("UIMgr", "Could not initialize GLFW");
			return false;
		}

#ifdef __APPLE__
        const char* glsl_version = "#version 150";
#else
        const char* glsl_version = "#version 130";
#endif

        {

            const int maxMajor = 4;
            const int maxMinor = 5;

            // Try versions from highest to lowest
            for (int major = maxMajor; major >= 3 && gWindow == nullptr; major--)
            {
                int minorStart = (major == maxMajor) ? maxMinor : 9; // If trying max major, start with max minor

                for (int minor = minorStart; minor >= 0 && gWindow == nullptr; minor--)
                {
                    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, major);
                    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, minor);
                    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
                    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#else
                    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_FALSE); // Change this
#endif

#if TOOL_GL_DEBUG == 1
                    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, true);
#endif

                    // Try to create window with these settings
                    gWindow = glfwCreateWindow(1280, 720, "Starlight", NULL, NULL);
                }
            }
        }

        // Create window with graphics context
        if (gWindow == nullptr)
        {
            application::util::Logger::Error("UIMgr", "Could not create window");
            return false;
        }

#if defined(_WIN32)
        {
            HWND hwnd = glfwGetWin32Window(gWindow);
            HINSTANCE instance = GetModuleHandleW(nullptr);
            HICON iconLarge = static_cast<HICON>(LoadImageW(
                instance,
                L"IDI_MAIN_ICON",
                IMAGE_ICON,
                GetSystemMetrics(SM_CXICON),
                GetSystemMetrics(SM_CYICON),
                LR_DEFAULTCOLOR
            ));
            HICON iconSmall = static_cast<HICON>(LoadImageW(
                instance,
                L"IDI_MAIN_ICON",
                IMAGE_ICON,
                GetSystemMetrics(SM_CXSMICON),
                GetSystemMetrics(SM_CYSMICON),
                LR_DEFAULTCOLOR
            ));

            if (hwnd != nullptr)
            {
                if (iconLarge != nullptr)
                    SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(iconLarge));
                if (iconSmall != nullptr)
                    SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(iconSmall));
            }

            if (iconLarge == nullptr && iconSmall == nullptr)
                application::util::Logger::Warning("UIMgr", "Failed to load embedded window icon IDI_MAIN_ICON");
        }
#endif

        glfwMakeContextCurrent(gWindow);

        gladLoadGL();

        glfwSwapInterval(1); // Enable vsync

        int ActualMajor, ActualMinor;
        glGetIntegerv(GL_MAJOR_VERSION, &ActualMajor);
        glGetIntegerv(GL_MINOR_VERSION, &ActualMinor);

        application::util::Logger::Info("UIMgr", "Created OpenGL context version: %i.%i", ActualMajor, ActualMinor);

        // Setup Dear ImGui context
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO(); (void)io;
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
        //io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
#if TOOL_IMGUI_VIEWPORTS_ENABLED == 1
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows
#endif
        //io.ConfigViewportsNoAutoMerge = true;
        //io.ConfigViewportsNoTaskBarIcon = true;


        int fbWidth, fbHeight;
        int winWidth, winHeight;
        glfwGetFramebufferSize(gWindow, &fbWidth, &fbHeight);
        glfwGetWindowSize(gWindow, &winWidth, &winHeight);

        float retinaScaleX = (float)fbWidth / (float)winWidth;
        float retinaScaleY = (float)fbHeight / (float)winHeight;
        io.FontGlobalScale = 1.0f / retinaScaleX;

        // Setup Dear ImGui style
        ImGui::StyleColorsDark();
        //ImGui::StyleColorsLight();
        
        ImGuiStyle& Style = ImGui::GetStyle();

        // When viewports are enabled we tweak WindowRounding/WindowBg so platform windows can look identical to regular ones.
#if TOOL_IMGUI_VIEWPORTS_ENABLED == 1
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            Style.WindowRounding = 0.0f;
            Style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }
#endif

        Style.Alpha = 1.0;
        Style.DisabledAlpha = 0.6000000238418579;
        Style.WindowPadding = ImVec2(8.0, 8.0);
        Style.WindowRounding = 6.0;
        Style.WindowBorderSize = 1.0;
        Style.WindowMinSize = ImVec2(32.0, 32.0);
        Style.WindowTitleAlign = ImVec2(0.0, 0.5);
        Style.WindowMenuButtonPosition = ImGuiDir_Left;
        Style.ChildRounding = 6.0;
        Style.ChildBorderSize = 1.0;
        Style.PopupRounding = 6.0;
        Style.PopupBorderSize = 1.0;
        Style.FramePadding = ImVec2(6.0, 4.0);
        Style.FrameRounding = 4.0;
        Style.FrameBorderSize = 0.0;
        Style.ItemSpacing = ImVec2(8.0, 4.0);
        Style.ItemInnerSpacing = ImVec2(4.0, 4.0);
        Style.CellPadding = ImVec2(4.0, 2.0);
        Style.IndentSpacing = 21.0;
        Style.ColumnsMinSpacing = 6.0;
        Style.ScrollbarSize = 14.0;
        Style.ScrollbarRounding = 8.0;
        Style.GrabMinSize = 10.0;
        Style.GrabRounding = 4.0;
        Style.TabRounding = 4.0;
        Style.TabBorderSize = 0.0;
        Style.ColorButtonPosition = ImGuiDir_Right;
        Style.ButtonTextAlign = ImVec2(0.5, 0.5);
        Style.SelectableTextAlign = ImVec2(0.0, 0.0);

        Style.Colors[ImGuiCol_Text] = ImVec4(1.0, 1.0, 1.0, 1.0);
        Style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.592156862745098, 0.592156862745098, 0.592156862745098, 1.0);
        Style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_ChildBg] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_PopupBg] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_Border] = ImVec4(0.3058823529411765, 0.3058823529411765, 0.3058823529411765, 1.0);
        Style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.3058823529411765, 0.3058823529411765, 0.3058823529411765, 1.0);
        Style.Colors[ImGuiCol_FrameBg] = ImVec4(0.2, 0.2, 0.21568627450980393, 1.0);
        Style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.11372549019607843, 0.592156862745098, 0.9254901960784314, 1.0);
        Style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_TitleBg] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.2, 0.2, 0.21568627450980393, 1.0);
        Style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.2, 0.2, 0.21568627450980393, 1.0);
        Style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3215686274509804, 0.3215686274509804, 0.3333333333333333, 1.0);
        Style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35294117647058826, 0.35294117647058826, 0.37254901960784315, 1.0);
        Style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.35294117647058826, 0.35294117647058826, 0.37254901960784315, 1.0);
        Style.Colors[ImGuiCol_CheckMark] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.11372549019607843, 0.592156862745098, 0.9254901960784314, 1.0);
        Style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_Button] = ImVec4(0.2, 0.2, 0.21568627450980393, 1.0);
        Style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.11372549019607843, 0.592156862745098, 0.9254901960784314, 1.0);
        Style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.11372549019607843, 0.592156862745098, 0.9254901960784314, 1.0);
        Style.Colors[ImGuiCol_Header] = ImVec4(0.2, 0.2, 0.21568627450980393, 1.0);
        Style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.11372549019607843, 0.592156862745098, 0.9254901960784314, 1.0);
        Style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_Separator] = ImVec4(0.3058823529411765, 0.3058823529411765, 0.3058823529411765, 1.0);
        Style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.3058823529411765, 0.3058823529411765, 0.3058823529411765, 1.0);
        Style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.3058823529411765, 0.3058823529411765, 0.3058823529411765, 1.0);
        Style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.2, 0.2, 0.21568627450980393, 1.0);
        Style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.3215686274509804, 0.3215686274509804, 0.3333333333333333, 1.0);
        Style.Colors[ImGuiCol_Tab] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_TabHovered] = ImVec4(0.11372549019607843, 0.592156862745098, 0.9254901960784314, 1.0);
        Style.Colors[ImGuiCol_TabActive] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_PlotLines] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.11372549019607843, 0.592156862745098, 0.9254901960784314, 1.0);
        Style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.11372549019607843, 0.592156862745098, 0.9254901960784314, 1.0);
        Style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.18823529411764706, 0.18823529411764706, 0.2, 1.0);
        Style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.30980392156862746, 0.30980392156862746, 0.34901960784313724, 1.0);
        Style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.22745098039215686, 0.22745098039215686, 0.24705882352941178, 1.0);
        Style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0, 0.0, 0.0, 0.0);
        Style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0, 1.0, 1.0, 0.05999999865889549);
        Style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.0, 0.4666666666666667, 0.7843137254901961, 1.0);
        Style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);
        Style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0, 1.0, 1.0, 0.699999988079071);
        Style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.8, 0.8, 0.8, 0.2000000029802322);
        //Style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.1450980392156863, 0.1450980392156863, 0.14901960784313725, 1.0);

        ApplyBackgroundTheme(gBackgroundThemeIndex);
        ApplyTheme(gThemeIndex);

        /*
        style.Colors[ImGuiCol_Text] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.00f, 0.40f, 0.41f, 1.00f);
        style.Colors[ImGuiCol_WindowBg] = ImVec4(0.00f, 0.00f, 0.00f, 1.00f);
        style.Colors[ImGuiCol_Border] = ImVec4(0.00f, 1.00f, 1.00f, 0.65f);
        style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
        style.Colors[ImGuiCol_FrameBg] = ImVec4(0.44f, 0.80f, 0.80f, 0.18f);
        style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.44f, 0.80f, 0.80f, 0.27f);
        style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.44f, 0.81f, 0.86f, 0.66f);
        style.Colors[ImGuiCol_TitleBg] = ImVec4(0.14f, 0.18f, 0.21f, 0.73f);
        style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.00f, 0.00f, 0.00f, 0.54f);
        style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.27f);
        style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.00f, 0.00f, 0.00f, 0.20f);
        style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.22f, 0.29f, 0.30f, 0.71f);
        style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.00f, 1.00f, 1.00f, 0.44f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
        style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_CheckMark] = ImVec4(0.00f, 1.00f, 1.00f, 0.68f);
        style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.00f, 1.00f, 1.00f, 0.36f);
        style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.76f);
        style.Colors[ImGuiCol_Button] = ImVec4(0.00f, 0.65f, 0.65f, 0.46f);
        style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.01f, 1.00f, 1.00f, 0.43f);
        style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.62f);
        style.Colors[ImGuiCol_Header] = ImVec4(0.00f, 1.00f, 1.00f, 0.33f);
        style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.42f);
        style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
        style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.00f, 1.00f, 1.00f, 0.54f);
        style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.00f, 1.00f, 1.00f, 0.74f);
        style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_PlotLines] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.00f, 1.00f, 1.00f, 1.00f);
        style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.00f, 1.00f, 1.00f, 0.22f);
        style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.04f, 0.10f, 0.09f, 0.51f);*/


        // Setup Platform/Renderer backends
        ImGui_ImplGlfw_InitForOpenGL(gWindow, true);
        ImGui_ImplOpenGL3_Init(glsl_version);

        io.Fonts->AddFontFromFileTTF(application::util::FileUtil::GetAssetFilePath("Fonts/Regular.ttf").c_str(), 14.0f * ImGui::GetPlatformIO().Monitors[0].DpiScale);

        /**
         * FontAwesome setup START (required for icons)
        */

        float baseFontSize = 16.0f;
        float iconFontSize = baseFontSize * 2.0f / 3.0f; // FontAwesome fonts need to have their sizes reduced by 2.0f/3.0f in order to align correctly

        static constexpr ImWchar iconsRanges[] = { ICON_MIN_FA, ICON_MAX_16_FA, 0 };
        ImFontConfig iconsConfig;
        iconsConfig.MergeMode = true;
        iconsConfig.PixelSnapH = true;
        iconsConfig.GlyphMinAdvanceX = iconFontSize;
        io.Fonts->AddFontFromMemoryCompressedTTF(fa_solid_900_compressed_data, fa_solid_900_compressed_size, iconFontSize, &iconsConfig, iconsRanges);

        /**
         * FontAwesome setup END
        */

        // Home tab fonts are baked at kFontOversample times their intended display size (see comment
        // near kFontOversample) so that scaling them up on a large/fullscreen window never magnifies
        // past the baked bitmap - that's what causes ImGui text to blur when a window grows.
        const float DpiScale = ImGui::GetPlatformIO().Monitors[0].DpiScale;

        const std::string LogoFontPath = application::util::FileUtil::GetAssetFilePath("Fonts/Hylia-Serif.otf");
        if (application::util::FileUtil::FileExists(LogoFontPath))
        {
            ImFontConfig LogoConfig;
            LogoConfig.OversampleH = 3;
            LogoConfig.OversampleV = 3;
            gLogoFont = io.Fonts->AddFontFromFileTTF(LogoFontPath.c_str(), kLogoTargetSize * kFontOversample * DpiScale, &LogoConfig);
        }

        ImFontConfig HeadingConfig;
        HeadingConfig.OversampleH = 3;
        HeadingConfig.OversampleV = 3;
        gHeadingFont = io.Fonts->AddFontFromFileTTF(application::util::FileUtil::GetAssetFilePath("Fonts/Regular.ttf").c_str(), kHeadingTargetSize * kFontOversample * DpiScale, &HeadingConfig);

        // Standalone (non-merged) larger FontAwesome atlas entry, used for oversized icons on the Home tab's tool buttons.
        ImFontConfig bigIconConfig;
        bigIconConfig.PixelSnapH = true;
        gBigIconFont = io.Fonts->AddFontFromMemoryCompressedTTF(fa_solid_900_compressed_data, fa_solid_900_compressed_size, kIconTargetSize * kFontOversample * DpiScale, &bigIconConfig, iconsRanges);

        // Larger body-text atlas entry, used for the Home tab's tool button labels.
        ImFontConfig LabelConfig;
        LabelConfig.OversampleH = 3;
        LabelConfig.OversampleV = 3;
        gButtonLabelFont = io.Fonts->AddFontFromFileTTF(application::util::FileUtil::GetAssetFilePath("Fonts/Regular.ttf").c_str(), kLabelTargetSize * kFontOversample * DpiScale, &LabelConfig);


        gSettingsPopUp.Title("Preferences").Width(550.0f).Height(360.0f).NeedsConfirmation(false).ContentDrawingFunction([](application::rendering::popup::PopUpBuilder& Builder)
            {
                ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(189 / 255.0f, 195 / 255.0f, 199 / 255.0f, 1.0f));

                if (ImGui::BeginTabBar("PreferencesTabs"))
                {
                    if (ImGui::BeginTabItem("General"))
                    {
                        ImGui::NewLine();

                        const float LabelWidth = ImGui::CalcTextSize("RomFS Path").x + ImGui::GetStyle().ItemSpacing.x * 3.0f;

                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("RomFS Path");
                        ImGui::SameLine(LabelWidth);

                        bool RomFSValid = !application::util::FileUtil::gRomFSPath.empty();
                        if (RomFSValid)
                            RomFSValid = application::util::FileUtil::FileExists(application::util::FileUtil::gRomFSPath + "/Pack/Bootup.Nin_NX_NVN.pack.zs");

                        const float BrowseButtonWidth = ImGui::CalcTextSize(ICON_FA_FOLDER_OPEN).x + ImGui::GetStyle().FramePadding.x * 2.0f;
                        const float InputWidth = ImGui::GetContentRegionAvail().x - BrowseButtonWidth - ImGui::GetStyle().ItemSpacing.x;

                        ImGui::SetNextItemWidth(InputWidth);
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, RomFSValid ? ImVec4(0.06f, 0.26f, 0.07f, 1.0f) : ImVec4(0.26f, 0.06f, 0.07f, 1.0f));
                        ImGui::InputText("##RomFSPath", &application::util::FileUtil::gRomFSPath);
                        ImGui::PopStyleColor();

                        ImGui::SameLine();
                        if (ImGui::Button(ICON_FA_FOLDER_OPEN "##BrowseRomFSPath"))
                        {
                            auto Dialog = pfd::select_folder("Select RomFS Folder",
                                application::util::FileUtil::gRomFSPath.empty() ? pfd::path::home() : application::util::FileUtil::gRomFSPath);

                            std::string Selected = Dialog.result();
                            if (!Selected.empty())
                                application::util::FileUtil::gRomFSPath = Selected;
                        }
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Browse for RomFS folder");

                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Starlight"))
                    {
                        ImGui::NewLine();

                        if (ImGui::BeginTabBar("StarlightSubTabs"))
                        {
                            if (ImGui::BeginTabItem("General"))
                            {
                                ImGui::NewLine();
                                ImGui::Checkbox("Enable Projects", &application::manager::ProjectMgr::gProjectsEnabled);
                                ImGui::TextWrapped("When disabled, the Projects menu is hidden and editors can be opened without selecting a project.");

                                ImGui::EndTabItem();
                            }

                            if (ImGui::BeginTabItem("Theme"))
                            {
                                ImGui::NewLine();

                                const float LabelWidth = ImGui::CalcTextSize("Background").x + ImGui::GetStyle().ItemSpacing.x * 3.0f;

                                ImGui::AlignTextToFramePadding();
                                ImGui::Text("Accent");
                                ImGui::SameLine(LabelWidth);

                                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                                if (ImGui::BeginCombo("##AccentTheme", application::manager::UIMgr::GetThemeName(application::manager::UIMgr::gThemeIndex)))
                                {
                                    for (int i = 0; i < application::manager::UIMgr::GetThemeCount(); i++)
                                    {
                                        bool Selected = application::manager::UIMgr::gThemeIndex == i;
                                        if (ImGui::Selectable(application::manager::UIMgr::GetThemeName(i), Selected))
                                        {
                                            application::manager::UIMgr::ApplyTheme(i);
                                        }
                                        if (Selected)
                                            ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }

                                ImGui::AlignTextToFramePadding();
                                ImGui::Text("Background");
                                ImGui::SameLine(LabelWidth);

                                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                                if (ImGui::BeginCombo("##BackgroundTheme", application::manager::UIMgr::GetBackgroundThemeName(application::manager::UIMgr::gBackgroundThemeIndex)))
                                {
                                    for (int i = 0; i < application::manager::UIMgr::GetBackgroundThemeCount(); i++)
                                    {
                                        bool Selected = application::manager::UIMgr::gBackgroundThemeIndex == i;
                                        if (ImGui::Selectable(application::manager::UIMgr::GetBackgroundThemeName(i), Selected))
                                        {
                                            application::manager::UIMgr::ApplyBackgroundTheme(i);
                                        }
                                        if (Selected)
                                            ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::EndCombo();
                                }

                                ImGui::EndTabItem();
                            }

                            ImGui::EndTabBar();
                        }

                        ImGui::EndTabItem();
                    }

                    ImGui::EndTabBar();
                }

                ImGui::PopStyleColor();
            }).Register();

        gAddProjectPopUp.Title("Projects").Width(500.0f).Flag(ImGuiWindowFlags_NoResize).NeedsConfirmation(false).AddDataStorage(512).ContentDrawingFunction([](application::rendering::popup::PopUpBuilder& Builder)
            {
                ImGui::InputText("Name", reinterpret_cast<char*>(Builder.GetDataStorage(0).mPtr), Builder.GetDataStorage(0).mSize);
            }).Register();

        gExportProjectPopUp.Title("Export").Width(500.0f).Flag(ImGuiWindowFlags_NoResize).AddDataStorageInstanced<bool>([](void* Bool) { *reinterpret_cast<bool*>(Bool) = true; }).NeedsConfirmation(false).ContentDrawingFunction([](application::rendering::popup::PopUpBuilder& Builder)
            {
                ImGui::InputText("Path", &application::manager::ProjectMgr::gExportProjectPath);
                ImGui::Checkbox("Generate RSTB", reinterpret_cast<bool*>(Builder.GetDataStorage(0).mPtr));
            }).Register();

        gEnterLicenseKeyPopUp.Title("License").Width(500.0f).NeedsConfirmation(true).AddDataStorageInstanced<std::string>([](void* Str) { *reinterpret_cast<std::string*>(Str) = ""; }).ContentDrawingFunction([](application::rendering::popup::PopUpBuilder& Builder)
            {
                if(application::file::tool::LicenseFile::gLicenseSeed.empty())
				    application::file::tool::LicenseFile::gLicenseSeed = application::util::Math::GenerateRandomString();

                ImGui::Text("Your license seed: %s", application::file::tool::LicenseFile::gLicenseSeed.c_str());
                ImGui::SameLine();
                if(ImGui::Button("Copy"))
                {
                    ImGui::SetClipboardText(application::file::tool::LicenseFile::gLicenseSeed.c_str());
				}
                ImGui::NewLine();
				ImGui::Text("Please enter your license key to continue:");
                ImGui::InputText("License Key", reinterpret_cast<std::string*>(Builder.GetDataStorage(0).mPtr));
            }).Register();

        gGenerateLicenseKeyPopUp.Title("Generate License").Width(500.0f).AddDataStorageInstanced<std::string>([](void* Str) { *reinterpret_cast<std::string*>(Str) = ""; }).ContentDrawingFunction([](application::rendering::popup::PopUpBuilder& Builder)
            {
                ImGui::Text("Please enter the license seed to generate:");
                ImGui::InputText("License Seed", reinterpret_cast<std::string*>(Builder.GetDataStorage(0).mPtr));
            }).Register();

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_STENCIL_TEST);
        glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glPatchParameteri(GL_PATCH_VERTICES, 4); //4 is for terrain

#if TOOL_GL_DEBUG == 1
        if (GLAD_GL_VERSION_4_3 && glad_glDebugMessageCallback != nullptr)
        {
            glEnable(GL_DEBUG_OUTPUT);
            glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback(glDebugOutput, nullptr);
            glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
            gUseDebugCallback = true;
            application::util::Logger::Info("UIMgr", "Enabled OpenGL debug callback");
        }
        else
        {
            gUseDebugCallback = false;
            application::util::Logger::Info("UIMgr", "OpenGL debug callback unsupported on this context, using glGetError polling");
        }
        PollGLErrors("UIMgr::Initialize");
#endif

        GLint NumFormats = 0;
        glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS, &NumFormats);
        GLint* Formats = new GLint[NumFormats];
        glGetIntegerv(GL_COMPRESSED_TEXTURE_FORMATS, Formats);
        for (int i = 0; i < NumFormats; i++)
        {
            if (Formats[i] == GL_COMPRESSED_RGBA_ASTC_4x4_KHR)
            {
                gASTCSupported = true;
                break;
            }
        }
        delete[] Formats;
        application::util::Logger::Info("UIMgr", "GPU based ASTC decoding supported: %s", gASTCSupported ? "True" : "False");

        const GLubyte* renderer = glGetString(GL_RENDERER);
        const GLubyte* vendor = glGetString(GL_VENDOR);
		application::util::Logger::Info("UIMgr", "GPU Renderer: %s", renderer);
		application::util::Logger::Info("UIMgr", "GPU Vendor: %s", vendor);

        application::util::Logger::Info("UIMgr", "UI initialized");

        return true;
	}

    void UIMgr::Render()
    {
        assert(gWindow != nullptr && "UI not initialized!");

        glfwPollEvents();

        // Start the Dear ImGui frame
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        ImGuiID DockSpace = ImGui::DockSpaceOverViewport(ImGui::GetMainViewport()->ID);

        if(gFirstFrame)
        {
            ImGui::DockBuilderRemoveNode(DockSpace);
			ImGui::DockBuilderAddNode(DockSpace, ImGuiDockNodeFlags_CentralNode);
			ImGui::DockBuilderSetNodeSize(DockSpace, ImGui::GetMainViewport()->Size);

			//ImGui::DockBuilderSplitNode(DockSpace, ImGuiDir_Down, 0.25f, &gDockBottom, &gDockMain);
            gDockMain = DockSpace;

            //ImGui::DockBuilderDockWindow("Content Browser", gDockBottom);

            ImGui::DockBuilderFinish(DockSpace);
            gFirstFrame = false;
        }

        //ImGui::ShowDemoWindow();

        UpdateWaitingWindows();

        DrawHomeWindow();

        for (auto Iter = gWindows.begin(); Iter != gWindows.end(); )
        {
            Iter->get()->Draw();

            if (!Iter->get()->mOpen)
            {
                Iter->get()->Delete();
                Iter = gWindows.erase(Iter);

                gBlockProjectSwitch = false;
                for (auto& Window : gWindows)
                {
                    Window->UpdateSameWindowCount();
                    gBlockProjectSwitch |= !Window->SupportsProjectChange();
                }

                continue;
            }
            Iter++;
        }

        PopUpMgr::Render();

        if (ImGui::BeginMainMenuBar())
        {
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
            ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.4f, 0.4f, 0.4f, 1.0f));

            const bool RequiresProject = application::manager::ProjectMgr::gProjectsEnabled && !application::manager::ProjectMgr::IsAnyProjectSelected();

#if defined(TOOL_FORCE_PATHS)
            if (!application::util::FileUtil::gPathsValid)
                ImGui::BeginDisabled();
#endif

            if (RequiresProject)
                ImGui::BeginDisabled();

            if (ImGui::BeginMenu("Window"))
            {
                if (ImGui::MenuItem("Map Editor"))
                {
                    LaunchTool(application::rendering::UIWindowBase::WindowType::EDITOR_MAP);
                }
                if (ImGui::MenuItem("AINB Editor"))
                {
                    LaunchTool(application::rendering::UIWindowBase::WindowType::EDITOR_AINB);
                }
                if (ImGui::MenuItem("Actor Editor"))
                {
                    LaunchTool(application::rendering::UIWindowBase::WindowType::EDITOR_ACTOR);
                }
                if (ImGui::MenuItem("Collision Generator"))
                {
                    LaunchTool(application::rendering::UIWindowBase::WindowType::EDITOR_COLLISION);
                }
                if (ImGui::MenuItem("Plugins"))
                {
                    LaunchTool(application::rendering::UIWindowBase::WindowType::EDITOR_PLUGINS);
                }
                ImGui::EndMenu();
            }

            if (RequiresProject)
                ImGui::EndDisabled();

            if (ImGui::MenuItem("Preferences"))
            {
                OpenPreferencesPopUp();
            }

            if (application::manager::ProjectMgr::gProjectsEnabled)
            {
                if (ImGui::BeginMenu("Projects"))
                {
                    if (application::util::FileUtil::gPathsValid && gBlockProjectSwitch)
                        ImGui::BeginDisabled();

                    ImGui::Text("Selected: %s", application::manager::ProjectMgr::IsAnyProjectSelected() ? application::manager::ProjectMgr::gProject.c_str() : "None");
                    if (ImGui::MenuItem("Add"))
                    {
                        gAddProjectPopUp.Open([](application::rendering::popup::PopUpBuilder& Builder)
                            {
                                application::manager::ProjectMgr::AddProject(std::string(reinterpret_cast<char*>(Builder.GetDataStorage(0).mPtr)));
                            });
                    }
                    ImGui::Separator();
                    for (const std::string& Name : application::manager::ProjectMgr::gProjects)
                    {
                        if (ImGui::MenuItem(Name.c_str()))
                        {
                            application::manager::ProjectMgr::SelectProject(Name);
                        }
                    }

                    if (application::util::FileUtil::gPathsValid && gBlockProjectSwitch)
                        ImGui::EndDisabled();

                    if (!application::manager::ProjectMgr::IsAnyProjectSelected())
                        ImGui::BeginDisabled();

                    ImGui::Separator();
                    if (ImGui::MenuItem("Export"))
                    {
                        gExportProjectPopUp.Open([](application::rendering::popup::PopUpBuilder& Builder)
                            {
                                application::manager::ProjectMgr::ExportProject(*reinterpret_cast<bool*>(Builder.GetDataStorage(0).mPtr));
                            });
                    }

                    if (!application::manager::ProjectMgr::IsAnyProjectSelected())
                        ImGui::EndDisabled();

                    ImGui::EndMenu();
                }
            }

#if defined(TOOL_FORCE_PATHS)
            if (!application::util::FileUtil::gPathsValid)
                ImGui::EndDisabled();
#endif

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor();

            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.0f, 1.0f, 0.0f, 1.0f));
            ImGui::Text("Version: %s", STARLIGHT_APP_VERSION);
            ImGui::SameLine();
            ImGui::Text("| Build: %s", STARLIGHT_GIT_COMMIT);
            ImGui::PopStyleColor();

            ImGui::EndMainMenuBar();
        }

        // Notifications style setup
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f); // Disable round borders
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f); // Disable borders

        // Notifications color setup
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.10f, 1.00f)); // Background color


        // Main rendering function
        ImGui::RenderNotifications();


        //������������������������������� WARNING �������������������������������
        // Argument MUST match the amount of ImGui::PushStyleVar() calls 
        ImGui::PopStyleVar(2);
        // Argument MUST match the amount of ImGui::PushStyleColor() calls 
        ImGui::PopStyleColor(1);

        // Rendering
        ImGui::Render();
        int DisplayW, DisplayH;
        glfwGetFramebufferSize(gWindow, &DisplayW, &DisplayH);
        glViewport(0, 0, DisplayW, DisplayH);
        glClearColor(gClearColor.x, gClearColor.y, gClearColor.z, gClearColor.w);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

#if TOOL_GL_DEBUG == 1
        if (!gUseDebugCallback)
            PollGLErrors("UIMgr::Render");
#endif

        // Update and Render additional Platform Windows
        // (Platform functions may change the current OpenGL context, so we save/restore it to make it easier to paste this code elsewhere.
        //  For this specific demo app we could also call glfwMakeContextCurrent(window) directly)
#if TOOL_IMGUI_VIEWPORTS_ENABLED == 1
        GLFWwindow* backup_current_context = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backup_current_context);
#endif

        glfwSwapBuffers(gWindow);
    }

    bool UIMgr::ShouldWindowClose()
    {
        assert(gWindow != nullptr && "UI not initialized!");

        return glfwWindowShouldClose(gWindow);
    }

    void UIMgr::Cleanup()
    {
        assert(gWindow != nullptr && "UI not initialized!");

        for (std::unique_ptr<application::rendering::UIWindowBase>& Window : gWindows)
        {
            Window->Delete();
        }
        gWindows.clear();

        // Cleanup
        application::manager::BfresRendererMgr::Cleanup();
        application::manager::FramebufferMgr::Cleanup();
        application::manager::TextureMgr::Cleanup();
        application::manager::ShaderMgr::Cleanup();

        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();

        glfwDestroyWindow(gWindow);
        glfwTerminate();

        application::util::Logger::Info("UIMgr", "UIMgr cleaned up");
    }

    std::unique_ptr<application::rendering::UIWindowBase>& UIMgr::OpenWindow(std::unique_ptr<application::rendering::UIWindowBase> Window)
    {
        if (!Window->SupportsProjectChange())
            gBlockProjectSwitch = true;

        application::rendering::UIWindowBase::WindowType Type = Window->GetWindowType();
        if (Type != application::rendering::UIWindowBase::WindowType::GENERAL_CONTENT_BROWSER)
        {
            gRecentTools.erase(std::remove(gRecentTools.begin(), gRecentTools.end(), Type), gRecentTools.end());
            gRecentTools.insert(gRecentTools.begin(), Type);
            if (gRecentTools.size() > 5)
                gRecentTools.resize(5);
        }

        gWaitingWindows.push_back(std::move(Window));
        return gWaitingWindows.back();
    }

    void UIMgr::UpdateWaitingWindows()
    {
        if(gWaitingWindows.empty())
            return;

        for(auto& Window : gWaitingWindows)
        {
            Window->mWindowId = gWindowId++;
            Window->Initialize();
            gWindows.push_back(std::move(Window));
        }
        gWaitingWindows.clear();
    }
}
