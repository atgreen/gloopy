#include "GrpcServer.h"
#include "MainComponent.h"

#include <grpcpp/grpcpp.h>
#include "gloopy.grpc.pb.h"

#include <thread>
#include <vector>
#include <string>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::Status;
namespace pb = gloopy::v1;

namespace
{
    juce::String js (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

    class ServiceImpl final : public pb::Gloopy::Service
    {
    public:
        explicit ServiceImpl (MainComponent& mc) : main (mc) {}

        // ---- transport ----
        Status Play (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiPlay(); r->set_ok (true); return Status::OK; }

        Status Stop (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiStop(); r->set_ok (true); return Status::OK; }

        Status SetTempo (ServerContext*, const pb::Tempo* q, pb::Ack* r) override
        { main.apiSetTempo (q->bpm()); r->set_ok (true); return Status::OK; }

        Status Seek (ServerContext*, const pb::Position* q, pb::Ack* r) override
        { main.apiSeek (q->beats()); r->set_ok (true); return Status::OK; }

        Status GetTransport (ServerContext*, const pb::Empty*, pb::TransportState* r) override
        {
            auto s = main.apiGetTransport();
            r->set_playing (s.playing);
            r->set_bpm (s.bpm);
            r->set_position_beats (s.positionBeats);
            return Status::OK;
        }

        // ---- tracks ----
        Status AddSynthTrack (ServerContext*, const pb::AddSynthTrackRequest* q, pb::TrackId* r) override
        {
            const int id = main.apiAddSynthTrack (js (q->name()), (int) q->wave(),
                                                  q->attack(), q->decay(), q->sustain(), q->release(), q->gain());
            r->set_id (id);
            return Status::OK;
        }

        Status SetTrackParams (ServerContext*, const pb::TrackParams* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetTrackParams (q->id(),
                                q->has_volume(), q->volume(), q->has_pan(), q->pan(),
                                q->has_mute(),   q->mute(),   q->has_solo(), q->solo(),
                                q->has_name(),   js (q->name()));
            r->set_ok (ok);
            if (! ok) r->set_error ("track not found");
            return Status::OK;
        }

        Status ListTracks (ServerContext*, const pb::Empty*, pb::TrackList* r) override
        {
            for (auto& t : main.apiListTracks())
            {
                auto* ti = r->add_tracks();
                ti->set_id (t.id);
                ti->set_name (t.name.toStdString());
                ti->set_type (t.type.toStdString());
                ti->set_volume (t.volume);
                ti->set_pan (t.pan);
                ti->set_mute (t.mute);
                ti->set_clips (t.clips);
            }
            return Status::OK;
        }

        // ---- clips ----
        Status AddClip (ServerContext*, const pb::AddClipRequest* q, pb::ClipId* r) override
        {
            std::vector<Note> notes;
            notes.reserve ((size_t) q->notes_size());
            for (int i = 0; i < q->notes_size(); ++i)
            {
                const auto& n = q->notes (i);
                notes.push_back ({ n.pitch(), n.start_beat(), n.length_beats(), n.velocity() });
            }
            const int idx = main.apiAddClip (q->track_id(), q->start_beat(), q->length_beats(),
                                             q->content_len_beats(), q->looped(), notes, js (q->name()));
            r->set_track_id (q->track_id());
            r->set_index (idx);
            return Status::OK;
        }

    private:
        MainComponent& main;
    };
}

struct GrpcServer::Impl
{
    explicit Impl (MainComponent& mc) : service (mc) {}
    ServiceImpl service;
    std::unique_ptr<Server> server;
    std::thread thread;
};

GrpcServer::GrpcServer (MainComponent& owner) : impl (std::make_unique<Impl> (owner)) {}
GrpcServer::~GrpcServer() { stop(); }

bool GrpcServer::start (int port)
{
    ServerBuilder builder;
    const std::string addr = "127.0.0.1:" + std::to_string (port);
    builder.AddListeningPort (addr, grpc::InsecureServerCredentials());
    builder.RegisterService (&impl->service);
    impl->server = builder.BuildAndStart();
    if (impl->server == nullptr)
        return false;
    impl->thread = std::thread ([this] { impl->server->Wait(); });
    return true;
}

void GrpcServer::stop()
{
    if (impl != nullptr && impl->server != nullptr)
    {
        impl->server->Shutdown();
        if (impl->thread.joinable()) impl->thread.join();
        impl->server.reset();
    }
}
