#pragma once
#include "MotionMatchingController.h"
#include "TraversalWorld.h"

namespace tutorial
{
class TraversalController : public MotionMatchingController
{
  public:
    motion::World level;
    int stage=29;
    bool grounded=true;
    std::string reason="Ready";
    uint64_t respawns=0;
    float penetration=0;
    float airTime=0,landingTime=1;
    void Initialize(const GltfSkinnedModel &model,const std::filesystem::path &directory,int sampleStage)
    {
        if(sampleStage!=29 && sampleStage!=30)throw std::runtime_error("Unsupported character sample stage");
        stage=sampleStage;MotionMatchingController::Initialize(model,directory);
        if(stage>=30){level.Load(directory.parent_path()/"TraversalCourse/Traversal.json");worldLimit=30;}
        Reset();
    }
    bool Grounded() const override {return stage==29?MotionMatchingController::Grounded():grounded;}
    void Reset() override
    {
        MotionMatchingController::Reset();if(stage>=30 && !level.boxes.empty()){position=level.spawn;cameraTarget=motion::Add(position,{0,1,0});}
        desiredTrajectory.fill(position);matchedTrajectory.fill(position);
        grounded=true;airTime=0;landingTime=1;vertical_=0;respawns=0;reason="Ready";penetration=0;checkpoint_=position;
        UpdateCamera();
    }
    void Tick(const CharacterCommand &command) override
    {
        using namespace motion;const float dt=float(Step);
        if(stage==29){MotionMatchingController::Tick(command);return;}
        const bool wasGrounded=grounded;
        MotionMatchingController::Tick(command);
        if(!grounded)
        {
            airTime=wasGrounded?0:airTime+dt;
            const auto clip=database.Find(airTime<.18f?"MM_JumpStart":"MM_JumpLoop");
            localPose=database.Pose(*model_,clip,airTime<.18f?airTime/.18f*database.clips[clip].duration:airTime);
        }
        else
        {
            if(!wasGrounded)landingTime=0;landingTime+=dt;
            if(landingTime<.16f){const auto clip=database.Find("MM_JumpLand");localPose=BlendGltfLocalPoses(database.Pose(*model_,clip,landingTime/.16f*database.clips[clip].duration),localPose,landingTime/.16f);}
        }
        for(const auto &p:level.checkpoints)if(Length(Sub(position,p))<.65f)checkpoint_=p;
        if(position.y< -4)
        {
            auto saved=checkpoint_;const auto deaths=respawns+1;MotionMatchingController::Reset();position=saved;checkpoint_=saved;respawns=deaths;grounded=true;vertical_=0;reason="Respawn at checkpoint";
            cameraTarget=Add(position,{0,1,0});desiredTrajectory.fill(position);matchedTrajectory.fill(position);airTime=0;landingTime=1;penetration=0;
        }
    }
  protected:
    void ResolveMovement(DirectX::XMFLOAT3 before,const CharacterCommand &command) override
    {
        using namespace motion;if(stage<30)return;
        const float dt=float(Step);const auto horizontal=Sub(position,before);
        if(command.jump && grounded){vertical_=5;grounded=false;}
        auto delta=horizontal;delta.y=vertical_*dt-4.9f*dt*dt;vertical_-=9.8f*dt;
        auto resolved=level.Move(before,delta);
        bool stepped=false;
        if(grounded && Length(Sub(XMFLOAT3{resolved.x,0,resolved.z},XMFLOAT3{position.x,0,position.z}))>.00001f)
        {
            auto raised=Add(before,{0,.25f,0});
            if(level.Clear(raised))
            {
                const auto over=level.Move(raised,{delta.x,0,delta.z});const auto down=level.Sweep(over,{0,-.3f,0});
                // The capsule first touches the rounded corner with a shallow
                // normal. Keep support while climbing a box within step height;
                // requiring a floor normal here drops grounding at the corner.
                if(down.hit && down.normal.y>.01f && level.boxes[down.obstacle].Top()<=before.y+.251f &&
                   level.boxes[down.obstacle].Top()>before.y+.001f &&
                   Length(Sub(over,raised))>std::hypot(resolved.x-before.x,resolved.z-before.z)+.000001f)
                {resolved=Add(over,{0,-.3f*down.time,0});stepped=true;}
            }
        }
        const auto floor=level.Sweep(resolved,{0,-.04f,0});
        grounded=vertical_<=0 && (stepped || (floor.hit && floor.normal.y>.6f));
        if(grounded){if(!stepped)resolved.y-=.04f*floor.time;vertical_=0;}
        if(delta.y>0 && resolved.y<before.y+delta.y-.001f)vertical_=0;
        position=resolved;penetration=level.Penetration(position);
    }
    void UpdateCamera() override
    {
        MotionMatchingController::UpdateCamera();
        if(stage<30)return;
        using namespace motion;motion::World cameraWorld=level;cameraWorld.radius=.12f;
        const auto cameraStart=Sub(cameraTarget,{0,.12f,0}),delta=Sub(cameraEye,cameraTarget);const auto hit=cameraWorld.Sweep(cameraStart,delta,.24f);
        if(hit.hit)cameraEye=Add(cameraTarget,Mul(delta,std::max(.1f,hit.time-.03f)));
    }
  private:
    float vertical_=0;
    DirectX::XMFLOAT3 checkpoint_{};
};
} // namespace tutorial
