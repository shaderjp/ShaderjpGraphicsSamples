#include "../Samples/Shared/MotionMatchingController.h"
#include "../Samples/Shared/TraversalController.h"
#include <iostream>
#include <set>
#include <crtdbg.h>
#include <cstdlib>

static void Require(bool value,const char *message){if(!value)throw std::runtime_error(message);}
int wmain(int argc,wchar_t **argv)
{
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);
    try
    {
        if(argc!=2)throw std::runtime_error("Expected repository root");
        const auto dir=std::filesystem::path(argv[1])/"Assets/Models/SilverRobotMotionSet";
        const auto model=tutorial::LoadGltfSkinned(dir/"SilverRobot.gltf");
        tutorial::MotionMatchingController c;c.Initialize(model,dir);
        Require(c.database.frames.size()>200,"Not enough searchable frames");
        Require(c.database.clips.size()==13,"Unexpected motion inventory");
        for(const auto &clip:c.database.clips)if(clip.locomotion)
            for(size_t f=1;f<clip.roots.size();++f)
            {
                const auto delta=tutorial::motion::Sub(clip.roots[f].position,clip.roots[f-1].position);
                if(tutorial::motion::Length(delta)<.0001f)continue;
                const auto forward=tutorial::motion::Rotate({0,0,-1},clip.roots[f].yaw);
                Require(tutorial::motion::Dot(tutorial::motion::Unit(delta),forward)>.95f,"Root yaw disagrees with movement after coordinate conversion");
            }
        for(const auto &frame:c.database.frames)
        {
            const size_t found=c.database.Search(frame.values,c.weights);
            Require(c.database.Cost(frame.values,c.database.frames[found].values,c.weights)<1e-6f,"Search did not return an exact feature match");
        }
        std::set<size_t> chosen;float maxFoot=0;
        for(int t=0;t<720;++t)
        {
            tutorial::CharacterCommand input;
            if(t>=60 && t<500)input.move={0,1};input.run=t>=240 && t<360;
            if(t>=360 && t<500)input.move={1,0};
            c.Advance(1./120,input);chosen.insert(c.selected);maxFoot=std::max(maxFoot,c.footError);
            Require(std::isfinite(c.position.x+c.position.z+c.yaw+c.footError),"Non-finite controller output");
            const auto globals=tutorial::MakeGltfGlobalPose(c.localPose);
            for(const auto &m:globals.globals)Require(std::isfinite(m._11+m._22+m._33+m._41+m._42+m._43),"Non-finite pose");
        }
        Require(c.searches>=60 && c.switches>2 && chosen.size()>2,"Motion matching did not respond to inputs");
        c.Reset();
        for(int frame=1;frame<=300;++frame)
        {
            tutorial::CharacterCommand input;input.reset=frame==1 || frame==270;
            if(frame>=30 && frame<225)input.move.y=1;input.run=frame>=90 && frame<130;
            if(frame>=130 && frame<160)input.lookVelocity.x=-2;
            if(frame>=130 && frame<200)input.move={1,0};if(frame>=200 && frame<240)input.move={0,-1};
            c.Advance(1./60,input);maxFoot=std::max(maxFoot,c.footError);
            Require(c.footError<.05f,"Sharp-turn foot lock exceeds five centimetres");
        }
        std::array<DirectX::XMFLOAT3,3> positions;std::array<float,3> yaws;
        for(size_t i=0;i<3;++i)
        {
            const int fps=std::array{30,60,144}[i];c.Reset();tutorial::CharacterCommand input;input.move={0,1};
            for(int frame=0;frame<fps*3;++frame)c.Advance(1./fps,input);
            positions[i]=c.position;yaws[i]=c.yaw;
        }
        for(size_t i=1;i<3;++i)Require(tutorial::motion::Length(tutorial::motion::Sub(positions[0],positions[i]))<1e-4f && std::abs(yaws[0]-yaws[i])<1e-4f,"Controller depends on display frame rate");
        c.Advance(.5,{},false);Require(c.ticks==360,"Inactive controller advanced");
        c.Reset();Require(c.position.x==0 && c.position.z==0 && c.ticks==0,"Reset failed");
        auto stale=c.database;++stale.fingerprint;bool rejected=false;
        try{stale.Load(dir/"Locomotion.mmdb");}catch(const std::exception&){rejected=true;}
        Require(rejected,"Stale database was accepted");
        // Limb solve must reach a nearby target without changing segment lengths.
        auto pose=c.localPose;auto global=tutorial::MakeGltfGlobalPose(pose);
        const size_t a=tutorial::motion::Node(model,"UpperLeg.L"),b=tutorial::motion::Node(model,"LowerLeg.L"),end=c.database.feet[0];
        auto target=tutorial::motion::Point(global,end);target.z-=.03f;target.y+=.02f;
        Require(tutorial::motion::SolveLimb(pose,a,b,end,target)<.001f,"Two bone IK did not reach target");
        Require(maxFoot<.05f,"Foot lock exceeds five centimetres");
        tutorial::motion::World world;
        world.boxes.push_back({"wall",{0,1,-5},{2,1,.5f},.3f});
        const auto hit=world.Sweep({0,0,0},{0,0,-100});
        Require(hit.hit && hit.time<.05f,"Continuous capsule sweep tunneled through a rotated wall");
        const auto stopped=world.Move({0,0,0},{1,0,-10});
        Require(world.Penetration(stopped)<.001f,"Capsule slide penetrated a wall");
        auto sphereWorld=world;sphereWorld.radius=.12f;
        for(int i=0;i<1000;++i){DirectX::XMFLOAT3 normal;const float distance=sphereWorld.Separation({.17f,float(i)*.0037f,-4.2f},world.boxes[0],.24f,0,normal);Require(std::isfinite(distance),"Sphere camera separation is non-finite");}
        tutorial::TraversalController jumping;jumping.Initialize(model,dir,30);bool airborne=false,landed=false;
        float worstPenetration=0,highest=0;
        for(int frame=1;frame<=600;++frame)
        {
            tutorial::CharacterCommand input;input.move={0,1};input.reset=frame==1;input.jump=frame==140;
            jumping.Advance(1./60,input);
            if(!jumping.Grounded())airborne=true;if(airborne && jumping.Grounded())landed=true;
            highest=std::max(highest,jumping.position.y);worstPenetration=std::max(worstPenetration,jumping.penetration);
            const auto jumpPose=tutorial::MakeGltfGlobalPose(jumping.localPose);
            for(const auto &matrix:jumpPose.globals)Require(std::isfinite(matrix._11+matrix._22+matrix._33+matrix._41+matrix._42+matrix._43),"Jump pose is non-finite");
        }
        Require(airborne && landed && highest>.8f,"Normal jump did not take off and land");
        Require(jumping.position.z> -5.1f && jumping.position.z< -4.5f,"Character crossed or failed to approach the blocking wall");
        Require(worstPenetration<.01f,"Capsule penetration exceeds tolerance");
        // A 20 cm step is traversable without a jump. A 65 cm wall is not.
        tutorial::TraversalController stepping;stepping.Initialize(model,dir,30);float stepHeight=0;
        for(int tick=0;tick<1200;++tick){tutorial::CharacterCommand input;input.move={0,1};stepping.Advance(1./120,input);stepHeight=std::max(stepHeight,stepping.position.y);}
        std::cout<<"Step height "<<stepHeight<<" m, stopped Z "<<stepping.position.z<<" m\n";
        Require(stepHeight>.19f && stepHeight<.22f && stepping.position.z< -4.5f && stepping.position.z> -5.1f,"Step-up or wall blocking failed");
        jumping.position={0,-5,1};jumping.Advance(1./120,{});
        Require(jumping.respawns==1 && jumping.Grounded() && jumping.position.y>=0,"Fall recovery failed");
        jumping.Reset();Require(jumping.respawns==0 && jumping.position.z==jumping.level.spawn.z,"Traversal Reset failed");
        std::cout<<"PASS: database oracle, input response, 30/60/144 Hz, pause, Reset, stale fingerprint, IK; selected "<<chosen.size()<<" clips, max foot residual "<<maxFoot<<" m\n";
        std::cout<<"PASS: swept capsule, rotated wall, step-up, ordinary jump/landing and fall recovery; max penetration "<<worstPenetration<<" m\n";
        const nlohmann::json report={{"result","PASS"},{"clips",c.database.clips.size()},{"searchableFrames",c.database.frames.size()},{"selectedClips",chosen.size()},{"maxFootResidualMetres",maxFoot},{"maxCapsulePenetrationMetres",worstPenetration},{"stepHeightMetres",stepHeight},{"jumpHeightMetres",highest},{"frameRates",{30,60,144}},{"staleDatabaseRejected",true},{"rootHeadingVerified",true},{"rotatedWallBlocked",true},{"normalJumpLanded",true},{"fallRecovery",true}};
        std::ofstream(std::filesystem::path(argv[1])/"Docs/MotionMatchingCpu.json")<<report.dump(2)<<'\n';
        return 0;
    }
    catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
