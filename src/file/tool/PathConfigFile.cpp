#include "PathConfigFile.h"

#include <util/FileUtil.h>
#include <util/BinaryVectorReader.h>
#include <util/BinaryVectorWriter.h>
#include <util/Logger.h>
#include <manager/ProjectMgr.h>

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
		if (Version != 0x02 && Version != 0x03)
		{
			application::util::Logger::Error("PathConfig", "Version invalid, expected 0x02 or 0x03. Please delete the path config file and reconfigure your paths");
			return;
		}

		uint16_t RomFSPathSize = Reader.ReadUInt16();

		application::util::FileUtil::gRomFSPath.resize(RomFSPathSize);

		Reader.ReadStruct(application::util::FileUtil::gRomFSPath.data(), RomFSPathSize);

		if (Version >= 0x03)
			application::manager::ProjectMgr::gProjectsEnabled = Reader.ReadUInt8() != 0;

		application::util::FileUtil::ValidatePaths();

		application::util::Logger::Info("PathConfig", "Loaded paths");
	}

	void PathConfigFile::Save(const std::string& Path)
	{
		application::util::BinaryVectorWriter Writer;

		Writer.WriteBytes("EPATHCFG"); //Magic
		Writer.WriteInteger(0x03, sizeof(uint8_t)); //Version

		Writer.WriteInteger(application::util::FileUtil::gRomFSPath.size(), sizeof(uint16_t));

		Writer.WriteBytes(application::util::FileUtil::gRomFSPath.c_str());

		Writer.WriteInteger(application::manager::ProjectMgr::gProjectsEnabled ? 1 : 0, sizeof(uint8_t));

		application::util::FileUtil::WriteFile(Path, Writer.GetData());

		application::util::Logger::Info("PathConfig", "Saved paths");
	}
}