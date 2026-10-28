#include "fumbo.hpp"
#include "game/pathfinder_state.hpp"

int main() {
    Fumbo::Instance().Init(1280, 720, "MBG - 3D Raycasting & Pathfinder", 60);
    Fumbo::Assets::AddAssetPack("data.fpk");
    Fumbo::Instance().Run(std::make_shared<PathfinderState>());
    return 0;
}
