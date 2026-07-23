#pragma once

#include "mp_engine_adapter.h"
#include <vector>

#define PLAYER_PAWN_BONE_COUNT 108

class Dolly {
  public:
    class Marker {
      public:
        inline Marker(int frame, float fov, Classes::FVector position,
                      Classes::FVector rotation)
            : Frame{frame}, FOV{fov}, Position{position}, Rotation{rotation} {}

        int Frame;
        float FOV;
        Classes::FVector Position;
        Classes::FVector Rotation;
    };

    class Recording {
      public:
        class Frame {
          public:
            Classes::FVector Position;
            Classes::FRotator Rotation;
            Classes::FBoneAtom Bones[PLAYER_PAWN_BONE_COUNT];
        };

        int StartFrame = 0;
        Engine::Character Character = Engine::Character::Faith;
        Classes::ASkeletalMeshActorSpawnable *Actor = nullptr;
        std::vector<Frame> Frames;
    };
};

namespace DollyPlugin {
bool Initialize();
void Shutdown();
} // namespace DollyPlugin
