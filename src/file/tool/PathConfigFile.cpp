#include "PathConfigFile.h"

#include <util/FileUtil.h>
#include <util/BinaryVectorReader.h>
#include <util/BinaryVectorWriter.h>
#include <util/Logger.h>
#include <manager/ProjectMgr.h>
#include <manager/UIMgr.h>
#include <rendering/ainb/UIAINBEditor.h>

namespace application::file::tool
{
	void PathConfigFile::Load(const std::string& Path)
	{
		if (!application::util::FileUtil::FileExists(Path))
			return;

		application::util::BinaryVectorReader Reader(application::util::FileUtil::ReadFile(Path));

		char Magic[9];
		Reader.ReadStruct(&Magic[0], 8); //EPATHCFG
		Magic[8] = '\0';
		if (strcmp(Magic, "EPATHCFG") != 0)
		{
			application::util::Logger::Error("PathConfig", "File magic invalid, expected EPATHCFG");
			return;
		}

		uint8_t Version = Reader.ReadUInt8();
		if (Version != 0x02 && Version != 0x03 && Version != 0x04 && Version != 0x05 && Version != 0x06 && Version != 0x07 && Version != 0x08 && Version != 0x09)
		{
			application::util::Logger::Error("PathConfig", "Version invalid, expected 0x02 through 0x09. Please delete the path config file and reconfigure your paths");
			return;
		}

		uint16_t RomFSPathSize = Reader.ReadUInt16();

		application::util::FileUtil::gRomFSPath.resize(RomFSPathSize);

		Reader.ReadStruct(application::util::FileUtil::gRomFSPath.data(), RomFSPathSize);

		if (Version >= 0x03)
			application::manager::ProjectMgr::gProjectsEnabled = Reader.ReadUInt8() != 0;

		if (Version >= 0x04)
			application::manager::UIMgr::gThemeIndex = Reader.ReadUInt8();

		if (Version >= 0x05)
			application::manager::UIMgr::gBackgroundThemeIndex = Reader.ReadUInt8();

		if (Version >= 0x06)
		{
			application::manager::UIMgr::gRecentTools.clear();
			uint8_t RecentToolCount = Reader.ReadUInt8();
			for (uint8_t i = 0; i < RecentToolCount; i++)
			{
				uint8_t RawType = Reader.ReadUInt8();
				auto ParsedType = static_cast<application::rendering::UIWindowBase::WindowType>(RawType);
				if (RawType <= static_cast<uint8_t>(application::rendering::UIWindowBase::WindowType::EDITOR_PLUGINS)
					&& ParsedType != application::rendering::UIWindowBase::WindowType::GENERAL_CONTENT_BROWSER)
					application::manager::UIMgr::gRecentTools.push_back(ParsedType);
			}
		}

		if (Version >= 0x07)
			application::rendering::ainb::UIAINBEditor::gEnableCullingOptimization = Reader.ReadUInt8() != 0;

		if (Version >= 0x08)
			application::rendering::ainb::UIAINBEditor::gShowPerformanceStats = Reader.ReadUInt8() != 0;

		if (Version >= 0x09)
			application::manager::UIMgr::gVSyncEnabled = Reader.ReadUInt8() != 0;

		application::util::FileUtil::ValidatePaths();

		application::util::Logger::Info("PathConfig", "Loaded paths");
	}

	void PathConfigFile::Save(const std::string& Path)
	{
		application::util::BinaryVectorWriter Writer;

		Writer.WriteBytes("EPATHCFG"); //Magic
		Writer.WriteInteger(0x09, sizeof(uint8_t)); //Version

		Writer.WriteInteger(application::util::FileUtil::gRomFSPath.size(), sizeof(uint16_t));

		Writer.WriteBytes(application::util::FileUtil::gRomFSPath.c_str());

		Writer.WriteInteger(application::manager::ProjectMgr::gProjectsEnabled ? 1 : 0, sizeof(uint8_t));
		Writer.WriteInteger(application::manager::UIMgr::gThemeIndex, sizeof(uint8_t));
		Writer.WriteInteger(application::manager::UIMgr::gBackgroundThemeIndex, sizeof(uint8_t));

		Writer.WriteInteger(application::manager::UIMgr::gRecentTools.size(), sizeof(uint8_t));
		for (application::rendering::UIWindowBase::WindowType Type : application::manager::UIMgr::gRecentTools)
			Writer.WriteInteger(static_cast<uint8_t>(Type), sizeof(uint8_t));

		Writer.WriteInteger(application::rendering::ainb::UIAINBEditor::gEnableCullingOptimization ? 1 : 0, sizeof(uint8_t));
		Writer.WriteInteger(application::rendering::ainb::UIAINBEditor::gShowPerformanceStats ? 1 : 0, sizeof(uint8_t));
		Writer.WriteInteger(application::manager::UIMgr::gVSyncEnabled ? 1 : 0, sizeof(uint8_t));

		application::util::FileUtil::WriteFile(Path, Writer.GetData());

		application::util::Logger::Info("PathConfig", "Saved paths");
	}
}