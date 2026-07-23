#include "GrpcServer.h"
#include "MainComponent.h"

#include <grpcpp/grpcpp.h>
#include "gloopy.grpc.pb.h"

#include <thread>
#include <chrono>
#include <vector>
#include <string>

using grpc::Server;
using grpc::ServerBuilder;
using grpc::ServerContext;
using grpc::ServerWriter;
using grpc::Status;
namespace pb = gloopy::v1;

namespace
{
    juce::String js (const std::string& s) { return juce::String::fromUTF8 (s.c_str(), (int) s.size()); }

    void fillInsert (pb::MixerInsert* mi, const MainComponent::InsertSnap& s)
    {
        mi->set_index (s.index); mi->set_name (s.name.toStdString());
        mi->set_volume (s.volume); mi->set_pan (s.pan); mi->set_mute (s.mute); mi->set_solo (s.solo);
        for (auto& e : s.effects)
        {
            auto* ei = mi->add_effects();
            ei->set_slot (e.slot); ei->set_name (e.name.toStdString()); ei->set_bypassed (e.bypassed);
        }
    }

    void fillPlugin (pb::PluginInfo* pi, const MainComponent::PluginSnap& s)
    {
        pi->set_name (s.name.toStdString());
        pi->set_format (s.format.toStdString());
        pi->set_is_instrument (s.isInstrument);
        pi->set_identifier (s.identifier.toStdString());
    }

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

        // ---- mixer / effects ----
        Status ListInserts (ServerContext*, const pb::Empty*, pb::InsertList* r) override
        {
            for (auto& ins : main.apiListInserts()) fillInsert (r->add_inserts(), ins);
            return Status::OK;
        }

        Status AddEffect (ServerContext*, const pb::AddEffectRequest* q, pb::EffectRef* r) override
        {
            const int slot = main.apiAddEffect (q->insert(), (int) q->type());
            r->set_insert (q->insert()); r->set_slot (slot);
            return Status::OK;
        }

        Status RemoveEffect (ServerContext*, const pb::EffectRef* q, pb::Ack* r) override
        {
            const bool ok = main.apiRemoveEffect (q->insert(), q->slot());
            r->set_ok (ok); if (! ok) r->set_error ("effect not found");
            return Status::OK;
        }

        Status SetEffectParam (ServerContext*, const pb::EffectParamSet* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetEffectParam (q->insert(), q->slot(), js (q->name()), q->value());
            r->set_ok (ok); if (! ok) r->set_error ("effect/param not found");
            return Status::OK;
        }

        Status SetEffectBypass (ServerContext*, const pb::EffectBypassSet* q, pb::Ack* r) override
        {
            const bool ok = main.apiSetEffectBypass (q->insert(), q->slot(), q->bypassed());
            r->set_ok (ok); if (! ok) r->set_error ("effect not found");
            return Status::OK;
        }

        Status GetEffectParams (ServerContext*, const pb::EffectRef* q, pb::ParamList* r) override
        {
            for (auto& p : main.apiGetEffectParams (q->insert(), q->slot()))
            {
                auto* pp = r->add_params();
                pp->set_name (p.name.toStdString()); pp->set_value (p.value);
                pp->set_min (p.min); pp->set_max (p.max);
            }
            return Status::OK;
        }

        // ---- project / state ----
        Status GetState (ServerContext*, const pb::Empty*, pb::ProjectState* r) override
        {
            auto ts = main.apiGetTransport();
            auto* t = r->mutable_transport();
            t->set_playing (ts.playing); t->set_bpm (ts.bpm); t->set_position_beats (ts.positionBeats);
            for (auto& tr : main.apiListTracks())
            {
                auto* ti = r->add_tracks();
                ti->set_id (tr.id); ti->set_name (tr.name.toStdString()); ti->set_type (tr.type.toStdString());
                ti->set_volume (tr.volume); ti->set_pan (tr.pan); ti->set_mute (tr.mute); ti->set_clips (tr.clips);
            }
            for (auto& ins : main.apiListInserts()) fillInsert (r->add_inserts(), ins);
            return Status::OK;
        }

        Status NewProject (ServerContext*, const pb::Empty*, pb::Ack* r) override
        { main.apiNewProject(); r->set_ok (true); return Status::OK; }

