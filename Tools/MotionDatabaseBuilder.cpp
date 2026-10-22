#include "../Samples/Shared/MotionDatabase.h"
#include <iostream>
int wmain(int argc,wchar_t **argv)
{
    try
    {
        if(argc!=3)throw std::runtime_error("Usage: MotionDatabaseBuilder <motion directory> <output.mmdb>");
        const std::filesystem::path directory=argv[1];
        auto model=tutorial::LoadGltfSkinned(directory/"SilverRobot.gltf");
        tutorial::motion::Database database;database.LoadMetadata(model,directory);database.Build(model);database.Save(argv[2]);
        std::cout<<database.clips.size()<<" clips, "<<database.frames.size()<<" searchable frames, 30 features\n";
        return 0;
    }
    catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
