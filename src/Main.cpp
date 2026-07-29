#include <Editor.h>
#include <manager/UIMgr.h>
#include <manager/AINBNodeMgr.h>
#include <util/backward.h>
#include <util/Logger.h>

#include <fstream>
#include <csignal>

#ifdef _WIN32
#include <io.h>
#include <fcntl.h>
#define dup _dup
#define dup2 _dup2
#define fileno _fileno
#else
#include <unistd.h>
#include <fcntl.h>
#endif

#include <stdio.h>

/*

Dear Developer,
There are only two hard things in Computer Science: cache invalidation, naming things, and understanding how this codebase works. (Yes, I know that's three things – the math is about as solid as the architecture here.)

The good news is that: the code works. The bad news is that I don't remember why. If you decide to refactor (and please, for the love of all that is holy, do), proceed with caution. Change one thing at a time, test everything twice (especially physics stuff [fun-fact: collision, navigation mesh and static compound all use different physics interfaces :D]), and keep a backup of the backup of the backup.
Remember: you're not just debugging code, you're practicing digital archaeology. Document your findings, leave breadcrumbs for the next developer, and may the force be with you.
Godspeed,
MrMystery - 2023

*/

#include <file/game/ainb/AINBFile.h>
#include <iostream>
#include <filesystem>
#include <string>

int main(int argc, char* argv[])
{
    backward::SignalHandling sh;

    if (argc > 1 && std::string(argv[1]) == "--test-file")
    {
        if (argc > 2)
        {
            std::string path = argv[2];
            std::string outPath = path.substr(0, path.find_last_of('.')) + "_test.ainb";
            try
            {
                application::file::game::ainb::AINBFile file(path);
                file.Write(outPath);
            }
            catch (const std::exception& e)
            {
                std::cerr << "Error processing " << path << ": " << e.what() << std::endl;
            }
        }
        return 0;
    }

    if (argc > 1 && (std::string(argv[1]) == "--test" || std::string(argv[1]) == "-t" || std::string(argv[1]) == "--ainb-roundtrip"))
    {
        std::string testFolder = argc > 2 ? argv[2] : "test/HeavyLiftingTest";
        std::cout << "[Unit Test Mode] Running AINB round-trip tests on: " << testFolder << std::endl;
        for (const auto& entry : std::filesystem::directory_iterator(testFolder))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".ainb")
            {
                std::string filename = entry.path().filename().string();
                if (filename.find("_test.ainb") != std::string::npos)
                    continue;
                std::string outPath = entry.path().parent_path().string() + "/" + entry.path().stem().string() + "_test.ainb";
                std::cout << "Processing: " << filename << " -> " << outPath << std::endl;
                try
                {
                    application::file::game::ainb::AINBFile file(entry.path().string());
                    file.Write(outPath);
                    std::cout << "Successfully saved: " << outPath << std::endl;
                }
                catch (const std::exception& e)
                {
                    std::cerr << "Error processing " << filename << ": " << e.what() << std::endl;
                }
            }
        }
        return 0;
    }

    std::ofstream crash_file("crash_log.txt");
    std::streambuf* old_stderr = std::cerr.rdbuf(crash_file.rdbuf());

    //application::file::game::ZStdBackend::InitializeDicts(application::util::FileUtil::GetRomFSFilePath("Pack/ZsDic.pack.zs"));

    //application::manager::AINBNodeMgr::GenerateNodeDefinitionFileWonder("C:/Users/XXX/AppData/Roaming/yuzu/dump/010015100B514000/romfs");

    if (!application::Editor::Initialize())
    {
        return 1;
    }

    application::Editor::InitializeRomFSPathDependant();

    //application::manager::AINBNodeMgr::GenerateNodeDefinitionFile();

	application::util::Logger::Info("Main", "Entering main application loop");

    while (!application::manager::UIMgr::ShouldWindowClose())
    {
        application::manager::UIMgr::Render();
    }


    application::Editor::Shutdown();

    std::cerr.rdbuf(old_stderr);

    return 0;
}