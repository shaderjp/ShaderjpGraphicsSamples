#pragma once
#include "CharacterController.h"
#include "PoseInertializer.h"
#include <chrono>

namespace tutorial
{
class MotionMatchingController
{
  public:
    static constexpr double Step=1.0/120.0;
    motion::Database database;
    DirectX::XMFLOAT3 position{},velocity{},cameraTarget{0,1,0},cameraEye{0,2,4},desiredVelocity{};
    float yaw=0,cameraYaw=0,cameraPitch=.3f,stateTime=0,selectedCost=0,continuationCost=0,footError=0;
    double searchMilliseconds=0;
    uint64_t ticks=0,searches=0,switches=0;
    SkinLocalPose localPose;
    std::array<float,3> weights{1,3,1};
    std::array<float,3> costParts{};
    bool inertialization=true,footIk=true;
    std::array<DirectX::XMFLOAT3,3> desiredTrajectory{},matchedTrajectory{};
    size_t selected=0;
    float worldLimit=14;
    float playbackTime=0;
    virtual bool Grounded() const {return position.y<=.0001f;}
    float Speed() const {return std::hypot(velocity.x,velocity.z);}
    const char *Name() const {return database.clips[selected].name.c_str();}
    size_t Clip() const {return database.clips[selected].modelClip;}
    bool FootLocked(size_t side) const{return locked_.at(side);}
    DirectX::XMFLOAT3 FootAnchor(size_t side) const{return anchors_.at(side);}
    void Initialize(const GltfSkinnedModel &model,const std::filesystem::path &directory)
    {
        model_=&model;database.LoadMetadata(model,directory);database.Load(directory/"Locomotion.mmdb");
        for(size_t s=0;s<2;++s){const std::string side=s?"R":"L";upper_[s]=motion::Node(model,"UpperLeg."+side);lower_[s]=motion::Node(model,"LowerLeg."+side);}
        Reset();
    }
    virtual void Reset()
    {
        position=velocity=desiredVelocity={};yaw=cameraYaw=stateTime=playbackTime=0;cameraPitch=.3f;cameraTarget={0,1,0};
        ticks=searches=switches=0;selected=database.Find("MM_Idle");selectedCost=continuationCost=footError=0;searchMilliseconds=0;costParts={};desiredTrajectory={};matchedTrajectory={};
        accumulator_=0;locked_={};localPose=database.Pose(*model_,selected,0);previous_=displayed_=localPose;inertia_.Reset(localPose.nodes.size());UpdateCamera();
    }
    DirectX::XMMATRIX World() const {return DirectX::XMMatrixRotationY(yaw)*DirectX::XMMatrixTranslation(position.x,position.y,position.z);}
    void Advance(double seconds,const CharacterCommand &command,bool active=true)
    {
        if(!active){accumulator_=0;return;}if(command.reset){Reset();return;}
        cameraYaw=motion::Angle(cameraYaw+command.orbitDelta.x);cameraPitch=std::clamp(cameraPitch+command.orbitDelta.y,.12f,1.25f);
        accumulator_+=std::clamp(seconds,0.,.05);
        while(accumulator_+1e-9>=Step){Tick(command);accumulator_-=Step;}
        UpdateCamera();
    }
    virtual void Tick(const CharacterCommand &command)
    {
        using namespace motion;const float dt=float(Step);++ticks;
        cameraYaw=Angle(cameraYaw+command.lookVelocity.x*dt);cameraPitch=std::clamp(cameraPitch+command.lookVelocity.y*dt,.12f,1.25f);
        const float length=std::hypot(command.move.x,command.move.y),speed=command.run?5.45f:1.f;
        const float scale=speed/std::max(1.f,length);
        const XMFLOAT3 desired{(-command.move.x*std::cos(cameraYaw)-command.move.y*std::sin(cameraYaw))*scale,0,(command.move.x*std::sin(cameraYaw)-command.move.y*std::cos(cameraYaw))*scale};
        desiredVelocity=Add(desiredVelocity,Mul(Sub(desired,desiredVelocity),1-std::exp(-8*dt)));
        const float facing=length>.001f?std::atan2(-desired.x,-desired.z):yaw;
        auto query=database.Extract(*model_,selected,playbackTime);
        const auto globals=MakeGltfGlobalPose(displayed_),old=MakeGltfGlobalPose(previous_);
        size_t k=0;
        auto put=[&](XMFLOAT3 v){query[k++]=v.x;query[k++]=v.y;query[k++]=v.z;};
        for(auto n:database.feet)put(Point(globals,n));
        for(auto n:database.feet)put(Mul(Sub(Point(globals,n),Point(old,n)),1/dt));
        put(Mul(Sub(Point(globals,database.hips[0]),Point(old,database.hips[0])),1/dt));
        for(size_t i=0;i<3;++i)
        {
            const float h=.2f*float(i+1);const auto displacement=Add(Mul(desired,h),Mul(Sub(desiredVelocity,desired),(1-std::exp(-8*h))/8));
            desiredTrajectory[i]=Add(position,displacement);const auto local=Rotate(displacement,-yaw);
            query[k++]=local.x;query[k++]=local.z;const float angle=std::clamp(Angle(facing-yaw),-4*h,4*h);
            query[k++]=-std::sin(angle);query[k++]=-std::cos(angle);
        }
        put(Rotate(desiredVelocity,-yaw));
        const auto normalized=database.Normalize(query);
        const auto &clip=database.clips[selected];
        bool changed=false;
        if(ticks%12==1 || (!clip.loop && playbackTime+dt>=clip.duration))
        {
            const auto start=std::chrono::steady_clock::now();const auto best=database.Search(normalized,weights);const auto &frame=database.frames[best];
            selectedCost=database.Cost(normalized,frame.values,weights);
            costParts={};for(size_t j=0;j<Features;++j){const float d=normalized[j]-frame.values[j];costParts[j<15?0:j<27?1:2]+=d*d*Database::Weight(j,weights);}
            continuationCost=database.Cost(normalized,database.Normalize(database.Extract(*model_,selected,playbackTime)),weights);
            if(selectedCost<continuationCost*.9f || (!clip.loop && playbackTime+dt>=clip.duration))
                if(frame.clip!=selected || std::abs(frame.time-playbackTime)>.2f){selected=frame.clip;playbackTime=frame.time;changed=true;++switches;}
            searchMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();++searches;
        }
        if(changed)inertia_.Transition(displayed_,previous_,database.Pose(*model_,selected,playbackTime),database.Pose(*model_,selected,playbackTime+dt),dt);
        const auto before=database.clips[selected].At(playbackTime);playbackTime+=dt;const auto after=database.clips[selected].At(playbackTime);
        const auto oldPosition=position;
        position=Add(position,Rotate(Sub(after.position,before.position),yaw-before.yaw));
        yaw=Angle(yaw+Angle(after.yaw-before.yaw));
        // Small bounded steering keeps control when free clips lack turning coverage.
        if(Length(desiredVelocity)>.05f)yaw=Angle(yaw+std::clamp(Angle(facing-yaw),-1.5f*dt,1.5f*dt));
        position.x=std::clamp(position.x,-worldLimit,worldLimit);position.z=std::clamp(position.z,-worldLimit,worldLimit);
        ResolveMovement(oldPosition,command);
        velocity=Mul(Sub(position,oldPosition),1/dt);
        previous_=displayed_;displayed_=inertia_.Apply(database.Pose(*model_,selected,playbackTime),dt,inertialization);localPose=displayed_;
        auto contacts=database.clips[selected].Contact(playbackTime);if(!Grounded())contacts={false,false};footError=0;
        for(size_t s=0;s<2;++s)
        {
            if(!contacts[s] || !footIk){locked_[s]=false;continue;}
            if(!locked_[s]){anchors_[s]=Add(position,Rotate(Point(MakeGltfGlobalPose(localPose),database.feet[s]),yaw));anchors_[s].y=position.y+.105f;locked_[s]=true;}
        }
        // Project pelvis placement into the reachable region of both planted legs.
        // Y-only lowering cannot preserve contacts during a sharp direction change.
        XMFLOAT3 pelvisShift{};
        for(int iteration=0;iteration<5 && footIk;++iteration)for(size_t s=0;s<2;++s)if(contacts[s])
        {
            const auto g=MakeGltfGlobalPose(localPose);const auto a=Point(g,upper_[s]),b=Point(g,lower_[s]),c=Point(g,database.feet[s]);
            const auto target=Rotate(Sub(anchors_[s],position),-yaw),delta=Sub(target,a);const float distance=Length(delta);
            auto correction=Mul(Unit(delta),std::max(0.f,distance-(Length(Sub(b,a))+Length(Sub(c,b)))*.99f));
            const auto proposed=Add(pelvisShift,correction);
            if(Length(proposed)>.3f)correction=Sub(Mul(Unit(proposed),.3f),pelvisShift);
            pelvisShift=Add(pelvisShift,correction);
            const auto parent=localPose.nodes[database.hips[0]].parent;
            if(parent>=0){XMFLOAT3 local;XMStoreFloat3(&local,XMVector3TransformNormal(XMLoadFloat3(&correction),XMMatrixInverse(nullptr,XMLoadFloat4x4(&g.globals[size_t(parent)]))));correction=local;}
            localPose.nodes[database.hips[0]].translation=Add(localPose.nodes[database.hips[0]].translation,correction);
        }
        for(size_t s=0;s<2;++s)
        {
            const auto local=Point(MakeGltfGlobalPose(localPose),database.feet[s]);const auto world=Add(position,Rotate(local,yaw));
            if(!contacts[s] || !footIk){locked_[s]=false;continue;}
            if(!locked_[s]){anchors_[s]=world;anchors_[s].y=position.y+.105f;locked_[s]=true;}
            const auto target=Rotate(Sub(anchors_[s],position),-yaw);
            footError=std::max(footError,SolveLimb(localPose,upper_[s],lower_[s],database.feet[s],target));
        }
        const auto currentRoot=database.clips[selected].At(playbackTime);
        for(size_t i=0;i<3;++i)matchedTrajectory[i]=Add(position,Rotate(Sub(database.clips[selected].At(playbackTime+.2f*float(i+1)).position,currentRoot.position),yaw-currentRoot.yaw));
        stateTime=playbackTime;
        cameraTarget=Add(cameraTarget,Mul(Sub(Add(position,{0,1,0}),cameraTarget),1-std::exp(-10*dt)));
    }
  protected:
    virtual void ResolveMovement(DirectX::XMFLOAT3,const CharacterCommand &) {}
    const GltfSkinnedModel *model_=nullptr;
    SkinLocalPose displayed_,previous_;
    motion::Inertializer inertia_;
    double accumulator_=0;
    std::array<bool,2> locked_{};
    std::array<DirectX::XMFLOAT3,2> anchors_{};
    std::array<size_t,2> upper_{},lower_{};
    virtual void UpdateCamera()
    {
        cameraEye={cameraTarget.x+4*std::sin(cameraYaw)*std::cos(cameraPitch),cameraTarget.y+4*std::sin(cameraPitch),cameraTarget.z+4*std::cos(cameraYaw)*std::cos(cameraPitch)};
    }
};
} // namespace tutorial