        Status LoadProject (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        {
            const bool ok = main.apiLoadProject (js (q->path()));
            r->set_ok (ok); if (! ok) r->set_error ("file not found");
            return Status::OK;
        }

        Status SaveProject (ServerContext*, const pb::FilePath* q, pb::Ack* r) override
        { r->set_ok (main.apiSaveProject (js (q->path()))); return Status::OK; }

        // ---- track & clip management ----
        Status RemoveTrack (ServerContext*, const pb::TrackId* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveTrack (q->id()); r->set_ok (ok); if (! ok) r->set_error ("track not found"); return Status::OK; }

        Status AddAudioTrack (ServerContext*, const pb::AddAudioTrackRequest* q, pb::TrackId* r) override
        { r->set_id (main.apiAddAudioTrack (js (q->name()))); return Status::OK; }

        Status AddSamplerTrack (ServerContext*, const pb::AddSamplerTrackRequest* q, pb::TrackId* r) override
        { r->set_id (main.apiAddSamplerTrack (js (q->name()), js (q->path()), q->root_note())); return Status::OK; }

        Status AddPluginTrack (ServerContext*, const pb::AddPluginTrackRequest* q, pb::TrackId* r) override
        { r->set_id (main.apiAddPluginTrack (js (q->identifier()))); return Status::OK; }

        Status RemoveClip (ServerContext*, const pb::ClipRef* q, pb::Ack* r) override
        { const bool ok = main.apiRemoveClip (q->track_id(), q->index()); r->set_ok (ok); if (! ok) r->set_error ("clip not found"); return Status::OK; }

        Status MoveClip (ServerContext*, const pb::MoveClipRequest* q, pb::Ack* r) override
        {
            const bool ok = main.apiMoveClip (q->track_id(), q->index(), q->start_beat(),
                                              q->has_to_track_id(), q->to_track_id());
            r->set_ok (ok); if (! ok) r->set_error ("clip not found");
            return Status::OK;
        }

        Status AddAudioClip (ServerContext*, const pb::AddAudioClipRequest* q, pb::ClipId* r) override
        {
            const int idx = main.apiAddAudioClip (q->track_id(), q->start_beat(), js (q->path()), q->gain());
            r->set_track_id (q->track_id()); r->set_index (idx);
            return Status::OK;
        }

        // ---- plugins ----
        Status ScanPlugins (ServerContext*, const pb::ScanPluginsRequest* q, pb::PluginList* r) override
        { for (auto& p : main.apiScanPlugins (q->force())) fillPlugin (r->add_plugins(), p); return Status::OK; }

        Status ListPlugins (ServerContext*, const pb::Empty*, pb::PluginList* r) override
        { for (auto& p : main.apiListPlugins()) fillPlugin (r->add_plugins(), p); return Status::OK; }

        Status AddPluginEffect (ServerContext*, const pb::AddPluginEffectRequest* q, pb::EffectRef* r) override
        {
            const int slot = main.apiAddPluginEffect (q->insert(), js (q->identifier()));
            r->set_insert (q->insert()); r->set_slot (slot);
            return Status::OK;
        }

        Status OpenPluginEditor (ServerContext*, const pb::TrackId* q, pb::Ack* r) override
        { const bool ok = main.apiOpenPluginEditor (q->id()); r->set_ok (ok); if (! ok) r->set_error ("no plugin on track"); return Status::OK; }

        // ---- events (streaming out) ----
        Status Subscribe (ServerContext* ctx, const pb::SubscribeRequest* q, ServerWriter<pb::Event>* writer) override
        {
            const int interval = q->interval_ms() > 0 ? (int) q->interval_ms() : 50;
            while (! ctx->IsCancelled())
            {
                if (q->transport())
                {
                    auto s = main.apiGetTransport();
                    pb::Event e;
                    auto* t = e.mutable_transport();
                    t->set_playing (s.playing); t->set_bpm (s.bpm); t->set_position_beats (s.positionBeats);
                    if (! writer->Write (e)) break;
                }
                if (q->meters())
                {
                    std::vector<float> L, R;
                    if (main.apiSnapshotMeters (L, R))
                    {
                        pb::Event e;
                        auto* m = e.mutable_meters();
                        for (float v : L) m->add_peak_l (v);
                        for (float v : R) m->add_peak_r (v);
                        if (! writer->Write (e)) break;
                    }
                }
                std::this_thread::sleep_for (std::chrono::milliseconds (interval));
            }
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
